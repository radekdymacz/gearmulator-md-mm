"use strict";
/* MD Desk page: the approved mockup (doc/modern-ux/mockup/index.html) running
   on the plug-in's documents. Rendering and gestures are the mockup's; state
   comes from mdDeskModel.js (deriveView) and every edit is a small command to
   the plug-in (Bridge.send). What is still example data is marked MOCK. */

/* ===== Manual data that is not in the contract ===== */
const SIGNED = new Set(["MLEV", "ILEV", "MBAL", "IBAL", "EQG", "PAN", "LG", "HG", "PG"]);
const FAMC = { TRX: "var(--trx)", EFM: "var(--efm)", E12: "var(--e12)", PI: "var(--pi)", GND: "var(--gnd)", INP: "var(--inp)", MID: "var(--mid)", CTR: "var(--ctr)", SMP: "var(--smp)" };
const FAMN = { TRX: "TRX · analogue model", EFM: "EFM · FM drums", E12: "E12 · 12-bit samples", PI: "P-I · physical model", GND: "GND · tone and noise", INP: "INP · external input", MID: "MID · MIDI out", CTR: "CTR · control", SMP: "ROM / RAM · UW samples" };
const ABOUT = {
	INP: "Processes external input A or B. It needs one trig to become active.",
	MID: "No audio. Sends NOTE plus two chord notes (N2, N3), LEN, VEL, PB, MW, AT and 5 CCs on its own MIDI channel.",
	"CTR-AL": "No audio. Moves the same parameter on all 16 tracks at once, and it can be locked per step.",
	"CTR-8P": "No audio. Holds 8 shortcuts to parameters on any tracks. It can change them without playing a sound (a trigless trig).",
	CTRM: "No audio. The only way to lock and sequence this master effect.",
	"RAM-R": "Records on its trig. MLEV/MBAL = the machine's own main out (resample), ILEV/IBAL = inputs A/B. LEN up to 2 bars. RAM is lost at power-off.",
	"RAM-P": "Plays what the matching RAM recorder captured. Lock STRT per step to chop. END below STRT plays it reversed.",
	ROM: "Plays a sample kept in ROM. ROM-25 to ROM-48 are made for loops (STRT and END are linear).",
	"GND-EMPTY": "No machine on this track." };
function about(m) { const f = famKey(m); if (/^CTR-(RE|GB|EQ|DX)/.test(m)) return ABOUT.CTRM; return ABOUT[m] || ABOUT[m.slice(0, 5)] || ABOUT[f] || ""; }
/* nameOf (and its FULL table) live in mdDeskModel.js (P6): pure model, no call up into the app. */
const isSampler = m => /^(ROM|RAM-P)/.test(m), isRec = m => /^RAM-R/.test(m);
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
let gesture = 0;	// non-zero while a drag runs: one undo step
/* A command to the plug-in. optimistic: the [path, value] writes into the view the gesture shows at
   once (mdDeskModel.js, Overlay), on the document the command edits (docOf); they are kept over
   every new derivation until its result. */
function cmd(op, args = {}, key, optimistic, onDone, merge) {
	const msg = Object.assign({ op }, args);
	if (gesture) msg.g = gesture;
	/* any other edit (and undo, redo) closes a mutation trial's gesture: the trial ends (DESIGN-generators.md §4.6) */
	const edit = op === "undo" || op === "redo" || docOf(op, args);
	if (S.mut && S.mut.trial && !(op === "params" && args.g === S.mut.trial.g) && edit) S.mut.trial = null;
	/* the same for a GEN run: its own steps edits carry its gesture, anything else ends it */
	if (S.gen && S.gen.run && !(op === "steps" && args.g === S.gen.run.g) && edit) S.gen.run = null;
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
	if (!r.ok && r.errors && r.errors.length) { toast(r.errors[0]); showLastError(r.errors); }
	else if (r.note) toast(r.note);
}
/* The kit parameter index 0-23. With the group (syn, fx, rt) it is looked up in that page only: some
   machines have a synthesis parameter with a routing parameter's name (DIST), and the Mix DIST box must
   not move the machine's own DIST (P4, found in the plug-in). */
function pidx(t, n, g) {
	const a = slots(V.tracks[t].m), base = { syn: 0, fx: 8, rt: 16 }[g];
	if (base == null) return a.indexOf(n);
	const k = a.slice(base, base + 8).indexOf(n); return k < 0 ? -1 : base + k;
}

/* ===== Model helpers (view, optimistic) ===== */
function lk(t, p) { return t + ":" + p; }
function setLock(t, p, s, v, quiet) {
	const k = lk(t, p);
	if (!V.locks.has(k) && V.locks.size >= 64) { if (!quiet) toast("All 64 locked parameters are in use. Clear one before you lock a new parameter."); return false; }
	const i = pidx(t, p);
	if (i >= 0) cmd("lock", { p: V.pat, t, i, s, v }, "lock:" + t + ":" + i + ":" + s, [[["locks", k, s], v]]);
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
function params(t) { const p = pages(V.tracks[t].m); return [...names(p.s), ...names(p.e), ...names(p.r)]; }
function grp(t, p) { const x = V.tracks[t]; return p in x.syn ? x.syn : p in x.fx ? x.fx : x.rt; }
function audible(t) { const any = V.tracks.some(x => x.solo); return any ? V.tracks[t].solo : !V.tracks[t].mute; }
function clamp(v, a = 0, b = 127) { return Math.max(a, Math.min(b, v)); }
const $ = q => document.querySelector(q), $$ = q => [...document.querySelectorAll(q)];
let toastT; function toast(m) { const e = $("#toast"); e.textContent = m; e.classList.add("on"); clearTimeout(toastT); toastT = setTimeout(() => e.classList.remove("on"), 2800); }
function showLastError(errors) { const e = $("#errline"); if (!e) return; e.textContent = errors.join(" · "); e.hidden = false; clearTimeout(e._t); e._t = setTimeout(() => { e.hidden = true; }, 6000); }
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
	const c = n === "PARAM" ? slots(V.tracks[V.tracks[t].lfo.TRCK].m).indexOf(v) : n === "UPDTE" ? E.lfoUpdates.indexOf(v) : v;
	if (c >= 0) cmd("lfo", { k: V.kit, t, field, v: c }, undefined, w);
}
/* SPD, DEPTH, SHMIX's names on track t's routing page (LFOS, LFOD, LFOM), from the catalogue's lfoParams. */
function lfoRtName(n, t) { const pi = Enums().lfoParams[n], tr = V.tracks[t]; return pi != null && tr ? slots(tr.m)[pi] || null : null; }
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
function ask(html, btns) { const d = $("#dlg"); d.innerHTML = `<div class="dlgbox" role="alertdialog" aria-modal="true"><p>${html}</p><div class="btnrow">${btns.map(([t, c], i) => `<button class="${c}" data-dlg="${i}">${t}</button>`).join("")}</div></div>`; d.hidden = false; d._btns = btns; d.querySelector(".btnrow button:last-child")?.focus(); }

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

/* ===== Top bar ===== */
let lastQueued = null;
function renderTop() {
	$$("#tabs button").forEach(b => b.setAttribute("aria-selected", b.dataset.ws === S.ws));
	const lkk = $("#learnkey"); if (lkk) { lkk.setAttribute("aria-pressed", S.ctl.learn); lkk.classList.toggle("on", S.ctl.learn); }
	const pk = $("#platekey"); if (pk) pk.querySelector("span").textContent = S.plate === "mk2" ? "MKII" : "MKI";
	const n = V.locks.size, m = $("#meter"); $("#lockn").textContent = String(n).padStart(2, "0") + "/64"; m.className = "f meter" + (n >= 64 ? " full" : n >= 52 ? " warn" : ""); syncLockBudget();
	$("#bpm").textContent = (+V.bpm).toFixed(1);
	/* Mockup v49: only the target's name, blinking while it waits for the pattern end; one short
	   flash when the machine really switches (the desk clears the queue at the playhead wrap). */
	$("#pat").textContent = patName(V.queued ?? V.pat);
	$("#pat").parentElement.classList.toggle("queued", V.queued != null);
	if (lastQueued != null && V.queued == null && V.pat === lastQueued) { const pf = $(".patf"); if (pf) { pf.classList.remove("flash"); void pf.offsetWidth; pf.classList.add("flash"); } }
	lastQueued = V.queued;
	$("#kitname").textContent = kitName(V.kit);
	setKitState(V.kitState);
	$("#undo").disabled = !V.canUndo; $("#redo").disabled = !V.canRedo; syncUndoCounts();
	/* One key: PLAY while stopped, STOP while playing (the icon follows the machine). */
	$("#play").setAttribute("aria-pressed", V.playing); $("#playico").textContent = V.playing ? "■" : "▶"; $("#play").setAttribute("aria-label", V.playing ? "Stop" : "Play");
	$("#rec").setAttribute("aria-pressed", !!V.rec); $("#recled").classList.toggle("on", !!V.rec);
	$("#rec").title = V.rec ? "Live recording: click a track's steps to play it, move a value to lock it. A moved value locks the track's next trig whose step has not started yet (the editor marks it). REC again: stop recording, keep playing (Alt+Space)" : "Live recording, as RECORD + PLAY on the machine (Alt+Space: Alt + play)";
	document.body.classList.toggle("liverec", !!V.rec);
	renderEngine();
	syncTx();
	const st = $("#status");
	if (st) {
		/* P7: while the machine starts, the start-up card says so (Boot); after it, the first read */
		const msg = V.input && !V.loaded ? "Reading the current pattern and kit from the machine…" : "";
		st.textContent = msg; st.hidden = !msg;
	}
	/* the start-up card over the whole window until the machine takes input; NO ROM and ROM ERROR are its
	   first-run states (an engine over MIDI has no start-up of its own) */
	const bootState = { missing: "missing", unsupported: "unsupported", loading: "loading", booting: "booting", animating: "booting" }[V.lifecycle] || "ready";
	Boot.update({ state: bootState, machine: "Machinedrum" });
	if ($("#dlg").dataset.first === "1") { $("#dlg").hidden = true; $("#dlg").dataset.first = ""; }
}

/* The engine label in the LCD shows the engine's real state (mockup v48), from the device:
   NO ROM, LOADING ROM (the machine is prepared or restored), BOOTING OS (the firmware starts),
   the engine's own label when it takes input (EMU OS 1.63, HW MIDI), ROM ERROR (not OS 1.63),
   HW CONNECT / HW NO MIDI for a machine on the MIDI wire. While the machine takes no input the LCD
   fields dim, REC and PLAY are disabled and edits wait (the desk refuses them).
   P6: the page's words for the one lifecycle value (machine.lifecycle) are this table's label and
   LED only. What a state means is the plug-in's (machine.lifecycleText: the tooltip and the status
   line); whether the machine takes input (machine.input) and whether its firmware answers MIDI
   (machine.midi) are facts too. When ready the label is the engine's own (capabilities label,
   about); the menu is the engine map (machine.engines). */
const LIFE = {
	missing: { label: "NO ROM", led: "off" },
	loading: { label: "LOADING ROM", led: "blink" },
	booting: { label: "BOOTING OS", led: "blink" },
	animating: { label: "BOOTING OS", led: "blink" },
	unsupported: { label: "ROM ERROR", led: "off" },
	hwConnecting: { label: "HW CONNECT", led: "blink" },
	hwLost: { label: "HW NO MIDI", led: "off" },
	ready: { label: "READY", led: "on" } };
const lifeOf = l => LIFE[l] || LIFE.booting;
/* the engine menu's own entries (not engines) */
const ENGINE_ACTIONS = ["global", "audio", "rom"];
function engineLabel() {
	const e = lifeOf(V.lifecycle);
	if (V.lifecycle === "ready") return [V.caps.label || e.label, e.led, V.caps.about || ""];
	return [e.label, e.led, V.lifecycleText];
}
/* the menu's engine entries are the engine map's, in its order, before the menu's own */
function renderEngineMenu(sel) {
	const engines = machineState().engines || [];
	for (const o of [...sel.options]) if (!ENGINE_ACTIONS.includes(o.value) && !engines.some(e => e.id === o.value)) o.remove();
	const first = [...sel.options].find(o => ENGINE_ACTIONS.includes(o.value)) || null;
	for (const e of engines) {
		let o = sel.querySelector(`option[value="${e.id}"]`);
		if (!o) { o = document.createElement("option"); o.value = e.id; }
		sel.insertBefore(o, first);
		o.textContent = e.label;
		o.disabled = !e.available;
		o.title = e.available ? "" : e.reason || "";
	}
	if (V.caps.engine) sel.value = V.caps.engine;
}
function renderEngine() {
	const btn = $(".lcdeng"), led = $("#engled"); if (!btn || !led) return;
	const [txt, mode, about] = engineLabel();
	btn.querySelector("span").textContent = txt;
	led.className = "led " + (mode === "on" ? "on" : mode === "blink" ? "on blink" : "");
	/* REC, PLAY and edits wait while the machine takes no input (machine.input) */
	$(".lcdpanel").classList.toggle("engwait", !V.input);
	["rec", "play"].forEach(id => { const k = document.getElementById(id); if (k) k.disabled = !V.input; });
	btn.title = about || "Engine: " + txt.toLowerCase() + ". Editing starts when it is ready.";
	const sel = document.getElementById("engsel");
	if (sel) renderEngineMenu(sel);
	markCapabilities();
}
/* What the engine cannot do (P6): each capability's controls, disabled with its reason while
   machine.capabilities.can[name] is not true (a name the plug-in does not publish: not allowed).
   CAP_INFO: capabilities the page shows no control for (they say how the machine is read). The
   sync script checks both lists against the contract's capability names. */
const CAP_CONTROLS = {
	transport: "#play,#rec",
	liveRecord: "#rec",
	chains: "[data-chainpad]",
	sampleNames: "[data-rename]",
	sampleLoad: "[data-smpload]",
	sampleAudio: "[data-aud]",
	modulators: '[data-addsrc],[data-delsrc],[data-srcshape],[data-minv],[data-mdel],#maddl,[data-set="srcrate"] button,[data-set="lcurve"] button,.pc[data-g="src"],.pc[data-g="link"]' };
const CAP_INFO = ["panelKeys", "lcd", "workingKitMemory", "mutesFromMemory"];
function markCapabilities() {
	const why = {};	/* element -> the reasons it is not allowed */
	for (const [cap, sel] of Object.entries(CAP_CONTROLS)) {
		if (canDo(V, cap)) continue;
		for (const el of $$(sel)) (why[cap] = why[cap] || []).push(el);
	}
	const off = new Map();
	for (const [cap, els] of Object.entries(why)) for (const el of els) if (!off.has(el)) off.set(el, V.caps.reasons[cap] || "Not available with this engine.");
	for (const el of $$("[data-capna]")) if (!off.has(el)) { delete el.dataset.capna; if (el.dataset.captitle != null) { el.title = el.dataset.captitle; delete el.dataset.captitle; } el.removeAttribute("aria-disabled"); }
	for (const [el, reason] of off) {
		if (el.dataset.capna == null) el.dataset.captitle = el.title || "";
		el.dataset.capna = "1"; el.title = reason; el.setAttribute("aria-disabled", "true");
		if ("disabled" in el) el.disabled = true;
	}
}
/* a control the engine cannot do takes no gesture: its reason instead */
for (const ev of ["pointerdown", "click", "wheel", "keydown", "dblclick"])
	document.addEventListener(ev, e => { const el = e.target.closest?.("[data-capna]"); if (!el) return; e.preventDefault(); e.stopImmediatePropagation(); if (e.type === "click") toast(el.title); }, { capture: true, passive: false });
document.addEventListener("change", e => {
	if (e.target.id !== "engsel") return;
	const sel = e.target, v = sel.value; renderEngine();
	if (v === "rom") romMenu();
	else if (v === "global") { sel.value = V.caps.engine; openGlobal(); }
	else if (v === "audio") { sel.value = V.caps.engine; openAudio(); }
	else if ((machineState().engines || []).some(x => x.id === v)) cmd("engine", { kind: v });
});

/* ===== Track header (one component, used by rail and grid) ===== */
function th(i, extra = "") {
	const t = V.tracks[i]; return `<div class="th ${i === S.sel ? "sel" : ""} ${S.multi.has(i) && S.ws === "seq" ? "multi" : ""} ${audible(i) ? "" : "off"} ${extra}" data-sel="${i}" style="--c:${FAMC[t.fam]}">
 <div class="sw"></div><div class="n ${i % 4 === 0 ? "fill" : ""}">${i + 1}</div><div class="nm" title="${t.name}">${S.ws === "seq" ? `<b>${codeOf(t.m)}</b><i class="gtag" title="${t.m}: its generator (GEN bar)">${genTag(genSpec(i))}</i>` : `<b>${t.m}</b>`}</div>
 <button class="ms m" data-mute="${i}" aria-pressed="${t.mute}" aria-label="Mute track ${i + 1}">M</button><button class="ms s" data-solo="${i}" aria-pressed="${t.solo}" aria-label="Solo track ${i + 1}">S</button></div>`;
}
function renderRail() {
	if (S.ws === "sampler") { renderSlots(); return; }
	$("#rail").innerHTML = `<div class="railhead ${S.ws === "seq" ? "tall" : ""}">Track<button class="iconkey allon" id="allon" ${V.tracks.some(t => t.mute || t.solo) ? "" : "disabled"} title="Unmute and unsolo every track (0)">M/S off</button></div>` + V.tracks.map((_, i) => th(i)).join("") + (S.ws === "seq" ? `<div class="railparams"><div class="rphead"><span class="cap">Lock parameter</span><button id="clearLane" class="iconkey" aria-label="Clear ${S.lane} locks" title="Clear ${S.lane} locks"><svg viewBox="0 0 14 14" aria-hidden="true"><path d="M2 4h10M5.5 4V2.5h3V4M3.5 4l.7 8h5.6l.7-8M6 6.5v3.5M8 6.5v3.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg></button></div><div class="pgrid vert" id="chips"></div></div>` : "");
}

/* ===== Sequence ===== */
function pages16() { return V.len / 16; }
function vis() { if (S.viewAll) return [0, V.len]; S.page = Math.min(S.page, pages16() - 1); return [S.page * 16, S.page * 16 + 16]; }
function steps() { const [a, b] = vis(); return Array.from({ length: b - a }, (_, k) => a + k); }
/* Mockup v51: ALL fits every step (minmax 0, a 2 px gap, class "viewall" on html); the page
   boundary is a 2 px shadow in the cell gap, so the lock lane lines up with the steps. */
function cols() { return S.viewAll ? `repeat(${steps().length},minmax(0,1fr))` : `repeat(${steps().length},minmax(18px,1fr))`; }
function stepCls(i, s) {
	const t = V.tracks[i], c = ["st"]; if (s % 4 === 0) c.push("q"); if (s % 16 === 0 && s !== vis()[0]) c.push("gap"); if (s >= V.length) c.push("past");
	if (t.trigs[s]) { c.push("on"); if (t.acc.has(s)) c.push("acc"); if (t.slide.has(s)) c.push("sl"); if (stepLocked(i, s)) c.push("lk"); }
	if (V.playing && s === S.step) c.push("ph"); return c.join(" ");
}
/* The PAGE control sits on the right, above the grid, on the ruler row. */
/* the step gestures, behind a small ? key at the right of the bar under the grid (a click: the list of keys) */
function stepLegend() {
	const row = (cls, what, how) => `<span>${cls != null ? `<i class="lg on ${cls}"></i>` : `<i class="lg none"></i>`}<b>${what}</b>${how}</span>`;
	return `<span class="steplegend"><button class="glegkey" id="steplegend" aria-label="Step gestures" aria-describedby="steplegpop">?</button><span class="legend glegpop" id="steplegpop" role="tooltip">${row("", "Trig", "click")}${row("acc", "Accent", "shift-click" + (V.accAll ? " (all)" : ""))}${row("sl", "Slide", "alt-click" + (V.slideAll ? " (all)" : ""))}${row("lk", "Has locks", "a lock on the step")}${row(null, "Fill", "⌘-click: every 2nd step from there to the end comes on (from a trig: off); ⌘⇧-click: every 4th")}<small>? the list of keys</small></span></span>`;
}
function pageCtl() { return `<span class="pagectl seqpage"><button class="pgkey" id="pgkey" ${pages16() < 2 ? "disabled" : ""} title="Next page. Shift-click = previous. Keys [ and ].">Page</button><span class="pleds" aria-hidden="true">${[0, 1, 2, 3].map(k => `<span class="pl ${k < pages16() ? "" : "na"} ${!S.viewAll && k === S.page ? "cur" : ""}" data-plp="${k}"><i class="led"></i></span>`).join("")}</span><button class="ptog ${S.viewAll ? "on" : ""}" id="pgall" aria-pressed="${S.viewAll}" title="Show all steps"><i class="led"></i>All</button><button class="ptog ${S.follow ? "on" : ""}" id="pgfollow" aria-pressed="${S.follow}" title="Page follows the play position"><i class="led"></i>Fol</button></span>`; }
function renderSeq() {
	document.documentElement.classList.toggle("viewall", !!S.viewAll);
	let h = `<div class="panel ${V.mode === "CLASSIC" ? "classic" : ""}" id="seqp"><div class="scroll" id="seqscroll"><div class="seq" id="seq">
  <div class="r" style="grid-template-columns:${cols()}">${steps().map(s => `<div class="rul ${s % 16 === 0 && s !== vis()[0] ? "gap" : ""}">${s % 4 === 0 ? s + 1 : ""}</div>`).join("")}</div>`;
	V.tracks.forEach((t, i) => { h += `<div class="r ${i === S.sel ? "sel" : ""} ${audible(i) ? "" : "off"}" data-row="${i}" style="grid-template-columns:${cols()};--c:${FAMC[t.fam]}">${steps().map(s => `<button class="${stepCls(i, s)}" data-t="${i}" data-s="${s}" aria-label="Track ${i + 1} step ${s + 1}" aria-pressed="${t.trigs[s]}"></button>`).join("")}</div>`; });
	h += `</div></div><div class="genbar"><div class="genband" id="genband">${genStripHtml()}</div><span class="gdiv" aria-hidden="true"></span>${stepLegend()}${pageCtl()}</div><div class="lanewrap"><div class="lanetop"><span class="cap">Lock lane · ${S.sel + 1} ${V.tracks[S.sel].name} · <b id="lanename">${laneLabel(S.sel, S.lane)}</b> <span class="lanescale">${bipLane() ? "L 64 · centre · R 63" : "0–127"}</span></span><span class="lockbudget" id="lockbudget"></span>${V.mode === "CLASSIC" ? `<span class="warnline" title="Locks stay in the pattern but do nothing until you switch to EXTENDED.">CLASSIC: locks muted</span>` : ""}<span class="lanehelp" title="Draw across the bars to lock this parameter per step. Alt-drag erases. Shift-drag draws a ramp, a straight line from where you press to where you let go. The wheel over a step with a trig moves its lock (Shift: fine). Hatched steps have no trig, so they cannot hold a lock. Dashed line = kit value.">Draw to lock · ⇧ ramp · alt erases</span></div>
</div>
  <div class="scroll" id="lanescroll"><div class="lane" id="lane" style="grid-template-columns:${cols()}"></div></div></div>`;
	$("#main").innerHTML = h; renderLane(); syncScroll();
}
/* Mockup v57: bipolar lanes draw from the centre (64): up = right / boost / louder, down = left / cut.
   The machine's signed parameters (manual: displayed -64..+63): PAN, EQG, the RAM-R levels and balances
   MLEV MBAL ILEV IBAL, and the master EQ gains LG HG PG (CTR machines). */
const BIP = SIGNED;
function bipLane() { return BIP.has(S.lane); }
function barHTML(v) { return bipLane() ? (v >= 64 ? `<i class="bp up" style="height:${(v - 64) / 63 * 50}%"></i>` : `<i class="bp dn" style="height:${(64 - v) / 64 * 50}%"></i>`) : `<i style="--f:${(v / 127).toFixed(4)}"></i>`; }
function renderLane() {
	const lane = $("#lane"); if (!lane) return;
	const t = S.sel, tr = V.tracks[t], m = V.locks.get(lk(t, S.lane)), g = grp(t, S.lane), base = g[S.lane] ?? 0;
	altLabels(); syncLockBudget();
	const pg = pages(tr.m); $("#chips").innerHTML = [["Synth", pg.s], ["Effects", pg.e], ["Routing", pg.r]].map(([lab, ps]) => {
		return `<span class="plab">${lab}</span>` + ps.map(p => {
			if (!p) return `<span class="pk empty"></span>`; const n = V.locks.get(lk(t, p))?.size || 0;
			return `<button class="pk ${n ? "has" : ""}" data-lane="${p}" aria-pressed="${p === S.lane}" title="${n ? n + " locked step" + (n > 1 ? "s" : "") : "No locks yet"}">${laneLabel(t, p)}${n ? `<i>${n}</i>` : ""}</button>`;
		}).join("");
	}).join("");
	lane.innerHTML = steps().map(s => {
		const on = tr.trigs[s], v = m?.get(s);
		return `<div class="lb ${on ? "" : "none"} ${s % 16 === 0 && s !== vis()[0] ? "gap" : ""} ${V.playing && s === S.step ? "ph" : ""}" data-s="${s}">${on ? `<div class="base" style="--bf:${(base / 127).toFixed(4)}"></div>${bipLane() ? `<div class="mid"></div>` : ""}${v != null ? barHTML(v) : ""}` : ""}</div>`;
	}).join("");
}
function syncScroll() { const a = $("#seqscroll"), b = $("#lanescroll"); if (!a || !b) return; a.onscroll = () => { b.scrollLeft = a.scrollLeft; }; b.onscroll = () => { a.scrollLeft = b.scrollLeft; }; }
function refreshRow(i) { $$(`.st[data-t="${i}"]`).forEach(b => { const s = +b.dataset.s; b.className = stepCls(i, s); b.setAttribute("aria-pressed", V.tracks[i].trigs[s]); }); }

let laneDraw = null;
function laneAt(e) {
	if (laneDraw.ramp) { rampAt(e); return; }
	const lane = $("#lane"); if (!lane) return; const el = document.elementFromPoint(e.clientX, e.clientY)?.closest(".lb"); if (!el || !lane.contains(el)) return;
	const s = +el.dataset.s, t = S.sel; if (!V.tracks[t].trigs[s]) return; const r = el.getBoundingClientRect(); const v = clamp(Math.round((r.bottom - 3 - e.clientY) / (r.height - 6) * 127));
	if (laneDraw.erase) eraseLock(t, S.lane, s); else if (!setLock(t, S.lane, s, v)) return;
	/* One shape for render and drag (mockup's barHTML): a bipolar bar redraws while it is dragged. */
	el.querySelector("i")?.remove(); if (!laneDraw.erase) el.insertAdjacentHTML("beforeend", barHTML(v));
	laneDraw.touched.add(s); renderTop();
}
function endLaneDraw() { if (!laneDraw) return; if (laneDraw.ramp) rampSend(); laneDraw = null; gesture = 0; refreshRow(S.sel); renderLane(); }

/* ===== Sound ===== */
function pc(g, n, { t, f, color, unit } = {}) {
	if (!n) return `<div class="pc empty" aria-hidden="true"></div>`;
	return `<div class="pc" role="slider" tabindex="0" aria-label="${n}" aria-valuemin="0" aria-valuemax="127" data-g="${g}" data-n="${n}"${t != null ? ` data-t="${t}"` : ""}${f ? ` data-f="${f}"` : ""}${color ? ` style="--pc:${color}"` : ""}><span>${n}</span>${unit ? `<i class="pu" data-unit="${unit}"></i>` : ""}<b></b></div>`;
}
function shapeIcon(i, inv) { const pts = Array.from({ length: 25 }, (_, k) => { const x = k / 24; return [(x * 26 + 1).toFixed(1), (11 - 7 * shape(i, x, inv)).toFixed(1)]; }); return `<svg width="28" height="22" viewBox="0 0 28 22" aria-hidden="true"><polyline fill="none" stroke="currentColor" stroke-width="1.6" points="${pts.map(p => p.join(",")).join(" ")}"/></svg>`; }
const RND = [.35, -.7, .9, -.25, .55, -.9, .1, .7];
function shape(i, x, inv) { const v = [1 - 4 * Math.abs(x - .5), 2 * x - 1, x < .5 ? 1 : -1, 1 - 2 * x, 2 * Math.exp(-4 * x) - 1, RND[Math.floor(x * 8) % 8]][i] ?? 0; return inv ? -v : v; }
function machButton(tr) {
	const fk = famKey(tr.m), fam = FAMS.find(x => x[0] === fk) || ["", ""];
	return `<button class="machbtn" id="machbtn" aria-haspopup="dialog" aria-expanded="false" aria-label="Change machine"><span class="lcdtxt">${tr.m}</span><span class="mfam">${fam[0].replace("PI", "P-I")} · ${fam[1]}</span><svg viewBox="0 0 10 6" aria-hidden="true"><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5"/></svg></button>`;
}
/* Sound (round 5): the track's sound in small groups of a few knobs, in the page's section look (a
   title on a rule, the Machinedrum page its knobs are on at the right, no frame). Three rows, in the
   Machinedrum's order: the SYNTHESIS page's groups, then EFFECTS (amp mod, EQ, filter, sample rate)
   and ROUTING (drive, level and pan, sends), then the LFO. Each row is one grid of three lines: the
   titles, the screens, the boxes, so every title, screen and box row is level across the row. A group
   whose knobs draw a picture has a screen; two groups without one share a column (the upper one's
   boxes at the screens' top, the lower one's on the row's box line); a lone one says what its knobs
   do where the screen would be. A row without screens is compact. Every knob of the machine is in
   exactly one group: the synthesis page's groups are per machine (SYN_TAB, from the manual's
   Appendix A), a name the table does not know goes to SYNTHESIS. */
const SND_TAG = { syn: ["synth", "SYNTHESIS"], fx: ["fx", "EFFECTS"], rt: ["route", "ROUTING"], lfo: ["lfo", "LFO"], kit: ["kit", "kit (EDIT KIT → RELATE)"] };
/* a synthesis group: its title, its knobs (the page's names, unqualified), its screen (or null) and,
   for a group without one, what its knobs do */
const SG = (title, knobs, ed, note) => ({ title, knobs: knobs.split(" "), ed, note });
const AENV = k => SG("Amp env", k, "env");
/* E12: pitch and bend, the start and decay, the filter (or the machine's own tone knob), retrigs */
function e12Groups(m) {
	const x = (Cat.byName[m]?.params || [])[3];
	if (m === "E12-BD") return [SG("Pitch", "PTCH BEND", "pitch"), AENV("START DEC"), SG("Snap", "SNAP SPLEN", "atk"), SG("Retrig", "RTRG RTIM", "rtrg")];
	const flt = x === "HPQ" ? SG("Filter", "HP HPQ", "resp") : x === "STOP" ? SG("Filter", "HP", "resp") : SG("Tone", "HP " + x, "resp");
	return [SG("Pitch", "PTCH BEND", "pitch"), AENV(x === "STOP" ? "STRT DEC STOP" : "STRT DEC"), flt, SG("Retrig", "RTRG RTIM", "rtrg")];
}
const OUT_NOTE = { MONO: "MONO sums the echo to mono", LEV: "LEV is the effect's output level", GATE: "GATE gates the reverb's tail", GAIN: "GAIN is the EQ's output gain",
	HP: "HP high-passes the compressor's side chain", OUTG: "OUTG is the output gain", MIX: "MIX blends the dry signal back in" };
const outG = k => SG("Output", k, null, k.split(" ").map(n => OUT_NOTE[n]).join(". ") + ".");
const SMP_G = [SG("Pitch", "PTCH", null), SG("Sample", "STRT END", "sample"), AENV("DEC HOLD"), SG("Retrig", "RTRG RTIM", "rtrg"), SG("Bit rate", "BRR", null)];
const SYN_TAB = {
	"TRX-BD": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC"), SG("Attack", "STRT NOIS", "atk"), SG("Tone", "HARM CLIP", "wave")],
	"TRX-B2": [SG("Pitch", "PTCH RAMP", "pitch"), AENV("DEC HOLD"), SG("Attack", "TICK NOIS", "atk"), SG("Tone", "DIRT DIST", "wave")],
	"TRX-SD": [SG("Pitch", "PTCH BUMP BENV TUNE", "pitch"), AENV("DEC"), SG("Snap", "SNAP", "noise"), SG("Tone", "TONE CLIP", "wave")],
	"TRX-XT": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC DAMP"), SG("Distortion", "DIST DTYP", "wave")],
	"TRX-CP": [SG("Claps", "CLPY RATE HARD", "claps"), SG("Tone", "TONE RICH", "spec"), SG("Room", "ROOM RSIZ RTUN", "room")],
	"TRX-RS": [SG("Body", "PTCH DEC", "osc"), SG("Distortion", "DIST", "wave")],
	"TRX-CB": [SG("Pitch", "PTCH BUMP", "pitch"), AENV("DEC DAMP"), SG("Tone", "TONE ENH", "spec")],
	"TRX-CH": [SG("Metal", "GAP MTAL", "metal"), SG("Filter", "HPF LPF", "resp"), AENV("DEC")],
	"TRX-CY": [SG("Body", "RICH SIZE PEAK", "metal"), SG("Top", "TOP TTUN", "spec"), AENV("DEC")],
	"TRX-MA": [SG("Shake", "ATT SUS REV", "env"), SG("Rattle", "RATL DAMP RTYP", "grains"), SG("Tone", "TONE HARD", "spec")],
	"TRX-CL": [SG("Body", "PTCH DEC TUNE ENH", "osc"), SG("Attack", "CLIC DUAL", "atk")],
	"EFM-BD": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC"), SG("FM", "MOD MFRQ MDEC MFB", "fm")],
	"EFM-SD": [SG("Body", "PTCH DEC", "osc"), SG("Noise", "NOISE NDEC HPF", "noise"), SG("FM", "MOD MFRQ MDEC", "fm")],
	"EFM-XT": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC CLIC"), SG("FM", "MOD MFRQ MDEC", "fm")],
	"EFM-CP": [SG("Body", "PTCH DEC HPF", "osc"), SG("Claps", "CLPS CDEC", "claps"), SG("FM", "MOD MFRQ MDEC", "fm")],
	"EFM-RS": [SG("Rim", "PTCH DEC MOD HPF", "fm"), SG("Snare", "SNAR SPTC SDEC SMOD", "fm")],
	"EFM-CB": [SG("Body", "PTCH DEC SNAP", "osc"), SG("FM", "MOD MFRQ MDEC FB", "fm")],
	"EFM-HH": [SG("Body", "PTCH DEC", "osc"), SG("Tremolo", "TREM TFRQ", "trem"), SG("FM", "MOD MFRQ MDEC FB", "fm")],
	"EFM-CY": [SG("Body", "PTCH DEC HPF", "osc"), SG("FM", "MOD MFRQ MDEC FB", "fm")],
	"P-I-BD": [SG("Body", "PTCH DEC DAMP", "osc"), SG("Strike", "HARD HAMR TENS", "strike")],
	"P-I-SD": [SG("Body", "PTCH DEC RING", "osc"), SG("Strike", "HARD TENS", "strike"), SG("Snares", "RVOL RDEC", "noise")],
	"P-I-MT": [SG("Body", "PTCH DEC DAMP", "osc"), SG("Strike", "HARD HAMR POS", "strike"), SG("Shell", "TUNE SIZE", "metal")],
	"P-I-RS": [SG("Body", "PTCH DEC RING", "osc"), SG("Strike", "HARD", "strike"), SG("Snares", "RVOL RDEC", "noise")],
	"P-I-ML": [SG("Body", "PTCH DEC", "osc"), SG("Strike", "HARD TENS", "strike")],
	"P-I-MA": [AENV("DEC"), SG("Grains", "GRNS GLEN SIZE HARD", "grains")],
	"P-I-HH": [SG("Metal", "PTCH CLSN RING", "metal"), SG("Decay · close", "DEC CLOS", "env"), SG("EQ", "BR AU AG", "spec")],
	"P-I-RC": [SG("Metal", "PTCH HARD RING", "metal"), SG("Decay · grab", "DEC GRAB", "env"), SG("EQ", "BR AU AG", "spec")],
	"GND-SIN": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC")],
	"GND-NS": [AENV("DEC")],
	"GND-IM": [SG("Impulse", "UP UVAL DOWN DVAL", "imp")],
	"INP-GA": [SG("Input", "VOL GATE", "ingate"), SG("Gate env", "ATCK HLD DEC", "env")],
	"INP-FA": [SG("Input", "ALEV GATE", "ingate"), SG("Filter env", "FATK FHLD FDEC FDPH", "env"), SG("Filter", "FFRQ FQ", "resp")],
	"INP-EA": [SG("Amp env", "AVOL AHLD ADEC", "env"), SG("Filter env", "FDPH FHLD FDEC", "env"), SG("Filter", "FFRQ FQ", "resp")],
	MID: [SG("Notes", "NOTE N2 N3", "chord"), SG("Length · velocity", "LEN VEL", "note"), SG("Controllers", "PB MW AT", "bars")],
	CTR: [SG("Parameters", "P1 P2 P3 P4 P5 P6 P7 P8", null)],
	"CTR-RE": [SG("Delay", "TIME FB", "taps"), SG("Modulation", "MOD MFRQ", "trem"), SG("Filter", "FILTF FILTW", "resp"), outG("MONO LEV")],
	"CTR-GB": [SG("Reverb", "DVOL PRED DEC DAMP", "verb"), SG("Filter", "HP LP", "resp"), outG("GATE LEV")],
	"CTR-EQ": [SG("EQ", "LF LG PF PG PQ HF HG", "eq3"), outG("GAIN")],
	"CTR-DX": [SG("Curve", "TRHD RTIO KNEE", "comp"), SG("Attack · release", "ATCK REL", "env"), outG("HP OUTG MIX")],
	ROM: SMP_G, "RAM-P": SMP_G,
	"RAM-R": [SG("Main in", "MLEV MBAL", "lvbal"), SG("Input", "ILEV IBAL", "lvbal"), SG("Cue", "CUE1 CUE2", "bars"), SG("Record", "LEN RATE", "rec")] };
/* machines that share a synthesis page */
const SYN_SAME = { "TRX-XC": "TRX-XT", "TRX-OH": "TRX-CH", "P-I-CC": "P-I-RC", "INP-GB": "INP-GA", "INP-FB": "INP-FA", "INP-EB": "INP-EA" };
function synTable(m) {
	const f = famKey(m), k = SYN_SAME[m] || m;
	if (f === "E12") return e12Groups(m);
	return SYN_TAB[k] || SYN_TAB[m.slice(0, 5)] || SYN_TAB[f] || [];
}
/* the effects and routing pages' groups: key → [title, screen] (MID's CC pairs and CTR-8P's targets by pattern) */
const FXRT_GRP = { am: ["Amp mod", "am"], eq: ["EQ", "peq"], flt: ["Filter", "flt"], srr: ["Sample rate", "srr"], drv: ["Drive", "dist"], mix: ["Level · pan", "pan"], snd: ["Sends", "sends"], prog: ["Program", null] };
const FXRT_BY = { AMD: "am", AMF: "am", EQF: "eq", EQG: "eq", FLTF: "flt", FLTW: "flt", FLTQ: "flt", SRR: "srr", DIST: "drv", VOL: "mix", PAN: "mix", DEL: "snd", REV: "snd", PCHG: "prog" };
function fxrtKey(n, g) {
	const cc = /^CC(\d)[DV]$/.exec(n), p = /^P(\d)(TR|PA)$/.exec(n);
	if (cc) return ["cc" + cc[1], "CC " + cc[1], null];
	if (p) return ["p" + p[1], "P" + p[1] + " target", null];
	const k = FXRT_BY[n]; return k ? [k, ...FXRT_GRP[k]] : [g + "page", g === "fx" ? "Effects page" : "Routing page", null];
}
/* LFOS LFOD LFOM are the LFO's SPD DEPTH SHMIX (the same kit parameters, lfoParams) */
const LFO_RT = ["LFOS", "LFOD", "LFOM"];
/* each screen's help: its tooltip */
const SND_TIP = {
	sample: "Drag STRT and END. Ticks = STRT locks per step (the chops). END left of STRT = reverse. The waveform is the slot's own, read from the machine.",
	rec: "Drag LEN to set the recording length (127 = 2 bars). The waveform is the last take, read from the machine.",
	env: "The level after a trig: the attack rises, HOLD keeps it, the decay lets it fall; STOP, CLOS or GRAB cut it, STRT skips the start (dashed). Drag the dots; a dot on a rail at the foot is a knob the curve shows.",
	pitch: "The pitch after a trig: the dashed line is PTCH; RAMP or BUMP start above it and fall at RDEC or BENV, BEND bends up or down into it. Drag the dots.",
	atk: "The first moments of the hit: the click (STRT, TICK, CLIC, SNAP) and its length, the noise burst (NOIS, dashed), a second attack (DUAL), over the body (faint). Drag the dots up.",
	wave: "Two cycles through the tone stage (dashed = clean): harmonics, drive, its hardness, fewer bits. Drag the dots.",
	claps: "The clap: how many hands (the last one), how far apart (the second), how hard (the first); the last one's tail. Drag the dots.",
	room: "The claps (faint), then the room: ROOM how loud, RSIZ how long, RTUN its tone. Drag the dots.",
	spec: "The tone, low to high: where its colour sits and how much (the dot), or the lows and highs. Drag the dots.",
	metal: "The partials, low to high: pitch and spread move them sideways, the amounts lift them. Drag the dots.",
	osc: "The body after a trig: how fast it swings (the first peak, sideways), how long (dashed, sideways); knobs on the rails at the foot shape it. Drag the dots.",
	fm: "The carrier after a trig and the modulation that moves its tone (dotted: how much, how long). The modulator's frequency and feedback are on the rails. Drag the dots.",
	noise: "The noise burst: how loud (up), how long (sideways); HPF thins it. Drag the dots.",
	strike: "The mallet's hit: how hard (up), how soft a mallet (sideways), the skin's tension (dashed). Drag the dots.",
	grains: "The grains: how many, how long the shake, how hard each. Drag the dots.",
	trem: "The level the tremolo leaves: the dot is its speed (sideways) and depth (down).",
	resp: "The filter's response, low to high. Drag the edges sideways; up for the peak where the filter has a Q.",
	imp: "The impulse: how long and how far it goes up, then down. Drag the corners.",
	ingate: "The input (faint) and what passes the gate: louder than GATE (dashed), at the input's volume. Drag the dots up.",
	chord: "The notes a trig sends: NOTE on the keyboard at the foot, N2 and N3 semitones above it. Drag the dots.",
	note: "One note: LEN how long, VEL how loud. Drag the corner.",
	bars: "One bar a knob. Drag the bars' tops.",
	lvbal: "The input in the stereo field: balance sideways, level up. Drag the dot.",
	taps: "The repeats: TIME apart, FB how much each keeps. Drag the second one.",
	verb: "The dry hit (DVOL), then PRED later the tail, DEC long, DAMP smoother. Drag the dots.",
	eq3: "The master EQ from this machine: the shelves and the peak, sideways for the frequency, up to boost. PQ is the peak's width.",
	comp: "The compressor, input to output (dashed = unchanged): TRHD where it starts, RTIO how much, KNEE how softly. Drag the dots.",
	rtrg: "Retrigs: RTRG is how many (drag the last hit sideways), RTIM the time between them, relative to the tempo (drag the second hit).",
	am: "Tremolo: the level the amplitude modulator leaves. Dot = AMF (sideways) and AMD (up).",
	peq: "One band: EQF moves it, EQG boosts (up) or cuts (down).",
	flt: "24 dB filter: the pass band from FLTF to FLTF + FLTW. Left dot = FLTF (drag up for FLTQ), right dot = FLTW.",
	srr: "Sample-rate reduction: more SRR, longer held steps. Drag the dot sideways.",
	dist: "Distortion: input to output (dashed = clean). Drag the dot.",
	pan: "The track in the stereo field: PAN sideways, VOL up. Drag the dot.",
	sends: "Sends to the master effects: DEL = Rhythm Echo, REV = Gate Box. Drag the bars' dots.",
	lshape: "One cycle of the LFO: solid = SHP1 and SHP2 (inverted) mixed by SHMIX; faint = the two shapes. Drag the dot sideways for SHMIX.",
	lmotion: "The LFO across one bar of this track: SPD cycles it faster, DEPTH scales it (dashed). UPDTE TRIG restarts it on every trig (the ticks), HOLD keeps the value a trig takes. Dot = SPD (sideways) and DEPTH (up). Speed in 1/128 notes; 16 LFOs per kit." };
const attr = s => String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
const sndPlot = (inner, tip, note) => `<div class="plot" title="${attr(tip)}">${inner}${note ? `<span class="plotnote">${note}</span>` : ""}</div>`;
/* a group's screen, or "": its canvas knows the group's knobs (data-k), so one editor serves every
   machine that has them */
function sndScreen(ed, tr, at, knobs = []) {
	if (!ed) return "";
	if (ed === "sample") return sndPlot(waveBox(`<canvas class="ed" data-ed="sample" aria-label="Sample with start and end markers. Drag the markers."></canvas>`, at), SND_TIP.sample, smpWhy(at));
	if (ed === "rec") return sndPlot(waveBox(`<canvas class="ed" data-ed="rec" aria-label="Recording window. Drag LEN."></canvas>`, at), SND_TIP.rec, smpWhy(at));
	const tip = SND_TIP[ed] || "Drag the dots.";
	return sndPlot(`<canvas class="ed" data-ed="${ed}" data-k="${knobs.join(" ")}" aria-label="${attr(tip)}"></canvas>`, tip);
}
/* The track's groups: { key, title, g, knobs, ed } per row. Synthesis: the machine's table (its knobs
   by their page names: SYN·DIST where the routing page has a DIST too), then any knob it leaves out. */
function sndGroups(tr) {
	const pg = pages(tr.m), have = names(pg.s), syn = [], fxrt = [], used = new Set();
	const qual = n => have.includes(n) ? n : have.includes("SYN·" + n) ? "SYN·" + n : null;
	for (const d of synTable(tr.m)) {
		const knobs = d.knobs.map(qual).filter(n => n && !used.has(n)); if (!knobs.length) continue;
		knobs.forEach(n => used.add(n));
		syn.push({ key: d.title.toLowerCase().replace(/[^a-z0-9]+/g, "-"), title: d.title, g: "syn", knobs, ed: d.ed, note: d.note });
	}
	const rest = have.filter(n => !used.has(n));
	if (rest.length) syn.push({ key: "syn", title: "Synthesis", g: "syn", knobs: rest, ed: null });
	const add = (list, key, title, g, n, ed) => { let x = list.find(a => a.key === key && a.g === g); if (!x) list.push(x = { key, title, g, knobs: [], ed }); x.knobs.push(n); };
	for (const [g, p] of [["fx", "e"], ["rt", "r"]]) for (const n of names(pg[p])) { if (g === "rt" && LFO_RT.includes(n)) continue; const [k, title, ed] = fxrtKey(n, g); add(fxrt, k, title, g, n, ed); }
	return [syn, fxrt];
}
const SND_COL = { fx: "var(--e12)", rt: "var(--gnd)", lfo: "var(--teal)" };
/* a group: its title on the rule, its screen (or what its knobs do), its boxes; its page's word only on
   the first group of that page in its row (tag false). Each part is a cell of the row's grid. */
function sgHtml(x, cols, color, tag, note) {
	const page = SND_TAG[x.g][1];
	const body = x.body || `<div class="ctl" style="grid-template-columns:repeat(${cols},minmax(0,1fr))">${x.knobs.map(n => pc(x.g, n, { color: SND_COL[x.g] || color })).join("")}</div>`;
	return `<section class="sg" data-sg="${x.key}"><header><h3>${mutTitle(x)}</h3>${tag ? `<span title="${attr(x.tagTip || "Its knobs are on the Machinedrum's " + page + " page")}">${SND_TAG[x.g][0]}</span>` : ""}</header>${x.plot || (note ? `<p class="sgnote">${note}</p>` : "")}${body}</section>`;
}
/* One row: a grid of columns over three lines (titles, screens, boxes). A group with a screen is a
   column of its own (as wide as its boxes, but never narrower than about two of them, a waveform
   wider); the ones without pair up in one column (a lone one says what its knobs do). A row without
   any screen is two lines, its groups side by side. */
function sgRow(list, cls, color) {
	if (!list.length) return "";
	let prev = ""; const first = x => prev !== x.g && (prev = x.g), n = x => x.n ?? x.knobs.length;
	const narrow = list.reduce((a, x) => a + n(x), 0) > 10 ? 1.8 : 2.6, w = x => x.w ?? (x.ed === "sample" ? 6 : x.ed === "rec" ? 5 : x.plot ? Math.max(n(x), narrow) : n(x));
	const tracks = cols => `grid-template-columns:${cols.map(c => `minmax(min-content,${c}fr)`).join(" ")}`;
	if (!list.some(x => x.plot)) {
		const ws = list.map(x => Math.max(1.4, w(x)));
		return `<div class="sgrow flat ${cls}" style="${tracks(list.about ? [...ws, 4] : ws)}">${list.map(x => `<div class="sgcol">${sgHtml(x, n(x), color, first(x))}</div>`).join("")}${list.about ? `<p class="sgabout">${list.about}</p>` : ""}</div>`;
	}
	const cols = [], stacks = [];
	for (const x of list) {
		if (x.plot) { cols.push([x]); continue; }
		const s = stacks[stacks.length - 1];
		if (s && s.length < 2) s.push(x); else { const c = [x]; stacks.push(c); cols.push(c); }
	}
	return `<div class="sgrow ${cls}" style="${tracks(cols.map(c => Math.max(...c.map(w))))}">${cols.map(c => {
		const cn = Math.max(...c.map(n)), kind = c[0].plot ? "" : c.length > 1 ? " stack" : " lone";
		return `<div class="sgcol${kind}">${c.map(x => sgHtml(x, cn, color, first(x), kind === " lone" ? x.note || about(V.tracks[S.sel].m) : "")).join("")}</div>`;
	}).join("")}</div>`;
}
function renderSound() {
	const t = S.sel, tr = V.tracks[t], l = tr.lfo, at = smpOfMachine(tr.m), color = FAMC[tr.fam];
	const relSel = (id, kind, word) => `<select id="${id}"><option value="">none</option>${V.tracks.map((x, i) => i !== t ? `<option value="${i}" ${tr[kind] === i ? "selected" : ""}>${word} ${i + 1} ${x.name}</option>` : "").join("")}</select>`;
	const [syn, fxrt] = sndGroups(tr);
	[...syn, ...fxrt].forEach(x => { x.plot = sndScreen(x.ed, tr, at, x.knobs.map(n => n.replace(/^SYN·/, ""))); });
	if (!syn.length) syn.push({ key: "syn", title: "Synthesis", g: "syn", knobs: [], n: 0, w: 8, body: `<p class="sgabout">${about(tr.m) || "This machine has no synthesis knobs."}</p>` });
	else if (!syn.some(x => x.plot)) syn.about = about(tr.m);
	const lfoTip = "SPD DEPTH SHMIX are LFOS LFOD LFOM on the Machinedrum's ROUTING page; the shapes, the target and UPDTE are its LFO page";
	const shapeRow = sl => `<div class="kv"><span class="mono">${sl}</span><div class="shapes">${SHAPES.map((n, i) => `<button data-slot="${sl}" data-shape="${i}" aria-pressed="${l[sl] === i}" title="${n}${sl === "SHP2" ? ", inverted" : ""}" aria-label="${sl} ${n}">${shapeIcon(i, sl === "SHP2")}</button>`).join("")}</div></div>`;
	const lfo = [
		{ key: "lshape", title: `LFO ${t + 1} shape`, g: "lfo", knobs: ["SHMIX"], w: 5, tagTip: lfoTip,
			plot: sndPlot(`<canvas class="ed" data-ed="lshape" aria-label="One cycle of the LFO's shape. Drag the dot for SHMIX."></canvas>`, SND_TIP.lshape),
			body: `<div class="sgline lfoshape">${shapeRow("SHP1")}${shapeRow("SHP2")}${pc("lfo", "SHMIX", { color: SND_COL.lfo })}</div>` },
		{ key: "lmotion", title: "LFO motion", g: "lfo", knobs: ["SPD", "DEPTH"], w: 3.4, tagTip: lfoTip,
			plot: sndPlot(`<canvas class="ed" data-ed="lmotion" aria-label="The LFO across one bar. Drag the dot for SPD and DEPTH."></canvas>`, SND_TIP.lmotion),
			body: `<div class="sgline">${["SPD", "DEPTH"].map(n => pc("lfo", n, { color: SND_COL.lfo })).join("")}<span class="seg" data-set="upd" title="UPDTE: FREE runs on, TRIG restarts it on every trig, HOLD keeps the value a trig takes">${["FREE", "TRIG", "HOLD"].map(u => `<button data-v="${u}" aria-pressed="${l.UPDTE === u}">${u}</button>`).join("")}</span></div>` },
		{ key: "ltarget", title: "LFO target", g: "lfo", knobs: [], w: 2, tagTip: lfoTip,
			body: `<div class="sgsel" title="The parameter this LFO moves: a track, then one of its parameters"><select id="lfoT">${V.tracks.map((x, i) => `<option value="${i}" ${i === l.TRCK ? "selected" : ""}>T${i + 1} ${x.name}</option>`).join("")}</select><select id="lfoP">${params(l.TRCK).map(p => `<option ${p === l.PARAM ? "selected" : ""}>${p}</option>`).join("")}</select></div>` },
		{ key: "rel", title: "Relations", g: "kit", knobs: [], w: 2, tagTip: "Kit relations: EDIT KIT → RELATE",
			body: `<div class="sgsel pair"><label title="Mute group: this track's trig mutes the chosen track, as open and closed hihats.">Mute${relSel("mg", "muteGroup", "mutes")}</label>
			<label title="Trig group: this track's trig also trigs the chosen track. Trig relations do not chain.">Trig${relSel("tg", "trigGroup", "trigs")}</label></div>` }];
	$("#main").innerHTML = `<div class="snd mutating">
  <div class="sndhead">${machButton(tr)}
   <div class="genband mutband" id="mutband" title="The groups follow the sound's path: synthesis, effects, routing, then the LFO and the track's relations. Drag a dot or a box; hold Alt to move the same knob on every track (Control All).">${mutStripHtml()}</div></div>
  ${sgRow(syn, "synrow", color)}
  ${sgRow(fxrt, "fxrow", color)}
  ${sgRow(lfo, "lforow", color)}
 </div>`;
	syncControls(); redraw();
}

/* ===== Mix ===== */
function renderMix() {
	$("#main").innerHTML = `<div class="panel pad">
 <div class="scroll"><div class="strips">${V.tracks.map((t, i) => {
		const out = t.out || "MAIN", direct = out !== "MAIN"; return `<div class="strip ${i === S.sel ? "sel" : ""} ${direct ? "direct" : ""}" data-sel="${i}" style="${audible(i) ? "" : "opacity:.5"}">
  <div class="top2"><i class="led act" data-act="${i}"></i><span>${i + 1}</span></div>
  ${"VOL" in t.rt ? `<div class="fader" role="slider" tabindex="0" aria-label="Track ${i + 1} volume" data-g="rt" data-n="VOL" data-t="${i}"><div class="tr"><i></i></div><div class="cap2"></div></div>` : `<div class="fader"></div>`}
  <div class="v" data-show="${i}"></div>
  ${["PAN", "DIST", "DEL", "REV"].map(n => n in t.rt ? (direct && (n === "DEL" || n === "REV") ? pc("rt", n, { t: i }).replace('class="pc"', `class="pc mainonly" title="${n === "DEL" ? "Delay" : "Reverb"} sends only reach the main outputs; this track goes to OUT ${out}. The value is kept."`) : pc("rt", n, { t: i })) : `<div class="pc empty" aria-hidden="true"></div>`).join("")}
  <button class="outk ${direct ? "on" : ""}" data-out="${i}" title="${direct ? "Individual output " + out + ": skips the master effects" : "Main output, through the master effects"}">OUT ${out}</button>
  <div class="mrow"><button class="ms m" data-mute="${i}" aria-pressed="${t.mute}" aria-label="Mute track ${i + 1}">M</button><button class="ms s" data-solo="${i}" aria-pressed="${t.solo}" aria-label="Solo track ${i + 1}">S</button></div>
  <div class="nm" title="${t.name}">${t.m}</div></div>`;
	}).join("")}</div></div></div>
 <div class="flow" aria-label="Signal path">SENDS <i>→</i> RHYTHM ECHO <i>→</i> GATE BOX <i>→</i> MASTER EQ <i>→</i> DYNAMIX <i>→</i> MAIN OUT <span>tracks on A–F skip this chain</span></div>
 <div class="fx4">${Object.entries(V.mfx).map(([id, f]) => `<section class="card"><header><h3>${f.name}</h3><span>${{ echo: "DEL send", gate: "REV send + DVOL", eq: "main out", dyn: "main out" }[id]}</span></header>
  <canvas class="ed sm" data-ed="${id}" aria-label="${f.name} screen. Drag the dots."></canvas>
  <div class="ctl four">${f.k.map(n => pc("mfx", n, { f: id })).join("")}</div></section>`).join("")}</div>`;
	syncControls(); redraw();
}

/* ===== Song ===== */
function patLen(p) { return lengthOfPattern(p); }
function hasPat(p) { const d = Docs.patterns[p]; return !!d && d.tracks.some(t => t.trigs.length); }
const rowLen = r => r.len ?? (patLen(r.pat) - (r.ofs || 0));
function songSteps() { let n = 0; V.song.forEach(r => { if (!r.type) n += rowLen(r) * r.rep; }); V.song.forEach((r, i) => { if (r.type === "loop" && r.count !== Infinity) { let seg = 0; for (let k = r.to; k < i; k++) { const q = V.song[k]; if (!q.type) seg += rowLen(q) * q.rep; } n += seg * (r.count - 1); } }); return n; }
function songTime() { const st = songSteps(), sec = st * 60 / V.bpm / 4; return `${Math.floor(sec / 60)}:${String(Math.round(sec % 60)).padStart(2, "0")}`; }
function loopOf(i) { return V.song.findIndex((r, k) => r.type === "loop" && k > i && r.to <= i); }
function songCmd(op, args, optimistic) { cmd(op, Object.assign({ s: V.songSlot }, args), undefined, optimistic); }
/* row i becomes r (a view row): sent, and shown at once */
function rowSet(i, r) { songCmd("rowSet", { i, row: rowToContract(r, patLen) }, [[["song", i], r]]); }
/* One palette, two ways to play its pads (S.songPick): ARRANGE adds the pad after the selected row of the
   song (stored, any bank, loops and jumps; heard after STOP + reload), CHAIN numbers it into the machine's
   own chain (live, one bank, loops; chainFooter, the Plays line). The header says which one the machine
   plays (playsOf: CHAIN, SONG or PATTERN). */
S.songPick = "arrange"; S.songMore = false; S.chainDraft = []; S.chainTimer = 0; S.chainSent = false;
function chainDoc() { const d = machineState().desk || {}; return d.chain || null; }
/* Every pad (and BACK) chains at once: the pads are sent as the machine's chain 150 ms after the last
   click (the latest wins; the desk also holds a chain back while the keys of the one before are on
   their way, MdMachine::cmdChain). Fewer than two pads: the chain the machine plays ends. */
function chainSoon() {
	clearTimeout(S.chainTimer);
	S.chainTimer = setTimeout(() => {
		const d = S.chainDraft.slice(), c = chainDoc();
		if (!canDo(V, "chains")) return;
		if (d.length >= 2) { if (!(c && c.active && c.patterns.length === d.length && c.patterns.every((p, i) => p === d[i]))) { cmd("chain", { patterns: d }); S.chainSent = true; } }
		else if ((c && c.active) || S.chainSent) { cmd("chainClear"); S.chainSent = false; }
	}, 150);
}
function chainFooter() {
	const c = chainDoc(), bn = "ABCDEFGH"[S.bank], d = S.chainDraft;
	const known = !!c, active = known && c.active && c.patterns.length > 0;
	const playing = V.pat, list = active ? c.patterns : [], at = list.indexOf(playing), next = active ? list[(at + 1) % list.length] : null;
	/* what the engine can do (machine.capabilities.chains, with its reason) */
	const can = canDo(V, "chains"), why = V.caps.reasons.chains || "";
	const live = !can ? `<span class="note">${why}</span>` : !known ? `<span class="note">The chain is not readable on this firmware.</span>` : active ? list.map(p => `<span class="lcdchip${p === playing ? " now" : ""}${V.playing && p === next && at >= 0 ? " nx" : ""}">${patName(p)}</span>`).join("<i>»</i>") + "<i>↺</i>"
		: `<span class="note">No chain. The machine plays ${patName(playing)} and stays on it.</span>`;
	return `<div class="chainfoot"><div class="irow"><span class="ilab">Plays</span><div class="chainrow">${live}</div></div>
  <div class="irow"><span class="ilab"></span><span class="chainacts"><button data-chain="undo"${d.length ? "" : " disabled"} title="Takes the last pad out and chains the rest at once">Back</button><button class="danger" data-chain="clear"${active || d.length ? "" : " disabled"} title="LOAD PATTERN of the current pattern: the machine's way to end a chain. The pads start over">Clear</button></span>
  <span class="note">${d.length === 1 ? `One more pad and the machine plays the chain (BANK ${bn} held, the TRIG keys in order; ${V.playing ? "from the pattern end" : "PLAY starts at the first"}).` : "Each pad chains at once: the machine plays them in order and loops."} One bank, each pattern once. Picking a pattern ends the chain; editing its patterns does not.</span></div></div>`;
}
function renderSong() {
	const sel = V.song[S.songSel] || V.song[0], chain = S.songPick === "chain", plays = playsOf(Docs);
	/* a chain is one bank's: another bank starts the draft over */
	S.chainDraft = S.chainDraft.filter(p => p >> 4 === S.bank);
	const pad = p => {
		const info = `${patName(p)}<small>${Docs.patterns[p] ? (hasPat(p) ? patLen(p) : "empty") : "…"}</small>`;
		if (!chain) return `<button class="padd ${hasPat(p) ? "has" : ""} ${!sel.type && sel.pat === p ? "cur" : ""}" data-addpat="${p}" draggable="true" title="Drag into the arrangement. Click adds after the selected row.">${info}</button>`;
		const n = S.chainDraft.indexOf(p);
		return `<button class="padd ${hasPat(p) ? "has" : ""}${n >= 0 ? " in" : ""}" data-chainpad="${p}" title="${patName(p)}${n >= 0 ? ": number " + (n + 1) + " in the chain. Click takes it out, and the machine plays the rest." : ". Click adds it: the machine plays the chain at once."}">${info}${n >= 0 ? `<em>${n + 1}</em>` : ""}</button>`;
	};
	const palette = `<div class="banks">${[..."ABCDEFGH"].map((b, k) => `<button class="bank ${k === S.bank ? "on" : ""}" data-bank="${k}"><i class="led"></i>${b}</button>`).join("")}</div>
  <div class="pgridp">${Array.from({ length: 16 }, (_, k) => pad(S.bank * 16 + k)).join("")}</div>
  ${chain ? chainFooter() : `<p class="note pnote">Click adds after row ${String(S.songSel + 1).padStart(3, "0")} · drag onto the grid</p>`}`;
	const playsTip = { chain: "The machine plays its own chain (live, one bank, loops). Clear it in CHAIN, or pick a pattern.", song: "The machine is in SONG mode: it plays the stored song (edits are heard after STOP + reload).", pattern: "The machine is in pattern mode: it plays this pattern and stays on it." }[plays.kind];
	const head = `<header class="phead"><h3>Patterns</h3><span class="seg" data-set="songpick" title="ARRANGE: a click adds the pattern to the song. CHAIN: a click numbers it into the machine's chain.">${[["arrange", "Arrange"], ["chain", "Chain"]].map(([v, t]) => `<button data-v="${v}" aria-pressed="${S.songPick === v}">${t}</button>`).join("")}</span><span class="lcdchip playschip ${plays.kind}" id="songPlays" title="${playsTip}">${plays.label}</span></header>`;
	let insp = "";
	if (!sel.type) {
		const L = patLen(sel.pat), o = sel.ofs || 0, ln = rowLen(sel);
		insp = `<div class="irow"><span class="ilab">Row</span><span class="lcdchip">${String(S.songSel + 1).padStart(3, "0")} · ${patName(sel.pat)}</span>
    <span class="stepper"><button data-step="pat" data-d="-1" aria-label="Previous pattern">‹</button><button data-step="pat" data-d="1" aria-label="Next pattern">›</button></span></div>
   <div class="irow"><span class="ilab">Repeat</span><span class="stepper"><button data-step="rep" data-d="-1">−</button><b class="mono">${sel.rep}</b><button data-step="rep" data-d="1">+</button></span>
    <span class="ilab" style="margin-left:18px">Tempo</span><button class="ptog ${sel.bpm ? "" : "on"}" data-bpmkeep="1"><i class="led"></i>Keep</button>
    ${sel.bpm ? `<span class="stepper"><button data-step="bpm" data-d="-1">−</button><b class="mono">${sel.bpm}</b><button data-step="bpm" data-d="1">+</button></span>` : `<span class="note">uses the tempo before it</span>`}</div>
   <div class="irow"><span class="ilab"></span><button class="ptog morebtn ${S.songMore ? "on" : ""}" data-rowmore="1" aria-expanded="${S.songMore}" title="Play part of the pattern, or mute tracks for this row"><i class="led"></i>More<small>${[sel.ofs || sel.len ? `part ${o + 1}–${o + ln}` : "", (sel.mutes || []).length ? `${sel.mutes.length} muted` : ""].filter(Boolean).map(x => " · " + x).join("") || " · part, mutes"}</small></button></div>
   ${S.songMore ? `   <div class="irow"><span class="ilab">Part</span><div class="partbar" style="grid-template-columns:repeat(${L},1fr)">${Array.from({ length: L }, (_, k) => `<i class="${k >= o && k < o + ln ? "on" : ""} ${k % 16 === 0 && k ? "pg" : ""}"></i>`).join("")}</div></div>
   <div class="irow"><span class="ilab"></span><span class="stepper"><span class="ilab">Start</span><button data-step="ofs" data-d="-1">−</button><b class="mono">${o + 1}</b><button data-step="ofs" data-d="1">+</button></span>
    <span class="stepper"><span class="ilab">Length</span><button data-step="len" data-d="-1">−</button><b class="mono">${ln}</b><button data-step="len" data-d="1">+</button></span>
    <button class="ptog ${sel.ofs || sel.len ? "" : "on"}" data-fullpat="1"><i class="led"></i>Whole pattern</button></div>
   <div class="irow"><span class="ilab">Mutes</span><div class="mkeys">${Array.from({ length: 16 }, (_, k) => `<button class="mkey ${(sel.mutes || []).includes(k) ? "off" : ""}" data-rowmute="${k}" title="Track ${k + 1} ${V.tracks[k].m}">${k + 1}</button>`).join("")}</div></div>` : ""}`;
	}
	else if (sel.type === "end") insp = `<div class="irow"><span class="ilab">End</span><span class="note">The song stops here. Add patterns before it from the palette.</span></div>`;
	else insp = `<div class="irow"><span class="ilab">Command</span><span class="seg" data-set="loopkind">${["loop", "jump", "halt"].map(k => `<button data-v="${k}" aria-pressed="${sel.type === k}">${k.toUpperCase()}</button>`).join("")}</span></div>
   ${sel.type !== "halt" ? `<div class="irow"><span class="ilab">${sel.type === "loop" ? "Back to" : "Jump to"}</span><span class="stepper"><button data-step="to" data-d="-1">−</button><b class="mono">${String(sel.to + 1).padStart(3, "0")}</b><button data-step="to" data-d="1">+</button></span></div>` : ""}
   ${sel.type === "loop" ? `<div class="irow"><span class="ilab">Times</span><span class="stepper"><button data-step="count" data-d="-1">−</button><b class="mono">${sel.count === Infinity ? "∞" : sel.count}</b><button data-step="count" data-d="1">+</button></span><button class="ptog ${sel.count === Infinity ? "on" : ""}" data-inf="1"><i class="led"></i>Forever</button></div>` : ""}
   <div class="irow"><span class="ilab"></span><span class="note">${sel.type === "loop" ? "Loops can be nested. Forever loops are good live: pick the next row while it plays." : sel.type === "jump" ? "Jumps the song pointer to another row." : "Pauses playback until you pick a row to go on from."}</span></div>`;
	$("#main").innerHTML = `<div class="songui lay2"><div class="songleft"><section class="card ${chain ? "chainmode" : ""}">${head}${palette}</section>
   <section class="card"><header><h3>Selected row</h3><span class="rowacts"><button data-rowact="up" title="Move left">←</button><button data-rowact="down" title="Move right">→</button><button data-rowact="dup">Duplicate</button><button data-rowact="loop">Add loop</button><button data-rowact="del" class="danger">Delete</button></span></header><div class="insp">${insp}</div></section></div>
  <section class="card"><header><h3>Arrangement</h3>${V.songReload ? `<span class="songwarn"><span class="lcdchip warnchip">Edits heard after STOP + reload</span><button class="cream" data-reloadsong="1">Reload song</button></span>` : ""}<span class="note">${V.song.length} of 256 rows · drag patterns onto the grid · drag cells to move · Delete removes</span></header>
    <div class="durbar" title="Song shape by time (length × repeats)">${V.song.map((r, i) => r.type ? `<i class="db dbm"></i>` : `<i class="db ${i === S.songSel ? "sel" : ""}" data-row="${i}" style="flex:${rowLen(r) * r.rep} 1 0"></i>`).join("")}</div>
    <div class="slotgrid" id="tl">${Array.from({ length: 16 }, (_, line) => `<span class="sglab">${String(line * 16 + 1).padStart(3, "0")}</span>${Array.from({ length: 16 }, (_, c) => {
		const i = line * 16 + c, r = V.song[i];
		if (!r) return `<div class="scell empty" data-i="${i}"></div>`;
		const cls = `scell ${i === S.songSel ? "sel" : ""} ${r.type ? "cmd " + r.type : ""} ${!r.type && loopOf(i) >= 0 ? "inloop" : ""}`;
		const txt = r.type === "end" ? "END" : r.type === "loop" ? `↺${String(r.to + 1).padStart(3, "0")}` : r.type === "jump" ? `→${String(r.to + 1).padStart(3, "0")}` : r.type === "halt" ? "HALT" : patName(r.pat);
		const sub = r.type === "loop" ? (r.count === Infinity ? "∞" : "×" + r.count) : !r.type ? `${r.rep > 1 ? "×" + r.rep : ""}${r.ofs || r.len ? "~" : ""}` : "";
		return `<button class="${cls}" data-row="${i}" data-i="${i}" draggable="${r.type === "end" ? "false" : "true"}" title="Row ${String(i + 1).padStart(3, "0")}${r.type ? "" : " · " + patName(r.pat) + " ×" + r.rep + " · " + rowLen(r) + " steps"}"><b>${txt}</b><small>${sub}</small></button>`;
	}).join("")}`).join("")}</div></section></div>`;
}
function songAction(a) {
	const i = S.songSel, r = V.song[i];
	if (a === "del") { if (r.type === "end") return; songCmd("rowDelete", { i }); S.songSel = Math.max(0, Math.min(i, V.song.length - 2)); }
	if (a === "dup" && !r.type) { songCmd("rowInsert", { i: i + 1, row: rowToContract(r, patLen) }); S.songSel = i + 1; }
	if (a === "up" && i > 0 && r.type !== "end") { songCmd("rowMove", { from: i, to: i - 1 }); S.songSel = i - 1; }
	if (a === "down" && i < V.song.length - 2 && r.type !== "end") { songCmd("rowMove", { from: i, to: i + 1 }); S.songSel = i + 1; }
	if (a === "loop") { const at = r.type === "end" ? i : i + 1; songCmd("rowInsert", { i: at, row: { kind: "loop", target: Math.max(0, at - 1), repeats: 1 } }); S.songSel = at; }
}
function songStep(k, d) {
	const r = { ...V.song[S.songSel] };
	if (k === "pat") r.pat = (r.pat + d + 128) % 128;
	if (k === "rep") r.rep = Math.max(1, Math.min(64, r.rep + d));
	if (k === "bpm") r.bpm = Math.max(30, Math.min(300, (r.bpm || Math.round(V.bpm)) + d));
	if (k === "ofs") { const L = patLen(r.pat); r.ofs = Math.max(0, Math.min(L - 1, (r.ofs || 0) + d)); if (r.len == null) r.len = L - r.ofs; if (r.ofs + r.len > L) r.len = L - r.ofs; }
	if (k === "len") { const L = patLen(r.pat); r.len = Math.max(1, Math.min(L - (r.ofs || 0), rowLen(r) + d)); }
	if (k === "to") r.to = Math.max(r.type === "jump" ? S.songSel + 1 : 0, Math.min(r.type === "loop" ? S.songSel - 1 : V.song.length - 1, r.to + d));
	/* The firmware plays a loop repeats + 1 times and 0 is infinite, so a finite loop plays at least twice. */
	if (k === "count") r.count = r.count === Infinity ? (d < 0 ? 64 : Infinity) : Math.max(2, Math.min(64, r.count + d));
	rowSet(S.songSel, r); render();
}
let drag2 = null;
function dropTarget(el) { const c = el?.closest?.(".scell"); if (!c) return null; const i = +c.dataset.i, endI = V.song.length - 1; return i < endI ? { i, mode: "onto" } : { i: endI, mode: "append" }; }
function showTarget(t) {
	$$(".scell.over,.scell.appendto").forEach(x => x.classList.remove("over", "appendto")); if (!t) return;
	const c = document.querySelector(`.scell[data-i="${t.mode === "append" ? V.song.length : t.i}"]`); c && c.classList.add(t.mode === "append" ? "appendto" : "over");
}
document.addEventListener("dragstart", e => {
	const b = e.target.closest(".scell:not(.empty)"), pk = e.target.closest(".padd");
	if (b) { drag2 = { kind: "row", v: +b.dataset.row }; b.classList.add("dragging"); } else if (pk) { drag2 = { kind: "pat", v: +pk.dataset.addpat }; pk.classList.add("dragging"); } else return;
	e.dataTransfer.effectAllowed = drag2.kind === "row" ? "move" : "copy"; try { e.dataTransfer.setData("text/plain", String(drag2.v)); } catch (_) { }
});
document.addEventListener("dragover", e => { if (!drag2) return; const t = dropTarget(e.target); if (!t) return; e.preventDefault(); showTarget(t); });
document.addEventListener("drop", e => {
	if (!drag2) return; const t = dropTarget(e.target); if (!t) return; e.preventDefault();
	if (drag2.kind === "pat") {
		if (V.song.length >= 256) { toast("A song holds 256 rows."); }
		else if (t.mode === "onto" && !V.song[t.i].type) { rowSet(t.i, { ...V.song[t.i], pat: drag2.v }); S.songSel = t.i; }
		else { const at = t.mode === "onto" ? t.i : V.song.length - 1; songCmd("rowInsert", { i: at, row: rowToContract({ pat: drag2.v, rep: 1 }, patLen) }); S.songSel = at; }
	}
	else { const from = drag2.v; let to = t.mode === "onto" ? t.i : V.song.length - 2; if (from !== to && V.song[from].type !== "end") { songCmd("rowMove", { from, to }); S.songSel = to; } }
	drag2 = null; render();
});
document.addEventListener("dragend", () => { drag2 = null; showTarget(null); $$(".dragging").forEach(x => x.classList.remove("dragging")); });

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

/* ===== Curve editors (the mockup's): a handle's drag gives the values it moves, the editor's "to"
   where they go (sendEditor). ===== */
const toTrack = g => () => ({ t: S.sel, g }), toMfx = f => () => ({ f });
const cssv = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const ED = {
	/* LFO SHAPE: one cycle of what the LFO sends: SHP1 and SHP2 (inverted, faint) mixed by SHMIX (solid).
	   Dot = SHMIX sideways. */
	lshape: {
		to: () => ({ t: S.sel, g: "lfo" }),
		draw(g, W, H) {
			const l = V.tracks[S.sel].lfo, mix = l.SHMIX / 127, A = H / 2 - 16, y = (p, w) => H / 2 + 4 - ((1 - w) * shape(l.SHP1, p, false) + w * shape(l.SHP2, p, true)) * A; grid(g, W, H);
			line(g, W, x => y(x / W, 0), inkA(.3), 1.2); line(g, W, x => y(x / W, 1), inkA(.3), 1.2, [3, 3]); line(g, W, x => y(x / W, mix), cssv("--ink"), 2.2);
			label(g, `${SHAPES[l.SHP1] || "?"} ${Math.round((1 - mix) * 100)}% · ${(SHAPES[l.SHP2] || "?").toLowerCase()} inv ${Math.round(mix * 100)}%`);
		},
		handles(W, H) { return [{ x: 8 + V.tracks[S.sel].lfo.SHMIX / 127 * (W - 16), y: H - 10, k: "SHMIX", c: cssv("--ink"), drag: x => ({ SHMIX: clamp(Math.round((x - 8) / (W - 16) * 127)) }) }]; }
	},
	/* LFO MOTION: the LFO across one bar of this track: SPD cycles (more as it rises), DEPTH high (dashed
	   bounds); UPDTE TRIG restarts it on every trig (the ticks), HOLD keeps the value a trig takes. Dot = SPD
	   sideways, DEPTH up. */
	lmotion: {
		to: () => ({ t: S.sel, g: "lfo" }),
		geo(W, H) { return { x0: 8, x1: W - 8, A: H / 2 - 18, mid: H / 2 + 2 }; },
		draw(g, W, H) {
			const tr = V.tracks[S.sel], l = tr.lfo, G = this.geo(W, H), mix = l.SHMIX / 127, dep = l.DEPTH / 127, cyc = .5 + l.SPD / 127 * 7.5, ink = cssv("--ink"); grid(g, W, H);
			const wave = u => ((1 - mix) * shape(l.SHP1, ((u % 1) + 1) % 1, false) + mix * shape(l.SHP2, ((u % 1) + 1) % 1, true)) * dep;
			const trigs = Array.from({ length: 16 }, (_, s) => tr.trigs[s] ? s : -1).filter(s => s >= 0), last = s => trigs.filter(x => x <= s).pop();
			const at = x => { const s = x / W * 16, k = last(s); if (l.UPDTE === "FREE" || k == null) return l.UPDTE === "HOLD" ? 0 : wave(s / 16 * cyc); return l.UPDTE === "TRIG" ? wave((s - k) / 16 * cyc) : wave(k / 16 * cyc); };
			g.strokeStyle = inkA(.45); g.lineWidth = 1; g.setLineDash([2, 4]); g.beginPath(); g.moveTo(0, G.mid - dep * G.A); g.lineTo(W, G.mid - dep * G.A); g.moveTo(0, G.mid + dep * G.A); g.lineTo(W, G.mid + dep * G.A); g.stroke(); g.setLineDash([]);
			line(g, W, x => G.mid - at(x) * G.A, ink, 2.2);
			g.fillStyle = ink; trigs.forEach(s => g.fillRect(Math.round(s / 16 * W) + 1, H - 6, 3, 6));
			label(g, `${l.UPDTE} · one bar`);
		},
		handles(W, H) {
			const l = V.tracks[S.sel].lfo, G = this.geo(W, H);
			return [{ x: G.x0 + l.SPD / 127 * (G.x1 - G.x0), y: G.mid - l.DEPTH / 127 * G.A, k: "SPD · DEPTH", c: cssv("--ink"),
				drag: (x, y) => ({ SPD: clamp(Math.round((x - G.x0) / (G.x1 - G.x0) * 127)), DEPTH: clamp(Math.round((G.mid - y) / G.A * 127)) }) }];
		}
	},
	eq: {
		to: toMfx("eq"),
		resp(u) { const v = V.mfx.eq.v; return (v.LG - 64) / 64 / (1 + Math.exp((u - v.LF / 127) * 18)) + (v.HG - 64) / 64 / (1 + Math.exp(-(u - v.HF / 127) * 18)) + (v.PG - 64) / 64 * Math.exp(-Math.pow((u - v.PF / 127) * (4 + v.PQ / 8), 2)); },
		draw(g, W, H) { grid(g, W, H); line(g, W, x => H / 2 - this.resp(x / W) * H / 3, cssv("--ink"), 2); label(g, "master EQ"); },
		handles(W, H) {
			const v = V.mfx.eq.v, yy = u => H / 2 - this.resp(u) * H / 3, gy = y => clamp(Math.round(64 + (H / 2 - y) / (H / 3) * 64));
			return [["LF", "LG"], ["PF", "PG"], ["HF", "HG"]].map(([fk, gk]) => ({ x: v[fk] / 127 * W, y: yy(v[fk] / 127), k: fk, c: cssv("--ink"), drag: (x, y) => ({ [fk]: clamp(Math.round(x / W * 127)), [gk]: gy(y) }) }));
		}
	},
	dyn: {
		to: toMfx("dyn"),
		out(i) { const v = V.mfx.dyn.v, th = v.TRHD / 127, r = 1 + v.RTIO / 127 * 9, kn = Math.max(.001, v.KNEE / 127 * .25); return i <= th - kn ? i : i >= th + kn ? th + (i - th) / r : i + ((1 / r - 1) * Math.pow(i - th + kn, 2)) / (4 * kn); },
		draw(g, W, H) {
			grid(g, W, H); g.strokeStyle = inkA(0.45); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 6); g.lineTo(W, 6); g.stroke(); g.setLineDash([]);
			line(g, W, x => H - 6 - this.out(x / W) * (H - 12), cssv("--ink"), 2); label(g, "in → out");
		},
		handles(W, H) {
			const v = V.mfx.dyn.v, th = v.TRHD / 127; return [{ x: th * W, y: H - 6 - this.out(th) * (H - 12), k: "TRHD", c: cssv("--ink"), drag: (x) => ({ TRHD: clamp(Math.round(x / W * 127)) }) },
			{ x: W - 6, y: H - 6 - this.out(1) * (H - 12), k: "RTIO", c: cssv("--ink"), drag: (x, y) => { const o = (H - 6 - y) / (H - 12), th = v.TRHD / 127; const r = (1 - th) / Math.max(.01, o - th); return { RTIO: clamp(Math.round((r - 1) / 9 * 127)) }; } }];
		}
	}
};
function inkA(a) { const h = cssv("--ink").replace("#", ""); const n = parseInt(h.length === 3 ? h.split("").map(c => c + c).join("") : h, 16); return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`; }
/* The Sound chain's small screens, one per module (renderSound): each draws one stage of the track's
   sound and its dots move that stage's knobs. */
const fxOf = () => V.tracks[S.sel].fx, rtOf = () => V.tracks[S.sel].rt;
/* AMP MOD: the level the tremolo leaves (1 down to 1 - AMD), AMF cycles across. Dot = AMF sideways, AMD up. */
ED.am = {
	to: toTrack("fx"),
	geo(W, H) { return { x0: 8, x1: W - 8, y0: 24, y1: H - 8 }; },
	draw(g, W, H) {
		const f = fxOf(), G = this.geo(W, H), dep = f.AMD / 127, cyc = 1 + f.AMF / 127 * 7; grid(g, W, H);
		line(g, W, x => { const u = clamp((x - G.x0) / (G.x1 - G.x0), 0, 1); return G.y1 - (1 - dep * (.5 - .5 * Math.cos(2 * Math.PI * cyc * u))) * (G.y1 - G.y0); }, cssv("--ink"), 2);
		label(g, "tremolo");
	},
	handles(W, H) {
		const f = fxOf(), G = this.geo(W, H);
		return [{ x: G.x0 + f.AMF / 127 * (G.x1 - G.x0), y: G.y1 - (1 - f.AMD / 127) * (G.y1 - G.y0), k: "AM", c: cssv("--ink"),
			drag: (x, y) => ({ AMF: clamp(Math.round((x - G.x0) / (G.x1 - G.x0) * 127)), AMD: 127 - clamp(Math.round((G.y1 - y) / (G.y1 - G.y0) * 127)) }) }];
	}
};
/* EQ: one bell at EQF, EQG up = boost, down = cut. */
ED.peq = {
	to: toTrack("fx"),
	y(u, H) { const f = fxOf(); return H / 2 - (f.EQG - 64) / 64 * (H / 2 - 18) * Math.exp(-Math.pow((u - f.EQF / 127) * 7, 2)); },
	draw(g, W, H) {
		grid(g, W, H); g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H / 2 + .5); g.lineTo(W, H / 2 + .5); g.stroke(); g.setLineDash([]);
		line(g, W, x => this.y(x / W, H), cssv("--ink"), 2); label(g, "eq");
	},
	handles(W, H) {
		const f = fxOf();
		return [{ x: f.EQF / 127 * W, y: this.y(f.EQF / 127, H), k: "EQ", c: cssv("--ink"), drag: (x, y) => ({ EQF: clamp(Math.round(x / W * 127)), EQG: clamp(Math.round(64 + (H / 2 - y) / (H / 2 - 18) * 64)) }) }];
	}
};
/* FILTER: the response of the 24 dB filter, a pass band from FLTF to FLTF + FLTW with FLTQ peaks at its edges. */
ED.flt = {
	to: toTrack("fx"),
	resp(u) {
		const f = fxOf(), hp = f.FLTF / 127, lp = Math.min(1, hp + f.FLTW / 127), q = f.FLTQ / 127; let d = 0;
		if (u < hp) d -= Math.pow((hp - u) * 7, 2); if (u > lp) d -= Math.pow((u - lp) * 7, 2);
		d += q * 1.6 * (Math.exp(-Math.pow((u - hp) * 28, 2)) * (hp > .01 ? 1 : 0) + Math.exp(-Math.pow((u - lp) * 28, 2)) * (lp < .99 ? 1 : 0)); return d;
	},
	yy(u, H) { return clamp(H / 2 - this.resp(u) * (H / 4), 6, H - 6); },
	draw(g, W, H) { grid(g, W, H); line(g, W, x => this.yy(x / W, H), cssv("--ink"), 2.2); label(g, "filter"); },
	handles(W, H) {
		const f = fxOf(), hp = f.FLTF / 127, lp = Math.min(1, hp + f.FLTW / 127);
		return [{ x: Math.max(6, hp * W), y: this.yy(hp, H), k: "FLTF", c: cssv("--ink"), drag: (x, y) => ({ FLTF: clamp(Math.round(x / W * 127)), FLTQ: clamp(Math.round((H / 2 - y) / (H / 2) * 127)) }) },
		{ x: Math.min(W - 6, lp * W), y: this.yy(lp, H), k: "FLTW", c: cssv("--ink"), drag: x => ({ FLTW: clamp(Math.round((x / W - fxOf().FLTF / 127) * 127)) }) }];
	}
};
/* CRUSH: a sine held for longer steps as SRR rises (0 = smooth). Dot = SRR sideways. */
ED.srr = {
	to: toTrack("fx"),
	draw(g, W, H) {
		const f = fxOf(), step = 1 + f.SRR / 127 * W / 5, y = h => H / 2 + 4 - .4 * (H - 30) * Math.sin(2 * Math.PI * 1.5 * h / W); grid(g, W, H);
		line(g, W, x => y(Math.floor(x / step) * step), cssv("--ink"), 2); label(g, "srr");
	},
	handles(W, H) { return [{ x: 8 + fxOf().SRR / 127 * (W - 16), y: H - 12, k: "SRR", c: cssv("--ink"), drag: x => ({ SRR: clamp(Math.round((x - 8) / (W - 16) * 127)) }) }]; }
};
/* DRIVE: DIST's transfer curve, input to output (dashed = clean). */
ED.dist = {
	to: toTrack("rt"),
	g() { return 1 + (rtOf().DIST || 0) / 127 * 8; }, sh(x, g) { return Math.tanh(x * g) / Math.tanh(g); },
	draw(g, W, H) {
		const G = this.g(); grid(g, W, H);
		g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 8); g.lineTo(W, 8); g.stroke(); g.setLineDash([]);
		line(g, W, x => { const u = x / W * 2 - 1; return H / 2 - this.sh(u, G) * (H / 2 - 8); }, cssv("--ink"), 2.2);
		label(g, "drive");
	},
	handles(W, H) {
		const self = this, G = this.g(), u = .4, yD = H / 2 - this.sh(u, G) * (H / 2 - 8);
		return [{ x: (u + 1) / 2 * W, y: yD, k: "DIST", c: cssv("--ink"), drag: (x, y) => { const want = (H / 2 - y) / (H / 2 - 8); let best = 0, bd = 9; for (let d = 0; d <= 127; d++) { const o = self.sh(u, 1 + d / 127 * 8); if (Math.abs(o - want) < bd) { bd = Math.abs(o - want); best = d; } } return { DIST: best }; } }];
	}
};
/* MIX: the track in the stereo field: PAN sideways, VOL up. */
ED.pan = {
	to: toTrack("rt"),
	geo(W, H) { return { x0: 12, x1: W - 12, yt: 26, yb: H - 18 }; },
	draw(g, W, H) {
		const r = rtOf(), G = this.geo(W, H), ink = cssv("--ink"), x = G.x0 + (r.PAN ?? 64) / 127 * (G.x1 - G.x0), y = G.yb - (r.VOL ?? 0) / 127 * (G.yb - G.yt), pan = (r.PAN ?? 64) - 64; grid(g, W, H);
		g.strokeStyle = inkA(.45); g.lineWidth = 1; g.setLineDash([3, 3]); g.beginPath(); g.moveTo(W / 2 + .5, G.yt - 6); g.lineTo(W / 2 + .5, G.yb); g.stroke(); g.setLineDash([]);
		g.fillStyle = inkA(.22); g.fillRect(x - 7, y, 14, G.yb - y); g.fillStyle = ink; g.fillRect(G.x0, G.yb, G.x1 - G.x0, 1.5);
		g.font = "10px Silkscreen, ui-monospace, monospace"; g.fillText("L", G.x0, H - 5); g.fillText("R", G.x1 - 6, H - 5);
		label(g, "pan " + (pan ? (pan < 0 ? "L" : "R") + Math.abs(pan) : "C"));
	},
	handles(W, H) {
		const r = rtOf(), G = this.geo(W, H);
		return [{ x: G.x0 + (r.PAN ?? 64) / 127 * (G.x1 - G.x0), y: G.yb - (r.VOL ?? 0) / 127 * (G.yb - G.yt), k: "PAN · VOL", c: cssv("--ink"),
			drag: (x, y) => ({ ...("PAN" in r ? { PAN: clamp(Math.round((x - G.x0) / (G.x1 - G.x0) * 127)) } : {}), ...("VOL" in r ? { VOL: clamp(Math.round((G.yb - y) / (G.yb - G.yt) * 127)) } : {}) }) }];
	}
};
/* SENDS: the DEL and REV send levels as two bars. */
ED.sends = {
	to: toTrack("rt"),
	geo(W, H) { return { yt: 26, yb: H - 18, col: i => W * (i ? .7 : .3) }; },
	draw(g, W, H) {
		const r = rtOf(), G = this.geo(W, H), ink = cssv("--ink"); grid(g, W, H); g.font = "10px Silkscreen, ui-monospace, monospace";
		[["DEL", r.DEL], ["REV", r.REV]].forEach(([k, v], i) => {
			const cx = G.col(i), w = Math.min(22, W / 6), y = G.yb - (v ?? 0) / 127 * (G.yb - G.yt);
			g.fillStyle = inkA(.18); g.fillRect(cx - w / 2, G.yt, w, G.yb - G.yt); g.fillStyle = ink; g.fillRect(cx - w / 2, y, w, G.yb - y);
			g.fillText(k, cx - g.measureText(k).width / 2, H - 5);
		});
		label(g, "sends");
	},
	handles(W, H) {
		const r = rtOf(), G = this.geo(W, H), vy = y => clamp(Math.round((G.yb - y) / (G.yb - G.yt) * 127));
		return ["DEL", "REV"].map((k, i) => k in r && { x: G.col(i), y: G.yb - r[k] / 127 * (G.yb - G.yt), k, c: cssv("--ink"), drag: (x, y) => ({ [k]: vy(y) }) }).filter(Boolean);
	}
};
/* RETRIG: the trig, then RTRG more hits RTIM apart, fading. Dots = RTIM (the second hit) and RTRG (the last). */
ED.rtrg = {
	to: toTrack("syn"),
	geo(W, H) { const s = V.tracks[S.sel].syn, span = (W - 20) / 3; return { x0: 10, span, sp: 6 + (s.RTIM ?? 0) / 127 * span, n: s.RTRG ?? 0, yt: 24, yb: H - 10 }; },
	draw(g, W, H) {
		const G = this.geo(W, H); grid(g, W, H);
		for (let i = 0; i <= G.n; i++) {
			const x = G.x0 + i * G.sp; if (x > W - 4) break; const a = 1 - .7 * i / Math.max(1, G.n), top = G.yt + (1 - a) * (G.yb - G.yt);
			g.fillStyle = i ? inkA(.3 + .6 * a) : cssv("--ink"); g.fillRect(Math.round(x) - 1.5, top, 3, G.yb - top);
		}
		label(g, G.n ? "retrig ×" + G.n : "no retrig");
	},
	handles(W, H) {
		const G = this.geo(W, H);
		return [{ x: G.x0 + G.sp, y: G.yt + 4, k: "RTIM", c: cssv("--ink"), drag: x => ({ RTIM: clamp(Math.round((x - G.x0 - 6) / G.span * 127)) }) },
		{ x: Math.min(W - 6, G.x0 + G.n * G.sp), y: G.yb - 6, k: "RTRG", c: cssv("--ink"), drag: x => ({ RTRG: clamp(Math.round((x - G.x0) / G.sp)) }) }];
	}
};
/* ===== The synthesis groups' screens (SYN_TAB): one editor per kind of picture, for every machine
   whose group has its knobs (the canvas's data-k, the page's names). An editor is a model: from the
   knobs' values (P, with one value tried in place: P.with) it gives what to paint and its dots, each
   a knob ("x", "y") or two ([nx, ny]) at a point of the picture. A dot's drag tries the knob's 128
   values and keeps the one whose point is nearest the pointer, so every dot stays on its picture. ===== */
const synOf = () => V.tracks[S.sel].syn;
/* a knob's value by its plain name (the page may call it SYN·NAME), and the name it is sent by */
const synQ = n => ("SYN·" + n) in synOf() ? "SYN·" + n : n;
const synV = n => synOf()[synQ(n)];
function synEd(model) {
	const run = (c, W, H, over) => {
		const ks = (c.dataset.k || "").split(" ").filter(Boolean), P = n => over && n in over ? over[n] : synV(n) ?? 64;
		const role = (...l) => l.find(n => ks.includes(n)) || null;
		const G = { W, H, X: u => 8 + u * (W - 16), Y: v => H - 10 - v * (H - 34), mid: H / 2 + 7, amp: H / 2 - 19 };
		return model(P, role, G, ks);
	};
	const pick = (c, W, H, key, part, n, px, py) => {
		let best = synV(n) ?? 0, bd = Infinity;
		for (let v = 0; v < 128; v++) { const h = run(c, W, H, { [n]: v }).h.find(e => String(e[0]) === key); if (!h) continue; const d = part === "x" ? Math.abs(h[1] - px) : Math.abs(h[2] - py); if (d < bd) { bd = d; best = v; } }
		return best;
	};
	return {
		to: toTrack("syn"),
		draw(g, W, H, c) { grid(g, W, H); const m = run(c, W, H); m.paint(g); if (m.label) label(g, m.label); },
		handles(W, H, c) {
			return run(c, W, H).h.map(([n, x, y, ax]) => ({ x, y, k: [].concat(n).join(" · "), c: cssv("--ink"),
				drag: (px, py) => Array.isArray(n) ? { [synQ(n[0])]: pick(c, W, H, String(n), "x", n[0], px, py), [synQ(n[1])]: pick(c, W, H, String(n), "y", n[1], px, py) } : { [synQ(n)]: pick(c, W, H, String(n), ax, n, px, py) } }));
		}
	};
}
/* painting helpers: a curve over u = 0..1, a dashed level, noise that is the same on every draw */
const inkL = () => cssv("--ink");
function curveU(g, G, f, c, w, dash) { line(g, G.W, x => f(clamp((x - 8) / (G.W - 16), 0, 1)), c, w, dash); }
function hLine(g, y, W, a = .35) { g.strokeStyle = inkA(a); g.lineWidth = 1; g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, Math.round(y) + .5); g.lineTo(W, Math.round(y) + .5); g.stroke(); g.setLineDash([]); }
const rnd = i => { const s = Math.sin(i * 12.9898 + 78.233) * 43758.5453; return (s - Math.floor(s)) * 2 - 1; };
/* a knob without a place in the picture: a dot on a short rail at the screen's foot, its name beside */
function rail(G, i, n, P) { const y = G.H - 10 - i * 14, x0 = G.W * .62, x1 = G.W - 10; return { n, x0, x1, y, x: x0 + P(n) / 127 * (x1 - x0) }; }
function paintRails(g, rails) {
	g.font = "9px Silkscreen, ui-monospace, monospace";
	if (!rails.length) return;
	const lw = Math.max(...rails.map(r => g.measureText(r.n).width)), top = Math.min(...rails.map(r => r.y)) - 8;
	g.fillStyle = cssv("--lcd"); g.globalAlpha = .85; g.fillRect(rails[0].x0 - lw - 14, top, rails[0].x1 - rails[0].x0 + lw + 22, rails[0].y + 7 - top); g.globalAlpha = 1;
	rails.forEach(r => { g.fillStyle = inkA(.25); g.fillRect(r.x0, r.y - 1, r.x1 - r.x0, 2); g.fillStyle = inkA(.7); g.fillText(r.n, r.x0 - g.measureText(r.n).width - 8, r.y + 3); });
}
/* PITCH: the pitch after a trig: PTCH the note (dashed), RAMP or BUMP above it falling back at RDEC or
   BENV, BEND up or down into it. */
ED.pitch = synEd((P, role, G) => {
	const p = role("PTCH"), a = role("RAMP", "BUMP", "BEND"), t = role("RDEC", "BENV");
	const b = p ? .12 + .5 * P(p) / 127 : .3, amt = !a ? 0 : a === "BEND" ? (P(a) - 64) / 64 * .3 : P(a) / 127 * .38, tau = t ? .01 + P(t) / 127 * .3 : a === "BEND" ? .12 : .05;
	const f = u => b + amt * Math.exp(-u / tau), h = [];
	if (p) h.push([p, G.X(1) - 4, G.Y(b), "y"]);
	if (a) h.push([a, G.X(0) + 2, G.Y(f(0)), "y"]);
	if (t) h.push([t, G.X(tau), G.Y(b + amt / Math.E), "x"]);
	return { h, label: a === "BEND" ? "pitch bend" : a ? "pitch " + a.toLowerCase() : "pitch", paint(g) { hLine(g, G.Y(b), G.W); curveU(g, G, u => G.Y(f(u)), inkL(), 2.2); } };
});
/* ENVELOPE: a level after a trig (or the input's gate): ATT rises, HOLD keeps it, DEC lets it fall;
   DAMP shortens the tail, STOP CLOS GRAB cut it (127 = never), CLIC adds a click, STRT skips the start
   (dashed: what is skipped), REV plays the shake backwards. AVOL FDPH set its height. */
ED.env = synEd((P, role, G) => {
	const at = role("ATT", "ATCK", "FATK"), ho = role("HOLD", "SUS", "HLD", "AHLD", "FHLD"), de = role("DEC", "ADEC", "FDEC", "REL"), lv = role("AVOL", "FDPH");
	const cu = role("STOP", "CLOS", "GRAB"), dm = role("DAMP"), ck = role("CLIC"), st = role("STRT", "START"), rv = role("REV");
	const a = at ? .004 + P(at) / 127 * .25 : .008, hd = ho ? P(ho) / 127 * .4 : 0, k = (.03 + (de ? P(de) : 64) / 127 * .35) * (dm ? 1 - .7 * P(dm) / 127 : 1);
	const L = (lv ? P(lv) / 127 : 1) * (ck ? .78 : 1), cut = cu && P(cu) < 127 ? .04 + P(cu) / 127 * .9 : 2, s = st ? P(st) / 127 * .35 : 0;
	const lvl0 = u => u < 0 ? 0 : u < a ? u / a : u < a + hd ? 1 : Math.exp(-(u - a - hd) / k), lvl = u => (u > cut ? 0 : lvl0(u + s)) * L;
	const h = [], rails = [];
	if (at) h.push([at, G.X(a), G.Y(L), "x"]);
	if (ho) h.push([ho, G.X(a + hd), G.Y(L), "x"]);
	if (de) h.push([de, G.X(clamp(a + hd + k * Math.log(4) - s, 0, 1)), G.Y(L / 4), "x"]);
	if (lv) h.push([lv, G.X(a + hd * .5) + (ho ? 0 : 10), G.Y(L), "y"]);
	if (cu) h.push([cu, Math.min(G.X(cut), G.W - 6), G.Y(0) - 4, "x"]);
	if (ck) h.push([ck, G.X(0) + 2, G.Y(L + P(ck) / 127 * .22), "y"]);
	if (st) h.push([st, G.X(s), G.Y(lvl0(s) * L), "x"]);
	if (dm) rails.push(rail(G, 0, dm, P));
	if (rv) rails.push(rail(G, rails.length, rv, P));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	const parts = [st && "start", at && "attack", ho && "hold", de === "REL" ? "release" : "decay", cu && cu.toLowerCase()].filter(Boolean);
	return { h, label: parts.join(" · "), paint(g) {
		if (s) curveU(g, G, u => G.Y(lvl0(u) * L), inkA(.35), 1.2, [3, 3]);
		if (rv) curveU(g, G, u => G.Y(lvl(1 - u) * P(rv) / 127), inkA(.4), 1.2, [2, 3]);
		curveU(g, G, u => G.Y(lvl(u)), inkL(), 2.2);
		if (ck) { g.fillStyle = inkL(); g.fillRect(G.X(0), G.Y(L + P(ck) / 127 * .22), 3, G.Y(0) - G.Y(L + P(ck) / 127 * .22)); }
		paintRails(g, rails);
	} };
});
/* ATTACK: the first moments of the hit: a click (STRT TICK CLIC SNAP) SPLEN long, a noise burst (NOIS),
   a second attack (DUAL) over the body's first cycles (faint). */
ED.atk = synEd((P, role, G) => {
	const ck = role("STRT", "TICK", "CLIC", "SNAP"), nz = role("NOIS"), du = role("DUAL"), ln = role("SPLEN");
	const c = ck ? P(ck) / 127 : 0, w = .012 + (ln ? P(ln) / 127 * .12 : .025), z = nz ? P(nz) / 127 : 0, d = du ? P(du) / 127 : 0;
	const body = u => .5 * Math.exp(-u / .7) * Math.sin(2 * Math.PI * 6 * u), clk = u => u < w ? c * (1 - u / w) : 0, ne = u => z * Math.exp(-u / .08), dl = u => u > .09 && u < .09 + w ? d * .8 * (1 - (u - .09) / w) : 0;
	const h = [];
	if (ck) h.push([ck, G.X(0) + 2, G.mid - G.amp * c, "y"]);
	if (ln) h.push([ln, G.X(w), G.mid, "x"]);
	if (nz) h.push([nz, G.X(.14), G.mid - G.amp * ne(.14), "y"]);
	if (du) h.push([du, G.X(.09) + 2, G.mid - G.amp * d * .8, "y"]);
	return { h, label: "attack", paint(g) {
		curveU(g, G, u => G.mid - G.amp * body(u), inkA(.35), 1.2);
		if (nz) curveU(g, G, u => G.mid - G.amp * ne(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * Math.max(-1, Math.min(1, body(u) + clk(u) + dl(u) + ne(u) * rnd(Math.round(u * 400)))), inkL(), 1.8);
	} };
});
/* WAVE: two cycles through the tone stage: HARM TONE add harmonics, CLIP DIST drive it, DTYP from soft
   to hard, DIRT reduces its bits (dashed = clean). */
ED.wave = synEd((P, role, G) => {
	const hm = role("HARM", "TONE"), cl = role("CLIP", "DIST"), ht = role("DTYP"), dr = role("DIRT");
	const H_ = hm ? P(hm) / 127 : 0, gain = 1 + (cl ? P(cl) / 127 : 0) * 7, hard = ht ? P(ht) / 127 : 0, bits = dr ? 2 + (1 - P(dr) / 127) * 30 : 0;
	const src = u => (Math.sin(2 * Math.PI * 2 * u) + H_ * .45 * Math.sin(2 * Math.PI * 6 * u)) / (1 + H_ * .3);
	const shp = x => { const s = Math.tanh(gain * x) / Math.tanh(gain), q = clamp(gain * x, -1, 1); let y = (1 - hard) * s + hard * q; if (bits) y = Math.round(y * bits) / bits; return y; };
	const rails = [], h = [];
	if (cl) h.push([cl, G.X(.06), G.mid - G.amp * shp(src(.06)), "y"]);
	if (hm) h.push([hm, G.X(.208), G.mid - G.amp * shp(src(.208)), "y"]);
	if (ht) rails.push(rail(G, 0, ht, P));
	if (dr) rails.push(rail(G, rails.length, dr, P));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: dr ? "bits · drive" : cl && !hm ? "drive" : "tone", paint(g) {
		curveU(g, G, u => G.mid - G.amp * src(u), inkA(.35), 1.2, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * shp(src(u)), inkL(), 2.2); paintRails(g, rails);
	} };
});
/* CLAPS: the hands one after another: CLPY CLPS how many, RATE how far apart, HARD how sharp, CDEC the
   last one's tail. */
function clapsOf(P, cnt, rate, hard, cdec) {
	const n = 1 + Math.round((cnt ? P(cnt) : 64) / 127 * 7), sp = .025 + (rate ? P(rate) / 127 : .4) * .1, hd = hard ? P(hard) / 127 : .5, dk = .02 + (cdec ? P(cdec) / 127 : .4) * .2;
	const env = u => { let e = 0; for (let i = 0; i < n; i++) { const d = u - i * sp; if (d < 0) break; const last = i === n - 1; e = Math.max(e, (last ? 1 : .55 + .35 * hd) * Math.exp(-d / (last ? dk : .006 + .012 * (1 - hd)))); } return e; };
	return { n, sp, hd, dk, env, end: (n - 1) * sp };
}
ED.claps = synEd((P, role, G) => {
	const cnt = role("CLPY", "CLPS"), rate = role("RATE"), hard = role("HARD"), cdec = role("CDEC"), C = clapsOf(P, cnt, rate, hard, cdec), h = [];
	if (cnt) h.push([cnt, G.X(C.end), G.mid - G.amp, "x"]);
	if (rate && C.n > 1) h.push([rate, G.X(C.sp), G.mid - G.amp * (.55 + .35 * C.hd), "x"]);
	if (hard) h.push([hard, G.X(0) + 2, G.mid - G.amp * (C.n > 1 ? .55 + .35 * C.hd : 1), "y"]);
	if (cdec) h.push([cdec, G.X(C.end + C.dk), G.mid - G.amp / Math.E, "x"]);
	return { h, label: `${C.n} ${C.n > 1 ? "claps" : "clap"}`, paint(g) {
		curveU(g, G, u => G.mid - G.amp * C.env(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * C.env(u) * rnd(Math.round(u * 500)), inkL(), 1.6);
	} };
});
/* ROOM: the claps (faint), then the room: ROOM how loud, RSIZ how long, RTUN its tone (smoother lower). */
ED.room = synEd((P, role, G) => {
	const C = clapsOf(P, "CLPY", "RATE", "HARD"), R = P("ROOM") / 127 * .8, len = .06 + P("RSIZ") / 127 * .5, sm = 1 + Math.round((1 - P("RTUN") / 127) * 6), t0 = C.end + .02;
	const tail = u => u < t0 ? 0 : R * Math.exp(-(u - t0) / len) * Math.min(1, (u - t0) / .03);
	const nz = u => { let s = 0; const i = Math.round(u * 500); for (let k = 0; k < sm; k++) s += rnd(i - k); return s / Math.sqrt(sm); };
	const r = rail(G, 0, "RTUN", P);
	return { h: [["ROOM", G.X(t0 + .05), G.mid - G.amp * tail(t0 + .05), "y"], ["RSIZ", G.X(t0 + .03 + len), G.mid - G.amp * tail(t0 + .03 + len), "x"], ["RTUN", r.x, r.y, "x"]], label: "room", paint(g) {
		curveU(g, G, u => G.mid - G.amp * C.env(u) * rnd(Math.round(u * 500)), inkA(.35), 1.2);
		curveU(g, G, u => G.mid - G.amp * tail(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * clamp(tail(u) * nz(u), -1, 1), inkL(), 1.6); paintRails(g, [r]);
	} };
});
/* SPECTRUM: the sound's tone, low to high: TONE TTUN where its colour sits, RICH TOP ENH HARD how much
   of it; BR the lows, AU less highs, AG more highs. */
ED.spec = synEd((P, role, G) => {
	const ce = role("TONE", "TTUN"), lv = role("RICH", "TOP", "ENH", "HARD"), br = role("BR"), au = role("AU"), ag = role("AG");
	const c = ce ? .15 + P(ce) / 127 * .75 : .5, L = lv ? P(lv) / 127 : 0, sg = x => 1 / (1 + Math.exp(-x));
	const f = u => .45 - .2 * u + (ce || lv ? L * .4 * Math.exp(-Math.pow((u - c) / .1, 2)) : 0) + (br ? P(br) / 127 * .3 * sg((.25 - u) * 18) : 0) - (au ? P(au) / 127 * .3 * sg((u - .6) * 14) : 0) + (ag ? P(ag) / 127 * .3 * sg((u - .8) * 22) : 0);
	const h = [];
	if (ce && lv) h.push([[ce, lv], G.X(c), G.Y(f(c)), "xy"]);
	if (br) h.push([br, G.X(.06), G.Y(f(.06)), "y"]);
	if (au) h.push([au, G.X(.72), G.Y(f(.72)), "y"]);
	if (ag) h.push([ag, G.X(.97), G.Y(f(.97)), "y"]);
	return { h, label: br ? "low · high" : "tone", paint(g) {
		g.fillStyle = inkA(.12); g.beginPath(); g.moveTo(0, G.H); for (let x = 0; x <= G.W; x++) g.lineTo(x, G.Y(f(clamp((x - 8) / (G.W - 16), 0, 1)))); g.lineTo(G.W, G.H); g.fill();
		curveU(g, G, u => G.Y(f(u)), inkL(), 2.2);
	} };
});
/* METAL: the partials of a metal or a shell, low to high: PTCH moves them, GAP TUNE spread them, SIZE
   makes the body bigger (lower), MTAL RICH CLSN HARD lift the upper ones, RING PEAK add the highest. */
ED.metal = synEd((P, role, G) => {
	const pt = role("PTCH"), sp = role("GAP", "TUNE"), sz = role("SIZE"), a1 = role("MTAL", "RICH", "CLSN", "HARD"), a2 = role("RING", "PEAK");
	const R = [1, 1.47, 1.93, 2.41, 2.88, 3.36, 3.83, 4.3, 4.9, 5.4, 5.9, 6.5];
	const f0 = (.05 + (pt ? P(pt) / 127 : .3) * .2) * (sz ? 1.4 - P(sz) / 127 * .8 : 1), spr = .45 + (sp ? P(sp) / 127 : .4) * .7;
	const up = a1 ? P(a1) / 127 : .5, hi = a2 ? P(a2) / 127 : 0;
	const parts = R.map((r, k) => ({ u: f0 * (1 + (r - 1) * spr), v: k === 0 ? .85 : k < 8 ? (.2 + .65 * up) * (1 - k / 11) * (.75 + .25 * Math.abs(rnd(k))) : hi * (.8 - (k - 8) * .12) })).filter(p => p.u <= 1);
	const at = k => parts[Math.min(k, parts.length - 1)], h = [];
	if (pt) h.push([pt, G.X(at(0).u), G.Y(at(0).v), "x"]);
	if (sz) h.push([sz, G.X(at(0).u), G.Y(at(0).v), "x"]);
	if (sp) h.push([sp, G.X(at(3).u), G.Y(at(3).v), "x"]);
	if (a1) h.push([a1, G.X(at(2).u), G.Y(at(2).v), "y"]);
	if (a2 && parts.length > 8) h.push([a2, G.X(parts[8].u), G.Y(parts[8].v), "y"]);
	else if (a2) h.push([a2, G.X(.98), G.Y(hi * .8), "y"]);
	return { h, label: "partials", paint(g) { g.fillStyle = inkL(); parts.forEach(p => { const x = G.X(p.u), y = G.Y(p.v); g.fillRect(Math.round(x) - 1.5, y, 3, G.Y(0) - y); }); g.fillStyle = inkA(.4); g.fillRect(0, G.Y(0), G.W, 1.5); } };
});
/* OSC: the drum's body after a trig: PTCH how fast it swings, DEC how long (dashed), DAMP shortens it,
   TUNE beats against a second skin, RING ENH add overtones, SNAP a snap at the start. */
ED.osc = synEd((P, role, G) => {
	const pt = role("PTCH"), de = role("DEC"), dm = role("DAMP"), tu = role("TUNE"), rg = role("RING", "ENH"), sn = role("SNAP");
	const cyc = 2 + (pt ? P(pt) : 64) / 127 * 14, k = (.04 + (de ? P(de) : 64) / 127 * .55) * (dm ? 1 - .65 * P(dm) / 127 : 1);
	const tn = tu ? P(tu) / 127 : 0, rr = rg ? P(rg) / 127 : 0, s = sn ? P(sn) / 127 : 0, e = u => Math.exp(-u / k);
	const w = u => e(u) * ((Math.sin(2 * Math.PI * cyc * u) * (1 - .3 * tn) + .3 * tn * Math.sin(2 * Math.PI * cyc * (1 + .08 * tn) * u) + rr * .35 * Math.sin(2 * Math.PI * cyc * 2.7 * u)) / (1 + rr * .35)) + (u < .02 ? s * .6 * (1 - u / .02) : 0);
	const h = [], rails = [], q = 1 / (4 * cyc);
	if (pt) h.push([pt, G.X(q), G.mid - G.amp * w(q), "x"]);
	if (de) h.push([de, G.X(Math.min(1, k * Math.log(4))), G.mid - G.amp * .25, "x"]);
	if (sn) h.push([sn, G.X(0) + 2, G.mid - G.amp * clamp(w(0), -1, 1), "y"]);
	[dm, tu, rg].filter(Boolean).forEach(n => rails.push(rail(G, rails.length, n, P)));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: "body", paint(g) {
		curveU(g, G, u => G.mid - G.amp * e(u), inkA(.45), 1, [3, 3]); curveU(g, G, u => G.mid + G.amp * e(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * clamp(w(u), -1, 1), inkL(), 1.8); paintRails(g, rails);
	} };
});
/* FM: the carrier after a trig, its tone moved by the modulator: MOD SMOD how much (dashed: the
   modulation index), MDEC how long, MFRQ its frequency, MFB FB its feedback; SNAR SPTC SDEC the snare
   voice's level, pitch and decay. The carrier's pitch and decay are the track's PTCH and DEC. */
ED.fm = synEd((P, role, G) => {
	const md = role("MOD", "SMOD"), mf = role("MFRQ"), mk = role("MDEC"), fb = role("MFB", "FB"), lv = role("SNAR"), pt = role("SPTC", "PTCH"), de = role("SDEC", "DEC"), hp = role("HPF");
	const cyc = 2 + (pt ? P(pt) : synV("PTCH") ?? 64) / 127 * 12, k = .05 + (de ? P(de) : synV("DEC") ?? 64) / 127 * .55, L = lv ? .15 + P(lv) / 127 * .85 : 1;
	const I = u => (md ? P(md) : 64) / 127 * 5 * Math.exp(-u / (mk ? .02 + P(mk) / 127 * .5 : .15)), ratio = .5 + (mf ? P(mf) : 40) / 127 * 7.5, F = fb ? P(fb) / 127 * 1.4 : 0;
	const N = 600, ys = []; let prev = 0;
	for (let i = 0; i <= N; i++) { const u = i / N, y = L * Math.exp(-u / k) * Math.sin(2 * Math.PI * cyc * u + I(u) * Math.sin(2 * Math.PI * cyc * ratio * u) + F * prev); ys.push(y); prev = y; }
	const yI = u => G.Y(.55 + .45 * Math.min(1, I(u) / 5)), h = [], rails = [];
	if (md) h.push([md, G.X(0) + 2, yI(0), "y"]);
	if (mk) { const t = .02 + P(mk) / 127 * .5; h.push([mk, G.X(Math.min(1, t)), yI(t), "x"]); }
	if (lv) h.push([lv, G.X(.5), G.mid - G.amp * L * Math.exp(-.5 / k), "y"]);
	if (de) h.push([de, G.X(Math.min(1, k * Math.log(4))), G.mid + G.amp * L * .25, "x"]);
	[mf, fb, pt, hp].filter(Boolean).forEach(n => rails.push(rail(G, rails.length, n, P)));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: lv ? "snare voice · fm" : "fm", paint(g) {
		curveU(g, G, u => G.mid + G.amp * L * Math.exp(-u / k), inkA(.4), 1, [3, 3]);
		curveU(g, G, yI, inkA(.55), 1.2, [2, 3]);
		curveU(g, G, u => G.mid - G.amp * ys[Math.round(u * N)], inkL(), 1.6); paintRails(g, rails);
	} };
});
/* NOISE: a noise burst after a trig: NOISE RVOL SNAP how loud, NDEC RDEC how long, HPF thins it. */
ED.noise = synEd((P, role, G) => {
	const lv = role("NOISE", "RVOL", "SNAP"), de = role("NDEC", "RDEC"), hp = role("HPF");
	const L = lv ? P(lv) / 127 : .6, k = de ? .02 + P(de) / 127 * .4 : .07, sm = hp ? 1 + Math.round((1 - P(hp) / 127) * 5) : 3, e = u => L * Math.exp(-u / k);
	const nz = u => { let s = 0; const i = Math.round(u * 500); for (let j = 0; j < sm; j++) s += rnd(i - j); return s / Math.sqrt(sm); };
	const h = [], rails = [];
	if (lv) h.push([lv, G.X(0) + 2, G.mid - G.amp * L, "y"]);
	if (de) h.push([de, G.X(Math.min(1, k)), G.mid - G.amp * e(k), "x"]);
	if (hp) { rails.push(rail(G, 0, hp, P)); h.push([hp, rails[0].x, rails[0].y, "x"]); }
	return { h, label: "noise", paint(g) { curveU(g, G, u => G.mid - G.amp * e(u), inkA(.45), 1, [3, 3]); curveU(g, G, u => G.mid - G.amp * clamp(e(u) * nz(u), -1, 1), inkL(), 1.4); paintRails(g, rails); } };
});
/* STRIKE: the mallet's hit: HARD how hard (taller), HAMR a softer mallet (wider), TENS the skin's pitch
   rising under a hard hit (dashed), POS from the centre to the edge. */
ED.strike = synEd((P, role, G) => {
	const hd = role("HARD"), hm = role("HAMR"), te = role("TENS"), po = role("POS");
	const A = .3 + (hd ? P(hd) : 64) / 127 * .65, w = .02 + (hm ? P(hm) / 127 : .3) * .2, T = te ? P(te) / 127 : 0, ps = po ? P(po) / 127 : 0;
	const pulse = u => u < w ? A * Math.sin(Math.PI * u / w) : 0, ring = u => u < w ? 0 : A * .35 * Math.exp(-(u - w) / .25) * Math.sin(2 * Math.PI * (5 + 6 * ps) * (u - w)) * (1 - .5 * ps) + A * .15 * ps * Math.exp(-(u - w) / .25) * Math.sin(2 * Math.PI * 17 * (u - w));
	const pit = u => .62 + T * .3 * Math.exp(-u / .08), h = [], rails = [];
	if (hd) h.push([hd, G.X(w / 2), G.Y(A), "y"]);
	if (hm) h.push([hm, G.X(w), G.Y(.2), "x"]);
	if (te) h.push([te, G.X(0) + 2, G.Y(pit(0)), "y"]);
	if (po) { rails.push(rail(G, 0, po, P)); h.push([po, rails[0].x, rails[0].y, "x"]); }
	return { h, label: "strike", paint(g) {
		if (te) curveU(g, G, u => G.Y(pit(u)), inkA(.55), 1.2, [3, 3]);
		curveU(g, G, u => G.Y(.2 + pulse(u) * .78 + ring(u) * .5), inkL(), 2); hLine(g, G.Y(.2), G.W, .25); paintRails(g, rails);
	} };
});
/* GRAINS: the maraca's grains or rattle: GRNS RATL how many, DAMP fewer, GLEN how long each, SIZE how
   long the shake, HARD how loud each, RTYP the kind of rattle. */
ED.grains = synEd((P, role, G) => {
	const cn = role("GRNS", "RATL"), dm = role("DAMP"), gl = role("GLEN"), sz = role("SIZE"), hd = role("HARD"), ty = role("RTYP");
	const n = Math.round(6 + (cn ? P(cn) : 64) / 127 * 60 * (dm ? 1 - .75 * P(dm) / 127 : 1)), span = .2 + (sz ? P(sz) / 127 : .6) * .78, lw = 1 + (gl ? P(gl) / 127 : .3) * 5;
	const A = .3 + (hd ? P(hd) : 64) / 127 * .65, seed = ty ? Math.round(P(ty) / 16) * 31 : 0;
	const gr = Array.from({ length: n }, (_, i) => { const u = (i + .5 + rnd(i + seed) * .45) / n * span; return { u, v: A * Math.exp(-u / span * 1.4) * (.55 + .45 * Math.abs(rnd(i * 7 + seed))) }; });
	const h = [], rails = [];
	if (sz) h.push([sz, G.X(span), G.Y(0) - 2, "x"]);
	if (hd) h.push([hd, G.X(gr[0].u), G.Y(gr[0].v), "y"]);
	[cn, dm, gl, ty].filter(Boolean).forEach(x => rails.push(rail(G, rails.length, x, P)));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: `${n} grains`, paint(g) { g.fillStyle = inkL(); gr.forEach(p => { const y = G.Y(p.v); g.fillRect(Math.round(G.X(p.u)), y, lw, G.Y(0) - y); }); g.fillStyle = inkA(.4); g.fillRect(0, G.Y(0), G.W, 1.5); paintRails(g, rails); } };
});
/* TREMOLO: the level the modulation leaves: TREM MOD how deep (the dot's height), TFRQ MFRQ how fast. */
ED.trem = synEd((P, role, G) => {
	const dp = role("TREM", "MOD"), fq = role("TFRQ", "MFRQ"), d = P(dp) / 127, cyc = 1 + P(fq) / 127 * 9;
	return { h: [[[fq, dp], G.X(P(fq) / 127), G.Y(1 - d) + 2, "xy"]], label: "tremolo", paint(g) { curveU(g, G, u => G.Y(1 - d * (.5 - .5 * Math.cos(2 * Math.PI * cyc * u))), inkL(), 2); hLine(g, G.Y(1 - d), G.W, .3); } };
});
/* RESPONSE: a filter, low to high: HP HPF FILTF from below, LPF LP FFRQ from above (FILTW the band's
   width), HPQ FQ the peak at its edge. */
ED.resp = synEd((P, role, G) => {
	const hpn = role("HP", "HPF", "FILTF"), lpn = role("LPF", "LP", "FFRQ"), bw = role("FILTW"), qn = role("HPQ", "FQ");
	const hp = hpn ? P(hpn) / 127 : 0, lp = bw ? Math.min(1, hp + P(bw) / 127) : lpn ? P(lpn) / 127 : 1, q = qn ? P(qn) / 127 : 0, qAt = hpn && !lpn ? hp : lp;
	const d = u => (u < hp ? -Math.pow((hp - u) * 7, 2) : 0) + (u > lp ? -Math.pow((u - lp) * 7, 2) : 0) + q * 1.6 * Math.exp(-Math.pow((u - qAt) * 26, 2));
	const yy = u => clamp(G.H / 2 + 4 - d(u) * (G.H / 4), 6, G.H - 6), h = [];
	if (hpn) h.push(qn && !lpn ? [[hpn, qn], G.X(hp), yy(hp), "xy"] : [hpn, G.X(hp), yy(hp), "x"]);
	if (lpn) h.push(qn ? [[lpn, qn], G.X(lp), yy(lp), "xy"] : [lpn, G.X(lp), yy(lp), "x"]);
	if (bw) h.push([bw, G.X(lp), yy(lp), "x"]);
	return { h, label: hpn && (lpn || bw) ? "band" : hpn ? "high pass" : "low pass", paint(g) { curveU(g, G, yy, inkL(), 2.2); } };
});
/* IMPULSE: UP and DOWN how long it stays positive and negative, UVAL and DVAL how far. */
ED.imp = synEd((P, role, G) => {
	const u0 = .05, up = .02 + P("UP") / 127 * .42, dn = .02 + P("DOWN") / 127 * .42, uv = P("UVAL") / 127, dv = P("DVAL") / 127;
	const f = u => u < u0 ? 0 : u < u0 + up ? uv : u < u0 + up + dn ? -dv : 0;
	return { h: [[["UP", "UVAL"], G.X(u0 + up), G.mid - G.amp * uv, "xy"], [["DOWN", "DVAL"], G.X(Math.min(1, u0 + up + dn)), G.mid + G.amp * dv, "xy"]], label: "impulse",
		paint(g) { hLine(g, G.mid, G.W, .3); curveU(g, G, u => G.mid - G.amp * f(u), inkL(), 2.2); } };
});
/* INPUT GATE: the input's level (faint) and what passes: above GATE (dashed), at VOL ALEV. */
ED.ingate = synEd((P, role, G) => {
	const vo = role("VOL", "ALEV"), ga = role("GATE"), th = P(ga) / 127, V_ = P(vo) / 127;
	const hits = [[.04, 1], [.3, .45], [.52, .8], [.78, .3]], e = u => Math.max(0, ...hits.map(([t, a]) => u < t ? 0 : a * Math.exp(-(u - t) / .07))), o = u => e(u) > th ? e(u) * V_ : 0;
	return { h: [[ga, G.X(.97), G.Y(th * .9), "y"], [vo, G.X(.04) + 3, G.Y(o(.041) * .9), "y"]], label: "input gate",
		paint(g) { curveU(g, G, u => G.Y(e(u) * .9), inkA(.35), 1.2); hLine(g, G.Y(th * .9), G.W, .5); curveU(g, G, u => G.Y(o(u) * .9), inkL(), 2); } };
});
const NOTE_N = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = v => NOTE_N[v % 12] + (Math.floor(v / 12) - 2);
/* CHORD: the notes a trig sends: NOTE (the keyboard at the foot), N2 and N3 semitones above it. */
ED.chord = synEd((P, role, G) => {
	const nt = P("NOTE"), top = 26, bot = G.H - 22, sy = s => bot - Math.min(24, s) / 24 * (bot - top), len = .15 + synV("LEN") / 127 * .8;
	const h = [["NOTE", G.X(nt / 127), G.H - 8, "x"], ["N2", G.X(.5), sy(P("N2")), "y"], ["N3", G.X(.7), sy(P("N3")), "y"]];
	return { h, label: [noteName(nt), P("N2") && "+" + P("N2"), P("N3") && "+" + P("N3")].filter(Boolean).join(" "), paint(g) {
		for (let s = 0; s <= 24; s++) { g.fillStyle = inkA([1, 3, 6, 8, 10].includes((nt + s) % 12) ? .1 : .04); g.fillRect(0, sy(s) - (bot - top) / 48, G.W, (bot - top) / 24); }
		g.fillStyle = inkL(); [0, P("N2"), P("N3")].forEach((s, i) => { if (i && !s) return; g.fillRect(G.X(.04), sy(s) - 3, G.X(len) - G.X(.04), 6); });
		for (let k = 0; k < 128; k++) { g.fillStyle = [1, 3, 6, 8, 10].includes(k % 12) ? inkA(.6) : inkA(.18); g.fillRect(G.X(k / 127), G.H - 12, Math.max(1, (G.W - 16) / 128 - .4), 8); }
	} };
});
/* NOTE: one note as a bar: LEN how long, VEL how loud (the corner). */
ED.note = synEd((P, role, G) => {
	const x1 = G.X(.04 + P("LEN") / 127 * .92), y = G.Y(P("VEL") / 127);
	return { h: [[["LEN", "VEL"], x1, y, "xy"]], label: "note", paint(g) { g.fillStyle = inkA(.22); g.fillRect(G.X(.04), y, x1 - G.X(.04), G.Y(0) - y); g.fillStyle = inkL(); g.fillRect(G.X(.04), y, x1 - G.X(.04), 2.5); g.fillRect(G.X(.04), G.Y(0), G.W - 16, 1.5); } };
});
/* BARS: one bar a knob (PB from the middle, as the wheel). */
ED.bars = synEd((P, role, G, ks) => {
	const bip = n => n === "PB", xs = ks.map((_, i) => G.X((i + .5) / ks.length)), bw = Math.min(26, (G.W - 16) / ks.length * .45);
	const top = n => bip(n) ? G.Y(.5 + (P(n) - 64) / 127) : G.Y(P(n) / 127), base = n => bip(n) ? G.Y(.5) : G.Y(0);
	return { h: ks.map((n, i) => [n, xs[i], top(n), "y"]), label: ks.length > 2 ? "controllers" : "cue sends", paint(g) {
		g.font = "10px Silkscreen, ui-monospace, monospace";
		ks.forEach((n, i) => { g.fillStyle = inkA(.15); g.fillRect(xs[i] - bw / 2, G.Y(1), bw, G.Y(0) - G.Y(1)); g.fillStyle = inkL(); const a = top(n), b = base(n); g.fillRect(xs[i] - bw / 2, Math.min(a, b), bw, Math.max(2, Math.abs(b - a))); });
	} };
});
/* LEVEL · BALANCE: an input in the stereo field: BAL sideways, LEV up. */
ED.lvbal = synEd((P, role, G, ks) => {
	const lv = ks.find(n => /LEV$/.test(n)), bl = ks.find(n => /BAL$/.test(n)), x = G.X(P(bl) / 127), y = G.Y(P(lv) / 127);
	return { h: [[[bl, lv], x, y, "xy"]], label: lv.startsWith("M") ? "main out" : "inputs a/b", paint(g) {
		hLine(g, G.Y(.5), G.W, .2); g.strokeStyle = inkA(.45); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(G.W / 2 + .5, G.Y(1)); g.lineTo(G.W / 2 + .5, G.Y(0)); g.stroke(); g.setLineDash([]);
		g.fillStyle = inkA(.22); g.fillRect(x - 7, y, 14, G.Y(0) - y); g.fillStyle = inkL(); g.fillRect(8, G.Y(0), G.W - 16, 1.5);
	} };
});
/* ECHO: the repeats: TIME apart, FB how much each keeps (the second tap). */
ED.taps = synEd((P, role, G) => {
	const t = .03 + P("TIME") / 127 * .3, fb = P("FB") / 127 * .95, taps = [];
	for (let k = 0; .04 + k * t <= 1 && k < 40; k++) { const a = .9 * Math.pow(fb, k); if (k && a < .02) break; taps.push([.04 + k * t, a]); }
	return { h: [[["TIME", "FB"], G.X(.04 + t), G.Y(.9 * fb), "xy"]], label: "echo", paint(g) { g.fillStyle = inkL(); taps.forEach(([u, a], i) => { const y = G.Y(a); g.fillStyle = i ? inkL() : inkA(.5); g.fillRect(Math.round(G.X(u)) - 2, y, 4, G.Y(0) - y); }); } };
});
/* REVERB: the dry hit (DVOL), PRED later the tail, DEC long, DAMP smoother. */
ED.verb = synEd((P, role, G) => {
	const dry = P("DVOL") / 127 * .9, s = .05 + P("PRED") / 127 * .3, k = .03 + P("DEC") / 127 * .45, sm = 1 + Math.round(P("DAMP") / 127 * 6), tail = u => u < s ? 0 : .6 * Math.exp(-(u - s) / k) * Math.min(1, (u - s) / .02);
	const nz = u => { let a = 0; const i = Math.round(u * 500); for (let j = 0; j < sm; j++) a += Math.abs(rnd(i - j)); return a / sm * 1.6; }, r = rail(G, 0, "DAMP", P);
	return { h: [["DVOL", G.X(.03), G.Y(dry), "y"], ["PRED", G.X(s), G.Y(.6), "x"], ["DEC", G.X(Math.min(1, s + k)), G.Y(tail(s + k)), "x"], ["DAMP", r.x, r.y, "x"]], label: "gate box", paint(g) {
		g.fillStyle = inkL(); g.fillRect(G.X(.03) - 2, G.Y(dry), 4, G.Y(0) - G.Y(dry));
		curveU(g, G, u => G.Y(tail(u)), inkA(.45), 1, [3, 3]); curveU(g, G, u => G.Y(Math.min(1, tail(u) * nz(u))), inkL(), 1.4); paintRails(g, [r]);
	} };
});
/* EQ (CTR-EQ): the master EQ's curve from this machine's knobs: the shelves at LF and HF, the peak at
   PF with its width PQ; the gains LG PG HG up to boost. */
ED.eq3 = synEd((P, role, G) => {
	const r = u => (P("LG") - 64) / 64 / (1 + Math.exp((u - P("LF") / 127) * 18)) + (P("HG") - 64) / 64 / (1 + Math.exp(-(u - P("HF") / 127) * 18)) + (P("PG") - 64) / 64 * Math.exp(-Math.pow((u - P("PF") / 127) * (4 + P("PQ") / 8), 2));
	const y = u => G.H / 2 + 4 - r(u) * (G.H / 3), rq = rail(G, 0, "PQ", P);
	return { h: [[["LF", "LG"], G.X(P("LF") / 127), y(P("LF") / 127), "xy"], [["PF", "PG"], G.X(P("PF") / 127), y(P("PF") / 127), "xy"], [["HF", "HG"], G.X(P("HF") / 127), y(P("HF") / 127), "xy"], ["PQ", rq.x, rq.y, "x"]],
		label: "master eq", paint(g) { hLine(g, G.H / 2 + 4, G.W, .3); curveU(g, G, y, inkL(), 2.2); paintRails(g, [rq]); } };
});
/* CURVE (CTR-DX): the compressor, input to output (dashed = unchanged): TRHD where it starts, RTIO how
   much it holds back, KNEE how softly. */
ED.comp = synEd((P, role, G) => {
	const th = P("TRHD") / 127, ra = 1 + P("RTIO") / 127 * 9, kn = Math.max(.001, P("KNEE") / 127 * .25);
	const o = i => i <= th - kn ? i : i >= th + kn ? th + (i - th) / ra : i + ((1 / ra - 1) * Math.pow(i - th + kn, 2)) / (4 * kn), rk = rail(G, 0, "KNEE", P);
	return { h: [["TRHD", G.X(th), G.Y(o(th)), "x"], ["RTIO", G.X(1) - 2, G.Y(o(1)), "y"], ["KNEE", rk.x, rk.y, "x"]], label: "in → out", paint(g) {
		g.strokeStyle = inkA(.45); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(G.X(0), G.Y(0)); g.lineTo(G.X(1), G.Y(1)); g.stroke(); g.setLineDash([]);
		curveU(g, G, u => G.Y(o(u)), inkL(), 2.2); paintRails(g, [rk]);
	} };
});
function grid(g, W, H) { g.strokeStyle = inkA(0.13); g.lineWidth = 1; for (let i = 1; i < 4; i++) { g.beginPath(); g.moveTo(0, Math.round(H * i / 4) + .5); g.lineTo(W, Math.round(H * i / 4) + .5); g.stroke(); } for (let i = 1; i < 8; i++) { g.beginPath(); g.moveTo(Math.round(W * i / 8) + .5, 0); g.lineTo(Math.round(W * i / 8) + .5, H); g.stroke(); } }
function line(g, W, fy, c, w, dash) { g.strokeStyle = c; g.lineWidth = w; g.setLineDash(dash || []); g.beginPath(); for (let x = 0; x <= W; x += 1) { const y = fy(x); x ? g.lineTo(x, y) : g.moveTo(x, y); } g.stroke(); g.setLineDash([]); }
function label(g, t) { g.fillStyle = cssv("--ink"); g.font = "10px Silkscreen, ui-monospace, monospace"; g.fillText(t.toUpperCase(), 8, 14); }
let raf = 0, active = null;
/* P7: the editors and the playhead follow the window */
addEventListener("resize", () => { redraw(); alignLock(); if (V && V.playing) movePH(); });
function redraw() { if (raf) return; raf = requestAnimationFrame(() => { raf = 0; $$("canvas.ed").forEach(drawEd); $$("canvas.tw").forEach(drawTile); }); }
function drawEd(c) {
	const ed = ED[c.dataset.ed], dpr = devicePixelRatio || 1, W = c.clientWidth, H = c.clientHeight; if (!W || !ed) return;
	if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
	const g = c.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, W, H); ed.draw(g, W, H, c);
	ed.handles(W, H, c).forEach(h => {
		const on = active && active.c === c && active.k === h.k; g.beginPath(); g.arc(h.x, clamp(h.y, 6, H - 6), on ? 7 : 5.5, 0, 7); g.fillStyle = cssv("--lcd"); g.fill(); g.lineWidth = 2.5; g.strokeStyle = h.c; g.stroke();
		if (on) { g.fillStyle = cssv("--ink"); g.font = "10px Silkscreen, monospace"; g.fillText(h.k, Math.min(W - 40, h.x + 10), Math.max(14, h.y - 10)); }
	});
}
function nearest(c, e) { const r = c.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top; let best = null, bd = 16; ED[c.dataset.ed].handles(r.width, r.height, c).forEach(h => { const d = Math.hypot(h.x - x, clamp(h.y, 6, r.height - 6) - y); if (d < bd) { bd = d; best = h; } }); return best; }

/* ===== Sampler (UW): real kit machines, pattern locks, recorder mutes, sample names (0x73) and P9:
   the slots' own waveforms (md-desk/samples, from the emulated machine's memory: ROM slots from flash,
   RAM buffers from the DSP) and a WAV or AIFF into a ROM slot (chooseSample: the plug-in reads the file
   and sends it as SDS; sampleLoad says how it goes). A real Machinedrum cannot send its sample audio
   (capabilities.sampleAudio). Not possible from here: a file into a RAM slot, RAM to ROM copy. ===== */
const NA = {
	send: "Send is not available: the Machinedrum ignores SDS dump requests (measured on OS 1.63). It sends samples only from its own SAMPLE MGR menu.",
	rom: "Copy RAM to ROM is not available here: the Machinedrum does it only in its SAMPLE MGR menu (FUNCTION + REC, FUNCTION + STOP). Not wired yet.",
	ramLoad: "A file goes into a ROM slot. A RAM slot holds only what RAM-R records: the Machinedrum does not answer an SDS sample sent to a RAM slot and keeps nothing (measured on OS 1.63).",
	memory: "Sample memory in use: a real Machinedrum does not report it over MIDI.",
	names: "Sample names: a real Machinedrum takes a new name (Rename) but never reports names, so only the names sent this session are shown." };
/* P9: the slots as the plug-in read them (Docs.samples), or why there is nothing to draw. */
const smpBank = () => canDo(V, "sampleAudio") ? Docs.samples : null;
function smpSlotOf(kind, i) { const b = smpBank(); return b ? b[kind][i] : null; }
function smpOfMachine(m) { const r = /^ROM-(\d+)/.exec(m), p = /^RAM-[RP](\d)/.exec(m); return r ? ["rom", +r[1] - 1] : p ? ["ram", +p[1] - 1] : null; }
function smpWhy(at) {
	if (!canDo(V, "sampleAudio")) return V.caps.reasons.sampleAudio || "This engine cannot read the samples.";
	if (!Docs.samples) return "Reading the samples from the machine…";
	if (at && at[0] === "ram" && !Docs.samples.ramReadable) return Docs.samples.ramReason;
	return "";
}
function smpTime(s) { return s && !s.empty && s.rate ? (s.length / s.rate).toFixed(2) + " s" : ""; }
function smpInfo(s) { return !s ? "" : s.empty ? "empty" : `${smpTime(s)} · ${s.length} samples · ${s.rate} Hz${s.loop ? " · loops" : ""}`; }
/* P9: the slots' detail (sampleWave), asked for once per slot and width, dropped with every new bank. */
const Waves = new Map(), WavesAsked = new Map();
function waveOf(at, s, cols) {
	const key = at.join(":"), held = Waves.get(key), want = waveWant(held, s, cols);
	if (want && waveWant(WavesAsked.get(key), s, cols)) { WavesAsked.set(key, { bins: want, length: s.length }); cmd("sampleWave", { bank: at[0], slot: at[1], bins: want }); }
	return held && held.length === s.length ? held : null;
}
function onSampleWave(m) {
	const key = m.bank + ":" + m.slot; Waves.delete(key); Waves.set(key, m);
	while (Waves.size > 8) Waves.delete(Waves.keys().next().value);
	redraw();
}
/* A detail bin every few CSS pixels: a solid wave with its shape (a bin a pixel is a thin zigzag). */
const WAVE_CSS_PX = 3;
/* The slot's waveform across the canvas: one min/max bar a device pixel column (the detail when it is
   here, the overview until then), dim outside [from, to]. */
function peaksWave(g, W, H, at, s, from, to, color, dim) {
	const dpr = devicePixelRatio || 1, cols = Math.max(1, Math.round(W * dpr)), px = 1 / dpr, mid = H / 2 + 6, half = H / 2 - 14;
	const d = waveOf(at, s, Math.round(W / WAVE_CSS_PX)), col = d ? waveColumns(d.peaks, d.scale, cols) : waveColumns(s.peaks, 127, cols);
	const snap = y => Math.round(y * dpr) / dpr, lo = Math.min(from, to), hi = Math.max(from, to);
	for (let x = 0; x < cols; x++) {
		const u = (x + .5) / cols; g.fillStyle = u >= lo && u <= hi ? color : dim;
		const y0 = snap(mid - col[2 * x + 1] * half), y1 = snap(mid - col[2 * x] * half); g.fillRect(x * px, y0, px, Math.max(px, y1 - y0));
	}
}
/* Draw slot at (kind, i), or say why not. True when a waveform was drawn. */
function drawSlot(g, W, H, at, from, to, dim) {
	const why = smpWhy(at); if (why) { label(g, why.length > 70 ? why.slice(0, 68) + "…" : why); return false; }
	const s = smpSlotOf(at[0], at[1]); if (!s || s.empty) { label(g, "Empty"); return false; }
	peaksWave(g, W, H, at, s, from, to, cssv("--ink"), dim); return true;
}
/* P9: the audition: a slot heard once from the start on the plug-in's own output (the emulator mixes it
   in, so a DAW hears it). One at a time; it stops when its waveform leaves the page (another slot,
   track or workspace). {"type":"audition"} says playing, then stopped; the playhead moves from the
   sample's rate here, never from a stream. */
let Aud = null, audRaf = 0;
const audKey = at => at[0] + ":" + at[1];
function audBtn(at) {
	const s = at && smpSlotOf(at[0], at[1]), why = at ? smpWhy(at) : "", on = Aud && at && Aud.key === audKey(at);
	const na = why || (!s || s.empty ? "The slot is empty." : "");
	return `<button class="aud" data-aud="${at ? audKey(at) : ""}" aria-pressed="${!!on}" ${na ? `disabled title="${na}"` : `title="${on ? "Stop" : "Play the sample once, from the start (the plug-in's output)"}"`}>${audFace(on)}</button>`;
}
const audFace = on => on ? `<svg viewBox="0 0 8 8" aria-hidden="true"><rect x="1" y="1" width="6" height="6"/></svg>STOP` : `<svg viewBox="0 0 8 8" aria-hidden="true"><path d="M1.5 1v6l5.5-3z"/></svg>PLAY`;
/* a waveform canvas with its audition button and playhead */
const waveBox = (canvas, at) => `<div class="wavebox">${canvas}${audBtn(at)}<i class="audph" aria-hidden="true" hidden></i></div>`;
function onAudition(m) {
	const key = m.bank + ":" + m.slot;
	if (m.state === "playing") Aud = { key, t0: performance.now(), length: m.length, rate: m.rate };
	else if (Aud && Aud.key === key) Aud = null;
	syncAud();
}
function toggleAud(b) {
	const key = b.dataset.aud, [bank, slot] = key.split(":");
	if (Aud && Aud.key === key) { cmd("auditionStop", {}); Aud = null; syncAud(); return; }
	const s = smpSlotOf(bank, +slot); if (!s) return;
	Aud = { key, t0: performance.now(), length: s.length, rate: s.rate };
	cmd("audition", { bank, slot: +slot }, undefined, undefined, r => { if (!r.ok && Aud && Aud.key === key) { Aud = null; syncAud(); } });
	syncAud();
}
/* the buttons' faces and the playheads, without a render */
function syncAud() {
	for (const b of $$("[data-aud]")) {
		const on = !!Aud && Aud.key === b.dataset.aud; if (b.getAttribute("aria-pressed") === String(on)) continue;
		b.setAttribute("aria-pressed", String(on)); b.innerHTML = audFace(on); if (!b.disabled) b.title = on ? "Stop" : "Play the sample once, from the start (the plug-in's output)";
	}
	if (Aud && !audRaf) audRaf = requestAnimationFrame(audTick);
	if (!Aud) for (const p of $$(".audph")) p.hidden = true;
}
function audTick() {
	audRaf = 0; if (!Aud) return;
	const u = auditionAt(Aud, performance.now() - Aud.t0);
	for (const b of $$("[data-aud]")) { const p = (b.closest(".wavebox,.romtile") || b.parentElement).querySelector(".audph"); if (!p) continue; p.hidden = Aud.key !== b.dataset.aud || u >= 1; p.style.width = (u * 100).toFixed(2) + "%"; }
	if (u < 1) audRaf = requestAnimationFrame(audTick);
}
/* after a render: an audition whose waveform is gone (or can no longer play) stops */
function audKeep() {
	if (Aud && !$$("[data-aud]").some(b => b.dataset.aud === Aud.key && !b.disabled)) { cmd("auditionStop", {}); Aud = null; }
	syncAud();
}
/* P9: the sample on its way to a ROM slot (sampleLoad), shown on that slot's card. */
let smpLoad = null;
function onSampleLoad(m) {
	smpLoad = m;
	if (m.state !== "sending") { toast(m.text); if (m.state === "done") smpLoad.doneAt = Date.now(); }
	if (S.ws === "sampler") scheduleRender();
}
function smpLoadCard(k) {
	const m = smpLoad; if (!m || m.slot !== k - 1) return "";
	const f = m.total ? Math.round(m.sent / m.total * 100) : 0;
	const notes = (m.notes || []).map(n => `<span class="note">${n}</span>`).join(" ");
	if (m.state === "sending") return `<div class="irow"><span class="ilab">Sending</span><div class="meter" style="flex:1"><div class="row"><span>${m.file} → ${romCode(k)} · ${m.name}${m.handshake === false ? " · no handshake" : ""}</span><b>${f}%</b></div><div class="bar"><i style="--f:${f}%"></i></div></div><button data-smpstop="1" title="Stop sending (SDS CANCEL)">Stop</button></div>${notes ? `<div class="irow"><span class="ilab"></span>${notes}</div>` : ""}`;
	return `<div class="irow"><span class="ilab">${m.state === "done" ? "Loaded" : m.state === "cancelled" ? "Stopped" : "Not loaded"}</span><span class="note">${m.text}${notes ? " " + notes : ""}</span></div>`;
}
const recTrack = n => V.tracks.findIndex(t => t.m === "RAM-R" + n), playTrack = n => V.tracks.findIndex(t => t.m === "RAM-P" + n);
/* Names the user sent this session (0x73). A real machine cannot report them; the emulated one's are
   read with its samples (Docs.samples) and win. */
const SENT_NAMES = {};
ED.sample = {
	to: toTrack("syn"),
	draw(g, W, H) {
		const tr = V.tracks[S.sel], y = tr.syn, n = +(tr.m.match(/RAM-P(\d)/) || [])[1]; grid(g, W, H);
		const a = y.STRT / 127, b = y.END / 127, rev = b < a;
		if (!drawSlot(g, W, H, smpOfMachine(tr.m), a, b, inkA(0.26))) return;
		const m = V.locks.get(lk(S.sel, "STRT")); if (m) { g.strokeStyle = cssv("--ink"); g.lineWidth = 1; g.font = "10px Silkscreen, monospace"; g.fillStyle = cssv("--ink");
			[...m.entries()].sort((p, q) => p[1] - q[1]).forEach(([st, v]) => { const x = Math.round(v / 127 * W) + .5; g.beginPath(); g.moveTo(x, 22); g.lineTo(x, H - 4); g.stroke(); g.fillText(st + 1, x + 2, H - 6); }); }
		const at = smpOfMachine(tr.m), s = smpSlotOf(at[0], at[1]);
		label(g, (n ? `RAM-P${n} · plays RAM-R${n}` : `${tr.m}${s.name ? " · " + s.name : ""}`) + " · " + smpTime(s) + (rev ? " · reversed" : ""));
	},
	handles(W, H) { const y = V.tracks[S.sel].syn; return [{ x: y.STRT / 127 * W, y: H - 14, k: "STRT", c: cssv("--ink"), drag: x => ({ STRT: clamp(Math.round(x / W * 127)) }) }, { x: y.END / 127 * W, y: 30, k: "END", c: cssv("--ink"), drag: x => ({ END: clamp(Math.round(x / W * 127)) }) }]; }
};
ED.rec = {
	to: toTrack("syn"),
	draw(g, W, H) {
		const tr = V.tracks[S.sel], n = +tr.m.slice(5), R = tr.syn, len = R.LEN / 127; grid(g, W, H);
		const s = smpSlotOf("ram", n - 1);
		if (!smpWhy(["ram", n - 1]) && s && !s.empty) peaksWave(g, W, H, ["ram", n - 1], s, 0, 1, cssv("--ink"), "transparent"); g.fillStyle = inkA(0.3); g.fillRect(len * W, 0, W - len * W, H);
		g.fillStyle = cssv("--ink"); g.font = "10px Silkscreen, monospace"; for (let k = 0; k <= 32; k += 4) { const x = k / 32 * W; g.fillText(k ? k + "" : "", x + 2, H - 4); }
		label(g, `capture ${Math.round(R.LEN / 4)} steps · RATE ${R.RATE}${s && !s.empty ? " · last take " + smpTime(s) : ""}`);
	},
	handles(W, H) { const R = V.tracks[S.sel].syn; return [{ x: R.LEN / 127 * W, y: H / 2, k: "LEN", c: cssv("--ink"), drag: x => ({ LEN: clamp(Math.round(x / W * 127)) }) }]; }
};
ED.slot = {
	to: c => { const p = playTrack(+c.dataset.n); return p < 0 ? null : { t: p, g: "syn" }; },
	draw(g, W, H, c) {
		const n = +c.dataset.n, p = playTrack(n); grid(g, W, H);
		const y = p >= 0 ? V.tracks[p].syn : { STRT: 0, END: 127 }; const a = y.STRT / 127, b = y.END / 127;
		if (!drawSlot(g, W, H, ["ram", n - 1], a, b, inkA(0.26))) return;
		const m = p >= 0 && V.locks.get(lk(p, "STRT")); if (m) { g.strokeStyle = cssv("--ink"); [...m.values()].forEach(v => { const x = Math.round(v / 127 * W) + .5; g.beginPath(); g.moveTo(x, 20); g.lineTo(x, H - 2); g.stroke(); }); }
		label(g, `RAM-R${n} take · ${smpInfo(smpSlotOf("ram", n - 1))}`);
	},
	handles(W, H, c) { const p = playTrack(+c.dataset.n); if (p < 0) return []; const y = V.tracks[p].syn; return [{ x: y.STRT / 127 * W, y: H - 12, k: "STRT", c: cssv("--ink"), drag: x => ({ STRT: clamp(Math.round(x / W * 127)) }) }, { x: y.END / 127 * W, y: 26, k: "END", c: cssv("--ink"), drag: x => ({ END: clamp(Math.round(x / W * 127)) }) }]; }
};
function chopInner(p, s) {
	const t = V.tracks[p]; if (!t.trigs[s]) return ""; const st = V.locks.get(lk(p, "STRT"))?.get(s), en = V.locks.get(lk(p, "END"))?.get(s), rt = V.locks.get(lk(p, "RTRG"))?.get(s);
	const v = st ?? t.syn.STRT, rev = (en ?? t.syn.END) < v; return `<span class="cpn">${Math.floor(v / 8) + 1}</span><span class="cpfx">${rev ? "REV" : ""}${rt ? " RTRG" : ""}</span>`;
}

/* ===== LCD line 2: the workspace's own values ===== */
const L2 = (k, label, val, title, edit) => `<span class="l2 ${edit ? "ed" : ""}" ${edit ? `data-l2="${k}" role="button" tabindex="0"` : ""} title="${title || ""}"><small>${label}</small><b>${val}</b></span>`;
function renderSub() {
	const t = S.sel, tr = V.tracks[t]; let h = "";
	if (S.ws === "seq") h = L2("len", "LEN", V.length === V.len ? V.len : V.length + "/" + V.len, "Pattern length (total length " + V.len + "). Click to step 16 / 32 / 48 / 64; alt-click steps the length inside it.", 1) + L2("mult", "SPD", V.mult, "Tempo multiplier. Click to step 1X / 2X / 3/4X / 3/2X.", 1)
		+ L2("swing", "SWG", V.swing + "%", "Swing 50–80 %. Drag up or down, or scroll.", 1) + L2("accAmt", "ACC", V.accAmt, "Accent 0–15. Drag up or down, or scroll.", 1) + L2("mode", "MODE", V.mode === "EXTENDED" ? "EXT" : "CLASSIC", "Classic or Extended. Locks only play in Extended. Click to switch.", 1)
		+ L2("dbl", "LEN", "×2", `Double the pattern: ${V.length} to ${V.length * 2} steps, the new half a copy of the steps and locks. Above 32 steps only in EXTENDED. One undo step.`, 1);
	else if (S.ws === "sound") h = L2("", "TRACK", String(t + 1).padStart(2, "0")) + L2("", "MACHINE", tr.m) + L2("", "", tr.name.toUpperCase());
	else if (S.ws === "mix") h = L2("", "PATH", "SEND›ECHO›GATE›EQ›DYN›MAIN", "Sends feed the master effects. Tracks on outputs A–F skip them.");
	else if (S.ws === "sampler") { const used = V.tracks.filter(t => /^ROM/.test(t.m)).length; const b = smpBank(); h = L2("", "MEM", b ? Math.round(b.used / b.capacity * 100) + "%" : "n/a", b ? `Sample memory: the ROM slots hold ${b.used} of ${b.capacity} samples (${(b.used / 44100).toFixed(1)} of ${(b.capacity / 44100).toFixed(1)} s at 44.1 kHz); the four RAM buffers share the rest.` : canDo(V, "sampleAudio") ? "Reading the samples from the machine…" : NA.memory) + L2("", "KIT", used + " ROM", "Tracks in this kit that play a ROM slot") + L2("", "SLOT", S.smpSlot.replace(/^RAM/, "RAM ").replace(/^ROM/, "ROM ")); }
	else if (S.ws === "control") h = L2("", "IN", "MIDI LEARN") + L2("", "MAPS", (Docs.learn?.mappings || []).length);
	else h = L2("song", "SONG", String(V.songSlot + 1).padStart(2, "0"), "Song slot. Click for the next one, shift-click for the previous (the machine loads it when stopped).", 1) + L2("", "ROWS", V.song.length) + L2("", "BARS", Math.round(songSteps() / 16)) + L2("", "TIME", songTime());
	$("#lcd2").innerHTML = h;
}
const romName = k => smpSlotOf("rom", k - 1)?.name || SENT_NAMES[k] || ""; const romCode = k => "ROM-" + String(k).padStart(2, "0");
function players(n) { return V.tracks.map((t, i) => t.m === "RAM-P" + n ? i : -1).filter(i => i >= 0); }
function slotState(n) { const r = recTrack(n); if (r < 0) return "none"; if (S.capture[n]) return "cap"; return V.tracks[r].mute ? "frozen" : "live"; }
const STATE_TXT = { none: "not in kit", live: "live", frozen: "frozen", cap: "capturing" };
function renderSlots() {
	$("#rail").innerHTML = `<div class="railhead">Slots</div>
 <div class="slotsec"><div class="scap">RAM · lost at power-off</div>${[1, 2, 3, 4].map(n => { const st = slotState(n), r = recTrack(n);
		return `<button class="slotk ram st-${st}" data-slot="RAM${n}" aria-pressed="${S.smpSlot === "RAM" + n}"><i class="led"></i><b>RAM ${n}</b><span>${STATE_TXT[st]}${r >= 0 ? " · R" + (r + 1) : ""}</span></button>`; }).join("")}
 <div class="scap">ROM · kept · 48 slots</div><div class="romgrid">${Array.from({ length: 48 }, (_, i) => { const k = i + 1;
		const used = V.tracks.map((t, i) => t.m === romCode(k) ? i + 1 : 0).filter(Boolean), s = smpSlotOf("rom", i);
		return `<button class="slotk rom ${used.length ? "has" : ""}" data-slot="ROM${k}" aria-pressed="${S.smpSlot === "ROM" + k}" ${s && s.empty ? 'style="opacity:.55"' : ""} title="${romCode(k)}${romName(k) ? " · " + romName(k) + (s ? "" : " (sent this session)") : ""}${s ? " · " + smpInfo(s) : ""}${used.length ? " · played by track " + used.join(", ") : ""}">${romName(k) || String(k).padStart(2, "0")}</button>`; }).join("")}</div>
 <div class="scap" title="${smpBank() ? "" : NA.names + " " + NA.memory}">${smpBank() ? "Lit: used by this kit. Faint: an empty slot." : "Lit: used by this kit. " + (smpWhy() || "")}</div></div>`;
}
/* P4: an empty RAM slot is one call to action. It shows what changes (which machines are replaced,
   which trigs stay), does it as one undo step, and then the recorder is ready below. */
function setupTracks() {
	let r = S.smpRec ?? 12, p = S.smpPlay ?? 13;
	if (p === r) p = (r + 1) % 16;
	return [r, p];
}
function setupCard(n) {
	const [rt, pt] = setupTracks(), R = V.tracks[rt], P = V.tracks[pt];
	const rtr = R.trigs.slice(0, V.len).filter(Boolean).length, ptr = P.trigs.slice(0, V.len).filter(Boolean).length;
	const pl = k => k + " trig" + (k === 1 ? "" : "s"), choice = rtr > 1 || (rtr === 1 && !R.trigs[0]), once = S.smpOnce && rtr;
	/* one side of the flow: its track (the LCD window between ‹ ›), the machine it changes, its trigs; the
	   option row is always there (hidden when there is no choice), so stepping never moves anything */
	const side = (w, cap, t, from, to, trigs, opt, tip) => `<div class="spside" title="${tip}">
    <div class="sphead"><span class="ilab">${cap}</span><small>track</small></div>
    <div class="sptrk"><button data-setupt="${w}" data-d="-1" aria-label="Previous ${cap.toLowerCase()} track">‹</button><b class="sptn" aria-live="polite">${String(t + 1).padStart(2, "0")}</b><button data-setupt="${w}" data-d="1" aria-label="Next ${cap.toLowerCase()} track">›</button></div>
    <div class="spmach"><span class="from" title="${from}">${from}</span><i aria-hidden="true">→</i><b>${to}</b></div>
    <div class="sptrig">${trigs}</div>
    <div class="spopt">${opt}</div></div>`;
	const recTrig = !rtr ? "no trig · one goes on step 1" : once ? `${pl(rtr)} cleared · one on step 1` : `${pl(rtr)} · records on ${rtr > 1 ? "each" : "it"}`;
	const recOpt = `<span class="seg" ${choice ? "" : 'style="visibility:hidden" aria-hidden="true"'}><button data-smponce="0" aria-pressed="${!S.smpOnce}" ${choice ? "" : 'tabindex="-1"'} title="The recorder keeps its trigs and records on each">Keep</button><button data-smponce="1" aria-pressed="${!!S.smpOnce}" ${choice ? "" : 'tabindex="-1"'} title="Clear the recorder's trigs and put one on step 1: it records once a loop">Once, on step 1</button></span>`;
	const playTrig = ptr ? `${pl(ptr)} · plays on ${ptr > 1 ? "each" : "it"}` : "no trig · add them in the chop grid";
	const fx = S.keepFx ? "effects and routing kept · " : "";
	return `<section class="card smpsetup"><header><h3>Set up sampling · RAM ${n}</h3><span>the UW's RAM machines: one records, one plays</span></header>
  <div class="spflow">
   ${side("r", "Recorder", rt, R.m, "RAM-R" + n, recTrig, recOpt, `Track ${rt + 1}'s machine (${R.m}) becomes RAM-R${n}: it records into RAM ${n} on its trigs.`)}
   <div class="spbuf" aria-hidden="true"><i class="ln"></i><b>RAM ${n}</b><small>buffer</small><i class="ln"></i></div>
   ${side("p", "Player", pt, P.m, "RAM-P" + n, playTrig, `<span class="sphint">plays what RAM ${n} holds</span>`, `Track ${pt + 1}'s machine (${P.m}) becomes RAM-P${n}: it plays what RAM ${n} holds on its trigs.`)}
  </div>
  <div class="spgo"><button class="cream" data-setupgo="${n}">Set up sampling</button><span class="note" title="Both tracks' machines are replaced${S.keepFx ? " (effects and routing kept)" : ""}${once ? `; track ${rt + 1}'s trigs are cleared` : "; no trig is cleared"}. Undo takes it all back.">One undo step · ${fx}${once ? `track ${rt + 1}'s trigs cleared` : "no trig cleared"}</span></div>
</section>`;
}
/* The recorder's source, from its levels: MLEV/MBAL = the machine's own mix, ILEV/IBAL = inputs A/B. */
/* Manual A-15: MLEV/ILEV 0 records "as is", -64 records nothing (stored 64 and 0); the balances are
   -64..+63 (stored 0..127, 64 = centre). */
const SOURCES = [["main", "Main mix", { MLEV: 64, MBAL: 64, ILEV: 0, IBAL: 64 }], ["a", "Input A", { MLEV: 0, MBAL: 64, ILEV: 64, IBAL: 0 }],
	["b", "Input B", { MLEV: 0, MBAL: 64, ILEV: 64, IBAL: 127 }], ["ab", "A + B", { MLEV: 0, MBAL: 64, ILEV: 64, IBAL: 64 }]];
function sourceOf(t) { const y = V.tracks[t].syn; const hit = SOURCES.find(([, , v]) => Object.keys(v).every(k => y[k] === v[k])); return hit ? hit[0] : "custom"; }
function sourceSeg(t) { const cur = sourceOf(t); return `<span class="seg srcseg">${SOURCES.map(([id, label]) => `<button data-recsrc="${id}" data-t="${t}" aria-pressed="${cur === id}">${label}</button>`).join("")}</span>${cur === "custom" ? `<span class="note">custom levels</span>` : ""}`; }
/* RAM-R LEN and RATE in the manual's units (A-15). LEN: "each parameter value is ¼ of step", 127 records
   2 bars (the tutorial: 64 = one bar); the time is at the pattern's tempo and speed. RATE: the manual says
   only that turning it down lowers the quality, 127 = maximum quality (the tutorial); it gives no rate per
   value, so below 127 the raw value stands. The rate a take was recorded at is the machine's own (the DSP
   table, Docs.samples), shown with the take. */
const lenSteps = v => v >= 127 ? 32 : v / 4;
function stepsTxt(st) { const w = Math.floor(st), q = ["", "¼", "½", "¾"][Math.round((st - w) * 4)]; return (w || !q ? String(w) : "") + q; }
const UNITS = {
	len: v => { const st = lenSteps(v), sec = st * stepMs() / 1000;
		return [st && st % 16 === 0 ? st / 16 + (st === 16 ? " bar" : " bars") : stepsTxt(st) + " st",
			`LEN ${v}: records ${stepsTxt(st)} step${st === 1 ? "" : "s"}, ${sec.toFixed(2)} s at ${(+V.bpm || 120).toFixed(1)} BPM ${V.mult} (manual A-15: each value is ¼ step, 127 = 2 bars)`]; },
	rate: v => [v >= 127 ? "full" : "reduced", `RATE ${v}: ${v >= 127 ? "maximum recording quality" : "below 127 the recording quality is lower"} (manual A-15). The manual gives no sample rate per value; the take's own rate shows under Playback.`]
};
/* The RAM view's steps are the Sequence's grid: its ruler, its keys (stepCls), its page control (pageCtl, the
   same S.page / S.viewAll / S.follow), one row for the recorder's trigs and one for the player's chops. */
const smpLab = (t, what, cls = "") => `<span class="srlab ${cls}" title="${what} · track ${t + 1} · ${V.tracks[t].m}"><b>${what}</b><small><em>${t + 1}</em>${V.tracks[t].m}</small></span>`;
const smpCols = () => `var(--srlab) ${cols()}`;
/* Every ROM slot as a tile: its overview wave (no detail asked), number, name, lit when this kit plays
   it, faint when empty, its own audition key; an empty one offers Load sample…. A click selects it. */
function romTiles(sel, sending) {
	const b = smpBank(), why = smpWhy(), inKit = new Set(V.tracks.map(t => t.m));
	const used = b ? b.rom.filter(s => !s.empty).length : null;
	return `<section class="card"><header><h3>ROM slots</h3><span>${used != null ? `${used} of 48 hold a sample · ` : ""}lit: used by this kit${why ? " · " + why : ""}</span></header>
  <div class="romtiles">${Array.from({ length: 48 }, (_, i) => {
		const k = i + 1, s = b ? b.rom[i] : null, code = romCode(k), name = romName(k), empty = !!(s && s.empty), lit = inKit.has(code);
		const face = s && !empty ? `<canvas class="tw" data-k="${k}" aria-hidden="true"></canvas>` : empty ? `<span class="twno">empty</span>` : "";
		const act = empty ? `<button class="twload" data-smpload="${k}" ${sending ? "disabled" : ""} title="Choose a WAV or AIFF file for ${code}">Load…</button>` : audBtn(["rom", i]);
		return `<div class="romtile ${lit ? "has" : ""} ${empty ? "empty" : ""} ${s ? "" : "nowave"}"><button class="twsel" data-romtile="${k}" aria-pressed="${sel === k}" title="${code}${name ? " · " + name : ""}${s ? " · " + smpInfo(s) : ""}${lit ? " · used by this kit" : ""}"><span class="twhead"><i class="led ${lit ? "on" : ""}"></i><b>${String(k).padStart(2, "0")}</b><span>${name}</span><small>${smpTime(s)}</small></span>${face}</button>${act}<i class="audph" aria-hidden="true" hidden></i></div>`;
	}).join("")}</div></section>`;
}
/* a tile's wave: the overview at a bin every WAVE_CSS_PX (the detail view's solid look), drawn again only
   when the slot, the size or the plate changed */
function drawTile(c) {
	const dpr = devicePixelRatio || 1, W = c.clientWidth, H = c.clientHeight, s = smpSlotOf("rom", +c.dataset.k - 1); if (!W || !H || !s || s.empty) return;
	const ink = cssv("--ink"), sig = [W, H, dpr, ink, s.length, s.peaks.length, s.peaks[0], s.peaks[s.peaks.length >> 1]].join(":"); if (c._sig === sig) return; c._sig = sig;
	c.width = Math.round(W * dpr); c.height = Math.round(H * dpr);
	const g = c.getContext("2d"); g.setTransform(1, 0, 0, 1, 0, 0); g.clearRect(0, 0, c.width, c.height); g.fillStyle = ink;
	const n = Math.max(1, Math.round(W / WAVE_CSS_PX)), col = waveColumns(s.peaks, 127, n), mid = c.height / 2, half = c.height / 2 - Math.round(2 * dpr);
	for (let x = 0; x < c.width; x++) { const j = Math.min(n - 1, Math.floor(x * n / c.width)), y0 = Math.round(mid - col[2 * j + 1] * half), y1 = Math.round(mid - col[2 * j] * half); g.fillRect(x, y0, 1, Math.max(1, y1 - y0)); }
}
function renderSampler() {
	document.documentElement.classList.toggle("viewall", !!S.viewAll); const id = S.smpSlot; let h = "", ram = false;
	if (id.startsWith("RAM")) {
		const n = +id.slice(3), r = recTrack(n), ps = players(n), st = slotState(n);
		if (!V.midi) { h = `<div class="smpempty"><div class="edblank big">${V.lifecycle === "unsupported" ? "This firmware is not MD OS 1.63 UW: the Sampler needs the UW's ROM and RAM machines." : "The machine is not running yet. The Sampler works with the UW machine once it is ready."}</div></div>`; }
		else if (r < 0) h = setupCard(n);
		else {
			if (S.chopTrack == null || !ps.includes(S.chopTrack)) S.chopTrack = ps[0] ?? null; const p = S.chopTrack, at = ["ram", n - 1], take = smpSlotOf("ram", n - 1);
			const plays = ps.length ? ps.map(i => i + 1).join(", ") : "none";
			const pTrigs = p != null ? V.tracks[p].trigs.slice(0, V.len).filter(Boolean).length : 0;
			/* the slot (its state, its take and audition key on one bar above the take), the steps (where the recorder
			   records, where the player plays and from where), then the recorder's and the player's values side by side */
			ram = true; h = `<section class="card ramtop"><header><h3>RAM ${n} · RAM-R${n} → RAM-P${n}</h3><span>record track ${r + 1} · play track ${plays}</span></header>
    <div class="wavebox ramwave"><div class="slotbar"><span class="stbox st-${st}"><i class="led"></i>${STATE_TXT[st]}</span>
     <button class="${st === "live" ? "cream" : ""}" data-slotmode="live" aria-pressed="${st === "live"}" title="Record again on every loop (unmutes the recorder track)">Live</button>
     <button class="${st === "frozen" ? "cream" : ""}" data-slotmode="frozen" aria-pressed="${st === "frozen"}" title="Mute the recorder track and keep this take">Freeze</button>
     <button class="rec" data-capture="${n}" title="Record one loop, then freeze">Capture next loop</button>
     <span class="grow"></span><span class="note ramna" title="${NA.ramLoad} ${NA.rom}">RAM holds only what RAM-R records: no file load, no copy to ROM from here</span>${audBtn(at)}</div>
     <div class="wavecv"><canvas class="ed smpwave" data-ed="slot" data-n="${n}" aria-label="Captured audio with start and end markers"></canvas><i class="audph" aria-hidden="true" hidden></i></div></div></section>
   <section class="card ramsteps"><header><h3>Steps</h3><span class="chophead">${ps.length > 1 ? ps.map(i => `<button class="${i === p ? "cream" : ""}" data-choptrk="${i}">Track ${i + 1}</button>`).join("") : ""}</span></header>
    <div class="seq smpseq"><div class="r" style="grid-template-columns:${smpCols()}"><span></span>${steps().map(s => `<div class="rul ${s % 16 === 0 && s !== vis()[0] ? "gap" : ""}">${s % 4 === 0 ? s + 1 : ""}</div>`).join("")}</div>
     <div class="r" id="recon" data-r="${r}" style="grid-template-columns:${smpCols()}">${smpLab(r, "Record on", "rec")}${steps().map(s => `<button class="${stepCls(r, s)}" data-rc="${s}" aria-pressed="${V.tracks[r].trigs[s]}" aria-label="Record on step ${s + 1}"></button>`).join("")}</div>
     ${p != null ? `<div class="r" id="chop" data-p="${p}" style="grid-template-columns:${smpCols()}">${smpLab(p, "Chop")}${steps().map(s => `<button class="${stepCls(p, s)}" data-cp="${s}" aria-pressed="${V.tracks[p].trigs[s]}" aria-label="Chop step ${s + 1}">${chopInner(p, s)}</button>`).join("")}</div>` : `<div class="r" style="grid-template-columns:var(--srlab) minmax(0,1fr)"><span class="srlab"><b>Chop</b></span><div class="edblank">No track plays RAM-P${n}. Put RAM-P${n} on a track in Sound.</div></div>`}</div>
    <div class="seqfoot"><span></span><div class="legend"><span><i class="lg on"></i>Trig: click</span>${p != null ? `<span>Chop slice: drag up / down</span><span>Reverse: alt-click</span><span>Retrig: shift-click</span>` : ""}<span><i class="lg on lk"></i>Has locks</span></div>${pageCtl()}</div></section>
   <div class="smp2"><section class="card"><header><h3>Source</h3><span>recorder · track ${r + 1}</span></header><div class="irow">${sourceSeg(r)}</div><div class="ctl four">${RAMR.map(k => pc("syn", k, { t: r, unit: k === "LEN" ? "len" : k === "RATE" ? "rate" : "" })).join("")}</div></section>
    ${p != null ? `<section class="card"><header><h3>Playback</h3><span>player · track ${p + 1}</span></header><div class="irow"><span class="ilab">Take</span><span class="note pbtake">${take ? (take.empty ? "empty: nothing recorded yet" : smpInfo(take)) : smpWhy(at) || "not read"} · ${pTrigs} trig${pTrigs === 1 ? "" : "s"}</span></div><div class="ctl four">${SMPL.map(k => pc("syn", k, { t: p })).join("")}</div></section>` : ""}</div>`;
		}
	}
	else {
		const k = +id.slice(3), code = romCode(k), users = V.tracks.map((t, i) => t.m === code ? i : -1).filter(i => i >= 0), u = users[0];
		const s = smpSlotOf("rom", k - 1), sending = smpLoad && smpLoad.state === "sending";
		/* The selected slot on top (its waveform, its actions, the playback of the track that plays it),
		   every ROM slot under it (romTiles). */
		h = `<section class="card romsel"><header><h3>${code}${s && s.name ? ` <span class="note">${s.name}</span>` : romName(k) ? ` <span class="note" title="Sent this session with Rename; a real machine cannot report names">sent: ${romName(k)}</span>` : ""}</h3><span title="${s ? "" : NA.names}">${s ? smpInfo(s) : smpWhy() || ""}</span></header>
   <div class="romtop">${waveBox(`<canvas class="ed smpwave" data-ed="rom" data-k="${k}" aria-label="${code} waveform"></canvas>`, ["rom", k - 1])}
    <div class="romside"><div class="slotbar"><span class="note">${users.length ? "Used by " + users.map(i => "track " + (i + 1)).join(", ") : "Not used in this kit"}</span></div>
     <div class="slotbar"><button class="cream" data-romput="${k}">Put on track ${S.sel + 1}</button><button data-smpload="${k}" ${sending ? "disabled" : ""} title="Choose a WAV or AIFF file for ${code}. The plug-in reads it, makes it mono 16-bit (44.1 kHz at most), cuts it to the memory left and sends it as SDS with a 4-letter name from the file name. It replaces what the slot holds.">Load sample…</button><button data-na="send" disabled title="${NA.send}">Send</button><button data-rename="${k}" title="Send a new name (4 letters) to the machine, SysEx 0x73">Rename</button></div>
     ${u != null ? `<div class="romplay"><div class="cap">Playback · track ${u + 1}</div><div class="ctl four">${SMPL.map(q => pc("syn", q, { t: u })).join("")}</div></div>` : `<div class="romplay"><div class="cap">Playback</div><span class="note">No track in this kit plays ${code}. Put it on a track to set its pitch, decay, start and end here.</span></div>`}</div></div>
   ${smpLoadCard(k)}</section>
   ${romTiles(k, sending)}`;
	}
	$("#main").innerHTML = `<div class="smpmain ${ram ? "ram" : ""}">${h}</div>`; syncControls(); redraw();
}
ED.rom = {
	draw(g, W, H, c) {
		const k = +c.dataset.k; grid(g, W, H);
		if (drawSlot(g, W, H, ["rom", k - 1], 0, 1, "transparent")) { const s = smpSlotOf("rom", k - 1); label(g, romCode(k) + (s.name ? " · " + s.name : "") + " · " + smpInfo(s)); }
	}, handles: () => []
};
let chopDrag = null;
document.getElementById("main").addEventListener("pointerdown", e => { const c = e.target.closest("[data-cp].on"); if (!c || e.altKey || e.shiftKey) return; const p = +$("#chop").dataset.p, s = +c.dataset.cp, cur = V.locks.get(lk(p, "STRT"))?.get(s) ?? V.tracks[p].syn.STRT; chopDrag = { c, p, s, y: e.clientY, v: cur, moved: false }; gesture = Bridge.gesture(); grabPointer(c, e); });
document.getElementById("main").addEventListener("pointermove", e => { if (!chopDrag) return; if (e.buttons === 0 && e.pointerType === "mouse") { chopDrag = null; gesture = 0; return; } const d = Math.round((chopDrag.y - e.clientY) / 6) * 8; if (!d && !chopDrag.moved) return; chopDrag.moved = true; const v = clamp(chopDrag.v + d); if (setLock(chopDrag.p, "STRT", chopDrag.s, v)) { chopDrag.c.innerHTML = chopInner(chopDrag.p, chopDrag.s); renderTop(); redraw(); } });
document.addEventListener("pointerup", () => { if (chopDrag) { chopDrag.c.dataset.moved = chopDrag.moved ? "1" : ""; chopDrag = null; gesture = 0; } });

/* ===== Machine picker (catalogue from the plug-in) ===== */
const FAMS = [["TRX", "Analogue model"], ["EFM", "FM drums"], ["E12", "12-bit samples"], ["PI", "Physical model"], ["GND", "Tone and noise"], ["INP", "External input"], ["MID", "MIDI out"], ["CTR", "Control"], ["ROM", "ROM samples · UW"], ["RAM", "RAM record + play · UW"]];
function machList(f) { return Cat.list.filter(m => famKey(m.machine) === f).map(m => m.machine); }
function openPicker() {
	const tr = V.tracks[S.sel]; S.pickFam = S.pickFam && S.pickOpenFor === S.sel ? S.pickFam : famKey(tr.m); S.pickOpenFor = S.sel; drawPicker();
	const pop = $("#machpop"), b = $("#machbtn").getBoundingClientRect(); pop.hidden = false; pop.style.top = (b.bottom + scrollY + 6) + "px"; pop.style.left = Math.max(16, Math.min(b.left + scrollX, innerWidth - pop.offsetWidth - 16)) + "px"; $("#machbtn").setAttribute("aria-expanded", "true"); pop.querySelector(".mk[aria-pressed=true],.mk")?.focus();
}
function closePicker() { const pop = $("#machpop"); if (pop.hidden) return; pop.hidden = true; $("#machbtn")?.setAttribute("aria-expanded", "false"); if (pendingRender) scheduleRender(); }
function drawPicker() {
	const tr = V.tracks[S.sel], list = machList(S.pickFam), small = list.length > 16;
	$("#machpop").innerHTML = `<div class="mp-fams">${FAMS.map(([f, n]) => `<button class="mf" data-fam="${f}" aria-pressed="${f === S.pickFam}"><i class="led"></i><b>${f === "PI" ? "P-I" : f}</b><span>${n}</span></button>`).join("")}</div>
  <div class="mp-right"><div class="mp-head"><span class="cap">${S.pickFam === "PI" ? "P-I" : S.pickFam} · ${FAMS.find(x => x[0] === S.pickFam)[1]}</span><span class="note">${list.length} machines</span></div>
  <div class="mp-grid ${small ? "small" : ""}">${list.map(m => `<button class="mk" data-mach="${m}" aria-pressed="${m === tr.m}"><b>${codeOf(m)}</b><span>${small ? "" : nameOf(m)}</span></button>`).join("")}</div>
  <div class="mp-foot"><div class="mp-prev" id="mpprev">${prevText(tr.m)}</div>
   <button class="mp-keep" id="mpkeep" aria-pressed="${S.keepFx}"><i class="led ${S.keepFx ? "on" : ""}"></i>Keep effects + routing</button></div></div>`;
}
function prevText(m) { return `<b>${m}</b> ${nameOf(m)} · ${names(pages(m).s).join(" ")}`; }
function setMachine(v, t = S.sel) {
	const c = Cat.byName[v]; if (!c) return;
	cmd("machine", { k: V.kit, t, model: c.model, keepFx: S.keepFx }, undefined, [[["tracks", t, "m"], v], [["tracks", t, "fam"], famOf(v)], [["tracks", t, "name"], nameOf(v)]]);
	if (!params(t).includes(S.lane)) S.lane = params(t).includes("FLTF") ? "FLTF" : params(t)[0] || "FLTF";
	closePicker(); render();
}
document.addEventListener("click", e => {
	if (e.target.closest("#machbtn")) { $("#machpop").hidden ? openPicker() : closePicker(); return; }
	const pop = $("#machpop"); if (pop.hidden) return;
	const f = e.target.closest(".mf"); if (f) { S.pickFam = f.dataset.fam; drawPicker(); return; }
	const m = e.target.closest(".mk"); if (m) { setMachine(m.dataset.mach); return; }
	if (e.target.closest("#mpkeep")) { S.keepFx = !S.keepFx; drawPicker(); return; }
	if (!e.target.closest("#machpop")) closePicker();
}, true);
document.addEventListener("mouseover", e => { const m = e.target.closest("#machpop .mk"); if (m) $("#mpprev").innerHTML = prevText(m.dataset.mach); });
document.addEventListener("focusin", e => { const m = e.target.closest("#machpop .mk"); if (m) $("#mpprev").innerHTML = prevText(m.dataset.mach); });
document.addEventListener("keydown", e => { if (e.key === "Escape" && !$("#machpop").hidden) { closePicker(); $("#machbtn")?.focus(); } });

/* ===== Key-style dropdowns: every <select> gets a key + a list of keys ===== */
let kFor = null;
function enhanceSelects(root) {
	root.querySelectorAll("select").forEach(sel => {
		if (sel.dataset.k) return; sel.dataset.k = "1"; sel.hidden = true;
		const b = document.createElement("button"); b.className = "kselbtn"; b.type = "button"; b.dataset.for = sel.id; b.setAttribute("aria-haspopup", "listbox"); b.setAttribute("aria-expanded", "false");
		const lab = sel.closest("label"); b.setAttribute("aria-label", (lab ? lab.firstChild.textContent.trim() + ": " : "") + sel.selectedOptions[0]?.text);
		b.innerHTML = `<span>${sel.selectedOptions[0]?.text ?? ""}</span><svg viewBox="0 0 10 6" aria-hidden="true"><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>`; sel.after(b);
	});
}
function openK(btn) {
	const sel = document.getElementById(btn.dataset.for); kFor = btn; const pop = $("#kpop"); let n = 0, h = "";
	for (const node of sel.children) { if (node.tagName === "OPTGROUP") { h += `<div class="kgrp">${node.label}</div>`; for (const o of node.children) { h += kopt(o, sel); n++; } } else { h += kopt(node, sel); n++; } }
	const cols = n > 18 ? 3 : n > 9 ? 2 : 1; pop.innerHTML = `<div class="klist" style="grid-template-columns:repeat(${cols},minmax(0,1fr))">${h}</div>`;
	pop.hidden = false; pop.style.minWidth = Math.max(btn.offsetWidth, cols * 150) + "px"; placeK(); btn.setAttribute("aria-expanded", "true");
	(pop.querySelector(".kopt[aria-selected=true]") || pop.querySelector(".kopt"))?.focus({ preventScroll: true });
}
/* The popup beside its button, inside the window: below when it fits, else above; neither: the side
   with more room, its height capped (the list scrolls). Fixed to the viewport, placed again on resize
   and scroll. */
function placeK() {
	const pop = $("#kpop"), btn = kFor; if (pop.hidden || !btn) return;
	if (!btn.isConnected) { closeK(); return; }
	const M = 8, G = 4, r = btn.getBoundingClientRect(), vw = document.documentElement.clientWidth, vh = document.documentElement.clientHeight;
	pop.style.position = "fixed"; pop.style.maxHeight = ""; pop.style.overflowY = ""; pop.style.top = "0px"; pop.style.left = "0px";
	const h = pop.offsetHeight, w = pop.offsetWidth, below = vh - r.bottom - G - M, above = r.top - G - M;
	const down = h <= below || (h > above && below >= above);
	if (h > (down ? below : above)) { pop.style.maxHeight = Math.max(80, down ? below : above) + "px"; pop.style.overflowY = "auto"; }
	const hh = pop.offsetHeight;
	pop.style.top = Math.round(down ? r.bottom + G : Math.max(M, r.top - G - hh)) + "px";
	pop.style.left = Math.round(Math.max(M, Math.min(r.left, vw - w - M))) + "px";
}
addEventListener("resize", placeK);
addEventListener("scroll", e => { if (!$("#kpop").contains(e.target)) placeK(); }, true);
function kopt(o, sel) { if (o.hidden) return ""; return `<button class="kopt" role="option" data-v="${o.value}" aria-selected="${o.value === sel.value}"${o.disabled ? ` disabled aria-disabled="true" title="${o.title || "Not available"}"` : ""}>${o.text}</button>`; }
function closeK() { const pop = $("#kpop"); if (pop.hidden) return; pop.hidden = true; kFor?.setAttribute("aria-expanded", "false"); if (pendingRender) scheduleRender(); }
document.addEventListener("click", e => {
	const b = e.target.closest(".kselbtn"); if (b) { const same = kFor === b && !$("#kpop").hidden; closeK(); if (!same) openK(b); return; }
	const o = e.target.closest("#kpop .kopt"); if (o && o.disabled) return; if (o && kFor) { const sel = document.getElementById(kFor.dataset.for); sel.value = o.dataset.v; kFor.querySelector("span").textContent = sel.selectedOptions[0].text; closeK(); kFor.focus(); sel.dispatchEvent(new Event("change", { bubbles: true })); return; }
	if (!e.target.closest("#kpop")) closeK();
}, true);
document.addEventListener("keydown", e => {
	if ($("#kpop").hidden) return; if (e.key === "Escape") { closeK(); kFor?.focus(); return; }
	const opts = [...document.querySelectorAll("#kpop .kopt")], i = opts.indexOf(document.activeElement), d = { ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1 }[e.key]; if (d && opts.length) { e.preventDefault(); opts[(i + d + opts.length) % opts.length].focus(); }
});
ED.echo = {
	to: toMfx("echo"),
	draw(g, W, H) {
		const v = V.mfx.echo.v, dt = Math.max(1, v.TIME) / 256, fb = v.FB / 64; grid(g, W, H);
		g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 8 - (H - 22)); g.lineTo(W, H - 8 - (H - 22)); g.stroke(); g.setLineDash([]);
		let a = 1, x = 0, k = 0; g.fillStyle = cssv("--ink"); while (x <= 1 && k < 40) { const h = Math.min(1.25, a) * (H - 22); g.fillRect(Math.round(x * W), H - 8 - h, k ? 4 : 6, h); x += dt; a *= fb; k++; if (a < .02) break; }
		label(g, fb > 1 ? "echo taps · feedback grows!" : "echo taps · 2 bars");
	},
	handles(W, H) { const v = V.mfx.echo.v, dt = Math.max(1, v.TIME) / 256, a2 = Math.min(1.25, v.FB / 64); return [{ x: dt * W + 2, y: H - 8 - a2 * (H - 22), k: "TIME · FB", c: cssv("--ink"), drag: (x, y) => ({ TIME: clamp(Math.round(x / W * 256)), FB: clamp(Math.round((H - 8 - y) / (H - 22) * 64)) }) }]; }
};
ED.gate = {
	to: toMfx("gate"),
	draw(g, W, H) {
		const v = V.mfx.gate.v, pre = v.PRED / 127 * .15, dec = .5 + v.DEC / 127 * 2.5, gate = v.GATE >= 127 ? 9 : v.GATE / 127 * 3, span = 3.3; grid(g, W, H);
		line(g, W, x => { const t = x / W * span; if (t < pre) return H - 8; if (t > pre + gate) return H - 8; return H - 8 - (H - 22) * Math.exp(-(t - pre) / (dec / 4)); }, cssv("--ink"), 2.2);
		if (v.GATE < 127) { const gx = (pre + gate) / span * W; g.strokeStyle = inkA(.6); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(gx, 6); g.lineTo(gx, H); g.stroke(); g.setLineDash([]); }
		label(g, v.GATE >= 127 ? "reverb · gate off" : "reverb · gated");
	},
	handles(W, H) {
		const v = V.mfx.gate.v, span = 3.3, pre = v.PRED / 127 * .15, dec = .5 + v.DEC / 127 * 2.5, gate = v.GATE >= 127 ? 3.2 : v.GATE / 127 * 3;
		return [{ x: pre / span * W + 6, y: H - 10, k: "PRED", c: cssv("--ink"), drag: x => ({ PRED: clamp(Math.round((x / W * span) / .15 * 127)) }) },
		{ x: (pre + dec / 4) / span * W, y: H - 8 - (H - 22) / Math.E, k: "DEC", c: cssv("--ink"), drag: x => ({ DEC: clamp(Math.round(((x / W * span - pre) * 4 - .5) / 2.5 * 127)) }) },
		{ x: Math.min(W - 6, (pre + gate) / span * W), y: 14, k: "GATE", c: cssv("--ink"), drag: x => { const t = x / W * span - pre; return { GATE: t >= 3.05 ? 127 : clamp(Math.round(t / 3 * 127)) }; } }];
	}
};

/* ===== Control: the plug-in's MIDI Learn (jucePluginLib) as the backend ===== */
function toggleLearn() {
	S.ctl.learn = !S.ctl.learn; S.ctl.learnT = null; document.body.classList.toggle("learn", S.ctl.learn); renderTop(); syncControls();
	if (S.ctl.learn) toast("LEARN: click a value, then turn a knob on your MIDI controller."); else cmd("learnCancel");
}
/* App modulators: the page edits build a new Mods.doc (mdDeskMod.js) and it is sent whole; values
   and the CC rate come back. modInFlight is the id of a modSet not answered yet (the MM page's
   pattern): while it is set, an incoming "mod" message does not overwrite the pending edit. */
let modInFlight = 0;
function sendMods() { modInFlight = cmd("modSet", { doc: Mods.doc }, "modSet", undefined, r => { if (r.id === modInFlight) modInFlight = 0; }); }
function paramName(t, i) { return slots(V.tracks[t].m)[i] || "#" + (i + 1); }
function modRow(sr) {
	const C = S.ctl, id = "app:" + sr.id;
	return `<button class="srch k-${sr.kind === "lfo" ? "lfo" : "rnd"} ${id === C.sel ? "sel" : ""}" data-src="${id}" style="--f:${Mods.valueOf(sr.id) / 127 * 100}%" title="App only: runs in the editor on the machine's steps and sends CCs"><span class="sk">APP</span><b>${sr.label}</b><span class="sv mono">${Mods.valueOf(sr.id)}</span></button>
	 ${V.tracks.map((t, i) => { const ls = Mods.linksOf(sr.id).filter(o => o.l.track === i); return `<button class="mxc ${ls.length ? "on" : ""} ${id === C.sel && C.selT === i ? "sel" : ""}" data-mxsrc="${id}" data-mxt="${i}" title="${ls.map(o => paramName(i, o.l.param)).join(", ") || "no link"}">${ls.slice(0, 2).map(o => `<i>${paramName(i, o.l.param)}</i>`).join("")}${ls.length > 2 ? `<i>+${ls.length - 2}</i>` : ""}</button>`; }).join("")}`;
}
function modInspector(sr) {
	const C = S.ctl, links = Mods.linksOf(sr.id).filter(o => C.selT == null || o.l.track === C.selT);
	const rate = `<div class="irow"><span class="ilab">Rate</span><span class="seg" data-set="srcrate">${Mods.RATES.map(x => `<button data-v="${x}" aria-pressed="${sr.rate === x}">${x}</button>`).join("")}</span></div>`;
	const params = sr.kind === "lfo" ? `<div class="irow"><span class="ilab">Shape</span><div class="shapes">${SHAPES.map((n, i) => `<button data-srcshape="${i}" aria-pressed="${sr.shape === i}" title="${n}">${shapeIcon(i, false)}</button>`).join("")}</div></div>${rate}
	 <div class="irow"><span class="ilab">Depth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="Depth" aria-valuemax="100" data-g="src" data-n="depth" data-src="${sr.id}"><span>DEPTH</span><b></b></div></div></div>`
		: `${rate}<div class="irow"><span class="ilab">Smooth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="Smooth" data-g="src" data-n="smooth" data-src="${sr.id}"><span>SMOOTH</span><b></b></div></div></div>`;
	return `<section class="card"><header><h3>${sr.label}</h3><span><b class="apponly">App only</b> · not in the kit · <span title="${Mods.runs === "plug-in" ? "Run by the plug-in: they keep moving with the editor closed, saved with the project" : "Run by the editor while it is open"}">${Mods.runs === "plug-in" ? "runs with the editor closed" : "runs while the editor is open"}</span> · <span class="mono" id="ccrate" title="CCs the app modulators sent in the last second; the budget is ${Mods.limit}">${Mods.cc}/s of ${Mods.limit}</span></span></header>
	 <div class="note">Runs in the editor and moves with the machine's own steps while it plays, sent as CCs like host automation (at most ${Mods.limit} a second). A real Machinedrum does not play it, a lock wins on its step, and live recording does not record it. The setup is not saved with the project yet.</div>
	 ${params}
	 <div class="lhead"><span class="cap">Targets</span>${C.selT != null ? `<button class="ptog on" data-selt="all"><i class="led"></i>Track ${C.selT + 1} only</button>` : `<span class="note">${links.length} target${links.length === 1 ? "" : "s"}</span>`}</div>
	 <div class="lnks">${links.map(({ l, li }) => `<div class="lnk"><span class="lcdchip" title="${V.tracks[l.track].name}">T${l.track + 1} ${paramName(l.track, l.param)}</span>
	  <div class="pc" role="slider" tabindex="0" aria-label="Min" data-g="link" data-n="min" data-li="${li}"><span>MIN</span><b></b></div><div class="pc" role="slider" tabindex="0" aria-label="Max" data-g="link" data-n="max" data-li="${li}"><span>MAX</span><b></b></div>
	  <span class="seg" data-set="lcurve" data-li="${li}">${["lin", "exp", "log"].map(c => `<button data-v="${c}" aria-pressed="${l.curve === c}">${c.toUpperCase()}</button>`).join("")}</span>
	  <button class="ptog ${l.invert ? "on" : ""}" data-minv="${li}"><i class="led"></i>Inv</button><button class="iconkey" data-mdel="${li}" aria-label="Remove target" title="Remove">×</button></div>`).join("") || `<div class="note">No targets yet. Add one below.</div>`}</div>
	 <div class="irow addrow"><span class="ilab">Add</span><select id="mt">${V.tracks.map((t, i) => `<option value="${i}" ${i === C.addT ? "selected" : ""}>Track ${i + 1} · ${t.m}</option>`).join("")}</select>
	  <select id="mp">${slots(V.tracks[C.addT].m).map((p, i) => p ? `<option value="${i}">${p}</option>` : "").join("")}</select><button class="cream" id="maddl">Add target</button></div>
	 <div class="irow"><span class="ilab"></span><button data-addsrc="lfo">+ App LFO</button><button data-addsrc="random">+ Random</button><button class="danger" data-delsrc="${sr.id}">Remove ${sr.label}</button></div></section>`;
}
/* Controller rows: 8 knobs (CC 21-28 by default, the CC number is editable per row; kept per
   viewer), plus any other CC the plug-in's MIDI Learn preset maps. A row id is "ch:cc" (ch 255 =
   any channel). Mapping works with LEARN or here: a cell opens the row with that track's picker. */
/* The eight knob rows' CCs are part of the editor's setup (md-desk/setup), kept with the project by
   the plug-in (P4); the desk sends them with "setup". */
const KNOB_CCS = [21, 22, 23, 24, 25, 26, 27, 28];
function saveKnobs() { cmd("knobs", { ccs: KNOB_CCS.slice() }); }
function ccRows(maps) {
	const rows = KNOB_CCS.map((cc, k) => ({ id: "255:" + cc, ch: 255, cc, label: "Knob " + (k + 1), knob: k }));
	for (const m of maps) { const id = m.ch + ":" + m.cc; if (!rows.some(r => r.id === id)) rows.push({ id, ch: m.ch, cc: m.cc, label: m.ch === 255 ? "CC " + m.cc : "CC " + m.cc + " ch " + (m.ch + 1), knob: -1 }); }
	return rows;
}
function renderControl() {
	const L = Docs.learn || { mappings: [], learning: null }, maps = L.mappings, rows = ccRows(maps);
	const C = S.ctl;
	if (C.sel == null || (C.sel.startsWith("app:") && !Mods.source(C.sel.slice(4))) || (!C.sel.startsWith("app:") && !rows.some(r => r.id === C.sel))) C.sel = rows[0].id;
	const mapsOf = id => maps.filter(m => m.ch + ":" + m.cc === id);
	const mx = `<div class="mx"><span></span>${V.tracks.map((t, i) => `<span class="mxh" title="${t.name}"><b>${i + 1}</b><small>${t.m}</small></span>`).join("")}
  ${rows.map(src => { const ms = mapsOf(src.id); return `<button class="srch ${src.id === C.sel ? "sel" : ""} k-cc" data-src="${src.id}" style="--f:0%" title="${ms.length ? ms.length + " target" + (ms.length > 1 ? "s" : "") : "Not mapped: does nothing yet"}"><span class="sk">CC ${src.cc}</span><b>${src.label}</b><span class="sv mono">${ms.length ? ms.length : "–"}</span></button>
   ${V.tracks.map((t, i) => { const ls = ms.filter(m => m.t === i); return `<button class="mxc ${ls.length ? "on" : ""} ${src.id === C.sel && C.selT === i ? "sel" : ""}" data-mxsrc="${src.id}" data-mxt="${i}" title="${ls.map(l => slots(t.m)[l.i] || l.name).join(", ") || "Map " + src.label + " to track " + (i + 1)}">${ls.slice(0, 2).map(l => `<i>${slots(t.m)[l.i] || l.name}</i>`).join("")}${ls.length > 2 ? `<i>+${ls.length - 2}</i>` : ""}</button>`; }).join("")}`; }).join("")}
  ${Mods.doc.sources.map(modRow).join("")}</div>`;
	const app = C.sel.startsWith("app:") ? Mods.source(C.sel.slice(4)) : null;
	let insp;
	if (app) insp = modInspector(app);
	else {
		const row = rows.find(r => r.id === C.sel), selMaps = mapsOf(C.sel).filter(m => C.selT == null || m.t === C.selT);
		const t = C.selT != null ? C.selT : C.addT;
		insp = `<section class="card"><header><h3>${row.label} · CC ${row.cc}</h3><span>${row.ch === 255 ? "any channel" : "channel " + (row.ch + 1)} · from your MIDI controller · saved with the plug-in's MIDI Learn preset</span></header>
  ${row.knob >= 0 ? `<div class="irow"><span class="ilab">CC</span><span class="stepper"><button data-knobcc="-1" aria-label="Lower CC number">−</button><b class="mono">${row.cc}</b><button data-knobcc="1" aria-label="Higher CC number">+</button></span><span class="note">the CC your controller's knob ${row.knob + 1} sends; its targets follow</span></div>` : ""}
  <div class="note">${L.learning ? `Learning <b>track ${L.learning.t + 1} ${slots(V.tracks[L.learning.t].m)[L.learning.i] || L.learning.name}</b>: turn a knob both ways.` : "Pick a target below, or press LEARN, click any value and turn a knob. The mapping moves the value through the plug-in's parameters, like host automation."}</div>
  <div class="lhead"><span class="cap">Targets</span>${C.selT != null ? `<button class="ptog on" data-selt="all"><i class="led"></i>Track ${C.selT + 1} only</button>` : `<span class="note">${selMaps.length} target${selMaps.length === 1 ? "" : "s"}</span>`}</div>
  <div class="lnks">${selMaps.map(l => `<div class="lnk"><span class="lcdchip" title="${V.tracks[l.t].name}">T${l.t + 1} ${slots(V.tracks[l.t].m)[l.i] || l.name}</span><span class="note">${l.mode}</span><span></span>
   <button class="ptog ${l.invert ? "on" : ""}" data-linv="${l.index}"><i class="led"></i>Inv</button><button class="iconkey" data-ldel="${l.index}" aria-label="Remove mapping" title="Remove">×</button></div>`).join("") || `<div class="note">Not mapped: this row does nothing yet.</div>`}</div>
  <div class="irow addrow"><span class="ilab">Add</span><select id="ct">${V.tracks.map((x, i) => `<option value="${i}" ${i === t ? "selected" : ""}>Track ${i + 1} · ${x.m}</option>`).join("")}</select>
   <select id="cp">${slots(V.tracks[t].m).map((p, i) => p ? `<option value="${i}">${p}</option>` : "").join("")}<option value="24">LEVEL</option></select><button class="cream" id="caddl">Add target</button></div>
  <div class="irow"><span class="ilab"></span><button data-addsrc="lfo">+ App LFO</button><button data-addsrc="random">+ Random</button></div></section>`;
	}
	$("#main").innerHTML = `<div class="ctlui"><section class="card"><header><h3>Mapping matrix</h3><span>rows = controller knobs, learned CCs and app modulators · columns = tracks</span></header>${mx}</section>${insp}</div>`;
	syncControls();
}
/* Values and the CC rate while the machine plays: in place, no re-render. */
function syncMods() {
	$$(".srch[data-src^='app:']").forEach(h => { const v = Mods.valueOf(h.dataset.src.slice(4)); h.style.setProperty("--f", v / 127 * 100 + "%"); const sv = h.querySelector(".sv"); if (sv) sv.textContent = v; });
	const r = $("#ccrate"); if (r) { r.textContent = `${Mods.cc}/s of ${Mods.limit}`; r.classList.toggle("hot", Mods.cc >= Mods.limit); }
}
document.addEventListener("pointerdown", e => {
	if (!S.ctl.learn) return;
	const el = e.target.closest(".pc[data-g],.fader[data-g]");
	if (el && ["syn", "fx", "rt", "lfo"].includes(el.dataset.g)) {
		e.stopPropagation(); e.preventDefault();
		const t = el.dataset.t != null ? +el.dataset.t : S.sel, n = el.dataset.n;
		const i = el.dataset.g === "lfo" ? Enums().lfoParams[n] : pidx(t, n, el.dataset.g);
		if (i == null || i < 0) return;
		/* only the targets the plug-in's learn takes (the learn document's limits) */
		const lim = Docs.learn && Docs.learn.limits;
		if (lim && (t >= lim.tracks || !lim.params.some(q => q.i === i))) { toast(`Track ${t + 1} ${n} cannot be learned.`); return; }
		S.ctl.learnT = { t, p: n }; syncControls(); cmd("learnStart", { t, i }); toast(`Target: track ${t + 1} ${n}. Now turn a knob on your controller.`);
	}
}, true);

/* ===== Copy / clear / paste (the hardware's COPY CLEAR PASTE, per workspace; clipboard in the plug-in) ===== */
function secAction(kind) {
	if (S.ws === "seq") { const [a, b] = vis(); const range = { p: V.pat, t: S.sel, from: a, to: Math.min(b, V.len) };
		if (kind === "copy") cmd("copySteps", range); else if (kind === "clear") cmd("clearSteps", range); else if (S.multi.size) pasteToMany(a); else cmd("pasteSteps", { p: V.pat, t: S.sel, from: a }); return; }
	if (S.ws === "sound") { const a = { k: V.kit, t: S.sel }; cmd(kind === "copy" ? "copySound" : kind === "clear" ? "clearSound" : "pasteSound", a); return; }
	if (S.ws === "song") { const i = S.songSel;
		if (kind === "copy") songCmd("copyRow", { i }); else if (kind === "clear") songAction("del"); else { const at = V.song[i]?.type === "end" ? i : i + 1; songCmd("pasteRow", { i: at }); S.songSel = at; } return; }
	toast("Copy, clear and paste work in Sequence, Sound and Song.");
}

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
function showAlt(on) { if (S.alt === on) return; S.alt = on; document.body.classList.toggle("althold", on); altLabels(); if (S.ws === "seq") genDraw(); if (S.ws === "sound") renderMutStrip(); }
addEventListener("keydown", e => showAlt(e.altKey), true);
addEventListener("keyup", e => showAlt(e.altKey), true);
addEventListener("blur", () => showAlt(false));
document.addEventListener("pointermove", e => showAlt(e.altKey), { passive: true, capture: true });

/* ===== Generators and mutation (DESIGN-generators.md): the GEN bar, always on the Sequence page, and the
   MUTATE bar, always on the Sound page. Both are live: a change writes at once, and a run of changes is one
   gesture (one undo step) that one Undo takes back. The results come from mdDeskGen.js (pure); the page sends
   them as the two plain edits: steps (one pattern change, one dump) and params (one working-kit change, CCs).
   Alt is the global "all": Alt + a GEN change every track. One randomise action (R, the bars' R key) means what
   the workspace edits: a new GEN variation of the selected track (Sequence, and every workspace but Sound), a fresh
   mutation of the selected track from the trial's base (Sound); Alt+R or Alt-click on the R key: every track (the
   whole kit). ===== */
S.gen = { specs: null, last: [], run: null };
S.mut = { amount: 20, seed: genSeed(), scope: new Set(["syn"]), trial: null, note: "" };
const GEN_TRACKS = Array.from({ length: 16 }, (_, t) => t);
/* every track's spec, from its machine's role the first time (and on Defaults) */
function genSpecs(fill) {
	const len = V.len || 64;
	if (!S.gen.specs || fill) { S.gen.specs = V.tracks.map(t => genDefault(t.m, len)); S.gen.last = V.tracks.map(() => ({})); }
	S.gen.specs.forEach((sp, t) => { S.gen.specs[t] = genFit(sp, len); });	/* STEPS never longer than the pattern, also when it got shorter */
	return S.gen.specs;
}
function genSpec(t = S.sel) { return genSpecs()[t]; }
/* the range a generator writes: the steps shown (one page, or all), Alt: the whole pattern */
function genRange(all) { const [a, b] = vis(); return all ? [0, V.len] : [a, Math.min(b, V.len)]; }
/* A run (the GEN bar) or a trial (the MUTATE bar) lives in one context: the workspace, the selected track and
   the pattern (kit). Selecting another track, another workspace or another pattern ends it; so does any other
   edit, undo or redo (cmd). */
const genKey = () => `${S.ws}:${S.sel}:${V.pat}`, mutKey = () => `${S.ws}:${S.sel}:${V.kit}`;
function endStaleRuns() {
	if (S.gen.run && S.gen.run.key !== genKey()) S.gen.run = null;
	if (S.mut.trial && S.mut.trial.key !== mutKey()) { S.mut.trial = null; S.mut.note = ""; }
}
/* Live: every change of a GEN control writes at once. The run's changes share one gesture (one undo step back
   to the pattern before the run) and generate from that pattern, so a value moved back gives its steps back.
   Alt held: every track's spec over the whole pattern, in the same run. */
function genLive(all = S.alt) {
	if (V.rec) { toast("The generators wait while the machine records live."); return false; }
	if (!V.loaded) { toast("The pattern is not loaded yet."); return false; }
	const run = genRunFor(S.gen.run, genKey(), () => V.tracks.map(x => x.trigs.slice()), Bridge.gesture);
	const [from, to] = genRange(all), rows = [], w = [];
	for (const t of all ? GEN_TRACKS : [S.sel]) {
		const r = genResult(t, from, to, run); if (!r) continue;
		const tr = V.tracks[t], on = new Set(r.on), row = { t, on: r.on };
		if (r.acc && !V.accAll) row.acc = r.acc;
		const acc = row.acc ? new Set(row.acc) : null;
		for (let s = from; s < to; s++) {
			const want = on.has(s);
			if (want !== tr.trigs[s]) w.push([["tracks", t, "trigs", s], want]);
			if (!want && tr.trigs[s]) w.push([["tracks", t, "acc", s], false], [["tracks", t, "slide", s], false], ...clearStep(t, s));
			if (acc && want) w.push([["tracks", t, "acc", s], acc.has(s)]);
		}
		rows.push(row);
	}
	if (!rows.length) { if (all) toast("Every track is set to keep: nothing to generate."); genDraw(); return false; }
	S.gen.run = run;
	cmd("steps", { p: V.pat, from, to, rows, g: run.g }, "gen", w);
	run.applied++;
	render();
	return true;
}
/* a track's result in a run: its spec from the run's base; Keep during a run gives the base back */
function genResult(t, from, to, run) {
	const base = run ? run.base[t] : V.tracks[t].trigs, r = generate(genSpec(t), t, from, to, base);
	if (r || !run || !run.applied) return r;
	const on = []; for (let s = from; s < to; s++) if (base[s]) on.push(s);
	return { on };
}
/* Defaults (the GEN bar's Defaults key): every track's spec from its machine, written as a change */
function genDefaults() { genSpecs(true); toast("Every track's spec from its machine."); genLive(); }
/* GEN randomise: a new variation of a track's spec: a new seed (random), random hits and rotation inside the
   cycle (euclid); keep stays. False: nothing to vary. */
function genVary(t) {
	const sp = genSpec(t), r = n => Math.floor(Math.random() * n);
	if (sp.kind === "random") sp.seed = genSeed();
	else if (sp.kind === "euclid") { sp.k = 1 + r(sp.n); sp.rot = r(sp.n); if (sp.acc) sp.acc.k = Math.min(sp.acc.k, sp.k); }
	else return false;
	return true;
}
/* R on Sequence (and every workspace but Sound): the selected track; all: every track's spec, the whole pattern */
function genAgain(all = false) {
	const varied = (all ? GEN_TRACKS : [S.sel]).filter(genVary).length;
	if (!varied) { toast(all ? "Every track is set to keep: nothing to randomise." : `Track ${S.sel + 1} is set to keep: nothing to randomise.`); return; }
	if (genLive(all) && S.ws !== "seq") toast(all ? "GEN: a new variation of every track." : `GEN: a new variation of track ${S.sel + 1}.`);
}
/* the one randomise action (R; Alt+R or Alt-click the R key: all), by workspace */
function randomise(all) { if (S.ws === "sound") mutAgain(all); else genAgain(all); }
function genKind(kind) {
	const t = S.sel, sp = genSpec(t), last = S.gen.last[t];
	if (sp.kind === kind) return;
	last[sp.kind] = sp;
	S.gen.specs[t] = genFit(last[kind], V.len || 64) || (kind === "euclid" ? { kind, k: 4, n: Math.min(16, V.len || 64), rot: 0 } : kind === "random" ? { kind, density: 25, seed: genSeed(), mode: "replace" } : { kind });
	genLive();
}
/* a value of a bar moved by d (wheel, arrows, click, drag): the spec's own ranges */
function genVal(k, d) {
	const sp = genSpec(), lim = (v, a, b) => Math.max(a, Math.min(b, v));
	if (k === "amt") { S.mut.amount = lim(S.mut.amount + d, 0, 100); mutLive(); renderMutStrip(); return; }
	if (k === "k") sp.k = lim(sp.k + d, 0, sp.n);
	if (k === "n") { sp.n = lim(sp.n + d, 1, Math.min(64, V.len || 64)); sp.k = Math.min(sp.k, sp.n); sp.rot = Math.min(sp.rot, sp.n - 1); if (sp.acc) sp.acc.k = Math.min(sp.acc.k, sp.k); }
	if (k === "rot") sp.rot = ((sp.rot + d) % sp.n + sp.n) % sp.n;
	if (k === "acc") { const a = lim((sp.acc ? sp.acc.k : 0) + d, 0, sp.k); if (a) sp.acc = { k: a, rot: 0 }; else delete sp.acc; }
	if (k === "dens") sp.density = lim(sp.density + d, 0, 100);
	if (k === "racc") { const a = lim((sp.acc ? sp.acc.density : 0) + d, 0, 100); if (a) sp.acc = { density: a }; else delete sp.acc; }
	if (k === "seed") sp.seed = ((sp.seed - 1 + d) % 99999 + 99999) % 99999 + 1;
	genLive();
}
/* The bars' pieces: a group (a title on a thin rule over its controls), a value (an LCD window, its label inside),
   a key hint. */
const gbg = (label, body, cls = "", tip = "") => `<div class="gbg ${cls}"${tip ? ` title="${tip}"` : ""}><span class="gbl">${label}</span><div class="gbc">${body}</div></div>`;
const gv = (k, label, v, tip) => `<span class="gv" data-gv="${k}" role="spinbutton" tabindex="0" aria-label="${label}" aria-valuenow="${parseInt(v) || 0}" title="${tip}. Drag up or down, scroll, or click (⇧-click: down)."><small>${label}</small><b>${v}</b></span>`;
const kbd = k => `<kbd>${k}</kbd>`;
/* a key of a bar: a small square cap with its key on it (the keyboard shortcut too) and a tiny label over it */
const kc = (attr, act, cap, label, tip, off = false, cls = "", on = null) => `<button class="kc ${cls}${on ? " on" : ""}" ${attr}="${act}" ${off ? "disabled" : ""}${on != null ? ` aria-pressed="${on}"` : ""} title="${tip}" aria-label="${label}"><small>${label}</small><kbd>${cap}</kbd></button>`;
/* the bars' one randomise key: R, with its "all" chord under it */
const randKey = tip => `<button class="kc cream krand" data-rand="1" title="${tip}" aria-label="Randomise"><small>Random <em>⌥R all</em></small><kbd>R</kbd></button>`;
const gtitle = (name, target, all, tip) => `<div class="gbt" title="${tip}"><b>${name}</b><span class="${all ? "all" : ""}">${target}</span></div>`;
function genStripHtml() {
	const sp = genSpec(), t = S.sel, tr = V.tracks[t], all = S.alt, run = S.gen.run && S.gen.run.key === genKey() ? S.gen.run : null;
	const [from, to] = genRange(all), r = genResult(t, from, to, run), base = run ? run.base[t] : tr.trigs;
	const seg = (attr, cur, items, tips) => `<span class="seg">${items.map(([k, n]) => `<button ${attr}="${k}" aria-pressed="${cur === k}" title="${tips[k]}">${n}</button>`).join("")}</span>`;
	const mode = gbg("Mode", seg("data-genkind", sp.kind, [["euclid", "Euclid"], ["random", "Random"], ["keep", "Keep"]], { euclid: "k hits spread evenly over n steps, rotated", random: "each step on by chance, from a seed", keep: "leave this track as it is (in a run: as it was before the run)" }), "gmode");
	const params = sp.kind === "euclid"
		? gbg("Euclid", gv("k", "Hits", sp.k, "How many hits in a cycle") + gv("n", "Steps", sp.n, "The cycle's length in steps, at most the pattern's; it repeats over the pattern") + gv("rot", "Rotate", sp.rot, "Moves the hits later") + gv("acc", "Accent", sp.acc ? sp.acc.k : "off", "Accents spread over the hits" + (V.accAll ? " (EDIT ALL is on: accents are pattern-wide and stay)" : "")))
		: sp.kind === "random"
		? gbg("Random", gv("dens", "Density", sp.density + "%", "The chance of each step") + gv("racc", "Accent", sp.acc ? sp.acc.density + "%" : "off", "Accents by chance on the hits")
			+ gv("seed", "Seed", sp.seed, "The same seed gives the same steps") )
			+ gbg("Write", seg("data-genmode", sp.mode, [["replace", "Replace"], ["add", "Add"], ["thin", "Thin"]], { replace: "the track becomes the result", add: "only steps that are off may turn on", thin: "only steps that are on may turn off" }), "gwrite")
		: gbg("Keep", `<span class="gsum">Track ${t + 1} is left as it is.</span>`);
	let sum = "";
	if (r) {
		const b = new Set(); for (let s = from; s < to; s++) if (base[s]) b.add(s);
		const add = r.on.filter(s => !b.has(s)).length, gone = [...b].filter(s => !r.on.includes(s)).length;
		sum = all ? `every track · steps ${from + 1}–${to}` : `${sp.kind === "euclid" ? genSummary(sp, V.len) + " · " : ""}${r.on.length} on${run ? ` <em>+${add} −${gone}</em>` : ""} · steps ${from + 1}–${to}`;
	} else if (all) sum = `every track · steps ${from + 1}–${to}`;
	const live = run && run.applied;
	return `${gtitle("Gen", all ? "all tracks" : `track ${t + 1} · ${codeOf(tr.m)}`, all, "Generators: every change writes to the pattern at once. A run of changes on this track is one undo step; Undo takes it back in one step. Alt: every track's spec, the whole pattern.")}
  ${mode}${params}
  <div class="gsum" title="${live ? "What the run changed, against the pattern before it" : "What a change writes"}">${sum}</div>
  <div class="gkeys">${randKey(all ? "Randomise every track: a new variation of each spec, the whole pattern (Alt+R)" : `Randomise track ${t + 1}: a new variation, ${sp.kind === "random" ? "a new seed" : sp.kind === "euclid" ? "random hits and rotation in the cycle" : "nothing while it is set to Keep"} (R). Alt+R or Alt-click: every track`)}${kc("data-gen", "fill", "↺", "Defaults", "Defaults: every track's spec from its machine: kicks 4/16, snares on 2 and 4, hats 8/16, the rest random; MIDI, CTR and inputs kept. Writes this track (Alt: every track)")}</div>`;
}
/* the bar again (and the rail's spec tags), without a full render */
function genDraw() {
	if (S.ws !== "seq") return;
	const host = $("#genband"); if (host) host.innerHTML = genStripHtml();
	$$(".th[data-sel]").forEach(h => { const i = +h.dataset.sel, g = h.querySelector(".gtag"); if (g) g.textContent = genTag(genSpec(i)); });
}

/* ---- mutation ---- */
/* the kit as values: 16 tracks of {m, v[24]} (the working kit document) */
function kitValues() {
	const K = kitDocOf(Docs); if (!K) return null;
	return K.tracks.map(kt => ({ m: kt.machine || "GND-EMPTY", v: [...kt.synth, ...kt.effects, ...kt.routing] }));
}
function mutGroupKnobs(t, id) {
	const [g, key] = id.split(":"), [syn, fxrt] = sndGroups(V.tracks[t]);
	return ([...syn, ...fxrt].find(x => x.g === g && x.key === key) || { knobs: [] }).knobs;
}
/* one apply of the trial: always from its base (R randomises the sound, it never walks away), the trial's gesture, so the trial is one undo step */
function mutApply(all) {
	if (!S.mut.scope.size) { toast("Pick what to move: SYN, FX, RTG or a group's title."); return; }
	let tr = S.mut.trial;
	if (!tr || tr.key !== mutKey()) {
		const base = kitValues(); if (!base) { toast("The kit is not loaded yet."); return; }
		S.mut.seed = genSeed();	/* a new trial, a new seed */
		tr = S.mut.trial = { key: mutKey(), g: Bridge.gesture(), kit: V.kit, base, touched: new Set(), all: false, applied: 0 };
	}
	tr.all = all;
	const tracks = all ? GEN_TRACKS : [S.sel];
	const spec = { tracks, groups: [...S.mut.scope], amount: S.mut.amount, seed: S.mut.seed, protect: ["VOL"] };
	const values = mutate(spec, tr.base, m => slots(m), mutGroupKnobs);
	/* Again from the base: what an earlier apply moved and this one does not goes back to the base */
	const now = new Set(values.map(([t, i]) => t + ":" + i));
	for (const k of tr.touched) if (!now.has(k)) { const [t, i] = k.split(":").map(Number); values.push([t, i, tr.base[t].v[i]]); }
	if (!values.length) { toast("Nothing to move here: no named knobs in that scope (MIDI and CTR tracks are left alone)."); return; }
	const w = [], L = { 21: "SPD", 22: "DEPTH", 23: "SHMIX" };
	for (const [t, i, v] of values) {
		if (v !== tr.base[t].v[i]) tr.touched.add(t + ":" + i); else tr.touched.delete(t + ":" + i);
		const n = slots(V.tracks[t].m)[i]; if (n) w.push([["tracks", t, i < 8 ? "syn" : i < 16 ? "fx" : "rt", n], v]);
		if (L[i]) w.push([["tracks", t, "lfo", L[i]], v]);
	}
	cmd("params", { k: V.kit, values, g: tr.g }, "mut", w);
	tr.applied++;
	const moved = new Set(values.map(([t]) => t)).size;
	S.mut.note = `seed ${S.mut.seed} · ${moved} track${moved === 1 ? "" : "s"}, ${values.length} values`;
	if (S.ws === "sound") { syncControls(); redraw(); renderMutStrip(); }
}
/* amount or scope moved during a trial: heard at once, the same seed */
function mutLive() { if (S.mut.trial && S.mut.trial.key === mutKey()) mutApply(S.mut.trial.all); }
/* R on Sound: a fresh random mutation (a new seed) from the trial's base; all: the whole kit */
function mutAgain(all = false) { S.mut.seed = genSeed(); mutApply(all); }
function mutStripHtml() {
	const tr = S.mut.trial && S.mut.trial.key === mutKey() ? S.mut.trial : null, t = S.sel, all = S.alt;
	const chips = [["syn", "Syn"], ["fx", "Fx"], ["rt", "Rtg"]].map(([g, n]) => `<button data-mutg="${g}" aria-pressed="${S.mut.scope.has(g)}" title="Every named knob of the ${{ syn: "SYNTHESIS", fx: "EFFECTS", rt: "ROUTING" }[g]} page${g === "rt" ? " (VOL is kept)" : ""}">${n}</button>`).join("");
	const groups = [...S.mut.scope].filter(x => x.includes(":")).length;
	return `${gtitle("Mutate", all ? "whole kit" : `track ${t + 1} · ${codeOf(V.tracks[t].m)}`, all, "Mutate: each knob in scope is pulled toward a random target by the amount, from a seed. A trial is one undo step; Undo takes it back in one step. Alt: the whole kit.")}
  ${gbg("Move", gv("amt", "Amount", S.mut.amount + "%", "How far each knob moves toward its random target"))}
  ${gbg("Scope", `<span class="seg">${chips}</span><span class="ghint">${groups ? `+ ${groups} group${groups === 1 ? "" : "s"}` : "+ a group's title"}</span>`, "gscope", "Click a group's title below to add that group to the scope")}
  <div class="gsum" title="${S.mut.note}">${S.mut.note}</div>
  <div class="gkeys">${randKey(`${all ? "Randomise the whole kit" : `Randomise track ${t + 1}`}: a fresh random mutation, from the sound before the trial${tr ? "" : " (this sound)"}. One trial is one undo step (R; Alt+R or Alt-click: the whole kit)`)}</div>`;
}
function renderMutStrip() {
	const host = $("#mutband"); if (host) host.innerHTML = mutStripHtml();
	$$("[data-mutsg]").forEach(b => b.setAttribute("aria-pressed", S.mut.scope.has(b.dataset.mutsg)));
}
/* a group's title as a scope chip on the Sound page */
function mutTitle(x) { return ["syn", "fx", "rt"].includes(x.g) ? `<button class="mutg" data-mutsg="${x.g}:${x.key}" aria-pressed="${S.mut.scope.has(x.g + ":" + x.key)}" title="Add ${x.title} to what Mutate moves">${x.title}</button>` : x.title; }

/* the bars' clicks, values (drag, wheel, arrows) and keys */
document.addEventListener("click", e => {
	const g = e.target.closest("[data-gen]"); if (g && !g.disabled) {
		const a = g.dataset.gen;
		if (a === "fill") genDefaults();
		return;
	}
	const rk = e.target.closest("[data-rand]"); if (rk && !rk.disabled) { randomise(e.altKey || e.metaKey || e.ctrlKey); return; }
	const k = e.target.closest("[data-genkind]"); if (k) { genKind(k.dataset.genkind); return; }
	const md = e.target.closest("[data-genmode]"); if (md) { if (genSpec().mode !== md.dataset.genmode) { genSpec().mode = md.dataset.genmode; genLive(); } return; }
	const v = e.target.closest(".gv[data-gv]"); if (v && !v.dataset.dragged) { genVal(v.dataset.gv, e.shiftKey ? -1 : 1); return; }
	const c = e.target.closest("[data-mutg],[data-mutsg]"); if (c) {
		const id = c.dataset.mutg || c.dataset.mutsg; S.mut.scope.has(id) ? S.mut.scope.delete(id) : S.mut.scope.add(id);
		renderMutStrip(); mutLive(); e.stopPropagation(); return;
	}
}, true);
let gvDrag = null;
document.addEventListener("pointerdown", e => { const v = e.target.closest(".gv[data-gv]"); if (!v || e.button !== 0) return; gvDrag = { v, y: e.clientY, k: v.dataset.gv, acc: 0 }; delete v.dataset.dragged; grabPointer(v, e); });
document.addEventListener("pointermove", e => {
	if (!gvDrag) return; const d = Math.trunc((gvDrag.y - e.clientY) / 6) - gvDrag.acc; if (!d) return;
	gvDrag.acc += d; gvDrag.v.dataset.dragged = "1"; genVal(gvDrag.k, d * (gvDrag.k === "dens" || gvDrag.k === "amt" || gvDrag.k === "racc" ? 2 : 1));
	const n = document.querySelector(`.gv[data-gv="${gvDrag.k}"]`); if (n) { n.dataset.dragged = "1"; gvDrag.v = n; }
});
document.addEventListener("pointerup", () => { if (!gvDrag) return; const k = gvDrag.k; gvDrag = null; setTimeout(() => { const n = document.querySelector(`.gv[data-gv="${k}"]`); if (n) delete n.dataset.dragged; }, 0); });
document.addEventListener("wheel", e => { const v = e.target.closest(".gv[data-gv]"); if (!v) return; e.preventDefault(); genVal(v.dataset.gv, ((e.deltaY || e.deltaX) < 0 ? 1 : -1) * (e.shiftKey ? 10 : 1)); }, { passive: false });
document.addEventListener("keydown", e => {
	const v = e.target.closest?.(".gv[data-gv]"); if (!v) return; const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key]; if (d == null) return;
	e.preventDefault(); e.stopPropagation(); const k = v.dataset.gv; genVal(k, d * (e.shiftKey ? 10 : 1)); document.querySelector(`.gv[data-gv="${k}"]`)?.focus();
}, true);
const dlgClosed = () => $("#dlg").hidden;
const genRunOn = () => S.ws === "seq" && !!S.gen.run && S.gen.run.applied > 0 && S.gen.run.key === genKey();
const mutRunOn = () => S.ws === "sound" && !!S.mut.trial && S.mut.trial.applied > 0 && S.mut.trial.key === mutKey();
Keys.bind({ keys: ["R"], code: "KeyR", when: () => dlgClosed() && !LIB.open, group: "Selected track", does: "Randomise the selected track: on Sound a fresh random sound (MUTATE, from the sound before the trial); everywhere else a new GEN variation, a new seed or random hits and rotation", run: () => randomise(false) });
Keys.bind({ keys: ["R"], code: "KeyR", mod: "alt", when: () => dlgClosed() && !LIB.open, group: "All", does: "Randomise every track: on Sound the whole kit, everywhere else every track's GEN spec over the whole pattern", run: () => randomise(true) });
Keys.bind({ keys: ["GEN value"], mod: "alt", group: "All", does: "Change a GEN value: every track's spec, the whole pattern (one pattern change, one undo step per run)" });
Keys.bind({ keys: ["R key"], mod: "alt", group: "All", does: "Click: randomise every track (Sound: the whole kit; MIDI and CTR tracks are left alone, VOL is kept)" });

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
   at the length. Every press is one rotate edit; the presses while Alt stays down share one g: one undo step. */
let rotG = 0;
addEventListener("keyup", e => { if (e.key === "Alt") rotG = 0; }, true);
addEventListener("blur", () => { rotG = 0; });
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
	if (!rotG) rotG = Bridge.gesture();
	cmd("rotate", { p: V.pat, t, by, g: rotG }, undefined, rotateWrites(t, by, len));
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

/* ---- the wheel over a step with a trig moves its lock in the lane's parameter (from the kit value when it has
   none): 4 a notch, Shift 1. A run of notches on one step is one undo step. Only the vertical wheel (and Shift's
   sideways one), so a sideways scroll of the grid still scrolls. */
let wheelLock = null;
$("#main").addEventListener("wheel", e => {
	const st = e.target.closest("#seq .st"); if (!st || S.ws !== "seq") return;
	const t = +st.dataset.t, s = +st.dataset.s, d = e.deltaY || (e.shiftKey ? e.deltaX : 0);
	if (!d || !V.tracks[t].trigs[s] || V.rec || !V.loaded || !params(t).includes(S.lane)) return;
	e.preventDefault();
	if (t !== S.sel) select(t);
	const now = performance.now(), key = t + ":" + s + ":" + S.lane;
	if (!wheelLock || wheelLock.key !== key || now - wheelLock.at > 700) wheelLock = { key, g: Bridge.gesture() };
	wheelLock.at = now;
	const had = V.locks.get(lk(t, S.lane))?.get(s), cur = had ?? grp(t, S.lane)[S.lane] ?? 0, v = clamp(cur + (d < 0 ? 1 : -1) * (e.shiftKey ? 1 : 4));
	if (had != null && v === had) return;
	const g0 = gesture; gesture = wheelLock.g; const ok = setLock(t, S.lane, s, v); gesture = g0;
	if (!ok) return;
	renderTop(); refreshRow(t); renderLane();
	toast(`${laneLabel(t, S.lane)} ${v} · track ${t + 1}, step ${s + 1}`);
}, { passive: false });

/* ---- a ramp in the lock lane: Shift-drag draws a straight line from the press to the pointer, shown as it moves;
   at the release every step with a trig under it is locked on the line, one gesture (one undo step). */
function lanePoint(e) {
	const cells = $$("#lane .lb"); if (!cells.length) return null;
	const el = cells.find(c => e.clientX < c.getBoundingClientRect().right) || cells[cells.length - 1], r = el.getBoundingClientRect();
	return { s: +el.dataset.s, v: clamp(Math.round((r.bottom - 3 - e.clientY) / (r.height - 6) * 127)) };
}
function rampPoints() { const d = laneDraw, tr = V.tracks[S.sel]; return d && d.from ? genRamp(d.from.s, d.from.v, d.to.s, d.to.v, s => !!tr.trigs[s]) : []; }
function rampAt(e) {
	const p = lanePoint(e); if (!p) return;
	if (!laneDraw.from) laneDraw.from = p;
	laneDraw.to = p;
	renderLane();
	for (const [s, v] of rampPoints()) {
		const el = document.querySelector(`#lane .lb[data-s="${s}"]`); if (!el) continue;
		el.querySelector("i")?.remove(); el.insertAdjacentHTML("beforeend", barHTML(v)); el.classList.add("ramp");
	}
}
function rampSend() {
	let first = true;
	for (const [s, v] of rampPoints()) { if (!setLock(S.sel, S.lane, s, v, !first)) break; first = false; }
	renderTop();
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
	S.multi.has(i) ? S.multi.delete(i) : S.multi.add(i);
	renderRail(); renderLane();
	const list = [...S.multi].sort((a, b) => a - b).map(x => x + 1).join(" ");
	toast(S.multi.size ? `⌘V pastes into tracks ${list}` : "No tracks marked for paste");
}
function pasteToMany(from) {
	if (!seqReady()) return;
	const tracks = [...S.multi].sort((a, b) => a - b), g0 = gesture;
	gesture = Bridge.gesture();
	const say = `Pasted into tracks ${tracks.map(x => x + 1).join(" ")} (one undo step)`;
	tracks.forEach((t, j) => cmd("pasteSteps", { p: V.pat, t, from }, undefined, undefined, j === tracks.length - 1 ? r => { if (r.ok) toast(say); } : undefined));
	gesture = g0;
}

/* ---- unmute and unsolo every track (0, or M/S off over the rail) */
function unmuteAll() {
	if (!V.tracks.some(t => t.mute || t.solo)) { toast("No track is muted or soloed."); return; }
	S.soloSet = new Set(); S.userMutes = new Set(); V = view();
	V.tracks.forEach((t, i) => { if (t.mute) setMute(i, false); });
	refreshAudible();
}
document.addEventListener("click", e => { if (e.target.closest("#allon")) unmuteAll(); });
const seqKeys = () => S.ws === "seq" && dlgClosed() && $("#keyspop").hidden;
Keys.bind({ keys: ["ArrowLeft", "ArrowRight"], mod: "alt", group: "Selected track", does: "Sequence: rotate the selected track one step earlier / later: trigs, accents and locks, wrapping at the length. Presses while ⌥ is down are one undo step. The one Alt that is not \"all\": FUNCTION + arrows on the machine", when: seqKeys, run: e => rotateTrack(e.key === "ArrowRight" ? 1 : -1) });
Keys.bind({ keys: ["0"], group: "All", does: "Unmute and unsolo every track", run: () => unmuteAll() });
Keys.bind({ keys: ["Escape"], group: "Sequence", does: "Unmark the tracks marked for paste", when: () => seqKeys() && S.multi.size > 0 && !genRunOn(), run: () => { S.multi.clear(); renderRail(); renderLane(); } });
Keys.bind({ keys: ["step"], mod: "cmd", group: "Sequence", does: "Click: every 2nd step from there to the end on (from a trig: off), one undo step" });
Keys.bind({ keys: ["step"], mod: "cmd+shift", group: "Sequence", does: "Click: every 4th step from there to the end" });
Keys.bind({ keys: ["wheel on a step"], group: "Sequence", does: "Move its lock in the lane's parameter, 4 a notch (⇧: 1)" });
Keys.bind({ keys: ["lock lane"], mod: "shift", group: "Sequence", does: "Drag: a ramp, a straight line from the press to the release (one undo step)" });
Keys.bind({ keys: ["track header"], mod: "shift", group: "Sequence", does: "Click: mark the track for paste; ⌘V then pastes into every marked track (one undo step)" });

/* ===== First run: firmware needed ===== */
/* The firmware screen. Opened by itself while no MD OS 1.63 runs (it cannot be closed then), or
   from LOAD ROM in the engine menu (then it has a Close key while the firmware runs). */
function firstRun(manual) {
	const d = $("#dlg"), mode = manual && V.lifecycle !== "missing" ? "manual" : "1";
	if (d.dataset.first === mode && !d.hidden) return;
	const m = machineState().desk || {};
	d.innerHTML = `<div class="dlgbox first" role="dialog" aria-modal="true" aria-label="Firmware needed">
 <div class="lcdbig">MACHINEDRUM FIRMWARE NEEDED</div>
 <p>Machinedrum Editor runs the real Machinedrum operating system. Elektron's firmware cannot be shipped with the app, so you add the one from your own machine.</p>
 <ol><li>Dump the <b>OS 1.63</b> flash image from your Machinedrum (8 MiB, <span class="mono">.bin</span>).</li><li>Put it in the ROM folder${m.romFolder ? `: <span class="mono">${m.romFolder}</span>` : ""}.</li><li>Press <b>Check again</b>. Machinedrum Editor checks its size and version and keeps it on this computer only.</li></ol>
 <div class="btnrow"><button class="cream" data-choose-rom="1">Choose ROM file…</button><button data-romfolder="1">Show the ROM folder</button><button data-recheck="1">Check again</button><span class="note">UW, MKII and MKI units all use the same OS 1.63 image.${V.midi ? " OS 1.63 runs now. A new ROM is used after you reopen the plug-in." : ""}</span></div></div>`;
	if (mode === "manual") d.querySelector(".btnrow").insertAdjacentHTML("beforeend", `<button data-firstclose="1">Close</button>`);
	d.hidden = false; d.dataset.first = mode;
}

/* LOAD ROM with a firmware installed: which one, and REPLACE or REMOVE it (the ROM folder is the editor's:
   nothing outside it is touched). Without one, the start-up card's own words. */
function romMenu() { if (V.lifecycle === "missing") { firstRun(true); return; } cmd("romInfo"); }
function showRomInfo(m) {
	if (!m.installed) { firstRun(true); return; }
	Boot.showInstalled({ machine: "Machinedrum", os: m.os, name: m.name, size: m.size, inFolder: m.inFolder, folder: m.folder });
}
function askRemoveRom(m) {
	if (!m.inFolder) { toast("This image is outside the editor's ROM folder (" + m.folder + "); remove it there yourself."); return; }
	ask(`Remove <b>${escH(m.os)}</b> from the ROM folder? The machine stops and the editor asks for a firmware again. Your project stays.`,
		[["Remove", "danger", () => cmd("removeRom")], ["Cancel", "", () => {}]]);
}
/* what the plug-in has to say to the user (a question, a warning): its modal, never a native alert. One at a
   time: a notice waits while another dialog is open. */
const noticeQueue = [];
function showNotice(m) {
	noticeQueue.push(m);
	pumpNotices();
}
function pumpNotices() {
	if (!noticeQueue.length) return;
	if (!$("#dlg").hidden) { setTimeout(pumpNotices, 400); return; }
	const m = noticeQueue.shift(), names = m.buttons && m.buttons.length ? m.buttons : ["OK"];
	ask(`<b>${escH(m.title)}</b><br>${escH(m.text).replace(/\n/g, "<br>")}`,
		names.map((t, i) => [escH(t), i === 0 && names.length > 1 ? "cream" : "", () => { cmd("noticeAnswer", { id: m.id, button: i }); setTimeout(pumpNotices, 0); }]));
}
Bridge.onMessage(m => { if (m.type === "romInfo") showRomInfo(m); else if (m.type === "notice") showNotice(m); });

/* the start-up card's keys: the host's native file chooser, the ROM folder, a new look (the ROM stays on this computer) */
Boot.host = { chooseRom: () => cmd("chooseRom"), revealRom: () => cmd("revealRomFolder"), recheck: () => cmd("recheckFirmware"),
	removeRom: askRemoveRom, say: toast };
Bridge.onMessage(m => { if (m.type === "romInstall") { Boot.rom(m); toast(m.text); } });
/* SysEx import and export: the host's file dialogs and document writes (the page never reads the file) */
Syx.host = { choose: () => cmd("chooseSyx"), exportAll: () => cmd("syxExport"), start: kinds => cmd("syxImport", { kinds }), stop: () => cmd("syxCancel") };
Bridge.onMessage(m => {
	if (m.type === "syxPreview") Syx.preview(m);
	else if (m.type === "syxProgress") Syx.progress(m);
	else if (m.type === "syxExport") toast(m.text);
});

/* LCD line 2 editing */
let l2drag = null;
function l2step(k, d, alt) {
	if (k === "len") { if (alt) cmd("length", { p: V.pat, v: ((V.length - 1 + d + V.len) % V.len) + 1 }); else { const o = [16, 32, 48, 64], v = o[(o.indexOf(V.len) + d + 4) % 4]; cmd("totalLength", { p: V.pat, v }, undefined, [[["len"], v]]); } }
	if (k === "song") { cmd("selectSong", { s: (V.songSlot + d + 32) % 32 }); return; }
	if (k === "dbl") { doublePattern(); return; }
	if (k === "mult") { const o = Enums().tempoMultipliers; if (!o.length) return; const v = o[(o.indexOf(V.mult) + d + o.length) % o.length]; cmd("speed", { p: V.pat, v }, undefined, [[["mult"], v]]); }
	if (k === "mode") { const v = V.mode === "EXTENDED" ? "CLASSIC" : "EXTENDED"; cmd("extended", { on: v === "EXTENDED" }, undefined, [[["mode"], v]]); }
	if (k === "swing" || k === "accAmt") l2set(k, V[k] + d);
	render();
}
/* swing (50-80 %) or accent (0-15) set to v */
function l2set(k, v) {
	v = k === "swing" ? clamp(v, 50, 80) : clamp(v, 0, 15); if (v === V[k]) return;
	cmd(k === "swing" ? "swing" : "accentAmount", { p: V.pat, v }, k, [[[k], v]]);
}
document.addEventListener("pointerdown", e => { const el = e.target.closest(".l2.ed"); if (!el) return; const k = el.dataset.l2; if (k === "swing" || k === "accAmt") { l2drag = { k, y: e.clientY, v: V[k], moved: false }; gesture = Bridge.gesture(); grabPointer(el, e); e.preventDefault(); } });
document.addEventListener("pointermove", e => {
	if (!l2drag) return; if (e.buttons === 0 && e.pointerType === "mouse") { l2drag = null; gesture = 0; return; } const d = Math.round((l2drag.y - e.clientY) / (l2drag.k === "swing" ? 3 : 6)); if (d) l2drag.moved = true;
	const before = V[l2drag.k]; l2set(l2drag.k, l2drag.v + d); if (V[l2drag.k] !== before) renderSub();
});
document.addEventListener("pointerup", e => { if (!l2drag) return; const k = l2drag; l2drag = null; gesture = 0; if (!k.moved) l2step(k.k, 1); });
document.addEventListener("click", e => { const el = e.target.closest(".l2.ed"); if (!el) return; const k = el.dataset.l2; if (k !== "swing" && k !== "accAmt") l2step(k, e.shiftKey ? -1 : 1, e.altKey); });
document.addEventListener("wheel", e => { const el = e.target.closest(".l2.ed"); if (!el) return; e.preventDefault(); l2step(el.dataset.l2, (e.deltaY || e.deltaX) < 0 ? 1 : -1, e.altKey); }, { passive: false });

/* ===== Input ===== */
let drag = null;
const main = $("#main");
main.addEventListener("pointerdown", e => {
	const c = e.target.closest("canvas.ed"); if (c) { const h = nearest(c, e); if (!h) return; active = { c, k: h.k }; gesture = Bridge.gesture(); grabPointer(c, e); e.preventDefault(); redraw(); return; }
	const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (el) { drag = { el, x: e.clientX, y: e.clientY, v: getV(el), vert: el.classList.contains("fader") }; gesture = Bridge.gesture(); grabPointer(el, e); el.classList.add("act"); e.preventDefault(); return; }
	const lb = e.target.closest(".lb"); if (lb) { laneDraw = { erase: e.altKey, ramp: e.shiftKey && !e.altKey, touched: new Set() }; gesture = Bridge.gesture(); grabPointer($("#lane"), e); laneAt(e); e.preventDefault(); }
});
main.addEventListener("pointermove", e => {
	if (e.buttons === 0 && e.pointerType === "mouse" && (drag || active || laneDraw)) { endDrag(); return; }
	if (active) { const r = active.c.getBoundingClientRect(), h = ED[active.c.dataset.ed].handles(r.width, r.height, active.c).find(h => h.k === active.k); if (h) { sendEditor(active.c, h.drag(clamp(e.clientX - r.left, 0, r.width), clamp(e.clientY - r.top, 0, r.height))); syncControls(); redraw(); } return; }
	/* Mockup v60: a value box follows the axis that moved more (sideways or up/down), one value a pixel. */
	if (drag) { const fine = e.shiftKey ? .25 : 1, dx = e.clientX - drag.x, dy = drag.y - e.clientY; const d = drag.vert ? dy * 127 / 132 : (Math.abs(dx) >= Math.abs(dy) ? dx : dy); setV(drag.el, drag.v + d * fine); return; }
	if (laneDraw) { laneAt(e); return; }
	const c = e.target.closest("canvas.ed"); if (c && ED[c.dataset.ed]) c.style.cursor = nearest(c, e) ? "grab" : "default";
});
function endDrag() { if (active) { active = null; redraw(); } if (drag) { drag.el.classList.remove("act"); drag = null; } endLaneDraw(); gesture = 0; if (pendingRender) scheduleRender(); }
/* P7: a gesture ends wherever the button comes up (a pointerup outside #main left the drag on: every later
   mouse move edited the value, and the page waited for the gesture to end before showing the machine again).
   A move with no button down ends it too, and so does leaving the window. */
document.addEventListener("pointerup", endDrag); document.addEventListener("pointercancel", endDrag);
window.addEventListener("blur", () => { if (drag || active || laneDraw) endDrag(); });
/* A gesture ends with any pointer release, wherever it lands. */
document.addEventListener("pointerup", () => setTimeout(() => { if (!interacting()) gesture = 0; }), true);
main.addEventListener("wheel", e => { const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (!el) return; e.preventDefault(); const d = (e.deltaY || e.deltaX) < 0 ? 1 : -1; setV(el, getV(el) + d * (e.shiftKey ? 10 : 1)); }, { passive: false });
main.addEventListener("dblclick", e => { const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (el) setV(el, el.dataset.n === "VOL" ? 100 : 64); });
main.addEventListener("keydown", e => { const el = e.target.closest("[data-g]"); if (!el) return; const d = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 10, PageDown: -10 }[e.key]; if (d == null) return; e.preventDefault(); setV(el, getV(el) + d * (e.shiftKey ? 10 : 1)); });

function setMute(i, on) { cmd("mute", { t: i, on }, undefined, [[["tracks", i, "mute"], on]]); }
/* P7: drag across steps paints them: the first step decides (on or off) and every step the pointer
   crosses becomes that, one undo step for the whole drag. A click is a one-step paint. */
let paint = null;
function paintStep(el) {
	const i = +el.dataset.t, s = +el.dataset.s, t = V.tracks[i], k = i + ":" + s;
	if (!paint || paint.done.has(k)) return;
	paint.done.add(k);
	if (t.trigs[s] === paint.on) return;
	const w = [[["tracks", i, "trigs", s], paint.on]];
	if (!paint.on) w.push([["tracks", i, "acc", s], false], [["tracks", i, "slide", s], false], ...clearStep(i, s));
	cmd("trig", { p: V.pat, t: i, s, on: paint.on }, undefined, w);
	refreshRow(i);
	if (!paint.on) paint.locks = true;
}
main.addEventListener("pointerdown", e => {
	const st = e.target.closest("#seq .st"); if (!st || e.button !== 0 || e.shiftKey || e.altKey || e.metaKey || e.ctrlKey || V.rec) return;
	if (!V.loaded) { toast("The pattern is not loaded yet."); return; }
	const i = +st.dataset.t, s = +st.dataset.s;
	paint = { on: !V.tracks[i].trigs[s], done: new Set(), first: i };
	gesture = Bridge.gesture(); grabPointer(main, e); e.preventDefault();
	paintStep(st);
}, true);
main.addEventListener("pointermove", e => {
	if (!paint) return;
	if (e.buttons === 0 && e.pointerType === "mouse") { endPaint(); return; }
	const st = document.elementFromPoint(e.clientX, e.clientY)?.closest("#seq .st"); if (st) paintStep(st);
});
function endPaint() {
	if (!paint) return; const p = paint; paint = null; gesture = 0;
	if (p.locks) renderTop();
	if (p.first !== S.sel) select(p.first); else renderLane();
}
document.addEventListener("pointerup", endPaint); document.addEventListener("pointercancel", endPaint);
window.addEventListener("blur", endPaint);
/* Solo is the page's idea: it mutes every other track on the machine, and un-solo restores the
   mutes the user had set (S.userMutes). */
function applySolo() {
	const any = S.soloSet.size > 0;
	V.tracks.forEach((t, i) => { const want = any ? !S.soloSet.has(i) : S.userMutes.has(i); if (t.mute !== want) setMute(i, want); });
}
document.addEventListener("click", e => {
	const mu = e.target.closest("[data-mute]"), so = e.target.closest("[data-solo]");
	if (mu || so) {
		const i = +(mu || so).dataset[mu ? "mute" : "solo"], t = V.tracks[i];
		if (mu) { t.mute ? S.userMutes.delete(i) : S.userMutes.add(i); setMute(i, !t.mute); }
		else { const next = new Set(S.soloSet); next.has(i) ? next.delete(i) : next.add(i); S.soloSet = next; V = view(); applySolo(); }	/* solo is UI state the view reads, replaced per gesture, never mutated in place */
		refreshAudible(); return;
	}
	const st = e.target.closest("#seq .st"); if (st) {
		const i = +st.dataset.t, s = +st.dataset.s, t = V.tracks[i];
		if (!V.loaded) { toast("The pattern is not loaded yet."); return; }
		/* Live recording: a click plays the track like its TRIG key; the machine records it. */
		if (V.rec) { cmd("recTrig", { t: i }); if (i !== S.sel) select(i); return; }
		if (e.metaKey || e.ctrlKey) { fillEvery(i, s, e.shiftKey ? 4 : 2); return; }
		/* a plain mouse click was the paint gesture's (pointerdown); the keyboard's click toggles here */
		if (!e.shiftKey && !e.altKey && e.detail > 0) return;
		if (e.shiftKey && t.trigs[s]) cmd("accent", { p: V.pat, t: i, s }, undefined, [[["tracks", i, "acc", s], !t.acc.has(s)]]);
		else if (e.altKey && t.trigs[s]) cmd("slide", { p: V.pat, t: i, s }, undefined, [[["tracks", i, "slide", s], !t.slide.has(s)]]);
		else {
			const on = !t.trigs[s], w = [[["tracks", i, "trigs", s], on]];
			if (!on) w.push([["tracks", i, "acc", s], false], [["tracks", i, "slide", s], false], ...clearStep(i, s));
			cmd("trig", { p: V.pat, t: i, s, on }, undefined, w);
			if (!on) renderTop();
		}
		refreshRow(i); if (i !== S.sel) select(i); else renderLane(); return;
	}
	const sel = e.target.closest("[data-sel]"); if (sel && !e.target.closest("button,select,.pc,.fader")) { if (e.shiftKey && S.ws === "seq" && sel.classList.contains("th")) { multiToggle(+sel.dataset.sel); return; } if (S.multi.size) S.multi.clear(); select(+sel.dataset.sel); return; }
	const sg = e.target.closest(".seg[data-set] button"); if (sg) {
		const k = sg.parentElement.dataset.set, v = sg.dataset.v;
		if (k === "upd") { sendLfo(S.sel, "UPDTE", v); sg.parentElement.querySelectorAll("button").forEach(b => b.setAttribute("aria-pressed", b === sg)); redraw(); }
		else if (k === "srcrate" && S.ws === "control") { const id = S.ctl.sel.slice(4); if (Mods.source(id)) { Mods.setSource(id, { rate: v }); sendMods(); render(); } }
		else if (k === "lcurve" && S.ws === "control") { const li = +sg.parentElement.dataset.li; if (Mods.doc.links[li]) { Mods.setLink(li, { curve: v }); sendMods(); render(); } }
		else if (k === "songpick" && S.ws === "song") { S.songPick = v; render(); }
		else if (k === "loopkind" && S.ws === "song") { const r = { ...V.song[S.songSel], type: v }; if (v === "halt") r.to = S.songSel; if (v === "loop") { r.count = r.count || 2; r.to = Math.min(r.to ?? 0, Math.max(0, S.songSel - 1)); } if (v === "jump") r.to = Math.max(r.to ?? 0, S.songSel + 1); rowSet(S.songSel, r); render(); }
		return;
	}
	const ch = e.target.closest("[data-lane]"); if (ch) { S.lane = ch.dataset.lane; render(); return; }
	if (e.target.closest("#clearLane") && e.altKey) { clearTrackLocks(S.sel); return; }
	if (e.target.closest("#clearLane")) { const i = pidx(S.sel, S.lane); if (i >= 0) cmd("clearLane", { p: V.pat, t: S.sel, i }, undefined, [[["locks", lk(S.sel, S.lane)], DELETE]]); renderTop(); refreshRow(S.sel); renderLane(); return; }
	const sh = e.target.closest("[data-shape]"); if (sh) { sendLfo(S.sel, sh.dataset.slot, +sh.dataset.shape); $$(`[data-slot="${sh.dataset.slot}"]`).forEach(b => b.setAttribute("aria-pressed", b === sh)); redraw(); return; }
	const rc = e.target.closest("[data-rc]"); if (rc) {
		const r = +$("#recon").dataset.r, s = +rc.dataset.rc, on = !V.tracks[r].trigs[s];
		cmd("trig", { p: V.pat, t: r, s, on }, undefined, [[["tracks", r, "trigs", s], on], ...(on ? [] : clearStep(r, s))]);
		renderTop(); rc.className = stepCls(r, s); rc.setAttribute("aria-pressed", on); return;
	}
	const cp = e.target.closest("[data-cp]"); if (cp) {
		if (cp.dataset.moved) { cp.dataset.moved = ""; return; } const p = +$("#chop").dataset.p, s = +cp.dataset.cp, t = V.tracks[p];
		if (t.trigs[s] && e.altKey) { const st = V.locks.get(lk(p, "STRT"))?.get(s) ?? t.syn.STRT, en = V.locks.get(lk(p, "END"))?.get(s); if (en != null && en < st) eraseLock(p, "END", s); else setLock(p, "END", s, Math.max(0, st - 8)); }
		else if (t.trigs[s] && e.shiftKey) { const r = V.locks.get(lk(p, "RTRG"))?.get(s); if (r) { eraseLock(p, "RTRG", s); eraseLock(p, "RTIM", s); } else { setLock(p, "RTRG", s, 20); setLock(p, "RTIM", s, 10); } }
		else { const on = !t.trigs[s]; cmd("trig", { p: V.pat, t: p, s, on }, undefined, [[["tracks", p, "trigs", s], on], ...(on ? [] : clearStep(p, s))]); if (on) setLock(p, "STRT", s, t.syn.STRT); }
		renderTop(); cp.className = stepCls(p, s); cp.setAttribute("aria-pressed", t.trigs[s]); cp.innerHTML = chopInner(p, s); redraw(); return;
	}
	const stt = e.target.closest("[data-setupt]"); if (stt) {
		const [r, p] = setupTracks(), d = +stt.dataset.d;
		if (stt.dataset.setupt === "r") { S.smpRec = (r + d + 16) % 16; if (S.smpRec === p) S.smpRec = (S.smpRec + d + 16) % 16; }
		else { S.smpPlay = (p + d + 16) % 16; if (S.smpPlay === r) S.smpPlay = (S.smpPlay + d + 16) % 16; }
		render(); return;
	}
	const sgo = e.target.closest("[data-setupgo]"); if (sgo) {
		const n = sgo.dataset.setupgo, [r, p] = setupTracks(), noTrig = !V.tracks[r].trigs.slice(0, V.len).some(Boolean);
		gesture = Bridge.gesture();	/* one undo step */
		setMachine("RAM-R" + n, r); setMachine("RAM-P" + n, p);
		if (!noTrig && S.smpOnce) cmd("clearSteps", { p: V.pat, t: r, from: 0, to: V.len }, undefined, V.tracks[r].trigs.map((on, s) => on && [["tracks", r, "trigs", s], false]).filter(Boolean));
		if (noTrig || S.smpOnce) cmd("trig", { p: V.pat, t: r, s: 0, on: true }, undefined, [[["tracks", r, "trigs", 0], true]]);
		gesture = 0;
		toast(`Sampling ready: track ${r + 1} records (RAM-R${n}), track ${p + 1} plays (RAM-P${n}). Undo takes it back.`); render(); return;
	}
	const son = e.target.closest("[data-smponce]"); if (son) { S.smpOnce = son.dataset.smponce === "1"; render(); return; }
	const rs = e.target.closest("[data-recsrc]"); if (rs) {
		const t = +rs.dataset.t, src = SOURCES.find(([id]) => id === rs.dataset.recsrc);
		if (src) { Object.entries(src[2]).forEach(([n, v]) => sendParam(t, "syn", n, v)); render(); }
		return;
	}
	const rn = e.target.closest("[data-rename]"); if (rn) {
		const k = +rn.dataset.rename;
		ask(`Name for <b>${romCode(k)}</b> (up to 4 letters, sent with SysEx 0x73):<br><input id="rname" maxlength="4" value="${romName(k)}" style="font:16px var(--mono);width:8ch;margin-top:8px;text-transform:uppercase">`,
			[["Send name", "cream", () => { const v = ($("#rname")?.value || "").toUpperCase(); if (!v) return; SENT_NAMES[k] = v; cmd("sampleName", { slot: k - 1, name: v }); render(); }], ["Cancel", "", () => { }]]);
		setTimeout(() => $("#rname")?.focus(), 0); return;
	}
	if (e.target.closest("#steplegend")) { toggleKeys(true); return; }
	const pgk2 = e.target.closest("#pgkey"); if (pgk2 && !pgk2.disabled) { const n = pages16(); S.viewAll = false; S.page = (S.page + (e.shiftKey ? -1 : 1) + n) % n; render(); return; }
	const plp = e.target.closest(".pl[data-plp]"); if (plp && !plp.classList.contains("na")) { S.page = +plp.dataset.plp; S.viewAll = false; render(); return; }
	if (e.target.closest("#pgall")) { S.viewAll = !S.viewAll; render(); return; }
	if (e.target.closest("#pgfollow")) { S.follow = !S.follow; render(); return; }
	const sk = e.target.closest(".slotk"); if (sk) { S.smpSlot = sk.dataset.slot; render(); return; }
	const sm = e.target.closest("[data-slotmode]"); if (sm) { const n = +S.smpSlot.slice(3), r = recTrack(n); if (r >= 0) { setMute(r, sm.dataset.slotmode === "frozen"); delete S.capture[n]; render(); } return; }
	const cap = e.target.closest("[data-capture]"); if (cap) { const n = +cap.dataset.capture, r = recTrack(n); if (!V.playing) { toast("Press PLAY first. The capture starts at the next loop."); return; } if (r < 0) return; S.capture[n] = "armed"; setMute(r, true); toast("RAM " + n + ": records the next whole loop, then freezes."); render(); return; }
	const ct = e.target.closest("[data-choptrk]"); if (ct) { S.chopTrack = +ct.dataset.choptrk; render(); return; }
	const sl = e.target.closest("[data-smpload]"); if (sl) { if (!sl.disabled) cmd("chooseSample", { slot: +sl.dataset.smpload - 1 }); return; }
	if (e.target.closest("[data-smpstop]")) { cmd("sampleCancel", {}); return; }
	const au = e.target.closest("[data-aud]"); if (au) { if (!au.disabled) toggleAud(au); return; }
	const rt = e.target.closest("[data-romtile]"); if (rt) { S.smpSlot = "ROM" + rt.dataset.romtile; render(); return; }
	const rp = e.target.closest("[data-romput]"); if (rp) { S.keepFx = true; setMachine(romCode(+rp.dataset.romput)); toast("Track " + (S.sel + 1) + " now plays " + romCode(+rp.dataset.romput) + "."); return; }
	if (S.ws === "song") {
		const bk = e.target.closest("[data-bank]"); if (bk) { S.bank = +bk.dataset.bank; render(); return; }
		const cp = e.target.closest("[data-chainpad]"); if (cp) { if (cp.disabled) return; const n = +cp.dataset.chainpad, i = S.chainDraft.indexOf(n); if (i >= 0) S.chainDraft.splice(i, 1); else if (S.chainDraft.length < 16) S.chainDraft.push(n); render(); chainSoon(); return; }
		const ca = e.target.closest("[data-chain]"); if (ca) {
			if (ca.disabled) return;
			if (ca.dataset.chain === "undo") { S.chainDraft.pop(); render(); chainSoon(); }
			else if (ca.dataset.chain === "clear") { clearTimeout(S.chainTimer); S.chainDraft = []; render(); if (chainDoc()?.active || S.chainSent) { cmd("chainClear"); S.chainSent = false; } }
			return;
		}
		if (e.target.closest("[data-rowmore]")) { S.songMore = !S.songMore; render(); return; }
		const ap = e.target.closest("[data-addpat]"); if (ap) { if (V.song.length >= 256) { toast("A song holds 256 rows."); return; } let at = S.songSel + 1; if (V.song[S.songSel]?.type === "end") at = S.songSel; songCmd("rowInsert", { i: at, row: rowToContract({ pat: +ap.dataset.addpat, rep: 1 }, patLen) }); S.songSel = at; return; }
		const rw = e.target.closest(".scell:not(.empty),.db[data-row]"); if (rw) { S.songSel = +rw.dataset.row; render(); return; }
		const ra = e.target.closest("[data-rowact]"); if (ra) { songAction(ra.dataset.rowact); return; }
		const stp = e.target.closest("[data-step]"); if (stp) { songStep(stp.dataset.step, +stp.dataset.d * (e.shiftKey ? 10 : 1)); return; }
		const mk = e.target.closest("[data-rowmute]"); if (mk) { const r = V.song[S.songSel], k = +mk.dataset.rowmute, m = r.mutes || []; rowSet(S.songSel, { ...r, mutes: m.includes(k) ? m.filter(x => x !== k) : [...m, k] }); render(); return; }
		if (e.target.closest("[data-bpmkeep]")) { const r = V.song[S.songSel]; rowSet(S.songSel, { ...r, bpm: r.bpm ? undefined : Math.round(V.bpm) }); render(); return; }
		if (e.target.closest("[data-fullpat]")) { const { ofs, len, ...r } = V.song[S.songSel]; rowSet(S.songSel, r); render(); return; }
		if (e.target.closest("[data-inf]")) { const r = V.song[S.songSel]; rowSet(S.songSel, { ...r, count: r.count === Infinity ? 2 : Infinity }); render(); return; }
	}
	const ok = e.target.closest("[data-out]"); if (ok) { const i = +ok.dataset.out, O = Enums().outputs, out = O[(O.indexOf(V.tracks[i].out || O[0]) + 1) % O.length]; if (!out) return; cmd("route", { t: i, out }, undefined, [[["tracks", i, "out"], out]]); render(); return; }
	const dl = e.target.closest("[data-dlg]"); if (dl) { const d = $("#dlg"), f = d._btns[+dl.dataset.dlg][2]; d.hidden = true; f(); return; }
	if (e.target.closest("[data-romfolder]")) { cmd("revealRomFolder"); return; }
	if (e.target.closest("[data-choose-rom]")) { cmd("chooseRom"); return; }
	if (e.target.closest("[data-recheck]")) { cmd("recheckFirmware"); return; }
	if (e.target.closest("[data-firstclose]")) { const d = $("#dlg"); d.hidden = true; d.dataset.first = ""; return; }
	if ((e.target.closest("[data-dlgclose]") || e.target.id === "dlg") && $("#dlg").dataset.first !== "1") { $("#dlg").hidden = true; $("#dlg").dataset.first = ""; return; }
	if (e.target.closest("#undo")) { cmd("undo"); return; }
	if (e.target.closest("#redo")) { cmd("redo"); return; }
	const sc = e.target.closest("[data-sec]"); if (sc) { if (e.altKey && sc.dataset.sec === "clear") clearPattern(); else secAction(sc.dataset.sec); return; }
	/* #kitf and #pat open the kit library / pattern chooser (mdDeskLibrary.js). */
	if (e.target.closest("[data-reloadsong]")) { cmd("reloadSong"); return; }
	if (e.target.closest("#learnkey")) { toggleLearn(); return; }
	if (S.ws === "control") {
		const C = S.ctl;
		const shh = e.target.closest(".srch.k-cc"); if (shh) { C.sel = shh.dataset.src; C.selT = null; render(); return; }
		const mc = e.target.closest(".mxc[data-mxsrc]"); if (mc) { C.sel = mc.dataset.mxsrc; C.selT = C.addT = +mc.dataset.mxt; render(); return; }
		if (e.target.closest("#caddl")) { const [ch, cc] = C.sel.split(":").map(Number); cmd("learnAdd", Object.assign({ cc, t: +$("#ct").value, i: +$("#cp").value }, ch === 255 ? {} : { ch })); return; }	/* no ch: any channel */
		const kc = e.target.closest("[data-knobcc]"); if (kc) {
			const [, cc] = C.sel.split(":").map(Number), k = KNOB_CCS.indexOf(cc), to = clamp(cc + +kc.dataset.knobcc, 0, 127);
			if (k < 0 || KNOB_CCS.includes(to)) { toast("Another knob row already uses CC " + to + "."); return; }
			KNOB_CCS[k] = to; saveKnobs(); cmd("learnSetCc", { from: cc, to }); C.sel = "255:" + to; render(); return;
		}
		if (e.target.closest("[data-selt]")) { C.selT = null; render(); return; }
		const li = e.target.closest("[data-linv]"); if (li) { cmd("learnInvert", { index: +li.dataset.linv }); return; }
		const as = e.target.closest("[data-addsrc]"); if (as) { const id = Mods.add(as.dataset.addsrc); C.sel = "app:" + id; C.selT = null; sendMods(); render(); return; }
		const ds = e.target.closest("[data-delsrc]"); if (ds) { Mods.remove(ds.dataset.delsrc); C.sel = null; sendMods(); render(); return; }
		const mi = e.target.closest("[data-minv]"); if (mi) { const li = +mi.dataset.minv, l = Mods.doc.links[li]; if (l) { Mods.setLink(li, { invert: !l.invert }); sendMods(); render(); } return; }
		const md = e.target.closest("[data-mdel]"); if (md) { Mods.removeLink(+md.dataset.mdel); sendMods(); render(); return; }
		const ss = e.target.closest("[data-srcshape]"); if (ss) { const id = C.sel.slice(4); if (Mods.source(id)) { Mods.setSource(id, { shape: +ss.dataset.srcshape }); sendMods(); render(); } return; }
		if (e.target.closest("#maddl")) { const s = Mods.source(C.sel.slice(4)), p = +$("#mp").value; if (s && Mods.link(s.id, C.addT, p)) { sendMods(); render(); } else toast("That target is already linked."); return; }
		const asrc = e.target.closest(".srch[data-src^='app:']"); if (asrc) { C.sel = asrc.dataset.src; C.selT = null; render(); return; }
		const ld = e.target.closest("[data-ldel]"); if (ld) { cmd("learnRemove", { index: +ld.dataset.ldel }); return; }
	}
	const tb = e.target.closest("#tabs button"); if (tb) { if (tb.dataset.ws === "control" && !S.mapping) return; S.ws = tb.dataset.ws; render(); return; }
	if (e.target.closest("#platekey")) { setPlate(S.plate === "mk2" ? "mk1" : "mk2"); return; }
	if (e.target.closest("#play")) { cmd(V.playing ? "stop" : "play"); return; }
	if (e.target.closest("#rec")) { cmd("record"); return; }
	if (e.target.closest("#patPrev")) { goPattern((V.queued ?? V.pat) - 1); return; }
	if (e.target.closest("#patNext")) { goPattern((V.queued ?? V.pat) + 1); return; }
});
document.addEventListener("change", e => {
	const id = e.target.id, v = e.target.value, tr = V.tracks[S.sel], l = tr.lfo;
	if (id === "mt" || id === "ct") { S.ctl.addT = +v; if (id === "ct" && S.ctl.selT != null) S.ctl.selT = +v; render(); return; }
	if (id === "lfoT") { const p = params(+v).includes(l.PARAM) ? l.PARAM : params(+v)[0]; sendLfo(S.sel, "TRCK", +v); sendLfo(S.sel, "PARAM", p); renderSound(); enhanceSelects($("#main")); }
	if (id === "lfoP") sendLfo(S.sel, "PARAM", v);
	if (id === "mg") sendGroup(S.sel, "mute", v === "" ? null : +v);
	if (id === "tg") sendGroup(S.sel, "trig", v === "" ? null : +v);
});

function select(i) { S.sel = i; if (!params(i).includes(S.lane)) S.lane = params(i).includes("FLTF") ? "FLTF" : params(i)[0] || "FLTF"; render(); }
function refreshAudible() {
	$$(".th").forEach(h => { const i = +h.dataset.sel; h.classList.toggle("off", !audible(i)); h.querySelector(".m").setAttribute("aria-pressed", V.tracks[i].mute); h.querySelector(".s").setAttribute("aria-pressed", V.tracks[i].solo); });
	$$(".r[data-row],.mr[data-row]").forEach(r => r.classList.toggle("off", !audible(+r.dataset.row)));
	const allOn = $("#allon"); if (allOn) allOn.disabled = !V.tracks.some(t => t.mute || t.solo);
	$$(".strip").forEach(s => { const i = +s.dataset.sel; s.style.opacity = audible(i) ? "" : ".5"; s.querySelector(".m").setAttribute("aria-pressed", V.tracks[i].mute); s.querySelector(".s").setAttribute("aria-pressed", V.tracks[i].solo); });
}

/* BPM: drag up or down, arrows -> global tempo (0x61) */
(() => {
	const b = $("#bpm"); let d = null;
	const set = v => { const bpm = clamp(Math.round(v * 10) / 10, 30, 300); cmd("tempo", { bpm }, "tempo", [[["bpm"], bpm]]); renderTop(); };
	b.addEventListener("pointerdown", e => { d = { y: e.clientY, v: V.bpm }; gesture = Bridge.gesture(); grabPointer(b, e); });
	b.addEventListener("pointermove", e => { if (!d) return; if (e.buttons === 0 && e.pointerType === "mouse") { d = null; gesture = 0; return; } const v = d.v + (d.y - e.clientY) * (e.shiftKey ? .1 : .5); if (Math.abs(v - V.bpm) >= .05) set(v); });
	b.addEventListener("pointerup", () => { d = null; gesture = 0; });
	b.addEventListener("keydown", e => { const k = { ArrowUp: 1, ArrowDown: -1 }[e.key]; if (!k) return; e.preventDefault(); set(V.bpm + k * (e.shiftKey ? .1 : 1)); });
})();

/* Transport: the playhead comes from the machine (telemetry), moved by class only */
let lastStep = -1;
function onTelemetry(m) {
	Docs.telemetry = m;
	/* the transport is derived from the telemetry document (transportOf): a new view when it changed */
	const wasPlaying = V.playing, wasRec = V.rec, tp = transportOf(Docs);
	if (tp.playing !== wasPlaying || tp.rec !== wasRec) { Base = deriveView(Docs, S); V = view(); }
	if (V.rec !== wasRec) { renderTop(); if (V.rec) toast("Live recording: click a track's steps to play it, move a value to lock it."); }
	S.step = V.playing ? m.step : -1;
	if (wasPlaying !== V.playing) { renderTop(); $$(".ph").forEach(c => c.classList.remove("ph")); setPos(); phLast = -1; movePH(); }
	const prev = lastStep; lastStep = S.step;
	if (S.step === prev) return;
	/* Capture next loop (UW): the recorder track plays (records) for one whole loop from the next
	   wrap, then it is muted again, which keeps the take (Freeze). The mutes are the machine's. */
	if (prev >= 0 && S.step >= 0 && S.step < prev) for (const n in S.capture) {
		const r = recTrack(+n);
		if (S.capture[n] === "armed") { S.capture[n] = "rec"; if (r >= 0) setMute(r, false); }
		else { delete S.capture[n]; if (r >= 0) setMute(r, true); toast("RAM " + n + " captured and frozen."); if (S.ws === "sampler") render(); }
	}
	const pp = Math.floor(Math.max(0, S.step) / 16);
	if (S.ws === "mix" && S.step >= 0) V.tracks.forEach((t, i) => { if (t.trigs[S.step] && audible(i)) { const l = document.querySelector(`.act[data-act="${i}"]`); if (l) { l.classList.add("on"); setTimeout(() => l.classList.remove("on"), 90); } } });
	$$(".pl").forEach(b => b.classList.toggle("play", +b.dataset.plp === pp && V.playing));
	if (S.follow && (S.ws === "seq" || S.ws === "sampler") && !S.viewAll && pp !== S.page && !laneDraw && V.playing) { S.page = pp; render(); }
	$("#tempoled").classList.toggle("on", V.playing && S.step % 4 === 0); setPos(); queueMicrotask(movePH); $("#playled")?.classList.toggle("on", V.playing && S.step % 4 === 0);
	$$(`.st[data-s="${prev}"],.lb[data-s="${prev}"],[data-cp="${prev}"],[data-rc="${prev}"]`).forEach(c => c.classList.remove("ph"));
	if (V.playing) $$(`.st[data-s="${S.step}"],.lb[data-s="${S.step}"],[data-cp="${S.step}"],[data-rc="${S.step}"]`).forEach(c => c.classList.add("ph"));
}

/* Soft playhead (mockup v45): one glowing column over the grid that glides from step to step.
   It jumps without animation on a wrap or a re-render and fades out on stop. The step is the
   machine's own (RAM telemetry). */
let phLast = -1;
function stepMs() { const m = multFactor(V.mult); return 60000 / (V.bpm || 120) / 4 / m; }
function movePH() {
	const seq = document.getElementById("seq") || document.querySelector(".smpseq"); if (!seq) return; let ph = document.getElementById("phcol");
	const col = V.playing && S.step >= 0 ? seq.querySelectorAll(`.st[data-s="${S.step}"],[data-rc="${S.step}"],[data-cp="${S.step}"]`) : [], c = col[0];
	if (!c) { if (ph) ph.style.opacity = "0"; phLast = -1; return; }
	let fresh = false; if (!ph) { fresh = true; ph = document.createElement("div"); ph.id = "phcol"; ph.setAttribute("aria-hidden", "true"); seq.appendChild(ph); }
	const last = col[col.length - 1];
	const wrap = fresh || phLast < 0 || c.offsetLeft < phLast;
	ph.style.transition = wrap ? "opacity .15s" : `transform ${Math.round(Math.min(stepMs() * .85, 140))}ms cubic-bezier(.2,.7,.3,1),opacity .15s`;
	ph.style.width = c.offsetWidth + "px"; ph.style.top = (c.offsetTop - 3) + "px"; ph.style.height = (last.offsetTop + last.offsetHeight - c.offsetTop + 6) + "px";
	ph.style.transform = `translateX(${c.offsetLeft}px)`; ph.style.opacity = "1"; phLast = c.offsetLeft;
}

/* POSITION: bar.step of the machine's playhead (a 16-step bar), --.-- when stopped. */
function setPos() { const p = $("#pos"); if (p) p.textContent = V.playing && S.step >= 0 ? String(Math.floor(S.step / 16) + 1).padStart(2, "0") + "." + String(S.step % 16 + 1).padStart(2, "0") : "--.--"; }

/* ===== Documents in: re-derive, then render (in place while a gesture runs) ===== */
let pendingRender = false, renderRaf = 0;
let Base = null;	// the view last derived from the documents (V is it with the overlay)
/* A machine document often changes only the view's status (TX, round trip, undo counts): then the
   view is the same value with the new status (no render); any other change of the derived view renders. */
const STATUS = ["tx", "roundTrip", "canUndo", "canRedo", "undoCount", "redoCount"];
function sameValue(a, b) {
	if (a === b) return true;
	if (!a || !b || typeof a !== "object" || typeof b !== "object") return false;
	if (a instanceof Set || b instanceof Set) return a instanceof Set && b instanceof Set && a.size === b.size && [...a].every(x => b.has(x));
	if (a instanceof Map || b instanceof Map) return a instanceof Map && b instanceof Map && a.size === b.size && [...a].every(([k, x]) => b.has(k) && sameValue(x, b.get(k)));
	if (Array.isArray(a) !== Array.isArray(b)) return false;
	const ka = Object.keys(a), kb = Object.keys(b);
	return ka.length === kb.length && ka.every(k => sameValue(a[k], b[k]));
}
const beyondStatus = v => { const o = { ...v }; for (const k of STATUS) delete o[k]; return o; };
/* an open menu (the machine picker, a key-style dropdown) holds renders too: a document landing would rebuild what it belongs to and close it */
function menuOpen() { return !$("#machpop")?.hidden || !$("#kpop")?.hidden; }
function interacting() { return !!(drag || active || laneDraw || l2drag || chopDrag || drag2 || paint) || menuOpen(); }
function scheduleRender() {
	if (renderRaf) return;
	/* A timer, not an animation frame: documents must land while the window is covered. */
	renderRaf = setTimeout(() => {
		renderRaf = 0;
		Base = deriveView(Docs, S);
		V = view();
		if (interacting()) { pendingRender = true; syncControls(); renderTop(); renderSub(); redraw(); return; }
		pendingRender = false;
		render();
	}, 16);
}
Bridge.onMessage(m => {
	switch (m.type) {
	case "catalogue": setCatalogue(m.doc); scheduleRender(); break;
	case "doc": {
		/* stored where its kind keeps it (mdDeskModel.js, storeDoc); the working kit is the kit that
		   plays, "kit" the stored slots */
		const slot = storeDoc(Docs, m); if (slot == null) break;
		/* Background loads of other patterns only matter to the song palette. */
		const relevant = (m.kind === "pattern" && (slot === currentPatternSlot() || S.ws === "song")) || ((m.kind === "kit" || m.kind === "workingKit") && slot === currentKitSlot()) || m.kind === "global" || (m.kind === "song" && slot === currentSongSlot());
		if (relevant) scheduleRender();
		break;
	}
	case "machine": {
		Docs.machine = m.doc;
		const next = deriveView(Docs, S);
		if (!Base || !sameValue(beyondStatus(Base), beyondStatus(next))) { scheduleRender(); break; }
		Base = next;
		V = Object.assign({}, V, Object.fromEntries(STATUS.map(k => [k, next[k]])));
		$("#undo").disabled = !V.canUndo; $("#redo").disabled = !V.canRedo; syncUndoCounts(); syncTx();
		break;
	}
	case "telemetry": onTelemetry(m); break;
	case "setup": if (m.doc && Array.isArray(m.doc.knobCcs) && m.doc.knobCcs.join() !== KNOB_CCS.join()) { m.doc.knobCcs.forEach((c, i) => KNOB_CCS[i] = c); if (S.ws === "control") scheduleRender(); } break;
	case "mod": {
		/* a modSet is still on its way: keep the pending edit, only its values/CC rate are live */
		if (modInFlight) { Mods.applyLive(m); if (S.ws === "control") syncMods(); break; }
		const before = Mods.doc; Mods.onMessage(m);
		if (S.ws === "control") { if (!sameValue(Mods.doc, before) && !interacting()) scheduleRender(); else syncMods(); }
		break;
	}
	case "ask": onAsk(m); break;
	case "samples": Docs.samples = m.doc; Waves.clear(); WavesAsked.clear(); if (S.ws === "sampler" || S.ws === "sound") scheduleRender(); break;
	case "sampleWave": onSampleWave(m); break;
	case "audition": onAudition(m); break;
	case "sampleLoad": onSampleLoad(m); break;
	case "error": toast(m.message); showLastError([m.message]); break;
	case "learn": Docs.learn = m.doc; applyMapping(m.doc.enabled); if (!S.mapping) break; if (S.ws === "control") scheduleRender(); else syncControls(); if (!m.doc.learning && S.ctl.learnT) { S.ctl.learnT = null; syncControls(); } break;
	}
});

/* The page's first real render, logged so a blank page fails the self-tests. */
let firstRenderLogged = false;
function logFirstRender() {
	if (firstRenderLogged || !Base) return; firstRenderLogged = true;
	const r = document.querySelector(".app").getBoundingClientRect();
	Bridge.log(`first render: ${S.ws}, ${document.querySelectorAll("#main *").length} elements in #main, page ${Math.round(r.width)} x ${Math.round(r.height)}, window ${innerWidth} x ${innerHeight}, ${Math.round(performance.now())} ms`);
}
function render() {
	endStaleRuns(); closePicker(); closeK(); const sl = $("#seqscroll")?.scrollLeft || 0; renderTop();
	const full = S.ws === "mix" || S.ws === "song" || S.ws === "control"; $("#body").classList.toggle("full", full); $("#rail").hidden = full;
	if (!Base) { $("#main").innerHTML = ""; renderSub(); return; }	/* no document yet */
	if (!full) renderRail(); renderSub();
	({ seq: renderSeq, sound: renderSound, mix: renderMix, song: renderSong, sampler: renderSampler, control: renderControl })[S.ws]();
	const sc = $("#seqscroll"); if (sc) { sc.scrollLeft = sl; $("#lanescroll").scrollLeft = sl; } enhanceSelects(document.getElementById("main"));
	markCapabilities(); audKeep(); phLast = -1; movePH(); logFirstRender(); alignLock();
}
/* The rail's LOCK PARAMETER block lines up with the lock lane: its top border with the line above the lane, its first key with the
   top of the bars and its last key with their bottom (the keys' rows share the height the lane has). */
function alignLock() {
	const rp = $("#rail .railparams"), lt = $(".lanetop"), ln = $("#lane"), ch = $("#chips");
	if (!rp || !lt || !ln || !ch || S.ws !== "seq") return;
	const keys = [...ch.querySelectorAll(".pk")], lab = ch.querySelector(".plab"), top = e => e.getBoundingClientRect().top;
	rp.style.removeProperty("margin-top"); ch.style.removeProperty("grid-template-rows"); ch.style.marginTop = ""; ch.style.height = "";
	keys.forEach(k => k.style.removeProperty("height"));
	const lw = lt.closest(".lanewrap"), line = lw ? lw.getBoundingClientRect().top : top(lt);	/* the lane's top border: the line above the lane */
	rp.style.setProperty("margin-top", (parseFloat(getComputedStyle(rp).marginTop) + line - top(rp)) + "px", "important");
	const a = rp.querySelector(".rphead .cap"), b = lt.querySelector(".cap"), mid = e => { const q = e.getBoundingClientRect(); return q.top + q.height / 2; };
	lt.style.marginTop = "0px";	/* the lane's title sits on the LOCK PARAMETER title's line */
	if (a && b) lt.style.marginTop = (mid(a) - mid(b)) + "px";
	const rows = getComputedStyle(ch).gridTemplateRows.split(" ").length, lh = lab ? lab.getBoundingClientRect().height : 0;
	ch.style.setProperty("grid-template-rows", `${lh}px repeat(${Math.max(1, rows - 1)}, minmax(0, 1fr))`, "important");
	keys.forEach(k => k.style.setProperty("height", "auto", "important"));
	if (keys[0]) ch.style.marginTop = (top(ln) - top(keys[0])) + "px";
	ch.style.height = Math.max(0, ln.getBoundingClientRect().bottom - top(ch)) + "px";
	if (keys[0]) ch.style.marginTop = (parseFloat(ch.style.marginTop) + top(ln) - top(keys[0])) + "px";
	ch.style.height = Math.max(0, ln.getBoundingClientRect().bottom - top(ch)) + "px";
}
function setPlate(v) { S.plate = v; document.documentElement.dataset.plate = v; try { localStorage.setItem("mddesk.plate", v); } catch (_) { } renderTop(); redraw(); }
(() => { let v = null; try { v = localStorage.getItem("mddesk.plate"); } catch (_) { } if (!v) v = matchMedia("(prefers-color-scheme: dark)").matches ? "mk2" : "mk1"; S.plate = v; document.documentElement.dataset.plate = v; })();
document.fonts && document.fonts.ready.then(() => { redraw(); alignLock(); });
/* No page selection from a drag (an LCD value, a knob box, a plot, steps, the lock lane): the page is a control
   surface, so nothing selects but the text fields (the CSS has user-select: none on the body, text on the
   fields). selectstart is refused outside them (WebKit, the plug-in's engine, too), and a press outside them
   clears what was selected. */
const textField = n => !!(n && (n.nodeType === 1 ? n : n.parentElement)?.closest?.("input,textarea,select,[contenteditable]:not([contenteditable=false]),.selectable"));
document.addEventListener("selectstart", e => { if (!textField(e.target)) e.preventDefault(); }, true);
document.addEventListener("pointerdown", e => {
	if (textField(e.target)) return;
	const sel = getSelection(); if (sel && sel.rangeCount && !sel.isCollapsed && !textField(sel.anchorNode)) sel.removeAllRanges();
}, true);
document.addEventListener("dragstart", e => { if (!e.target.closest?.("[draggable=true]")) e.preventDefault(); }, true);
/* The editor's keys (mdDeskKeys.js: dispatched from this map, and listed by ?). */
const dlgOpen = () => !$("#dlg").hidden && $("#dlg").dataset.first !== "1";
Keys.bind({ keys: ["Escape"], group: "Anywhere", does: "Close the dialog", when: dlgOpen, field: true, run: () => { $("#dlg").hidden = true; $("#dlg").dataset.first = ""; } });
Keys.bind({ keys: ["Z"], mod: "cmd", group: "Anywhere", does: "Undo", run: () => cmd("undo") });
Keys.bind({ keys: ["Z"], mod: "cmd+shift", group: "Anywhere", does: "Redo", run: () => cmd("redo") });
Keys.bind({ keys: ["Y"], mod: "cmd", group: "Anywhere", does: "Redo", run: () => cmd("redo") });
Keys.bind({ keys: ["C"], mod: "cmd", group: "Anywhere", does: "Copy (track page, sound, song row)", run: () => secAction("copy") });
Keys.bind({ keys: ["V"], mod: "cmd", group: "Anywhere", does: "Paste", run: () => secAction("paste") });
Keys.bind({ keys: ["Escape"], group: "Anywhere", does: "Leave LEARN", mapping: true, when: () => S.mapping && S.ctl.learn, run: () => toggleLearn() });
Keys.bind({ keys: ["Space"], group: "Transport", does: "Play / stop", run: () => cmd(V.playing ? "stop" : "play") });
Keys.bind({ keys: ["Space"], code: "Space", mod: "alt", group: "Transport", does: "Live recording (RECORD + PLAY): Alt + play, the other Alt that is not \"all\"", run: () => cmd("record") });
["seq", "sound", "mix", "sampler", "song", "control"].forEach((ws, i) => Keys.bind({ keys: [String(i + 1)], group: "Workspaces", does: ["Sequence", "Sound", "Mix", "Sampler", "Song", "Control"][i], mapping: ws === "control", when: ws === "control" ? () => S.mapping : null, run: () => { S.ws = ws; render(); } }));
Keys.bind({ keys: ["[", "]"], group: "Sequence", does: "Previous / next page", when: () => (S.ws === "seq" || S.ws === "sampler") && pages16() > 1, run: e => { const n = pages16(); S.viewAll = false; S.page = (S.page + (e.key === "]" ? 1 : -1) + n) % n; render(); } });
Keys.bind({ keys: ["Delete", "Backspace"], group: "Sequence", does: "Clear the selected steps (Song: delete the row)", when: () => S.ws === "song" || S.ws === "seq", run: () => S.ws === "song" ? songAction("del") : secAction("clear") });
Keys.bind({ keys: ["Delete", "Backspace"], mod: "alt", group: "All", does: "Sequence: clear the whole pattern: every track's trigs and locks", when: () => S.ws === "seq", run: () => clearPattern() });
Keys.bind({ keys: ["CLR"], mod: "alt", group: "All", does: "Click: clear the whole pattern, every track's trigs and locks (one undo step)" });
Keys.bind({ keys: ["ArrowLeft", "ArrowRight"], group: "Song", does: "Previous / next row", when: () => S.ws === "song", run: e => { S.songSel = Math.max(0, Math.min(V.song.length - 1, S.songSel + (e.key === "ArrowRight" ? 1 : -1))); render(); } });
Keys.bind({ keys: ["step"], mod: "shift", group: "Sequence", does: "Click: accent" });
Keys.bind({ keys: ["step"], mod: "alt", group: "Sequence", does: "Click: slide" });
Keys.bind({ keys: ["lock lane"], mod: "alt", group: "Sequence", does: "Drag: erase locks" });
Keys.bind({ keys: ["lock lane clear"], mod: "alt", group: "Sequence", does: "Click: clear every lock of the track (all its parameters)" });
Keys.bind({ keys: ["ArrowUp", "ArrowDown"], group: "Values", does: "A focused value, tempo or bar: one step (⇧: fine or ×10)" });
Keys.bind({ keys: ["ArrowLeft", "ArrowRight"], group: "Values", does: "A focused value: one step" });
new ResizeObserver(() => redraw()).observe(document.body);
render();
Bridge.ready();
