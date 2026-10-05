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
	{ const send0 = Bridge.send; Bridge.send = (m, o) => { sent.push(m.op); if (sent.length > 50) sent.shift(); return send0(m, o); }; }
	Bridge.onMessage(m => {
		if (m.type === "telemetry") { tele.last = m; if (m.playing) { tele.steps.push(m.step); if (tele.steps.length > 64) tele.steps.shift(); } }
		if (m.type === "result") { results.push(m); if (results.length > 50) results.shift(); }
	});
	const PAGES = ["SYN", "AMP", "FLT", "EFX", "LF1", "LF2", "LF3"];
	const go = ws => ({ say: `click the ${{ seq: "Sequence", sound: "Sound", mix: "Mix", perform: "Perform", song: "Song" }[ws]} tab`, act: u => u.click(tab(ws)), screen: () => onTab(ws) });
	const sel = t => ({ say: `pick track ${t + 1} on the rail`, act: (u, c) => { c.t = t; if (S().side === "midi" && t < 6) u.click('[data-side="int"]'); u.click(rail(t)); }, screen: () => ok(S().sel === t, "selected " + S().sel) });
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
	const freeSteps = (t, n, from = 0) => { const busy = trigsOf(t), out = []; for (let s = from; s < S().len && out.length < n; s++) if (!busy.includes(s) && !S().tracks[t].steps[s] && !out.some(x => Math.abs(x - s) < 2)) out.push(s); return out; };

	/* ---------- start-up ---------- */
	const bootCard = {
		name: "mm-boot-card", boot: true,
		steps: [
			{ say: "open the editor: the start-up card covers the window while the firmware starts", within: 30000, screen: () => ok(typeof Boot !== "undefined" && Boot.state() === "booting" && !$1("#bootcard").hidden, "card " + (typeof Boot !== "undefined" ? Boot.state() : "?")) },
			{ say: "click PLAY under the card: it is not pressed", act: (u, c) => { const r = $1("#play").getBoundingClientRect(), top = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); c.cover = top?.closest("#bootcard") ? "card" : top?.id === "modalbg" ? "backdrop" : (top?.id || top?.className); if (c.cover !== "card" && c.cover !== "backdrop") throw new Error("PLAY is reachable: " + c.cover); top.dispatchEvent(new MouseEvent("click", { bubbles: true })); },
				machine: () => ok(!tele.last?.playing, "the machine plays"), within: 800 },
			{ say: "the card shows the firmware's own LCD", screen: () => { const g = $1("#bootlcd")?.getContext("2d")?.getImageData(0, 0, 128, 64).data; if (!g) return "no LCD"; let n = 0; for (let i = 0; i < g.length; i += 4) if (g[i] !== g[0] || g[i + 1] !== g[1] || g[i + 2] !== g[2]) n++; return ok(n > 50, n + " pixels"); }, within: 15000 },
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
				c.steps = [0, 4, 8, 12].map(k => freeSteps(c.t, 1, k)[0]).filter(s => s != null);
				if (c.steps.length < 4) throw new Error("no four free steps");
				for (const s of c.steps) { const p = rollCell(c.t, s); c.n = p.n; u.click(p.c, {}, p.fx, p.fy); await sleep(250); }
				c.note = `track ${c.t + 1} steps ${c.steps.map(s => s + 1).join(" ")}, note ${c.n}`;
			}, screen: c => ok(c.steps.every(s => S().tracks[c.t].steps[s]?.n?.includes(c.n)), "roll shows " + c.steps.map(s => JSON.stringify(S().tracks[c.t].steps[s]?.n || null)).join(",")),
				machine: c => ok(c.steps.every(s => trigsOf(c.t).includes(s)), "pattern trigs " + trigsOf(c.t).join(",")), within: 15000 },
			{ say: "press PLAY", act: u => { tele.steps = []; u.click("#play"); }, screen: () => ok(/^\d\d\.\d\d$/.test($1("#pos").textContent), "POSITION " + $1("#pos").textContent), machine: () => ok(new Set(tele.steps).size >= 4, "telemetry steps " + tele.steps.join(",")), within: 8000 },
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
	const tempoDrag = {
		name: "mm-top-tempo-drag",
		steps: [
			{ say: "drag the BPM on the LCD up", act: async (u, c) => { c.b0 = machine().tempo; await u.drag("#bpm", [[0, -6], [0, -12], [0, -18], [0, -24]]); c.note = `${c.b0} -> ${S().bpm}`; }, screen: c => ok(parseFloat($1("#bpm").textContent) === S().bpm && S().bpm > c.b0, "LCD " + $1("#bpm").textContent), machine: c => ok(machine().tempo > c.b0, "tempo " + machine().tempo) },
			{ say: "drag it back down as far", act: u => u.drag("#bpm", [[0, 6], [0, 12], [0, 18], [0, 24]]), screen: c => ok(parseFloat($1("#bpm").textContent) === c.b0, "LCD " + $1("#bpm").textContent), machine: c => ok(machine().tempo === c.b0, "tempo " + machine().tempo) }
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
			{ say: "press ?: the list of keys", act: u => { blur(); u.key("?", { shift: true }); }, screen: () => ok(!$1("#keyspop").hidden && $all("#keyspop .krow").length > 20, $all("#keyspop .krow").length + " keys") },
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
			{ say: "click RECORD (stopped): GRID RECORDING", act: u => u.click("#rec"), machine: () => ok(tele.last?.record === "grid", "record " + tele.last?.record), within: 6000 },
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

	const all = [bootCard, firstBeat, spaceKey, tempoDrag, patNext, wsKeys, helpKeys, plate, undoRedo, gridRecord, slidePaint, lenKey, lockLane, arpDock, trnKeys,
		genMut, shapeSound, machinePick, midiSide, controlAll, mixStrip, mixSolo, shiftMutes, routing, panTrim, msOff,
		poly, multiTrig, multiMap, kbPlay, songRows, songPicker, songChain, kitLoad, kitCopy, patGo, dialogEsc,
		audioPanel, romCard, notePlay, hwNoMachine];

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
