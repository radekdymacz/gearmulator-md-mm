"use strict";
/* MD Desk page: the approved mockup (doc/modern-ux/mockup/index.html) running
   on the plug-in's documents. Rendering and gestures are the mockup's; state
   comes from mdDeskModel.js (deriveView) and every edit is a small command to
   the plug-in (Bridge.send).
   The page is classic scripts sharing one global scope, loaded in this order (sync-mdstudio-skin.py SCRIPTS):
     mdDeskApp.js         this file: the UI state S, the gesture slot (Held), commands (cmd), value access,
                          the controls' sync, the track keys
     mdDeskSoundGroups.js the Sound page's tables (data: groups per machine, screens' help, machine notes)
     mdDeskTop.js         the top bar, the LCD (line 2 too), the engine and capabilities, the rail, the transport
     mdDeskSeq.js         Sequence: the grid, the lock lane, the page control
     mdDeskSound.js       Sound: the groups and their rows
     mdDeskEditors.js     the canvas editors (ED): LFO, effects and routing, the synthesis screens
     mdDeskMix.js         Mix: strips, master effects and their editors
     mdDeskSampler.js     Sampler: slots, waveforms, audition, sample load, set-up, chops
     mdDeskSong.js        Song: the arrangement and the chain
     mdDeskPicker.js      the machine picker and the key-style dropdowns
     mdDeskControl.js     Control: MIDI learn and the app modulators (hidden until enabled)
     mdDeskGenUi.js       the GEN and MUTATE bars over mdDeskGen.js
     mdDeskComforts.js    Alt, rotate, every-N fill, double, paste to many, unmute all
     mdDeskSelect.js      a selection of steps: copy, cut, paste, duplicate, clear, a dropped copy
     mdDeskRom.js         firmware, notices, the dialog, the start-up card's and SysEx import's hosts
     mdDeskGestures.js    the pointer handlers; each owns its kind of the gesture slot
     mdDeskRender.js      documents in, render, the click router, the editor's keys; the first render
   Each file only defines at load; what it calls in another file runs after every file is in. */

/* ===== Manual data that is not in the contract ===== */
const SIGNED = new Set(["MLEV", "ILEV", "MBAL", "IBAL", "EQG", "PAN", "LG", "HG", "PG"]);
const FAMC = { TRX: "var(--trx)", EFM: "var(--efm)", E12: "var(--e12)", PI: "var(--pi)", GND: "var(--gnd)", INP: "var(--inp)", MID: "var(--mid)", CTR: "var(--ctr)", SMP: "var(--smp)" };
const FAMN = { TRX: "TRX · analogue model", EFM: "EFM · FM drums", E12: "E12 · 12-bit samples", PI: "P-I · physical model", GND: "GND · tone and noise", INP: "INP · external input", MID: "MID · MIDI out", CTR: "CTR · control", SMP: "ROM / RAM · UW samples" };
/* nameOf (and its FULL table) live in mdDeskModel.js (P6): pure model, no call up into the app. A machine's
   facts (family, sample slot, recorder...) are machineFacts(m, Cat) there, never a test of its name here. */
const SHAPES = ["Triangle", "Saw", "Square", "Linear decay", "Exp decay", "Random"];

/* ===== State (P6): the UI's own store here; the machine's data is the view V (mdDeskModel.js,
   deriveView), a value replaced on every document ===== */
const S = { ws: "seq", sel: 0, lane: "FLTF", page: 0, viewAll: true, follow: false, step: -1, soloSet: new Set(), userMutes: new Set(),
	songSel: 0, bank: 0, songZoom: "fit", smpSlot: "RAM1", chopTrack: null, capture: {}, keepFx: true, plate: "mk1" };
S.ctl = { learn: false, learnT: null, sel: null, selT: null, addT: 1 };
/* MIDI mapping (the CONTROL workspace and LEARN) is hidden until the plug-in's learn document says
   "enabled" (mdmm::midiMappingEnabled, deskHost.h). Off: no tab, no LEARN key, the keys do nothing. */
S.mapping = false;
function applyMapping(on) {
	S.mapping = !!on;
	const tab = document.querySelector('#tabs [data-ws="control"]'), key = document.getElementById("learnkey");
	if (tab) tab.hidden = !on;
	if (key) key.hidden = !on;
	if (!on) { S.ctl.learn = false; S.ctl.learnT = null; document.body.classList.remove("learn"); if (S.ws === "control") S.ws = "seq"; }
}
applyMapping(false);
V = view();	/* the view before the first document (mdDeskModel.js) */

/* Pointer capture can fail for a pointer the browser no longer tracks; the gesture still works. */
/* P7: named apart from the sampler model's capture(n, bins) (the mock waveform, gone in P9), which
   shadowed it (a later function declaration wins), so no drag ever held the pointer */
function grabPointer(el, e) { try { el.setPointerCapture(e.pointerId); } catch (_) { } }

/* ===== Commands ===== */
/* The undo step of the gesture that runs (a drag, a paint, a held control): begun where the pointer goes down
   and ended where it comes up; every edit sent meanwhile belongs to it. What the pointer holds is Held, below. */
const Gesture = (() => {
	let id = 0;
	return { begin() { id = Bridge.gesture(); return id; }, end() { id = 0; }, get id() { return id; } };
})();
/* What the pointer (or a run of keys) holds: one value {kind, ...} in one slot, or null. A handler begins its
   own kind (replacing whatever was held), replaces it as it moves (with) and ends only its own (end). The
   kinds (mdDeskGestures.js): "value" a knob or fader drag, "editor" a curve editor's handle, "lane" the lock
   lane's draw or ramp, "paint" steps, "chop" a chop slice, "l2" an LCD line 2 value, "song" a song pad or row
   dragged, "gv" a GEN or MUTATE value, "bpm" the tempo, "mutePaint" a drag across the M or S keys (mdDeskLive.js); "wheel" a run of wheel notches on one step's lock and
   "rotate" the rotate presses while Alt is down (their undo step g). interacting() (mdDeskRender.js) reads it. */
const Held = (() => {
	let now = null;
	const enders = [];
	return {
		get now() { return now; },
		begin(kind, v = {}) { now = Object.assign({ kind }, v); return now; },
		as(kind) { return now && now.kind === kind ? now : null; },
		/* the held value of that kind with these fields replaced (a new value), or null when another is held */
		with(kind, v) { if (!now || now.kind !== kind) return null; now = Object.assign({}, now, v); return now; },
		/* ends a gesture of that kind; returns its last value, or null. What waited for it (a render held while it
		   ran, mdDeskRender.js) hears of it after its own end handler (onEnd). */
		end(kind) { if (!now || now.kind !== kind) return null; const was = now; now = null; for (const f of enders) f(was); return was; },
		onEnd(f) { enders.push(f); }
	};
})();
/* Features that last over several edits (a GEN run, a MUTATE trial) hear of every edit sent, and end
   themselves when it is not theirs: cmd knows no feature. f(op, args) */
const editListeners = [];
function onEditSent(f) { editListeners.push(f); }
/* A command to the plug-in. Its undo step is explicit: args.g, the gesture it belongs to (a run's or a
   trial's, or a gesture of several edits); without one, the gesture that runs (Gesture), if any.
   optimistic: the [path, value] writes into the view the gesture shows at once (skins/shared/deskOverlay.js,
   Overlay), on the document the command edits (docOf); they are kept over every new derivation until its
   result. */
function cmd(op, args = {}, key, optimistic, onDone, merge) {
	const msg = Object.assign({ op }, args);
	if (msg.g == null) { delete msg.g; if (Gesture.id) msg.g = Gesture.id; }
	if (op === "undo" || op === "redo" || docOf(op, args)) for (const f of editListeners) f(op, args);
	let answered = false;	/* a host may answer at once, inside send */
	const id = Bridge.send(msg, { key, merge, onResult: r => { answered = true; onResult(r); if (onDone) onDone(r); } });
	if (optimistic && optimistic.length) { if (!answered) Overlay.add(id, optimistic, docOf(op, args)); V = view(); }
	tx();
	return id;
}
const DELETE = Overlay.DELETE;
function onResult(r) {
	/* its optimistic edits leave the overlay; a refused one is shown as the documents have it */
	if (Overlay.answered(r.id) && !r.ok) scheduleRender();
	if (!r.ok && r.errors && r.errors.length) showLastError(r.errors);	/* once: the error line, not a toast too */
	else if (r.note) toast(r.note);
}
/* The kit parameter index 0-23. With the group (syn, fx, rt) it is looked up in that page only: some
   machines have a synthesis parameter with a routing parameter's name (DIST), and the Mix DIST box must
   not move the machine's own DIST (P4, found in the plug-in). */
function pidx(t, n, g) {
	const a = slots(V.tracks[t].m, Cat), base = { syn: 0, fx: 8, rt: 16 }[g];
	if (base == null) return a.indexOf(n);
	const k = a.slice(base, base + 8).indexOf(n); return k < 0 ? -1 : base + k;
}

/* ===== Model helpers (view, optimistic) ===== */
function lk(t, p) { return t + ":" + p; }
/* g: the gesture the lock belongs to (none: the one that runs) */
function setLock(t, p, s, v, quiet, g) {
	const k = lk(t, p);
	if (!V.locks.has(k) && V.locks.size >= 64) { if (!quiet) toast("All 64 locked parameters are in use. Clear one before you lock a new parameter."); return false; }
	const i = pidx(t, p);
	if (i >= 0) cmd("lock", { p: V.pat, t, i, s, v, g }, "lock:" + t + ":" + i + ":" + s, [[["locks", k, s], v]]);
	return true;
}
function eraseLock(t, p, s) {
	const i = pidx(t, p);
	if (i >= 0) cmd("lock", { p: V.pat, t, i, s, v: null }, "lock:" + t + ":" + i + ":" + s, [[["locks", lk(t, p), s], DELETE]]);
}
function stepLocked(t, s) { for (const [k, m] of V.locks) if (+k.split(":")[0] === t && m.has(s)) return true; return false; }
function trackLocks(t) { return [...V.locks.keys()].filter(k => +k.split(":")[0] === t).map(k => k.split(":")[1]); }
/* the view writes that clear a step's locks (a trig removed takes its locks with it) */
function clearStep(t, s) { return [...V.locks].filter(([k, m]) => +k.split(":")[0] === t && m.has(s)).map(([k]) => [["locks", k, s], DELETE]); }
function params(t) { const p = pages(V.tracks[t].m, Cat); return [...names(p.s), ...names(p.e), ...names(p.r)]; }
function grp(t, p) { const x = V.tracks[t]; return p in x.syn ? x.syn : p in x.fx ? x.fx : x.rt; }
function audible(t) { const any = V.tracks.some(x => x.solo); return any ? V.tracks[t].solo : !V.tracks[t].mute; }
function clamp(v, a = 0, b = 127) { return Math.max(a, Math.min(b, v)); }
const $ = q => document.querySelector(q), $$ = q => [...document.querySelectorAll(q)];
let toastT; function toast(m) { const e = $("#toast"); e.textContent = m; e.classList.add("on"); clearTimeout(toastT); toastT = setTimeout(() => e.classList.remove("on"), 2800); }
/* the error line under the header (over the page, never in its flow): 6 s, or until its × is clicked */
function showLastError(errors) {
	const e = $("#errline"); if (!e) return;
	const hide = () => { clearTimeout(e._t); e.hidden = true; };
	e.innerHTML = `<span></span><button class="errx" type="button" aria-label="Dismiss" title="Dismiss">×</button>`;
	e.firstChild.textContent = errors.join(" · "); e.lastChild.onclick = ev => { ev.stopPropagation(); hide(); };
	e.hidden = false; clearTimeout(e._t); e._t = setTimeout(hide, 6000);
}
const kitName = k => "K" + String(k + 1).padStart(2, "0") + " " + (V.kitNames[k] || "KIT " + String(k + 1).padStart(2, "0"));
/* TX LED: lit while the desk has an edit on the wire (machine.desk.tx), and briefly for every command. */
let txT; function tx() { const l = $("#txled"); if (!l) return; l.classList.add("on"); clearTimeout(txT); txT = setTimeout(syncTx, 90); }
/* P7: the sync slot on LCD line 2 (fixed width): SEND while an edit is on its way, SYNC when the machine shows them all */
function syncTx() {
	const l = $("#txled"), t = $("#synct"), f = $("#syncf"); if (!l) return;
	/* the machine follows an external clock (in a DAW: the host's tempo and transport, set by the plug-in) */
	const host = Docs.global?.control?.tempoIn === "external";
	/* the slot by priority: the background read of the library (READ n/m and its bar), then SEND, HOST, SYNC */
	const left = (machineState().desk || {}).loading || 0, done = Object.keys(Docs.patterns).length + Object.keys(Docs.kits).length + Object.keys(Docs.songs).length;
	const reading = left > 0 && V.input;
	if (f) { f.classList.toggle("read", reading); f.style.setProperty("--rf", reading ? (done / (done + left)).toFixed(3) : 0); }
	l.classList.toggle("on", V.tx); if (t) t.textContent = reading ? `${done}/${done + left}` : V.tx ? "Send" : host ? "Host" : "Sync";
	if (f && reading) f.title = "Reading the machine's patterns, kits and songs in the background: edit away, the library fills in";
	else if (f) f.title = V.tx ? "Sending edits to the machine" : (host ? "Follows the host's tempo and transport (GLOBAL: TEMPO IN external). " : "") + "In step with the machine" + (V.roundTrip >= 0 ? " · last round trip " + Math.round(V.roundTrip) + " ms" : "");
}
function setKitState(st) {
	const s = $("#save"); if (!s) return;
	s.classList.toggle("dirty", st === "edited");
	s.lastElementChild.textContent = st === "edited" ? "edited" : st === "clean" ? "saved" : "?";
	s.title = (st === "edited" ? "Kit edits are not saved on the machine. They are kept in the DAW project." : st === "clean" ? "The kit matches its saved slot on the machine." : "Not known yet")
		+ (V.kitSource === "memory" ? " Read from the machine's memory." : "");
}
function syncUndoCounts() { const u = $("#undon"), r = $("#redon"); if (u) u.textContent = V.undoCount || ""; if (r) r.textContent = V.redoCount || ""; }
/* Commands at the gesture (P6): a control that changed a kit value sends that value; nothing is
   compared with an earlier copy. */
function sendParam(t, g, n, v) {
	const i = pidx(t, n, g); if (i < 0) return;
	cmd("param", { k: V.kit, t, i, v }, "param:" + t + ":" + i, [[["tracks", t, g, n], v]]);
}
/* An LFO field's new view value v (TRCK a track, PARAM a parameter name, UPDTE one of the catalogue's
   lfoUpdates). SPD DEPTH SHMIX are kit parameters (the catalogue's lfoParams), the rest lfo fields. */
function sendLfo(t, n, v) {
	const w = [[["tracks", t, "lfo", n], v]], E = Enums(), pi = E.lfoParams[n];
	if (pi != null) { const rn = lfoRtName(n, t); if (rn && rn in V.tracks[t].rt) w.push([["tracks", t, "rt", rn], v]); cmd("param", { k: V.kit, t, i: pi, v }, "param:" + t + ":" + pi, w); return; }
	const field = lfoField(n, E); if (!field) return;
	const c = n === "PARAM" ? slots(V.tracks[V.tracks[t].lfo.TRCK].m, Cat).indexOf(v) : n === "UPDTE" ? E.lfoUpdates.indexOf(v) : v;
	if (c >= 0) cmd("lfo", { k: V.kit, t, field, v: c }, undefined, w);
}
/* SPD, DEPTH, SHMIX's names on track t's routing page (LFOS, LFOD, LFOM), from the catalogue's lfoParams. */
function lfoRtName(n, t) { const pi = Enums().lfoParams[n], tr = V.tracks[t]; return pi != null && tr ? slots(tr.m, Cat)[pi] || null : null; }
function sendGroup(t, kind, target) { cmd("group", { k: V.kit, t, kind, target }, undefined, [[["tracks", t, kind === "mute" ? "muteGroup" : "trigGroup"], target]]); }
function sendMfx(id, n, v) { const i = MFXD[id].k.indexOf(n), fx = mfxName(id); if (i >= 0 && fx) cmd("masterFx", { k: V.kit, fx, i, v }, "mfx:" + id + ":" + i, [[["mfx", id, "v", n], v]]); }
/* A curve editor's handle moved: its drag gives the values it moves ({name: value}); the editor's
   "to" says where they go (a track's page, or a master effect). */
function sendEditor(c, vals) {
	const to = ED[c.dataset.ed].to?.(c); if (!to || !vals) return;
	/* the LFO's SPD DEPTH SHMIX are its routing page's LFOS LFOD LFOM: those are what Alt moves on every track */
	if (to.g === "lfo") {
		if (tweakEditor({ t: to.t, g: "rt" }, Object.fromEntries(Object.entries(vals).map(([n, v]) => [lfoRtName(n, to.t), v]).filter(([n]) => n)))) return;
		for (const [n, v] of Object.entries(vals)) sendLfo(to.t, n, v);
		return;
	}
	if (tweakEditor(to, vals)) return;	/* Alt held: Control All (mdDeskLive.js) */
	for (const [n, v] of Object.entries(vals)) to.f ? sendMfx(to.f, n, v) : sendParam(to.t, to.g, n, v);
}
/* A control (data-g, data-n, data-t, data-f) set to v, sent. */
function sendControl(el, v) {
	const d = el.dataset, t = d.t != null ? +d.t : S.sel;
	if (d.g === "syn" || d.g === "fx" || d.g === "rt") sendParam(t, d.g, d.n, v);
	else if (d.g === "lfo") sendLfo(t, d.n, v);
	else if (d.g === "mfx") sendMfx(d.f, d.n, v);
}
function goPattern(p) {
	p = (p + 128) % 128;
	if (p === V.pat && V.queued == null) return;
	cmd("select", { p });
}
function saveKit() { cmd("saveKit"); }
/* The plug-in asks before a command would lose something (P6: one protocol, the plug-in's): its
   message, its alternatives, its confirm button, Cancel. Confirm sends the command again with force; an
   alternative ("Save and load") sends its first commands, then the same. The page decides and words no
   question of its own. */
function onAsk(m) {
	const send = c => { const { op, id, ...args } = c; cmd(op, args); };
	const again = () => send(Object.assign({}, m.command, { force: true }));
	const alts = (m.alternatives || []).map(a => [a.label, "cream", () => { (a.first || []).forEach(send); again(); }]);
	ask(m.message || "Go on?", [...alts, [m.confirm || "Go on", "danger", again], ["Cancel", "", () => { }]]);
}
/* the question dialog: queued, never replacing what it shows (Dlg, skins/shared/deskModal.js); item: {notice, cancel, key} */
function ask(html, btns, item = {}) { Dlg.show(Object.assign(item, { draw: () => drawAsk(html, btns) })); }
function drawAsk(html, btns) { const d = $("#dlg"); d.dataset.first = ""; d.innerHTML = `<div class="dlgbox" role="alertdialog" aria-modal="true"><p>${html}</p><div class="btnrow">${btns.map(([t, c], i) => `<button class="${c}" data-dlg="${i}">${t}</button>`).join("")}</div></div>`; d.hidden = false; d._btns = btns; d.querySelector(".btnrow button:last-child")?.focus(); }

/* Value access for every control: data-g group, data-n name, data-t track, data-f master fx */
function ref(el) {
	const d = el.dataset, t = d.t != null ? +d.t : S.sel, tr = V.tracks[t];
	switch (d.g) {
	case "syn": return [tr.syn, d.n]; case "fx": return [tr.fx, d.n]; case "rt": return [tr.rt, d.n]; case "lfo": return [tr.lfo, d.n];
	case "mfx": return [V.mfx[d.f].v, d.n]; case "src": return [Mods.source(d.src), d.n]; case "link": return [Mods.doc.links[+d.li], d.n];
	}
}
const getV = el => { const [o, n] = ref(el); return o[n]; };
function setV(el, v) {
	const [o, n] = ref(el); v = clamp(Math.round(v), 0, n === "depth" ? 100 : 127); if (o[n] === v) return;
	/* the page's own modulator setup (Mods.doc): a new doc, sent whole */
	if (el.dataset.g === "src") { Mods.setSource(el.dataset.src, { [n]: v }); sendMods(); }
	else if (el.dataset.g === "link") { Mods.setLink(+el.dataset.li, { [n]: v }); sendMods(); }
	else sendControl(el, v);
	syncControls(); redraw();
}
/* ===== Controls: sync in place, never rebuild while dragging ===== */
function syncControls() {
	$$("#main [data-g]").forEach(el => {
		const v = getV(el); if (v == null) { el.style.setProperty("--f", "0%"); const b0 = el.querySelector("b"); if (b0) b0.textContent = "—"; return; }
		const f = v / 127 * 100 + "%"; el.style.setProperty("--f", f); el.setAttribute("aria-valuenow", v);
		if (el.classList.contains("pc") && SIGNED.has(el.dataset.n)) { const q = v / 127 * 100; el.classList.add("bip"); el.style.setProperty("--pl", Math.min(q, 50.4) + "%"); el.style.setProperty("--pw", v === 64 ? "0%" : Math.max(3, Math.abs(q - 50.4)) + "%"); }
		const b = el.querySelector("b"); if (b) b.textContent = SIGNED.has(el.dataset.n) && el.dataset.g !== "mfx" ? (v - 64 > 0 ? "+" : "") + (v - 64) : v;
		const u = el.querySelector(".pu"); if (u) { const [txt, tip] = UNITS[u.dataset.unit](v); u.textContent = txt; el.title = tip; }
		if (["syn", "fx", "rt", "lfo"].includes(el.dataset.g)) {
			const tt = el.dataset.t != null ? +el.dataset.t : S.sel, idx = el.dataset.g === "lfo" ? Enums().lfoParams[el.dataset.n] ?? -1 : pidx(tt, el.dataset.n, el.dataset.g);
			const mp = (Docs.learn?.mappings || []).filter(m => m.t === tt && m.i === idx);
			el.classList.toggle("mapped", mp.length > 0);
			el.classList.toggle("learnt", !!S.ctl.learnT && S.ctl.learnT.t === tt && S.ctl.learnT.p === el.dataset.n);
			if (mp.length) el.title = "Mapped: " + mp.map(m => "CC " + m.cc).join(", ");
		}
		/* a lock on LFOS, LFOD or LFOM shows on the LFO section's SPD, DEPTH, SHMIX (the same kit parameters) */
		if (el.classList.contains("pc") && el.dataset.g !== "mfx" && el.dataset.g !== "src" && el.dataset.g !== "link") el.classList.toggle("lk", V.locks.has(lk(el.dataset.t != null ? +el.dataset.t : S.sel, el.dataset.g === "lfo" ? lfoRtName(el.dataset.n, el.dataset.t != null ? +el.dataset.t : S.sel) : el.dataset.n)));
	});
	$$("#main [data-show]").forEach(el => el.textContent = V.tracks[+el.dataset.show].rt.VOL ?? "—");
}
/* ===== Copy / clear / paste (the hardware's COPY CLEAR PASTE, per workspace; clipboard in the plug-in) ===== */
function secAction(kind) {
	if (S.ws === "seq") {
		/* a selection of steps (mdDeskSelect.js) is what copy and clear take, and where paste puts the clipboard */
		if (S.stepSel && (kind !== "paste" || !S.multi.size) && (kind === "copy" ? selCopy() : kind === "clear" ? selClear() : selPaste())) return;
		const [a, b] = vis(); const range = { p: V.pat, t: S.sel, from: a, to: Math.min(b, V.len) };
		if (kind === "copy") { S.clipBlock = blockOf({ t: S.sel, n: 1, from: a, to: range.to }); cmd("copySteps", range); }
		else if (kind === "clear") cmd("clearSteps", range); else if (S.multi.size) pasteToMany(S.stepSel ? S.stepSel.from : a); else cmd("pasteSteps", { p: V.pat, t: S.sel, from: a }); return; }
	if (S.ws === "sound") { const a = { k: V.kit, t: S.sel }; cmd(kind === "copy" ? "copySound" : kind === "clear" ? "clearSound" : "pasteSound", a); return; }
	if (S.ws === "song") { const i = S.songSel;
		if (kind === "copy") songCmd("copyRow", { i }); else if (kind === "clear") songAction("del"); else { const at = V.song[i]?.type === "end" ? i : i + 1; songCmd("pasteRow", { i: at }); S.songSel = at; } return; }
	toast("Copy, clear and paste work in Sequence, Sound and Song.");
}

/* ===== The track keys (rail and Mix): mute, solo, select ===== */
/* the machine's mute of track i (shown at once: the machine's mutes, and the track's while no solo holds) */
function setMute(i, on) { const w = [[["mutes", i], on]]; if (!S.soloSet.size) w.push([["tracks", i, "mute"], on]); cmd("mute", { t: i, on }, undefined, w); }
/* Solo is the page's idea (mdDeskModel.js, soloTo / muteTo / soloWrites, as the MM page): it mutes every other
   track on the machine; the user's mutes are the machine's when the first solo begins, M edits only them
   during a solo, and the last solo let go gives them back to the machine. */
function applySolo() { for (const [i, on] of soloWrites(S, V.mutes, soloKeep())) setMute(i, on); }
/* the tracks a solo leaves alone: the RAM recorders (their mute is the sampler's capture and freeze) */
function soloKeep() { return new Set(V.tracks.map((t, i) => machineFacts(t.m, Cat).recorder ? i : -1).filter(i => i >= 0)); }
/* the solo set becomes next (a new Set: solo is UI state the view reads, never mutated in place) */
function setSolo(next) { Object.assign(S, soloTo(S, V.mutes, next)); V = view(); applySolo(); }
/* M: track i muted or not, as the user means it (during a solo only the page's record; muteSet, mdDeskLive.js, is the gestures') */
function userMute(i, on) { const u = muteTo(S, i, on); if (!u) { setMute(i, on); return; } S.userMutes = u; V = view(); if (soloKeep().has(i)) setMute(i, on); }
function select(i) { S.sel = i; if (!params(i).includes(S.lane)) S.lane = params(i).includes("FLTF") ? "FLTF" : params(i)[0] || "FLTF"; render(); }
function refreshAudible() {
	$$(".th").forEach(h => { const i = +h.dataset.sel; h.classList.toggle("off", !audible(i)); h.querySelector(".m").setAttribute("aria-pressed", V.tracks[i].mute); h.querySelector(".s").setAttribute("aria-pressed", V.tracks[i].solo); });
	$$(".r[data-row],.mr[data-row]").forEach(r => r.classList.toggle("off", !audible(+r.dataset.row)));
	const allOn = $("#allon"); if (allOn) allOn.disabled = !V.tracks.some(t => t.mute || t.solo);
	$$(".strip").forEach(s => { const i = +s.dataset.sel; s.style.opacity = audible(i) ? "" : ".5"; s.querySelector(".m").setAttribute("aria-pressed", V.tracks[i].mute); s.querySelector(".s").setAttribute("aria-pressed", V.tracks[i].solo); });
}
/* the clicks on them (the router's first and third, mdDeskRender.js CLICKS): true when the click was theirs */
function clickTrackKeys(e) {
	const mu = e.target.closest("[data-mute]"), so = e.target.closest("[data-solo]");
	if (!mu && !so) return false;
	const i = +(mu || so).dataset[mu ? "mute" : "solo"], t = V.tracks[i];
	if (mu) userMute(i, !t.mute);
	else { const next = new Set(S.soloSet); next.has(i) ? next.delete(i) : next.add(i); setSolo(next); }
	refreshAudible(); return true;
}
function clickSelect(e) {
	const sel = e.target.closest("[data-sel]"); if (!sel || e.target.closest("button,select,.pc,.fader")) return false;
	if (e.shiftKey && S.ws === "seq" && sel.classList.contains("th")) { multiToggle(+sel.dataset.sel); return true; }
	if (S.multi.size) S.multi = new Set();
	select(+sel.dataset.sel); return true;
}
