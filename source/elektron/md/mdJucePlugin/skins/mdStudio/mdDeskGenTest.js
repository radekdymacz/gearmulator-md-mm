"use strict";
/* The generators and the mutation (mdDeskGen.js, DESIGN-generators.md): pure, deterministic, pinned.
   Euclid is the formula of §4.1 (E(3,8) x..x..x., E(5,8) x.x.xx.x), the seeds the specified PRNG
   (mulberry32 over hash32(seed, track, step)), the role table is checked against the firmware's own
   machine list (elektronData/mdMachines.cpp), the mutation against §4.5.
     node mdDeskGenTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const ctx = vm.createContext({ console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "mdDeskGen.js"), "utf8")
	+ "\nthis.T = { hash32, genU, euclid, euclidHit, generate, genRole, genDefault, genTag, mutate, GEN_ROLES, genRotStep, genEveryN, genRamp, genRunFor, genFit, genRepeats, genSummary };", ctx);
const { hash32, genU, euclid, generate, genRole, genDefault, genTag, mutate, genRotStep, genEveryN, genRamp, genRunFor, genFit, genRepeats, genSummary } = ctx.T;
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const str = a => a.map(x => x ? "x" : ".").join("");
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

/* ---- euclid ---- */
check(str(euclid(3, 8)) === "x..x..x.", "E(3,8) = x..x..x. (tresillo)");
check(str(euclid(5, 8)) === "x.x.xx.x", "E(5,8) = x.x.xx.x (cinquillo)");
check(str(euclid(4, 16)) === "x...x...x...x...", "E(4,16) rot 0: four on the floor");
check(str(euclid(4, 16, 1)) === ".x...x...x...x..", "E(4,16) rot 1: one step later");
check(str(euclid(4, 16, 2)) === "..x...x...x...x.", "E(4,16) rot 2");
check(str(euclid(4, 16, 3)) === "...x...x...x...x", "E(4,16) rot 3");
check(str(euclid(2, 16, 4)) === "....x.......x...", "E(2,16) rot 4: beats 2 and 4 (the snare default)");
check(str(euclid(0, 8)) === "........" && str(euclid(8, 8)) === "xxxxxxxx", "E(0,8) is empty, E(8,8) every step");
const e64 = generate({ kind: "euclid", k: 3, n: 8, rot: 0 }, 0, 0, 64);
check(e64.on.length === 24 && e64.on.every(s => [0, 3, 6].includes(s % 8)), "the cycle repeats to 64 steps");
const e16 = generate({ kind: "euclid", k: 3, n: 8, rot: 0 }, 0, 16, 32);
check(same(e16.on, [16, 19, 22, 24, 27, 30]), "a range starts its cycle at its first step (steps 17-32)");
const hat = generate({ kind: "euclid", k: 8, n: 16, rot: 0, acc: { k: 4, rot: 0 } }, 2, 0, 16);
check(same(hat.on, [0, 2, 4, 6, 8, 10, 12, 14]) && same(hat.acc, [0, 4, 8, 12]), "accents: 4 over 8 hits fall on every other hit");
const tres = generate({ kind: "euclid", k: 3, n: 8, rot: 0, acc: { k: 1, rot: 0 } }, 0, 0, 16);
check(same(tres.acc, [0, 8]), "accents: 1 over 3 hits is the first hit of each cycle");
check(generate({ kind: "euclid", k: 3, n: 8, rot: 0 }, 0, 0, 8).acc === undefined, "no acc in the spec: no accents sent (they stay)");
check(generate({ kind: "keep" }, 0, 0, 16) === null, "keep: no row");

/* ---- the PRNG, pinned ---- */
check(hash32(0, 0, 0) === 493009611 && hash32(1, 2, 3) === 4093101951 && hash32(12345, 15, 63) === 3852176581, "hash32 pinned");
check(genU(0, 0, 0) === 0.9475175943225622 && genU(1, 2, 3) === 0.6054499999154359 && genU(12345, 15, 63) === 0.6911206755321473, "u = mulberry32(hash32) pinned");
let quarter = 0;
for (let i = 0; i < 100000; i++) quarter += genU(7, i % 16, i >> 4) < .25;
check(Math.abs(quarter / 100000 - .25) < .01, "u is uniform enough (" + (quarter / 1000).toFixed(1) + " % below 0.25)");

/* ---- random ---- */
const R = (density, seed, mode = "replace", acc) => ({ kind: "random", density, seed, mode, acc });
check(same(generate(R(30, 42), 3, 0, 64), generate(R(30, 42), 3, 0, 64)), "random: the same seed gives the same steps");
check(!same(generate(R(30, 42), 3, 0, 64).on, generate(R(30, 43), 3, 0, 64).on), "random: another seed other steps");
check(!same(generate(R(30, 42), 3, 0, 64).on, generate(R(30, 42), 4, 0, 64).on), "random: keyed by track (another track other steps)");
check(same(generate(R(30, 42), 3, 16, 32).on, generate(R(30, 42), 3, 0, 64).on.filter(s => s >= 16 && s < 32)), "random: keyed by step (a range is a window on the same steps)");
check(generate(R(0, 9), 1, 0, 64).on.length === 0 && generate(R(100, 9), 1, 0, 64).on.length === 64, "random: 0 % none, 100 % every step");
const cur = Array.from({ length: 64 }, (_, s) => s % 3 === 0);
const add = generate(R(25, 5, "add"), 0, 0, 64, cur), thin = generate(R(50, 5, "thin"), 0, 0, 64, cur);
check(cur.every((on, s) => !on || add.on.includes(s)) && add.on.length > cur.filter(Boolean).length, "random add: never removes a trig, adds some");
check(thin.on.every(s => cur[s]) && thin.on.length < cur.filter(Boolean).length && thin.on.length > 0, "random thin: never adds a trig, removes some");
const ra = generate(R(60, 77, "replace", { density: 50 }), 2, 0, 64);
check(ra.acc.length > 0 && ra.acc.length < ra.on.length && ra.acc.every(s => ra.on.includes(s)), "random accents: only on hits, about the density");

/* ---- roles against the firmware's machines (elektronData/mdMachines.cpp) ---- */
const src = fs.readFileSync(path.join(__dirname, "../../../elektronData/mdMachines.cpp"), "utf8");
const std = [...src.slice(src.indexOf("g_standard"), src.indexOf("}};")).matchAll(/\{\d+, "([A-Z0-9-]+)"\}/g)].map(m => m[1]);
const pad = n => String(n).padStart(2, "0");
const all = [...std, ...Array.from({ length: 16 }, (_, i) => "MID-" + pad(i + 1)), ...Array.from({ length: 48 }, (_, i) => "ROM-" + pad(i + 1)),
	...[1, 2, 3, 4].map(n => "RAM-R" + n), ...[1, 2, 3, 4].map(n => "RAM-P" + n)];
check(std.length === 62, "the firmware's machine list read (" + std.length + " standard machines)");
const drums = std.filter(m => /^(TRX|EFM|E12|P-I)-/.test(m));
const loose = drums.filter(m => genRole(m) === "other");
check(loose.length === 0, "every TRX, EFM, E12 and P-I machine has a drum role" + (loose.length ? ": not " + loose.join(" ") : ""));
check(all.filter(m => /^(MID|CTR|INP)-|^RAM-R|^GND-EMPTY$/.test(m)).every(m => genRole(m) === "keep"), "MID, CTR, INP, the empty track and the RAM recorders are left alone (keep)");
check(["GND-SIN", "GND-NS", "GND-IM", "ROM-05", "RAM-P2"].every(m => genRole(m) === "other"), "GND tones, ROM and RAM players are other (random 10 %)");
check(["TRX-BD", "TRX-B2", "EFM-BD", "E12-BD", "P-I-BD"].every(m => genRole(m) === "kick") && genTag(genDefault("TRX-BD")) === "E 4/16", "kicks: euclid 4/16");
check(["TRX-SD", "TRX-CP", "TRX-RS", "TRX-CL", "E12-BR"].every(m => genRole(m) === "snare") && genTag(genDefault("EFM-SD")) === "E 2/16+4", "snares and claps: euclid 2/16 rot 4");
check(["TRX-CH", "EFM-HH", "E12-CH", "P-I-HH"].every(m => genRole(m) === "hat") && genDefault("TRX-CH").acc.k === 4, "closed hats: euclid 8/16, 4 accents");
check(["TRX-OH", "E12-OH"].every(m => genRole(m) === "open"), "open hats: off-beats");
check(["E12-HT", "E12-LT", "P-I-MT", "TRX-XT", "TRX-XC"].every(m => genRole(m) === "perc") && genTag(genDefault("E12-HT")) === "R 15%", "toms and percussion: random 15 % (E12-HT is a hi tom)");
/* ---- STEPS and the pattern's length: the default cycle is min(16, length), never longer than the pattern ---- */
check(genDefault("TRX-BD", 32).n === 16 && genDefault("TRX-BD", 12).n === 12 && genDefault("TRX-BD", 8).n === 8, "a role's default STEPS is min(16, pattern length)");
check(genTag(genDefault("TRX-CH", 6)) === "E 6/6" && genDefault("TRX-CH", 6).acc.k === 4, "a default on a short pattern: hits inside the cycle (hats 8/16 on 6 steps: 6/6)");
check(genDefault("EFM-SD", 4).rot === 0 && genDefault("TRX-OH", 2).rot === 0, "a default's rotation inside a short cycle");
check(genDefault("E12-HT", 8).kind === "random" && genDefault("MID-01", 8).kind === "keep", "random and keep defaults do not depend on the length");
{
	const sp = { kind: "euclid", k: 5, n: 16, rot: 9, acc: { k: 5, rot: 0 } };
	check(genFit(sp, 32) === sp, "a spec that fits is the same value");
	const f = genFit(sp, 8);
	check(f !== sp && sp.n === 16 && f.n === 8 && f.k === 5 && f.rot === 1 && f.acc.k === 5, "a shorter pattern clamps STEPS to it, the rotation wraps, the spec itself is left alone");
	const g = genFit({ kind: "euclid", k: 12, n: 16, rot: 0, acc: { k: 9, rot: 0 } }, 4);
	check(g.n === 4 && g.k === 4 && g.acc.k === 4, "hits and accents clamp with STEPS");
	const r = { kind: "random", density: 20, seed: 3, mode: "replace" };
	check(genFit(r, 4) === r && genFit(sp, 0) === sp && genFit(null, 8) === null, "random, no length and no spec are left alone");
}
check(genRepeats(8, 32) === "×4" && genRepeats(16, 16) === "×1" && genRepeats(5, 16) === "×3+1" && genRepeats(12, 32) === "×2+8", "the repeats: length / steps, +the remainder");
check(genSummary({ kind: "euclid", k: 3, n: 8, rot: 0 }, 32) === "E 3/8 · ×4", "the summary: E 3/8 · ×4");
check(genSummary({ kind: "euclid", k: 2, n: 16, rot: 4 }, 24) === "E 2/16+4 · ×1+8" && genSummary({ kind: "random", density: 9, seed: 1, mode: "replace" }, 16) === "", "the summary with a rotation and a remainder; none for random");
const counts = {};
for (const m of all) counts[genRole(m)] = (counts[genRole(m)] || 0) + 1;
console.log("  roles over " + all.length + " machines: " + Object.entries(counts).map(([r, n]) => r + " " + n).join(", "));

/* ---- mutation ---- */
const NAMES = {
	"TRX-BD": ["PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP", "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR", "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"],
	"GND-NS": ["DEC", null, null, null, null, null, null, null, "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR", "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"],
	"MID-01": ["NOTE", "N2", "N3", "LEN", "VEL", "PB", "MW", "AT", "CC1D", "CC1V", "CC2D", "CC2V", "CC3D", "CC3V", "CC4D", "CC4V", "CC5D", "CC5V", "CC6D", "CC6V", "PCHG", "LFOS", "LFOD", "LFOM"] };
const names = m => NAMES[m] || NAMES["TRX-BD"];
const base = Array.from({ length: 16 }, (_, t) => ({ m: t === 1 ? "GND-NS" : t === 2 ? "MID-01" : t === 3 ? "CTR-AL" : "TRX-BD", v: Array.from({ length: 24 }, (_, i) => (t * 7 + i * 5) % 128) }));
const M = (o = {}) => Object.assign({ tracks: [0], groups: ["syn", "fx", "rt"], amount: 40, seed: 1234, protect: ["VOL"] }, o);
const at = (vals, t, i) => (vals.find(x => x[0] === t && x[1] === i) || [])[2];
const m0 = mutate(M({ amount: 0 }), base, names);
check(m0.every(([t, i, v]) => v === base[t].v[i]), "0 %: nothing moves");
const m40 = mutate(M(), base, names);
check(same(m40, mutate(M(), base, names)), "the same seed on the same base gives the same values");
check(!same(m40, mutate(M({ seed: 1235 }), base, names)), "another seed other values");
check(at(m40, 0, 17) === undefined && m40.length === 23, "VOL is protected by default (23 of 24 knobs)");
check(at(mutate(M({ protect: [] }), base, names), 0, 17) !== undefined, "an empty protect list moves VOL too");
check(m40.every(([t, i, v]) => v >= 0 && v <= 127 && Number.isInteger(v)), "values stay 0..127, whole");
check(m40.some(([t, i, v]) => v !== base[t].v[i]), "40 % moves the knobs");
const m100 = mutate(M({ amount: 100 }), base, names);
check(m100.every(([t, i, v]) => v === Math.round(genU(1234, t, i) * 127)), "100 % is the random target itself");
const mns = mutate(M({ tracks: [1] }), base, names);
check(mns.every(([, i]) => i === 0 || i >= 8) && mns.length === 16, "unnamed slots are never touched (GND-NS: DEC and the effects and routing pages)");
check(mutate(M({ tracks: [2, 3] }), base, names).length === 0, "MID and CTR tracks are skipped");
const fxOnly = mutate(M({ groups: ["fx"] }), base, names);
check(fxOnly.length === 8 && fxOnly.every(([, i]) => i >= 8 && i < 16), "scope fx: the effects page only");
check(same(mutate(M({ tracks: [0, 4], groups: ["fx"] }), base, names).filter(([t]) => t === 0), fxOnly), "keyed by (track, param): another track in scope changes nothing for this one");
const grp = (t, id) => id === "syn:pitch" ? ["PTCH", "RAMP", "RDEC"] : [];
const gp = mutate(M({ groups: ["syn:pitch"] }), base, names, grp);
check(same(gp.map(x => x[1]), [0, 2, 3]), "a Sound-page group: its knobs only");
const moved = base.map((tr, t) => ({ m: tr.m, v: tr.v.slice() }));
for (const [t, i, v] of m40) moved[t].v[i] = v;
const walk = mutate(M({ seed: 99 }), moved, names), again = mutate(M({ seed: 99 }), base, names);
check(!same(walk, again) && again.every(([t, i, v]) => Math.abs(v - base[t].v[i]) <= Math.abs(Math.round(genU(99, t, i) * 127) - base[t].v[i]) + 1),
	"from the base and from a moved kit differ; Again pulls from the base");

/* ---- small comforts (§7) ---- */
check(genRotStep(0, 1, 16) === 1 && genRotStep(15, 1, 16) === 0 && genRotStep(0, -1, 16) === 15, "rotate: one step later wraps at the length, earlier wraps back");
check(genRotStep(20, 1, 16) === 20 && genRotStep(5, 3, 1) === 5, "rotate: steps from the length on stay; a one-step pattern does not move");
check([...Array(12).keys()].map(s => genRotStep(s, 5, 12)).sort((a, b) => a - b).join() === [...Array(12).keys()].join(), "rotate: a permutation of the steps");
const few = Array(64).fill(false); few[1] = few[5] = true;
check(same(genEveryN(few, 4, 16, 2, true), [4, 5, 6, 8, 10, 12, 14]), "fill every 2nd from step 5: the grid on, the other trigs kept");
check(same(genEveryN(few, 0, 16, 4, true), [0, 1, 4, 5, 8, 12]), "fill every 4th from step 1");
const allOn = Array(64).fill(true);
check(same(genEveryN(allOn, 0, 8, 2, false), [1, 3, 5, 7]), "from a trig: the grid turns off, the rest stays");
check(same(genRamp(0, 0, 4, 100), [[0, 0], [1, 25], [2, 50], [3, 75], [4, 100]]), "ramp: a straight line, one value a step");
check(same(genRamp(4, 100, 0, 0), [[0, 0], [1, 25], [2, 50], [3, 75], [4, 100]]), "ramp: drawn right to left, the same line");
check(same(genRamp(0, 10, 6, 70, s => s % 2 === 0), [[0, 10], [2, 30], [4, 50], [6, 70]]), "ramp: only the steps with a trig");
check(same(genRamp(3, 0, 3, 64), [[3, 64]]), "ramp: a click is one step at its value");
check(genRamp(0, -40, 2, 300).every(([, v]) => v >= 0 && v <= 127), "ramp: values stay 0..127");

/* ---- the live GEN run (§4.6): one key, one gesture, one base; values moved back give the steps back ---- */
{
	let g = 0, bases = 0;
	const gesture = () => ++g, pat = Array(64).fill(false); [0, 4, 8, 12].forEach(s => pat[s] = true);
	const base = () => { bases++; return [pat.slice()]; };
	const r1 = genRunFor(null, "seq:2:5", base, gesture), r2 = genRunFor(r1, "seq:2:5", base, gesture);
	check(r1 === r2 && r1.g === 1 && g === 1 && bases === 1, "run: changes in one context are one run, one gesture, one base");
	const r3 = genRunFor(r2, "seq:3:5", base, gesture);
	check(r3 !== r2 && r3.g === 2 && r3.applied === 0 && bases === 2, "run: another track is a new run, a new gesture and base");
	check(genRunFor(r3, "sound:3:5", base, gesture).g === 3 && genRunFor(r3, "seq:3:6", base, gesture).g === 4, "run: another workspace or pattern is a new run");
	/* ADD over a live run: from the base, 10% -> 40% -> 10% is the same as 10% at once (no pile-up) */
	const add = d => generate({ kind: "random", density: d, seed: 77, mode: "add" }, 0, 0, 32, r1.base[0]).on;
	const now = add(10); let cur = r1.base[0].slice();
	for (const d of [10, 40, 10]) { cur = Array(64).fill(false); add(d).forEach(s => cur[s] = true); }
	check(same(cur.flatMap((x, s) => x ? [s] : []), now) && now.length >= 4, "run: ADD from the base, a value moved back gives the same steps");
	const thin = generate({ kind: "random", density: 0, seed: 77, mode: "thin" }, 0, 0, 32, r1.base[0]).on;
	check(thin.length === 0 && same(generate({ kind: "random", density: 100, seed: 77, mode: "thin" }, 0, 0, 32, r1.base[0]).on, [0, 4, 8, 12]), "run: THIN from the base keeps only the base's steps");
}

console.log(failures ? `mdDeskGenTest: ${failures} failure(s)` : "mdDeskGenTest: PASS");
process.exit(failures ? 1 : 0);
