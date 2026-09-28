"use strict";
/* Monomachine Editor: the plug-in's host for the approved mockup (P6).
   The mockup (mmMockup.js, copied verbatim from doc/modern-ux/mm-mockup) is the UI. It hands
   everything the machine does to its host, window.MMHost, which this file defines before the
   mockup loads: transport, patterns, the kit library, undo, tempo, mutes, the keyboard, the
   Control workspace's modulators and the AUDIO / MIDI panel. Nothing of the mockup is replaced.
   Its view is used through window.MMView only: values to read, named setters, and
   disable(capability, reason); never its state (sync-mmstudio-skin.py checks that).
   - The plug-in publishes the core's documents (observed, or pending while an edit is on its
     way), keyed by the contract's kinds: pattern, kit (the stored slots), workingKit (the kit
     that plays), song, global; and the machine document, which says which slots are current.
     They are shown in the view through MmConvert (the pure page <-> contract translation).
   - An edit is sent as the intent the core takes for the Monomachine: the whole document
     ({"op":"set","kind","doc","g"}) of each kind the gesture said it edited: "workingKit" for the
     kit that plays, "kit" only for a stored slot the library wrote. The page does not compare
     documents: the core takes a document equal to its own as no change. The gesture id g makes
     a drag one undo step; undo and redo are the core's (C++).
   - The Control workspace's LFO and Random sources run in the plug-in ("modSet", the "mod"
     message brings their values); they are never kit edits.
   - What the engine cannot do is disabled with the reason, from machine.capabilities. */
(() => {
	const V = () => window.MMView;			// the mockup's view: after it ran
	const $ = q => document.querySelector(q), $$ = q => [...document.querySelectorAll(q)];
	const send = (msg, opt) => Bridge.send(msg, opt);
	const log = t => Bridge.log(t);
	const now = () => performance.now();
	const copy = o => o == null ? o : JSON.parse(JSON.stringify(o));
	const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);	// documents are JSON values

	/* ---------------- what the core shows ---------------- */
	const KINDS = ["kit", "pattern", "song", "global"];	// the working set's kinds, in sending order
	const DOCS = { pattern: [], kit: [], song: [], global: [] };	// by slot, as the plug-in published them
	let working = null;		// the workingKit message: {slot, source, pending, doc}
	let machine = null, catalogue = null, learn = null;
	/* the current slot of a kind, from a machine document (null: not known yet) */
	function slotIn(d, kind) {
		if (!d) return null;
		if (kind === "pattern") return d.pattern.current;
		if (kind === "kit") return d.kit.current;
		if (kind === "song") return d.song.current ?? 0;
		return d.global ?? 0;
	}
	const cur = kind => slotIn(machine, kind);
	const queuedIn = d => { const q = d?.pattern.queued; return q != null && q !== d.pattern.current ? q : null; };
	/* the kit that plays: the working kit when it is the current slot's, else the stored slot (until
	   the first working kit arrives, and while a kit change has not brought the new one yet) */
	const kitNow = () => working && working.slot === cur("kit") ? working.doc : DOCS.kit[cur("kit")];
	const globalNow = () => DOCS.global[cur("global")];
	const last = { mute: [], ready: false, note: "", noteMs: 0, readyLabel: "", caps: null };
	let synced = false;		// the view shows the machine (current pattern and working kit arrived)
	const wantApply = new Set();	// the current kinds whose documents the view has not shown yet
	const dirty = new Set();	// the working set's kinds a gesture edited
	const written = new Set();	// library slots a gesture wrote ("kit:5", "pattern:12")
	let gesture = Bridge.gesture();
	let tempoInFlight = 0, modInFlight = 0;	// the id of a tempo or modSet command not answered yet
	let audioDocument = null;
	const readyHooks = [];
	/* What a gesture edits when it does not name the document: the mockup's two edit kinds. */
	const EDITS = { struct: ["pattern", "song"], sound: ["kit"] };

	/* ---------------- the machine's documents -> the view ---------------- */
	const C = () => MmConvert;
	const kitPageOf = k => k === cur("kit") && synced ? V().captureKit() : V().kitSlot(k).data || (DOCS.kit[k] ? C().kitToPage(DOCS.kit[k], globalNow()) : null);
	const lenOf = p => p === cur("pattern") && synced ? V().patternLength(p) : DOCS.pattern[p]?.length ?? V().patternLength(p);
	/* what the view's state is as the contract's documents */
	function patDoc(p) {
		const b = DOCS.pattern[p] || DOCS.pattern[cur("pattern")] || DOCS.pattern.find(Boolean);
		if (!b) return null;
		const slot = V().patternSlot(p), data = p === cur("pattern") ? V().capturePat() : slot.data;
		return data ? C().patternToFw(data, b, slot.kit, kitPageOf(slot.kit), p) : null;
	}
	function kitDoc(k) {
		const b = (k === cur("kit") ? kitNow() : DOCS.kit[k]) || kitNow() || DOCS.kit.find(Boolean);
		if (!b) return null;
		if (k === cur("kit")) return C().kitToFw(V().captureKit(), b, V().workName(), k);
		const s = V().kitSlot(k);
		return C().kitToFw(s.data || V().clearedKit(), b, s.empty ? "" : s.name, k);
	}
	const songDoc = () => DOCS.song[cur("song")] ? C().songToFw(V().song(), DOCS.song[cur("song")], lenOf) : null;
	const globDoc = () => globalNow() ? C().globalToFw(globalNow(), V().routing(), V().midiTracks()) : null;

	function setSlotPattern(p) {
		const d = DOCS.pattern[p];
		if (!d || p === cur("pattern")) return;
		V().setPatternSlot(p, { data: C().patternToPage(d, kitPageOf(d.kit)), kit: d.kit, has: C().hasTrigs(d), len: d.length });
	}
	function setSlotKit(k) {
		const d = DOCS.kit[k];
		V().setKitSlot(k, { name: C().kitName(d), empty: C().kitEmpty(d), data: C().kitToPage(d, globalNow()) });
	}
	const kitState = () => machine?.kit.working === "edited" ? "edited" : "clean";
	function applyCurrentKit() {
		const d = kitNow();
		V().setWorkingKit(C().kitToPage(d, globalNow()), C().kitName(d));
		if (machine) V().setKitState(kitState());
	}
	function applyCurrentPattern() {
		const d = DOCS.pattern[cur("pattern")];
		V().setPatternSlot(cur("pattern"), { data: C().patternToPage(d, V().captureKit()), kit: d.kit, has: C().hasTrigs(d), len: d.length });
	}
	const applyCurrentSong = () => V().setSong(C().songToPage(DOCS.song[cur("song")], lenOf), cur("song"));
	function applyCurrentGlobal() {
		const g = globalNow();
		V().setRouting(g.routingMode);
		V().setMidiTracks(g.midiSeq.channels.map((c, t) => ({ ch: c + 1, cc: [...g.midiSeq.ccs[t]] })));
		mapFromGlobal(g);
	}
	/* ---- MULTI MAP: read from the global; its fields past key and pattern are not decoded ---- */
	function mapFromGlobal(g) {
		if (!g.multiMap) return;
		const hi = g.multiMap[0], pat = g.multiMap[1], rows = [];
		for (let r = 0; r < 32; r++) {
			if (r && hi[r] <= hi[r - 1]) break;
			rows.push({ hi: hi[r], pat: pat[r] === 255 ? -1 : pat[r], ofs: null, len: null, trn: null, tim: null });
		}
		rows[rows.length - 1].hi = 127;
		V().setMultiMap(rows);
	}

	/* Show what the core holds, unless the user is in a gesture or the view has edits not sent
	   yet (the next reconcile sends them; the core then shows them). */
	function applyPending() {
		if (!wantApply.size) return;
		if (DOCS.pattern[cur("pattern")] == null || kitNow() == null) return;
		if (synced && (V().busy() || !$("#dlg").hidden || V().libBusy() || syncT)) return;
		const kinds = [...wantApply];
		wantApply.clear();
		/* the kit first: the pattern's locks read their machines from it */
		const kit = kinds.includes("kit"), song = kinds.includes("song") && DOCS.song[cur("song")], glob = kinds.includes("global") && globalNow();
		if (kit) applyCurrentKit();
		if (kinds.includes("pattern") || kit) applyCurrentPattern();
		if (song) applyCurrentSong();
		if (glob) applyCurrentGlobal();
		if (!synced) {
			synced = true;
			for (let i = 0; i < 6; i++) last.mute[i] = !V().audible(i);
		}
		if (kit || kinds.includes("pattern") || song || glob) {
			V().autoRange(V().sel());
			V().render();
			V().drawLib();
		}
		showEngine();
	}

	/* the library slots not read yet */
	function markReading() {
		for (const kind of ["pattern", "kit"]) V().setReading(kind, [...Array(128).keys()].filter(s => !DOCS[kind][s]));
	}
	function onDoc(m) {
		const { kind, slot, doc } = m;
		if (kind === "workingKit") {
			working = { slot, source: m.source, pending: !!m.pending, doc };
			if (slot === cur("kit")) wantApply.add("kit");
		}
		else if (DOCS[kind]) {
			const first = !DOCS[kind][slot];
			DOCS[kind][slot] = doc;
			if (kind === "pattern") { if (slot === cur("pattern")) wantApply.add("pattern"); else setSlotPattern(slot); }
			else if (kind === "kit") { setSlotKit(slot); if (slot === cur("kit") && !(working && working.slot === slot)) wantApply.add("kit"); }
			else if (slot === cur(kind)) wantApply.add(kind);
			if (first && (kind === "pattern" || kind === "kit")) markReading();
		}
		/* the patterns that play this kit read their locks' machines from it */
		if (kind === "kit" || kind === "workingKit")
			for (let p = 0; p < 128; p++) if (DOCS.pattern[p] && DOCS.pattern[p].kit === slot) setSlotPattern(p);
		libDirty = true;
		/* the current documents are shown right away (the 120 ms timer only catches up after a gesture) */
		if (wantApply.size && !applyT) applyT = setTimeout(() => { applyT = 0; applyPending(); }, 0);
	}
	let applyT = 0;
	let libDirty = false;

	function onMachine(d) {
		const prev = machine;
		machine = d;
		const changed = kind => slotIn(d, kind) != null && slotIn(d, kind) !== slotIn(prev, kind);
		if (changed("kit")) { V().setCurrent({ kit: cur("kit") }); if (kitNow()) wantApply.add("kit"); }
		if (changed("pattern")) {
			const old = slotIn(prev, "pattern");
			V().setCurrent({ pattern: cur("pattern") });
			if (old != null) setSlotPattern(old);
			if (DOCS.pattern[cur("pattern")]) wantApply.add("pattern");
		}
		for (const kind of ["song", "global"]) if (changed(kind) && DOCS[kind][cur(kind)]) wantApply.add(kind);
		if (queuedIn(d) !== queuedIn(prev)) { V().setQueued(queuedIn(d)); V().renderTop(); V().drawLib(); }
		V().setPlaying(!!d.playing);
		/* the machine's tempo (RAM), unless a tempo the user set is still on its way */
		if (!tempoInFlight && d.tempo != null && d.tempo !== V().tempo()) { V().setTempo(d.tempo); V().renderTop(); }
		if (synced && !V().busy()) V().setKitState(kitState());
		host.renderPst();
		V().setEngines(d.engines || [], d.capabilities?.engine);
		markCapabilities(d.capabilities);
		showEngine();
		V().renderTop();
	}
	/* what the engine cannot do: disabled, with the reason (machine.capabilities) */
	function markCapabilities(caps) {
		if (!caps || same(caps, last.caps)) return;
		last.caps = copy(caps);
		for (const [cap, can] of Object.entries(caps)) if (typeof can === "boolean") V().disable(cap, can ? "" : caps.reasons?.[cap] || "Not available here.");
	}

	/* the plug-in's engine changed (HW MIDI or the emulator): its documents start over */
	function onReset() {
		for (const k of KINDS) DOCS[k] = [];
		working = null;
		machine = null;
		synced = false;
		wantApply.clear();
		dirty.clear();
		written.clear();
		last.ready = false;
		markReading();
		showEngine();
	}

	/* ---------------- the view -> the core ---------------- */
	/* After an edit, the working set's documents the gesture edited (and the slots a library
	   gesture wrote) go out whole, tagged with the gesture. */
	let syncT = 0;
	function scheduleSync() { if (!syncT) syncT = setTimeout(() => { syncT = 0; sync(); }, 40); }
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
		/* the kit that plays goes live (CC, NRPN, machine, routing, name) */
		if (edited.includes("kit")) sendDoc("workingKit", kitDoc(cur("kit")), cur("kit"));
		if (edited.includes("pattern")) sendDoc("pattern", patDoc(cur("pattern")), cur("pattern"));
		for (const w of [...written]) {
			const [kind, s] = w.split(":"), slot = +s;
			written.delete(w);
			if (kind === "kit" && slot === cur("kit")) send({ op: "saveKit", k: slot });	// the library wrote the current kit's slot: store it too
			else if (kind === "kit") sendDoc("kit", kitDoc(slot), slot);
			else if (kind === "pattern" && slot !== cur("pattern")) sendDoc("pattern", patDoc(slot), slot);
		}
		if (edited.includes("song")) sendDoc("song", songDoc(), cur("song"));
		if (edited.includes("global")) sendDoc("global", globDoc(), cur("global"));
	}

	/* ---------------- the Control workspace's modulators: run by the plug-in ---------------- */
	/* the page's setup as the plug-in has it: a MIDI track target is not modulated there */
	function sendMods(setup) {
		const doc = C().modToFw(setup);
		if (setup.links.some(l => l.t >= 6) && !same(doc, last.modSent)) V().toast("Targets on MIDI tracks are not moved by the plug-in's modulators.");
		last.modSent = doc;
		modInFlight = send({ op: "modSet", doc }, { key: "modSet", onResult: r => {
			if (r.id === modInFlight) modInFlight = 0;
			if (!r.ok) V().toast(r.errors[0] || "The plug-in did not take the modulators.");
		} });
	}
	/* {type:"mod", doc, values, ccPerSecond, ccLimit, runs}: the setup as taken and its sources' values */
	function onMod(m) {
		const setup = V().ctlSetup();
		/* the plug-in's setup is the one that runs (a project it restored, say); the page's MIDI track
		   targets, which it does not take, stay on the page */
		if (!modInFlight && m.doc && !same(C().modToFw(setup), m.doc)) {
			const page = C().modToPage(m.doc), ids = new Set(page.sources.map(x => x.id));
			page.links.push(...setup.links.filter(l => l.t >= 6 && ids.has(l.src)));
			V().setCtlSetup(page);
		}
		const values = {};
		(m.doc?.sources || []).forEach((x, i) => { if (m.values?.[i] != null) values[x.id] = m.values[i]; });
		V().setModulation({ values, ccPerSecond: m.ccPerSecond || 0 });
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
	/* machine.lifecycle -> the mockup's engine states */
	const LIFE = { missing: "norom", unsupported: "unsupported", loading: "loading", booting: "boot", animating: "boot", ready: "ready",
		hwConnecting: "hwwait", hwLost: "hwnone" };
	function showEngine() {
		if (!window.MMView) return;
		/* the label follows the one lifecycle value; when ready it is the engine's own (capabilities),
		   and while the library fills in the background it says how far */
		const caps = machine?.capabilities, life = LIFE[machine?.lifecycle || "loading"] || "boot";
		const st = life === "ready" && !synced ? "sync" : life;
		if (caps) {
			const l = machine.loading, text = caps.label + (l && l.done < l.total ? ` · ${l.done}/${l.total}` : "");
			if (text + caps.about !== last.readyLabel) { last.readyLabel = text + caps.about; V().setEngineLabel("ready", [text, "on", caps.about || ""]); }
		}
		if (V().engineState() !== st) V().setEng(st);
		/* NO ROM only when the plug-in has said so for 1.5 s (a device being made or replaced is not
		   a missing ROM), and the dialog closes by itself as soon as the engine is anything else. */
		if (st === "norom") { if (!noRomAt) noRomAt = now(); if (!noRomShown && now() - noRomAt > 1500) { noRomShown = true; host.firstRun(); } else if (!noRomShown) setTimeout(showEngine, 1600); }
		else { noRomAt = 0; noRomShown = false; const d = $("#dlg"); if (d && !d.hidden && /FIRMWARE NEEDED/.test(d.textContent)) d.hidden = true; }
		if (st === "ready" && !last.ready) {
			last.ready = true;
			log("ready: pattern " + cur("pattern") + " kit " + cur("kit"));
			for (const f of readyHooks.splice(0)) try { f(); } catch (e) { log("ready hook: " + e.message); }
		}
	}

	/* ---------------- the keyboard and the joystick play the machine (MIDI into the plug-in) ---------------- */
	const chan = () => globalNow()?.channels;
	function keyChannel() {
		const c = chan();
		if (!c) return null;
		const ch = V().mode() === "multi" ? c.multiTrig : V().mode() === "map" ? c.multiMap : c.base + V().asgT();
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
		const prev = learn;
		learn = doc;
		if (prev && prev.learning && !doc.learning && doc.mappings.length > prev.mappings.length) {
			const m = doc.mappings[doc.mappings.length - 1];
			V().toast(`Learned: CC ${m.cc} → T${m.t + 1} ${PAGES[m.pg] || ""} ${V().pname(m.t, PAGES[m.pg] + "." + m.i) || ""}. Stored with the plug-in.`);
			V().clearLearnTarget(); learnPid = null;
		}
	}

	/* ---------------- what the plug-in asks, and what it refused ---------------- */
	/* {type:"ask", ask, message?, command?}: a confirmation; "Go on" sends the command again with force */
	function onAsk(m) {
		const text = m.message || "The machine asks before it goes on (" + m.ask + ").";
		V().ask(text, m.command ? [["Go on", "danger", () => send(Object.assign({}, m.command, { force: true }))], ["Cancel", "", () => {}]] : [["OK", "", () => {}]]);
	}
	function onError(m) { log("error: " + m.message); V().toast(m.message); }

	/* ================= the host: what the mockup hands to the machine ================= */
	const kitNameOf = k => V().kitName(k);
	const host = window.MMHost = {
		ownsClock: true,	// the firmware is the sequencer: no local clock
		engineLabels: { sync: ["READING MACHINE", "blink"], unsupported: ["NOT OS 1.32B", "off"] },
		notes: {
			ccSource: "On-screen knob: it drives its targets through the editor. Your controller's knobs reach the machine through LEARN (the plug-in's MIDI learn).",
			appSource: "Runs in the plug-in on the machine's own steps and sends CCs, at most 300 a second, also with the editor closed. A lock wins on its step. Targets on MIDI tracks are not moved."
		},
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
		history() { const h = machine?.history; return { undo: h?.undoCount || 0, redo: h?.redoCount || 0 }; },
		togglePlay() { if (V().engReady()) send({ op: V().playing() ? "stop" : "play" }); },
		selectPattern(p, nowFlag) { send({ op: "select", p, now: !!nowFlag }, { onResult: r => r.note && V().toast(r.note) }); },
		kit(op, k) {
			const ck = cur("kit"), edited = V().kitState() === "edited";
			if (op === "save") {
				V().setKitSlot(ck, { name: V().workName(), empty: false, data: V().captureKit() });	// the page's copy of the slot, at once
				sync();	// working kit edits first
				send({ op: "saveKit", k: ck }, { onResult: r => r.ok || V().toast(r.errors[0]) });
				return;
			}
			if (op === "reload") {
				if (!edited) { V().toast(kitNameOf(ck) + " matches its saved slot. Nothing to reload."); return; }
				V().ask(`Reload <b>${kitNameOf(ck)}</b> from the machine? Your edits go to its UNDO KIT.`,
					[["Reload (discard edits)", "danger", () => { send({ op: "loadKit", k: ck }); V().toast("Reloading " + kitNameOf(ck) + " (LOAD KIT)."); V().drawLib(true); }],
						["Cancel", "", () => V().drawLib(true)]]);
				return;
			}
			if (op === "load") {
				if (k === ck) { if (edited) host.kit("reload", k); else V().toast(kitNameOf(k) + " is already the current kit."); return; }
				const go = () => { send({ op: "loadKit", k }); V().toast("Loading " + kitNameOf(k) + " (LOAD KIT)."); V().drawLib(true); };
				if (edited) {
					V().ask(`Load <b>${kitNameOf(k)}</b>? Your edits to <b>${kitNameOf(ck)}</b> are not saved on the machine. They go to its UNDO KIT.`,
						[["Save kit, then load", "cream", () => { host.kit("save"); go(); }], ["Load (edits to UNDO KIT)", "danger", go], ["Cancel", "", () => V().drawLib(true)]]);
					return;
				}
				go();
				return;
			}
			if (op === "saveAs") {
				if (k === ck) { V().kitSave(); return; }
				const go = () => { sync(); send({ op: "saveKit", k }); V().toast("Saving as " + kitNameOf(k) + " (SAVE KIT). It becomes the current kit."); V().drawLib(true); };
				if (!V().kitSlot(k).empty) {
					V().ask(`Overwrite <b>${kitNameOf(k)}</b> with the current kit <b>${kitNameOf(ck)}</b>? The machine keeps the overwritten kit in its UNDO KIT.`,
						[["Overwrite", "danger", go], ["Cancel", "", () => V().drawLib(true)]]);
					return;
				}
				go();
			}
		},
		/* the machine's tempo: the machine document's tempo is shown again once this is answered */
		tempo(bpm) {
			tempoInFlight = send({ op: "tempo", bpm }, { key: "tempo", onResult: r => { if (r.id === tempoInFlight) tempoInFlight = 0; } });
		},
		mutes() {
			/* mutes and solos of the synth tracks: the plug-in's mute parameters */
			for (let i = 0; i < 6; i++) {
				const off = !V().audible(i);
				if (off !== last.mute[i]) { last.mute[i] = off; send({ op: "mute", t: i, on: off ? 1 : 0 }); }
			}
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
			const what = V().mode() === "multi" ? "MULTI TRIG" : V().mode() === "map" ? "MULTI MAP" : "T" + (V().asgT() + 1);
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
		/* the Control workspace's LFO and Random sources and their links: the plug-in runs them */
		modulators(setup) { sendMods(setup); },
		engine(kind) { send({ op: "engine", kind }, { onResult: r => V().toast(r.ok ? r.note : r.errors[0]) }); },
		firstRun() {
			log("NO ROM dialog shown (lifecycle " + machine?.lifecycle + ")");
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
			const st = V().engineState(), on = st === "boot" || st === "sync" || st === "loading";
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
			const r = machine?.recv, manual = machine?.capabilities?.dumps === "manual";
			const on = r && (r.sending > 0 || r.state === "entering" || r.state === "parked");
			p.textContent = manual && r?.sending ? "SEND " + r.sending : on ? (r.sending > 0 ? "RECV " + r.sending : "RECV") : "";
			p.className = manual && r?.sending ? "pst warn" : "pst";
			p.title = manual && r?.sending ? machine.capabilities.reasons?.recvSession || ""
				: on ? "The machine is on GLOBAL › SYSEX RECV and takes the edits (" + r.state + ", " + r.received + " received)." : "";
		},
		/* the AUDIO / MIDI panel: the plug-in's devices (mdAudioMidiLink.cpp); in a plug-in the
		   document says standalone false, and the engine menu has no entry for it */
		audioDoc() { return audioDocument; },
		audioSend(c) { send(Object.assign({ op: "audioSet" }, c)); },
		audioMeter(on) { send({ op: "audioMeter", on: !!on }); }
	};

	/* What the page holds, read-only, for whoever looks (a diagnostics build's self-tests register
	   here; the page does not know them): copies of the documents, and a call once the machine is
	   ready. */
	window.MMPage = {
		inspect() {
			return { machine: copy(machine), workingKit: copy(working), learn: copy(learn), catalogue: copy(catalogue),
				doc: (kind, slot) => copy(DOCS[kind]?.[slot]), slots: kind => DOCS[kind] ? DOCS[kind].map((d, s) => d ? s : -1).filter(s => s >= 0) : [] };
		},
		whenReady(f) { if (last.ready) f(); else readyHooks.push(f); }
	};

	/* ---------------- start: after the mockup and MmConvert are there ---------------- */
	function init() {
		const v = V();
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
		v.startEmpty();
		for (let i = 0; i < 6; i++) last.mute[i] = !v.audible(i);
		markReading();
		document.addEventListener("pointerup", () => { noteOff(); if (joyOn) { joyOn = false; joySend(0, 0); } }, true);
		document.addEventListener("pointercancel", () => noteOff(), true);
		document.addEventListener("pointerdown", () => setTimeout(() => {
			const lt = v.learnTarget();
			if (!lt) return;
			const k = lt.t + "|" + lt.pid;
			if (k === learnPid) return;
			learnPid = k;
			const [pg, i] = lt.pid.split(".");
			if (lt.t >= 6 || pg === "MID") { v.toast("MIDI page values are NRPN on the machine: the plug-in's MIDI learn cannot map them."); v.clearLearnTarget(); return; }
			send({ op: "learnStart", t: lt.t, pg: PAGES.indexOf(pg), i: +i }, { onResult: r => r.ok ? v.toast("Move a knob on your controller, or press 1-8 for CC 21-28.") : v.toast(r.errors[0]) });
		}, 0), true);
		document.addEventListener("click", e => {
			if (e.target.closest("#learnkey")) setTimeout(() => { if (!v.learning()) { learnPid = null; send({ op: "learnCancel" }); } }, 0);
		}, true);
		/* the editor's menu (skins, GUI scale, settings): right-click an empty part of the header */
		document.addEventListener("contextmenu", e => {
			if (!e.target.closest(".top") || e.target.closest("button,[role=slider],[role=button],select,input,b,.lcdpanel")) return;
			e.preventDefault();
			send({ op: "openMenu" });
		});
		addEventListener("error", e => log("page error: " + e.message + " at " + (e.filename || "").split("/").pop() + ":" + e.lineno));
		Bridge.onMessage(m => {
			try { onMessage(m); } catch (e) { log("page error in " + m.type + ": " + e.message + " " + (e.stack || "").split("\n")[0]); }
		});
		setInterval(() => {
			applyPending();
			if (libDirty && !v.busy()) { libDirty = false; v.drawLib(); }
		}, 120);
		v.setAudioEntry(false);
		v.setEng("loading");
		send({ op: "audio" });
		Bridge.ready();
	}

	function onMessage(m) {
		if (m.type === "doc") onDoc(m);
		else if (m.type === "machine") onMachine(m.doc);
		else if (m.type === "tel") { if (m.playing !== V().playing()) V().setPlaying(m.playing); if (V().playing()) V().setStep(m.step); }
		else if (m.type === "lcd") { drawLcd(m.bits); showEngine(); }
		else if (m.type === "mod") onMod(m);
		else if (m.type === "catalogue") catalogue = m.doc;
		else if (m.type === "reset") onReset();
		else if (m.type === "audio") { audioDocument = m.doc; V().setAudioEntry(!!(audioDocument && audioDocument.standalone)); if (window.AP?.open) drawAudio(); }
		else if (m.type === "audioLevel") audioLevel(m.in);
		else if (m.type === "openAudio") openAudio();
		else if (m.type === "learn") onLearn(m.doc);
		else if (m.type === "ask") onAsk(m);
		else if (m.type === "error") onError(m);
		else if (m.type === "result" && !m.ok && m.errors?.length && m.op !== "set" && m.op !== "modSet") V().toast(m.errors[0]);
	}
})();
