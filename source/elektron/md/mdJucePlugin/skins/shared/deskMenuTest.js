"use strict";
/* The menu drawn in the page (deskMenu.js, both editors; I-008: the editor's menu too). Pure parts: where a panel
   goes (at a point, beside an entry, kept inside the window), the keys that move between entries, and the plug-in's
   editorMenu message as items, whose entries send their number back (menuPick). Then the wiring: both pages ask for
   the menu and draw the answer, the MM page loads the file, the window answers and runs the pick, the entries the
   journeys find are the plug-in's.
     node deskMenuTest.js */
const fs = require("fs"), path = require("path");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const HERE = __dirname, read = f => fs.readFileSync(path.join(HERE, f), "utf8");
const { DeskMenu } = require("./deskMenu.js");

/* placement: at a point, below and right of it where it fits */
let p = DeskMenu.place(240, 300, 1440, 900, { x: 100, y: 50 });
check(p.left === 100 && p.top === 50, `at the point where it fits (${p.left}, ${p.top})`);
p = DeskMenu.place(240, 300, 1440, 900, { x: 1400, y: 800 });
check(p.left === 1160 && p.top === 500, `near the bottom right corner: left of and above the point (${p.left}, ${p.top})`);
p = DeskMenu.place(240, 1000, 1440, 900, { x: 10, y: 10 });
check(p.top === 4, "taller than the window: from its top edge (the panel scrolls)");
p = DeskMenu.place(260, 200, 1440, 900, { left: 300, right: 520, top: 120 });
check(p.left === 518 && p.top === 115, `a submenu beside its entry, on the right (${p.left}, ${p.top})`);
p = DeskMenu.place(260, 200, 1440, 900, { left: 1100, right: 1320, top: 120 });
check(p.left === 842, `no room on the right: on the left of its entry (${p.left})`);
p = DeskMenu.place(260, 400, 1440, 900, { left: 300, right: 520, top: 800 });
check(p.top === 496, `low in the window: moved up to fit (${p.top})`);

/* keys: down, up (wrapping), Home, End; any other key moves nothing */
check(DeskMenu.step(5, 0, "ArrowDown") === 1 && DeskMenu.step(5, 4, "ArrowDown") === 0, "↓ goes to the next entry, from the last to the first");
check(DeskMenu.step(5, 0, "ArrowUp") === 4 && DeskMenu.step(5, -1, "ArrowUp") === 4, "↑ from the first (or none) to the last");
check(DeskMenu.step(5, 2, "Home") === 0 && DeskMenu.step(5, 2, "End") === 4, "Home and End");
check(DeskMenu.step(5, 2, "x") === -1 && DeskMenu.step(0, 0, "ArrowDown") === -1, "another key, or no entries: nothing");

/* the plug-in's message (mdEditorMenu.h toJson) as items */
const message = { type: "editorMenu", menu: 7, title: "Machinedrum Editor 0.3.5", items: [
	{ kind: "submenu", id: "zoom", label: "Zoom", key: "100 %", enabled: true, items: [
		{ kind: "action", id: "zoom-in", label: "Zoom In", key: "⌘+", enabled: true, n: 0 },
		{ kind: "action", id: "zoom-out", label: "Zoom Out", key: "⌘−", enabled: false },
		{ kind: "separator" },
		{ kind: "action", id: "zoom-100", label: "100 %", enabled: true, checked: true, n: 1 },
		{ kind: "submenu", id: "window-size", label: "Window Size", enabled: true, items: [{ kind: "action", id: "window-100", label: "100 %", enabled: true, n: 2 }] }] },
	{ kind: "separator" },
	{ kind: "action", id: "open-log-folder", label: "Open Log Folder", enabled: true, n: 3 },
	{ kind: "submenu", id: "developer", label: "Developer", enabled: true, items: [
		{ kind: "heading", label: "RAM recording" }, { kind: "note", label: "Idle" }] }] };
const sent = [];
const items = DeskMenu.fromEditor(message.items, n => sent.push(n));
check(items.length === 4 && items[1] === "-", "the entries in order, a separator as \"-\"");
const zoom = items[0];
check(zoom.id === "zoom" && zoom.key === "100 %" && zoom.items.length === 5 && zoom.enabled, "Zoom: a submenu with its five entries and its value at the right");
check(zoom.items[0].run && zoom.items[0].key === "⌘+" && zoom.items[0].checked === false, "Zoom In runs, with its key, unticked");
check(zoom.items[1].enabled === false && !zoom.items[1].run, "Zoom Out disabled: nothing to run");
check(zoom.items[3].checked === true, "the zoom shown is ticked");
check(zoom.items[4].items[0].id === "window-100", "a submenu in a submenu");
check(items[3].items[0].heading === "RAM recording" && items[3].items[1].note === "Idle", "a heading and a note");
zoom.items[0].run(); zoom.items[4].items[0].run(); items[2].run();
check(sent.join(",") === "0,2,3", `each entry sends its own number (${sent.join(",")})`);
check(DeskMenu.fromEditor(undefined, () => {}).length === 0, "no entries: an empty menu");

/* showEditor: the title, and the pick as the window reads it (menuPick with the menu's number) */
const opened = [];
const saved = DeskMenu.open;
DeskMenu.open = (list, x, y, o) => opened.push({ list, x, y, o });
const picks = [];
DeskMenu.showEditor(message, 40, 30, m => picks.push(m));
check(opened.length === 1 && opened[0].o.title === "Machinedrum Editor 0.3.5" && opened[0].x === 40 && opened[0].y === 30, "drawn at the point, the editor and its version as its title");
opened[0].list[2].run();
check(picks.length === 1 && picks[0].op === "menuPick" && picks[0].menu === 7 && picks[0].n === 3, "a choice sends menuPick {menu, n}");
DeskMenu.open = saved;

/* the wiring: both pages ask for the menu and draw the answer; the MM page has the file; the window answers it */
const R = path.join(HERE, "../../../../../..");
const live = read("../mdStudio/mdDeskLive.js"), adapter = read("../mmStudio/mmAdapter.js");
check(/op: "openMenu"/.test(live) && /m\.type === "editorMenu"\) DeskMenu\.showEditor/.test(live), "the Machinedrum page asks for the menu and draws the answer");
check(/send\(\{ op: "openMenu" \}\)/.test(adapter) && /m\.type === "editorMenu"\) DeskMenu\.showEditor/.test(adapter), "the Monomachine page too");
check(/\$SHARED\/deskMenu\.js/.test(fs.readFileSync(path.join(R, "doc/modern-ux/mm-mockup/build.sh"), "utf8")), "the Monomachine page loads deskMenu.js (build.sh)");
check(/<script src="deskMenu\.js"><\/script>/.test(read("../mdStudio/mdStudio.html")), "the Machinedrum page loads deskMenu.js");
check(/\["#deskmenu", "menu", "closeDeskMenu"\]/.test(read("deskModal.js")), "the modal layer knows it (Esc, a click outside)");
const editor = fs.readFileSync(path.join(HERE, "../../mdPageEditor.cpp"), "utf8"), state = fs.readFileSync(path.join(HERE, "../../mdPluginEditorState.cpp"), "utf8");
check(/Action::MenuPick/.test(editor) && /"editorMenu"/.test(editor), "the window sends editorMenu and runs menuPick (mdPageEditor.cpp)");
/* the entries the journeys and the shots choose are the plug-in's */
for (const id of ["zoom", "zoom-in", "zoom-actual", "window-size", "updates", "update-daily", "open-log-folder", "developer", "perf-capture"])
	check(new RegExp(`"${id}"`).test(editor + state), `the plug-in's menu has "${id}"`);

console.log(failures ? `deskMenuTest: FAIL (${failures})` : "deskMenuTest: PASS");
process.exit(failures ? 1 : 0);
