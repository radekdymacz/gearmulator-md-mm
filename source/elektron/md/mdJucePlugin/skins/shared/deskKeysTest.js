"use strict";
/* The key dispatcher's one gating rule (deskKeys.js, both editors; release review 2026-10-04, code-js S2 and S8):
   while a dialog or a panel is open (the modal layer's top, deskModal.js) no page shortcut runs and the key is
   left to the dialog (not default-prevented: Space presses its focused button, Backspace edits its field);
   an entry naming that dialog (modal) still runs; Keys.free() is false while one is open or a field has the
   focus. Run on a stand-in document and a stand-in modal layer.
     node deskKeysTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

let keydown = null;
const document = { addEventListener: (type, f) => { if (type === "keydown") keydown = f; }, activeElement: null };
const ctx = vm.createContext({ document, console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskKeys.js"), "utf8") + "\nthis.Keys = Keys;", ctx);
/* the modal layer: a const in the page's global scope, as deskModal.js makes it */
vm.runInContext("const Modal = { top: () => this.modalTop };", ctx);
const Keys = ctx.Keys, ran = [];
const setTop = id => { ctx.modalTop = id; };
Keys.bind({ keys: ["Space"], group: "Transport", does: "play", run: () => ran.push("play") });
Keys.bind({ keys: ["Delete", "Backspace"], group: "Sequence", does: "clear", run: () => ran.push("clear") });
Keys.bind({ keys: ["1"], group: "Workspaces", does: "seq", run: () => ran.push("ws1") });
Keys.bind({ keys: ["Z"], mod: "cmd", group: "Anywhere", does: "undo", run: () => ran.push("undo") });
Keys.bind({ keys: ["T"], group: "Transport", does: "tap", when: () => Keys.free(), run: () => ran.push("tap") });
Keys.bind({ keys: ["?"], group: "Help", does: "keys", modal: "keyspop", run: () => ran.push("help") });
/* a key pressed on a button inside the dialog */
function press(key, mods = {}) {
	let prevented = false;
	const e = Object.assign({ key, code: key.length === 1 ? "Key" + key.toUpperCase() : key, metaKey: false, ctrlKey: false, altKey: false, shiftKey: false,
		target: { closest: () => null }, preventDefault: () => { prevented = true; } }, mods);
	ran.length = 0; keydown(e);
	return { ran: ran.join(","), prevented };
}

setTop(null);
check(press(" ").ran === "play" && press(" ").prevented, "no dialog: Space plays (and takes the key)");
check(press("T").ran === "tap" && Keys.free(), "no dialog: T taps, Keys.free() is true");
for (const id of ["dlg", "globpop", "audiopop", "libpop", "machpop", "bootcard"]) {
	setTop(id);
	const sp = press(" "), bs = press("Backspace"), d1 = press("1"), z = press("z", { metaKey: true }), t = press("T");
	check(!sp.ran && !sp.prevented, `${id} open: Space is the dialog's (no play, not prevented: the focused button is pressed)`);
	check(!bs.ran && !bs.prevented, `${id} open: Backspace clears no steps behind it`);
	check(!d1.ran && !z.ran && !t.ran, `${id} open: the digits, ⌘Z and T do nothing behind it`);
	check(!Keys.free(), `${id} open: Keys.free() is false (the keyboard does not play)`);
}
setTop("keyspop");
check(press("?", { shiftKey: true }).ran === "help", "the list of keys open: ? (its own key, modal: \"keyspop\") still closes it");
check(!press(" ").ran, "the list of keys open: Space does nothing behind it");
setTop("dlg");
check(!press("?", { shiftKey: true }).ran, "a question open: ? does not open the list of keys over it");
setTop(null);
document.activeElement = { closest: () => ({}) };
check(!Keys.free(), "a text field focused: Keys.free() is false");
document.activeElement = null;
check(Keys.free(), "nothing open, no field: Keys.free() is true");

console.log(failures ? `${failures} failure(s)` : "deskKeysTest: all passed");
process.exit(failures ? 1 : 0);
