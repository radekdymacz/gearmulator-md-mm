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
			await W(m => m.type === "machine" && m.doc.playing, 4000);
			stage = "tel playing";
			await W(m => m.type === "telemetry" && m.playing && m.step > 0, 4000);
			/* the soft playhead column glides with the machine's step (RAM telemetry), POSITION too */
			/* the column's target (its style): a covered window runs no transitions, so the computed one can stand still */
			const at = () => { const ph = $("#phcol"); return { x: ph && ph.style.transform ? new DOMMatrix(ph.style.transform).m41 : null, o: ph ? +ph.style.opacity : 0, h: ph ? ph.offsetHeight : 0, pos: $("#pos").textContent, step: s.step }; };
			await sleep(150); const a = at(); stage = "next step";
			await W(m => m.type === "telemetry" && m.playing && m.step > a.step);
			await sleep(250); const b = at(); stage = "stop";
			window.togglePlay();
			await W(m => m.type === "machine" && !m.doc.playing);
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
		await check("capabilities as data (the engine's reasons)", async () => {
			const caps = machine()?.capabilities;
			if (!caps || caps.engine !== "emu" || caps.can?.midiMutes !== false || !caps.reasons?.midiMutes) throw new Error(JSON.stringify(caps));
			const el = $('[data-mute="6"]');
			if (el && el.dataset.na !== "1") throw new Error("MIDI track mute not marked");
			return Object.keys(caps.reasons).length + " reasons";
		});
		const ok = results.filter(Boolean).length;
		log(`SELFTEST ${ok === results.length ? "PASS" : "FAIL"} ${ok}/${results.length}`);
	}

	const TESTS = {
		/* ?selftest=1: edits through the mockup's own gestures and its host, each round trip logged */
		1: () => setTimeout(runSelfTest, 500),
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
})();
