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
const FULL = { BD: "Bass drum", B2: "Bass drum 2", SD: "Snare drum", XT: "Tom", MT: "Tom", CP: "Clap", RS: "Rim shot", CB: "Cow bell", CH: "Closed hihat", OH: "Open hihat", CY: "Cymbal", MA: "Maracas", CL: "Claves", XC: "Congas", HH: "Hihat", HT: "High tom", LT: "Low tom", RC: "Ride cymbal", CC: "Crash cymbal", BR: "Brushed snare", TA: "Tambourine", TR: "Triangle", SH: "Shaker", BC: "Bongo conga", ML: "Metallica", SIN: "Sinus", NS: "Noise", IM: "Impulse", EMPTY: "Empty", GA: "Input gate A", GB: "Input gate B", FA: "Filter follower A", FB: "Filter follower B", EA: "Input envelope A", EB: "Input envelope B", AL: "Control all", "8P": "Control 8 parameters", RE: "Control rhythm echo", GB2: "Control gate box", EQ: "Control master EQ", DX: "Control Dynamix" };
function nameOf(m) {
	const f = famKey(m), c = codeOf(m);
	if (f === "MID") return "MIDI channel " + (+c);
	if (f === "ROM") return "ROM sample " + c;
	if (/^R\d/.test(c) && f === "RAM") return "RAM record " + c.slice(1);
	if (/^P\d/.test(c) && f === "RAM") return "RAM play " + c.slice(1);
	if (f === "CTR" && c === "GB") return FULL.GB2;
	return FULL[c] || m;
}
const isSampler = m => /^(ROM|RAM-P)/.test(m), isRec = m => /^RAM-R/.test(m);
const SHAPES = ["Triangle", "Saw", "Square", "Linear decay", "Exp decay", "Random"];

/* ===== State: UI fields here, data fields from deriveView() ===== */
const S = { ws: "seq", sel: 0, lane: "FLTF", page: 0, viewAll: false, follow: false, step: -1, soloSet: new Set(), userMutes: new Set(),
	songSel: 0, bank: 0, songZoom: "fit", smpSlot: "RAM1", chopTrack: null, capture: {}, keepFx: true, plate: "mk1",
	tracks: [], locks: new Map(), song: [{ type: "end" }], len: 16, length: 16, mult: "1X", swing: 50, accAmt: 0, mode: "EXTENDED", bpm: 120, pat: 0, kit: 0,
	kitState: "unknown", kitNames: {}, patKit: [], queued: null, firmware: "booting", loaded: false };
S.ctl = { learn: false, learnT: null, sel: null, selT: null, addT: 1 };

/* Pointer capture can fail for a pointer the browser no longer tracks; the gesture still works. */
function capture(el, e) { try { el.setPointerCapture(e.pointerId); } catch (_) { } }

/* ===== Commands ===== */
let gesture = 0;	// non-zero while a drag runs: one undo step
function cmd(op, args = {}, key) {
	const msg = Object.assign({ op }, args);
	if (gesture) msg.g = gesture;
	Bridge.send(msg, { key, onResult: r => onResult(r) });
	tx();
}
const SELFTEST = /[?&]selftest=(1|p4)/.test(location.search);
function onResult(r) {
	if (SELFTEST && (r.op === "record" || r.op === "recTrig" || r.op === "play" || r.op === "stop" || !r.ok)) Bridge.log("result " + r.op + " ok " + r.ok + " " + (r.errors || []).join(";") + " " + (r.note || ""));
	if (!r.ok && r.errors && r.errors.length) { toast(r.errors[0]); showLastError(r.errors); }
	else if (r.note) toast(r.note);
}
function pidx(t, n) { return slots(S.tracks[t].m).indexOf(n); }

/* ===== Model helpers (view, optimistic) ===== */
function lk(t, p) { return t + ":" + p; }
function setLock(t, p, s, v, quiet) {
	const k = lk(t, p);
	if (!S.locks.has(k)) {
		if (S.locks.size >= 64) { if (!quiet) toast("All 64 locked parameters are in use. Clear one before you lock a new parameter."); return false; }
		S.locks.set(k, new Map());
	}
	S.locks.get(k).set(s, v);
	const i = pidx(t, p);
	if (i >= 0) cmd("lock", { p: S.pat, t, i, s, v }, "lock:" + t + ":" + i + ":" + s);
	return true;
}
function eraseLock(t, p, s) {
	const m = S.locks.get(lk(t, p));
	if (m) { m.delete(s); if (!m.size) S.locks.delete(lk(t, p)); }
	const i = pidx(t, p);
	if (i >= 0) cmd("lock", { p: S.pat, t, i, s, v: null }, "lock:" + t + ":" + i + ":" + s);
}
function stepLocked(t, s) { for (const [k, m] of S.locks) if (+k.split(":")[0] === t && m.has(s)) return true; return false; }
function trackLocks(t) { return [...S.locks.keys()].filter(k => +k.split(":")[0] === t).map(k => k.split(":")[1]); }
function clearStep(t, s) { for (const [k, m] of [...S.locks]) if (+k.split(":")[0] === t) { m.delete(s); if (!m.size) S.locks.delete(k); } }
function params(t) { const p = pages(S.tracks[t].m); return [...names(p.s), ...names(p.e), ...names(p.r)]; }
function grp(t, p) { const x = S.tracks[t]; return p in x.syn ? x.syn : p in x.fx ? x.fx : x.rt; }
function audible(t) { const any = S.tracks.some(x => x.solo); return any ? S.tracks[t].solo : !S.tracks[t].mute; }
function clamp(v, a = 0, b = 127) { return Math.max(a, Math.min(b, v)); }
const $ = q => document.querySelector(q), $$ = q => [...document.querySelectorAll(q)];
let toastT; function toast(m) { const e = $("#toast"); e.textContent = m; e.classList.add("on"); clearTimeout(toastT); toastT = setTimeout(() => e.classList.remove("on"), 2800); }
function showLastError(errors) { const e = $("#errline"); if (!e) return; e.textContent = errors.join(" · "); e.hidden = false; clearTimeout(e._t); e._t = setTimeout(() => { e.hidden = true; }, 6000); }
const kitName = k => "K" + String(k + 1).padStart(2, "0") + " " + (S.kitNames[k] || "KIT " + String(k + 1).padStart(2, "0"));
/* TX LED: lit while the desk has an edit on the wire (machine.desk.tx), and briefly for every command. */
let txT; function tx() { const l = $("#txled"); if (!l) return; l.classList.add("on"); clearTimeout(txT); txT = setTimeout(syncTx, 90); }
function syncTx() { const l = $("#txled"); if (!l) return; l.classList.toggle("on", S.tx); if (S.roundTrip >= 0) l.parentElement.parentElement.title = "Synced with the machine · last round trip " + Math.round(S.roundTrip) + " ms"; }
function setKitState(st) {
	const s = $("#save"); if (!s) return;
	s.classList.toggle("dirty", st === "edited");
	s.lastElementChild.textContent = st === "edited" ? "edited" : st === "clean" ? "saved" : "?";
	s.title = (st === "edited" ? "Kit edits are not saved on the machine. They are kept in the DAW project." : st === "clean" ? "The kit matches its saved slot on the machine." : "Not known yet")
		+ (S.kitSource === "memory" ? " Read from the machine's memory." : "");
}
function syncUndoCounts() { const u = $("#undon"), r = $("#redon"); if (u) u.textContent = S.undoCount || ""; if (r) r.textContent = S.redoCount || ""; }
/* Kit values: the view may be edited by any control; this sends what changed since the last document. */
function syncKitValues() {
	if (!S.loaded) return;
	const base = JSON.parse(S.base);
	S.tracks.forEach((t, i) => {
		const b = base.tracks[i];
		for (const g of ["syn", "fx", "rt"]) for (const n in t[g]) if (t[g][n] !== b[g][n]) {
			const idx = pidx(i, n); if (idx >= 0) cmd("param", { k: S.kit, t: i, i: idx, v: t[g][n] }, "param:" + i + ":" + idx);
		}
		const l = t.lfo, bl = b.lfo;
		if (l.SPD !== bl.SPD) cmd("param", { k: S.kit, t: i, i: 21, v: l.SPD }, "param:" + i + ":21");
		if (l.DEPTH !== bl.DEPTH) cmd("param", { k: S.kit, t: i, i: 22, v: l.DEPTH }, "param:" + i + ":22");
		if (l.SHMIX !== bl.SHMIX) cmd("param", { k: S.kit, t: i, i: 23, v: l.SHMIX }, "param:" + i + ":23");
		if (l.TRCK !== bl.TRCK) cmd("lfo", { k: S.kit, t: i, field: "track", v: l.TRCK });
		if (l.PARAM !== bl.PARAM) { const pi = slots(S.tracks[l.TRCK].m).indexOf(l.PARAM); if (pi >= 0) cmd("lfo", { k: S.kit, t: i, field: "param", v: pi }); }
		if (l.SHP1 !== bl.SHP1) cmd("lfo", { k: S.kit, t: i, field: "shape1", v: l.SHP1 });
		if (l.SHP2 !== bl.SHP2) cmd("lfo", { k: S.kit, t: i, field: "shape2", v: l.SHP2 });
		if (l.UPDTE !== bl.UPDTE) cmd("lfo", { k: S.kit, t: i, field: "update", v: UPDATES.indexOf(l.UPDTE) });
		if (t.muteGroup !== b.mg) cmd("group", { k: S.kit, t: i, kind: "mute", target: t.muteGroup });
		if (t.trigGroup !== b.tg) cmd("group", { k: S.kit, t: i, kind: "trig", target: t.trigGroup });
	});
	for (const id in S.mfx) for (const n in S.mfx[id].v) if (S.mfx[id].v[n] !== base.mfx[id][n]) {
		const i = S.mfx[id].k.indexOf(n); cmd("masterFx", { k: S.kit, fx: MFX[id], i, v: S.mfx[id].v[n] }, "mfx:" + id + ":" + i);
	}
	S.base = snapshotKit(S);
}
function goPattern(p) {
	p = (p + 128) % 128;
	if (p === S.pat && S.queued == null) return;
	cmd("select", { p });
}
function saveKit() { cmd("saveKit"); }
function ask(html, btns) { const d = $("#dlg"); d.innerHTML = `<div class="dlgbox" role="alertdialog" aria-modal="true"><p>${html}</p><div class="btnrow">${btns.map(([t, c], i) => `<button class="${c}" data-dlg="${i}">${t}</button>`).join("")}</div></div>`; d.hidden = false; d._btns = btns; d.querySelector("button")?.focus(); }

/* Value access for every control: data-g group, data-n name, data-t track, data-f master fx */
function ref(el) {
	const d = el.dataset, t = d.t != null ? +d.t : S.sel, tr = S.tracks[t];
	switch (d.g) {
	case "syn": return [tr.syn, d.n]; case "fx": return [tr.fx, d.n]; case "rt": return [tr.rt, d.n]; case "lfo": return [tr.lfo, d.n];
	case "mfx": return [S.mfx[d.f].v, d.n]; case "src": return [Mods.source(d.src), d.n]; case "link": return [Mods.doc.links[+d.li], d.n];
	}
}
const getV = el => { const [o, n] = ref(el); return o[n]; };
function setV(el, v) { const [o, n] = ref(el); v = clamp(Math.round(v), 0, n === "depth" ? 100 : 127); if (o[n] === v) return; o[n] = v; if (el.dataset.g === "src" || el.dataset.g === "link") sendMods(); else syncKitValues(); syncControls(); redraw(); }

/* ===== Top bar ===== */
let lastQueued = null;
function renderTop() {
	$$("#tabs button").forEach(b => b.setAttribute("aria-selected", b.dataset.ws === S.ws));
	const lkk = $("#learnkey"); if (lkk) { lkk.setAttribute("aria-pressed", S.ctl.learn); lkk.classList.toggle("on", S.ctl.learn); }
	const pk = $("#platekey"); if (pk) pk.querySelector("span").textContent = S.plate === "mk2" ? "MKII" : "MKI";
	const n = S.locks.size, m = $("#meter"); $("#lockn").textContent = String(n).padStart(2, "0") + "/64"; m.className = "f meter" + (n >= 64 ? " full" : n >= 52 ? " warn" : "");
	$("#bpm").textContent = (+S.bpm).toFixed(1);
	/* Mockup v49: only the target's name, blinking while it waits for the pattern end; one short
	   flash when the machine really switches (the desk clears the queue at the playhead wrap). */
	$("#pat").textContent = patName(S.queued ?? S.pat);
	$("#pat").parentElement.classList.toggle("queued", S.queued != null);
	if (lastQueued != null && S.queued == null && S.pat === lastQueued) { const pf = $(".patf"); if (pf) { pf.classList.remove("flash"); void pf.offsetWidth; pf.classList.add("flash"); } }
	lastQueued = S.queued;
	$("#kitname").textContent = kitName(S.kit);
	setKitState(S.kitState);
	$("#undo").disabled = !S.canUndo; $("#redo").disabled = !S.canRedo; syncUndoCounts();
	/* One key: PLAY while stopped, STOP while playing (the icon follows the machine). */
	$("#play").setAttribute("aria-pressed", S.playing); $("#playico").textContent = S.playing ? "■" : "▶"; $("#play").setAttribute("aria-label", S.playing ? "Stop" : "Play");
	$("#rec").setAttribute("aria-pressed", !!S.rec); $("#recled").classList.toggle("on", !!S.rec);
	$("#rec").title = S.rec ? "Live recording: click a track's steps to play it, move a value to lock it. REC again: stop recording, keep playing (R)" : "Live recording, as RECORD + PLAY on the machine (R)";
	document.body.classList.toggle("liverec", !!S.rec);
	renderEngine();
	syncTx();
	const st = $("#status");
	if (st) {
		const msg = S.firmware === "booting" || S.firmware === "loading" ? "The machine is starting. Edits wait until it answers." : S.firmware === "unsupported" ? "This firmware is not Machinedrum OS 1.63. MD Desk needs OS 1.63." : !S.loaded ? "Reading the current pattern and kit from the machine…" : "";
		st.textContent = msg; st.hidden = !msg;
	}
	if (S.firmware === "missing") firstRun(); else if ($("#dlg").dataset.first === "1") { $("#dlg").hidden = true; $("#dlg").dataset.first = ""; }
}

/* The engine label in the LCD shows the engine's real state (mockup v48), from the device:
   NO ROM, LOADING ROM (the machine is prepared or restored), BOOTING OS (the firmware starts),
   EMU OS 1.63 (it answers MIDI: status reply seen), ROM ERROR (not OS 1.63). While it is not
   ready the LCD fields dim, REC and PLAY are disabled and edits wait (the desk refuses them).
   HW MIDI is not available yet. */
const ENG = { missing: ["NO ROM", "off"], loading: ["LOADING ROM", "blink"], booting: ["BOOTING OS", "blink"], ready: ["EMU OS 1.63", "on"], unsupported: ["ROM ERROR", "off"] };
let lastEng = "";
function renderEngine() {
	const btn = $(".lcdeng"), led = $("#engled"); if (!btn || !led) return;
	const [txt, mode] = ENG[S.firmware] || ENG.booting, ready = S.firmware === "ready";
	btn.querySelector("span").textContent = txt;
	led.className = "led " + (mode === "on" ? "on" : mode === "blink" ? "on blink" : "");
	$(".lcdpanel").classList.toggle("engwait", !ready);
	["rec", "play"].forEach(id => { const k = document.getElementById(id); if (k) k.disabled = !ready; });
	btn.title = ready ? "Engine: the real Machinedrum OS 1.63 runs inside the app. Editing a real Machinedrum over MIDI is not available yet." : "Engine: " + txt.toLowerCase() + ". Editing starts when it is ready.";
	if (S.firmware !== lastEng) { if (typeof SELFTEST !== "undefined" && SELFTEST) Bridge.log(`engine: ${txt} at ${Math.round(performance.now())} ms`); lastEng = S.firmware; }
}
document.addEventListener("change", e => {
	if (e.target.id !== "engsel") return;
	const sel = e.target, v = sel.value; sel.value = "emu"; renderEngine();
	if (v === "rom") firstRun(true);
	else if (v === "hw") toast("HW MIDI is not available yet. The editor runs the emulated OS 1.63.");
});

/* ===== Track header (one component, used by rail and grid) ===== */
function th(i, extra = "") {
	const t = S.tracks[i]; return `<div class="th ${i === S.sel ? "sel" : ""} ${audible(i) ? "" : "off"} ${extra}" data-sel="${i}" style="--c:${FAMC[t.fam]}">
 <div class="sw"></div><div class="n ${i % 4 === 0 ? "fill" : ""}">${i + 1}</div><div class="nm" title="${t.name}"><b>${t.m}</b></div>
 <button class="ms m" data-mute="${i}" aria-pressed="${t.mute}" aria-label="Mute track ${i + 1}">M</button><button class="ms s" data-solo="${i}" aria-pressed="${t.solo}" aria-label="Solo track ${i + 1}">S</button></div>`;
}
function renderRail() {
	if (S.ws === "sampler") { renderSlots(); return; }
	$("#rail").innerHTML = `<div class="railhead ${S.ws === "seq" ? "tall" : ""}">Track</div>` + S.tracks.map((_, i) => th(i)).join("") + (S.ws === "seq" ? `<div class="railparams"><div class="rphead"><span class="cap">Lock parameter</span><button id="clearLane" class="iconkey" aria-label="Clear ${S.lane} locks" title="Clear ${S.lane} locks"><svg viewBox="0 0 14 14" aria-hidden="true"><path d="M2 4h10M5.5 4V2.5h3V4M3.5 4l.7 8h5.6l.7-8M6 6.5v3.5M8 6.5v3.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg></button></div><div class="pgrid vert" id="chips"></div></div>` : "");
}

/* ===== Sequence ===== */
function pages16() { return S.len / 16; }
function vis() { if (S.viewAll) return [0, S.len]; S.page = Math.min(S.page, pages16() - 1); return [S.page * 16, S.page * 16 + 16]; }
function steps() { const [a, b] = vis(); return Array.from({ length: b - a }, (_, k) => a + k); }
/* Mockup v51: ALL fits every step (minmax 0, a 2 px gap, class "viewall" on html); the page
   boundary is a 2 px shadow in the cell gap, so the lock lane lines up with the steps. */
function cols() { return S.viewAll ? `repeat(${steps().length},minmax(0,1fr))` : `repeat(${steps().length},minmax(18px,1fr))`; }
function stepCls(i, s) {
	const t = S.tracks[i], c = ["st"]; if (s % 4 === 0) c.push("q"); if (s % 16 === 0 && s !== vis()[0]) c.push("gap"); if (s >= S.length) c.push("past");
	if (t.trigs[s]) { c.push("on"); if (t.acc.has(s)) c.push("acc"); if (t.slide.has(s)) c.push("sl"); if (stepLocked(i, s)) c.push("lk"); }
	if (S.playing && s === S.step) c.push("ph"); return c.join(" ");
}
/* The PAGE control sits on the right, above the grid, on the ruler row. */
function pageCtl() { return `<span class="pagectl rh seqpage"><button class="pgkey" id="pgkey" ${pages16() < 2 ? "disabled" : ""} title="Next page. Shift-click = previous. Keys [ and ].">Page</button><span class="pleds" aria-hidden="true">${[0, 1, 2, 3].map(k => `<span class="pl ${k < pages16() ? "" : "na"} ${!S.viewAll && k === S.page ? "cur" : ""}" data-plp="${k}"><i class="led"></i></span>`).join("")}</span><button class="ptog ${S.viewAll ? "on" : ""}" id="pgall" aria-pressed="${S.viewAll}" title="Show all steps"><i class="led"></i>All</button><button class="ptog ${S.follow ? "on" : ""}" id="pgfollow" aria-pressed="${S.follow}" title="Page follows the play position"><i class="led"></i>Fol</button></span>`; }
function renderSeq() {
	document.documentElement.classList.toggle("viewall", !!S.viewAll);
	let h = `<div class="panel ${S.mode === "CLASSIC" ? "classic" : ""}" id="seqp">${pageCtl()}<div class="scroll" id="seqscroll"><div class="seq" id="seq">
  <div class="r" style="grid-template-columns:${cols()}">${steps().map(s => `<div class="rul ${s % 16 === 0 && s !== vis()[0] ? "gap" : ""}">${s % 4 === 0 ? s + 1 : ""}</div>`).join("")}</div>`;
	S.tracks.forEach((t, i) => { h += `<div class="r ${i === S.sel ? "sel" : ""} ${audible(i) ? "" : "off"}" data-row="${i}" style="grid-template-columns:${cols()};--c:${FAMC[t.fam]}">${steps().map(s => `<button class="${stepCls(i, s)}" data-t="${i}" data-s="${s}" aria-label="Track ${i + 1} step ${s + 1}" aria-pressed="${t.trigs[s]}"></button>`).join("")}</div>`; });
	h += `</div></div><div class="lanewrap"><div class="lanetop"><span class="cap">Lock lane · ${S.sel + 1} ${S.tracks[S.sel].name} · <b id="lanename">${S.lane}</b> <span class="lanescale">0–127</span></span>${S.mode === "CLASSIC" ? `<span class="warnline" title="Locks stay in the pattern but do nothing until you switch to EXTENDED.">CLASSIC: locks muted</span>` : ""}<span class="lanehelp" title="Draw across the bars to lock this parameter per step. Alt-drag erases. Hatched steps have no trig, so they cannot hold a lock. Dashed line = kit value.">Draw to lock · alt-drag erases</span>
  <div class="legend"><span><i class="lg on"></i>Trig</span><span><i class="lg on acc"></i>Accent: shift-click${S.accAll ? " (all)" : ""}</span><span><i class="lg on sl"></i>Slide: alt-click${S.slideAll ? " (all)" : ""}</span><span><i class="lg on lk"></i>Has locks</span></div></div>
</div>
  <div class="scroll" id="lanescroll"><div class="lane" id="lane" style="grid-template-columns:${cols()}"></div></div></div>`;
	$("#main").innerHTML = h; renderLane(); syncScroll();
}
function renderLane() {
	const lane = $("#lane"); if (!lane) return;
	const t = S.sel, tr = S.tracks[t], m = S.locks.get(lk(t, S.lane)), g = grp(t, S.lane), base = g[S.lane] ?? 0;
	const cl = $("#clearLane"); if (cl) { cl.title = "Clear " + S.lane + " locks"; cl.setAttribute("aria-label", "Clear " + S.lane + " locks"); }
	const pg = pages(tr.m); $("#chips").innerHTML = [["Synth", pg.s], ["Effects", pg.e], ["Routing", pg.r]].map(([lab, ps]) => {
		return `<span class="plab">${lab}</span>` + ps.map(p => {
			if (!p) return `<span class="pk empty"></span>`; const n = S.locks.get(lk(t, p))?.size || 0;
			return `<button class="pk ${n ? "has" : ""}" data-lane="${p}" aria-pressed="${p === S.lane}" title="${n ? n + " locked step" + (n > 1 ? "s" : "") : "No locks yet"}">${p}${n ? `<i>${n}</i>` : ""}</button>`;
		}).join("");
	}).join("");
	lane.innerHTML = steps().map(s => {
		const on = tr.trigs[s], v = m?.get(s);
		return `<div class="lb ${on ? "" : "none"} ${s % 16 === 0 && s !== vis()[0] ? "gap" : ""} ${S.playing && s === S.step ? "ph" : ""}" data-s="${s}">${on ? `<div class="base" style="--b:${3 + base / 127 * 168}px"></div>${v != null ? `<i style="--h:${v / 127 * 168}px"></i>` : ""}` : ""}</div>`;
	}).join("");
}
function syncScroll() { const a = $("#seqscroll"), b = $("#lanescroll"); if (!a || !b) return; a.onscroll = () => { b.scrollLeft = a.scrollLeft; }; b.onscroll = () => { a.scrollLeft = b.scrollLeft; }; }
function refreshRow(i) { $$(`.st[data-t="${i}"]`).forEach(b => { const s = +b.dataset.s; b.className = stepCls(i, s); b.setAttribute("aria-pressed", S.tracks[i].trigs[s]); }); }

let laneDraw = null;
function laneAt(e) {
	const lane = $("#lane"); if (!lane) return; const el = document.elementFromPoint(e.clientX, e.clientY)?.closest(".lb"); if (!el || !lane.contains(el)) return;
	const s = +el.dataset.s, t = S.sel; if (!S.tracks[t].trigs[s]) return; const r = el.getBoundingClientRect(); const v = clamp(Math.round((r.bottom - 3 - e.clientY) / (r.height - 6) * 127));
	if (laneDraw.erase) eraseLock(t, S.lane, s); else if (!setLock(t, S.lane, s, v)) return;
	const bar = el.querySelector("i"); if (laneDraw.erase) { bar?.remove(); } else if (bar) bar.style.setProperty("--h", v / 127 * 168 + "px"); else el.insertAdjacentHTML("beforeend", `<i style="--h:${v / 127 * 168}px"></i>`);
	laneDraw.touched.add(s); renderTop();
}
function endLaneDraw() { if (!laneDraw) return; laneDraw = null; gesture = 0; refreshRow(S.sel); renderLane(); }

/* ===== Sound ===== */
function pc(g, n, { t, f, color } = {}) {
	if (!n) return `<div class="pc empty" aria-hidden="true"></div>`;
	return `<div class="pc" role="slider" tabindex="0" aria-label="${n}" aria-valuemin="0" aria-valuemax="127" data-g="${g}" data-n="${n}"${t != null ? ` data-t="${t}"` : ""}${f ? ` data-f="${f}"` : ""}${color ? ` style="--pc:${color}"` : ""}><span>${n}</span><b></b></div>`;
}
function eight(g, list, o) { const a = [...list]; while (a.length < 8) a.push(null); return a.map(n => pc(g, n, o)).join(""); }
function shapeIcon(i, inv) { const pts = Array.from({ length: 25 }, (_, k) => { const x = k / 24; return [(x * 26 + 1).toFixed(1), (11 - 7 * shape(i, x, inv)).toFixed(1)]; }); return `<svg width="28" height="22" viewBox="0 0 28 22" aria-hidden="true"><polyline fill="none" stroke="currentColor" stroke-width="1.6" points="${pts.map(p => p.join(",")).join(" ")}"/></svg>`; }
const RND = [.35, -.7, .9, -.25, .55, -.9, .1, .7];
function shape(i, x, inv) { const v = [1 - 4 * Math.abs(x - .5), 2 * x - 1, x < .5 ? 1 : -1, 1 - 2 * x, 2 * Math.exp(-4 * x) - 1, RND[Math.floor(x * 8) % 8]][i] ?? 0; return inv ? -v : v; }
const isFxPage = e => e.join() === FX.join();
function machButton(tr) {
	const fk = famKey(tr.m), fam = FAMS.find(x => x[0] === fk) || ["", ""];
	return `<button class="machbtn" id="machbtn" aria-haspopup="dialog" aria-expanded="false" aria-label="Change machine"><span class="lcdtxt">${tr.m}</span><span class="mfam">${fam[0].replace("PI", "P-I")} · ${fam[1]}</span><svg viewBox="0 0 10 6" aria-hidden="true"><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5"/></svg></button>`;
}
function renderSound() {
	const t = S.sel, tr = S.tracks[t], l = tr.lfo, c = FAMC[tr.fam], pg = pages(tr.m), en = names(pg.e), rn = names(pg.r);
	const synthScreen = isSampler(tr.m) ? `<canvas class="ed" data-ed="sample" aria-label="Sample with start and end markers. Drag the markers."></canvas><div class="edhint">Drag STRT and END. Ticks = STRT locks per step (the chops). END left of STRT = reverse. The waveform is an example (MOCK).</div>`
		: isRec(tr.m) ? `<canvas class="ed" data-ed="rec" aria-label="Recording window. Drag LEN."></canvas><div class="edhint">Drag LEN to set the recording length (127 = 2 bars). The waveform is modelled from the pattern (MOCK).</div>`
			: "DEC" in tr.syn || "RAMP" in tr.syn ? `<canvas class="ed" data-ed="synth" aria-label="Amp decay and pitch ramp. Drag the dots."></canvas><div class="edhint">${"RAMP" in tr.syn ? "Solid = amp decay (DEC). Dashed = pitch ramp (RAMP, RDEC)." : "Curve = amp decay (DEC)."}</div>`
				: `<div class="edblank">${about(tr.m) || "No curve for this machine."}</div>`;
	$("#main").innerHTML = `<div class="soundgrid">
  <section class="card"><header><h3>Synthesis</h3>${machButton(tr)}</header>
   ${synthScreen}<div class="ctl four">${eight("syn", pg.s, { color: c })}</div></section>
  ${en.length ? `<section class="card"><header><h3>Effects</h3><span>${isFxPage(pg.e) ? "Track effects page" : "CC page"}</span></header>
   ${isFxPage(pg.e) ? `<canvas class="ed" data-ed="fx" aria-label="Filter and EQ response. Drag the dots."></canvas><div class="edhint">Left dot = FLTF (drag up for FLTQ). Right dot = FLTW. Middle dot = EQF and EQG.</div>` : `<div class="edblank">MIDI CC pages: each pair picks a CC number (D) and sends its value (V).</div>`}
   <div class="ctl four">${eight("fx", pg.e, { color: "var(--e12)" })}</div></section>` : `<section class="card"><header><h3>Effects</h3><span>none</span></header><div class="edblank">This control machine has no track effects or routing. It drives the master effect directly.</div></section>`}
  <section class="card"><header><h3>Routing</h3><span>Level, pan and sends</span></header>
   ${"PAN" in tr.rt ? `<canvas class="ed" data-ed="route" aria-label="Distortion curve and stereo position. Drag the dots."></canvas><div class="edhint">Curve = DIST drive. Dot = PAN (sideways) and VOL (up and down).</div>` : `<div class="edblank">No audio routing for this machine.</div>`}
   ${rn.length ? `<div class="ctl four">${eight("rt", pg.r, { color: "var(--gnd)" })}</div>` : ""}</section>
  <section class="card lfocard"><header><h3>LFO ${t + 1}</h3><span>Speed in 1/128 notes · 16 LFOs per kit</span></header>
   <div class="lfoin"><canvas class="ed" data-ed="lfo" aria-label="LFO waveform"></canvas>
    <div class="lfoctl">
     <div class="kv"><span class="mono" style="width:52px">TARGET</span><select id="lfoT">${S.tracks.map((x, i) => `<option value="${i}" ${i === l.TRCK ? "selected" : ""}>TRCK ${i + 1} ${x.name}</option>`).join("")}</select>
      <select id="lfoP">${params(l.TRCK).map(p => `<option ${p === l.PARAM ? "selected" : ""}>${p}</option>`).join("")}</select></div>
     ${["SHP1", "SHP2"].map(sl => `<div class="kv"><span class="mono" style="width:52px">${sl}</span><div class="shapes">${SHAPES.map((n, i) => `<button data-slot="${sl}" data-shape="${i}" aria-pressed="${l[sl] === i}" title="${n}${sl === "SHP2" ? ", inverted" : ""}" aria-label="${sl} ${n}">${shapeIcon(i, sl === "SHP2")}</button>`).join("")}</div></div>`).join("")}
     <div class="kv"><span class="mono" style="width:52px">UPDTE</span><span class="seg" data-set="upd">${["FREE", "TRIG", "HOLD"].map(u => `<button data-v="${u}" aria-pressed="${l.UPDTE === u}">${u}</button>`).join("")}</span></div>
     <div class="ctl" style="grid-template-columns:repeat(3,minmax(0,1fr))">${["SPD", "DEPTH", "SHMIX"].map(n => pc("lfo", n, { color: "var(--teal)" })).join("")}</div></div></div></section>
  <section class="card"><header><h3>Relations</h3><span>Kit · EDIT KIT → RELATE</span></header>
   <div class="two" style="grid-template-columns:1fr">
    <label>Mute group<select id="mg"><option value="">none</option>${S.tracks.map((x, i) => i !== t ? `<option value="${i}" ${tr.muteGroup === i ? "selected" : ""}>mutes ${i + 1} ${x.name}</option>` : "").join("")}</select></label>
    <label>Trig group<select id="tg"><option value="">none</option>${S.tracks.map((x, i) => i !== t ? `<option value="${i}" ${tr.trigGroup === i ? "selected" : ""}>trigs ${i + 1} ${x.name}</option>` : "").join("")}</select></label></div>
   <div class="note">Mute groups interleave sounds, like open and closed hihats. Trig groups layer two tracks from one trig. Trig relations do not chain.</div></section>
 </div>`;
	syncControls(); redraw();
}

/* ===== Mix ===== */
function renderMix() {
	$("#main").innerHTML = `<div class="panel pad">
 <div class="scroll"><div class="strips">${S.tracks.map((t, i) => {
		const out = t.out || "MAIN", direct = out !== "MAIN"; return `<div class="strip ${i === S.sel ? "sel" : ""} ${direct ? "direct" : ""}" data-sel="${i}" style="${audible(i) ? "" : "opacity:.5"}">
  <div class="top2"><i class="led act" data-act="${i}"></i><span>${i + 1}</span></div>
  ${"VOL" in t.rt ? `<div class="fader" role="slider" tabindex="0" aria-label="Track ${i + 1} volume" data-g="rt" data-n="VOL" data-t="${i}"><div class="tr"><i></i></div><div class="cap2"></div></div>` : `<div class="fader"></div>`}
  <div class="v" data-show="${i}"></div>
  ${["PAN", "DIST", "DEL", "REV"].map(n => n in t.rt ? pc("rt", n, { t: i }) : `<div class="pc empty" aria-hidden="true"></div>`).join("")}
  <button class="outk ${direct ? "on" : ""}" data-out="${i}" title="${direct ? "Individual output " + out + ": skips the master effects" : "Main output, through the master effects"}">OUT ${out}</button>
  <div class="mrow"><button class="ms m" data-mute="${i}" aria-pressed="${t.mute}" aria-label="Mute track ${i + 1}">M</button><button class="ms s" data-solo="${i}" aria-pressed="${t.solo}" aria-label="Solo track ${i + 1}">S</button></div>
  <div class="nm" title="${t.name}">${t.m}</div></div>`;
	}).join("")}</div></div></div>
 <div class="flow" aria-label="Signal path">SENDS <i>→</i> RHYTHM ECHO <i>→</i> GATE BOX <i>→</i> MASTER EQ <i>→</i> DYNAMIX <i>→</i> MAIN OUT <span>tracks on A–F skip this chain</span></div>
 <div class="fx4">${Object.entries(S.mfx).map(([id, f]) => `<section class="card"><header><h3>${f.name}</h3><span>${{ echo: "DEL send", gate: "REV send + DVOL", eq: "main out", dyn: "main out" }[id]}</span></header>
  <canvas class="ed sm" data-ed="${id}" aria-label="${f.name} screen. Drag the dots."></canvas>
  <div class="ctl four">${f.k.map(n => pc("mfx", n, { f: id })).join("")}</div></section>`).join("")}</div>`;
	syncControls(); redraw();
}

/* ===== Song ===== */
function patLen(p) { return lengthOfPattern(p); }
function hasPat(p) { const d = Docs.patterns[p]; return !!d && d.tracks.some(t => t.trigs.length); }
const rowLen = r => r.len ?? (patLen(r.pat) - (r.ofs || 0));
function songSteps() { let n = 0; S.song.forEach(r => { if (!r.type) n += rowLen(r) * r.rep; }); S.song.forEach((r, i) => { if (r.type === "loop" && r.count !== Infinity) { let seg = 0; for (let k = r.to; k < i; k++) { const q = S.song[k]; if (!q.type) seg += rowLen(q) * q.rep; } n += seg * (r.count - 1); } }); return n; }
function songTime() { const st = songSteps(), sec = st * 60 / S.bpm / 4; return `${Math.floor(sec / 60)}:${String(Math.round(sec % 60)).padStart(2, "0")}`; }
function loopOf(i) { return S.song.findIndex((r, k) => r.type === "loop" && k > i && r.to <= i); }
function songCmd(op, args) { cmd(op, Object.assign({ s: S.songSlot }, args)); }
function rowSet(i) { songCmd("rowSet", { i, row: rowToContract(S.song[i], patLen) }); }
function renderSong() {
	const sel = S.song[S.songSel] || S.song[0];
	const palette = `<div class="banks">${[..."ABCDEFGH"].map((b, k) => `<button class="bank ${k === S.bank ? "on" : ""}" data-bank="${k}"><i class="led"></i>${b}</button>`).join("")}</div>
  <div class="pgridp">${Array.from({ length: 16 }, (_, k) => { const p = S.bank * 16 + k; return `<button class="padd ${hasPat(p) ? "has" : ""} ${!sel.type && sel.pat === p ? "cur" : ""}" data-addpat="${p}" draggable="true" title="Drag into the arrangement. Click adds after the selected row.">${patName(p)}<small>${Docs.patterns[p] ? (hasPat(p) ? patLen(p) : "empty") : "…"}</small></button>`; }).join("")}</div>`;
	let insp = "";
	if (!sel.type) {
		const L = patLen(sel.pat), o = sel.ofs || 0, ln = rowLen(sel);
		insp = `<div class="irow"><span class="ilab">Row</span><span class="lcdchip">${String(S.songSel + 1).padStart(3, "0")} · ${patName(sel.pat)}</span>
    <span class="stepper"><button data-step="pat" data-d="-1" aria-label="Previous pattern">‹</button><button data-step="pat" data-d="1" aria-label="Next pattern">›</button></span></div>
   <div class="irow"><span class="ilab">Repeat</span><span class="stepper"><button data-step="rep" data-d="-1">−</button><b class="mono">${sel.rep}</b><button data-step="rep" data-d="1">+</button></span>
    <span class="ilab" style="margin-left:18px">Tempo</span><button class="ptog ${sel.bpm ? "" : "on"}" data-bpmkeep="1"><i class="led"></i>Keep</button>
    ${sel.bpm ? `<span class="stepper"><button data-step="bpm" data-d="-1">−</button><b class="mono">${sel.bpm}</b><button data-step="bpm" data-d="1">+</button></span>` : `<span class="note">uses the tempo before it</span>`}</div>
   <div class="irow"><span class="ilab">Part</span><div class="partbar" style="grid-template-columns:repeat(${L},1fr)">${Array.from({ length: L }, (_, k) => `<i class="${k >= o && k < o + ln ? "on" : ""} ${k % 16 === 0 && k ? "pg" : ""}"></i>`).join("")}</div></div>
   <div class="irow"><span class="ilab"></span><span class="stepper"><span class="ilab">Start</span><button data-step="ofs" data-d="-1">−</button><b class="mono">${o + 1}</b><button data-step="ofs" data-d="1">+</button></span>
    <span class="stepper"><span class="ilab">Length</span><button data-step="len" data-d="-1">−</button><b class="mono">${ln}</b><button data-step="len" data-d="1">+</button></span>
    <button class="ptog ${sel.ofs || sel.len ? "" : "on"}" data-fullpat="1"><i class="led"></i>Whole pattern</button></div>
   <div class="irow"><span class="ilab">Mutes</span><div class="mkeys">${Array.from({ length: 16 }, (_, k) => `<button class="mkey ${(sel.mutes || []).includes(k) ? "off" : ""}" data-rowmute="${k}" title="Track ${k + 1} ${S.tracks[k].m}">${k + 1}</button>`).join("")}</div></div>`;
	}
	else if (sel.type === "end") insp = `<div class="irow"><span class="ilab">End</span><span class="note">The song stops here. Add patterns before it from the palette.</span></div>`;
	else insp = `<div class="irow"><span class="ilab">Command</span><span class="seg" data-set="loopkind">${["loop", "jump", "halt"].map(k => `<button data-v="${k}" aria-pressed="${sel.type === k}">${k.toUpperCase()}</button>`).join("")}</span></div>
   ${sel.type !== "halt" ? `<div class="irow"><span class="ilab">${sel.type === "loop" ? "Back to" : "Jump to"}</span><span class="stepper"><button data-step="to" data-d="-1">−</button><b class="mono">${String(sel.to + 1).padStart(3, "0")}</b><button data-step="to" data-d="1">+</button></span></div>` : ""}
   ${sel.type === "loop" ? `<div class="irow"><span class="ilab">Times</span><span class="stepper"><button data-step="count" data-d="-1">−</button><b class="mono">${sel.count === Infinity ? "∞" : sel.count}</b><button data-step="count" data-d="1">+</button></span><button class="ptog ${sel.count === Infinity ? "on" : ""}" data-inf="1"><i class="led"></i>Forever</button></div>` : ""}
   <div class="irow"><span class="ilab"></span><span class="note">${sel.type === "loop" ? "Loops can be nested. Forever loops are good live: pick the next row while it plays." : sel.type === "jump" ? "Jumps the song pointer to another row." : "Pauses playback until you pick a row to go on from."}</span></div>`;
	$("#main").innerHTML = `<div class="songui lay2"><div class="songleft"><section class="card"><header><h3>Patterns</h3><span>drag onto the grid · click = add after row ${String(S.songSel + 1).padStart(3, "0")}</span></header>${palette}</section>
   <section class="card"><header><h3>Selected row</h3><span class="rowacts"><button data-rowact="up" title="Move left">←</button><button data-rowact="down" title="Move right">→</button><button data-rowact="dup">Duplicate</button><button data-rowact="loop">Add loop</button><button data-rowact="del" class="danger">Delete</button></span></header><div class="insp">${insp}</div></section></div>
  <section class="card"><header><h3>Arrangement</h3>${S.songReload ? `<span class="songwarn"><span class="lcdchip warnchip">Edits heard after STOP + reload</span><button class="cream" data-reloadsong="1">Reload song</button></span>` : ""}<span class="note">${S.song.length} of 256 rows · drag patterns onto the grid · drag cells to move · Delete removes</span></header>
    <div class="durbar" title="Song shape by time (length × repeats)">${S.song.map((r, i) => r.type ? `<i class="db dbm"></i>` : `<i class="db ${i === S.songSel ? "sel" : ""}" data-row="${i}" style="flex:${rowLen(r) * r.rep} 1 0"></i>`).join("")}</div>
    <div class="slotgrid" id="tl">${Array.from({ length: 16 }, (_, line) => `<span class="sglab">${String(line * 16 + 1).padStart(3, "0")}</span>${Array.from({ length: 16 }, (_, c) => {
		const i = line * 16 + c, r = S.song[i];
		if (!r) return `<div class="scell empty" data-i="${i}"></div>`;
		const cls = `scell ${i === S.songSel ? "sel" : ""} ${r.type ? "cmd " + r.type : ""} ${!r.type && loopOf(i) >= 0 ? "inloop" : ""}`;
		const txt = r.type === "end" ? "END" : r.type === "loop" ? `↺${String(r.to + 1).padStart(3, "0")}` : r.type === "jump" ? `→${String(r.to + 1).padStart(3, "0")}` : r.type === "halt" ? "HALT" : patName(r.pat);
		const sub = r.type === "loop" ? (r.count === Infinity ? "∞" : "×" + r.count) : !r.type ? `${r.rep > 1 ? "×" + r.rep : ""}${r.ofs || r.len ? "~" : ""}` : "";
		return `<button class="${cls}" data-row="${i}" data-i="${i}" draggable="${r.type === "end" ? "false" : "true"}" title="Row ${String(i + 1).padStart(3, "0")}${r.type ? "" : " · " + patName(r.pat) + " ×" + r.rep + " · " + rowLen(r) + " steps"}"><b>${txt}</b><small>${sub}</small></button>`;
	}).join("")}`).join("")}</div></section></div>`;
}
function songAction(a) {
	const i = S.songSel, r = S.song[i];
	if (a === "del") { if (r.type === "end") return; songCmd("rowDelete", { i }); S.songSel = Math.max(0, Math.min(i, S.song.length - 2)); }
	if (a === "dup" && !r.type) { songCmd("rowInsert", { i: i + 1, row: rowToContract(r, patLen) }); S.songSel = i + 1; }
	if (a === "up" && i > 0 && r.type !== "end") { songCmd("rowMove", { from: i, to: i - 1 }); S.songSel = i - 1; }
	if (a === "down" && i < S.song.length - 2 && r.type !== "end") { songCmd("rowMove", { from: i, to: i + 1 }); S.songSel = i + 1; }
	if (a === "loop") { const at = r.type === "end" ? i : i + 1; songCmd("rowInsert", { i: at, row: { kind: "loop", target: Math.max(0, at - 1), repeats: 1 } }); S.songSel = at; }
}
function songStep(k, d) {
	const r = S.song[S.songSel];
	if (k === "pat") r.pat = (r.pat + d + 128) % 128;
	if (k === "rep") r.rep = Math.max(1, Math.min(64, r.rep + d));
	if (k === "bpm") r.bpm = Math.max(30, Math.min(300, (r.bpm || Math.round(S.bpm)) + d));
	if (k === "ofs") { const L = patLen(r.pat); r.ofs = Math.max(0, Math.min(L - 1, (r.ofs || 0) + d)); if (r.len == null) r.len = L - r.ofs; if (r.ofs + r.len > L) r.len = L - r.ofs; }
	if (k === "len") { const L = patLen(r.pat); r.len = Math.max(1, Math.min(L - (r.ofs || 0), rowLen(r) + d)); }
	if (k === "to") r.to = Math.max(r.type === "jump" ? S.songSel + 1 : 0, Math.min(r.type === "loop" ? S.songSel - 1 : S.song.length - 1, r.to + d));
	/* The firmware plays a loop repeats + 1 times and 0 is infinite, so a finite loop plays at least twice. */
	if (k === "count") r.count = r.count === Infinity ? (d < 0 ? 64 : Infinity) : Math.max(2, Math.min(64, r.count + d));
	rowSet(S.songSel); render();
}
let drag2 = null;
function dropTarget(el) { const c = el?.closest?.(".scell"); if (!c) return null; const i = +c.dataset.i, endI = S.song.length - 1; return i < endI ? { i, mode: "onto" } : { i: endI, mode: "append" }; }
function showTarget(t) {
	$$(".scell.over,.scell.appendto").forEach(x => x.classList.remove("over", "appendto")); if (!t) return;
	const c = document.querySelector(`.scell[data-i="${t.mode === "append" ? S.song.length : t.i}"]`); c && c.classList.add(t.mode === "append" ? "appendto" : "over");
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
		if (S.song.length >= 256) { toast("A song holds 256 rows."); }
		else if (t.mode === "onto" && !S.song[t.i].type) { S.song[t.i].pat = drag2.v; rowSet(t.i); S.songSel = t.i; }
		else { const at = t.mode === "onto" ? t.i : S.song.length - 1; songCmd("rowInsert", { i: at, row: rowToContract({ pat: drag2.v, rep: 1 }, patLen) }); S.songSel = at; }
	}
	else { const from = drag2.v; let to = t.mode === "onto" ? t.i : S.song.length - 2; if (from !== to && S.song[from].type !== "end") { songCmd("rowMove", { from, to }); S.songSel = to; } }
	drag2 = null; render();
});
document.addEventListener("dragend", () => { drag2 = null; showTarget(null); $$(".dragging").forEach(x => x.classList.remove("dragging")); });

/* ===== Controls: sync in place, never rebuild while dragging ===== */
function syncControls() {
	$$("#main [data-g]").forEach(el => {
		const v = getV(el); if (v == null) { el.style.setProperty("--f", "0%"); const b0 = el.querySelector("b"); if (b0) b0.textContent = "—"; return; }
		const f = v / 127 * 100 + "%"; el.style.setProperty("--f", f); el.setAttribute("aria-valuenow", v);
		if (el.classList.contains("pc") && SIGNED.has(el.dataset.n)) { const q = v / 127 * 100; el.classList.add("bip"); el.style.setProperty("--pl", Math.min(q, 50.4) + "%"); el.style.setProperty("--pw", Math.max(1.5, Math.abs(q - 50.4)) + "%"); }
		const b = el.querySelector("b"); if (b) b.textContent = SIGNED.has(el.dataset.n) && el.dataset.g !== "mfx" ? (v - 64 > 0 ? "+" : "") + (v - 64) : v;
		if (["syn", "fx", "rt"].includes(el.dataset.g)) {
			const tt = el.dataset.t != null ? +el.dataset.t : S.sel, idx = pidx(tt, el.dataset.n);
			const mp = (Docs.learn?.mappings || []).filter(m => m.t === tt && m.i === idx);
			el.classList.toggle("mapped", mp.length > 0);
			el.classList.toggle("learnt", !!S.ctl.learnT && S.ctl.learnT.t === tt && S.ctl.learnT.p === el.dataset.n);
			if (mp.length) el.title = "Mapped: " + mp.map(m => "CC " + m.cc).join(", ");
		}
		if (el.classList.contains("pc") && el.dataset.g !== "mfx" && el.dataset.g !== "lfo" && el.dataset.g !== "src" && el.dataset.g !== "link") el.classList.toggle("lk", S.locks.has(lk(el.dataset.t != null ? +el.dataset.t : S.sel, el.dataset.n)));
	});
	$$("#main [data-show]").forEach(el => el.textContent = S.tracks[+el.dataset.show].rt.VOL ?? "—");
}

/* ===== Curve editors (the mockup's; they edit the view, syncKitValues sends) ===== */
const cssv = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const ED = {
	synth: {
		draw(g, W, H) {
			const tr = S.tracks[S.sel], s = tr.syn, Y = v => H - 12 - v * (H - 30); grid(g, W, H);
			const k = .03 + (s.DEC ?? 64) / 127 * .35; line(g, W, x => { const t = x / W; return Y(t < .01 ? t / .01 : Math.exp(-(t - .01) / k)); }, cssv("--ink"), 2.2);
			if ("RAMP" in s) { const r = s.RAMP / 127, rd = .01 + (s.RDEC ?? 0) / 127 * .3; line(g, W, x => Y(.25 + r * .65 * Math.exp(-(x / W) / rd)), cssv("--ink"), 1.5, [5, 4]); }
			label(g, "amp" + ("RAMP" in s ? " + pitch" : ""));
		},
		handles(W, H) {
			const s = S.tracks[S.sel].syn, Y = v => H - 12 - v * (H - 30), out = []; const k = .03 + (s.DEC ?? 64) / 127 * .35, td = .01 + k * Math.log(4);
			if ("DEC" in s) out.push({ x: td * W, y: Y(.25), k: "DEC", c: cssv("--ink"), drag: (x) => { s.DEC = clamp(Math.round(((x / W - .01) / Math.log(4) - .03) / .35 * 127)); } });
			if ("RAMP" in s) {
				const r = s.RAMP / 127, rd = .01 + (s.RDEC ?? 0) / 127 * .3;
				out.push({ x: 6, y: Y(.25 + r * .65), k: "RAMP", c: cssv("--ink"), drag: (x, y) => { s.RAMP = clamp(Math.round(((H - 12 - y) / (H - 30) - .25) / .65 * 127)); } });
				if ("RDEC" in s) out.push({ x: rd * W, y: Y(.25 + r * .65 / Math.E), k: "RDEC", c: cssv("--ink"), drag: (x) => { s.RDEC = clamp(Math.round((x / W - .01) / .3 * 127)); } });
			}
			return out;
		}
	},
	fx: {
		resp(u) {
			const f = S.tracks[S.sel].fx, hp = f.FLTF / 127, lp = Math.min(1, hp + f.FLTW / 127), q = f.FLTQ / 127; let d = 0;
			if (u < hp) d -= Math.pow((hp - u) * 7, 2); if (u > lp) d -= Math.pow((u - lp) * 7, 2);
			d += q * 1.6 * (Math.exp(-Math.pow((u - hp) * 28, 2)) * (hp > .01 ? 1 : 0) + Math.exp(-Math.pow((u - lp) * 28, 2)) * (lp < .99 ? 1 : 0)); d += (f.EQG - 64) / 64 * .9 * Math.exp(-Math.pow((u - f.EQF / 127) * 9, 2)); return d;
		},
		draw(g, W, H) { grid(g, W, H); line(g, W, x => clamp(H / 2 - this.resp(x / W) * (H / 4), 6, H - 6), cssv("--ink"), 2.2); label(g, "filter + EQ"); },
		handles(W, H) {
			const f = S.tracks[S.sel].fx, hp = f.FLTF / 127, lp = Math.min(1, hp + f.FLTW / 127), yy = u => clamp(H / 2 - this.resp(u) * (H / 4), 6, H - 6);
			return [{ x: Math.max(6, hp * W), y: yy(hp), k: "FLTF", c: cssv("--ink"), drag: (x, y) => { f.FLTF = clamp(Math.round(x / W * 127)); f.FLTQ = clamp(Math.round((H / 2 - y) / (H / 2) * 127)); } },
			{ x: Math.min(W - 6, lp * W), y: yy(lp), k: "FLTW", c: cssv("--ink"), drag: (x) => { f.FLTW = clamp(Math.round((x / W - f.FLTF / 127) * 127)); } },
			{ x: f.EQF / 127 * W, y: yy(f.EQF / 127), k: "EQ", c: cssv("--ink"), drag: (x, y) => { f.EQF = clamp(Math.round(x / W * 127)); f.EQG = clamp(Math.round(64 + (H / 2 - y) / (H / 4) / .9 * 64)); } }];
		}
	},
	lfo: {
		draw(g, W, H) {
			const l = S.tracks[S.sel].lfo, mix = l.SHMIX / 127, cyc = 1 + Math.round((127 - l.SPD) / 40), dep = .2 + .8 * l.DEPTH / 127; grid(g, W, H);
			line(g, W, x => { const p = (x / W * cyc) % 1; return H / 2 - ((1 - mix) * shape(l.SHP1, p, false) + mix * shape(l.SHP2, p, true)) * (H / 2 - 10) * dep; }, cssv("--ink"), 2.2);
			label(g, `${SHAPES[l.SHP1] || "?"} to ${(SHAPES[l.SHP2] || "?").toLowerCase()} (inverted) · ${l.UPDTE}`);
		}, handles: () => []
	},
	eq: {
		resp(u) { const v = S.mfx.eq.v; return (v.LG - 64) / 64 / (1 + Math.exp((u - v.LF / 127) * 18)) + (v.HG - 64) / 64 / (1 + Math.exp(-(u - v.HF / 127) * 18)) + (v.PG - 64) / 64 * Math.exp(-Math.pow((u - v.PF / 127) * (4 + v.PQ / 8), 2)); },
		draw(g, W, H) { grid(g, W, H); line(g, W, x => H / 2 - this.resp(x / W) * H / 3, cssv("--ink"), 2); label(g, "master EQ"); },
		handles(W, H) {
			const v = S.mfx.eq.v, yy = u => H / 2 - this.resp(u) * H / 3, gy = y => clamp(Math.round(64 + (H / 2 - y) / (H / 3) * 64));
			return [["LF", "LG"], ["PF", "PG"], ["HF", "HG"]].map(([fk, gk]) => ({ x: v[fk] / 127 * W, y: yy(v[fk] / 127), k: fk, c: cssv("--ink"), drag: (x, y) => { v[fk] = clamp(Math.round(x / W * 127)); v[gk] = gy(y); } }));
		}
	},
	dyn: {
		out(i) { const v = S.mfx.dyn.v, th = v.TRHD / 127, r = 1 + v.RTIO / 127 * 9, kn = Math.max(.001, v.KNEE / 127 * .25); return i <= th - kn ? i : i >= th + kn ? th + (i - th) / r : i + ((1 / r - 1) * Math.pow(i - th + kn, 2)) / (4 * kn); },
		draw(g, W, H) {
			grid(g, W, H); g.strokeStyle = inkA(0.45); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 6); g.lineTo(W, 6); g.stroke(); g.setLineDash([]);
			line(g, W, x => H - 6 - this.out(x / W) * (H - 12), cssv("--ink"), 2); label(g, "in → out");
		},
		handles(W, H) {
			const v = S.mfx.dyn.v, th = v.TRHD / 127; return [{ x: th * W, y: H - 6 - this.out(th) * (H - 12), k: "TRHD", c: cssv("--ink"), drag: (x) => { v.TRHD = clamp(Math.round(x / W * 127)); } },
			{ x: W - 6, y: H - 6 - this.out(1) * (H - 12), k: "RTIO", c: cssv("--ink"), drag: (x, y) => { const o = (H - 6 - y) / (H - 12), th = v.TRHD / 127; const r = (1 - th) / Math.max(.01, o - th); v.RTIO = clamp(Math.round((r - 1) / 9 * 127)); } }];
		}
	}
};
function inkA(a) { const h = cssv("--ink").replace("#", ""); const n = parseInt(h.length === 3 ? h.split("").map(c => c + c).join("") : h, 16); return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`; }
ED.route = {
	g() { return 1 + (S.tracks[S.sel].rt.DIST || 0) / 127 * 8; }, sh(x, g) { return Math.tanh(x * g) / Math.tanh(g); },
	draw(g, W, H) {
		const r = S.tracks[S.sel].rt, G = this.g(); grid(g, W, H);
		g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 8); g.lineTo(W, 8); g.stroke(); g.setLineDash([]);
		line(g, W, x => { const u = x / W * 2 - 1; return H / 2 - this.sh(u, G) * (H / 2 - 8); }, cssv("--ink"), 2.2);
		const px = r.PAN / 127 * W; g.strokeStyle = inkA(.5); g.beginPath(); g.moveTo(px + .5, 0); g.lineTo(px + .5, H); g.stroke();
		label(g, "dist · pan × vol");
	},
	handles(W, H) {
		const r = S.tracks[S.sel].rt, self = this; const G = this.g(), u = .4, yD = H / 2 - this.sh(u, G) * (H / 2 - 8);
		return [{ x: r.PAN / 127 * W, y: H - 10 - r.VOL / 127 * (H - 24), k: "PAN · VOL", c: cssv("--ink"), drag: (x, y) => { r.PAN = clamp(Math.round(x / W * 127)); r.VOL = clamp(Math.round((H - 10 - y) / (H - 24) * 127)); } },
		{ x: (u + 1) / 2 * W, y: yD, k: "DIST", c: cssv("--ink"), drag: (x, y) => { const want = (H / 2 - y) / (H / 2 - 8); let best = 0, bd = 9; for (let d = 0; d <= 127; d++) { const o = self.sh(u, 1 + d / 127 * 8); if (Math.abs(o - want) < bd) { bd = Math.abs(o - want); best = d; } } r.DIST = best; } }];
	}
};
function grid(g, W, H) { g.strokeStyle = inkA(0.13); g.lineWidth = 1; for (let i = 1; i < 4; i++) { g.beginPath(); g.moveTo(0, Math.round(H * i / 4) + .5); g.lineTo(W, Math.round(H * i / 4) + .5); g.stroke(); } for (let i = 1; i < 8; i++) { g.beginPath(); g.moveTo(Math.round(W * i / 8) + .5, 0); g.lineTo(Math.round(W * i / 8) + .5, H); g.stroke(); } }
function line(g, W, fy, c, w, dash) { g.strokeStyle = c; g.lineWidth = w; g.setLineDash(dash || []); g.beginPath(); for (let x = 0; x <= W; x += 1) { const y = fy(x); x ? g.lineTo(x, y) : g.moveTo(x, y); } g.stroke(); g.setLineDash([]); }
function label(g, t) { g.fillStyle = cssv("--ink"); g.font = "10px Silkscreen, ui-monospace, monospace"; g.fillText(t.toUpperCase(), 8, 14); }
let raf = 0, active = null;
function redraw() { if (raf) return; raf = requestAnimationFrame(() => { raf = 0; $$("canvas.ed").forEach(drawEd); }); }
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

/* ===== Sampler (UW): real kit machines, pattern locks, recorder mutes and sample names sent with
   0x73. Not readable from the machine (so not shown): the sample audio (waveforms are examples),
   names and memory in use. Not possible from here: SDS send, RAM to ROM copy (SAMPLE MGR only). ===== */
const NA = {
	send: "Send is not available: the Machinedrum ignores SDS dump requests (measured on OS 1.63). It sends samples only from its own SAMPLE MGR menu.",
	rom: "Copy RAM to ROM is not available here: the Machinedrum does it only in its SAMPLE MGR menu (FUNCTION + REC, FUNCTION + STOP). Not wired yet.",
	memory: "Sample memory in use: the Machinedrum does not report it over MIDI, and it has not been found in its memory yet.",
	names: "Sample names: the Machinedrum takes a new name (Rename) but never reports names, so they are not shown." };
const recTrack = n => S.tracks.findIndex(t => t.m === "RAM-R" + n), playTrack = n => S.tracks.findIndex(t => t.m === "RAM-P" + n);
/* Names the user sent this session (0x73). They are not read back: the machine cannot report them. */
const SENT_NAMES = {};
function hash(i) { const x = Math.sin(i * 127.1) * 43758.5; return x - Math.floor(x); }
/* MOCK: the captured audio is modelled from the pattern itself: resampling records your own beat. */
function capture(n, bins) {
	const r = recTrack(n); if (r < 0) return null; const R = S.tracks[r].syn, steps = Math.max(1, Math.round((R.LEN ?? 64) / 4)), mix = (R.MLEV ?? 64) / 127, inp = (R.ILEV ?? 0) / 127, q = (R.RATE ?? 127) / 127; const out = new Float32Array(bins);
	for (let b = 0; b < bins; b++) {
		const pos = b / bins * steps; let a = 0;
		S.tracks.forEach((t, i) => { if (t.fam === "SMP" || t.fam === "CTR" || t.fam === "MID" || !audible(i)) return; const d = .12 + (t.syn.DEC ?? 64) / 127 * .9;
			for (let k = Math.floor(pos) - 3; k <= Math.floor(pos); k++) { if (k < 0 || !t.trigs[k % S.len]) continue; const dt = pos - k; a += (t.rt.VOL ?? 100) / 127 * Math.exp(-dt / d) * (i < 2 ? 1 : .55); } });
		a = a * mix * 1.6 + inp * .25 * (.5 + .5 * Math.sin(b * .07)); a *= (.55 + .45 * hash(b)); if (q < .98) { const lv = 2 + Math.round(q * 30); a = Math.round(a * lv) / lv; } out[b] = Math.min(1, a);
	}
	return out;
}
function wave(g, W, H, data, from, to, color, dim) { const n = data.length, mid = H / 2 + 6; for (let x = 0; x < W; x++) { const v = data[Math.floor(x / W * n)] || 0, h = v * (H / 2 - 14); const u = x / W; const inside = u >= Math.min(from, to) && u <= Math.max(from, to); g.fillStyle = inside ? color : dim; g.fillRect(x, mid - h, 1, Math.max(1, h * 2)); } }
ED.sample = {
	draw(g, W, H) {
		const tr = S.tracks[S.sel], y = tr.syn, n = +(tr.m.match(/RAM-P(\d)/) || [])[1]; grid(g, W, H);
		const data = n ? capture(n, W) : Float32Array.from({ length: W }, (_, i) => Math.exp(-i / W * 5) * (.5 + .5 * hash(i)));
		if (!data) { label(g, `No RAM-R${n} in this kit, so there is nothing to play`); return; }
		const a = y.STRT / 127, b = y.END / 127, rev = b < a; wave(g, W, H, data, a, b, cssv("--ink"), inkA(0.26));
		const m = S.locks.get(lk(S.sel, "STRT")); if (m) { g.strokeStyle = cssv("--ink"); g.lineWidth = 1; g.font = "10px Silkscreen, monospace"; g.fillStyle = cssv("--ink");
			[...m.entries()].sort((p, q) => p[1] - q[1]).forEach(([st, v]) => { const x = Math.round(v / 127 * W) + .5; g.beginPath(); g.moveTo(x, 22); g.lineTo(x, H - 4); g.stroke(); g.fillText(st + 1, x + 2, H - 6); }); }
		label(g, (n ? `RAM-P${n} · plays RAM-R${n}` : "ROM sample") + (rev ? " · reversed" : ""));
	},
	handles(W, H) { const y = S.tracks[S.sel].syn; return [{ x: y.STRT / 127 * W, y: H - 14, k: "STRT", c: cssv("--ink"), drag: x => { y.STRT = clamp(Math.round(x / W * 127)); } }, { x: y.END / 127 * W, y: 30, k: "END", c: cssv("--ink"), drag: x => { y.END = clamp(Math.round(x / W * 127)); } }]; }
};
ED.rec = {
	draw(g, W, H) {
		const tr = S.tracks[S.sel], n = +tr.m.slice(5), R = tr.syn, data = capture(n, W), len = R.LEN / 127; grid(g, W, H);
		if (data) wave(g, W, H, data, 0, 1, cssv("--ink"), "transparent"); g.fillStyle = inkA(0.3); g.fillRect(len * W, 0, W - len * W, H);
		g.fillStyle = cssv("--ink"); g.font = "10px Silkscreen, monospace"; for (let k = 0; k <= 32; k += 4) { const x = k / 32 * W; g.fillText(k ? k + "" : "", x + 2, H - 4); }
		label(g, `capture ${Math.round(R.LEN / 4)} steps · RATE ${R.RATE}`);
	},
	handles(W, H) { const R = S.tracks[S.sel].syn; return [{ x: R.LEN / 127 * W, y: H / 2, k: "LEN", c: cssv("--ink"), drag: x => { R.LEN = clamp(Math.round(x / W * 127)); } }]; }
};
ED.slot = {
	draw(g, W, H, c) {
		const n = +c.dataset.n, p = playTrack(n), data = capture(n, W); grid(g, W, H); if (!data) { label(g, "Empty"); return; }
		const y = p >= 0 ? S.tracks[p].syn : { STRT: 0, END: 127 }; const a = y.STRT / 127, b = y.END / 127; wave(g, W, H, data, a, b, cssv("--ink"), inkA(0.26));
		const m = p >= 0 && S.locks.get(lk(p, "STRT")); if (m) { g.strokeStyle = cssv("--ink"); [...m.values()].forEach(v => { const x = Math.round(v / 127 * W) + .5; g.beginPath(); g.moveTo(x, 20); g.lineTo(x, H - 2); g.stroke(); }); }
		label(g, `RAM-R${n} capture (example waveform)`);
	},
	handles(W, H, c) { const p = playTrack(+c.dataset.n); if (p < 0) return []; const y = S.tracks[p].syn; return [{ x: y.STRT / 127 * W, y: H - 12, k: "STRT", c: cssv("--ink"), drag: x => { y.STRT = clamp(Math.round(x / W * 127)); } }, { x: y.END / 127 * W, y: 26, k: "END", c: cssv("--ink"), drag: x => { y.END = clamp(Math.round(x / W * 127)); } }]; }
};
function chopCls(p, s) { const t = S.tracks[p], c = ["cp"]; if (s % 4 === 0) c.push("q"); if (s % 16 === 0 && s) c.push("gap"); if (t.trigs[s]) c.push("on"); if (S.playing && s === S.step) c.push("ph"); return c.join(" "); }
function chopInner(p, s) {
	const t = S.tracks[p]; if (!t.trigs[s]) return ""; const st = S.locks.get(lk(p, "STRT"))?.get(s), en = S.locks.get(lk(p, "END"))?.get(s), rt = S.locks.get(lk(p, "RTRG"))?.get(s);
	const v = st ?? t.syn.STRT, rev = (en ?? t.syn.END) < v; return `<span class="sl">${Math.floor(v / 8) + 1}</span><span class="fx">${rev ? "REV" : ""}${rt ? " RTRG" : ""}</span>`;
}

/* ===== LCD line 2: the workspace's own values ===== */
const L2 = (k, label, val, title, edit) => `<span class="l2 ${edit ? "ed" : ""}" ${edit ? `data-l2="${k}" role="button" tabindex="0"` : ""} title="${title || ""}"><small>${label}</small><b>${val}</b></span>`;
function renderSub() {
	const t = S.sel, tr = S.tracks[t]; let h = "";
	if (S.ws === "seq") h = L2("len", "LEN", S.length === S.len ? S.len : S.length + "/" + S.len, "Pattern length (total length " + S.len + "). Click to step 16 / 32 / 48 / 64; alt-click steps the length inside it.", 1) + L2("mult", "SPD", S.mult, "Tempo multiplier. Click to step 1X / 2X / 3/4X / 3/2X.", 1)
		+ L2("swing", "SWG", S.swing + "%", "Swing 50–80 %. Drag up or down, or scroll.", 1) + L2("accAmt", "ACC", S.accAmt, "Accent 0–15. Drag up or down, or scroll.", 1) + L2("mode", "MODE", S.mode === "EXTENDED" ? "EXT" : "CLASSIC", "Classic or Extended. Locks only play in Extended. Click to switch.", 1);
	else if (S.ws === "sound") h = L2("", "TRACK", String(t + 1).padStart(2, "0")) + L2("", "MACHINE", tr.m) + L2("", "", tr.name.toUpperCase());
	else if (S.ws === "mix") h = L2("", "PATH", "SEND›ECHO›GATE›EQ›DYN›MAIN", "Sends feed the master effects. Tracks on outputs A–F skip them.");
	else if (S.ws === "sampler") { const used = S.tracks.filter(t => /^ROM/.test(t.m)).length; h = L2("", "MEM", "n/a", NA.memory) + L2("", "KIT", used + " ROM", "Tracks in this kit that play a ROM slot") + L2("", "SLOT", S.smpSlot.replace(/^RAM/, "RAM ").replace(/^ROM/, "ROM ")); }
	else if (S.ws === "control") h = L2("", "IN", "MIDI LEARN") + L2("", "MAPS", (Docs.learn?.mappings || []).length);
	else h = L2("song", "SONG", String(S.songSlot + 1).padStart(2, "0"), "Song slot. Click for the next one, shift-click for the previous (the machine loads it when stopped).", 1) + L2("", "ROWS", S.song.length) + L2("", "BARS", Math.round(songSteps() / 16)) + L2("", "TIME", songTime());
	$("#lcd2").innerHTML = h;
}
const romName = k => SENT_NAMES[k] || ""; const romCode = k => "ROM-" + String(k).padStart(2, "0");
function players(n) { return S.tracks.map((t, i) => t.m === "RAM-P" + n ? i : -1).filter(i => i >= 0); }
function slotState(n) { const r = recTrack(n); if (r < 0) return "none"; if (S.capture[n]) return "cap"; return S.tracks[r].mute ? "frozen" : "live"; }
const STATE_TXT = { none: "not in kit", live: "live", frozen: "frozen", cap: "capturing" };
function renderSlots() {
	$("#rail").innerHTML = `<div class="railhead">Slots</div>
 <div class="slotsec"><div class="scap">RAM · lost at power-off</div>${[1, 2, 3, 4].map(n => { const st = slotState(n), r = recTrack(n);
		return `<button class="slotk ram st-${st}" data-slot="RAM${n}" aria-pressed="${S.smpSlot === "RAM" + n}"><i class="led"></i><b>RAM ${n}</b><span>${STATE_TXT[st]}${r >= 0 ? " · R" + (r + 1) : ""}</span></button>`; }).join("")}
 <div class="scap">ROM · kept · 48 slots</div><div class="romgrid">${Array.from({ length: 48 }, (_, i) => { const k = i + 1;
		return `<button class="slotk rom ${S.tracks.some(t => t.m === romCode(k)) ? "has" : ""}" data-slot="ROM${k}" aria-pressed="${S.smpSlot === "ROM" + k}" title="${romCode(k)}">${String(k).padStart(2, "0")}</button>`; }).join("")}</div></div>`;
}
function pageKeys() { return `<span class="pagectl mini"><button class="pgkey" id="pgkey" ${pages16() < 2 ? "disabled" : ""} title="Next page. Shift-click = previous.">Page</button><span class="pleds" aria-hidden="true">${[0, 1, 2, 3].map(k => `<span class="pl ${k < pages16() ? "" : "na"} ${k === S.page ? "cur" : ""}" data-plp="${k}"><i class="led"></i><small>${k + 1}:4</small></span>`).join("")}</span></span>`; }
function renderSampler() {
	S.viewAll = false; const id = S.smpSlot; let h = "";
	if (id.startsWith("RAM")) {
		const n = +id.slice(3), r = recTrack(n), ps = players(n), st = slotState(n);
		if (r < 0) { h = `<div class="smpempty"><div class="edblank big">RAM ${n} is not in this kit.<br>Put RAM-R${n} on a track to record and RAM-P${n} on another to play it.</div><button class="cream" data-assign="${n}">Put RAM-R${n} on track 13 and RAM-P${n} on track 14</button></div>`; }
		else {
			if (S.chopTrack == null || !ps.includes(S.chopTrack)) S.chopTrack = ps[0] ?? null; const p = S.chopTrack, R = S.tracks[r];
			const recSteps = R.trigs.slice(0, S.len).map((x, i) => x ? i + 1 : 0).filter(Boolean);
			h = `<section class="card"><header><h3>RAM ${n} · RAM-R${n} → RAM-P${n}</h3><span>records on track ${r + 1}, step ${recSteps.join(", ") || "— (no trig)"} · plays on ${ps.length ? ps.map(i => "track " + (i + 1)).join(", ") : "no track"}</span></header>
    <div class="slotbar"><span class="stbox st-${st}"><i class="led"></i>${STATE_TXT[st]}</span>
     <button class="${st === "live" ? "cream" : ""}" data-slotmode="live" aria-pressed="${st === "live"}" title="Record again on every loop (unmutes the recorder track)">Live</button>
     <button class="${st === "frozen" ? "cream" : ""}" data-slotmode="frozen" aria-pressed="${st === "frozen"}" title="Mute the recorder track and keep this take">Freeze</button>
     <button class="rec" data-capture="${n}" title="Record one loop, then freeze">Capture next loop</button>
     <span class="grow"></span><button data-na="rom" disabled title="${NA.rom}">Copy RAM to ROM</button></div>
    <canvas class="ed smpwave" data-ed="slot" data-n="${n}" aria-label="Captured audio with start and end markers"></canvas></section>
   ${p != null ? `<section class="card"><header><h3>Chop</h3><span class="chophead">${ps.length > 1 ? ps.map(i => `<button class="${i === p ? "cream" : ""}" data-choptrk="${i}">Track ${i + 1}</button>`).join("") : "track " + (p + 1)}${pageKeys()}</span></header>
    <div class="chop" id="chop" data-p="${p}" style="grid-template-columns:repeat(16,minmax(0,1fr))">${steps().map(s => `<button class="${chopCls(p, s)}" data-cp="${s}" aria-label="Chop step ${s + 1}">${chopInner(p, s)}</button>`).join("")}</div>
    <div class="edhint">Click = trig. Drag a trig up or down = slice (a STRT lock). Alt-click = reverse. Shift-click = retrig roll.</div></section>` : `<div class="edblank">No track plays RAM-P${n}. Put RAM-P${n} on a track in Sound.</div>`}
   <div class="smp2"><section class="card"><header><h3>Source</h3><span>recorder · track ${r + 1}</span></header><div class="ctl four">${RAMR.map(k => pc("syn", k, { t: r })).join("")}</div></section>
    ${p != null ? `<section class="card"><header><h3>Playback</h3><span>player · track ${p + 1}</span></header><div class="ctl four">${SMPL.map(k => pc("syn", k, { t: p })).join("")}</div></section>` : ""}</div>`;
		}
	}
	else {
		const k = +id.slice(3), code = romCode(k), users = S.tracks.map((t, i) => t.m === code ? i : -1).filter(i => i >= 0), u = users[0];
		h = `<section class="card"><header><h3>${code}${romName(k) ? ` <span class="note" title="Sent this session with Rename; the machine cannot report names">sent: ${romName(k)}</span>` : ""}</h3><span title="${NA.names}">${k <= 24 ? "one-shot" : "loop (STRT and END are linear)"} · the machine does not report sample names or audio</span></header>
   <div class="slotbar"><span class="note">${users.length ? "Used by " + users.map(i => "track " + (i + 1)).join(", ") : "Not used in this kit"}</span><span class="grow"></span>
    <button class="cream" data-romput="${k}">Put on track ${S.sel + 1}</button><button data-na="send" disabled title="${NA.send}">Send</button><button data-rename="${k}" title="Send a new name (4 letters) to the machine, SysEx 0x73">Rename</button></div>
   <canvas class="ed smpwave" data-ed="rom" data-k="${k}" aria-label="Example ROM sample waveform"></canvas></section>
   ${u != null ? `<div class="smp2"><section class="card"><header><h3>Playback</h3><span>track ${u + 1}</span></header><div class="ctl four">${SMPL.map(q => pc("syn", q, { t: u })).join("")}</div></section></div>` : ""}`;
	}
	$("#main").innerHTML = `<div class="smpmain">${h}</div>`; syncControls(); redraw();
}
ED.rom = {
	draw(g, W, H, c) {
		const k = +c.dataset.k; grid(g, W, H); const d = Float32Array.from({ length: W }, (_, i) => { const u = i / W, hits = k > 24 ? 8 : 1; const ph = (u * hits) % 1; return Math.min(1, Math.exp(-ph * (k > 24 ? 6 : 4)) * (.55 + .45 * hash(i + k * 97))); });
		wave(g, W, H, d, 0, 1, cssv("--ink"), "transparent"); label(g, romCode(k) + " · example waveform");
	}, handles: () => []
};
let chopDrag = null;
document.getElementById("main").addEventListener("pointerdown", e => { const c = e.target.closest(".cp.on"); if (!c || e.altKey || e.shiftKey) return; const p = +$("#chop").dataset.p, s = +c.dataset.cp, cur = S.locks.get(lk(p, "STRT"))?.get(s) ?? S.tracks[p].syn.STRT; chopDrag = { c, p, s, y: e.clientY, v: cur, moved: false }; gesture = Bridge.gesture(); capture(c, e); });
document.getElementById("main").addEventListener("pointermove", e => { if (!chopDrag) return; const d = Math.round((chopDrag.y - e.clientY) / 6) * 8; if (!d && !chopDrag.moved) return; chopDrag.moved = true; const v = clamp(chopDrag.v + d); if (setLock(chopDrag.p, "STRT", chopDrag.s, v)) { chopDrag.c.innerHTML = chopInner(chopDrag.p, chopDrag.s); renderTop(); redraw(); } });
document.getElementById("main").addEventListener("pointerup", () => { if (chopDrag) { chopDrag.c.dataset.moved = chopDrag.moved ? "1" : ""; chopDrag = null; gesture = 0; } });

/* ===== Machine picker (catalogue from the plug-in) ===== */
const FAMS = [["TRX", "Analogue model"], ["EFM", "FM drums"], ["E12", "12-bit samples"], ["PI", "Physical model"], ["GND", "Tone and noise"], ["INP", "External input"], ["MID", "MIDI out"], ["CTR", "Control"], ["ROM", "ROM samples · UW"], ["RAM", "RAM record + play · UW"]];
function machList(f) { return Cat.list.filter(m => famKey(m.machine) === f).map(m => m.machine); }
function openPicker() {
	const tr = S.tracks[S.sel]; S.pickFam = S.pickFam && S.pickOpenFor === S.sel ? S.pickFam : famKey(tr.m); S.pickOpenFor = S.sel; drawPicker();
	const pop = $("#machpop"), b = $("#machbtn").getBoundingClientRect(); pop.hidden = false; pop.style.top = (b.bottom + scrollY + 6) + "px"; pop.style.left = Math.max(16, Math.min(b.left + scrollX, innerWidth - pop.offsetWidth - 16)) + "px"; $("#machbtn").setAttribute("aria-expanded", "true"); pop.querySelector(".mk[aria-pressed=true],.mk")?.focus();
}
function closePicker() { const pop = $("#machpop"); if (pop.hidden) return; pop.hidden = true; $("#machbtn")?.setAttribute("aria-expanded", "false"); }
function drawPicker() {
	const tr = S.tracks[S.sel], list = machList(S.pickFam), small = list.length > 16;
	$("#machpop").innerHTML = `<div class="mp-fams">${FAMS.map(([f, n]) => `<button class="mf" data-fam="${f}" aria-pressed="${f === S.pickFam}"><i class="led"></i><b>${f === "PI" ? "P-I" : f}</b><span>${n}</span></button>`).join("")}</div>
  <div class="mp-right"><div class="mp-head"><span class="cap">${S.pickFam === "PI" ? "P-I" : S.pickFam} · ${FAMS.find(x => x[0] === S.pickFam)[1]}</span><span class="note">${list.length} machines</span></div>
  <div class="mp-grid ${small ? "small" : ""}">${list.map(m => `<button class="mk" data-mach="${m}" aria-pressed="${m === tr.m}"><b>${codeOf(m)}</b><span>${small ? "" : nameOf(m)}</span></button>`).join("")}</div>
  <div class="mp-foot"><div class="mp-prev" id="mpprev">${prevText(tr.m)}</div>
   <button class="mp-keep" id="mpkeep" aria-pressed="${S.keepFx}"><i class="led ${S.keepFx ? "on" : ""}"></i>Keep effects + routing</button></div></div>`;
}
function prevText(m) { return `<b>${m}</b> ${nameOf(m)} · ${names(pages(m).s).join(" ")}`; }
function setMachine(v, t = S.sel) {
	const c = Cat.byName[v]; if (!c) return;
	cmd("machine", { k: S.kit, t, model: c.model, keepFx: S.keepFx });
	const tr = S.tracks[t]; tr.m = v; tr.fam = famOf(v); tr.name = nameOf(v);
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
	pop.hidden = false; pop.style.minWidth = Math.max(btn.offsetWidth, cols * 150) + "px"; const r = btn.getBoundingClientRect();
	pop.style.top = (r.bottom + scrollY + 4) + "px"; pop.style.left = Math.max(16, Math.min(r.left + scrollX, innerWidth - pop.offsetWidth - 16)) + "px"; btn.setAttribute("aria-expanded", "true");
	(pop.querySelector(".kopt[aria-selected=true]") || pop.querySelector(".kopt"))?.focus();
}
function kopt(o, sel) { return `<button class="kopt" role="option" data-v="${o.value}" aria-selected="${o.value === sel.value}"${o.disabled ? ` disabled aria-disabled="true" title="${o.title || "Not available"}"` : ""}>${o.text}</button>`; }
function closeK() { const pop = $("#kpop"); if (pop.hidden) return; pop.hidden = true; kFor?.setAttribute("aria-expanded", "false"); }
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
	draw(g, W, H) {
		const v = S.mfx.echo.v, dt = Math.max(1, v.TIME) / 256, fb = v.FB / 64; grid(g, W, H);
		g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 8 - (H - 22)); g.lineTo(W, H - 8 - (H - 22)); g.stroke(); g.setLineDash([]);
		let a = 1, x = 0, k = 0; g.fillStyle = cssv("--ink"); while (x <= 1 && k < 40) { const h = Math.min(1.25, a) * (H - 22); g.fillRect(Math.round(x * W), H - 8 - h, k ? 4 : 6, h); x += dt; a *= fb; k++; if (a < .02) break; }
		label(g, fb > 1 ? "echo taps · feedback grows!" : "echo taps · 2 bars");
	},
	handles(W, H) { const v = S.mfx.echo.v, dt = Math.max(1, v.TIME) / 256, a2 = Math.min(1.25, v.FB / 64); return [{ x: dt * W + 2, y: H - 8 - a2 * (H - 22), k: "TIME · FB", c: cssv("--ink"), drag: (x, y) => { v.TIME = clamp(Math.round(x / W * 256)); v.FB = clamp(Math.round((H - 8 - y) / (H - 22) * 64)); } }]; }
};
ED.gate = {
	draw(g, W, H) {
		const v = S.mfx.gate.v, pre = v.PRED / 127 * .15, dec = .5 + v.DEC / 127 * 2.5, gate = v.GATE >= 127 ? 9 : v.GATE / 127 * 3, span = 3.3; grid(g, W, H);
		line(g, W, x => { const t = x / W * span; if (t < pre) return H - 8; if (t > pre + gate) return H - 8; return H - 8 - (H - 22) * Math.exp(-(t - pre) / (dec / 4)); }, cssv("--ink"), 2.2);
		if (v.GATE < 127) { const gx = (pre + gate) / span * W; g.strokeStyle = inkA(.6); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(gx, 6); g.lineTo(gx, H); g.stroke(); g.setLineDash([]); }
		label(g, v.GATE >= 127 ? "reverb · gate off" : "reverb · gated");
	},
	handles(W, H) {
		const v = S.mfx.gate.v, span = 3.3, pre = v.PRED / 127 * .15, dec = .5 + v.DEC / 127 * 2.5, gate = v.GATE >= 127 ? 3.2 : v.GATE / 127 * 3;
		return [{ x: pre / span * W + 6, y: H - 10, k: "PRED", c: cssv("--ink"), drag: x => { v.PRED = clamp(Math.round((x / W * span) / .15 * 127)); } },
		{ x: (pre + dec / 4) / span * W, y: H - 8 - (H - 22) / Math.E, k: "DEC", c: cssv("--ink"), drag: x => { v.DEC = clamp(Math.round(((x / W * span - pre) * 4 - .5) / 2.5 * 127)); } },
		{ x: Math.min(W - 6, (pre + gate) / span * W), y: 14, k: "GATE", c: cssv("--ink"), drag: x => { const t = x / W * span - pre; v.GATE = t >= 3.05 ? 127 : clamp(Math.round(t / 3 * 127)); } }];
	}
};

/* ===== Control: the plug-in's MIDI Learn (jucePluginLib) as the backend ===== */
function toggleLearn() {
	S.ctl.learn = !S.ctl.learn; S.ctl.learnT = null; document.body.classList.toggle("learn", S.ctl.learn); renderTop(); syncControls();
	if (S.ctl.learn) toast("LEARN: click a value, then turn a knob on your MIDI controller."); else cmd("learnCancel");
}
/* App modulators: the page edits Mods.doc and sends it whole; values and the CC rate come back. */
function sendMods() { cmd("modSet", { doc: Mods.doc }, "modSet"); }
function paramName(t, i) { return slots(S.tracks[t].m)[i] || "#" + (i + 1); }
function modRow(sr) {
	const C = S.ctl, id = "app:" + sr.id;
	return `<button class="srch k-${sr.kind === "lfo" ? "lfo" : "rnd"} ${id === C.sel ? "sel" : ""}" data-src="${id}" style="--f:${Mods.valueOf(sr.id) / 127 * 100}%" title="App only: runs in the editor on the machine's steps and sends CCs"><span class="sk">APP</span><b>${sr.label}</b><span class="sv mono">${Mods.valueOf(sr.id)}</span></button>
	 ${S.tracks.map((t, i) => { const ls = Mods.linksOf(sr.id).filter(o => o.l.track === i); return `<button class="mxc ${ls.length ? "on" : ""} ${id === C.sel && C.selT === i ? "sel" : ""}" data-mxsrc="${id}" data-mxt="${i}" title="${ls.map(o => paramName(i, o.l.param)).join(", ") || "no link"}">${ls.slice(0, 2).map(o => `<i>${paramName(i, o.l.param)}</i>`).join("")}${ls.length > 2 ? `<i>+${ls.length - 2}</i>` : ""}</button>`; }).join("")}`;
}
function modInspector(sr) {
	const C = S.ctl, links = Mods.linksOf(sr.id).filter(o => C.selT == null || o.l.track === C.selT);
	const rate = `<div class="irow"><span class="ilab">Rate</span><span class="seg" data-set="srcrate">${Mods.RATES.map(x => `<button data-v="${x}" aria-pressed="${sr.rate === x}">${x}</button>`).join("")}</span></div>`;
	const params = sr.kind === "lfo" ? `<div class="irow"><span class="ilab">Shape</span><div class="shapes">${SHAPES.map((n, i) => `<button data-srcshape="${i}" aria-pressed="${sr.shape === i}" title="${n}">${shapeIcon(i, false)}</button>`).join("")}</div></div>${rate}
	 <div class="irow"><span class="ilab">Depth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="Depth" aria-valuemax="100" data-g="src" data-n="depth" data-src="${sr.id}"><span>DEPTH</span><b></b></div></div></div>`
		: `${rate}<div class="irow"><span class="ilab">Smooth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="Smooth" data-g="src" data-n="smooth" data-src="${sr.id}"><span>SMOOTH</span><b></b></div></div></div>`;
	return `<section class="card"><header><h3>${sr.label}</h3><span><b class="apponly">App only</b> · not in the kit · <span class="mono" id="ccrate" title="CCs the app modulators sent in the last second; the budget is ${Mods.limit}">${Mods.cc}/s of ${Mods.limit}</span></span></header>
	 <div class="note">Runs in the editor and moves with the machine's own steps while it plays, sent as CCs like host automation (at most ${Mods.limit} a second). A real Machinedrum does not play it, a lock wins on its step, and live recording does not record it. The setup is not saved with the project yet.</div>
	 ${params}
	 <div class="lhead"><span class="cap">Targets</span>${C.selT != null ? `<button class="ptog on" data-selt="all"><i class="led"></i>Track ${C.selT + 1} only</button>` : `<span class="note">${links.length} target${links.length === 1 ? "" : "s"}</span>`}</div>
	 <div class="lnks">${links.map(({ l, li }) => `<div class="lnk"><span class="lcdchip" title="${S.tracks[l.track].name}">T${l.track + 1} ${paramName(l.track, l.param)}</span>
	  <div class="pc" role="slider" tabindex="0" aria-label="Min" data-g="link" data-n="min" data-li="${li}"><span>MIN</span><b></b></div><div class="pc" role="slider" tabindex="0" aria-label="Max" data-g="link" data-n="max" data-li="${li}"><span>MAX</span><b></b></div>
	  <span class="seg" data-set="lcurve" data-li="${li}">${["lin", "exp", "log"].map(c => `<button data-v="${c}" aria-pressed="${l.curve === c}">${c.toUpperCase()}</button>`).join("")}</span>
	  <button class="ptog ${l.invert ? "on" : ""}" data-minv="${li}"><i class="led"></i>Inv</button><button class="iconkey" data-mdel="${li}" aria-label="Remove target" title="Remove">×</button></div>`).join("") || `<div class="note">No targets yet. Add one below.</div>`}</div>
	 <div class="irow addrow"><span class="ilab">Add</span><select id="mt">${S.tracks.map((t, i) => `<option value="${i}" ${i === C.addT ? "selected" : ""}>Track ${i + 1} · ${t.m}</option>`).join("")}</select>
	  <select id="mp">${slots(S.tracks[C.addT].m).map((p, i) => p ? `<option value="${i}">${p}</option>` : "").join("")}</select><button class="cream" id="maddl">Add target</button></div>
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
	const mx = `<div class="mx"><span></span>${S.tracks.map((t, i) => `<span class="mxh" title="${t.name}"><b>${i + 1}</b><small>${t.m}</small></span>`).join("")}
  ${rows.map(src => { const ms = mapsOf(src.id); return `<button class="srch ${src.id === C.sel ? "sel" : ""} k-cc" data-src="${src.id}" style="--f:0%" title="${ms.length ? ms.length + " target" + (ms.length > 1 ? "s" : "") : "Not mapped: does nothing yet"}"><span class="sk">CC ${src.cc}</span><b>${src.label}</b><span class="sv mono">${ms.length ? ms.length : "–"}</span></button>
   ${S.tracks.map((t, i) => { const ls = ms.filter(m => m.t === i); return `<button class="mxc ${ls.length ? "on" : ""} ${src.id === C.sel && C.selT === i ? "sel" : ""}" data-mxsrc="${src.id}" data-mxt="${i}" title="${ls.map(l => slots(t.m)[l.i] || l.name).join(", ") || "Map " + src.label + " to track " + (i + 1)}">${ls.slice(0, 2).map(l => `<i>${slots(t.m)[l.i] || l.name}</i>`).join("")}${ls.length > 2 ? `<i>+${ls.length - 2}</i>` : ""}</button>`; }).join("")}`; }).join("")}
  ${Mods.doc.sources.map(modRow).join("")}</div>`;
	const app = C.sel.startsWith("app:") ? Mods.source(C.sel.slice(4)) : null;
	let insp;
	if (app) insp = modInspector(app);
	else {
		const row = rows.find(r => r.id === C.sel), selMaps = mapsOf(C.sel).filter(m => C.selT == null || m.t === C.selT);
		const t = C.selT != null ? C.selT : C.addT;
		insp = `<section class="card"><header><h3>${row.label} · CC ${row.cc}</h3><span>${row.ch === 255 ? "any channel" : "channel " + (row.ch + 1)} · from your MIDI controller · saved with the plug-in's MIDI Learn preset</span></header>
  ${row.knob >= 0 ? `<div class="irow"><span class="ilab">CC</span><span class="stepper"><button data-knobcc="-1" aria-label="Lower CC number">−</button><b class="mono">${row.cc}</b><button data-knobcc="1" aria-label="Higher CC number">+</button></span><span class="note">the CC your controller's knob ${row.knob + 1} sends; its targets follow</span></div>` : ""}
  <div class="note">${L.learning ? `Learning <b>track ${L.learning.t + 1} ${slots(S.tracks[L.learning.t].m)[L.learning.i] || L.learning.name}</b>: turn a knob both ways.` : "Pick a target below, or press LEARN (L), click any value and turn a knob. The mapping moves the value through the plug-in's parameters, like host automation."}</div>
  <div class="lhead"><span class="cap">Targets</span>${C.selT != null ? `<button class="ptog on" data-selt="all"><i class="led"></i>Track ${C.selT + 1} only</button>` : `<span class="note">${selMaps.length} target${selMaps.length === 1 ? "" : "s"}</span>`}</div>
  <div class="lnks">${selMaps.map(l => `<div class="lnk"><span class="lcdchip" title="${S.tracks[l.t].name}">T${l.t + 1} ${slots(S.tracks[l.t].m)[l.i] || l.name}</span><span class="note">${l.mode}</span><span></span>
   <button class="ptog ${l.invert ? "on" : ""}" data-linv="${l.index}"><i class="led"></i>Inv</button><button class="iconkey" data-ldel="${l.index}" aria-label="Remove mapping" title="Remove">×</button></div>`).join("") || `<div class="note">Not mapped: this row does nothing yet.</div>`}</div>
  <div class="irow addrow"><span class="ilab">Add</span><select id="ct">${S.tracks.map((x, i) => `<option value="${i}" ${i === t ? "selected" : ""}>Track ${i + 1} · ${x.m}</option>`).join("")}</select>
   <select id="cp">${slots(S.tracks[t].m).map((p, i) => p ? `<option value="${i}">${p}</option>` : "").join("")}<option value="24">LEVEL</option></select><button class="cream" id="caddl">Add target</button></div>
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
		const i = el.dataset.g === "lfo" ? { SPD: 21, DEPTH: 22, SHMIX: 23 }[n] : pidx(t, n);
		if (i == null || i < 0) return;
		S.ctl.learnT = { t, p: n }; syncControls(); cmd("learnStart", { t, i }); toast(`Target: track ${t + 1} ${n}. Now turn a knob on your controller.`);
	}
}, true);

/* ===== Copy / clear / paste (the hardware's COPY CLEAR PASTE, per workspace; clipboard in the plug-in) ===== */
function secAction(kind) {
	if (S.ws === "seq") { const [a, b] = vis(); const range = { p: S.pat, t: S.sel, from: a, to: Math.min(b, S.len) };
		if (kind === "copy") cmd("copySteps", range); else if (kind === "clear") cmd("clearSteps", range); else cmd("pasteSteps", { p: S.pat, t: S.sel, from: a }); return; }
	if (S.ws === "sound") { const a = { k: S.kit, t: S.sel }; cmd(kind === "copy" ? "copySound" : kind === "clear" ? "clearSound" : "pasteSound", a); return; }
	if (S.ws === "song") { const i = S.songSel;
		if (kind === "copy") songCmd("copyRow", { i }); else if (kind === "clear") songAction("del"); else { const at = S.song[i]?.type === "end" ? i : i + 1; songCmd("pasteRow", { i: at }); S.songSel = at; } return; }
	toast("Copy, clear and paste work in Sequence, Sound and Song.");
}

/* ===== First run: firmware needed ===== */
/* The firmware screen. Opened by itself while no MD OS 1.63 runs (it cannot be closed then), or
   from LOAD ROM in the engine menu (then it has a Close key while the firmware runs). */
function firstRun(manual) {
	const d = $("#dlg"), mode = manual && S.firmware !== "missing" ? "manual" : "1";
	if (d.dataset.first === mode && !d.hidden) return;
	const m = machineState().desk || {};
	d.innerHTML = `<div class="dlgbox first" role="dialog" aria-modal="true" aria-label="Firmware needed">
 <div class="lcdbig">MACHINEDRUM FIRMWARE NEEDED</div>
 <p>Machinedrum Editor runs the real Machinedrum operating system. Elektron's firmware cannot be shipped with the app, so you add the one from your own machine.</p>
 <ol><li>Dump the <b>OS 1.63</b> flash image from your Machinedrum (8 MiB, <span class="mono">.bin</span>).</li><li>Put it in the ROM folder${m.romFolder ? `: <span class="mono">${m.romFolder}</span>` : ""}.</li><li>Press <b>Check again</b>. Machinedrum Editor checks its size and version and keeps it on this computer only.</li></ol>
 <div class="btnrow"><button class="cream" data-romfolder="1">Show the ROM folder</button><button data-recheck="1">Check again</button><span class="note">UW, MKII and MKI units all use the same OS 1.63 image.${S.firmware === "ready" ? " OS 1.63 runs now. A new ROM is used after you reopen the plug-in." : ""}</span></div></div>`;
	if (mode === "manual") d.querySelector(".btnrow").insertAdjacentHTML("beforeend", `<button data-firstclose="1">Close</button>`);
	d.hidden = false; d.dataset.first = mode;
}

/* LCD line 2 editing */
let l2drag = null;
function l2step(k, d, alt) {
	if (k === "len") { if (alt) cmd("length", { p: S.pat, v: ((S.length - 1 + d + S.len) % S.len) + 1 }); else { const o = [16, 32, 48, 64]; S.len = o[(o.indexOf(S.len) + d + 4) % 4]; cmd("totalLength", { p: S.pat, v: S.len }); } }
	if (k === "song") { cmd("selectSong", { s: (S.songSlot + d + 32) % 32 }); return; }
	if (k === "mult") { const o = ["1X", "2X", "3/4X", "3/2X"]; S.mult = o[(o.indexOf(S.mult) + d + 4) % 4]; cmd("speed", { p: S.pat, v: S.mult }); }
	if (k === "mode") { S.mode = S.mode === "EXTENDED" ? "CLASSIC" : "EXTENDED"; cmd("extended", { on: S.mode === "EXTENDED" }); }
	if (k === "swing") { S.swing = clamp(S.swing + d, 50, 80); cmd("swing", { p: S.pat, v: S.swing }, "swing"); }
	if (k === "accAmt") { S.accAmt = clamp(S.accAmt + d, 0, 15); cmd("accentAmount", { p: S.pat, v: S.accAmt }, "accAmt"); }
	render();
}
document.addEventListener("pointerdown", e => { const el = e.target.closest(".l2.ed"); if (!el) return; const k = el.dataset.l2; if (k === "swing" || k === "accAmt") { l2drag = { k, y: e.clientY, v: S[k], moved: false }; gesture = Bridge.gesture(); capture(el, e); e.preventDefault(); } });
document.addEventListener("pointermove", e => {
	if (!l2drag) return; const d = Math.round((l2drag.y - e.clientY) / (l2drag.k === "swing" ? 3 : 6)); if (d) l2drag.moved = true; const v = l2drag.k === "swing" ? clamp(l2drag.v + d, 50, 80) : clamp(l2drag.v + d, 0, 15);
	if (v !== S[l2drag.k]) { S[l2drag.k] = v; cmd(l2drag.k === "swing" ? "swing" : "accentAmount", { p: S.pat, v }, l2drag.k); renderSub(); }
});
document.addEventListener("pointerup", e => { if (!l2drag) return; const k = l2drag; l2drag = null; gesture = 0; if (!k.moved) l2step(k.k, 1); });
document.addEventListener("click", e => { const el = e.target.closest(".l2.ed"); if (!el) return; const k = el.dataset.l2; if (k !== "swing" && k !== "accAmt") l2step(k, e.shiftKey ? -1 : 1, e.altKey); });
document.addEventListener("wheel", e => { const el = e.target.closest(".l2.ed"); if (!el) return; e.preventDefault(); l2step(el.dataset.l2, (e.deltaY || e.deltaX) < 0 ? 1 : -1, e.altKey); }, { passive: false });

/* ===== Input ===== */
let drag = null;
const main = $("#main");
main.addEventListener("pointerdown", e => {
	const c = e.target.closest("canvas.ed"); if (c) { const h = nearest(c, e); if (!h) return; active = { c, k: h.k }; gesture = Bridge.gesture(); capture(c, e); e.preventDefault(); redraw(); return; }
	const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (el) { drag = { el, x: e.clientX, y: e.clientY, v: getV(el), vert: el.classList.contains("fader") }; gesture = Bridge.gesture(); capture(el, e); el.classList.add("act"); e.preventDefault(); return; }
	const lb = e.target.closest(".lb"); if (lb) { laneDraw = { erase: e.altKey, touched: new Set() }; gesture = Bridge.gesture(); capture($("#lane"), e); laneAt(e); e.preventDefault(); }
});
main.addEventListener("pointermove", e => {
	if (active) { const r = active.c.getBoundingClientRect(), h = ED[active.c.dataset.ed].handles(r.width, r.height, active.c).find(h => h.k === active.k); if (h) { h.drag(clamp(e.clientX - r.left, 0, r.width), clamp(e.clientY - r.top, 0, r.height)); syncKitValues(); syncControls(); redraw(); } return; }
	if (drag) { const fine = e.shiftKey ? .25 : 1; const d = drag.vert ? (drag.y - e.clientY) * 127 / 132 : ((e.clientX - drag.x) + (drag.y - e.clientY)) / 2; setV(drag.el, drag.v + d * fine); return; }
	if (laneDraw) { laneAt(e); return; }
	const c = e.target.closest("canvas.ed"); if (c && ED[c.dataset.ed]) c.style.cursor = nearest(c, e) ? "grab" : "default";
});
function endDrag() { if (active) { active = null; redraw(); } if (drag) { drag.el.classList.remove("act"); drag = null; } endLaneDraw(); gesture = 0; if (pendingRender) scheduleRender(); }
main.addEventListener("pointerup", endDrag); main.addEventListener("pointercancel", endDrag);
/* A gesture ends with any pointer release, wherever it lands. */
document.addEventListener("pointerup", () => setTimeout(() => { if (!interacting()) gesture = 0; }), true);
main.addEventListener("wheel", e => { const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (!el) return; e.preventDefault(); const d = (e.deltaY || e.deltaX) < 0 ? 1 : -1; setV(el, getV(el) + d * (e.shiftKey ? 10 : 1)); }, { passive: false });
main.addEventListener("dblclick", e => { const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (el) setV(el, el.dataset.n === "VOL" ? 100 : 64); });
main.addEventListener("keydown", e => { const el = e.target.closest("[data-g]"); if (!el) return; const d = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 10, PageDown: -10 }[e.key]; if (d == null) return; e.preventDefault(); setV(el, getV(el) + d * (e.shiftKey ? 10 : 1)); });

function setMute(i, on) { cmd("mute", { t: i, on }); S.tracks[i].mute = on; }
/* Solo is the page's idea: it mutes every other track on the machine, and un-solo restores the
   mutes the user had set (S.userMutes). */
function applySolo() {
	const any = S.soloSet.size > 0;
	S.tracks.forEach((t, i) => { const want = any ? !S.soloSet.has(i) : S.userMutes.has(i); if (t.mute !== want) setMute(i, want); });
}
document.addEventListener("click", e => {
	const mu = e.target.closest("[data-mute]"), so = e.target.closest("[data-solo]");
	if (mu || so) {
		const i = +(mu || so).dataset[mu ? "mute" : "solo"], t = S.tracks[i];
		if (mu) { t.mute ? S.userMutes.delete(i) : S.userMutes.add(i); setMute(i, !t.mute); }
		else { S.soloSet.has(i) ? S.soloSet.delete(i) : S.soloSet.add(i); t.solo = S.soloSet.has(i); applySolo(); }
		refreshAudible(); return;
	}
	const st = e.target.closest(".st"); if (st) {
		const i = +st.dataset.t, s = +st.dataset.s, t = S.tracks[i];
		if (!S.loaded) { toast("The pattern is not loaded yet."); return; }
		/* Live recording: a click plays the track like its TRIG key; the machine records it. */
		if (S.rec) { cmd("recTrig", { t: i }); if (i !== S.sel) select(i); return; }
		if (e.shiftKey && t.trigs[s]) { t.acc.has(s) ? t.acc.delete(s) : t.acc.add(s); cmd("accent", { p: S.pat, t: i, s }); }
		else if (e.altKey && t.trigs[s]) { t.slide.has(s) ? t.slide.delete(s) : t.slide.add(s); cmd("slide", { p: S.pat, t: i, s }); }
		else { t.trigs[s] = !t.trigs[s]; if (!t.trigs[s]) { t.acc.delete(s); t.slide.delete(s); clearStep(i, s); renderTop(); } cmd("trig", { p: S.pat, t: i, s, on: t.trigs[s] }); }
		refreshRow(i); if (i !== S.sel) select(i); else renderLane(); return;
	}
	const sel = e.target.closest("[data-sel]"); if (sel && !e.target.closest("button,select,.pc,.fader")) { select(+sel.dataset.sel); return; }
	const sg = e.target.closest(".seg[data-set] button"); if (sg) {
		const k = sg.parentElement.dataset.set, v = sg.dataset.v;
		if (k === "upd") { S.tracks[S.sel].lfo.UPDTE = v; syncKitValues(); sg.parentElement.querySelectorAll("button").forEach(b => b.setAttribute("aria-pressed", b === sg)); redraw(); }
		else if (k === "srcrate" && S.ws === "control") { const s = Mods.source(S.ctl.sel.slice(4)); if (s) { s.rate = v; sendMods(); render(); } }
		else if (k === "lcurve" && S.ws === "control") { const l = Mods.doc.links[+sg.parentElement.dataset.li]; if (l) { l.curve = v; sendMods(); render(); } }
		else if (k === "loopkind" && S.ws === "song") { const r = S.song[S.songSel]; r.type = v; if (v === "halt") r.to = S.songSel; if (v === "loop") { r.count = r.count || 2; r.to = Math.min(r.to ?? 0, Math.max(0, S.songSel - 1)); } if (v === "jump") r.to = Math.max(r.to ?? 0, S.songSel + 1); rowSet(S.songSel); render(); }
		return;
	}
	const ch = e.target.closest("[data-lane]"); if (ch) { S.lane = ch.dataset.lane; render(); return; }
	if (e.target.closest("#clearLane")) { const i = pidx(S.sel, S.lane); S.locks.delete(lk(S.sel, S.lane)); if (i >= 0) cmd("clearLane", { p: S.pat, t: S.sel, i }); renderTop(); refreshRow(S.sel); renderLane(); return; }
	const sh = e.target.closest("[data-shape]"); if (sh) { S.tracks[S.sel].lfo[sh.dataset.slot] = +sh.dataset.shape; syncKitValues(); $$(`[data-slot="${sh.dataset.slot}"]`).forEach(b => b.setAttribute("aria-pressed", b === sh)); redraw(); return; }
	const cp = e.target.closest(".cp"); if (cp) {
		if (cp.dataset.moved) { cp.dataset.moved = ""; return; } const p = +$("#chop").dataset.p, s = +cp.dataset.cp, t = S.tracks[p];
		if (t.trigs[s] && e.altKey) { const st = S.locks.get(lk(p, "STRT"))?.get(s) ?? t.syn.STRT, en = S.locks.get(lk(p, "END"))?.get(s); if (en != null && en < st) eraseLock(p, "END", s); else setLock(p, "END", s, Math.max(0, st - 8)); }
		else if (t.trigs[s] && e.shiftKey) { const r = S.locks.get(lk(p, "RTRG"))?.get(s); if (r) { eraseLock(p, "RTRG", s); eraseLock(p, "RTIM", s); } else { setLock(p, "RTRG", s, 20); setLock(p, "RTIM", s, 10); } }
		else { t.trigs[s] = !t.trigs[s]; cmd("trig", { p: S.pat, t: p, s, on: t.trigs[s] }); if (!t.trigs[s]) clearStep(p, s); else setLock(p, "STRT", s, t.syn.STRT); }
		renderTop(); cp.className = chopCls(p, s); cp.innerHTML = chopInner(p, s); redraw(); return;
	}
	const as = e.target.closest("[data-assign]"); if (as) {
		const n = as.dataset.assign; setMachine("RAM-R" + n, 12); setMachine("RAM-P" + n, 13);
		cmd("clearSteps", { p: S.pat, t: 12, from: 0, to: S.len }); cmd("trig", { p: S.pat, t: 12, s: 0, on: true });
		toast("Tracks 13 and 14 now hold RAM-R" + n + " and RAM-P" + n + "."); return;
	}
	const rn = e.target.closest("[data-rename]"); if (rn) {
		const k = +rn.dataset.rename;
		ask(`Name for <b>${romCode(k)}</b> (up to 4 letters, sent with SysEx 0x73):<br><input id="rname" maxlength="4" value="${romName(k)}" style="font:16px var(--mono);width:8ch;margin-top:8px;text-transform:uppercase">`,
			[["Send name", "cream", () => { const v = ($("#rname")?.value || "").toUpperCase(); if (!v) return; SENT_NAMES[k] = v; cmd("sampleName", { slot: k - 1, name: v }); render(); }], ["Cancel", "", () => { }]]);
		setTimeout(() => $("#rname")?.focus(), 0); return;
	}
	const pgk2 = e.target.closest("#pgkey"); if (pgk2 && !pgk2.disabled) { const n = pages16(); S.viewAll = false; S.page = (S.page + (e.shiftKey ? -1 : 1) + n) % n; render(); return; }
	const plp = e.target.closest(".pl[data-plp]"); if (plp && !plp.classList.contains("na")) { S.page = +plp.dataset.plp; S.viewAll = false; render(); return; }
	if (e.target.closest("#pgall")) { S.viewAll = !S.viewAll; render(); return; }
	if (e.target.closest("#pgfollow")) { S.follow = !S.follow; render(); return; }
	const sk = e.target.closest(".slotk"); if (sk) { S.smpSlot = sk.dataset.slot; render(); return; }
	const sm = e.target.closest("[data-slotmode]"); if (sm) { const n = +S.smpSlot.slice(3), r = recTrack(n); if (r >= 0) { setMute(r, sm.dataset.slotmode === "frozen"); delete S.capture[n]; render(); } return; }
	const cap = e.target.closest("[data-capture]"); if (cap) { const n = +cap.dataset.capture, r = recTrack(n); if (!S.playing) { toast("Press PLAY first. The capture starts at the next loop."); return; } if (r < 0) return; S.capture[n] = "armed"; setMute(r, true); toast("RAM " + n + ": records the next whole loop, then freezes."); render(); return; }
	const ct = e.target.closest("[data-choptrk]"); if (ct) { S.chopTrack = +ct.dataset.choptrk; render(); return; }
	const rp = e.target.closest("[data-romput]"); if (rp) { S.keepFx = true; setMachine(romCode(+rp.dataset.romput)); toast("Track " + (S.sel + 1) + " now plays " + romCode(+rp.dataset.romput) + "."); return; }
	if (S.ws === "song") {
		const bk = e.target.closest("[data-bank]"); if (bk) { S.bank = +bk.dataset.bank; render(); return; }
		const ap = e.target.closest("[data-addpat]"); if (ap) { if (S.song.length >= 256) { toast("A song holds 256 rows."); return; } let at = S.songSel + 1; if (S.song[S.songSel]?.type === "end") at = S.songSel; songCmd("rowInsert", { i: at, row: rowToContract({ pat: +ap.dataset.addpat, rep: 1 }, patLen) }); S.songSel = at; return; }
		const rw = e.target.closest(".scell:not(.empty),.db[data-row]"); if (rw) { S.songSel = +rw.dataset.row; render(); return; }
		const ra = e.target.closest("[data-rowact]"); if (ra) { songAction(ra.dataset.rowact); return; }
		const stp = e.target.closest("[data-step]"); if (stp) { songStep(stp.dataset.step, +stp.dataset.d * (e.shiftKey ? 10 : 1)); return; }
		const mk = e.target.closest("[data-rowmute]"); if (mk) { const r = S.song[S.songSel], k = +mk.dataset.rowmute; r.mutes = r.mutes || []; r.mutes = r.mutes.includes(k) ? r.mutes.filter(x => x !== k) : [...r.mutes, k]; rowSet(S.songSel); render(); return; }
		if (e.target.closest("[data-bpmkeep]")) { const r = S.song[S.songSel]; r.bpm = r.bpm ? undefined : Math.round(S.bpm); rowSet(S.songSel); render(); return; }
		if (e.target.closest("[data-fullpat]")) { const r = S.song[S.songSel]; delete r.ofs; delete r.len; rowSet(S.songSel); render(); return; }
		if (e.target.closest("[data-inf]")) { const r = S.song[S.songSel]; r.count = r.count === Infinity ? 2 : Infinity; rowSet(S.songSel); render(); return; }
	}
	const ok = e.target.closest("[data-out]"); if (ok) { const t = S.tracks[+ok.dataset.out]; t.out = OUTS[(OUTS.indexOf(t.out || "MAIN") + 1) % OUTS.length]; cmd("route", { t: +ok.dataset.out, out: t.out }); render(); return; }
	const dl = e.target.closest("[data-dlg]"); if (dl) { const d = $("#dlg"), f = d._btns[+dl.dataset.dlg][2]; d.hidden = true; f(); return; }
	if (e.target.closest("[data-romfolder]")) { cmd("revealRomFolder"); return; }
	if (e.target.closest("[data-recheck]")) { cmd("recheckFirmware"); return; }
	if (e.target.closest("[data-firstclose]")) { const d = $("#dlg"); d.hidden = true; d.dataset.first = ""; return; }
	if ((e.target.closest("[data-dlgclose]") || e.target.id === "dlg") && $("#dlg").dataset.first !== "1") { $("#dlg").hidden = true; $("#dlg").dataset.first = ""; return; }
	if (e.target.closest("#undo")) { cmd("undo"); return; }
	if (e.target.closest("#redo")) { cmd("redo"); return; }
	const sc = e.target.closest("[data-sec]"); if (sc) { secAction(sc.dataset.sec); return; }
	if (e.target.closest("#kitf")) { ask(`<b>${kitName(S.kit)}</b> is ${S.kitState === "edited" ? "<b>edited</b>: the changes are not saved on the machine. They are kept in the DAW project." : "saved on the machine."}`, [["Save kit", "cream", saveKit], ["Reload kit (discard edits)", "danger", () => cmd("reloadKit")], ["Close", "", () => { }]]); return; }
	if (e.target.closest("[data-reloadsong]")) { cmd("reloadSong"); return; }
	if (e.target.closest("#learnkey")) { toggleLearn(); return; }
	if (S.ws === "control") {
		const C = S.ctl;
		const shh = e.target.closest(".srch.k-cc"); if (shh) { C.sel = shh.dataset.src; C.selT = null; render(); return; }
		const mc = e.target.closest(".mxc[data-mxsrc]"); if (mc) { C.sel = mc.dataset.mxsrc; C.selT = C.addT = +mc.dataset.mxt; render(); return; }
		if (e.target.closest("#caddl")) { const [ch, cc] = C.sel.split(":").map(Number); cmd("learnAdd", { cc, ch, t: +$("#ct").value, i: +$("#cp").value }); return; }
		const kc = e.target.closest("[data-knobcc]"); if (kc) {
			const [, cc] = C.sel.split(":").map(Number), k = KNOB_CCS.indexOf(cc), to = clamp(cc + +kc.dataset.knobcc, 0, 127);
			if (k < 0 || KNOB_CCS.includes(to)) { toast("Another knob row already uses CC " + to + "."); return; }
			KNOB_CCS[k] = to; saveKnobs(); cmd("learnSetCc", { from: cc, to }); C.sel = "255:" + to; render(); return;
		}
		if (e.target.closest("[data-selt]")) { C.selT = null; render(); return; }
		const li = e.target.closest("[data-linv]"); if (li) { cmd("learnInvert", { index: +li.dataset.linv }); return; }
		const as = e.target.closest("[data-addsrc]"); if (as) { const id = Mods.add(as.dataset.addsrc); C.sel = "app:" + id; C.selT = null; sendMods(); render(); return; }
		const ds = e.target.closest("[data-delsrc]"); if (ds) { Mods.remove(ds.dataset.delsrc); C.sel = null; sendMods(); render(); return; }
		const mi = e.target.closest("[data-minv]"); if (mi) { const l = Mods.doc.links[+mi.dataset.minv]; l.invert = !l.invert; sendMods(); render(); return; }
		const md = e.target.closest("[data-mdel]"); if (md) { Mods.doc.links.splice(+md.dataset.mdel, 1); sendMods(); render(); return; }
		const ss = e.target.closest("[data-srcshape]"); if (ss) { const s = Mods.source(C.sel.slice(4)); if (s) { s.shape = +ss.dataset.srcshape; sendMods(); render(); } return; }
		if (e.target.closest("#maddl")) { const s = Mods.source(C.sel.slice(4)), p = +$("#mp").value; if (s && Mods.link(s.id, C.addT, p)) { sendMods(); render(); } else toast("That target is already linked."); return; }
		const asrc = e.target.closest(".srch[data-src^='app:']"); if (asrc) { C.sel = asrc.dataset.src; C.selT = null; render(); return; }
		const ld = e.target.closest("[data-ldel]"); if (ld) { cmd("learnRemove", { index: +ld.dataset.ldel }); return; }
	}
	const tb = e.target.closest("#tabs button"); if (tb) { S.ws = tb.dataset.ws; render(); return; }
	if (e.target.closest("#platekey")) { setPlate(S.plate === "mk2" ? "mk1" : "mk2"); return; }
	if (e.target.closest("#play")) { cmd(S.playing ? "stop" : "play"); return; }
	if (e.target.closest("#rec")) { cmd("record"); return; }
	if (e.target.closest("#patPrev")) { goPattern((S.queued ?? S.pat) - 1); return; }
	if (e.target.closest("#patNext")) { goPattern((S.queued ?? S.pat) + 1); return; }
});
document.addEventListener("change", e => {
	const id = e.target.id, v = e.target.value, tr = S.tracks[S.sel], l = tr.lfo;
	if (id === "mt" || id === "ct") { S.ctl.addT = +v; if (id === "ct" && S.ctl.selT != null) S.ctl.selT = +v; render(); return; }
	if (id === "lfoT") { l.TRCK = +v; if (!params(+v).includes(l.PARAM)) l.PARAM = params(+v)[0]; syncKitValues(); renderSound(); enhanceSelects($("#main")); }
	if (id === "lfoP") { l.PARAM = v; syncKitValues(); }
	if (id === "mg") { tr.muteGroup = v === "" ? null : +v; syncKitValues(); }
	if (id === "tg") { tr.trigGroup = v === "" ? null : +v; syncKitValues(); }
});

function select(i) { S.sel = i; if (!params(i).includes(S.lane)) S.lane = params(i).includes("FLTF") ? "FLTF" : params(i)[0] || "FLTF"; render(); }
function refreshAudible() {
	$$(".th").forEach(h => { const i = +h.dataset.sel; h.classList.toggle("off", !audible(i)); h.querySelector(".m").setAttribute("aria-pressed", S.tracks[i].mute); h.querySelector(".s").setAttribute("aria-pressed", S.tracks[i].solo); });
	$$(".r[data-row],.mr[data-row]").forEach(r => r.classList.toggle("off", !audible(+r.dataset.row)));
	$$(".strip").forEach(s => { const i = +s.dataset.sel; s.style.opacity = audible(i) ? "" : ".5"; s.querySelector(".m").setAttribute("aria-pressed", S.tracks[i].mute); s.querySelector(".s").setAttribute("aria-pressed", S.tracks[i].solo); });
}

/* BPM: drag up or down, arrows -> global tempo (0x61) */
(() => {
	const b = $("#bpm"); let d = null;
	const set = v => { S.bpm = clamp(Math.round(v * 10) / 10, 30, 300); renderTop(); cmd("tempo", { bpm: S.bpm }, "tempo"); };
	b.addEventListener("pointerdown", e => { d = { y: e.clientY, v: S.bpm }; gesture = Bridge.gesture(); capture(b, e); });
	b.addEventListener("pointermove", e => { if (!d) return; const v = d.v + (d.y - e.clientY) * (e.shiftKey ? .1 : .5); if (Math.abs(v - S.bpm) >= .05) set(v); });
	b.addEventListener("pointerup", () => { d = null; gesture = 0; });
	b.addEventListener("keydown", e => { const k = { ArrowUp: 1, ArrowDown: -1 }[e.key]; if (!k) return; e.preventDefault(); set(S.bpm + k * (e.shiftKey ? .1 : 1)); });
})();

/* Transport: the playhead comes from the machine (telemetry), moved by class only */
let lastStep = -1;
function onTelemetry(m) {
	Tele.step = m.step; Tele.pattern = m.pattern; Tele.valid = m.valid;
	if (!!m.recording !== !!S.rec) { S.rec = !!m.recording; renderTop(); if (S.rec) toast("Live recording: click a track's steps to play it, move a value to lock it."); }
	const wasPlaying = S.playing; S.playing = m.playing; S.step = m.playing ? m.step : -1;
	if (wasPlaying !== S.playing) { renderTop(); $$(".ph").forEach(c => c.classList.remove("ph")); setPos(); phLast = -1; movePH(); }
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
	if (S.ws === "mix" && S.step >= 0) S.tracks.forEach((t, i) => { if (t.trigs[S.step] && audible(i)) { const l = document.querySelector(`.act[data-act="${i}"]`); if (l) { l.classList.add("on"); setTimeout(() => l.classList.remove("on"), 90); } } });
	$$(".pl").forEach(b => b.classList.toggle("play", +b.dataset.plp === pp && S.playing));
	if (S.follow && S.ws === "seq" && !S.viewAll && pp !== S.page && !laneDraw && S.playing) { S.page = pp; render(); }
	$("#tempoled").classList.toggle("on", S.playing && S.step % 4 === 0); setPos(); queueMicrotask(movePH); $("#playled")?.classList.toggle("on", S.playing && S.step % 4 === 0);
	$$(`.st[data-s="${prev}"],.lb[data-s="${prev}"],.cp[data-cp="${prev}"]`).forEach(c => c.classList.remove("ph"));
	if (S.playing) $$(`.st[data-s="${S.step}"],.lb[data-s="${S.step}"],.cp[data-cp="${S.step}"]`).forEach(c => c.classList.add("ph"));
}

/* Soft playhead (mockup v45): one glowing column over the grid that glides from step to step.
   It jumps without animation on a wrap or a re-render and fades out on stop. The step is the
   machine's own (RAM telemetry). */
let phLast = -1;
function stepMs() { const m = { "1X": 1, "2X": 2, "3/4X": .75, "3/2X": 1.5 }[S.mult] || 1; return 60000 / (S.bpm || 120) / 4 / m; }
function movePH() {
	const seq = document.getElementById("seq"); if (!seq) return; let ph = document.getElementById("phcol");
	const c = S.playing && S.step >= 0 ? seq.querySelector(`.st[data-t="0"][data-s="${S.step}"]`) : null;
	if (!c) { if (ph) ph.style.opacity = "0"; phLast = -1; return; }
	let fresh = false; if (!ph) { fresh = true; ph = document.createElement("div"); ph.id = "phcol"; ph.setAttribute("aria-hidden", "true"); seq.appendChild(ph); }
	const last = seq.querySelector(`.st[data-t="${S.tracks.length - 1}"][data-s="${S.step}"]`) || c;
	const wrap = fresh || phLast < 0 || c.offsetLeft < phLast;
	ph.style.transition = wrap ? "opacity .15s" : `transform ${Math.round(Math.min(stepMs() * .85, 140))}ms cubic-bezier(.2,.7,.3,1),opacity .15s`;
	ph.style.width = c.offsetWidth + "px"; ph.style.top = (c.offsetTop - 3) + "px"; ph.style.height = (last.offsetTop + last.offsetHeight - c.offsetTop + 6) + "px";
	ph.style.transform = `translateX(${c.offsetLeft}px)`; ph.style.opacity = "1"; phLast = c.offsetLeft;
}

/* POSITION: bar.step of the machine's playhead (a 16-step bar), --.-- when stopped. */
function setPos() { const p = $("#pos"); if (p) p.textContent = S.playing && S.step >= 0 ? String(Math.floor(S.step / 16) + 1).padStart(2, "0") + "." + String(S.step % 16 + 1).padStart(2, "0") : "--.--"; }

/* ===== Documents in: re-derive, then render (in place while a gesture runs) ===== */
let pendingRender = false, renderRaf = 0, lastKey = "";
function interacting() { return !!(drag || active || laneDraw || l2drag || chopDrag || drag2); }
function scheduleRender() {
	if (renderRaf) return;
	/* A timer, not an animation frame: documents must land while the window is covered. */
	renderRaf = setTimeout(() => {
		renderRaf = 0;
		deriveView(S);
		if (interacting()) { pendingRender = true; syncControls(); renderTop(); renderSub(); redraw(); return; }
		pendingRender = false;
		render();
	}, 16);
}
Bridge.onMessage(m => {
	switch (m.type) {
	case "catalogue": setCatalogue(m.doc); scheduleRender(); break;
	case "doc": {
		const d = m.doc, slot = d.slot;
		if (m.kind === "pattern") Docs.patterns[slot] = d; else if (m.kind === "kit") Docs.kits[slot] = d; else if (m.kind === "song") Docs.songs[slot] = d; else if (m.kind === "global") Docs.global = d;
		/* Background loads of other patterns only matter to the song palette. */
		const relevant = (m.kind === "pattern" && (slot === currentPatternSlot() || S.ws === "song")) || (m.kind === "kit" && slot === currentKitSlot()) || m.kind === "global" || (m.kind === "song" && slot === currentSongSlot());
		if (relevant) scheduleRender();
		break;
	}
	case "machine": {
		const before = Docs.machine; Docs.machine = m.doc;
		const key = [m.doc.pattern?.current, m.doc.kit?.current, m.doc.desk?.queued, m.doc.desk?.firmware, m.doc.kit?.working, m.doc.song?.reloadNeeded, m.doc.extendedMode, (m.doc.desk?.mutes || []).join(), m.doc.desk?.playing, m.doc.desk?.recording, m.doc.desk?.kitSource, m.doc.song?.current].join("|");
		if (key !== lastKey || !before) { lastKey = key; scheduleRender(); }
		else { S.tx = !!m.doc.desk.tx; S.roundTrip = m.doc.desk.roundTripMs; S.canUndo = !!m.doc.desk.undo; S.canRedo = !!m.doc.desk.redo; S.undoCount = m.doc.desk.undoCount; S.redoCount = m.doc.desk.redoCount; $("#undo").disabled = !S.canUndo; $("#redo").disabled = !S.canRedo; syncUndoCounts(); syncTx(); }
		break;
	}
	case "telemetry": onTelemetry(m); break;
	case "setup": if (m.doc && Array.isArray(m.doc.knobCcs) && m.doc.knobCcs.join() !== KNOB_CCS.join()) { m.doc.knobCcs.forEach((c, i) => KNOB_CCS[i] = c); if (S.ws === "control") scheduleRender(); } break;
	case "mod": { const before = JSON.stringify(Mods.doc); Mods.onMessage(m); if (S.ws === "control") { if (JSON.stringify(Mods.doc) !== before && !interacting()) scheduleRender(); else syncMods(); } break; }
	case "ask":
		if (m.ask === "discardKit") ask(`<b>${patName(m.p)}</b> uses kit <b>${kitName(m.target)}</b>. Your edits to <b>${kitName(m.kit)}</b> are not saved on the machine and will be lost.`,
			[["Save kit, then switch", "cream", () => { saveKit(); cmd("select", { p: m.p, force: true }); }], ["Switch and lose edits", "danger", () => cmd("select", { p: m.p, force: true })], ["Cancel", "", () => { }]]);
		break;
	case "error": toast(m.message); showLastError([m.message]); break;
	case "learn": Docs.learn = m.doc; if (S.ws === "control") scheduleRender(); else syncControls(); if (!m.doc.learning && S.ctl.learnT) { S.ctl.learnT = null; syncControls(); } break;
	case "log": break;
	}
});

function render() {
	closePicker(); closeK(); const sl = $("#seqscroll")?.scrollLeft || 0; renderTop();
	const full = S.ws === "mix" || S.ws === "song" || S.ws === "control"; $("#body").classList.toggle("full", full); $("#rail").hidden = full;
	if (!S.tracks.length) { $("#main").innerHTML = ""; renderSub(); return; }
	if (!full) renderRail(); renderSub();
	({ seq: renderSeq, sound: renderSound, mix: renderMix, song: renderSong, sampler: renderSampler, control: renderControl })[S.ws]();
	const sc = $("#seqscroll"); if (sc) { sc.scrollLeft = sl; $("#lanescroll").scrollLeft = sl; } enhanceSelects(document.getElementById("main"));
	phLast = -1; movePH();
}
function setPlate(v) { S.plate = v; document.documentElement.dataset.plate = v; try { localStorage.setItem("mddesk.plate", v); } catch (_) { } renderTop(); redraw(); }
(() => { let v = null; try { v = localStorage.getItem("mddesk.plate"); } catch (_) { } if (!v) v = matchMedia("(prefers-color-scheme: dark)").matches ? "mk2" : "mk1"; S.plate = v; document.documentElement.dataset.plate = v; })();
document.fonts && document.fonts.ready.then(() => redraw());
document.addEventListener("keydown", e => {
	const mod = e.metaKey || e.ctrlKey; const inField = e.target.closest?.("input,select,textarea");
	if (!$("#dlg").hidden && e.key === "Escape" && $("#dlg").dataset.first !== "1") { $("#dlg").hidden = true; $("#dlg").dataset.first = ""; return; }
	if (mod && !inField && (e.key === "z" || e.key === "Z")) { e.preventDefault(); cmd(e.shiftKey ? "redo" : "undo"); return; }
	if (mod && !inField && e.key === "y") { e.preventDefault(); cmd("redo"); return; }
	if (mod && !inField && (e.key === "c" || e.key === "v")) { e.preventDefault(); secAction(e.key === "c" ? "copy" : "paste"); return; }
	if (S.ctl.learn && e.key === "Escape") { toggleLearn(); return; }
	if (e.target.closest?.("input,select,textarea,[role=slider]") || e.metaKey || e.ctrlKey || e.altKey) return;
	if ((e.key === "l" || e.key === "L")) { toggleLearn(); return; }
	if (e.key === "r" || e.key === "R") { cmd("record"); return; }
	const ws = ["seq", "sound", "mix", "sampler", "song", "control"][+e.key - 1]; if (ws) { S.ws = ws; render(); return; }
	if ((S.ws === "song" || S.ws === "seq") && (e.key === "Delete" || e.key === "Backspace")) { e.preventDefault(); S.ws === "song" ? songAction("del") : secAction("clear"); return; }
	if (S.ws === "song" && (e.key === "ArrowLeft" || e.key === "ArrowRight")) { S.songSel = Math.max(0, Math.min(S.song.length - 1, S.songSel + (e.key === "ArrowRight" ? 1 : -1))); render(); return; }
	if (e.key === " ") { e.preventDefault(); cmd(S.playing ? "stop" : "play"); }
	if ((e.key === "[" || e.key === "]") && S.ws === "seq" && pages16() > 1) { const n = pages16(); S.viewAll = false; S.page = (S.page + (e.key === "]" ? 1 : -1) + n) % n; render(); }
});
new ResizeObserver(() => redraw()).observe(document.body);
render();
Bridge.ready();

/* GEARMULATOR_MDSTUDIO_SELFTEST=1 (?selftest=1): edit a trig, a lock and a kit value through the same
   commands a click sends, and log what the firmware read back and how long it took. Times are taken
   when the document arrives (message handler), so page timer throttling does not inflate them. */
if (/[?&]selftest=1/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	let waiter = null;
	Bridge.onMessage(m => {
		if (m.type === "error") Bridge.log("selftest: page error message: " + m.message);
		if (waiter && waiter.f()) { const w = waiter; waiter = null; w.done(performance.now()); }
	});
	const until = (f, ms) => new Promise(done => {
		if (f()) { done(performance.now()); return; }
		waiter = { f, done };
		setTimeout(() => { if (waiter && waiter.done === done) { waiter = null; done(-1); } }, ms);
	});
	const log = t => Bridge.log("selftest: " + t);
	const pat = () => Docs.patterns[currentPatternSlot()], kit = () => Docs.kits[currentKitSlot()];
	const idle = () => !(machineState().desk || {}).tx;
	const status = () => `loaded ${!!(pat() && kit())} firmware ${machineState().desk?.firmware} pattern ${currentPatternSlot()} kit ${currentKitSlot()} docs ${Object.keys(Docs.patterns).length}/${Object.keys(Docs.kits).length}/${Object.keys(Docs.songs).length}`;
	if (await until(() => pat() && kit() && machineState().desk?.firmware === "ready", 60000) < 0) { log("FAIL: the machine did not load: " + status()); return; }
	log("loaded: " + status() + " visibility " + document.visibilityState);
	await sleep(1500);
	const p = currentPatternSlot(), k = currentKitSlot(), has = () => pat().tracks[0].trigs.includes(8);
	const ms = (t0, t1) => t1 < 0 ? "TIMEOUT" : (t1 - t0).toFixed(1) + " ms";
	for (const round of [1, 2, 3, 4, 5]) {
		const before = has();
		const t0 = performance.now();
		const cell = document.querySelector('.st[data-t="0"][data-s="8"]');
		if (cell) cell.click(); else cmd("trig", { p, t: 0, s: 8, on: !before });	// the grid's own click handler
		const shown = await until(() => has() !== before, 3000);
		const confirmed = await until(() => has() !== before && idle(), 3000);
		log(`trig round ${round}: ${confirmed >= 0 ? "ok" : "FAIL"} click -> view ${ms(t0, shown)}, -> confirmed read-back ${ms(t0, confirmed)} (desk ${Math.round(machineState().desk.roundTripMs)} ms)`);
		await sleep(400);
	}
	if (!has()) { cmd("trig", { p, t: 0, s: 8, on: true }); await until(() => has() && idle(), 3000); }
	let t0 = performance.now();
	cmd("lock", { p, t: 0, i: 12, s: 8, v: 99 });
	let t1 = await until(() => pat().locks.some(l => l.track === 0 && l.param === 12 && l.steps.some(([s, v]) => s === 8 && v === 99)) && idle(), 3000);
	log(`lock: ${t1 >= 0 ? "ok" : "FAIL"} confirmed ${ms(t0, t1)}`);
	cmd("lock", { p, t: 0, i: 12, s: 8, v: null }); await until(idle, 3000);
	const cells = [...document.querySelectorAll(".st.on")].length;
	log(`page shows ${cells} lit trigs on this page, LCD "${$("#pat").textContent} ${$("#kitname").textContent} ${$("#lcd2").textContent}", lock meter ${$("#lockn").textContent}`);
	/* The kit value through the Sound workspace: a wheel step on the DIST control. */
	S.sel = 0; S.ws = "sound"; render();
	const old = kit().tracks[0].routing[0];
	const knob = document.querySelector('.pc[data-g="rt"][data-n="DIST"]');
	t0 = performance.now();
	for (let n = 0; n < 13; ++n) knob.dispatchEvent(new WheelEvent("wheel", { deltaY: old + 13 > 127 ? 1 : -1, bubbles: true, cancelable: true }));
	const want = old + 13 > 127 ? old - 13 : old + 13;
	log(`Sound: DIST control shows ${knob.querySelector("b").textContent}`);
	t1 = await until(() => kit().tracks[0].routing[0] === want && machineState().kit.working === "edited", 2000);
	log(`kit DIST ${old} -> ${want}: ${t1 >= 0 ? "ok" : "FAIL"} sent as CC, working copy + "edited" after ${ms(t0, t1)}`);
	cmd("saveKit"); await sleep(300);
	delete Docs.kits[k];
	t0 = performance.now();
	Bridge.send({ op: "load", kind: "kit", slot: k });
	t1 = await until(() => kit() && kit().tracks[0].routing[0] === want, 3000);
	log(`kit read back after SAVE KIT: ${t1 >= 0 ? "ok" : "FAIL"} DIST = ${kit() ? kit().tracks[0].routing[0] : "?"} (${ms(t0, t1)})`);
	cmd("param", { k, t: 0, i: 16, v: old }); await sleep(200); cmd("saveKit"); await sleep(500);
	S.ws = "seq"; render();
	/* P3: working kit from memory, LCD width, REC. The firmware's start-up animation runs for about
	   20 s after it answers MIDI and eats the first key press: wait it out. */
	while (performance.now() < 30000) await sleep(500);
	log(`P3: kit source ${machineState().desk.kitSource}, kit ${machineState().kit.working}, song mode ${machineState().songMode}, mutes ${machineState().desk.mutes}`);
	const lcdW = () => Math.round(document.querySelector(".lcdpanel").getBoundingClientRect().width * 10) / 10;
	const widths = [lcdW()];
	const bpm0 = S.bpm;
	for (const b of [30, 299.5, bpm0]) { cmd("tempo", { bpm: b }); await sleep(250); widths.push(lcdW()); }
	await sleep(800);
	log(`P3: tempo back to ${S.bpm} (was ${bpm0})`);
	log(`P3: LCD width through tempo changes ${widths.join(" / ")} px`);
	await document.fonts.ready;
	const faces = [...document.fonts].map(f => `${f.family.replace(/"/g, "")} ${f.weight} ${f.status}`);
	const fam = q => getComputedStyle(document.querySelector(q)).fontFamily.split(",")[0].replace(/"/g, "");
	log(`P3: fonts ${faces.join(", ")}; LCD value ${fam("#bpm")}, workspace keys ${fam("#tabs button")}, grid ruler ${fam(".rul")}`);
	const rect = q => { const b = document.querySelector(q).getBoundingClientRect(); return `${Math.round(b.left)},${Math.round(b.top)} ${Math.round(b.width)}x${Math.round(b.height)}`; };
	const rb = $("#rec").getBoundingClientRect(), pb = $("#play").getBoundingClientRect();
	log(`P3: transport keys REC ${rect("#rec")}, PLAY ${rect("#play")}: ${Math.abs(rb.width - rb.height) < 1 && Math.abs(pb.width - pb.height) < 1 && rb.width > 30 && Math.abs(rb.top - pb.top) < 1 && pb.left > rb.right ? "ok square, side by side" : "FAIL"}; LCD ${rect(".lcdpanel")}, window ${innerWidth}x${innerHeight}`);
	const recOn = () => !!machineState().desk.recording;
	t0 = performance.now();
	$("#play").click();
	t1 = await until(() => machineState().desk.playing, 3000);
	log(`P3: PLAY key -> playing ${t1 >= 0 ? "ok" : "FAIL"} ${ms(t0, t1)}, key shows ${$("#playico").textContent}`);
	t0 = performance.now();
	$("#play").click();
	t1 = await until(() => !machineState().desk.playing, 3000);
	log(`P3: STOP (same key) -> stopped ${t1 >= 0 ? "ok" : "FAIL"} ${ms(t0, t1)}, key shows ${$("#playico").textContent}`);
	await sleep(300);
	t0 = performance.now();
	$("#rec").click();
	t1 = await until(recOn, 3000);
	log(`P3: after REC: playing ${machineState().desk.playing} step ${S.step} telemetry ${machineState().desk.telemetry}`);
	log(`P3: REC -> live recording ${t1 >= 0 ? "ok" : "FAIL"} ${ms(t0, t1)}; LCD ${lcdW()} px, REC key pressed ${$("#rec").getAttribute("aria-pressed")}`);
	if (t1 >= 0) {
		const tr = 13, before = (pat().tracks[tr].trigs || []).length;
		await until(() => S.step >= 2 && S.step <= 4, 3000);
		document.querySelector(`.st[data-t="${tr}"][data-s="0"]`).click();
		await sleep(1200);
		const after = (pat().tracks[tr].trigs || []).length;
		log(`P3: grid click while recording -> recorded trig on track ${tr + 1}: ${after > before ? "ok" : "FAIL"} (${before} -> ${after} trigs, read back while recording)`);
		$("#rec").click();
		t1 = await until(() => !recOn(), 3000);
		log(`P3: REC again -> recording off ${t1 >= 0 ? "ok" : "FAIL"}, still playing ${machineState().desk.playing}`);
		/* v49: a queued pattern shows only its name, blinking; a flash when it starts. */
		const from = currentPatternSlot(), to = (from + 1) % 128, w0 = lcdW();
		cmd("select", { p: to, force: true });
		await until(() => machineState().desk.queued === to, 2000); await sleep(150);
		log(`P3: queued ${patName(to)}: LCD shows "${$("#pat").textContent}" ${getComputedStyle($("#pat")).animationName}, LCD ${w0} -> ${lcdW()} px`);
		let flashed = false; const obs = new MutationObserver(() => { if ($(".patf").classList.contains("flash")) flashed = true; }); obs.observe($(".patf"), { attributes: true });
		t0 = performance.now(); t1 = await until(() => machineState().desk.queued == null && currentPatternSlot() === to, 12000); await sleep(100); obs.disconnect();
		log(`P3: switch heard after ${ms(t0, t1)}: LCD "${$("#pat").textContent}", flash ${flashed ? "ok" : "FAIL"}, LCD ${lcdW()} px`);
		cmd("stop"); await until(() => !machineState().desk.playing, 3000);
		cmd("select", { p: from, force: true }); await sleep(500);
		$("#play").click(); await until(() => !machineState().desk.playing, 3000);
		log(`P3: PLAY key shows ${$("#playico").textContent} after stop`);
		cmd("clearSteps", { p: currentPatternSlot(), t: tr, from: 0, to: S.len }); await sleep(400);
	}
	log("selftest done; kit " + JSON.stringify(machineState().kit) + ", undo steps " + machineState().desk.undoCount);
})();
