"use strict";
/* Rhythm generators and sound mutation (doc/modern-ux/DESIGN-generators.md): pure functions, values in,
   values out. A generator gives a track's steps in a range, a mutation a list of kit values; the page
   hands them to the two plain edits (steps, params) and never sends a recipe. The same spec on the same
   pattern or kit gives the same result, here and in node (mdDeskGenTest.js). Nothing here reads the page.
   The generators without a machine in them are skins/shared/deskGen.js (both editors, loaded before this
   file, tested by shared/deskGenTest.js); this file is the Machinedrum's own. */

/* ---- the Machinedrum's own: the roles of its machines and the mutation of its kit (24 values a track) ---- */
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
/* a euclid default's cycle: 16 steps, or the pattern's length when it is shorter */
const GEN_DEFAULTS = {
	kick: n => ({ kind: "euclid", k: 4, n, rot: 0 }),
	snare: n => ({ kind: "euclid", k: 2, n, rot: 4 }),
	hat: n => ({ kind: "euclid", k: 8, n, rot: 0, acc: { k: 4, rot: 0 } }),
	open: n => ({ kind: "euclid", k: 2, n, rot: 2 }),
	cymbal: () => ({ kind: "random", density: 12, seed: genSeed(), mode: "replace" }),
	perc: () => ({ kind: "random", density: 15, seed: genSeed(), mode: "replace" }),
	other: () => ({ kind: "random", density: 10, seed: genSeed(), mode: "replace" }),
	keep: () => ({ kind: "keep" }) };
function genRole(m) { return (GEN_ROLES.find(([, re]) => re.test(m || "GND-EMPTY")) || ["other"])[0]; }
/* len: the pattern's length (1..64) */
function genDefault(m, len = 64) { return genFit(GEN_DEFAULTS[genRole(m)](Math.min(16, len)), len); }
/* ---- mutation (DESIGN-generators.md §4.5): each named knob in scope pulled toward a random target,
   v' = round(v + amount / 100 * (u(seed, t, i) * 127 - v)): 0 % moves nothing, 100 % is fully random,
   never out of 0..127. Values in, values out:
     spec: { tracks: [t...], groups: ["syn" | "fx" | "rt" | "<g>:<key>" ...], amount 0..100, seed, protect: ["VOL"] }
     base: 16 tracks of { m: machine name, v: [24 values] } (the kit before the trial)
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
			out.push([t, i, mutPull(tr.v[i], spec.amount, genU(spec.seed, t, i))]);
		}
	}
	return out;
}
/* ---- a mutation trial (DESIGN-generators.md §4.6), as values: { key, g, kit, base, touched, all, applied }.
   base: the kit before the trial (mutate's base); touched: "t:i" of every value an apply moved off its base.
   One apply always starts from the base, so a value an earlier apply moved and this one does not goes back to
   the base: nextTrial(trial, values, all) -> { values: what to send (the apply's, plus those resets),
   trial: the trial after it (touched from what is sent, all, applied + 1) }. Nothing is changed in place. */
function mutTrial(key, g, kit, base) { return { key, g, kit, base, touched: new Set(), all: false, applied: 0 }; }
function nextTrial(trial, values, all) {
	const now = new Set(values.map(([t, i]) => t + ":" + i)), out = values.slice();
	for (const k of trial.touched) if (!now.has(k)) { const [t, i] = k.split(":").map(Number); out.push([t, i, trial.base[t].v[i]]); }
	const touched = new Set(out.filter(([t, i, v]) => v !== trial.base[t].v[i]).map(([t, i]) => t + ":" + i));
	return { values: out, trial: Object.assign({}, trial, { touched, all, applied: trial.applied + 1 }) };
}
