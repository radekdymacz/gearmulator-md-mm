"use strict";
/* The GEN bar (Sequence) and the MUTATE bar (Sound) over the pure generators and mutation (mdDeskGen.js,
   shared/deskGen.js). Their state is values, replaced per change, never changed in place: S.gen.specs (a
   spec a track), S.gen.run (the run of GEN changes that is one undo step), S.mut.trial (the MUTATE trial),
   S.mut.scope (what MUTATE moves). The GEN and MUTATE values' drag is mdDeskGestures.js's. */

/* ===== Generators and mutation (DESIGN-generators.md): the GEN bar, always on the Sequence page, and the
   MUTATE bar, always on the Sound page. Both are live: a change writes at once, and a run of changes is one
   gesture (one undo step) that one Undo takes back. The results come from mdDeskGen.js (pure); the page sends
   them as the two plain edits: steps (one pattern change, one dump) and params (one working-kit change, CCs).
   Alt is the global "all": Alt + a GEN change every track. One randomise action (R, the bars' R key) means what
   the workspace edits: a new GEN variation of the selected track (Sequence, and every workspace but Sound), a fresh
   mutation of the selected track from the trial's base (Sound); Alt+R or Alt-click on the R key: every track (the
   whole kit). ===== */
S.gen = { specs: null, last: [], run: null };
S.mut = { amount: 20, seed: genSeed(), scope: new Set(["syn"]), trial: null, note: "" };
const GEN_TRACKS = Array.from({ length: 16 }, (_, t) => t);
/* Every track's spec, from its machine's role the first time (render, or a GEN change before one) and on
   Defaults (fill): the one place the specs are made; reading them never writes. */
function genEnsure(fill) {
	if (S.gen.specs && !fill) return;
	const len = V.len || 64;
	S.gen = Object.assign({}, S.gen, { specs: V.tracks.map(t => genDefault(t.m, len)), last: V.tracks.map(() => ({})) });
}
/* every track's spec as it applies to the pattern now: fitted on read (STEPS never longer than the pattern, also
   when it got shorter; the stored spec keeps its own) */
function genSpecs() {
	const len = V.len || 64;
	return (S.gen.specs || V.tracks.map(t => genDefault(t.m, len))).map(sp => genFit(sp, len));
}
function genSpec(t = S.sel) { return genSpecs()[t]; }
/* track t's spec replaced by sp (the specs' new value) */
function setGenSpec(t, sp) { genEnsure(); S.gen = Object.assign({}, S.gen, { specs: S.gen.specs.map((x, i) => i === t ? sp : x) }); }
/* the range a generator writes: the steps shown (one page, or all), Alt: the whole pattern */
function genRange(all) { const [a, b] = vis(); return all ? [0, V.len] : [a, Math.min(b, V.len)]; }
/* A run (the GEN bar) or a trial (the MUTATE bar) lives in one context: the workspace, the selected track and
   the pattern (kit). Selecting another track, another workspace or another pattern ends it; so does any other
   edit, undo or redo (cmd). */
const genKey = () => `${S.ws}:${S.sel}:${V.pat}`, mutKey = () => `${S.ws}:${S.sel}:${V.kit}`;
function endStaleRuns() {
	if (S.gen.run && S.gen.run.key !== genKey()) S.gen.run = null;
	if (S.mut.trial && S.mut.trial.key !== mutKey()) { S.mut.trial = null; S.mut.note = ""; }
}
/* a GEN run's own steps edits carry its gesture; any other edit (and undo, redo) ends it (§4.6) */
onEditSent((op, args) => { if (S.gen && S.gen.run && !(op === "steps" && args.g === S.gen.run.g)) S.gen.run = null; });
/* Live: every change of a GEN control writes at once. The run's changes share one gesture (one undo step back
   to the pattern before the run) and generate from that pattern, so a value moved back gives its steps back.
   Alt held: every track's spec over the whole pattern, in the same run. The run is replaced per apply. */
function genLive(all = S.alt) {
	if (V.rec) { toast("The generators wait while the machine records live."); return false; }
	if (!V.loaded) { toast("The pattern is not loaded yet."); return false; }
	const run = genRunFor(S.gen.run, genKey(), () => V.tracks.map(x => x.trigs.slice()), Bridge.gesture);
	const [from, to] = genRange(all), rows = [], w = [];
	for (const t of all ? GEN_TRACKS : [S.sel]) {
		const r = genResult(t, from, to, run); if (!r) continue;
		const tr = V.tracks[t], on = new Set(r.on), row = { t, on: r.on };
		if (r.acc && !V.accAll) row.acc = r.acc;
		const acc = row.acc ? new Set(row.acc) : null;
		for (let s = from; s < to; s++) {
			const want = on.has(s);
			if (want !== tr.trigs[s]) w.push([["tracks", t, "trigs", s], want]);
			if (!want && tr.trigs[s]) w.push([["tracks", t, "acc", s], false], [["tracks", t, "slide", s], false], ...clearStep(t, s));
			if (acc && want) w.push([["tracks", t, "acc", s], acc.has(s)]);
		}
		rows.push(row);
	}
	if (!rows.length) { if (all) toast("Every track is set to keep: nothing to generate."); genDraw(); return false; }
	S.gen.run = run;
	cmd("steps", { p: V.pat, from, to, rows, g: run.g }, "gen", w);
	S.gen.run = Object.assign({}, run, { applied: run.applied + 1 });
	render();
	return true;
}
/* a track's result in a run: its spec from the run's base; Keep during a run gives the base back */
function genResult(t, from, to, run) {
	const base = run ? run.base[t] : V.tracks[t].trigs, r = generate(genSpec(t), t, from, to, base);
	if (r || !run || !run.applied) return r;
	const on = []; for (let s = from; s < to; s++) if (base[s]) on.push(s);
	return { on };
}
/* Defaults (the GEN bar's Defaults key): every track's spec from its machine, written as a change */
function genDefaults() { genEnsure(true); toast("Every track's spec from its machine."); genLive(); }
/* GEN randomise: a new variation of a spec: a new seed (random), random hits and rotation inside the cycle
   (euclid); null for keep (nothing to vary). */
function genVaried(sp) {
	const r = n => Math.floor(Math.random() * n);
	if (sp.kind === "random") return Object.assign({}, sp, { seed: genSeed() });
	if (sp.kind !== "euclid") return null;
	const k = 1 + r(sp.n), rot = r(sp.n), out = Object.assign({}, sp, { k, rot });
	if (sp.acc) out.acc = Object.assign({}, sp.acc, { k: Math.min(sp.acc.k, k) });
	return out;
}
/* R on Sequence (and every workspace but Sound): the selected track; all: every track's spec, the whole pattern */
function genAgain(all = false) {
	genEnsure();
	let varied = 0;
	for (const t of all ? GEN_TRACKS : [S.sel]) { const sp = genVaried(genSpec(t)); if (sp) { setGenSpec(t, sp); varied++; } }
	if (!varied) { toast(all ? "Every track is set to keep: nothing to randomise." : `Track ${S.sel + 1} is set to keep: nothing to randomise.`); return; }
	if (genLive(all) && S.ws !== "seq") toast(all ? "GEN: a new variation of every track." : `GEN: a new variation of track ${S.sel + 1}.`);
}
/* the one randomise action (R; Alt+R or Alt-click the R key: all), by workspace */
function randomise(all) { if (S.ws === "sound") mutAgain(all); else genAgain(all); }
function genKind(kind) {
	genEnsure();
	const t = S.sel, sp = genSpec(t);
	if (sp.kind === kind) return;
	const last = Object.assign({}, S.gen.last[t], { [sp.kind]: sp });
	S.gen = Object.assign({}, S.gen, { last: S.gen.last.map((x, i) => i === t ? last : x) });
	setGenSpec(t, genFit(last[kind], V.len || 64) || (kind === "euclid" ? { kind, k: 4, n: Math.min(16, V.len || 64), rot: 0 } : kind === "random" ? { kind, density: 25, seed: genSeed(), mode: "replace" } : { kind }));
	genLive();
}
/* a spec with value k moved by d (the spec's own ranges; len: the pattern's length): a new spec */
function genStepped(sp, k, d, len) {
	const lim = (v, a, b) => Math.max(a, Math.min(b, v)), x = Object.assign({}, sp);
	const accK = () => { if (x.acc) x.acc = Object.assign({}, x.acc, { k: Math.min(x.acc.k, x.k) }); };
	if (k === "k") x.k = lim(x.k + d, 0, x.n);
	if (k === "n") { x.n = lim(x.n + d, 1, Math.min(64, len)); x.k = Math.min(x.k, x.n); x.rot = Math.min(x.rot, x.n - 1); accK(); }
	if (k === "rot") x.rot = ((x.rot + d) % x.n + x.n) % x.n;
	if (k === "acc") { const a = lim((x.acc ? x.acc.k : 0) + d, 0, x.k); if (a) x.acc = { k: a, rot: 0 }; else delete x.acc; }
	if (k === "dens") x.density = lim(x.density + d, 0, 100);
	if (k === "racc") { const a = lim((x.acc ? x.acc.density : 0) + d, 0, 100); if (a) x.acc = { density: a }; else delete x.acc; }
	if (k === "seed") x.seed = ((x.seed - 1 + d) % 99999 + 99999) % 99999 + 1;
	return x;
}
/* a value of a bar moved by d (wheel, arrows, click, drag) */
function genVal(k, d) {
	if (k === "amt") { S.mut.amount = Math.max(0, Math.min(100, S.mut.amount + d)); mutLive(); renderMutStrip(); return; }
	genEnsure();
	setGenSpec(S.sel, genStepped(genSpec(), k, d, V.len || 64));
	genLive();
}
/* the Write mode of a random spec (Replace, Add, Thin) */
function genMode(mode) { const sp = genSpec(); if (sp.mode === mode) return; setGenSpec(S.sel, Object.assign({}, sp, { mode })); genLive(); }
/* The bars' pieces: a group (a title on a thin rule over its controls), a value (an LCD window, its label inside),
   a key hint. */
const gbg = (label, body, cls = "", tip = "") => `<div class="gbg ${cls}"${tip ? ` title="${tip}"` : ""}><span class="gbl">${label}</span><div class="gbc">${body}</div></div>`;
const gv = (k, label, v, tip) => `<span class="gv" data-gv="${k}" role="spinbutton" tabindex="0" aria-label="${label}" aria-valuenow="${parseInt(v) || 0}" title="${tip}. Drag up or down, scroll, or click (⇧-click: down)."><small>${label}</small><b>${v}</b></span>`;
const kbd = k => `<kbd>${k}</kbd>`;
/* a key of a bar: a small square cap with its key on it (the keyboard shortcut too) and a tiny label over it */
const kc = (attr, act, cap, label, tip, off = false, cls = "", on = null) => `<button class="kc ${cls}${on ? " on" : ""}" ${attr}="${act}" ${off ? "disabled" : ""}${on != null ? ` aria-pressed="${on}"` : ""} title="${tip}" aria-label="${label}"><small>${label}</small><kbd>${cap}</kbd></button>`;
/* the bars' one randomise key: R, with its "all" chord under it */
const randKey = tip => `<button class="kc cream krand" data-rand="1" title="${tip}" aria-label="Randomise"><small>Random <em>⌥R all</em></small><kbd>R</kbd></button>`;
const gtitle = (name, target, all, tip) => `<div class="gbt" title="${tip}"><b>${name}</b><span class="${all ? "all" : ""}">${target}</span></div>`;
function genStripHtml() {
	const sp = genSpec(), t = S.sel, tr = V.tracks[t], all = S.alt, run = S.gen.run && S.gen.run.key === genKey() ? S.gen.run : null;
	const [from, to] = genRange(all), r = genResult(t, from, to, run), base = run ? run.base[t] : tr.trigs;
	const seg = (attr, cur, items, tips) => `<span class="seg">${items.map(([k, n]) => `<button ${attr}="${k}" aria-pressed="${cur === k}" title="${tips[k]}">${n}</button>`).join("")}</span>`;
	const mode = gbg("Mode", seg("data-genkind", sp.kind, [["euclid", "Euclid"], ["random", "Random"], ["keep", "Keep"]], { euclid: "k hits spread evenly over n steps, rotated", random: "each step on by chance, from a seed", keep: "leave this track as it is (in a run: as it was before the run)" }), "gmode");
	const params = sp.kind === "euclid"
		? gbg("Euclid", gv("k", "Hits", sp.k, "How many hits in a cycle") + gv("n", "Steps", sp.n, "The cycle's length in steps, at most the pattern's; it repeats over the pattern") + gv("rot", "Rotate", sp.rot, "Moves the hits later") + gv("acc", "Accent", sp.acc ? sp.acc.k : "off", "Accents spread over the hits" + (V.accAll ? " (EDIT ALL is on: accents are pattern-wide and stay)" : "")))
		: sp.kind === "random"
		? gbg("Random", gv("dens", "Density", sp.density + "%", "The chance of each step") + gv("racc", "Accent", sp.acc ? sp.acc.density + "%" : "off", "Accents by chance on the hits")
			+ gv("seed", "Seed", sp.seed, "The same seed gives the same steps") )
			+ gbg("Write", seg("data-genmode", sp.mode, [["replace", "Replace"], ["add", "Add"], ["thin", "Thin"]], { replace: "the track becomes the result", add: "only steps that are off may turn on", thin: "only steps that are on may turn off" }), "gwrite")
		: gbg("Keep", `<span class="gsum">Track ${t + 1} is left as it is.</span>`);
	let sum = "";
	if (r) {
		const b = new Set(); for (let s = from; s < to; s++) if (base[s]) b.add(s);
		const add = r.on.filter(s => !b.has(s)).length, gone = [...b].filter(s => !r.on.includes(s)).length;
		sum = all ? `every track · steps ${from + 1}–${to}` : `${sp.kind === "euclid" ? genSummary(sp, V.len) + " · " : ""}${r.on.length} on${run ? ` <em>+${add} −${gone}</em>` : ""} · steps ${from + 1}–${to}`;
	} else if (all) sum = `every track · steps ${from + 1}–${to}`;
	const live = run && run.applied;
	return `${gtitle("Gen", all ? "all tracks" : `track ${t + 1} · ${codeOf(tr.m, Cat)}`, all, "Generators: every change writes to the pattern at once. A run of changes on this track is one undo step; Undo takes it back in one step. Alt: every track's spec, the whole pattern.")}
  ${mode}${params}
  <div class="gsum" title="${live ? "What the run changed, against the pattern before it" : "What a change writes"}">${sum}</div>
  <div class="gkeys">${randKey(all ? "Randomise every track: a new variation of each spec, the whole pattern (Alt+R)" : `Randomise track ${t + 1}: a new variation, ${sp.kind === "random" ? "a new seed" : sp.kind === "euclid" ? "random hits and rotation in the cycle" : "nothing while it is set to Keep"} (R). Alt+R or Alt-click: every track`)}${kc("data-gen", "fill", "↺", "Defaults", "Defaults: every track's spec from its machine: kicks 4/16, snares on 2 and 4, hats 8/16, the rest random; MIDI, CTR and inputs kept. Writes this track (Alt: every track)")}</div>`;
}
/* the bar again (and the rail's spec tags), without a full render */
function genDraw() {
	if (S.ws !== "seq") return;
	const host = $("#genband"); if (host) host.innerHTML = genStripHtml();
	$$(".th[data-sel]").forEach(h => { const i = +h.dataset.sel, g = h.querySelector(".gtag"); if (g) g.textContent = genTag(genSpec(i)); });
}

/* ---- mutation ---- */
/* the kit as values: 16 tracks of {m, v[24]} (the working kit document) */
function kitValues() {
	const K = kitDocOf(Docs); if (!K) return null;
	return K.tracks.map(kt => ({ m: kt.machine || "GND-EMPTY", v: [...kt.synth, ...kt.effects, ...kt.routing] }));
}
function mutGroupKnobs(t, id) {
	const [g, key] = id.split(":"), [syn, fxrt] = sndGroups(V.tracks[t]);
	return ([...syn, ...fxrt].find(x => x.g === g && x.key === key) || { knobs: [] }).knobs;
}
/* any edit but the trial's own params (and undo, redo) closes a mutation trial's gesture: the trial ends (DESIGN-generators.md §4.6) */
onEditSent((op, args) => { if (S.mut && S.mut.trial && !(op === "params" && args.g === S.mut.trial.g)) S.mut.trial = null; });
/* one apply of the trial: always from its base (R randomises the sound, it never walks away), the trial's gesture,
   so the trial is one undo step. The trial is a value: the apply's next one replaces it (nextTrial, mdDeskGen.js). */
function mutApply(all) {
	if (!S.mut.scope.size) { toast("Pick what to move: SYN, FX, RTG or a group's title."); return; }
	let tr = S.mut.trial;
	if (!tr || tr.key !== mutKey()) {
		const base = kitValues(); if (!base) { toast("The kit is not loaded yet."); return; }
		S.mut.seed = genSeed();	/* a new trial, a new seed */
		tr = S.mut.trial = mutTrial(mutKey(), Bridge.gesture(), V.kit, base);
	}
	const tracks = all ? GEN_TRACKS : [S.sel];
	const spec = { tracks, groups: [...S.mut.scope], amount: S.mut.amount, seed: S.mut.seed, protect: ["VOL"] };
	/* Again from the base: what an earlier apply moved and this one does not goes back to the base */
	const { values, trial } = nextTrial(tr, mutate(spec, tr.base, m => slots(m, Cat), mutGroupKnobs), all);
	if (!values.length) { S.mut.trial = Object.assign({}, tr, { all }); toast("Nothing to move here: no named knobs in that scope (MIDI and CTR tracks are left alone)."); return; }
	const w = [], L = { 21: "SPD", 22: "DEPTH", 23: "SHMIX" };
	for (const [t, i, v] of values) {
		const n = slots(V.tracks[t].m, Cat)[i]; if (n) w.push([["tracks", t, i < 8 ? "syn" : i < 16 ? "fx" : "rt", n], v]);
		if (L[i]) w.push([["tracks", t, "lfo", L[i]], v]);
	}
	S.mut.trial = trial;
	cmd("params", { k: V.kit, values, g: tr.g }, "mut", w);
	const moved = new Set(values.map(([t]) => t)).size;
	S.mut.note = `seed ${S.mut.seed} · ${moved} track${moved === 1 ? "" : "s"}, ${values.length} values`;
	if (S.ws === "sound") { syncControls(); redraw(); renderMutStrip(); }
}
/* amount or scope moved during a trial: heard at once, the same seed */
function mutLive() { if (S.mut.trial && S.mut.trial.key === mutKey()) mutApply(S.mut.trial.all); }
/* R on Sound: a fresh random mutation (a new seed) from the trial's base; all: the whole kit */
function mutAgain(all = false) { S.mut.seed = genSeed(); mutApply(all); }
function mutStripHtml() {
	const tr = S.mut.trial && S.mut.trial.key === mutKey() ? S.mut.trial : null, t = S.sel, all = S.alt;
	const chips = [["syn", "Syn"], ["fx", "Fx"], ["rt", "Rtg"]].map(([g, n]) => `<button data-mutg="${g}" aria-pressed="${S.mut.scope.has(g)}" title="Every named knob of the ${{ syn: "SYNTHESIS", fx: "EFFECTS", rt: "ROUTING" }[g]} page${g === "rt" ? " (VOL is kept)" : ""}">${n}</button>`).join("");
	const groups = [...S.mut.scope].filter(x => x.includes(":")).length;
	return `${gtitle("Mutate", all ? "whole kit" : `track ${t + 1} · ${codeOf(V.tracks[t].m, Cat)}`, all, "Mutate: each knob in scope is pulled toward a random target by the amount, from a seed. A trial is one undo step; Undo takes it back in one step. Alt: the whole kit.")}
  ${gbg("Move", gv("amt", "Amount", S.mut.amount + "%", "How far each knob moves toward its random target"))}
  ${gbg("Scope", `<span class="seg">${chips}</span><span class="ghint">${groups ? `+ ${groups} group${groups === 1 ? "" : "s"}` : "+ a group's title"}</span>`, "gscope", "Click a group's title below to add that group to the scope")}
  <div class="gsum" title="${S.mut.note}">${S.mut.note}</div>
  <div class="gkeys">${randKey(`${all ? "Randomise the whole kit" : `Randomise track ${t + 1}`}: a fresh random mutation, from the sound before the trial${tr ? "" : " (this sound)"}. One trial is one undo step (R; Alt+R or Alt-click: the whole kit)`)}</div>`;
}
function renderMutStrip() {
	const host = $("#mutband"); if (host) host.innerHTML = mutStripHtml();
	$$("[data-mutsg]").forEach(b => b.setAttribute("aria-pressed", S.mut.scope.has(b.dataset.mutsg)));
}
/* a group's title as a scope chip on the Sound page */
function mutTitle(x) { return ["syn", "fx", "rt"].includes(x.g) ? `<button class="mutg" data-mutsg="${x.g}:${x.key}" aria-pressed="${S.mut.scope.has(x.g + ":" + x.key)}" title="Add ${x.title} to what Mutate moves">${x.title}</button>` : x.title; }

/* the bars' clicks, values (drag, wheel, arrows) and keys */
document.addEventListener("click", e => {
	const g = e.target.closest("[data-gen]"); if (g && !g.disabled) {
		const a = g.dataset.gen;
		if (a === "fill") genDefaults();
		return;
	}
	const rk = e.target.closest("[data-rand]"); if (rk && !rk.disabled) { randomise(e.altKey || e.metaKey || e.ctrlKey); return; }
	const k = e.target.closest("[data-genkind]"); if (k) { genKind(k.dataset.genkind); return; }
	const md = e.target.closest("[data-genmode]"); if (md) { genMode(md.dataset.genmode); return; }
	const v = e.target.closest(".gv[data-gv]"); if (v && !v.dataset.dragged) { genVal(v.dataset.gv, e.shiftKey ? -1 : 1); return; }
	const c = e.target.closest("[data-mutg],[data-mutsg]"); if (c) {
		const id = c.dataset.mutg || c.dataset.mutsg, scope = new Set(S.mut.scope); scope.has(id) ? scope.delete(id) : scope.add(id); S.mut.scope = scope;
		renderMutStrip(); mutLive(); e.stopPropagation(); return;
	}
}, true);
document.addEventListener("keydown", e => {
	const v = e.target.closest?.(".gv[data-gv]"); if (!v) return; const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key]; if (d == null) return;
	e.preventDefault(); e.stopPropagation(); const k = v.dataset.gv; genVal(k, d * (e.shiftKey ? 10 : 1)); document.querySelector(`.gv[data-gv="${k}"]`)?.focus();
}, true);
const dlgClosed = () => $("#dlg").hidden;
const genRunOn = () => S.ws === "seq" && !!S.gen.run && S.gen.run.applied > 0 && S.gen.run.key === genKey();
const mutRunOn = () => S.ws === "sound" && !!S.mut.trial && S.mut.trial.applied > 0 && S.mut.trial.key === mutKey();
Keys.bind({ keys: ["R"], code: "KeyR", when: () => dlgClosed() && !LIB.open, group: "Selected track", does: "Randomise the selected track: on Sound a fresh random sound (MUTATE, from the sound before the trial); everywhere else a new GEN variation, a new seed or random hits and rotation", run: () => randomise(false) });
Keys.bind({ keys: ["R"], code: "KeyR", mod: "alt", when: () => dlgClosed() && !LIB.open, group: "All", does: "Randomise every track: on Sound the whole kit, everywhere else every track's GEN spec over the whole pattern", run: () => randomise(true) });
Keys.bind({ keys: ["GEN value"], mod: "alt", group: "All", does: "Change a GEN value: every track's spec, the whole pattern (one pattern change, one undo step per run)" });
Keys.bind({ keys: ["R key"], mod: "alt", group: "All", does: "Click: randomise every track (Sound: the whole kit; MIDI and CTR tracks are left alone, VOL is kept)" });
