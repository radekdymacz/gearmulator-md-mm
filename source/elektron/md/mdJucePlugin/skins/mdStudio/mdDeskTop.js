"use strict";
/* The top bar and the frame around the workspaces: the LCD (pattern, kit, tempo, sync, line 2 and its
   values), the engine label and menu, what the engine cannot do (CAP_CONTROLS), the track rail, the transport
   (telemetry, the playhead, the position) and the panel colour. */

/* ===== Top bar ===== */
let lastQueued = null;
function renderTop() {
	$$("#tabs button").forEach(b => b.setAttribute("aria-selected", b.dataset.ws === S.ws));
	const lkk = $("#learnkey"); if (lkk) { lkk.setAttribute("aria-pressed", S.ctl.learn); lkk.classList.toggle("on", S.ctl.learn); }
	const pk = $("#platekey"); if (pk) pk.querySelector("span").textContent = S.plate === "mk2" ? "MKII" : "MKI";
	const n = V.locks.size, m = $("#meter"); $("#lockn").textContent = String(n).padStart(2, "0") + "/64"; m.className = "f meter" + (n >= 64 ? " full" : n >= 52 ? " warn" : ""); syncLockBudget();
	$("#bpm").textContent = (+V.bpm).toFixed(1);
	/* B-030: the host's tempo (in a DAW the machine follows): marked, and not edited here */
	const bf = $("#bpm").parentElement; bf.classList.toggle("hosttempo", !!V.hostTempo);
	$("#bpm").title = V.hostTempo ? "The DAW's tempo: the machine follows the host. Change it in the DAW." : "Drag up or down";
	$("#bpm").setAttribute("aria-readonly", V.hostTempo ? "true" : "false");
	/* Mockup v49: only the target's name, blinking while it waits for the pattern end; one short
	   flash when the machine really switches (the desk clears the queue at the playhead wrap). */
	$("#pat").textContent = patName(V.queued ?? V.pat);
	songLcd();
	$("#pat").parentElement.classList.toggle("queued", V.queued != null);
	if (lastQueued != null && V.queued == null && V.pat === lastQueued) { const pf = $(".patf"); if (pf) { pf.classList.remove("flash"); void pf.offsetWidth; pf.classList.add("flash"); } }
	lastQueued = V.queued;
	/* no kit document yet (HW MIDI not answering, or the first read): no placeholder name and no saved/edited chip */
	$("#kitname").textContent = V.loaded ? kitName(V.kit) : "—";
	setKitState(V.kitState);
	const sv = $("#save"); if (sv) sv.style.visibility = V.loaded ? "" : "hidden";
	$("#undo").disabled = !V.canUndo; $("#redo").disabled = !V.canRedo; syncUndoCounts();
	secLabels();	/* COPY CLR PASTE: the selected steps, or the workspace's own (mdDeskComforts.js) */
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
		const hw = V.lifecycle === "hwConnecting" || V.lifecycle === "hwLost";	/* Boot.midi's card says it then */
		const msg = V.input && !V.loaded && !hw ? "Reading the current pattern and kit from the machine…" : "";
		st.textContent = msg; st.hidden = !msg;
	}
	/* the start-up card over the whole window until the machine takes input; NO ROM and ROM ERROR are its
	   first-run states (an engine over MIDI has no start-up of its own) */
	const bootState = { missing: "missing", unsupported: "unsupported", loading: "loading", booting: "booting", animating: "booting" }[V.lifecycle] || "ready";
	Boot.update({ state: bootState, machine: "Machinedrum" });
	/* HW MIDI with no machine answering: a card over the dimmed workspace says what to do (deskBoot.js) */
	Boot.midi({ state: { hwConnecting: "connecting", hwLost: "lost" }[V.lifecycle] || null, machine: "Machinedrum", text: V.lifecycleText });
	/* the start-up card is the no-ROM screen: the old one goes, shown or still waiting behind another dialog (Dlg, deskModal.js) */
	Dlg.drop("first:1"); if ($("#dlg").hidden) $("#dlg").dataset.first = "";
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
/* one routine for both editors (skins/shared/deskCaps.js): this page's look is a disabled control whose own tooltip
   comes back when it is allowed again */
function markCapabilities() {
	DeskCaps.mark({ controls: CAP_CONTROLS, reason: cap => canDo(V, cap) ? "" : V.caps.reasons[cap] || "Not available with this engine.",
		attr: "capna", keepTitle: true, disable: true, all: $$ });
}
/* a control the engine cannot do takes no gesture: its reason instead */
DeskCaps.guard({ attr: "capna", events: ["pointerdown", "click", "wheel", "keydown", "dblclick"], say: t => toast(t) });
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
 <div class="sw"></div><div class="n ${i % 4 === 0 ? "fill" : ""}">${i + 1}</div><div class="nm${S.ws === "seq" ? " tagged" : ""}" title="${t.name}">${S.ws === "seq" ? `<b>${codeOf(t.m, Cat)}</b><i class="gtag" title="${t.m}: its generator (GEN bar)">${genTag(genSpec(i))}</i>` : `<b>${t.m}</b>`}</div>
 <button class="ms m" data-mute="${i}" aria-pressed="${t.mute}" aria-label="Mute track ${i + 1}">M</button><button class="ms s" data-solo="${i}" aria-pressed="${t.solo}" aria-label="Solo track ${i + 1}">S</button></div>`;
}
function renderRail() {
	if (S.ws === "sampler") { renderSlots(); return; }
	$("#rail").innerHTML = `<div class="railhead ${S.ws === "seq" ? "tall" : ""}">Track<button class="iconkey allon" id="allon" ${V.tracks.some(t => t.mute || t.solo) ? "" : "disabled"} title="Unmute and unsolo every track (0)">M/S off</button></div>` + V.tracks.map((_, i) => th(i)).join("") + (S.ws === "seq" ? `<div class="railparams"><div class="rphead"><span class="cap">Lock parameter</span><button id="clearLane" class="iconkey" aria-label="Clear ${S.lane} locks" title="Clear ${S.lane} locks"><svg viewBox="0 0 14 14" aria-hidden="true"><path d="M2 4h10M5.5 4V2.5h3V4M3.5 4l.7 8h5.6l.7-8M6 6.5v3.5M8 6.5v3.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg></button></div><div class="pgrid vert" id="chips"></div></div>` : "");
}
/* ===== LCD line 2: the workspace's own values ===== */
const L2 = (k, label, val, title, edit) => `<span class="l2 ${edit ? "ed" : ""}" ${edit ? `data-l2="${k}" role="button" tabindex="0"` : ""} title="${title || ""}"><small>${label}</small><b>${val}</b></span>`;
function renderSub() {
	const t = S.sel, tr = V.tracks[t]; let h = "";
	if (S.ws === "seq") h = L2("len", "LEN", V.length === V.len ? V.len : V.length + "/" + V.len, "Pattern length (total length " + V.len + "). Click, drag or scroll to step 16 / 32 / 48 / 64; with alt, the length inside it.", 1) + L2("mult", "SPD", V.mult, "Tempo multiplier: 1X / 2X / 3/4X / 3/2X. Click, drag or scroll.", 1)
		+ L2("swing", "SWG", V.swing + "%", "Swing 50–80 %. Click, drag or scroll.", 1) + L2("accAmt", "ACC", V.accAmt, "Accent 0–15. Click, drag or scroll.", 1) + L2("mode", "MODE", V.mode === "EXTENDED" ? "EXT" : "CLASSIC", "Classic or Extended. Locks only play in Extended. Click to switch.", 1)
		+ L2("dbl", "LEN", "×2", `Double the pattern: ${V.length} to ${V.length * 2} steps, the new half a copy of the steps and locks. Above 32 steps only in EXTENDED. One undo step.`, 1);
	else if (S.ws === "sound") h = L2("", "TRACK", String(t + 1).padStart(2, "0")) + L2("", "MACHINE", tr.m) + L2("", "", tr.name.toUpperCase());
	else if (S.ws === "mix") h = L2("", "PATH", "SEND›ECHO›GATE›EQ›DYN›MAIN", "Sends feed the master effects. Tracks on outputs A–F skip them.");
	else if (S.ws === "sampler") { const used = V.tracks.filter(t => machineFacts(t.m, Cat).family === "ROM").length; const b = smpBank(); h = L2("", "MEM", b ? Math.round(b.used / b.capacity * 100) + "%" : "n/a", b ? `Sample memory: the ROM slots hold ${b.used} of ${b.capacity} samples (${(b.used / 44100).toFixed(1)} of ${(b.capacity / 44100).toFixed(1)} s at 44.1 kHz); the four RAM buffers share the rest.` : canDo(V, "sampleAudio") ? "Reading the samples from the machine…" : NA.memory) + L2("", "KIT", used + " ROM", "Tracks in this kit that play a ROM slot") + L2("", "SLOT", S.smpSlot.replace(/^RAM/, "RAM ").replace(/^ROM/, "ROM ")); }
	else if (S.ws === "control") h = L2("", "IN", "MIDI LEARN") + L2("", "MAPS", (Docs.learn?.mappings || []).length);
	else h = L2("song", "SONG", String(V.songSlot + 1).padStart(2, "0"), "Song slot. Click, drag or scroll; shift-click for the previous (the machine loads it when stopped).", 1) + L2("", "ROWS", V.song.length) + L2("", "BARS", Math.round(songSteps() / 16)) + L2("", "TIME", songTime());
	/* 0.3.5, every workspace: PATTERN or SONG mode, as the machine reports it; a click switches (the Song page's switch) */
	h += L2("seqmode", "PLAY", V.songMode === true ? "SONG" : V.songMode === false ? "PAT" : "—", "What the machine plays: PAT (the pattern and its chain) or SONG (the song). Click to switch; it shows what the machine reports.", 1);
	$("#lcd2").innerHTML = h;
}
/* LCD line 2 editing: a click or the wheel steps a value (a drag of any of them: mdDeskGestures.js) */
function l2step(k, d, alt) {
	if (k === "len") { if (alt) cmd("length", { p: V.pat, v: ((V.length - 1 + d + V.len) % V.len) + 1 }); else { const o = [16, 32, 48, 64], v = o[(o.indexOf(V.len) + d + 4) % 4]; cmd("totalLength", { p: V.pat, v }, undefined, [[["len"], v]]); } }
	if (k === "song") { cmd("selectSong", { s: (V.songSlot + d + 32) % 32 }); return; }
	if (k === "seqmode") { cmd("seqMode", { song: V.songMode !== true }); return; }
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
document.addEventListener("click", e => { const el = e.target.closest(".l2.ed"); if (!el) return; const k = el.dataset.l2; if (k !== "swing" && k !== "accAmt") l2step(k, e.shiftKey ? -1 : 1, e.altKey); });
document.addEventListener("wheel", e => { const el = e.target.closest(".l2.ed"); if (!el) return; e.preventDefault(); if (el.dataset.l2 === "seqmode") return; l2step(el.dataset.l2, (e.deltaY || e.deltaX) < 0 ? 1 : -1, e.altKey); }, { passive: false });
/* B-030: an edit of the tempo while the DAW sets it is refused with a word (drag, arrows, tap) */
function hostTempoRefused() { if (!V.hostTempo) return false; toast("The DAW sets the tempo. Change it there."); return true; }
/* a refused followHost (the machine's global could not be set to follow the host): said once per reason, and logged */
let hostRefusedText = "";
function hostRefused(m) {
	const t = (m.errors || []).join(" · ") || "refused";
	Bridge.log("followHost refused: " + t);
	if (t === hostRefusedText) return;
	hostRefusedText = t; toast("The machine could not be set to follow the DAW's tempo: " + t);
}

/* ===== Song playhead (0.3.5): the song row the machine plays (telemetry songRow, RAM), shown without a render:
   the arrangement cell, the time bar up to it, the What plays line and the LCD's pattern slot ===== */
let songRowShown = -2;
function songRowNow() { const t = Docs.telemetry; return V.songMode === true && V.playing && t && t.songRow != null && t.songRow >= 0 ? t.songRow : -1; }
const nn3 = n => String(n + 1).padStart(3, "0");
const songRows = () => V.song.filter(r => r.type !== "end").length;
/* the What plays line: PATTERN B07 / SONG 01 · row 017 of 65 · B01 / CHAIN A03»A05 */
function playsText() {
	const plays = playsOf(Docs);
	if (plays.kind !== "song") return plays.label;
	const row = songRowNow(), r = row >= 0 ? V.song[row] : null;
	return `SONG ${String(V.songSlot + 1).padStart(2, "0")} · ` + (row >= 0 ? `row ${nn3(row)} of ${songRows()}${r && !r.type ? " · " + patName(r.pat) : ""}` : `${songRows()} rows · ${V.playing ? "…" : "stopped"}`);
}
/* the LCD's pattern slot reads SONG 01 · 017 in song mode */
function songLcd() {
	const p = $("#pat"), lab = p?.closest(".patf")?.querySelector("small"); if (!p) return;
	const song = V.songMode === true, row = songRowNow();
	if (lab) lab.textContent = song ? "Song" : "Pattern";
	if (song) p.textContent = String(V.songSlot + 1).padStart(2, "0") + (row >= 0 ? "·" + nn3(row) : "");
	p.closest(".patf")?.classList.toggle("songlcd", song);
}
function markSongRow(force) {
	/* the arrangement's header and frame follow the transport (no render on PLAY / STOP) */
	const st = document.getElementById("arrstate");
	if (st && st.textContent !== arrState()) st.textContent = arrState();
	document.querySelector(".songui")?.classList.toggle("songplaying", V.songMode === true && V.playing);
	const row = songRowNow();
	if (row === songRowShown && !force) return;
	songRowShown = row;
	songLcd();
	const pl = document.getElementById("songPlays"); if (pl) pl.textContent = playsText();
	if (S.ws !== "song") return;
	$$(".scell.ph").forEach(c => c.classList.remove("ph"));
	if (row >= 0) document.querySelector(`.scell[data-i="${row}"]`)?.classList.add("ph");
	$$(".durbar .db").forEach(d => { const i = +d.dataset.i; d.classList.toggle("past", row >= 0 && i < row); d.classList.toggle("now", i === row); });
}

/* Transport: the playhead comes from the machine (telemetry), moved by class only */
let lastStep = -1;
function onTelemetry(m) {
	Docs.telemetry = m;
	/* the transport is derived from the telemetry document (transportOf): a new view when it changed */
	const wasPlaying = V.playing, wasRec = V.rec, tp = transportOf(Docs);
	if (tp.playing !== wasPlaying || tp.rec !== wasRec) { Base = deriveView(Docs, S); V = view(); }
	if (V.rec !== wasRec) { renderTop(); if (V.rec) toast("Live recording: click a track's steps to play it, move a value to lock it."); }
	S.step = V.playing ? m.step : -1;
	markSongRow();
	if (wasPlaying !== V.playing) { renderTop(); $$(".ph").forEach(c => c.classList.remove("ph")); setPos(); phLast = -1; movePH(); }
	const prev = lastStep; lastStep = S.step;
	if (S.step === prev) return;
	/* Capture next loop (UW): the recorder track plays (records) for one whole loop from the next
	   wrap, then it is muted again, which keeps the take (Freeze). The mutes are the machine's. */
	if (prev >= 0 && S.step >= 0 && S.step < prev) for (const n in S.capture) {
		const r = recTrack(+n);
		if (S.capture[n] === "armed") { S.capture[n] = "rec"; if (r >= 0) userMute(r, false); }
		else { delete S.capture[n]; if (r >= 0) userMute(r, true); toast("RAM " + n + " captured and frozen."); if (S.ws === "sampler") render(); }
	}
	const pp = Math.floor(Math.max(0, S.step) / 16);
	if (S.ws === "mix" && S.step >= 0) V.tracks.forEach((t, i) => { if (t.trigs[S.step] && audible(i)) { const l = document.querySelector(`.act[data-act="${i}"]`); if (l) { l.classList.add("on"); setTimeout(() => l.classList.remove("on"), 90); } } });
	$$(".pl").forEach(b => b.classList.toggle("play", +b.dataset.plp === pp && V.playing));
	if (S.follow && (S.ws === "seq" || S.ws === "sampler") && !S.viewAll && pp !== S.page && !Held.as("lane") && V.playing) { S.page = pp; render(); }
	$("#tempoled").classList.toggle("on", V.playing && S.step % 4 === 0); setPos(); queueMicrotask(movePH); $("#playled")?.classList.toggle("on", V.playing && S.step % 4 === 0);
	/* B-014: the step cells are not marked (.st.ph draws nothing since the soft playhead, #phcol): marking a column of
	   16 cells every step made WebKit repaint them, with their glows, about half a core of its GPU process */
	$$(`.lb[data-s="${prev}"],[data-cp="${prev}"],[data-rc="${prev}"]`).forEach(c => c.classList.remove("ph"));
	if (V.playing) $$(`.lb[data-s="${S.step}"],[data-cp="${S.step}"],[data-rc="${S.step}"]`).forEach(c => c.classList.add("ph"));
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
function setPlate(v) { S.plate = v; document.documentElement.dataset.plate = v; try { localStorage.setItem("mddesk.plate", v); } catch (_) { } renderTop(); redraw(); }
(() => { let v = null; try { v = localStorage.getItem("mddesk.plate"); } catch (_) { } if (!v) v = matchMedia("(prefers-color-scheme: dark)").matches ? "mk2" : "mk1"; S.plate = v; document.documentElement.dataset.plate = v; })();

/* the top bar's clicks (the router's, mdDeskRender.js CLICKS): true when the click was theirs */
function clickEdit(e) {
	if (e.target.closest("#undo")) { cmd("undo"); return true; }
	if (e.target.closest("#redo")) { cmd("redo"); return true; }
	const sc = e.target.closest("[data-sec]"); if (sc) { if (e.altKey && sc.dataset.sec === "clear") clearPattern(); else secAction(sc.dataset.sec); return true; }
	/* #kitf and #pat open the kit library / pattern chooser (mdDeskLibrary.js). */
	if (e.target.closest("[data-reloadsong]")) { cmd("reloadSong"); return true; }
	if (e.target.closest("#learnkey")) { toggleLearn(); return true; }
	return false;
}
function clickTop(e) {
	const tb = e.target.closest("#tabs button"); if (tb) { if (tb.dataset.ws === "control" && !S.mapping) return true; S.ws = tb.dataset.ws; render(); return true; }
	if (e.target.closest("#platekey")) { setPlate(S.plate === "mk2" ? "mk1" : "mk2"); return true; }
	if (e.target.closest("#play")) { cmd(V.playing ? "stop" : "play"); return true; }
	if (e.target.closest("#rec")) { cmd("record"); return true; }
	if (e.target.closest("#patPrev")) { goPattern((V.queued ?? V.pat) - 1); return true; }
	if (e.target.closest("#patNext")) { goPattern((V.queued ?? V.pat) + 1); return true; }
	return false;
}
