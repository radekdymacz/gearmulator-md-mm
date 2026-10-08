"use strict";
/* The keyboard view (K-view, deskKeyView.js), on both editors' real maps (scripts/keymap-export.js loads them as the
   pages make them): every key the map dispatches is on a drawn key in its layer, so the view cannot leave one out;
   the legends follow the platform (⌘ / Ctrl, ⌥ / Alt); a layer shows its own keys; the page filter and the search;
   the piano keys and their notes; the mouse's tricks by area; the drawing renders.
     node deskKeyViewTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
const X = require(path.join(__dirname, "../../../../../../scripts/keymap-export.js"));
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const ctx = vm.createContext({ console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskKeyView.js"), "utf8") + "\nthis.KeyView = KeyView;", ctx);
const KV = ctx.KeyView;

const Kmd = X.load("md"), md = Kmd.list(), mm = X.load("mm").list();
const caps = m => m.rows.flat().flatMap(c => c.stack ? c.stack : [c]);
const cap = (m, code) => caps(m).find(c => c.code === code);
const ids = (m, code) => (cap(m, code) || {}).entries || [];
const view = (o = {}) => KV.model(md, Object.assign({ mac: true, layer: "", page: "seq", all: false, query: "", mapping: false }, o));

/* ---- every dispatched key is drawn, in its layer (all pages) ---- */
const LAYERS = ["", "shift", "alt", "cmd", "alt+shift", "cmd+shift"];
for (const [name, list] of [["MD", md], ["MM", mm]]) {
	const shown = new Set();
	for (const layer of LAYERS) { const m = KV.model(list, { mac: true, layer, page: "seq", all: true, query: "", mapping: true }); for (const c of caps(m)) (c.entries || []).forEach(id => shown.add(id)); }
	/* a hidden entry (dispatched; another entry describes it) is drawn by the entries that describe its keys */
	const described = b => KV.capsOf(b).every(c => list.some(o => !o.hidden && o.id !== b.id && KV.capsOf(o).some(d => d.code === c.code && d.mod === c.mod)));
	const missed = list.filter(b => b.run && !b.area && !b.tip && !shown.has(b.id) && !(b.hidden && described(b)));
	check(!missed.length, `${name}: every key the map dispatches is on a drawn key in its layer` + (missed.length ? ": " + missed.map(b => b.id + " " + b.keys.join(",")).join("; ") : ""));
	const unmapped = list.filter(b => b.run && !b.area && !b.tip && !KV.capsOf(b).length);
	check(!unmapped.length, `${name}: no dispatched key the drawn keyboard has no key for` + (unmapped.length ? ": " + unmapped.map(b => b.id).join(", ") : ""));
}

/* ---- the platform's legends ---- */
const mac = view(), pc = view({ mac: false });
check(cap(mac, "MetaLeft").legend.includes("⌘") && cap(mac, "MetaLeft").mod === "cmd" && cap(mac, "AltLeft").legend.includes("⌥"), "Mac: ⌘ command is the command key, ⌥ option");
check(cap(mac, "ControlLeft").inert && !cap(mac, "ControlLeft").mod, "Mac: control is no modifier of the map (Ctrl-click is the right-click)");
check(cap(pc, "ControlLeft").legend === "Ctrl" && cap(pc, "ControlLeft").mod === "cmd" && cap(pc, "AltLeft").legend === "Alt" && cap(pc, "MetaLeft").inert, "Windows / Linux: Ctrl is the command key, Alt is ⌥, the Windows key nothing");
check(!cap(mac, "Delete") && !!cap(pc, "Delete"), "a Mac keyboard has no forward Delete key, a PC's has");

/* ---- the layers ---- */
check(ids(mac, "KeyR")[0] === "randomise-track" && ids(view({ layer: "alt" }), "KeyR")[0] === "randomise-all", "R: the selected track plain, every track on ⌥");
check(ids(view({ layer: "cmd" }), "KeyZ")[0] === "undo" && ids(view({ layer: "cmd+shift" }), "KeyZ")[0] === "redo", "Z: undo on ⌘, redo on ⌘⇧");
check(ids(view({ layer: "shift" }), "Slash").includes("keys-help"), "? is on its key's ⇧ layer");
check(cap(view({ layer: "alt" }), "AltLeft").on && !cap(mac, "AltLeft").on, "the layer's modifier keys are drawn pressed");
check(cap(view({ layer: "cmd" }), "KeyA").entries[0] === "select-all" && cap(view({ layer: "cmd" }), "KeyA").action === "Select all", "⌘A: Select all, its short words on the key");
check(cap(mac, "KeyZ").action === "Oct −" && cap(mac, "KeyX").action === "Oct +", "one short per key: Z Oct −, X Oct +");

/* ---- the page shown, or all ---- */
check(ids(view({ page: "song" }), "ArrowLeft")[0] === "song-row" && ids(view({ page: "seq" }), "ArrowLeft").includes("sel-move"), "← on Song: the previous row; on Sequence: the selection");
check(!view({ page: "seq" }).groups.some(g => g.group === "Kit library, pattern chooser") && view({ all: true }).groups.some(g => g.group === "Kit library, pattern chooser"), "the library's keys show under All only");
check(view({ page: "sound" }).areas.every(a => a.area !== "Steps") && view({ page: "seq" }).areas.some(a => a.area === "Steps" && a.rows.some(r => r.id === "step-menu")), "Mouse tricks: the steps' on Sequence (the step menu among them), none on Sound");
check(view({ page: "sampler" }).areas.some(a => a.area === "Sampler"), "Mouse tricks: the chop steps on the Sampler");

/* ---- the search ---- */
const s = view({ layer: "cmd", query: "undo" });
check(cap(s, "KeyZ").hit && !cap(s, "KeyC").hit, "search \"undo\": ⌘Z is marked, ⌘C is not");
check(s.groups.flatMap(g => g.rows).every(r => /undo/i.test(r.does + r.id + r.label)) && s.groups.length > 0, "search: the list keeps only what matches");
check(view({ query: "fill", all: true }).areas.some(a => a.rows.some(r => r.id === "step-menu")), "search \"fill\" finds the step menu (where the fill is)");

/* ---- the piano ---- */
check(cap(mac, "KeyA").piano === "white" && cap(mac, "KeyA").note === "C" && cap(mac, "KeyL").note === "D", "MD: A to L are white piano keys, C to D");
check(!cap(mac, "KeyW").piano, "MD: W is no piano key yet (K6)");
const mmv = KV.model(mm, { mac: true, layer: "", page: "seq", all: false, query: "", mapping: false });
check(cap(mmv, "KeyW").piano === "black" && cap(mmv, "KeyW").note === "C♯" && cap(mmv, "KeyT").note === "F♯", "MM: W E T Y U O P are black keys, C♯ ... (the same view, its map)");
check(!cap(view({ layer: "alt" }), "KeyA").piano, "the piano is drawn on the plain layer only");

/* ---- tips, and the drawing ---- */
check(mac.tips.length >= 5, `tips from the map: ${mac.tips.length}`);
const h = KV.html(view(), { note: "n", pageName: "Sequence" });
check(h.includes('data-code="KeyR"') && h.includes('data-kvmod="alt"') && h.includes("Mouse tricks") && h.includes("Every key on this page") && !/undefined|NaN/.test(h), "the drawing renders: keys, layer buttons, tricks, the list");
check(caps(mac).length >= 70, `a whole keyboard: ${caps(mac).length} keys`);
check(!fs.readFileSync(path.join(__dirname, "deskKeyView.css"), "utf8").includes("color-mix"), "its stylesheet has no color-mix (an older WebKit)");

console.log(failures ? `${failures} failure(s)` : "deskKeyViewTest: all passed");
process.exit(failures ? 1 : 0);
