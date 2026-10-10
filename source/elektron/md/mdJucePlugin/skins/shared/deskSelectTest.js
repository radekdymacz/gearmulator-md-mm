"use strict";
/* Selected steps, both editors' (deskSelect.js, DESIGN-step-selection.md): the selection as a value, what moves it,
   the select gesture's decisions, the keys it binds (the same on both editors), the step menu's list.
     node deskSelectTest.js */
const StepSel = require("./deskSelect.js");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const J = JSON.stringify;

/* ---- the value ---- */
check(J(StepSel.between({ t: 3, s: 9 }, { t: 1, s: 4 })) === '{"t":1,"n":3,"from":4,"to":10}', "between: the block of two cells, both in it, whichever comes first");
const x = { t: 1, n: 2, from: 4, to: 8 };
check(StepSel.inside(x, 2, 7) && !StepSel.inside(x, 3, 7) && !StepSel.inside(x, 1, 8) && !StepSel.inside(null, 1, 4), "inside: tracks t..t+n-1, steps [from, to)");
check(J(StepSel.shown(x, 6)) === '{"t":1,"n":2,"from":4,"to":6}' && StepSel.shown(x, 4) === null, "shown: cut at the length; none of it there: null");
check(J(StepSel.extend(x, { t: 5, s: 1 })) === '{"t":1,"n":5,"from":1,"to":8}', "extend: grown to take the cell in");
check(J(StepSel.landing({ tracks: 3, length: 8 }, 10, 12, 12, 16)) === '{"t":10,"n":2,"from":12,"to":16}', "landing: a paste stops at the last track and at the length");
check(J(StepSel.moved(x, 0, 1, 16, 12)) === '{"t":1,"n":2,"from":5,"to":9}' && J(StepSel.moved(x, 0, 20, 16, 12)) === '{"t":1,"n":2,"from":12,"to":16}', "moved: a step later, kept inside the length");
check(J(StepSel.moved(x, 20, 0, 16, 12)) === '{"t":10,"n":2,"from":4,"to":8}' && J(StepSel.moved(x, -5, -9, 16, 12)) === '{"t":0,"n":2,"from":0,"to":4}', "moved: a track, kept inside the tracks and step 1");
check(J(StepSel.grown(x, 1, 8)) === J(x) && J(StepSel.grown(x, -1, 8)) === '{"t":1,"n":2,"from":3,"to":8}', "grown: a step at its end (not past the length) or its start");
const md = (t, n) => n > 1 ? `tracks ${t + 1}–${t + n}` : `track ${t + 1}`;
check(StepSel.say(x, md) === "tracks 2–3, steps 5–8" && StepSel.say({ t: 0, n: 1, from: 2, to: 3 }, md) === "track 1, step 3", "say: in the machine's words");
check(StepSel.menuTitle({ t: 0, n: 1, from: 2, to: 3 }, 0, 2, md) === "Track 1, step 3" && StepSel.menuTitle(x, 1, 4, md) === "Selected: tracks 2–3, steps 5–8", "the step menu's title: the step, or the selection");

/* ---- the gesture ---- */
const m = o => Object.assign({ cmd: false, shift: false, alt: false, ctrl: false }, o);
check(!!StepSel.start(m({ cmd: true }), { t: 0, s: 1 }, false, null, 0), "⌘-press on a step starts a selection");
check(!StepSel.start(m({}), { t: 0, s: 1 }, false, null, 0) && !StepSel.start(m({ cmd: true, alt: true }), { t: 0, s: 1 }, false, null, 0), "a plain or ⌘⌥ press on a step is not one");
check(!!StepSel.start(m({}), { s: 3 }, true, null, 2) && !StepSel.start(m({ cmd: true }), { s: 3 }, true, null, 2) && !StepSel.start(m({ ctrl: true }), { s: 3 }, true, null, 2), "the ruler: a plain press selects; ⌘ or Ctrl does not");
check(StepSel.start(m({}), { s: 3 }, true, null, 2).from.t === 2 && StepSel.start(m({ shift: true }), { s: 3 }, true, x, 2).from.t === 1, "the ruler: the selected track, or the selection's with ⇧");
const drop = StepSel.start(m({ cmd: true }), { t: 2, s: 5 }, false, x, 0);
check(drop.drop && !StepSel.start(m({ cmd: true, shift: true }), { t: 2, s: 5 }, false, x, 0).drop && !StepSel.start(m({ cmd: true }), { t: 0, s: 0 }, false, { t: 0, n: 1, from: 0, to: 1 }, 0).drop,
	"⌘-press inside a selection of more than one step is a drop (not with ⇧, not on one step)");
const d1 = Object.assign({}, drop, { at: { t: 3, s: 9 }, moved: true });
check(J(StepSel.end(d1, x, 16, 12)) === '{"drop":{"t":2,"n":2,"from":8,"to":12}}' && J(StepSel.during(d1, x, 16, 12)) === '{"ghost":{"t":2,"n":2,"from":8,"to":12}}', "a drop lands moved by the drag");
check(J(StepSel.dropAt(x, Object.assign({}, drop, { at: { t: 11, s: 15 } }), 16, 12)) === '{"t":10,"n":2,"from":14,"to":16}', "a drop stays inside the tracks and the length");
const click = StepSel.start(m({ cmd: true }), { t: 0, s: 3 }, false, null, 0);
check(J(StepSel.end(click, null, 16, 12)) === '{"sel":{"t":0,"n":1,"from":3,"to":4}}', "a click: that one step");
check(J(StepSel.end(Object.assign({}, click, { at: { t: 2, s: 6 }, moved: true }), null, 16, 12)) === '{"sel":{"t":0,"n":3,"from":3,"to":7}}', "a drag: the block between press and release");
const ext = StepSel.start(m({ cmd: true, shift: true }), { t: 4, s: 10 }, false, x, 0);
check(J(StepSel.end(ext, x, 16, 12)) === '{"sel":{"t":1,"n":4,"from":4,"to":11}}', "⌘⇧-click: the selection extended to the step");

/* ---- the keys: one set of ids, keys and modifiers for both editors ---- */
const bound = [];
const Keys = { bind: b => bound.push(b), byId: id => bound.find(b => b.id === id), modsLabel: mod => mod.split("+").map(k => ({ cmd: "⌘", shift: "⇧", alt: "⌥" }[k])).join(""), label: b => (b.mod ? b.mod + "+" : "") + b.keys.join("/") };
const ran = [];
const page = { seqKeys: () => true, has: () => true, selKeys: () => true, deselectWhen: () => true, area: "Roll", does: { "select-all": "the side shown" } };
for (const k of ["cut", "duplicate", "selectAll", "trigs", "deselect"]) page[k] = () => ran.push(k);
page.move = ds => ran.push("move" + ds); page.grow = ds => ran.push("grow" + ds);
StepSel.bind(Keys, page);
const ids = bound.map(b => b.id);
check(J(ids) === J(["cut", "duplicate", "select-all", "sel-move", "sel-extend", "sel-trigs", "deselect", "step-select", "step-extend", "ruler-select", "step-menu"]), "bind: the selection's entries: " + ids.join(" "));
check(bound.every(b => b.scope === "seq" && b.group === "Sequence") && bound.filter(b => b.area).every(b => b.area === "Roll" && !b.run), "bind: on Sequence; the pointer gestures described in the page's area, never dispatched");
check(Keys.byId("select-all").does === "the side shown" && /Cut the selected/.test(Keys.byId("cut").does), "bind: a page says an entry its own way");
for (const b of bound.filter(b => b.run)) b.run({ key: "ArrowRight" });
check(J(ran) === J(["cut", "duplicate", "selectAll", "move1", "grow1", "trigs", "deselect"]), "bind: each key runs the page's operation: " + ran.join(" "));
check(StepSel.keyOf(Keys, "step-extend") === "⌘⇧-click" && StepSel.keyOf(Keys, "cut") === "cmd+X" && StepSel.keyOf(Keys, "none") === "", "keyOf: a pointer gesture's words, a key's label");

/* ---- the step menu ---- */
const picked = [];
const items = StepSel.menuItems({ marks: [{ id: "mark", label: "Mark", run: () => picked.push("mark") }], copy: () => picked.push("copy"), cut: () => picked.push("cut"), paste: () => picked.push("paste"), canPaste: false,
	duplicate: () => picked.push("duplicate"), clear: () => picked.push("clear"), fill: n => picked.push("fill" + n), key: id => "<" + id + ">" });
check(J(items.map(i => typeof i === "string" ? i : i.id)) === J(["mark", "-", "copy", "cut", "paste", "duplicate", "delete", "-", "step-fill-2", "step-fill-4"]), "the step menu: the machine's marks, the operations, the fills");
check(items.find(i => i.id === "paste").enabled === false && items.find(i => i.id === "cut").key === "<cut>", "Paste here only with something copied; each item shows its key");
items.filter(i => typeof i !== "string").forEach(i => i.run());
check(J(picked) === J(["mark", "copy", "cut", "paste", "duplicate", "clear", "fill2", "fill4"]), "each item runs the page's operation");

console.log("deskSelectTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
process.exit(failures ? 1 : 0);
