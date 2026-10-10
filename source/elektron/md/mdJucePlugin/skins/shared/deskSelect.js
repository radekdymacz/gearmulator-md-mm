"use strict";
/* Selected steps, one for both editors (DESIGN-step-selection.md; DESIGN-keymap.md K2, K3, K7): the selection as a
   value and what moves it, the select gesture's decisions, the selection's keys and the step menu's list. A
   selection is {t, n, from, to}: tracks t to t + n - 1, steps [from, to), the core's argument shape (copySteps,
   clearSteps, copyStepsTo; deskCore/deskBlocks.h); a cell is {t, s}. Pure but for bind() (the page's Keys) and
   keyOf() (the words of an entry's key). Each page keeps its own drawing, its own operations (they send its own
   commands) and its own pointer wiring: the Machinedrum's grid (mdDeskSelect.js, mdDeskGestures.js), the
   Monomachine's piano roll and trig rows (the mockup's 72-select.js). `tracks` is the machine's track count (16 or
   12), `len` the pattern's length. */
const StepSel = (() => {
	const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
	/* the block between two cells, both in it */
	function between(a, b) {
		const t = Math.min(a.t, b.t), from = Math.min(a.s, b.s);
		return { t, n: Math.abs(a.t - b.t) + 1, from, to: Math.max(a.s, b.s) + 1 };
	}
	const inside = (x, t, s) => !!x && t >= x.t && t < x.t + x.n && s >= x.from && s < x.to;
	/* the selection within `len` steps, or null when none of it is there */
	function shown(x, len) { if (!x) return null; const to = Math.min(x.to, len); return x.from < to ? { t: x.t, n: x.n, from: x.from, to } : null; }
	/* the selection grown to take a cell in */
	function extend(x, c) { const t = Math.min(x.t, c.t), last = Math.max(x.t + x.n - 1, c.t); return { t, n: last - t + 1, from: Math.min(x.from, c.s), to: Math.max(x.to, c.s + 1) }; }
	/* the block a paste of `size` ({tracks, length}) puts down with its first step at `at` on track dt: where it stops */
	const landing = (size, dt, at, tracks, len) => ({ t: dt, n: Math.min(size.tracks, tracks - dt), from: at, to: Math.min(at + size.length, len) });
	/* ← → (a step) and ↑ ↓ (a track): the selection moved, kept inside the pattern */
	function moved(x, dt, ds, len, tracks) {
		const n = x.to - x.from, from = clamp(x.from + ds, 0, Math.max(0, len - n)), t = clamp(x.t + dt, 0, tracks - x.n);
		return { t, n: x.n, from, to: from + n };
	}
	/* ⇧← ⇧→: the selection a step longer at its start or its end */
	const grown = (x, ds, len) => ds > 0 ? { t: x.t, n: x.n, from: x.from, to: Math.min(len, x.to + 1) } : { t: x.t, n: x.n, from: Math.max(0, x.from - 1), to: x.to };
	/* "track 3, steps 5–8" in the machine's words: name(t, n) names tracks t to t + n - 1 */
	const say = (x, name) => `${name(x.t, x.n)}, ${x.to - x.from > 1 ? `steps ${x.from + 1}–${x.to}` : `step ${x.from + 1}`}`;
	const one = x => !!x && x.n === 1 && x.to - x.from === 1;

	/* ---- the select gesture: a press decides whether it is one (start), each cell the pointer crosses moves it (at),
	   the release decides what it made (end). m: the press's modifiers {cmd (⌘, Ctrl off a Mac: Modifiers.cmd), shift,
	   alt, ctrl}; on a step only ⌘ selects (⌘⇧ extends); in the step ruler a press without ⌘, ⌥ or Ctrl selects steps
	   of the selected track (⇧ extends); a ⌘-press inside a selection of more than one step drops a copy of it where it
	   lets go. x: the selection now, selTrack the selected track. ---- */
	function start(m, cell, ruler, x, selTrack) {
		if (!ruler && (!m.cmd || m.alt)) return null;
		if (ruler && (m.alt || m.cmd || m.ctrl)) return null;
		const at = ruler ? { t: x && m.shift ? x.t : selTrack, s: cell.s } : cell;
		const drop = !ruler && !m.shift && !!x && inside(x, at.t, at.s) && (x.n > 1 || x.to - x.from > 1);
		return { from: at, at, ruler, extend: !!m.shift && !!x, drop, moved: false };
	}
	/* where a dragged selection lands: moved by the drag, within the tracks and the steps */
	function dropAt(x, d, len, tracks) {
		const t = clamp(x.t + d.at.t - d.from.t, 0, tracks - x.n), from = clamp(x.from + d.at.s - d.from.s, 0, len - 1);
		return { t, n: x.n, from, to: Math.min(from + x.to - x.from, len) };
	}
	/* what the pointer shows while it moves: the drop's target (ghost) or the selection so far (sel) */
	const during = (d, x, len, tracks) => d.drop ? { ghost: dropAt(x, d, len, tracks) } : { sel: between(d.from, d.at) };
	/* what the release made: a copy dropped ({drop: target}) or a selection ({sel}) */
	function end(d, x, len, tracks) {
		if (d.drop && d.moved) return { drop: dropAt(x, d, len, tracks) };
		return { sel: d.extend && !d.moved && x ? extend(x, d.at) : between(d.from, d.at) };
	}

	/* ---- the selection's keys (the same ids, keys and modifiers on both editors: deskKeymapTest.js). p: the page's
	   {seqKeys(): its Sequence keys are on, has(): a selection, selKeys(): its keys may take the arrows and Enter (a
	   selection, no other control focused), cut, duplicate, selectAll, move(ds), grow(ds), trigs, deselect,
	   deselectWhen(), does: {id: words} for what the page says its own way, area: the pointer's area name} ---- */
	function bind(Keys, p) {
		const does = (id, words) => (p.does && p.does[id]) || words;
		const area = p.area || "Steps";
		Keys.bind({ id: "cut", short: "Cut", scope: "seq", keys: ["X"], mod: "cmd", group: "Sequence", does: does("cut", "Cut the selected steps (copy, then clear; one undo step)"), when: () => p.seqKeys() && p.has(), run: () => p.cut() });
		Keys.bind({ id: "duplicate", short: "Duplicate", scope: "seq", keys: ["D"], mod: "cmd", group: "Sequence", does: does("duplicate", "Duplicate the selected steps right after themselves (the clipboard stays); again: once more"), when: () => p.seqKeys() && p.has(), run: () => p.duplicate() });
		Keys.bind({ id: "select-all", short: "Select all", scope: "seq", keys: ["A"], mod: "cmd", group: "Sequence", does: does("select-all", "Select every step of every track up to the length (then ⌘C copies the pattern as a block)"), when: () => p.seqKeys(), run: () => p.selectAll() });
		Keys.bind({ id: "sel-move", short: "Sel ← / Sel →", scope: "seq", keys: ["ArrowLeft", "ArrowRight"], group: "Sequence", does: does("sel-move", "Move the selection a step earlier / later (one step selected: a cursor)"), when: () => p.selKeys(), run: e => p.move(e.key === "ArrowRight" ? 1 : -1) });
		Keys.bind({ id: "sel-extend", short: "Grow ← / Grow →", scope: "seq", keys: ["ArrowLeft", "ArrowRight"], mod: "shift", group: "Sequence", does: does("sel-extend", "Extend the selection a step earlier / later"), when: () => p.selKeys(), run: e => p.grow(e.key === "ArrowRight" ? 1 : -1) });
		Keys.bind({ id: "sel-trigs", short: "Trigs", scope: "seq", keys: ["Enter"], group: "Sequence", does: does("sel-trigs", "The selected steps' trigs on, or off when each has one (one undo step)"), when: () => p.selKeys(), run: () => p.trigs() });
		Keys.bind({ id: "deselect", short: "Deselect", scope: "seq", keys: ["Escape"], group: "Sequence", does: does("deselect", "Clear the step selection"), when: () => p.seqKeys() && p.has() && p.deselectWhen(), run: () => p.deselect() });
		Keys.bind({ id: "step-select", scope: "seq", area, keys: ["step"], mod: "cmd", group: "Sequence", does: does("step-select", "Click: select the step (⌘V pastes there). Drag: select steps × tracks. Drag the selection: a copy where you let go") });
		Keys.bind({ id: "step-extend", scope: "seq", area, keys: ["step"], mod: "cmd+shift", group: "Sequence", does: does("step-extend", "Click: extend the selection to the step") });
		Keys.bind({ id: "ruler-select", scope: "seq", area, keys: ["step ruler"], group: "Sequence", does: does("ruler-select", "Click or drag: select steps of the selected track (down over the grid: more tracks); ⇧-click extends") });
		Keys.bind({ id: "step-menu", scope: "seq", area, keys: ["right-click a step"], group: "Sequence", does: does("step-menu", "The step menu: trig, accent, slide, copy, cut, paste here, duplicate, clear, fill every 2nd / 4th from it; on a selected step for the whole selection (Ctrl-click on a Mac; the Menu key on a focused step)") });
	}

	/* the words of an entry's key (a pointer gesture's: "⌘⇧-click") */
	function keyOf(Keys, id) {
		const b = Keys.byId(id); if (!b) return "";
		return b.area ? Keys.modsLabel(b.mod) + "-click" : Keys.label(b);
	}
	/* ---- the step menu (K3): the machine's marks first (its trig, accent, slide; the MM's note off, trigless, chord),
	   then the selection's operations with their keys, then the fills, for DeskMenu (deskMenu.js). p: {marks: items,
	   copy, cut, paste, canPaste, duplicate, clear, fill(n), key(id)} ---- */
	function menuItems(p) {
		return [...p.marks, "-",
			{ id: "copy", label: "Copy", key: p.key("copy"), run: p.copy },
			{ id: "cut", label: "Cut", key: p.key("cut"), run: p.cut },
			{ id: "paste", label: "Paste here", key: p.key("paste"), enabled: !!p.canPaste, run: p.paste },
			{ id: "duplicate", label: "Duplicate", key: p.key("duplicate"), run: p.duplicate },
			{ id: "delete", label: "Clear", key: p.key("delete"), run: p.clear },
			"-",
			{ id: "step-fill-2", label: "Fill every 2nd from here", key: "", run: () => p.fill(2) },
			{ id: "step-fill-4", label: "Fill every 4th from here", key: "", run: () => p.fill(4) }];
	}
	/* the menu's title: the step, or the selection */
	const menuTitle = (x, t, s, name) => one(x) ? `${name(t, 1).replace(/^./, c => c.toUpperCase())}, step ${s + 1}` : `Selected: ${say(x, name)}`;

	return { between, inside, shown, extend, landing, moved, grown, say, one, start, dropAt, during, end, bind, keyOf, menuItems, menuTitle };
})();
if (typeof module !== "undefined") module.exports = StepSel;
