"use strict";
/* Small comforts (DESIGN-generators.md §7) and Alt, the global modifier. The comforts that are pointer
   gestures (the wheel on a step, the lock lane's ramp) are mdDeskGestures.js's. */

/* ===== Alt, a global modifier: the wider clear. Alt + CLR (or Alt + Delete in Sequence) clears the whole
   pattern, every track's trigs and locks; Alt + the lock lane's clear key every lock of the selected track
   (the lane and its parameter keys are that track's). One undo step each (clearPattern, clearLocks). While
   Alt is held the keys it changes say so (altLabels); Alt-drag stays Control All (mdDeskLive.js). ===== */
function clearPattern() {
	const opt = [];
	V.tracks.forEach((t, i) => t.trigs.forEach((on, s) => { if (on) opt.push([["tracks", i, "trigs", s], false]); }));
	for (const k of V.locks.keys()) opt.push([["locks", k], DELETE]);
	cmd("clearPattern", { p: V.pat }, undefined, opt); render();
}
function clearTrackLocks(t) {
	cmd("clearLocks", { p: V.pat, t }, undefined, [...V.locks.keys()].filter(k => +k.split(":")[0] === t).map(k => [["locks", k], DELETE]));
	renderTop(); refreshRow(t); renderLane();
}
function altLabels() {
	const c = $('[data-sec="clear"]');
	if (c) { c.textContent = S.alt ? "All" : "Clr"; c.title = S.alt ? `Clear the whole pattern ${patName(V.pat)}: every track's trigs and locks (one undo step)` : "Clear (Delete). Alt: the whole pattern"; }
	const cl = $("#clearLane");
	if (cl) { const t = S.alt ? `Clear every lock of track ${S.sel + 1}` : `Clear ${S.lane} locks. Alt: every lock of track ${S.sel + 1}`; cl.title = t; cl.setAttribute("aria-label", t); }
}
S.alt = false;
/* Alt seen up (any key or pointer event without it) also ends a rotate run: its keyup may never reach the page
   (the Monomachine Editor's fix, MM-PORT-PLAN.md 2026-10-05) */
function showAlt(on) { if (S.alt === on) return; S.alt = on; if (!on) Held.end("rotate"); document.body.classList.toggle("althold", on); altLabels(); if (S.ws === "seq") genDraw(); if (S.ws === "sound") renderMutStrip(); }
addEventListener("keydown", e => showAlt(e.altKey), true);
addEventListener("keyup", e => showAlt(e.altKey), true);
addEventListener("blur", () => showAlt(false));
document.addEventListener("pointermove", e => showAlt(e.altKey), { passive: true, capture: true });
/* ===== Small comforts (DESIGN-generators.md §7), each one undo step, each a plain edit the machine takes on any
   engine: the lock budget in the lane's header, rotate a track (Alt + arrows), the every-N fill (⌘-click), the
   wheel on a step moves its lock, a ramp in the lock lane (Shift-drag), double the pattern (LEN ×2), paste to many
   tracks (Shift-click the headers, ⌘V), unmute and unsolo all (0). The step arithmetic is mdDeskGen.js's. ===== */
S.multi = new Set();
/* "41 / 64 locked parameters" over the lock lane: the machine's budget per pattern, as the top bar's meter */
function syncLockBudget() {
	const el = $("#lockbudget"); if (!el) return;
	const n = V.locks.size, left = 64 - n;
	el.className = "lockbudget" + (n >= 64 ? " full" : n >= 52 ? " warn" : "");
	el.innerHTML = `<b>${n}</b> / 64 locked parameters`;
	el.title = `The machine holds 64 locked parameters in a pattern (a track and a parameter with any lock counts once). ${n} in use, ${left} left.${n >= 64 ? " Clear a lane before you lock a new parameter." : ""}`;
}
const seqReady = () => { if (!V.loaded) { toast("The pattern is not loaded yet."); return false; } if (V.rec) { toast("Wait until live recording stops."); return false; } return true; };
const patLenNow = () => Math.min(V.length, V.len);

/* ---- rotate: Alt + Left / Right (the one Alt that is not "all": FUNCTION + arrows on the machine) moves the selected track's trigs, accents, slides and locks one step, wrapping
   at the length. Every press is one rotate edit; the presses while Alt stays down share one g: one undo step (the
   gesture slot's "rotate", ended when Alt comes up: mdDeskGestures.js). */
function rotateWrites(t, by, len) {
	const tr = V.tracks[t], w = [], from = s => genRotStep(s, -by, len);	/* the step that lands on s */
	for (let s = 0; s < len; s++) {
		const f = from(s);
		w.push([["tracks", t, "trigs", s], !!tr.trigs[f]]);
		if (!V.accAll) w.push([["tracks", t, "acc", s], tr.acc.has(f)]);
		if (!V.slideAll) w.push([["tracks", t, "slide", s], tr.slide.has(f)]);
	}
	for (const [k, m] of V.locks) {
		if (+k.split(":")[0] !== t) continue;
		for (let s = 0; s < len; s++) { const v = m.get(from(s)); w.push([["locks", k, s], v == null ? DELETE : v]); }
	}
	return w;
}
function rotateTrack(by) {
	if (!seqReady()) return;
	const t = S.sel, len = patLenNow();
	if (len < 2) return;
	const run = Held.as("rotate") || Held.begin("rotate", { g: Bridge.gesture() });
	cmd("rotate", { p: V.pat, t, by, g: run.g }, undefined, rotateWrites(t, by, len));
	refreshRow(t); renderLane();
}

/* ---- every-N fill: ⌘-click a step, every 2nd step from there to the end (⌘⇧: every 4th) comes on; from a step
   with a trig they go off (the first step decides, as the paint does). One steps edit. */
function fillEvery(t, s, n) {
	if (!seqReady()) return;
	const tr = V.tracks[t], end = s < V.length ? patLenNow() : V.len, on = !tr.trigs[s];
	const want = new Set(genEveryN(tr.trigs, s, end, n, on)), w = [];
	for (let k = s; k < end; k++) {
		if (want.has(k) === !!tr.trigs[k]) continue;
		w.push([["tracks", t, "trigs", k], want.has(k)]);
		if (!want.has(k)) w.push([["tracks", t, "acc", k], false], [["tracks", t, "slide", k], false], ...clearStep(t, k));
	}
	const say = `${on ? "Filled every" : "Cleared every"} ${n === 2 ? "2nd" : "4th"} step of track ${t + 1}, steps ${s + 1}–${end}`;
	if (w.length) cmd("steps", { p: V.pat, from: s, to: end, rows: [{ t, on: [...want] }] }, undefined, w, r => { if (r.ok) toast(say); });
	else toast(`Track ${t + 1} already has every ${n === 2 ? "2nd" : "4th"} step ${on ? "on" : "off"} from step ${s + 1}`);
	if (!on) renderTop();
	refreshRow(t); if (t !== S.sel) select(t); else renderLane();
}

/* ---- double the pattern (LEN ×2 on the LCD): length × 2, the new half a copy; the core refuses what the
   machine cannot hold (64 steps, or above 32 in CLASSIC) and says why. */
function doublePattern() {
	if (!seqReady()) return;
	if (V.length * 2 > 64) { toast(`A pattern of ${V.length} steps cannot double: 64 steps is the longest.`); return; }
	cmd("doublePattern", { p: V.pat });
}

/* ---- paste to many: Shift-click track headers to mark them, then ⌘V pastes the copied steps into each one
   (from the page shown), one gesture, one undo step. A plain click on a header, or Esc, unmarks them. */
function multiToggle(i) {
	const multi = new Set(S.multi); multi.has(i) ? multi.delete(i) : multi.add(i); S.multi = multi;
	renderRail(); renderLane();
	const list = [...S.multi].sort((a, b) => a - b).map(x => x + 1).join(" ");
	toast(S.multi.size ? `⌘V pastes into tracks ${list}` : "No tracks marked for paste");
}
function pasteToMany(from) {
	if (!seqReady()) return;
	const tracks = [...S.multi].sort((a, b) => a - b), g = Bridge.gesture();	/* one undo step */
	const say = `Pasted into tracks ${tracks.map(x => x + 1).join(" ")} (one undo step)`;
	tracks.forEach((t, j) => cmd("pasteSteps", { p: V.pat, t, from, g }, undefined, undefined, j === tracks.length - 1 ? r => { if (r.ok) toast(say); } : undefined));
}

/* ---- unmute and unsolo every track (0, or M/S off over the rail) */
function unmuteAll() {
	if (!V.tracks.some(t => t.mute || t.solo)) { toast("No track is muted or soloed."); return; }
	S.soloSet = new Set(); S.userMutes = new Set(); V = view();
	applySolo();
	refreshAudible();
}
document.addEventListener("click", e => { if (e.target.closest("#allon")) unmuteAll(); });
const seqKeys = () => S.ws === "seq" && dlgClosed() && $("#keyspop").hidden;
Keys.bind({ keys: ["ArrowLeft", "ArrowRight"], mod: "alt", group: "Selected track", does: "Sequence: rotate the selected track one step earlier / later: trigs, accents and locks, wrapping at the length. Presses while ⌥ is down are one undo step. The one Alt that is not \"all\": FUNCTION + arrows on the machine", when: seqKeys, run: e => rotateTrack(e.key === "ArrowRight" ? 1 : -1) });
Keys.bind({ keys: ["0"], group: "All", does: "Unmute and unsolo every track", when: () => kbOn(), run: () => unmuteAll() });
Keys.bind({ keys: ["Escape"], group: "Sequence", does: "Unmark the tracks marked for paste", when: () => seqKeys() && S.multi.size > 0 && !genRunOn(), run: () => { S.multi = new Set(); renderRail(); renderLane(); } });
Keys.bind({ keys: ["step"], mod: "cmd", group: "Sequence", does: "Click: every 2nd step from there to the end on (from a trig: off), one undo step" });
Keys.bind({ keys: ["step"], mod: "cmd+shift", group: "Sequence", does: "Click: every 4th step from there to the end" });
Keys.bind({ keys: ["wheel on a step"], group: "Sequence", does: "Move its lock in the lane's parameter, 4 a notch (⇧: 1)" });
Keys.bind({ keys: ["lock lane"], mod: "shift", group: "Sequence", does: "Drag: a ramp, a straight line from the press to the release (one undo step)" });
Keys.bind({ keys: ["track header"], mod: "shift", group: "Sequence", does: "Click: mark the track for paste; ⌘V then pastes into every marked track (one undo step)" });

