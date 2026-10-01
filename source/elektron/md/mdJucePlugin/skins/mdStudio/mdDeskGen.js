"use strict";
/* Rhythm generators and sound mutation (doc/modern-ux/DESIGN-generators.md): pure functions, values in,
   values out. A generator gives a track's steps in a range, a mutation a list of kit values; the page
   hands them to the two plain edits (steps, params) and never sends a recipe. The same spec on the same
   pattern or kit gives the same result, here and in node (mdDeskGenTest.js). Nothing here reads the page. */

/* ---- seeds: u(seed, a, b) in [0, 1), keyed by (track, step) or (track, param), so a scope change never
   reshuffles the others. hash32 mixes the three into one 32-bit word (murmur3's finaliser constants);
   mulberry32's first draw from it is u. */
function hash32(seed, a, b) {
	let h = (seed ^ 0x9e3779b9) >>> 0;
	h = Math.imul(h ^ (a >>> 0), 0x85ebca6b); h ^= h >>> 13;
	h = Math.imul(h ^ (b >>> 0), 0xc2b2ae35); h ^= h >>> 16;
	return h >>> 0;
}
function mulberry32(a) {
	a = (a + 0x6d2b79f5) | 0;
	let t = Math.imul(a ^ (a >>> 15), 1 | a);
	t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
	return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const genU = (seed, a, b) => mulberry32(hash32(seed, a, b));
/* a new seed the user can read and type: 1..99999 */
const genSeed = () => 1 + Math.floor(Math.random() * 99999);

/* ---- euclid: step s (0-based from the range's start) is a hit when ((s - rot) mod n) * k mod n < k.
   E(3,8) x..x..x., E(5,8) x.x.xx.x; rot moves the hits later; the cycle repeats. */
const genMod = (a, n) => ((a % n) + n) % n;
function euclidHit(s, k, n, rot = 0) { return n > 0 && k > 0 && (genMod(s - rot, n) * k) % n < k; }
function euclid(k, n, rot = 0, len = n) { return Array.from({ length: len }, (_, s) => euclidHit(s, k, n, rot)); }

/* A track's steps from a spec, over [from, to): { on: [step...], acc: [step...] | undefined }, or null for
   "keep". cur: the track's trigs now (64 booleans), for random's add and thin. t: the track (the seed's key).
     { kind: "euclid", k, n, rot, acc?: { k, rot } }   accents spread over the hits (acc.k over k)
     { kind: "random", density, seed, mode: "replace" | "add" | "thin", acc?: { density } } */
function generate(spec, t, from, to, cur = []) {
	if (!spec || spec.kind === "keep") return null;
	const on = [], acc = spec.acc ? [] : undefined;
	if (spec.kind === "euclid") {
		const k = Math.max(0, Math.min(spec.n, spec.k | 0)), n = Math.max(1, spec.n | 0);
		let h = 0;
		for (let s = from; s < to; s++) {
			if (!euclidHit(s - from, k, n, spec.rot | 0)) continue;
			on.push(s);
			if (acc && euclidHit(h, spec.acc.k | 0, k, spec.acc.rot | 0)) acc.push(s);
			h++;
		}
		return { on, acc };
	}
	if (spec.kind === "random") {
		for (let s = from; s < to; s++) {
			const r = genU(spec.seed, t, s) * 100 < spec.density, was = !!cur[s];
			const hit = spec.mode === "add" ? was || r : spec.mode === "thin" ? was && r : r;
			if (!hit) continue;
			on.push(s);
			if (acc && genU(spec.seed ^ 0xacc, t, s) * 100 < spec.acc.density) acc.push(s);
		}
		return { on, acc };
	}
	return null;
}

/* ---- defaults per machine role (DESIGN-generators.md §4.2), on the machine's name, first match wins.
   Checked against the catalogue's machines in mdDeskGenTest.js: every drum machine has a role, the
   MIDI, CTR and input machines, the empty track and the RAM recorders are left alone. */
const GEN_ROLES = [
	["keep", /^(MID-|CTR-|INP-|RAM-R|GND-EMPTY$)/],
	["kick", /-(BD|B2)$/],
	["snare", /-(SD|CP|RS|CL|BR)$/],
	["hat", /-(CH|HH)$/],
	["open", /-OH$/],
	["cymbal", /-(CY|RC|CC|CB)$/],
	["perc", /-(XT|XC|LT|MT|HT|MA|ML|TA|TR|SH|BC)$/],
	["other", /./]];
const GEN_DEFAULTS = {
	kick: () => ({ kind: "euclid", k: 4, n: 16, rot: 0 }),
	snare: () => ({ kind: "euclid", k: 2, n: 16, rot: 4 }),
	hat: () => ({ kind: "euclid", k: 8, n: 16, rot: 0, acc: { k: 4, rot: 0 } }),
	open: () => ({ kind: "euclid", k: 2, n: 16, rot: 2 }),
	cymbal: () => ({ kind: "random", density: 12, seed: genSeed(), mode: "replace" }),
	perc: () => ({ kind: "random", density: 15, seed: genSeed(), mode: "replace" }),
	other: () => ({ kind: "random", density: 10, seed: genSeed(), mode: "replace" }),
	keep: () => ({ kind: "keep" }) };
function genRole(m) { return (GEN_ROLES.find(([, re]) => re.test(m || "GND-EMPTY")) || ["other"])[0]; }
function genDefault(m) { return GEN_DEFAULTS[genRole(m)](); }
/* the spec's short tag for the track header: E 4/16, R 15%, — */
function genTag(spec) {
	if (!spec || spec.kind === "keep") return "—";
	return spec.kind === "euclid" ? `E ${spec.k}/${spec.n}${spec.rot ? "+" + spec.rot : ""}` : `R ${spec.density}%${spec.mode === "replace" ? "" : spec.mode === "add" ? "+" : "−"}`;
}

/* ---- mutation (DESIGN-generators.md §4.5): each named knob in scope pulled toward a random target,
   v' = round(v + amount / 100 * (u(seed, t, i) * 127 - v)): 0 % moves nothing, 100 % is fully random,
   never out of 0..127. Values in, values out:
     spec: { tracks: [t...], groups: ["syn" | "fx" | "rt" | "<g>:<key>" ...], amount 0..100, seed, protect: ["VOL"] }
     base: 16 tracks of { m: machine name, v: [24 values] } (the kit before the trial, or now for Walk)
     names(m): the machine's 24 parameter names (null where it has none; "SYN·DIST" for a synthesis DIST)
     groupKnobs(t, id): the names of a Sound-page group ("syn:pitch", "fx:flt"...) on that track, or []
   -> [[t, i, v]...] for every knob in scope (an unmoved one too, so an Again from the base resets it). */
const MUT_SKIP = /^(MID-|CTR-|GND-EMPTY$)/;
const MUT_PAGE = { syn: 0, fx: 8, rt: 16 };
function mutate(spec, base, names, groupKnobs = () => []) {
	const out = [], protect = new Set(spec.protect || ["VOL"]), plain = n => String(n).replace(/^SYN·/, "");
	for (const t of spec.tracks) {
		const tr = base[t];
		if (!tr || MUT_SKIP.test(tr.m || "GND-EMPTY")) continue;
		const slots = names(tr.m), idx = new Set();
		for (const g of spec.groups) {
			if (g in MUT_PAGE) { for (let i = MUT_PAGE[g]; i < MUT_PAGE[g] + 8; i++) idx.add(i); continue; }
			for (const n of groupKnobs(t, g)) { const i = slots.indexOf(n); if (i >= 0) idx.add(i); }
		}
		for (const i of [...idx].sort((a, b) => a - b)) {
			if (!slots[i] || protect.has(plain(slots[i]))) continue;
			const v = tr.v[i], target = genU(spec.seed, t, i) * 127;
			out.push([t, i, Math.max(0, Math.min(127, Math.round(v + spec.amount / 100 * (target - v))))]);
		}
	}
	return out;
}

/* ---- small comforts (DESIGN-generators.md §7): pure step arithmetic the Sequence page sends as plain edits ---- */
/* rotate: where step s goes when a track moves by steps, wrapping inside [0, len); steps from len on stay */
function genRotStep(s, by, len) { return s < len && len > 1 ? genMod(s + by, len) : s; }
/* every-N fill from step s to the end (to, exclusive): the track's on steps in [s, to) with every n-th step
   from s turned on (or off): cur is the track's 64 trigs now */
function genEveryN(cur, s, to, n, on) {
	const out = [];
	for (let k = s; k < to; k++) { const grid = (k - s) % n === 0; if (grid ? on : !!cur[k]) out.push(k); }
	return out;
}
/* a ramp in a lock lane: a straight line from (s0, v0) to (s1, v1), one value 0..127 a step, either way round;
   only the steps where keep(s) is true (a step with a trig) */
function genRamp(s0, v0, s1, v1, keep = () => true) {
	const out = [], a = Math.min(s0, s1), b = Math.max(s0, s1);
	for (let s = a; s <= b; s++) {
		if (!keep(s)) continue;
		const v = s1 === s0 ? v1 : v0 + (v1 - v0) * (s - s0) / (s1 - s0);
		out.push([s, Math.max(0, Math.min(127, Math.round(v)))]);
	}
	return out;
}
