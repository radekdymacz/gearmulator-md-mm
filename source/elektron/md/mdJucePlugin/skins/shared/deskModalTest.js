"use strict";
/* The question dialog's queue (Dlg, deskModal.js, both editors; release review 2026-10-04, code-js S3): nothing
   replaces what the dialog shows; a plug-in notice (the C++ side waits on its answer) goes before the page's own
   questions, which wait and come back; a notice is always answered, by its last key when it is closed any other
   way. Run on a stand-in document: #dlg is an object with a hidden flag; its observer is called by hand, as the
   browser calls it after the attribute changes.
     node deskModalTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const stub = () => new Proxy(function () { }, { get: (t, k) => k === "style" || k === "classList" || k === "dataset" ? stub() : k === "hidden" ? true : stub(), apply: () => stub(), set: () => true });
const dlg = { hidden: true, shows: "", querySelector: () => null, querySelectorAll: () => [], classList: { add() { }, remove() { } }, style: { setProperty() { } },
	hasAttribute: () => true, setAttribute() { }, contains: () => false, id: "dlg" };
const observers = [];
class MutationObserver { constructor(f) { this.f = f; } observe(el) { observers.push({ el, f: this.f }); } }
const document = { addEventListener() { }, createElement: () => stub(), body: { appendChild() { } }, readyState: "complete", documentElement: { classList: { toggle() { } } },
	querySelector: s => s === "#dlg" ? dlg : null, activeElement: null };
const ctx = vm.createContext({ document, MutationObserver, console, setTimeout: () => 0, clearTimeout() { }, innerWidth: 1440, window: {} });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskModal.js"), "utf8") + "\nthis.Dlg = Dlg;", ctx);
const Dlg = ctx.Dlg;
/* the dialog closed by whoever (a key's click, Esc, the page): the hidden attribute, then its observers */
const close = () => { dlg.hidden = true; observers.filter(o => o.el === dlg).forEach(o => o.f([])); };
const answers = [];
/* an item as the pages make one: draw() writes the dialog and unhides it */
const ask = (name, extra = {}) => Object.assign({ draw: () => { dlg.shows = name; dlg.hidden = false; } }, extra);
const notice = (name, cancelKey) => { const it = ask(name, { notice: true }); it.answer = k => { if (it.done) return; it.done = true; answers.push(name + ":" + k); }; it.cancel = () => it.answer(cancelKey); return it; };

/* a question while nothing shows: at once */
Dlg.show(ask("load K05?"));
check(dlg.shows === "load K05?" && !dlg.hidden, "a question with nothing open shows at once");
/* a second question waits; it never replaces the first */
Dlg.show(ask("remove ROM?"));
check(dlg.shows === "load K05?" && Dlg.waiting() === 1, "a second question waits behind the first, which stays");
/* a notice comes: it goes first; the question it covers comes back after it */
const n1 = notice("plug-in: overwrite?", 1);
Dlg.show(n1);
check(dlg.shows === "plug-in: overwrite?", "a plug-in notice goes before the page's question");
check(Dlg.waiting() === 2, "the question it covered waits again (with the other one)");
/* an ask while the notice shows: waits, the notice stays */
Dlg.show(ask("first run"));
check(dlg.shows === "plug-in: overwrite?", "an ask while a notice shows never replaces it");
/* the notice answered by its own key */
n1.answer(0); close();
check(answers.join() === "plug-in: overwrite?:0", "the notice's own key answers it once");
check(dlg.shows === "load K05?", "then the question it covered comes back: " + dlg.shows);
close(); check(dlg.shows === "remove ROM?", "then the next: " + dlg.shows);
/* two notices: in their order, both before the page's questions */
const n2 = notice("notice A", 2), n3 = notice("notice B", 1);
Dlg.show(n2); Dlg.show(n3);
check(dlg.shows === "notice A", "a notice goes first: " + dlg.shows);
/* a notice closed without its keys (the dialog hidden by something else): answered by its last key */
answers.length = 0; close();
check(answers.join() === "notice A:2", "a notice closed another way is answered by its last key: " + answers.join());
check(dlg.shows === "notice B", "the second notice before the waiting questions: " + dlg.shows);
n3.answer(0); close();
check(answers.join() === "notice A:2,notice B:0", "each notice answered exactly once: " + answers.join());
check(dlg.shows === "remove ROM?" || dlg.shows === "first run", "the page's questions after the notices: " + dlg.shows);
while (Dlg.waiting()) close();
close();
check(dlg.hidden && Dlg.waiting() === 0, "the queue empties");
/* a key: the same dialog is not added twice (the first-run screen asked for again) */
Dlg.show(ask("first", { key: "first:1" })); Dlg.show(ask("first", { key: "first:1" }));
check(Dlg.waiting() === 0, "an item whose key shows already is not added again");
Dlg.show(ask("other")); Dlg.show(ask("other2", { key: "k" })); Dlg.show(ask("other2", { key: "k" }));
check(Dlg.waiting() === 2, "nor one whose key waits already");
/* closed and asked again in the same click (a key whose handler asks): drawn at once, nothing lost */
while (Dlg.waiting()) close();
close();
const n4 = notice("notice C", 1); Dlg.show(n4); n4.answer(0);
dlg.hidden = true; Dlg.show(ask("follow-up")); observers.forEach(o => o.f([]));
check(dlg.shows === "follow-up" && !dlg.hidden && answers.filter(a => a.startsWith("notice C")).length === 1, "a question asked as the dialog closes shows at once; the notice is answered once");

console.log(failures ? `${failures} failure(s)` : "deskModalTest: all passed");
process.exit(failures ? 1 : 0);
