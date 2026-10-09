"use strict";
/* The page's key map (P5): every shortcut is an entry here. The ? overlay is generated from it, so it
   lists what the keys really do. An entry with run() is dispatched by the one handler below (in order,
   the first match wins); an entry without run() is handled next to its own code (a panel, a focused
   control) and only described here. Nothing here knows the DOM of the workspaces. The dispatcher (Keys)
   is skins/shared/deskKeys.js, loaded before this file and shared with the Monomachine Editor. */

/* ===== ? : the keyboard view (K-view, shared/deskKeyView.js) =====
   The map's rules: plain keys play (the home row, Z / X octave, C / V velocity, Space); a plain letter off the
   piano row acts on the selected track (R, M, T; ↑ / ↓ pick it); ⌥ means all (⌥R, ⌥M, ⌥Delete, ⌥-drag, ⌥-click; FN
   on the screen is ⌥). Two ⌥s are not "all", as on the machine: ⌥←/→ rotate (FUNCTION + arrows) and ⌥Space record
   (⌥ + play). ⌘ (Ctrl off a Mac) is the desktop's: ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V ⌘X ⌘D ⌘A, and on steps the selection. The view draws
   a keyboard from this map with what each key does on the page shown (or every page), the mouse's tricks per area, the
   tips, and every entry by group; holding or clicking a modifier shows its layer. */
const KEY_GROUPS = ["Playing", "Selected track", "All", "Transport", "Workspaces", "Sequence", "Song", "Values", "Anywhere", "Kit library, pattern chooser", "Help"];
const WS_NAMES = { seq: "Sequence", sound: "Sound", mix: "Mix", sampler: "Sampler", song: "Song", control: "Control" };
let keyView = null;
function drawKeys() {
	const pop = $("#keyspop"); if (!pop) return;
	if (!keyView) keyView = KeyView.mount(pop, { entries: () => Keys.list(), page: () => S.ws, pageName: () => WS_NAMES[S.ws] || "This page", mapping: () => S.mapping,
		mac: () => Modifiers.mac, groups: KEY_GROUPS, note: () => Modifiers.say("? or Esc closes. Click a key for what it does.") });
	pop.classList.add("kview");
	keyView.draw();
	pop.hidden = false;
	const r = $(".lcdpanel").getBoundingClientRect(), top = Math.max(16, r.bottom + 8);
	pop.style.top = (top + scrollY) + "px"; pop.style.maxHeight = Math.max(240, innerHeight - top - 12) + "px"; pop.style.left = Math.max(16, (document.documentElement.clientWidth - pop.offsetWidth) / 2 + scrollX) + "px";
}
function toggleKeys(on) { const pop = $("#keyspop"); if (!pop) return; if (on ?? pop.hidden) drawKeys(); else { pop.hidden = true; keyView?.reset(); } }
Keys.bind({ id: "keys-help", short: "This view", scope: "any", keys: ["?"], group: "Help", does: "The keyboard view: what every key and gesture does (this)", modal: "keyspop", run: () => toggleKeys() });
Keys.bind({ id: "keys-help-close", short: "Close", scope: "any", keys: ["Escape"], group: "Help", does: "Close the keyboard view", when: () => !$("#keyspop").hidden, run: () => toggleKeys(false) });
document.addEventListener("click", e => { const pop = $("#keyspop"); if (!pop || pop.hidden) return; if (e.target.closest("[data-keysx]") || !pop.contains(e.target)) { pop.hidden = true; keyView?.reset(); } }, true);
/* the page zoom keys (shared/deskZoom.js takes them before the map), described */
Keys.bind({ id: "zoom-out", short: "Zoom −", scope: "any", keys: ["-"], mod: "cmd", group: "Anywhere", does: "Page zoom: smaller (also in the editor's menu)" });
Keys.bind({ id: "zoom-in", short: "Zoom +", scope: "any", keys: ["="], mod: "cmd", group: "Anywhere", does: "Page zoom: larger (⌘+ too)" });
Keys.bind({ id: "zoom-reset", short: "Zoom 100 %", scope: "any", keys: ["0"], mod: "cmd", group: "Anywhere", does: "Page zoom: back to 100 %" });
/* tips: the view's short list (a tip is an entry without keys) */
[
	["tip-hold-alt", "Hold ⌥ (or click FN in the top bar): the keys and buttons it changes say what they do then (Clr becomes All)."],
	["tip-fn", "No ⌥ to hand (a touch screen, a DAW or desktop that takes Alt)? Click FN: the next click, drag or key gets ⌥; double-click it to keep it on."],
	["tip-step-menu", "Right-click a step: everything a step can do, with its key (Ctrl-click on a Mac)."],
	["tip-select", "⌘-click or ⌘-drag steps to select them; the LCD's Copy, Clr and Paste then act on the selection."],
	["tip-undo", "A drag, a paint or a run of rotates is one undo step: ⌘Z takes it back whole."],
	["tip-play", "A to L play the selected track from any workspace; Z / X move the octave, C / V the velocity."],
	["tip-reset", "Double-click a value: back to its default. ⇧ while dragging: fine."]
].forEach(([id, does]) => Keys.bind({ id, scope: "any", tip: true, keys: [], group: "Tips", does }));

/* ===== The pointer's gestures that no key shares, described (K0, DESIGN-keymap.md): each is handled next to its
   own code (named after it); here only so the ? overlay, the guide and the keyboard view list every one. area: where
   on the page (the "Mouse tricks" of the keyboard view). ===== */
[
	/* Sequence (mdDeskGestures.js, mdDeskSeq.js) */
	["step-paint", "seq", "Steps", ["step"], "", "Sequence", "Click: trig on / off. Drag: every step crossed becomes what the first became (one undo step)"],
	["lane-draw", "seq", "Lock lane", ["lock lane"], "", "Sequence", "Drag over the bars: lock the lane's parameter on each step with a trig"],
	["page-key", "seq sampler", "Steps", ["Page key"], "shift", "Sequence", "Click: the previous page (a plain click: the next)"],
	/* the values (mdDeskGestures.js) */
	["value-drag", "sound mix sampler control", "Values", ["value"], "", "Values", "Drag up / down or sideways: change it, a value a pixel"],
	["value-fine", "sound mix sampler control", "Values", ["value"], "shift", "Values", "Drag: fine, a quarter as fast"],
	["value-wheel", "sound mix sampler control", "Values", ["wheel on a value"], "", "Values", "One step a notch (⇧: 10)"],
	["value-reset", "sound mix sampler control", "Values", ["double-click a value"], "", "Values", "Back to its default (64; VOL 100)"],
	/* the top bar and the LCD (mdDeskGestures.js, mdDeskTop.js, mdDeskLive.js) */
	["tempo-drag", "any", "Top bar", ["tempo"], "", "Transport", "Drag up or down: the tempo, half a BPM a pixel (⇧: a tenth)"],
	["lcd-value", "any", "LCD", ["LCD value"], "", "Anywhere", "Click: its next value; ⇧-click the previous one. LEN, SPD, SONG: drag up or down to step through them"],
	["lcd-len-inner", "any", "LCD", ["LEN"], "alt", "Anywhere", "Click or drag: the pattern's inner length instead of its total"],
	["header-menu", "any", "Top bar", ["right-click the header"], "", "Anywhere", "The editor's menu: zoom and window size, updates, the log folder, Developer"],
	/* the GEN bar (mdDeskGenUi.js) */
	["gen-value", "seq", "GEN bar", ["GEN value"], "", "Sequence", "Click: +1; ⇧-click: −1; drag up or down, or the wheel (⇧: 10)"],
	["rkey", "any", "GEN bar", ["R key"], "", "Selected track", "Click: randomise the selected track, as R"],
	/* tracks (mdDeskApp.js) */
	["ms-click", "any", "Tracks", ["M / S key"], "", "Selected track", "Click: mute / solo that track"],
	/* Song (mdDeskSong.js, mdDeskGestures.js) */
	["song-steppers", "song", "Song", ["row value − / +"], "shift", "Song", "Click: ten at a time"],
	["song-drag", "song", "Song", ["drag a pattern pad or a row"], "", "Song", "Onto the arrangement: a new row there, or the row moved"],
	/* Sampler (mdDeskSampler.js, mdDeskGestures.js) */
	["chop-start", "sampler", "Sampler", ["drag a chop step"], "", "Sampler", "Up or down: where its slice starts"],
	["chop-reverse", "sampler", "Sampler", ["chop step"], "alt", "Sampler", "Click: play its slice backwards"],
	["chop-retrig", "sampler", "Sampler", ["chop step"], "shift", "Sampler", "Click: a retrig on it"],
	/* the kit library and the pattern chooser (mdDeskLibrary.js) */
	["lib-slot-click", "library", "Library", ["kit slot"], "", "Kit library, pattern chooser", "Click: load it (⌥-click: only select it). Double-click: rename"],
	["lib-pattern-now", "library", "Library", ["pattern slot"], "shift", "Kit library, pattern chooser", "Click: go there at once, not at the end of the pattern"],
	["lib-slot-drag", "library", "Library", ["drag a slot onto another"], "", "Kit library, pattern chooser", "Copy it there"]
].forEach(([id, scope, area, keys, mod, group, does]) => Keys.bind({ id, scope, area, keys, mod, group, does }));
