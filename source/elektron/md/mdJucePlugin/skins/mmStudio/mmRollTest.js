"use strict";
/* The Monomachine Editor's note lengths (I-010; the mockup's 58-roll.js, MmRoll, from the generated page mmMockup.js):
   what a note of a length is on the machine, measured on the firmware (mmDeskFirmwareTest notelength, lenprobe): a
   synth note sounds to the track's next NOTE OFF or trig, round the pattern's end; a MIDI note LEN ticks (6 a step),
   cut there too, LEN 127 until a NOTE OFF. Checked: where a note ends, the steps a length writes (the same rows the
   firmware test stores and plays), a removed note leaving the one before it its length, the steps a selection of a
   note takes, and the words of a length.
     node mmRollTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

const page = fs.readFileSync(path.join(__dirname, "mmMockup.js"), "utf8");
const HEAD = /\n\/\* ---- (?:\d+-[\w-]+\.js|shared\/[\w-]+\.js) ---- \*\/\n/g;
const part = name => { const a = page.indexOf(`/* ---- ${name} ---- */\n`); if (a < 0) return ""; HEAD.lastIndex = a + 10; const m = HEAD.exec(page); return page.slice(a, m ? m.index : undefined); };
const src = part("58-roll.js");
check(!!src, "the page has 58-roll.js");
check(!/\bS\.|\$\(|document|trk\(|edit\(/.test(src.replace(/\/\*[\s\S]*?\*\//g, "")), "MmRoll is pure: it reads no page state and sends nothing");
const ctx = vm.createContext({});
vm.runInContext(src + "\nthis.R = MmRoll;", ctx);
const R = ctx.R;

const N = n => ({ n: [n], a: 1, f: 1, l: 1 }), OFF = { off: 1 };
const steps = (len, map) => { const a = Array(64).fill(null); for (const [s, v] of Object.entries(map)) a[+s] = v; return a; };
const apply = (a, ch) => { const b = a.slice(); for (const [k, v] of ch) b[k] = v; return b; };
const show = (a, len) => a.slice(0, len).map((x, i) => x ? i + (x.off ? ":OFF" : ":" + x.n) : "").filter(Boolean).join(" ");

/* ---- where a note ends ---- */
{
	const a = steps(16, { 0: N(48), 3: N(50), 6: OFF, 12: N(52) });
	check(R.end(a, 16, 0) === 3 && R.end(a, 16, 3) === 6, "a synth note ends at the track's next trig or NOTE OFF");
	check(R.end(a, 16, 12) === 16, "past the pattern's end it goes on to the first trig at its start (step 1 of the next pass)");
	check(R.end(steps(16, { 5: N(48) }), 16, 5) === 21, "a track's only note sounds until it comes round again");
	check(R.end(steps(16, { 12: N(48), 2: OFF }), 16, 12) === 18, "a NOTE OFF on step 3 ends a note of step 13 of 16: 6 steps");
	const m = steps(32, { 0: N(60), 2: N(62), 10: N(67), 13: OFF });
	check(R.end(m, 32, 0, { len: 8 }) === 0 + 8 / 6 && R.end(m, 32, 2, { len: 126 }) === 10, "a MIDI note: LEN ticks (8 = 1.33 steps), or less when the next trig comes first");
	check(R.end(m, 32, 10, { len: 32 }) === 13 && R.end(m, 32, 10, { len: 127 }) === 13, "a NOTE OFF cuts a MIDI note before its LEN; LEN 127 lasts to it");
	check(R.before(a, 16, 3) === 0 && R.before(a, 16, 6) === 3 && R.before(a, 16, 9) === null && R.before(a, 16, 0) === 12, "the trig that runs into a step (none after a NOTE OFF; round the end)");
	check(R.closer(a, 16, 3) === 6 && R.closer(a, 16, 0) === -1, "the NOTE OFF that ends a note");
}

/* ---- a length: the steps it writes ---- */
{
	/* the firmware test's T1 (mmDeskFirmwareTest notelength): notes on 4 7 11 17 28 of 32, 1 2 4 8 6 steps long; each drawn
	   on its own in an empty pattern, as the roll draws them one after the other */
	let a = steps(32, {});
	for (const [s, L, n] of [[4, 1, 48], [7, 2, 50], [11, 4, 52], [17, 8, 53], [28, 6, 55]]) { a[s] = N(n); const r = R.setLen(a, 32, s, L); check(r.L === L, `step ${s + 1}, ${L} step${L > 1 ? "s" : ""}: drawn at that length`); a = apply(a, r.steps); }
	check(show(a, 32) === "2:OFF 4:48 5:OFF 7:50 9:OFF 11:52 15:OFF 17:53 25:OFF 28:55", "the rows the firmware test stores and plays: NOTE OFFs on steps 3 6 10 16 26 (the last one past the end): " + show(a, 32));
	check([4, 7, 11, 17, 28].every((s, k) => R.end(a, 32, s) - s === [1, 2, 4, 8, 6][k]), "and each note ends where it was drawn");
	/* a length up to the next trig, NOTE OFFs in between gone */
	let b = steps(16, { 0: N(48), 2: OFF, 5: N(50) });
	let r = R.setLen(b, 16, 0, 9);
	check(r.L === 5 && same(r.steps, [[2, null]]), "longer than the room: up to the next trig (it ends the note), the NOTE OFF in between gone");
	r = R.setLen(b, 16, 0, 1);
	check(r.L === 1 && same(r.steps, [[2, null], [1, OFF]]), "shorter: the NOTE OFF moves to where it ends");
	r = R.setLen(b, 16, 0, 2);
	check(r.L === 2 && same(r.steps, []), "the length it has: nothing to write");
	/* 1/16 then 1/8 (I-010, tester C): no note runs into the next */
	b = steps(16, {});
	for (const [s, L] of [[0, 1], [1, 1], [2, 1], [4, 2], [6, 2], [8, 2]]) { b[s] = N(60); b = apply(b, R.setLen(b, 16, s, L).steps); }
	check(show(b, 16) === "0:60 1:60 2:60 3:OFF 4:60 6:60 8:60 10:OFF" && [0, 1, 2].every(s => R.end(b, 16, s) - s === 1) && [4, 6, 8].every(s => R.end(b, 16, s) - s === 2),
		"three 1/16 notes then three 1/8 notes: each as long as drawn (" + show(b, 16) + ")");
	/* MIDI: LEN 6 a step up to 126 (21 steps), then LEN 127 and a NOTE OFF; the kit's LEN needs no lock */
	const m = steps(64, { 0: N(60) });
	check(same(R.setLen(m, 64, 0, 2, { kitLen: 96 }), { steps: [], len: 12, L: 2 }), "MIDI, 2 steps: LEN 12 locked, no NOTE OFF");
	check(R.setLen(m, 64, 0, 16, { kitLen: 96 }).len === null, "MIDI, 16 steps on a kit LEN of 96: no lock (the kit's value)");
	check(same(R.setLen(m, 64, 0, 21, { kitLen: 96 }), { steps: [], len: 126, L: 21 }), "MIDI, 21 steps: LEN 126");
	check(same(R.setLen(m, 64, 0, 24, { kitLen: 96 }), { steps: [[24, OFF]], len: 127, L: 24 }), "MIDI, 24 steps: LEN 127 and a NOTE OFF where it ends");
	check(same(R.setLen(m, 64, 0, 24, { kitLen: 127 }), { steps: [[24, OFF]], len: null, L: 24 }), "MIDI, 24 steps on a kit LEN of 127: the NOTE OFF only");
}

/* ---- removing a note ---- */
{
	const a = steps(16, { 0: N(48), 3: N(50), 5: OFF, 9: N(52) });
	check(same(R.remove(a, 16, 3), [[3, OFF], [5, null]]), "the note before ran into it: a NOTE OFF where it began (it keeps its length), its own NOTE OFF gone");
	check(same(R.remove(a, 16, 9), [[9, null]]), "after a NOTE OFF: just gone");
	const b = steps(16, { 0: N(48), 4: N(50) });
	check(same(R.remove(b, 16, 0), [[0, OFF]]) , "the first note when the last one runs round into it: it keeps its length too");
	check(same(R.remove(steps(16, { 4: N(50) }), 16, 4), [[4, null]]), "a track's only note: gone");
	const m = steps(16, { 0: N(60), 8: N(62) });
	check(same(R.remove(m, 16, 8, s => s === 0 ? 12 : 96), [[8, null]]) && same(R.remove(m, 16, 8, s => s === 0 ? 96 : 96), [[8, OFF]]),
		"MIDI: a NOTE OFF only when the LEN of the note before went past it");
}

/* ---- a note's steps for a selection ---- */
{
	const a = steps(16, { 0: N(48), 2: OFF, 5: N(50), 13: N(52), 1: null });
	check(same(R.span(a, 16, 0), { from: 0, to: 3 }), "from its trig to its NOTE OFF, the NOTE OFF in (a copy carries where it ends)");
	check(same(R.span(a, 16, 5), { from: 5, to: 13 }), "ended by the next trig: up to it");
	check(same(R.span(a, 16, 13), { from: 13, to: 16 }), "round the end: up to the pattern's end");
	check(same(R.span(steps(16, { 0: N(60) }), 16, 0, { len: 9 }), { from: 0, to: 2 }), "MIDI: its LEN's steps");
}

/* ---- the words ---- */
check(R.say(1) === "1/16" && R.say(2) === "1/8" && R.say(16) === "1 bar" && R.say(5) === "5 steps", "lengths in a DAW's words: 1/16 1/8 … 1 bar, else steps");
check(same([...R.LENGTHS], [1, 2, 4, 8, 16]) && R.TICKS === 6 && R.LEN_MAX === 126 && R.LEN_HOLD === 127, "the Len key's lengths; 6 ticks a step, LEN 1-126, 127 holds (measured)");

console.log(failures ? `mmRollTest: ${failures} failure(s)` : "mmRollTest: PASS");
process.exit(failures ? 1 : 0);
