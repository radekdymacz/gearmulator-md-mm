"use strict";
/* The page's key map (mdDeskKeys.js over the shared dispatcher, shared/deskKeys.js), checked: every Keys.bind entry of the page's scripts, as the page makes
   them (the scripts run here on a stand-in DOM that answers everything and does nothing), against the map's
   rules: plain keys play, a plain letter off the piano row acts on the selected track, Alt is all (two Alts
   are not: rotate and record), no ⇧ or ⌘ letter commands but the standard ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V ⌘X and ⌘D (duplicate); and no two
   dispatched entries share a key and modifiers unless both have a when() the test knows are exclusive.
     node mdDeskKeysTest.js */
const fs = require("fs"), path = require("path");
/* the page's scripts in load order, as the page loads them (mdStudio.html, from sync-mdstudio-skin.py SCRIPTS), less
   the self-tests; a shared one (desk*) is in skins/shared/ */
const FILES = [...fs.readFileSync(path.join(__dirname, "mdStudio.html"), "utf8").matchAll(/<script src="([\w.]+)"><\/script>/g)].map(m => m[1])
	.filter(f => !/SelfTest\.js$/.test(f)).map(f => f.startsWith("desk") ? "../shared/" + f : f);
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

/* ---- the scripts on a stand-in: every unknown name is "any" (callable, constructible, every property any) ---- */
const any = new Proxy(function () { }, {
	get: (t, k) => k === Symbol.toPrimitive ? () => 0 : k === Symbol.iterator ? function* () { } : k === "length" ? 0 : any,
	apply: () => any, construct: () => any, has: () => false, set: () => true });
const real = { Math, JSON, Object, Array, String, Number, Boolean, Set, Map, WeakMap, WeakSet, Symbol, Promise, Date, RegExp, Error, TypeError, parseInt, parseFloat, isNaN, isFinite,
	Uint8Array, Int8Array, Uint16Array, Int16Array, Uint32Array, Int32Array, Float32Array, Float64Array, DataView, ArrayBuffer, console, encodeURIComponent, decodeURIComponent,
	Infinity, NaN, undefined };
const scope = new Proxy({}, {
	has: () => true,
	get: (t, k) => k === Symbol.unscopables ? undefined : k in t ? t[k] : k in real ? real[k] : any,
	set: (t, k, v) => { t[k] = v; return true; } });
const src = FILES.map(f => fs.readFileSync(path.join(__dirname, f), "utf8").replace(/^"use strict";/, ""))
	.join("\n;\n").replace(/^render\(\);\s*$/m, "").replace(/^Bridge\.ready\(\);\s*$/m, "");	/* the page's first render: not a key */
const Keys = new Function("scope", "with (scope) {\n" + src + "\n;return Keys; }")(scope);
const list = Keys.list(), does = b => { try { return typeof b.does === "function" ? String(b.does()) : String(b.does); } catch (_) { return ""; } };
check(list.length > 40, `the scripts ran to their ends: ${list.length} entries`);

/* ---- the physical key of an entry's key, as the dispatcher matches it (e.key upper-cased, or e.code) ---- */
const keyId = k => /^[A-Z]$/.test(k) ? "Key" + k : /^[0-9]$/.test(k) ? "Digit" + k : k;
const ids = b => b.code ? [b.code] : b.keys.map(keyId);
/* the modifiers an entry answers: its own; a plain non-letter character also with ⇧ (the dispatcher's "?" rule) */
const mods = b => (b.mod || "") === "" && !b.code && b.keys.some(k => k.length === 1 && !/[A-Z]/.test(k)) ? ["", "shift"] : [b.mod || ""];
const run = list.filter(b => b.run), name = b => `${Keys.label(b)} (${b.group})`;

/* ---- rule: no ⇧ chords, ⌘ only for the standard edit keys ---- */
/* and the selection's own (K2, DESIGN-keymap.md §3.2): ⌘A selects every step, ⇧← ⇧→ extend it */
const STD_CMD = new Set(["cmd KeyZ", "cmd+shift KeyZ", "cmd KeyY", "cmd KeyC", "cmd KeyV", "cmd KeyX", "cmd KeyD", "cmd KeyA", "shift ArrowLeft", "shift ArrowRight"]);
const badMod = run.filter(b => ids(b).some(id => { const m = b.mod || ""; return m.includes("shift") && !STD_CMD.has(m + " " + id) || m.includes("cmd") && !STD_CMD.has(m + " " + id); }));
check(!badMod.length, "no ⇧ or ⌘ commands but ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V ⌘X ⌘D ⌘A and ⇧← ⇧→ (the selection)" + (badMod.length ? ": " + badMod.map(name).join(", ") : ""));

/* ---- rule: no two dispatched entries on one key and modifiers, unless both are conditional and known exclusive ---- */
/* Escape: each closes what is open (the help, a dialog, LEARN, the paste marks, GLOBAL), one at a time */
/* ← →: Song's rows, or the step selection on Sequence (each when() names its workspace) */
const EXCLUSIVE = new Set([" Escape", " ArrowLeft", " ArrowRight"]);
const by = new Map();
for (const b of run) for (const id of ids(b)) for (const m of mods(b)) { const k = m + " " + id; by.set(k, [...(by.get(k) || []), b]); }
const clash = [...by].filter(([k, bs]) => bs.length > 1 && (bs.some(b => !b.when) || !EXCLUSIVE.has(k)));
check(!clash.length, "no two dispatched entries share a key and modifiers" + (clash.length ? ": " + clash.map(([k, bs]) => `${k} → ${bs.map(name).join(" / ")}`).join("; ") : ""));

/* ---- the approved map is there (RULE 1-3) ---- */
const has = (m, id) => run.some(b => (b.mod || "") === m && ids(b).includes(id));
const WANT = [["", "KeyA", "a note"], ["", "KeyL", "a note"], ["", "KeyZ", "octave down"], ["", "KeyX", "octave up"], ["", "KeyC", "velocity down"], ["", "KeyV", "velocity up"],
	["", "Space", "play / stop"], ["alt", "Space", "record + play"], ["", "KeyR", "randomise the selected track"], ["", "KeyM", "mute the selected track"], ["", "KeyT", "tap tempo"], ["", "KeyB", "tap tempo (B, the Monomachine Editor's tap key)"],
	["", "ArrowUp", "previous track"], ["", "ArrowDown", "next track"], ["alt", "KeyR", "randomise all"], ["alt", "KeyM", "mute / unmute all"], ["alt", "Delete", "clear the pattern"],
	["alt", "ArrowLeft", "rotate"], ["alt", "ArrowRight", "rotate"], ["cmd", "KeyZ", "undo"], ["cmd", "KeyC", "copy"], ["cmd", "KeyV", "paste"], ["cmd", "KeyX", "cut the selected steps"], ["cmd", "KeyD", "duplicate the selected steps"],
	["cmd", "KeyA", "select every step"], ["", "Enter", "the selected steps' trigs"], ["shift", "ArrowRight", "extend the selection"]];
const missing = WANT.filter(([m, id]) => !has(m, id));
check(!missing.length, "the approved keys are bound" + (missing.length ? ": missing " + missing.map(([m, id, w]) => `${m}+${id} (${w})`).join(", ") : ""));
/* K2 (DESIGN-keymap.md): ⇧← ⇧→ are the selection's, never rotate again (rotate is ⌥← ⌥→); steps select on ⌘, ⌥ selects nothing */
const byIdOf = id => list.find(b => b.id === id);
check(run.filter(b => b.mod === "shift" && ids(b).includes("ArrowLeft")).every(b => b.id === "sel-extend"), "⇧← is the selection's extend, not rotate");
check(byIdOf("step-select")?.mod === "cmd" && byIdOf("step-extend")?.mod === "cmd+shift" && !list.some(b => b.area === "Steps" && b.mod === "alt"),
	"a step is selected with ⌘ (⌘⇧ extends); no ⌥ gesture on a step");
check(!list.some(b => b.area === "Steps" && /fill/i.test(does(b)) && b.mod), "the fill is no ⌘-click any more (the step menu has it)");
const GONE = [["", "KeyW", "Walk"], ["cmd", "KeyR", "⌘R"], ["shift", "KeyR", "⇧R"], ["shift", "KeyD", "⇧D"], ["shift", "KeyF", "⇧F"], ["shift", "KeyG", "⇧G"], ["shift", "KeyL", "⇧L"],
	["shift", "Space", "⇧Space"]];
const still = GONE.filter(([m, id]) => has(m, id));
check(!still.length, "the removed keys are gone" + (still.length ? ": " + still.map(x => x[2]).join(", ") : ""));
/* the old mute row (Alt+1..8 / Alt+Q..I) had its own listener: its code list must not come back */
const page = FILES.map(f => fs.readFileSync(path.join(__dirname, f), "utf8")).join("\n") + fs.readFileSync(path.join(__dirname, "mdStudio.html"), "utf8");
check(!/MKEYS|"Digit1", "Digit2"/.test(page), "no Alt mute row (Alt+1..8 / Alt+Q..I)");

/* ---- what the page says names the keys as they are ---- */
const OLD = ["⌘R", "⇧R", "⇧D", "⇧F", "⇧G", "⇧L", "Shift+G", "Shift+R", "Shift+Enter", "Walk", "data-mut=\"walk\"", "Option+1", "Alt+Q"];
const said = OLD.filter(t => page.includes(t)), saidHelp = OLD.filter(t => list.some(b => does(b).includes(t)));
check(!said.length && !saidHelp.length, "no hint, tooltip or help row names a removed key" + (said.length || saidHelp.length ? ": " + [...new Set([...said, ...saidHelp])].join(", ") : ""));

/* ---- the help overlay: its groups, and no row twice ---- */
const shown = list.filter(b => !b.hidden), groups = [...new Set(shown.map(b => b.group))];
check(["Playing", "Selected track", "All", "Transport", "Anywhere"].every(g => groups.includes(g)), "the help has Playing, Selected track, All, Transport, Anywhere: " + groups.join(", "));
const rows = shown.map(b => b.group + "|" + Keys.label(b) + "|" + does(b)), twice = rows.filter((r, i) => rows.indexOf(r) !== i);
check(!twice.length, "no help row twice" + (twice.length ? ": " + twice.join("; ") : ""));
const undescribed = run.filter(b => !b.hidden && !does(b));
check(!undescribed.length, "every visible dispatched key says what it does" + (undescribed.length ? ": " + undescribed.map(name).join(", ") : ""));

console.log("mdDeskKeysTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
process.exit(failures ? 1 : 0);
