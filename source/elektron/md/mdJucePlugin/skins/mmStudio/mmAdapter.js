"use strict";
/* Monomachine Editor: the approved mockup (mmMockup.js) on the real machine.
   The mockup is the UI and keeps its state in `S`. This file
   - fills `S` from the firmware's documents (via MmConvert) and the machine
     state the desk publishes (mmDesk, through Bridge),
   - turns the user's edits back into whole documents: after every edit the
     page's state is converted and compared with what the machine holds, and
     what differs is sent ("set"); the desk delivers it (CC and NRPN live, dumps
     on SYSEX RECV) and publishes the firmware's read-back,
   - replaces the mockup's example engine, clock, transport and library actions
     with the real commands, and shows the firmware's own LCD while it starts.
   Mockup functions it replaces or calls go through MOCK(), so the
   skin sync script (doc/modern-ux/sync-mmstudio-skin.py) can check they exist. */
(() => {
	const MOCK = name => {
		const f = window[name];
		if (typeof f !== "function") throw new Error("mmAdapter: the mockup has no " + name + "()");
		return f;
	};
	const C = MmConvert;
	const J = o => JSON.stringify(o, (k, v) => v === Infinity ? "inf" : v);
	const now = () => performance.now();
	const selfTest = /[?&]selftest=1/.test(location.search);
	const cpuTest = /[?&]selftest=mmcpu/.test(location.search);

	/* ---------------- what the machine holds ---------------- */
	/* The engine is unknown until the plug-in says (P5): not "missing", which opened the NO ROM dialog
	   before the first report. */
	const FW = { pat: [], kit: [], song: [], glob: [], machine: null, engine: "loading", cat: null };
	/* per slot: the JSON of the document the page's state converts to when it
	   shows the machine's version, i.e. "nothing to send" */
	const BASE = { pat: [], kit: [], song: [], glob: [] };
	const REF = { pat: [], kit: [], kitName: [] };	// S.patData / S.kits objects last seen
	const CUR = { pat: -1, kit: -1, song: 0, glob: 0 };
	const last = { edit: 0, kitEdit: 0, tempoEdit: -1e9, mute: [], bpm: null, ready: false, note: "", noteMs: 0 };
	let synced = false;	// S shows the machine (current pattern and working kit arrived)
	const wantApply = new Set();

	const send = (msg, opt) => Bridge.send(msg, opt);
	const log = t => Bridge.log(t);
	/* The page's first render, logged so a blank page fails the self-tests (P5). */
	setTimeout(() => { const a = document.querySelector(".app"), r = a ? a.getBoundingClientRect() : { width: 0, height: 0 };
		log(`first render: ${document.querySelectorAll("#main *").length} elements in #main, page ${Math.round(r.width)} x ${Math.round(r.height)}, window ${innerWidth} x ${innerHeight}`); }, 1500);

	/* ---------------- start empty, not with the mockup's example ---------------- */
	const { applyKit, applyPat, emptyPat, clearedKit, captureKit, capturePat, setKitState, render, renderTop, drawLib, toast, ask, setEng } =
		Object.fromEntries(["applyKit", "applyPat", "emptyPat", "clearedKit", "captureKit", "capturePat", "setKitState", "render", "renderTop",
			"drawLib", "toast", "ask", "setEng"].map(n => [n, MOCK(n)]));
	applyKit({ ...clearedKit(), multi: S.multi });
	S.tracks.forEach(t => t.name = machName(t.m));
	applyPat(emptyPat(16));
	S.song = [{ type: "end" }];
	S.songSel = 0;
	S.kits = Array.from({ length: 128 }, () => ({ name: "", empty: true, data: null }));
	S.patInfo = Array.from({ length: 128 }, () => ({ has: false, len: 16 }));
	S.patKit = Array(128).fill(0);
	S.patData = {};
	S.workName = "";
	S.kit = 0; S.pat = 0; S.queued = null; S.playing = false; S.step = -1;
	S.ctl.links = [];	// the Control workspace starts without the mockup's example mappings
	S.kits.forEach((k, i) => { REF.kit[i] = k; REF.kitName[i] = ""; });
	S.tracks.forEach((t, i) => last.mute[i] = !!t.mute);

	/* ---------------- undo covers the current pattern, kit and song, not the library ----------------
	   The library (other slots) comes from the machine in the background; undoing
	   it would write stale slots back. The machine's own UNDO KIT still works. */
	const snap0 = MOCK("snap");
	window.snap = function () {
		const o = JSON.parse(snap0());
		for (const k of ["kits", "patData", "patInfo", "patKit", "kit", "workName"]) delete o[k];
		return JSON.stringify(o);
	};
	H.undo = []; H.redo = []; H.last = snap();

	/* ---------------- no local clock: the firmware is the sequencer ---------------- */
	window.tick = () => {};
	window.restartClock = () => {};
	window.startEngine = () => {};
	window.togglePlay = function () {
		if (!MOCK("engReady")()) return;
		send({ op: S.playing ? "stop" : "play" });
	};
	function showStep(step) {
		const prev = S.step;
		S.step = step;
		/* the Control workspace's LFO and Random sources move with the machine's steps */
		MOCK("ctlTick")();
		if (S.ctl.links.length) scheduleSync();
		const pp = Math.floor(step / 16);
		$$(".pl").forEach(b => b.classList.toggle("play", +b.dataset.plp === pp && S.playing));
		if (S.follow && S.ws === "seq" && !S.viewAll && pp !== S.page && !busy()) { S.page = pp; render(); }
		$("#tempoled")?.classList.toggle("on", step % 4 === 0);
		MOCK("setPos")();
		$$(`.mst[data-s="${prev}"],.lb[data-s="${prev}"],.tc[data-s="${prev}"]`).forEach(c => c.classList.remove("ph"));
		$$(`.mst[data-s="${step}"],.lb[data-s="${step}"],.tc[data-s="${step}"]`).forEach(c => c.classList.add("ph"));
		if (S.ws === "seq") MOCK("redraw")();
		if (S.ws === "perform") {
			const act = [0, 1, 2, 3, 4, 5].filter(i => { const st = S.tracks[i].steps[step]; return st && !st.off && st.a && MOCK("audible")(i); });
			MOCK("flashTracks")(act);
		}
	}
	function setPlaying(on) {
		if (S.playing === on) return;
		S.playing = on;
		if (!on) {
			$$(".pl").forEach(b => b.classList.remove("play"));
			$("#tempoled")?.classList.remove("on");
			$$(".ph").forEach(c => c.classList.remove("ph"));
			S.step = -1;
		}
		MOCK("setPos")();
		renderTop();
		MOCK("redraw")();
	}

	/* ---------------- patterns: the machine switches ---------------- */
	window.queuePattern = p => send({ op: "select", p }, { onResult: r => r.note && toast(r.note) });
	window.switchNow = function (p) {
		if (!S.playing) { send({ op: "select", p }); return; }
		/* the machine only switches at the pattern end: STOP, LOAD PATTERN, PLAY */
		send({ op: "stop" });
		send({ op: "select", p });
		send({ op: "play" });
	};
	window.applyPattern = p => window.queuePattern(p);

	/* ---------------- kits: LOAD / SAVE are the machine's ---------------- */
	const saveKit0 = MOCK("saveKit");
	window.saveKit = function () {
		saveKit0();	// the page's copy of the slot, at once
		REF.kit[S.kit] = S.kits[S.kit];
		sync();	// working kit edits first
		send({ op: "saveKit", k: S.kit }, { onResult: r => r.ok || toast(r.errors[0]) });
	};
	window.kitLoad = function (k) {
		if (k === S.kit) { if (S.kitState === "edited") window.kitReload(); else toast(kitName(k) + " is already the current kit."); return; }
		const go = () => { send({ op: "loadKit", k }); relinkLater(); toast("Loading " + kitName(k) + " (LOAD KIT)."); drawLib(true); };
		if (S.kitState === "edited") {
			ask(`Load <b>${kitName(k)}</b>? Your edits to <b>${kitName(S.kit)}</b> are not saved on the machine. They go to its UNDO KIT.`,
				[["Save kit, then load", "cream", () => { window.saveKit(); go(); }], ["Load (edits to UNDO KIT)", "danger", go], ["Cancel", "", () => drawLib(true)]]);
			return;
		}
		go();
	};
	window.kitSaveAs = function (k) {
		if (k === S.kit) { MOCK("kitSave")(); return; }
		const go = () => { sync(); send({ op: "saveKit", k }); relinkLater(); toast("Saving as " + kitName(k) + " (SAVE KIT). It becomes the current kit."); drawLib(true); };
		if (!S.kits[k].empty) {
			ask(`Overwrite <b>${kitName(k)}</b> with the current kit <b>${kitName(S.kit)}</b>? The machine keeps the overwritten kit in its UNDO KIT.`,
				[["Overwrite", "danger", go], ["Cancel", "", () => drawLib(true)]]);
			return;
		}
		go();
	};
	window.kitReload = function () {
		if (S.kitState !== "edited") { toast(kitName(S.kit) + " matches its saved slot. Nothing to reload."); return; }
		ask(`Reload <b>${kitName(S.kit)}</b> from the machine? Your edits go to its UNDO KIT.`,
			[["Reload (discard edits)", "danger", () => { send({ op: "loadKit", k: S.kit }); toast("Reloading " + kitName(S.kit) + " (LOAD KIT)."); drawLib(true); }],
				["Cancel", "", () => drawLib(true)]]);
	};
	/* LOAD KIT and SAVE KIT relink the current pattern to the kit: read it back */
	function relinkLater() { setTimeout(() => CUR.pat >= 0 && send({ op: "load", kind: "pattern", slot: CUR.pat }), 400); }

	/* ---------------- the ROM ---------------- */
	window.firstRun = function () {
		log("NO ROM dialog shown (engine " + FW.engine + ")");
		ask(`<div class="lcdbig">MONOMACHINE FIRMWARE NEEDED</div>
 <p>Monomachine Editor runs the real Monomachine operating system. Elektron's firmware cannot ship with the plug-in, so you add the one from your own machine.</p>
 <ol class="recvsteps"><li>Dump the <b>OS 1.32B</b> flash image from your Monomachine (<span class="mono">.bin</span>).</li><li>Put it in the plug-in's ROM folder (<b>Show ROM folder</b>).</li><li>Reopen the plug-in. It stays on this computer only.</li></ol>`,
			[["Show ROM folder", "cream", () => send({ op: "revealRomFolder" })],
				["Check again", "", () => send({ op: "recheckFirmware" }, { onResult: r => { toast(r.ok ? r.note : r.errors[0]); if (!r.ok) setTimeout(window.firstRun, 50); } })], ["Close", "", () => {}]], "first");
	};

	/* ---------------- engine and the firmware's LCD ---------------- */
	ENG.sync = ["READING MACHINE", "blink"];
	ENG.unsupported = ["NOT OS 1.32B", "off"];
	let lcdFade = 0;
	window.bootScreen = function () {
		const el = $("#bootscr");
		if (!el) return;
		const on = S.eng === "boot" || S.eng === "sync" || S.eng === "loading";
		clearTimeout(lcdFade);
		if (on) { el.classList.remove("fading"); el.classList.add("on", "fw"); return; }
		if (!el.classList.contains("on")) return;
		/* cross-fade from the machine's own screen to the editor's fields */
		el.classList.add("fading");
		lcdFade = setTimeout(() => el.classList.remove("on", "fw", "fading"), 460);
	};
	function drawLcd(hex) {
		const el = $("#bootscr");
		if (!el || hex.length < 2048) return;
		let c = el.querySelector("canvas.fwlcd");
		if (!c) { c = document.createElement("canvas"); c.className = "fwlcd"; c.width = 128; c.height = 64; el.appendChild(c); }
		const g = c.getContext("2d"), cs = getComputedStyle(el);
		g.fillStyle = cs.getPropertyValue("--lcd").trim() || "#b7c79a";
		g.fillRect(0, 0, 128, 64);
		g.fillStyle = cs.getPropertyValue("--ink").trim() || "#1d2a1a";
		for (let y = 0; y < 64; y++)
			for (let xb = 0; xb < 16; xb++) {
				const b = parseInt(hex.substr((y * 16 + xb) * 2, 2), 16);
				if (b) for (let k = 0; k < 8; k++) if (b & (0x80 >> k)) g.fillRect(xb * 8 + k, y, 1, 1);
			}
	}
	let noRomShown = false, noRomAt = 0;
	function showEngine() {
		const e = FW.engine;
		const st = e === "missing" ? "norom" : e === "unsupported" ? "unsupported" : e === "loading" ? "loading" : e === "booting" ? "boot"
			: synced ? "ready" : "sync";
		if (S.eng !== st) setEng(st);
		/* NO ROM only when the plug-in has said so for 1.5 s (a device being made or replaced is not a
		   missing ROM), and the dialog closes by itself as soon as the engine is anything else. */
		if (st === "norom") { if (!noRomAt) noRomAt = performance.now(); if (!noRomShown && performance.now() - noRomAt > 1500) { noRomShown = true; window.firstRun(); } else if (!noRomShown) setTimeout(showEngine, 1600); }
		else { noRomAt = 0; noRomShown = false; const d = $("#dlg"); if (d && !d.hidden && /FIRMWARE NEEDED/.test(d.textContent)) d.hidden = true; }
		if (st === "ready" && !last.ready) { last.ready = true; log("ready: pattern " + CUR.pat + " kit " + CUR.kit); if (selfTest) setTimeout(runSelfTest, 500); if (cpuTest) runCpuPhases(); }
	}

	/* ---------------- documents -> the page ---------------- */
	const kitPageOf = k => k === CUR.kit && synced ? captureKit() : S.kits[k]?.data || (FW.kit[k] ? C.kitToPage(FW.kit[k], FW.glob[CUR.glob]) : null);
	const lenOf = p => p === S.pat && synced ? S.len : FW.pat[p]?.length ?? S.patInfo[p].len;
	function patDoc(p) {
		const b = FW.pat[p] || FW.pat[CUR.pat] || FW.pat.find(Boolean);
		if (!b) return null;
		const data = p === CUR.pat ? capturePat() : S.patData[p];
		return data ? C.patternToFw(data, b, S.patKit[p], kitPageOf(S.patKit[p]), p) : null;
	}
	function kitDoc(k) {
		const b = FW.kit[k] || FW.kit[CUR.kit] || FW.kit.find(Boolean);
		if (!b) return null;
		if (k === CUR.kit) return C.kitToFw(captureKit(), b, S.workName, k);
		const s = S.kits[k];
		return C.kitToFw(s.data || clearedKit(), b, s.empty ? "" : s.name, k);
	}
	const songDoc = () => FW.song[CUR.song] ? C.songToFw(S.song, FW.song[CUR.song], lenOf) : null;
	const globDoc = () => FW.glob[CUR.glob] ? C.globalToFw(FW.glob[CUR.glob], S.routing, S.midi) : null;

	function setSlotPattern(p) {
		const d = FW.pat[p];
		if (p === CUR.pat) return;
		S.patData[p] = C.patternToPage(d, kitPageOf(d.kit));
		REF.pat[p] = S.patData[p];
		S.patInfo[p] = { has: C.hasTrigs(d), len: d.length };
		S.patKit[p] = d.kit;
		BASE.pat[p] = J(patDoc(p));
	}
	function setSlotKit(k) {
		const d = FW.kit[k];
		if (k === CUR.kit) return;
		S.kits[k] = { name: C.kitName(d), empty: C.kitEmpty(d), data: C.kitToPage(d, FW.glob[CUR.glob]) };
		REF.kit[k] = S.kits[k]; REF.kitName[k] = S.kits[k].name;
		BASE.kit[k] = J(kitDoc(k));
	}
	function applyCurrentKit() {
		const d = FW.kit[CUR.kit];
		const page = C.kitToPage(d, FW.glob[CUR.glob]);
		page.multi = S.multi;
		applyKit(page);
		S.workName = C.kitName(d);
		S.kits[CUR.kit] = { name: S.workName, empty: false, data: C.kitToPage(d, FW.glob[CUR.glob]) };
		REF.kit[CUR.kit] = S.kits[CUR.kit]; REF.kitName[CUR.kit] = S.workName;
		BASE.kit[CUR.kit] = J(kitDoc(CUR.kit));
		if (FW.machine) setKitState(FW.machine.kit.working === "edited" ? "edited" : "clean");
	}
	function applyCurrentPattern() {
		const d = FW.pat[CUR.pat];
		applyPat(C.patternToPage(d, captureKit()));
		S.patKit[CUR.pat] = d.kit;
		S.patInfo[CUR.pat] = { has: C.hasTrigs(d), len: d.length };
		BASE.pat[CUR.pat] = J(patDoc(CUR.pat));
	}
	function applyCurrentSong() {
		S.song = C.songToPage(FW.song[CUR.song], lenOf);
		S.songSel = Math.min(S.songSel || 0, S.song.length - 1);
		BASE.song[CUR.song] = J(songDoc());
	}
	function applyCurrentGlobal() {
		const g = FW.glob[CUR.glob];
		S.routing = g.routingMode;
		S.midi.forEach((x, t) => { x.ch = g.midiSeq.channels[t] + 1; x.cc = [...g.midiSeq.ccs[t]]; });
		mapFromGlobal();
		BASE.glob[CUR.glob] = J(globDoc());
	}

	/* the user is in the middle of something: a drag, a dialog, a name */
	function busy() {
		try {
			if (drag || laneDraw || rollDrag || active || arpDrag || l2drag || joyDrag || splitDrag || cord || kbDown) return true;
		} catch (e) { /* a mockup without one of them */ }
		return !$("#dlg").hidden || LIB.renaming != null || LIB.drag != null;
	}
	/* Apply what the machine holds, unless the user is editing or has just edited
	   (then the machine's copy is our own edit on its way, or it is applied later). */
	function applyPending() {
		if (!wantApply.size) return;
		if (FW.pat[CUR.pat] == null || FW.kit[CUR.kit] == null) return;
		if (synced && (busy() || now() - last.edit < 700)) return;
		let changed = false;
		const kinds = [...wantApply];
		wantApply.clear();
		/* the kit first: the pattern's locks read their machines from it */
		if (kinds.includes("kit")) {
			const incoming = J(C.kitToFw(C.kitToPage(FW.kit[CUR.kit], FW.glob[CUR.glob]), FW.kit[CUR.kit], C.kitName(FW.kit[CUR.kit])));
			if (!synced || incoming !== J(kitDoc(CUR.kit))) { applyCurrentKit(); changed = true; } else BASE.kit[CUR.kit] = incoming;
			if (FW.machine) setKitState(FW.machine.kit.working === "edited" ? "edited" : "clean");
		}
		if (kinds.includes("pat") || (changed && FW.pat[CUR.pat])) {
			const d = FW.pat[CUR.pat];
			const incoming = J(C.patternToFw(C.patternToPage(d, captureKit()), d, d.kit, captureKit(), CUR.pat));
			if (!synced || incoming !== J(patDoc(CUR.pat))) { applyCurrentPattern(); changed = true; } else BASE.pat[CUR.pat] = incoming;
		}
		if (kinds.includes("song") && FW.song[CUR.song]) {
			const incoming = J(C.songToFw(C.songToPage(FW.song[CUR.song], lenOf), FW.song[CUR.song], lenOf));
			if (!synced || incoming !== J(songDoc())) { applyCurrentSong(); changed = true; } else BASE.song[CUR.song] = incoming;
		}
		if (kinds.includes("glob") && FW.glob[CUR.glob]) {
			const incoming = J(C.globalToFw(FW.glob[CUR.glob], FW.glob[CUR.glob].routingMode,
				FW.glob[CUR.glob].midiSeq.channels.map((c, t) => ({ ch: c + 1, cc: FW.glob[CUR.glob].midiSeq.ccs[t] }))));
			if (!synced || incoming !== J(globDoc())) { applyCurrentGlobal(); changed = true; } else BASE.glob[CUR.glob] = incoming;
		}
		if (!synced) {
			synced = true;
			S.tracks.forEach((t, i) => last.mute[i] = !MOCK("audible")(i));
		}
		if (changed) {
			MOCK("autoRange")(S.sel);
			render();
			drawLib();
			H.last = snap();
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
	}
	let libDirty = false;

	function onMachine(d) {
		FW.machine = d;
		FW.engine = d.engine;
		const cp = d.pattern.current, ck = d.kit.current, cs = d.song.current ?? 0, cg = d.global ?? 0;
		if (ck != null && ck !== CUR.kit) {
			const old = CUR.kit;
			CUR.kit = ck; S.kit = ck;
			/* the old slot holds what was saved, not the edits that were left: read it */
			if (old >= 0) send({ op: "load", kind: "kit", slot: old });
			if (FW.kit[ck]) wantApply.add("kit");
			H.undo = []; H.redo = [];
		}
		if (cp != null && cp !== CUR.pat) {
			const old = CUR.pat;
			CUR.pat = cp; S.pat = cp;
			if (old >= 0 && FW.pat[old]) setSlotPattern(old);
			if (FW.pat[cp]) wantApply.add("pat");
			H.undo = []; H.redo = [];
		}
		if (cs !== CUR.song) { CUR.song = cs; if (FW.song[cs]) wantApply.add("song"); }
		if (cg !== CUR.glob) { CUR.glob = cg; if (FW.glob[cg]) wantApply.add("glob"); }
		const q = d.pattern.queued;
		const queued = q != null && q !== cp ? q : null;
		if (queued !== S.queued) { S.queued = queued; renderTop(); drawLib(); }
		setPlaying(!!d.playing);
		/* the machine's tempo (RAM), unless the user is setting it */
		if (d.tempo != null && now() - last.tempoEdit > 1500 && d.tempo !== S.bpm) { S.bpm = d.tempo; last.bpm = d.tempo; renderTop(); }
		if (synced && !busy() && now() - last.edit > 700) setKitState(d.kit.working === "edited" ? "edited" : "clean");
		renderPst();
		if (d.error && d.error !== last.error) { last.error = d.error; toast(d.error); }
		showEngine();
		/* the library fills in the background: say how far */
		const chip = document.querySelector(".lcdeng span");
		if (chip && S.eng === "ready") chip.textContent = d.loading.done < d.loading.total ? `EMU OS 1.32B · ${d.loading.done}/${d.loading.total}` : ENG.ready[0];
	}

	/* ---------------- the page -> the machine ---------------- */
	/* Everything the user can change converts to documents; what differs from the
	   machine's version is sent. Called shortly after every edit. */
	let syncT = 0;
	function scheduleSync() { last.edit = now(); if (!syncT) syncT = setTimeout(() => { syncT = 0; sync(); }, 40); }
	function sendDoc(kind, doc, base, i) {
		const text = J(doc);
		if (text === base[i]) return false;
		base[i] = text;
		send({ op: "set", kind, doc }, { key: kind + ":" + i, onResult: r => { if (!r.ok) { toast(r.errors[0] || "The machine did not take it."); log("set " + kind + " " + i + ": " + r.errors.join("; ")); } else note(r.note); } });
		return true;
	}
	function note(t) {
		if (!t) return;
		if (t === last.note && now() - last.noteMs < 8000) return;
		last.note = t; last.noteMs = now();
		toast(t);
	}
	function sync() {
		if (!synced || !MOCK("engReady")()) return;
		/* the working kit: live (CC, NRPN, machine, routing, name) */
		const kd = kitDoc(CUR.kit);
		if (kd && sendDoc("kit", kd, BASE.kit, CUR.kit)) last.kitEdit = now();
		/* the library wrote the current kit's slot (paste, clear): store it too */
		if (S.kits[CUR.kit] !== REF.kit[CUR.kit]) {
			REF.kit[CUR.kit] = S.kits[CUR.kit];
			send({ op: "saveKit", k: CUR.kit });
		}
		const pd = patDoc(CUR.pat);
		if (pd) sendDoc("pattern", pd, BASE.pat, CUR.pat);
		/* other slots the library changed */
		for (let p = 0; p < 128; p++) {
			if (p === CUR.pat || S.patData[p] === REF.pat[p]) continue;
			REF.pat[p] = S.patData[p];
			const d = patDoc(p);
			if (d) sendDoc("pattern", d, BASE.pat, p);
		}
		for (let k = 0; k < 128; k++) {
			if (k === CUR.kit || (S.kits[k] === REF.kit[k] && S.kits[k].name === REF.kitName[k])) continue;
			REF.kit[k] = S.kits[k]; REF.kitName[k] = S.kits[k].name;
			const d = kitDoc(k);
			if (d) sendDoc("kit", d, BASE.kit, k);
		}
		const sd = songDoc();
		if (sd) sendDoc("song", sd, BASE.song, CUR.song);
		const gd = globDoc();
		if (gd) sendDoc("global", gd, BASE.glob, CUR.glob);
		/* mutes: the plug-in's mute parameters */
		/* mutes and solos of the synth tracks: the plug-in's mute parameters */
		S.tracks.forEach((t, i) => {
			const off = !MOCK("audible")(i);
			if (off !== last.mute[i]) { last.mute[i] = off; send({ op: "mute", t: i, on: off ? 1 : 0 }); }
		});
		if (last.bpm !== S.bpm) {
			if (last.bpm != null) { send({ op: "tempo", bpm: S.bpm }, { key: "tempo" }); last.tempoEdit = now(); }
			last.bpm = S.bpm;
		}
	}
	/* every path by which the mockup records an edit */
	for (const name of ["commit", "structEdited", "soundEdited", "tx"]) {
		const f = MOCK(name);
		window[name] = function (...a) { const r = f.apply(this, a); scheduleSync(); return r; };
	}
	for (const name of ["undo", "redo"]) {
		const f = MOCK(name);
		window[name] = function (...a) { const r = f.apply(this, a); scheduleSync(); return r; };
	}
	/* SYSEX RECV state in the pattern field */
	window.renderPst = function () {
		const p = $("#pst");
		if (!p) return;
		const r = FW.machine?.recv;
		const on = r && (r.sending > 0 || r.state === "entering" || r.state === "parked");
		p.textContent = on ? (r.sending > 0 ? "RECV " + r.sending : "RECV") : "";
		p.className = "pst";
		p.title = on ? "The machine is on GLOBAL › SYSEX RECV and takes the edits (" + r.state + ", " + r.received + " received)." : "";
	};
	const renderPst = window.renderPst;


	/* ================= MM-P3: every control real, or disabled with the reason ================= */

	/* ---- the keyboard and the joystick play the machine (MIDI into the plug-in) ---- */
	const chan = () => FW.glob[CUR.glob]?.channels;
	function keyChannel() {
		const c = chan();
		if (!c) return null;
		const ch = S.mode === "multi" ? c.multiTrig : S.mode === "map" ? c.multiMap : c.base + asgT();
		return ch >= 0 && ch < 16 ? ch : null;
	}
	const trackChannel = t => { const c = chan(); const ch = c ? c.base + t : -1; return ch >= 0 && ch < 16 ? ch : null; };
	const midi = (b, key) => send({ op: "midi", b }, key ? { key } : {});
	let held = null;
	function noteOff() { if (held) { midi([0x80 | held.ch, held.n, 0]); held = null; } }
	window.playKey = function (n) {
		$$(".kb .dn").forEach(k => k.classList.remove("dn"));
		$(`.kb [data-key="${n}"]`)?.classList.add("dn");
		const ch = keyChannel(), info = $("#kbinfo");
		if (ch == null) { if (info) info.textContent = "That MIDI channel is OFF in the global (GLOBAL › MIDI › CHANNELS)."; return; }
		if (held && held.n === n && held.ch === ch) return;
		noteOff();
		midi([0x90 | ch, n, 100]);
		held = { ch, n };
		const what = S.mode === "multi" ? "MULTI TRIG" : S.mode === "map" ? "MULTI MAP" : "T" + (asgT() + 1);
		if (info) info.textContent = `${what} · ${noteName(n)} · MIDI channel ${ch + 1}`;
	};
	document.addEventListener("pointerup", () => { noteOff(); if (joyOn) { joyOn = false; joySend(0, 0); } }, true);
	document.addEventListener("pointercancel", () => noteOff(), true);
	let joyOn = false;
	function joySend(x, y) {
		const ch = trackChannel(asgT());
		if (ch == null) return;
		const pb = Math.max(0, Math.min(16383, Math.round(8192 + x * 8191)));
		midi([0xe0 | ch, pb & 0x7f, pb >> 7], "joyx");
		midi([0xb0 | ch, 1, Math.round(Math.max(0, y) * 127)], "joyu");
		midi([0xb0 | ch, 2, Math.round(Math.max(0, -y) * 127)], "joyd");
	}
	const joyAt0 = MOCK("joyAt");
	window.joyAt = function (e) { joyAt0(e); joyOn = true; joySend(S.joy.x, S.joy.y); };

	/* ---- LEARN: the plug-in's MIDI learn (persistent, per plug-in) ---- */
	let learnPid = null;
	document.addEventListener("pointerdown", () => setTimeout(() => {
		if (!S.learn || !S.learnT) return;
		const k = S.learnT.t + "|" + S.learnT.pid;
		if (k === learnPid) return;
		learnPid = k;
		const [pg, i] = S.learnT.pid.split(".");
		if (S.learnT.t >= 6 || pg === "MID") { toast("MIDI page values are NRPN on the machine: the plug-in's MIDI learn cannot map them."); S.learnT = null; return; }
		send({ op: "learnStart", t: S.learnT.t, pg: PAGES.indexOf(pg), i: +i }, { onResult: r => r.ok ? toast("Move a knob on your controller, or press 1-8 for CC 21-28.") : toast(r.errors[0]) });
	}, 0), true);
	const learnBind0 = MOCK("learnBind");
	window.learnBind = function (k) {
		const lt = S.learnT;
		if (!lt) return;
		const [pg, i] = lt.pid.split(".");
		if (lt.t >= 6 || pg === "MID") { toast("MIDI page values cannot be learned."); return; }
		send({ op: "learnCancel" });
		send({ op: "learnAdd", cc: 20 + k, t: lt.t, pg: PAGES.indexOf(pg), i: +i }, { onResult: r => { if (!r.ok) toast(r.errors[0]); } });
		learnPid = null;
		learnBind0(k);	// the Control workspace's Knob k drives the same value on screen
	};
	function onLearn(doc) {
		const prev = FW.learn;
		FW.learn = doc;
		if (prev && prev.learning && !doc.learning && doc.mappings.length > prev.mappings.length) {
			const m = doc.mappings[doc.mappings.length - 1];
			toast(`Learned: CC ${m.cc} → T${m.t + 1} ${PAGES[m.pg] || ""} ${pname(m.t, PAGES[m.pg] + "." + m.i) || ""}. Stored with the plug-in.`);
			S.learnT = null; learnPid = null;
		}
	}
	document.addEventListener("click", e => {
		if (e.target.closest("#learnkey")) setTimeout(() => { if (!S.learn) { learnPid = null; send({ op: "learnCancel" }); } }, 0);
	}, true);

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
		S.mmap = rows;
		S.mmapSel = Math.min(S.mmapSel || 0, rows.length - 1);
	}

	/* ---- what the editor cannot do (yet): disabled, with the reason ---- */
	const R = {
		midiMute: "MIDI track mutes are set on the machine (FUNCTION + a track key in MIDI mode). The plug-in has no command for them yet.",
		poly: "POLY is switched on the machine. The editor does not drive it yet.",
		multi: "MULTI TRIG mode, split and timing are settings the editor does not decode yet: set them on the machine. The keys here do play on the MULTI TRIG channel.",
		map: "MULTI MAP ranges live in the global slot. The editor reads each range's upper key and pattern; offset, length, transpose and timing are not decoded yet, so edit the map on the machine (GLOBAL › CONTROL › MULTIMAP EDIT). The keys here do play on the MULTI MAP channel.",
		port: "PORTAMENTO mode (ALWAYS / ONLY LEGATO) is not decoded in the kit yet: set it on the machine.",
		rec: "GRID RECORD and LIVE RECORD run on the machine. In the editor you draw steps directly.",
		loading: "Still reading this slot from the machine."
	};
	const NA = [
		['[data-mute="6"],[data-mute="7"],[data-mute="8"],[data-mute="9"],[data-mute="10"],[data-mute="11"],[data-solo="6"],[data-solo="7"],[data-solo="8"],[data-solo="9"],[data-solo="10"],[data-solo="11"],[data-gmute="6"],[data-gmute="7"],[data-gmute="8"],[data-gmute="9"],[data-gmute="10"],[data-gmute="11"]', R.midiMute],
		['[data-pmode="poly"]', R.poly],
		['[data-set="mtmode"] button,[data-strk],[data-tim],#splitm', R.multi],
		['.maprow [data-mhi],.maprow select,.maprow .kselbtn,.maprow .pc,[data-mdel],[data-madd],[data-band]', R.map],
		['[data-set="port"] button', R.port],
		["#rec", R.rec]
	];
	function markNa() {
		for (const [sel, why] of NA)
			for (const el of $$(sel)) if (el.dataset.na !== "1") { el.dataset.na = "1"; el.title = why; el.setAttribute("aria-disabled", "true"); }
		/* library slots not read yet */
		for (const el of $$("#libpop .ps[data-ps]")) {
			const miss = !FW.pat[+el.dataset.ps];
			if (miss !== (el.dataset.na === "1")) { if (miss) { el.dataset.na = "1"; el.title = R.loading; } else delete el.dataset.na; }
		}
		for (const el of $$("#libpop .ks[data-ks]")) {
			const miss = !FW.kit[+el.dataset.ks];
			if (miss !== (el.dataset.na === "1")) { if (miss) { el.dataset.na = "1"; el.title = R.loading; } else delete el.dataset.na; }
		}
		for (const el of $$(".band[data-band]")) { const r = S.mmap[+el.dataset.band]; if (r && r.pat < 0 && el.textContent !== "CUR") el.textContent = "CUR"; }
		/* the reason in words where a whole card is the machine's */
		const card = $(".maprow");
		if (card && !card.querySelector(".statusline")) card.querySelector("header").insertAdjacentHTML("afterend", `<p class="statusline">${R.map}</p>`);
		const note = $(".songui .card:last-child header .note");
		if (note && !note.dataset.song) { note.dataset.song = "1"; note.textContent = `Song ${String(CUR.song + 1).padStart(2, "0")} (the machine's current song) · ` + note.textContent; }
		for (const el of $$(".srch")) if (!el.dataset.hint) { el.dataset.hint = "1"; el.title = "On-screen source: it drives its targets through the editor. Your controller's knobs reach the machine through LEARN (the plug-in's MIDI learn)."; }
	}
	const blockNa = e => {
		const el = e.target.closest?.("[data-na]");
		if (!el) return;
		e.preventDefault(); e.stopImmediatePropagation();
		if (e.type === "click") toast(el.title);
	};
	for (const ev of ["pointerdown", "click", "change", "wheel", "keydown", "dragstart"]) document.addEventListener(ev, blockNa, { capture: true, passive: false });
	let naQueued = false;
	new MutationObserver(() => { if (!naQueued) { naQueued = true; queueMicrotask(() => { naQueued = false; markNa(); }); } })
		.observe(document.body, { childList: true, subtree: true });

	/* ---- the editor's menu (skins, GUI scale, settings): right-click an empty part of the header ---- */
	document.addEventListener("contextmenu", e => {
		if (!e.target.closest(".top") || e.target.closest("button,[role=slider],[role=button],select,input,b,.lcdpanel")) return;
		e.preventDefault();
		send({ op: "openMenu" });
	});

	/* ---------------- messages from the plug-in ---------------- */
	Bridge.onMessage(m => {
		if (m.type === "doc") onDoc(m);
		else if (m.type === "machine") onMachine(m.doc);
		else if (m.type === "tel") { if (m.playing !== S.playing) setPlaying(m.playing); if (S.playing) showStep(m.step); }
		else if (m.type === "lcd") { if (m.engine) FW.engine = m.engine; drawLcd(m.bits); showEngine(); }
		else if (m.type === "catalogue") FW.cat = m.doc;
		else if (m.type === "learn") onLearn(m.doc);
		else if (m.type === "result" && !m.ok && m.errors?.length && m.op !== "set") toast(m.errors[0]);
		selfTestSeen(m);
	});
	setInterval(() => {
		applyPending();
		if (libDirty && LIB.open && !busy()) { libDirty = false; drawLib(); }
	}, 120);
	setEng("loading");
	Bridge.ready();

	/* ---------------- self test (GEARMULATOR_MMSTUDIO_SELFTEST=1) ----------------
	   Edits through the mockup's own functions, waits for the firmware's
	   read-back and logs each round trip to the plug-in log. */
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
		MOCK("goWs")("seq");
		await phase("stopped", 30000);
		if (!S.playing) window.togglePlay();
		await sleep(1000);
		await phase("playing-seq", 30000);
		MOCK("goWs")("mix");
		await phase("playing-mix", 30000);
		window.togglePlay();
		log2("cpu done");
	}
	async function runSelfTest() {
		const results = [];
		const check = async (name, fn) => {
			const t0 = now();
			try { const note = await fn(); results.push(true); log(`SELFTEST ok ${name} ${Math.round(now() - t0)} ms${note ? " " + note : ""}`); }
			catch (e) { results.push(false); log(`SELFTEST FAIL ${name}: ${e.message}`); }
			await sleep(1200);
		};
		const p = CUR.pat;
		const readBack = test => waitFor(m => m.type === "doc" && m.kind === "pattern" && m.slot === p && !m.pending && test(m.doc));
		const free = () => [...Array(S.len).keys()].find(s => !S.tracks[0].steps[s]);
		log(`SELFTEST start: pattern ${p} kit ${CUR.kit} song ${CUR.song} global ${CUR.glob}, loaded ${FW.machine?.loading.done}/${FW.machine?.loading.total}`);
		await check("pattern trig (SYSEX RECV round trip)", async () => {
			const s = free();
			if (s == null) throw new Error("no free step on T1");
			MOCK("clickStep")(0, s, {});
			await readBack(d => d.tracks[0].trig.includes(s) && d.tracks[0].amp.includes(s));
			MOCK("clickStep")(0, s, {});
			await readBack(d => !d.tracks[0].trig.includes(s));
			return "step " + (s + 1);
		});
		await check("trigless trig", async () => {
			const s = free();
			MOCK("clickStep")(0, s, { altKey: true });
			await readBack(d => d.tracks[0].trig.includes(s) && !d.tracks[0].amp.includes(s) && d.tracks[0].notes.some(n => n[0] === s));
			MOCK("clickStep")(0, s, {});
			await readBack(d => !d.tracks[0].trig.includes(s));
		});
		await check("pitchless trig", async () => {
			const s = free();
			S.tracks[0].steps[s] = { a: 1, f: 1, l: 1 };
			window.structEdited();
			await readBack(d => d.tracks[0].trig.includes(s) && !d.tracks[0].notes.some(n => n[0] === s));
			S.tracks[0].steps[s] = null;
			window.structEdited();
			await readBack(d => !d.tracks[0].trig.includes(s));
		});
		await check("kit value live (AMP VOL, CC)", async () => {
			const v0 = S.tracks[0].v.AMP[5], v = v0 > 60 ? v0 - 7 : v0 + 7;
			const got = waitFor(m => m.type === "doc" && m.kind === "kit" && m.working && m.doc.tracks[0].pages[1][5] === v, 5000);
			S.tracks[0].v.AMP[5] = v;
			window.soundEdited();
			await got;
			S.tracks[0].v.AMP[5] = v0;
			window.soundEdited();
			await waitFor(m => m.type === "doc" && m.kind === "kit" && m.working && m.doc.tracks[0].pages[1][5] === v0, 5000);
		});
		await check("pattern switch (LOAD PATTERN)", async () => {
			const to = (p + 1) % 128;
			window.queuePattern(to);
			await waitFor(m => m.type === "machine" && m.doc.pattern.current === to);
			await sleep(800);
			if (S.pat !== to) throw new Error("page shows " + S.pat);
			window.queuePattern(p);
			await waitFor(m => m.type === "machine" && m.doc.pattern.current === p);
		});
		await check("play and stop", async () => {
			window.togglePlay();
			await waitFor(m => m.type === "tel" && m.playing && m.step > 0);
			window.togglePlay();
			await waitFor(m => m.type === "machine" && !m.doc.playing);
		});
		await check("tempo out and read back (0x61, RAM)", async () => {
			const t0 = S.bpm, t = t0 === 133 ? 127 : 133;
			S.bpm = t; window.tx();
			await waitFor(m => m.type === "machine" && m.doc.tempo === t, 5000);
			S.bpm = t0; window.tx();
			await waitFor(m => m.type === "machine" && m.doc.tempo === t0, 5000);
			return t0 + " -> " + t + " -> " + t0 + " BPM";
		});
		await check("solo mutes the other synth tracks", async () => {
			S.tracks[2].solo = true; window.tx();
			await sleep(300);
			const muted = last.mute.join("");
			S.tracks[2].solo = false; window.tx();
			await sleep(300);
			if (muted !== "truetruefalsetruetruetrue") throw new Error("mutes " + muted);
		});
		await check("song and global documents", async () => {
			if (!FW.song[CUR.song] || !FW.glob[CUR.glob]) throw new Error("not loaded");
			return S.song.length + " rows, routing " + S.routing;
		});
		const ok = results.filter(Boolean).length;
		log(`SELFTEST ${ok === results.length ? "PASS" : "FAIL"} ${ok}/${results.length}`);
	}
})();
