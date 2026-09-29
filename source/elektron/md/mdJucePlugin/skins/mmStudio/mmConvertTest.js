"use strict";
/* Round trip check for mmConvert.js, the page <-> contract translation of the
   Monomachine Editor:  toFirmware(toPage(doc), doc) == doc  for every document.
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
	+ fs.readFileSync(path.join(here, "mmConvert.js"), "utf8") + "\nthis.C=MmConvert;", ctx);
const C = ctx.C;
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

for (const { d, f } of docs["mm-desk/kit"]) {
	const page = C.kitToPage(d, global0);
	check("kit", f, C.kitToFw(page, d, C.kitName(d)), d);
}
for (const { d, f } of docs["mm-desk/pattern"]) {
	const k = kitBySlot[d.kit] ? C.kitToPage(kitBySlot[d.kit], global0) : null;
	check("pattern", f, C.patternToFw(C.patternToPage(d, k), d, d.kit, k), d);
}
for (const { d, f } of docs["mm-desk/song"]) check("song", f, C.songToFw(C.songToPage(d, lenOf), d, lenOf), d);
for (const { d, f } of docs["mm-desk/global"]) {
	const midi = d.midiSeq.channels.map((c, t) => ({ ch: c + 1, cc: [...d.midiSeq.ccs[t]] }));
	check("global", f, C.globalToFw(d, d.routingMode, midi, C.mapToPage(d)), d);
}

/* edits land where they should */
const edits = [];
if (docs["mm-desk/pattern"].length) {
	const d = docs["mm-desk/pattern"][0].d, p = C.patternToPage(d, null);
	const free = [...Array(64).keys()].find(s => !p.tr[0].steps[s]);
	p.tr[0].steps[free] = { n: [60], a: 0, f: 0, l: 0 };	// trigless, with a pitch
	const q = C.patternToFw(p, d, d.kit, null);
	edits.push(["trigless trig", q.tracks[0].trig.includes(free) && !q.tracks[0].amp.includes(free) && q.tracks[0].notes.some(([s, v]) => s === free && v === 60)]);
	p.tr[0].steps[free] = { a: 1, f: 1, l: 1 };	// pitchless
	const r = C.patternToFw(p, d, d.kit, null);
	edits.push(["pitchless trig", r.tracks[0].trig.includes(free) && r.tracks[0].amp.includes(free) && !r.tracks[0].notes.some(([s]) => s === free)]);
	p.tr[0].steps[free] = { n: [60, 64, 67], a: 1, f: 1, l: 1 };	// chord
	const c = C.patternToFw(p, d, d.kit, null);
	edits.push(["chord", c.tracks[0].chord.includes(free) && c.chordNotes.filter(([t, s]) => t === 0 && s === free).length === 2]);
	p.swingAmt = 58; p.patTrn = 70;
	const w = C.patternToFw(p, d, d.kit, null);
	edits.push(["swing and transpose", w.swingAmount === 8 && w.patternTranspose === 6]);
}
if (docs["mm-desk/kit"].length) {
	const d = docs["mm-desk/kit"][0].d, k = C.kitToPage(d, global0);
	k.tracks[0].m = "SID-6581"; k.tracks[0].v.SYN[3] = 2;	// WAVE = PULS
	k.tracks[0].v.LF1[3] = 4;	// SQR
	k.tracks[1].assign.tabs["JOY U"][1].add = 10;
	const o = C.kitToFw(k, d, "RENAMED");
	edits.push(["machine and enums", o.tracks[0].machine === 3 && C.valueToPage("SID-6581", "SYN", 3, o.tracks[0].pages[0][3]) === 2
		&& Math.floor(o.tracks[0].pages[4][3] * 11 / 128) === 4]);
	edits.push(["assign amount", o.tracks[1].assign.add[5] === -54]);
	edits.push(["name", o.name === "RENAMED" && !("nameBytes" in o)]);
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
/* MM-P4: MULTI MAP, MULTI TRIG and PORTAMENTO, and a song row more with its residue after END kept inside */
if (docs["mm-desk/global"].length) {
	const d = docs["mm-desk/global"][0].d, rows = C.mapToPage(d);
	rows[0] = { ...rows[0], pat: 2, ofs: 3, len: 8, trn: 61, tim: 3 };
	const o = C.globalToFw(d, d.routingMode, d.midiSeq.channels.map((c, t) => ({ ch: c + 1, cc: d.midiSeq.ccs[t] })), rows);
	edits.push(["multi map row", o.multiMap[1][0] === 2 && o.multiMap[2][0] === 2 && o.multiMap[3][0] === 8 && o.multiMap[4][0] === 253 && o.multiMap[5][0] === 3]);
	const two = C.mapToPage(d).slice(0, 1).map(r => ({ ...r, hi: 60 })).concat([{ hi: 127, pat: -1, ofs: 0, len: 16, trn: 64, tim: 0 }]);
	const o2 = C.globalToFw(d, d.routingMode, d.midiSeq.channels.map((c, t) => ({ ch: c + 1, cc: d.midiSeq.ccs[t] })), two);
	edits.push(["multi map split: two ranges, the rest repeat the last upper key", C.mapToPage(o2).length === 2 && o2.multiMap[0][31] === 127]);
	const k = docs["mm-desk/kit"][0]?.d;
	if (k) {
		const p = C.kitToPage(k, d);
		edits.push(["multi trig to the page (split track from 1)", p.multi && p.multi.splitTrack === k.multiTrig.splitTrack + 1 && p.multi.mode === k.multiTrig.mode]);
		p.multi = { mode: 1, splitKey: 48, splitTrack: 3, timing: 6 };
		p.tracks[2].port = 1;
		const o3 = C.kitToFw(p, k, C.kitName(k));
		edits.push(["multi trig and portamento", o3.multiTrig.mode === 1 && o3.multiTrig.splitTrack === 2 && o3.multiTrig.timing === 6 && !((o3.trackMasks.portamento >> 2) & 1)]);
	}
}
if (docs["mm-desk/song"].length) {
	const d = docs["mm-desk/song"].find(x => ((x.d.firmware || x.d.hidden || {}).rowsAfterEnd || []).length)?.d || docs["mm-desk/song"][0].d;
	const rows = C.songToPage(d, lenOf);
	rows.splice(0, 0, { pat: 3, rep: 2 });
	const o = C.songToFw(rows, d, lenOf), fw = o.firmware || o.hidden;
	const size = (200 - o.rows.length) * 24;
	edits.push(["song row added, residue kept inside the region", o.rows.length === d.rows.length + 1 && (fw.rowsAfterEnd || []).every(([i, h]) => i * 2 + h.length <= size * 2)]);
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
for (const [what, ok] of edits) { n++; if (!ok) { fails++; console.log("FAIL edit:", what); } }

const counts = Object.entries(docs).map(([k, l]) => l.length + " " + k.split("/")[1]).join(", ");
console.log(`mmConvertTest: ${counts}; ${n} checks, ${fails ? fails + " FAILED" : "PASS"}`);
process.exit(fails ? 1 : 0);
