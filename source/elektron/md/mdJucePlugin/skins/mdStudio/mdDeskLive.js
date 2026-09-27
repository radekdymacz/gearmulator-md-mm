"use strict";
/* P4 live controls on the machine (mockup v52-v54): mutes on the track keys, the pattern chain and the
   firmware's own LCD while it boots. Loaded after mdDeskApp.js; uses its state (S), its
   render functions and its commands (cmd, setMute). What the page shows is the machine's:
   - mutes: machine.desk.mutes, read from RAM (mutesSource "memory"), whoever set them;
   - chain: machine.desk.chain, read from the firmware's own chain (active, patterns, next);
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
	for (const [i, m] of ch) if (S.tracks[i] && S.tracks[i].mute !== m) muteSet(i, m);
	PREP.clear(); refreshAudible(); showPrep(); lcdSay(ch);
}
function prepToggle(i) { if (!S.tracks[i]) return; if (PREP.has(i)) PREP.delete(i); else PREP.set(i, !S.tracks[i].mute); showPrep(); }
document.addEventListener("click", e => { const b = e.target.closest(".ms.m[data-mute]"); if (!b || !e.shiftKey) return; e.stopImmediatePropagation(); e.preventDefault(); prepToggle(+b.dataset.mute); }, true);
document.addEventListener("keyup", e => { if (e.key === "Shift") applyPrep(); });
const MKEYS = ["Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI"];
document.addEventListener("keydown", e => {
	if (!e.altKey || e.metaKey || e.ctrlKey || e.target.closest?.("input,select,textarea")) return;
	const i = MKEYS.indexOf(e.code); if (i < 0 || !S.tracks[i]) return;
	e.preventDefault(); e.stopImmediatePropagation();
	if (e.shiftKey) { prepToggle(i); return; }
	const on = !S.tracks[i].mute; muteSet(i, on); refreshAudible(); lcdSay([[i, on]]);
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
document.addEventListener("keydown", e => {
	if ((e.key !== "t" && e.key !== "T") || e.metaKey || e.ctrlKey || e.altKey || e.target.closest?.("input,select,textarea")) return;
	const now = performance.now(); if (TAP.length && now - TAP[TAP.length - 1] > 2000) TAP.length = 0;
	TAP.push(now); if (TAP.length > 5) TAP.shift();
	if (TAP.length >= 2) { const bpm = clamp(Math.round(60000 / ((TAP[TAP.length - 1] - TAP[0]) / (TAP.length - 1)) * 10) / 10, 30, 300); S.bpm = bpm; renderTop(); cmd("tempo", { bpm }, "tempo"); toast("Tap tempo: " + bpm.toFixed(1) + " BPM"); }
	else toast("Tap tempo: keep tapping T");
	e.preventDefault();
});

/* ===== Parameter tweaking (manual p.37, FUNCTION + a DATA ENTRY knob): Alt held while moving a track
   value moves the same knob on every track by the same amount. As on the machine, RAM machines, MIDI and
   CTR tracks are left out, and a value that hits 0 or 127 does not come back symmetrically. ===== */
let tweak = null;
document.addEventListener("pointerdown", e => { const el = e.target.closest?.("#main .pc[data-g],#main .fader[data-g]"); tweak = el && e.altKey && ["syn", "fx", "rt"].includes(el.dataset.g) ? { el, t: el.dataset.t != null ? +el.dataset.t : S.sel } : null; }, true);
document.addEventListener("pointerup", () => { if (tweak) { tweak = null; } }, true);
const skipTweak = m => /^(RAM|MID|CTR)/.test(m);
const setV0 = setV;
setV = function (el, v) {
	if (!tweak || tweak.el !== el) return setV0(el, v);
	const g = el.dataset.g, n = el.dataset.n, t0 = tweak.t, before = S.tracks[t0][g][n];
	setV0(el, v);
	const delta = S.tracks[t0][g][n] - before; if (!delta) return;
	const slot = g === "syn" ? pages(S.tracks[t0].m).s.indexOf(n) : -1;
	S.tracks.forEach((tr, t) => {
		if (t === t0 || skipTweak(tr.m)) return;
		const name = g === "syn" ? pages(tr.m).s[slot] : n;	/* synthesis: the same knob, whatever it is on that machine */
		if (!name || !(name in tr[g])) return;
		tr[g][name] = clamp(tr[g][name] + delta);
	});
	syncKitValues(); syncControls();
};

/* ===== The editor's menu (skins, GUI scale, settings): right-click an empty part of the header
   (P4; the standalone also has it in the native menu bar). ===== */
document.addEventListener("contextmenu", e => {
	if (!e.target.closest(".top") || e.target.closest("button,[role=slider],[role=button],select,input,b,.lcdpanel")) return;
	e.preventDefault(); Bridge.send({ op: "openMenu" });
});

/* ===== Pattern chain (manual p.37): the machine's own, made with its keys ===== */
S.chainDraft = [];
function chainDoc() { const d = machineState().desk || {}; return d.chain || null; }
function chainCard() {
	const c = chainDoc(), b = S.bank, bn = "ABCDEFGH"[b], d = S.chainDraft.filter(p => p >> 4 === b);
	S.chainDraft = d;
	const known = !!c, active = known && c.active && c.patterns.length > 0;
	const pads = Array.from({ length: 16 }, (_, k) => { const p = b * 16 + k, n = d.indexOf(p); return `<button class="chk${hasPat(p) ? "" : " empty"}${n >= 0 ? " in" : ""}" data-chainpad="${p}" title="${patName(p)}${n >= 0 ? ": number " + (n + 1) + " in the chain. Click removes it." : ". Click adds it to the chain."}">${patName(p)}<small>${hasPat(p) ? patLen(p) : "empty"}</small>${n >= 0 ? `<em>${n + 1}</em>` : ""}</button>`; }).join("");
	const playing = S.pat, list = active ? c.patterns : [], at = list.indexOf(playing), next = active ? list[(at + 1) % list.length] : null;
	const live = !known ? `<span class="note">The chain is not readable on this firmware.</span>` : active ? list.map(p => `<span class="lcdchip${p === playing ? " now" : ""}${S.playing && p === next && at >= 0 ? " nx" : ""}">${patName(p)}</span>`).join("<i>»</i>") + "<i>↺</i>"
		: `<span class="note">No chain. The machine plays ${patName(playing)} and stays on it.</span>`;
	return `<section class="card"><header><h3>Chain</h3><span>bank ${bn} · loops · BANK + TRIGs on the machine</span></header>
  <div class="chainp">${pads}</div>
  <div class="irow"><span class="ilab">Plays</span><div class="chainrow">${live}</div></div>
  <div class="irow"><span class="ilab"></span><span class="chainacts"><button class="cream" data-chain="send"${d.length < 2 || !known ? " disabled" : ""} title="Holds BANK ${bn} and presses the TRIG keys in this order on the machine. ${S.playing ? "It starts at the pattern end." : "PLAY starts at the first one."}">Chain ${d.length ? d.length : ""}</button><button data-chain="undo"${d.length ? "" : " disabled"}>Back</button><button class="danger" data-chain="clear"${active ? "" : " disabled"} title="LOAD PATTERN of the current pattern: the machine's way to end a chain">Clear</button></span>
  <span class="note">One bank, each pattern once. Picking a pattern ends the chain; editing its patterns does not.</span></div></section>`;
}
document.addEventListener("click", e => {
	const p = e.target.closest("[data-chainpad]");
	if (p) { const n = +p.dataset.chainpad, i = S.chainDraft.indexOf(n); if (i >= 0) S.chainDraft.splice(i, 1); else if (S.chainDraft.length < 16) S.chainDraft.push(n); render(); return; }
	const a = e.target.closest("[data-chain]"); if (!a || a.disabled) return;
	if (a.dataset.chain === "send") cmd("chain", { patterns: S.chainDraft.slice() });
	else if (a.dataset.chain === "undo") { S.chainDraft.pop(); render(); }
	else if (a.dataset.chain === "clear") cmd("chainClear");
});
const renderSong0 = renderSong;
renderSong = function () { renderSong0(); const left = document.querySelector(".songleft"); if (left) left.insertAdjacentHTML("beforeend", chainCard()); };

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
Bridge.onMessage(m => {
	/* The engine changed (emulator <-> HW MIDI): the documents start over. */
	if (m.type === "reset") { Docs.patterns = {}; Docs.kits = {}; Docs.songs = {}; Docs.global = null; Docs.machine = null; S.loaded = false; scheduleRender(); return; }
	if (m.type === "lcd") {
		if (m.bits) { const s = atob(m.bits); fwLcd.bits = Uint8Array.from(s, ch => ch.charCodeAt(0)); drawFwLcd(); }
		const fw = (machineState().desk || {}).firmware;
		showFwLcd(!!fwLcd.bits && fw !== "ready" && fw !== "missing" && fw !== "unsupported");
	}
	else if (m.type === "machine") {
		const fw = m.doc.desk && m.doc.desk.firmware;
		if (fw === "ready" || fw === "missing" || fw === "unsupported") showFwLcd(false);
		/* BOOTING OS lasts until keys work: the firmware answers MIDI early, but its start-up
		   animation ignores panel keys until it is over (about 13 s). */
		if (m.doc.desk && m.doc.desk.boot === "animation") { const b = $(".lcdeng"); if (b) b.title = "Engine: MD OS 1.63 answers, but its start-up animation ignores keys until it ends (shown in the LCD). Editing starts then."; }
		if (S.ws === "song" && m.doc.desk && JSON.stringify(m.doc.desk.chain) !== chainCard.last) { chainCard.last = JSON.stringify(m.doc.desk.chain); scheduleRender(); }
	}
	else if (m.type === "ask" && m.ask === "breakChain") {
		const c = chainDoc(), list = c && c.patterns ? c.patterns.map(patName).join(" » ") : "";
		ask(`Picking <b>${patName(m.p)}</b> ends the chain <b>${list}</b>, as on the machine.`, [["Pick it, end the chain", "danger", () => cmd("select", { p: m.p, chainOk: true })], ["Cancel", "", () => { }]]);
	}
});

/* GEARMULATOR_MDSTUDIO_SELFTEST=p4 (?selftest=p4): the P4 checks in the plug-in, through the page's
   own controls, logged with "P4:" (the log file of mdStudioEditor). */
if (/[?&]selftest=p4(&|$)/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const log = t => Bridge.log("P4: " + t);
	const t0 = performance.now(), desk = () => machineState().desk || {};
	const until = async (f, ms) => { const end = performance.now() + ms; while (performance.now() < end) { if (f()) return performance.now(); await sleep(20); } return -1; };
	let frames = 0, firstLcd = -1, bootSeen = false;
	Bridge.onMessage(m => { if (m.type === "lcd" && m.bits) { frames++; if (firstLcd < 0) firstLcd = performance.now() - t0; } if (m.type === "machine" && m.doc.desk && m.doc.desk.boot === "animation") bootSeen = true; });
	const ready = await until(() => desk().firmware === "ready" && S.loaded, 90000);
	log(`boot: firmware LCD frames ${frames} (first at ${Math.round(firstLcd)} ms), animation state seen ${bootSeen}, input ready at ${Math.round(ready - t0)} ms, LCD mirror shown now ${$(".lcdpanel").classList.contains("fwboot")}`);
	if (ready < 0) { log("FAIL: not ready"); return; }
	await sleep(1500);
	/* The first key after ready is taken. */
	let p0 = performance.now(); $("#play").click();
	let p1 = await until(() => desk().playing, 3000);
	log(`first PLAY after ready: ${p1 >= 0 ? "ok plays" : "FAIL"} ${Math.round(p1 - p0)} ms`);
	$("#play").click(); await until(() => !desk().playing, 3000); await sleep(400);
	/* Mutes: a rail M key, then two prepared with Shift, applied on release. */
	S.ws = "seq"; render(); await sleep(200);
	const has = t => (desk().mutes || []).includes(t);
	document.querySelector('.ms.m[data-mute="3"]').click();
	p1 = await until(() => has(3), 2000);
	log(`M key track 4 -> machine mute (RAM): ${p1 >= 0 ? "ok" : "FAIL"}, source ${desk().mutesSource}`);
	const ev = (el, sh) => el.dispatchEvent(new MouseEvent("click", { bubbles: true, shiftKey: sh }));
	ev(document.querySelector('.ms.m[data-mute="3"]'), true); ev(document.querySelector('.ms.m[data-mute="6"]'), true);
	const prep = [...document.querySelectorAll(".ms.m.prep")].map(b => (+b.dataset.mute + 1) + b.dataset.prep).join(" ");
	await sleep(300);
	const held = !has(6) && has(3);
	document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift" }));
	p1 = await until(() => has(6) && !has(3), 2000);
	log(`Shift-prepared ${prep}: nothing sent while held ${held ? "ok" : "FAIL"}, applied together on release ${p1 >= 0 ? "ok" : "FAIL"}`);
	document.querySelector('.ms.m[data-mute="6"]').click(); await until(() => !(desk().mutes || []).length, 2000);
	/* Chain: A02 A03 A04 in Song, then PLAY; the machine's chain and the order it plays. */
	S.ws = "song"; S.bank = 0; render(); await sleep(200);
	[1, 2, 3].forEach(p => document.querySelector(`[data-chainpad="${p}"]`).click());
	document.querySelector('[data-chain="send"]').click();
	p1 = await until(() => desk().chain && desk().chain.active && desk().chain.patterns.join() === "1,2,3", 3000);
	log(`chain A02 A03 A04 -> firmware chain ${p1 >= 0 ? "ok" : "FAIL"} (${JSON.stringify(desk().chain)})`);
	$("#play").click(); await until(() => desk().playing, 3000);
	const seen = []; let last = -1;
	await until(() => { const p = Tele.pattern; if (p !== last && p >= 0) { seen.push(p); last = p; } return seen.length >= 5; }, 20000);
	render(); log(`plays ${seen.map(patName).join(" ")}; chain card "${document.querySelector(".chainrow")?.textContent}"`);
	goPattern(0); await sleep(300);
	log(`picking A01 while chained asks first: ${!$("#dlg").hidden ? "ok" : "FAIL"} "${$("#dlg p")?.textContent || ""}"`);
	$("#dlg").querySelector('[data-dlg="0"]')?.click();
	p1 = await until(() => desk().chain && !desk().chain.active, 3000);
	log(`then the chain is gone: ${p1 >= 0 ? "ok" : "FAIL"}`);
	$("#play").click(); await until(() => !desk().playing, 3000);
	/* v51: the lock lane lines up with the steps, 16 and ALL. */
	S.ws = "seq"; S.viewAll = false; render(); await sleep(300);
	const off = () => { let worst = 0; for (const s of [0, 5, 15, 16, 31]) { const a = document.querySelector(`.st[data-t="0"][data-s="${s}"]`), b = document.querySelector(`.lb[data-s="${s}"]`); if (a && b) worst = Math.max(worst, Math.abs(a.getBoundingClientRect().left - b.getBoundingClientRect().left)); } return Math.round(worst * 10) / 10; };
	const o16 = off(); S.viewAll = true; render(); await sleep(300);
	const oAll = off(), sc = $("#seqscroll"); const scroll = sc ? sc.scrollWidth - sc.clientWidth : -1;
	S.viewAll = false; render();
	log(`lane vs steps: 16 view ${o16} px, ALL ${oAll} px, ALL horizontal scroll ${scroll} px; header ${document.querySelector(".top").scrollWidth} px in ${innerWidth} px`);
	/* Sampler: a RAM slot the kit does not use is one call to action, undoable. */
	const kitDoc = () => Docs.kits[currentKitSlot()];
	const n = [1, 2, 3, 4].find(k => !S.tracks.some(t => t.m === "RAM-R" + k));
	if (n) {
		S.ws = "sampler"; S.smpSlot = "RAM" + n; render(); await sleep(200);
		log(`sampler RAM ${n} empty state: "${(document.querySelector(".smpsetup")?.innerText || "none").replace(/\s+/g, " ").slice(0, 260)}"`);
		const [rt, pt] = setupTracks(), trigsBefore = S.tracks[rt].trigs.filter(Boolean).length, mBefore = [kitDoc().tracks[rt].machine, kitDoc().tracks[pt].machine];
		document.querySelector("[data-setupgo]").click();
		p1 = await until(() => kitDoc().tracks[rt].machine === "RAM-R" + n && kitDoc().tracks[pt].machine === "RAM-P" + n, 4000);
		await sleep(600); render(); await sleep(200);
		log(`set up sampling: ${p1 >= 0 ? "ok" : "FAIL"} track ${rt + 1} ${kitDoc().tracks[rt].machine}, track ${pt + 1} ${kitDoc().tracks[pt].machine}; trigs ${trigsBefore} -> ${S.tracks[rt].trigs.filter(Boolean).length}; shows Live/Freeze/Capture ${!!document.querySelector("[data-capture]")}, source keys ${document.querySelectorAll("[data-recsrc]").length}, chop grid ${!!document.querySelector("#chop")}`);
		cmd("undo");
		p1 = await until(() => kitDoc().tracks[rt].machine === mBefore[0] && kitDoc().tracks[pt].machine === mBefore[1], 4000);
		log(`one Undo restores ${mBefore.join(" + ")}: ${p1 >= 0 ? "ok" : "FAIL"}`);
	}
	/* Kit library and pattern chooser: 64 kits and 128 patterns from the machine, a paste into K64, undo. */
	S.ws = "seq"; render();
	await until(() => Object.keys(Docs.kits).length === 64, 30000);
	$("#kitf").click(); await sleep(300);
	const cells = document.querySelectorAll("#libpop .ks").length, named = [...document.querySelectorAll("#libpop .ks .lsn")].filter(e => e.textContent !== "…").length;
	log(`kit library: ${cells} slots, ${named} read from the machine; open ${!$("#libpop").hidden}`);
	const src = currentKitSlot(), before63 = JSON.stringify(Docs.kits[63]);
	LIB.sel = src; libAct("copy"); await sleep(200);
	LIB.sel = 63; drawLib(true); libAct("paste"); await sleep(200);
	$("#dlg").querySelector('[data-dlg="0"]')?.click();
	p1 = await until(() => Docs.kits[63] && Docs.kits[63].name === Docs.kits[src].name && JSON.stringify(Docs.kits[63].tracks.map(t => t.machine)) === JSON.stringify(Docs.kits[src].tracks.map(t => t.machine)), 4000);
	log(`paste K${nn(src)} into K64: ${p1 >= 0 ? "ok" : "FAIL"} (the machine's read-back of K64)`);
	cmd("undo"); p1 = await until(() => JSON.stringify(Docs.kits[63]) === before63, 4000);
	log(`undo restores K64: ${p1 >= 0 ? "ok" : "FAIL"}`);
	closeLib(false); $("#pat").click(); await sleep(300);
	log(`pattern chooser: ${document.querySelectorAll("#libpop .ps").length} slots, ${[...document.querySelectorAll("#libpop .ps span")].filter(e => e.textContent !== "…").length} read; LCD ${Math.round(document.querySelector(".lcdpanel").getBoundingClientRect().width)} px`);
	closeLib(false);
	await p4Mix(log, sleep);
	log("done");
})();

/* ?selftest=p4set: change the editor's setup (an app LFO, knob row 1 on CC 40) so that a restart shows
   whether it came back with the plug-in state (the editor logs "setup restored"). */
if (/[?&]selftest=p4set/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	while (!(machineState().desk && machineState().desk.firmware === "ready")) await sleep(200);
	await sleep(500);
	Mods.doc.sources = []; Mods.doc.links = [];
	const id = Mods.add("lfo"); Mods.link(id, 2, 16); sendMods();
	KNOB_CCS[0] = 40; saveKnobs();
	await sleep(800);
	Bridge.log(`P4: setup set: ${Mods.doc.sources.length} app source, knob 1 CC ${KNOB_CCS[0]}`);
})();
if (/[?&]selftest=p4get/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	await sleep(4000);
	Bridge.log(`P4: page setup: ${Mods.doc.sources.length} app source(s) ${Mods.doc.sources.map(s => s.label + " -> " + Mods.linksOf(s.id).map(o => "T" + (o.l.track + 1) + " p" + o.l.param).join()).join("; ")}, knob CCs ${KNOB_CCS.join(" ")}`);
})();

/* ?selftest=p4hw: the engine menu's HW MIDI in the plug-in (no Machinedrum is connected here): the label
   follows the link, the editor's traffic goes to the plug-in's MIDI out, and EMU brings the emulator back. */
if (/[?&]selftest=p4hw/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const log = t => Bridge.log("P4: " + t);
	const label = () => $(".lcdeng span")?.textContent;
	while (!(machineState().desk && machineState().desk.firmware === "ready" && S.loaded)) await sleep(200);
	await sleep(1000);
	log(`start: ${label()}`);
	const sel = $("#engsel"); sel.value = "hw"; sel.dispatchEvent(new Event("change", { bubbles: true }));
	await sleep(1200);
	log(`HW MIDI chosen: engine label ${label()}, link ${machineState().desk?.link}, REC disabled ${$("#rec").disabled}`);
	await sleep(6000);
	log(`nothing answers after 7 s: ${label()} (link ${machineState().desk?.link})`);
	cmd("select", { p: 3 }); await sleep(300);
	sel.value = "emu"; sel.dispatchEvent(new Event("change", { bubbles: true }));
	let t0 = performance.now(); while (!(machineState().desk && machineState().desk.engine === "emu" && machineState().desk.firmware === "ready" && S.loaded) && performance.now() - t0 < 15000) await sleep(100);
	log(`EMU again: ${label()} after ${Math.round(performance.now() - t0)} ms, pattern ${patName(S.pat)}`);
	log("hw done");
})();

/* The Mix checks (v60): also alone with ?selftest=p4mix. */
async function p4Mix(log, sleep) {
	/* Mix (v60 fixes): a dragged fader keeps its geometry, a value box follows a sideways drag, DEL/REV on OUT A say MAIN only. */
	S.ws = "mix"; render(); await sleep(300);
	const strip = () => [...document.querySelectorAll(".strip")][6];
	const fd = strip().querySelector(".fader"), fb = fd.getBoundingClientRect(), fx = fb.left + fb.width / 2, fy = fb.top + fb.height / 2, sh = strip().getBoundingClientRect().height;
	const pe = (el, t, x, y) => el.dispatchEvent(new PointerEvent(t, { bubbles: true, clientX: x, clientY: y, pointerId: 7, buttons: 1, pointerType: "mouse" }));
	const vol0 = S.tracks[6].rt.VOL; pe(fd, "pointerdown", fx, fy); for (let i = 1; i <= 6; i++) { pe(fd, "pointermove", fx, fy + i * 4); await sleep(30); }
	const midF = fd.getBoundingClientRect().height, midS = strip().getBoundingClientRect().height; pe(fd, "pointerup", fx, fy + 24); await sleep(300);
	log(`Mix fader drag: fader ${fb.height}->${midF} px, strip ${sh}->${midS} px while dragging, VOL ${vol0} -> ${S.tracks[6].rt.VOL}: ${midF === fb.height && midS === sh ? "ok geometry fixed" : "FAIL"}`);
	const ds = strip().querySelector('.pc[data-n="DIST"]'), db = ds.getBoundingClientRect(), dist0 = S.tracks[6].rt.DIST;
	const sent = [], send0 = Bridge.send; Bridge.send = (m, o) => { if (m.op === "param") sent.push(m.t + ":" + m.i + "=" + m.v); return send0(m, o); };
	let seen = 0, lastX = null; const probe = e => { seen++; lastX = e.clientX; }; main.addEventListener("pointermove", probe);
	pe(ds, "pointerdown", db.left + 10, db.top + 10); log(`  drag after pointerdown: ${drag ? "set, vert " + drag.vert + ", v " + drag.v : "none"}`); for (let i = 1; i <= 5; i++) { pe(ds, "pointermove", db.left + 10 + i * 4, db.top + 10); await sleep(30); }
	const midD = ds.getBoundingClientRect().width, midV = S.tracks[6].rt.DIST; main.removeEventListener("pointermove", probe); log(`  moves seen ${seen}, last clientX ${lastX} (down at ${db.left + 10}, drag.x ${drag && drag.x}), drag ${drag ? "still set" : "gone"}, tweak ${!!tweak}, sent ${sent.join(" ")}`); Bridge.send = send0; pe(ds, "pointerup", db.left + 30, db.top + 10); await sleep(300);
	log(`DIST sideways drag: ${dist0} -> ${midV} while dragging, ${S.tracks[6].rt.DIST} after, box ${db.width}->${midD} px: ${S.tracks[6].rt.DIST !== dist0 && midD === db.width ? "ok" : "FAIL"}`);
	cmd("param", { k: S.kit, t: 6, i: 16, v: dist0 }); cmd("param", { k: S.kit, t: 6, i: 17, v: vol0 }); await sleep(200);
	log(`fit: page zoom ${Math.round(innerWidth / 1440 * 1000) / 1000 || 1}, page ${document.documentElement.scrollWidth} px wide in ${innerWidth} px, header right edge ${Math.round(document.querySelector(".rightgrp").getBoundingClientRect().right)} px`);
}
if (/[?&]selftest=p4mix/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const log = t => Bridge.log("P4: " + t);
	while (!(machineState().desk && machineState().desk.firmware === "ready" && S.loaded)) await sleep(200);
	await sleep(1500);
	await p4Mix(log, sleep);
	log("mix done");
})();
