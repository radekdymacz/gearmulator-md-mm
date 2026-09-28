"use strict";
/* The Machinedrum page's model (P6): deriveView is pure (the same documents give the same view,
   whatever the page's own Docs hold) and the optimistic overlay keeps a gesture's values over a
   new derivation until the command's answer, then leaves.
     node mdDeskModelTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const ctx = vm.createContext({ console, queueMicrotask });
vm.runInContext(fs.readFileSync(path.join(__dirname, "mdDeskModel.js"), "utf8")
	+ "\nfunction nameOf(m) { return m; }\nconst S = { soloSet: new Set() };"
	+ "\nthis.T = { deriveView, Overlay, view, Docs, S, get V() { return V; }, set V(v) { V = v; } };", ctx);
const { deriveView, Overlay, Docs, S } = ctx.T;
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const track = m => ({ machine: m, model: 0, level: 100, synth: Array(8).fill(10), effects: Array(8).fill(20), routing: Array(8).fill(30),
	lfo: { track: 0, param: 0, shape1: 0, shape2: 0, update: 0 }, muteGroup: null, trigGroup: null });
const pattern = { kit: 3, length: 16, totalLength: 16, tempoMultiplier: "1X", swingAmount: 0, accentAmount: 0,
	accent: { editAll: 0, steps: [] }, slide: { editAll: 0, steps: [] },
	tracks: Array.from({ length: 16 }, (_, i) => ({ trigs: i === 0 ? [0, 4] : [], accent: [], slide: [] })), locks: [{ track: 0, param: 0, steps: [[4, 99]] }] };
const kit = { name: "TEST", tracks: Array.from({ length: 16 }, () => track("GND-EMPTY")),
	masterFx: { rhythmEcho: Array(8).fill(0), gateBox: Array(8).fill(0), eq: Array(8).fill(0), dynamix: Array(8).fill(0) } };
const docs = () => ({ patterns: { 5: pattern }, kits: { 3: kit }, songs: {}, global: null, catalogue: null, telemetry: null,
	machine: { pattern: { current: 5 }, kit: { current: 3, working: "clean" }, song: {}, desk: { playing: false }, lifecycle: "ready", capabilities: {} } });

/* pure */
const a = deriveView(docs(), S);
Docs.patterns = { 1: { ...pattern, tracks: [] } };	// the page's own documents must not matter
const b = deriveView(docs(), S);
check(a.pat === 5 && b.pat === 5 && a.kit === 3 && a.tracks[0].trigs[4] && b.tracks[0].trigs[4], "deriveView reads only the documents it is given");
check(deriveView({ ...docs(), telemetry: { playing: true, recording: false } }, S).playing, "the telemetry is a document: it gives the transport");

/* overlay */
let V = Overlay.over(deriveView(docs(), S));
V.tracks[0].trigs[8] = true;
V.tracks[0].acc.add(8);
V.locks.get("1:#1") || V.locks.set("1:#1", new Map());
V.locks.get("1:#1").set(8, 64);
Overlay.sent(7);
check(Overlay.size() === 4, "a gesture's writes are the overlay, owned by the command sent after them");
V = Overlay.over(deriveView(docs(), S));
check(V.tracks[0].trigs[8] && V.tracks[0].acc.has(8) && V.locks.get("1:#1").get(8) === 64, "a new derivation keeps them until the answer");
V = Overlay.over(deriveView(docs(), S));
check(V.tracks[0].acc.has(8) && V.tracks[0].acc.size === 1, "applying an entry again changes nothing (values, not toggles)");
check(Overlay.answered(7) && Overlay.size() === 0, "the answer takes them out");
V = Overlay.over(deriveView(docs(), S));
check(!V.tracks[0].trigs[8] && !V.tracks[0].acc.has(8), "then the view is the documents' again");
V.tracks[0].mute = true;
setTimeout(() => {
	check(Overlay.size() === 0, "a write no command follows is not kept");
	for (const [k, m] of [...V.locks]) { m.delete(4); if (!m.size) V.locks.delete(k); }
	Overlay.sent(8);
	V = Overlay.over(deriveView(docs(), S));
	check(V.locks.size === 0, "a lock cleared through the view's iterator is in the overlay too");
	console.log("mdDeskModelTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
	process.exit(failures ? 1 : 0);
}, 0);
