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
const renderP0 = render; render = function () { renderP0(); showPrep(); };

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
	const x = c.getContext("2d"), cs = getComputedStyle(document.documentElement);
	x.fillStyle = cs.getPropertyValue("--lcd"); x.fillRect(0, 0, 128, 64); x.fillStyle = cs.getPropertyValue("--ink");
	const b = fwLcd.bits;
	for (let y = 0; y < 64; y++) for (let i = 0; i < 128; i++) if (b[y * 16 + (i >> 3)] & (0x80 >> (i & 7))) x.fillRect(i, y, 1, 1);
}
function showFwLcd(on) {
	const p = $(".lcdpanel"); if (!p || on === fwLcd.shown) return;
	fwLcd.shown = on;
	if (on) { p.classList.remove("fwfade"); p.classList.add("fwboot"); return; }
	p.classList.remove("fwboot"); p.classList.add("fwfade"); setTimeout(() => p.classList.remove("fwfade"), 700);
}
Bridge.onMessage(m => {
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
