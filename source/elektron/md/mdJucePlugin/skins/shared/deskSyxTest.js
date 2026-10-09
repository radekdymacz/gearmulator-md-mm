"use strict";
/* The SysEx import panel's pure part (Syx.model, deskSyx.js, both editors): the machine's slot grid per kind, the
   counts the tabs and the footer show, Shift-click ranges and the report per slot. The panel's markup is drawn from
   these; the journeys (md-lib-syx-import, mm-lib-syx-import) check it against the machine.
     node deskSyxTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const el = () => ({ setAttribute() { }, appendChild() { }, addEventListener() { }, classList: { toggle() { } }, querySelector: () => null, querySelectorAll: () => [] });
const ctx = vm.createContext({ document: { createElement: el, body: el(), addEventListener() { } }, console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskSyx.js"), "utf8") + "\nthis.Syx = Syx;", ctx);
const M = ctx.Syx.model;
const items = (n, f) => Array.from({ length: n }, (_, slot) => Object.assign({ slot, name: "N" + slot, overwrites: false, plays: false }, f ? f(slot) : {}));

/* the slot names, as on the machine */
check(M.slotLabel("kit", 0) === "K01" && M.slotLabel("kit", 63) === "K64", "kits K01..K64");
check(M.slotLabel("pattern", 0) === "A01" && M.slotLabel("pattern", 17) === "B02" && M.slotLabel("pattern", 127) === "H16", "patterns A01..H16");
check(M.slotLabel("song", 4) === "S05" && M.slotLabel("global", 7) === "G8", "songs S01.., globals G1..G8");

/* the grids: the machine's slots, whether the file has them or not */
{
	const g = M.layout("Machinedrum", "kit", items(3));
	check(g.cols === 8 && g.rows.length === 8 && g.rows[1].label === "09", "MD kits: 8 x 8, rows 01, 09, ..");
	check(g.rows[0].cells[2].item?.name === "N2" && g.rows[0].cells[3].item === null, "a slot not in the file is drawn empty");
	const m = M.layout("Monomachine", "kit", items(128));
	check(m.cols === 16 && m.rows.length === 8 && m.rows[7].cells[15].label === "K128", "MM kits: 8 x 16");
	const p = M.layout("Machinedrum", "pattern", items(128));
	check(p.cols === 16 && p.rows.map(r => r.label).join("") === "ABCDEFGH", "patterns: banks A-H x 16, as the pattern palette");
	check(M.layout("Machinedrum", "song", items(24)).rows.length === 4, "songs: 32 slots, 4 x 8, though the file has 24");
	const gl = M.layout("Machinedrum", "global", items(8));
	check(gl.cols === 8 && gl.rows.length === 1, "globals: 1 x 8");
	check(M.layout("Machinedrum", "kit", [{ slot: 70, name: "X" }]).rows.some(r => r.cells.some(c => c.item?.name === "X")), "a file with more slots than the machine has: more rows, nothing lost");
	const o = M.layout("Machinedrum", "other", items(3));
	check(o.cols === 0 && o.rows[0].cells.length === 3, "other messages: no slots, a list");
}

/* the counts: what a tab and the footer say */
{
	const list = items(10, s => ({ overwrites: s < 4, plays: s === 1 }));
	const skip = new Set(["kit:0", "kit:9"]);
	const s = M.kindSums("kit", list, skip, true);
	check(s.total === 10 && s.n === 8 && s.over === 3 && s.fresh === 5 && s.plays === 1 && s.out === 2, "a kind: 8 of 10 chosen, 3 overwrite, 5 new, 1 plays, 2 left out");
	const off = M.kindSums("kit", list, skip, false);
	check(off.n === 0 && off.out === 10, "a kind not ticked sends nothing");
	check(M.footLine({ n: 0 }) === "Nothing chosen.", "nothing chosen");
	check(/^3 slots on the machine overwritten, 1 playing now\. .*No undo: Export SysEx… first\.$/.test(M.footLine({ n: 8, over: 3, plays: 1 })), "overwrites: how many, what plays, no undo");
	check(/into empty slots/.test(M.footLine({ n: 5, over: 0 })), "into empty slots only");
	check(/Globals change MIDI channels/.test(M.footLine({ n: 1, over: 1, globals: true })), "B-026: globals chosen, the footer says MIDI channels change");
	check(M.OFF.global && M.OFF.other && !M.OFF.kit, "globals and other messages are off unless ticked (B-026)");
}

/* a click and a Shift-click */
{
	const list = items(16).filter(i => i.slot !== 5);
	let skip = M.toggleRange("kit", list, new Set(), 3, 3);
	check(skip.has("kit:3") && skip.size === 1, "a click leaves one out");
	skip = M.toggleRange("kit", list, skip, 3, 3);
	check(skip.size === 0, "a second click takes it again");
	skip = M.toggleRange("kit", list, new Set(), 2, 7);
	check([2, 3, 4, 6, 7].every(s => skip.has("kit:" + s)) && skip.size === 5, "Shift-click: the range out, the slot not in the file not counted");
	skip = M.toggleRange("kit", list, skip, 9, 4);
	check(!skip.has("kit:4") && !skip.has("kit:7") && skip.has("kit:2") && skip.has("kit:3"), "Shift-click backwards on a left-out slot: the range taken again");
}

/* the report: every slot's outcome */
{
	const picked = [{ kind: "kit", slot: 0 }, { kind: "kit", slot: 1 }, { kind: "pattern", slot: 4 }, { kind: "other", slot: 0 }];
	const r = M.outcomes(picked, { taken: 2, items: [{ kind: "kit", slot: 1, outcome: "ignored", text: "kit 2: ignored" }] });
	check(r.get("kit:0").outcome === "taken" && r.get("kit:1").outcome === "ignored" && r.get("kit:1").text === "kit 2: ignored", "listed: its outcome and text; not listed: taken");
	check(r.get("pattern:4").outcome === "taken" && r.get("other:0").outcome === "sent", "other messages: sent");
	const stopped = M.outcomes(picked, { taken: 1, unsent: 1, items: [{ kind: "kit", slot: 1, outcome: "ignored", text: "" }] });
	check(stopped.get("kit:0").outcome === "unclear", "stopped (the counts do not add up): not listed is not known, never claimed taken");
}

console.log(failures ? `${failures} FAILED` : "all ok");
process.exit(failures ? 1 : 0);
