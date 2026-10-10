"use strict";
/* The Sampler (the UW's ROM and RAM machines): the slots in the rail, a RAM slot's set-up, take, steps and
   chops, a ROM slot's waveform and every ROM slot as a tile; the samples' waveforms (overview and detail),
   the audition, a sample file on its way to a ROM slot, and the sample screens of the Sound page. The chop
   drag is mdDeskGestures.js's. */

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
/* the sample slot machine m plays or records (["rom", 0-47], ["ram", 0-3]), or null */
function smpOfMachine(m) { return machineFacts(m, Cat).slot; }
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
	if (m.state !== "sending" && smpDropWait === m.slot) nextSmpDrop();
	if (S.ws === "sampler") scheduleRender();
}
/* Samples dropped on the window (shared/deskDrop.js): into the ROM slot they were dropped on (a tile, a slot key),
   else the selected one, and the slots after it, one file after another: the machine takes one SDS transfer at a
   time, so the next goes when sampleLoad says the one before ended (done, failed or cancelled). One file goes at once,
   as Load sample… does; several are asked about first (each replaces what its slot holds). The words for the drop's
   toast, or "". */
let smpDrops = [], smpDropWait = null;
function smpDropSlot(x, y) {
	const el = typeof document.elementFromPoint === "function" ? document.elementFromPoint(x, y) : null;
	const on = el && el.closest ? el.closest("[data-romtile],[data-smpload],.slotk.rom") : null;
	if (on) return +(on.dataset.romtile || on.dataset.smpload || on.dataset.slot.slice(3)) - 1;
	return S.smpSlot.startsWith("ROM") ? +S.smpSlot.slice(3) - 1 : -1;
}
function dropSamples(drop, items, x, y) {
	if (S.ws !== "sampler") return "Open the Sampler and select a ROM slot, then drop the samples.";
	if (!canDo(V, "sampleLoad")) return V.caps.reasons.sampleLoad || "This engine cannot load samples.";
	if (smpDrops.length || smpDropWait != null || (smpLoad && smpLoad.state === "sending"))
		return "A sample is on its way: drop the next ones when it is in.";
	const first = smpDropSlot(x, y);
	if (first < 0) return "Select a ROM slot, then drop the samples: a RAM slot holds only what RAM-R records.";
	const fit = items.slice(0, 48 - first), left = items.length - fit.length;
	const go = () => { smpDrops = fit.map((it, i) => ({ drop, n: it.n, slot: first + i })); nextSmpDrop(); };
	if (fit.length === 1) go();
	else ask(`Load ${fit.length} samples into ${romCode(first + 1)} to ${romCode(first + fit.length)}? `
		+ "Each replaces what its slot holds.",
		[["Load", "danger", go], ["Cancel", "", () => { }]]);
	return left ? `ROM-48 is the last slot: ${left} sample${left === 1 ? "" : "s"} not loaded.` : "";
}
function nextSmpDrop() {
	const d = smpDrops.shift(); smpDropWait = d ? d.slot : null; if (!d) return;
	cmd("dropSample", { drop: d.drop, n: d.n, slot: d.slot }, undefined, undefined,
		r => { if (!r.ok) { smpDrops = []; smpDropWait = null; } });
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
		const tr = V.tracks[S.sel], y = tr.syn, f = machineFacts(tr.m, Cat), n = f.player ? f.slot[1] + 1 : 0; grid(g, W, H);
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
		const tr = V.tracks[S.sel], n = machineFacts(tr.m, Cat).slot[1] + 1, R = tr.syn, len = R.LEN / 127; grid(g, W, H);
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
  <p class="note spmix" title="${OUT_OF_MIX}">The recorder samples the main mix with its track's VOL at 0, so it never records itself.</p>
</section>`;
}
/* The recorder's source, from its levels: MLEV/MBAL = the machine's own mix, ILEV/IBAL = inputs A/B. */
/* Manual A-15: MLEV/ILEV 0 records "as is", -64 records nothing (stored 64 and 0); the balances are
   -64..+63 (stored 0..127, 64 = centre). */
const SOURCES = [["main", "Main mix", { MLEV: 64, MBAL: 64, ILEV: 0, IBAL: 64 }], ["a", "Input A", { MLEV: 0, MBAL: 64, ILEV: 64, IBAL: 0 }],
	["b", "Input B", { MLEV: 0, MBAL: 64, ILEV: 64, IBAL: 127 }], ["ab", "A + B", { MLEV: 0, MBAL: 64, ILEV: 64, IBAL: 64 }]];
/* A recorder that samples the machine's own mix (MLEV above 0) is itself in that mix: what it records comes out of its
   track and goes back into what it records, a feedback loop (heard: mdDeskFirmwareTest recordermix, +8 dB). Its track's
   VOL at 0 takes it out of the main mix and its recording goes on; an input source gives its VOL back. recVol keeps
   the VOL it had, per track. */
const recVol = {};
const samplesMix = t => (V.tracks[t].syn.MLEV ?? 0) > 0;
function recorderOutOfMix(t, mix) {
	const vol = V.tracks[t].rt.VOL;
	if (mix && vol !== 0) { recVol[t] = vol; sendParam(t, "rt", "VOL", 0); }
	else if (!mix && vol === 0 && recVol[t] != null) { sendParam(t, "rt", "VOL", recVol[t]); delete recVol[t]; }
}
const OUT_OF_MIX = "VOL 0: the recorder is out of the main mix while it samples it, or its own sound would go back into what it records. An input as the source gives VOL back.";
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
   <div class="smp2"><section class="card"><header><h3>Source</h3><span>recorder · track ${r + 1}</span></header><div class="irow">${sourceSeg(r)}</div>${samplesMix(r) ? `<p class="note recmix" title="${OUT_OF_MIX}">${V.tracks[r].rt.VOL === 0 ? "VOL 0: out of the main mix while it samples it" : "Samples the main mix and is in it: its own sound goes back into the recording. Main mix sets its VOL to 0."}</p>` : ""}<div class="ctl four">${RAMR.map(k => pc("syn", k, { t: r, unit: k === "LEN" ? "len" : k === "RATE" ? "rate" : "" })).join("")}</div></section>
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
	/* .setup: the page is the set-up card alone (a class, not :has(): macOS 12's WebKit 15 has none, B-054) */
	$("#main").innerHTML = `<div class="smpmain ${ram ? "ram" : ""}${/^\s*<section class="card smpsetup"/.test(h) ? " setup" : ""}">${h}</div>`; syncControls(); redraw();
}
ED.rom = {
	draw(g, W, H, c) {
		const k = +c.dataset.k; grid(g, W, H);
		if (drawSlot(g, W, H, ["rom", k - 1], 0, 1, "transparent")) { const s = smpSlotOf("rom", k - 1); label(g, romCode(k) + (s.name ? " · " + s.name : "") + " · " + smpInfo(s)); }
	}, handles: () => []
};

/* the Sampler's clicks (the router's, mdDeskRender.js CLICKS): true when the click was theirs. First the RAM
   view's steps, the set-up card and the ROM names; then (after the page control) the slots and their keys. */
function clickSamplerSteps(e) {
	const rc = e.target.closest("[data-rc]"); if (rc) {
		const r = +$("#recon").dataset.r, s = +rc.dataset.rc, on = !V.tracks[r].trigs[s];
		cmd("trig", { p: V.pat, t: r, s, on }, undefined, [[["tracks", r, "trigs", s], on], ...(on ? [] : clearStep(r, s))]);
		renderTop(); rc.className = stepCls(r, s); rc.setAttribute("aria-pressed", on); return true;
	}
	const cp = e.target.closest("[data-cp]"); if (cp) {
		if (cp.dataset.moved) { cp.dataset.moved = ""; return true; } const p = +$("#chop").dataset.p, s = +cp.dataset.cp, t = V.tracks[p];
		if (t.trigs[s] && e.altKey) { const st = V.locks.get(lk(p, "STRT"))?.get(s) ?? t.syn.STRT, en = V.locks.get(lk(p, "END"))?.get(s); if (en != null && en < st) eraseLock(p, "END", s); else setLock(p, "END", s, Math.max(0, st - 8)); }
		else if (t.trigs[s] && e.shiftKey) { const r = V.locks.get(lk(p, "RTRG"))?.get(s); if (r) { eraseLock(p, "RTRG", s); eraseLock(p, "RTIM", s); } else { setLock(p, "RTRG", s, 20); setLock(p, "RTIM", s, 10); } }
		else { const on = !t.trigs[s]; cmd("trig", { p: V.pat, t: p, s, on }, undefined, [[["tracks", p, "trigs", s], on], ...(on ? [] : clearStep(p, s))]); if (on) setLock(p, "STRT", s, t.syn.STRT); }
		renderTop(); cp.className = stepCls(p, s); cp.setAttribute("aria-pressed", t.trigs[s]); cp.innerHTML = chopInner(p, s); redraw(); return true;
	}
	const stt = e.target.closest("[data-setupt]"); if (stt) {
		const [r, p] = setupTracks(), d = +stt.dataset.d;
		if (stt.dataset.setupt === "r") { S.smpRec = (r + d + 16) % 16; if (S.smpRec === p) S.smpRec = (S.smpRec + d + 16) % 16; }
		else { S.smpPlay = (p + d + 16) % 16; if (S.smpPlay === r) S.smpPlay = (S.smpPlay + d + 16) % 16; }
		render(); return true;
	}
	const sgo = e.target.closest("[data-setupgo]"); if (sgo) {
		const n = sgo.dataset.setupgo, [r, p] = setupTracks(), noTrig = !V.tracks[r].trigs.slice(0, V.len).some(Boolean);
		Gesture.begin();	/* one undo step */
		setMachine("RAM-R" + n, r); setMachine("RAM-P" + n, p);
		/* the main mix as the source, the recorder out of it (recorderOutOfMix) */
		Object.entries(SOURCES[0][2]).forEach(([k, v]) => sendParam(r, "syn", k, v));
		if (V.tracks[r].rt.VOL !== 0) { recVol[r] = V.tracks[r].rt.VOL; sendParam(r, "rt", "VOL", 0); }
		if (!noTrig && S.smpOnce) cmd("clearSteps", { p: V.pat, t: r, from: 0, to: V.len }, undefined, V.tracks[r].trigs.map((on, s) => on && [["tracks", r, "trigs", s], false]).filter(Boolean));
		if (noTrig || S.smpOnce) cmd("trig", { p: V.pat, t: r, s: 0, on: true }, undefined, [[["tracks", r, "trigs", 0], true]]);
		Gesture.end();
		toast(`Sampling ready: track ${r + 1} records the main mix (RAM-R${n}, its VOL at 0 so it does not record itself), track ${p + 1} plays (RAM-P${n}). Undo takes it back.`); render(); return true;
	}
	const son = e.target.closest("[data-smponce]"); if (son) { S.smpOnce = son.dataset.smponce === "1"; render(); return true; }
	const rs = e.target.closest("[data-recsrc]"); if (rs) {
		const t = +rs.dataset.t, src = SOURCES.find(([id]) => id === rs.dataset.recsrc);
		if (src) { Gesture.begin(); Object.entries(src[2]).forEach(([n, v]) => sendParam(t, "syn", n, v)); recorderOutOfMix(t, src[2].MLEV > 0); Gesture.end(); render(); }
		return true;
	}
	const rn = e.target.closest("[data-rename]"); if (rn) {
		const k = +rn.dataset.rename;
		ask(`Name for <b>${romCode(k)}</b> (up to 4 letters, sent with SysEx 0x73):<br><input id="rname" maxlength="4" value="${romName(k)}" style="font:16px var(--mono);width:8ch;margin-top:8px;text-transform:uppercase">`,
			[["Send name", "cream", () => { const v = ($("#rname")?.value || "").toUpperCase(); if (!v) return; SENT_NAMES[k] = v; cmd("sampleName", { slot: k - 1, name: v }); render(); }], ["Cancel", "", () => { }]]);
		setTimeout(() => $("#rname")?.focus(), 0); return true;
	}
	return false;
}
function clickSlots(e) {
	const sk = e.target.closest(".slotk"); if (sk) { S.smpSlot = sk.dataset.slot; render(); return true; }
	const sm = e.target.closest("[data-slotmode]"); if (sm) { const n = +S.smpSlot.slice(3), r = recTrack(n); if (r >= 0) { userMute(r, sm.dataset.slotmode === "frozen"); delete S.capture[n]; render(); } return true; }
	const cap = e.target.closest("[data-capture]"); if (cap) { const n = +cap.dataset.capture, r = recTrack(n); if (!V.playing) { toast("Press PLAY first. The capture starts at the next loop."); return true; } if (r < 0) return true; S.capture[n] = "armed"; userMute(r, true); toast("RAM " + n + ": records the next whole loop, then freezes."); render(); return true; }
	const ct = e.target.closest("[data-choptrk]"); if (ct) { S.chopTrack = +ct.dataset.choptrk; render(); return true; }
	const sl = e.target.closest("[data-smpload]"); if (sl) { if (!sl.disabled) cmd("chooseSample", { slot: +sl.dataset.smpload - 1 }); return true; }
	if (e.target.closest("[data-smpstop]")) { cmd("sampleCancel", {}); return true; }
	const au = e.target.closest("[data-aud]"); if (au) { if (!au.disabled) toggleAud(au); return true; }
	const rt = e.target.closest("[data-romtile]"); if (rt) { S.smpSlot = "ROM" + rt.dataset.romtile; render(); return true; }
	const rp = e.target.closest("[data-romput]"); if (rp) { S.keepFx = true; setMachine(romCode(+rp.dataset.romput)); toast("Track " + (S.sel + 1) + " now plays " + romCode(+rp.dataset.romput) + "."); return true; }
	return false;
}
