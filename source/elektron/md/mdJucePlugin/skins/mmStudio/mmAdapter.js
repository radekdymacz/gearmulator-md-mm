"use strict";
/* Monomachine Editor: the plug-in's host for the approved mockup (P6).
   The mockup (mmMockup.js, copied verbatim from doc/modern-ux/mm-mockup) is the UI. It hands
   everything the machine does to its host, window.MMHost, which this file defines before the
   mockup loads: transport, patterns, the kit library, undo, tempo, mutes, the keyboard, the
   Control workspace's modulators and the AUDIO / MIDI panel. Nothing of the mockup is replaced.
   Its view is used through window.MMView only: values to read, named setters, and
   disable(capability, reason); never its state and never its markup (no DOM here:
   sync-mmstudio-skin.py checks that).
   - The plug-in publishes the core's documents (observed, or pending while an edit is on its
     way), keyed by the contract's kinds: pattern, kit (the stored slots), workingKit (the kit
     that plays), song, global; and the machine document, which says which slots are current.
     They are shown in the view through MmConvert (the pure page <-> contract translation, its
     enumerations from the catalogue).
   - An edit is sent at its gesture as the intent the core takes for the Monomachine: the whole
     document ({"op":"set","kind","doc","g"}) of each kind the gesture said it edited:
     "workingKit" for the kit that plays, "kit" for a slot the library wrote (the plug-in decides
     how a slot write reaches the machine). The send is keyed per document, so a drag's latest
     value wins. The page does not compare documents: the core takes a document equal to its own
     as no change. The gesture id g makes a drag one undo step; undo and redo are the core's (C++).
   - The plug-in asks before a command would lose something (the "ask" message); the page shows
     its words and sends the command again with force. The page asks nothing itself.
   - The Control workspace's LFO and Random sources run in the plug-in ("modSet", the "mod"
     message brings their values); they are never kit edits.
   - What the engine cannot do is disabled with the reason, from machine.capabilities. */
(() => {
	const V = () => window.MMView;			// the mockup's view: after it ran
	const C = () => MmConvert;
	const send = (msg, opt) => Bridge.send(msg, opt);
	const log = t => Bridge.log(t);
	const now = () => performance.now();
	/* documents are JSON values: equal when their members are */
	function sameValue(a, b) {
		if (a === b) return true;
		if (!a || !b || typeof a !== "object" || typeof b !== "object" || Array.isArray(a) !== Array.isArray(b)) return false;
		const ka = Object.keys(a), kb = Object.keys(b);
		return ka.length === kb.length && ka.every(k => sameValue(a[k], b[k]));
	}
	const copy = o => o == null ? o : structuredClone(o);
	// The Monomachine's synth tracks (the mute command's t) and its pattern and kit slots: the
	// catalogue's counts once it has come (the defaults only until then).
	let SYNTH_TRACKS = 6;
	let SLOTS = 128;

	/* ---------------- what the core shows ---------------- */
	const KINDS = ["kit", "pattern", "song", "global"];
	const DOCS = { pattern: [], kit: [], song: [], global: [] };	// by slot, as the plug-in published them
	let working = null;		// the workingKit message: {slot, source, pending, doc}
	let machine = null, catalogue = null, learn = null;
	/* the current slot of a kind, from a machine document ({current} per kind; null: not known yet).
	   One song and one global are always selected on the machine: slot 0 until it says. */
	function slotIn(d, kind) {
		const c = d && d[kind] ? d[kind].current : null;
		return c != null ? c : kind === "song" || kind === "global" ? (d ? 0 : null) : null;
	}
	const cur = kind => slotIn(machine, kind);
	const queuedIn = d => { const q = d?.pattern.queued; return q != null && q !== d.pattern.current ? q : null; };
	/* the kit that plays: the working kit when it is the current slot's, else the stored slot (until
	   the first working kit arrives, and while a kit change has not brought the new one yet) */
	const kitNow = () => working && working.slot === cur("kit") ? working.doc : DOCS.kit[cur("kit")];
	const globalNow = () => DOCS.global[cur("global")];
	const last = { mute: [], muteMs: -1e9, poly: null, polyMs: -1e9, record: null, songs: "", ready: false, readyLabel: "", caps: null, modSent: null, note: { g: 0, text: "" } };
	/* the song the Song workspace edits (MM-P4): any of the 24; null = the machine's current one */
	let songEdit = null;
	const songSlot = () => songEdit ?? cur("song");
	let synced = false;		// the view shows the machine (current pattern and working kit arrived)
	const wantApply = new Set();	// the current kinds whose documents the view has not shown yet
	let gesture = Bridge.gesture();
	let tempoInFlight = 0, modInFlight = 0;	// the id of a tempo or modSet command not answered yet
	let audioDocument = null, audioError = "";
	let lcdBits = null, lcdShown = null;	// the firmware's last LCD picture, and the one the view shows
	let noRomShown = false;
	const readyHooks = [];
	/* What a gesture edits when it does not name the document: the mockup's two edit kinds. */
	const EDITS = { struct: ["pattern", "song"], sound: ["kit"] };

	/* ---------------- the machine's documents -> the view ---------------- */
	const kitPageOf = k => k === cur("kit") && synced ? V().captureKit() : V().kitSlot(k).data || (DOCS.kit[k] ? C().kitToPage(DOCS.kit[k], globalNow()) : null);
	const lenOf = p => p === cur("pattern") && synced ? V().patternLength(p) : DOCS.pattern[p]?.length ?? V().patternLength(p);
	/* what the view's state is as the contract's documents. b is that slot's own base (its
	   firmware pass-through fields): never another slot's, even the current one's -- no base, no
	   send (sendDoc shows why). */
	function patDoc(p) {
		const b = DOCS.pattern[p];
		if (!b) return null;
		const slot = V().patternSlot(p), data = p === cur("pattern") ? V().capturePat() : slot.data;
		return data ? C().patternToFw(data, b, slot.kit, kitPageOf(slot.kit), p) : null;
	}
	function kitDoc(k) {
		const b = k === cur("kit") ? kitNow() : DOCS.kit[k];
		if (!b) return null;
		if (k === cur("kit")) return C().kitToFw(V().captureKit(), b, V().workName(), k);
		const s = V().kitSlot(k);
		return C().kitToFw(s.data || V().clearedKit(), b, s.empty ? "" : s.name, k);
	}
	const songDoc = () => DOCS.song[songSlot()] ? C().songToFw(V().song(), DOCS.song[songSlot()], lenOf) : null;
	const globDoc = () => globalNow() ? C().globalToFw(globalNow(), V().routing(), V().midiTracks(), V().multiMap()) : null;

	function setSlotPattern(p) {
		const d = DOCS.pattern[p];
		if (!d || p === cur("pattern") || !catalogue) return;
		V().setPatternSlot(p, { data: C().patternToPage(d, kitPageOf(d.kit)), kit: d.kit, has: C().hasTrigs(d), len: d.length });
	}
	function setSlotKit(k) {
		const d = DOCS.kit[k];
		if (!catalogue) return;
		V().setKitSlot(k, { name: C().kitName(d), empty: C().kitEmpty(d), data: C().kitToPage(d, globalNow()) });
	}
	/* the kit state is the machine's (machine.kit.working: "unknown" | "clean" | "edited"),
	   never a guess of the page -- "unknown" stays its own state, never folded into "clean" */
	const kitState = () => machine?.kit.working || "unknown";
	function applyCurrentKit() {
		const d = kitNow();
		V().setWorkingKit(C().kitToPage(d, globalNow()), C().kitName(d));
		if (machine) V().setKitState(kitState());
	}
	function applyCurrentPattern() {
		const d = DOCS.pattern[cur("pattern")];
		V().setPatternSlot(cur("pattern"), { data: C().patternToPage(d, V().captureKit()), kit: d.kit, has: C().hasTrigs(d), len: d.length });
	}
	const applyCurrentSong = () => V().setSong(C().songToPage(DOCS.song[songSlot()], lenOf), songSlot());
	/* the 24 songs for the Song workspace's picker: the one edited and the machine's */
	function showSongs() {
		const names = Array.from({ length: 24 }, (_, i) => DOCS.song[i] ? (C().kitEmpty(DOCS.song[i]) ? "EMPTY" : C().kitName(DOCS.song[i])) : "…");
		const v = { names, slot: songSlot(), current: cur("song") };
		if (JSON.stringify(v) === last.songs) return;
		last.songs = JSON.stringify(v);
		V().setSongs(v);
	}
	function applyCurrentGlobal() {
		const g = globalNow();
		V().setRouting(g.routingMode);
		V().setMidiTracks(g.midiSeq.channels.map((c, t) => ({ ch: c + 1, cc: [...g.midiSeq.ccs[t]] })));
		mapFromGlobal(g);
	}
	/* ---- MULTI MAP: the global's ranges (MULTIMAP EDIT, MM-P4) ---- */
	function mapFromGlobal(g) {
		if (g.multiMap) V().setMultiMap(C().mapToPage(g));
	}

	/* Show what the core holds, unless the user is in a gesture, a dialog or a library edit (the
	   gesture's own documents are already on their way; the core then shows them). The view needs
	   the catalogue first: MmConvert's enumerations come from it. */
	function applyPending() {
		if (!wantApply.size || !catalogue) return;
		if (DOCS.pattern[cur("pattern")] == null || kitNow() == null) return;
		if (synced && (V().busy() || V().dialogOpen() || V().libBusy())) return;
		const kinds = [...wantApply];
		wantApply.clear();
		/* the kit first: the pattern's locks read their machines from it */
		const kit = kinds.includes("kit"), song = kinds.includes("song") && DOCS.song[songSlot()], glob = kinds.includes("global") && globalNow();
		if (kit) applyCurrentKit();
		if (kinds.includes("pattern") || kit) applyCurrentPattern();
		if (song) applyCurrentSong();
		if (glob) applyCurrentGlobal();
		if (!synced) {
			synced = true;
			for (let i = 0; i < SYNTH_TRACKS; i++) last.mute[i] = !V().audible(i);
		}
		V().autoRange(V().sel());
		V().render();
		V().drawLib();
		showEngine();
	}

	/* the library slots not read yet */
	function markReading() {
		for (const kind of ["pattern", "kit"]) V().setReading(kind, [...Array(SLOTS).keys()].filter(s => !DOCS[kind][s]));
	}
	/* the stored slots the view shows once the catalogue is there */
	function showSlots() {
		DOCS.kit.forEach((d, k) => d && setSlotKit(k));
		DOCS.pattern.forEach((d, p) => d && setSlotPattern(p));
		libDirty = true;
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
			else if (kind === "song") { if (slot === songSlot()) wantApply.add(kind); if (catalogue) showSongs(); }
			else if (slot === cur(kind)) wantApply.add(kind);
			if (first && (kind === "pattern" || kind === "kit")) markReading();
		}
		/* the patterns that play this kit read their locks' machines from it */
		if (kind === "kit" || kind === "workingKit")
			DOCS.pattern.forEach((d, p) => { if (d && d.kit === slot) setSlotPattern(p); });
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
		if (changed("song")) { if (songEdit === cur("song")) songEdit = null; if (songEdit == null && DOCS.song[cur("song")]) wantApply.add("song"); }
		if (changed("global") && DOCS.global[cur("global")]) wantApply.add("global");
		if (catalogue) showSongs();
		machineStates(d);
		if (queuedIn(d) !== queuedIn(prev)) { V().setQueued(queuedIn(d)); V().renderTop(); V().drawLib(); }
		/* whether the machine takes input (as the MD page gates: machine.input, not a lifecycle
		   guess) -- playing is telemetry's only, never the machine document's (P6) */
		V().setInput(!!d.input);
		/* the machine's tempo (RAM), unless a tempo the user set is still on its way */
		if (!tempoInFlight && d.tempo != null && d.tempo !== V().tempo()) { V().setTempo(d.tempo); V().renderTop(); }
		if (synced) V().setKitState(kitState());
		host.renderPst();
		V().setEngines(d.engines || [], d.capabilities?.engine);
		markCapabilities(d.capabilities);
		showEngine();
		V().renderTop();
	}
	/* what the engine cannot do: every capability the view has controls for is disabled, with the
	   reason, unless machine.capabilities.can says it can (a name it does not publish: not allowed) */
	function markCapabilities(caps) {
		if (!caps || sameValue(caps, last.caps)) return;
		last.caps = caps;
		const can = caps.can || {}, why = caps.reasons || {};
		for (const cap of V().gated()) V().disable(cap, can[cap] === true ? "" : why[cap] || "Not available with this engine.");
	}

	/* MM-P4: the machine's own mutes (RAM; made on its panel too) and POLY, unless the page's own change is
	   still on its way or a solo holds (the solo drives the mutes) */
	function machineStates(d) {
		if (d.mutes && now() - last.muteMs > 1200 && !V().soloed() && !V().busy()) {
			const bits = [d.mutes.synth, d.mutes.midi];
			const list = Array.from({ length: 12 }, (_, i) => { const b = bits[i < 6 ? 0 : 1]; return b == null ? null : !!((b >> (i % 6)) & 1); });
			list.forEach((m, i) => { if (m != null) last.mute[i] = m; });
			V().setMutes(list);
		}
		if (d.poly != null && now() - last.polyMs > 1500) {
			last.poly = d.poly;
			const mode = V().mode();
			if (d.poly && mode !== "poly") V().setMode("poly");
			else if (!d.poly && mode === "poly") V().setMode("normal");
		}
	}

	/* the plug-in's engine changed (HW MIDI or the emulator): its documents start over */
	function onReset() {
		for (const k of KINDS) DOCS[k] = [];
		working = null;
		machine = null;
		songEdit = null;
		last.record = last.poly = null;
		last.songs = "";
		synced = false;
		wantApply.clear();
		last.ready = false;
		lcdBits = null;
		if (window.MMView) V().setInput(false);
		markReading();
		showEngine();
	}

	/* ---------------- the view -> the core: at the gesture ---------------- */
	/* the view shows the machine and the machine takes input: an edit can go out */
	const canSend = () => synced && !!catalogue && V().engReady();
	function sendDoc(kind, doc, slot) {
		/* no base document for this slot (patDoc/kitDoc): never borrow another slot's, and never
		   send silently -- the same reason the library shows while a slot is still coming in */
		if (!doc) { V().toast("Still reading this slot from the machine."); log("no base for " + kind + " " + slot + ": not sent"); return; }
		const g = gesture;
		send({ op: "set", kind, doc, g }, { key: kind + ":" + slot, onResult: r => {
			if (!r.ok) { V().toast(r.errors[0] || "The machine did not take it."); log("set " + kind + " " + slot + ": " + r.errors.join("; ")); }
			else note(r.note, g);
		} });
	}
	/* a result's note, once a gesture */
	function note(t, g) {
		if (!t || (last.note.g === g && last.note.text === t)) return;
		last.note = { g, text: t };
		V().toast(t);
	}
	/* the working set's documents of one kind, built from the view now */
	function sendKind(kind) {
		if (kind === "kit") sendDoc("workingKit", kitDoc(cur("kit")), cur("kit"));	// the kit that plays goes live (CC, NRPN, machine, routing, name)
		else if (kind === "pattern") sendDoc("pattern", patDoc(cur("pattern")), cur("pattern"));
		else if (kind === "song") sendDoc("song", songDoc(), songSlot());
		else if (kind === "global") sendDoc("global", globDoc(), cur("global"));
	}

	/* ---------------- the Control workspace's modulators: run by the plug-in ---------------- */
	/* the page's setup as the plug-in has it: a MIDI track target is not modulated there */
	function sendMods(setup) {
		const doc = C().modToFw(setup);
		if (setup.links.some(l => l.t >= SYNTH_TRACKS) && !sameValue(doc, last.modSent)) V().toast("Targets on MIDI tracks are not moved by the plug-in's modulators.");
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
		if (!modInFlight && m.doc && !sameValue(C().modToFw(setup), m.doc)) {
			const page = C().modToPage(m.doc), ids = new Set(page.sources.map(x => x.id));
			page.links.push(...setup.links.filter(l => l.t >= SYNTH_TRACKS && ids.has(l.src)));
			V().setCtlSetup(page);
		}
		const values = {};
		(m.doc?.sources || []).forEach((x, i) => { if (m.values?.[i] != null) values[x.id] = m.values[i]; });
		V().setModulation({ values, ccPerSecond: m.ccPerSecond || 0 });
	}

	/* ---------------- the engine, its capabilities and the firmware's LCD ---------------- */
	/* the firmware's LCD (128 x 64, one bit a pixel, rows of 16 bytes, base64: one encoding for both
	   editors) shows while the machine takes no input yet or the view is still reading it */
	function showLcd() {
		if (!window.MMView) return;
		const want = lcdBits && (!machine || !machine.input || !synced) ? lcdBits : null;
		if (want === lcdShown) return;
		lcdShown = want;
		V().setLcd(want);
	}
	/* machine.lifecycle -> the mockup's engine states */
	const LIFE = { missing: "norom", unsupported: "unsupported", loading: "loading", booting: "boot", animating: "boot", ready: "ready",
		hwConnecting: "hwwait", hwLost: "hwnone" };
	function showEngine() {
		if (!window.MMView) return;
		/* the label follows the one lifecycle value; when ready it is the engine's own (capabilities),
		   and while the library fills in the background it says how far; what a state means is the
		   plug-in's words (machine.lifecycleText) */
		const caps = machine?.capabilities, life = LIFE[machine?.lifecycle || "loading"] || "boot";
		const st = life === "ready" && !synced ? "sync" : life;
		if (caps) {
			/* the engine's own label only: the background read is in the sync slot (renderPst) */
			const text = caps.label;
			if (text + caps.about !== last.readyLabel) { last.readyLabel = text + caps.about; V().setEngineLabel("ready", [text, "on", caps.about || ""]); }
		}
		if (machine && life !== "ready") V().setEngineTip(st, machine.lifecycleText || "");
		if (V().engineState() !== st) V().setEng(st);
		/* NO ROM and ROM ERROR are the start-up card's first-run states (the view's setEng shows them, P7);
		   a firmware dialog the LOAD ROM menu opened closes as soon as the engine is anything else */
		if (st !== "norom" && noRomShown) { noRomShown = false; V().closeFirmwareDialog(); }
		if (st === "ready" && !last.ready) {
			last.ready = true;
			log("ready: pattern " + cur("pattern") + " kit " + cur("kit"));
			for (const f of readyHooks.splice(0)) try { f(); } catch (e) { log("ready hook: " + e.message); }
		}
		showLcd();
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
	function joySend(x, y) {
		const ch = trackChannel(V().asgT());
		if (ch == null) return;
		const pb = Math.max(0, Math.min(16383, Math.round(8192 + x * 8191)));
		midi([0xe0 | ch, pb & 0x7f, pb >> 7], "joyx");
		midi([0xb0 | ch, 1, Math.round(Math.max(0, y) * 127)], "joyu");
		midi([0xb0 | ch, 2, Math.round(Math.max(0, -y) * 127)], "joyd");
	}

	/* ---------------- LEARN: the plug-in's MIDI learn (persistent, per plug-in) ---------------- */
	/* a value the plug-in's learn takes: one of the learn document's limits */
	function learnable(lt) {
		const L = learn && learn.limits, [pg, i] = lt.pid.split("."), p = PAGES.indexOf(pg);
		return !!L && lt.t < L.tracks && L.params.some(q => q.pg === p && q.i === +i);
	}
	const learnRef = lt => { const [pg, i] = lt.pid.split("."); return { t: lt.t, pg: PAGES.indexOf(pg), i: +i }; };
	const notLearnable = "The plug-in's MIDI learn cannot map this value (MIDI page values are NRPN on the machine).";
	function onLearn(doc) {
		const prev = learn;
		learn = doc;
		if (prev && prev.learning && !doc.learning && doc.mappings.length > prev.mappings.length) {
			const m = doc.mappings[doc.mappings.length - 1];
			V().toast(`Learned: CC ${m.cc} → T${m.t + 1} ${PAGES[m.pg] || ""} ${V().pname(m.t, PAGES[m.pg] + "." + m.i) || ""}. Stored with the plug-in.`);
			V().clearLearnTarget();
		}
	}

	/* ---------------- what the plug-in asks, and what it refused ---------------- */
	/* {type:"ask", ask, message, confirm, command}: its words and two keys; confirm sends the command
	   again with force */
	function onAsk(m) {
		const again = () => { const c = Object.assign({}, m.command, { force: true }); delete c.id; send(c); };
		V().ask(m.message || "The machine asks before it goes on.", m.command ? [[m.confirm || "Go on", "danger", again], ["Cancel", "", () => {}]] : [["OK", "", () => {}]]);
	}
	function onError(m) { log("error: " + m.message); V().toast(m.message); }
	const noteOf = r => { if (r.ok && r.note) V().toast(r.note); };

	/* ================= the host: what the mockup hands to the machine ================= */
	const host = window.MMHost = {
		ownsClock: true,	// the firmware is the sequencer: no local clock
		engineLabels: { sync: ["READING MACHINE", "blink"], unsupported: ["NOT OS 1.32B", "off"] },
		notes: {
			ccSource: "On-screen knob: it drives its targets through the editor. Your controller's knobs reach the machine through LEARN (the plug-in's MIDI learn).",
			appSource: "Runs in the plug-in on the machine's own steps and sends CCs, at most 300 a second, also with the editor closed. A lock wins on its step. Targets on MIDI tracks are not moved."
		},
		/* the mockup calls it at the end of its script: its markup and view are there */
		start() { init(); },
		edited(what, kind) {
			if (what === "commit") {
				/* the gesture ended: the next edit is a new undo step, and the view may show the core again */
				gesture = Bridge.gesture();
				applyPending();
				return;
			}
			if (canSend()) for (const k of kind ? [kind] : EDITS[what] || []) sendKind(k);
		},
		/* a library gesture wrote a slot: that slot's document, as "kit" or "pattern" */
		slotWritten(kind, slot) { if (canSend()) sendDoc(kind, kind === "kit" ? kitDoc(slot) : patDoc(slot), slot); },
		undo() { send({ op: "undo" }, { onResult: r => r.ok ? V().toast(r.note || "Undo") : V().toast(r.errors[0]) }); },
		redo() { send({ op: "redo" }, { onResult: r => r.ok ? V().toast(r.note || "Redo") : V().toast(r.errors[0]) }); },
		history() { const h = machine?.history; return { undo: h?.undoCount || 0, redo: h?.redoCount || 0 }; },
		togglePlay() { if (V().engReady()) send({ op: V().playing() ? "stop" : "play" }); },
		selectPattern(p, nowFlag) { send({ op: "select", p, now: !!nowFlag }, { onResult: noteOf }); },
		/* the kit keys: plain commands; the plug-in asks when something would be lost */
		kit(op, k) {
			if (op === "save") send({ op: "saveKit" }, { onResult: noteOf });
			else if (op === "reload") send({ op: "loadKit" }, { onResult: noteOf });
			else if (op === "load") send({ op: "loadKit", k }, { onResult: noteOf });
			else if (op === "saveAs") send({ op: "saveKit", k }, { onResult: noteOf });
		},
		/* the machine's tempo: the machine document's tempo is shown again once this is answered */
		tempo(bpm) {
			tempoInFlight = send({ op: "tempo", bpm }, { key: "tempo", onResult: r => { if (r.id === tempoInFlight) tempoInFlight = 0; } });
		},
		mutes() {
			/* mutes and solos: the synth tracks' mute parameters (CC 3); the MIDI tracks' MUTE window (MM-P4),
			   where the engine can reach it (machine.capabilities) */
			last.muteMs = now();
			for (let i = 0; i < SYNTH_TRACKS; i++) {
				const off = !V().audible(i);
				if (off !== last.mute[i]) { last.mute[i] = off; send({ op: "mute", t: i, on: off }); }
			}
			if (machine?.capabilities?.can?.midiMutes !== true) return;
			for (let i = 0; i < 6; i++) {
				const t = SYNTH_TRACKS + i, off = !V().audible(t);
				if (off !== last.mute[t]) { last.mute[t] = off; send({ op: "muteMidi", t: i, on: off }, { onResult: r => { if (!r.ok) V().toast(r.errors[0]); } }); }
			}
		},
		/* POLY is the machine's audio mode (SET STATUS 0x20): entering or leaving it */
		keyMode(mode) {
			const poly = mode === "poly";
			if (poly === last.poly) return;
			last.poly = poly;
			last.polyMs = now();
			send({ op: "poly", on: poly }, { onResult: noteOf });
		},
		/* the RECORD key: stopped = GRID RECORDING, playing = LIVE RECORDING, recording = off */
		record() {
			if (!V().engReady()) return;
			const mode = last.record && last.record !== "off" ? "off" : V().playing() ? "live" : "grid";
			send({ op: "record", mode }, { onResult: r => V().toast(!r.ok ? r.errors[0] : mode === "off" ? "Recording off."
				: mode === "live" ? "LIVE RECORDING: play the keyboard; the notes are recorded to the nearest step."
				: "GRID RECORDING on the machine: its TRIG keys write steps; the editor reads them back.") });
		},
		/* the Song workspace: another of the 24 songs to edit; LOAD SONG makes it the machine's */
		songSlot(slot) {
			songEdit = slot === cur("song") ? null : slot;
			if (DOCS.song[slot]) { applyCurrentSong(); V().render(); }
			else send({ op: "load", kind: "song", slot });
			showSongs();
		},
		loadSong(slot) {
			send({ op: "loadSong", s: slot }, { onResult: r => V().toast(r.ok ? `S${String(slot + 1).padStart(2, "0")} is the machine's song now.` : r.errors[0]) });
		},
		/* HW MIDI: what waits for the machine's SYSEX RECV, and the person says it is there */
		waiting() { return machine?.recv?.waiting || 0; },
		sendNow() { send({ op: "hwSend" }, { onResult: r => V().toast(r.ok ? r.note : r.errors[0]) }); },
		playKey(n) {
			const ch = keyChannel();
			if (ch == null) { V().setKeyDown(n, "That MIDI channel is OFF in the global (GLOBAL › MIDI › CHANNELS)."); return; }
			if (held && held.n === n && held.ch === ch) return;
			noteOff();
			midi([0x90 | ch, n, 100]);
			held = { ch, n };
			const what = V().mode() === "multi" ? "MULTI TRIG" : V().mode() === "map" ? "MULTI MAP" : "T" + (V().asgT() + 1);
			V().setKeyDown(n, `${what} · ${V().noteName(n)} · MIDI channel ${ch + 1}`);
		},
		keyUp() { noteOff(); },
		joy(xy) { joySend(xy.x, xy.y); },
		learning(on) { if (!on) send({ op: "learnCancel" }); },
		learnTarget(lt) {
			if (!learnable(lt)) { V().toast(notLearnable); V().clearLearnTarget(); return; }
			send(Object.assign({ op: "learnStart" }, learnRef(lt)), { onResult: r => r.ok ? V().toast("Move a knob on your controller, or press 1-8 for CC 21-28.") : V().toast(r.errors[0]) });
		},
		learnBind(lt, k) {
			if (!learnable(lt)) { V().toast(notLearnable); return; }
			send({ op: "learnCancel" });
			/* the mockup's knob k is CC 20 + k; its Control workspace then binds that knob on screen too */
			send(Object.assign({ op: "learnAdd", cc: 20 + k }, learnRef(lt)), { onResult: r => { if (!r.ok) V().toast(r.errors[0]); } });
		},
		/* the Control workspace's LFO and Random sources and their links: the plug-in runs them */
		modulators(setup) { sendMods(setup); },
		engine(kind) { send({ op: "engine", kind }, { onResult: r => V().toast(r.ok ? r.note : r.errors[0]) }); },
		/* the start-up card's keys (P7): the window's native file chooser for the firmware (the page never reads
		   it), the ROM folder, and a new look */
		chooseRom() { send({ op: "chooseRom" }); },
		/* SysEx import and export (P7): the window's file dialogs; the plug-in parses and writes */
		syxChoose() { send({ op: "chooseSyx" }); },
		syxExport() { send({ op: "syxExport" }); },
		syxStart(kinds) { send({ op: "syxImport", kinds }, { onResult: r => { if (!r.ok) V().toast(r.errors[0]); } }); },
		syxStop() { send({ op: "syxCancel" }); },
		revealRom() { send({ op: "revealRomFolder" }); },
		recheck() { send({ op: "recheckFirmware" }, { onResult: r => V().toast(r.ok ? r.note : r.errors[0]) }); },
		firstRun() {
			noRomShown = true;
			log("NO ROM dialog shown (lifecycle " + machine?.lifecycle + ")");
			V().ask(`<div class="lcdbig">MONOMACHINE FIRMWARE NEEDED</div>
 <p>Monomachine Editor runs the real Monomachine operating system. Elektron's firmware cannot ship with the plug-in, so you add the one from your own machine.</p>
 <ol class="recvsteps"><li>Dump the <b>OS 1.32B</b> flash image from your Monomachine (<span class="mono">.bin</span>).</li><li>Choose it here, or drop it on the window: the editor checks it and copies it into its ROM folder.</li><li>The machine starts with it at once. It stays on this computer only.</li></ol>`,
				[["Choose ROM file…", "cream", () => send({ op: "chooseRom" })], ["Show ROM folder", "", () => send({ op: "revealRomFolder" })],
					["Check again", "", () => send({ op: "recheckFirmware" }, { onResult: r => { V().toast(r.ok ? r.note : r.errors[0]); if (!r.ok) { noRomShown = false; showEngine(); } } })], ["Close", "", () => {}]], "first");
		},
		/* while the firmware starts, the LCD shows the machine's own screen (the lcd messages), not
		   the mockup's example boot screen */
		bootScreen() { showLcd(); },
		/* SYSEX RECV state in the pattern field */
		renderPst() {
			if (!window.MMView) return;	// the mockup's first render, before its view is there
			const r = machine?.recv, caps = machine?.capabilities, manual = caps?.values?.dumps === "manual";
			const on = r && (r.sending > 0 || r.state === "entering" || r.state === "parked");
			const l = machine?.loading;
			if (manual && r?.waiting) V().setPst("SEND " + r.waiting, "Edits wait for the Monomachine's SYSEX RECV screen: click for how to send them. " + (caps.reasons?.recvSession || ""), true);
			else if (machine?.input && l && l.done < l.total) V().setPst(`${l.done}/${l.total}`, "Reading the machine's patterns, kits and songs in the background: edit away, the library fills in.", false, l.done / l.total);
			else if (on) V().setPst(r.sending > 0 ? "RECV " + r.sending : "RECV", "The machine is on GLOBAL › SYSEX RECV and takes the edits (" + r.state + ", " + r.received + " received).", false);
			/* P7: the machine follows an external clock (in a DAW: the host's, set by the plug-in): CONTROL IN
			   TEMPO SYNC EXT MIDI CLK and TRANSPORT ACCEPT, the global's controlIn */
			else if (globalNow()?.controlIn?.tempoSync === 1 && globalNow()?.controlIn?.transport === 1) V().setPst("Host", "Follows the host's tempo and transport (GLOBAL › CONTROL IN: EXT MIDI CLK, TRANSPORT ACCEPT).", false);
			else V().setPst("", "", false);
		},
		/* the editor's menu (skins, GUI scale, settings) */
		menu() { send({ op: "openMenu" }); },
		/* the AUDIO / MIDI panel: the plug-in's devices (mdAudioMidiLink.cpp); in a plug-in the
		   document says standalone false, and the engine menu has no entry for it. A change's error is
		   only in its result: the panel shows the last one until the next change. */
		audioDoc() { return audioDocument && Object.assign({}, audioDocument, { error: audioError }); },
		audioSend(c) { send(Object.assign({ op: "audioSet" }, c), { onResult: r => { audioError = r.ok ? "" : r.errors[0] || ""; V().redrawAudio(); } }); },
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
		/* start empty, not with the mockup's example */
		v.startEmpty();
		for (let i = 0; i < SYNTH_TRACKS; i++) last.mute[i] = !v.audible(i);
		markReading();
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

	function onCatalogue(doc) {
		catalogue = doc;
		if (doc.tracks) SYNTH_TRACKS = doc.tracks;
		if (doc.slots && doc.slots.pattern) SLOTS = doc.slots.pattern;
		const off = C().useCatalogue(doc);
		if (off.length) log("catalogue: the page's tables differ: " + off.join("; "));
		showSlots();
		showSongs();
		applyPending();
	}
	function onMessage(m) {
		if (m.type === "doc") onDoc(m);
		else if (m.type === "machine") onMachine(m.doc);
		else if (m.type === "telemetry") {
			if (m.playing !== V().playing()) V().setPlaying(m.playing);
			if (V().playing()) V().setStep(m.step);
			/* MM-P4: the machine's recording mode */
			if (m.record !== undefined && m.record !== last.record) { last.record = m.record; V().setRecord(m.record || "off"); }
		}
		else if (m.type === "lcd") { lcdBits = Uint8Array.from(atob(m.bits || ""), c => c.charCodeAt(0)); showLcd(); }
		else if (m.type === "mod") onMod(m);
		else if (m.type === "catalogue") onCatalogue(m.doc);
		else if (m.type === "reset") onReset();
		else if (m.type === "audio") { audioDocument = m.doc; V().setAudioEntry(!!(audioDocument && audioDocument.standalone)); V().redrawAudio(); }
		else if (m.type === "audioLevel") V().audioLevel(m.in);
		else if (m.type === "openAudio") V().openAudio();
		else if (m.type === "learn") onLearn(m.doc);
		else if (m.type === "ask") onAsk(m);
			else if (m.type === "romInstall") { V().bootRom(m); V().toast(m.text); }
			else if (m.type === "syxPreview") V().syxPreview(m);
			else if (m.type === "syxProgress") V().syxProgress(m);
			else if (m.type === "syxExport") V().toast(m.text);
		else if (m.type === "error") onError(m);
		else if (m.type === "result" && !m.ok && m.errors?.length && m.op !== "set" && m.op !== "modSet") V().toast(m.errors[0]);
	}
})();
