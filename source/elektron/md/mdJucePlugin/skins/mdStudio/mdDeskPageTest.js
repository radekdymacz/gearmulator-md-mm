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
return { S, Docs, Overlay, Held, PREP, scheduleRender, clickTrackKeys, userMute, soloWrites, muteSel, msSet, prepToggle, unmuteAll, Keys,
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

/* ---- a reset (another engine, a restored project) starts the solos over ---- */
P.S.soloSet = new Set([2]); P.S.userMutes = new Set([4]);
bridgeListeners.forEach(f => { try { f({ type: "reset" }); } catch (_) { } });
check(P.S.soloSet.size === 0 && P.S.userMutes.size === 0, "a reset clears the page's solos and its record of the user's mutes");

console.log(failures ? `${failures} failure(s)` : "mdDeskPageTest: all passed");
process.exit(failures ? 1 : 0);
