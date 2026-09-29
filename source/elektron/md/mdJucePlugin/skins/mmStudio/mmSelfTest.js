"use strict";
/* The Monomachine Editor's self-tests (P6): diagnostics only. This bundle is in the plug-in only
   when it is built with gearmulator_MDMM_DIAGNOSTICS (a test or development build); a release
   page has none of it. GEARMULATOR_MMSTUDIO_SELFTEST=<kind> gives the page ?selftest=<kind>; this
   bundle registers itself with the page (MMPage.whenReady) and reads the documents the page holds
   through MMPage.inspect(). The page knows nothing about it.
   It loads after mmAdapter.js and before the mockup: window.MMDiagnostics, set here, is where the
   mockup puts what only the tests may touch (its state S, the AUDIO / MIDI panel's own test); a
   release page has no MMDiagnostics, so its view exports neither. */
window.MMDiagnostics = {};
(() => {
	const V = () => window.MMView;
	const S = () => window.MMDiagnostics.S;	// the tests play the user: they edit the view's state itself
	const $ = q => document.querySelector(q), $$ = q => [...document.querySelectorAll(q)];
	const host = window.MMHost, log = t => Bridge.log(t), now = () => performance.now();
	const I = () => window.MMPage.inspect();
	const machine = () => I().machine;
	/* the current slots, from the machine document */
	const cur = () => { const m = machine(); return { pat: m.pattern.current, kit: m.kit.current, song: m.song.current ?? 0, glob: m.global.current ?? 0 }; };
	/* the mute commands the page sent (the plug-in's mute parameters), by track */
	const sentMute = [];
	const send0 = Bridge.send;
	Bridge.send = (msg, opt) => { if (msg.op === "mute") sentMute[msg.t] = !!msg.on; return send0(msg, opt); };
	/* waits for a message the plug-in sends */
	const waiters = [];
	Bridge.onMessage(m => { for (const w of [...waiters]) if (w.test(m)) { waiters.splice(waiters.indexOf(w), 1); w.done(m); } });
	const waitFor = (test, ms = 8000) => new Promise((done, fail) => {
		const w = { test, done };
		waiters.push(w);
		setTimeout(() => { const i = waiters.indexOf(w); if (i >= 0) { waiters.splice(i, 1); fail(new Error("timeout")); } }, ms);
	});
	const sleep = ms => new Promise(r => setTimeout(r, ms));

	/* ?selftest=mmcpu (scripts/mm-editor-cpu.sh): fixed 30 s phases, each start and end logged
	   ("cpu <phase> start/end"), so the script reads the processes' CPU time from outside. */
	async function runCpuPhases() {
		const log2 = t => log("MM-P3: " + t);
		while (!machine() || machine().loading.done < machine().loading.total) await sleep(200);
		await sleep(5000);	// the background loads settle
		const phase = async (name, ms) => { log2(`cpu ${name} start`); await sleep(ms); log2(`cpu ${name} end`); };
		V().goWs("seq");
		await phase("stopped", 30000);
		if (!S().playing) host.togglePlay();
		await sleep(1000);
		await phase("playing-seq", 30000);
		V().goWs("mix");
		await phase("playing-mix", 30000);
		host.togglePlay();
		log2("cpu done");
	}
	async function runSelfTest() {
		const s = S(), results = [];
		const check = async (name, fn) => {
			const t0 = now();
			try { const n = await fn(); results.push(true); log(`SELFTEST ok ${name} ${Math.round(now() - t0)} ms${n ? " " + n : ""}`); }
			catch (e) { results.push(false); log(`SELFTEST FAIL ${name}: ${e.message}`); }
			await sleep(1200);
		};
		const CUR = cur(), p = CUR.pat;
		const readBack = test => waitFor(m => m.type === "doc" && m.kind === "pattern" && m.slot === p && !m.pending && test(m.doc));
		const free = () => [...Array(s.len).keys()].find(st => !s.tracks[0].steps[st]);
		const clickStep = (t, st, e) => { V().clickStep(t, st, e); host.edited("commit"); };
		log(`SELFTEST start: pattern ${p} kit ${CUR.kit} song ${CUR.song} global ${CUR.glob}, loaded ${machine()?.loading.done}/${machine()?.loading.total}`);
		await check("pattern trig (SYSEX RECV round trip)", async () => {
			const st = free();
			if (st == null) throw new Error("no free step on T1");
			clickStep(0, st, {});
			await readBack(d => d.tracks[0].trig.includes(st) && d.tracks[0].amp.includes(st));
			clickStep(0, st, {});
			await readBack(d => !d.tracks[0].trig.includes(st));
			return "step " + (st + 1);
		});
		await check("undo and redo in C++ (the core's history)", async () => {
			const st = free();
			clickStep(0, st, {});
			await readBack(d => d.tracks[0].trig.includes(st));
			await sleep(200);
			const before = machine()?.history?.undoCount || 0;
			host.undo();
			await readBack(d => !d.tracks[0].trig.includes(st));
			for (let n = 0; n < 15 && s.tracks[0].steps[st]; n++) await sleep(100);
			if (s.tracks[0].steps[st]) throw new Error("the view still shows the undone trig");
			host.redo();
			await readBack(d => d.tracks[0].trig.includes(st));
			host.undo();
			await readBack(d => !d.tracks[0].trig.includes(st));
			return "undo steps " + before;
		});
		await check("trigless trig", async () => {
			const st = free();
			clickStep(0, st, { altKey: true });
			await readBack(d => d.tracks[0].trig.includes(st) && !d.tracks[0].amp.includes(st) && d.tracks[0].notes.some(n => n[0] === st));
			clickStep(0, st, {});
			await readBack(d => !d.tracks[0].trig.includes(st));
		});
		await check("pitchless trig", async () => {
			const st = free();
			/* no gesture reaches a=1,f=1,l=1 with no note: clickStep's alt-click only toggles an
			   existing step between full and trigless (it keeps whatever note is there), and a
			   fresh alt-click always adds one. A direct write is the only way in (kept, per the
			   round's own rule for a case with no named call or DOM event). */
			s.tracks[0].steps[st] = { a: 1, f: 1, l: 1 };
			window.structEdited();
			await readBack(d => d.tracks[0].trig.includes(st) && !d.tracks[0].notes.some(n => n[0] === st));
			s.tracks[0].steps[st] = null;
			window.structEdited();
			await readBack(d => !d.tracks[0].trig.includes(st));
		});
		await check("kit value live (AMP VOL, CC)", async () => {
			const v0 = s.tracks[0].v.AMP[5], v = v0 > 60 ? v0 - 7 : v0 + 7;
			/* the kit that plays: the working kit document (the older contract: kit, working) */
			const playing = m => m.type === "doc" && (m.kind === "workingKit" || (m.kind === "kit" && m.working));
			const got = waitFor(m => playing(m) && m.doc.tracks[0].pages[1][5] === v, 5000);
			/* AMP VOL has no exact-value gesture (only the knob's relative pointer drag, 115-control.js):
			   a direct write is the only way to land on a precise value (kept, as above). */
			s.tracks[0].v.AMP[5] = v;
			window.soundEdited();
			await got;
			s.tracks[0].v.AMP[5] = v0;
			window.soundEdited();
			await waitFor(m => playing(m) && m.doc.tracks[0].pages[1][5] === v0, 5000);
		});
		await check("pattern switch (LOAD PATTERN)", async () => {
			const to = (p + 1) % 128;
			window.queuePattern(to);
			await waitFor(m => m.type === "machine" && m.doc.pattern.current === to);
			await sleep(800);
			if (s.pat !== to) throw new Error("page shows " + s.pat);
			window.queuePattern(p);
			await waitFor(m => m.type === "machine" && m.doc.pattern.current === p);
		});
		await check("play and stop, playhead and POSITION follow the machine", async () => {
			let stage = "play";
			const W = (t, ms) => waitFor(t, ms).catch(() => { throw new Error("timeout at " + stage + ", step " + s.step + ", playing " + s.playing); });
			V().goWs("seq");
			await sleep(300);
			window.togglePlay();
			await W(m => m.type === "telemetry" && m.playing, 4000);
			stage = "tel playing";
			await W(m => m.type === "telemetry" && m.playing && m.step > 0, 4000);
			/* the soft playhead column glides with the machine's step (RAM telemetry), POSITION too */
			/* the column's target (its style): a covered window runs no transitions, so the computed one can stand still */
			const at = () => { const ph = $("#phcol"); return { x: ph && ph.style.transform ? new DOMMatrix(ph.style.transform).m41 : null, o: ph ? +ph.style.opacity : 0, h: ph ? ph.offsetHeight : 0, pos: $("#pos").textContent, step: s.step }; };
			await sleep(150); const a = at(); stage = "next step";
			await W(m => m.type === "telemetry" && m.playing && m.step > a.step);
			await sleep(250); const b = at(); stage = "stop";
			window.togglePlay();
			await W(m => m.type === "telemetry" && !m.playing);
			await sleep(300); const c = at();
			const lane = $("#lane")?.getBoundingClientRect(), roll = $("#seq .nlane.big")?.getBoundingClientRect(), ph = $("#phcol")?.getBoundingClientRect();
			const spans = lane && roll && ph && ph.top <= roll.top && ph.bottom >= lane.bottom;
			const n = `x ${Math.round(a.x)} -> ${Math.round(b.x)}, step ${a.step} -> ${b.step}, POSITION ${a.pos} -> ${b.pos} -> ${c.pos}, column ${a.h} px over roll..lane ${spans ? "yes" : "no"}, opacity after stop ${c.o}`;
			if (a.x == null || !(b.x > a.x) || a.o !== 1 || b.o !== 1 || !spans || a.pos === b.pos || c.pos !== "--.--" || c.o !== 0) throw new Error(n);
			return n;
		});
		await check("tempo out and read back (0x61, RAM)", async () => {
			const t0 = s.bpm, t = t0 === 133 ? 127 : 133;
			/* V().setTempo is the named call the BPM drag itself uses (130-main.js); no direct S write */
			V().setTempo(t); host.tempo(t);
			await waitFor(m => m.type === "machine" && m.doc.tempo === t, 5000);
			V().setTempo(t0); host.tempo(t0);
			await waitFor(m => m.type === "machine" && m.doc.tempo === t0, 5000);
			return t0 + " -> " + t + " -> " + t0 + " BPM";
		});
		await check("solo mutes the other synth tracks", async () => {
			const before = [0, 1, 2, 3, 4, 5].map(i => !V().audible(i));
			sentMute.length = 0;
			/* the rail's own SOLO key (a DOM event), not a direct write of S.tracks[2].solo: it
			   flips the solo and calls host.mutes() itself, as a real click would */
			const solo = $('[data-solo="2"]');
			solo.click();
			await sleep(300);
			const muted = before.map((m, i) => sentMute[i] ?? m).join("");
			solo.click();
			await sleep(300);
			if (muted !== "truetruefalsetruetruetrue") throw new Error("mutes " + muted);
		});
		await check("song and global documents", async () => {
			if (!I().doc("song", CUR.song) || !I().doc("global", CUR.glob)) throw new Error("not loaded");
			return s.song.length + " rows, routing " + s.routing;
		});
		await check("capabilities as data: the emulator can do every gated control (MM-P4)", async () => {
			const caps = machine()?.capabilities;
			const off = V().gated().filter(c => caps?.can?.[c] !== true);
			if (!caps || caps.engine !== "emu" || off.length) throw new Error("not allowed: " + off.join(", ") + " " + JSON.stringify(caps?.reasons));
			const el = $('[data-mute="6"]');
			if (el && el.dataset.na === "1") throw new Error("MIDI track mute still marked: " + el.title);
			return V().gated().length + " capabilities allowed";
		});
		const ok = results.filter(Boolean).length;
		log(`SELFTEST ${ok === results.length ? "PASS" : "FAIL"} ${ok}/${results.length}`);
	}

	/* ?selftest=p4: MM-P4's features, ported onto the P6 page in P8, through the user's own paths: MIDI track
	   mutes (the MUTE window), POLY, GRID RECORDING, MULTI TRIG and PORTAMENTO, the MULTI MAP, another song,
	   undo of a library write, HW MIDI with nothing attached. */
	async function runP4() {
		const s = S(), results = [];
		const check = async (name, fn) => {
			const t0 = now();
			try { const n = await fn(); results.push(true); log(`SELFTEST ok ${name} ${Math.round(now() - t0)} ms${n ? " " + n : ""}`); }
			catch (e) { results.push(false); log(`SELFTEST FAIL ${name}: ${e.message}`); }
			await sleep(1200);
		};
		while (!machine() || machine().loading.done < machine().loading.total) await sleep(200);
		await sleep(2000);
		const CUR = cur();
		const machineIs = test => waitFor(m => m.type === "machine" && test(m.doc), 8000);
		const docIs = (kind, slot, test) => waitFor(m => m.type === "doc" && m.kind === kind && m.slot === slot && !m.pending && test(m.doc), 12000);
		log(`SELFTEST p4 start: pattern ${CUR.pat} kit ${CUR.kit} song ${CUR.song} global ${CUR.glob}`);
		await check("MIDI track mute (the MUTE window, read back from RAM)", async () => {
			{ const w = machineIs(d => d.mutes.midi != null && ((d.mutes.midi >> 2) & 1) === 1);
			s.midi[2].mute = true; host.mutes();
			await w; }
			{ const w = machineIs(d => ((d.mutes.midi >> 2) & 1) === 0);
			s.midi[2].mute = false; host.mutes();
			await w; }
		});
		await check("POLY (SET STATUS 0x20)", async () => {
			V().goWs("perform"); await sleep(300);
			{ const w = machineIs(d => d.poly === true);
			$('[data-pmode="poly"]').click();
			await w; }
			{ const w = machineIs(d => d.poly === false);
			$('[data-pmode="normal"]').click();
			await w; }
			V().goWs("seq");
		});
		await check("GRID RECORDING on and off (the RECORD key)", async () => {
			{ const w = waitFor(m => m.type === "telemetry" && m.record === "grid", 6000);
			$("#rec").click();
			await w; }
			await sleep(300);
			{ const w = waitFor(m => m.type === "telemetry" && m.record === "off", 6000);
			$("#rec").click();
			await w; }
		});
		await check("MULTI TRIG and PORTAMENTO in the kit that plays", async () => {
			const m0 = { ...s.multi }, p0 = s.tracks[1].port;
			s.multi = { ...s.multi, mode: 1, splitKey: 55, splitTrack: 4, timing: 2 };
			s.tracks[1].port = p0 === 1 ? 0 : 1;
			const w1 = waitFor(m => m.type === "doc" && m.kind === "workingKit" && !m.pending && m.doc.multiTrig.mode === 1 && m.doc.multiTrig.splitKey === 55
				&& m.doc.multiTrig.splitTrack === 3 && m.doc.multiTrig.timing === 2 && ((m.doc.trackMasks.portamento >> 1) & 1) === (p0 === 1 ? 1 : 0), 12000);
			host.edited("sound"); host.edited("commit");
			await w1;
			s.multi = m0; s.tracks[1].port = p0;
			{ const w = waitFor(m => m.type === "doc" && m.kind === "workingKit" && !m.pending && m.doc.multiTrig.mode === m0.mode, 12000);
			host.edited("sound"); host.edited("commit");
			await w; }
		});
		await check("a MULTI MAP row in the global", async () => {
			const r0 = { ...s.mmap[0] };
			Object.assign(s.mmap[0], { ofs: 3, len: 12, trn: 66, tim: 2 });
			{ const w = docIs("global", CUR.glob, d => d.multiMap[2][0] === 2 && d.multiMap[3][0] === 12 && d.multiMap[4][0] === 2 && d.multiMap[5][0] === 2);
			host.edited("struct", "global"); host.edited("commit");
			await w; }
			Object.assign(s.mmap[0], r0);
			{ const w = docIs("global", CUR.glob, d => d.multiMap[5][0] === r0.tim);
			host.edited("struct", "global"); host.edited("commit");
			await w; }
		});
		await check("another song than the machine's (S24), from the picker", async () => {
			V().goWs("song"); await sleep(300);
			const sel = $("#songsel");
			if (!sel) throw new Error("no song picker");
			sel.value = "23"; sel.dispatchEvent(new Event("change", { bubbles: true }));
			await sleep(500);
			const before = s.song.length;
			{ const w = docIs("song", 23, d => d.rows.length === before + 1 && d.rows[0].pattern === 7);
			s.song.splice(0, 0, { pat: 7, rep: 2 }); host.edited("struct", "song"); host.edited("commit");
			await w; }
			{ const w = docIs("song", 23, d => d.rows.length === before);
			s.song.splice(0, 1); host.edited("struct", "song"); host.edited("commit");
			await w; }
			host.songSlot(CUR.song);
			V().goWs("seq");
			return `${before} rows`;
		});
		await check("undo a library write (a kit copied into K100)", async () => {
			const I0 = I(), before = I0.doc("kit", 99);
			const from = [...Array(99).keys()].find(k => k !== cur().kit && I0.doc("kit", k) && !MmConvert.kitEmpty(I0.doc("kit", k))
				&& MmConvert.kitName(I0.doc("kit", k)) !== MmConvert.kitName(before || {}));
			const src = from != null ? I0.doc("kit", from) : null;
			if (!before || !src) throw new Error("kits not read");
			log(`SELFTEST p4: kit copy K${from + 1} "${MmConvert.kitName(src)}" into K100 "${MmConvert.kitName(before)}"`);
			const written = docIs("kit", 99, d => MmConvert.kitName(d) === MmConvert.kitName(src));
			window.kitPut(99, window.kitSrc(from), "Copy");
			await sleep(150);
			$('#dlg [data-dlg="0"]')?.click();
			await written;
			host.edited("commit");
			const undone = docIs("kit", 99, d => MmConvert.kitName(d) === MmConvert.kitName(before));
			host.undo();
			await undone;
			return `"${MmConvert.kitName(src)}" then back to "${MmConvert.kitName(before)}"`;
		});
		await check("HW MIDI with no Monomachine attached: HW NO MIDI, then the emulator again", async () => {
			const hw = (machine().engines || []).find(e => e.id === "hw");
			if (!hw) throw new Error("no hw engine");
			if (!hw.available) return "not offered here: " + hw.reason;
			{ const w = waitFor(m => m.type === "machine" && m.doc.capabilities?.engine === "hw" && m.doc.lifecycle === "hwLost", 12000);
			host.engine("hw");
			await w; }
			const caps = machine().capabilities;
			if (caps.can.midiMutes || caps.can.gridRecord) throw new Error("MIDI track mutes or RECORD allowed over MIDI");
			{ const w = waitFor(m => m.type === "machine" && m.doc.capabilities?.engine === "emu" && m.doc.lifecycle === "ready", 20000);
			host.engine("emu");
			await w; }
			return "HW NO MIDI, the reasons, then EMU OS 1.32B";
		});
		const ok = results.filter(Boolean).length;
		log(`SELFTEST ${ok === results.length ? "PASS" : "FAIL"} ${ok}/${results.length}`);
	}

	/* ?selftest=p7: P7's bugs through the user's own paths. A loaded pattern leaves its kit clean
	   (no kit edit goes out, no question on the next switch), stopped and playing; the LEN key
	   (LCD line 2) changes the step count, read back from the machine. */
	async function runP7() {
		const s = S(), results = [];
		const check = async (name, fn) => {
			try { const n = await fn(); results.push(true); log(`SELFTEST ok ${name}${n ? " " + n : ""}`); }
			catch (e) { results.push(false); log(`SELFTEST FAIL ${name}: ${e.message}`); }
			await sleep(800);
		};
		for (const [name, pass, note] of window.__p7boot || []) { results.push(!!pass); log(`SELFTEST ${pass ? "ok" : "FAIL"} ${name} ${note}`); }
		{
			const card = $("#bootcard");
			results.push(card.hidden); log(`SELFTEST ${card.hidden ? "ok" : "FAIL"} the card is gone once the machine takes input`);
		}
		while (!machine() || machine().loading.done < machine().loading.total) await sleep(200);
		V().goWs("seq");
		await sleep(4000);
		const sent = [], asks = [];
		let lenTest = false;
		const send1 = Bridge.send;
		Bridge.send = (msg, opt) => { sent.push(msg.op + (msg.kind ? ":" + msg.kind : "")); if (msg.op === "set") log("p7: set " + msg.kind + " from " + new Error().stack.split("\n").slice(2, 7).map(x => x.trim().replace(/\(.*\//, "(")).join(" < ")); return send1(msg, opt); };
		/* an ask is answered with the dialog's confirm key, as the user would, only while
		   confirming is on: the kit that plays may hold real edits when the test starts */
		let confirming = false;
		Bridge.onMessage(m => { if (m.type !== "ask") return; asks.push(m.ask); if (confirming) setTimeout(() => $('#dlg [data-dlg="0"]')?.click(), 300); });
		const p0 = cur().pat;
		log(`p7: start: pattern ${p0} kit ${cur().kit} ${machine().kit.working}, modulators ${JSON.stringify(V().ctlSetup().links.length)} links`);
		/* where the kit that plays differs from its stored slot (the documents' own paths) */
		const kitDiff = () => {
			const w = I().workingKit, st = I().doc("kit", cur().kit), out = [];
			const walk = (a, b, at) => { if (out.length > 12) return; if (a && b && typeof a === "object") { for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) walk(a[k], b[k], at + "." + k); } else if (JSON.stringify(a) !== JSON.stringify(b)) out.push(`${at} ${JSON.stringify(a)} vs ${JSON.stringify(b)}`); };
			if (w && st) walk(w.doc, st, "");
			return `working (${w?.source}, slot ${w?.slot}) vs stored: ${out.join("; ") || "same"}`;
		};
		log("p7: " + kitDiff());
		const switchTo = async to => {
			sent.length = 0; asks.length = 0;
			const before = machine().kit.working;
			window.queuePattern(to);
			const moved = await waitFor(m => m.type === "machine" && m.doc.pattern.current === to, 30000).then(() => true, () => false);
			await sleep(2500);
			const edits = sent.filter(x => x.startsWith("set:"));
			if (machine().kit.working !== "clean") log("p7: after select " + to + ": " + kitDiff());
			log(`p7: select ${to}: kit ${before} before, ${moved ? "switched" : "NOT switched"}, kit ${cur().kit} ${machine().kit.working}; sent ${sent.join(",") || "-"}; asked ${asks.join(",") || "-"}`);
			if (!moved) { document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true })); throw new Error(`pattern ${to} not loaded (asked ${asks.join(",") || "nothing"}, kit ${before})`); }
			return { state: machine().kit.working, edits, asks: [...asks], kit: cur().kit };
		};
		await check("a loaded pattern's kit stays clean (stopped)", async () => {
			const seen = [];
			/* start from a clean kit: an edited one asks once (the question is right), and is confirmed */
			if (machine().kit.working === "edited") {
				confirming = true;
				const r = await switchTo((p0 + 1) % 128);
				confirming = false;
				seen.push(`edited at start, asked ${r.asks.join(",") || "nothing"}, then kit ${r.kit} ${r.state}`);
				if (r.asks.length !== 1 || r.state !== "clean") throw new Error(seen.join("; "));
			}
			const q = cur().pat;
			for (const to of [(q + 1) % 128, (q + 2) % 128, (q + 3) % 128, q]) {
				const r = await switchTo(to);
				seen.push(`${to}->kit ${r.kit} ${r.state}${r.edits.length ? " sent " + r.edits.join(",") : ""}${r.asks.length ? " asked " + r.asks.join(",") : ""}`);
				if (r.state !== "clean" || r.edits.length || r.asks.length) throw new Error(seen.join("; "));
			}
			return seen.join("; ");
		});
		await check("a loaded pattern's kit stays clean (playing)", async () => {
			const seen = [];
			if (!s.playing) host.togglePlay();
			await waitFor(m => m.type === "telemetry" && m.playing, 4000);
			try {
				const q = cur().pat;
				for (const to of [(q + 1) % 128, q]) {
					const r = await switchTo(to);
					seen.push(`${to}->kit ${r.kit} ${r.state}${r.edits.length ? " sent " + r.edits.join(",") : ""}${r.asks.length ? " asked " + r.asks.join(",") : ""}`);
					if (r.state !== "clean" || r.edits.length || r.asks.length) throw new Error(seen.join("; "));
				}
			} finally { if (s.playing) host.togglePlay(); }
			return seen.join("; ");
		});
		await check("a drag ends when its element goes and the button comes up elsewhere", async () => {
			V().goWs("mix");
			await sleep(600);
			const f = $(".fader[data-g]");
			if (!f) throw new Error("no fader on Mix");
			const r = f.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;
			const pe = (type, o) => new PointerEvent(type, Object.assign({ bubbles: true, cancelable: true, pointerId: 7, pointerType: "mouse", clientX: x, clientY: y }, o));
			const seen = [];
			for (const how of ["pointerup outside #main", "no pointerup at all"]) {
				f.isConnected && f.dispatchEvent(pe("pointerdown", { buttons: 1 }));
				const f2 = $(".fader[data-g]");
				f2.dispatchEvent(pe("pointerdown", { buttons: 1 }));
				V().render();	// the machine's documents re-draw the workspace: the fader is a new element
				if (how === "pointerup outside #main") $(".top").dispatchEvent(pe("pointerup", { buttons: 0 }));
				sent.length = 0;
				$("#main").dispatchEvent(pe("pointermove", { buttons: 0, clientY: y - 60 }));
				await sleep(400);
				const edits = sent.filter(k => k.startsWith("set:"));
				seen.push(`${how}: ${edits.length} edits, busy ${V().busy()}`);
				if (edits.length || V().busy()) throw new Error(seen.join("; "));
			}
			V().goWs("seq");
			await sleep(600);
			return seen.join("; ");
		});
		await check("all steps by default, and the page fits the window", async () => {
			const n = $$('.tc[data-tl="sld"]').length, de = document.documentElement, lane = $("#lanescroll")?.getBoundingClientRect();
			const note = `${n} of ${s.len} steps, page ${de.scrollHeight} / window ${innerHeight}, lock lane bottom ${lane ? Math.round(lane.bottom) : "?"}`;
			if (n !== s.len || de.scrollHeight > innerHeight + 1 || !lane || lane.bottom > innerHeight) throw new Error(note);
			return note;
		});
		await check("a drag paints SLIDE steps on, then off, one undo step each", async () => {
			V().goWs("seq"); await sleep(600);
			const p = cur().pat, q = st => $(`.tc[data-tl="sld"][data-s="${st}"]`);
			const free = [...Array(s.len - 3).keys()].find(st => [0, 1, 2, 3].every(k => !s.tracks[s.sel].slide.has(st + k)));
			if (free == null) throw new Error("no four free SLIDE steps");
			const run = [free, free + 1, free + 2, free + 3];
			const pe = (type, el, o = {}) => { const r = el.getBoundingClientRect(); return new PointerEvent(type, Object.assign({ bubbles: true, cancelable: true, pointerId: 8, pointerType: "mouse", button: 0, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 }, o)); };
			const drag = steps => { q(steps[0]).dispatchEvent(pe("pointerdown", q(steps[0]), { buttons: 1 })); for (const st of steps.slice(1)) $("#main").dispatchEvent(pe("pointermove", q(st), { buttons: 1 })); const l = q(steps[steps.length - 1]); l.dispatchEvent(pe("pointerup", l, { buttons: 0 })); l.dispatchEvent(new MouseEvent("click", { bubbles: true, detail: 1 })); };
			const slides = d => d.tracks[s.sel].slide || [];
			const u0 = machine().history?.undoCount || 0;
			drag(run);
			await waitFor(m => m.type === "doc" && m.kind === "pattern" && m.slot === p && !m.pending && run.every(st => slides(m.doc).includes(st)), 15000).catch(() => { throw new Error("not read back on"); });
			await sleep(800);
			const u1 = machine().history?.undoCount || 0;
			drag([...run].reverse());
			await waitFor(m => m.type === "doc" && m.kind === "pattern" && m.slot === p && !m.pending && !run.some(st => slides(m.doc).includes(st)), 15000).catch(() => { throw new Error("not read back off"); });
			await sleep(800);
			const u2 = machine().history?.undoCount || 0;
			if (u1 !== u0 + 1 || u2 !== u1 + 1) throw new Error(`undo steps ${u0} -> ${u1} -> ${u2}`);
			return `steps ${free + 1}-${free + 4}, undo steps ${u0} -> ${u1} -> ${u2}`;
		});
		await check("Shift + M prepares mutes; they apply together when Shift comes up", async () => {
			const a = [0, 1].map(i => V().audible(i));
			sentMute.length = 0;
			for (const i of [0, 1]) $(`[data-mute="${i}"]`).dispatchEvent(new MouseEvent("click", { bubbles: true, shiftKey: true, detail: 1 }));
			await sleep(300);
			const prep = $$(".ms.m.prep").map(b => b.dataset.mute + b.dataset.prep).join(" "), held = sentMute.filter(x => x != null).length;
			document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift", bubbles: true }));
			await sleep(400);
			const after = [0, 1].map(i => V().audible(i)), sent = [0, 1].map(i => sentMute[i]);
			/* and back */
			for (const i of [0, 1]) $(`[data-mute="${i}"]`).click();
			await sleep(300);
			if (prep !== "0X 1X" || held || after.some((x, i) => x === a[i]) || sent.some(x => x !== true)) throw new Error(`prepared ${prep}, ${held} sent while held, audible ${a} -> ${after}, sent ${sent}`);
			return `prepared ${prep}, nothing sent while held, both muted on release`;
		});
		await check("LEN on LCD line 2 changes the steps (click, shift-click)", async () => {
			lenTest = true;
			V().goWs("seq"); await sleep(600);
			const p = cur().pat, len0 = s.len, seen = [len0];
			const seenDocs = [];
			Bridge.onMessage(m => { if (lenTest && m.type === "doc" && m.kind === "pattern" && m.slot === p) { seenDocs.push(m.doc.length + (m.pending ? "p" : "")); log("p7: doc pattern " + p + " length " + m.doc.length + (m.pending ? " pending" : "") + ", page LEN " + s.len); } if (lenTest && m.type === "result" && m.op === "set") seenDocs.push("result " + m.ok + " " + (m.errors || []).join(",") + " " + (m.note || "")); });
			log(`p7: LEN test on pattern ${p}, LEN ${len0}, playing ${s.playing}, recv ${machine().recv.state}`);
			const key = () => document.querySelector('[data-l2="len"]');
			const readBack = n => waitFor(m => m.type === "doc" && m.kind === "pattern" && m.slot === p && !m.pending && m.doc.length === n, 20000).catch(() => { throw new Error("LEN " + n + " not read back; saw " + seenDocs.join(" ") + "; sent " + sent.join(",") + "; recv " + machine().recv.state); });
			for (const shift of [false, true]) {
				const want = shift ? len0 : null;
				log("p7: LEN click" + (shift ? " (shift)" : "") + " at " + s.len);
				key().dispatchEvent(new MouseEvent("click", { bubbles: true, shiftKey: shift }));
				host.edited("commit");
				const n = s.len;
				if (want == null && n === len0) throw new Error("the click left LEN at " + n);
				await readBack(n);
				seen.push(n);
				await sleep(400);	// the read-back is shown first, as a user's next click would find it
			}
			if (s.len !== len0) throw new Error("LEN " + seen.join(" -> "));
			return "LEN " + seen.join(" -> ") + ", read back each";
		});
		await check("one modal system: the library centred over a backdrop, Esc closes it; a question starts on Cancel and keeps its place", async () => {
			const esc = () => document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
			$("#kitf").click(); await sleep(400);
			const lib = $("#libpop"), r = lib.getBoundingClientRect(), bg = $("#modalbg");
			const centred = Math.abs(r.left + r.width / 2 - innerWidth / 2) < 2 && Math.abs(r.top + r.height / 2 - innerHeight / 2) < 2;
			const open1 = !lib.hidden && bg && !bg.hidden && lib.contains(document.activeElement);
			esc(); await sleep(300);
			let answer = null;
			V().ask("P7 self-test question?", [["Go on", "danger", () => { answer = "go"; }], ["Cancel", "", () => { answer = "cancel"; }]]);
			await sleep(400);
			const first = document.activeElement?.textContent;
			bg.dispatchEvent(new MouseEvent("click", { bubbles: true })); $("#dlg").dispatchEvent(new MouseEvent("click", { bubbles: true }));
			await sleep(200);
			const stays = !$("#dlg").hidden && answer == null;
			esc(); await sleep(300);
			const note = `library centred ${centred}, open ${open1}, closed ${lib.hidden}; question focus ${first}, stays ${stays}, answer ${answer}`;
			if (!centred || !open1 || !lib.hidden || first !== "Cancel" || !stays || answer !== "cancel") throw new Error(note);
			return note;
		});
		Bridge.send = send1;
		const ok = results.filter(Boolean).length;
		log(`SELFTEST ${ok === results.length ? "PASS" : "FAIL"} ${ok}/${results.length}`);
	}

	const TESTS = {
		/* ?selftest=1: edits through the mockup's own gestures and its host, each round trip logged */
		1: () => setTimeout(runSelfTest, 500),
		p7: () => setTimeout(runP7, 500),
		p4: () => setTimeout(runP4, 500),
		mmcpu: () => runCpuPhases(),
		/* ?selftest=p6audio: the AUDIO / MIDI panel's self-test (the mockup's) */
		p6audio: () => setTimeout(() => window.MMDiagnostics.audioSelfTest({ log: t => log("AUDIO: " + t), play: on => { if (on !== V().playing()) host.togglePlay(); }, step: () => V().step(), playing: () => V().playing() }), 3000)
	};
	/* The page's first render, logged so a blank page fails the self-tests (P5), with its layout checks:
	   the LCD transport keys must be square and side by side (they collapsed to slivers once). */
	setTimeout(() => {
		const a = $(".app"), r = a ? a.getBoundingClientRect() : { width: 0, height: 0 };
		log(`first render: ${$$("#main *").length} elements in #main, page ${Math.round(r.width)} x ${Math.round(r.height)}, window ${innerWidth} x ${innerHeight}`);
		const rb = $("#rec")?.getBoundingClientRect(), pb = $("#play")?.getBoundingClientRect();
		const ok = rb && pb && Math.abs(rb.width - rb.height) < 1 && Math.abs(pb.width - pb.height) < 1 && rb.width > 30 && Math.abs(rb.top - pb.top) < 1 && pb.left > rb.right;
		log(`transport keys: REC ${rb ? Math.round(rb.width) + "x" + Math.round(rb.height) : "?"}, PLAY ${pb ? Math.round(pb.width) + "x" + Math.round(pb.height) : "?"}: ${ok ? "ok square, side by side" : "FAIL"}`);
		const mk = getComputedStyle($(".lcdgroup"), "::after"), top = $(".top").getBoundingClientRect(), g = $(".lcdgroup").getBoundingClientRect();
		log(`MKII print: bottom ${mk.bottom}, LCD group bottom ${Math.round(g.bottom)}, header bottom ${Math.round(top.bottom)}`);
	}, 1500);
	/* the catalogue and the page's tables (mmConvert, the mockup's) agree */
	window.MMPage.whenReady(() => {
		const cat = I().catalogue, off = cat ? MmConvert.useCatalogue(cat) : ["no catalogue"];
		log(`catalogue: ${off.length ? "FAIL " + off.join("; ") : "ok, the page's tables agree"}`);
	});
	const kind = (location.search.match(/[?&]selftest=(\w+)/) || [])[1];
	if (kind && TESTS[kind]) window.MMPage.whenReady(TESTS[kind]);
	/* p7, from the page's start: the start-up card covers the window and blocks input while the machine starts */
	if (kind === "p7") (async () => {
		const until = async (f, ms) => { const end = now() + ms; while (now() < end) { if (f()) return true; await sleep(30); } return false; };
		const out = window.__p7boot = [];
		const sent = [], send0 = Bridge.send;
		Bridge.send = (m, o) => { sent.push(m.op); return send0(m, o); };
		const booting = await until(() => typeof Boot !== "undefined" && Boot.state() === "booting", 20000);
		await sleep(2500);
		const card = $("#bootcard"), top = typeof Modal !== "undefined" ? Modal.top() : null, cardShown = !!card && !card.hidden;
		const hit = el => { if (!el) return false; const r = el.getBoundingClientRect(); const t = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); return !!t && (t.closest("#bootcard") || t.id === "modalbg"); };
		const covered = hit($("#play")) && hit($('[data-ws="mix"]'));
		document.dispatchEvent(new KeyboardEvent("keydown", { key: " ", code: "Space", bubbles: true, cancelable: true }));
		await sleep(300);
		/* the animation has blank frames: the most pixels over a few seconds */
		let inked = 0;
		for (let k = 0; k < 20 && !$("#bootcard").hidden; k++) {
			const g = $("#bootlcd").getContext("2d").getImageData(0, 0, 128, 64).data;
			let n = 0; for (let i = 0; i < g.length; i += 4) if (g[i] !== g[0] || g[i + 1] !== g[1] || g[i + 2] !== g[2]) n++;
			inked = Math.max(inked, n); await sleep(200);
		}
		const blocked = covered && !sent.includes("play");
		Bridge.send = send0;
		out.push([`while it starts, the start-up card covers the window and blocks input`, booting && cardShown && top === "bootcard" && blocked,
			`card ${cardShown}, top ${top}, blocked ${blocked} (sent ${sent.join(",") || "-"})`]);
		out.push(["the card mirrors the firmware's LCD", inked > 50, inked + " pixels"]);
	})();
})();
