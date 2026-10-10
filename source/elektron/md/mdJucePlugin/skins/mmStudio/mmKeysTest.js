"use strict";
/* The Monomachine Editor page's key map (the mockup's 56-keys.js, the Machinedrum Editor's map; MM-PORT-PLAN.md b),
   checked as mdDeskKeysTest.js checks the MD's: every Keys.bind entry of the page's script, as the page makes them
   (the generated mmMockup.js runs here on a stand-in DOM that answers everything and does nothing; no host, so the
   mockup's own example engine), against the map's rules: plain keys play, a plain letter off the piano row acts on
   the selected track, Alt is all (two Alts are not: rotate and record), no ⇧ or ⌘ letter commands but the standard
   ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V; and no two dispatched entries share a key and modifiers unless both have a when() the test knows
   are exclusive.
     node mmKeysTest.js */
const fs = require("fs"), path = require("path");
/* the page's UI script (sync-mmstudio-skin.py: the mockup's sources in build.sh order); the host and the translation are not keys */
const FILES = ["mmMockup.js"];
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

/* ---- the scripts on a stand-in: every unknown name is "any" (callable, constructible, every property any) ---- */
const any = new Proxy(function () { }, {
	get: (t, k) => k === Symbol.toPrimitive ? () => 0 : k === Symbol.iterator ? function* () { } : k === "length" ? 0 : any,
	apply: () => any, construct: () => any, has: () => false, set: () => true });
const real = { Math, JSON, Object, Array, String, Number, Boolean, Set, Map, WeakMap, WeakSet, Symbol, Promise, Date, RegExp, Error, TypeError, parseInt, parseFloat, isNaN, isFinite,
	Uint8Array, Int8Array, Uint16Array, Int16Array, Uint32Array, Int32Array, Float32Array, Float64Array, DataView, ArrayBuffer, console, encodeURIComponent, decodeURIComponent,
	Infinity, NaN, undefined,
	URLSearchParams, location: { hash: "", search: "" } };	/* no deep link: the mockup starts as it does on its own */
const scope = new Proxy({}, {
	has: () => true,
	get: (t, k) => k === Symbol.unscopables ? undefined : k in t ? t[k] : k in real ? real[k] : any,
	set: (t, k, v) => { t[k] = v; return true; } });
const src = FILES.map(f => fs.readFileSync(path.join(__dirname, f), "utf8").replace(/^"use strict";/, ""))
	.join("\n;\n");
const { Keys, piano } = new Function("scope", "with (scope) {\n" + src + "\n;return { Keys, piano: KEYS_PIANO }; }")(scope);
const list = Keys.list(), does = b => { try { return typeof b.does === "function" ? String(b.does()) : String(b.does); } catch (_) { return ""; } };
check(list.length > 40, `the scripts ran to their ends: ${list.length} entries`);

/* ---- the physical key of an entry's key, as the dispatcher matches it (e.key upper-cased, or e.code) ---- */
const keyId = k => /^[A-Z]$/.test(k) ? "Key" + k : /^[0-9]$/.test(k) ? "Digit" + k : k;
const ids = b => b.code ? [b.code] : b.keys.map(keyId);
/* the modifiers an entry answers: its own; a plain non-letter character also with ⇧ (the dispatcher's "?" rule) */
const mods = b => (b.mod || "") === "" && !b.code && b.keys.some(k => k.length === 1 && !/[A-Z]/.test(k)) ? ["", "shift"] : [b.mod || ""];
const run = list.filter(b => b.run), name = b => `${Keys.label(b)} (${b.group})`;

/* ---- rule: no ⇧ chords, ⌘ only for the standard edit keys ---- */
/* the standard edit keys, and the selection's (K7, as the Machinedrum Editor: ⌘X ⌘D ⌘A, ⇧← ⇧→ extend) */
const STD_CMD = new Set(["cmd KeyZ", "cmd+shift KeyZ", "cmd KeyY", "cmd KeyC", "cmd KeyV", "cmd KeyX", "cmd KeyD", "cmd KeyA", "shift ArrowLeft", "shift ArrowRight", "shift KeyB"]);
const badMod = run.filter(b => ids(b).some(id => { const m = b.mod || ""; return m.includes("shift") && !STD_CMD.has(m + " " + id) || m.includes("cmd") && !STD_CMD.has(m + " " + id); }));
check(!badMod.length, "no ⇧ or ⌘ commands but ⇧B (tap tempo), ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V, the selection's ⌘X ⌘D ⌘A ⇧← ⇧→" + (badMod.length ? ": " + badMod.map(name).join(", ") : ""));

/* ---- rule: no two dispatched entries on one key and modifiers, unless both are conditional and known exclusive ---- */
/* Escape: each closes what is open (the help, a dialog, LEARN, the paste marks, the selection, GLOBAL), one at a time;
   ← →: the Song's rows or the selected steps (by workspace); B: the piano roll's Draw on Sequence, tap tempo elsewhere (I-007) */
const EXCLUSIVE = new Set([" Escape", " ArrowLeft", " ArrowRight", " KeyB"]);
const by = new Map();
for (const b of run) for (const id of ids(b)) for (const m of mods(b)) { const k = m + " " + id; by.set(k, [...(by.get(k) || []), b]); }
const clash = [...by].filter(([k, bs]) => bs.length > 1 && (bs.some(b => !b.when) || !EXCLUSIVE.has(k)));
check(!clash.length, "no two dispatched entries share a key and modifiers" + (clash.length ? ": " + clash.map(([k, bs]) => `${k} → ${bs.map(name).join(" / ")}`).join("; ") : ""));

/* ---- the approved map is there (RULE 1-3) ---- */
const has = (m, id) => run.some(b => (b.mod || "") === m && ids(b).includes(id));
/* R / Alt+R: randomise the selected track / every track (GEN, on Sound MUTATE; MM-PORT-PLAN.md d) */
const WANT = [["shift", "KeyB", "tap tempo on every workspace"], ["", "KeyR", "randomise the selected track"], ["alt", "KeyR", "randomise every track"], ["", "KeyA", "a note"], ["", "KeyL", "a note"], ["", "KeyZ", "octave down"], ["", "KeyX", "octave up"], ["", "KeyC", "velocity down"], ["", "KeyV", "velocity up"],
	["", "Space", "play / stop"], ["alt", "Space", "record + play"], ["", "KeyM", "mute the selected track"], ["", "KeyB", "tap tempo off Sequence (Draw on Sequence)"], ["", "KeyW", "a black key (C♯)"], ["", "KeyT", "a black key (F♯)"], ["", "KeyP", "a black key (D♯)"],
	["", "ArrowUp", "previous track"], ["", "ArrowDown", "next track"], ["alt", "KeyM", "mute / unmute all"], ["alt", "Delete", "clear the pattern"],
	["alt", "ArrowLeft", "rotate"], ["alt", "ArrowRight", "rotate"], ["", "Digit0", "unmute / unsolo all"], ["cmd", "KeyZ", "undo"], ["cmd", "KeyC", "copy"], ["cmd", "KeyV", "paste"],
	["", "?", "the list of keys"]];
const missing = WANT.filter(([m, id]) => !has(m, id));
check(!missing.length, "the approved keys are bound" + (missing.length ? ": missing " + missing.map(([m, id, w]) => `${m}+${id} (${w})`).join(", ") : ""));
const GONE = [["cmd", "KeyR", "⌘R"], ["shift", "KeyR", "⇧R"], ["shift", "KeyD", "⇧D"], ["shift", "KeyF", "⇧F"], ["shift", "KeyG", "⇧G"], ["shift", "KeyL", "⇧L"],
	["shift", "Space", "⇧Space"]];
const still = GONE.filter(([m, id]) => has(m, id));
/* R randomises: on Sound MUTATE, elsewhere GEN; Alt+R every track (matched on the physical key, e.code) */
const rKeys = run.filter(b => b.code === "KeyR");
check(rKeys.length === 2 && rKeys.every(b => /randomise/i.test(does(b))) && rKeys.some(b => b.mod === "alt" && /every/i.test(does(b))), "R and Alt+R randomise (GEN, on Sound MUTATE), on the physical key");
/* W is a black key now (C♯), not Walk; T is F♯, so tap tempo is B (MM-PORT-PLAN.md 2026-10-05) */
const walk = run.filter(b => ids(b).includes("KeyW") && /walk/i.test(does(b))), tTap = run.filter(b => ids(b).includes("KeyT") && /tap/i.test(does(b)));
check(!walk.length && !tTap.length, "W is not Walk and T does not tap (both play notes)" + (walk.length || tTap.length ? ": " + [...walk, ...tTap].map(name).join(", ") : ""));
/* the two rows play every semitone: A..P are 0..15 above the A key's note, the white keys C major's steps */
const PIANO = "A W S E D F T G Y H U J K O L P".split(" ");
check(piano && PIANO.every((k, i) => piano[k] === i) && Object.keys(piano).length === 16, "the piano keys play semitones 0-15 in the DAW layout: " + JSON.stringify(piano));
check(["A", "S", "D", "F", "G", "H", "J", "K", "L"].map(k => piano[k]).join(" ") === "0 2 4 5 7 9 11 12 14", "the home row stays the white keys C D E F G A B C D");
const playing = run.filter(b => b.run && b.group === "Playing" && b.hidden).flatMap(ids);
check(PIANO.every(k => playing.includes("Key" + k)), "every piano key is dispatched to the keyboard: " + PIANO.filter(k => !playing.includes("Key" + k)).join(" "));
/* ⇧← ⇧→ extend the selection now (K7); rotate stays on Alt + the arrows */
const shiftRotate = run.filter(b => b.mod === "shift" && ids(b).includes("ArrowLeft") && /rotate/i.test(does(b)));
check(!shiftRotate.length, "⇧← does not rotate (it extends the selection)");
check(!still.length, "the removed keys are gone" + (still.length ? ": " + still.map(x => x[2]).join(", ") : ""));
/* the MM's old map: R was RECORD and L was LEARN; the home row's L is a note now and recording is Alt+Space */
const oldR = run.filter(b => ids(b).includes("KeyR") && /record/i.test(does(b))), oldL = run.filter(b => ids(b).includes("KeyL") && /learn/i.test(does(b)));
check(!oldR.length && !oldL.length, "R is not RECORD and L is not LEARN any more" + (oldR.length || oldL.length ? ": " + [...oldR, ...oldL].map(name).join(", ") : ""));
/* the old hard-wired key handler (e.key === "r" for record, "l" for LEARN) must not come back beside the map */
const page0 = fs.readFileSync(path.join(__dirname, "mmMockup.js"), "utf8");
check(!/e\.key==="r"\|\|e\.key==="R"|e\.key==="l"\|\|e\.key==="L"/.test(page0), "no key handler outside the map for R or L");
const page = FILES.map(f => fs.readFileSync(path.join(__dirname, f), "utf8")).join("\n") + fs.readFileSync(path.join(__dirname, "mmStudio.html"), "utf8");

/* ---- what the page says names the keys as they are ---- */
const OLD = ["⌘R", "⇧R", "⇧D", "⇧F", "⇧G", "⇧L", "Shift+G", "Shift+R", "Shift+Enter", "Walk", "Grid record (R)", "Hold R", "Option+1", "Alt+Q"];
const said = OLD.filter(t => page.includes(t)), saidHelp = OLD.filter(t => list.some(b => does(b).includes(t)));
check(!said.length && !saidHelp.length, "no hint, tooltip or help row names a removed key" + (said.length || saidHelp.length ? ": " + [...new Set([...said, ...saidHelp])].join(", ") : ""));

/* ---- the help overlay: its groups, and no row twice ---- */
const shown = list.filter(b => !b.hidden), groups = [...new Set(shown.map(b => b.group))];
check(["Playing", "Selected track", "All", "Transport", "Anywhere"].every(g => groups.includes(g)), "the help has Playing, Selected track, All, Transport, Anywhere: " + groups.join(", "));
const rows = shown.map(b => b.group + "|" + Keys.label(b) + "|" + does(b)), twice = rows.filter((r, i) => rows.indexOf(r) !== i);
check(!twice.length, "no help row twice" + (twice.length ? ": " + twice.join("; ") : ""));
const undescribed = run.filter(b => !b.hidden && !does(b));
check(!undescribed.length, "every visible dispatched key says what it does" + (undescribed.length ? ": " + undescribed.map(name).join(", ") : ""));

console.log("mmKeysTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
process.exit(failures ? 1 : 0);
