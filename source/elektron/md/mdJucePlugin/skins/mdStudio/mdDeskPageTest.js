"use strict";
/* The page's wiring that the model test cannot reach, checked on the page's own scripts (release review
   2026-10-04, code-js S1, S4, S6): they run here as mdDeskKeysTest.js runs them, on a stand-in DOM that answers
   everything and does nothing, with a stand-in Bridge (the commands the page sends are recorded, none is
   answered), timers run by hand and the window's listeners recorded.
   - S1 solo: the machine's own mutes survive a solo and its end; M (click, key, drag) during a solo edits only
     the user's mutes, which the end of the solo gives to the machine;
   - S4: a render held while a gesture ran is made when the gesture ends (paint, l2, chop, song alike);
   - S6: Shift-prepared mutes are dropped when the window loses the focus.
     node mdDeskPageTest.js */
const fs = require("fs"), path = require("path");
const FILES = [...fs.readFileSync(path.join(__dirname, "mdStudio.html"), "utf8").matchAll(/<script src="([\w.]+)"><\/script>/g)].map(m => m[1])
	.filter(f => !/SelfTest\.js$/.test(f) && f !== "deskBridge.js").map(f => f.startsWith("desk") ? "../shared/" + f : f);
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

/* ---- the stand-ins ---- */
const any = new Proxy(function () { }, {
	get: (t, k) => k === Symbol.toPrimitive ? () => 0 : k === Symbol.iterator ? function* () { } : k === "length" ? 0 : any,
	apply: () => any, construct: () => any, has: () => false, set: () => true });
const orAny = o => new Proxy(o, { get: (t, k) => k in t ? t[k] : any });
const real = { Math, JSON, Object, Array, String, Number, Boolean, Set, Map, WeakMap, WeakSet, Symbol, Promise, Date, RegExp, Error, TypeError, parseInt, parseFloat, isNaN, isFinite,
	Uint8Array, Int8Array, Uint16Array, Int16Array, Uint32Array, Int32Array, Float32Array, Float64Array, DataView, ArrayBuffer, console, encodeURIComponent, decodeURIComponent,
	Infinity, NaN, undefined };
/* timers: run by hand (run()), never by the clock */
let timers = [];
const setTimeout = (f, ms) => { timers.push(f); return timers.length; }, clearTimeout = () => { };
function run() { for (let n = 0; n < 20 && timers.length; n++) { const t = timers; timers = []; t.forEach(f => { try { f(); } catch (e) { /* a stand-in DOM can throw in a draw: not what is checked */ } }); } }
/* the window's and the document's listeners */
const on = { window: {}, document: {} };
const listen = where => (type, f) => { (on[where][type] = on[where][type] || []).push(f); };
const fire = (where, type, e = {}) => (on[where][type] || []).forEach(f => f(e));
const document = orAny({ addEventListener: listen("document"), activeElement: null, readyState: "complete" });
/* the Bridge: records every command; answers none (so the optimistic writes stay, as while they are on their way) */
const sent = [], bridgeListeners = [];
let id = 0;
const Bridge = orAny({ send(msg) { sent.push(msg); return ++id; }, onMessage(f) { bridgeListeners.push(f); }, gesture: () => ++id, ready() { }, log() { } });
const scope = new Proxy({ setTimeout, clearTimeout, document, addEventListener: listen("window"), Bridge }, {
	has: () => true,
	get: (t, k) => k === Symbol.unscopables ? undefined : k in t ? t[k] : k in real ? real[k] : any,
	set: (t, k, v) => { t[k] = v; return true; } });
const src = FILES.map(f => fs.readFileSync(path.join(__dirname, f), "utf8").replace(/^"use strict";/, ""))
	.join("\n;\n").replace(/^render\(\);\s*$/m, "").replace(/^Bridge\.ready\(\);\s*$/m, "");
/* the page's names, reached from inside its scope; its drawing replaced by a count (the stand-in DOM draws nothing) */
const P = new Function("scope", "with (scope) {\n" + src + `
;let renders = 0;
render = () => { renders++; }; syncControls = () => { }; renderTop = () => { }; renderSub = () => { }; redraw = () => { }; refreshAudible = () => { };
return { S, Docs, Overlay, Held, PREP, scheduleRender, clickSteps, secAction, selCut, selDuplicate, clearSel, setSel, endSelect, interacting, genEnsure, genSpec, setGenSpec, clickTrackKeys, userMute, soloWrites, muteSel, msSet, prepToggle, unmuteAll, Keys,
	get V() { return V; }, setV(v) { V = v; }, view, get renders() { return renders; }, get pending() { return pendingRender; } }; }`)(scope);

/* the machine document: its mutes (machine.desk.mutes) as the machine has them */
function machineMutes(list) { P.Docs.machine = Object.assign({}, P.Docs.machine || {}, { desk: { mutes: list } }); P.setV(P.view()); }
const muteCmds = () => sent.filter(m => m.op === "mute").map(m => (m.on ? "+" : "-") + (m.t + 1)).join(" ");
const click = (kind, i) => P.clickTrackKeys({ target: { closest: s => s === `[data-${kind}]` ? { dataset: { [kind]: String(i) } } : null } });
const shown = () => P.V.tracks.map((t, i) => t.mute ? i + 1 : 0).filter(Boolean).join(" ");

/* ---- S1 (a): a project opens with tracks 3 and 7 muted on the machine; solo 1, then un-solo ---- */
machineMutes([2, 6]);
check(shown() === "3 7", "the machine's mutes show on the M keys: " + shown());
sent.length = 0; click("solo", 0);
check(muteCmds() === "+2 +4 +5 +6 +8 +9 +10 +11 +12 +13 +14 +15 +16", "solo 1 mutes every other track not muted yet: " + muteCmds());
check(shown() === "3 7", "during the solo the M keys show the user's mutes (3 and 7): " + shown());
sent.length = 0; click("solo", 0);
check(muteCmds() === "-2 -4 -5 -6 -8 -9 -10 -11 -12 -13 -14 -15 -16", "un-solo gives back every track but 3 and 7, which stay muted: " + muteCmds());
check(shown() === "3 7", "after the solo the M keys show 3 and 7 muted: " + shown());

/* ---- S1 (b): M on a non-soloed track during a solo does not unmute it on the machine ---- */
P.Overlay.clear(); sent.length = 0; P.S.soloSet = new Set(); P.S.userMutes = new Set();
machineMutes([2, 6]);
click("solo", 0); sent.length = 0;
click("mute", 4);	/* M on track 5, muted by the solo */
check(muteCmds() === "", "M on track 5 during the solo sends nothing to the machine (it stays muted by the solo): " + (muteCmds() || "none"));
check(shown() === "3 5 7", "its M key shows it muted, as the user meant: " + shown());
click("mute", 2);	/* M on track 3: the user unmutes it while the solo holds */
check(muteCmds() === "" && shown() === "5 7", "M on track 3 during the solo: no command, the M keys show 5 and 7: " + shown());
sent.length = 0; click("solo", 0);
check(muteCmds() === "-2 -3 -4 -6 -8 -9 -10 -11 -12 -13 -14 -15 -16", "un-solo plays every track but 5 and 7 (3 unmuted, 5 muted by the user): " + muteCmds());
/* the M key and the M/S drag go the same way */
sent.length = 0; click("solo", 1); sent.length = 0;
P.muteSel();	/* the selected track is 1 */
P.msSet({ group: "mute", i: 8 }, true);
check(muteCmds() === "", "the M key and the M/S drag during a solo send nothing: " + (muteCmds() || "none"));
check(P.S.userMutes.has(0) && P.S.userMutes.has(8), "they edit the user's mutes (1 and 9)");
sent.length = 0; P.msSet({ group: "solo", i: 1 }, false);	/* the drag lets the solo go */
check(muteCmds() === "-3 -4 -6 -8 -10 -11 -12 -13 -14 -15 -16", "the drag's un-solo leaves 1, 5, 7 and 9 muted (1 and 9 by the user during the solo): " + muteCmds());

/* ---- S1 (c): with no solo, M is the machine's mute at once ---- */
sent.length = 0; click("mute", 11);
check(muteCmds() === "+12", "with no solo, M mutes on the machine: " + muteCmds());

/* ---- S6: Shift-prepared mutes do not survive leaving the window ---- */
P.prepToggle(1); P.prepToggle(4);
check(P.PREP.size === 2, "Shift-click prepares two mutes");
fire("window", "blur");
check(P.PREP.size === 0, "the window's blur drops them (the Shift keyup never comes)");
sent.length = 0; fire("document", "keyup", { key: "Shift" });
check(muteCmds() === "", "a later Shift keyup applies nothing: " + (muteCmds() || "none"));

/* ---- S4: a render held during a gesture is made when it ends ---- */
for (const kind of ["paint", "l2", "chop", "song", "lane", "value", "mutePaint"]) {
	run();
	const before = P.renders;
	P.Held.begin(kind, {});
	P.scheduleRender(); run();
	const held = P.pending && P.renders === before;
	P.Held.end(kind); run();
	check(held && !P.pending && P.renders === before + 1, `${kind}: the render waits while it is held, and is made when it ends`);
}
/* a gesture not holding the page (gv, bpm, wheel, rotate) ends without a render of its own */
run(); { const before = P.renders; P.Held.begin("gv", {}); P.Held.end("gv"); run(); check(P.renders === before, "a gesture with nothing held renders nothing when it ends"); }

/* ---- a rotate run (Alt + arrows, one undo step) ends when an event shows Alt up, not only on Alt's own keyup ---- */
{
	fire("window", "keydown", { altKey: true, key: "ArrowRight" });
	P.Held.begin("rotate", { g: 99 });
	fire("window", "keyup", { altKey: true, key: "ArrowRight" });
	const kept = !!P.Held.as("rotate");
	fire("document", "pointermove", { altKey: false });
	check(kept && !P.Held.as("rotate"), "a rotate run lasts while Alt is down and ends at the first event without Alt");
}

/* ---- a solo leaves a RAM recorder alone: its mute is the sampler's capture and freeze ---- */
{
	const HEX62 = "0".repeat(62);
	const tr = m => ({ machine: m, model: 0, level: 100, synth: Array(8).fill(0), effects: Array(8).fill(0), routing: Array(8).fill(0),
		lfo: { track: 0, param: 0, shape1: 0, shape2: 0, update: 0 }, muteGroup: null, trigGroup: null });
	P.Docs.kits[0] = { schema: "md-desk/kit", version: 2, slot: 0, name: "K", tracks: Array.from({ length: 16 }, (_, i) => tr(i === 9 ? "RAM-R1" : "GND-EMPTY")),
		masterFx: { rhythmEcho: Array(8).fill(0), gateBox: Array(8).fill(0), eq: Array(8).fill(0), dynamix: Array(8).fill(0) },
		firmware: { format: { version: 2, revision: 0 }, lfoState: Array(16).fill(HEX62) } };
	P.Overlay.clear(); P.S.soloSet = new Set(); P.S.userMutes = new Set();
	machineMutes([]);	/* the recorder plays: a capture records */
	sent.length = 0; click("solo", 0);
	check(!/[+-]10\b/.test(muteCmds()), "solo 1 does not mute the recording RAM recorder on track 10: " + muteCmds());
	P.userMute(9, true);	/* the capture's end freezes it during the solo */
	check(/\+10\b/.test(muteCmds()), "the capture's freeze during a solo goes to the machine at once");
	machineMutes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);	/* the machine now: every track but 1 muted */
	sent.length = 0; click("solo", 0);
	check(!/-10\b/.test(muteCmds()), "un-solo does not unfreeze the take on track 10: " + muteCmds());
	check(P.soloWrites({ soloSet: new Set([0]), userMutes: new Set() }, Array(16).fill(false), new Set([9])).every(([i]) => i !== 9), "soloWrites leaves the kept tracks out");
	delete P.Docs.kits[0];
}

/* ---- GEN: the specs made before the kit came (every track GND-EMPTY: Keep) follow the machines once they come;
   a spec the person changed stays ---- */
{
	const HEX62 = "0".repeat(62);
	const tr = m => ({ machine: m, model: 0, level: 100, synth: Array(8).fill(0), effects: Array(8).fill(0), routing: Array(8).fill(0),
		lfo: { track: 0, param: 0, shape1: 0, shape2: 0, update: 0 }, muteGroup: null, trigGroup: null });
	const kit = ms => ({ schema: "md-desk/kit", version: 2, slot: 0, name: "K", tracks: Array.from({ length: 16 }, (_, i) => tr(ms[i] || "GND-EMPTY")),
		masterFx: { rhythmEcho: Array(8).fill(0), gateBox: Array(8).fill(0), eq: Array(8).fill(0), dynamix: Array(8).fill(0) },
		firmware: { format: { version: 2, revision: 0 }, lfoState: Array(16).fill(HEX62) } });
	P.S.gen = { specs: null, last: [], m: [], edited: [], run: null };
	P.setV(P.view()); P.genEnsure();
	check(P.genSpec(0).kind === "keep", "before the kit, track 1's spec is Keep (GND-EMPTY)");
	P.Docs.kits[0] = kit(["TRX-BD", "TRX-SD"]); P.setV(P.view());
	check(P.V.tracks[0].m === "TRX-BD", "the kit came: track 1 is " + P.V.tracks[0].m);
	check(P.genSpec(0).kind === "euclid", "reading the specs already follows the machine: " + P.genSpec(0).kind);
	P.genEnsure();
	check(P.genSpec(0).kind === "euclid" && P.genSpec(0).k === 4 && P.genSpec(1).kind === "euclid" && P.genSpec(1).k === 2, "the render's genEnsure makes the kick's and the snare's defaults: " + JSON.stringify([P.genSpec(0), P.genSpec(1)]));
	P.setGenSpec(1, { kind: "keep" });
	P.Docs.kits[0] = kit(["TRX-BD", "EFM-SD"]); P.setV(P.view()); P.genEnsure();
	check(P.genSpec(1).kind === "keep", "a spec the person set stays when the machine changes");
	P.Docs.kits[0] = kit(["TRX-CH", "EFM-SD"]); P.setV(P.view()); P.genEnsure();
	check(P.genSpec(0).kind === "euclid" && P.genSpec(0).k === 8, "an untouched spec follows a new machine (a hat): " + JSON.stringify(P.genSpec(0)));
	delete P.Docs.kits[0]; P.setV(P.view());
}

/* ---- a selection of steps (DESIGN-step-selection.md): ⌥-click, ⌥-drag, ⌘C ⌘V ⌘X ⌘D Delete, the dropped copy; the
   step clicks that were there before are as they were ---- */
{
	const HEX62 = "0".repeat(62);
	const tr = m => ({ machine: m, model: 0, level: 100, synth: Array(8).fill(0), effects: Array(8).fill(0), routing: Array(8).fill(0),
		lfo: { track: 0, param: 0, shape1: 0, shape2: 0, update: 0 }, muteGroup: null, trigGroup: null });
	P.Docs.kits[3] = { schema: "md-desk/kit", version: 2, slot: 3, name: "K", tracks: Array.from({ length: 16 }, () => tr("GND-EMPTY")),
		masterFx: { rhythmEcho: Array(8).fill(0), gateBox: Array(8).fill(0), eq: Array(8).fill(0), dynamix: Array(8).fill(0) },
		firmware: { format: { version: 2, revision: 0 }, lfoState: Array(16).fill(HEX62) } };
	P.Docs.patterns[5] = { schema: "md-desk/pattern", version: 2, slot: 5, kit: 3, length: 16, totalLength: 16, tempoMultiplier: "1X", swingAmount: 0, accentAmount: 0,
		accent: { editAll: 0, steps: [] }, slide: { editAll: 0, steps: [] }, swing: { editAll: 0, steps: [] },
		tracks: Array.from({ length: 16 }, (_, i) => ({ trigs: i === 0 ? [0, 1, 4] : i === 1 ? [1] : [], accent: [], slide: [], swing: [] })),
		locks: [{ track: 0, param: 0, steps: [[1, 99]] }], firmware: { format: { version: 2, revision: 0 }, lockedRowsField: 0 } };
	const machine = clip => Object.assign({}, P.Docs.machine || {}, { pattern: { current: 5 }, kit: { current: 3 }, desk: {},
		clipboard: Object.assign({ steps: false, sound: false, songRow: false, kit: null, pattern: null, stepsSize: null }, clip) });
	P.Docs.machine = machine({}); P.Overlay.clear(); P.S.multi = new Set(); P.S.sel = 0; P.S.ws = "seq"; P.setV(P.view());
	check(P.V.loaded && P.V.tracks[0].trigs[1], "a loaded pattern: track 1 has a trig on step 2");
	const stepEv = (t, s, mods = {}) => Object.assign({ detail: 1, target: { closest: q => q === "#seq .st" ? { dataset: { t: String(t), s: String(s) } } : null } }, mods);
	const last = () => sent[sent.length - 1] || {};
	const args = m => { const { op, g, ...rest } = m; return op + " " + JSON.stringify(rest); };

	/* ⌥-click a step: the pointer's gesture selects it (no other cell crossed); its click sends nothing */
	sent.length = 0;
	P.Held.begin("select", { from: { t: 0, s: 1 }, at: { t: 0, s: 1 }, ruler: false, extend: false, drop: false, moved: false });
	check(P.interacting(), "the select gesture holds the page's renders while it runs");
	P.endSelect(); P.clickSteps(stepEv(0, 1, { altKey: true }));
	check(JSON.stringify(P.S.stepSel) === '{"t":0,"n":1,"from":1,"to":2}' && !sent.some(m => m.op === "slide" || m.op === "trig"), "⌥-click selects the one step and sends no edit: " + JSON.stringify(P.S.stepSel));
	P.clickSteps(stepEv(1, 1, { altKey: true, detail: 0 }));
	check(P.S.stepSel.t === 1 && P.S.stepSel.from === 1, "⌥ and the keyboard's click on a step select it too");
	/* the main case: one step copied, another step picked, pasted there */
	P.setSel({ t: 0, n: 1, from: 1, to: 2 }); sent.length = 0;
	P.secAction("copy");
	check(args(last()) === 'copySteps {"p":5,"t":0,"n":1,"from":1,"to":2}', "⌘C copies the selected step: " + args(last()));
	P.Docs.machine = machine({ steps: true, stepsSize: { tracks: 1, length: 1 } }); P.setV(P.view());
	P.setSel({ t: 0, n: 1, from: 8, to: 9 }); sent.length = 0;
	P.secAction("paste");
	check(args(last()) === 'pasteSteps {"p":5,"t":0,"from":8}', "⌘V pastes at the selected step: " + args(last()));
	check(P.V.tracks[0].trigs[8] && [...P.V.locks.values()].some(m => m.get(8) === 99), "the paste shows at once: the trig and its lock on step 9");
	/* a block: ⌥-drag from track 1 step 1 to track 2 step 4 */
	P.Held.begin("select", { from: { t: 0, s: 0 }, at: { t: 1, s: 3 }, ruler: false, extend: false, drop: false, moved: true });
	P.endSelect();
	check(JSON.stringify(P.S.stepSel) === '{"t":0,"n":2,"from":0,"to":4}', "⌥-drag selects steps × tracks: " + JSON.stringify(P.S.stepSel));
	sent.length = 0; P.secAction("copy");
	check(args(last()) === 'copySteps {"p":5,"t":0,"n":2,"from":0,"to":4}', "⌘C copies the block: " + args(last()));
	sent.length = 0; P.selCut();
	check(sent.map(m => m.op).join() === "copySteps,clearSteps" && sent[0].g && sent[0].g === sent[1].g, "⌘X: a copy and a clear under one gesture (one undo step)");
	check(!P.V.tracks[0].trigs[0] && !P.V.tracks[1].trigs[1], "the cut shows at once");
	P.Overlay.clear(); P.setV(P.view());
	sent.length = 0; P.selDuplicate();
	check(args(last()) === 'copyStepsTo {"p":5,"t":0,"n":2,"from":0,"to":4,"at":4,"dt":0}' && P.S.stepSel.from === 4 && P.S.stepSel.to === 8,
		"⌘D duplicates the block right after itself and selects the copy: " + args(last()));
	sent.length = 0; P.secAction("clear");
	check(args(last()) === 'clearSteps {"p":5,"t":0,"n":2,"from":4,"to":8}', "Delete clears the selected block: " + args(last()));
	/* ⌥-drag of the selection: a copy where it lets go */
	P.Overlay.clear(); P.setSel({ t: 0, n: 2, from: 0, to: 4 }); P.setV(P.view()); sent.length = 0;
	P.Held.begin("select", { from: { t: 0, s: 1 }, at: { t: 2, s: 6 }, ruler: false, extend: false, drop: true, moved: true });
	P.endSelect();
	check(args(last()) === 'copyStepsTo {"p":5,"t":0,"n":2,"from":0,"to":4,"at":5,"dt":2}' && P.S.stepSel.t === 2 && P.S.stepSel.from === 5,
		"⌥-dragging the selection drops a copy where it lets go: " + args(last()));
	/* the ruler: ⇧-click extends */
	P.setSel({ t: 0, n: 1, from: 2, to: 3 });
	P.Held.begin("select", { from: { t: 0, s: 9 }, at: { t: 0, s: 9 }, ruler: true, extend: true, drop: false, moved: false }); P.endSelect();
	check(JSON.stringify(P.S.stepSel) === '{"t":0,"n":1,"from":2,"to":10}', "⇧-click in the ruler extends the selection: " + JSON.stringify(P.S.stepSel));
	/* Esc clears it; without one, ⌘C is the page of the selected track as before */
	P.clearSel(); sent.length = 0; P.secAction("copy");
	check(!P.S.stepSel && args(last()) === 'copySteps {"p":5,"t":0,"from":0,"to":16}', "no selection: ⌘C copies the track's page as before: " + args(last()));
	/* the step clicks that were there: ⇧-click accent, ⌥⇧-click slide, ⌘-click fill */
	P.Overlay.clear(); P.setV(P.view()); sent.length = 0;
	P.clickSteps(stepEv(0, 4, { shiftKey: true }));
	check(last().op === "accent" && last().s === 4, "⇧-click on a trig is still an accent");
	P.clickSteps(stepEv(0, 4, { shiftKey: true, altKey: true }));
	check(last().op === "slide" && last().s === 4, "⌥⇧-click on a trig is the slide");
	P.clickSteps(stepEv(2, 0, { metaKey: true }));
	check(last().op === "steps", "⌘-click is still the fill");
	P.clearSel(); delete P.Docs.patterns[5]; delete P.Docs.kits[3]; P.Overlay.clear(); P.setV(P.view());
}

/* ---- a reset (another engine, a restored project) starts the solos over ---- */
P.S.soloSet = new Set([2]); P.S.userMutes = new Set([4]);
bridgeListeners.forEach(f => { try { f({ type: "reset" }); } catch (_) { } });
check(P.S.soloSet.size === 0 && P.S.userMutes.size === 0, "a reset clears the page's solos and its record of the user's mutes");

console.log(failures ? `${failures} failure(s)` : "mdDeskPageTest: all passed");
process.exit(failures ? 1 : 0);
