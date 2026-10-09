"use strict";
/* The Monomachine Editor page's documents and view (DESIGN-UNIFY.md phase 1), on the page as the plug-in loads it
   (deskBridge.js, deskDocs.js, deskOverlay.js, deskDrop.js, mmAdapter.js, the generated mmMockup.js, mmConvert.js, mmView.js) on
   a stand-in DOM that answers everything and does nothing, with the plug-in's messages played in:
   - derive: the view of the fixture's documents (mmViewFixture.json) is what the adapter before phase 1 showed
     (golden values captured once from its applyPending), and the background read of other slots costs nothing;
   - echoes by command id: an edit intent sent (DESIGN-UNIFY.md 4.1: every gesture), an older document arrives, the
     page keeps what it showed; the result arrives, the view is the core's; a keyed send that replaces a waiting one
     keeps its id (one entry); a song row, a sound, a paste (the page's copy of the core's clipboard) are shown at
     once; the library's slot ops are intents with nothing to show, and what they would lose the plug-in asks;
     the page never sends a whole document ("set");
   - the intent cases (doc/modern-ux/intent-cases.json, run on the core by mmDeskTest): MmView.writes of each case's
     commands (in order, a paste from the page's copy) over the view of its documents is the view of the documents
     after them;
   - a refused command shows the documents' value again;
   - the mutes, POLY and the tempo: the page's value until the command is answered (Overlay), then the machine
     document's (the core says the expected value until memory shows it, mmDeskTest); a later change on the
     machine's panel shows;
   - no clock in the page: the adapter reads no time;
   - the plug-in's notices (codex review 2026-10): a notice's key, the dialog's and the update banner's, answers that
     notice by its number (noticeAnswer's notice), the request's own id apart; a refused answer is not shown.
     The Rosetta notice (mdRosettaNotice.h): "Don't show again" first, OK last; closed another way it answers OK.
   - files dropped on the window (shared/deskDrop.js through the adapter's Drop.host): a .syx opens its import window,
     a ROM is asked about while the machine runs, samples are said to have no place on a Monomachine.
     node mmViewTest.js */
const fs = require("fs"), path = require("path");
const SK = path.join(__dirname, ".."), R = path.join(__dirname, "../../../../../..");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const fixture = JSON.parse(fs.readFileSync(path.join(__dirname, "mmViewFixture.json"), "utf8"));
const catalogue = JSON.parse(fs.readFileSync(path.join(R, "doc/modern-ux/mm-catalogue.json"), "utf8"));
const FILES = ["shared/deskBridge.js", "shared/deskDocs.js", "shared/deskOverlay.js", "shared/deskDrop.js", "mmStudio/mmAdapter.js", "mmStudio/mmMockup.js",
	"mmStudio/mmConvert.js", "mmStudio/mmView.js"].map(f => path.join(SK, f));

/* ---- the page on a stand-in: every unknown name is "any" (callable, constructible, every property any); timers
   run when the test says; the bridge's dev transport collects what the page sends ---- */
function page() {
	const any = new Proxy(function () { }, {
		get: (t, k) => k === Symbol.toPrimitive ? () => 0 : k === Symbol.iterator ? function* () { } : k === "length" ? 0 : any,
		apply: () => any, construct: () => any, has: () => false, set: () => true });
	const timers = [], sent = [], win = {}, frames = { n: 0 };
	const windowP = new Proxy(win, { get: (t, k) => k in t ? t[k] : /EventListener$/.test(String(k)) ? () => { } : undefined,
		set: (t, k, v) => { t[k] = v; return true; }, has: (t, k) => k in t });
	win.gmDev = batch => sent.push(...JSON.parse(JSON.stringify(batch)));
	const real = { Math, JSON, Object, Array, String, Number, Boolean, Set, Map, WeakMap, WeakSet, Symbol, Promise, Date, RegExp, Error, TypeError, parseInt,
		parseFloat, isNaN, isFinite, Uint8Array, Int8Array, Uint16Array, Int16Array, Uint32Array, Int32Array, Float32Array, Float64Array, DataView, ArrayBuffer,
		console, encodeURIComponent, decodeURIComponent, Infinity, NaN, undefined, URLSearchParams, structuredClone,
		atob: s => Buffer.from(s, "base64").toString("binary"), location: { hash: "", search: "", protocol: "http:" }, window: windowP,
		setTimeout: f => { timers.push(f); return timers.length; }, clearTimeout: () => { }, setInterval: () => 0, clearInterval: () => { },
		queueMicrotask: f => timers.push(f), performance: { now: () => 0 },
		/* the frames the page asks for (redraw: every canvas), counted; run at once */
		requestAnimationFrame: f => { frames.n++; f(0); return 0; }, cancelAnimationFrame: () => { } };
	const scope = new Proxy({}, {
		has: () => true,
		get: (t, k) => k === Symbol.unscopables ? undefined : k in t ? t[k] : k in real ? real[k] : any,
		set: (t, k, v) => { t[k] = v; return true; } });
	/* the mockup calls MMHost.start() at its end, before mmConvert.js and mmView.js are there (later scripts in the
	   plug-in, where start() waits for them): here it is held until they are */
	const hold = "\n;const __start = window.MMHost.start; window.MMHost.start = () => { };\n";
	const src = FILES.map(f => fs.readFileSync(f, "utf8").replace(/^"use strict";/, "") + (/mmAdapter/.test(f) ? hold : "")).join("\n;\n")
		+ "\n;__start();\nreturn { S: () => S, Overlay, MmView, docStore, storeDoc, MM_SEAM, Dlg, Banner, mockup: { mutApply, genLive, genEnd, setMachine, songAction, secAction } };";
	const out = new Function("scope", "with (scope) {\n" + src + "\n}")(scope);
	const run = () => { for (let n = 0; timers.length && n < 10000; n++) timers.shift()(); };
	run();
	return Object.assign(out, { win, sent, run, frames, host: win.MMHost, recv: msgs => { win.gm.recv(msgs); run(); } });
}
const plain = v => JSON.parse(JSON.stringify(v, (k, x) => x instanceof Set ? [...x].sort((a, b) => a - b) : x instanceof Map ? [...x] : x === Infinity ? "inf" : x));
/* the page's document members, as the golden values hold them */
function members(S) {
	const T = t => ({ m: t.m, name: t.name, v: t.v, lev: t.lev, out: t.out, inp: t.inp, trigpos: t.trigpos, port: t.port, leg: t.leg, assign: t.assign,
		cc: t.cc, ch: t.ch, mute: t.mute, steps: t.steps, slide: t.slide, swing: t.swing, arp: t.arp, tr: t.tr });
	return plain({ tracks: S.tracks.map(T), midi: S.midi.map(T), locks: S.locks, len: S.len, mult: S.mult, swingAmt: S.swingAmt, patTrn: S.patTrn,
		routing: S.routing, multi: S.multi, menv: S.menv, song: S.song, songs: S.songs, mmap: S.mmap, bpm: S.bpm, pat: S.pat, kit: S.kit, kitState: S.kitState,
		workName: S.workName, mode: S.mode, queued: S.queued, plays: S.plays, patKit: S.patKit, patInfo: S.patInfo,
		kits: S.kits.map(k => ({ name: k.name, empty: k.empty, has: !!k.data })) });
}
function firstDiff(a, b, at = "") {
	if (JSON.stringify(a) === JSON.stringify(b)) return null;
	if (a && b && typeof a === "object" && typeof b === "object")
		for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) { const d = firstDiff(a[k], b[k], at + "." + k); if (d) return d; }
	return at + ": " + String(JSON.stringify(a)).slice(0, 60) + " vs " + String(JSON.stringify(b)).slice(0, 60);
}
const copy = o => JSON.parse(JSON.stringify(o));
const machine = (patch = {}) => Object.assign(copy(fixture.machine), patch);
const docMsg = (kind, slot) => fixture.docs.find(m => m.kind === kind && m.slot === slot);
/* a page with the fixture's documents shown */
function loaded() {
	const p = page();
	p.recv([{ type: "catalogue", doc: catalogue }]);
	p.recv([{ type: "machine", doc: fixture.machine }]);
	p.recv(fixture.docs);
	p.sent.length = 0;
	return p;
}
const lastSet = (p, kind) => [...p.sent].reverse().find(m => m.op === "set" && m.kind === kind);
const lastOp = (p, op) => [...p.sent].reverse().find(m => m.op === op);
const result = (m, ok = true, errors = []) => ({ type: "result", id: m.id, op: m.op, ok, errors });

/* ---- the seam as data (src/53-seam.js, review finding 16): the loaded page's host and view are its lists ---- */
{
	const p = page(), sorted = a => [...a].sort().join();
	check(sorted(Object.keys(p.host)) === sorted(p.MM_SEAM.host), "window.MMHost implements exactly the seam's host calls (53-seam.js)");
	check(sorted(Object.keys(p.win.MMView)) === sorted(p.MM_SEAM.view), "window.MMView gives exactly the seam's view members (53-seam.js)");
}

/* ---- derive: what the adapter before phase 1 showed ---- */
{
	const p = loaded(), S = p.S();
	const d = firstDiff(members(S), fixture.golden);
	check(!d, "derived from the fixture's documents: the page's members are the golden ones" + (d ? " (" + d + ")" : ""));
	check(S.tracks[0].mute && !S.tracks[1].mute && S.tracks[2].mute && S.midi[1].mute && S.bpm === 121.5 && S.pat === 0 && S.kit === 0,
		"the machine's mutes (T1 T3 M2), tempo and current slots");
	/* the store as the adapter keeps it: deriving twice from the same documents shares the members (show is cheap) */
	const store = p.docStore([{ kind: "pattern", at: "patterns" }, { kind: "kit", at: "kits" }, { kind: "song", at: "songs" },
		{ kind: "global", at: "globals" }, { kind: "workingKit", working: "workingKit" }]);
	const docs = { patterns: {}, kits: {}, songs: {}, globals: {}, workingKit: null, sources: {}, machine: fixture.machine };
	fixture.docs.forEach(m => p.storeDoc(docs, m, store));
	const a = p.MmView.derive(docs), b = p.MmView.derive(Object.assign({}, docs, { patterns: Object.assign({ 77: docMsg("pattern", 1).doc }, docs.patterns) }));
	check(a.ready && a.tracks.length === 6 && a.midi.length === 6 && a.tracks[0].steps === b.tracks[0].steps && a.locks === b.locks && a.tracks[0].v === b.tracks[0].v,
		"another slot's document (the background read): the current members are the same values, not derived again");
	const none = p.MmView.derive({ patterns: {}, kits: {}, songs: {}, globals: {}, workingKit: null, sources: {}, machine: fixture.machine });
	check(!none.ready && none.tracks === undefined && none.pat === 0 && none.bpm === 121.5, "without the current pattern and kit: the machine's members only");
	const edited = p.MmView.derive(docs, { songEdit: 1 });
	check(edited.songSlot === 1 && edited.song === undefined && edited.songs.slot === 1 && edited.songs.current === 2, "the song the Song workspace edits (not read yet: none)");
	/* B-030: in a DAW whose clock the machine follows (CONTROL IN TEMPO SYNC external), TEMPO is the host's */
	const g = docs.globals[1];
	if (g) {
		const follow = Object.assign({}, docs, { globals: Object.assign({}, docs.globals, { 1: Object.assign({}, g, { controlIn: Object.assign({}, g.controlIn, { tempoSync: 1 }) }) }) });
		const own = Object.assign({}, docs, { globals: Object.assign({}, docs.globals, { 1: Object.assign({}, g, { controlIn: Object.assign({}, g.controlIn, { tempoSync: 0 }) }) }) });
		const host = { type: "host", bpm: 72, follows: true };
		const daw = p.MmView.derive(follow, { host });
		check(daw.bpm === 72 && daw.hostTempo === true, "the machine follows the DAW: TEMPO is the host's 72 (" + daw.bpm + "), marked");
		check(p.MmView.derive(own, { host }).bpm === 121.5 && !p.MmView.derive(own, { host }).hostTempo, "TEMPO SYNC internal: the machine's own tempo");
		check(p.MmView.derive(follow, { host: Object.assign({}, host, { follows: false }) }).bpm === 121.5, "a host that is not followed (the standalone): the machine's tempo");
		check(p.MmView.derive(follow).bpm === 121.5, "no host message: the machine's tempo");
	} else check(false, "the fixture has the active global 2");
}

/* ---- echoes by command id ---- */
{
	const p = loaded(), S = p.S();
	const old = S.swingAmt;
	S.swingAmt = 66;
	p.host.intent("swing", { v: 66 });
	p.run();
	const sw = lastOp(p, "swing");
	check(sw && sw.v === 66 && sw.p === 0 && sw.g > 0 && !("doc" in sw) && p.Overlay.size() === 1,
		"a gesture's intent: the op with p and g (no document), shown at once (one Overlay entry)");
	p.recv([docMsg("pattern", 0)]);
	check(S.swingAmt === 66, "an older document of that pattern arrives: the page keeps what it showed");
	p.recv([{ type: "machine", doc: machine({ tempo: 121.5 }) }]);
	check(S.swingAmt === 66, "a machine document arrives: still the page's");
	const taken = copy(docMsg("pattern", 0).doc);
	taken.swingAmount = 16;
	p.recv([{ type: "doc", kind: "pattern", slot: 0, source: "dump", pending: true, doc: taken }, result(sw)]);
	check(S.swingAmt === 66 && p.Overlay.size() === 0, "the core's document, then the result: the entry leaves, the view is the core's (the same)");
	const later = copy(taken);
	later.swingAmount = 12;
	p.recv([{ type: "doc", kind: "pattern", slot: 0, source: "dump", doc: later }]);
	check(S.swingAmt === 62, "answered: a newer document from the machine shows (" + old + " -> 66 -> " + S.swingAmt + ")");

	/* a keyed send that replaces a waiting one keeps its id: one entry, the latest value */
	p.host.intent("length", { v: 32 });
	p.host.intent("length", { v: 48 });
	const before = p.sent.length;
	p.run();
	const lens = p.sent.slice(before).filter(m => m.op === "length");
	check(lens.length === 1 && lens[0].v === 48 && p.Overlay.size() === 1 && S.len === 48, "a drag: one intent a frame, the latest, one entry, shown at once");
	p.recv([docMsg("pattern", 0)]);
	check(S.len === 48, "an older document under the drag: the page keeps its length");
	const len48 = copy(later);
	len48.length = 48;
	p.recv([{ type: "doc", kind: "pattern", slot: 0, source: "dump", doc: len48 }, result(lens[0])]);
	check(S.len === 48 && p.Overlay.size() === 0, "answered: the core's length");

	/* a refused intent: the documents' value shows again */
	const patTrn = S.patTrn;
	S.patTrn = 90;
	p.host.intent("transpose", { v: 26 });
	p.run();
	p.recv([result(lastOp(p, "transpose"), false, ["The machine did not take it."])]);
	check(S.patTrn === patTrn && p.Overlay.size() === 0, `a refused intent: the page shows the documents again (PTRN ${patTrn} -> 90 -> ${S.patTrn})`);

	/* the lost update (DESIGN-UNIFY.md 1): an intent carries only what the gesture changed, never the page's copy of
	   the pattern, so a step the machine recorded meanwhile is not overwritten (mmDeskFirmwareTest record) */
	p.host.intent("step", { t: 0, s: 2, v: { n: [47], a: 1, f: 1, l: 1 } });
	p.run();
	const st = lastOp(p, "step");
	check(st && st.t === 0 && st.s === 2 && st.v.n[0] === 47 && !p.sent.some(m => m.op === "set" && m.kind === "pattern"), "a step: its own intent, no pattern document");
	check(JSON.stringify(S.tracks[0].steps[2]) === JSON.stringify({ a: 1, f: 1, l: 1, n: [47] }), "shown at once, from the intent alone (the page's own state was not changed)");
	p.recv([result(st, false, ["no"])]);

	/* the kit that plays: a level intent names the kit that plays (k), shown at once */
	p.host.intent("level", { t: 3, v: 11 });
	p.run();
	const lev = lastOp(p, "level");
	check(lev && lev.k === 0 && lev.t === 3 && S.tracks[3].lev === 11, "a level: k is the kit that plays; the view shows it at once");
	p.recv([result(lev, false, ["no"])]);
	check(S.tracks[3].lev !== 11, "refused: the kit's level again");

	/* a song row (the Song workspace edits the machine's song, S3): rowSet with s and the row in the song's units, shown
	   at once (the page's row as the view holds it) */
	const row0 = copy(S.song[0]);
	p.host.intent("rowSet", { i: 0, row: Object.assign({}, row0, { rep: 5 }) });
	p.run();
	const rs = lastOp(p, "rowSet");
	check(rs && rs.s === 2 && rs.i === 0 && rs.row.repeats === 4 && rs.row.kind === "pattern" && !("p" in rs) && S.song[0].rep === 5,
		"a song row: rowSet {s, i, row} in the song's units (REP 5 is repeats 4), shown at once");
	p.recv([result(rs, false, ["no"])]);
	check(S.song[0].rep === row0.rep, "refused: the song's row again");

	/* a sound: the machine intent names the kit that plays; its start values and the processing machine's input show at once */
	p.host.intent("machine", { t: 2, model: 13, keepFx: false });
	p.run();
	const mc = lastOp(p, "machine");
	check(mc && mc.k === 0 && mc.model === 13 && S.tracks[2].m === "FX-REVERB" && S.tracks[2].v.AMP[2] === 127 && S.tracks[2].v.FLT[1] === 127 && S.tracks[2].inp === "NEIBOR",
		"a machine: k, the machine's id; FX-REVERB with its start values, shown at once");
	p.recv([result(mc, false, ["no"])]);

	/* a paste shows at once from the page's copy of the core's clipboard (its own copy command's) */
	p.host.intent("copySteps", { t: 0, from: 0, to: 4 });
	p.host.intent("pasteSteps", { t: 3, from: 8, to: 12 });
	p.run();
	const cp = lastOp(p, "copySteps"), ps = lastOp(p, "pasteSteps");
	check(cp && ps && ps.p === 0 && JSON.stringify(S.tracks[3].steps.slice(8, 12)) === JSON.stringify(S.tracks[0].steps.slice(0, 4)),
		"copySteps then pasteSteps: the core's clipboard, the paste shown at once from the page's copy");
	p.recv([result(cp), result(ps, false, ["no"])]);

	/* the library's slot ops: intents with nothing to show (the slots come as documents); the plug-in asks */
	p.host.library("kitCopyTo", { from: 1, to: 0 });
	p.run();
	const kc = lastOp(p, "kitCopyTo");
	check(kc && kc.from === 1 && kc.to === 0 && !("k" in kc) && kc.g > 0 && p.Overlay.size() === 0, "a library op: its own arguments and g, nothing shown before the result");
	check(!p.sent.some(m => m.op === "set"), "no gesture sent a whole document");
}

/* ---- the page's own gestures are intents (DESIGN-UNIFY.md phases 4 and 5): MUTATE, GEN, the machine picker, a song
   row and the Sound page's CLEAR, through the mockup's functions; one gesture each ---- */
{
	const p = loaded(), S = p.S(), M = p.mockup;
	const sentOps = from => p.sent.slice(from).filter(m => m.op && !["ready", "audio"].includes(m.op));
	S.ws = "sound"; S.sel = 0;
	let at = p.sent.length;
	M.mutApply(false); p.run();
	const mu = sentOps(at);
	check(mu.length === 1 && mu[0].op === "params" && mu[0].k === 0 && mu[0].values.length > 0 && mu[0].values.every(([t]) => t === 0),
		"MUTATE: one params intent of the values it moved (track 1), in the kit that plays");
	M.genEnd(); p.host.commit();
	at = p.sent.length;
	M.setMachine("SID-6581"); p.run();
	const mc = sentOps(at);
	check(mc[0] && mc[0].op === "machine" && mc[0].model === 3 && mc.slice(1).every(m => m.op === "clearLane" && m.g === mc[0].g), "the machine picker: a machine intent (and its SYN lock lanes cleared, the same gesture)");
	p.host.commit();
	at = p.sent.length;
	M.secAction("clear"); p.run();
	check(sentOps(at).map(m => m.op).join() === "clearSound", "Sound's CLEAR: clearSound");
	p.host.commit();
	S.ws = "seq"; S.sel = 0;
	at = p.sent.length;
	M.genLive(false); p.run();
	const gn = sentOps(at);
	check(gn.length === 1 && gn[0].op === "steps" && gn[0].p === 0 && gn[0].rows.length === 1, "GEN: one steps intent of the range it wrote");
	M.genEnd(); p.host.commit();
	S.ws = "song"; S.songSel = 0;
	at = p.sent.length;
	M.songAction("dup"); p.run();
	const sg = sentOps(at);
	check(sg.length === 1 && sg[0].op === "rowInsert" && sg[0].s === 2 && sg[0].i === 1 && sg[0].row.kind, "a song row duplicated: rowInsert {s, i, row} in the song's units");
	check(!p.sent.some(m => m.op === "set"), "none of them sent a whole document");
}

/* ---- the mutes, POLY and the tempo: the page's until answered, then the machine document's ---- */
{
	const p = loaded(), S = p.S();
	S.tracks[1].mute = true;
	p.host.mutes();
	const mutes = p.sent.filter(m => m.op === "mute");
	check(mutes.length === 1 && mutes[0].t === 1 && mutes[0].on === true, "mute T2: only that track is sent (the others are the machine's already)");
	p.recv([{ type: "machine", doc: machine() }]);
	check(S.tracks[1].mute === true, "a machine document from before the command: T2 stays muted");
	p.recv([{ type: "machine", doc: machine({ mutes: { synth: 7, midi: 2 } }) }, result(mutes[0])]);
	check(S.tracks[1].mute === true && p.Overlay.size() === 0, "the core says it (expected), then the result: the machine's, muted");
	p.recv([{ type: "machine", doc: machine({ mutes: { synth: 5, midi: 2 } }) }]);
	check(S.tracks[1].mute === false, "a press on the machine's panel unmutes T2: it shows");
	S.tracks[1].mute = true;
	p.host.mutes();
	const again = p.sent.filter(m => m.op === "mute").pop();
	p.recv([result(again, false, ["no"])]);
	check(S.tracks[1].mute === false, "a refused mute: the machine's again");

	/* a solo drives the mutes: the machine's then are the solo's, not the page's own mutes */
	p.sent.length = 0;
	S.tracks[4].solo = true;
	p.host.mutes();
	const soloed = p.sent.filter(m => m.op === "mute").map(m => m.t + ":" + m.on).join(" ");
	p.recv([{ type: "machine", doc: machine({ mutes: { synth: 0b101111, midi: 2 } }) }]);
	check(soloed === "1:true 3:true 5:true" && S.tracks[1].mute === false && S.tracks[3].mute === false, "solo T5: the others are muted on the machine, the page's own mutes stay");

	S.mode = "poly";
	p.host.keyMode("poly");
	const poly = p.sent.filter(m => m.op === "poly").pop();
	p.recv([{ type: "machine", doc: machine({ poly: false }) }]);
	check(poly && poly.on === true && S.mode === "poly", "POLY: sent; a machine document from before it: still POLY");
	p.recv([{ type: "machine", doc: machine({ poly: true }) }, result(poly)]);
	p.recv([{ type: "machine", doc: machine({ poly: false }) }]);
	check(S.mode === "normal", "answered; then the machine leaves POLY: the keyboard is normal again");

	S.bpm = 133;
	p.host.tempo(133);
	p.run();
	const tempo = p.sent.filter(m => m.op === "tempo").pop();
	p.recv([{ type: "machine", doc: machine({ tempo: 121.5 }) }]);
	check(tempo && S.bpm === 133, "tempo: sent; the machine's old tempo arrives: still 133");
	p.recv([{ type: "machine", doc: machine({ tempo: 133 }) }, result(tempo)]);
	p.recv([{ type: "machine", doc: machine({ tempo: 125 }) }]);
	check(S.bpm === 125, "answered; then the machine's own tempo shows");
	check(![FILES[3], FILES[6]].some(f => /performance\.now|Date\.now|setInterval/.test(fs.readFileSync(f, "utf8").replace(/\/\*[\s\S]*?\*\//g, ""))),
		"no clock in the adapter or the view (no time read, no interval): echoes by id, never by time");
}

/* ---- a reset: the engine changed, the documents start over ---- */
{
	const p = loaded(), S = p.S();
	S.swingAmt = 70;
	p.host.intent("swing", { v: 70 });
	p.run();
	p.recv([{ type: "reset" }]);
	check(p.Overlay.size() === 0, "a reset: nothing of the old engine waits");
	p.recv([{ type: "machine", doc: fixture.machine }]);
	p.recv(fixture.docs);
	check(S.swingAmt === fixture.golden.swingAmt, "the documents again: the view is theirs");
}
/* ---- a reset leaves nothing of the old engine on screen, and no modSet waiting (release review 2026-10-04, code-js S5) ---- */
{
	const fresh = page().S(), p = loaded(), S = p.S();
	const steps = s => JSON.stringify(plain(s.tracks.map(t => t.steps))), kit = s => s.tracks.map(t => t.m).join();
	check(steps(S) !== steps(fresh), "the fixture's pattern is on screen before the reset");
	p.recv([{ type: "reset" }]);
	check(steps(S) === steps(fresh) && kit(S) === kit(fresh) && S.workName === "", "after the reset (no machine yet: HW MIDI connecting) the old engine's pattern and kit are gone, as at the start");
	/* the Control workspace is not the machine's: a CC link and a MIDI track's target survive the reset (a machine
	   restart sends no mod message to bring them back) */
	{
		const c = loaded(), C = c.S();
		C.ctl.sources.push({ id: "cc99", kind: "cc", cc: 99, label: "Knob 99", val: 64 }, { id: "L7", kind: "lfo", label: "LFO 7", val: 64, SHAPE: 0, RATE: "1/4", DEPTH: 50 });
		C.ctl.links.push({ src: "cc99", t: 0, pid: "SYN.1", min: 10, max: 90, curve: "exp", inv: true }, { src: "L7", t: 6, pid: "MID.0", min: 0, max: 127, curve: "lin", inv: false });
		const before = JSON.stringify({ s: C.ctl.sources, l: C.ctl.links });
		c.recv([{ type: "reset" }]);
		const after = c.S().ctl;
		check(JSON.stringify({ s: after.sources, l: after.links }) === before, "a reset keeps the Control workspace: the CC link (min, max, curve) and the track 7 target are there");
	}
	/* a modSet on its way when the engine changes is never answered: the new engine's setup is taken */
	const q = loaded(), Q = q.S();
	const setup = { sources: [{ id: "L1", kind: "lfo", label: "LFO 1", SHAPE: 0, RATE: "1/4", DEPTH: 80 }], links: [] };
	q.host.modulators(setup); q.run();
	check(q.sent.some(m => m.op === "modSet"), "a modSet is on its way");
	q.recv([{ type: "reset" }]);
	const theirs = { schema: "mm-desk/modulators", version: 1, sources: [{ id: "R9", label: "RND 9", kind: "random", shape: 0, rate: "1", depth: 100, smooth: 40 }], links: [] };
	q.recv([{ type: "mod", doc: theirs, values: [64], ccPerSecond: 0 }]);
	check(q.win.MMView.ctlSetup().sources.some(x => x.id === "R9"), "after the reset the plug-in's setup is taken (the old modSet no longer holds it off)");
	/* the same when the machine goes from not ready to ready */
	const r = loaded();
	r.host.modulators(setup); r.run();
	r.recv([{ type: "machine", doc: machine({ input: false }) }, { type: "machine", doc: machine({ input: true }) }]);
	r.recv([{ type: "mod", doc: theirs, values: [64], ccPerSecond: 0 }]);
	check(r.win.MMView.ctlSetup().sources.some(x => x.id === "R9"), "not ready -> ready: the plug-in's setup is taken");
}

/* ---- the intent cases: the page's optimistic writes are the core's edit (DESIGN-UNIFY.md 4.2) ---- */
{
	const cases = JSON.parse(fs.readFileSync(path.join(R, "doc/modern-ux/intent-cases.json"), "utf8")).mm;
	const p = loaded();
	const setIn = (o, at, v) => { let x = o; at.slice(0, -1).forEach(k => { x = x[k]; }); x[at[at.length - 1]] = v; };
	const patched = (docs, entries) => { const d = copy(docs); for (const [name, at, v] of entries) if (at.length) setIn(d[name], at, copy(v)); else d[name] = copy(v); return d; };
	const base = patched(cases.docs, cases.given);
	/* the cases' documents by name, as the page stores them (the schema says the kind) */
	const AT = { "mm-desk/pattern": "patterns", "mm-desk/kit": "kits", "mm-desk/song": "songs", "mm-desk/global": "globals" };
	const docsOf = j => {
		const d = { patterns: {}, kits: {}, songs: {}, globals: {}, workingKit: { slot: j.workingKit.slot, doc: j.workingKit }, sources: {}, machine: fixture.machine };
		for (const [name, doc] of Object.entries(j)) if (name !== "workingKit") d[AT[doc.schema]][doc.slot] = doc;
		return d;
	};
	/* the view's members an edit can change, in one form (Sets and Maps sorted, object keys sorted) */
	const canon = x => x instanceof Set ? { set: [...x].sort((a, b) => a - b) } : x instanceof Map ? { map: [...x].sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0).map(([k, y]) => [k, canon(y)]) }
		: Array.isArray(x) ? x.map(canon) : x && typeof x === "object" ? Object.fromEntries(Object.keys(x).sort().map(k => [k, canon(x[k])])) : x;
	const shown = v => canon({ tracks: v.tracks, midi: v.midi, locks: v.locks, len: v.len, mult: v.mult, swingAmt: v.swingAmt, patTrn: v.patTrn, routing: v.routing,
		menv: v.menv, multi: v.multi, workName: v.workName, song: v.song, mmap: v.mmap });
	let ran = 0, optimistic = 0;
	for (const c of cases.cases) {
		if (c.refused || c.optimistic === false) continue;
		ran++;
		/* each command over the view the ones before it made (Overlay), a paste from the page's copy of the clipboard */
		let v = p.MmView.own(p.MmView.derive(docsOf(base))), clip = null, any = false;
		for (const cmd of c.commands || [c.command]) {
			clip = p.MmView.copied(v, cmd) || clip;
			const w = p.MmView.writes(v, cmd, clip);
			any = any || w.length > 0;
			p.Overlay.clear();
			p.Overlay.add(1, w);
			v = p.Overlay.over(p.MmView.own(structuredClone(v)));
		}
		p.Overlay.clear();
		const want = p.MmView.derive(docsOf(patched(base, c.after)));
		if (c.after.length) optimistic++;
		const d = firstDiff(JSON.parse(JSON.stringify(shown(v))), JSON.parse(JSON.stringify(shown(want))));
		check(!d && any === (c.after.length > 0), "writes: " + c.name + (d ? " (" + d + ")" : any ? "" : " (no writes)"));
	}
	check(ran >= 75 && optimistic >= 70, `every intent case the view can show was checked (${ran}, ${optimistic} with writes)`);
	check(p.MmView.derive(docsOf(base)).ready, "the derive of the cases' documents is the machine's view (ready)");
}

/* ---- B-036: the playhead's cost per step on the Sequence page (as the Machinedrum Editor's B-014): no canvas redrawn
   per step, the same step twice is nothing ---- */
{
	const p = page(), S = p.S(), V = p.win.MMView;
	S.ws = "seq";
	V.setPlaying(true); p.run();
	p.frames.n = 0;
	for (let s = 0; s < 32; s++) { V.setStep(s); V.setStep(s); p.run(); }
	check(p.frames.n === 0, `32 steps playing: ${p.frames.n} canvas redraws (none: only the soft playhead moves)`);
	V.setPlaying(false); p.run();
}

/* ---- the plug-in's notices over the real bridge (codex review 2026-10: Bridge.send's request id had replaced the
   notice's number since 0.3.0, so no key reached the plug-in): the dialog's and the banner's answers name the notice ---- */
{
	const p = loaded(), items = [], banners = [], toasts = [];
	p.Dlg.show = item => items.push(item);	/* the dialog's queue: the item the adapter asks (its cancel is its last key) */
	p.Banner.show = (m, answer) => banners.push({ m, answer });
	p.win.MMView.toast = t => toasts.push(t);
	p.recv([{ type: "notice", id: 41, title: "Overwrite K05?", text: "", buttons: ["Overwrite", "Cancel"] }]);
	check(items.length === 1 && items[0].notice, "a notice is a dialog item the plug-in waits on");
	p.sent.length = 0;
	items[0].cancel();
	const a = p.sent.find(m => m.op === "noticeAnswer");
	check(a && a.notice === 41 && a.button === 1 && typeof a.id === "number" && a.id !== 41,
		"closed another way, the notice is answered by its last key: notice 41, the request's own id apart: " + JSON.stringify(a));
	p.recv([{ type: "notice", id: 42, title: "Update available: 9.9.9", text: "", buttons: ["Update", "Later"], modal: false }]);
	check(banners.length === 1 && banners[0].m.id === 42 && items.length === 1, "\"modal\": false is the banner, not the dialog");
	p.sent.length = 0;
	banners[0].answer(0);
	const b = p.sent.find(m => m.op === "noticeAnswer");
	check(b && b.notice === 42 && b.button === 0 && b.id !== 42, "Update on the banner answers notice 42: " + JSON.stringify(b));
	p.recv([{ type: "result", op: "noticeAnswer", id: b.id, ok: false, errors: ["notice 42 is not waiting for an answer"], note: "" }]);
	check(toasts.length === 0, "a refused answer shows nothing (the log only)");
	/* Rosetta (mdRosettaNotice.h): "Don't show again" first, OK last. Closed another way (Esc, a click outside) the page
	   answers with the last key, OK, which keeps nothing; only its own key sends the first button, which the plug-in keeps */
	const asked = [];
	p.win.MMView.ask = (html, btns, cls, item) => asked.push({ html, btns, item });
	const rosetta = { type: "notice", id: 43, title: "Running under Rosetta", text: "This is the Intel version of the editor running translated on an Apple silicon Mac. It uses about twice the CPU.", buttons: ["Don't show again", "OK"] };
	p.recv([rosetta]);
	check(asked.length === 1 && /uses about twice the CPU/.test(asked[0].html) && asked[0].item.notice && asked[0].btns.length === 2
		&& asked[0].btns[0][0] === "Don't show again" && asked[0].btns[1][0] === "OK", "the Rosetta notice is a dialog the plug-in waits on: its text, Don't show again and OK");
	p.sent.length = 0;
	asked[0].item.cancel();
	const closedRosetta = p.sent.find(m => m.op === "noticeAnswer");
	check(closedRosetta && closedRosetta.notice === 43 && closedRosetta.button === 1, "closed another way: answered by OK, the last key (nothing is kept): " + JSON.stringify(closedRosetta));
	p.recv([Object.assign({}, rosetta, { id: 44 })]);
	p.sent.length = 0;
	asked[1].btns[0][2]();
	const neverRosetta = p.sent.find(m => m.op === "noticeAnswer");
	check(neverRosetta && neverRosetta.notice === 44 && neverRosetta.button === 0, "Don't show again answers its own notice with button 0 (the plug-in keeps it): " + JSON.stringify(neverRosetta));
}

/* ---- files dropped on the window (shared/deskDrop.js, the adapter's Drop.host) ---- */
{
	const p = loaded(), asked = [], toasts = [];
	p.win.MMView.toast = t => toasts.push(t);
	p.win.MMView.ask = (html, btns) => asked.push({ html, btns });
	p.recv([{ type: "drop", drop: 4, items: [{ n: 0, kind: "sample", name: "kick.wav" }, { n: 1, kind: "sysex", name: "kits.syx" }, { n: 2, kind: "unknown", name: "a.txt" }], x: 10, y: 20 }]);
	const drops = p.sent.filter(m => /^drop/.test(m.op));
	check(drops.length === 1 && drops[0].op === "dropSyx" && drops[0].drop === 4 && drops[0].n === 1, "a .syx among a sample and a text file: dropSyx {drop 4, n 1} only: " + JSON.stringify(drops));
	check(toasts.length === 1 && /^The Monomachine has no samples\. Not a ROM .*: a\.txt\.$/.test(toasts[0]), "one toast: no samples on a Monomachine, the text file named: " + toasts.join(" | "));
	p.sent.length = 0;
	p.recv([{ type: "drop", drop: 5, items: [{ n: 0, kind: "rom", name: "mm.bin" }], x: 0, y: 0 }]);
	check(!p.sent.some(m => m.op === "dropRom") && asked.length === 1 && /Install <b>mm\.bin<\/b> as the firmware\?/.test(asked[0].html), "a ROM while the machine runs: asked first");
	asked[0].btns.find(b => b[0] === "Install")[2]();
	p.run();
	check(p.sent.some(m => m.op === "dropRom" && m.drop === 5 && m.n === 0), "Install: dropRom {drop 5, n 0}");
}

console.log(failures ? `mmViewTest: ${failures} failure(s)` : "mmViewTest: PASS");
process.exit(failures ? 1 : 0);
