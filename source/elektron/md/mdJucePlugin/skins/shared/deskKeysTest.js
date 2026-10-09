"use strict";
/* The key dispatcher's one gating rule (deskKeys.js, both editors; release review 2026-10-04, code-js S2 and S8):
   while a dialog or a panel is open (the modal layer's top, deskModal.js) no page shortcut runs and the key is
   left to the dialog (not default-prevented: Space presses its focused button, Backspace edits its field);
   an entry naming that dialog (modal) still runs; Keys.free() is false while one is open or a field has the
   focus. The document's copy, cut and paste events (how ⌘C, ⌘X and ⌘V arrive on macOS, B-015) are those keys,
   outside a text field and not twice for one press. Run on a stand-in document and a stand-in modal layer.
     node deskKeysTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const on = {};
const document = { addEventListener: (type, f) => { on[type] = f; }, activeElement: null };
const keydown = e => on.keydown(e);
const ctx = vm.createContext({ document, console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskKeys.js"), "utf8") + "\nthis.Keys = Keys; this.Modifiers = Modifiers;", ctx);
/* the modal layer: a const in the page's global scope, as deskModal.js makes it */
vm.runInContext("const Modal = { top: () => this.modalTop, kind: () => this.modalTop ? (this.modalTop === \"dlg\" || this.modalTop === \"bootcard\" ? \"other\" : \"panel\") : null };", ctx);
const Keys = ctx.Keys, Modifiers = ctx.Modifiers, ran = [];
Modifiers.setPlatform(true);	/* a Mac first: ⌘ is metaKey */
const setTop = id => { ctx.modalTop = id; };
Keys.bind({ keys: ["Space"], group: "Transport", does: "play", run: () => ran.push("play") });
Keys.bind({ keys: ["Delete", "Backspace"], group: "Sequence", does: "clear", run: () => ran.push("clear") });
Keys.bind({ keys: ["1"], group: "Workspaces", does: "seq", run: () => ran.push("ws1") });
Keys.bind({ keys: ["Z"], mod: "cmd", group: "Anywhere", does: "undo", modal: "panel", run: () => ran.push("undo") });
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
	check(!d1.ran && !t.ran, `${id} open: the digits and T do nothing behind it`);
	const panel = id !== "dlg" && id !== "bootcard";
	check(panel ? z.ran === "undo" : !z.ran, panel ? `${id} (a panel) open: ⌘Z still undoes (modal: "panel")` : `${id} open: ⌘Z does nothing behind a question or the start-up card`);
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

/* ---- K1 (DESIGN-keymap.md P4): ⌘ is metaKey on a Mac and Ctrl elsewhere; a Mac's Ctrl and the Windows key are neither ---- */
Keys.bind({ id: "t-octave", scope: "any", keys: ["Z"], group: "Playing", does: "octave", run: () => ran.push("octave") });
Keys.bind({ id: "t-redo", scope: "any", keys: ["Z"], mod: "cmd+shift", group: "Anywhere", does: "redo", run: () => ran.push("redo") });
Modifiers.setPlatform(true);
check(press("z", { metaKey: true }).ran === "undo", "Mac: ⌘Z undoes");
check(press("z", { ctrlKey: true }).ran === "", "Mac: Ctrl+Z is not ⌘Z, and plays no octave either (Ctrl is not a plain key)");
check(Modifiers.cmd({ ctrlKey: true }) === false && Modifiers.cmd({ metaKey: true }) === true, "Mac: Modifiers.cmd reads metaKey only");
check(Keys.label(Keys.byId("t-octave")) === "Z" && Keys.label({ keys: ["Z"], mod: "cmd+shift" }) === "⌘⇧Z" && Keys.label({ keys: ["step"], mod: "alt+shift", area: "Steps" }) === "⌥⇧ step",
	"Mac labels: ⌘⇧Z, ⌥⇧ step: " + Keys.label({ keys: ["Z"], mod: "cmd+shift" }));
check(Modifiers.say("⌘C copy · ⌥-drag") === "⌘C copy · ⌥-drag", "Mac: the page's words keep their symbols");
Modifiers.setPlatform(false);
check(press("z", { ctrlKey: true }).ran === "undo", "Windows / Linux: Ctrl+Z undoes");
check(press("z", { metaKey: true }).ran === "", "Windows / Linux: the Windows key + Z is not Ctrl+Z");
check(press("z", { ctrlKey: true, shiftKey: true }).ran === "redo", "Windows / Linux: Ctrl+Shift+Z redoes");
check(Keys.label({ keys: ["Z"], mod: "cmd+shift" }) === "Ctrl+Shift+Z" && Keys.label({ keys: ["step"], mod: "cmd", area: "Steps" }) === "Ctrl + step",
	"Windows labels: Ctrl+Shift+Z, Ctrl + step: " + Keys.label({ keys: ["Z"], mod: "cmd+shift" }));
const said = Modifiers.say("Select: ⌘-click a step, ⌘⇧-click extends; then ⌘C ⌘V. Wheel: 4 a notch (⇧: 1). Undo (Cmd+Z), ⌥R all, Option-drag");
check(said === "Select: Ctrl-click a step, Ctrl+Shift-click extends; then Ctrl+C Ctrl+V. Wheel: 4 a notch (Shift: 1). Undo (Ctrl+Z), Alt+R all, Alt-drag", "Windows: the page's words say Ctrl, Alt, Shift: " + said);
Modifiers.setPlatform(true);

/* ⌘C / ⌘X / ⌘V as edit commands (B-015; macOS: JUCE's web view turns the keys into copy:, cut:, paste:, so the page
   gets the document's copy, cut and paste events and no keydown) are those keys: the map's entries copy, cut, paste */
Keys.bind({ id: "copy", scope: "any", keys: ["C"], mod: "cmd", group: "Anywhere", does: "copy", run: () => ran.push("copy") });
Keys.bind({ id: "cut", scope: "any", keys: ["X"], mod: "cmd", group: "Anywhere", does: "cut", run: () => ran.push("cut") });
Keys.bind({ id: "paste", scope: "any", keys: ["V"], mod: "cmd", group: "Anywhere", does: "paste", run: () => ran.push("paste") });
function edit(type) {
	let prevented = false;
	ran.length = 0; on[type]({ type, preventDefault: () => { prevented = true; } });
	return { ran: ran.join(","), prevented };
}
check(["copy", "cut", "paste"].every(t => typeof on[t] === "function"), "the dispatcher listens to the copy, cut and paste events");
const CtxDate = vm.runInContext("Date", ctx), realNow = CtxDate.now;
CtxDate.now = () => realNow() + 10000;	/* long after the ⌘Z keydowns above */
for (const t of ["copy", "cut", "paste"]) {
	const r = edit(t);
	check(r.ran === t && r.prevented, `a ${t} event with no keydown before it runs the ⌘${{ copy: "C", cut: "X", paste: "V" }[t]} entry (id ${t}) and takes it`);
}
check(Keys.seen().slice(-3).join(" ") === "cmd+C cmd+X cmd+V", "the key probe notes them as cmd+C cmd+X cmd+V: " + Keys.seen().slice(-3).join(" "));
document.activeElement = { closest: q => (q.includes("input") ? {} : null) };
const inField = edit("copy");
check(!inField.ran && !inField.prevented, "a text field focused: the copy event is the field's own (not run, not prevented)");
document.activeElement = null;
press("c", { metaKey: true });
const twice = edit("copy");
check(!twice.ran, "a copy event right after the ⌘C keydown of the same press is not a second ⌘C");
CtxDate.now = () => realNow() + 20000;
check(edit("copy").ran === "copy", "a later copy event is a press of its own again");
setTop("dlg");
check(!edit("paste").ran, "a question open: a paste event pastes nothing behind it");
setTop(null);
/* off a Mac the edit command is Ctrl's (K1): it still runs the map's entry */
Modifiers.setPlatform(false);
CtxDate.now = () => realNow() + 30000;
check(edit("paste").ran === "paste", "Windows / Linux: a paste event runs the paste entry (Ctrl+V)");
press("c", { ctrlKey: true });
check(!edit("copy").ran, "Windows / Linux: a copy event right after the Ctrl+C keydown is not a second one");
Modifiers.setPlatform(true);
CtxDate.now = realNow;

/* B-023: a focused value (role=slider; a click or its arrow keys focus it) keeps only the keys that move it: ⌘Z, ⌘C
   and Space are the page's (⌘Z after an Alt-drag on a value did nothing: the drag's undo step stayed), ↑ / ↓ are the
   value's (no track change); a text field keeps every key */
const slider = { closest: q => (q.includes("role=slider") ? slider : null) }, text = { closest: q => (q.includes("input") ? text : null) };
const pressOn = (target, key, mods = {}) => {
	let prevented = false;
	const e = Object.assign({ key, code: key.length === 1 ? "Key" + key.toUpperCase() : key, metaKey: false, ctrlKey: false, altKey: false, shiftKey: false,
		target, preventDefault: () => { prevented = true; } }, mods);
	ran.length = 0; keydown(e);
	return { ran: ran.join(","), prevented };
};
Keys.bind({ id: "t-track", scope: "any", keys: ["ArrowUp", "ArrowDown"], group: "Selected track", does: "track", run: () => ran.push("track") });
CtxDate.now = () => realNow() + 40000;
check(pressOn(slider, "z", { metaKey: true }).ran === "undo", "a value focused: ⌘Z undoes");
check(pressOn(slider, " ").ran === "play", "a value focused: Space plays");
check(pressOn(slider, "ArrowUp").ran === "", "a value focused: ↑ is the value's (no track change)");
check(pressOn(slider, "PageDown").ran === "", "a value focused: Page Down is the value's");
check(pressOn({ closest: () => null }, "ArrowUp").ran === "track", "nothing focused: ↑ picks the track");
check(pressOn(text, "z", { metaKey: true }).ran === "" && pressOn(text, " ").ran === "", "a text field focused: ⌘Z and Space are the field's");
document.activeElement = slider;
CtxDate.now = () => realNow() + 50000;
check(edit("copy").ran === "copy", "a value focused: a copy event (⌘C on macOS) runs the copy entry");
document.activeElement = null;
CtxDate.now = realNow;

console.log(failures ? `${failures} failure(s)` : "deskKeysTest: all passed");
process.exit(failures ? 1 : 0);
