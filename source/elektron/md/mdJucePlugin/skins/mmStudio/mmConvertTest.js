"use strict";
/* Round trip check for mmConvert.js, the page <-> contract translation of the
   Monomachine Editor:  toFirmware(toPage(doc), doc) == doc  for every document.
     node mmConvertTest.js <json-dir>...
   The JSON comes from  mmDataCorpusTest --json <json-dir> <dumps>  (factory set
   and programmed read-backs). The mockup's tables are read from mmMockup.js next
   to this file, so the test runs against exactly what the plug-in ships. */
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
if (docs["mm-desk/global"].length) {
	const d = docs["mm-desk/global"][0].d, rows = C.mapToPage(d);
	rows[0] = { ...rows[0], pat: 2, ofs: 3, len: 8, trn: 61, tim: 3 };
	const o = C.globalToFw(d, d.routingMode, d.midiSeq.channels.map((c, t) => ({ ch: c + 1, cc: d.midiSeq.ccs[t] })), rows);
	edits.push(["multi map row", o.multiMap[1][0] === 2 && o.multiMap[2][0] === 2 && o.multiMap[3][0] === 8 && o.multiMap[4][0] === 253 && o.multiMap[5][0] === 3]);
	const k = docs["mm-desk/kit"][0]?.d;
	if (k) {
		const p = C.kitToPage(k, d);
		p.multi = { mode: 1, splitKey: 48, splitTrack: 3, timing: 6 };
		p.tracks[2].port = 1;
		const o2 = C.kitToFw(p, k, C.kitName(k));
		edits.push(["multi trig and portamento", o2.multiTrig.mode === 1 && o2.multiTrig.splitTrack === 2 && o2.multiTrig.timing === 6 && !((o2.trackMasks.portamento >> 2) & 1)]);
	}
}
if (docs["mm-desk/song"].length) {
	/* a row more and a row less: the residue after END moves with it and the document stays valid */
	const d = docs["mm-desk/song"].find(x => x.d.hidden.rowsAfterEnd.length)?.d || docs["mm-desk/song"][0].d;
	const rows = C.songToPage(d, lenOf);
	rows.splice(0, 0, { pat: 3, rep: 2 });
	const o = C.songToFw(rows, d, lenOf);
	const size = (200 - o.rows.length) * 24;
	edits.push(["song row added, residue kept inside the region", o.rows.length === d.rows.length + 1 && o.hidden.rowsAfterEnd.every(([i, h]) => i * 2 + h.length <= size * 2)]);
}
for (const [what, ok] of edits) { n++; if (!ok) { fails++; console.log("FAIL edit:", what); } }

const counts = Object.entries(docs).map(([k, l]) => l.length + " " + k.split("/")[1]).join(", ");
console.log(`mmConvertTest: ${counts}; ${n} checks, ${fails ? fails + " FAILED" : "PASS"}`);
process.exit(fails ? 1 : 0);
