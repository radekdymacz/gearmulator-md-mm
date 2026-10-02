"use strict";
/* The Monomachine Editor's GEN and MUTATE (MM-PORT-PLAN.md d) on MM data, from the generated page (mmMockup.js):
   the shared GEN block (the Machinedrum Editor's mdDeskGen.js, included as it is: checked to be the same text),
   genNotes (the NOTES group: in the scale and the range, STEP never more than two degrees, the same seed the same
   notes, a drum box only BD SD CH OH), the roles of the MM's machines (every machine of the page's table has one),
   a track's steps from a spec (hits on, trigs off, pitches kept or written, NOTE OFFs kept), and the mutation of
   the DATA pages (0 % nothing, VOL TUNE INP PAGE DEST never, the knob's own range, keyed by track and knob).
     node mmGenTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

const page = fs.readFileSync(path.join(__dirname, "mmMockup.js"), "utf8");
/* a part of the page as sync-mmstudio-skin.py heads it: "/* ---- 52-gen.js ---- *\/" (comments inside a part may look alike) */
const HEAD = /\n\/\* ---- (?:\d+-[\w-]+\.js|mdDeskGen\.js: the GEN block) ---- \*\/\n/g;
const part = name => { const a = page.indexOf(`/* ---- ${name} ---- */\n`); if (a < 0) return ""; HEAD.lastIndex = a + 10; const m = HEAD.exec(page); return page.slice(a, m ? m.index : undefined); };
const genBlock = part("mdDeskGen.js: the GEN block"), mmGen = part("52-gen.js"), data = part("40-data.js");
check(genBlock && mmGen && data, "the page has the GEN block, 52-gen.js and 40-data.js");
/* one source: the block in the page is the Machinedrum Editor's own text */
const mdSrc = fs.readFileSync(path.join(__dirname, "../mdStudio/mdDeskGen.js"), "utf8");
const mdBlock = (mdSrc.match(/^\/\* GEN BEGIN[\s\S]*?^\/\* GEN END \*\/\n/m) || [""])[0];
check(mdBlock && genBlock.includes(mdBlock), "the page's GEN block is mdDeskGen.js's, the same text (run sync-mmstudio-skin.py when it differs)");
check(!/MACH|\bS\.|\$\(|document|DPRO|SWAVE|-BD|GND-/.test(mdBlock), "the shared block names no machine and reads no page");

const ctx = vm.createContext({ console });
vm.runInContext(data.replace(/^"use strict";/m, "") + "\n" + genBlock + "\n" + mmGen
	+ "\nthis.T = { MACH, FIXED, EN, BBOX, genU, generate, genFit, genScale, genNotes, GEN_SCALES, mutPull, mmGenRole, mmGenDefault, mmGenRoot, mmNoteSpec, mmGenSteps, mmLastNotes, mmMutate, MM_DRUMS, MM_SCALES, mmNotesTag };", ctx);
const T = ctx.T;

/* ---- genScale and genNotes ---- */
check(same(T.genScale(36, "PENT", 1), [36, 39, 41, 43, 46, 48]), "PENT from C-2 over one octave: C D# F G A# C");
check(same(T.genScale(60, "MAJ", 1), [60, 62, 64, 65, 67, 69, 71, 72]) && T.genScale(57, "DORIAN", 2).length === 15, "MAJ from C-4; DORIAN over two octaves: 15 notes");
check(T.genScale(120, "MIN", 2).every(n => n <= 127), "the scale stays inside MIDI 0..127");
const hits = [0, 3, 4, 7, 8, 11, 12, 14, 16, 19, 22, 24, 27, 30];
for (const scale of T.MM_SCALES) for (const range of [1, 2]) for (const motion of ["step", "leap"]) {
	const spec = { root: 38, scale, range, motion }, sc = T.genScale(38, scale, range), a = T.genNotes(spec, hits, 4242, 0);
	const idx = a.map(([, n]) => sc.indexOf(n));
	const ok = a.length === hits.length && same(a.map(([s]) => s), hits) && idx.every(i => i >= 0) && same(a, T.genNotes(spec, hits, 4242, 0));
	const steps = motion === "step" ? idx.every((i, k) => k === 0 || Math.abs(i - idx[k - 1]) <= 2) && idx[0] === 0 : true;
	check(ok && steps, `${motion.toUpperCase()} ${scale} ×${range}: every note in the scale and the range, the same seed the same notes${motion === "step" ? ", from the root, never more than two degrees" : ""}`);
}
const walk = T.genNotes({ root: 36, scale: "PENT", range: 1, motion: "step" }, [...Array(64).keys()], 7, 0).map(([, n]) => n);
check(new Set(walk).size >= 4 && walk.some((n, k) => k && n !== walk[k - 1]), "STEP walks: over 64 hits it moves through the scale");
check(!same(T.genNotes({ root: 36, scale: "PENT", range: 1, motion: "step" }, hits, 7, 0), T.genNotes({ root: 36, scale: "PENT", range: 1, motion: "step" }, hits, 8, 0)), "another seed other notes");
check(!same(T.genNotes({ root: 36, scale: "MIN", range: 2, motion: "leap" }, hits, 7, 0), T.genNotes({ root: 36, scale: "MIN", range: 2, motion: "leap" }, hits, 7, 1)), "keyed by the track: another track, other notes from the same seed");
const kit = T.genNotes({ pool: T.MM_DRUMS }, hits, 99, 4);
check(kit.every(([, n]) => T.MM_DRUMS.includes(n)) && new Set(kit.map(([, n]) => n)).size > 1, "a drum box picks from BD1 SD1 CH OH only");
check(same(T.MM_DRUMS.map(n => T.BBOX[n]), ["BD1", "SD1", "CH", "OH"]), "MM_DRUMS are DPRO-BBOX's BD1 SD1 CH OH (40-data.js BBOX)");

/* ---- roles and defaults ---- */
const roles = Object.keys(T.MACH).map(m => [m, T.mmGenRole(m)]);
check(roles.every(([, r]) => ["keep", "drums", "pad", "bass", "voice", "lead", "other"].includes(r)), "every machine of the page's table has a role");
check(roles.filter(([m]) => /^FX-/.test(m)).every(([, r]) => r === "keep") && T.mmGenRole("GND-GND") === "keep", "FX machines and GND-GND are kept (one trig opens an FX machine)");
check(T.mmGenRole("DPRO-BBOX") === "drums" && T.mmGenRole("SWAVE-SAW") === "bass" && T.mmGenRole("SWAVE-ENS") === "pad" && T.mmGenRole("VO-6") === "voice", "BBOX drums, SWAVE-SAW bass, SWAVE-ENS pad, VO-6 voice");
check(T.mmGenRole("SID-6581", [60, 67]) === "lead" && T.mmGenRole("SID-6581", [36, 48]) === "bass" && T.mmGenRole("FM+STAT") === "lead", "SID with low notes is a bass, with high ones a lead");
check(T.mmGenRole("SWAVE-SAW", [], true) === "midi", "a MIDI track has its own plain default");
const bass = T.mmGenDefault({ m: "SWAVE-SAW", notes: [38, 41, 50], key: 2, scale: 3 }, 32, false, 11);
check(bass.kind === "euclid" && bass.k === 5 && bass.n === 16 && bass.notes.motion === "step" && bass.notes.scale === "MIN" && bass.notes.root === 38 && bass.seed === 11, "bass: E 5/16, STEP in the track's D MIN at its lowest note (D-2)");
const lead = T.mmGenDefault({ m: "SID-6581", notes: [], key: 4, scale: 0 }, 64, false, 5);
check(lead.kind === "random" && lead.notes.scale === "PENT" && lead.notes.root === 60 && lead.notes.range === 2, "lead without a scale: random, PENT on C (the track's KEY counts only with MAJ / MIN), two octaves from C-4");
check(T.mmGenDefault({ m: "SWAVE-SAW", notes: [], key: 0, scale: 0 }, 12).n === 12, "STEPS defaults to min(16, the pattern's length)");
check(T.mmGenDefault({ m: "DPRO-BBOX", notes: [], key: 0, scale: 1 }, 64).notes.motion === "kit" && T.mmGenDefault({ m: "FX-REVERB", notes: [] }, 64).kind === "keep", "the drum box's notes are KIT; an FX machine is kept");
const mid = T.mmGenDefault({ m: "MIDI", notes: [36] }, 64, true);
check(mid.kind === "euclid" && !mid.notes && T.mmNoteSpec(mid) === null, "a MIDI track: rhythm only");
check(T.mmGenRoot(9, 36) === 33 && T.mmGenRoot(0, 36) === 36 && T.mmGenRoot(0, 34) === 36 && T.mmGenRoot(6, 36) === 30 && T.mmGenRoot(0, 125, 2) <= 103, "the root is the one nearest the lowest note (a tie goes down), inside the range");

/* ---- a track's steps from a spec, on MM steps ---- */
const N = (n, a = 1, f = 1, l = 1) => ({ n: [n], a, f, l });
const base = Array(64).fill(null);
base[0] = N(36, 1, 1, 0); base[2] = N(39); base[4] = N(41, 0, 0, 0); base[6] = { off: 1 }; base[9] = N(43); base[13] = N(46);
const e4 = T.mmGenSteps({ kind: "euclid", k: 4, n: 16, rot: 0, seed: 3, notes: { motion: "off" } }, 0, 0, 16, base, 16);
const at = (r, s) => (r.steps.find(([x]) => x === s) || [])[1];
check(same(e4.steps.filter(([, st]) => st && !st.off).map(([s]) => s), [0, 4, 8, 12]), "E 4/16: the trigs are exactly the hits");
check(same(e4.gone, [2, 9, 13]) && at(e4, 2) === null, "the trigs off the hits go (their locks and slides with them)");
check(same(at(e4, 0), base[0]) && same(at(e4, 4), base[4]), "a trig on a hit keeps its step: its pitch, its envelope trigs (a trigless trig stays trigless)");
check(same(at(e4, 8), N(41)) && same(at(e4, 12), N(43)), "a new hit is a note at the track's last note before it (in the pattern before the run)");
check(same(at(e4, 6), { off: 1 }), "a NOTE OFF where no hit lands stays");
const chordBase = Array(64).fill(null); chordBase[0] = { n: [48, 51, 55], a: 1, f: 1, l: 1 };
const ch = T.mmGenSteps({ kind: "euclid", k: 4, n: 16, rot: 0, seed: 3 }, 7, 0, 16, chordBase, 16, true);
check([4, 8, 12].every(s => same(at(ch, s).n, [48, 51, 55])), "a new hit plays the last trig's chord (a pad stays a pad)");
check(e4.steps.length === 16, "the result covers every step of the range");
const e4n = T.mmGenSteps({ kind: "euclid", k: 4, n: 16, rot: 0, seed: 3, notes: { motion: "step", root: 36, scale: "PENT", range: 1 } }, 0, 0, 16, base, 16);
const sc = T.genScale(36, "PENT", 1);
check([0, 4, 8, 12].every(s => sc.includes(at(e4n, s).n[0])) && at(e4n, 0).n[0] === 36 && at(e4n, 0).l === 0, "NOTES write every hit in EUCLID (from the root); a kept trig keeps its envelope trigs");
const add = T.mmGenSteps({ kind: "random", density: 40, seed: 77, mode: "add", notes: { motion: "leap", root: 48, scale: "MAJ", range: 1 } }, 0, 0, 16, base, 16);
check([0, 2, 4, 9, 13].every(s => same(at(add, s), base[s])) && !add.gone.length, "ADD keeps every trig with its pitch, turns none off");
check(add.steps.filter(([s, st]) => st && !st.off && !base[s]).every(([, st]) => T.genScale(48, "MAJ", 1).includes(st.n[0])), "ADD: the new hits take the notes");
const thin = T.mmGenSteps({ kind: "random", density: 50, seed: 77, mode: "thin", notes: { motion: "step", root: 48, scale: "MAJ", range: 1 } }, 0, 0, 16, base, 16);
check(thin.steps.every(([s, st]) => !st || st.off || same(st, base[s])), "THIN only turns trigs off and writes no note");
check(T.mmGenSteps({ kind: "keep" }, 0, 0, 16, base, 16) === null, "keep: nothing");
const midiR = T.mmGenSteps({ kind: "euclid", k: 4, n: 16, rot: 2, seed: 1, notes: { motion: "step", root: 36, scale: "MIN", range: 1 } }, 7, 0, 16, base, 16, true);
const midiNew = midiR.steps.filter(([s, st]) => st && !st.off && !(base[s] && !base[s].off));
check(midiNew.length === 3 && midiNew.every(([s, st]) => same(st.n, T.mmLastNotes(base, s, 16, true))), "a MIDI track: no notes generated (its spec's notes are ignored), new steps take the last note");
const bb = T.mmGenSteps({ kind: "euclid", k: 8, n: 16, rot: 0, seed: 5, notes: { motion: "kit" } }, 4, 0, 32, Array(64).fill(null), 32);
check(bb.steps.filter(([, st]) => st).length === 16 && bb.steps.every(([, st]) => !st || T.MM_DRUMS.includes(st.n[0])), "the drum box: 8/16 over 32 steps, every hit BD1 SD1 CH or OH");
check(same(bb, T.mmGenSteps({ kind: "euclid", k: 8, n: 16, rot: 0, seed: 5, notes: { motion: "kit" } }, 4, 0, 32, Array(64).fill(null), 32)), "the same spec on the same base gives the same steps");
const page2 = T.mmGenSteps({ kind: "euclid", k: 3, n: 8, rot: 0, seed: 1, notes: { motion: "off" } }, 0, 16, 32, base, 32);
check(page2.steps[0][0] === 16 && same(page2.steps.filter(([, st]) => st).map(([s]) => s), [16, 19, 22, 24, 27, 30]), "a page's range starts its cycle at its first step (steps 17-32)");
check(T.mmNotesTag({ kind: "euclid", notes: { motion: "step", scale: "PENT", root: 36, range: 1 } }, n => "C-2") === "PENT C-2 ×1 STEP" && T.mmNotesTag({ kind: "euclid", notes: { motion: "off" } }) === "", "the NOTES summary");

/* ---- MUTATE on the DATA pages ---- */
const pages = ["SYN", "AMP", "FLT", "EFX", "LF1", "LF2", "LF3"];
const MS = ["SWAVE-SAW", "FX-CHORUS", "SID-6581", "VO-6", "DPRO-BBOX", "GND-GND"];
const info = (t, pg, i) => { const m = MS[t], name = pg === "SYN" ? T.MACH[m].p[i] : T.FIXED[pg][i]; if (!name) return null; const en = T.EN[m + "." + name] || T.EN[name]; return { name, max: en ? en.length - 1 : 127 }; };
/* a kit inside every knob's range */
const kitBase = MS.map((m, t) => ({ m, v: Object.fromEntries(pages.map(pg => [pg, Array.from({ length: 8 }, (_, i) => (i * 13 + pg.length * 7) % ((info(t, pg, i) || { max: 127 }).max + 1))])) }));
const M = o => Object.assign({ tracks: [0, 1, 2, 3, 4, 5], pages: ["SYN", "AMP", "FLT", "EFX", "LFO"], amount: 40, seed: 1234 }, o);
const m0 = T.mmMutate(M({ amount: 0 }), kitBase, info);
check(m0.length > 0 && m0.every(([t, pg, i, v]) => v === kitBase[t].v[pg][i]), "0 % moves nothing (every knob in scope listed)");
const m40 = T.mmMutate(M(), kitBase, info);
check(same(m40, T.mmMutate(M(), kitBase, info)) && !same(m40, T.mmMutate(M({ seed: 9 }), kitBase, info)), "the same seed gives the same values, another seed others");
check(m40.some(([t, pg, i, v]) => v !== kitBase[t].v[pg][i]), "40 % moves the sound");
const names = m40.map(([t, pg, i]) => info(t, pg, i).name);
check(!names.some(n => ["VOL", "TUNE", "INP", "PAGE", "DEST"].includes(n)), "VOL, TUNE, an FX machine's INP and an LFO's PAGE and DEST never move");
check(T.mmMutate(M({ protect: [] }), kitBase, info).some(([t, pg, i]) => info(t, pg, i).name === "VOL"), "an empty protect list moves VOL too (INP, PAGE, DEST still stay)");
check(!m40.some(([t]) => t === 5), "GND-GND has nothing to move");
check(m40.every(([t, pg, i]) => !(pg === "SYN" && !T.MACH[kitBase[t].m].p[i])), "unused knobs (no name) are never touched");
const m100 = T.mmMutate(M({ amount: 100 }), kitBase, info);
check(m100.every(([t, pg, i, v]) => v >= 0 && v <= info(t, pg, i).max && Number.isInteger(v)), "100 %: every value inside its knob's range (SID WAVE 0-4, an enumeration)");
check(m100.filter(([t, pg, i]) => t === 2 && pg === "SYN" && i === 3).every(([, , , v]) => v <= 4), "SID-6581 WAVE stays one of its five waves");
const synOnly = T.mmMutate(M({ pages: ["SYN"] }), kitBase, info);
check(synOnly.every(([, pg]) => pg === "SYN") && same(synOnly, m40.filter(([, pg]) => pg === "SYN")), "keyed by (track, page, knob): another page in scope changes nothing for these");
check(same(T.mmMutate(M({ tracks: [2] }), kitBase, info), m40.filter(([t]) => t === 2)), "and another track in scope changes nothing for this one");
check(T.mmMutate(M({ pages: ["LFO"] }), kitBase, info).every(([, pg]) => /^LF[123]$/.test(pg)), "LFO is the three LFO pages");
check(T.mutPull(64, 0, 0.9) === 64 && T.mutPull(64, 100, 0.5) === 64 && T.mutPull(0, 100, 0.999, 4) === 4 && T.mutPull(100, 50, 0, 127) === 50, "mutPull: the Machinedrum's pull toward u × max");

console.log("mmGenTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
process.exit(failures ? 1 : 0);
