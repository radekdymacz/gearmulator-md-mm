"use strict";
/* Sequence: a selection of steps (DESIGN-step-selection.md), and what it does: copy, cut, paste, duplicate, clear, and
   a copy dropped where a ⌘-drag of it lets go. The selection is one value, S.stepSel = {t, n, from, to} (tracks t to
   t + n - 1, steps [from, to)), the core's argument shape; each operation is one core edit (cut: two under one g), so
   one undo step. Its pointer gestures (⌘-click, ⌘-drag, the ruler; K2 below) are mdDeskGestures.js's ("select"). */

S.stepSel = null;
S.clipBlock = null;	/* the page's copy of the block it put in the core's clipboard, to show a paste at once */

/* the block between two cells {t, s}, both in it */
function selBetween(a, b) {
	const t = Math.min(a.t, b.t), from = Math.min(a.s, b.s);
	return { t, n: Math.abs(a.t - b.t) + 1, from, to: Math.max(a.s, b.s) + 1 };
}
function inSel(t, s, x = S.stepSel) { return !!x && t >= x.t && t < x.t + x.n && s >= x.from && s < x.to; }
/* the selection within the pattern shown (its steps), or null when none of it is there */
function selShown(x = S.stepSel) { if (!x) return null; const to = Math.min(x.to, V.len); return x.from < to ? { t: x.t, n: x.n, from: x.from, to } : null; }
/* the classes of the selection (and of a drop's target, selghost) on the steps and the ruler, in place */
function syncSel(ghost) {
	$$("#seq .st").forEach(b => { const t = +b.dataset.t, s = +b.dataset.s; b.classList.toggle("selx", inSel(t, s)); b.classList.toggle("selghost", inSel(t, s, ghost)); });
	$$("#seq .rul[data-s]").forEach(r => r.classList.toggle("selx", !!S.stepSel && +r.dataset.s >= S.stepSel.from && +r.dataset.s < S.stepSel.to));
	secLabels();
}
function setSel(x) { S.stepSel = x; syncSel(); }
function clearSel() { if (!S.stepSel) return; S.stepSel = null; syncSel(); }
function selSay(x) { return `${x.n > 1 ? `tracks ${x.t + 1}–${x.t + x.n}` : `track ${x.t + 1}`}, ${x.to - x.from > 1 ? `steps ${x.from + 1}–${x.to}` : `step ${x.from + 1}`}`; }

/* ---- the view's copy of a block and the writes that show it put down (the core does the same: putSteps) ---- */
/* a lock's parameter index on its track (its name in the view, "#n" for one the machine does not name) */
function lockIndex(t, name) { const i = slots(V.tracks[t].m, Cat).indexOf(name); if (i >= 0) return i; const m = /^#(\d+)$/.exec(name); return m ? +m[1] - 1 : -1; }
function lockName(t, i) { return slots(V.tracks[t].m, Cat)[i] || "#" + (i + 1); }
function blockOf(x) {
	const rows = [];
	for (let t = x.t; t < x.t + x.n; t++) {
		const tr = V.tracks[t], row = { trigs: [], acc: [], slide: [], locks: [] };
		for (let s = x.from; s < x.to; s++) { row.trigs.push(!!tr.trigs[s]); row.acc.push(tr.acc.has(s)); row.slide.push(tr.slide.has(s)); }
		for (const [k, m] of V.locks) {
			const [kt, name] = [+k.split(":")[0], k.slice(k.indexOf(":") + 1)]; if (kt !== t) continue;
			const i = lockIndex(t, name); if (i < 0) continue;
			for (const [s, v] of m) if (s >= x.from && s < x.to && tr.trigs[s]) row.locks.push([i, s - x.from, v]);
		}
		rows.push(row);
	}
	return { length: x.to - x.from, rows };
}
/* the block that lands with its first step at `at` on track dt: where it stops (the length, track 16) */
function landing(size, dt, at) { return { t: dt, n: Math.min(size.tracks, 16 - dt), from: at, to: Math.min(at + size.length, patLenNow()) }; }
function clearWrites(x) {
	const w = [];
	for (let t = x.t; t < x.t + x.n; t++) for (let s = x.from; s < x.to; s++) {
		if (!V.tracks[t].trigs[s]) continue;
		w.push([["tracks", t, "trigs", s], false], [["tracks", t, "acc", s], false], [["tracks", t, "slide", s], false], ...clearStep(t, s));
	}
	return w;
}
function putWrites(block, dt, at) {
	const land = landing({ tracks: block.rows.length, length: block.length }, dt, at), w = clearWrites(land);
	for (let r = 0; r < land.n; r++) {
		const t = dt + r, row = block.rows[r];
		for (let s = land.from; s < land.to; s++) {
			const k = s - at; if (!row.trigs[k]) continue;
			w.push([["tracks", t, "trigs", s], true]);
			if (!V.accAll) w.push([["tracks", t, "acc", s], row.acc[k]]);
			if (!V.slideAll) w.push([["tracks", t, "slide", s], row.slide[k]]);
		}
		for (const [i, k, v] of row.locks) if (at + k < land.to) w.push([["locks", lk(t, lockName(t, i)), at + k], v]);
	}
	return w;
}

/* ---- the operations (⌘C ⌘X ⌘V ⌘D Delete, the drop): each one undo step; the result's note says what was done ---- */
function selArgs(x) { return { p: V.pat, t: x.t, n: x.n, from: x.from, to: x.to }; }
function selCopy() {
	const x = selShown(); if (!x) return false;
	S.clipBlock = blockOf(x);
	cmd("copySteps", selArgs(x));
	return true;
}
function selCut() {
	const x = selShown(); if (!x || !seqReady()) return false;
	const g = Bridge.gesture();	/* the copy changes no document: the clear is the step, under one g */
	S.clipBlock = blockOf(x);
	cmd("copySteps", Object.assign(selArgs(x), { g }));
	cmd("clearSteps", Object.assign(selArgs(x), { g }), undefined, clearWrites(x));
	refreshAfter(x);
	return true;
}
/* ⌘V at the selection: the clipboard's first step on its first step and track */
function selPaste() {
	const x = S.stepSel; if (!x || !seqReady()) return false;
	const size = V.clipboard.stepsSize;
	if (!V.clipboard.steps || !size) { toast("Copy some steps first: ⌘-click or ⌘-drag steps, then ⌘C."); return true; }
	if (x.from >= patLenNow()) { toast(`Step ${x.from + 1} is past the pattern's length (${patLenNow()}).`); return true; }
	const block = S.clipBlock && S.clipBlock.length === size.length && S.clipBlock.rows.length === size.tracks ? S.clipBlock : null;
	cmd("pasteSteps", { p: V.pat, t: x.t, from: x.from }, undefined, block ? putWrites(block, x.t, x.from) : undefined);
	const land = landing(size, x.t, x.from);
	setSel(land); refreshAfter(land);
	return true;
}
/* the block copied within the pattern: at its own end (⌘D), or where a ⌘-drag of it lets go */
function selCopyTo(dt, at) {
	const x = selShown(); if (!x || !seqReady()) return false;
	if (at >= patLenNow()) { toast(`No room: the pattern is ${patLenNow()} steps.`); return true; }
	const block = blockOf(x), land = landing({ tracks: x.n, length: x.to - x.from }, dt, at);
	cmd("copyStepsTo", Object.assign(selArgs(x), { at, dt }), undefined, putWrites(block, dt, at));
	setSel(land); refreshAfter(land);
	return true;
}
function selDuplicate() { const x = selShown(); return x ? selCopyTo(x.t, x.to) : false; }
function selClear() {
	const x = selShown(); if (!x || !seqReady()) return false;
	cmd("clearSteps", selArgs(x), undefined, clearWrites(x));
	refreshAfter(x);
	return true;
}
function refreshAfter(x) { for (let t = x.t; t < x.t + x.n; t++) refreshRow(t); syncSel(); renderTop(); renderLane(); }

/* ===== K2 (DESIGN-keymap.md, D1): the selection is ⌘'s (Ctrl off a Mac: Modifiers, shared/deskKeys.js), as on a
   desktop; ⌥ is FUNCTION, every track. Where a press starts a select gesture (mdDeskGestures.js), or null when the
   press is not one: ⌘-press a step selects it (a drag: steps × tracks), ⌘⇧ extends the selection to it; a press in
   the step ruler selects steps of the selected track, ⇧ extends; a ⌘-press inside a selection of more than one step
   drops a copy of it where it lets go. ===== */
function selStart(e, cell, ruler) {
	const cmdK = Modifiers.cmd(e);
	if (!ruler && (!cmdK || e.altKey)) return null;
	if (ruler && (e.altKey || cmdK || e.ctrlKey)) return null;
	const x = S.stepSel, at = ruler ? { t: x && e.shiftKey ? x.t : S.sel, s: cell.s } : cell;
	const drop = !ruler && !e.shiftKey && !!x && inSel(at.t, at.s) && (x.n > 1 || x.to - x.from > 1);
	return { from: at, at, ruler, extend: e.shiftKey && !!x, drop, moved: false };
}
/* the selection grown to take a cell in */
function selExtend(x, c) { const t = Math.min(x.t, c.t), last = Math.max(x.t + x.n - 1, c.t); return { t, n: last - t + 1, from: Math.min(x.from, c.s), to: Math.max(x.to, c.s + 1) }; }
/* the page of 16 that shows the selection's first step (while one page shows) */
function selToPage() { const x = S.stepSel; if (!x || S.viewAll) return; const p = Math.floor(x.from / 16); if (p !== S.page && p < pages16()) { S.page = p; render(); } }
/* ← → move the selection a step (↑ ↓ a track: mdDeskLive.js), the selected track following a one-track selection */
function selMove(dt, ds) {
	const x = S.stepSel; if (!x) return;
	const len = x.to - x.from, from = clamp(x.from + ds, 0, Math.max(0, V.len - len)), t = clamp(x.t + dt, 0, 16 - x.n);
	setSel({ t, n: x.n, from, to: from + len });
	if (x.n === 1 && t !== S.sel) select(t);
	selToPage();
}
/* ⇧← ⇧→ grow the selection a step at its start / end */
function selGrow(ds) {
	const x = S.stepSel; if (!x) return;
	setSel(ds > 0 ? { t: x.t, n: x.n, from: x.from, to: Math.min(V.len, x.to + 1) } : { t: x.t, n: x.n, from: Math.max(0, x.from - 1), to: x.to });
}
/* Enter, the step menu's Trig: the selected steps' trigs on, or off when every one has one (one steps edit) */
function selTrigs() {
	const x = selShown(); if (!x || !seqReady()) return false;
	const end = Math.min(x.to, patLenNow()); if (x.from >= end) { toast(`Step ${x.from + 1} is past the pattern's length (${patLenNow()}).`); return true; }
	let on = false;
	for (let t = x.t; t < x.t + x.n; t++) for (let s = x.from; s < end; s++) if (!V.tracks[t].trigs[s]) on = true;
	const rows = [], w = [];
	for (let t = x.t; t < x.t + x.n; t++) {
		const tr = V.tracks[t], keep = [];
		for (let s = x.from; s < end; s++) {
			if (on) keep.push(s);
			if (!!tr.trigs[s] === on) continue;
			w.push([["tracks", t, "trigs", s], on]);
			if (!on) w.push([["tracks", t, "acc", s], false], [["tracks", t, "slide", s], false], ...clearStep(t, s));
		}
		rows.push({ t, on: keep });
	}
	cmd("steps", { p: V.pat, from: x.from, to: end, rows }, undefined, w);
	refreshAfter(x);
	return true;
}
/* the step menu's Accent and Slide: on every selected step with a trig, or off when each has it (one undo step) */
function selMark(kind) {
	const x = selShown(); if (!x || !seqReady()) return false;
	const key = kind === "accent" ? "acc" : "slide", cells = [];
	for (let t = x.t; t < x.t + x.n; t++) for (let s = x.from; s < x.to; s++) if (V.tracks[t].trigs[s]) cells.push([t, s]);
	if (!cells.length) { toast(`No trig to ${kind === "accent" ? "accent" : "slide"} there: a ${kind} needs a trig.`); return true; }
	const on = cells.some(([t, s]) => !V.tracks[t][key].has(s)), g = Bridge.gesture();
	for (const [t, s] of cells) if (V.tracks[t][key].has(s) !== on) cmd(kind, { p: V.pat, t, s, on, g }, undefined, [[["tracks", t, key, s], on]]);
	refreshAfter(x);
	return true;
}

/* ===== K3: the step menu (right-click a step; Ctrl-click on a Mac; the Menu key on a focused step): every step and
   selection action with its key, so nothing is only behind a key (P5) and the fill has its home. On a selected step
   it acts on the selection, on another step it selects that step first. Items as data for DeskMenu (shared/deskMenu.js). ===== */
function keyOf(id) {
	const b = Keys.byId(id); if (!b) return "";
	return b.area ? Keys.modsLabel(b.mod) + "-click" : Keys.label(b);
}
function stepMenuItems(t, s) {
	const x = S.stepSel, one = !!x && x.n === 1 && x.to - x.from === 1, clip = !!(V.clipboard.steps && V.clipboard.stepsSize);
	return [
		{ id: "sel-trigs", label: one ? (V.tracks[t].trigs[s] ? "Trig off" : "Trig on") : "Trigs on / off", key: keyOf("sel-trigs"), run: () => selTrigs() },
		{ id: "step-accent", label: "Accent", key: keyOf("step-accent"), run: () => selMark("accent") },
		{ id: "step-slide", label: "Slide", key: keyOf("step-slide"), run: () => selMark("slide") },
		"-",
		{ id: "copy", label: "Copy", key: keyOf("copy"), run: () => selCopy() },
		{ id: "cut", label: "Cut", key: keyOf("cut"), run: () => selCut() },
		{ id: "paste", label: "Paste here", key: keyOf("paste"), enabled: clip, run: () => selPaste() },
		{ id: "duplicate", label: "Duplicate", key: keyOf("duplicate"), run: () => selDuplicate() },
		{ id: "delete", label: "Clear", key: keyOf("delete"), run: () => selClear() },
		"-",
		{ id: "step-fill-2", label: "Fill every 2nd from here", key: "", run: () => fillEvery(t, s, 2) },
		{ id: "step-fill-4", label: "Fill every 4th from here", key: "", run: () => fillEvery(t, s, 4) }
	];
}
function stepMenu(t, s, x, y) {
	if (!V.loaded) { toast("The pattern is not loaded yet."); return; }
	if (!inSel(t, s)) { setSel({ t, n: 1, from: s, to: s + 1 }); if (t !== S.sel) select(t); }
	const sel = S.stepSel, one = sel.n === 1 && sel.to - sel.from === 1;
	DeskMenu.open(stepMenuItems(t, s), x, y, { title: one ? `Track ${t + 1}, step ${s + 1}` : `Selected: ${selSay(sel)}` });
}
document.addEventListener("contextmenu", e => {
	const st = e.target.closest?.("#seq .st"); if (!st || S.ws !== "seq") return;
	e.preventDefault();
	if (V.rec) { toast("Wait until live recording stops."); return; }
	const r = st.getBoundingClientRect(), x = e.clientX || r.left + r.width / 2, y = e.clientY || r.bottom;
	stepMenu(+st.dataset.t, +st.dataset.s, x, y);
});

/* the keys of the selection: on Sequence while it has one, no dialog, and no other key has the focus (a value keeps
   its arrows, a button its Enter) */
const selKeys = () => seqKeys() && !!S.stepSel && !document.activeElement?.closest?.("button:not(.st),[role=button],[role=tab],[data-g],[data-gv],[role=slider],input,select,textarea");
Keys.bind({ id: "cut", short: "Cut", scope: "seq", keys: ["X"], mod: "cmd", group: "Sequence", does: "Cut the selected steps (copy, then clear; one undo step)", when: () => seqKeys() && !!S.stepSel, run: () => selCut() });
Keys.bind({ id: "duplicate", short: "Duplicate", scope: "seq", keys: ["D"], mod: "cmd", group: "Sequence", does: "Duplicate the selected steps right after themselves (the clipboard stays); again: once more", when: () => seqKeys() && !!S.stepSel, run: () => selDuplicate() });
Keys.bind({ id: "select-all", short: "Select all", scope: "seq", keys: ["A"], mod: "cmd", group: "Sequence", does: "Select every step of every track up to the length (then ⌘C copies the pattern as a block)", when: seqKeys,
	run: () => { setSel({ t: 0, n: 16, from: 0, to: patLenNow() }); toast(`Selected ${selSay(S.stepSel)} · ⌘C copy · Delete clear · Esc`); } });
Keys.bind({ id: "sel-move", short: "Sel ← / Sel →", scope: "seq", keys: ["ArrowLeft", "ArrowRight"], group: "Sequence", does: "Move the selection a step earlier / later (one step selected: a cursor)", when: selKeys, run: e => selMove(0, e.key === "ArrowRight" ? 1 : -1) });
Keys.bind({ id: "sel-extend", short: "Grow ← / Grow →", scope: "seq", keys: ["ArrowLeft", "ArrowRight"], mod: "shift", group: "Sequence", does: "Extend the selection a step earlier / later", when: selKeys, run: e => selGrow(e.key === "ArrowRight" ? 1 : -1) });
Keys.bind({ id: "sel-trigs", short: "Trigs", scope: "seq", keys: ["Enter"], group: "Sequence", does: "The selected steps' trigs on, or off when each has one (one undo step)", when: selKeys, run: () => selTrigs() });
Keys.bind({ id: "deselect", short: "Deselect", scope: "seq", keys: ["Escape"], group: "Sequence", does: "Clear the step selection", when: () => seqKeys() && !!S.stepSel && !S.multi.size && !genRunOn(), run: () => clearSel() });
Keys.bind({ id: "step-select", scope: "seq", area: "Steps", keys: ["step"], mod: "cmd", group: "Sequence", does: "Click: select the step (⌘V pastes there). Drag: select steps × tracks. Drag the selection: a copy where you let go" });
Keys.bind({ id: "step-extend", scope: "seq", area: "Steps", keys: ["step"], mod: "cmd+shift", group: "Sequence", does: "Click: extend the selection to the step" });
Keys.bind({ id: "ruler-select", scope: "seq", area: "Steps", keys: ["step ruler"], group: "Sequence", does: "Click or drag: select steps of the selected track (down over the grid: more tracks); ⇧-click extends" });
Keys.bind({ id: "step-menu", scope: "seq", area: "Steps", keys: ["right-click a step"], group: "Sequence", does: "The step menu: trig, accent, slide, copy, cut, paste here, duplicate, clear, fill every 2nd / 4th from it; on a selected step for the whole selection (Ctrl-click on a Mac; the Menu key on a focused step)" });

