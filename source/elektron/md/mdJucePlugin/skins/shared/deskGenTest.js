"use strict";
/* The generators without a machine in them (deskGen.js, both editors; DESIGN-generators.md): pure,
   deterministic, pinned. Euclid is the formula of §4.1 (E(3,8) x..x..x., E(5,8) x.x.xx.x), the seeds the
   specified PRNG (mulberry32 over hash32(seed, track, step)); a spec fitted to a pattern's length, the small
   comforts (§7) and the live GEN run (§4.6). The machines' own parts: mdStudio/mdDeskGenTest.js (the MD's
   roles and mutation), mmStudio/mmGenTest.js (the MM's).
     node deskGenTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const ctx = vm.createContext({ console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskGen.js"), "utf8")
	+ "\nthis.T = { hash32, genU, euclid, euclidHit, generate, genTag, genRotStep, genEveryN, genRamp, genRunFor, genFit, genRepeats, genSummary };", ctx);
const { hash32, genU, euclid, generate, genRotStep, genEveryN, genRamp, genRunFor, genFit, genRepeats, genSummary } = ctx.T;
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

/* ---- a spec fitted to the pattern's length; the repeats and the summary ---- */
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

console.log(failures ? `deskGenTest: ${failures} failure(s)` : "deskGenTest: PASS");
process.exit(failures ? 1 : 0);
