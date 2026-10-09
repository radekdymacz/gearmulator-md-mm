"use strict";
/* P4 live controls on the machine (mockup v52-v54): mutes on the track keys, the pattern chain and the
   firmware's own LCD while it boots. Loaded after the app's files (mdDeskApp.js lists them); uses their state (S), their
   render functions and its commands (cmd, setMute). What the page shows is the machine's:
   - mutes: machine.desk.mutes, read from RAM (mutesSource "memory"), whoever set them; during a solo the M
     keys show the user's mutes, which the end of the solo gives back to the machine (mdDeskModel.js, soloTo);
   - chain: machine.desk.chain, read from the firmware's own chain (active, patterns, next), shown and
     made in the Song page (mdDeskSong.js renderSong);
   - LCD: "lcd" messages with the 128 x 64 display while the engine says BOOTING OS. */

/* ===== Mutes on the track keys (manual p.44, mockup v54) =====
   The rail M keys and the Mix strips are the machine's pattern mutes (RAM, machine.desk.mutes).
   Shift held is the machine's FUNCTION in its MUTE window: clicks only prepare ("+" unmute,
   "X" mute, blinking) and all apply together when Shift is let go. M mutes or unmutes the selected
   track, Alt+M every track (any audible: mute all; none: unmute all), in any workspace; without
   track keys on screen LCD line 2 says what changed. */
const PREP = new Map();
function showPrep() { $$(".ms.m[data-mute]").forEach(b => { const i = +b.dataset.mute, p = PREP.get(i); b.classList.toggle("prep", p != null); if (p != null) b.dataset.prep = p ? "X" : "+"; else delete b.dataset.prep; }); }
function muteSet(i, on) { userMute(i, on); }
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
/* leaving the window drops them (as the MM page): Shift let go elsewhere never reaches the page */
addEventListener("blur", () => { if (!PREP.size) return; PREP.clear(); showPrep(); });
Keys.bind({ id: "mkey-prepare", scope: "any", area: "Tracks", keys: ["M key"], mod: "shift", group: "Anywhere", does: "Click: prepare that track's mute (+ / X); applied when ⇧ is let go" });
Keys.bind({ id: "control-all", scope: "sound mix", area: "Values", keys: ["drag a value"], mod: "alt", group: "All", does: "Control All: move that knob on every track (FUNCTION + knob on the machine)" });
/* M: the selected track; Alt+M: every track, one toggle (matched on e.code: with Alt macOS types µ) */
function muteSel() { const t = S.sel; if (!V.tracks[t]) return; const on = !V.tracks[t].mute; muteSet(t, on); refreshAudible(); lcdSay([[t, on]]); }
function muteAllToggle() {
	const on = V.tracks.some(t => !t.mute), ch = [];
	V.tracks.forEach((t, i) => { if (t.mute !== on) { muteSet(i, on); ch.push([i, on]); } });
	refreshAudible(); if (ch.length) lcdSay(ch);
	toast(on ? "Every track muted (Alt+M again: unmute all)" : "Every track unmuted");
}
Keys.bind({ id: "mute-track", short: "Mute", scope: "any", keys: ["M"], code: "KeyM", group: "Selected track", does: "Mute or unmute the selected track", when: () => kbOn(), run: () => muteSel() });
Keys.bind({ id: "mute-all", short: "Mute all", scope: "any", keys: ["M"], code: "KeyM", mod: "alt", group: "All", does: "Mute every track; when none is audible, unmute every track", when: () => kbOn(), run: () => muteAllToggle() });
/* ↑ / ↓: the previous / next track, while no value has the keys (a focused value, tempo or bar keeps them:
   the dispatcher leaves [role=slider] alone, a bar's value stops them itself) */
Keys.bind({ id: "track-prev-next", short: "Track − / Track +", scope: "any", keys: ["ArrowUp", "ArrowDown"], group: "Selected track", does: "Select the previous / next track (a focused value keeps ↑ / ↓ for itself). Sequence with selected steps: move the selection a track",
	when: () => kbOn() && $("#kpop").hidden && $("#keyspop").hidden,
	run: e => { if (selKeys()) { selMove(e.key === "ArrowDown" ? 1 : -1, 0); return; } select((S.sel + (e.key === "ArrowDown" ? 1 : 15)) % 16); } });
const refreshAudible0 = refreshAudible; refreshAudible = function () { refreshAudible0(); showPrep(); };
const renderP0 = render; render = function () { renderP0(); showPrep(); markRecLock(); };

/* ===== A drag across the M (or S) keys paints them, rail and Mix alike (shared/deskTogglePaint.js): the pressed
   key toggles where the pointer goes down and its new state is the paint; every other M key (S key) the pointer
   crosses becomes that, once; a key already so sends nothing. The pointer is the page's (held on the body) and
   every point between two of its events is looked at, so a fast drag skips no key. Held: {kind: "mutePaint",
   group: "mute"|"solo", value, seen, at: the last point}. Each changed track's mute goes as a click's does
   (muteSet, applySolo); mutes are no undo step. A click is a one-key paint; the click that follows the press is
   the gesture's. The keyboard's click (Enter, Space) and Shift-click (prepare) stay the click's: Shift takes no
   drag, as the machine's MUTE window takes one key at a time. ===== */
const MS_KEY = ".ms[data-mute],.ms[data-solo]";
function msKeyAt(el) {
	const b = el?.closest?.(MS_KEY); if (!b || b.disabled || b.dataset.capna != null) return null;
	const k = b.dataset.mute != null ? { group: "mute", i: +b.dataset.mute } : { group: "solo", i: +b.dataset.solo };
	return V.tracks[k.i] ? k : null;
}
const msOn = k => k.group === "mute" ? !!V.tracks[k.i].mute : S.soloSet.has(k.i);
function msSet(k, on) {
	if (k.group === "mute") muteSet(k.i, on);
	else { const next = new Set(S.soloSet); on ? next.add(k.i) : next.delete(k.i); setSolo(next); }
	refreshAudible();
}
let msClickEaten = false;
addEventListener("pointerdown", e => {
	msClickEaten = false;
	if (e.button !== 0 || e.shiftKey || e.altKey || e.metaKey || e.ctrlKey) return;
	const k = msKeyAt(e.target); if (!k) return;
	const { paint, change } = TogglePaint.begin(k.group, k.i, msOn(k));
	Held.begin("mutePaint", Object.assign(paint, { at: { x: e.clientX, y: e.clientY } }));
	msClickEaten = true; grabPointer(document.body, e); e.preventDefault();
	msSet(k, change);
}, true);
document.addEventListener("pointermove", e => {
	if (!Held.as("mutePaint")) return;
	if (e.buttons === 0 && e.pointerType === "mouse") { endMutePaint(); return; }
	const co = e.getCoalescedEvents?.() || [];	/* none for a made-up event: the event itself */
	for (const ev of co.length ? co : [e]) {
		const at = { x: ev.clientX, y: ev.clientY };
		for (const pt of TogglePaint.points(Held.as("mutePaint").at, at)) {
			const k = msKeyAt(document.elementFromPoint(pt.x, pt.y)), p = Held.as("mutePaint"); if (!k) continue;
			const r = TogglePaint.visit(p, k.group, k.i, msOn(k));
			if (r.paint !== p) Held.with("mutePaint", { seen: r.paint.seen });
			if (r.change != null) msSet(k, r.change);
		}
		Held.with("mutePaint", { at });
	}
});
function endMutePaint() { if (!Held.end("mutePaint")) return; setTimeout(() => { msClickEaten = false; }, 0); }
document.addEventListener("pointerup", endMutePaint); document.addEventListener("pointercancel", endMutePaint);
window.addEventListener("blur", endMutePaint);
addEventListener("click", e => { if (!msClickEaten) return; msClickEaten = false; e.stopImmediatePropagation(); e.preventDefault(); }, true);
Keys.bind({ id: "ms-paint", scope: "any", area: "Tracks", keys: ["drag M / S keys"], group: "Anywhere", does: "Mute (solo) or unmute every track the drag crosses, as the first key became" });

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

/* ===== Tap tempo (manual p.36): T (or B, the Monomachine Editor's tap key, where T is a black key) taps, the
   average of the last taps sets the tempo (0x61). ===== */
const TAP = [];
Keys.bind({ id: "tap-tempo", short: "Tap / Tap", scope: "any", keys: ["T", "B"], group: "Transport", does: "Tap tempo (the average of the last taps; B as in the Monomachine Editor)", when: () => kbOn(), run: () => {
	const now = performance.now(); if (TAP.length && now - TAP[TAP.length - 1] > 2000) TAP.length = 0;
	TAP.push(now); if (TAP.length > 5) TAP.shift();
	if (hostTempoRefused()) { TAP.length = 0; return; }
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
	const p = TWEAK_PAGES[g], k = p && V.tracks[t] ? pages(V.tracks[t].m, Cat)[p].indexOf(n) : -1;
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
	cmd("tweak", { k: V.kit, group: g, knob, d, t }, "tweak:" + g + ":" + knob, tweakWrites(V, g, knob, d, Enums(), Cat), undefined,
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

/* ===== The editor's menu (zoom, updates, the log folder, Developer): right-click anywhere the page has no menu of
   its own (DeskMenu.wantsEditor; P4; the standalone also has it in the native menu bar). I-008: the plug-in sends its entries (editorMenu), the
   page draws them where it was right-clicked (DeskMenu.showEditor, shared/deskMenu.js). ===== */
let editorMenuAt = { x: 24, y: 24 };
function openEditorMenu(x, y) { editorMenuAt = { x, y }; Bridge.send({ op: "openMenu" }); }
document.addEventListener("contextmenu", e => {
	if (!DeskMenu.wantsEditor(e)) return;
	e.preventDefault(); openEditorMenu(e.clientX, e.clientY);
});
Bridge.onMessage(m => { if (m.type === "editorMenu") DeskMenu.showEditor(m, editorMenuAt.x, editorMenuAt.y, c => Bridge.send(c)); });

/* The pattern chain (manual p.37) is made in the Song page's palette, CHAIN (mdDeskSong.js renderSong,
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
/* modInFlight (mdDeskControl.js) guards an outstanding modSet: it must not survive an engine change
   (its result will never come) or outlive the machine going from not-ready to ready (a fresh
   "mod" message follows and any earlier in-flight id is moot). */
let wasReady = false;
Bridge.onMessage(m => {
	/* The engine changed (emulator <-> HW MIDI) or the machine restarted (a project restored): the documents start
	   over, and so do the page's solos (the new machine's mutes are its own). */
	if (m.type === "reset") { resetDocs(Docs); Overlay.clear(); modInFlight = 0; wasReady = false; S.soloSet = new Set(); S.userMutes = new Set(); scheduleRender(); return; }
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

/* ===== The keyboard (P10): the home row always plays the selected track =====
   A S D F G H J K L are white keys C D E F G A B C D, Z / X the octave down / up (−2..+2), C / V the
   velocity a step down / up (20 40 60 80 100 127, from 100: keyVel, mdDeskModel.js), in every
   workspace while no text field or dialog has the keys. A key is the note intent: noteOn {t, vel, pitch}
   (pitch: semitones from the sound, keyPitch) and noteOff {t, pitch} when it is let go. The core plays it:
   the track's MAP EDITOR note, on the sample machines (ROM, RAM-P) a PTCH held while the key is down and
   given back after (not an edit, no undo step); others at their own pitch; GND-EMPTY and the recorders
   not at all; while live recording the track's TRIG key. What the core says about it (a refusal, "at its
   own pitch", recording) is said once. Key repeat is ignored; a key let go ends its own note (the core
   keeps one note per track: a later key replaces it). */
const KB = { oct: 0, vel: KEYS_VEL, held: new Map(), told: new Set() };
/* the keys that play and act on tracks: no dialog or panel open (GLOBAL, AUDIO / MIDI, the library, the picker, the
   keys list, a question), no text field focused: the one rule of both editors (deskKeys.js) */
function kbOn() { return Keys.free(); }
function kbTell(key, text) { if (KB.told.has(key)) return; KB.told.add(key); toast(text); }
function kbSaid(r) { const say = r.ok ? r.note : (r.errors || [])[0]; if (say) kbTell(say, say); }
function kbDown(e) {
	if (e.repeat || KB.held.has(e.code)) return;
	const t = S.sel, pitch = keyPitch(e.code.replace(/^Key/, ""), KB.oct); if (!V.tracks[t] || pitch == null) return;
	Bridge.send({ op: "noteOn", t, vel: KB.vel, pitch }, { onResult: kbSaid }); tx();
	KB.held.set(e.code, { t, pitch });
}
function kbUp(code) {
	const h = KB.held.get(code); if (!h) return;
	KB.held.delete(code);
	Bridge.send({ op: "noteOff", t: h.t, pitch: h.pitch });
}
function kbVel(d) { KB.vel = keyVel(KB.vel, d); toast(`Keyboard velocity ${KB.vel}`); }
function kbOct(d) { KB.oct = clamp(KB.oct + d, KEYS_OCT[0], KEYS_OCT[1]); toast(`Keyboard octave ${KB.oct > 0 ? "+" : ""}${KB.oct}`); }
document.addEventListener("keyup", e => kbUp(e.code));
addEventListener("blur", () => [...KB.held.keys()].forEach(kbUp));
Keys.bind({ id: "piano-run", scope: "any", keys: [...KEYS_WHITE], group: "Playing", hidden: true, field: true, when: kbOn, run: kbDown, does: "" });
Keys.bind({ id: "octave-down", scope: "any", keys: ["Z"], group: "Playing", hidden: true, field: true, when: kbOn, run: () => kbOct(-1), does: "" });
Keys.bind({ id: "octave-up", scope: "any", keys: ["X"], group: "Playing", hidden: true, field: true, when: kbOn, run: () => kbOct(1), does: "" });
Keys.bind({ id: "velocity-down", scope: "any", keys: ["C"], group: "Playing", hidden: true, field: true, when: kbOn, run: () => kbVel(-1), does: "" });
Keys.bind({ id: "velocity-up", scope: "any", keys: ["V"], group: "Playing", hidden: true, field: true, when: kbOn, run: () => kbVel(1), does: "" });
Keys.bind({ id: "piano-white", short: "Play", scope: "any", notes: "C D E F G A B C D", keys: ["A S D F G H J K L"], group: "Playing", does: "Play the selected track: white keys C D E F G A B C D, from any workspace. ROM and RAM-P machines are pitched (PTCH, 3 steps a semitone; given back when the key is let go), others play at their own pitch. While recording: records the trig" });
Keys.bind({ id: "octave", short: "Oct − / Oct +", scope: "any", keys: ["Z", "X"], group: "Playing", does: () => `Octave down / up, −2 to +2 (now ${KB.oct > 0 ? "+" : ""}${KB.oct})` });
Keys.bind({ id: "velocity", short: "Vel − / Vel +", scope: "any", keys: ["C", "V"], group: "Playing", does: () => `Velocity down / up: 20 40 60 80 100 127 (now ${KB.vel})` });
