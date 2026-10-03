"use strict";
/* The drag across the M and S keys (deskTogglePaint.js, both editors): the first key toggles and sets the paint,
   every key crossed becomes that once, keys already in it and keys of another group are left alone.
     node deskTogglePaintTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const ctx = vm.createContext({ console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskTogglePaint.js"), "utf8") + "\nthis.T = { TogglePaint };", ctx);
const { TogglePaint } = ctx.T;
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const states = bits => Object.fromEntries([...bits].map((c, i) => [i, c === "x"]));
const drag = (bits, keys) => TogglePaint.changes(states(bits), "mute", keys).map(([k, v]) => [+k, v]);

/* ---- a click ---- */
check(same(drag("....", [2]), [[2, true]]), "a click on an unmuted key mutes it");
check(same(drag("..x.", [2]), [[2, false]]), "a click on a muted key unmutes it");
check(same(drag("....", []), []), "no key: nothing changes");

/* ---- a drag ---- */
check(same(drag("................", [0, 1, 2, 3, 4, 5]), [[0, true], [1, true], [2, true], [3, true], [4, true], [5, true]]),
	"a drag from an unmuted key mutes every key it crosses");
check(same(drag("x.x.x.x.", [0, 1, 2, 3, 4, 5, 6, 7]), [[0, false], [2, false], [4, false], [6, false]]),
	"a drag from a muted key unmutes; keys already unmuted send nothing");
check(same(drag(".x.x....", [0, 1, 2, 3]), [[0, true], [2, true]]), "keys already in the paint's state are left alone");
check(same(drag("........", [3, 4, 5, 4, 3, 2]), [[3, true], [4, true], [5, true], [2, true]]),
	"a key crossed twice (dragging back) is not toggled back, the first one neither");
check(same(drag("........", [3, 3, 3]), [[3, true]]), "the pressed key repeated (pointer moves on it) changes once");
check(same(drag("xxxxxxxxxxxxxxxx", [15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0]).length, 16), "sixteen keys upwards, each once");

/* ---- begin / visit as the pages use them ---- */
{
	const b = TogglePaint.begin("mute", 4, false);
	check(b.change === true && b.paint.value === true && b.paint.seen.has(4) && b.paint.group === "mute", "begin: the pressed key's new state is the paint");
	const o = TogglePaint.visit(b.paint, "solo", 5, false);
	check(o.change === null && o.paint === b.paint, "a key of another group (an S key in a mute drag) is left alone");
	const v = TogglePaint.visit(b.paint, "mute", 5, false);
	check(v.change === true && v.paint !== b.paint && !b.paint.seen.has(5) && v.paint.seen.has(5), "visit: a new paint value, the old one unchanged");
	check(TogglePaint.visit(v.paint, "mute", 5, false).change === null, "the same key again: nothing");
	check(TogglePaint.visit(null, "mute", 1, false).change === null, "no drag: nothing");
	check(TogglePaint.visit(TogglePaint.begin("mute:synth", 0, false).paint, "mute:midi", 6, false).change === null,
		"MM: a mute drag on the synth side does not reach the MIDI side's keys");
}

/* ---- the points under a fast drag ---- */
{
	const P = TogglePaint.points;
	check(same(P(null, { x: 5, y: 7 }), [{ x: 5, y: 7 }]), "points: the first event is its own point");
	check(same(P({ x: 0, y: 0 }, { x: 0, y: 2 }), [{ x: 0, y: 2 }]), "points: a short move is its end");
	const far = P({ x: 10, y: 0 }, { x: 10, y: 400 }, 4);
	check(far.length === 100 && far.every((p, k) => k === 0 || p.y - far[k - 1].y <= 4) && same(far[99], { x: 10, y: 400 }) && far[0].y > 0,
		"points: a 400 px jump (sixteen 25 px keys) is looked at every 4 px, so no key is skipped; the start is not repeated");
}

console.log(failures ? `deskTogglePaintTest: ${failures} failure(s)` : "deskTogglePaintTest: PASS");
process.exit(failures ? 1 : 0);
