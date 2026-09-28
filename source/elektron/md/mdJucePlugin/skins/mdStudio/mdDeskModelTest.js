"use strict";
/* The Machinedrum page's model (P6): deriveView is pure (the same documents give the same view,
   whatever the page's own Docs hold), the kit that plays comes from the working kit document, and
   the optimistic overlay is explicit: a command's [path, value] writes are kept over every new
   derivation until that command's answer, then leave, and only while the view shows the document
   the command edits. The document shape is written once (EMPTY_DOCS, storeDoc, resetDocs); the
   capabilities are nested and an unknown one is not allowed; the enumerations are the catalogue's.
     node mdDeskModelTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const ctx = vm.createContext({ console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "mdDeskModel.js"), "utf8")
	+ "\nconst S = { soloSet: new Set() };"
	+ "\nthis.T = { deriveView, Overlay, view, Docs, S, EMPTY_DOCS, storeDoc, resetDocs, canDo, docOf, multFactor, mfxName, nameOf, get V() { return V; } };", ctx);
const { deriveView, Overlay, Docs, S, EMPTY_DOCS, storeDoc, resetDocs, canDo, docOf, multFactor, nameOf } = ctx.T;
const DELETE = Overlay.DELETE;
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const track = m => ({ machine: m, model: 0, level: 100, synth: Array(8).fill(10), effects: Array(8).fill(20), routing: Array(8).fill(30),
	lfo: { track: 0, param: 0, shape1: 0, shape2: 0, update: 0 }, muteGroup: null, trigGroup: null });
const pattern = { kit: 3, length: 16, totalLength: 16, tempoMultiplier: "1X", swingAmount: 0, accentAmount: 0,
	accent: { editAll: 0, steps: [] }, slide: { editAll: 0, steps: [] },
	tracks: Array.from({ length: 16 }, (_, i) => ({ trigs: i === 0 ? [0, 4] : [], accent: [], slide: [] })), locks: [{ track: 0, param: 0, steps: [[4, 99]] }] };
const kit = (name, level = 100) => ({ name, tracks: Array.from({ length: 16 }, () => ({ ...track("GND-EMPTY"), level })),
	masterFx: { rhythmEcho: Array(8).fill(0), gateBox: Array(8).fill(1), eq: Array(8).fill(2), dynamix: Array(8).fill(3) } });
/* the catalogue's enumerations (contract-r4: the C++ tables) */
const enums = { tempoMultipliers: ["1X", "2X", "3/4X", "3/2X"], masterFx: ["rhythmEcho", "gateBox", "eq", "dynamix"], outputs: ["MAIN", "A", "B", "C", "D", "E", "F"],
	lfoFields: ["track", "param", "shape1", "shape2", "update"], lfoUpdates: ["FREE", "TRIG", "HOLD"], lfoParams: { SPD: 21, DEPTH: 22, SHMIX: 23 } };
const catalogue = { schema: "md-desk/machines", version: 1, machines: [], enums };
const caps = { engine: "emu", label: "EMU OS 1.63", about: "", can: { transport: true, chains: false }, reasons: { chains: "keys only" }, values: { dumps: "direct" } };
const docs = (extra = {}) => ({ patterns: { 5: pattern }, kits: { 3: kit("STORED") }, songs: {}, global: null, catalogue, telemetry: null,
	workingKit: null, sources: { "kit:3": "dump" },
	machine: { pattern: { current: 5 }, kit: { current: 3, working: "clean" }, song: {}, desk: { playing: true, recording: true }, lifecycle: "ready", input: true, midi: true,
		lifecycleText: "", capabilities: caps, clipboard: { steps: false, sound: true, songRow: false, kit: 7, pattern: null } }, ...extra });

/* pure */
const a = deriveView(docs(), S);
Docs.patterns = { 1: { ...pattern, tracks: [] } };	// the page's own documents must not matter
const b = deriveView(docs(), S);
check(a.pat === 5 && b.pat === 5 && a.kit === 3 && a.tracks[0].trigs[4] && b.tracks[0].trigs[4], "deriveView reads only the documents it is given");
check(deriveView(docs({ telemetry: { playing: true, recording: false } }), S).playing, "the telemetry is a document: it gives the transport");
check(!a.playing && !a.rec, "the transport is the telemetry's only (the machine document's desk.playing is not read)");
check(deriveView(docs({ global: { extendedMode: false, tempo: 120, routing: Array(16).fill("MAIN") } }), S).mode === "CLASSIC" && a.mode === "EXTENDED", "the mode is the global's only");
check(a.input && a.midi && a.clipboard.kit === 7 && a.clipboard.pattern === null && a.clipboard.sound, "input, midi and the clipboard are the machine document's");
check(canDo(a, "transport") && !canDo(a, "chains") && !canDo(a, "noSuchCapability") && a.caps.reasons.chains === "keys only", "capabilities nested: can[name] true only; an unknown name is not allowed");
check(nameOf("TRX-BD") === "Bass drum" && nameOf("MID-03") === "MIDI channel 3" && nameOf("ROM-25") === "ROM sample 25", "nameOf is the model's own (no call up into the app)");
check(!canDo(deriveView(EMPTY_DOCS, S), "transport"), "no machine document: nothing is allowed");
check(a.tracks[0].lfo.UPDTE === "FREE" && a.tracks[0].lfo.SPD === 30 && a.mfx.gate.v.DVOL === 1 && a.mfx.dyn.v.ATCK === 3, "LFO updates, LFO parameters and the master effects come from the catalogue's enumerations");
check(multFactor("3/4X") === 0.75 && multFactor("2X") === 2 && multFactor("1X") === 1, "a tempo multiplier's factor is read from its name");
const e = deriveView(EMPTY_DOCS, S);
check(!e.loaded && e.tracks.length === 16 && e.lifecycle === "booting" && e.locks.size === 0, "the view before any document is deriveView of the empty documents");

/* the document shape, once */
const d0 = { ...JSON.parse(JSON.stringify(EMPTY_DOCS)), catalogue, learn: { mappings: [] } };
check(storeDoc(d0, { type: "doc", kind: "pattern", slot: 9, doc: pattern, source: "dump" }) === 9 && d0.patterns[9] === pattern && d0.sources["pattern:9"] === "dump", "a doc message is stored where its kind keeps it");
storeDoc(d0, { type: "doc", kind: "workingKit", slot: 3, source: "memory", pending: true, doc: kit("W") });
check(d0.workingKit.slot === 3 && d0.workingKit.pending && d0.workingKit.doc.name === "W" && !("workingKit:3" in d0.sources), "the working kit is its own document");
check(storeDoc(d0, { type: "doc", kind: "sampleSlot", slot: 1, doc: {} }) === null, "a kind the page does not know is not stored");
resetDocs(d0);
check(!Object.keys(d0.patterns).length && d0.workingKit === null && d0.catalogue === catalogue && d0.learn && Object.keys(EMPTY_DOCS).every(k => k in d0), "a reset starts the engine's documents over (EMPTY_DOCS), the catalogue and learn stay");

/* the working kit */
check(a.kitNames[3] === "STORED" && a.kitSource === "dump", "no working kit yet: the current kit is the stored slot");
const w = deriveView(docs({ workingKit: { slot: 3, source: "memory", pending: false, doc: kit("PLAYING", 77) } }), S);
check(w.kitNames[3] === "PLAYING" && w.tracks[0].level === 77 && w.kitSource === "memory", "the kit that plays is the working kit document");
const o = deriveView(docs({ workingKit: { slot: 9, source: "memory", pending: false, doc: kit("OLD", 1) } }), S);
check(o.kitNames[3] === "STORED" && o.tracks[0].level === 100, "a working kit of another slot (a kit change under way) is not the current kit");

/* the explicit overlay */
let V = Overlay.over(deriveView(docs(), S));
check(Overlay.size() === 0 && !V.tracks[0].trigs[8], "no command, no overlay");
Overlay.add(7, [[["tracks", 0, "trigs", 8], true], [["tracks", 0, "acc", 8], true], [["locks", "1:#1", 8], 64], [["bpm"], 133.5]]);
check(Overlay.size() === 4, "a command's writes are the overlay, owned by its id");
V = Overlay.over(deriveView(docs(), S));
check(V.tracks[0].trigs[8] && V.tracks[0].acc.has(8) && V.locks.get("1:#1").get(8) === 64 && V.bpm === 133.5, "a new derivation shows them (a write into a missing lock lane makes it)");
V = Overlay.over(V);
check(V.tracks[0].acc.size === 1 && V.locks.get("1:#1").size === 1, "applying an entry again changes nothing (values, not toggles)");
Overlay.add(8, [[["bpm"], 140]]);
check(Overlay.size() === 4, "a later write of the same path takes it over");
check(Overlay.answered(7) && Overlay.size() === 1, "the answer takes its own entries out");
V = Overlay.over(deriveView(docs(), S));
check(!V.tracks[0].trigs[8] && !V.tracks[0].acc.has(8) && !V.locks.has("1:#1") && V.bpm === 140, "then the view is the documents' again, with the later command's write");
check(Overlay.answered(8) && !Overlay.answered(8) && Overlay.size() === 0, "an answer leaves nothing behind");
Overlay.add(9, [[["locks", "0:#1", 4], DELETE], [["tracks", 0, "trigs", 4], false]]);
V = Overlay.over(deriveView(docs(), S));
check(!V.locks.has("0:#1") && !V.tracks[0].trigs[4], "a delete that empties a lock lane removes the lane");
Overlay.clear();
/* entries carry the document their command edits */
check(JSON.stringify(docOf("trig", { p: 5, t: 0, s: 3 })) === '{"kind":"pattern","slot":5}' && docOf("param", { k: 3, t: 0, i: 1, v: 2 }).kind === "kit"
	&& docOf("rowSet", { s: 2, i: 0, row: {} }).slot === 2 && docOf("tempo", { bpm: 120 }) === null, "a command's document from its arguments");
Overlay.add(12, [[["tracks", 0, "trigs", 9], true]], docOf("trig", { p: 5, t: 0, s: 9 }));
Overlay.add(13, [[["tracks", 0, "level"], 5]], docOf("level", { k: 3, t: 0, v: 5 }));
V = Overlay.over(deriveView(docs(), S));
check(V.tracks[0].trigs[9] && V.tracks[0].level === 5, "entries show on the document they edit");
V = Overlay.over(deriveView(docs({ machine: { ...docs().machine, pattern: { current: 6 }, kit: { current: 4, working: "clean" } }, patterns: { 5: pattern, 6: pattern }, kits: { 3: kit("A"), 4: kit("B") } }), S));
check(!V.tracks[0].trigs[9] && V.tracks[0].level === 100 && Overlay.size() === 2, "a pattern or kit switch under a waiting edit: the new documents show without it");
Overlay.clear();
Overlay.add(10, [[["tracks", 99, "mute"], true], [["song", 3, "pat"], 7]]);
V = Overlay.over(deriveView(docs(), S));
check(V.tracks.length === 16 && !V.song[3], "a write into a part of the view that is not there changes nothing");
Overlay.clear();
const row = { pat: 2, rep: 1 };
Overlay.add(11, [[["song", 0], row]]);
row.rep = 5;
V = Overlay.over(deriveView(docs(), S));
check(V.song[0].pat === 2 && V.song[0].rep === 1, "an entry holds a copy of its value");
Overlay.clear();

console.log("mdDeskModelTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
process.exit(failures ? 1 : 0);
