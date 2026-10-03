"use strict";
/* The editors' shared page modules (DESIGN-UNIFY.md phase 0): the document store (deskDocs.js) and the two
   optimistic layers (deskOverlay.js). Overlay: writes through objects, arrays, Maps and Sets, DELETE, the
   Map-of-Maps rule, values never toggles, a later write of a path owns it, an answer takes its own entries,
   entries scoped to the document their command edits. DocOverlay: the page's documents over the published
   ones until answered, the latest for a slot last, the published documents never written.
     node deskOverlayTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const ctx = vm.createContext({ console });
vm.runInContext(["deskDocs.js", "deskOverlay.js"].map(f => fs.readFileSync(path.join(__dirname, f), "utf8")).join("\n")
	+ "\nthis.T = { DOC_KINDS, DOC_STORE, docStore, storeDoc, docOf, shows, Overlay, DocOverlay };", ctx);
const { DOC_KINDS, docStore, storeDoc, docOf, shows, Overlay, DocOverlay } = ctx.T;
const DELETE = Overlay.DELETE;
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const docs = () => ({ patterns: {}, kits: {}, songs: {}, global: null, workingKit: null, sources: {} });

/* ---- storeDoc over a kinds list ---- */
{
	const d = docs();
	check(storeDoc(d, { kind: "pattern", slot: 4, doc: { a: 1 }, source: "dump" }) === 4 && d.patterns[4].a === 1 && d.sources["pattern:4"] === "dump", "a slotted kind: docs[at][slot], its source kept");
	check(storeDoc(d, { kind: "global", doc: { slot: 2, g: 1 } }) === 2 && d.global.g === 1, "one document: the slot from the document itself");
	storeDoc(d, { kind: "workingKit", slot: 3, source: "memory", pending: true, doc: { k: 1 } });
	check(same(d.workingKit, { slot: 3, source: "memory", pending: true, doc: { k: 1 } }) && !("workingKit:3" in d.sources), "the kit that plays: slot, source and pending with it, no source entry");
	check(storeDoc(d, { kind: "sampleSlot", slot: 1, doc: {} }) === null, "a kind the page does not know is not stored");
	const mine = docStore([...DOC_KINDS, { kind: "multiMap", one: "multiMap" }]), d2 = Object.assign(docs(), { multiMap: null });
	check(storeDoc(d2, { kind: "multiMap", doc: { slot: 0, m: 1 } }, mine) === 0 && d2.multiMap.m === 1 && storeDoc(d2, { kind: "multiMap", doc: { slot: 0 } }) === null,
		"a page's own kinds list adds a kind; the default list does not know it");
}

/* ---- Overlay: paths through objects, arrays, Maps and Sets ---- */
const view = () => ({ pat: 5, kit: 3, songSlot: 0, bpm: 120, tracks: [{ trigs: [false, false], level: 100 }],
	locks: new Map([["0:#1", new Map([[4, 10]])]]), sel: new Set([1]) });
Overlay.add(1, [[["bpm"], 130], [["tracks", 0, "trigs", 1], true], [["locks", "0:#1", 6], 64], [["locks", "2:#3", 0], 9], [["sel", 2], true], [["sel", 1], false]]);
let v = Overlay.over(view());
check(v.bpm === 130 && v.tracks[0].trigs[1] === true, "object and array members");
check(v.locks.get("0:#1").get(6) === 64 && v.locks.get("0:#1").get(4) === 10, "a Map entry, the others kept");
check(Object.prototype.toString.call(v.locks.get("2:#3")) === "[object Map]" && v.locks.get("2:#3").get(0) === 9, "a write into a missing inner Map makes it");
check(v.sel.has(2) && !v.sel.has(1), "a Set member: true in, false out");
v = Overlay.over(v);
check(v.bpm === 130 && v.sel.has(2) && v.locks.get("0:#1").size === 2, "values, never toggles: applied twice is applied once");
Overlay.add(2, [[["locks", "0:#1", 4], DELETE], [["locks", "0:#1", 6], DELETE]]);
v = Overlay.over(view());
check(!v.locks.has("0:#1"), "a delete that empties an inner Map removes it");
check(Overlay.size() === 7 && Overlay.answered(2) && Overlay.size() === 5, "a later write of a path owns it; the answer takes its own entries only");
Overlay.add(3, [[["tracks", 9, "level"], 1], [["nothere", "x"], 1]]);
v = Overlay.over(view());
check(v.tracks.length === 1 && !("nothere" in v), "a path the view does not have is left alone");
const value = { a: [1, 2] };
Overlay.add(4, [[["obj"], value]]);
value.a.push(3);
check(same(Overlay.over(view()).obj, { a: [1, 2] }), "a written value is a copy, not the caller's object");
Overlay.clear();
check(Overlay.size() === 0 && !Overlay.answered(1), "clear: nothing left");

/* ---- Overlay: scoped to the document its command edits ---- */
check(same(docOf("trig", { p: 5, t: 0, s: 3 }), { kind: "pattern", slot: 5 }) && same(docOf("param", { k: 3 }), { kind: "kit", slot: 3 })
	&& same(docOf("rowSet", { s: 2 }), { kind: "song", slot: 2 }) && docOf("tempo", { bpm: 120 }) === null && docOf("select", { s: 2 }) === null,
	"docOf: p a pattern, k a kit, a song row op's s a song, else none");
check(shows(view(), { kind: "pattern", slot: 5 }) && !shows(view(), { kind: "pattern", slot: 6 }) && shows(view(), { kind: "global", slot: 0 }), "shows: the view's pattern, kit and song; other kinds always");
Overlay.add(5, [[["tracks", 0, "level"], 7]], { kind: "kit", slot: 3 });
check(Overlay.over(view()).tracks[0].level === 7 && Overlay.over(Object.assign(view(), { kit: 4 })).tracks[0].level === 100, "an entry shows only on its document");
Overlay.clear();

/* ---- DocOverlay: the page's documents until their command is answered ---- */
{
	const base = docs(); base.patterns[1] = { n: "published" };
	DocOverlay.add(10, "pattern", 1, { n: "first" });
	DocOverlay.add(11, "kit", 2, { n: "kit" });
	DocOverlay.add(12, "pattern", 1, { n: "second" });
	const d = DocOverlay.over(base);
	check(d.patterns[1].n === "second" && d.kits[2].n === "kit", "the latest document for a slot wins");
	check(base.patterns[1].n === "published" && !(2 in base.kits) && d !== base && d.patterns !== base.patterns, "the published documents are never written");
	check(DocOverlay.size() === 2 && !DocOverlay.answered(10), "a later document for a slot takes the earlier one's place (its answer finds nothing)");
	check(DocOverlay.answered(12) && DocOverlay.over(base).patterns[1].n === "published", "answered: the published document shows again");
	DocOverlay.add(13, "workingKit", 2, { w: 1 }, { source: "page", pending: true });
	check(same(DocOverlay.over(base).workingKit, { slot: 2, source: "page", pending: true, doc: { w: 1 } }), "a working kit with its source and pending");
	DocOverlay.clear();
	check(DocOverlay.size() === 0 && DocOverlay.over(base) === base, "nothing waiting: the documents as published");
}

console.log(failures ? `deskOverlayTest: ${failures} failure(s)` : "deskOverlayTest: PASS");
process.exit(failures ? 1 : 0);
