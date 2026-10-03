"use strict";
/* The Machinedrum's own generator parts (mdDeskGen.js, over the shared deskGen.js; DESIGN-generators.md): the
   role table checked against the firmware's own machine list (elektronData/mdMachines.cpp), the role
   defaults on a pattern's length, the mutation against §4.5. The shared generators: shared/deskGenTest.js.
     node mdDeskGenTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const ctx = vm.createContext({ console });
vm.runInContext(["../shared/deskGen.js", "mdDeskGen.js"].map(f => fs.readFileSync(path.join(__dirname, f), "utf8")).join("\n")
	+ "\nthis.T = { genU, genRole, genDefault, genTag, mutate, mutTrial, nextTrial, GEN_ROLES };", ctx);
const { genU, genRole, genDefault, genTag, mutate, mutTrial, nextTrial } = ctx.T;
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const str = a => a.map(x => x ? "x" : ".").join("");
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

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

/* ---- a trial as values (§4.6): each apply from the base; what an earlier apply moved and this one does not goes back */
{
	const t0 = mutTrial("sound:1:3", 7, 3, base);
	const a1 = nextTrial(t0, [[0, 1, 99], [0, 2, base[0].v[2]]], false);
	check(same(a1.values, [[0, 1, 99], [0, 2, base[0].v[2]]]) && same([...a1.trial.touched], ["0:1"]) && a1.trial.applied === 1 && !a1.trial.all
		&& t0.touched.size === 0 && t0.applied === 0, "nextTrial: touched is what moved off the base; the trial before it is unchanged");
	const a2 = nextTrial(a1.trial, [[0, 3, 5]], true);
	check(same(a2.values, [[0, 3, 5], [0, 1, base[0].v[1]]]) && same([...a2.trial.touched], ["0:3"]) && a2.trial.all && a2.trial.applied === 2 && a2.trial.g === 7
		&& same([...a1.trial.touched], ["0:1"]), "nextTrial: a value no longer moved goes back to the base, as its own value");
}

console.log(failures ? `mdDeskGenTest: ${failures} failure(s)` : "mdDeskGenTest: PASS");
process.exit(failures ? 1 : 0);
