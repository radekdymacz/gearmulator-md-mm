"use strict";
/* Sequence: a selection of steps (DESIGN-step-selection.md), and what it does: copy, cut, paste, duplicate, clear, and
   a copy dropped where an ⌥-drag of it lets go. The selection is one value, S.stepSel = {t, n, from, to} (tracks t to
   t + n - 1, steps [from, to)), the core's argument shape; each operation is one core edit (cut: two under one g), so
   one undo step. Its pointer gestures (⌥-click, ⌥-drag, the ruler) are mdDeskGestures.js's ("select"). */

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
	if (!V.clipboard.steps || !size) { toast("Copy some steps first: ⌥-click or ⌥-drag steps, then ⌘C."); return true; }
	if (x.from >= patLenNow()) { toast(`Step ${x.from + 1} is past the pattern's length (${patLenNow()}).`); return true; }
	const block = S.clipBlock && S.clipBlock.length === size.length && S.clipBlock.rows.length === size.tracks ? S.clipBlock : null;
	cmd("pasteSteps", { p: V.pat, t: x.t, from: x.from }, undefined, block ? putWrites(block, x.t, x.from) : undefined);
	const land = landing(size, x.t, x.from);
	setSel(land); refreshAfter(land);
	return true;
}
/* the block copied within the pattern: at its own end (⌘D), or where an ⌥-drag of it lets go */
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

Keys.bind({ keys: ["X"], mod: "cmd", group: "Sequence", does: "Cut the selected steps (copy, then clear; one undo step)", when: () => seqKeys() && !!S.stepSel, run: () => selCut() });
Keys.bind({ keys: ["D"], mod: "cmd", group: "Sequence", does: "Duplicate the selected steps right after themselves (the clipboard stays); again: once more", when: () => seqKeys() && !!S.stepSel, run: () => selDuplicate() });
Keys.bind({ keys: ["Escape"], group: "Sequence", does: "Clear the step selection", when: () => seqKeys() && !!S.stepSel && !S.multi.size && !genRunOn(), run: () => clearSel() });
Keys.bind({ keys: ["step"], mod: "alt", group: "Sequence", does: "Click: select the step (⌘V pastes there). Drag: select steps × tracks. Drag the selection: a copy where you let go" });
Keys.bind({ keys: ["step ruler"], group: "Sequence", does: "Click or drag: select steps of the selected track (down over the grid: more tracks); ⇧-click extends" });
