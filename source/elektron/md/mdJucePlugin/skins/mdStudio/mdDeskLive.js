"use strict";
/* P4 live controls on the machine (mockup v52-v54): mutes on the track keys, the pattern chain and the
   firmware's own LCD while it boots. Loaded after mdDeskApp.js; uses its state (S), its
   render functions and its commands (cmd, setMute). What the page shows is the machine's:
   - mutes: machine.desk.mutes, read from RAM (mutesSource "memory"), whoever set them;
   - chain: machine.desk.chain, read from the firmware's own chain (active, patterns, next), shown and
     made in the Song page (mdDeskApp.js renderSong);
   - LCD: "lcd" messages with the 128 x 64 display while the engine says BOOTING OS. */

/* ===== Mutes on the track keys (manual p.44, mockup v54) =====
   The rail M keys and the Mix strips are the machine's pattern mutes (RAM, machine.desk.mutes).
   Shift held is the machine's FUNCTION in its MUTE window: clicks only prepare ("+" unmute,
   "X" mute, blinking) and all apply together when Shift is let go. Option+1-8 / Option+Q-I
   toggle tracks 1-16 in any workspace; without track keys on screen LCD line 2 says what changed. */
const PREP = new Map();
function showPrep() { $$(".ms.m[data-mute]").forEach(b => { const i = +b.dataset.mute, p = PREP.get(i); b.classList.toggle("prep", p != null); if (p != null) b.dataset.prep = p ? "X" : "+"; else delete b.dataset.prep; }); }
function muteSet(i, on) { on ? S.userMutes.add(i) : S.userMutes.delete(i); setMute(i, on); }
function lcdSay(ch) {
	if (S.ws === "seq" || S.ws === "sound" || S.ws === "mix") return;
	const on = ch.filter(([, m]) => m).map(([i]) => i + 1), off = ch.filter(([, m]) => !m).map(([i]) => i + 1);
	const t = (on.length ? "MUTE " + on.join(" ") : "") + (on.length && off.length ? " · " : "") + (off.length ? "UNMUTE " + off.join(" ") : "");
	lcdSay.text = t; lcdSay.until = performance.now() + 1600; renderSub(); clearTimeout(lcdSay.t); lcdSay.t = setTimeout(renderSub, 1650);
}
/* The message holds LCD line 2 for its time, whatever re-renders meanwhile. */
const renderSub0 = renderSub;
renderSub = function () { renderSub0(); const l = $("#lcd2"); if (l && lcdSay.until && performance.now() < lcdSay.until) l.innerHTML = `<b class="lcdmsg">${lcdSay.text}</b>`; };
function applyPrep() {
	if (!PREP.size) return; const ch = [...PREP];
	for (const [i, m] of ch) if (V.tracks[i] && V.tracks[i].mute !== m) muteSet(i, m);
	PREP.clear(); refreshAudible(); showPrep(); lcdSay(ch);
}
function prepToggle(i) { if (!V.tracks[i]) return; if (PREP.has(i)) PREP.delete(i); else PREP.set(i, !V.tracks[i].mute); showPrep(); }
document.addEventListener("click", e => { const b = e.target.closest(".ms.m[data-mute]"); if (!b || !e.shiftKey) return; e.stopImmediatePropagation(); e.preventDefault(); prepToggle(+b.dataset.mute); }, true);
document.addEventListener("keyup", e => { if (e.key === "Shift") applyPrep(); });
Keys.bind({ keys: ["1–8", "Q–I"], mod: "alt", group: "Mutes", does: "Mute or unmute track 1–16, in any workspace" });
Keys.bind({ keys: ["1–8", "Q–I"], mod: "alt+shift", group: "Mutes", does: "Prepare a mute (+ / X); applied when ⇧ is let go" });
Keys.bind({ keys: ["M key"], mod: "shift", group: "Mutes", does: "Click: prepare that track's mute" });
Keys.bind({ keys: ["drag a value"], mod: "alt", group: "Values", does: "Control All: move that knob on every track (FUNCTION + knob on the machine)" });
const MKEYS = ["Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI"];
document.addEventListener("keydown", e => {
	if (!e.altKey || e.metaKey || e.ctrlKey || e.target.closest?.("input,select,textarea")) return;
	const i = MKEYS.indexOf(e.code); if (i < 0 || !V.tracks[i]) return;
	e.preventDefault(); e.stopImmediatePropagation();
	if (e.shiftKey) { prepToggle(i); return; }
	const on = !V.tracks[i].mute; muteSet(i, on); refreshAudible(); lcdSay([[i, on]]);
}, true);
const refreshAudible0 = refreshAudible; refreshAudible = function () { refreshAudible0(); showPrep(); };
const renderP0 = render; render = function () { renderP0(); showPrep(); markRecLock(); };

/* ===== Knob locks while live recording (P4): the firmware locks the track's next trig whose step has
   not started when the turn lands; the desk says which (machine.desk.recLock), the cell shows it until
   the read-back brings the real lock. ===== */
let recLockKey = "";
function markRecLock() {
	$$(".st.lkpend").forEach(c => c.classList.remove("lkpend"));
	const l = (machineState().desk || {}).recLock; if (!l) return;
	const c = document.querySelector(`.st[data-t="${l.track}"][data-s="${l.step}"]`); if (c) c.classList.add("lkpend");
}
Bridge.onMessage(m => {
	if (m.type !== "machine" || !m.doc.desk) return;
	const l = m.doc.desk.recLock, key = l ? l.track + ":" + l.param + ":" + l.step : "";
	if (key && key !== recLockKey) toast(`Locks track ${l.track + 1} step ${l.step + 1}: its next trig after the move. A trig already playing is too late.`);
	recLockKey = key; setTimeout(markRecLock, 20);
});

/* ===== Fit: the page is laid out for 1440 px. A smaller plug-in window (GUI scale 75 % is 1080 px)
   zooms the whole page out natively (WKWebView pageZoom, mdStudioWebZoom.mm), so nothing is cut. ===== */

/* ===== Tap tempo (manual p.36): T taps, the average of the last taps sets the tempo (0x61). ===== */
const TAP = [];
Keys.bind({ keys: ["T"], group: "Transport", does: "Tap tempo (the average of the last taps)", run: () => {
	const now = performance.now(); if (TAP.length && now - TAP[TAP.length - 1] > 2000) TAP.length = 0;
	TAP.push(now); if (TAP.length > 5) TAP.shift();
	if (TAP.length >= 2) { const bpm = clamp(Math.round(60000 / ((TAP[TAP.length - 1] - TAP[0]) / (TAP.length - 1)) * 10) / 10, 30, 300); cmd("tempo", { bpm }, "tempo", [[["bpm"], bpm]]); renderTop(); toast("Tap tempo: " + bpm.toFixed(1) + " BPM"); }
	else toast("Tap tempo: keep tapping T");
} });

/* ===== Control All (manual p.37, FUNCTION + a DATA ENTRY knob; "CTRL + ALL"): Alt held while moving a track
   value moves the same knob on every track by the same amount. One intent, not sixteen: a tweak command
   per frame (its steps summed in the frame), one undo step per gesture; the plug-in makes it the machine's
   own gesture on the emulator, or coalesced CCs over MIDI. As on the machine (measured), MIDI and CTR
   machines are left out, a RAM recorder on its synthesis page too (tweakWrites, mdDeskModel.js), and a
   value that hits 0 or 127 does not come back symmetrically. t is the gesture's track: the machine tweaks
   from its selected track, so the plug-in selects it first (or another that can lead). The LFO section's SPD, DEPTH and SHMIX are the
   routing page's LFOS, LFOD, LFOM; a curve editor's handle tweaks each value it moves. ===== */
let tweak = null;
function tweakKnob(g, n, t) {
	if (g === "lfo") { const pi = Enums().lfoParams[n]; return pi != null && pi >= 16 ? { g: "rt", knob: pi - 16 } : null; }
	const p = TWEAK_PAGES[g], k = p && V.tracks[t] ? pages(V.tracks[t].m)[p].indexOf(n) : -1;
	return k >= 0 ? { g, knob: k } : null;
}
document.addEventListener("pointerdown", e => {
	tweak = null; if (!e.altKey) return;
	const el = e.target.closest?.("#main .pc[data-g],#main .fader[data-g]");
	if (el) { const t = el.dataset.t != null ? +el.dataset.t : S.sel, k = tweakKnob(el.dataset.g, el.dataset.n, t); if (k) tweak = { el, t, ...k }; return; }
	if (e.target.closest?.("#main canvas.ed")) tweak = { editor: true };
}, true);
document.addEventListener("pointerup", () => { tweak = null; }, true);
function sendTweak(g, knob, d, t) {
	if (!d) return;
	cmd("tweak", { k: V.kit, group: g, knob, d, t }, "tweak:" + g + ":" + knob, tweakWrites(V, g, knob, d, Enums()), undefined,
		(waiting, next) => Object.assign(next, { d: clamp(waiting.d + next.d, -127, 127) }));
}
const setV0 = setV;
setV = function (el, v) {
	if (!tweak || tweak.el !== el) return setV0(el, v);
	sendTweak(tweak.g, tweak.knob, clamp(Math.round(v)) - getV(el), tweak.t);
	syncControls(); redraw();
};
/* sendEditor (mdDeskApp.js) asks first: Alt held on a curve editor, each value its handle moves is a tweak. */
function tweakEditor(to, vals) {
	if (!tweak || !tweak.editor || to.f || !TWEAK_PAGES[to.g]) return false;
	for (const [n, v] of Object.entries(vals)) { const k = tweakKnob(to.g, n, to.t); if (k) sendTweak(k.g, k.knob, clamp(Math.round(v)) - V.tracks[to.t][to.g][n], to.t); }
	return true;
}

/* ===== The editor's menu (skins, GUI scale, settings): right-click an empty part of the header
   (P4; the standalone also has it in the native menu bar). ===== */
document.addEventListener("contextmenu", e => {
	if (!e.target.closest(".top") || e.target.closest("button,[role=slider],[role=button],select,input,b,.lcdpanel")) return;
	e.preventDefault(); Bridge.send({ op: "openMenu" });
});

/* The pattern chain (manual p.37) is made in the Song page's palette, CHAIN (mdDeskApp.js renderSong,
   chainFooter); what the machine plays (playsOf) shows in its header. A new chain or sequencer mode
   re-renders the Song page (below). */
let playsLast;

/* ===== Boot: the firmware's own LCD while the engine says BOOTING OS ===== */
let fwLcd = { shown: false, bits: null };
function drawFwLcd() {
	const c = $("#lcdfwc"); if (!c || !fwLcd.bits) return;
	/* The LCD's own tokens (the plate's --lcd/--ink where the LCD is): the overlay's background is the LCD
	   colour over the whole LCD, the canvas draws only the lit pixels. */
	const x = c.getContext("2d"), cs = getComputedStyle($(".lcdpanel"));
	x.clearRect(0, 0, 128, 64); x.fillStyle = cs.getPropertyValue("--ink");
	const b = fwLcd.bits;
	for (let y = 0; y < 64; y++) for (let i = 0; i < 128; i++) if (b[y * 16 + (i >> 3)] & (0x80 >> (i & 7))) x.fillRect(i, y, 1, 1);
}
/* A plate switch while it shows: redraw with the new plate's ink. */
new MutationObserver(() => { if (fwLcd.shown) drawFwLcd(); }).observe(document.documentElement, { attributes: true, attributeFilter: ["data-plate"] });
function showFwLcd(on) {
	const p = $(".lcdpanel"); if (!p || on === fwLcd.shown) return;
	fwLcd.shown = on;
	if (on) { p.classList.remove("fwfade"); p.classList.add("fwboot"); return; }
	p.classList.remove("fwboot"); p.classList.add("fwfade"); setTimeout(() => p.classList.remove("fwfade"), 700);
}
/* modInFlight (mdDeskApp.js) guards an outstanding modSet: it must not survive an engine change
   (its result will never come) or outlive the machine going from not-ready to ready (a fresh
   "mod" message follows and any earlier in-flight id is moot). */
let wasReady = false;
Bridge.onMessage(m => {
	/* The engine changed (emulator <-> HW MIDI): the documents start over. */
	if (m.type === "reset") { resetDocs(Docs); Overlay.clear(); modInFlight = 0; wasReady = false; scheduleRender(); return; }
	/* The firmware's LCD shows while the machine takes no input (machine.input: the firmware starts);
	   BOOTING OS lasts until keys work: the firmware answers MIDI early, but its start-up animation
	   ignores panel keys until it is over (about 13 s; the lifecycle's "animating"). */
	if (m.type === "lcd") {
		if (m.bits) { const s = atob(m.bits); fwLcd.bits = Uint8Array.from(s, ch => ch.charCodeAt(0)); Boot.lcd(fwLcd.bits); }
		/* P7: the start-up animation lives in the start-up card; the header's LCD stays itself */
		showFwLcd(false);
	}
	else if (m.type === "machine") {
		if (m.doc.input) { showFwLcd(false); if (!wasReady) modInFlight = 0; wasReady = true; }
		else wasReady = false;
		const plays = { chain: m.doc.desk ? m.doc.desk.chain : undefined, songMode: m.doc.songMode, song: m.doc.song };
		if (S.ws === "song" && !sameValue(plays, playsLast)) { playsLast = plays; scheduleRender(); }
	}
});
