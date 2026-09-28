"use strict";
/* Monomachine Editor: the plug-in's host for the approved mockup (P6).
   The mockup (mmMockup.js, copied verbatim from doc/modern-ux/mm-mockup) is the UI. It hands
   everything the machine does to its host, window.MMHost, which this file defines before the
   mockup loads: transport, patterns, the kit library, undo, tempo, mutes, the keyboard and the
   AUDIO / MIDI panel. Nothing of the mockup is replaced; its view is used through window.MMView.
   - The plug-in publishes the core's documents (observed, or pending while an edit is on its
     way) and the machine document; they are shown in the mockup's view through MmConvert (the
     pure page <-> contract translation).
   - An edit is sent as the intent the core takes for the Monomachine: the whole document
     ({"op":"set","kind","doc","g"}) of each kind the gesture said it edited. The page does not
     compare documents: the core takes a document equal to its own as no change. The gesture id g
     makes a drag one undo step; undo and redo are the core's (C++).
   - What the engine cannot do is disabled with the reason, from machine.capabilities. */
(() => {
	const V = () => window.MMView;			// the mockup's view: after it ran
	const S = () => window.MMView.S;
	const $ = q => document.querySelector(q), $$ = q => [...document.querySelectorAll(q)];
	const send = (msg, opt) => Bridge.send(msg, opt);
	const log = t => Bridge.log(t);
	const now = () => performance.now();
	const selfTest = /[?&]selftest=1/.test(location.search);
	const cpuTest = /[?&]selftest=mmcpu/.test(location.search);
	const audioTest = /[?&]selftest=p6audio/.test(location.search);

	/* ---------------- what the core shows ---------------- */
	/* The engine is unknown until the plug-in says (P5): not "missing", which opened the NO ROM
	   dialog before the first report. */
	const FW = { pat: [], kit: [], song: [], glob: [], machine: null, engine: "loading", cat: null, learn: null };
	const CUR = { pat: -1, kit: -1, song: 0, glob: 0 };
	const last = { edit: 0, tempoEdit: -1e9, mute: [], ready: false, note: "", noteMs: 0, error: "" };
	let synced = false;		// the view shows the machine (current pattern and working kit arrived)
	const wantApply = new Set();
	const dirty = new Set();	// the working set's kinds a gesture edited ("kit", "pattern", "song", "global")
	const written = new Set();	// library slots a gesture wrote ("kit:5", "pattern:12")
	let gesture = Bridge.gesture();
	let audioDocument = null;
	/* What a gesture edits when it does not name the document: the mockup's two edit kinds. The
	   kinds are sent in this order (a pattern's locks read their machines from the kit). */
	const EDITS = { struct: ["pattern", "song"], sound: ["kit"] };
	const KINDS = ["kit", "pattern", "song", "global"];

	/* ---------------- the machine's documents -> the view ---------------- */
	const C = () => MmConvert;
	const kitPageOf = k => k === CUR.kit && synced ? V().captureKit() : S().kits[k]?.data || (FW.kit[k] ? C().kitToPage(FW.kit[k], FW.glob[CUR.glob]) : null);
	const lenOf = p => p === S().pat && synced ? S().len : FW.pat[p]?.length ?? S().patInfo[p].len;
	/* what the view's state is as the contract's documents */
	function patDoc(p) {
		const b = FW.pat[p] || FW.pat[CUR.pat] || FW.pat.find(Boolean);
		if (!b) return null;
		const data = p === CUR.pat ? V().capturePat() : S().patData[p];
		return data ? C().patternToFw(data, b, S().patKit[p], kitPageOf(S().patKit[p]), p) : null;
	}
	function kitDoc(k) {
		const b = FW.kit[k] || FW.kit[CUR.kit] || FW.kit.find(Boolean);
		if (!b) return null;
		if (k === CUR.kit) return C().kitToFw(V().captureKit(), b, S().workName, k);
		const s = S().kits[k];
		return C().kitToFw(s.data || V().clearedKit(), b, s.empty ? "" : s.name, k);
	}
	const songDoc = () => FW.song[CUR.song] ? C().songToFw(S().song, FW.song[CUR.song], lenOf) : null;
	const globDoc = () => FW.glob[CUR.glob] ? C().globalToFw(FW.glob[CUR.glob], S().routing, S().midi) : null;

	function setSlotPattern(p) {
		const d = FW.pat[p];
		if (p === CUR.pat) return;
		S().patData[p] = C().patternToPage(d, kitPageOf(d.kit));
		S().patInfo[p] = { has: C().hasTrigs(d), len: d.length };
		S().patKit[p] = d.kit;
	}
	function setSlotKit(k) {
		const d = FW.kit[k];
		if (k === CUR.kit) return;
		S().kits[k] = { name: C().kitName(d), empty: C().kitEmpty(d), data: C().kitToPage(d, FW.glob[CUR.glob]) };
	}
	function applyCurrentKit() {
		const d = FW.kit[CUR.kit];
		const page = C().kitToPage(d, FW.glob[CUR.glob]);
		page.multi = S().multi;
		V().applyKit(page);
		S().workName = C().kitName(d);
		S().kits[CUR.kit] = { name: S().workName, empty: false, data: C().kitToPage(d, FW.glob[CUR.glob]) };
		if (FW.machine) V().setKitState(FW.machine.kit.working === "edited" ? "edited" : "clean");
	}
	function applyCurrentPattern() {
		const d = FW.pat[CUR.pat];
		V().applyPat(C().patternToPage(d, V().captureKit()));
		S().patKit[CUR.pat] = d.kit;
		S().patInfo[CUR.pat] = { has: C().hasTrigs(d), len: d.length };
	}
	function applyCurrentSong() {
		S().song = C().songToPage(FW.song[CUR.song], lenOf);
		S().songSel = Math.min(S().songSel || 0, S().song.length - 1);
	}
	function applyCurrentGlobal() {
		const g = FW.glob[CUR.glob];
		S().routing = g.routingMode;
		S().midi.forEach((x, t) => { x.ch = g.midiSeq.channels[t] + 1; x.cc = [...g.midiSeq.ccs[t]]; });
		mapFromGlobal();
	}

	/* Show what the core holds, unless the user is in a gesture or the view has edits not sent
	   yet (the next reconcile sends them; the core then shows them). */
	function applyPending() {
		if (!wantApply.size) return;
		if (FW.pat[CUR.pat] == null || FW.kit[CUR.kit] == null) return;
		if (synced && (V().busy() || !$("#dlg").hidden || V().LIB.renaming != null || V().LIB.drag != null || syncT)) return;
		let changed = false;
		const kinds = [...wantApply];
		wantApply.clear();
		/* the kit first: the pattern's locks read their machines from it */
		if (kinds.includes("kit")) { applyCurrentKit(); changed = true; }
		if (kinds.includes("kit") && FW.machine) V().setKitState(FW.machine.kit.working === "edited" ? "edited" : "clean");
		if ((kinds.includes("pat") || changed) && FW.pat[CUR.pat]) { applyCurrentPattern(); changed = true; }
		if (kinds.includes("song") && FW.song[CUR.song]) { applyCurrentSong(); changed = true; }
		if (kinds.includes("glob") && FW.glob[CUR.glob]) { applyCurrentGlobal(); changed = true; }
		if (!synced) {
			synced = true;
			S().tracks.forEach((t, i) => last.mute[i] = !V().audible(i));
		}
		if (changed) {
			V().autoRange(S().sel);
			V().render();
			V().drawLib();
		}
		showEngine();
	}

	function onDoc(m) {
		const { kind, slot, doc } = m;
		if (kind === "pattern") { FW.pat[slot] = doc; if (slot === CUR.pat) wantApply.add("pat"); else setSlotPattern(slot); }
		else if (kind === "kit") {
			FW.kit[slot] = doc;
			if (slot === CUR.kit) wantApply.add("kit"); else setSlotKit(slot);
			/* the patterns that play this kit read their locks' machines from it */
			for (let p = 0; p < 128; p++) if (p !== CUR.pat && FW.pat[p] && FW.pat[p].kit === slot) setSlotPattern(p);
		}
		else if (kind === "song") { FW.song[slot] = doc; if (slot === CUR.song) wantApply.add("song"); }
		else if (kind === "global") { FW.glob[slot] = doc; if (slot === CUR.glob) wantApply.add("glob"); }
		libDirty = true;
		/* the current documents are shown right away (the 120 ms timer only catches up after a gesture) */
		if (wantApply.size && !applyT) applyT = setTimeout(() => { applyT = 0; applyPending(); }, 0);
	}
	let applyT = 0;
	let libDirty = false;

	function onMachine(d) {
		FW.machine = d;
		FW.engine = d.engine;
		const cp = d.pattern.current, ck = d.kit.current, cs = d.song.current ?? 0, cg = d.global ?? 0;
		if (ck != null && ck !== CUR.kit) { CUR.kit = ck; S().kit = ck; if (FW.kit[ck]) wantApply.add("kit"); }
		if (cp != null && cp !== CUR.pat) {
			const old = CUR.pat;
			CUR.pat = cp; S().pat = cp;
			if (old >= 0 && FW.pat[old]) setSlotPattern(old);
			if (FW.pat[cp]) wantApply.add("pat");
		}
		if (cs !== CUR.song) { CUR.song = cs; if (FW.song[cs]) wantApply.add("song"); }
		if (cg !== CUR.glob) { CUR.glob = cg; if (FW.glob[cg]) wantApply.add("glob"); }
		const q = d.pattern.queued;
		const queued = q != null && q !== cp ? q : null;
		if (queued !== S().queued) { S().queued = queued; V().renderTop(); V().drawLib(); }
		setPlaying(!!d.playing);
		/* the machine's tempo (RAM), unless the user is setting it */
		if (d.tempo != null && now() - last.tempoEdit > 1500 && d.tempo !== S().bpm) { S().bpm = d.tempo; V().renderTop(); }
		if (synced && !V().busy()) V().setKitState(d.kit.working === "edited" ? "edited" : "clean");
		host.renderPst();
		if (d.error && d.error !== last.error) { last.error = d.error; V().toast(d.error); }
		markEngines(d);
		showEngine();
		V().renderTop();
		/* the library fills in the background: say how far */
		const chip = $(".lcdeng span");
		if (chip && S().eng === "ready") chip.textContent = d.loading.done < d.loading.total ? `EMU OS 1.32B · ${d.loading.done}/${d.loading.total}` : V().ENG.ready[0];
	}

	/* the plug-in's engine changed (HW MIDI or the emulator): its documents start over */
	function onReset() {
		for (const k of ["pat", "kit", "song", "glob"]) FW[k] = [];
		FW.machine = null;
		CUR.pat = CUR.kit = -1; CUR.song = CUR.glob = 0;
		synced = false;
		wantApply.clear();
		dirty.clear();
		written.clear();
		last.ready = false;
		showEngine();
	}

	/* ---------------- the view -> the core ---------------- */
	/* After an edit, the working set's documents the gesture edited (and the slots a library
	   gesture wrote) go out whole, tagged with the gesture. */
	let syncT = 0;
	function scheduleSync() { last.edit = now(); if (!syncT) syncT = setTimeout(() => { syncT = 0; sync(); }, 40); }
	function sendDoc(kind, doc, slot) {
		if (!doc) return false;
		send({ op: "set", kind, doc, g: gesture }, { key: kind + ":" + slot, onResult: r => {
			if (!r.ok) { V().toast(r.errors[0] || "The machine did not take it."); log("set " + kind + " " + slot + ": " + r.errors.join("; ")); }
			else note(r.note);
		} });
		return true;
	}
	function note(t) {
		if (!t || (t === last.note && now() - last.noteMs < 8000)) return;
		last.note = t; last.noteMs = now();
		V().toast(t);
	}
	function sync() {
		if (!synced || !V().engReady()) return;
		const edited = KINDS.filter(k => dirty.has(k));
		dirty.clear();
		/* the working kit goes live (CC, NRPN, machine, routing, name) */
		if (edited.includes("kit")) sendDoc("kit", kitDoc(CUR.kit), CUR.kit);
		if (edited.includes("pattern")) sendDoc("pattern", patDoc(CUR.pat), CUR.pat);
		for (const w of [...written]) {
			const [kind, s] = w.split(":"), slot = +s;
			written.delete(w);
			if (kind === "kit" && slot === CUR.kit) send({ op: "saveKit", k: slot });	// the library wrote the current kit's slot: store it too
			else if (kind === "kit") sendDoc("kit", kitDoc(slot), slot);
			else if (kind === "pattern" && slot !== CUR.pat) sendDoc("pattern", patDoc(slot), slot);
		}
		if (edited.includes("song")) sendDoc("song", songDoc(), CUR.song);
		if (edited.includes("global")) sendDoc("global", globDoc(), CUR.glob);
	}

	/* ---------------- the machine's transport and playhead ---------------- */
	function showStep(step) {
		const s = S(), prev = s.step;
		s.step = step;
		/* the Control workspace's LFO and Random sources move with the machine's steps */
		V().ctlTick();
		if (s.ctl.links.length) { dirty.add("kit"); scheduleSync(); }
		const pp = Math.floor(step / 16);
		$$(".pl").forEach(b => b.classList.toggle("play", +b.dataset.plp === pp && s.playing));
		if (s.follow && s.ws === "seq" && !s.viewAll && pp !== s.page && !V().busy()) { s.page = pp; V().render(); }
		$("#tempoled")?.classList.toggle("on", step % 4 === 0);
		V().setPos();
		queueMicrotask(() => V().movePH());
		$$(`.mst[data-s="${prev}"],.lb[data-s="${prev}"],.tc[data-s="${prev}"]`).forEach(c => c.classList.remove("ph"));
		$$(`.mst[data-s="${step}"],.lb[data-s="${step}"],.tc[data-s="${step}"]`).forEach(c => c.classList.add("ph"));
		if (s.ws === "seq") V().redraw();
		if (s.ws === "perform") V().flashTracks([0, 1, 2, 3, 4, 5].filter(i => { const st = s.tracks[i].steps[step]; return st && !st.off && st.a && V().audible(i); }));
	}
	function setPlaying(on) {
		const s = S();
		if (s.playing === on) return;
		s.playing = on;
		if (!on) {
			$$(".pl").forEach(b => b.classList.remove("play"));
			$("#tempoled")?.classList.remove("on");
			$$(".ph").forEach(c => c.classList.remove("ph"));
			s.step = -1;
		}
		V().setPos();
		V().renderTop();
		V().redraw();
		V().movePH(false);
	}

	/* ---------------- the engine, its capabilities and the firmware's LCD ---------------- */
	let lcdFade = 0;
	/* the firmware's LCD: 128 x 64, one bit a pixel, rows of 16 bytes, base64 (one encoding for both editors) */
	function drawLcd(b64) {
		const el = $("#bootscr"), bits = Uint8Array.from(atob(b64 || ""), c => c.charCodeAt(0));
		if (!el || bits.length < 1024) return;
		let c = el.querySelector("canvas.fwlcd");
		if (!c) { c = document.createElement("canvas"); c.className = "fwlcd"; c.width = 128; c.height = 64; el.appendChild(c); }
		const g = c.getContext("2d"), cs = getComputedStyle(el);
		g.fillStyle = cs.getPropertyValue("--lcd").trim() || "#b7c79a";
		g.fillRect(0, 0, 128, 64);
		g.fillStyle = cs.getPropertyValue("--ink").trim() || "#1d2a1a";
		for (let y = 0; y < 64; y++)
			for (let xb = 0; xb < 16; xb++) {
				const b = bits[y * 16 + xb];
				if (b) for (let k = 0; k < 8; k++) if (b & (0x80 >> k)) g.fillRect(xb * 8 + k, y, 1, 1);
			}
	}
	let noRomShown = false, noRomAt = 0;
	function showEngine() {
		if (!window.MMView) return;
		const e = FW.engine, hw = FW.machine?.capabilities?.engine === "hw", link = FW.machine?.link;
		const st = hw ? (link === "ready" ? (synced ? "hwready" : "sync") : link === "lost" ? "hwnone" : "hwwait")
			: e === "missing" ? "norom" : e === "unsupported" ? "unsupported" : e === "loading" ? "loading" : e === "booting" ? "boot" : synced ? "ready" : "sync";
		if (S().eng !== st) V().setEng(st);
		/* NO ROM only when the plug-in has said so for 1.5 s (a device being made or replaced is not
		   a missing ROM), and the dialog closes by itself as soon as the engine is anything else. */
		if (st === "norom") { if (!noRomAt) noRomAt = now(); if (!noRomShown && now() - noRomAt > 1500) { noRomShown = true; host.firstRun(); } else if (!noRomShown) setTimeout(showEngine, 1600); }
		else { noRomAt = 0; noRomShown = false; const d = $("#dlg"); if (d && !d.hidden && /FIRMWARE NEEDED/.test(d.textContent)) d.hidden = true; }
		if ((st === "ready" || st === "hwready") && !last.ready) {
			last.ready = true;
			log("ready: pattern " + CUR.pat + " kit " + CUR.kit);
			if (selfTest) setTimeout(runSelfTest, 500);
			if (cpuTest) runCpuPhases();
			if (audioTest) setTimeout(() => V().audioSelfTest?.({ log: t => log("AUDIO: " + t), play: on => { if (on !== S().playing) host.togglePlay(); }, step: () => S().step, playing: () => S().playing }), 3000);
		}
	}
	/* the engine menu's entries: the plug-in's engine map (machine.engines) */
	function markEngines(d) {
		for (const e of d.engines || []) {
			const o = $(`#engsel option[value="${e.id}"]`);
			if (!o) continue;
			o.disabled = !e.available;
			o.title = e.available ? "" : e.reason || "";
		}
		const sel = $("#engsel");
		if (sel && d.capabilities?.engine) sel.value = d.capabilities.engine;
	}
	/* what the engine cannot do: disabled, with the reason (machine.capabilities) */
	const NA = [
		['[data-mute="6"],[data-mute="7"],[data-mute="8"],[data-mute="9"],[data-mute="10"],[data-mute="11"],[data-solo="6"],[data-solo="7"],[data-solo="8"],[data-solo="9"],[data-solo="10"],[data-solo="11"],[data-gmute="6"],[data-gmute="7"],[data-gmute="8"],[data-gmute="9"],[data-gmute="10"],[data-gmute="11"]', "midiMutes"],
		['[data-pmode="poly"]', "poly"],
		['[data-set="mtmode"] button,[data-strk],[data-tim],#splitm', "multiTrig"],
		['.maprow [data-mhi],.maprow select,.maprow .kselbtn,.maprow .pc,[data-mdel],[data-madd],[data-band]', "multiMap"],
		['[data-set="port"] button', "portamento"],
		["#rec", "gridRecord"]
	];
	const loadingNote = "Still reading this slot from the machine.";
	function markNa() {
		const caps = FW.machine?.capabilities;
		if (!caps) return;
		for (const [sel, cap] of NA) {
			const why = caps[cap] === false ? caps.reasons?.[cap] || "Not available here." : "";
			for (const el of $$(sel)) {
				if (why && el.dataset.na !== "1") { el.dataset.na = "1"; el.title = why; el.setAttribute("aria-disabled", "true"); }
				else if (!why && el.dataset.na === "1") { delete el.dataset.na; el.removeAttribute("aria-disabled"); }
			}
		}
		/* library slots not read yet */
		for (const el of $$("#libpop .ps[data-ps]")) {
			const miss = !FW.pat[+el.dataset.ps];
			if (miss !== (el.dataset.na === "1")) { if (miss) { el.dataset.na = "1"; el.title = loadingNote; } else delete el.dataset.na; }
		}
		for (const el of $$("#libpop .ks[data-ks]")) {
			const miss = !FW.kit[+el.dataset.ks];
			if (miss !== (el.dataset.na === "1")) { if (miss) { el.dataset.na = "1"; el.title = loadingNote; } else delete el.dataset.na; }
		}
		for (const el of $$(".band[data-band]")) { const r = S().mmap[+el.dataset.band]; if (r && r.pat < 0 && el.textContent !== "CUR") el.textContent = "CUR"; }
		/* the reason in words where a whole card is the machine's */
		const card = $(".maprow");
		if (card && caps.multiMap === false && !card.querySelector(".statusline")) card.querySelector("header").insertAdjacentHTML("afterend", `<p class="statusline">${caps.reasons.multiMap}</p>`);
		const songNote = $(".songui .card:last-child header .note");
		if (songNote && !songNote.dataset.song) { songNote.dataset.song = "1"; songNote.textContent = `Song ${String(CUR.song + 1).padStart(2, "0")} (the machine's current song) · ` + songNote.textContent; }
		for (const el of $$(".srch")) if (!el.dataset.hint) { el.dataset.hint = "1"; el.title = "On-screen source: it drives its targets through the editor. Your controller's knobs reach the machine through LEARN (the plug-in's MIDI learn)."; }
	}
	const blockNa = e => {
		const el = e.target.closest?.("[data-na]");
		if (!el) return;
		e.preventDefault(); e.stopImmediatePropagation();
		if (e.type === "click") V().toast(el.title);
	};

	/* ---------------- the keyboard and the joystick play the machine (MIDI into the plug-in) ---------------- */
	const chan = () => FW.glob[CUR.glob]?.channels;
	function keyChannel() {
		const c = chan();
		if (!c) return null;
		const ch = S().mode === "multi" ? c.multiTrig : S().mode === "map" ? c.multiMap : c.base + V().asgT();
		return ch >= 0 && ch < 16 ? ch : null;
	}
	const trackChannel = t => { const c = chan(); const ch = c ? c.base + t : -1; return ch >= 0 && ch < 16 ? ch : null; };
	const midi = (b, key) => send({ op: "midi", b }, key ? { key } : {});
	let held = null;
	function noteOff() { if (held) { midi([0x80 | held.ch, held.n, 0]); held = null; } }
	let joyOn = false;
	function joySend(x, y) {
		const ch = trackChannel(V().asgT());
		if (ch == null) return;
		const pb = Math.max(0, Math.min(16383, Math.round(8192 + x * 8191)));
		midi([0xe0 | ch, pb & 0x7f, pb >> 7], "joyx");
		midi([0xb0 | ch, 1, Math.round(Math.max(0, y) * 127)], "joyu");
		midi([0xb0 | ch, 2, Math.round(Math.max(0, -y) * 127)], "joyd");
	}

	/* ---------------- LEARN: the plug-in's MIDI learn (persistent, per plug-in) ---------------- */
	let learnPid = null;
	function onLearn(doc) {
		const prev = FW.learn;
		FW.learn = doc;
		if (prev && prev.learning && !doc.learning && doc.mappings.length > prev.mappings.length) {
			const m = doc.mappings[doc.mappings.length - 1];
			V().toast(`Learned: CC ${m.cc} → T${m.t + 1} ${PAGES[m.pg] || ""} ${V().pname(m.t, PAGES[m.pg] + "." + m.i) || ""}. Stored with the plug-in.`);
			S().learnT = null; learnPid = null;
		}
	}

	/* ---- MULTI MAP: read from the global; its fields past key and pattern are not decoded ---- */
	function mapFromGlobal() {
		const g = FW.glob[CUR.glob];
		if (!g || !g.multiMap) return;
		const hi = g.multiMap[0], pat = g.multiMap[1], rows = [];
		for (let r = 0; r < 32; r++) {
			if (r && hi[r] <= hi[r - 1]) break;
			rows.push({ hi: hi[r], pat: pat[r] === 255 ? -1 : pat[r], ofs: null, len: null, trn: null, tim: null });
		}
		rows[rows.length - 1].hi = 127;
		S().mmap = rows;
		S().mmapSel = Math.min(S().mmapSel || 0, rows.length - 1);
	}

	/* ================= the host: what the mockup hands to the machine ================= */
	const kitNameOf = k => V().kitName(k);
	const host = window.MMHost = {
		ownsClock: true,	// the firmware is the sequencer: no local clock
		engineLabels: { sync: ["READING MACHINE", "blink"], unsupported: ["NOT OS 1.32B", "off"] },
		start() {
			if (document.readyState === "loading") addEventListener("DOMContentLoaded", init);
			else init();
		},
		edited(what, kind) {
			if (what !== "commit") {
				for (const k of kind ? [kind] : EDITS[what] || []) dirty.add(k);
				scheduleSync();
				return;
			}
			/* the gesture ended: what it edited goes out with its id, the next edit is a new undo step */
			if (syncT) { clearTimeout(syncT); syncT = 0; sync(); }
			gesture = Bridge.gesture();
		},
		slotWritten(kind, slot) { written.add(kind + ":" + slot); scheduleSync(); },
		undo() { send({ op: "undo" }, { onResult: r => r.ok ? V().toast(r.note || "Undo") : V().toast(r.errors[0]) }); },
		redo() { send({ op: "redo" }, { onResult: r => r.ok ? V().toast(r.note || "Redo") : V().toast(r.errors[0]) }); },
		history() { const h = FW.machine?.history; return { undo: h?.undoCount || 0, redo: h?.redoCount || 0 }; },
		togglePlay() { if (V().engReady()) send({ op: S().playing ? "stop" : "play" }); },
		selectPattern(p, nowFlag) { send({ op: "select", p, now: !!nowFlag }, { onResult: r => r.note && V().toast(r.note) }); },
		kit(op, k) {
			const s = S();
			if (op === "save") {
				s.kits[s.kit] = { name: s.workName, empty: false, data: V().captureKit() };	// the page's copy of the slot, at once
				sync();	// working kit edits first
				send({ op: "saveKit", k: s.kit }, { onResult: r => r.ok || V().toast(r.errors[0]) });
				return;
			}
			if (op === "reload") {
				if (s.kitState !== "edited") { V().toast(kitNameOf(s.kit) + " matches its saved slot. Nothing to reload."); return; }
				V().ask(`Reload <b>${kitNameOf(s.kit)}</b> from the machine? Your edits go to its UNDO KIT.`,
					[["Reload (discard edits)", "danger", () => { send({ op: "loadKit", k: s.kit }); V().toast("Reloading " + kitNameOf(s.kit) + " (LOAD KIT)."); V().drawLib(true); }],
						["Cancel", "", () => V().drawLib(true)]]);
				return;
			}
			if (op === "load") {
				if (k === s.kit) { if (s.kitState === "edited") host.kit("reload", k); else V().toast(kitNameOf(k) + " is already the current kit."); return; }
				const go = () => { send({ op: "loadKit", k }); V().toast("Loading " + kitNameOf(k) + " (LOAD KIT)."); V().drawLib(true); };
				if (s.kitState === "edited") {
					V().ask(`Load <b>${kitNameOf(k)}</b>? Your edits to <b>${kitNameOf(s.kit)}</b> are not saved on the machine. They go to its UNDO KIT.`,
						[["Save kit, then load", "cream", () => { host.kit("save"); go(); }], ["Load (edits to UNDO KIT)", "danger", go], ["Cancel", "", () => V().drawLib(true)]]);
					return;
				}
				go();
				return;
			}
			if (op === "saveAs") {
				if (k === s.kit) { V().kitSave(); return; }
				const go = () => { sync(); send({ op: "saveKit", k }); V().toast("Saving as " + kitNameOf(k) + " (SAVE KIT). It becomes the current kit."); V().drawLib(true); };
				if (!s.kits[k].empty) {
					V().ask(`Overwrite <b>${kitNameOf(k)}</b> with the current kit <b>${kitNameOf(s.kit)}</b>? The machine keeps the overwritten kit in its UNDO KIT.`,
						[["Overwrite", "danger", go], ["Cancel", "", () => V().drawLib(true)]]);
					return;
				}
				go();
			}
		},
		tempo(bpm) { last.tempoEdit = now(); send({ op: "tempo", bpm }, { key: "tempo" }); },
		mutes() {
			/* mutes and solos of the synth tracks: the plug-in's mute parameters */
			S().tracks.forEach((t, i) => {
				const off = !V().audible(i);
				if (off !== last.mute[i]) { last.mute[i] = off; send({ op: "mute", t: i, on: off ? 1 : 0 }); }
			});
		},
		playKey(n) {
			$$(".kb .dn").forEach(k => k.classList.remove("dn"));
			$(`.kb [data-key="${n}"]`)?.classList.add("dn");
			const ch = keyChannel(), info = $("#kbinfo");
			if (ch == null) { if (info) info.textContent = "That MIDI channel is OFF in the global (GLOBAL › MIDI › CHANNELS)."; return; }
			if (held && held.n === n && held.ch === ch) return;
			noteOff();
			midi([0x90 | ch, n, 100]);
			held = { ch, n };
			const what = S().mode === "multi" ? "MULTI TRIG" : S().mode === "map" ? "MULTI MAP" : "T" + (V().asgT() + 1);
			if (info) info.textContent = `${what} · ${V().noteName(n)} · MIDI channel ${ch + 1}`;
		},
		joy(xy) { joyOn = true; joySend(xy.x, xy.y); },
		learnBind(lt, k) {
			const [pg, i] = lt.pid.split(".");
			if (lt.t >= 6 || pg === "MID") { V().toast("MIDI page values cannot be learned."); return; }
			send({ op: "learnCancel" });
			send({ op: "learnAdd", cc: 20 + k, t: lt.t, pg: PAGES.indexOf(pg), i: +i }, { onResult: r => { if (!r.ok) V().toast(r.errors[0]); } });
			learnPid = null;
			/* the mockup's Control workspace then binds its Knob k to the same value on screen */
		},
		engine(kind) { send({ op: "engine", kind }, { onResult: r => V().toast(r.ok ? r.note : r.errors[0]) }); },
		firstRun() {
			log("NO ROM dialog shown (engine " + FW.engine + ")");
			V().ask(`<div class="lcdbig">MONOMACHINE FIRMWARE NEEDED</div>
 <p>Monomachine Editor runs the real Monomachine operating system. Elektron's firmware cannot ship with the plug-in, so you add the one from your own machine.</p>
 <ol class="recvsteps"><li>Dump the <b>OS 1.32B</b> flash image from your Monomachine (<span class="mono">.bin</span>).</li><li>Put it in the plug-in's ROM folder (<b>Show ROM folder</b>).</li><li>Reopen the plug-in. It stays on this computer only.</li></ol>`,
				[["Show ROM folder", "cream", () => send({ op: "revealRomFolder" })],
					["Check again", "", () => send({ op: "recheckFirmware" }, { onResult: r => { V().toast(r.ok ? r.note : r.errors[0]); if (!r.ok) setTimeout(host.firstRun, 50); } })], ["Close", "", () => {}]], "first");
		},
		/* while the firmware starts, the LCD shows the machine's own screen (the lcd messages) */
		bootScreen() {
			const el = $("#bootscr");
			if (!el) return;
			const st = S().eng, on = st === "boot" || st === "sync" || st === "loading";
			clearTimeout(lcdFade);
			if (on) { el.classList.remove("fading"); el.classList.add("on", "fw"); return; }
			if (!el.classList.contains("on")) return;
			/* cross-fade from the machine's own screen to the editor's fields */
			el.classList.add("fading");
			lcdFade = setTimeout(() => el.classList.remove("on", "fw", "fading"), 460);
		},
		/* SYSEX RECV state in the pattern field */
		renderPst() {
			const p = $("#pst");
			if (!p) return;
			const r = FW.machine?.recv, manual = FW.machine?.capabilities?.dumps === "manual";
			const on = r && (r.sending > 0 || r.state === "entering" || r.state === "parked");
			p.textContent = manual && r?.sending ? "SEND " + r.sending : on ? (r.sending > 0 ? "RECV " + r.sending : "RECV") : "";
			p.className = manual && r?.sending ? "pst warn" : "pst";
			p.title = manual && r?.sending ? FW.machine.capabilities.reasons?.recvSession || ""
				: on ? "The machine is on GLOBAL › SYSEX RECV and takes the edits (" + r.state + ", " + r.received + " received)." : "";
		},
		/* the AUDIO / MIDI panel: the plug-in's devices (mdAudioMidiLink.cpp); in a plug-in the
		   document says standalone false, and the engine menu has no entry for it */
		audioDoc() { return audioDocument; },
		audioSend(c) { send(Object.assign({ op: "audioSet" }, c)); },
		audioMeter(on) { send({ op: "audioMeter", on: !!on }); }
	};
	function showAudioEntry() {
		const o = document.querySelector('#engsel option[value="audio"]');
		if (o) o.hidden = o.disabled = !(audioDocument && audioDocument.standalone);
	}

	/* ---------------- start: after the mockup and MmConvert are there ---------------- */
	function init() {
		const v = V(), s = v.S;
		/* The page's first render, logged so a blank page fails the self-tests (P5). */
		setTimeout(() => { const a = $(".app"), r = a ? a.getBoundingClientRect() : { width: 0, height: 0 };
			log(`first render: ${$$("#main *").length} elements in #main, page ${Math.round(r.width)} x ${Math.round(r.height)}, window ${innerWidth} x ${innerHeight}`);
			/* The LCD transport keys must be square and side by side (they collapsed to slivers once). */
			const rb = $("#rec")?.getBoundingClientRect(), pb = $("#play")?.getBoundingClientRect();
			const ok = rb && pb && Math.abs(rb.width - rb.height) < 1 && Math.abs(pb.width - pb.height) < 1 && rb.width > 30 && Math.abs(rb.top - pb.top) < 1 && pb.left > rb.right;
			log(`transport keys: REC ${rb ? Math.round(rb.width) + "x" + Math.round(rb.height) : "?"}, PLAY ${pb ? Math.round(pb.width) + "x" + Math.round(pb.height) : "?"}: ${ok ? "ok square, side by side" : "FAIL"}`);
			const mk = getComputedStyle($(".lcdgroup"), "::after"), top = $(".top").getBoundingClientRect(), g = $(".lcdgroup").getBoundingClientRect();
			log(`MKII print: bottom ${mk.bottom}, LCD group bottom ${Math.round(g.bottom)}, header bottom ${Math.round(top.bottom)}`); }, 1500);
		/* start empty, not with the mockup's example */
		v.applyKit({ ...v.clearedKit(), multi: s.multi });
		s.tracks.forEach(t => t.name = v.machName(t.m));
		v.applyPat(v.emptyPat(16));
		s.song = [{ type: "end" }];
		s.songSel = 0;
		s.kits = Array.from({ length: 128 }, () => ({ name: "", empty: true, data: null }));
		s.patInfo = Array.from({ length: 128 }, () => ({ has: false, len: 16 }));
		s.patKit = Array(128).fill(0);
		s.patData = {};
		s.workName = "";
		s.kit = 0; s.pat = 0; s.queued = null; s.playing = false; s.step = -1;
		s.ctl.links = [];	// the Control workspace starts without the mockup's example mappings
		s.tracks.forEach((t, i) => last.mute[i] = !!t.mute);
		document.addEventListener("pointerup", () => { noteOff(); if (joyOn) { joyOn = false; joySend(0, 0); } }, true);
		document.addEventListener("pointercancel", () => noteOff(), true);
		document.addEventListener("pointerdown", () => setTimeout(() => {
			if (!s.learn || !s.learnT) return;
			const k = s.learnT.t + "|" + s.learnT.pid;
			if (k === learnPid) return;
			learnPid = k;
			const [pg, i] = s.learnT.pid.split(".");
			if (s.learnT.t >= 6 || pg === "MID") { v.toast("MIDI page values are NRPN on the machine: the plug-in's MIDI learn cannot map them."); s.learnT = null; return; }
			send({ op: "learnStart", t: s.learnT.t, pg: PAGES.indexOf(pg), i: +i }, { onResult: r => r.ok ? v.toast("Move a knob on your controller, or press 1-8 for CC 21-28.") : v.toast(r.errors[0]) });
		}, 0), true);
		document.addEventListener("click", e => {
			if (e.target.closest("#learnkey")) setTimeout(() => { if (!s.learn) { learnPid = null; send({ op: "learnCancel" }); } }, 0);
		}, true);
		for (const ev of ["pointerdown", "click", "change", "wheel", "keydown", "dragstart"]) document.addEventListener(ev, blockNa, { capture: true, passive: false });
		let naQueued = false;
		new MutationObserver(() => { if (!naQueued) { naQueued = true; queueMicrotask(() => { naQueued = false; markNa(); }); } })
			.observe(document.body, { childList: true, subtree: true });
		/* the editor's menu (skins, GUI scale, settings): right-click an empty part of the header */
		document.addEventListener("contextmenu", e => {
			if (!e.target.closest(".top") || e.target.closest("button,[role=slider],[role=button],select,input,b,.lcdpanel")) return;
			e.preventDefault();
			send({ op: "openMenu" });
		});
		addEventListener("error", e => log("page error: " + e.message + " at " + (e.filename || "").split("/").pop() + ":" + e.lineno));
		Bridge.onMessage(m => {
			try { onMessage(m); } catch (e) { log("page error in " + m.type + ": " + e.message + " " + (e.stack || "").split("\n")[0]); }
			selfTestSeen(m);
		});
		setInterval(() => {
			applyPending();
			if (libDirty && v.LIB.open && !v.busy()) { libDirty = false; v.drawLib(); }
		}, 120);
		showAudioEntry();
		v.setEng("loading");
		send({ op: "audio" });
		Bridge.ready();
	}

	function onMessage(m) {
		if (m.type === "doc") onDoc(m);
		else if (m.type === "machine") onMachine(m.doc);
		else if (m.type === "tel") { if (m.playing !== S().playing) setPlaying(m.playing); if (S().playing) showStep(m.step); }
		else if (m.type === "lcd") { drawLcd(m.bits); showEngine(); }
		else if (m.type === "catalogue") FW.cat = m.doc;
		else if (m.type === "reset") onReset();
		else if (m.type === "audio") { audioDocument = m.doc; showAudioEntry(); if (window.AP?.open) drawAudio(); }
		else if (m.type === "audioLevel") audioLevel(m.in);
		else if (m.type === "openAudio") openAudio();
		else if (m.type === "learn") onLearn(m.doc);
		else if (m.type === "result" && !m.ok && m.errors?.length && m.op !== "set") V().toast(m.errors[0]);
	}

	/* ---------------- self test (GEARMULATOR_MMSTUDIO_SELFTEST=1) ----------------
	   Edits through the mockup's own gestures and its host, waits for the firmware's read-back and
	   logs each round trip to the plug-in log. */
	const waiters = [];
	function selfTestSeen(m) { for (const w of [...waiters]) if (w.test(m)) { waiters.splice(waiters.indexOf(w), 1); w.done(m); } }
	const waitFor = (test, ms = 8000) => new Promise((done, fail) => {
		const w = { test, done };
		waiters.push(w);
		setTimeout(() => { const i = waiters.indexOf(w); if (i >= 0) { waiters.splice(i, 1); fail(new Error("timeout")); } }, ms);
	});
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	/* ?selftest=mmcpu (scripts/mm-editor-cpu.sh): fixed 30 s phases, each start and end logged
	   ("cpu <phase> start/end"), so the script reads the processes' CPU time from outside. */
	async function runCpuPhases() {
		const log2 = t => log("MM-P3: " + t);
		while (!FW.machine || FW.machine.loading.done < FW.machine.loading.total) await sleep(200);
		await sleep(5000);	// the background loads settle
		const phase = async (name, ms) => { log2(`cpu ${name} start`); await sleep(ms); log2(`cpu ${name} end`); };
		V().goWs("seq");
		await phase("stopped", 30000);
		if (!S().playing) host.togglePlay();
		await sleep(1000);
		await phase("playing-seq", 30000);
		V().goWs("mix");
		await phase("playing-mix", 30000);
		host.togglePlay();
		log2("cpu done");
	}
	async function runSelfTest() {
		const s = S(), results = [];
		const check = async (name, fn) => {
			const t0 = now();
			try { const n = await fn(); results.push(true); log(`SELFTEST ok ${name} ${Math.round(now() - t0)} ms${n ? " " + n : ""}`); }
			catch (e) { results.push(false); log(`SELFTEST FAIL ${name}: ${e.message}`); }
			await sleep(1200);
		};
		const p = CUR.pat;
		const readBack = test => waitFor(m => m.type === "doc" && m.kind === "pattern" && m.slot === p && !m.pending && test(m.doc));
		const free = () => [...Array(s.len).keys()].find(st => !s.tracks[0].steps[st]);
		const clickStep = (t, st, e) => { V().clickStep(t, st, e); host.edited("commit"); };
		log(`SELFTEST start: pattern ${p} kit ${CUR.kit} song ${CUR.song} global ${CUR.glob}, loaded ${FW.machine?.loading.done}/${FW.machine?.loading.total}`);
		await check("pattern trig (SYSEX RECV round trip)", async () => {
			const st = free();
			if (st == null) throw new Error("no free step on T1");
			clickStep(0, st, {});
			await readBack(d => d.tracks[0].trig.includes(st) && d.tracks[0].amp.includes(st));
			clickStep(0, st, {});
			await readBack(d => !d.tracks[0].trig.includes(st));
			return "step " + (st + 1);
		});
		await check("undo and redo in C++ (the core's history)", async () => {
			const st = free();
			clickStep(0, st, {});
			await readBack(d => d.tracks[0].trig.includes(st));
			await sleep(200);
			const before = FW.machine?.history?.undoCount || 0;
			host.undo();
			await readBack(d => !d.tracks[0].trig.includes(st));
			for (let n = 0; n < 15 && s.tracks[0].steps[st]; n++) await sleep(100);
			if (s.tracks[0].steps[st]) throw new Error("the view still shows the undone trig");
			host.redo();
			await readBack(d => d.tracks[0].trig.includes(st));
			host.undo();
			await readBack(d => !d.tracks[0].trig.includes(st));
			return "undo steps " + before;
		});
		await check("trigless trig", async () => {
			const st = free();
			clickStep(0, st, { altKey: true });
			await readBack(d => d.tracks[0].trig.includes(st) && !d.tracks[0].amp.includes(st) && d.tracks[0].notes.some(n => n[0] === st));
			clickStep(0, st, {});
			await readBack(d => !d.tracks[0].trig.includes(st));
		});
		await check("pitchless trig", async () => {
			const st = free();
			s.tracks[0].steps[st] = { a: 1, f: 1, l: 1 };
			window.structEdited();
			await readBack(d => d.tracks[0].trig.includes(st) && !d.tracks[0].notes.some(n => n[0] === st));
			s.tracks[0].steps[st] = null;
			window.structEdited();
			await readBack(d => !d.tracks[0].trig.includes(st));
		});
		await check("kit value live (AMP VOL, CC)", async () => {
			const v0 = s.tracks[0].v.AMP[5], v = v0 > 60 ? v0 - 7 : v0 + 7;
			const got = waitFor(m => m.type === "doc" && m.kind === "kit" && m.working && m.doc.tracks[0].pages[1][5] === v, 5000);
			s.tracks[0].v.AMP[5] = v;
			window.soundEdited();
			await got;
			s.tracks[0].v.AMP[5] = v0;
			window.soundEdited();
			await waitFor(m => m.type === "doc" && m.kind === "kit" && m.working && m.doc.tracks[0].pages[1][5] === v0, 5000);
		});
		await check("pattern switch (LOAD PATTERN)", async () => {
			const to = (p + 1) % 128;
			window.queuePattern(to);
			await waitFor(m => m.type === "machine" && m.doc.pattern.current === to);
			await sleep(800);
			if (s.pat !== to) throw new Error("page shows " + s.pat);
			window.queuePattern(p);
			await waitFor(m => m.type === "machine" && m.doc.pattern.current === p);
		});
		await check("play and stop, playhead and POSITION follow the machine", async () => {
			let stage = "play";
			const W = (t, ms) => waitFor(t, ms).catch(() => { throw new Error("timeout at " + stage + ", step " + s.step + ", playing " + s.playing); });
			V().goWs("seq");
			await sleep(300);
			window.togglePlay();
			await W(m => m.type === "machine" && m.doc.playing, 4000);
			stage = "tel playing";
			await W(m => m.type === "tel" && m.playing && m.step > 0, 4000);
			/* the soft playhead column glides with the machine's step (RAM telemetry), POSITION too */
			/* the column's target (its style): a covered window runs no transitions, so the computed one can stand still */
			const at = () => { const ph = $("#phcol"); return { x: ph && ph.style.transform ? new DOMMatrix(ph.style.transform).m41 : null, o: ph ? +ph.style.opacity : 0, h: ph ? ph.offsetHeight : 0, pos: $("#pos").textContent, step: s.step }; };
			await sleep(150); const a = at(); stage = "next step";
			await W(m => m.type === "tel" && m.playing && m.step > a.step);
			await sleep(250); const b = at(); stage = "stop";
			window.togglePlay();
			await W(m => m.type === "machine" && !m.doc.playing);
			await sleep(300); const c = at();
			const lane = $("#lane")?.getBoundingClientRect(), roll = $("#seq .nlane.big")?.getBoundingClientRect(), ph = $("#phcol")?.getBoundingClientRect();
			const spans = lane && roll && ph && ph.top <= roll.top && ph.bottom >= lane.bottom;
			const n = `x ${Math.round(a.x)} -> ${Math.round(b.x)}, step ${a.step} -> ${b.step}, POSITION ${a.pos} -> ${b.pos} -> ${c.pos}, column ${a.h} px over roll..lane ${spans ? "yes" : "no"}, opacity after stop ${c.o}`;
			if (a.x == null || !(b.x > a.x) || a.o !== 1 || b.o !== 1 || !spans || a.pos === b.pos || c.pos !== "--.--" || c.o !== 0) throw new Error(n);
			return n;
		});
		await check("tempo out and read back (0x61, RAM)", async () => {
			const t0 = s.bpm, t = t0 === 133 ? 127 : 133;
			s.bpm = t; host.tempo(t);
			await waitFor(m => m.type === "machine" && m.doc.tempo === t, 5000);
			s.bpm = t0; host.tempo(t0);
			await waitFor(m => m.type === "machine" && m.doc.tempo === t0, 5000);
			return t0 + " -> " + t + " -> " + t0 + " BPM";
		});
		await check("solo mutes the other synth tracks", async () => {
			s.tracks[2].solo = true; host.mutes();
			await sleep(300);
			const muted = last.mute.join("");
			s.tracks[2].solo = false; host.mutes();
			await sleep(300);
			if (muted !== "truetruefalsetruetruetrue") throw new Error("mutes " + muted);
		});
		await check("song and global documents", async () => {
			if (!FW.song[CUR.song] || !FW.glob[CUR.glob]) throw new Error("not loaded");
			return s.song.length + " rows, routing " + s.routing;
		});
		await check("capabilities as data (the engine's reasons)", async () => {
			const caps = FW.machine?.capabilities;
			if (!caps || caps.engine !== "emu" || caps.midiMutes !== false || !caps.reasons?.midiMutes) throw new Error(JSON.stringify(caps));
			const el = $('[data-mute="6"]');
			if (el && el.dataset.na !== "1") throw new Error("MIDI track mute not marked");
			return Object.keys(caps.reasons).length + " reasons";
		});
		const ok = results.filter(Boolean).length;
		log(`SELFTEST ${ok === results.length ? "PASS" : "FAIL"} ${ok}/${results.length}`);
	}
})();
