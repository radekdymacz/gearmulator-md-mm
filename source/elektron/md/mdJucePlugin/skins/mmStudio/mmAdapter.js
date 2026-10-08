"use strict";
/* Monomachine Editor: the plug-in's host for the approved mockup (P6).
   The mockup (mmMockup.js, copied verbatim from doc/modern-ux/mm-mockup) is the UI. It hands
   everything the machine does to its host, window.MMHost, which this file defines before the
   mockup loads: transport, patterns, the kit library, undo, tempo, mutes, the keyboard, the
   Control workspace's modulators and the AUDIO / MIDI panel. Nothing of the mockup is replaced.
   Its view is used through window.MMView only: values to read, show(view) for the documents,
   named setters for the rest, and disable(capability, reason); never its state and never its
   markup (no DOM here: sync-mmstudio-skin.py checks that).
   - The plug-in publishes the core's documents (observed, or pending while an edit is on its
     way), keyed by the contract's kinds: pattern, kit (the stored slots), workingKit (the kit
     that plays), song, global; and the machine document, which says which slots are current.
     They are kept in one store (deskDocs.js) and shown as one derived view (mmView.js, through
     MmConvert: the pure page <-> contract translation, its enumerations from the catalogue),
     DESIGN-UNIFY.md phase 1.
   - An edit is sent at its gesture as an intent the core applies to its own documents (DESIGN-UNIFY.md 4.1:
     every gesture, host.intent; the library's slot ops, host.library): {op, args, g}, one vocabulary with the
     Machinedrum Editor (mmDeskEdit.cpp), so a step the machine recorded meanwhile is never overwritten. What
     it shows at once are the view writes MmView.writes makes of it (Overlay, owned by its id until answered;
     a paste from the page's own copy of the core's clipboard); the mutes, POLY and the tempo likewise. A drag's
     sends are keyed per what it moves, so its latest value wins. The gesture id g makes a gesture one undo
     step; undo and redo are the core's (C++). The page never builds a document: a whole document ("set") is
     the intent of an import, which is the plug-in's.
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

	/* ---------------- what the core shows (DESIGN-UNIFY.md 4.3, 4.4) ---------------- */
	/* The documents as the plug-in published them: one store (deskDocs.js), never written by a gesture, with
	   the machine document. Over the view derived from them (mmView.js), until their commands are answered: the
	   page's optimistic values (Overlay: each intent's writes, the mutes, POLY, the tempo). MMView.show is the
	   one writer of the mockup's document members:  V = Overlay.over(MmView.derive(DOCS, ui)).
	   An answer comes after the documents its command changed, so when it comes the entries go and the
	   documents carry the edit, or, refused, show what the machine has. No clock, no gating. */
	const KINDS = [{ kind: "pattern", at: "patterns" }, { kind: "kit", at: "kits" }, { kind: "song", at: "songs" },
		{ kind: "global", at: "globals" }, { kind: "workingKit", working: "workingKit" }];
	const STORE = docStore(KINDS);
	const AT = { pattern: "patterns", kit: "kits", song: "songs", global: "globals" };
	const noDocs = () => ({ patterns: {}, kits: {}, songs: {}, globals: {}, workingKit: null, sources: {}, machine: null });
	let DOCS = noDocs();
	let machine = null, catalogue = null, learn = null;	// machine: DOCS.machine, the machine document
	const docs = () => DOCS;
	/* the current slot of a kind (mmView.js's rule; here too: the mockup's first render asks before mmView.js is there) */
	function slotIn(d, kind) {
		const c = d && d[kind] ? d[kind].current : null;
		return c != null ? c : kind === "song" || kind === "global" ? (d ? 0 : null) : null;
	}
	const cur = kind => slotIn(machine, kind);
	const kitNow = () => { const d = docs(), k = cur("kit"); return k == null ? null : d.workingKit && d.workingKit.slot === k ? d.workingKit.doc : d.kits[k] || null; };
	const globalNow = () => docs().globals[cur("global")];
	/* the song the Song workspace edits (MM-P4): any of the 24; null = the machine's current one */
	let songEdit = null;
	const songSlot = () => songEdit ?? cur("song");
	const view = () => { const v = MmView.derive(docs(), { songEdit }); return Overlay.over(Overlay.size() ? MmView.own(v) : v); };
	/* the view shows the machine: the catalogue (MmConvert's enumerations), the current pattern and the kit that plays */
	const ready = () => !!catalogue && !!(docs().patterns[cur("pattern")] && kitNow());
	const last = { record: null, ready: false, readyLabel: "", caps: null, modSent: null, note: { g: 0, text: "" } };
	let gesture = Bridge.gesture();
	let modInFlight = 0;	// the id of a modSet command not answered yet
	let clip = null;	// the page's copy of what the core's clipboard holds (MmView.copied): a paste shows at once
	let audioDocument = null, audioError = "";
	let lcdBits = null, lcdShown = null;	// the firmware's last LCD picture, and the one the view shows
	let noRomShown = false;
	let libDirty = false;	// a library slot changed: drawn when no gesture holds the page
	let unsent = false;	// a gesture's edit could not go out (the view did not show the machine yet)
	const readyHooks = [];

	/* the view, shown; all: every member written again (a reset, an edit the machine did not take) */
	function refresh(all) {
		if (!window.MMView || !catalogue) return;
		V().show(view(), all);
		if (libDirty && !V().busy()) { libDirty = false; V().drawLib(); }
		showEngine();
	}
	/* a command's answer: its entries leave; a refused one shows the documents again */
	function answered(r) {
		const had = Overlay.answered(r.id);
		refresh(!r.ok && had);
	}

	/* ---------------- the machine's documents -> the library's other slots ---------------- */
	/* a kit as the page shows it, for a stored pattern's locks (the kit that plays: its working kit) */
	const kitPageOf = k => { const d = MmView.kitDocOf(docs(), k); return d ? C().kitToPage(d, globalNow()) : null; };

	/* a stored slot the library shows (the current pattern is the view's: MMView.show) */
	function setSlotPattern(p) {
		const d = docs().patterns[p];
		if (!d || p === cur("pattern") || !catalogue) return;
		V().setPatternSlot(p, { data: C().patternToPage(d, kitPageOf(d.kit)), kit: d.kit, has: C().hasTrigs(d), len: d.length });
		libDirty = true;
	}
	function setSlotKit(k) {
		const d = docs().kits[k];
		if (!d || !catalogue) return;
		V().setKitSlot(k, { name: C().kitName(d), empty: C().kitEmpty(d), data: C().kitToPage(d, globalNow()) });
		libDirty = true;
	}

	/* the library slots not read yet */
	function markReading() {
		for (const kind of ["pattern", "kit"]) V().setReading(kind, [...Array(SLOTS).keys()].filter(s => !DOCS[AT[kind]][s]));
	}
	/* the stored slots the view shows once the catalogue is there */
	function showSlots() {
		Object.keys(DOCS.kits).forEach(k => setSlotKit(+k));
		Object.keys(DOCS.patterns).forEach(p => setSlotPattern(+p));
	}
	function onDoc(m) {
		const { kind } = m;
		const first = AT[kind] && !DOCS[AT[kind]][m.slot];
		const slot = storeDoc(DOCS, m, STORE);
		if (slot == null) return;
		if (kind === "pattern") setSlotPattern(slot);
		else if (kind === "kit") setSlotKit(slot);
		/* the patterns that play this kit read their locks' machines from it */
		if (kind === "kit" || kind === "workingKit")
			Object.entries(DOCS.patterns).forEach(([p, d]) => { if (d.kit === slot) setSlotPattern(+p); });
		if (first && (kind === "pattern" || kind === "kit")) markReading();
		refresh();
	}

	function onMachine(d) {
		const prev = machine;
		DOCS.machine = machine = d;
		const old = slotIn(prev, "pattern");
		if (old != null && old !== cur("pattern")) setSlotPattern(old);	// the pattern that played goes back to the library
		if (slotIn(prev, "song") !== cur("song") && songEdit === cur("song")) songEdit = null;
		if ((d.pattern?.queued ?? null) !== (prev?.pattern?.queued ?? null)) libDirty = true;
		/* whether the machine takes input (as the MD page gates: machine.input, not a lifecycle
		   guess) -- playing is telemetry's only, never the machine document's (P6) */
		V().setInput(!!d.input);
		/* not ready -> ready: a fresh mod message follows, an earlier modSet's id is moot (as the MD page) */
		if (d.input && !prev?.input) modInFlight = 0;
		host.renderPst();
		V().setEngines(d.engines || [], d.capabilities?.engine);
		markCapabilities(d.capabilities);
		refresh();
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

	/* the plug-in's engine changed (HW MIDI or the emulator): its documents start over */
	function onReset() {
		DOCS = noDocs();
		machine = null;
		songEdit = null;
		clip = null;
		Overlay.clear();
		last.record = null;
		last.ready = false;
		lcdBits = null;
		/* a modSet on its way to the old engine is never answered (as the MD page) */
		modInFlight = 0;
		/* the old engine's pattern, kit and song leave the screen: the new engine's come as documents. The Control
		   workspace stays (no mod message follows a machine restart): only the machine's state goes */
		if (window.MMView) { V().setInput(false); V().startEmpty(true); V().show(null, true); if (!V().busy()) V().render(); }
		markReading();
		showEngine();
	}

	/* ---------------- the view -> the core: at the gesture ---------------- */
	/* the view shows the machine and the machine takes input: an edit can go out */
	const canSend = () => ready() && V().engReady();
	/* a result's note, once a gesture */
	function note(t, g) {
		if (!t || (last.note.g === g && last.note.text === t)) return;
		last.note = { g, text: t };
		V().toast(t);
	}
	/* a command whose value the view shows at once (the mutes, POLY, the tempo): [path, value] writes owned by its id */
	function expect(id, writes) { Overlay.add(id, writes); }

	/* ---------------- edit intents (DESIGN-UNIFY.md 4.1) ---------------- */
	/* a drag's latest value replaces its waiting one (per what it moves); a click is its own command */
	const fields = c => Object.keys(c).filter(k => !["op", "p", "k", "s", "g", "id"].includes(k)).sort();
	const KEYED = { level: c => c.t, param: c => [c.t, c.page, c.i], lock: c => [c.t, c.page, c.i, c.s], step: c => [c.t, c.s],
		arp: c => [c.t, c.field, c.i], transpose: c => [c.t, c.v != null, c.scale != null, c.key != null], length: () => 0, swing: () => 0,
		speed: () => 0, routing: () => 0, midiTrack: c => [c.t, c.ch != null], steps: c => [c.from, c.to, c.rows.map(r => r.t)],
		params: () => 0, assign: c => [c.t, c.src, c.row, fields(c)], multiEnv: c => c.i, multiTrig: fields, kitName: () => 0,
		multiMap: c => [c.i, fields(c)], rowSet: c => c.i };
	/* the documents an intent's values come from (MmView.toFw): the pattern and the kit that play, the song edited */
	const fwDocs = () => ({ pattern: docs().patterns[cur("pattern")], kit: kitNow(), song: docs().songs[songSlot()] });
	/* an edit intent of a gesture: sent with the gesture's id, shown at once (MmView.writes, Overlay) until its
	   answer, which comes after the documents it changed; refused: the view shows the documents again. The op
	   names its document (MmView.kindOf): the kit that plays gets k, a song row op s (the song the Song workspace
	   edits), a pattern op p; the global's and the library's ops name their own. */
	function intent(op, args) {
		if (!canSend()) { unsent = true; return; }
		const g = gesture, v = view(), kind = MmView.kindOf(op), c = MmView.toFw(Object.assign({ op }, structuredClone(args)), v, fwDocs());
		if (kind === "kit") c.k = cur("kit");
		else if (kind === "song") c.s = songSlot();
		else if (kind === "pattern") c.p = cur("pattern");
		c.g = g;
		const key = KEYED[op] ? op + ":" + JSON.stringify(KEYED[op](c)) : null;
		const id = send(c, { key, onResult: r => {
			answered(r);
			if (!r.ok) { V().toast(r.errors[0] || "The machine did not take it."); log(op + ": " + r.errors.join("; ")); }
			else note(r.note, g);
		} });
		clip = MmView.copied(v, c) || clip;
		Overlay.add(id, MmView.writes(v, c, clip), docOf(op, c));
		refresh();
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
		const want = lcdBits && (!machine || !machine.input || !ready()) ? lcdBits : null;
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
		const st = life === "ready" && !ready() ? "sync" : life;
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
		V().setMapping(!!doc.enabled);	/* MIDI mapping is off unless the plug-in says so (deskHost.h) */
		if (prev && prev.learning && !doc.learning && doc.mappings.length > prev.mappings.length) {
			const m = doc.mappings[doc.mappings.length - 1];
			V().toast(`Learned: CC ${m.cc} → T${m.t + 1} ${PAGES[m.pg] || ""} ${V().pname(m.t, PAGES[m.pg] + "." + m.i) || ""}. Stored with the plug-in.`);
			V().clearLearnTarget();
		}
	}

	/* ---------------- what the plug-in asks, and what it refused ---------------- */
	/* {type:"ask", ask, message, confirm, command, alternatives}: its words and its keys; confirm sends the
	   command again with force; an alternative ("Save and load") sends its first commands, then the same
	   (as the Machinedrum Editor's onAsk) */
	function onAsk(m) {
		const strip = c => { const o = Object.assign({}, c); delete o.id; delete o.force; return o; };
		const again = () => send(Object.assign(strip(m.command), { force: true }));
		const alts = m.command ? (m.alternatives || []).map(a => [a.label, "cream", () => { (a.first || []).forEach(c => send(strip(c))); again(); }]) : [];
		V().ask(m.message || "The machine asks before it goes on.", m.command ? [...alts, [m.confirm || "Go on", "danger", again], ["Cancel", "", () => {}]] : [["OK", "", () => {}]]);
	}
	/* LOAD ROM with a firmware installed: the start-up card in its installed form (Boot.showInstalled); the ROM
	   folder is the editor's, nothing outside it is touched */
	function onRomInfo(m) {
		if (!m.installed) { host.firstRun(); return; }
		V().bootInstalled({ machine: "Monomachine", os: m.os, name: m.name, size: m.size, inFolder: m.inFolder, folder: m.folder });
	}
	function askRemoveRom(m) {
		if (!m.inFolder) { V().toast("This image is outside the editor's ROM folder (" + m.folder + "); remove it there yourself."); return; }
		const esc = t => String(t).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
		V().ask(`Remove <b>${esc(m.os)}</b> from the ROM folder? The machine stops and the editor asks for a firmware again. Your project stays.`,
			[["Remove", "danger", () => send({ op: "removeRom" }, { onResult: r => { if (!r.ok) V().toast(r.errors[0]); } })], ["Cancel", "", () => {}]]);
	}
	/* what the plug-in has to say to the user (a question, a warning): its modal, never a native alert. The plug-in
	   waits for the answer, so a notice goes before the page's own questions, is never replaced by one, and is
	   always answered: closed any other way, by its last key (Dlg, skins/shared/deskModal.js; as the MD page) */
	function onNotice(m) {
		/* "modal": false (the update banner, DESIGN-updates.md): a strip, not the dialog; the page plays on under it */
		if (m.modal === false) { Banner.show(m, i => send({ op: "noticeAnswer", id: m.id, button: i })); return; }
		const names = m.buttons && m.buttons.length ? m.buttons : ["OK"], item = { notice: true };
		const answer = i => { if (item.done) return; item.done = true; send({ op: "noticeAnswer", id: m.id, button: i }); };
		item.cancel = () => answer(names.length - 1);
		const esc = t => String(t).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
		V().ask(`<b>${esc(m.title)}</b><br>${esc(m.text).replace(/\n/g, "<br>")}`,
			names.map((t, i) => [esc(t), i === 0 && names.length > 1 ? "cream" : "", () => answer(i)]), "", item);
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
		/* the mockup calls it at the end of its script: its markup and view are there, MmConvert not
		   yet (mmConvert.js loads after the mockup: it reads the mockup's tables). init() says ready,
		   and the plug-in answers with the catalogue at once: a catalogue that came before MmConvert
		   was lost (useCatalogue threw), so every document showed the enumerations as raw firmware
		   values (LFO PAGE 15 for SYN) and Sound threw on such a track. The page speaks only once
		   MmConvert is there. */
		start() { const go = () => typeof MmConvert === "undefined" || typeof MmView === "undefined" ? setTimeout(go, 0) : init(); go(); },
		/* the gesture ended: the next edit is a new undo step; the drawing it held comes now, and an edit that
		   could not go out is undone on the screen (the view shows the documents again) */
		commit() {
			gesture = Bridge.gesture();
			refresh(unsent);
			unsent = false;
		},
		/* a gesture's edit intent (op and arguments in the contract's vocabulary; knob and lock values and song rows
		   in the page's units), DESIGN-UNIFY.md 4.1 */
		intent(op, args) { intent(op, args); },
		/* the library's slot ops (kitCopy ... patClear, kitRename): intents too; what they would lose the machine
		   asks first (the "ask" message), and the result brings the slots */
		library(op, args) { intent(op, args); },
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
		/* the machine's tempo: shown at once (Overlay); the machine document says it from when the core took it
		   until its memory does (deskCore::FieldExpectation) */
		tempo(bpm) {
			expect(send({ op: "tempo", bpm }, { key: "tempo", onResult: answered }), [[["bpm"], bpm]]);
		},
		mutes() {
			/* mutes and solos: the synth tracks' mute parameters (CC 3); the MIDI tracks' MUTE window (MM-P4),
			   where the engine can reach it (machine.capabilities). A track is sent when the page's differs from the
			   machine's (the view's: memory, or what the core expects; not known: sent) */
			const machineMutes = view().mutes || [];
			for (let i = 0; i < SYNTH_TRACKS; i++) {
				const off = !V().audible(i);
				if (off !== machineMutes[i]) expect(send({ op: "mute", t: i, on: off }, { onResult: answered }), [[["mutes", i], off]]);
			}
			if (machine?.capabilities?.can?.midiMutes === true)
				for (let i = 0; i < 6; i++) {
					const t = SYNTH_TRACKS + i, off = !V().audible(t);
					if (off !== machineMutes[t]) expect(send({ op: "muteMidi", t: i, on: off }, { onResult: r => { answered(r); if (!r.ok) V().toast(r.errors[0]); } }), [[["mutes", t], off]]);
				}
			refresh();
		},
		/* POLY is the machine's audio mode (SET STATUS 0x20): entering or leaving it */
		keyMode(mode) {
			const poly = mode === "poly";
			if (poly === view().poly) return;
			expect(send({ op: "poly", on: poly }, { onResult: r => { answered(r); noteOf(r); } }), [[["poly"], poly]]);
			refresh();
		},
		/* the RECORD key: stopped = GRID RECORDING, playing = LIVE RECORDING, recording = off; live (Alt+Space,
		   RECORD + PLAY): LIVE RECORDING also when stopped (the machine starts playing) */
		record(live) {
			if (!V().engReady()) return;
			const mode = last.record && last.record !== "off" ? "off" : live || V().playing() ? "live" : "grid";
			send({ op: "record", mode }, { onResult: r => V().toast(!r.ok ? r.errors[0] : mode === "off" ? "Recording off."
				: mode === "live" ? "LIVE RECORDING: play the keyboard; the notes are recorded to the nearest step."
				: "GRID RECORDING on the machine: its TRIG keys write steps; the editor reads them back.") });
		},
		/* the Song workspace: another of the 24 songs to edit; LOAD SONG makes it the machine's */
		songSlot(slot) {
			songEdit = slot === cur("song") ? null : slot;
			if (!DOCS.songs[slot]) send({ op: "load", kind: "song", slot });
			refresh();
		},
		/* MM-P8: the machine's pattern chain (the Song palette's CHAIN), and its end */
		chain(patterns) {
			send({ op: "chain", patterns }, { onResult: r => { if (!r.ok) V().toast(r.errors[0] || "The machine did not take the chain."); } });
		},
		chainClear() {
			send({ op: "chainClear" }, { onResult: r => { if (!r.ok) V().toast(r.errors[0] || "The chain did not end."); } });
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
		/* the home row, the piano roll's and the transpose keyboard's keys: the note intent, as on the MD
		   (noteOn / noteOff, mm-data-contract.md). The core plays synth track t's note on its own MIDI channel
		   (GLOBAL › MIDI › CHANNELS: base + t, while t < span) and says why not when it cannot */
		noteOn(t, pitch, vel) { send({ op: "noteOn", t, vel, pitch }, { onResult: r => { if (!r.ok) V().toast(r.errors[0]); } }); },
		noteOff(t, pitch) { send({ op: "noteOff", t, pitch }); },
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
		removeRom(info) { askRemoveRom(info); },
		/* LOAD ROM in the engine menu: which firmware runs, REPLACE or REMOVE it (the reply is the romInfo message) */
		romManage() { if (machine?.lifecycle === "missing") host.firstRun(); else send({ op: "romInfo" }); },
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
 <ol class="recvsteps"><li>Dump the <b>OS 1.32B</b> flash image from your Monomachine (<span class="mono">.bin</span>).</li><li>Choose it here: the editor checks it and copies it into its ROM folder.</li><li>The machine starts with it at once. It stays on this computer only.</li></ol>`,
				[["Choose ROM file…", "cream", () => send({ op: "chooseRom" })], ["Show ROM folder", "", () => send({ op: "revealRomFolder" })],
					["Check again", "", () => send({ op: "recheckFirmware" }, { onResult: r => { V().toast(r.ok ? r.note : r.errors[0]); if (!r.ok) { noRomShown = false; showEngine(); } } })], ["Close", "", () => {}]], "first", { key: "firstRun" });
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
			return { machine: copy(machine), workingKit: copy(DOCS.workingKit), learn: copy(learn), catalogue: copy(catalogue),
				doc: (kind, slot) => copy(DOCS[AT[kind]]?.[slot]), slots: kind => DOCS[AT[kind]] ? Object.keys(DOCS[AT[kind]]).map(Number).sort((a, b) => a - b) : [] };
		},
		whenReady(f) { if (last.ready) f(); else readyHooks.push(f); }
	};

	/* ---------------- start: after the mockup and MmConvert are there ---------------- */
	function init() {
		const v = V();
		/* start empty, not with the mockup's example */
		v.startEmpty();
		markReading();
		Bridge.onMessage(m => {
			try { onMessage(m); } catch (e) { log("page error in " + m.type + ": " + e.message + " " + (e.stack || "").split("\n")[0]); }
		});
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
		refresh(true);
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
			else if (m.type === "romInfo") onRomInfo(m);
			else if (m.type === "notice") onNotice(m);
			else if (m.type === "syxPreview") V().syxPreview(m);
			else if (m.type === "syxProgress") V().syxProgress(m);
			else if (m.type === "syxExport") V().toast(m.text);
		else if (m.type === "error") onError(m);
		else if (m.type === "result" && !m.ok && m.errors?.length && m.op !== "set" && m.op !== "modSet") V().toast(m.errors[0]);
	}
})();
