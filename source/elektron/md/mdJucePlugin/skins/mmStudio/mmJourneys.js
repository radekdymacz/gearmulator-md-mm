"use strict";
/* The Monomachine Editor's user journeys (diagnostics builds only; skins/shared/deskJourney.js says what a
   journey is). Loaded last: the page's view state (window.MMDiagnostics.S, what the screen draws, the piano roll
   included) and the documents the page holds (MMPage.inspect(): what the firmware read back). Every step is
   done through the page's own controls. ?selftest=journey runs them all, journey-<names> some (journey-mm-*).
   Names: mm-<area>-<what>, the areas as the Machinedrum's (doc/modern-ux/JOURNEYS.md), and perform. */
const MmJourneys = (() => {
	const { ok, sleep, until } = Journey;
	const $1 = q => document.querySelector(q), $all = q => [...document.querySelectorAll(q)];
	const S = () => window.MMDiagnostics.S, I = () => window.MMPage.inspect();
	const machine = () => I().machine || {};
	const desk = () => machine().desk || {};
	const cur = () => machine().pattern?.current ?? 0;
	const patDoc = () => I().doc("pattern", cur());
	const trigsOf = t => (patDoc()?.tracks[t]?.trig || []).slice().sort((a, b) => a - b);
	const slidesOf = t => (patDoc()?.tracks[t]?.slide || []).slice().sort((a, b) => a - b);
	const wk = () => I().workingKit?.doc;
	const kitT = t => JSON.stringify(wk()?.tracks[t]);
	const allKit = () => JSON.stringify(wk()?.tracks);
	const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
	const tab = ws => `#tabs button[data-ws="${ws}"]`;
	const onTab = ws => ok(S().ws === ws, "workspace " + S().ws);
	const synthMuted = i => ((machine().mutes?.synth ?? 0) >> i & 1) === 1;
	const synthMutes = () => [0, 1, 2, 3, 4, 5].filter(synthMuted);
	const pressed = q => $1(q)?.getAttribute("aria-pressed") === "true";
	const dlgShown = () => !$1("#dlg").hidden;
	const dlgButton = text => [...$1("#dlg").querySelectorAll("[data-dlg]")].find(b => b.textContent.trim() === text);
	const rail = t => `#rail .th[data-sel="${t}"] .nm`;
	const tele = { last: null, steps: [] }, results = [], sent = [];
	/* what the page hands the plug-in (for what has no read-back: live notes) */
	const notesOn = [];	/* the pitches the keyboard sent (noteOn), newest last */
	{ const send0 = Bridge.send; Bridge.send = (m, o) => { sent.push(m.op); if (sent.length > 50) sent.shift(); if (m.op === "noteOn") { notesOn.push(m.pitch); if (notesOn.length > 50) notesOn.shift(); } return send0(m, o); }; }
	Bridge.onMessage(m => {
		if (m.type === "telemetry") { tele.last = m; if (m.playing) { tele.steps.push(m.step); if (tele.steps.length > 64) tele.steps.shift(); } }
		if (m.type === "result") { results.push(m); if (results.length > 50) results.shift(); }
	});
	const PAGES = ["SYN", "AMP", "FLT", "EFX", "LF1", "LF2", "LF3"];
	const go = ws => ({ say: `click the ${{ seq: "Sequence", sound: "Sound", mix: "Mix", perform: "Perform", song: "Song" }[ws]} tab`, act: u => u.click(tab(ws)), screen: () => onTab(ws) });
	const sel = t => ({ say: `pick track ${t + 1} on the rail`, act: (u, c) => { c.t = t; if (S().side === "midi" && t < 6) u.click('[data-side="int"]'); u.click(rail(t)); }, screen: () => ok(S().sel === t, "selected " + S().sel) });
	/* the piano roll's Draw on (B on Sequence; Off: a press drags a selection box), as the journeys that click notes in
	   need it whatever an earlier journey left; drawBack in their tidy puts it as it was */
	const drawOn = { say: "the piano roll's Draw on", act: (u, c) => { c.d0 = S().rollDraw; if (!S().rollDraw) u.click("#rolldraw"); }, screen: () => ok(S().rollDraw && pressed("#rolldraw"), "Draw off") };
	const drawBack = c => { if (c.d0 != null && S().rollDraw !== c.d0) setRollDraw(c.d0); };
	const undoKey = { say: "press Cmd+Z", act: u => u.key("z", { cmd: true }) };
	const blur = () => document.activeElement?.blur?.();
	const confirmIfAsked = async u => { if (await until(dlgShown, 1200)) u.click($1("#dlg .danger") || dlgButton("Cancel")); };
	/* a select the page shows as a key-style dropdown (or plain) */
	const choose = async (u, id, v) => { if ($1(`.kselbtn[data-for="${id}"]`)) await u.pick(id, v); else u.choose("#" + id, v); };

	/* a cell of the big piano roll: the fractions of the canvas box at step s, the middle row's note */
	function rollCell(t, s) {
		const c = $1(`#seq canvas.roll[data-big][data-t="${t}"]`); if (!c) throw new Error("no piano roll for track " + (t + 1));
		const G = laneGeom(c), r = c.getBoundingClientRect(), q = G.col[s]; if (!q) throw new Error("step " + (s + 1) + " is not shown");
		const row = Math.floor(G.rows / 2), n = G.lo + G.rows - 1 - row;
		return { c, n, fx: (q.x0 + q.x1) / 2 / r.width, fy: (G.top + (row + 0.5) * G.rh) / r.height };
	}
	/* the bar of note pitch on step s: the step before it that holds anything is a note whose first pitch it is */
	const trkOf = t => t < 6 ? S().tracks[t] : S().midi[t - 6];
	const underBar = (t, s, pitch) => { const st = trkOf(t).steps; for (let k = s - 1; k >= 0; k--) if (st[k]) return !st[k].off && st[k].n?.[0] === pitch; return false; };
	const freeSteps = (t, n, from = 0, pitch = null) => { const busy = trigsOf(t), out = []; for (let s = from; s < S().len && out.length < n; s++) if (!busy.includes(s) && !S().tracks[t].steps[s] && (pitch == null || !underBar(t, s, pitch)) && !out.some(x => Math.abs(x - s) < 2)) out.push(s); return out; };

	/* ---------- start-up ---------- */
	const bootCard = {
		name: "mm-boot-card", boot: true,
		steps: [
			{ say: "open the editor: the start-up card covers the window while the firmware starts", within: 30000, screen: () => ok(typeof Boot !== "undefined" && ["loading", "booting"].includes(Boot.state()) && !$1("#bootcard").hidden, "card " + (typeof Boot !== "undefined" ? Boot.state() : "?")) },
			{ say: "click PLAY under the card: it is not pressed", act: (u, c) => { const r = $1("#play").getBoundingClientRect(), top = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); c.cover = top?.closest("#bootcard") ? "card" : top?.id === "modalbg" ? "backdrop" : (top?.id || top?.className); if (c.cover !== "card" && c.cover !== "backdrop") throw new Error("PLAY is reachable: " + c.cover); top.dispatchEvent(new MouseEvent("click", { bubbles: true })); },
				machine: () => ok(!tele.last?.playing, "the machine plays"), within: 800 },
			{ say: "the card shows the firmware's own LCD", screen: () => { const g = $1("#bootlcd")?.getContext("2d")?.getImageData(0, 0, 128, 64).data; if (!g) return "no LCD"; let n = 0; for (let i = 0; i < g.length; i += 4) if (g[i] !== g[0] || g[i + 1] !== g[1] || g[i + 2] !== g[2]) n++; return ok(n > 50, n + " pixels, card " + Boot.state()); }, within: 90000 },
			{ say: "the card goes once the machine takes input", screen: () => ok($1("#bootcard").hidden, "still shown"), machine: () => ok(!!machine().input, "no input yet"), within: 90000 }
		]
	};

	/* ---------- Sequence ---------- */
	const firstBeat = {
		name: "mm-seq-first-beat",
		steps: [
			{ say: "click the Sequence tab", act: u => u.click(tab("seq")), screen: () => onTab("seq") === true ? ok(!!$1("#seq canvas.roll[data-big]"), "no piano roll") : onTab("seq"), machine: () => ok(!!patDoc(), "pattern not read") },
			sel(0),
			{ say: "click four empty cells of the piano roll", act: async (u, c) => {
				/* A synth note's bar runs on to the next step that holds anything, and a click on a bar takes that note
				   (B-013): the cells are ones no bar of the middle row's note covers, clicked right to left, so a new
				   note's bar never reaches the next cell. */
				c.n = rollCell(c.t, 0).n;
				c.steps = [0, 4, 8, 12].map(k => freeSteps(c.t, 1, k, c.n)[0]).filter(s => s != null);
				if (c.steps.length < 4) throw new Error("no four free steps");
				for (const s of [...c.steps].reverse()) { const p = rollCell(c.t, s); u.click(p.c, {}, p.fx, p.fy); await sleep(250); }
				c.note = `track ${c.t + 1} steps ${c.steps.map(s => s + 1).join(" ")}, note ${c.n}`;
			}, screen: c => ok(c.steps.every(s => S().tracks[c.t].steps[s]?.n?.includes(c.n)), "roll shows " + c.steps.map(s => JSON.stringify(S().tracks[c.t].steps[s]?.n || null)).join(",")),
				machine: c => ok(c.steps.every(s => trigsOf(c.t).includes(s)), "pattern trigs " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "press PLAY (pressed again when the plug-in says the panel is busy: the clicks' dumps are on their way, B-014)", act: async (u, c) => { tele.steps = []; results.length = 0; u.click("#play");
				for (let i = 0; i < 5 && await until(() => results.some(r => r.op === "play" && r.ok === false), 1500); i++) { c.note = "the plug-in said the panel was busy (SYSEX RECV); pressed again"; results.length = 0; await sleep(1500); u.click("#play"); } }, screen: () => ok(/^\d\d\.\d\d$/.test($1("#pos").textContent), "POSITION " + $1("#pos").textContent), machine: () => ok(new Set(tele.steps).size >= 4, "telemetry steps " + tele.steps.join(",")), within: 20000 },
			{ say: "press STOP", act: u => u.click("#play"), screen: () => ok($1("#pos").textContent === "--.--", "POSITION " + $1("#pos").textContent), machine: () => ok(tele.last && !tele.last.playing, "still playing") }
		],
		async tidy(u, c) { if (S().playing) u.click("#play"); for (let i = 0; i < 8 && c.steps && c.steps.some(s => trigsOf(c.t).includes(s)); i++) { u.click("#undo"); await sleep(1500); } }
	};
	const spaceKey = {
		name: "mm-keys-space-play",
		steps: [
			{ say: "press Space: it plays (pressed again when the plug-in says the panel is busy)", act: async (u, c) => { blur(); tele.steps = []; results.length = 0; u.key(" ");
				for (let i = 0; i < 5 && await until(() => results.some(r => r.op === "play" && r.ok === false), 1500); i++) { c.note = "the plug-in said the panel was busy (SYSEX RECV); pressed again"; results.length = 0; await sleep(1500); u.key(" "); } }, screen: () => ok(S().playing, "not playing"), machine: () => ok(new Set(tele.steps).size >= 2, "telemetry " + tele.steps.join(",")), within: 6000 },
			{ say: "press Space again: it stops", act: u => u.key(" "), screen: () => ok(!S().playing, "playing"), machine: () => ok(tele.last && !tele.last.playing, "still playing") }
		]
	};
	/* B-024: the tempo is the machine's (1/24 BPM steps: a saved project's may be off the 0.1 grid), the LCD shows it to one
	   decimal: compared as the LCD rounds it */
	const lcdBpm = v => +(+v).toFixed(1);
	const tempoDrag = {
		name: "mm-top-tempo-drag",
		steps: [
			{ say: "drag the BPM on the LCD up", act: async (u, c) => { c.b0 = machine().tempo; await u.drag("#bpm", [[0, -6], [0, -12], [0, -18], [0, -24]]); c.note = `${c.b0} -> ${S().bpm}`; }, screen: c => ok(parseFloat($1("#bpm").textContent) === lcdBpm(S().bpm) && S().bpm > c.b0, `LCD ${$1("#bpm").textContent} for ${S().bpm} (from ${c.b0})`), machine: c => ok(machine().tempo > c.b0, "tempo " + machine().tempo) },
			{ say: "drag it back down as far", act: u => u.drag("#bpm", [[0, 6], [0, 12], [0, 18], [0, 24]]), screen: c => ok(parseFloat($1("#bpm").textContent) === lcdBpm(c.b0), "LCD " + $1("#bpm").textContent), machine: c => ok(Math.abs(machine().tempo - c.b0) < 0.05, "tempo " + machine().tempo) }
		]
	};
	const patNext = {
		name: "mm-top-pattern-next",
		steps: [
			{ say: "click › next to the pattern on the LCD", act: async (u, c) => { c.p0 = cur(); u.click("#patNext"); await confirmIfAsked(u); }, machine: c => ok(cur() === c.p0 + 1, "machine pattern " + cur()), screen: c => ok(S().pat === c.p0 + 1, "page pattern " + S().pat), within: 8000 },
			{ say: "click ‹: back", act: async u => { u.click("#patPrev"); await confirmIfAsked(u); }, machine: c => ok(cur() === c.p0, "machine pattern " + cur()), within: 8000 }
		]
	};
	const wsKeys = {
		name: "mm-keys-workspaces",
		steps: [["2", "sound"], ["3", "mix"], ["4", "perform"], ["5", "song"], ["1", "seq"]].map(([k, ws]) => ({ say: `press ${k}`, act: u => { blur(); u.key(k); }, screen: () => ok(S().ws === ws && !!$1("#main").firstElementChild, "workspace " + S().ws) }))
	};
	const helpKeys = {
		name: "mm-keys-help",
		steps: [
			{ say: "press ?: the keyboard view and the list of keys", act: u => { blur(); u.key("?", { shift: true }); }, screen: () => ok(!$1("#keyspop").hidden && $all("#keyspop .keyrow").length > 20 && !!$1("#keyspop .kv-cap"), $all("#keyspop .keyrow").length + " keys") },
			{ say: "press Escape: it closes", act: u => u.key("Escape"), screen: () => ok($1("#keyspop").hidden, "still open") }
		]
	};
	const plate = {
		name: "mm-top-plate",
		steps: [
			{ say: "click the plate key", act: (u, c) => { c.p0 = document.documentElement.dataset.plate; u.click("#platekey"); }, screen: c => ok(document.documentElement.dataset.plate !== c.p0, "plate " + document.documentElement.dataset.plate) },
			{ say: "click it again", act: u => u.click("#platekey"), screen: c => ok(document.documentElement.dataset.plate === c.p0, "plate " + document.documentElement.dataset.plate) }
		]
	};
	const undoRedo = {
		name: "mm-top-undo-redo",
		steps: [
			go("seq"), sel(0),
			{ say: "click an empty cell of the roll", act: (u, c) => { c.s = freeSteps(0, 1, 6)[0]; const p = rollCell(0, c.s); u.click(p.c, {}, p.fx, p.fy); }, machine: c => ok(trigsOf(0).includes(c.s), "no trig"), within: 10000 },
			{ say: "press Cmd+Z", act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(!trigsOf(0).includes(c.s), "trig still there"), screen: c => ok(!S().tracks[0].steps[c.s], "roll still shows it"), within: 10000 },
			{ say: "press Cmd+Shift+Z", act: u => u.key("z", { cmd: true, shift: true }), machine: c => ok(trigsOf(0).includes(c.s), "no trig"), within: 10000 },
			{ say: "click Undo", act: u => u.click("#undo"), machine: c => ok(!trigsOf(0).includes(c.s), "trig still there"), within: 10000 }
		]
	};
	const gridRecord = {
		name: "mm-seq-grid-record",
		steps: [
			{ say: "click RECORD (stopped): GRID RECORDING (pressed again when the plug-in says the panel is busy)", act: async (u, c) => { results.length = 0; c.seen = []; c.recv0 = machine().recv?.state; c.t0 = performance.now(); u.click("#rec");
				for (let i = 0; i < 5 && await until(() => results.some(r => r.op === "record" && r.ok === false), 1000); i++) { c.note = `the plug-in said the panel was busy (SYSEX RECV, recv ${machine().recv?.state}); pressed again after ${Math.round(performance.now() - c.t0)} ms`; results.length = 0; await sleep(500); u.click("#rec"); } },
				machine: c => { const r = tele.last?.record; if (c.seen[c.seen.length - 1] !== r) c.seen.push(r); return ok(r === "grid", `record ${r} (seen ${c.seen.join(">")}); results ${results.map(x => x.op + ":" + x.ok + (x.errors ? " " + x.errors.join(";") : "") + (x.note ? " " + x.note : "")).join(", ")}; recv ${c.recv0} -> ${machine().recv?.state}`); }, within: 6000 },
			{ say: "click RECORD again: off (pressed again when the plug-in says the panel is busy)", act: async (u, c) => { results.length = 0; u.click("#rec");
				for (let i = 0; i < 5 && await until(() => results.some(r => r.op === "record" && r.ok === false), 1500); i++) { c.note = "the plug-in said the panel was busy; pressed again"; results.length = 0; await sleep(1500); u.click("#rec"); } },
				machine: () => ok(tele.last?.record === "off", "record " + tele.last?.record), within: 6000 }
		],
		async tidy(u) { if (tele.last?.record && tele.last.record !== "off") u.click("#rec"); }
	};
	const slidePaint = {
		name: "mm-seq-slide-paint",
		steps: [
			go("seq"), sel(0),
			{ say: "drag across four SLIDE steps: on, one undo step", act: async (u, c) => {
				c.s0 = [...Array(S().len - 3).keys()].find(s => [0, 1, 2, 3].every(k => !slidesOf(0).includes(s + k))); if (c.s0 == null) throw new Error("no four free SLIDE steps");
				c.run = [0, 1, 2, 3].map(k => c.s0 + k); c.u0 = machine().history?.undoCount || 0;
				await u.drag(`.tc[data-tl="sld"][data-s="${c.s0}"]`, c.run.slice(1).map(s => `.tc[data-tl="sld"][data-s="${s}"]`));
			}, machine: c => ok(c.run.every(s => slidesOf(0).includes(s)), "slides " + slidesOf(0).join(",")), screen: c => ok(c.run.every(s => S().tracks[0].slide.has(s)), "lane not lit"), within: 15000 },
			{ say: "one undo step: Cmd+Z takes all four", act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(c.run.every(s => !slidesOf(0).includes(s)), "slides " + slidesOf(0).join(",")), within: 15000 }
		]
	};
	const lenKey = {
		name: "mm-seq-len",
		steps: [
			go("seq"),
			{ say: "click LEN on the LCD: the next length", act: (u, c) => { c.l0 = patDoc().length; u.click('[data-l2="len"]'); }, machine: c => ok(patDoc().length !== c.l0 && patDoc().length === S().len, "length " + patDoc().length), screen: c => ok($1('[data-l2="len"] b')?.textContent === String(S().len), "LCD " + $1('[data-l2="len"] b')?.textContent), within: 20000 },
			{ say: "shift-click LEN: back", act: u => u.click('[data-l2="len"]', { shift: true }), machine: c => ok(patDoc().length === c.l0, "length " + patDoc().length), within: 20000 }
		]
	};
	const lockLane = {
		name: "mm-seq-lock-lane",
		steps: [
			go("seq"),
			{ say: "pick a track with notes, the Locks tab", act: async (u, c) => { c.t = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).length >= 2) ?? 0; u.click(rail(c.t)); await sleep(200); if ($1('[data-dock="locks"]')) u.click('[data-dock="locks"]'); }, screen: c => ok(S().sel === c.t && !!$1("#lane"), "no lane"), machine: c => ok(trigsOf(c.t).length >= 2, "fewer than two notes") },
			{ say: "draw across the lane over the notes", act: async (u, c) => { c.lane = S().lane; [c.pg, c.pi] = [PAGES.indexOf(c.lane.split(".")[0]), +c.lane.split(".")[1]]; c.on = trigsOf(c.t).filter(s => s < S().len).slice(0, 4); await u.drag(`#lane .lb[data-s="${c.on[0]}"]`, c.on.slice(1).map(s => `#lane .lb[data-s="${s}"]`), {}, { fy: 0.3 }); c.note = `${c.lane} on ${c.on.map(s => s + 1).join(" ")}`; },
				machine: c => { const l = (patDoc().locks || []).find(x => x.track === c.t && x.page === c.pg && x.param === c.pi); return ok(l && c.on.every(s => l.steps.some(([x]) => x === s)), "locks " + JSON.stringify(l)); }, within: 15000 },
			{ say: "click the lane's clear key", act: u => u.click("#clearLane"), machine: c => ok(!(patDoc().locks || []).some(x => x.track === c.t && x.page === c.pg && x.param === c.pi), "locks left"), within: 15000 }
		]
	};
	const arpDock = {
		name: "mm-seq-arp",
		steps: [
			go("seq"), sel(0),
			{ say: "click the Arp tab and an arpeggiator mode", act: async (u, c) => { u.click('[data-dock="arp"]'); await sleep(300); c.m0 = patDoc().tracks[0].arp?.mode; const b = $all('[data-set="arpmode"] button').find(x => +x.dataset.v !== S().tracks[0].arp.MODE && +x.dataset.v > 0); c.v = +b.dataset.v; u.click(b); },
				machine: c => ok(patDoc().tracks[0].arp?.mode === c.v, "arp mode " + patDoc().tracks[0].arp?.mode), screen: c => ok(pressed(`[data-set="arpmode"] button[data-v="${c.v}"]`), "not lit"), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(patDoc().tracks[0].arp?.mode === c.m0, "arp mode " + patDoc().tracks[0].arp?.mode), within: 15000 }
		],
		async tidy(u) { if ($1('[data-dock="locks"]')) u.click('[data-dock="locks"]'); }
	};
	/* 0.3.5: RNGE up to the machine's own end, 9 OCT (range 8, the knob's limit measured on the firmware; a 2008 backup
	   holds it): shown and kept, not snapped back */
	const rnge = '.pc[data-g="arp"][data-n="RNGE"]';
	const arpRange = {
		name: "mm-seq-arp-range",
		steps: [
			go("seq"), sel(0),
			{ say: "click the Arp tab, scroll RNGE up to its end", act: async (u, c) => { u.click('[data-dock="arp"]'); await sleep(300); c.r0 = patDoc().tracks[0].arp?.range; for (let k = 0; k < 10; k++) { u.wheel(rnge, 1); await sleep(120); } },
				machine: () => ok(patDoc().tracks[0].arp?.range === 8, "arp range " + patDoc().tracks[0].arp?.range), screen: () => ok($1(rnge + " b")?.textContent === "9 OCT", $1(rnge + " b")?.textContent), within: 15000 },
			{ say: "scroll it once more: it stays at 9 OCT", act: u => u.wheel(rnge, 1),
				machine: () => ok(patDoc().tracks[0].arp?.range === 8, "arp range " + patDoc().tracks[0].arp?.range), screen: () => ok($1(rnge + " b")?.textContent === "9 OCT", $1(rnge + " b")?.textContent), within: 3000 }
		],
		async tidy(u, c) {
			if (c.r0 != null && $1(rnge)) { for (let k = 0; k < 8 - c.r0; k++) { u.wheel(rnge, -1); await sleep(120); } await sleep(3000); }
			if ($1('[data-dock="locks"]')) u.click('[data-dock="locks"]');
		}
	};
	const trnKeys = {
		name: "mm-seq-transpose-keyboard",
		steps: [
			go("seq"), sel(0),
			{ say: "click the Transpose tab and the G key of its keyboard", act: async (u, c) => { u.click('[data-dock="trn"]'); await sleep(300); c.t0 = patDoc().tracks[0].transpose; c.v0 = S().tracks[0].tr.TRACK; const cv = $1('#dkvis canvas[data-ed="dktrn"]'), r = cv.getBoundingClientRect(), k = ED.dktrn.keys(r.width, r.height).find(x => x.n === 7);
				u.click(cv, {}, (k.x + k.w / 2) / r.width, (k.y + k.h * 0.85) / r.height); c.note = `TRACK ${c.v0} -> ${S().tracks[0].tr.TRACK}`; },
				machine: c => ok(patDoc().tracks[0].transpose !== c.t0, `transpose ${JSON.stringify(patDoc().tracks[0].transpose)} (was ${JSON.stringify(c.t0)}), page TRACK ${S().tracks[0].tr.TRACK}`), screen: c => ok(S().tracks[0].tr.TRACK !== c.v0, "TRACK " + S().tracks[0].tr.TRACK), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(patDoc().tracks[0].transpose === c.t0, "transpose " + patDoc().tracks[0].transpose), within: 15000 }
		],
		async tidy(u) { if ($1('[data-dock="locks"]')) u.click('[data-dock="locks"]'); }
	};

	/* ---------- generators ---------- */
	const genMut = {
		name: "mm-gen-mutate-undo",
		steps: [
			go("seq"), sel(0),
			{ say: "the GEN bar offers track 1's generator", screen: () => ok(!!$1("#genband [data-rand]"), "no GEN bar") },
			{ say: "click GEN's Random key until the notes change", act: async (u, c) => { c.p0 = JSON.stringify(patDoc().tracks[0]); for (let i = 0; i < 5; i++) { u.click("#genband [data-rand]"); if (await until(() => JSON.stringify(patDoc().tracks[0]) !== c.p0, 4000)) break; } },
				machine: c => ok(JSON.stringify(patDoc().tracks[0]) !== c.p0, "pattern unchanged"), within: 15000 },
			{ say: "click the Sound tab and MUTATE's Random key", act: async (u, c) => { u.click(tab("sound")); await sleep(300); c.k0 = kitT(0); u.click("#mutband [data-rand]"); }, machine: c => ok(kitT(0) !== c.k0, "kit unchanged"), within: 15000 },
			{ say: "click Undo: the sound is back", act: u => u.click("#undo"), machine: c => ok(kitT(0) === c.k0, "kit not back"), within: 15000 },
			{ say: "click Undo again: the notes are back", act: u => u.click("#undo"), machine: c => ok(JSON.stringify(patDoc().tracks[0]) === c.p0, "pattern not back"), within: 15000 }
		]
	};

	/* ---------- Sound ---------- */
	const valueText = q => $1(q)?.querySelector("b")?.textContent;
	const shapeSound = {
		name: "mm-sound-shape-undo",
		steps: [
			{ say: "click the Sound tab: grouped by function", act: u => u.click(tab("sound")), screen: () => onTab("sound") === true ? ok($all("#main .sg").length >= 4, $all("#main .sg").length + " groups") : onTab("sound"), machine: () => ok(!!wk(), "no working kit") },
			sel(0),
			{ say: "drag a filter value sideways", act: async (u, c) => {
				const el = $1('#main .pc[data-g="FLT"]') || $1('#main .pc[data-g="AMP"]'); if (!el) throw new Error("no FLT or AMP value");
				c.g = el.dataset.g; c.i = +el.dataset.n; c.pg = PAGES.indexOf(c.g); c.q = `#main .pc[data-g="${c.g}"][data-n="${c.i}"]`;
				c.v0 = wk().tracks[0].pages[c.pg][c.i]; c.txt0 = valueText(c.q); const d = c.v0 > 64 ? -1 : 1;
				await u.drag(c.q, [[d * 6, 0], [d * 12, 0], [d * 18, 0], [d * 24, 0], [d * 30, 0]]);
				c.note = `${c.g} ${c.i}: ${c.v0} -> ${S().tracks[0].v[c.g][c.i]}`;
			}, screen: c => ok(valueText(c.q) !== c.txt0, "shows " + valueText(c.q)), machine: c => ok(wk().tracks[0].pages[c.pg][c.i] !== c.v0, "kit value " + wk().tracks[0].pages[c.pg][c.i]), within: 8000 },
			{ say: "click Undo", act: u => u.click("#undo"), screen: c => ok(valueText(c.q) === c.txt0, "shows " + valueText(c.q)), machine: c => ok(wk().tracks[0].pages[c.pg][c.i] === c.v0, "kit value " + wk().tracks[0].pages[c.pg][c.i]), within: 8000 }
		]
	};
	const machinePick = {
		name: "mm-sound-machine-pick-undo",
		steps: [
			go("sound"), sel(0),
			{ say: "click the machine key: the picker opens", act: (u, c) => { c.k0 = kitT(0); c.m0 = S().tracks[0].m; u.click("#machbtn"); }, screen: () => ok(!$1("#machpop").hidden && $all("#machpop .mk").length > 1, "picker closed") },
			{ say: "pick another machine", act: (u, c) => { const m = $all("#machpop .mk").find(b => b.dataset.mach !== c.m0 && !b.disabled); c.m1 = m.dataset.mach; u.click(m); }, screen: c => ok($1("#machpop").hidden && S().tracks[0].m === c.m1, "view " + S().tracks[0].m), machine: c => ok(kitT(0) !== c.k0, "kit unchanged"), within: 10000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(kitT(0) === c.k0, "kit not back"), screen: c => ok(S().tracks[0].m === c.m0, "view " + S().tracks[0].m), within: 10000 }
		]
	};
	/* B-027: a machine picked on Sound stays when the person goes back to Sequence and clicks a step there: the step's
	   dump makes the Monomachine load the pattern's kit from its slot, and the editor sends the kit's edits again */
	const machineStays = {
		name: "mm-sound-machine-stays",
		steps: [
			go("sound"), sel(0),
			{ say: "pick another machine", act: async (u, c) => { c.k0 = kitT(0); c.m0 = S().tracks[0].m; u.click("#machbtn"); await sleep(300); const m = $all("#machpop .mk").find(b => b.dataset.mach !== c.m0 && !b.disabled); c.m1 = m.dataset.mach; u.click(m); },
				screen: c => ok(S().tracks[0].m === c.m1, "view " + S().tracks[0].m), machine: c => ok(kitT(0) !== c.k0, "kit unchanged"), within: 10000 },
			go("seq"),
			{ say: "click an empty cell of the piano roll (a dump of the pattern that plays)",
				act: (u, c) => { c.n = rollCell(0, 0).n; c.s = freeSteps(0, 1, 0, c.n)[0]; if (c.s == null)
				throw new Error("no free step"); const p = rollCell(0, c.s); u.click(p.c, {}, p.fx, p.fy); },
				machine: c => ok(trigsOf(0).includes(c.s), "pattern trigs " + trigsOf(0).join(",")), within: 15000 },
			{ say: "wait on Sequence: the machine stays", act: () => sleep(5000),
				screen: c => ok(S().tracks[0].m === c.m1, "view " + S().tracks[0].m),
				machine: c => ok(kitT(0) !== c.k0 && machine().kit?.working === "edited", "kit back to " + kitT(0)
				+ ", " + machine().kit?.working), within: 2000 },
			go("sound"),
			{ say: "and on Sound again", act: () => sleep(1000), screen: c => ok(S().tracks[0].m === c.m1, "view " + S().tracks[0].m), machine: c => ok(kitT(0) !== c.k0, "kit back to " + kitT(0)), within: 2000 }
		],
		async tidy(u, c) {
			blur();
			if (c.s != null && trigsOf(0).includes(c.s)) { u.key("z", { cmd: true }); await sleep(3000); }
			if (c.k0 != null && kitT(0) !== c.k0) { u.key("z", { cmd: true }); await sleep(3000); }
		}
	};
	/* 0.3.5: the Song page's PATTERN | SONG switch: what is lit is the status the machine reports */
	const songModeJ = {
		name: "mm-song-mode",
		steps: [
			go("song"),
			{ say: "click SONG next to Plays: the machine reports song mode", act: (u, c) => { c.m0 = machine().song?.songMode === true; u.click('[data-seqmode="song"]'); },
				machine: () => ok(machine().song?.songMode === true, "songMode " + machine().song?.songMode), screen: () => ok(pressed('[data-seqmode="song"]') && !pressed('[data-seqmode="pattern"]'), "SONG not lit"), within: 8000 },
			{ say: "click PATTERN: the machine reports pattern mode", act: u => u.click('[data-seqmode="pattern"]'),
				machine: () => ok(machine().song?.songMode === false, "songMode " + machine().song?.songMode), screen: () => ok(pressed('[data-seqmode="pattern"]') && !pressed('[data-seqmode="song"]'), "PATTERN not lit"), within: 8000 }
		],
		async tidy(u, c) { if (c.m0 && machine().song?.songMode !== true) { u.click('[data-seqmode="song"]'); await sleep(2000); } }
	};
	/* 0.3.5: the Song page's playhead. An empty song (the song picker, then Load on the machine) gets two rows, A01 and
	   A02; SONG mode, PLAY: the arrangement marks the row the machine plays (telemetry songRow, RAM), the What plays
	   line says it, and the mark moves on to the next row; STOP takes it away. */
	const emptySongMm = cur => { for (let d = 1; d < 24; d++) for (const s of [(cur + d) % 24, (cur - d + 24) % 24]) { const g = I().doc("song", s); if (g && g.rows.length && g.rows[0].kind === "end") return s; } return null; };
	/* the LCD's box and the places of its fields (0.3.5: the same in PATTERN and SONG mode) */
	const lcdGeo = () => [$1(".lcdpanel"), $1('#lcd2 [data-l2="seqmode"]'), $1("#kitname"), $1("#bpm")].map(e => { const r = e?.getBoundingClientRect(); return r ? `${Math.round(r.left)}/${Math.round(r.width)}` : "-"; }).join(" ");
	const markedMm = c => { const i = $1("#tl .scell.ph")?.dataset.i; if (i != null) c.seen.add(+i); return [...c.seen].join(","); };
	const songPlayhead = {
		name: "mm-song-playhead",
		steps: [
			go("song"),
			{ say: "stop, pick an empty song in the song picker and click Load on the machine", act: async (u, c) => { if (S().playing) { u.click("#play"); await until(() => !S().playing, 3000); } c.cur = machine().song?.current ?? 0; c.m0 = machine().song?.songMode === true; await until(() => I().slots("song").length >= 24, 20000); c.s = emptySongMm(c.cur); if (c.s == null) throw new Error("no empty song"); await choose(u, "songsel", c.s); await until(() => S().songs?.slot === c.s, 4000); await sleep(300); u.click("#songload"); },
				machine: c => ok(machine().song?.current === c.s, "machine song " + machine().song?.current), within: 15000 },
			{ say: "Arrange: click pads A01 and A02, two rows of the machine's song", act: async (u, c) => { u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); u.click('[data-addpat="0"]'); await until(() => songDoc()?.rows.length === 2, 8000); u.click('[data-addpat="1"]'); },
				machine: () => ok(songDoc()?.rows.length === 3 && songDoc().rows[0].pattern === 0 && songDoc().rows[1].pattern === 1, "rows " + JSON.stringify(songDoc()?.rows.map(r => r.pattern ?? r.kind))), within: 15000 },
			{ say: "click PATTERN: the header says pattern mode, the grid looks as ever", act: (u, c) => u.click('[data-seqmode="pattern"]'),
				screen: c => ok(!!$1(".songui.patmode") && /pattern mode/i.test($1("#arrstate")?.textContent || "") && /^PATTERN /.test($1("#songPlays")?.textContent || "") && (c.geo = lcdGeo()), "plays " + $1("#songPlays")?.textContent), machine: () => ok(machine().song?.songMode === false, "songMode " + machine().song?.songMode), within: 8000 },
			{ say: "click SONG: the LCD keeps its size and every field its place; the desk loads the edited song by itself (no LOAD SONG by hand)", act: u => u.click('[data-seqmode="song"]'),
				screen: c => ok(!!$1(".songui.songmode") && $1('#lcd2 [data-l2="seqmode"] b')?.textContent === "SONG" && lcdGeo() === c.geo && /song mode/i.test($1("#arrstate")?.textContent || ""), `LCD ${lcdGeo()} (was ${c.geo}); ${$1('#lcd2 [data-l2="seqmode"]')?.textContent}; header ${$1("#arrstate")?.textContent}`),
				machine: () => ok(machine().song?.songMode === true && machine().song?.reloadNeeded === false, "songMode " + machine().song?.songMode + ", reload needed " + machine().song?.reloadNeeded), within: 15000 },
			{ say: "press PLAY (pressed again when the plug-in says the panel is busy): row 001 is marked", act: async (u, c) => { c.seen = new Set(); results.length = 0; u.click("#play");
				for (let i = 0; i < 5 && await until(() => results.some(r => r.op === "play" && r.ok === false), 1500); i++) { c.note = "the panel was busy (SYSEX RECV); pressed again"; results.length = 0; await sleep(1500); u.click("#play"); } },
				screen: c => ok(markedMm(c).length > 0 && c.seen.has(0) && /row 001 of 2/.test($1("#songPlays")?.textContent || ""), `marked ${[...c.seen]}; plays ${$1("#songPlays")?.textContent}`), machine: () => ok(tele.last?.playing && tele.last.songRow === 0, `songRow ${tele.last?.songRow} playing ${tele.last?.playing}; results ${results.slice(-4).map(r => r.op + ":" + r.ok + (r.errors ? " " + r.errors.join(";") : "")).join(", ")}; rows ${JSON.stringify(songDoc()?.rows)}`), within: 20000 },
			{ say: "the mark moves on to row 002", act: async (u, c) => { await until(() => (markedMm(c), c.seen.has(1)), 30000); },
				screen: c => ok((markedMm(c), c.seen.has(1)), `marked ${[...c.seen]}`), machine: () => ok(tele.last?.songRow === 1, "songRow " + tele.last?.songRow), within: 30000 },
			{ say: "press STOP: no row is marked", act: u => { if (S().playing) u.click("#play"); }, screen: () => ok(!$1("#tl .scell.ph"), "still marked"), machine: () => ok(!S().playing, "playing"), within: 8000 }
		],
		async tidy(u, c) {
			if (S().playing) { u.click("#play"); await until(() => !S().playing, 3000); }
			if (c.s != null && S().songs?.slot === c.s) await undoUntil(u, () => songDoc()?.rows.length === 1, 4);
			if (c.m0 === false && machine().song?.songMode !== false) { u.click('[data-seqmode="pattern"]'); await sleep(1500); }
			if (c.cur != null) { await choose(u, "songsel", c.cur); await sleep(800); if (!$1("#songload")?.disabled) u.click("#songload"); await sleep(1500); }
			if (c.m0 != null && (machine().song?.songMode === true) !== c.m0) { u.click(`[data-seqmode="${c.m0 ? "song" : "pattern"}"]`); await sleep(2000); }
		}
	};
	const midiSide = {
		name: "mm-sound-midi-side",
		steps: [
			go("sound"),
			{ say: "click MIDI: the MIDI tracks", act: u => u.click('[data-side="midi"]'), screen: () => ok(S().side === "midi" && S().sel >= 6 && $all("#main .sg").length >= 1, `side ${S().side}, track ${S().sel}`) },
			{ say: "click SYNTH: back", act: u => u.click('[data-side="int"]'), screen: () => ok(S().side !== "midi" && S().sel < 6, `side ${S().side}`) }
		]
	};
	const controlAll = {
		name: "mm-sound-control-all",
		steps: [
			go("sound"), sel(0),
			{ say: "Alt-drag a filter value: all six synth tracks move", act: async (u, c) => { c.k0 = wk().tracks.map(t => JSON.stringify(t.pages)); const el = $1('#main .pc[data-g="FLT"]'); const d = (S().tracks[0].v.FLT[+el.dataset.n] ?? 0) > 64 ? -1 : 1; await u.drag(el, [[d * 6, 0], [d * 12, 0], [d * 18, 0], [d * 24, 0]], { alt: true }); },
				machine: c => { const n = wk().tracks.filter((t, i) => JSON.stringify(t.pages) !== c.k0[i]).length; return ok(n >= 2, n + " tracks moved"); }, within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(wk().tracks.every((t, i) => JSON.stringify(t.pages) === c.k0[i]), "not all back"), within: 15000 }
		]
	};

	/* ---------- Mix ---------- */
	const strip = (t, q) => `.strip6[data-sel="${t}"] ${q}`;
	const mixStrip = {
		name: "mm-mix-level-mute",
		steps: [
			{ say: "click the Mix tab", act: u => u.click(tab("mix")), screen: () => onTab("mix") === true ? ok($all(".strip6").length === 6, $all(".strip6").length + " strips") : onTab("mix"), machine: () => ok(machine().mutes?.synth != null, "mutes not read") },
			{ say: "drag track 1's LEVEL fader", act: async (u, c) => { c.l0 = wk().levels[0]; c.txt0 = $1(strip(0, ".lread")).textContent; const d = c.l0 > 64 ? 1 : -1; await u.drag(strip(0, '.fader[data-g="lev"]'), [[0, d * 8], [0, d * 16], [0, d * 24], [0, d * 32], [0, d * 40]]); c.note = `LEVEL ${c.l0} -> ${S().tracks[0].lev}`; },
				screen: c => ok($1(strip(0, ".lread")).textContent !== c.txt0 && $1(strip(0, ".lread")).textContent === String(S().tracks[0].lev), "shows " + $1(strip(0, ".lread")).textContent), machine: c => ok(wk().levels[0] !== c.l0 && wk().levels[0] === S().tracks[0].lev, "kit level " + wk().levels[0]), within: 8000 },
			{ say: "click Undo: the level is back", act: u => u.click("#undo"), screen: c => ok($1(strip(0, ".lread")).textContent === c.txt0, "shows " + $1(strip(0, ".lread")).textContent), machine: c => ok(wk().levels[0] === c.l0, "kit level " + wk().levels[0]), within: 8000 },
			{ say: "click track 1's M key", act: (u, c) => { c.m0 = synthMuted(0); u.click(strip(0, ".ms.m")); }, screen: c => ok($1(strip(0, ".ms.m")).getAttribute("aria-pressed") === String(!c.m0), "M " + $1(strip(0, ".ms.m")).getAttribute("aria-pressed")), machine: c => ok(synthMuted(0) === !c.m0, "machine mute " + synthMuted(0)) },
			{ say: "click M again", act: u => u.click(strip(0, ".ms.m")), screen: c => ok($1(strip(0, ".ms.m")).getAttribute("aria-pressed") === String(c.m0), "M " + $1(strip(0, ".ms.m")).getAttribute("aria-pressed")), machine: c => ok(synthMuted(0) === c.m0, "machine mute " + synthMuted(0)) }
		]
	};
	const mixSolo = {
		name: "mm-mix-solo",
		steps: [
			go("mix"),
			{ say: "click S on track 3: the other synth tracks are muted", act: (u, c) => { c.m0 = synthMutes(); u.click(strip(2, ".ms.s")); }, machine: () => ok(same(synthMutes(), [0, 1, 3, 4, 5]), "machine mutes " + synthMutes()), screen: () => ok(pressed(strip(2, ".ms.s")), "S not lit") },
			{ say: "click S again: the mutes are as before", act: u => u.click(strip(2, ".ms.s")), machine: c => ok(same(synthMutes(), c.m0), "machine mutes " + synthMutes()) }
		]
	};
	const shiftMutes = {
		name: "mm-mix-shift-mutes",
		steps: [
			go("mix"),
			{ say: "hold Shift and click two M keys: prepared, nothing sent", act: (u, c) => { c.m0 = synthMutes(); c.ts = [0, 1, 2, 3, 4, 5].filter(i => !c.m0.includes(i)).slice(0, 2); for (const i of c.ts) u.click(strip(i, ".ms.m"), { shift: true }); },
				screen: c => ok(c.ts.every(i => $1(strip(i, ".ms.m")).classList.contains("prep")), "not prepared"), machine: c => ok(same(synthMutes(), c.m0), "sent while held"), within: 1000 },
			{ say: "let Shift go: both mute together", act: () => document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift", code: "ShiftLeft", bubbles: true })), machine: c => ok(c.ts.every(synthMuted), "machine mutes " + synthMutes()) },
			{ say: "click both M keys again", act: async (u, c) => { for (const i of c.ts) { u.click(strip(i, ".ms.m")); await sleep(150); } }, machine: c => ok(same(synthMutes(), c.m0), "machine mutes " + synthMutes()) }
		]
	};
	const routing = {
		name: "mm-mix-routing",
		steps: [
			go("mix"),
			{ say: "click another routing mode", act: (u, c) => { c.g = machine().global?.current ?? 0; c.r0 = I().doc("global", c.g)?.routingMode; c.v0 = S().routing; c.v = ["3xSTEREO", "6xMONO"].find(v => v !== c.v0); u.click(`[data-set="routing"] button[data-v="${c.v}"]`); },
				machine: c => ok(I().doc("global", c.g)?.routingMode !== c.r0, "routing " + I().doc("global", c.g)?.routingMode), screen: c => ok(pressed(`[data-set="routing"] button[data-v="${c.v}"]`), "not lit"), within: 15000 },
			{ say: "click the first one again", act: (u, c) => u.click(`[data-set="routing"] button[data-v="${c.v0}"]`), machine: c => ok(I().doc("global", c.g)?.routingMode === c.r0, "routing " + I().doc("global", c.g)?.routingMode), within: 15000 }
		]
	};
	const panTrim = {
		name: "mm-mix-pan-undo",
		steps: [
			go("mix"),
			{ say: "drag track 2's PAN bar sideways", act: async (u, c) => { c.v0 = wk().tracks[1].pages[1][6]; const d = c.v0 > 64 ? -1 : 1; await u.drag(strip(1, ".pc.pan"), [[d * 8, 0], [d * 16, 0], [d * 24, 0]]); }, machine: c => ok(wk().tracks[1].pages[1][6] !== c.v0, "PAN " + wk().tracks[1].pages[1][6]), within: 8000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(wk().tracks[1].pages[1][6] === c.v0, "PAN " + wk().tracks[1].pages[1][6]), within: 8000 }
		]
	};
	const msOff = {
		name: "mm-mix-ms-off",
		steps: [
			go("seq"),
			{ say: "mute two tracks on the rail", act: async () => { }, machine: () => true },
			{ say: "click M on tracks 1 and 2, then M/S off", act: async u => { for (const i of [0, 1]) if (!synthMuted(i)) { u.click(`#rail .th[data-sel="${i}"] .ms.m`); await sleep(400); } await until(() => synthMuted(0) && synthMuted(1), 4000); u.click("#allon"); }, machine: () => ok(!synthMutes().length, "machine mutes " + synthMutes()), screen: () => ok($1("#allon").disabled, "M/S off still on") }
		]
	};

	/* ---------- Perform ---------- */
	const poly = {
		name: "mm-perform-poly",
		steps: [
			go("perform"),
			{ say: "click POLY", act: u => u.click('[data-pmode="poly"]'), machine: () => ok(machine().poly === true, "poly " + machine().poly), screen: () => ok(pressed('[data-pmode="poly"]'), "not lit"), within: 8000 },
			{ say: "click Auto track: POLY off", act: u => u.click('[data-pmode="normal"]'), machine: () => ok(machine().poly === false, "poly " + machine().poly), within: 8000 }
		]
	};
	const multiTrig = {
		name: "mm-perform-multi-trig",
		steps: [
			go("perform"),
			{ say: "click Multi trig and SPLIT KEY", act: async (u, c) => { c.m0 = wk().multiTrig?.mode; u.click('[data-pmode="multi"]'); await sleep(300); c.v = c.m0 === 1 ? 0 : 1; u.click(`[data-set="mtmode"] button[data-v="${c.v}"]`); }, machine: c => ok(wk().multiTrig?.mode === c.v, "multi trig " + JSON.stringify(wk().multiTrig)), within: 15000 },
			{ say: "click the first mode back, then Auto track", act: async (u, c) => { u.click(`[data-set="mtmode"] button[data-v="${c.m0}"]`); await sleep(300); u.click('[data-pmode="normal"]'); }, machine: c => ok(wk().multiTrig?.mode === c.m0, "multi trig " + JSON.stringify(wk().multiTrig)), within: 15000 }
		]
	};
	const multiMap = {
		name: "mm-perform-multi-map",
		steps: [
			go("perform"),
			{ say: "click Multi map: the map table", act: (u, c) => { c.g = machine().global?.current ?? 0; c.m0 = JSON.stringify(I().doc("global", c.g)?.multiMap); u.click('[data-pmode="map"]'); }, screen: () => ok(!!$1("table.mmap"), "no table") },
			{ say: "click Split selected range", act: (u, c) => { c.n0 = $all("table.mmap tbody tr").length; u.click("[data-madd]"); }, screen: c => ok($all("table.mmap tbody tr").length === c.n0 + 1, $all("table.mmap tbody tr").length + " rows"), machine: c => ok(JSON.stringify(I().doc("global", c.g)?.multiMap) !== c.m0, "global unchanged"), within: 15000 },
			{ say: "click × on the new range", act: (u, c) => u.click(`[data-mdel="${S().mmapSel}"]`), screen: c => ok($all("table.mmap tbody tr").length === c.n0, $all("table.mmap tbody tr").length + " rows"), machine: c => ok(JSON.stringify(I().doc("global", c.g)?.multiMap) === c.m0, "global differs"), within: 15000 }
		],
		async tidy(u) { if ($1('[data-pmode="normal"]')) u.click('[data-pmode="normal"]'); }
	};
	const kbPlay = {
		name: "mm-perform-keyboard",
		steps: [
			go("perform"),
			{ say: "click a key of the keyboard: the note goes to the machine (live notes have no read-back)", act: (u, c) => { sent.length = 0; c.k = $1('.kb .w[data-key="60"]') ? 60 : +$1(".kb .w[data-key]").dataset.key; u.click(`.kb [data-key="${c.k}"]`); },
				machine: () => ok(sent.includes("midi"), "sent " + sent.join(",")), screen: () => ok(/MIDI channel/.test($1("#kbinfo")?.textContent || ""), "info " + $1("#kbinfo")?.textContent) }
		]
	};

	/* ---------- Song ---------- */
	const songDoc = () => I().doc("song", S().songs?.slot ?? machine().song?.current ?? 0);
	const songRows = {
		name: "mm-song-rows",
		steps: [
			go("song"),
			{ say: "click a pattern pad: a row is added", act: async (u, c) => { u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); c.r0 = JSON.stringify(songDoc()?.rows); c.n0 = songDoc()?.rows.length; u.click('[data-addpat="0"]'); }, machine: c => ok(songDoc()?.rows.length === c.n0 + 1, songDoc()?.rows.length + " rows"), within: 15000 },
			{ say: "click Delete on the row", act: u => u.click('[data-rowact="del"]'), machine: c => ok(JSON.stringify(songDoc()?.rows) === c.r0, songDoc()?.rows.length + " rows"), within: 15000 }
		]
	};
	const songPicker = {
		name: "mm-song-picker-load",
		steps: [
			go("song"),
			{ say: "pick song 24 in the song picker", act: async (u, c) => { c.cur = machine().song?.current ?? 0; c.slot0 = S().songs?.slot; await choose(u, "songsel", 23); }, screen: () => ok(S().songs?.slot === 23, "editing " + S().songs?.slot), machine: () => ok(!!I().doc("song", 23), "song 24 not read"), within: 15000 },
			{ say: "click Load on the machine", act: u => u.click("#songload"), machine: () => ok(machine().song?.current === 23, "machine song " + machine().song?.current), within: 15000 },
			{ say: "pick the first song and load it back", act: async (u, c) => { await choose(u, "songsel", c.cur); await sleep(800); if (!$1("#songload")?.disabled) u.click("#songload"); }, machine: c => ok(machine().song?.current === c.cur, "machine song " + machine().song?.current), within: 15000 }
		]
	};
	const songChain = {
		name: "mm-song-chain",
		steps: [
			go("song"),
			{ say: "click Chain and pads A02, A03", act: async u => { u.click('[data-set="songpick"] button[data-v="chain"]'); await sleep(200); u.click('[data-bank="0"]'); await sleep(200); u.click('[data-chainpad="1"]'); await sleep(100); u.click('[data-chainpad="2"]'); },
				machine: () => ok(desk().chain?.active && same(desk().chain.patterns, [1, 2]), "chain " + JSON.stringify(desk().chain)), within: 8000 },
			{ say: "click Clear: the chain ends", act: u => u.click('[data-chain="clear"]'), machine: () => ok(!desk().chain?.active, "chain " + JSON.stringify(desk().chain)), within: 8000 }
		],
		async tidy(u) { if (S().ws === "song") u.click('[data-set="songpick"] button[data-v="arrange"]'); }
	};

	/* ---------- the library ---------- */
	const ks = k => `#libpop .ks[data-ks="${k}"]`;
	const kitName = k => MmConvert.kitName(I().doc("kit", k) || {});
	const kitLoad = {
		name: "mm-lib-kit-load",
		steps: [
			{ say: "click KIT on the LCD: the kit library", act: u => u.click("#kitf"), screen: () => ok(!$1("#libpop").hidden && $all("#libpop .ks").length >= 64, $all("#libpop .ks").length + " slots") },
			{ say: "click another kit: it loads", act: async (u, c) => { c.k0 = machine().kit.current; c.k = I().slots("kit").find(k => k !== c.k0 && I().doc("kit", k) && !MmConvert.kitEmpty(I().doc("kit", k))); u.click(ks(c.k)); if (await until(dlgShown, 1500)) { u.click(dlgButton("Load without saving") || $1("#dlg .danger")); c.note = "asked first"; } },
				machine: c => ok(machine().kit.current === c.k, "machine kit " + machine().kit.current), screen: c => ok($1(ks(c.k))?.classList.contains("cur"), "not current"), within: 10000 },
			{ say: "click the first kit again", act: async (u, c) => { u.click(ks(c.k0)); if (await until(dlgShown, 1500)) u.click(dlgButton("Load without saving") || $1("#dlg .danger")); }, machine: c => ok(machine().kit.current === c.k0, "machine kit " + machine().kit.current), within: 10000 },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};
	const kitCopy = {
		name: "mm-lib-kit-copy-paste-undo",
		steps: [
			{ say: "open the kit library, Alt-click a kit, click Copy", act: async (u, c) => { u.click("#kitf"); await sleep(300); const cur0 = machine().kit.current; c.src = I().slots("kit").find(k => k !== cur0 && kitName(k) && !MmConvert.kitEmpty(I().doc("kit", k))); u.click(ks(c.src), { alt: true }); await sleep(150); u.click('#libpop [data-la="copy"]'); }, screen: c => ok(true) },
			{ say: "Alt-click another kit and click Paste", act: async (u, c) => { const cur0 = machine().kit.current; c.dst = I().slots("kit").reverse().find(k => k !== cur0 && k !== c.src && kitName(k) !== kitName(c.src)); c.d0 = kitName(c.dst); u.click(ks(c.dst), { alt: true }); await sleep(150); u.click('#libpop [data-la="paste"]'); await sleep(300); if (dlgShown()) u.click($1("#dlg .danger") || $1('#dlg [data-dlg="0"]')); },
				machine: c => ok(kitName(c.dst) === kitName(c.src), "slot " + kitName(c.dst)), within: 15000 },
			{ ...undoKey, say: "press Cmd+Z (the library open): the slot is back", machine: c => ok(kitName(c.dst) === c.d0, "slot " + kitName(c.dst)), within: 15000 },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};
	const patGo = {
		name: "mm-lib-pattern-go",
		steps: [
			{ say: "click the pattern on the LCD and another pattern", act: async (u, c) => { c.p0 = cur(); u.click("#pat"); await sleep(300); u.click(`#libpop .ps[data-ps="${(c.p0 + 2) % 128}"]`); await confirmIfAsked(u); }, machine: c => ok(cur() === (c.p0 + 2) % 128, "machine pattern " + cur()), within: 10000 },
			{ say: "click the first one again", act: async (u, c) => { if ($1("#libpop").hidden) u.click("#pat"); await sleep(300); u.click(`#libpop .ps[data-ps="${c.p0}"]`); await confirmIfAsked(u); }, machine: c => ok(cur() === c.p0, "machine pattern " + cur()), within: 10000 },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};
	const dialogEsc = {
		name: "mm-dialog-esc-cancel",
		steps: [
			go("sound"), sel(0),
			{ say: "drag a value: the kit has unsaved edits", act: async u => { const el = $1('#main .pc[data-g="FLT"]'); await u.drag(el, [[6, 0], [12, 0], [18, 0]]); }, machine: () => ok(machine().kit?.working === "edited", "kit " + machine().kit?.working), within: 8000 },
			{ say: "open the kit library and click another kit: the machine asks", act: async (u, c) => { c.k0 = machine().kit.current; u.click("#kitf"); await sleep(300); c.k = I().slots("kit").find(k => k !== c.k0 && I().doc("kit", k) && !MmConvert.kitEmpty(I().doc("kit", k))); u.click(ks(c.k)); }, screen: () => ok(dlgShown() && document.activeElement?.textContent.trim() === "Cancel", `dialog ${dlgShown()}, focus ${document.activeElement?.textContent}`), within: 4000 },
			{ say: "press Escape: Cancel, nothing loads", act: u => u.key("Escape"), screen: () => ok(!dlgShown(), "still open"), machine: c => ok(machine().kit.current === c.k0, "machine kit " + machine().kit.current), within: 3000 }
		],
		async tidy(u) { if (!$1("#libpop").hidden) u.key("Escape"); await sleep(200); u.click("#undo"); await sleep(800); }
	};

	/* ---------- AUDIO / MIDI, the engine menu ---------- */
	const audioPanel = {
		name: "mm-audio-panel",
		steps: [
			{ say: "press , : AUDIO / MIDI opens", act: u => { blur(); u.key(","); }, screen: () => ok(!$1("#audiopop").hidden && $1("#audiopop").textContent.length > 40, "closed") },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#audiopop").hidden, "open") }
		]
	};
	const romCard = {
		name: "mm-engine-rom-card",
		steps: [
			{ say: "choose LOAD ROM: the card shows the firmware in use", act: u => u.pick("engsel", "rom"), screen: () => ok(!$1("#bootcard").hidden && /1\.32/.test($1("#bootcur")?.textContent || ""), "card " + $1("#bootcur")?.textContent), within: 5000 },
			{ say: "click Close", act: u => u.click('#bootcard [data-bootrom="close"]'), screen: () => ok($1("#bootcard").hidden, "still shown"), machine: () => ok(!!machine().input, "the machine stopped") }
		]
	};
	const hwNoMachine = {
		name: "mm-engine-hw-no-machine",
		steps: [
			{ say: "choose HW MIDI in the engine menu (no Monomachine attached)", act: u => u.pick("engsel", "hw"), screen: () => ok(!$1("#bootmidi").hidden, "no card"), machine: () => ok(machine().capabilities?.engine === "hw", "engine " + machine().capabilities?.engine), within: 10000 },
			{ say: "the card says no machine answers", screen: () => ok($1("#bootmidi").classList.contains("lost"), $1("#bmt")?.textContent), within: 20000 },
			{ say: "click Use the emulator", act: u => u.click('[data-bootmidi="emu"]'), screen: () => ok($1("#bootmidi").hidden, "card shown"), machine: () => ok(machine().capabilities?.engine === "emu" && machine().lifecycle === "ready", "engine " + machine().capabilities?.engine + " " + machine().lifecycle), within: 40000 }
		],
		async tidy() { await until(() => machine().loading && machine().loading.done >= machine().loading.total, 60000); }
	};
	const notePlay = {
		name: "mm-keys-play-notes",
		steps: [
			go("seq"), sel(0),
			{ say: "press A on the keyboard: track 1 plays", act: u => { results.length = 0; blur(); u.key("a"); }, machine: () => ok(results.some(r => /note/i.test(r.op) && r.ok), "results " + results.map(r => r.op + ":" + r.ok).join(",")) }
		]
	};

	/* ---------- more: the keys, the Sequence comforts, GEN and MUTATE, Sound, Mix, Perform, Song, the library ---------- */
	const undoUntil = async (u, done, n = 6) => { for (let i = 0; i < n && !done(); i++) { blur(); u.key("z", { cmd: true }); await until(done, 4000); } };
	const pat0 = () => JSON.stringify(patDoc()?.tracks);
	const roll = (t, s, m = {}) => async u => { const p = rollCell(t, s); u.click(p.c, m, p.fx, p.fy); };
	/* B taps the tempo on every workspace but Sequence, where it is the piano roll's Draw (Radek 2026-10-10;
	   mm-keys-tap-tempo-shift: Shift+B there): tapped on Mix, Draw left as it was */
	const tapTempo = {
		name: "mm-keys-tap-tempo",
		steps: [
			go("mix"),
			{ say: "tap B five times, about 0.5 s apart (T is a black key on the Monomachine Editor): the tempo follows, Draw stays as it was", act: async (u, c) => { c.b0 = machine().tempo; c.d0 = S().rollDraw; blur(); const at = []; for (let i = 0; i < 5; i++) { at.push(performance.now()); u.key("b"); await sleep(500); } c.want = Math.round(60000 / ((at[4] - at[0]) / 4) * 10) / 10; c.note = c.want + " BPM"; },
				screen: c => ok(Math.abs(S().bpm - c.want) <= 1.5 && S().rollDraw === c.d0, "BPM " + S().bpm + ", want " + c.want + ", Draw " + S().rollDraw), machine: c => ok(Math.abs(machine().tempo - c.want) <= 1.5, "tempo " + machine().tempo + ", want " + c.want), within: 8000 }
		],
		async tidy(u, c) { if (c.b0 != null) { const d = c.b0 > machine().tempo ? -1 : 1; await u.drag("#bpm", [[0, d * 2 * (c.b0 - machine().tempo)]]); } if (c.d0 != null && S().rollDraw !== c.d0) setRollDraw(c.d0); }
	};
	const queue = {
		name: "mm-seq-queue-while-playing",
		steps: [
			{ say: "press PLAY", act: (u, c) => { c.p0 = cur(); u.click("#play"); }, machine: () => ok(tele.last?.playing, "not playing"), within: 6000 },
			{ say: "click › on the LCD: the next pattern is queued", act: async u => { u.click("#patNext"); await confirmIfAsked(u); }, machine: c => ok(machine().pattern?.queued === c.p0 + 1 || cur() === c.p0 + 1, "queued " + machine().pattern?.queued), within: 4000 },
			{ say: "it starts at the pattern end", machine: c => ok(cur() === c.p0 + 1, "machine pattern " + cur()), screen: c => ok(S().pat === c.p0 + 1, "page pattern " + S().pat), within: 25000 },
			{ say: "press STOP and click ‹", act: async u => { u.click("#play"); await until(() => !S().playing, 3000); u.click("#patPrev"); await confirmIfAsked(u); }, machine: c => ok(cur() === c.p0, "machine pattern " + cur()), within: 8000 }
		],
		async tidy(u) { if (S().playing) u.click("#play"); }
	};
	const ks2 = k => `#libpop .ks[data-ks="${k}"]`;
	const anotherKit = () => I().slots("kit").find(k => k !== machine().kit.current && I().doc("kit", k) && !MmConvert.kitEmpty(I().doc("kit", k)));
	const dialogKeys = {
		name: "mm-dialog-keys-behind",
		steps: [
			go("sound"), sel(0),
			{ say: "drag a value: the kit has unsaved edits", act: async u => { await u.drag('#main .pc[data-g="FLT"]', [[6, 0], [12, 0], [18, 0]]); }, machine: () => ok(machine().kit?.working === "edited", "kit " + machine().kit?.working), within: 8000 },
			{ say: "open the kit library and click another kit: the machine asks", act: async (u, c) => { c.k0 = machine().kit.current; u.click("#kitf"); await sleep(300); c.k = anotherKit(); u.click(ks2(c.k)); }, screen: () => ok(dlgShown(), "no question"), within: 4000 },
			{ say: "press Delete, Backspace, 3 and 1: nothing behind the dialog acts", act: async (u, c) => { c.ws = S().ws; c.u0 = machine().history?.undoCount; for (const k of ["Delete", "Backspace", "3", "1"]) { u.key(k); await sleep(150); } },
				screen: c => ok(dlgShown() && S().ws === c.ws && !$1("#libpop").hidden, `dialog ${dlgShown()}, workspace ${S().ws}`), machine: c => ok(machine().kit.current === c.k0 && machine().history?.undoCount === c.u0, "the page behind acted"), within: 1500 },
			{ say: "press Space: the focused Cancel, nothing loads", act: u => u.key(" "), screen: () => ok(!dlgShown(), "still open"), machine: c => ok(machine().kit.current === c.k0, "machine kit " + machine().kit.current), within: 3000 }
		],
		async tidy(u) { if (dlgShown()) u.key("Escape"); if (!$1("#libpop").hidden) u.key("Escape"); await sleep(200); u.click("#undo"); await sleep(1500); }
	};
	const trackKeys = {
		name: "mm-keys-track-select",
		steps: [
			go("seq"), sel(0),
			{ say: "press ↓", act: u => { blur(); u.key("ArrowDown"); }, screen: () => ok(S().sel === 1 && $1("#rail .th.sel")?.dataset.sel === "1", "selected " + S().sel) },
			{ say: "press ↑", act: u => u.key("ArrowUp"), screen: () => ok(S().sel === 0, "selected " + S().sel) }
		]
	};
	const muteKeys = {
		name: "mm-keys-mute",
		steps: [
			go("seq"), sel(0),
			{ say: "press M: track 1 is muted", act: (u, c) => { c.m0 = synthMutes(); blur(); u.key("m"); }, machine: () => ok(synthMuted(0), "machine mutes " + synthMutes()), screen: () => ok(pressed('#rail .th[data-sel="0"] .ms.m'), "M not lit") },
			{ say: "press M again", act: u => u.key("m"), machine: c => ok(same(synthMutes(), c.m0), "machine mutes " + synthMutes()) },
			{ say: "press Alt+M: every synth track muted", act: u => u.key("m", { alt: true }), machine: () => ok(synthMutes().length === 6, "machine mutes " + synthMutes()) },
			{ say: "press 0: every track unmuted", act: u => u.key("0"), machine: () => ok(!synthMutes().length && !(machine().mutes?.midi), `machine mutes ${synthMutes()}, MIDI ${machine().mutes?.midi}`) }
		]
	};
	const lockRamp = {
		name: "mm-seq-lock-ramp-erase-wheel",
		steps: [
			go("seq"),
			{ say: "pick a track with three notes, the Locks tab", act: async (u, c) => { c.t = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).filter(s => s < S().len).length >= 3) ?? 0; u.click(rail(c.t)); await sleep(200); if ($1('[data-dock="locks"]')) u.click('[data-dock="locks"]'); await sleep(200); c.lane = S().lane; [c.pg, c.pi] = [PAGES.indexOf(c.lane.split(".")[0]), +c.lane.split(".")[1]]; c.on = trigsOf(c.t).filter(s => s < S().len).slice(0, 3); c.l0 = JSON.stringify(patDoc().locks); },
				machine: c => ok(c.on.length === 3, "notes " + trigsOf(c.t).join(",")), screen: () => ok(!!$1("#lane"), "no lane") },
			{ say: "Shift-drag across the lane: a ramp over the notes", act: async (u, c) => { await u.drag(`#lane .lb[data-s="${c.on[0]}"]`, c.on.slice(1).map(s => `#lane .lb[data-s="${s}"]`), { shift: true }, { fy: 0.8 }); },
				machine: c => { const l = (patDoc().locks || []).find(x => x.track === c.t && x.page === c.pg && x.param === c.pi), v = s => l?.steps.find(([x]) => x === s)?.[1]; return ok(l && c.on.every(s => v(s) != null) && v(c.on[0]) !== v(c.on[2]), "locks " + JSON.stringify(l)); }, within: 15000 },
			{ say: "Alt-drag across them: erased", act: async (u, c) => { await u.drag(`#lane .lb[data-s="${c.on[0]}"]`, c.on.slice(1).map(s => `#lane .lb[data-s="${s}"]`), { alt: true }); },
				machine: c => { const l = (patDoc().locks || []).find(x => x.track === c.t && x.page === c.pg && x.param === c.pi); return ok(!l || c.on.every(s => !l.steps.some(([x]) => x === s)), "locks " + JSON.stringify(l)); }, within: 15000 },
			{ say: "scroll the wheel over a lane step with a note: a lock", act: (u, c) => u.wheel(`#lane .lb[data-s="${c.on[0]}"]`, 2),
				machine: c => { const l = (patDoc().locks || []).find(x => x.track === c.t && x.page === c.pg && x.param === c.pi); return ok(l && l.steps.some(([x]) => x === c.on[0]), "locks " + JSON.stringify(l)); }, within: 15000 }
		],
		async tidy(u, c) { if (c.l0) await undoUntil(u, () => JSON.stringify(patDoc().locks) === c.l0, 5); }
	};
	const pages = {
		name: "mm-seq-pages",
		steps: [
			go("seq"),
			{ say: "click ALL off: one page of 16 steps", act: u => { if (S().viewAll) u.click("#pgall"); }, screen: () => ok(!S().viewAll && vis()[1] - vis()[0] === 16, "view " + vis().join("-")) },
			{ say: "click PAGE: the next page (when the pattern is longer than 16)", act: (u, c) => { c.p0 = S().page; if (S().len > 16) u.click("#pgkey"); }, screen: c => ok(S().len <= 16 || S().page === (c.p0 + 1) % (S().len / 16), "page " + S().page) },
			{ say: "press [: back", act: u => { blur(); if (S().len > 16) u.key("["); }, screen: c => ok(S().page === c.p0, "page " + S().page) },
			{ say: "click ALL: every step", act: u => u.click("#pgall"), screen: () => ok(S().viewAll && vis()[1] === S().len, "view " + vis().join("-")) }
		],
		async tidy(u) { if (!S().viewAll) u.click("#pgall"); }
	};
	const copyPaste = {
		name: "mm-seq-copy-paste-clear",
		steps: [
			go("seq"),
			{ say: "pick a track with notes and press Cmd+C", act: async (u, c) => { c.a = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).length) ?? 0; c.b = [0, 1, 2, 3, 4, 5].find(t => t !== c.a && !same(trigsOf(t), trigsOf(c.a))); c.ta = trigsOf(c.a); c.tb = trigsOf(c.b); c.p0 = pat0(); u.click(rail(c.a)); await sleep(200); blur(); u.key("c", { cmd: true }); }, screen: () => ok(/COPY PAGE/.test($1("#toast")?.textContent || ""), "toast " + $1("#toast")?.textContent) },
			{ say: "pick another track and press Cmd+V", act: async (u, c) => { u.click(rail(c.b)); await sleep(200); blur(); u.key("v", { cmd: true }); }, machine: c => ok(same(trigsOf(c.b), c.ta), "track " + (c.b + 1) + " " + trigsOf(c.b).join(",")), within: 15000 },
			/* D3 (DESIGN-keymap.md, K7): Delete takes only selected steps; the LCD's Clr clears the page shown */
			{ say: "press Delete with no step selected: nothing is cleared", act: (u, c) => { if (S().stepSel) clearSel(); blur(); u.key("Delete"); }, machine: c => ok(same(trigsOf(c.b), c.ta), "track " + trigsOf(c.b).join(",")), within: 3000 },
			{ say: "click Clr on the LCD: the page is cleared", act: u => u.click('[data-sec="clear"]'), machine: c => ok(!trigsOf(c.b).length, "track " + trigsOf(c.b).join(",")), within: 15000 },
			{ say: "Cmd+Z twice: as before", act: async (u, c) => { await undoUntil(u, () => same(trigsOf(c.b), c.tb), 3); }, machine: c => ok(same(trigsOf(c.b), c.tb), "track " + trigsOf(c.b).join(",")), within: 15000 }
		]
	};
	const clearAll = {
		name: "mm-seq-clear-pattern-undo",
		steps: [
			go("seq"),
			{ say: "press Alt+Delete: the whole pattern is cleared", act: (u, c) => { c.all = [0, 1, 2, 3, 4, 5].map(trigsOf); blur(); u.key("Delete", { alt: true }); }, machine: () => ok([0, 1, 2, 3, 4, 5].every(t => !trigsOf(t).length), "notes left"), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(same([0, 1, 2, 3, 4, 5].map(trigsOf), c.all), "pattern differs"), within: 15000 }
		]
	};
	const fill = {
		name: "mm-seq-fill-every",
		steps: [
			go("seq"), sel(0),
			{ say: "right-click an empty roll cell near the end, choose Fill every 2nd from here: every 2nd step gets a note", act: async (u, c) => { c.t0 = trigsOf(0); c.s = [...Array(S().len).keys()].find(s => s >= S().len - 8 && !c.t0.includes(s) && !S().tracks[0].steps[s]);
				if (S().stepSel) clearSel(); const p = rollCell(0, c.s); u.rightClick(p.c, {}, p.fx, p.fy); await sleep(150); u.click('#deskmenu [data-mid="step-fill-2"]'); },
				machine: c => { const want = []; for (let s = c.s; s < S().len; s += 2) if (!S().tracks[0].steps[s]?.off) want.push(s); return ok(want.every(s => trigsOf(0).includes(s)), "notes " + trigsOf(0).join(",")); }, within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(same(trigsOf(0), c.t0), "notes " + trigsOf(0).join(",")), within: 15000 }
		],
		/* the right-click selected the step (the Machinedrum's md-seq-fill-every clears it too) */
		async tidy() { if (S().stepSel) clearSel(); }
	};
	const rotate = {
		name: "mm-seq-rotate",
		steps: [
			go("seq"),
			{ say: "pick a track with notes", act: (u, c) => { c.t = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).length) ?? 0; c.t0 = trigsOf(c.t); u.click(rail(c.t)); }, screen: c => ok(S().sel === c.t, "selected " + S().sel) },
			{ say: "press Alt+→: one step later", act: u => { blur(); u.key("ArrowRight", { alt: true }); }, machine: c => { const L = S().len; return ok(same(trigsOf(c.t), c.t0.map(s => s >= L ? s : (s + 1) % L).sort((a, b) => a - b)), "notes " + trigsOf(c.t).join(",")); }, within: 15000 },
			{ say: "press Alt+←: back", act: u => u.key("ArrowLeft", { alt: true }), machine: c => ok(same(trigsOf(c.t), c.t0), "notes " + trigsOf(c.t).join(",")), within: 15000 }
		]
	};
	const pasteMany = {
		name: "mm-seq-paste-many",
		steps: [
			go("seq"),
			/* no step selected: Cmd+C copies the page (with a selection it copies the selected steps) */
			{ say: "pick a track with notes and press Cmd+C (no step selected: the page)", act: async (u, c) => { if (S().stepSel) clearSel(); c.a = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).length) ?? 0; c.bs = [0, 1, 2, 3, 4, 5].filter(t => t !== c.a && !same(trigsOf(t), trigsOf(c.a))).slice(0, 2); c.b0 = c.bs.map(trigsOf); c.ta = trigsOf(c.a); u.click(rail(c.a)); await sleep(200); blur(); u.key("c", { cmd: true }); }, screen: () => ok(/COPY PAGE/.test($1("#toast")?.textContent || ""), "toast " + $1("#toast")?.textContent) },
			{ say: "Shift-click two other headers: marked", act: async (u, c) => { for (const t of c.bs) { u.click(rail(t), { shift: true }); await sleep(150); } }, screen: c => ok(c.bs.every(t => $1(`#rail .th[data-sel="${t}"]`)?.classList.contains("marked")), "not marked") },
			{ say: "press Cmd+V: both get the notes", act: u => { blur(); u.key("v", { cmd: true }); }, machine: c => ok(c.bs.every(t => same(trigsOf(t), c.ta)), c.bs.map(t => `T${t + 1} ${trigsOf(t).join(",")}`).join("; ")), within: 20000 },
			{ ...undoKey, say: "press Cmd+Z once: both are back", act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(c.bs.every((t, k) => same(trigsOf(t), c.b0[k])), c.bs.map(t => `T${t + 1} ${trigsOf(t).join(",")}`).join("; ")), within: 20000 }
		],
		async tidy(u, c) { if (S().marks?.size) u.key("Escape"); if (c.bs) await undoUntil(u, () => c.bs.every((t, k) => same(trigsOf(t), c.b0[k])), 3); }
	};
	const liveRec = {
		name: "mm-seq-live-record",
		steps: [
			go("seq"), sel(0),
			{ say: "press Alt+Space: live recording, playing (pressed again when the plug-in says the panel is busy: an earlier journey's dumps on their way, as mm-seq-grid-record)", act: async (u, c) => { c.n0 = trigsOf(0).length; c.p0 = pat0(); blur(); results.length = 0; u.key(" ", { alt: true }, "Space");
				for (let i = 0; i < 5 && await until(() => results.some(r => r.op === "record" && r.ok === false), 1500); i++) { c.note = "the plug-in said the panel was busy (SYSEX RECV); pressed again"; results.length = 0; await sleep(1500); blur(); u.key(" ", { alt: true }, "Space"); }
				await sleep(400); if (!S().playing) u.click("#play"); },
				machine: () => ok(tele.last?.record === "live" && tele.last?.playing, `record ${tele.last?.record}, playing ${tele.last?.playing}`), within: 8000 },
			{ say: "play A, S, D on the keyboard while it records: the machine records notes", act: async u => { for (const k of ["a", "s", "d"]) { u.key(k); await sleep(450); } },
				machine: c => ok(pat0() !== c.p0, "pattern unchanged (" + c.n0 + " notes)"), within: 15000 },
			{ say: "click RECORD off, then STOP", act: async u => { u.click("#rec"); await sleep(600); if (S().playing) u.click("#play"); }, machine: () => ok(tele.last?.record === "off" && !tele.last?.playing, `record ${tele.last?.record}`), within: 8000 }
		],
		async tidy(u, c) { if (tele.last?.record && tele.last.record !== "off") u.click("#rec"); if (S().playing) u.click("#play"); await sleep(800); if (c.p0) await undoUntil(u, () => pat0() === c.p0, 3); }
	};
	const genKeys = {
		name: "mm-gen-keys-r",
		steps: [
			go("seq"), sel(0),
			{ say: "press R until the notes change", act: async (u, c) => { c.p0 = pat0(); blur(); for (let i = 0; i < 5; i++) { u.key("r"); if (await until(() => pat0() !== c.p0, 4000)) break; } }, machine: c => ok(pat0() !== c.p0, "unchanged"), within: 15000 },
			{ say: "click GEN's Hits value (a Euclid spec) or Density", act: (u, c) => { c.p1 = pat0(); u.click($1('#genband .gv[data-gv="k"]') ? '#genband .gv[data-gv="k"]' : '#genband .gv[data-gv="dens"]'); }, machine: c => ok(pat0() !== c.p1, "unchanged"), within: 15000 },
			{ ...undoKey, say: "press Cmd+Z: the whole run in one step", act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(pat0() === c.p0, "pattern not back"), within: 15000 }
		],
		async tidy(u, c) { if (c.p0) await undoUntil(u, () => pat0() === c.p0, 3); }
	};
	const mutScope = {
		name: "mm-gen-mutate-scope",
		steps: [
			go("sound"), sel(0),
			{ say: "click MUTATE's Flt chip on and Syn off", act: async (u, c) => { c.scope0 = [...S().mut.scope]; c.amt0 = S().mut.amount; if (!S().mut.scope.has("FLT")) u.click('#mutband [data-mutg="FLT"]'); await sleep(100); if (S().mut.scope.has("SYN")) u.click('#mutband [data-mutg="SYN"]'); await sleep(100); },
				screen: () => ok(pressed('#mutband [data-mutg="FLT"]') && !pressed('#mutband [data-mutg="SYN"]'), "chips " + [...S().mut.scope].join(",")) },
			{ say: "click the Amount value: one more", act: u => u.click('#mutband .gv[data-gv="amt"]'), screen: c => ok($1('#mutband .gv[data-gv="amt"] b')?.textContent === (c.amt0 + 1) + "%", "shows " + $1('#mutband .gv[data-gv="amt"] b')?.textContent) },
			{ say: "click MUTATE's Random key: only the filter page moves", act: (u, c) => { c.k0 = JSON.stringify(wk().tracks[0].pages); u.click("#mutband [data-rand]"); },
				machine: c => { const a = JSON.parse(c.k0), b = wk().tracks[0].pages, moved = b.map((pg, i) => JSON.stringify(pg) !== JSON.stringify(a[i]) ? PAGES[i] || "MID" : null).filter(Boolean); return ok(same(moved, ["FLT"]), "pages moved: " + (moved.join(",") || "none")); }, within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(JSON.stringify(wk().tracks[0].pages) === c.k0, "kit not back"), within: 15000 }
		],
		async tidy(u, c) { if (!c.scope0) return; for (const g of ["SYN", "AMP", "FLT", "EFX", "LFO"]) if (S().mut.scope.has(g) !== c.scope0.includes(g)) { u.click(`#mutband [data-mutg="${g}"]`); await sleep(80); } if (S().mut.amount !== c.amt0) u.click('#mutband .gv[data-gv="amt"]', { shift: true }); }
	};
	const valueKeys = {
		name: "mm-sound-value-keys",
		steps: [
			go("sound"), sel(0),
			{ say: "focus a filter value and press ↑ three times", act: async (u, c) => { const el = $1('#main .pc[data-g="FLT"]'); c.i = +el.dataset.n; c.v0 = wk().tracks[0].pages[2][c.i]; el.focus(); const d = c.v0 > 120 ? "ArrowDown" : "ArrowUp"; c.want = c.v0 + (d === "ArrowUp" ? 3 : -3); for (let k = 0; k < 3; k++) { u.key(d); await sleep(150); } },
				machine: c => ok(wk().tracks[0].pages[2][c.i] === c.want, `kit ${wk().tracks[0].pages[2][c.i]}, want ${c.want} (from ${c.v0}); selected ${S().sel + 1}; focus ${document.activeElement?.className || document.activeElement?.tagName}`), within: 10000 },
			{ say: "click Undo until it is back", act: async (u, c) => { for (let k = 0; k < 3 && wk().tracks[0].pages[2][c.i] !== c.v0; k++) { u.click("#undo"); await sleep(1500); } }, machine: c => ok(wk().tracks[0].pages[2][c.i] === c.v0, "kit " + wk().tracks[0].pages[2][c.i]), within: 10000 }
		]
	};
	const soundCopy = {
		name: "mm-sound-copy-paste",
		steps: [
			go("sound"),
			{ say: "pick track 1 and press Cmd+C", act: async (u, c) => { c.b = [1, 2, 3, 4, 5].find(t => wk().tracks[t].machine !== wk().tracks[0].machine) ?? 1; c.kb = kitT(c.b); u.click(rail(0)); await sleep(200); blur(); u.key("c", { cmd: true }); }, screen: () => ok(/COPY MACHINE/.test($1("#toast")?.textContent || ""), "toast " + $1("#toast")?.textContent) },
			{ say: "pick another track and press Cmd+V", act: async (u, c) => { u.click(rail(c.b)); await sleep(200); blur(); u.key("v", { cmd: true }); }, machine: c => ok(wk().tracks[c.b].machine === wk().tracks[0].machine && same(wk().tracks[c.b].pages.slice(0, 7), wk().tracks[0].pages.slice(0, 7)), `track ${c.b + 1} machine ${wk().tracks[c.b].machine}`), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(kitT(c.b) === c.kb, "track not back"), within: 15000 }
		]
	};
	const screenDrag = {
		name: "mm-sound-screen-drag",
		steps: [
			go("sound"), sel(0),
			{ say: "drag a handle of a Sound screen", act: async (u, c) => { const cv = $all("#main canvas.ed").find(x => ED[x.dataset.ed]?.handles); if (!cv) throw new Error("no screen"); c.k0 = kitT(0); const r = cv.getBoundingClientRect(), h = ED[cv.dataset.ed].handles(r.width, r.height, cv)[0]; c.note = cv.dataset.ed + " " + h.k; await u.drag(cv, [[10, 0], [20, -6], [30, -10]], {}, { fx: h.x / r.width, fy: Math.min(Math.max(h.y, 6), r.height - 6) / r.height }); },
				machine: c => ok(kitT(0) !== c.k0, "kit unchanged"), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(kitT(0) === c.k0, "kit not back"), within: 15000 }
		]
	};
	const dragM = {
		name: "mm-mix-drag-m-keys",
		steps: [
			go("mix"),
			{ say: "drag across the M keys of tracks 1 to 3", act: async (u, c) => { c.m0 = synthMutes(); if ([0, 1, 2].some(synthMuted)) throw new Error("tracks 1-3 not all audible"); await u.drag(strip(0, ".ms.m"), [strip(1, ".ms.m"), strip(2, ".ms.m")], {}, { captured: false }); },
				machine: () => ok([0, 1, 2].every(synthMuted), "machine mutes " + synthMutes()), screen: () => ok([0, 1, 2].every(i => pressed(strip(i, ".ms.m"))), "M keys not lit") },
			{ say: "click M/S off on the Sequence rail", act: async u => { u.click(tab("seq")); await sleep(300); u.click("#allon"); }, machine: c => ok(same(synthMutes(), c.m0), "machine mutes " + synthMutes()) }
		]
	};
	const midiMutes = {
		name: "mm-perform-midi-mutes",
		steps: [
			go("perform"),
			{ say: "click M1 in the Mutes card: MIDI track 1 is muted (the MUTE window)", act: (u, c) => { c.m0 = machine().mutes?.midi ?? 0; u.click('[data-gmute="6"]'); }, machine: c => ok(((machine().mutes?.midi ?? 0) & 1) !== (c.m0 & 1), "MIDI mutes " + machine().mutes?.midi), screen: c => ok(pressed('[data-gmute="6"]') === !!(c.m0 & 1), "key " + $1('[data-gmute="6"]')?.getAttribute("aria-pressed")), within: 10000 },
			{ say: "click it again", act: u => u.click('[data-gmute="6"]'), machine: c => ok(((machine().mutes?.midi ?? 0) & 1) === (c.m0 & 1), "MIDI mutes " + machine().mutes?.midi), within: 10000 }
		]
	};
	const joyAssign = {
		name: "mm-perform-joystick-assign",
		steps: [
			go("perform"),
			{ say: "drag the joystick and let go: the moves go to the machine, the stick springs back", act: async (u, c) => { sent.length = 0; if (!$1("#joy")) { const b = $all('[data-set="astab"] button').find(x => x.dataset.v.startsWith("JOY")); if (b) u.click(b); await sleep(200); } await u.drag("#joy", [[10, -10], [25, -20], [40, -30]]); },
				machine: () => ok(sent.filter(x => x === "midi").length >= 2, "sent " + sent.join(",")), screen: () => ok($1("#knobj")?.style.left === "50%", "stick at " + $1("#knobj")?.style.left) },
			{ say: "drag the first assign row's ADD", act: async (u, c) => { c.a0 = JSON.stringify(wk().tracks.map(t => t.assign)); const el = $1('#main .pc[data-g="asg"]'); c.txt0 = el.querySelector("b")?.textContent; c.tab = S().asTab; sent.length = 0; results.length = 0; const d = parseInt(c.txt0) > 0 ? -1 : 1; await u.drag(el, [[d * 6, 0], [d * 12, 0], [d * 18, 0], [d * 24, 0]]); c.txt1 = $1('#main .pc[data-g="asg"] b')?.textContent; },
				screen: c => ok(c.txt1 !== c.txt0, `ADD shows ${c.txt0} -> ${c.txt1}`),
				machine: c => ok(JSON.stringify(wk().tracks.map(t => t.assign)) !== c.a0, `assign unchanged (tab ${c.tab}, track ${S().sel + 1}); sent ${sent.join(",")}; results ${results.map(r => r.op + ":" + r.ok + (r.errors ? " " + r.errors.join(";") : "")).join(", ")}; track 1 assign ${JSON.stringify(wk().tracks[0].assign)}`), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(JSON.stringify(wk().tracks.map(t => t.assign)) === c.a0, "assign not back"), within: 15000 }
		]
	};
	const menvPort = {
		name: "mm-perform-menv-portamento",
		steps: [
			go("perform"),
			{ say: "drag the multi envelope's ATK", act: async (u, c) => { c.e0 = JSON.stringify(wk().tracks.map(t => t.multiEnv)); const q = '#main .pc[data-g="menv"][data-n="ATK"]', d = parseInt($1(q + " b")?.textContent) > 64 ? -1 : 1; await u.drag(q, [[d * 6, 0], [d * 12, 0], [d * 18, 0], [d * 24, 0]]); }, machine: c => ok(JSON.stringify(wk().tracks.map(t => t.multiEnv)) !== c.e0, "multi env unchanged"), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(JSON.stringify(wk().tracks.map(t => t.multiEnv)) === c.e0, "multi env not back"), within: 15000 },
			{ say: "on Sequence's Trig setup, click the other PORT mode", act: async (u, c) => { u.click(tab("seq")); await sleep(200); u.click(rail(0)); await sleep(200); u.click('[data-dock="trig"]'); await sleep(300); c.p0 = wk().trackMasks?.portamento ?? 0; const b = $all('[data-set="port"] button').find(x => x.getAttribute("aria-pressed") !== "true"); u.click(b); },
				machine: c => ok(((wk().trackMasks?.portamento ?? 0) & 1) !== (c.p0 & 1), "portamento mask " + wk().trackMasks?.portamento), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(((wk().trackMasks?.portamento ?? 0) & 1) === (c.p0 & 1), "portamento mask " + wk().trackMasks?.portamento), within: 15000 }
		],
		async tidy(u) { if ($1('[data-dock="locks"]')) u.click('[data-dock="locks"]'); }
	};
	/* B-017: dragging the multi envelope (its DEC value, or the DEC dot on its screen) never changes the page's layout:
	   the screen's canvas took its drawn size (its pixels, the display's scale times its box) as its own height, so each
	   redraw while dragging made the card, and the page, taller, until the render on release put it back. */
	const menvLayout = {
		name: "mm-perform-menv-layout",
		steps: [
			go("perform"),
			{ say: "drag the multi envelope's DEC, then the DEC dot on its screen: the card and the page keep their height",
				act: async (u, c) => {
					const card = () => $1("#main .menvcard"), cv = () => $1('#main canvas.ed[data-ed="menv"]');
					c.e0 = JSON.stringify(wk().tracks.map(t => t.multiEnv));
					const size = () => [Math.round(card().getBoundingClientRect().height), $1("#main").scrollHeight, Math.round(cv().getBoundingClientRect().height)];
					c.before = size(); c.max = c.before.slice();
					const sample = setInterval(() => { if (card() && cv()) size().forEach((v, i) => { c.max[i] = Math.max(c.max[i], v); }); }, 10);
					try {
						const q = '#main .pc[data-g="menv"][data-n="DEC"]', d = parseInt($1(q + " b")?.textContent) > 64 ? -1 : 1;
						await u.drag(q, Array.from({ length: 10 }, (_, k) => [d * 4 * (k + 1), 0]), {}, { stepMs: 40 });
						const r = cv().getBoundingClientRect(), h = ED.menv.handles(r.width, r.height, cv()).find(x => x.k.startsWith("DEC"));
						const fx = h.x / r.width, fy = Math.min(Math.max(h.y, 6), r.height - 6) / r.height, dx = h.x > r.width / 2 ? -1 : 1;
						await u.drag(cv(), Array.from({ length: 10 }, (_, k) => [dx * 3 * (k + 1), -2 * (k + 1)]), {}, { fx, fy, stepMs: 40 });
					} finally { clearInterval(sample); }
					c.after = size();
				},
				screen: c => ok(c.max.every((v, i) => v <= c.before[i] + 1) && c.after.every((v, i) => Math.abs(v - c.before[i]) <= 1),
					`card, page, screen heights ${c.before.join("/")} -> at most ${c.max.join("/")} while dragging, ${c.after.join("/")} after`),
				machine: c => ok(JSON.stringify(wk().tracks.map(t => t.multiEnv)) !== c.e0, "multi env unchanged"), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); } },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(JSON.stringify(wk().tracks.map(t => t.multiEnv)) === c.e0, "multi env not back"), within: 15000 }
		]
	};
	/* B-018: ? as the operating system delivers it, before any click in the window: the web view has the keyboard
	   (when the page is up, and whenever the window becomes the key window JUCE takes it for its own view and the page's
	   component hands it back), so the key reaches the page, opens the keyboard view and does not beep. "activate": what
	   JUCE does then; no "focus": nothing clicks first. */
	const osHelp = {
		name: "mm-keys-os-help", needs: Journey.osKeyPath,
		steps: [
			{ say: "the window becomes the key window (JUCE takes the keyboard), then press ? on the keyboard, no click in the page: the keyboard view", act: async u => { blur(); await u.osKey("activate"); await sleep(300); await u.osKey("?"); },
				screen: () => ok(!$1("#keyspop").hidden && !!$1("#keyspop .kv-cap"), "keys view " + ($1("#keyspop").hidden ? "hidden" : "without the drawn keyboard")) },
			{ say: "press Escape on the keyboard: it closes", act: u => u.osKey("escape"), screen: () => ok($1("#keyspop").hidden, "still open") }
		]
	};
	const songInspector = {
		name: "mm-song-row-inspector",
		steps: [
			go("song"),
			{ say: "add a row (Arrange, pad A01)", act: async (u, c) => { u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); c.r0 = JSON.stringify(songDoc()?.rows); c.n0 = songDoc()?.rows.length; u.click('[data-addpat="0"]'); await until(() => songDoc()?.rows.length === c.n0 + 1, 15000); c.i = S().songSel; }, machine: c => ok(songDoc()?.rows.length === c.n0 + 1, "rows " + songDoc()?.rows.length), within: 15000 },
			{ say: "click Repeat +", act: (u, c) => { c.rep = songDoc().rows[c.i].repeats; u.click('[data-step="rep"][data-d="1"]'); }, machine: c => ok(songDoc().rows[c.i].repeats === c.rep + 1, "repeats " + songDoc().rows[c.i].repeats), within: 15000 },
			{ say: "click More and mute track 1 for the row", act: async u => { if (!$1('[data-rowmute="0"]')) u.click("[data-rowmore]"); await sleep(250); u.click('[data-rowmute="0"]'); }, machine: c => ok((songDoc().rows[c.i].mutes & 1) === 1, "mutes " + songDoc().rows[c.i].mutes), within: 15000 },
			{ say: "click Add loop: a loop row after it", act: u => u.click('[data-rowact="loop"]'), machine: c => ok(songDoc().rows[c.i + 1]?.kind === "loop", "row " + JSON.stringify(songDoc().rows[c.i + 1]?.kind)), within: 15000 }
		],
		async tidy(u, c) { if (S().songMore) u.click("[data-rowmore]"); if (c.r0) await undoUntil(u, () => JSON.stringify(songDoc()?.rows) === c.r0, 8); }
	};
	const songDrag = {
		name: "mm-song-drag-drop",
		steps: [
			go("song"),
			{ say: "drag pad A02 onto the empty end of the grid: a row is appended", act: async (u, c) => { u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); c.r0 = JSON.stringify(songDoc()?.rows); c.n0 = S().song.length; await u.dragDrop('[data-addpat="1"]', `#tl .scell[data-i="${c.n0 + 1}"]`); },
				machine: c => ok(songDoc()?.rows.length === JSON.parse(c.r0).length + 1 && songDoc().rows.some(r => r.kind === "pattern" && r.pattern === 1), "rows " + songDoc()?.rows.map(r => r.kind === "pattern" ? r.pattern : r.kind).join(",")), within: 15000 },
			{ say: "drag that row onto the first cell", act: async (u, c) => { const i = S().song.findIndex((r, k) => !r.type && r.pat === 1 && k > 0); await u.dragDrop(`#tl .scell[data-i="${i}"]`, '#tl .scell[data-i="0"]'); },
				machine: () => ok(songDoc()?.rows[0]?.pattern === 1, "rows " + songDoc()?.rows.map(r => r.kind === "pattern" ? r.pattern : r.kind).join(",")), within: 15000 }
		],
		async tidy(u, c) { if (c.r0) await undoUntil(u, () => JSON.stringify(songDoc()?.rows) === c.r0, 6); }
	};
	const openKits2 = { say: "click KIT on the LCD", act: u => u.click("#kitf"), screen: () => ok(!$1("#libpop").hidden, "closed") };
	const altPick = f => ({ say: "Alt-click a slot: selected", act: (u, c) => { c.sel = f(c); if (c.sel == null) throw new Error("no slot"); u.click(ks2(c.sel), { alt: true }); }, screen: c => ok($1(ks2(c.sel))?.getAttribute("aria-selected") === "true", "not selected") });
	const answer = async (u, c) => { if (await until(dlgShown, 3000)) { c.note = "asked: " + ($1("#dlg p")?.textContent || "").slice(0, 80); u.click($1("#dlg .danger") || $1('#dlg [data-dlg="0"]')); } };
	const otherSlot = () => I().slots("kit").reverse().find(k => k !== machine().kit.current && I().doc("kit", k) && !MmConvert.kitEmpty(I().doc("kit", k)));
	const kitSaveAs = {
		name: "mm-lib-kit-saveas",
		steps: [
			openKits2, altPick(c => { c.k0 = machine().kit.current; return otherSlot(); }),
			{ say: "click Save as and answer the question", act: async (u, c) => { c.d0 = JSON.stringify(I().doc("kit", c.sel)); u.click('#libpop [data-la="saveas"]'); await answer(u, c); },
				machine: c => ok(same(I().doc("kit", c.sel)?.tracks.map(t => t.machine), wk().tracks.map(t => t.machine)) && MmConvert.kitName(I().doc("kit", c.sel)) === MmConvert.kitName(wk()), `slot ${MmConvert.kitName(I().doc("kit", c.sel) || {})}, working ${MmConvert.kitName(wk() || {})}, current ${machine().kit.current}`), within: 25000 },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};
	const kitRename = {
		name: "mm-lib-kit-rename-undo",
		steps: [
			openKits2, altPick(c => { const k = otherSlot(); c.n0 = MmConvert.kitName(I().doc("kit", k)); return k; }),
			{ say: "click Rename, type JOURNEY, press Enter and answer", act: async (u, c) => { u.click('#libpop [data-la="rename"]'); await sleep(250); u.type("#lsin", "JOURNEY"); await sleep(100); u.key("Enter"); await answer(u, c); }, machine: c => ok(MmConvert.kitName(I().doc("kit", c.sel)).trim() === "JOURNEY", "name " + MmConvert.kitName(I().doc("kit", c.sel))), within: 25000 },
			{ ...undoKey, machine: c => ok(MmConvert.kitName(I().doc("kit", c.sel)) === c.n0, "name " + MmConvert.kitName(I().doc("kit", c.sel))), within: 25000 },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};
	const kitClear = {
		name: "mm-lib-kit-clear-undo",
		steps: [
			openKits2, altPick(c => { const k = otherSlot(); c.d0 = JSON.stringify(I().doc("kit", k)); return k; }),
			{ say: "press Delete and answer: the slot is cleared", act: async (u, c) => { u.key("Delete"); await answer(u, c); }, machine: c => ok(MmConvert.kitEmpty(I().doc("kit", c.sel)), "slot " + MmConvert.kitName(I().doc("kit", c.sel))), within: 25000 },
			{ ...undoKey, machine: c => ok(JSON.stringify(I().doc("kit", c.sel)) === c.d0, "slot not back"), within: 25000 },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};
	const patTrigs = p => (I().doc("pattern", p)?.tracks || []).reduce((n, t) => n + t.trig.length, 0);
	const patClear = {
		name: "mm-lib-pattern-clear-undo",
		steps: [
			{ say: "open the pattern chooser and move to a pattern with notes (arrows)", act: async (u, c) => { u.click("#pat"); await sleep(300); c.p = I().slots("pattern").find(p => p !== cur() && patTrigs(p) > 0); if (c.p == null) throw new Error("no other pattern with notes"); c.d0 = JSON.stringify(I().doc("pattern", c.p)); for (let n = 0; n < 128 && LIB.sel !== c.p; n++) { u.key("ArrowRight"); await sleep(20); } },
				screen: c => ok($1(`#libpop .ps[data-ps="${c.p}"]`)?.getAttribute("aria-selected") === "true", "not selected") },
			{ say: "click Clear and answer", act: async (u, c) => { u.click('#libpop [data-la="clear"]'); await answer(u, c); }, machine: c => ok(patTrigs(c.p) === 0, patTrigs(c.p) + " notes"), within: 25000 },
			{ ...undoKey, machine: c => ok(JSON.stringify(I().doc("pattern", c.p)) === c.d0, "not back"), within: 25000 },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};


	/* ---------- the MD port of 2026-10-05 (MM-PORT-PLAN.md): the black keys, the roll's paint ---------- */
	const blackKeys = {
		name: "mm-keys-black-keys",
		steps: [
			go("seq"), sel(0),
			{ say: "press W, T and P: C♯, F♯ and the next D♯ above the A key's C", act: async (u, c) => { results.length = 0; notesOn.length = 0; blur(); c.o = 12 * KB.oct; for (const k of ["w", "t", "p"]) { u.key(k); await sleep(250); } },
				screen: c => ok(same(notesOn.slice(-3), [c.o + 1, c.o + 6, c.o + 15]), "pitches sent " + notesOn.join(",") + ", want " + [c.o + 1, c.o + 6, c.o + 15]),
				machine: () => ok(results.filter(r => r.op === "noteOn" && r.ok).length >= 3, "results " + results.map(r => r.op + ":" + r.ok).join(",")) },
			{ say: "press A and S: the white keys still play C and D", act: async (u, c) => { notesOn.length = 0; for (const k of ["a", "s"]) { u.key(k); await sleep(250); } },
				screen: c => ok(same(notesOn.slice(-2), [c.o, c.o + 2]), "pitches sent " + notesOn.join(",")) }
		]
	};
	/* four empty steps in a row (no note, no NOTE OFF) on track t */
	const freeRun = (t, n = 4) => { const tr = trkOf(t), busy = t < 6 ? trigsOf(t) : midiTrigsOf(t); for (let s = 0; s + n <= S().len; s++) if ([...Array(n).keys()].every(k => !tr.steps[s + k] && !busy.includes(s + k))) return [...Array(n).keys()].map(k => s + k); return null; };
	/* a drag in the roll from step a through steps b..., at the roll's middle row */
	const dragRoll = async (u, t, run, m = {}) => { const p = rollCell(t, run[0]), w = p.c.getBoundingClientRect().width;
		await u.drag(p.c, run.slice(1).map(s => [(rollCell(t, s).fx - p.fx) * w, 0]), m, { fx: p.fx, fy: p.fy }); };
	const rollPaint = {
		name: "mm-seq-roll-paint",
		steps: [
			go("seq"), drawOn,
			{ say: "pick a synth track with four empty steps in a row", act: (u, c) => { c.t = [0, 1, 2, 3, 4, 5].find(t => freeRun(t)); if (c.t == null) throw new Error("no synth track has four free steps in a row"); if (S().side === "midi") u.click('[data-side="int"]'); u.click(rail(c.t)); }, screen: c => ok(S().sel === c.t, "selected " + S().sel) },
			{ say: "press an empty roll cell and drag sideways over three more steps: four notes at that pitch", act: async (u, c) => {
				c.run = freeRun(c.t); c.n = rollCell(c.t, c.run[0]).n; c.t0 = trigsOf(c.t); await dragRoll(u, c.t, c.run); },
				screen: c => ok(c.run.every(s => same(S().tracks[c.t].steps[s]?.n, [c.n])), "roll " + c.run.map(s => JSON.stringify(S().tracks[c.t].steps[s]?.n || null)).join(",")),
				machine: c => ok(c.run.every(s => trigsOf(c.t).includes(s)), "pattern trigs " + trigsOf(c.t).join(",")), within: 15000 },
			/* I-010: the note before them keeps its length: a NOTE OFF on the first step when it ran into it */
			{ say: "Alt-press the first and drag over the others: all four erased", act: (u, c) => dragRoll(u, c.t, c.run, { alt: true }),
				screen: c => ok(c.run.every((s, k) => !S().tracks[c.t].steps[s] || k === 0 && S().tracks[c.t].steps[s].off), "roll " + c.run.map(s => JSON.stringify(S().tracks[c.t].steps[s] || null)).join(",")),
				machine: c => ok(c.run.every(s => !trigsOf(c.t).includes(s)), "pattern trigs " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "Cmd+Z: the erase was one undo step, all four are back", act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(c.run.every(s => trigsOf(c.t).includes(s)), "pattern trigs " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "Cmd+Z: the paint was one undo step, the track is as before", act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(same(trigsOf(c.t), c.t0), "pattern trigs " + trigsOf(c.t).join(",") + ", was " + c.t0.join(",")), within: 15000 }
		],
		async tidy(u, c) { if (c.t0 && !same(trigsOf(c.t), c.t0)) await undoUntil(u, () => same(trigsOf(c.t), c.t0)); drawBack(c); }
	};
	/* ---------- 0.5 slice 3: the piano roll's note lengths, draw and select (I-007, I-010) ---------- */
	const offsOf = t => (t < 6 ? patDoc()?.tracks[t]?.noteOff : patDoc()?.midiTracks?.[t - 6]?.noteOff || []).slice().sort((a, b) => a - b);
	const midiTrigsOf = t => (patDoc()?.midiTracks?.[t - 6]?.trig || []).slice().sort((a, b) => a - b);
	const lenLock = (t, s) => (patDoc()?.locks || []).find(l => l.track === t - 6 && l.page === 7 && l.param === 0)?.steps.find(([x]) => x === s)?.[1];
	/* a point of the roll: step s (fx: its middle, or its right edge less 2 px), pitch n (the row's middle) */
	const rollAt = (t, s, n, edge) => { const c = $1(`#seq canvas.roll[data-big][data-t="${t}"]`), G = laneGeom(c), r = c.getBoundingClientRect(), q = G.col[s];
		if (!q || n < G.lo || n > G.hi) throw new Error(`step ${s + 1} / ${noteName(n)} is not shown`);
		return { c, fx: (edge ? q.x1 - 2 : (q.x0 + q.x1) / 2) / r.width, fy: (G.y(n) + G.rh / 2) / r.height, w: r.width, h: r.height }; };
	/* a press at (s, n) dragged to (s2, n2) (and px more to the right: past a column's middle, where a length counts it) */
	const dragAt = async (u, t, from, to, m = {}, edge = false) => { const a = rollAt(t, from[0], from[1], edge), b = rollAt(t, to[0], to[1]), dx = (b.fx - a.fx) * a.w + (to[2] || 0), dy = (b.fy - a.fy) * a.h;
		await u.drag(a.c, [[dx / 2, dy / 2], [dx, dy]], m, { fx: a.fx, fy: a.fy }); };
	/* Draw on and the Len key at a length (it goes round 1/16 1/8 1/4 1/2 1 bar) */
	const drawLen = async (u, L) => { if (!S().rollDraw) u.click("#rolldraw"); for (let i = 0; i < 6 && S().rollLen !== L; i++) { u.click("#rolllen"); await sleep(80); } };
	/* the middle row's pitch, or one next to it, with no note's bar over step s (a press there would take that note) */
	const freePitch = (t, s) => { const n = rollCell(t, s).n; return [n, n + 1, n - 1, n + 2, n - 2].find(x => !underBar(t, s, x)) ?? n; };
	const clickAt = (u, t, s, n) => { const p = rollAt(t, s, n); u.click(p.c, {}, p.fx, p.fy); };
	const rollDrawLength = {
		name: "mm-roll-draw-length",
		steps: [
			go("seq"),
			{ say: "pick a synth track with six empty steps in a row", act: (u, c) => { c.t = [0, 1, 2, 3, 4, 5].find(t => freeRun(t, 6)); if (c.t == null) throw new Error("no synth track has six free steps in a row"); if (S().side === "midi") u.click('[data-side="int"]'); u.click(rail(c.t)); }, screen: c => ok(S().sel === c.t, "selected " + S().sel) },
			{ say: "Draw on, the Len key at 1/16", act: async (u, c) => { c.d0 = S().rollDraw; c.l0 = S().rollLen; await drawLen(u, 1); }, screen: () => ok(pressed("#rolldraw") && $1("#rolllen").textContent === "1/16", "Draw " + pressed("#rolldraw") + ", Len " + $1("#rolllen").textContent) },
			{ say: "click the run's first cell: a 1/16 note, a NOTE OFF on the next step (it does not run into the next note)", act: (u, c) => {
				c.run = freeRun(c.t, 6); c.n = freePitch(c.t, c.run[0]); c.t0 = trigsOf(c.t); c.o0 = offsOf(c.t); if (S().stepSel) clearSel(); clickAt(u, c.t, c.run[0], c.n); },
				screen: c => ok(!!S().tracks[c.t].steps[c.run[0]]?.n && !!S().tracks[c.t].steps[c.run[1]]?.off, "roll " + JSON.stringify(S().tracks[c.t].steps.slice(c.run[0], c.run[0] + 2))),
				machine: c => ok(trigsOf(c.t).includes(c.run[0]) && offsOf(c.t).includes(c.run[1]), `pattern trigs ${trigsOf(c.t).join(",")}, NOTE OFFs ${offsOf(c.t).join(",")}`), within: 15000 },
			{ say: "click the Len key (1/8), then the run's fourth cell: a note two steps long", act: async (u, c) => { u.click("#rolllen"); await sleep(150); clickAt(u, c.t, c.run[3], c.n); },
				screen: c => ok($1("#rolllen").textContent === "1/8" && !!S().tracks[c.t].steps[c.run[5]]?.off, "Len " + $1("#rolllen").textContent + ", roll " + JSON.stringify(S().tracks[c.t].steps.slice(c.run[3], c.run[3] + 3))),
				machine: c => ok(trigsOf(c.t).includes(c.run[3]) && offsOf(c.t).includes(c.run[5]), `pattern trigs ${trigsOf(c.t).join(",")}, NOTE OFFs ${offsOf(c.t).join(",")}`), within: 15000 },
			{ say: "drag the first note's end over two more steps: it lasts three steps, up to the next note (its NOTE OFF goes); new notes get 3/16", act: (u, c) => dragAt(u, c.t, [c.run[0], c.n], [c.run[2], c.n, 5], {}, true),
				screen: c => ok(!S().tracks[c.t].steps[c.run[1]] && $1("#rolllen").textContent === "3/16", "roll " + JSON.stringify(S().tracks[c.t].steps.slice(c.run[0], c.run[0] + 3)) + ", Len " + $1("#rolllen").textContent),
				machine: c => ok(!offsOf(c.t).includes(c.run[1]) && trigsOf(c.t).includes(c.run[0]), `NOTE OFFs ${offsOf(c.t).join(",")}`), within: 15000 },
			{ say: "Cmd+Z three times: the track as before", act: async (u, c) => { await undoUntil(u, () => same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0), 4); },
				machine: c => ok(same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0), `pattern trigs ${trigsOf(c.t).join(",")}, NOTE OFFs ${offsOf(c.t).join(",")}`), within: 15000 }
		],
		async tidy(u, c) { if (c.t0 && !(same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0))) await undoUntil(u, () => same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0)); if (c.l0) setRollLen(c.l0); if (c.d0 != null && S().rollDraw !== c.d0) setRollDraw(c.d0); }
	};
	const rollBoxMove = {
		name: "mm-roll-box-move",
		steps: [
			go("seq"),
			{ say: "pick a synth track with eight empty steps in a row, Draw on at 1/16, click the first: a note", act: async (u, c) => {
				c.t = [0, 1, 2, 3, 4, 5].find(t => freeRun(t, 8)); if (c.t == null) throw new Error("no synth track has eight free steps in a row"); if (S().side === "midi") u.click('[data-side="int"]'); u.click(rail(c.t)); await sleep(300);
				c.d0 = S().rollDraw; c.l0 = S().rollLen; await drawLen(u, 1); c.run = freeRun(c.t, 8); c.n = freePitch(c.t, c.run[0]); c.t0 = trigsOf(c.t); c.o0 = offsOf(c.t); if (S().stepSel) clearSel(); clickAt(u, c.t, c.run[0], c.n); },
				machine: c => ok(trigsOf(c.t).includes(c.run[0]) && offsOf(c.t).includes(c.run[1]), `pattern trigs ${trigsOf(c.t).join(",")}, NOTE OFFs ${offsOf(c.t).join(",")}`), within: 15000 },
			{ say: "press B: Draw off (select)", act: u => { blur(); u.key("b"); }, screen: () => ok(!S().rollDraw && !pressed("#rolldraw"), "Draw " + S().rollDraw) },
			{ say: "drag a box from an empty place round the note: the note is selected, its NOTE OFF too", act: (u, c) => dragAt(u, c.t, [c.run[2], c.n + 1], [c.run[0], c.n - 1]),
				screen: c => ok(same(S().stepSel, { t: c.t, n: 1, from: c.run[0], to: c.run[0] + 2 }), "selection " + JSON.stringify(S().stepSel)), machine: c => ok(trigsOf(c.t).includes(c.run[0]), "a box changed the pattern") },
			{ say: "drag the note four steps right: it moves, its NOTE OFF with it (one undo step)", act: (u, c) => dragAt(u, c.t, [c.run[0], c.n], [c.run[4], c.n]),
				screen: c => ok(!!S().tracks[c.t].steps[c.run[4]]?.n && !S().tracks[c.t].steps[c.run[0]]?.n && same(S().stepSel, { t: c.t, n: 1, from: c.run[4], to: c.run[4] + 2 }), "roll " + JSON.stringify(S().tracks[c.t].steps.slice(c.run[0], c.run[0] + 6)) + ", selection " + JSON.stringify(S().stepSel)),
				machine: c => ok(trigsOf(c.t).includes(c.run[4]) && !trigsOf(c.t).includes(c.run[0]) && offsOf(c.t).includes(c.run[5]), `pattern trigs ${trigsOf(c.t).join(",")}, NOTE OFFs ${offsOf(c.t).join(",")}`), within: 15000 },
			{ say: "press Delete: the selected note goes", act: u => { blur(); u.key("Delete"); }, machine: c => ok(!trigsOf(c.t).includes(c.run[4]), `pattern trigs ${trigsOf(c.t).join(",")}`), within: 15000 },
			{ say: "press B: Draw on; Cmd+Z three times (delete, move, note): the track as before", act: async (u, c) => { blur(); u.key("b"); await sleep(150); await undoUntil(u, () => same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0), 4); },
				screen: () => ok(S().rollDraw, "Draw off"), machine: c => ok(same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0), `pattern trigs ${trigsOf(c.t).join(",")}, NOTE OFFs ${offsOf(c.t).join(",")}`), within: 15000 }
		],
		async tidy(u, c) { if (S().stepSel) clearSel(); if (c.t0 && !(same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0))) await undoUntil(u, () => same(trigsOf(c.t), c.t0) && same(offsOf(c.t), c.o0)); if (c.l0) setRollLen(c.l0); if (c.d0 != null && S().rollDraw !== c.d0) setRollDraw(c.d0); }
	};
	const rollMidiLen = {
		name: "mm-roll-midi-len",
		steps: [
			go("seq"),
			{ say: "click MIDI and pick a MIDI track with six empty steps in a row", act: async (u, c) => { if (S().side !== "midi") u.click('[data-side="midi"]'); await sleep(300); c.t = [6, 7, 8, 9, 10, 11].find(t => freeRun(t, 6)); if (c.t == null) throw new Error("no MIDI track has six free steps in a row"); u.click(rail(c.t)); },
				screen: c => ok(S().sel === c.t, "selected " + S().sel) },
			{ say: "Draw on at 1/16, click the run's first cell: a MIDI note 6 ticks long (LEN 6)", act: async (u, c) => {
				c.d0 = S().rollDraw; c.l0 = S().rollLen; await drawLen(u, 1); c.run = freeRun(c.t, 6); c.n = freePitch(c.t, c.run[0]); c.t0 = midiTrigsOf(c.t); c.kit = S().midi[c.t - 6].v.MID[0]; if (S().stepSel) clearSel(); clickAt(u, c.t, c.run[0], c.n); },
				machine: c => ok(midiTrigsOf(c.t).includes(c.run[0]) && (lenLock(c.t, c.run[0]) ?? c.kit) === 6, `MIDI trigs ${midiTrigsOf(c.t).join(",")}, LEN ${lenLock(c.t, c.run[0]) ?? "kit " + c.kit}`), within: 15000 },
			{ say: "drag its end over two more steps: LEN 18 (three steps)", act: (u, c) => dragAt(u, c.t, [c.run[0], c.n], [c.run[2], c.n, 5], {}, true),
				screen: c => ok(S().locks.get(c.t + "|MID.0")?.get(c.run[0]) === 18, "LEN lock " + S().locks.get(c.t + "|MID.0")?.get(c.run[0])),
				machine: c => ok(lenLock(c.t, c.run[0]) === 18, "LEN " + lenLock(c.t, c.run[0])), within: 15000 },
			{ say: "Cmd+Z twice: the track as before", act: async (u, c) => { await undoUntil(u, () => same(midiTrigsOf(c.t), c.t0), 3); },
				machine: c => ok(same(midiTrigsOf(c.t), c.t0), "MIDI trigs " + midiTrigsOf(c.t).join(",")), within: 15000 }
		],
		async tidy(u, c) { if (c.t0 && !same(midiTrigsOf(c.t), c.t0)) await undoUntil(u, () => same(midiTrigsOf(c.t), c.t0)); if (c.l0) setRollLen(c.l0); if (c.d0 != null && S().rollDraw !== c.d0) setRollDraw(c.d0); if (S().side === "midi") $1('[data-side="int"]')?.click(); }
	};
	/* K7 (DESIGN-step-selection.md §7, the Machinedrum's md-seq-select-copy-paste): one note copied to another step of
	   its track (the LCD's PASTE on the selection), then a block of two tracks duplicated and cleared with the LCD's CLR */
	const inPat = s => s < S().len;
	const selCells = () => $all("#seq .tlane .tc.selx[data-tl=\"sld\"]").map(e => +e.dataset.s);
	const selectCopyPaste = {
		name: "mm-seq-select-copy-paste",
		steps: [
			go("seq"),
			{ say: "pick a synth track with a note (and one below it)", act: (u, c) => { c.t = [0, 1, 2, 3, 4].find(t => trigsOf(t).some(inPat)) ?? 0; if (S().len < 8) throw new Error("the pattern is shorter than 8 steps"); if (S().side === "midi") u.click('[data-side="int"]'); u.click(rail(c.t)); }, screen: c => ok(S().sel === c.t, "selected " + S().sel) },
			{ say: "Cmd-click a note in the roll: it is selected", act: (u, c) => {
				c.t0 = trigsOf(c.t); c.t1 = trigsOf(c.t + 1); c.p0 = pat0();
				c.s = c.t0.find(inPat); c.d = [...Array(S().len).keys()].find(s => !S().tracks[c.t].steps[s] && Math.abs(s - c.s) > 1 && (s < 4 || s >= 8));
				if (c.d == null) throw new Error("no free step to paste to");
				if (S().stepSel) clearSel(); const p = rollCell(c.t, c.s); u.click(p.c, { cmd: true }, p.fx, p.fy);
			}, screen: c => ok(same(S().stepSel, { t: c.t, n: 1, from: c.s, to: c.s + 1 }) && same(selCells(), [c.s]), "selection " + JSON.stringify(S().stepSel)),
				machine: c => ok(pat0() === c.p0, "a Cmd-click changed the pattern") },
			{ say: "press Cmd+C", act: u => { blur(); u.key("c", { cmd: true }); }, screen: () => ok(/Copied/.test($1("#toast")?.textContent || ""), "toast " + $1("#toast")?.textContent) },
			{ say: "Cmd-click an empty step of the same track", act: (u, c) => { const p = rollCell(c.t, c.d); u.click(p.c, { cmd: true }, p.fx, p.fy); },
				screen: c => ok(same(selCells(), [c.d]) && $1('[data-sec="paste"]').classList.contains("onsel"), "selected " + selCells().join(",") + ", PASTE marked " + $1('[data-sec="paste"]').classList.contains("onsel")) },
			{ say: "click PASTE on the LCD: the note lands on the selected step", act: u => u.click('[data-sec="paste"]'), screen: c => ok(!!S().tracks[c.t].steps[c.d]?.n, "roll shows nothing there"),
				machine: c => ok(trigsOf(c.t).includes(c.d), "pattern " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "Cmd-drag in the roll from step 1 to step 4: a block", act: async (u, c) => { c.b0 = [c.t, c.t + 1].map(t => trigsOf(t).filter(s => s < 4)); await dragRoll(u, c.t, [0, 2, 3], { cmd: true }); },
				screen: c => ok(same(S().stepSel, { t: c.t, n: 1, from: 0, to: 4 }), "selection " + JSON.stringify(S().stepSel)) },
			{ say: "pick the track below on the rail and Cmd-Shift-click its step 4: the block takes both tracks", act: async (u, c) => { u.click(rail(c.t + 1)); await sleep(300); const p = rollCell(c.t + 1, 3); u.click(p.c, { cmd: true, shift: true }, p.fx, p.fy); },
				screen: c => ok(same(S().stepSel, { t: c.t, n: 2, from: 0, to: 4 }) && $all("#rail .th.selt").length === 2, "selection " + JSON.stringify(S().stepSel)) },
			{ say: "press Cmd+D: the block again on steps 5-8", act: u => { blur(); u.key("d", { cmd: true }); },
				machine: c => ok([c.t, c.t + 1].every((t, k) => same(trigsOf(t).filter(s => s >= 4 && s < 8).map(s => s - 4), c.b0[k])), "steps 5-8 " + [c.t, c.t + 1].map(t => trigsOf(t).filter(s => s >= 4 && s < 8).join(",")).join(" / ")),
				screen: () => ok(S().stepSel?.from === 4 && S().stepSel?.n === 2, "selection " + JSON.stringify(S().stepSel)), within: 15000 },
			{ say: "click CLR on the LCD: the selected block (steps 5-8 of both tracks) is cleared, nothing else", act: (u, c) => { c.out = [c.t, c.t + 1].map(t => trigsOf(t).filter(s => s < 4 || s >= 8)); u.click('[data-sec="clear"]'); },
				machine: c => ok([c.t, c.t + 1].every((t, k) => !trigsOf(t).some(s => s >= 4 && s < 8) && same(trigsOf(t).filter(s => s < 4 || s >= 8), c.out[k])), "pattern " + [c.t, c.t + 1].map(t => trigsOf(t).join(",")).join(" / ")), within: 15000 },
			{ say: "press Esc: no selection", act: u => { blur(); u.key("Escape"); }, screen: () => ok(!S().stepSel && !selCells().length, "still selected") },
			{ say: "Cmd+Z three times (paste, duplicate, clear): both tracks as before", act: async (u, c) => { await undoUntil(u, () => same(trigsOf(c.t), c.t0) && same(trigsOf(c.t + 1), c.t1), 3); },
				machine: c => ok(same(trigsOf(c.t), c.t0) && same(trigsOf(c.t + 1), c.t1), `pattern ${trigsOf(c.t).join(",")} / ${trigsOf(c.t + 1).join(",")}`), within: 15000 }
		],
		async tidy(u, c) { if (S().stepSel) clearSel(); if (c.t0 && !(same(trigsOf(c.t), c.t0) && same(trigsOf(c.t + 1), c.t1))) await undoUntil(u, () => same(trigsOf(c.t), c.t0) && same(trigsOf(c.t + 1), c.t1)); }
	};
	/* K7 (the Machinedrum's md-seq-step-menu): the step menu: a slide, a copy and a paste with the pointer only */
	const stepMenuJ = {
		name: "mm-seq-step-menu",
		steps: [
			go("seq"), sel(0), drawOn,
			{ say: "click an empty roll cell: a note to work on", act: async (u, c) => {
				c.t0 = trigsOf(c.t); c.l0 = slidesOf(c.t); c.n = rollCell(c.t, 0).n;
				const free = freeSteps(c.t, 2, 0, c.n); if (free.length < 2) throw new Error("no two free steps"); [c.s, c.d] = free.slice().sort((a, b) => b - a);
				if (S().stepSel) clearSel(); await roll(c.t, c.s)(u); },
				screen: c => ok(!!S().tracks[c.t].steps[c.s]?.n, "no note"), machine: c => ok(trigsOf(c.t).includes(c.s), "pattern " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "right-click it: its menu", act: (u, c) => { const p = rollCell(c.t, c.s); u.rightClick(p.c, {}, p.fx, p.fy); },
				screen: c => ok(!$1("#deskmenu").hidden && !!$1('#deskmenu [data-mid="step-fill-2"]') && same(S().stepSel, { t: c.t, n: 1, from: c.s, to: c.s + 1 }), "no step menu") },
			{ say: "choose Slide", act: u => u.click('#deskmenu [data-mid="step-slide"]'), screen: c => ok($1("#deskmenu").hidden && S().tracks[c.t].slide.has(c.s), "no slide shown"),
				machine: c => ok(slidesOf(c.t).includes(c.s), "slides " + slidesOf(c.t).join(",")), within: 15000 },
			{ say: "right-click it again and choose Copy", act: async (u, c) => { const p = rollCell(c.t, c.s); u.rightClick(p.c, {}, p.fx, p.fy); await sleep(150); u.click('#deskmenu [data-mid="copy"]'); },
				screen: () => ok(/Copied/.test($1("#toast")?.textContent || ""), "toast " + $1("#toast")?.textContent) },
			{ say: "right-click an empty step and choose Paste here", act: async (u, c) => { const p = rollCell(c.t, c.d); u.rightClick(p.c, {}, p.fx, p.fy); await sleep(150); u.click('#deskmenu [data-mid="paste"]'); },
				screen: c => ok(!!S().tracks[c.t].steps[c.d]?.n, "nothing there"), machine: c => ok(trigsOf(c.t).includes(c.d) && slidesOf(c.t).includes(c.d), "pattern " + trigsOf(c.t).join(",") + ", slides " + slidesOf(c.t).join(",")), within: 15000 },
			{ say: "press Escape: no selection; Cmd+Z three times (paste, slide, note): the track as before", act: async (u, c) => { blur(); u.key("Escape"); await sleep(150); await undoUntil(u, () => same(trigsOf(c.t), c.t0) && same(slidesOf(c.t), c.l0), 4); },
				machine: c => ok(same(trigsOf(c.t), c.t0) && same(slidesOf(c.t), c.l0), `pattern ${trigsOf(c.t).join(",")}, slides ${slidesOf(c.t).join(",")}`), within: 15000 }
		],
		async tidy(u, c) { if (!$1("#deskmenu")?.hidden) closeDeskMenu(); if (S().stepSel) clearSel(); if (c.t0 && !same(trigsOf(c.t), c.t0)) await undoUntil(u, () => same(trigsOf(c.t), c.t0)); drawBack(c); }
	};
	/* the Machinedrum's md-seq-copy-paste-buttons: one note copied and pasted with the top bar's Copy, Paste and Clr */
	const buttonsCopyPaste = {
		name: "mm-seq-copy-paste-buttons",
		steps: [
			go("seq"),
			{ say: "pick a synth track with a note and Cmd-click the note: it is selected", act: async (u, c) => {
				c.t = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).some(inPat)) ?? 0; if (S().side === "midi") u.click('[data-side="int"]'); u.click(rail(c.t)); await sleep(300);
				c.t0 = trigsOf(c.t); c.s = c.t0.find(inPat); c.d = [...Array(S().len).keys()].find(s => !S().tracks[c.t].steps[s] && Math.abs(s - c.s) > 1);
				if (c.d == null) throw new Error("no free step to paste to");
				if (S().stepSel) clearSel(); const p = rollCell(c.t, c.s); u.click(p.c, { cmd: true }, p.fx, p.fy); },
				screen: c => ok(same(S().stepSel, { t: c.t, n: 1, from: c.s, to: c.s + 1 }), "selection " + JSON.stringify(S().stepSel)) },
			{ say: "click Copy in the top bar: the selected note is copied", act: u => u.click('[data-sec="copy"]'), screen: c => ok(/Copied/.test($1("#toast")?.textContent || "") && S().stepSel?.from === c.s, "toast " + $1("#toast")?.textContent) },
			{ say: "Cmd-click an empty step of the same track", act: (u, c) => { const p = rollCell(c.t, c.d); u.click(p.c, { cmd: true }, p.fx, p.fy); }, screen: c => ok(same(S().stepSel, { t: c.t, n: 1, from: c.d, to: c.d + 1 }), "selection " + JSON.stringify(S().stepSel)) },
			{ say: "click Paste in the top bar: the note lands at the selected step", act: u => u.click('[data-sec="paste"]'),
				machine: c => ok(trigsOf(c.t).includes(c.d) && same(trigsOf(c.t).filter(s => s !== c.d), c.t0), "pattern " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "click Clr in the top bar: the pasted note is cleared again, nothing else", act: u => u.click('[data-sec="clear"]'), machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "Cmd+Z twice (clear, paste): the track as before", act: async (u, c) => { u.key("Escape"); await undoUntil(u, () => trigsOf(c.t).includes(c.d), 2); await sleep(300); await undoUntil(u, () => same(trigsOf(c.t), c.t0), 2); },
				machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",")), within: 15000 }
		],
		async tidy(u, c) { if (S().stepSel) clearSel(); if (c.t0 && !same(trigsOf(c.t), c.t0)) await undoUntil(u, () => same(trigsOf(c.t), c.t0)); }
	};
	/* B-019: a .syx imported as from a cable (the dumps on SYSEX RECV). The file is the run's (GEARMULATOR_MDMM_SYX_FILE,
	   diagnostics builds: the plug-in opens it where the chooser would be). Kits only; afterwards every kit the report
	   does not list (taken as in the file) has the file's name on the machine. An import has no Undo: last. */
	const syxKitIds = () => $all('#syxpop button[data-syxitem^="kit:"]');
	/* the report: the slots of a kind the machine did not take as in the file (every reported item carries its outcome) */
	const syxNotTaken = kind => new Set($all(`#syxpop [data-syxitem^="${kind}:"][data-syxout]`).filter(d => d.dataset.syxout !== "taken").map(d => +d.dataset.syxitem.split(":")[1]));
	/* a .syx dropped on the window (FOUNDATION.md, "Files dropped on the window"): the run's file, dropped through the
	   window's drop path from the native side on ("dropfiles <x> <y> syx" in the page's log, diagnostics builds; as
	   md-drop-syx): its import window opens */
	const dropSyxJ = {
		name: "mm-drop-syx",
		needs: () => new URLSearchParams(location.search).get("syxfile") ? null
			: "no .syx for the run (GEARMULATOR_MDMM_SYX_FILE)",
		steps: [
			{ say: "drop the run's .syx on the page: its import window opens with the file's preview",
				act: () => Bridge.log(`dropfiles ${Math.round(innerWidth / 2)} ${Math.round(innerHeight / 2)} syx`),
				screen: () => ok(!$1("#syxpop").hidden && $all("#syxpop [data-syxkind]").length > 0, "no preview"),
					within: 8000 },
			{ say: "click Cancel: it closes, nothing is sent",
				act: u => u.click('#syxpop .syxfoot [data-syxgo="close"]'),
				screen: () => ok($1("#syxpop").hidden, "still open") }
		],
		async tidy(u) { if (!$1("#syxpop").hidden) u.key("Escape"); }
	};
	const syxImportJ = {
		name: "mm-lib-syx-import",
		needs: () => new URLSearchParams(location.search).get("syxfile") ? null : "no .syx for the run (GEARMULATOR_MDMM_SYX_FILE)",
		steps: [
			openKits2,
			{ say: "click Import SysEx…: the file's preview", act: u => u.click('#libpop [data-syx="import"]'), screen: () => ok(!$1("#syxpop").hidden && syxKitIds().length > 0, "no preview with kits"), within: 8000 },
			{ say: "leave Kits ticked only, click Import: sent on SYSEX RECV, read back, reported", act: (u, c) => {
				for (const b of $all("#syxpop [data-syxkind]")) if (b.checked !== (b.dataset.syxkind === "kit")) u.click(b);
				c.names = syxKitIds().map(b => [+b.dataset.syxitem.split(":")[1], (b.dataset.syxname || "").trim()]);
				u.click('#syxpop [data-syxgo="start"]');
			}, screen: () => ok(/imported/.test($1("#syxpop .syxsum")?.textContent || ""), "progress: " + ($1("#syxpop .syxbar span")?.textContent || "")),
			machine: c => {
				const notTaken = syxNotTaken("kit");
				const off = c.names.filter(([k, n]) => !notTaken.has(k) && n && (kitName(k) || "").trim() !== n);
				return ok(!off.length, off.length + " kits not as in the file: " + off.slice(0, 4).map(([k, n]) => `K${k + 1} "${kitName(k)}" not "${n}"`).join(", "));
			}, within: 240000 },
			{ say: "click Done: the panel closes", act: u => u.click('#syxpop [data-syxgo="close"]'), screen: () => ok($1("#syxpop").hidden, "still open") },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		]
	};
	/* B-026 / B-031 (the Machinedrum's md-lib-syx-import-mute): import a file with its globals, then mute on the rail and
	   play: the machine holds the mute (its memory, not the page's wish); a track the active global gives no MIDI channel
	   (B-051: base OFF, or beyond CHANNEL SPAN) is refused, and the notice's fix (each track its own channel) makes the next M work. Then the pattern still changes, stopped and
	   playing. The file is the run's (GEARMULATOR_MDMM_SYX_FILE). */
	const globalNow = () => I().doc("global", machine().global?.current ?? 0);
	const reaches = t => { const g = globalNow(); return !!g && g.baseChannel <= 14 && t < g.channelSpan && g.baseChannel + t <= 14; };
	const railM = t => `#rail .th[data-sel="${t}"] .ms.m`;
	const syxImportMute = {
		name: "mm-lib-syx-import-mute",
		needs: () => new URLSearchParams(location.search).get("syxfile") ? null : "no .syx for the run (GEARMULATOR_MDMM_SYX_FILE)",
		steps: [
			openKits2,
			{ say: "click Import SysEx…, tick Globals too", act: async u => { u.click('#libpop [data-syx="import"]'); await until(() => !$1("#syxpop").hidden && $all("#syxpop [data-syxkind]").length, 8000); const g = $1('#syxpop [data-syxkind="global"]'); if (g && !g.checked) u.click(g); },
				screen: () => ok(!$1("#syxpop").hidden && !!$1('#syxpop [data-syxkind="global"]')?.checked, "no Globals in the preview"), within: 8000 },
			{ say: "click Import: sent on SYSEX RECV, read back, reported", act: u => u.click('#syxpop [data-syxgo="start"]'), screen: () => ok(/imported/.test($1("#syxpop .syxsum")?.textContent || ""), "progress: " + ($1("#syxpop .syxbar span")?.textContent || "")), within: 900000 },
			{ say: "click Done, Escape, then the Sequence tab", act: async u => { u.click('#syxpop [data-syxgo="close"]'); await sleep(300); u.key("Escape"); await sleep(300); u.click(tab("seq")); if (S().side === "midi") u.click('[data-side="int"]'); },
				screen: () => onTab("seq"), machine: () => ok(!!globalNow() && machine().mutes?.synth != null, "global " + !!globalNow() + ", mutes " + machine().mutes?.synth), within: 8000 },
			{ say: "click M on a track of the rail: its mute flips on the machine; a track with no MIDI channel (B-051) is refused, the notice offers the fix: take it, then M again",
				act: async (u, c) => { c.m0 = synthMutes(); c.t = [0, 1, 2, 3, 4, 5].find(t => !c.m0.includes(t)) ?? 0; c.want = !c.m0.includes(c.t); c.fixed = !reaches(c.t); await sleep(1500); u.click(railM(c.t));
					if (c.fixed) { if (!await until(dlgShown, 4000)) throw new Error("no notice for the refused mute"); u.click(dlgButton("Give each track its own channel")); await until(() => reaches(c.t), 15000); await sleep(3000); u.click(railM(c.t)); } },
				machine: c => ok(synthMuted(c.t) === c.want, `base ${globalNow()?.baseChannel}, span ${globalNow()?.channelSpan}, track ${c.t + 1}${c.fixed ? " (channels given)" : ""}, machine mutes ${synthMutes()} (before ${c.m0})`),
				screen: c => ok(pressed(railM(c.t)) === c.want && !dlgShown(), "M " + (pressed(railM(c.t)) ? "lit" : "not lit")), within: 30000 },
			{ say: "press PLAY: the mute holds", act: u => { tele.steps = []; u.click("#play"); },
				machine: c => ok(new Set(tele.steps).size >= 4 && synthMuted(c.t) === c.want, `machine mutes ${synthMutes()}, steps ${tele.steps.length}`),
				screen: c => ok(pressed(railM(c.t)) === c.want, "M " + (pressed(railM(c.t)) ? "lit" : "not lit")), within: 10000 },
			{ say: "press STOP", act: u => u.click("#play"), machine: () => ok(tele.last && !tele.last.playing, "still playing"), within: 6000 },
			/* B-031: after an import the pattern still changes, stopped and playing */
			{ say: "click › next to the pattern: the machine selects it (stopped)", act: async (u, c) => { c.p0 = cur(); u.click("#patNext"); await confirmIfAsked(u); },
				machine: c => ok(cur() === c.p0 + 1, "machine pattern " + cur()), within: 8000 },
			{ say: "press PLAY, click ‹: the machine moves to it at the pattern's end (playing)", act: async u => { u.click("#play"); await sleep(500); u.click("#patPrev"); await confirmIfAsked(u); },
				machine: c => ok(cur() === c.p0 && S().playing, "machine pattern " + cur() + (S().playing ? ", playing" : ", stopped")), within: 30000 },
			{ say: "press STOP", act: u => u.click("#play"), machine: () => ok(tele.last && !tele.last.playing, "still playing"), within: 6000 }
		],
		async tidy(u, c) { if (dlgShown()) u.key("Escape"); if (S().playing) u.click("#play"); await sleep(300); if (c.t != null && synthMuted(c.t) !== c.m0.includes(c.t)) u.click(railM(c.t)); await sleep(500); }
	};
	/* the Machinedrum's md-seq-os-copy-paste: one note copied and pasted on its track with ⌘C / ⌘V as the operating system
	   delivers them (on macOS JUCE's web view turns them into the copy: and paste: commands, never a keydown), and ⌘Z the
	   same way in */
	const osCopyPaste = {
		name: "mm-seq-os-copy-paste", needs: Journey.osKeyPath,
		steps: [
			go("seq"),
			{ say: "pick a synth track with a note and Cmd-click the note: it is selected", act: async (u, c) => {
				c.t = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).some(inPat)) ?? 0; if (S().side === "midi") u.click('[data-side="int"]'); u.click(rail(c.t)); await sleep(300);
				c.t0 = trigsOf(c.t); c.s = c.t0.find(inPat); c.d = [...Array(S().len).keys()].find(s => !S().tracks[c.t].steps[s] && Math.abs(s - c.s) > 1);
				if (c.d == null) throw new Error("no free step to paste to");
				if (S().stepSel) clearSel(); const p = rollCell(c.t, c.s); u.click(p.c, { cmd: true }, p.fx, p.fy); },
				screen: c => ok(same(S().stepSel, { t: c.t, n: 1, from: c.s, to: c.s + 1 }), "selection " + JSON.stringify(S().stepSel)) },
			{ say: "press Cmd+C on the keyboard (the operating system's way in)", act: async u => { blur(); await u.osKey("focus cmd+c"); },
				screen: () => ok(Keys.seen().includes("cmd+C") && /Copied/.test($1("#toast")?.textContent || ""), "keys that reached the page: " + Keys.seen().join(" ") + "; toast " + $1("#toast")?.textContent), within: 4000 },
			{ say: "Cmd-click an empty step of the same track", act: (u, c) => { const p = rollCell(c.t, c.d); u.click(p.c, { cmd: true }, p.fx, p.fy); }, screen: c => ok(same(S().stepSel, { t: c.t, n: 1, from: c.d, to: c.d + 1 }), "selection " + JSON.stringify(S().stepSel)) },
			{ say: "press Cmd+V on the keyboard: the note lands there", act: u => u.osKey("cmd+v"), screen: c => ok(!!S().tracks[c.t].steps[c.d]?.n, "roll shows nothing there"),
				machine: c => ok(trigsOf(c.t).includes(c.d), "pattern " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "press Escape, then Cmd+Z on the keyboard: the track as before", act: async u => { blur(); u.key("Escape"); await sleep(200); await u.osKey("cmd+z"); },
				machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",")), within: 15000 }
		],
		async tidy(u, c) { if (S().stepSel) clearSel(); if (c.t0 && !same(trigsOf(c.t), c.t0)) await undoUntil(u, () => same(trigsOf(c.t), c.t0)); }
	};
	/* the Machinedrum's md-seq-rotate-undo: a rotate run ends when the page sees Alt up in any event, not only in Alt's own
	   keyup (which may never come: these synthetic chords send none, as a window switch can lose it); the next rotate is
	   then its own undo step */
	const rotateUndo = {
		name: "mm-seq-rotate-undo",
		steps: [
			go("seq"),
			{ say: "pick a synth track with notes that a rotate changes", act: (u, c) => { const L = S().len, r = a => a.map(s => s >= L ? s : (s + 1) % L).sort((x, y) => x - y);
				c.t = [0, 1, 2, 3, 4, 5].find(t => trigsOf(t).length && !same(r(trigsOf(t)), trigsOf(t)) && !same(r(r(trigsOf(t))), r(trigsOf(t)))); if (c.t == null) throw new Error("no track a rotate changes");
				c.t0 = trigsOf(c.t); c.t1 = r(c.t0); if (S().side === "midi") u.click('[data-side="int"]'); u.click(rail(c.t)); }, screen: c => ok(S().sel === c.t, "selected " + S().sel) },
			{ say: "press Alt+→: one step later", act: u => { blur(); u.key("ArrowRight", { alt: true }); }, machine: c => ok(same(trigsOf(c.t), c.t1), "notes " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "press Shift (Alt is up now), then Alt+→ again: another rotate", act: async u => { u.key("Shift"); await sleep(300); u.key("ArrowRight", { alt: true }); }, machine: c => ok(!same(trigsOf(c.t), c.t1), "notes " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "Cmd+Z: only the second rotate is undone", act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(same(trigsOf(c.t), c.t1), "notes " + trigsOf(c.t).join(",") + ", want " + c.t1.join(",")), within: 15000 },
			{ say: "Cmd+Z: and then the first", act: u => u.key("z", { cmd: true }), machine: c => ok(same(trigsOf(c.t), c.t0), "notes " + trigsOf(c.t).join(",") + ", want " + c.t0.join(",")), within: 15000 }
		],
		async tidy(u, c) { if (c.t0 && !same(trigsOf(c.t), c.t0)) await undoUntil(u, () => same(trigsOf(c.t), c.t0), 4); }
	};
	/* the Machinedrum's md-mix-fader-undo: a LEVEL fader dragged, then Cmd+Z: the kit's level as before */
	const faderUndo = {
		name: "mm-mix-fader-undo",
		steps: [
			go("mix"),
			{ say: "drag track 2's LEVEL fader", act: async (u, c) => { c.l0 = wk().levels[1]; const d = c.l0 > 64 ? 1 : -1; await u.drag(strip(1, '.fader[data-g="lev"]'), [[0, d * 8], [0, d * 16], [0, d * 24]]); },
				screen: c => ok($1(strip(1, ".lread")).textContent === String(S().tracks[1].lev) && S().tracks[1].lev !== c.l0, "shows " + $1(strip(1, ".lread")).textContent), machine: c => ok(wk().levels[1] !== c.l0, "kit level " + wk().levels[1]), within: 8000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(wk().levels[1] === c.l0, "kit level " + wk().levels[1]), screen: c => ok($1(strip(1, ".lread")).textContent === String(c.l0), "shows " + $1(strip(1, ".lread")).textContent), within: 8000 }
		]
	};
	/* Radek 2026-10-10: ⇧B taps the tempo on every workspace of both editors, Sequence included (B is Draw there) */
	const tapTempoShift = {
		name: "mm-keys-tap-tempo-shift",
		steps: [
			go("seq"),
			{ say: "on Sequence tap Shift+B five times, about 0.5 s apart: the tempo follows, Draw stays as it was", act: async (u, c) => { c.b0 = machine().tempo; c.d0 = S().rollDraw; blur(); const at = []; for (let i = 0; i < 5; i++) { at.push(performance.now()); u.key("B", { shift: true }); await sleep(500); } c.want = Math.round(60000 / ((at[4] - at[0]) / 4) * 10) / 10; c.note = c.want + " BPM"; },
				screen: c => ok(Math.abs(S().bpm - c.want) <= 1.5 && S().rollDraw === c.d0, "BPM " + S().bpm + ", want " + c.want + ", Draw " + S().rollDraw), machine: c => ok(Math.abs(machine().tempo - c.want) <= 1.5, "tempo " + machine().tempo + ", want " + c.want), within: 8000 }
		],
		async tidy(u, c) { if (c.b0 != null) { const d = c.b0 > machine().tempo ? -1 : 1; await u.drag("#bpm", [[0, d * 2 * (c.b0 - machine().tempo)]]); } }
	};
	/* the SysEx import panel as screenshots for a design review (scripts/mdmm-shots.sh with MDMM_SHOTS_JOURNEY=mm-shots-import
	   and GEARMULATOR_MDMM_SYX_FILE): the preview's tabs, then the default kinds imported (importing, the report).
	   Each step logs "SHOT <name>" and holds while the script captures the window. */
	const shot = name => Bridge.log("SHOT " + name), hold = 2500;
	const shotsImport = { name: "mm-shots-import", needs: () => !/mm-shots/.test(location.search) ? "screenshots only when asked by name" : syxImportJ.needs(),
		steps: [
			openKits2,
			{ say: "click Import SysEx…: the preview, the Kits tab", act: async u => { u.click('#libpop [data-syx="import"]'); await until(() => syxKitIds().length > 0, 8000); await sleep(400); shot("import-1-mm-kits"); },
				screen: () => ok(!$1("#syxpop").hidden && syxKitIds().length > 0, "no preview with kits"), within: 10000, hold },
			{ say: "the Songs tab", act: async u => { u.click('#syxpop [data-syxtab="song"]'); await sleep(400); shot("import-2-mm-songs"); }, hold },
			{ say: "the Globals tab", act: async u => { u.click('#syxpop [data-syxtab="global"]'); await sleep(400); shot("import-3-mm-globals"); }, hold },
			{ say: "click Import: importing", act: async u => { u.click('#syxpop [data-syxtab="kit"]'); u.click('#syxpop [data-syxgo="start"]');
				await until(() => parseFloat($1("#syxpop .syxbar i")?.style.width || "0") > 30, 300000); shot("import-4-mm-importing"); },
				screen: () => ok(!!$1("#syxpop .syxbar") || !!$1("#syxpop .syxsum"), "not importing"), within: 305000, hold: 1500 },
			{ say: "the report", act: async () => { await until(() => !!$1("#syxpop .syxsum"), 900000); await sleep(500); shot("import-5-mm-report"); },
				screen: () => ok(/imported/.test($1("#syxpop .syxsum")?.textContent || ""), "no report"), within: 905000, hold },
			{ say: "click Done", act: u => { u.click('#syxpop .syxfoot [data-syxgo="close"]'); shot("done"); }, screen: () => ok($1("#syxpop").hidden, "still open") },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden, "open") }
		] };
	/* B-051, F3: GLOBAL › MIDI. CHANNEL SPAN 0 (as an imported 2008 backup has it): T3 has no channel, its mute is refused
	   and its sound value still reaches the machine (a kit dump); span back: the mute lands again. */
	const gSpan = () => I().doc("global", machine().global?.current ?? 0)?.channels?.span;
	const spanTo = async (u, n) => { for (let i = 0; i < 17 && S().glob && S().glob.span !== n; i++) { u.click(`#globpop [data-ga="span"][data-d="${S().glob.span > n ? -1 : 1}"]`); await sleep(120); } };
	const globalChannels = {
		name: "mm-global-channels",
		steps: [
			{ say: "click GLOBAL in the top bar: the panel opens with the MIDI channels, the key is lit", act: u => u.click("#globkey"),
				screen: () => ok(!$1("#globpop").hidden && pressed("#globkey") && !!$1('#globpop [data-ga="span"]') && $all("#globpop .gslots button").length === 8, "panel " + ($1("#globpop").hidden ? "closed" : "open")),
				machine: c => { c.s0 = gSpan(); return ok(c.s0 != null, "no global"); } },
			{ say: "click − beside Channel span down to 0: no track has a channel, the panel says so", act: u => spanTo(u, 0),
				machine: () => ok(gSpan() === 0, "span " + gSpan()), screen: () => ok(($1("#globpop .gwarn")?.textContent || "").startsWith("No track has a MIDI channel of its own"), "warning: " + ($1("#globpop .gwarn")?.textContent || "none")), within: 20000 },
			{ say: "press Escape, click track 3's M on the rail: refused, nothing muted, the notice offers the fix", act: async (u, c) => { u.key("Escape"); await sleep(200); u.click(tab("seq")); await sleep(300); c.m0 = synthMutes(); u.click('#rail .th[data-sel="2"] .ms.m'); },
				machine: c => ok(same(synthMutes(), c.m0), "machine mutes " + synthMutes()), screen: () => ok(dlgShown() && !!dlgButton("Give each track its own channel") && !pressed('#rail .th[data-sel="2"] .ms.m'), "notice " + dlgShown()), within: 8000 },
			{ say: "click Close", act: u => u.click(dlgButton("Close")), screen: () => ok(!dlgShown(), "notice open") },
			go("mix"),
			{ say: "drag track 3's PAN bar: the machine takes it (a kit dump, no channel)", act: async (u, c) => { c.p0 = wk().tracks[2].pages[1][6]; const d = c.p0 > 64 ? -1 : 1; await u.drag(strip(2, ".pc.pan"), [[d * 8, 0], [d * 16, 0], [d * 24, 0]]); },
				machine: c => ok(wk().tracks[2].pages[1][6] !== c.p0, "PAN " + wk().tracks[2].pages[1][6]), within: 15000 },
			{ ...undoKey, act: u => { blur(); u.key("z", { cmd: true }); }, machine: c => ok(wk().tracks[2].pages[1][6] === c.p0, "PAN " + wk().tracks[2].pages[1][6]), within: 15000 },
			{ say: "click track 3's M again, then Give each track its own channel: CHANNEL SPAN 6", act: async u => { u.click(tab("seq")); await sleep(300); u.click('#rail .th[data-sel="2"] .ms.m'); if (await until(dlgShown, 4000)) u.click(dlgButton("Give each track its own channel")); },
				machine: () => ok(gSpan() === 6, "span " + gSpan()), screen: () => ok(!dlgShown(), "notice open"), within: 20000 },
			{ say: "wait for the channels, click track 3's M: it mutes", act: async u => { await sleep(7000); u.click('#rail .th[data-sel="2"] .ms.m'); },
				machine: () => ok(synthMuted(2), "machine mutes " + synthMutes()), within: 8000 },
			{ say: "click M again", act: u => u.click('#rail .th[data-sel="2"] .ms.m'), machine: () => ok(!synthMuted(2), "machine mutes " + synthMutes()) }
		],
		async tidy(u, c) {
			if (c.s0 != null && gSpan() !== c.s0) { if ($1("#globpop").hidden) { u.click("#globkey"); await sleep(300); } await spanTo(u, c.s0); await until(() => gSpan() === c.s0, 15000); }
			if (!$1("#globpop").hidden) u.key("Escape");
			if (synthMuted(2)) u.click('#rail .th[data-sel="2"] .ms.m');
		}
	};
	/* B-051, F3: GLOBAL › Reset to defaults (asked first), and the slot keys */
	const gDoc = () => I().doc("global", machine().global?.current ?? 0);
	const globalReset = {
		name: "mm-global-reset",
		steps: [
			{ say: "open GLOBAL, click Tempo out OFF and Channel span −", act: async (u, c) => { c.slot = machine().global?.current ?? 0; u.click("#globkey"); await sleep(300); u.click('#globpop [data-ga="tempoOut"][data-v="0"]'); await sleep(300); u.click('#globpop [data-ga="span"][data-d="-1"]'); },
				machine: () => ok(gDoc()?.controlOut?.clock === 0 && gDoc()?.channels?.span === 5, "clock out " + gDoc()?.controlOut?.clock + ", span " + gDoc()?.channels?.span), within: 20000 },
			{ say: "click Reset to defaults: it asks", act: u => u.click('#globpop [data-ga="reset"]'), screen: () => ok(dlgShown() && /Reset GLOBAL \d to the factory settings/.test($1("#dlg").textContent), "no question") },
			{ say: "click Reset: the factory global, read back, on the page", act: u => u.click(dlgButton("Reset")),
				machine: () => ok(gDoc()?.controlOut?.clock === 1 && gDoc()?.channels?.span === 6 && gDoc()?.channels?.base === 0, "clock out " + gDoc()?.controlOut?.clock + ", span " + gDoc()?.channels?.span),
				screen: () => ok(pressed('#globpop [data-ga="tempoOut"][data-v="1"]'), "Tempo out not ON"), within: 20000 }
		],
		async tidy(u) { if (dlgShown()) u.click(dlgButton("Cancel")); if (!$1("#globpop").hidden) u.key("Escape"); }
	};
	const globalSlot = {
		name: "mm-global-slot",
		steps: [
			{ say: "open GLOBAL, click slot 2: it is active (lit), the machine says so", act: async (u, c) => { c.s0 = machine().global?.current ?? 0; c.to = c.s0 === 1 ? 2 : 1; u.click("#globkey"); await sleep(300); u.click(`#globpop .gslots [data-v="${c.to}"]`); },
				machine: c => ok(machine().global?.current === c.to && !!gDoc(), "active " + machine().global?.current), screen: c => ok(pressed(`#globpop .gslots [data-v="${c.to}"]`), "slot not lit"), within: 15000 },
			{ say: "click the first slot again", act: (u, c) => u.click(`#globpop .gslots [data-v="${c.s0}"]`), machine: c => ok(machine().global?.current === c.s0, "active " + machine().global?.current), within: 15000 }
		],
		async tidy(u, c) { if (c.s0 != null && machine().global?.current !== c.s0) { if ($1("#globpop").hidden) { u.click("#globkey"); await sleep(300); } u.click(`#globpop .gslots [data-v="${c.s0}"]`); await until(() => machine().global?.current === c.s0, 8000); } if (!$1("#globpop").hidden) u.key("Escape"); }
	};
	/* B-051: the GLOBAL panel as a screenshot (scripts/mdmm-shots.sh with MDMM_SHOTS_JOURNEY=mm-shots-global) */
	const shotsGlobal = { name: "mm-shots-global", needs: () => !/mm-shots/.test(location.search) ? "screenshots only when asked by name" : null,
		steps: [{ say: "open GLOBAL", act: async u => { u.click("#globkey"); await sleep(800); shot("global-mm"); }, screen: () => ok(!$1("#globpop").hidden, "closed"), hold },
			{ say: "Esc", act: u => { u.key("Escape"); shot("done"); } }] };
	/* B-056: the GEN bar's room, R and Defaults inside the bar in Random and in Euclid (the roll's Draw and length keys
	   beside it): a MEASURE line each, with the bar's fit step (76-gen.js genBarFit) and the page's text floor (what 9 px
	   text becomes: more on a page zoomed below 100 %, where WebKit raises 9 to 15 px text, as in --background's small
	   window). mm-gen-bar-fit in every run, mm-shots-sound with its snapshots. */
	const genRoom = () => { const b = $1("#genband").getBoundingClientRect(), k = $1("#genband .gkeys").getBoundingClientRect(); return Math.round(b.right - k.right); };
	const textFloor = () => { const x = document.createElement("span"); x.style.fontSize = "9px"; document.body.appendChild(x); const v = parseFloat(getComputedStyle(x).fontSize); x.remove(); return v.toFixed(2); };
	const genBarFits = (label, snap = false) => ({ say: `the GEN bar on ${label}: Random and Euclid, R and Defaults inside the bar`, act: async (u, c) => {
		c.room = []; c.cut = [];
		const w = e => Math.round(e.getBoundingClientRect().width), parts = q => $1(q) ? [...$1(q).children].map(e => (e.className || e.tagName).split(" ")[0] + "=" + w(e)).join(" ") : "-";
		for (const kind of ["random", "euclid"]) {
			u.click(`[data-genkind="${kind}"]`); await sleep(400);
			const room = genRoom(), fit = [...$1(".genbar").classList].filter(k => /^fit\d$/.test(k)).pop() || "fit0";
			c.room.push(`${kind} ${room} (${fit})`); if (room < -1) c.cut.push(kind);
			Bridge.log(`MEASURE gen-mm ${label} ${kind} room ${room} ${fit} groups cut ${(fl => fl ? fl.scrollWidth - fl.clientWidth : "-")($1("#genband .gflow"))} text floor ${textFloor()} at ${innerWidth} x ${innerHeight} bar [${parts(".genbar")}] groups [${parts("#genband .gflow")}]`);
			if (snap) { await sleep(400); Journey.snapshot(kind === "euclid" ? "gen-mm" : "gen-mm-" + kind); }
		} },
		screen: c => ok(!c.cut.length, "R and Defaults cut off: " + c.room.join(", ")) });
	const genBarFit = { name: "mm-gen-bar-fit",
		steps: [go("seq"), sel(0), genBarFits("track 1"),
			{ say: "click MIDI: a MIDI track", act: u => u.click('[data-side="midi"]'), screen: () => ok(S().side === "midi" && S().sel >= 6, `side ${S().side}, track ${S().sel}`) },
			genBarFits("a MIDI track"),
			{ say: "click SYNTH: back", act: u => u.click('[data-side="int"]'), screen: () => ok(S().side !== "midi" && S().sel < 6, `side ${S().side}`) }] };
	/* 0.5 slice 5: the Sound page and the GEN bar as screenshots, beside the Machinedrum Editor's (md-shots-sound) */
	const shotsSound = { name: "mm-shots-sound", needs: () => !/mm-shots/.test(location.search) ? "screenshots only when asked by name" : null,
		steps: [go("sound"), sel(0),
			{ say: "the Sound page", act: async () => { await sleep(800); shot("sound-mm"); Journey.snapshot("sound-mm"); Bridge.log("MEASURE sound-mm screens " + [...document.querySelectorAll(".snd .sg>.plot")].map(e => Math.round(e.getBoundingClientRect().height)).join(" ") + " page " + Math.round(document.querySelector(".snd").scrollHeight) + "/" + Math.round(document.querySelector("#main").clientHeight)); }, hold },
			go("seq"), genBarFits("track 1", true),
			{ say: "the GEN bar", act: async () => { await sleep(400); shot("gen-mm"); }, hold },
			{ say: "done", act: () => shot("done") }] };
	/* B-054: every view, as macOS 12's WebKit 15 lays it out (Journey.viewJourneys: mm-old-webkit in every run,
	   mm-shots-views for scripts/mdmm-snap.py --safari15) */
	const ws = (name, extra, w = name) => ({ name, open: async u => { u.click(tab(w)); await until(() => S().ws === w, 3000); if (extra) await extra(u); } });
	const esc1 = async u => { u.key("Escape"); await sleep(200); };
	const viewJs = Journey.viewJourneys("mm", [
		ws("seq"), ws("seq-arp", async u => { u.click('[data-dock="arp"]'); }, "seq"), ws("sound", async u => { u.click(tab("seq")); await sleep(200); u.click('[data-dock="locks"]'); u.click(tab("sound")); await sleep(200); u.click(rail(0)); }),
		ws("mix"), ws("perform"), ws("song"),
		{ name: "kits", open: u => u.click("#kitf"), close: esc1 },
		{ name: "patterns", open: u => u.click("#pat"), close: esc1 },
		{ name: "global", open: u => u.click("#globkey"), close: esc1 },
		{ name: "audio", needs: () => { const o = $1('#engsel option[value="audio"]'); return o && !o.hidden && !o.disabled ? null : "no AUDIO / MIDI here (the plug-in in a host)"; }, open: u => { blur(); u.key(","); }, close: esc1 },
		{ name: "syx-import", needs: syxImportJ.needs, open: async u => { u.click("#kitf"); await sleep(300); u.click('#libpop [data-syx="import"]'); await until(() => syxKitIds().length > 0, 8000); },
			close: async u => { $1('#syxpop [data-syxgo="close"]')?.click(); await sleep(200); await esc1(u); } },
		{ name: "keys", open: u => { blur(); u.key("?", { shift: true }); }, close: esc1 },
		{ name: "menu", open: u => Journey.menu.via(u), close: async () => closeDeskMenu() },
		{ name: "step-menu", open: async u => { u.click(tab("seq")); await sleep(300); const t = [0, 1, 2, 3, 4, 5].find(x => trigsOf(x).length) ?? 0; const p = rollCell(t, trigsOf(t)[0] ?? 0); u.rightClick(p.c, {}, p.fx, p.fy); },
			close: async () => { closeDeskMenu(); if (S().stepSel) clearSel(); } },
		{ name: "rom-card", open: u => u.pick("engsel", "rom"), close: async u => { u.click('#bootcard [data-bootrom="close"]'); await sleep(300); } }
	]);
	/* I-008: the editor's menu (shared/deskJourney.js editorMenuJourney), as the Machinedrum's */
	const editorMenuJ = Journey.editorMenuJourney("mm-top-editor-menu", "Monomachine Editor");
	const all = [bootCard, firstBeat, spaceKey, tempoDrag, patNext, wsKeys, helpKeys, plate, undoRedo, gridRecord, slidePaint, lenKey, lockLane, arpDock, arpRange, trnKeys,
		genMut, shapeSound, machinePick, machineStays, songModeJ, songPlayhead, midiSide, controlAll, mixStrip, mixSolo, shiftMutes, routing, panTrim, msOff,
		poly, multiTrig, multiMap, kbPlay, songRows, songPicker, songChain, kitLoad, kitCopy, patGo, dialogEsc,
		audioPanel, romCard, notePlay,
		tapTempo, queue, dialogKeys, trackKeys, muteKeys, lockRamp, pages, copyPaste, clearAll, fill, rotate, pasteMany, liveRec, genKeys, mutScope,
		valueKeys, soundCopy, screenDrag, dragM, midiMutes, joyAssign, menvPort, menvLayout, osHelp, songInspector, songDrag, kitSaveAs, kitRename, kitClear, patClear, hwNoMachine,
		blackKeys, rollPaint, rollDrawLength, rollBoxMove, rollMidiLen, selectCopyPaste, stepMenuJ, buttonsCopyPaste, syxImportJ, syxImportMute, osCopyPaste, rotateUndo, faderUndo, tapTempoShift, shotsImport, editorMenuJ, dropSyxJ, globalChannels, globalReset, globalSlot, shotsGlobal, genBarFit, shotsSound, ...viewJs];

	async function between(u) {
		for (let i = 0; i < 3 && dlgShown(); i++) { u.key("Escape"); await sleep(200); }
		for (const q of ["#libpop", "#globpop", "#keyspop", "#audiopop", "#kpop", "#machpop"]) if ($1(q) && !$1(q).hidden) { u.key("Escape"); await sleep(150); }
		if (!$1("#bootcard").hidden) $1('#bootcard [data-bootrom="close"]')?.click();
		if (S().playing) { u.click("#play"); await until(() => !S().playing, 3000); }
		if (S().mode && S().mode !== "normal" && S().ws === "perform") u.click('[data-pmode="normal"]');
		await sleep(600);
	}
	function ready() {
		return new Promise(done => window.MMPage.whenReady(async () => {
			await until(() => machine().loading && machine().loading.done >= machine().loading.total, 120000);
			await sleep(2000);
			done(true);
		}));
	}
	/* what the page last told the person: its toast and its error line */
	const context = () => [...[["toast", $1("#toast")], ["error", $1("#errline")]].filter(([, e]) => e && !e.hidden && e.textContent.trim()).map(([k, e]) => `${k} "${e.textContent.trim().slice(0, 160)}"`),
		...results.filter(r => r.ok === false).slice(-3).map(r => `refused ${r.op}: ${(r.errors || []).join("; ")}`)].join(", ");
	if (/[?&]selftest=journey/.test(location.search)) addEventListener("load", () => Journey.run(all, { log: t => Bridge.log(t), ready, between, context }));
	return { all };
})();
