"use strict";
/* Checks of mmConvert.js, the contract -> page translation of the Monomachine Editor (DESIGN-UNIFY.md 4.3): every
   document reads into the page's units; what an intent carries back is exact (a knob's raw value inside its
   enumeration band, a song row: rowToFw(rowToPage(row), row) == row) for every document.
     node mmConvertTest.js <json-dir>...
   The JSON comes from  mmDataCorpusTest --json <json-dir> <dumps>  (factory set
   and programmed read-backs). The mockup's tables are read from mmMockup.js next
   to this file, so the test runs against exactly what the plug-in ships. The
   enumeration counts are the catalogue the plug-in sends, committed as
   doc/modern-ux/mm-catalogue.json (mmDeskTest --write-schema writes it and fails
   when it differs from MmModel::catalogue()); the mockup's own tables must agree
   with it. */
const fs = require("fs"), path = require("path"), vm = require("vm");

const here = __dirname;
const mock = fs.readFileSync(path.join(here, "mmMockup.js"), "utf8");
/* the data part of the mockup: 40-data.js (tables, no DOM) and the INPUTS row of 100-mix.js */
const data = mock.slice(mock.indexOf("/* ---- 40-data.js ---- */"), mock.indexOf("/* ---- 50-state.js ---- */"));
const inputs = mock.match(/const BUSES=.*INPUTS=\[[^\]]*\];/)[0];
const ctx = vm.createContext({ console });
vm.runInContext(data + "\n" + inputs + "\nfunction clamp(v,a=0,b=127){return Math.max(a,Math.min(b,v))}\n"
	+ fs.readFileSync(path.join(here, "mmConvert.js"), "utf8") + "\nthis.C=MmConvert;this.PAGES=PAGES;", ctx);
const C = ctx.C, PAGES = ctx.PAGES;
const catalogue = JSON.parse(fs.readFileSync(path.join(here, "../../../../../../doc/modern-ux/mm-catalogue.json"), "utf8"));
const catalogueOff = C.useCatalogue(catalogue);

function diff(a, b, at = "") {
	if (typeof a !== typeof b || Array.isArray(a) !== Array.isArray(b) || (a === null) !== (b === null)) return at + ": " + JSON.stringify(a) + " vs " + JSON.stringify(b);
	if (a && typeof a === "object") {
		const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
		for (const k of keys) {
			if (!(k in a) || !(k in b)) return at + "." + k + ": missing on one side";
			const d = diff(a[k], b[k], at + "." + k);
			if (d) return d;
		}
		return null;
	}
	return a === b ? null : at + ": " + JSON.stringify(a) + " vs " + JSON.stringify(b);
}

const docs = { "mm-desk/pattern": [], "mm-desk/kit": [], "mm-desk/song": [], "mm-desk/global": [] };
for (const dir of process.argv.slice(2))
	for (const f of fs.readdirSync(dir).filter(f => f.endsWith(".json"))) {
		const d = JSON.parse(fs.readFileSync(path.join(dir, f), "utf8"));
		if (docs[d.schema]) docs[d.schema].push({ d, f: path.join(dir, f) });
	}
if (!Object.values(docs).some(l => l.length)) { console.error("usage: node mmConvertTest.js <json-dir>..."); process.exit(2); }

const kitBySlot = {}, patBySlot = {};
docs["mm-desk/kit"].forEach(({ d }) => kitBySlot[d.slot] = kitBySlot[d.slot] || d);
docs["mm-desk/pattern"].forEach(({ d }) => patBySlot[d.slot] = patBySlot[d.slot] || d);
const global0 = docs["mm-desk/global"][0]?.d;
const lenOf = p => patBySlot[p]?.length ?? 16;
let fails = 0, n = 0;
const check = (what, f, out, d) => { n++; const e = diff(out, d); if (e) { fails++; if (fails <= 20) console.log("FAIL", what, path.basename(f), e); } };

/* every document reads into the page's units */
for (const { d, f } of docs["mm-desk/kit"]) { n++; try { const k = C.kitToPage(d, global0); if (k.tracks.length !== 6 || k.midi.length !== 6) throw new Error("tracks"); } catch (e) { fails++; console.log("FAIL kit to page", path.basename(f), e.message); } }
for (const { d, f } of docs["mm-desk/pattern"]) { n++; try { const k = kitBySlot[d.kit] ? C.kitToPage(kitBySlot[d.kit], global0) : null; if (C.patternToPage(d, k).tr.length !== 12) throw new Error("tracks"); } catch (e) { fails++; console.log("FAIL pattern to page", path.basename(f), e.message); } }
for (const { d, f } of docs["mm-desk/global"]) { n++; try { C.mapToPage(d); } catch (e) { fails++; console.log("FAIL global to page", path.basename(f), e.message); } }

/* an intent's values back in the contract's units: a knob's raw value stays inside its enumeration band (an unchanged
   index keeps it), for every value of every kit */
for (const { d, f } of docs["mm-desk/kit"]) d.tracks.forEach((x, t) => {
	const m = C.machineName(x.machine) ?? "GND-GND";
	PAGES.forEach((pg, p) => x.pages[p].forEach((raw, i) => { n++; const back = C.valueToFw(m, pg, i, C.valueToPage(m, pg, i, raw), raw); if (back !== raw) { fails++; if (fails <= 20) console.log("FAIL value", path.basename(f), t, pg, i, raw, back); } }));
});
/* a song row: the page's row back to the song's is the row (rowSet, rowInsert), for every row of every song */
for (const { d, f } of docs["mm-desk/song"]) d.rows.forEach((r, i) => check("song row " + i, f, C.rowToFw(C.rowToPage(r, i, lenOf), r, i, lenOf), r));

/* edits land where they should */
const edits = [];
{
	const base = C.rowToFw({ pat: 0, rep: 1 }, null, 0, lenOf);
	const r = C.rowToFw({ pat: 9, rep: 3, trn: 70, ttr: [64, 66, 64, 64, 64, 64, 60, 64, 64, 64, 64, 64], ofs: 4, len: 8, bpm: 124, mutes: [1, 7] }, base, 2, lenOf);
	edits.push(["a song row to the song's units", r.kind === "pattern" && r.pattern === 9 && r.repeats === 2 && r.transpose === 6 && r.trackTranspose[1] === 2 && r.midiTranspose[0] === -4
		&& r.offset === 4 && r.length === 8 && r.tempo === 124 && r.mutes === 2 && r.midiMutes === 2]);
	const h = C.rowToFw({ type: "halt", to: 0 }, null, 5, lenOf), l = C.rowToFw({ type: "loop", to: 1, count: Infinity }, null, 4, lenOf);
	edits.push(["a HALT row's target is its place, a loop for ever repeats 0", h.pattern === 254 && h.target === 5 && l.pattern === 254 && l.target === 1 && l.repeats === 0]);
	const w = C.rowToFw({ pat: 3, rep: 1 }, null, 0, q => q === 3 ? 48 : 16);
	edits.push(["a whole pattern's row is as long as the pattern", w.length === 48 && C.rowToPage(w, 0, q => q === 3 ? 48 : 16).len === undefined]);
}
/* the Control workspace's app sources <-> md-desk/modulators (the plug-in runs them) */
{
	const setup = { sources: [{ id: "lfoA", kind: "lfo", label: "LFO A", val: 64, SHAPE: 2, RATE: "1/4", DEPTH: 80 },
		{ id: "rndA", kind: "rnd", label: "Random A", val: 64, RATE: "1/16", SMOOTH: 30, _t: 64 }],
	links: [{ src: "lfoA", t: 2, pid: "AMP.6", min: 30, max: 98, curve: "lin", inv: false }, { src: "rndA", t: 7, pid: "MID.4", min: 0, max: 127, curve: "lin", inv: true },
		{ src: "rndA", t: 0, pid: "LF3.1", min: 5, max: 120, curve: "exp", inv: true }] };
	const m = C.modToFw(setup);
	edits.push(["modulators to the contract", m.schema === "mm-desk/modulators" && m.sources[0].kind === "lfo" && m.sources[0].shape === 3 && m.sources[0].depth === 80
		&& m.sources[1].kind === "random" && m.sources[1].smooth === 30 && m.links.length === 2 && m.links[0].param === 14 && m.links[1].param === 49 && m.links[1].invert]);
	const back = C.modToPage(m), again = C.modToFw(back);
	edits.push(["modulators round trip", !diff(again, m) && back.links[0].pid === "AMP.6" && back.sources[0].SHAPE === 2]);
}
/* MM-P4: MULTI MAP and MULTI TRIG to the page */
if (docs["mm-desk/global"].length) {
	const d = docs["mm-desk/global"][0].d, rows = C.mapToPage(d);
	edits.push(["multi map: the ranges end where an upper key repeats", rows.length > 0 && rows.every((r, i) => !i || r.hi > rows[i - 1].hi)]);
	const k = docs["mm-desk/kit"][0]?.d;
	if (k) {
		const p = C.kitToPage(k, d);
		edits.push(["multi trig to the page (split track from 1)", p.multi && p.multi.splitTrack === k.multiTrig.splitTrack + 1 && p.multi.mode === k.multiTrig.mode]);
	}
}
/* the catalogue's counts are the ones mmConvert used before them (OS 1.32B), and the mockup's tables agree with it */
edits.push(["catalogue: the mockup's tables agree" + (catalogueOff.length ? " (" + catalogueOff.join("; ") + ")" : ""), catalogueOff.length === 0]);
edits.push(["catalogue: LFO PAGE DEST TRIG WAVE MULT counts", [0, 1, 2, 3, 4].map(i => C.enumN("GND-GND", "LF1", i)).join() === "9,8,5,11,7"]);
edits.push(["catalogue: DPRO-WAVE WAVE is 32 waveforms", C.enumN("DPRO-WAVE", "SYN", 0) === 32 && C.enumN("SID-6581", "SYN", 3) === 5]);
/* P7: LEN on LCD line 2 (the mockup's lenStep): a click goes round the pages, a scroll steps (the
   factory patterns are LEN 64, where a click used to do nothing) */
{
	const src = mock.match(/function lenStep\(len,d,fine\)\{.*\}/)[0];
	const lenStep = vm.runInContext("(" + src.replace("function lenStep", "function") + ")", ctx);
	const pages = [64, 16, 32, 48, 64].map((l, i, a) => i ? lenStep(a[i - 1], 1, false) : l);
	edits.push(["LEN click goes round the pages: 64 " + pages.slice(1).join(" "), pages.join() === "64,16,32,48,64"]);
	edits.push(["LEN shift-click goes back, 16 -> 64", lenStep(16, -1, false) === 64 && lenStep(64, -1, false) === 48 && lenStep(17, 1, false) === 48]);
	edits.push(["LEN scroll: one step within 2-64", lenStep(64, 1, true) === 64 && lenStep(64, -1, true) === 63 && lenStep(2, -1, true) === 2]);
}
/* Control All on the Sound workspace (the mockup's controlAll, 60-ui.js; DESIGN-edit-flow.md): the same delta on
   the other synth tracks, a machine without that SYN parameter left out, the MIDI tracks never touched, held at the end */
{
	const src = mock.match(/function controlAll\(t0,g,i,d\)\{.*\}/)[0];
	const holey = vm.runInContext("Object.keys(MACH)", ctx);
	const machOf = m => vm.runInContext("MACH[" + JSON.stringify(m) + "]", ctx);
	const gap = holey.find(m => machOf(m).p.slice(0, 8).some((x, i) => !x && holey.some(o => machOf(o).p[i])));
	const i = gap ? machOf(gap).p.findIndex((x, k) => k < 8 && !x && holey.some(o => machOf(o).p[k])) : -1;
	const full = gap ? holey.find(o => machOf(o).p[i]) : null;
	const tracks = Array.from({ length: 6 }, (_, t) => ({ m: t === 2 ? gap : full, v: { SYN: Array(8).fill(t === 4 ? 125 : 60), FLT: Array(8).fill(60) } }));
	const midi = Array.from({ length: 6 }, () => ({ m: "MIDI", v: { MID: Array(8).fill(60) } }));
	ctx.CA = { tracks, midi };
	const controlAll = vm.runInContext("(()=>{const trk=t=>t<6?CA.tracks[t]:CA.midi[t-6],meta=()=>({max:127}),maxOf=m=>m.max;return " + src + "})()", ctx);
	controlAll(0, "SYN", i, 5);
	controlAll(0, "FLT", 0, -70);
	edits.push(["Control All: the other synth tracks move, a machine without that SYN parameter (" + gap + " " + i + ") stays, held at 127",
		gap && tracks[1].v.SYN[i] === 65 && tracks[2].v.SYN[i] === 60 && tracks[4].v.SYN[i] === 127 && tracks[0].v.SYN[i] === 60]);
	edits.push(["Control All: the shared pages on every other synth track, held at 0; the MIDI tracks untouched",
		tracks.slice(1).every(t => t.v.FLT[0] === 0) && midi.every(t => t.v.MID.every(x => x === 60))]);
}
/* A never-written kit slot (measured on the emulator, mmDeskFirmwareTest smoke: K65-K128 of a fresh machine): a
   first name byte of 0xff and battery-RAM bytes after it. The library shows it EMPTY (no name), never those bytes;
   a written slot keeps its name. (MM-PORT-PLAN f: the Machinedrum needs kitNameText for its K17-K64; the MM's
   firmware marks its unused slots, so the page reads the mark.) */
{
	const unused = { name: "?", nameBytes: "ff00415600000000000000" }, junk = { name: "?A", nameBytes: "ff4100560000000000000000".slice(0, 22) };
	edits.push(["a never-written kit slot (first name byte 0xff) has no name and is empty", C.kitName(unused) === "" && C.kitEmpty(unused) && C.kitName(junk) === "" && C.kitEmpty(junk)]);
	edits.push(["a written kit slot keeps its name and is not empty", C.kitName({ name: "ACID BATH" }) === "ACID BATH" && !C.kitEmpty({ name: "ACID BATH" })]);
}
for (const [what, ok] of edits) { n++; if (!ok) { fails++; console.log("FAIL edit:", what); } }

const counts = Object.entries(docs).map(([k, l]) => l.length + " " + k.split("/")[1]).join(", ");
console.log(`mmConvertTest: ${counts}; ${n} checks, ${fails ? fails + " FAILED" : "PASS"}`);
process.exit(fails ? 1 : 0);
