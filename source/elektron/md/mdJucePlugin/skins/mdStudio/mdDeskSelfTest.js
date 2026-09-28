"use strict";
/* The Machinedrum Editor's self-tests (P6): diagnostics only. This bundle is in the plug-in only
   when it is built with gearmulator_MDMM_DIAGNOSTICS (a test or development build); a release
   page has none of it. The plug-in starts one with GEARMULATOR_MDSTUDIO_SELFTEST=<kind>, which
   the page sees as ?selftest=<kind>. Each test drives the page's own controls and commands and
   logs to the plug-in's log (Bridge.log). */

/* the firmware runs and the editor takes input (its start-up animation is over): machine.input */
const runs = () => !!machineState().input;
/* A command that asks: the page shows the plug-in's question (mdDeskApp.js, onAsk); the tests answer
   it through that dialog, as the user does (its first key is the confirm). True when one came. */
async function answerAsk(ms = 1500) {
	const end = performance.now() + ms;
	while (performance.now() < end) {
		const b = !$("#dlg").hidden && $("#dlg").dataset.first !== "1" && $('#dlg [data-dlg="0"]');
		if (b) { b.click(); return true; }
		await new Promise(r => setTimeout(r, 20));
	}
	return false;
}

/* ?selftest=1 or p4: results of the transport and live-recording commands, and every refusal */
if (/[?&]selftest=(1|p4)/.test(location.search)) Bridge.onMessage(r => {
	if (r.type === "result" && (r.op === "record" || r.op === "recTrig" || r.op === "play" || r.op === "stop" || !r.ok))
		Bridge.log("result " + r.op + " ok " + r.ok + " " + (r.errors || []).join(";") + " " + (r.note || ""));
});
/* any self-test: when the engine label changes */
if (/[?&]selftest=/.test(location.search)) (() => {
	const span = document.querySelector(".lcdeng span");
	let shown = "";
	if (span) new MutationObserver(() => { if (span.textContent !== shown) { shown = span.textContent; Bridge.log(`engine: ${shown} at ${Math.round(performance.now())} ms`); } })
		.observe(span, { childList: true, characterData: true, subtree: true });
})();

/* GEARMULATOR_MDSTUDIO_SELFTEST=p4 (?selftest=p4): the P4 checks in the plug-in, through the page's
   own controls, logged with "P4:" (the log file of mdStudioEditor). */
if (/[?&]selftest=p4(&|$)/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const log = t => Bridge.log("P4: " + t);
	const t0 = performance.now(), desk = () => machineState().desk || {};
	const until = async (f, ms) => { const end = performance.now() + ms; while (performance.now() < end) { if (f()) return performance.now(); await sleep(20); } return -1; };
	let frames = 0, firstLcd = -1, bootSeen = false;
	Bridge.onMessage(m => { if (m.type === "lcd" && m.bits) { frames++; if (firstLcd < 0) firstLcd = performance.now() - t0; } if (m.type === "machine" && m.doc.lifecycle === "animating") bootSeen = true; });
	const ready = await until(() => runs() && V.loaded, 90000);
	log(`boot: firmware LCD frames ${frames} (first at ${Math.round(firstLcd)} ms), animation state seen ${bootSeen}, input ready at ${Math.round(ready - t0)} ms, LCD mirror shown now ${$(".lcdpanel").classList.contains("fwboot")}`);
	if (ready < 0) { log("FAIL: not ready"); return; }
	await sleep(1500);
	/* The first key after ready is taken. */
	let p0 = performance.now(); $("#play").click();
	let p1 = await until(() => V.playing, 3000);
	log(`first PLAY after ready: ${p1 >= 0 ? "ok plays" : "FAIL"} ${Math.round(p1 - p0)} ms`);
	$("#play").click(); await until(() => !V.playing, 3000); await sleep(400);
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
	$("#play").click(); await until(() => V.playing, 3000);
	const seen = []; let last = -1;
	await until(() => { const p = Docs.telemetry ? Docs.telemetry.pattern : -1; if (p !== last && p >= 0) { seen.push(p); last = p; } return seen.length >= 5; }, 20000);
	render(); log(`plays ${seen.map(patName).join(" ")}; chain card "${document.querySelector(".chainrow")?.textContent}"`);
	goPattern(0); await sleep(300);
	await until(() => !$("#dlg").hidden, 1500);
	log(`picking A01 while chained asks first: ${!$("#dlg").hidden ? "ok" : "FAIL"} "${$("#dlg p")?.textContent || ""}"`);
	await answerAsk();
	p1 = await until(() => desk().chain && !desk().chain.active, 3000);
	log(`then the chain is gone: ${p1 >= 0 ? "ok" : "FAIL"}`);
	$("#play").click(); await until(() => !V.playing, 3000);
	/* v51: the lock lane lines up with the steps, 16 and ALL. */
	S.ws = "seq"; S.viewAll = false; render(); await sleep(300);
	const off = () => { let worst = 0; for (const s of [0, 5, 15, 16, 31]) { const a = document.querySelector(`.st[data-t="0"][data-s="${s}"]`), b = document.querySelector(`.lb[data-s="${s}"]`); if (a && b) worst = Math.max(worst, Math.abs(a.getBoundingClientRect().left - b.getBoundingClientRect().left)); } return Math.round(worst * 10) / 10; };
	const o16 = off(); S.viewAll = true; render(); await sleep(300);
	const oAll = off(), sc = $("#seqscroll"); const scroll = sc ? sc.scrollWidth - sc.clientWidth : -1;
	S.viewAll = false; render();
	log(`lane vs steps: 16 view ${o16} px, ALL ${oAll} px, ALL horizontal scroll ${scroll} px; header ${document.querySelector(".top").scrollWidth} px in ${innerWidth} px`);
	/* Sampler: a RAM slot the kit does not use is one call to action, undoable. */
	const kitDoc = () => kitDocOf(Docs);
	const n = [1, 2, 3, 4].find(k => !V.tracks.some(t => t.m === "RAM-R" + k));
	if (n) {
		S.ws = "sampler"; S.smpSlot = "RAM" + n; render(); await sleep(200);
		log(`sampler RAM ${n} empty state: "${(document.querySelector(".smpsetup")?.innerText || "none").replace(/\s+/g, " ").slice(0, 260)}"`);
		const [rt, pt] = setupTracks(), trigsBefore = V.tracks[rt].trigs.filter(Boolean).length, mBefore = [kitDoc().tracks[rt].machine, kitDoc().tracks[pt].machine];
		document.querySelector("[data-setupgo]").click();
		p1 = await until(() => kitDoc().tracks[rt].machine === "RAM-R" + n && kitDoc().tracks[pt].machine === "RAM-P" + n, 4000);
		await sleep(600); render(); await sleep(200);
		log(`set up sampling: ${p1 >= 0 ? "ok" : "FAIL"} track ${rt + 1} ${kitDoc().tracks[rt].machine}, track ${pt + 1} ${kitDoc().tracks[pt].machine}; trigs ${trigsBefore} -> ${V.tracks[rt].trigs.filter(Boolean).length}; shows Live/Freeze/Capture ${!!document.querySelector("[data-capture]")}, source keys ${document.querySelectorAll("[data-recsrc]").length}, chop grid ${!!document.querySelector("#chop")}`);
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
	LIB.sel = 63; drawLib(true); libAct("paste"); await answerAsk();
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
	while (!(runs())) await sleep(200);
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
	while (!(runs() && V.loaded)) await sleep(200);
	await sleep(1000);
	log(`start: ${label()}`);
	const sel = $("#engsel"); sel.value = "hw"; sel.dispatchEvent(new Event("change", { bubbles: true }));
	await sleep(1200);
	log(`HW MIDI chosen: engine label ${label()}, lifecycle ${machineState().lifecycle}, REC disabled ${$("#rec").disabled}`);
	await sleep(6000);
	log(`nothing answers after 7 s: ${label()} (lifecycle ${machineState().lifecycle})`);
	cmd("select", { p: 3 }); await sleep(300);
	sel.value = "emu"; sel.dispatchEvent(new Event("change", { bubbles: true }));
	let t0 = performance.now(); while (!((machineState().capabilities || {}).engine === "emu" && runs() && V.loaded) && performance.now() - t0 < 15000) await sleep(100);
	log(`EMU again: ${label()} after ${Math.round(performance.now() - t0)} ms, pattern ${patName(V.pat)}`);
	log("hw done");
})();

/* The Mix checks (v60): also alone with ?selftest=p4mix. */
async function p4Mix(log, sleep) {
	/* Mix (v60 fixes): a dragged fader keeps its geometry, a value box follows a sideways drag, DEL/REV on OUT A say MAIN only. */
	S.ws = "mix"; render(); await sleep(300);
	const strip = () => [...document.querySelectorAll(".strip")][6];
	const fd = strip().querySelector(".fader"), fb = fd.getBoundingClientRect(), fx = fb.left + fb.width / 2, fy = fb.top + fb.height / 2, sh = strip().getBoundingClientRect().height;
	const pe = (el, t, x, y) => el.dispatchEvent(new PointerEvent(t, { bubbles: true, clientX: x, clientY: y, pointerId: 7, buttons: 1, pointerType: "mouse" }));
	const vol0 = V.tracks[6].rt.VOL; pe(fd, "pointerdown", fx, fy); for (let i = 1; i <= 6; i++) { pe(fd, "pointermove", fx, fy + i * 4); await sleep(30); }
	const midF = fd.getBoundingClientRect().height, midS = strip().getBoundingClientRect().height; pe(fd, "pointerup", fx, fy + 24); await sleep(300);
	log(`Mix fader drag: fader ${fb.height}->${midF} px, strip ${sh}->${midS} px while dragging, VOL ${vol0} -> ${V.tracks[6].rt.VOL}: ${midF === fb.height && midS === sh ? "ok geometry fixed" : "FAIL"}`);
	const ds = strip().querySelector('.pc[data-n="DIST"]'), db = ds.getBoundingClientRect(), dist0 = V.tracks[6].rt.DIST;
	const sent = [], send0 = Bridge.send; Bridge.send = (m, o) => { if (m.op === "param") sent.push(m.t + ":" + m.i + "=" + m.v); return send0(m, o); };
	let seen = 0, lastX = null; const probe = e => { seen++; lastX = e.clientX; }; main.addEventListener("pointermove", probe);
	pe(ds, "pointerdown", db.left + 10, db.top + 10); log(`  drag after pointerdown: ${drag ? "set, vert " + drag.vert + ", v " + drag.v : "none"}`); for (let i = 1; i <= 5; i++) { pe(ds, "pointermove", db.left + 10 + i * 4, db.top + 10); await sleep(30); }
	const midD = ds.getBoundingClientRect().width, midV = V.tracks[6].rt.DIST; main.removeEventListener("pointermove", probe); log(`  moves seen ${seen}, last clientX ${lastX} (down at ${db.left + 10}, drag.x ${drag && drag.x}), drag ${drag ? "still set" : "gone"}, tweak ${!!tweak}, sent ${sent.join(" ")}`); Bridge.send = send0; pe(ds, "pointerup", db.left + 30, db.top + 10); await sleep(300);
	log(`DIST sideways drag: ${dist0} -> ${midV} while dragging, ${V.tracks[6].rt.DIST} after, box ${db.width}->${midD} px: ${V.tracks[6].rt.DIST !== dist0 && midD === db.width ? "ok" : "FAIL"}`);
	cmd("param", { k: V.kit, t: 6, i: 16, v: dist0 }); cmd("param", { k: V.kit, t: 6, i: 17, v: vol0 }); await sleep(200);
	log(`fit: page zoom ${Math.round(innerWidth / 1440 * 1000) / 1000 || 1}, page ${document.documentElement.scrollWidth} px wide in ${innerWidth} px, header right edge ${Math.round(document.querySelector(".rightgrp").getBoundingClientRect().right)} px`);
}
if (/[?&]selftest=p4mix/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const log = t => Bridge.log("P4: " + t);
	while (!(runs() && V.loaded)) await sleep(200);
	await sleep(1500);
	await p4Mix(log, sleep);
	log("mix done");
})();

/* ?selftest=p4cpu (scripts/md-editor-cpu.sh): fixed phases for a CPU measurement from outside; each phase
   start and end is logged ("P4: cpu <phase> start/end") so the script can read the processes' CPU time. */
if (/[?&]selftest=p4cpu/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const log = t => Bridge.log("P4: " + t);
	while (!(runs() && V.loaded)) await sleep(200);
	await sleep(8000);	/* the background loads settle */
	const phase = async (name, ms) => { log(`cpu ${name} start`); await sleep(ms); log(`cpu ${name} end`); };
	S.ws = "seq"; render();
	await phase("stopped", 30000);
	if (!V.playing) $("#play").click();
	await sleep(1000);
	await phase("playing-seq", 30000);
	S.ws = "mix"; render();
	await phase("playing-mix", 30000);
	$("#play").click();
	log("cpu done");
})();

/* ?selftest=p5: P5 checks in the plug-in: PLAY right after ready (timed), GLOBAL, the ? list. */
if (/[?&]selftest=p5(&|$)/.test(location.search)) (async () => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const log = t => Bridge.log("P5: " + t);
	const desk = () => machineState().desk || {};
	const until = async (f, ms) => { const end = performance.now() + ms; while (performance.now() < end) { if (f()) return performance.now(); await sleep(5); } return -1; };
	const t0 = performance.now();
	await until(() => runs(), 90000);
	log(`ready at ${Math.round(performance.now() - t0)} ms, loads queued ${desk().loading}`);
	/* Times are taken in the message handler (a covered window throttles page timers to 1 s). */
	let seenAt = -1; Bridge.onMessage(m => { if (m.type === "machine" && m.doc.desk && m.doc.desk.playing && seenAt < 0) seenAt = performance.now(); });
	for (let i = 0; i < 4; i++) {
		seenAt = -1; const a = performance.now(); $("#play").click();
		await until(() => V.playing, 3000);
		log(`PLAY ${i + 1}: the page has "playing" ${Math.round(seenAt - a)} ms after the click (document.visibilityState ${document.visibilityState})`);
		await sleep(300); $("#play").click(); await until(() => !V.playing, 3000); await sleep(400);
	}
	/* GLOBAL: TEMPO OUT on, read back, off. */
	openGlobal(); await sleep(300);
	const g0 = Docs.global && Docs.global.control && Docs.global.control.tempoOut;
	document.querySelector('[data-ga="tempoOut"][data-v="1"]')?.click();
	let p1 = await until(() => Docs.global && Docs.global.control && Docs.global.control.tempoOut === true, 4000);
	log(`GLOBAL: TEMPO OUT on -> read back ${p1 >= 0 ? "ok" : "FAIL"} (was ${g0}); panel ${!$("#globpop").hidden}, header ${document.querySelector(".top").scrollWidth} px`);
	document.querySelector('[data-ga="tempoOut"][data-v="0"]')?.click();
	p1 = await until(() => Docs.global.control.tempoOut === false, 4000);
	log(`GLOBAL: TEMPO OUT off again ${p1 >= 0 ? "ok" : "FAIL"}`);
	closeGlobal();
	document.body.dispatchEvent(new KeyboardEvent("keydown", { key: "?", shiftKey: true, bubbles: true })); await sleep(200);
	log(`? list: ${document.querySelectorAll("#keyspop .keyrow").length} keys in ${document.querySelectorAll("#keyspop h3").length} groups`);
	toggleKeys(false);
	log(`modulators run in the ${Mods.runs || "?"}`);
	log("done");
})();

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
	const pat = () => Docs.patterns[currentPatternSlot()], kit = () => kitDocOf(Docs);
	const idle = () => !(machineState().desk || {}).tx;
	const status = () => `loaded ${!!(pat() && kit())} lifecycle ${machineState().lifecycle} pattern ${currentPatternSlot()} kit ${currentKitSlot()} docs ${Object.keys(Docs.patterns).length}/${Object.keys(Docs.kits).length}/${Object.keys(Docs.songs).length}`;
	if (await until(() => pat() && kit() && runs(), 60000) < 0) { log("FAIL: the machine did not load: " + status()); return; }
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
	log(`P3: kit source ${V.kitSource}, kit ${machineState().kit.working}, song mode ${machineState().songMode}, mutes ${machineState().desk.mutes}`);
	const lcdW = () => Math.round(document.querySelector(".lcdpanel").getBoundingClientRect().width * 10) / 10;
	const widths = [lcdW()];
	const bpm0 = V.bpm;
	for (const b of [30, 299.5, bpm0]) { cmd("tempo", { bpm: b }); await sleep(250); widths.push(lcdW()); }
	await sleep(800);
	log(`P3: tempo back to ${V.bpm} (was ${bpm0})`);
	log(`P3: LCD width through tempo changes ${widths.join(" / ")} px`);
	await document.fonts.ready;
	const faces = [...document.fonts].map(f => `${f.family.replace(/"/g, "")} ${f.weight} ${f.status}`);
	const fam = q => getComputedStyle(document.querySelector(q)).fontFamily.split(",")[0].replace(/"/g, "");
	log(`P3: fonts ${faces.join(", ")}; LCD value ${fam("#bpm")}, workspace keys ${fam("#tabs button")}, grid ruler ${fam(".rul")}`);
	const rect = q => { const b = document.querySelector(q).getBoundingClientRect(); return `${Math.round(b.left)},${Math.round(b.top)} ${Math.round(b.width)}x${Math.round(b.height)}`; };
	const rb = $("#rec").getBoundingClientRect(), pb = $("#play").getBoundingClientRect();
	log(`P3: transport keys REC ${rect("#rec")}, PLAY ${rect("#play")}: ${Math.abs(rb.width - rb.height) < 1 && Math.abs(pb.width - pb.height) < 1 && rb.width > 30 && Math.abs(rb.top - pb.top) < 1 && pb.left > rb.right ? "ok square, side by side" : "FAIL"}; LCD ${rect(".lcdpanel")}, window ${innerWidth}x${innerHeight}`);
	const recOn = () => !!V.rec;
	t0 = performance.now();
	$("#play").click();
	t1 = await until(() => V.playing, 3000);
	log(`P3: PLAY key -> playing ${t1 >= 0 ? "ok" : "FAIL"} ${ms(t0, t1)}, key shows ${$("#playico").textContent}`);
	t0 = performance.now();
	$("#play").click();
	t1 = await until(() => !V.playing, 3000);
	log(`P3: STOP (same key) -> stopped ${t1 >= 0 ? "ok" : "FAIL"} ${ms(t0, t1)}, key shows ${$("#playico").textContent}`);
	await sleep(300);
	t0 = performance.now();
	$("#rec").click();
	t1 = await until(recOn, 3000);
	log(`P3: after REC: playing ${V.playing} step ${S.step} telemetry ${machineState().desk.telemetry}`);
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
		log(`P3: REC again -> recording off ${t1 >= 0 ? "ok" : "FAIL"}, still playing ${V.playing}`);
		/* v49: a queued pattern shows only its name, blinking; a flash when it starts. */
		const from = currentPatternSlot(), to = (from + 1) % 128, w0 = lcdW();
		cmd("select", { p: to }); await answerAsk(600);
		await until(() => machineState().desk.queued === to, 2000); await sleep(150);
		log(`P3: queued ${patName(to)}: LCD shows "${$("#pat").textContent}" ${getComputedStyle($("#pat")).animationName}, LCD ${w0} -> ${lcdW()} px`);
		let flashed = false; const obs = new MutationObserver(() => { if ($(".patf").classList.contains("flash")) flashed = true; }); obs.observe($(".patf"), { attributes: true });
		t0 = performance.now(); t1 = await until(() => machineState().desk.queued == null && currentPatternSlot() === to, 12000); await sleep(100); obs.disconnect();
		log(`P3: switch heard after ${ms(t0, t1)}: LCD "${$("#pat").textContent}", flash ${flashed ? "ok" : "FAIL"}, LCD ${lcdW()} px`);
		cmd("stop"); await until(() => !V.playing, 3000);
		cmd("select", { p: from }); await answerAsk(600); await sleep(500);
		$("#play").click(); await until(() => !V.playing, 3000);
		log(`P3: PLAY key shows ${$("#playico").textContent} after stop`);
		cmd("clearSteps", { p: currentPatternSlot(), t: tr, from: 0, to: V.len }); await sleep(400);
	}
	log("selftest done; kit " + JSON.stringify(machineState().kit) + ", undo steps " + (machineState().history || {}).undoCount);
})();

/* The AUDIO / MIDI panel's own self-test: the same text as in the mockups' panel block
   (doc/modern-ux/audio_panel_check.py checks it), here so a release page has none of it. */
/* The panel's self-test (the editors' ?selftest=p6audio, or the mockup's console): open the panel, see
   the level arrive, change the buffer size and the output device and back, and check after each change
   that the machine keeps playing (its step moves). T gives log, play(on), step() and playing(). */
async function audioSelfTest(T){const sleep=ms=>new Promise(r=>setTimeout(r,ms)),res=[];
 const until=async(f,ms=8000)=>{const t0=Date.now();while(Date.now()-t0<ms){const D=audioDoc();if(D&&f(D))return D;await sleep(100)}const D=audioDoc();throw new Error("timeout: output "+D?.output?.id+", buffer "+D?.bufferSize?.value+", running "+D?.running+(D?.error?", error "+D.error:""))};
 const moving=async()=>{const s0=T.step();for(let i=0;i<30;i++){await sleep(100);if(T.playing()&&T.step()!==s0)return true}return false};
 const check=async(name,fn)=>{try{const n=await fn();res.push(true);T.log("ok   "+name+(n?": "+n:""))}catch(e){res.push(false);T.log("FAIL "+name+": "+e.message)}};
 const D0=await until(D=>D.standalone,15000),out0=D0.output.id,buf0=D0.bufferSize.value,muted0=!!D0.input.muted;
 T.log("devices: "+D0.output.list.length+" outputs ("+out0+"), "+D0.input.list.length+" inputs, "+D0.sampleRate.value+" Hz, buffer "+buf0+", "+(D0.midiInputs||[]).length+" MIDI inputs, running "+D0.running);
 let levels=0;const lv=audioLevel;window.audioLevel=v=>{levels++;lv(v)};
 await check("panel opens",async()=>{openAudio();await sleep(700);if(!AP.open||$("#audiopop").hidden)throw new Error("not shown");const n=$("#audiopop").querySelectorAll("select,button").length;return n+" controls, "+levels+" level updates"});
 window.audioLevel=lv;
 T.play(true);await sleep(1500);
 await check("playing before the changes",async()=>{if(!(await moving()))throw new Error("the step does not move");return "step "+T.step()});
 const buf1=D0.bufferSize.list.find(b=>b!==buf0&&b>=128&&b<=1024)??D0.bufferSize.list.find(b=>b!==buf0);
 await check("buffer size "+buf0+" -> "+buf1,async()=>{if(buf1==null)throw new Error("one buffer size only");audioSend({set:"bufferSize",value:buf1});const D=await until(D=>D.bufferSize.value===buf1&&D.running);if(!(await moving()))throw new Error("audio stopped");return D.latencyMs+" ms, still playing"});
 await check("buffer size back to "+buf0,async()=>{audioSend({set:"bufferSize",value:buf0});await until(D=>D.bufferSize.value===buf0&&D.running);if(!(await moving()))throw new Error("audio stopped");return "still playing"});
 const out1=D0.output.list.find(o=>o!==out0);
 await check("output "+out0+" -> "+(out1??"-"),async()=>{if(!out1)return "one output device only, skipped";audioSend({set:"output",device:out1});await until(D=>D.output.id===out1&&D.running);if(!(await moving()))throw new Error("audio stopped");return "still playing"});
 await check("output back to "+out0,async()=>{if(!out1)return "skipped";audioSend({set:"output",device:out0});await until(D=>D.output.id===out0&&D.running);if(!(await moving()))throw new Error("audio stopped");return "still playing"});
 await check("input mute toggles and is kept",async()=>{audioSend({set:"mute",on:!muted0});await until(D=>!!D.input.muted===!muted0);audioSend({set:"mute",on:muted0});await until(D=>!!D.input.muted===muted0);return muted0?"muted again":"live again"});
 T.play(false);closeAudio();await sleep(300);
 T.log((res.every(Boolean)?"PASS ":"FAIL ")+res.filter(Boolean).length+"/"+res.length)}

/* GEARMULATOR_MDSTUDIO_SELFTEST=p6audio: the panel's self-test once the engine is ready. */
if (/[?&]selftest=p6audio/.test(location.search)) (async () => {
	while ($(".lcdpanel").classList.contains("engwait")) await new Promise(r => setTimeout(r, 200));
	await new Promise(r => setTimeout(r, 3000));
	await audioSelfTest({ log: t => Bridge.log("AUDIO: " + t), play: on => { if (on !== V.playing) cmd(on ? "play" : "stop"); }, step: () => S.step, playing: () => V.playing });
})();
