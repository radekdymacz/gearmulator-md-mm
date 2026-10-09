"use strict";
/* The Machinedrum Editor's user journeys (diagnostics builds only; skins/shared/deskJourney.js says what a
   journey is). Each one is what a person does through the page's own controls, each step checked twice: on the
   screen, and in the plug-in's documents (what the firmware read back). ?selftest=journey runs them all,
   journey-<names> some (journey-md-seq-*). Names: md-<area>-<what>, the areas: boot, top, keys, seq, gen, sound,
   mix, song, sampler, lib, global, audio, engine, dialog (doc/modern-ux/JOURNEYS.md lists them against the features). */
const MdJourneys = (() => {
	const { ok, sleep, until } = Journey;
	const $1 = q => document.querySelector(q), $all = q => [...document.querySelectorAll(q)];
	const desk = () => machineState().desk || {};
	const pat = () => Docs.patterns[currentPatternSlot()];
	const trigsOf = t => (pat()?.tracks[t].trigs || []).slice().sort((a, b) => a - b);
	const flagsOf = (t, f) => (pat()?.tracks[t][f] || []).slice().sort((a, b) => a - b);
	const locksOf = (t, i) => (pat()?.locks || []).find(l => l.track === t && l.param === i)?.steps || [];
	const kit = () => kitDocOf(Docs);
	const kitVals = t => { const k = kit()?.tracks[t]; return k ? [...k.synth, ...k.effects, ...k.routing] : []; };
	const allKitVals = () => [...Array(16).keys()].map(kitVals);
	const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
	/* what differs between two allKitVals(): "T<track> #<index> <was>-><now>" (the first few) */
	const kitDiff = (a, b) => a.flatMap((v, t) => v.map((x, i) => x === b[t]?.[i] ? null : `T${t + 1} #${i} ${x}->${b[t]?.[i]}`).filter(Boolean)).slice(0, 8).join(", ");
	const sorted = a => a.slice().sort((x, y) => x - y);
	const idle = () => !desk().tx;
	const dlgShown = () => !$1("#dlg").hidden && $1("#dlg").dataset.first !== "1";
	const dlgButton = text => [...$1("#dlg").querySelectorAll("[data-dlg]")].find(b => b.textContent.trim() === text);
	const pressed = q => $1(q)?.getAttribute("aria-pressed") === "true";
	const cell = (t, s) => `#seq .st[data-t="${t}"][data-s="${s}"]`;
	const rail = t => `#rail .th[data-sel="${t}"] .nm`;
	const tab = ws => `#tabs [data-ws="${ws}"]`;
	const onTab = ws => ok(S.ws === ws, `workspace ${S.ws}`);
	const lit = t => V.tracks[t].trigs.map((on, s) => on ? s : -1).filter(s => s >= 0 && s < V.len);
	const gridShows = t => lit(t).every(s => pressed(cell(t, s))) && $all(`#seq .st[data-t="${t}"][aria-pressed="true"]`).length === lit(t).length;
	const mutes = () => sorted(desk().mutes || []);
	/* a track the person would pick: a sound on it (not empty, not MIDI, CTR or input, not a recorder) */
	const soundTracks = () => [...Array(16).keys()].filter(i => !/^(GND-EMPTY|MID-|CTR-|INP-)/.test(V.tracks[i].m) && !machineFacts(V.tracks[i].m, Cat).recorder);
	const soundTrack = () => soundTracks()[0] ?? 0;
	/* free steps of a track from a step on, spaced like a person picks them */
	const freeSteps = (t, n, from = 0) => { const busy = trigsOf(t), out = []; for (let s = from; s < V.len && out.length < n; s++) if (!busy.includes(s) && !out.some(x => Math.abs(x - s) < 2)) out.push(s); return out; };
	/* the machine's telemetry and the plug-in's command results, as they come */
	const tele = { last: null, steps: [], rows: [] }, results = [];
	/* the documents of one slot as they arrive (a failing step says what came) */
	const trace = { kind: null, slot: null, seen: [] };
	const traceDocs = (kind, slot) => Object.assign(trace, { kind, slot, seen: [] });
	const traced = () => trace.seen.join(" | ") || "no document arrived";
	Bridge.onMessage(m => {
		if (m.type === "doc" && m.kind === trace.kind && m.slot === trace.slot && trace.seen.length < 12) trace.seen.push(`${Math.round(performance.now())} ms${m.pending ? " pending" : ""}: ${trace.kind === "kit" ? JSON.stringify(m.doc?.name) : (m.doc?.tracks || []).reduce((n, t) => n + t.trigs.length, 0) + " trigs"}`);
		if (m.type === "telemetry") { const was = tele.last; tele.last = m; if (m.playing) { tele.steps.push(m.step); if (tele.steps.length > 64) tele.steps.shift(); }
			/* the song row as it came, with the transport and the step, when it changed (the song playhead's failures say it) */
			if (!was || was.songRow !== m.songRow || was.playing !== m.playing) { tele.rows.push(`${m.playing ? "P" : "S"}${m.step}:${m.songRow}`); if (tele.rows.length > 24) tele.rows.shift(); } }
		if (m.type === "result") { results.push(m); if (results.length > 50) results.shift(); }
		if (m.type === "audition") results.push({ heard: "audition", state: m.state });
	});
	/* common steps */
	const sel = t => ({ say: "pick a track on the rail", act: (u, c) => { c.t = t(c); u.click(rail(c.t)); }, screen: c => ok(S.sel === c.t && $1("#rail .th.sel")?.dataset.sel === String(c.t), "selected " + S.sel) });
	const go = ws => ({ say: `click the ${{ seq: "Sequence", sound: "Sound", mix: "Mix", song: "Song", sampler: "Sampler" }[ws]} tab`, act: u => u.click(tab(ws)), screen: () => onTab(ws), machine: () => ok(V.loaded, "not loaded") });
	const undoKey = { say: "press Cmd+Z", act: u => u.key("z", { cmd: true }) };
	const esc = { say: "press Escape: it closes", act: u => u.key("Escape"), screen: () => ok($1("#libpop").hidden && $1("#globpop").hidden, "still open") };
	const nudge = async (u, q, v, by = 30) => { const d = v > 64 ? -1 : 1, n = Math.max(1, Math.round(by / 6)); await u.drag(q, Array.from({ length: n }, (_, k) => [d * 6 * (k + 1), 0])); };
	const confirmIfAsked = async u => { if (await until(dlgShown, 1200)) u.click($1("#dlg .danger") || dlgButton("Cancel")); };

	/* ---------- start-up ---------- */
	const bootCard = {
		name: "md-boot-card", boot: true,
		steps: [
			{ say: "open the editor: the start-up card covers the window while the firmware starts (preparing the factory flash on a fresh data root, then booting)", within: 30000, screen: () => ok(["loading", "booting"].includes(Boot.state()) && !$1("#bootcard").hidden, "card " + Boot.state()) },
			{ say: "click PLAY under the card: it is not pressed", act: (u, c) => { const r = $1("#play").getBoundingClientRect(), top = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); c.cover = top?.closest("#bootcard") ? "card" : top?.id === "modalbg" ? "backdrop" : (top?.id || top?.className); if (c.cover !== "card" && c.cover !== "backdrop") throw new Error("PLAY is reachable: " + c.cover); top.dispatchEvent(new MouseEvent("click", { bubbles: true })); c.note = "covered by the " + c.cover; },
				screen: () => ok(!V.playing, "playing"), machine: () => ok(!tele.last?.playing, "the machine plays"), within: 800 },
			{ say: "the card shows the firmware's own LCD", screen: () => { const g = $1("#bootlcd")?.getContext("2d")?.getImageData(0, 0, 128, 64).data; if (!g) return "no LCD"; let n = 0; for (let i = 0; i < g.length; i += 4) if (g[i] !== g[0] || g[i + 1] !== g[1] || g[i + 2] !== g[2]) n++; return ok(n > 50, n + " pixels, card " + Boot.state()); }, within: 90000 },
			{ say: "the card goes once the machine takes input", screen: () => ok($1("#bootcard").hidden, "still shown"), machine: () => ok(!!machineState().input, "no input yet"), within: 90000 }
		]
	};

	/* ---------- the top bar and the keys ---------- */
	const firstBeat = {
		name: "md-seq-first-beat",
		steps: [
			go("seq"), sel(() => soundTrack()),
			{ say: "click four empty steps on that track", act: async (u, c) => { c.steps = [2, 6, 10, 14].map(k => freeSteps(c.t, 1, k)[0]).filter(s => s != null); if (c.steps.length < 4) throw new Error("no four free steps"); for (const s of c.steps) { u.click(cell(c.t, s)); await sleep(120); } c.note = `track ${c.t + 1} steps ${c.steps.map(s => s + 1).join(" ")}`; },
				screen: c => ok(c.steps.every(s => pressed(cell(c.t, s))), "not lit"), machine: c => ok(c.steps.every(s => trigsOf(c.t).includes(s)) && idle(), `pattern has ${trigsOf(c.t).join(",")}`) },
			{ say: "press PLAY", act: u => { tele.steps = []; u.click("#play"); }, screen: () => ok(V.playing && /^\d\d\.\d\d$/.test($1("#pos").textContent), `POSITION ${$1("#pos").textContent}`), machine: () => ok(new Set(tele.steps).size >= 4, `telemetry steps ${tele.steps.join(",")}`), within: 8000 },
			{ say: "it plays the pattern with those trigs", screen: c => ok(c.steps.every(s => V.tracks[c.t].trigs[s]), "grid lost a trig"), machine: c => ok(tele.last?.playing && c.steps.every(s => trigsOf(c.t).includes(s)), "not playing the trigs") },
			{ say: "press STOP (the same key)", act: u => u.click("#play"), screen: () => ok(!V.playing && $1("#pos").textContent === "--.--", `POSITION ${$1("#pos").textContent}`), machine: () => ok(tele.last && !tele.last.playing, "still playing") }
		],
		async tidy(u, c) { if (V.playing) u.click("#play"); for (let i = 0; i < 6 && c.steps && c.steps.some(s => trigsOf(c.t).includes(s)); i++) { u.key("z", { cmd: true }); await until(idle, 2000); await sleep(400); } }
	};
	const spaceTransport = {
		name: "md-keys-space-play",
		steps: [
			{ say: "press Space: it plays", act: u => { document.activeElement?.blur?.(); tele.steps = []; u.key(" "); }, screen: () => ok(V.playing, "not playing"), machine: () => ok(new Set(tele.steps).size >= 2, "telemetry " + tele.steps.join(",")), within: 6000 },
			{ say: "press Space again: it stops", act: u => u.key(" "), screen: () => ok(!V.playing, "playing"), machine: () => ok(tele.last && !tele.last.playing, "still playing") }
		]
	};
	/* B-024: the machine keeps the tempo in 1/24 BPM steps (a saved project's 93.79 is 2251/24) and the LCD shows it to
	   one decimal: the LCD is compared with the tempo as the LCD rounds it, never with the tempo itself (only a tempo on
	   the 0.1 grid, a fresh 120.0, passed that) */
	const lcdBpm = v => +(+v).toFixed(1);
	const tempoDrag = {
		name: "md-top-tempo-drag",
		steps: [
			{ say: "drag the BPM on the LCD up", act: async (u, c) => { c.b0 = Docs.global.tempo; await u.drag("#bpm", [[0, -6], [0, -12], [0, -18], [0, -24]]); c.note = `${c.b0} -> ${V.bpm}`; },
				screen: c => ok(parseFloat($1("#bpm").textContent) === lcdBpm(V.bpm) && V.bpm > c.b0, `LCD ${$1("#bpm").textContent} for ${V.bpm} (from ${c.b0}); tempo in ${Docs.global?.control?.tempoIn}`),
				machine: c => ok(Docs.global.tempo > c.b0 && Docs.global.tempo === V.bpm, "tempo " + Docs.global.tempo) },
			{ say: "drag it back down as far", act: u => u.drag("#bpm", [[0, 6], [0, 12], [0, 18], [0, 24]]), screen: c => ok(parseFloat($1("#bpm").textContent) === lcdBpm(c.b0), "LCD " + $1("#bpm").textContent),
				machine: c => ok(Math.abs(Docs.global.tempo - c.b0) < 0.05, "tempo " + Docs.global.tempo) }
		]
	};
	/* T taps; so does B, the Monomachine Editor's tap key (T is a black key there) */
	const tapWith = (name, key) => ({
		name,
		steps: [
			{ say: `tap ${key.toUpperCase()} five times, about 0.5 s apart`, act: async (u, c) => { c.b0 = Docs.global.tempo; document.activeElement?.blur?.(); const at = []; for (let i = 0; i < 5; i++) { at.push(performance.now()); u.key(key); await sleep(500); } c.want = Math.round(60000 / ((at[4] - at[0]) / 4) * 10) / 10; c.note = `taps ${Math.round((at[4] - at[0]) / 4)} ms apart: ${c.want} BPM`; },
				screen: c => ok(Math.abs(V.bpm - c.want) <= 1.5 && Math.abs(parseFloat($1("#bpm").textContent) - V.bpm) < 0.06, "BPM " + V.bpm + ", want " + c.want), machine: c => ok(Math.abs(Docs.global.tempo - c.want) <= 1.5, "tempo " + Docs.global.tempo + ", want " + c.want) }
		],
		async tidy(u, c) { if (c.b0 != null) cmd("tempo", { bpm: c.b0 }); await sleep(500); }
	});
	const tapTempo = tapWith("md-keys-tap-tempo", "t"), tapTempoB = tapWith("md-keys-tap-tempo-b", "b");
	const patStep = {
		name: "md-top-pattern-next",
		steps: [
			{ say: "click › next to the pattern on the LCD", act: async (u, c) => { c.p0 = currentPatternSlot(); u.click("#patNext"); await confirmIfAsked(u); }, screen: c => ok($1("#pat").textContent.includes(patName(c.p0 + 1)), "LCD " + $1("#pat").textContent), machine: c => ok(currentPatternSlot() === c.p0 + 1, "machine pattern " + currentPatternSlot()), within: 5000 },
			{ say: "click ‹: back", act: async u => { u.click("#patPrev"); await confirmIfAsked(u); }, screen: c => ok($1("#pat").textContent.includes(patName(c.p0)), "LCD " + $1("#pat").textContent), machine: c => ok(currentPatternSlot() === c.p0, "machine pattern " + currentPatternSlot()), within: 5000 }
		]
	};
	const queuePattern = {
		name: "md-seq-queue-while-playing",
		steps: [
			{ say: "press PLAY", act: (u, c) => { c.p0 = currentPatternSlot(); u.click("#play"); }, machine: () => ok(tele.last?.playing, "not playing"), within: 5000 },
			{ say: "click › on the LCD: the next pattern is queued", act: async u => { u.click("#patNext"); await confirmIfAsked(u); }, machine: c => ok(desk().queued === c.p0 + 1 || currentPatternSlot() === c.p0 + 1, "queued " + desk().queued), screen: c => ok($1("#pat").textContent.includes(patName(c.p0 + 1)), "LCD " + $1("#pat").textContent), within: 3000 },
			{ say: "it starts at the pattern end", machine: c => ok(currentPatternSlot() === c.p0 + 1 && desk().queued == null, "machine pattern " + currentPatternSlot()), within: 20000 },
			{ say: "press STOP and click ‹", act: async u => { u.click("#play"); await until(() => !V.playing, 3000); u.click("#patPrev"); await confirmIfAsked(u); }, machine: c => ok(currentPatternSlot() === c.p0 && !tele.last?.playing, "machine pattern " + currentPatternSlot()), within: 5000 }
		],
		async tidy(u) { if (V.playing) u.click("#play"); }
	};
	const plate = {
		name: "md-top-plate",
		steps: [
			{ say: "click the plate key (MK1 / MK2)", act: (u, c) => { c.p0 = document.documentElement.dataset.plate; c.bg = getComputedStyle(document.body).backgroundColor; u.click("#platekey"); }, screen: c => ok(document.documentElement.dataset.plate !== c.p0, "plate " + document.documentElement.dataset.plate) },
			{ say: "click it again", act: u => u.click("#platekey"), screen: c => ok(document.documentElement.dataset.plate === c.p0, "plate " + document.documentElement.dataset.plate) }
		]
	};
	const wsKeys = {
		name: "md-keys-workspaces",
		steps: [["2", "sound"], ["3", "mix"], ["4", "sampler"], ["5", "song"], ["1", "seq"]].map(([k, ws]) => ({ say: `press ${k}`, act: u => { document.activeElement?.blur?.(); u.key(k); }, screen: () => ok(S.ws === ws && !!$1("#main").firstElementChild, "workspace " + S.ws) }))
	};
	/* B-018: ? as the operating system delivers it after the window became the key window (JUCE then takes the keyboard
	   for its own view; the page's host hands it back), no click in the page first: the key reaches the page, no beep */
	const osHelp = {
		name: "md-keys-os-help", needs: Journey.osKeyPath,
		steps: [
			{ say: "the window becomes the key window, then press ? on the keyboard, no click in the page: the keyboard view",
				act: async u => { document.activeElement?.blur?.(); await u.osKey("activate"); await u.sleep(300); await u.osKey("?"); },
				screen: () => ok(!$1("#keyspop").hidden && !!$1("#keyspop .kv-cap"), "keys view " + ($1("#keyspop").hidden ? "hidden" : "empty")) },
			{ say: "press Escape on the keyboard: it closes", act: u => u.osKey("escape"), screen: () => ok($1("#keyspop").hidden, "still open") }
		]
	};
	const helpKeys = {
		name: "md-keys-help",
		steps: [
			/* K-view (DESIGN-keymap.md): ? is a drawn keyboard from the key map, with the mouse's tricks, tips and the list */
			{ say: "press ? on Sequence: the keyboard view", act: u => { if (S.ws !== "seq") u.click(tab("seq")); document.activeElement?.blur?.(); u.key("?", { shift: true }); },
				screen: () => ok(!$1("#keyspop").hidden && $all("#keyspop .kv-cap").length > 60 && $1('#keyspop .kv-cap[data-code="KeyR"]')?.dataset.ids === "randomise-track"
					&& $1('#keyspop .kv-cap.piano.white[data-code="KeyA"] .kv-note')?.textContent === "C" && $all("#keyspop .kv-mouse .kv-row").length > 5 && $all("#keyspop .keyrow").length > 20,
					`${$all("#keyspop .kv-cap").length} keys, R ${$1('#keyspop .kv-cap[data-code="KeyR"]')?.dataset.ids}, ${$all("#keyspop .keyrow").length} rows`) },
			{ say: "click the ⌥ / FN layer: R randomises every track there", act: u => u.click('#keyspop .kv-chip[data-kvmod="alt"]'),
				screen: () => ok($1('#keyspop .kv-cap[data-code="KeyR"]')?.dataset.ids === "randomise-all" && $1('#keyspop .kv-cap[data-code="AltLeft"]')?.classList.contains("on"), "R " + $1('#keyspop .kv-cap[data-code="KeyR"]')?.dataset.ids) },
			{ say: "click R on the drawn keyboard: what it does, in words", act: u => u.click('#keyspop .kv-cap[data-code="KeyR"]'), screen: () => ok(/every track/i.test($1("#keyspop .kv-detail")?.textContent || ""), $1("#keyspop .kv-detail")?.textContent) },
			{ say: "type \"undo\" in its search: ⌘Z is marked, the list keeps undo", act: u => { u.click('#keyspop .kv-chip[data-kvmod="cmd"]'); u.click('#keyspop .kv-chip[data-kvmod="alt"]'); u.type("#keyspop [data-kvsearch]", "undo"); },
				screen: () => ok($1('#keyspop .kv-cap.hit[data-code="KeyZ"]') && $all("#keyspop .keyrow").length >= 1 && $all("#keyspop .keyrow").every(r => /undo/i.test(r.textContent + r.dataset.id)), $all("#keyspop .keyrow").length + " rows") },
			{ say: "press Escape: it closes", act: u => { document.activeElement?.blur?.(); u.key("Escape"); }, screen: () => ok($1("#keyspop").hidden, "still open") }
		]
	};
	const undoRedo = {
		name: "md-top-undo-redo",
		steps: [
			go("seq"), sel(() => soundTrack()),
			{ say: "click an empty step", act: (u, c) => { c.s = freeSteps(c.t, 1, 5)[0]; u.click(cell(c.t, c.s)); }, screen: c => ok(pressed(cell(c.t, c.s)), "not lit"), machine: c => ok(trigsOf(c.t).includes(c.s), "no trig") },
			{ say: "press Cmd+Z", act: u => u.key("z", { cmd: true }), screen: c => ok(!pressed(cell(c.t, c.s)), "still lit"), machine: c => ok(!trigsOf(c.t).includes(c.s), "trig still there") },
			{ say: "press Cmd+Shift+Z", act: u => u.key("z", { cmd: true, shift: true }), screen: c => ok(pressed(cell(c.t, c.s)), "not lit"), machine: c => ok(trigsOf(c.t).includes(c.s), "no trig") },
			{ say: "click the Undo key", act: u => u.click("#undo"), screen: c => ok(!pressed(cell(c.t, c.s)) && !$1("#redo").disabled, "still lit"), machine: c => ok(!trigsOf(c.t).includes(c.s), "trig still there") },
			{ say: "click the Redo key", act: u => u.click("#redo"), screen: c => ok(pressed(cell(c.t, c.s)), "not lit"), machine: c => ok(trigsOf(c.t).includes(c.s), "no trig") },
			{ say: "click Undo", act: u => u.click("#undo"), machine: c => ok(!trigsOf(c.t).includes(c.s), "trig still there") }
		]
	};

	/* ---------- Sequence ---------- */
	const paintUndo = {
		name: "md-seq-paint-undo",
		steps: [
			go("seq"), sel(() => soundTracks().find(t => { const b = trigsOf(t); return [...Array(V.len - 4).keys()].some(s => [0, 1, 2, 3].every(k => !b.includes(s + k))); }) ?? soundTrack()),
			{ say: "drag across four empty steps", act: async (u, c) => {
				const busy = trigsOf(c.t); c.s0 = [...Array(V.len - 4).keys()].find(s => [0, 1, 2, 3].every(k => !busy.includes(s + k))); if (c.s0 == null) throw new Error("no four free steps");
				c.run = [0, 1, 2, 3].map(k => c.s0 + k); c.u0 = V.undoCount;
				await u.drag(cell(c.t, c.s0), c.run.slice(1).map(s => cell(c.t, s)));
			}, screen: c => ok(c.run.every(s => pressed(cell(c.t, s))), "not lit"), machine: c => ok(c.run.every(s => trigsOf(c.t).includes(s)) && V.undoCount === c.u0 + 1, `pattern ${trigsOf(c.t).join(",")}, undo steps ${c.u0} -> ${V.undoCount}`) },
			{ ...undoKey, say: "press Cmd+Z once: all four go", screen: c => ok(c.run.every(s => !pressed(cell(c.t, s))), "still lit"), machine: c => ok(c.run.every(s => !trigsOf(c.t).includes(s)), "pattern " + trigsOf(c.t).join(",")) }
		]
	};
	const accentSlide = {
		name: "md-seq-accent-slide",
		steps: [
			go("seq"), sel(() => soundTrack()),
			{ say: "click an empty step", act: (u, c) => { c.s = freeSteps(c.t, 1, 9)[0]; u.click(cell(c.t, c.s)); }, machine: c => ok(trigsOf(c.t).includes(c.s), "no trig") },
			{ say: "shift-click it: accent", act: (u, c) => u.click(cell(c.t, c.s), { shift: true }), screen: c => ok($1(cell(c.t, c.s)).classList.contains("acc"), "no accent shown"), machine: c => ok(flagsOf(c.t, "accent").includes(c.s) || (V.accAll && (pat().accent?.steps || []).includes(c.s)), `accent ${flagsOf(c.t, "accent").join(",")}, pattern-wide ${V.accAll ? (pat().accent?.steps || []).join(",") : "off"}`) },
			{ say: "alt-shift-click it: slide", act: (u, c) => u.click(cell(c.t, c.s), { alt: true, shift: true }), screen: c => ok($1(cell(c.t, c.s)).classList.contains("sl"), "no slide shown"), machine: c => ok(flagsOf(c.t, "slide").includes(c.s) || (V.slideAll && (pat().slide?.steps || []).includes(c.s)), `slide ${flagsOf(c.t, "slide").join(",")}, pattern-wide ${V.slideAll ? (pat().slide?.steps || []).join(",") : "off"}`) },
			{ say: "Cmd+Z three times: back to an empty step", act: async u => { for (let i = 0; i < 3; i++) { u.key("z", { cmd: true }); await sleep(500); } }, machine: c => ok(!trigsOf(c.t).includes(c.s), "trig still there"), screen: c => ok(!pressed(cell(c.t, c.s)), "still lit") }
		]
	};
	const lockLane = {
		name: "md-seq-lock-lane",
		steps: [
			go("seq"),
			{ say: "pick a track that has trigs", act: (u, c) => { c.t = soundTracks().find(t => trigsOf(t).length >= 2) ?? soundTrack(); u.click(rail(c.t)); }, screen: c => ok(S.sel === c.t, "selected " + S.sel), machine: c => ok(trigsOf(c.t).length >= 2, "fewer than two trigs") },
			{ say: "click a lock parameter key", act: (u, c) => { const k = $1('#chips .pk[data-lane="FLTF"]') || $1("#chips .pk[data-lane]"); c.p = k.dataset.lane; c.i = pidx(c.t, c.p); c.l0 = locksOf(c.t, c.i); u.click(k); }, screen: c => ok(S.lane === c.p && pressed(`#chips .pk[data-lane="${c.p}"]`), "lane " + S.lane) },
			{ say: "draw across the lane over the track's trigs", act: async (u, c) => { c.on = trigsOf(c.t).filter(s => s < V.len).slice(0, 4); await u.drag(`#lane .lb[data-s="${c.on[0]}"]`, c.on.slice(1).map(s => `#lane .lb[data-s="${s}"]`), {}, { fy: 0.3 }); c.note = `${c.p} on steps ${c.on.map(s => s + 1).join(" ")}`; },
				screen: c => ok(c.on.every(s => $1(`#lane .lb[data-s="${s}"] i`)), "no bars"), machine: c => ok(c.on.every(s => locksOf(c.t, c.i).some(([x]) => x === s)), "locks " + JSON.stringify(locksOf(c.t, c.i))), within: 8000 },
			{ say: "click the lane's clear key", act: u => u.click("#clearLane"), screen: c => ok(c.on.every(s => !$1(`#lane .lb[data-s="${s}"] i`)), "bars left"), machine: c => ok(!locksOf(c.t, c.i).length, "locks " + JSON.stringify(locksOf(c.t, c.i))), within: 8000 }
		],
		async tidy(u, c) { if (c.l0?.length) u.key("z", { cmd: true }); }
	};
	const pagesJ = {
		name: "md-seq-pages",
		steps: [
			go("seq"),
			{ say: "click ALL off: one page of 16 steps", act: u => { if (S.viewAll) u.click("#pgall"); }, screen: () => ok(!S.viewAll && $all('#seq .st[data-t="0"]').length === 16 && $1('#seq .st[data-t="0"]').dataset.s === String(S.page * 16), $all('#seq .st[data-t="0"]').length + " steps") },
			{ say: "click PAGE: the next page", act: (u, c) => { c.p0 = S.page; u.click("#pgkey"); }, screen: c => ok(V.len < 32 || (S.page === (c.p0 + 1) % (V.len / 16) && $1('#seq .st[data-t="0"]').dataset.s === String(S.page * 16)), "page " + S.page) },
			{ say: "press [: back", act: u => { document.activeElement?.blur?.(); u.key("["); }, screen: c => ok(S.page === c.p0, "page " + S.page) },
			{ say: "click ALL: every step again", act: u => u.click("#pgall"), screen: () => ok(S.viewAll && $all('#seq .st[data-t="0"]').length === V.len, $all('#seq .st[data-t="0"]').length + " steps") }
		],
		async tidy() { if (!S.viewAll) { S.viewAll = true; render(); } }
	};
	const inLen = s => s < Math.min(V.length, V.len);
	const copyPaste = {
		name: "md-seq-copy-paste-clear",
		steps: [
			go("seq"),
			{ say: "pick a track with trigs and press Cmd+C", act: async (u, c) => { c.a = soundTracks().find(t => trigsOf(t).length) ?? 0; c.b = soundTracks().find(t => t !== c.a && !same(trigsOf(t), trigsOf(c.a))) ?? (c.a + 1) % 16; c.ta = trigsOf(c.a); c.tb = trigsOf(c.b); u.click(rail(c.a)); await sleep(200); document.activeElement?.blur?.(); u.key("c", { cmd: true }); }, machine: () => ok(V.clipboard.steps, "nothing copied") },
			{ say: "pick another track and press Cmd+V", act: async (u, c) => { u.click(rail(c.b)); await sleep(200); document.activeElement?.blur?.(); u.key("v", { cmd: true }); }, screen: c => ok(gridShows(c.b) && same(lit(c.b).filter(inLen), c.ta.filter(inLen)), "grid " + lit(c.b).join(",")),
				/* a paste stops at the pattern's length (DESIGN-step-selection.md §4): the steps past it keep what they had */
				machine: c => ok(same(trigsOf(c.b).filter(inLen), c.ta.filter(inLen)) && same(trigsOf(c.b).filter(s => !inLen(s)), c.tb.filter(s => !inLen(s))), "pattern " + trigsOf(c.b).join(",")) },
			/* D3 (DESIGN-keymap.md): Delete takes only selected steps; the LCD's Clr clears the page shown */
			{ say: "press Delete with no step selected: nothing is cleared", act: (u, c) => { if (S.stepSel) clearSel(); c.lb = lit(c.b); document.activeElement?.blur?.(); u.key("Delete"); },
				screen: c => ok(same(lit(c.b), c.lb), "lit " + lit(c.b).join(",")), machine: c => ok(same(trigsOf(c.b).filter(inLen), c.ta.filter(inLen)), "pattern " + trigsOf(c.b).join(",")) },
			{ say: "click Clr on the LCD: the track's steps are cleared", act: u => u.click('[data-sec="clear"]'), screen: c => ok(!lit(c.b).length, "lit " + lit(c.b).join(",")), machine: c => ok(!trigsOf(c.b).length, "pattern " + trigsOf(c.b).join(",")) },
			{ say: "Cmd+Z twice: the track is as before", act: async u => { u.key("z", { cmd: true }); await sleep(800); u.key("z", { cmd: true }); }, machine: c => ok(same(trigsOf(c.b), c.tb), "pattern " + trigsOf(c.b).join(",")), within: 8000 }
		]
	};
	/* DESIGN-step-selection.md, on ⌘ since K2 (DESIGN-keymap.md): one step copied to another step of its track (the LCD's
	   PASTE acting on the selection), then a block duplicated and cleared with the LCD's CLR */
	const selectCopyPaste = {
		name: "md-seq-select-copy-paste",
		steps: [
			go("seq"), sel(() => soundTracks().find(t => t < 15 && trigsOf(t).some(s => s < Math.min(V.length, V.len))) ?? soundTrack()),
			{ say: "Cmd-click a step with a trig: it is selected", act: (u, c) => {
				c.L = Math.min(V.length, V.len); if (c.L < 8) throw new Error("the pattern is shorter than 8 steps");
				c.t0 = trigsOf(c.t); c.t1 = trigsOf(c.t + 1); c.u0 = V.undoCount;
				c.s = c.t0.find(s => s < c.L) ?? 0; c.d = [...Array(c.L).keys()].find(s => !c.t0.includes(s) && Math.abs(s - c.s) > 1 && (s < 4 || s >= 8));
				if (c.d == null) throw new Error("no free step to paste to");
				u.click(cell(c.t, c.s), { cmd: true });
			}, screen: c => ok($1(cell(c.t, c.s)).classList.contains("selx") && $all("#seq .st.selx").length === 1, $all("#seq .st.selx").length + " selected"),
				machine: c => ok(c.t0.includes(c.s) === trigsOf(c.t).includes(c.s) && !trigsOf(c.t).some(s => !c.t0.includes(s)), "a Cmd-click changed the pattern: " + trigsOf(c.t).join(",")) },
			{ say: "press Cmd+C", act: u => { document.activeElement?.blur?.(); u.key("c", { cmd: true }); }, machine: () => ok(V.clipboard.stepsSize?.length === 1 && V.clipboard.stepsSize?.tracks === 1, "clipboard " + JSON.stringify(V.clipboard.stepsSize)) },
			{ say: "Cmd-click an empty step of the same track", act: (u, c) => u.click(cell(c.t, c.d), { cmd: true }), screen: c => ok($1(cell(c.t, c.d)).classList.contains("selx") && !$1(cell(c.t, c.s)).classList.contains("selx") && $1('[data-sec="paste"]').classList.contains("onsel"), "not selected, or PASTE not marked for it") },
			{ say: "click PASTE on the LCD: the trig lands on the selected step", act: u => u.click('[data-sec="paste"]'), screen: c => ok(pressed(cell(c.t, c.d)), "not lit"), machine: c => ok(trigsOf(c.t).includes(c.d), "pattern " + trigsOf(c.t).join(",")) },
			{ say: "Cmd-drag from step 1 of the track to step 4 of the next: a block", act: async (u, c) => { c.b0 = [c.t, c.t + 1].map(t => trigsOf(t).filter(s => s < 4)); await u.drag(cell(c.t, 0), [cell(c.t, 2), cell(c.t + 1, 3)], { cmd: true }); },
				screen: () => ok($all("#seq .st.selx").length === 8 && S.stepSel?.n === 2 && S.stepSel?.to - S.stepSel?.from === 4, $all("#seq .st.selx").length + " selected, " + JSON.stringify(S.stepSel)) },
			{ say: "press Cmd+D: the block again on steps 5-8", act: u => { document.activeElement?.blur?.(); u.key("d", { cmd: true }); },
				machine: c => ok([c.t, c.t + 1].every((t, k) => same(trigsOf(t).filter(s => s >= 4 && s < 8).map(s => s - 4), c.b0[k])), "steps 5-8 " + [c.t, c.t + 1].map(t => trigsOf(t).filter(s => s >= 4 && s < 8).join(",")).join(" / ")),
				screen: () => ok(S.stepSel?.from === 4 && $all("#seq .st.selx").length === 8, "selection " + JSON.stringify(S.stepSel)) },
			{ say: "click CLR on the LCD: the selected block (steps 5-8 of both tracks) is cleared, nothing else", act: (u, c) => { c.out = [c.t, c.t + 1].map(t => trigsOf(t).filter(s => s < 4 || s >= 8)); u.click('[data-sec="clear"]'); },
				machine: c => ok([c.t, c.t + 1].every((t, k) => !trigsOf(t).some(s => s >= 4 && s < 8) && same(trigsOf(t).filter(s => s < 4 || s >= 8), c.out[k])), "pattern " + [c.t, c.t + 1].map(t => trigsOf(t).join(",")).join(" / ")) },
			{ say: "press Esc: no selection", act: u => u.key("Escape"), screen: () => ok(!S.stepSel && !$all("#seq .st.selx").length, "still selected") },
			{ say: "Cmd+Z three times (paste, duplicate, clear): both tracks as before", act: async u => { for (let i = 0; i < 3; i++) { u.key("z", { cmd: true }); await sleep(800); } },
				machine: c => ok(same(trigsOf(c.t), c.t0) && same(trigsOf(c.t + 1), c.t1) && V.undoCount === c.u0, `pattern ${trigsOf(c.t).join(",")} / ${trigsOf(c.t + 1).join(",")}, undo steps ${c.u0} -> ${V.undoCount}`), within: 8000 }
		],
		async tidy() { if (S.stepSel) clearSel(); }
	};
	/* K3 (DESIGN-keymap.md): the step menu: an accent, a copy and a paste with the pointer only */
	const stepMenuJ = {
		name: "md-seq-step-menu",
		steps: [
			go("seq"), sel(() => soundTrack()),
			{ say: "click an empty step: a trig to work on", act: (u, c) => {
				c.L = Math.min(V.length, V.len); c.t0 = trigsOf(c.t); c.a0 = flagsOf(c.t, "accent"); c.u0 = V.undoCount;
				/* not accented as shown (with EDIT ALL on, the accents are the pattern's), and a free step two away to paste to */
				const free = s => !c.t0.includes(s) && !V.tracks[c.t].acc.has(s);
				c.s = [...Array(c.L).keys()].find(s => free(s)); c.d = [...Array(c.L).keys()].find(s => free(s) && Math.abs(s - c.s) > 1);
				if (c.s == null || c.d == null) throw new Error("no two free steps");
				if (S.stepSel) clearSel(); u.click(cell(c.t, c.s));
			}, screen: c => ok(pressed(cell(c.t, c.s)), "not lit"), machine: c => ok(trigsOf(c.t).includes(c.s), "no trig") },
			{ say: "right-click it: its menu", act: (u, c) => u.rightClick(cell(c.t, c.s)),
				screen: c => ok(!$1("#deskmenu").hidden && $1('#deskmenu [data-mid="step-fill-2"]') && $1(cell(c.t, c.s)).classList.contains("selx"), "no step menu") },
			{ say: "choose Accent", act: u => u.click('#deskmenu [data-mid="step-accent"]'), screen: c => ok($1("#deskmenu").hidden && $1(cell(c.t, c.s)).classList.contains("acc"), "no accent shown"),
				machine: c => ok(flagsOf(c.t, "accent").includes(c.s) || (V.accAll && (pat().accent?.steps || []).includes(c.s)), "accent " + flagsOf(c.t, "accent").join(",")) },
			{ say: "right-click it again and choose Copy", act: async u => { u.rightClick(cell(S.stepSel.t, S.stepSel.from)); await sleep(150); u.click('#deskmenu [data-mid="copy"]'); },
				machine: () => ok(V.clipboard.stepsSize?.length === 1, "clipboard " + JSON.stringify(V.clipboard.stepsSize)) },
			{ say: "right-click an empty step and choose Paste here", act: async (u, c) => { u.rightClick(cell(c.t, c.d)); await sleep(150); u.click('#deskmenu [data-mid="paste"]'); },
				screen: c => ok(pressed(cell(c.t, c.d)), "not lit"), machine: c => ok(trigsOf(c.t).includes(c.d), "pattern " + trigsOf(c.t).join(",")) },
			{ say: "press Escape: no selection; Cmd+Z three times (paste, accent, trig): the track as before", act: async u => { u.key("Escape"); await sleep(150); for (let i = 0; i < 3; i++) { u.key("z", { cmd: true }); await sleep(800); } },
				machine: c => ok(same(trigsOf(c.t), c.t0) && same(flagsOf(c.t, "accent"), c.a0) && V.undoCount === c.u0, `pattern ${trigsOf(c.t).join(",")}, undo steps ${c.u0} -> ${V.undoCount}`), within: 8000 }
		],
		async tidy() { if (!$1("#deskmenu")?.hidden) closeDeskMenu(); if (S.stepSel) clearSel(); }
	};
	/* one step copied and pasted on its track, the two ways a person has: ⌘C / ⌘V as the operating system delivers them
	   (u.osKey: on macOS JUCE's web view turns them into the copy: and paste: commands, never a keydown; 0.3.3 lost them
	   there, in the standalone and in every DAW), and the top bar's Copy and Paste with the mouse (a host that keeps
	   ⌘C and ⌘V for itself leaves those). Each ends with the pattern as it was (⌘Z through the same way in). */
	const pickCopyStep = { say: "Cmd-click a step with a trig: it is selected", act: (u, c) => {
		c.L = Math.min(V.length, V.len); c.t0 = trigsOf(c.t); c.u0 = V.undoCount;
		c.s = c.t0.find(s => s < c.L) ?? 0; c.d = [...Array(c.L).keys()].find(s => !c.t0.includes(s) && Math.abs(s - c.s) > 1);
		if (c.d == null) throw new Error("no free step to paste to");
		if (S.stepSel) clearSel(); u.click(cell(c.t, c.s), { cmd: true });
	}, screen: c => ok($1(cell(c.t, c.s)).classList.contains("selx") && $all("#seq .st.selx").length === 1, $all("#seq .st.selx").length + " selected") };
	const pickPasteStep = { say: "Cmd-click an empty step of the same track", act: (u, c) => u.click(cell(c.t, c.d), { cmd: true }),
		screen: c => ok($1(cell(c.t, c.d)).classList.contains("selx") && !$1(cell(c.t, c.s)).classList.contains("selx"), "not selected") };
	const copyTrack = () => soundTracks().find(t => trigsOf(t).some(s => s < Math.min(V.length, V.len))) ?? soundTrack();
	const oneStepCopied = () => ok(V.clipboard.stepsSize?.length === 1 && V.clipboard.stepsSize?.tracks === 1, "clipboard " + JSON.stringify(V.clipboard.stepsSize));
	const osCopyPaste = {
		name: "md-seq-os-copy-paste", needs: Journey.osKeyPath,
		steps: [
			go("seq"), sel(copyTrack), pickCopyStep,
			{ say: "press Cmd+C on the keyboard (the operating system's way in)", act: async u => { document.activeElement?.blur?.(); await u.osKey("focus cmd+c"); },
				/* with the start tests' key probe on (GEARMULATOR_MDMM_KEYPROBE=1) its line says so too */
				screen: () => ok(Keys.seen().includes("cmd+C") && (!$1("#keyprobe") || $1("#keyprobe").textContent.includes(" cmd+C")),
					"keys that reached the page: " + Keys.seen().join(" ") + ($1("#keyprobe") ? "; probe: " + $1("#keyprobe").textContent : "")), machine: oneStepCopied },
			pickPasteStep,
			{ say: "press Cmd+V on the keyboard: the trig lands there", act: u => u.osKey("cmd+v"), screen: c => ok(pressed(cell(c.t, c.d)), "not lit"), machine: c => ok(trigsOf(c.t).includes(c.d), "pattern " + trigsOf(c.t).join(",")) },
			{ say: "press Cmd+Z on the keyboard: the track as before", act: u => u.osKey("cmd+z"),
				machine: c => ok(same(trigsOf(c.t), c.t0) && V.undoCount === c.u0, `pattern ${trigsOf(c.t).join(",")}, undo steps ${c.u0} -> ${V.undoCount}`), within: 8000 }
		],
		async tidy() { if (S.stepSel) clearSel(); }
	};
	const buttonsCopyPaste = {
		name: "md-seq-copy-paste-buttons",
		steps: [
			go("seq"), sel(copyTrack), pickCopyStep,
			{ say: "click Copy in the top bar: the selected step is copied", act: u => u.click('[data-sec="copy"]'), machine: oneStepCopied,
				screen: c => ok($1(cell(c.t, c.s)).classList.contains("selx"), "the selection went") },
			pickPasteStep,
			{ say: "click Paste in the top bar: the trig lands at the selected step", act: u => u.click('[data-sec="paste"]'), screen: c => ok(pressed(cell(c.t, c.d)), "not lit"),
				machine: c => ok(trigsOf(c.t).includes(c.d) && same(trigsOf(c.t).filter(s => s !== c.d), c.t0), "pattern " + trigsOf(c.t).join(",")) },
			{ say: "click Clr in the top bar: the pasted step is cleared again, nothing else", act: u => u.click('[data-sec="clear"]'), screen: c => ok(!pressed(cell(c.t, c.d)), "still lit"),
				machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",")) },
			{ say: "Cmd+Z twice (clear, paste): the track as before", act: async u => { u.key("z", { cmd: true }); await sleep(800); u.key("z", { cmd: true }); },
				machine: c => ok(same(trigsOf(c.t), c.t0) && V.undoCount === c.u0, `pattern ${trigsOf(c.t).join(",")}, undo steps ${c.u0} -> ${V.undoCount}`), within: 8000 }
		],
		async tidy() { if (S.stepSel) clearSel(); }
	};
	const clearPatternJ = {
		name: "md-seq-clear-pattern-undo",
		steps: [
			go("seq"),
			{ say: "press Alt+Delete: the whole pattern is cleared", act: (u, c) => { c.all = [...Array(16).keys()].map(trigsOf); document.activeElement?.blur?.(); u.key("Delete", { alt: true }); }, screen: () => ok(!$all("#seq .st.on").length, $all("#seq .st.on").length + " lit"), machine: () => ok([...Array(16).keys()].every(t => !trigsOf(t).length), "trigs left") },
			{ ...undoKey, say: "press Cmd+Z: every trig is back", screen: c => ok($all("#seq .st.on").length === c.all.flat().filter(s => s < V.len).length, $all("#seq .st.on").length + " lit"), machine: c => ok(same([...Array(16).keys()].map(trigsOf), c.all), "pattern differs") }
		]
	};
	const fillEveryJ = {
		name: "md-seq-fill-every",
		steps: [
			go("seq"), sel(() => soundTrack()),
			{ say: "right-click an empty step, choose Fill every 2nd from here: every 2nd step from there comes on", act: async (u, c) => { c.t0 = trigsOf(c.t); c.s = [...Array(V.len).keys()].find(s => s >= V.len - 8 && !c.t0.includes(s)) ?? V.len - 8; if (S.stepSel) clearSel(); u.rightClick(cell(c.t, c.s)); await sleep(150); u.click('#deskmenu [data-mid="step-fill-2"]'); },
				machine: c => { const want = []; for (let s = c.s; s < V.len; s += 2) want.push(s); return ok(want.every(s => trigsOf(c.t).includes(s)), "pattern " + trigsOf(c.t).join(",")); }, screen: c => ok(pressed(cell(c.t, c.s)) && pressed(cell(c.t, c.s + 2)), "not lit") },
			{ ...undoKey, machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",")) }
		],
		async tidy() { if (S.stepSel) clearSel(); }
	};
	const rotateJ = {
		name: "md-seq-rotate",
		steps: [
			go("seq"),
			{ say: "pick a track with trigs", act: (u, c) => { c.t = soundTracks().find(t => trigsOf(t).length) ?? 0; c.t0 = trigsOf(c.t); u.click(rail(c.t)); }, screen: c => ok(S.sel === c.t, "selected " + S.sel) },
			{ say: "press Alt+→: one step later", act: u => { document.activeElement?.blur?.(); u.key("ArrowRight", { alt: true }); }, machine: c => { const L = Math.min(V.length, V.len); return ok(same(trigsOf(c.t), sorted(c.t0.map(s => s >= L ? s : (s + 1) % L))), "pattern " + trigsOf(c.t).join(",")); }, screen: c => ok(gridShows(c.t), "grid differs") },
			{ say: "press Alt+←: back", act: u => u.key("ArrowLeft", { alt: true }), machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",")) }
		]
	};
	/* a rotate run ends when the page sees Alt up in any event, not only in Alt's own keyup (which may never come:
	   these synthetic chords send none, as a window switch can lose it); the next rotate is then its own undo step */
	const rotateUndo = {
		name: "md-seq-rotate-undo",
		steps: [
			go("seq"),
			{ say: "pick a track with trigs that a rotate changes", act: (u, c) => { const L = Math.min(V.length, V.len), r = t => sorted(trigsOf(t).map(s => s >= L ? s : (s + 1) % L));
				c.t = soundTracks().find(t => trigsOf(t).length && !same(r(t), trigsOf(t)) && !same(sorted(r(t).map(s => s >= L ? s : (s + 1) % L)), r(t))); if (c.t == null) throw new Error("no track a rotate changes");
				c.t0 = trigsOf(c.t); c.t1 = r(c.t); u.click(rail(c.t)); }, screen: c => ok(S.sel === c.t, "selected " + S.sel) },
			{ say: "press Alt+→: one step later", act: u => { document.activeElement?.blur?.(); u.key("ArrowRight", { alt: true }); }, machine: c => ok(same(trigsOf(c.t), c.t1), "pattern " + trigsOf(c.t).join(",")) },
			{ say: "press Shift (Alt is up now), then Alt+→ again: another rotate", act: async u => { u.key("Shift"); await sleep(300); u.key("ArrowRight", { alt: true }); }, machine: c => ok(!same(trigsOf(c.t), c.t1), "pattern " + trigsOf(c.t).join(",")) },
			{ say: "Cmd+Z: only the second rotate is undone", act: u => u.key("z", { cmd: true }), machine: c => ok(same(trigsOf(c.t), c.t1), "pattern " + trigsOf(c.t).join(",") + ", want " + c.t1.join(",")) },
			{ say: "Cmd+Z: and then the first", act: u => u.key("z", { cmd: true }), machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",") + ", want " + c.t0.join(",")) }
		],
		async tidy(u, c) { for (let i = 0; i < 4 && c.t0 && !same(trigsOf(c.t), c.t0); i++) { u.key("z", { cmd: true }); await until(() => same(trigsOf(c.t), c.t0), 3000); } }
	};
	const trackKeys = {
		name: "md-keys-track-select",
		steps: [
			go("seq"), sel(() => 0),
			{ say: "press ↓", act: u => { document.activeElement?.blur?.(); u.key("ArrowDown"); }, screen: () => ok(S.sel === 1 && $1("#rail .th.sel")?.dataset.sel === "1", "selected " + S.sel) },
			{ say: "press ↑", act: u => u.key("ArrowUp"), screen: () => ok(S.sel === 0, "selected " + S.sel) }
		]
	};
	const muteKeys = {
		name: "md-keys-mute",
		steps: [
			go("seq"), sel(() => soundTrack()),
			{ say: "press M: the track is muted", act: (u, c) => { c.m0 = mutes(); document.activeElement?.blur?.(); u.key("m"); }, screen: c => ok(pressed(`#rail .th[data-sel="${c.t}"] .ms.m`), "M not lit"), machine: c => ok(mutes().includes(c.t), "machine mutes " + mutes()) },
			{ say: "press M again", act: u => u.key("m"), machine: c => ok(same(mutes(), c.m0), "machine mutes " + mutes()) },
			{ say: "press Alt+M: every track muted", act: u => u.key("m", { alt: true }), machine: () => ok(mutes().length === 16, "machine mutes " + mutes()), screen: () => ok($all("#rail .ms.m").every(b => b.getAttribute("aria-pressed") === "true"), "not every M lit") },
			{ say: "press 0: every track unmuted", act: u => u.key("0"), machine: () => ok(!mutes().length, "machine mutes " + mutes()), screen: () => ok($all("#rail .ms.m").every(b => b.getAttribute("aria-pressed") === "false"), "an M lit") }
		]
	};
	const liveRec = {
		name: "md-seq-live-record",
		steps: [
			go("seq"),
			{ say: "click REC: live recording, playing", act: (u, c) => { c.t = soundTracks().slice().sort((a, b) => trigsOf(a).length - trigsOf(b).length)[0] ?? soundTrack(); c.n0 = trigsOf(c.t).length; u.click("#rec"); }, screen: () => ok(V.rec && $1("#rec").getAttribute("aria-pressed") === "true", "REC " + V.rec), machine: () => ok(tele.last?.playing, "not playing"), within: 5000 },
			{ say: "click a step of a track while it records: the machine records a trig", act: async (u, c) => { results.length = 0; c.at = S.step; u.click(cell(c.t, 0)); c.note = `track ${c.t + 1} (${c.n0} trigs), clicked at step ${c.at + 1}`; },
				machine: c => ok(trigsOf(c.t).length > c.n0, `${c.n0} -> ${trigsOf(c.t).length} trigs; results ${results.map(r => r.op + ":" + r.ok + (r.errors ? " " + r.errors.join(";") : "") + (r.note ? " " + r.note : "")).join(", ")}; telemetry rec ${tele.last?.rec ?? "?"} playing ${tele.last?.playing}`), within: 8000 },
			{ say: "click REC again, then STOP", act: async u => { u.click("#rec"); await sleep(500); if (V.playing) u.click("#play"); }, screen: () => ok(!V.rec && !V.playing, `REC ${V.rec}, playing ${V.playing}`), machine: () => ok(!tele.last?.playing, "playing") }
		],
		async tidy(u) { if (V.rec) u.click("#rec"); if (V.playing) u.click("#play"); }
	};

	/* ---------- generators ---------- */
	const genJourney = (name, defaults) => ({
		name,
		steps: [
			{ say: "on Sequence, pick a drum track (a kick if there is one)", act: (u, c) => {
				u.click(tab("seq")); const role = t => genRole(V.tracks[t].m);
				c.t = [...Array(16).keys()].find(t => role(t) === "kick") ?? [...Array(16).keys()].find(t => role(t) !== "keep") ?? 0;
				u.click(rail(c.t)); c.p0 = trigsOf(c.t);
			}, screen: c => ok(S.ws === "seq" && S.sel === c.t && !!$1("#genband [data-rand]"), "no GEN bar"), machine: c => ok(!!pat() && genRole(V.tracks[c.t].m) !== "keep", "no drum track") },
			...(defaults ? [{ say: "click GEN's Defaults key", act: u => u.click('#genband [data-gen="fill"]'), machine: c => ok(genSpec(c.t).kind !== "keep", "spec " + JSON.stringify(genSpec(c.t))) }] : []),
			{ say: "the GEN bar offers the track's generator, not Keep", screen: c => ok($1('#genband [data-genkind][aria-pressed="true"]')?.dataset.genkind !== "keep", `GEN mode shows ${$1('#genband [data-genkind][aria-pressed="true"]')?.dataset.genkind} for ${V.tracks[c.t].m} (role ${genRole(V.tracks[c.t].m)}); every track's spec: ${genSpecs().map(x => x.kind[0]).join("")}`) },
			{ say: "click GEN's Random key until the rhythm changes", act: async (u, c) => { c.base = defaults ? c.p0 : trigsOf(c.t); for (let i = 0; i < 5; i++) { u.click("#genband [data-rand]"); if (await until(() => !same(trigsOf(c.t), c.base) && idle(), 2500)) break; } c.note = `track ${c.t + 1}: ${c.p0.length} -> ${trigsOf(c.t).length} trigs`; },
				screen: c => ok(gridShows(c.t), "the grid does not show the pattern"), machine: c => ok(!same(trigsOf(c.t), c.p0) && same(trigsOf(c.t), lit(c.t)), `pattern ${trigsOf(c.t).join(",")}; before ${c.p0.join(",")}; view ${lit(c.t).join(",")}`) },
			{ say: "click the Sound tab and MUTATE's Random key", act: async (u, c) => { u.click(tab("sound")); await sleep(300); c.k0 = kitVals(c.t); u.click("#mutband [data-rand]"); },
				screen: c => ok(S.ws === "sound" && !!S.mut.note && $all('#main .pc[data-g="syn"]').every(el => V.tracks[c.t].syn[el.dataset.n] == null || +el.querySelector("b")?.textContent === V.tracks[c.t].syn[el.dataset.n]), "note " + S.mut.note), machine: c => ok(!same(kitVals(c.t), c.k0), "kit unchanged") },
			{ say: "click Undo: the sound is back", act: u => u.click("#undo"), screen: c => ok($all('#main .pc[data-g="syn"]').every(el => +el.querySelector("b")?.textContent === c.k0[pidx(c.t, el.dataset.n, "syn")]), "boxes " + $all('#main .pc[data-g="syn"] b').map(b => b.textContent).join(",")), machine: c => ok(same(kitVals(c.t), c.k0), "kit " + kitVals(c.t).join(",")), within: 8000 },
			{ say: "click Undo again: the rhythm is back (the GEN run was one undo step)", act: u => u.click("#undo"), machine: c => ok(same(trigsOf(c.t), c.p0), "pattern " + trigsOf(c.t).join(",")), within: 8000 },
			{ say: "click the Sequence tab: the grid shows it", act: u => u.click(tab("seq")), screen: c => ok(c.p0.every(s => pressed(cell(c.t, s))) && $all(`#seq .st[data-t="${c.t}"][aria-pressed="true"]`).length === c.p0.filter(s => s < V.len).length, "grid differs") }
		],
		async tidy(u, c) { for (let i = 0; i < 3 && c.p0 && !same(trigsOf(c.t), c.p0); i++) { u.click("#undo"); await until(idle, 3000); await sleep(500); } }
	});
	const genKeys = {
		name: "md-gen-keys-r",
		steps: [
			go("seq"),
			{ say: "pick a drum track and click Defaults", act: async (u, c) => { c.t = [...Array(16).keys()].find(t => genRole(V.tracks[t].m) === "kick") ?? soundTrack(); c.p0 = trigsOf(c.t); u.click(rail(c.t)); await sleep(200); u.click('#genband [data-gen="fill"]'); }, machine: c => ok(genSpec(c.t).kind !== "keep" && idle(), "keep") },
			{ say: "press R until the rhythm changes", act: async (u, c) => { c.p1 = trigsOf(c.t); document.activeElement?.blur?.(); for (let i = 0; i < 5; i++) { u.key("r"); if (await until(() => !same(trigsOf(c.t), c.p1) && idle(), 2500)) break; } }, machine: c => ok(!same(trigsOf(c.t), c.p1), "unchanged"), screen: c => ok(gridShows(c.t), "grid differs") },
			{ say: "click GEN's Hits value: one more hit", act: (u, c) => { c.p2 = trigsOf(c.t); const g = $1('#genband .gv[data-gv="k"]'); if (!g) throw new Error("not a Euclid spec: " + JSON.stringify(genSpec(c.t))); u.click(g); }, machine: c => ok(!same(trigsOf(c.t), c.p2), "unchanged") },
			{ ...undoKey, say: "press Cmd+Z: the whole run goes in one step", machine: c => ok(same(trigsOf(c.t), c.p0), "pattern " + trigsOf(c.t).join(",")), within: 8000 }
		],
		async tidy(u, c) { for (let i = 0; i < 3 && c.p0 && !same(trigsOf(c.t), c.p0); i++) { u.click("#undo"); await sleep(800); } }
	};

	/* ---------- Sound ---------- */
	const shapeSound = {
		name: "md-sound-shape-undo",
		steps: [
			{ say: "click the Sound tab: grouped by function", act: u => u.click(tab("sound")), screen: () => onTab("sound") === true ? ok($all("#main .sg").length >= 3, $all("#main .sg").length + " groups") : onTab("sound"), machine: () => ok(!!kit(), "no kit") },
			sel(() => soundTrack()),
			{ say: "drag a synthesis value sideways", act: async (u, c) => { const el = $1('#main .pc[data-g="syn"]'); c.n = el.dataset.n; c.i = pidx(c.t, c.n, "syn"); c.v0 = kitVals(c.t)[c.i]; await nudge(u, el, c.v0); c.note = `T${c.t + 1} ${c.n} ${c.v0} -> ${V.tracks[c.t].syn[c.n]}`; },
				screen: c => { const b = $1(`#main .pc[data-g="syn"][data-n="${c.n}"] b`)?.textContent; return ok(b != null && +b === V.tracks[c.t].syn[c.n] && +b !== c.v0, "shows " + b); },
				machine: c => ok(kitVals(c.t)[c.i] !== c.v0 && kitVals(c.t)[c.i] === V.tracks[c.t].syn[c.n] && machineState().kit.working === "edited", `kit ${c.n} = ${kitVals(c.t)[c.i]}, ${machineState().kit.working}`) },
			{ say: "click Undo", act: u => u.click("#undo"), screen: c => ok(+$1(`#main .pc[data-g="syn"][data-n="${c.n}"] b`)?.textContent === c.v0, "shows " + $1(`#main .pc[data-g="syn"][data-n="${c.n}"] b`)?.textContent), machine: c => ok(kitVals(c.t)[c.i] === c.v0, `kit ${c.n} = ${kitVals(c.t)[c.i]}`) }
		]
	};
	const arrows = {
		name: "md-sound-value-keys",
		steps: [
			go("sound"), sel(() => soundTrack()),
			{ say: "focus an effects value and press ↑ three times", act: async (u, c) => { const el = $1('#main .pc[data-g="fx"]'); c.n = el.dataset.n; c.i = pidx(c.t, c.n, "fx"); c.v0 = kitVals(c.t)[c.i]; el.focus(); const d = c.v0 > 120 ? "ArrowDown" : "ArrowUp"; c.want = c.v0 + (d === "ArrowUp" ? 3 : -3); for (let k = 0; k < 3; k++) { u.key(d); await sleep(80); } },
				screen: c => ok(S.sel === c.t && +$1(`#main .pc[data-g="fx"][data-n="${c.n}"] b`)?.textContent === c.want, `shows ${$1(`#main .pc[data-g="fx"][data-n="${c.n}"] b`)?.textContent}, want ${c.want} (from ${c.v0}); selected track ${S.sel + 1} (was ${c.t + 1}); focus now ${document.activeElement?.className || document.activeElement?.tagName}`), machine: c => ok(kitVals(c.t)[c.i] === c.want, "kit " + kitVals(c.t)[c.i]) },
			{ say: "click Undo until it is back", act: async (u, c) => { for (let k = 0; k < 3 && kitVals(c.t)[c.i] !== c.v0; k++) { u.click("#undo"); await sleep(700); } }, machine: c => ok(kitVals(c.t)[c.i] === c.v0, "kit " + kitVals(c.t)[c.i]) }
		]
	};
	const machinePick = {
		name: "md-sound-machine-pick-undo",
		steps: [
			go("sound"), sel(() => soundTrack()),
			{ say: "click the machine key: the picker opens", act: (u, c) => { c.m0 = kit().tracks[c.t].machine; u.click("#machbtn"); }, screen: () => ok(!$1("#machpop").hidden && $all("#machpop .mk").length > 3, "picker closed") },
			{ say: "pick another family and a machine in it", act: async (u, c) => { u.click('#machpop .mf[data-fam="GND"]'); await sleep(200); const m = $all("#machpop .mk").find(b => b.dataset.mach !== c.m0 && b.dataset.mach !== "GND-EMPTY"); c.m1 = m.dataset.mach; u.click(m); },
				screen: c => ok($1("#machpop").hidden && V.tracks[c.t].m === c.m1, "view " + V.tracks[c.t].m), machine: c => ok(kit().tracks[c.t].machine === c.m1, "kit machine " + kit().tracks[c.t].machine), within: 6000 },
			{ ...undoKey, machine: c => ok(kit().tracks[c.t].machine === c.m0, "kit machine " + kit().tracks[c.t].machine), screen: c => ok(V.tracks[c.t].m === c.m0, "view " + V.tracks[c.t].m), within: 6000 }
		]
	};
	const soundCopy = {
		name: "md-sound-copy-paste",
		steps: [
			go("sound"),
			{ say: "pick a track and press Cmd+C", act: async (u, c) => { const st = soundTracks(); c.a = st[0]; c.b = st.find(t => t !== c.a && kit().tracks[t].machine !== kit().tracks[c.a].machine) ?? st[1]; c.kb = JSON.stringify(kit().tracks[c.b]); u.click(rail(c.a)); await sleep(200); document.activeElement?.blur?.(); u.key("c", { cmd: true }); }, machine: () => ok(V.clipboard.sound, "nothing copied") },
			{ say: "pick another track and press Cmd+V", act: async (u, c) => { u.click(rail(c.b)); await sleep(200); document.activeElement?.blur?.(); u.key("v", { cmd: true }); },
				machine: c => ok(kit().tracks[c.b].machine === kit().tracks[c.a].machine && same(kitVals(c.b), kitVals(c.a)), `track ${c.b + 1} ${kit().tracks[c.b].machine}`), screen: c => ok(V.tracks[c.b].m === kit().tracks[c.a].machine, "view " + V.tracks[c.b].m), within: 8000 },
			{ ...undoKey, machine: c => ok(JSON.stringify(kit().tracks[c.b]) === c.kb, "track not restored"), within: 8000 }
		]
	};
	const editorDrag = {
		name: "md-sound-screen-drag",
		steps: [
			go("sound"), sel(() => soundTrack()),
			{ say: "drag a handle of the filter screen", act: async (u, c) => { const cv = $1('#main canvas.ed[data-ed="flt"]') || $1("#main canvas.ed"); if (!cv) throw new Error("no screen"); c.ed = cv.dataset.ed; c.k0 = kitVals(c.t); const r = cv.getBoundingClientRect(), h = ED[c.ed].handles(r.width, r.height, cv)[0]; c.note = `${c.ed} ${h.k}`; await u.drag(cv, [[10, 0], [20, -6], [30, -10]], {}, { fx: h.x / r.width, fy: h.y / r.height }); },
				machine: c => ok(!same(kitVals(c.t), c.k0), "kit unchanged") },
			{ ...undoKey, machine: c => ok(same(kitVals(c.t), c.k0), "kit not restored"), within: 8000 }
		]
	};
	const controlAll = {
		name: "md-sound-control-all",
		steps: [
			go("sound"), sel(() => soundTrack()),
			/* B-023: a real press focuses the value (a synthetic one does not): it is focused here, so ⌘Z below is pressed
			   with the value focused, as a person's is */
			{ say: "Alt-drag an effects value: every track's knob moves", act: async (u, c) => { c.k0 = allKitVals(); const el = $1('#main .pc[data-g="fx"]'), d = getV(el) > 64 ? -1 : 1; el.focus(); await u.drag(el, [[d * 4, 0], [d * 8, 0], [d * 16, 0], [d * 24, 0]], { alt: true }); },
				machine: c => { const moved = allKitVals().filter((v, t) => !same(v, c.k0[t])).length; return ok(moved >= 2, moved + " tracks moved"); }, within: 10000 },
			{ ...undoKey, say: "press Cmd+Z: one step back for all", machine: c => ok(same(allKitVals(), c.k0), "not all back: " + kitDiff(c.k0, allKitVals())), within: 10000 }
		]
	};
	/* 0.3.5: the top bar's GLOBAL key (where FN was) opens the machine's global settings; lit while open, Esc closes */
	const globalKey = {
		name: "md-global-key",
		steps: [
			{ say: "click GLOBAL in the top bar: the panel opens, the key is lit", act: u => u.click("#globkey"), screen: () => ok(!$1("#globpop").hidden && pressed("#globkey"), "panel " + ($1("#globpop").hidden ? "closed" : "open") + ", key " + $1("#globkey")?.getAttribute("aria-pressed")) },
			{ say: "press Escape: it closes, the key goes dark", act: u => u.key("Escape"), screen: () => ok($1("#globpop").hidden && !pressed("#globkey"), "still open") }
		]
	};

	/* ---------- Mix ---------- */
	const mixSolo = {
		name: "md-mix-mute-solo",
		steps: [
			{ say: "click the Mix tab", act: u => u.click(tab("mix")), screen: () => onTab("mix") === true ? ok($all(".strip").length === 16, $all(".strip").length + " strips") : onTab("mix"), machine: () => ok(Array.isArray(desk().mutes), "no mutes read from the machine") },
			{ say: "drag across four tracks' M keys", act: async (u, c) => {
				c.m0 = mutes(); const keep = soloKeep();
				c.a = [...Array(13).keys()].find(a => [0, 1, 2, 3].every(k => !c.m0.includes(a + k) && !keep.has(a + k)));
				c.b = [...Array(16).keys()].find(b => (b < c.a || b > c.a + 3) && !c.m0.includes(b) && !keep.has(b));
				if (c.a == null || c.b == null) throw new Error("no four unmuted tracks in a row");
				c.run = [0, 1, 2, 3].map(k => c.a + k);
				await u.drag(`.strip[data-sel="${c.a}"] .ms.m`, c.run.slice(1).map(i => `.strip[data-sel="${i}"] .ms.m`), {}, { captured: false });
				c.note = `tracks ${c.a + 1}-${c.a + 4}`;
			}, screen: c => ok(c.run.every(i => pressed(`.strip[data-sel="${i}"] .ms.m`)), "M keys not lit"), machine: c => ok(c.run.every(i => mutes().includes(i)), "machine mutes " + mutes()) },
			{ say: "click S on another track (solo)", act: (u, c) => u.click(`.strip[data-sel="${c.b}"] .ms.s`), screen: c => ok(pressed(`.strip[data-sel="${c.b}"] .ms.s`), "S not lit"),
				machine: c => { const keep = soloKeep(), want = [...Array(16).keys()].filter(i => i !== c.b && !keep.has(i)); return ok(want.every(i => mutes().includes(i)) && !mutes().includes(c.b), "machine mutes " + mutes()); } },
			{ say: "click S again (un-solo): the four mutes are kept", act: (u, c) => u.click(`.strip[data-sel="${c.b}"] .ms.s`),
				screen: c => { const l = $all(".strip .ms.m").filter(b => b.getAttribute("aria-pressed") === "true").map(b => +b.dataset.mute); return ok(same(l, sorted([...new Set([...c.m0, ...c.run])])), "M keys lit " + l.join(",")); },
				machine: c => ok(same(mutes(), sorted([...new Set([...c.m0, ...c.run])])), "machine mutes " + mutes()), within: 8000 }
		],
		async tidy(u, c) { if (c.b != null && S.soloSet.has(c.b)) u.click(`.strip[data-sel="${c.b}"] .ms.s`); for (const i of c.run || []) if (mutes().includes(i) && !c.m0.includes(i)) { u.click(`.strip[data-sel="${i}"] .ms.m`); await sleep(150); } }
	};
	const shiftMutes = {
		name: "md-mix-shift-mutes",
		steps: [
			go("mix"),
			{ say: "hold Shift and click two M keys: prepared, nothing sent", act: (u, c) => { c.m0 = mutes(); c.ts = [...Array(16).keys()].filter(i => !c.m0.includes(i)).slice(0, 2); for (const i of c.ts) u.click(`.strip[data-sel="${i}"] .ms.m`, { shift: true }); },
				screen: c => ok(c.ts.every(i => $1(`.strip[data-sel="${i}"] .ms.m`).classList.contains("prep")), "not prepared"), machine: c => ok(same(mutes(), c.m0), "sent while held: " + mutes()), within: 1000 },
			{ say: "let Shift go: both mute together", act: () => document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift", code: "ShiftLeft", bubbles: true })), machine: c => ok(c.ts.every(i => mutes().includes(i)), "machine mutes " + mutes()), screen: c => ok(c.ts.every(i => pressed(`.strip[data-sel="${i}"] .ms.m`)), "M not lit") },
			{ say: "click both M keys again", act: async (u, c) => { for (const i of c.ts) { u.click(`.strip[data-sel="${i}"] .ms.m`); await sleep(150); } }, machine: c => ok(same(mutes(), c.m0), "machine mutes " + mutes()) }
		]
	};
	const allOff = {
		name: "md-mix-ms-off",
		steps: [
			go("seq"),
			{ say: "mute two tracks on the rail", act: async (u, c) => { c.ts = [0, 1]; for (const i of c.ts) { if (!mutes().includes(i)) u.click(`#rail .th[data-sel="${i}"] .ms.m`); await sleep(150); } }, machine: c => ok(c.ts.every(i => mutes().includes(i)), "machine mutes " + mutes()) },
			{ say: "click M/S off", act: u => u.click("#allon"), machine: () => ok(!mutes().length, "machine mutes " + mutes()), screen: () => ok($1("#allon").disabled, "M/S off still on") }
		]
	};
	const fader = {
		name: "md-mix-fader-undo",
		steps: [
			go("mix"),
			{ say: "drag a track's volume fader", act: async (u, c) => { c.t = soundTrack(); c.i = pidx(c.t, "VOL", "rt"); c.v0 = kitVals(c.t)[c.i]; const d = c.v0 > 64 ? 1 : -1; await u.drag(`.strip[data-sel="${c.t}"] .fader[data-g]`, [[0, d * 8], [0, d * 16], [0, d * 24]]); },
				screen: c => ok($1(`.strip[data-sel="${c.t}"] .v`)?.textContent === String(V.tracks[c.t].rt.VOL) && V.tracks[c.t].rt.VOL !== c.v0, "shows " + $1(`.strip[data-sel="${c.t}"] .v`)?.textContent), machine: c => ok(kitVals(c.t)[c.i] !== c.v0, "kit VOL " + kitVals(c.t)[c.i]) },
			{ ...undoKey, machine: c => ok(kitVals(c.t)[c.i] === c.v0, "kit VOL " + kitVals(c.t)[c.i]), screen: c => ok($1(`.strip[data-sel="${c.t}"] .v`)?.textContent === String(c.v0), "shows " + $1(`.strip[data-sel="${c.t}"] .v`)?.textContent) }
		]
	};
	const outKey = {
		name: "md-mix-out-route",
		steps: [
			go("mix"),
			{ say: "click a track's OUT key: an individual output", act: (u, c) => { c.t = soundTrack(); c.o0 = Docs.global.routing[c.t]; u.click(`.strip[data-sel="${c.t}"] [data-out]`); },
				screen: c => ok($1(`.strip[data-sel="${c.t}"] [data-out]`).textContent !== "OUT " + c.o0, "key " + $1(`.strip[data-sel="${c.t}"] [data-out]`).textContent), machine: c => ok(Docs.global.routing[c.t] !== c.o0, "global routing " + Docs.global.routing[c.t]) },
			{ say: "click it until it is back", act: async (u, c) => { for (let k = 0; k < 8 && Docs.global.routing[c.t] !== c.o0; k++) { u.click(`.strip[data-sel="${c.t}"] [data-out]`); await sleep(800); } }, machine: c => ok(Docs.global.routing[c.t] === c.o0, "global routing " + Docs.global.routing[c.t]) }
		]
	};
	const masterFx = {
		name: "md-mix-master-fx",
		steps: [
			go("mix"),
			{ say: "drag a master effect's value", act: async (u, c) => { c.f0 = JSON.stringify(kit().masterFx); const el = $1('#main .pc[data-g="mfx"]'); await nudge(u, el, getV(el)); }, machine: c => ok(JSON.stringify(kit().masterFx) !== c.f0, "kit master effects unchanged") },
			{ ...undoKey, machine: c => ok(JSON.stringify(kit().masterFx) === c.f0, "not restored") }
		]
	};

	/* ---------- Song ---------- */
	const songRows = () => Docs.songs[currentSongSlot()]?.rows || [];
	const songArrange = {
		name: "md-song-arrange-rows",
		steps: [
			go("song"),
			{ say: "click Arrange and a pattern pad: a row is added", act: async (u, c) => { c.r0 = JSON.stringify(songRows()); c.n0 = songRows().length; u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); u.click('[data-addpat="0"]'); },
				machine: c => ok(songRows().length === c.n0 + 1, songRows().length + " rows"), screen: c => ok($all("#tl .scell:not(.empty)").length === c.n0 + 1, $all("#tl .scell:not(.empty)").length + " cells") },
			{ say: "click Delete on the selected row", act: u => u.click('[data-rowact="del"]'), machine: c => ok(songRows().length === c.n0, songRows().length + " rows") },
			{ say: "Cmd+Z twice, Cmd+Shift+Z, Cmd+Z: the song is as it was", act: async u => { for (const m of [{ cmd: true }, { cmd: true }, { cmd: true, shift: true }, { cmd: true }]) { u.key("z", m); await sleep(800); } }, machine: c => ok(JSON.stringify(songRows()) === c.r0, songRows().length + " rows"), within: 8000 }
		]
	};
	const songChain = {
		name: "md-song-chain",
		steps: [
			go("song"),
			{ say: "click Chain and pads A02, A03: the machine plays the chain", act: async u => { u.click('[data-set="songpick"] button[data-v="chain"]'); await sleep(200); u.click('[data-bank="0"]'); await sleep(200); u.click('[data-chainpad="1"]'); await sleep(100); u.click('[data-chainpad="2"]'); },
				machine: () => ok(desk().chain?.active && same(desk().chain.patterns, [1, 2]), "chain " + JSON.stringify(desk().chain)), screen: () => ok(/CHAIN/.test($1("#songPlays")?.textContent || ""), "header " + $1("#songPlays")?.textContent), within: 6000 },
			{ say: "click Clear: the chain ends", act: u => u.click('[data-chain="clear"]'), machine: () => ok(!desk().chain?.active, "chain " + JSON.stringify(desk().chain)), within: 6000 }
		],
		async tidy(u, c) { if (desk().chain?.active) cmd("chainClear"); if (S.ws === "song") u.click('[data-set="songpick"] button[data-v="arrange"]'); }
	};

	/* ---------- Sampler ---------- */
	const inked = cv => { if (!cv || !cv.width) return 0; const g = cv.getContext("2d").getImageData(0, 0, cv.width, cv.height).data; let n = 0; for (let i = 4; i < g.length; i += 4) if (g[i] !== g[0] || g[i + 1] !== g[1] || g[i + 2] !== g[2]) n++; return n; };
	const samplerSlots = {
		name: "md-sampler-slots", needs: Journey.onScreen,
		steps: [
			{ say: "click the Sampler tab: RAM and ROM slots", act: u => u.click(tab("sampler")), screen: () => ok(S.ws === "sampler" && $all(".slotk.ram").length === 4 && $all(".slotk.rom").length >= 32, $all(".slotk").length + " slot keys"), machine: () => ok(!!Docs.samples, "no samples document") },
			{ say: "click a ROM slot that holds a sample: its waveform is drawn", act: async (u, c) => { const k = $all(".slotk.rom.has")[0] || $all(".slotk.rom")[0]; c.slot = k.dataset.slot; u.click(k);
				let f = 0; const t0 = performance.now(); await new Promise(r => { const tick = () => { f++; if (performance.now() - t0 < 1000) requestAnimationFrame(tick); else r(); }; requestAnimationFrame(tick); setTimeout(r, 1500); }); c.frames = f; c.vis = document.visibilityState; },
				screen: c => { const cvs = $all("#main canvas"), n = Math.max(0, ...cvs.map(inked)); return ok(S.smpSlot === c.slot && n > 100, `${n} pixels drawn on ${cvs.length} canvases (${cvs.map(x => x.className + " " + x.width + "x" + x.height).slice(0, 4).join(", ")}), slot ${S.smpSlot}; ${c.frames} animation frames in 1 s, page ${c.vis}`); }, machine: () => ok(!!Docs.samples, "no samples"), within: 8000 }
		]
	};
	const samplerSetup = {
		name: "md-sampler-setup-undo",
		steps: [
			go("sampler"),
			{ say: "click an unused RAM slot", act: (u, c) => { c.n = [1, 2, 3, 4].find(k => !V.tracks.some(t => t.m === "RAM-R" + k || t.m === "RAM-P" + k)); if (!c.n) throw new Error("every RAM slot is used"); u.click(`.slotk[data-slot="RAM${c.n}"]`); }, screen: () => ok(!!$1("[data-setupgo]"), "no Set up sampling") },
			{ say: "click Set up sampling: a recorder of the main mix, out of it (VOL 0), and a player", act: (u, c) => { [c.r, c.p] = setupTracks(); c.m0 = [kit().tracks[c.r].machine, kit().tracks[c.p].machine]; c.vol0 = kitVals(c.r)[pidx(c.r, "VOL", "rt")]; u.click("[data-setupgo]"); },
				machine: c => ok(kit().tracks[c.r].machine === "RAM-R" + c.n && kit().tracks[c.p].machine === "RAM-P" + c.n && kitVals(c.r)[pidx(c.r, "MLEV", "syn")] === 64 && kitVals(c.r)[pidx(c.r, "VOL", "rt")] === 0,
					`tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.p].machine}, recorder MLEV ${kitVals(c.r)[pidx(c.r, "MLEV", "syn")]} VOL ${kitVals(c.r)[pidx(c.r, "VOL", "rt")]}`),
				screen: () => ok(/VOL 0/.test($1(".recmix")?.textContent || ""), "no VOL 0 note on the Source card"), within: 8000 },
			{ say: "click Input A: the recorder's VOL comes back", act: u => u.click('[data-recsrc="a"]'), machine: c => ok(kitVals(c.r)[pidx(c.r, "VOL", "rt")] === c.vol0 && kitVals(c.r)[pidx(c.r, "MLEV", "syn")] === 0, "VOL " + kitVals(c.r)[pidx(c.r, "VOL", "rt")]), within: 8000 },
			{ say: "click Main mix: VOL 0 again", act: u => u.click('[data-recsrc="main"]'), machine: c => ok(kitVals(c.r)[pidx(c.r, "VOL", "rt")] === 0, "VOL " + kitVals(c.r)[pidx(c.r, "VOL", "rt")]), within: 8000 },
			{ ...undoKey, say: "press Cmd+Z three times: as before the set-up", act: async u => { for (let k = 0; k < 3; k++) { u.key("z", { cmd: true }); await sleep(900); } },
				machine: c => ok(same([kit().tracks[c.r].machine, kit().tracks[c.p].machine], c.m0) && kitVals(c.r)[pidx(c.r, "VOL", "rt")] === c.vol0, `tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.p].machine}, VOL ${kitVals(c.r)[pidx(c.r, "VOL", "rt")]}`), within: 10000 }
		]
	};
	const audition = {
		name: "md-sampler-audition", needs: Journey.onScreen,
		steps: [
			go("sampler"),
			{ say: "click a ROM slot's play key: it plays", act: async (u, c) => { const k = $all(".slotk.rom.has")[0]; if (!k) throw new Error("no ROM sample"); u.click(k); await sleep(400); results.length = 0; const b = $all("[data-aud]").find(x => !x.disabled); if (!b) throw new Error("no play key"); c.key = b.dataset.aud; u.click(b);
				await until(() => results.some(r => r.heard === "audition" && r.state === "playing"), 4000); await sleep(30); c.shown = $all("[data-aud]").some(x => x.dataset.aud === c.key && x.getAttribute("aria-pressed") === "true"); },
				machine: () => ok(results.some(r => r.heard === "audition" && r.state === "playing"), "no audition"), screen: c => ok(c.shown, "the play key was not shown playing") }
		],
		async tidy() { if (typeof Aud !== "undefined" && Aud) cmd("auditionStop", {}); }
	};

	/* ---------- the library ---------- */
	const ks = k => `#libpop .ks[data-ks="${k}"]`;
	const kitName = k => kitNameText(Docs.kits[k]?.name);
	const libDialog = {
		name: "md-lib-load-save-dialog",
		steps: [
			{ say: "click KIT on the LCD: the kit library opens", act: u => u.click("#kitf"), screen: () => ok(!$1("#libpop").hidden && $all("#libpop .ks").length === 64, "library closed"), machine: () => ok(Object.keys(Docs.kits).length === 64, Object.keys(Docs.kits).length + " kits read") },
			{ say: "click another kit: it loads at once", act: async (u, c) => {
				const cur = V.kit; c.k = [...Array(64).keys()].find(k => k !== cur && !kitEmpty(k) && kitName(k) && kitName(k) !== kitName(cur));
				if (c.k == null) throw new Error("no other kit with a name");
				c.edited = V.kitState === "edited"; u.click(ks(c.k));
				if (c.edited && await until(dlgShown, 2000)) { u.click(dlgButton("Load without saving")); c.note = "the kit had edits: asked first, loaded without saving"; } else c.note = `K${c.k + 1} ${kitName(c.k)}`;
			}, screen: c => ok($1(ks(c.k))?.classList.contains("cur") && $1("#dlg").hidden && $1("#kitname").textContent.includes(kitName(c.k).trim()), `LCD kit "${$1("#kitname").textContent}"`), machine: c => ok(currentKitSlot() === c.k, "machine kit " + currentKitSlot()), within: 4000 },
			{ say: "Alt-click a third kit: selected, not loaded", act: (u, c) => { c.j = [...Array(64).keys()].reverse().find(k => k !== c.k && !kitEmpty(k) && kitLoaded(k)); c.j0 = JSON.stringify(Docs.kits[c.j]); u.click(ks(c.j), { alt: true }); }, screen: c => ok($1(ks(c.j))?.getAttribute("aria-selected") === "true", "not selected"), machine: c => ok(currentKitSlot() === c.k, "machine kit " + currentKitSlot()) },
			{ say: "click Save as: the plug-in asks before overwriting", act: (u, c) => { c.u0 = V.undoCount; u.click('#libpop [data-la="saveas"]'); }, screen: () => ok(dlgShown() && !!dlgButton("Overwrite"), "no question"), machine: c => ok(JSON.stringify(Docs.kits[c.j]) === c.j0, "slot changed before the answer") },
			{ say: "with the question open, press Delete, Backspace, 3 and 1", act: async (u, c) => { c.ws = S.ws; for (const k of ["Delete", "Backspace", "3", "1"]) { u.key(k); await sleep(150); } },
				screen: c => ok(dlgShown() && S.ws === c.ws && LIB.open === "kit", `dialog ${dlgShown()}, workspace ${S.ws}, library ${LIB.open}`), machine: c => ok(JSON.stringify(Docs.kits[c.j]) === c.j0 && V.undoCount === c.u0, "the page behind acted"), within: 1500 },
			{ say: "click Overwrite: saved into that slot", act: u => u.click(dlgButton("Overwrite")), screen: c => ok(!dlgShown() && $1(ks(c.j))?.classList.contains("cur"), "slot not current"),
				machine: c => ok(currentKitSlot() === c.j && same(Docs.kits[c.j]?.tracks.map(t => t.machine), Docs.kits[c.k]?.tracks.map(t => t.machine)), `machine kit ${currentKitSlot()}`), within: 8000 },
			esc
		]
	};
	const libSelect = k => ({ say: "Alt-click a slot: selected, not loaded", act: (u, c) => { c.sel = k(c); if (c.sel == null) throw new Error("no slot to pick"); u.click(ks(c.sel), { alt: true }); }, screen: c => ok($1(ks(c.sel))?.getAttribute("aria-selected") === "true", "not selected") });
	const openKits = { say: "click KIT on the LCD", act: u => u.click("#kitf"), screen: () => ok(!$1("#libpop").hidden && LIB.open === "kit", "closed") };
	const otherKit = (c, not = []) => [...Array(64).keys()].reverse().find(k => k !== V.kit && !not.includes(k) && !kitEmpty(k) && kitLoaded(k) && kitName(k));
	const kitCopy = {
		name: "md-lib-kit-copy-paste-undo",
		steps: [
			openKits,
			libSelect(c => (c.src = [...Array(64).keys()].find(k => k !== V.kit && !kitEmpty(k) && kitName(k)))),
			{ say: "click Copy", act: u => u.click('#libpop [data-la="copy"]'), machine: c => ok(V.clipboard.kit === c.src, "clipboard " + V.clipboard.kit) },
			libSelect(c => { c.dst = [...Array(64).keys()].reverse().find(k => k !== V.kit && k !== c.src && kitLoaded(k) && kitName(k) !== kitName(c.src)); c.d0 = JSON.stringify(Docs.kits[c.dst]); return c.dst; }),
			{ say: "click Paste", act: u => u.click('#libpop [data-la="paste"]'), machine: c => ok(kitName(c.dst) === kitName(c.src) && same(Docs.kits[c.dst].tracks.map(t => t.machine), Docs.kits[c.src].tracks.map(t => t.machine)), "slot " + kitName(c.dst)), screen: c => ok($1(ks(c.dst))?.textContent.includes(kitName(c.src).trim()), "tile " + $1(ks(c.dst))?.textContent), within: 8000 },
			{ ...undoKey, say: "press Cmd+Z (the library open): the slot is back", machine: c => ok(JSON.stringify(Docs.kits[c.dst]) === c.d0, "slot " + kitName(c.dst)), within: 8000 },
			esc
		]
	};
	const kitRename = {
		name: "md-lib-kit-rename-undo",
		steps: [
			openKits,
			libSelect(c => { const k = otherKit(c); c.n0 = kitName(k); return k; }),
			{ say: "click Rename, type JOURNEY, press Enter and answer the question", act: async (u, c) => { u.click('#libpop [data-la="rename"]'); await sleep(200); u.type("#lsin", "JOURNEY"); await sleep(100); c.typed = `field "${$1("#lsin")?.value}", focus ${document.activeElement?.id || document.activeElement?.tagName}, renaming ${LIB.renaming}`; results.length = 0; traceDocs("kit", c.sel); u.key("Enter"); await sleep(50); c.typed += `; after Enter renaming ${LIB.renaming}`; if (await until(dlgShown, 2000)) { c.asked = $1("#dlg p")?.textContent; u.click($1("#dlg .danger")); c.note = "asked: " + c.asked; } },
				machine: c => ok(kitName(c.sel).trim() === "JOURNEY", "name " + kitName(c.sel) + "; before Enter: " + c.typed + "; results " + results.map(r => r.op + ":" + r.ok + " " + (r.errors || []).join(";") + (r.note || "")).join(", ") + "; tx " + desk().tx + "; documents: " + traced()), screen: c => ok($1(ks(c.sel))?.textContent.includes("JOURNEY"), "tile " + $1(ks(c.sel))?.textContent), within: 8000 },
			{ ...undoKey, machine: c => ok(kitName(c.sel) === c.n0, "name " + kitName(c.sel)), within: 8000 },
			esc
		]
	};
	const kitClear = {
		name: "md-lib-kit-clear-undo",
		steps: [
			openKits,
			libSelect(c => { const k = otherKit(c); c.d0 = JSON.stringify(Docs.kits[k]); return k; }),
			{ say: "press Delete: the slot is cleared", act: async u => { u.key("Delete"); if (await until(dlgShown, 1200)) u.click($1("#dlg .danger")); }, machine: c => ok(kitEmpty(c.sel), "slot " + kitName(c.sel)), screen: c => ok($1(ks(c.sel))?.textContent.includes("EMPTY"), "tile " + $1(ks(c.sel))?.textContent), within: 8000 },
			{ ...undoKey, machine: c => ok(JSON.stringify(Docs.kits[c.sel]) === c.d0, "slot not restored"), within: 8000 },
			esc
		]
	};
	const psq = p => `#libpop .ps[data-ps="${p}"]`;
	const patGo = {
		name: "md-lib-pattern-go",
		steps: [
			{ say: "click the pattern on the LCD: 128 patterns", act: (u, c) => { c.p0 = currentPatternSlot(); u.click("#pat"); }, screen: () => ok(!$1("#libpop").hidden && $all("#libpop .ps").length === 128, $all("#libpop .ps").length + " slots"), machine: () => ok(Object.keys(Docs.patterns).length === 128, Object.keys(Docs.patterns).length + " read") },
			{ say: "click another pattern (stopped: it switches)", act: async (u, c) => { c.p = (c.p0 + 2) % 128; u.click(psq(c.p)); await confirmIfAsked(u); }, machine: c => ok(currentPatternSlot() === c.p, "machine pattern " + currentPatternSlot()), screen: c => ok($1(psq(c.p))?.classList.contains("cur"), "not current"), within: 5000 },
			{ say: "click the first one again", act: async (u, c) => { u.click(psq(c.p0)); await confirmIfAsked(u); }, machine: c => ok(currentPatternSlot() === c.p0, "machine pattern " + currentPatternSlot()), within: 5000 },
			esc
		]
	};
	const patClear = {
		name: "md-lib-pattern-clear-undo",
		steps: [
			{ say: "open the pattern chooser and move to a pattern with trigs (arrows)", act: async (u, c) => { u.click("#pat"); await sleep(300); c.p = [...Array(128).keys()].find(p => p !== currentPatternSlot() && hasPat(p)); if (c.p == null) throw new Error("no other pattern with trigs"); c.d0 = JSON.stringify(Docs.patterns[c.p]); for (let n = 0; n < 128 && LIB.sel !== c.p; n++) { u.key("ArrowRight"); await sleep(20); } },
				screen: c => ok($1(psq(c.p))?.getAttribute("aria-selected") === "true", "not selected") },
			{ say: "click Clear and answer the question", act: async (u, c) => { results.length = 0; c.selNow = LIB.sel; traceDocs("pattern", c.p); u.click('#libpop [data-la="clear"]'); if (await until(dlgShown, 2000)) { c.note = "asked: " + $1("#dlg p")?.textContent; u.click($1("#dlg .danger")); } }, machine: c => ok(!hasPat(c.p), `still has trigs; selected ${c.selNow} (want ${c.p}); results ${results.map(r => r.op + ":" + r.ok + " " + (r.errors || []).join(";") + (r.note || "")).join(", ")}; tx ${desk().tx}; documents: ${traced()}`), screen: c => ok($1(psq(c.p))?.classList.contains("empty"), "tile not empty"), within: 8000 },
			{ ...undoKey, machine: c => ok(JSON.stringify(Docs.patterns[c.p]) === c.d0, "not restored"), within: 8000 },
			esc
		]
	};

	/* ---------- dialogs ---------- */
	const dialogEsc = {
		name: "md-dialog-esc-space-cancel",
		steps: [
			go("sound"), sel(() => soundTrack()),
			{ say: "drag a value: the kit has unsaved edits", act: async u => { const el = $1('#main .pc[data-g="syn"]'); await nudge(u, el, getV(el)); }, machine: () => ok(machineState().kit.working === "edited", machineState().kit.working) },
			{ say: "open the kit library and click another kit: the plug-in asks", act: async (u, c) => { c.k0 = currentKitSlot(); u.click("#kitf"); await sleep(300); c.k = [...Array(64).keys()].find(k => k !== c.k0 && !kitEmpty(k)); u.click(ks(c.k)); },
				screen: () => ok(dlgShown() && document.activeElement?.textContent.trim() === "Cancel", `dialog ${dlgShown()}, focus ${document.activeElement?.textContent}`), machine: c => ok(currentKitSlot() === c.k0, "loaded without asking") },
			{ say: "press Escape: Cancel, nothing loads", act: u => u.key("Escape"), screen: () => ok(!dlgShown(), "still open"), machine: c => ok(currentKitSlot() === c.k0 && machineState().kit.working === "edited", "machine kit " + currentKitSlot()), within: 2000 },
			{ say: "click the kit again and press Space: the focused Cancel", act: async (u, c) => { u.click(ks(c.k)); await until(dlgShown, 2000); u.key(" "); }, screen: () => ok(!dlgShown(), "still open"), machine: c => ok(currentKitSlot() === c.k0, "machine kit " + currentKitSlot()), within: 2000 }
		],
		async tidy(u) { if (LIB.open) u.key("Escape"); await sleep(200); u.key("z", { cmd: true }); await sleep(500); }
	};

	/* ---------- GLOBAL, AUDIO / MIDI, the engine menu ---------- */
	const tog = (f, v) => `#globpop [data-ga="${f}"][data-v="${v ? 1 : 0}"]`;
	const globalJ = {
		name: "md-global-tempo-out",
		steps: [
			{ say: "choose GLOBAL… in the engine menu", act: u => u.pick("engsel", "global"), screen: () => ok(!$1("#globpop").hidden, "panel closed"), machine: () => ok(!!Docs.global?.control, "no global") },
			{ say: "click TEMPO OUT", act: (u, c) => { c.v0 = Docs.global.control.tempoOut; u.click(tog("tempoOut", !c.v0)); }, machine: c => ok(Docs.global.control.tempoOut === !c.v0, "tempoOut " + Docs.global.control.tempoOut), screen: c => ok(pressed(tog("tempoOut", !c.v0)), "key not lit"), within: 6000 },
			{ say: "click it back", act: (u, c) => u.click(tog("tempoOut", c.v0)), machine: c => ok(Docs.global.control.tempoOut === c.v0, "tempoOut " + Docs.global.control.tempoOut), within: 6000 },
			esc
		]
	};
	const globalRouting = {
		name: "md-global-routing",
		steps: [
			{ say: "open GLOBAL: ROUTING fits the dialog", act: u => u.pick("engsel", "global"), screen: () => { const p = $1("#globpop").getBoundingClientRect(), keys = $all('#globpop [data-ga="route"]'); return ok(keys.length === 16 && keys.every(k => { const r = k.getBoundingClientRect(); return r.left >= p.left - 1 && r.right <= p.right + 1 && r.bottom <= p.bottom + 1; }), keys.length + " route keys"); } },
			{ say: "click track 1's output", act: (u, c) => { c.o0 = Docs.global.routing[0]; u.click('#globpop [data-ga="route"][data-t="0"]'); }, machine: c => ok(Docs.global.routing[0] !== c.o0, "routing " + Docs.global.routing[0]), within: 6000 },
			{ say: "click it until it is back", act: async (u, c) => { for (let k = 0; k < 8 && Docs.global.routing[0] !== c.o0; k++) { u.click('#globpop [data-ga="route"][data-t="0"]'); await sleep(800); } }, machine: c => ok(Docs.global.routing[0] === c.o0, "routing " + Docs.global.routing[0]) },
			esc
		]
	};
	/* 0.3.5: the MAP EDITOR's whole range (16-143 patterns A01-H16, 144 START, 145 STOP: measured on the firmware), the
	   values a backup brings (B-019) shown and kept, not snapped back to 0-31. */
	const kmSel = '.kselbtn[data-for="gmapsel"]';
	const globalMapNote = {
		name: "md-global-map-note",
		steps: [
			{ say: "open GLOBAL", act: u => u.pick("engsel", "global"), screen: () => ok(!!$1(kmSel), "no Map a note key"), machine: () => ok(!!Docs.global?.keymap, "no global") },
			{ say: "click › beside Map a note", act: (u, c) => { c.n = GP.note + 1; c.v0 = Docs.global.keymap[c.n]; u.click('#globpop [data-ga="mapnote"][data-d="1"]'); }, screen: c => ok(GP.note === c.n && $1(kmSel)?.textContent === KTGT(c.v0), "note " + GP.note + ", " + $1(kmSel)?.textContent) },
			{ say: "choose a pattern of bank C or later for it", act: async (u, c) => { c.p = [...Array(112).keys()].map(k => 143 - k).find(v => !Docs.global.keymap.includes(v)); await u.pick("gmapsel", String(c.p)); },
				machine: c => ok(Docs.global.keymap[c.n] === c.p, "keymap " + Docs.global.keymap[c.n]), screen: c => ok($1(kmSel)?.textContent === KTGT(c.p) && c.p > 47, $1(kmSel)?.textContent), within: 6000 },
			{ say: "choose STOP", act: async (u, c) => { c.stopAt = Docs.global.keymap.indexOf(145); await u.pick("gmapsel", "145"); },
				machine: c => ok(Docs.global.keymap[c.n] === 145, "keymap " + Docs.global.keymap[c.n]), screen: c => ok($1(kmSel)?.textContent === "STOP" && $all("#globpop .note").some(n => n.textContent.includes("STOP")), $1(kmSel)?.textContent), within: 6000 }
		],
		async tidy(u, c) {
			if ($1("#globpop").hidden) { await u.pick("engsel", "global"); await sleep(300); }
			GP.note = c.n; drawGlobal(); await sleep(100);
			if (c.n != null && Docs.global.keymap[c.n] !== c.v0) { await u.pick("gmapsel", c.v0 == null ? "" : String(c.v0)); await sleep(1500); }
			if (c.stopAt >= 0 && Docs.global.keymap[c.stopAt] !== 145) { GP.note = c.stopAt; drawGlobal(); await sleep(100); await u.pick("gmapsel", "145"); await sleep(1500); }
			GP.note = 64; u.key("Escape"); await sleep(200);
		}
	};
	const audioPanel = {
		name: "md-audio-panel",
		steps: [
			{ say: "press , : AUDIO / MIDI opens", act: u => { document.activeElement?.blur?.(); u.key(","); }, screen: () => ok(!$1("#audiopop").hidden && $1("#audiopop").textContent.length > 40, "closed") },
			{ say: "press Escape", act: u => u.key("Escape"), screen: () => ok($1("#audiopop").hidden, "open") }
		]
	};
	const hwNoMachine = {
		name: "md-engine-hw-no-machine",
		steps: [
			{ say: "choose HW MIDI in the engine menu (no Machinedrum attached)", act: u => u.pick("engsel", "hw"), screen: () => ok(!$1("#bootmidi").hidden && document.body.classList.contains("deskmidi"), "no card"), machine: () => ok(machineState().capabilities?.engine === "hw", "engine " + machineState().capabilities?.engine), within: 10000 },
			{ say: "the card says no machine answers", screen: () => ok($1("#bootmidi").classList.contains("lost") && !$1('[data-bootmidi="emu"]').hidden, $1("#bmt")?.textContent), within: 20000 },
			{ say: "click Use the emulator", act: u => u.click('[data-bootmidi="emu"]'), screen: () => ok($1("#bootmidi").hidden, "card shown"), machine: () => ok(machineState().capabilities?.engine === "emu" && !!machineState().input && V.loaded, "engine " + machineState().capabilities?.engine), within: 40000 }
		],
		async tidy() { if (machineState().capabilities?.engine !== "emu") { const s = $1("#engsel"); s.value = "emu"; s.dispatchEvent(new Event("change", { bubbles: true })); await until(() => !!machineState().input, 40000); } await until(() => !desk().loading, 60000); }
	};
	const romCard = {
		name: "md-engine-rom-card",
		steps: [
			{ say: "choose LOAD ROM: the card shows the firmware in use", act: u => u.pick("engsel", "rom"), screen: () => ok(!$1("#bootcard").hidden && /1\.63/.test($1("#bootcur")?.textContent || ""), "card " + $1("#bootcur")?.textContent), within: 5000 },
			{ say: "click Close", act: u => u.click('#bootcard [data-bootrom="close"]'), screen: () => ok($1("#bootcard").hidden, "still shown"), machine: () => ok(!!machineState().input, "the machine stopped") }
		]
	};
	const notePlay = {
		name: "md-keys-play-notes",
		steps: [
			go("seq"), sel(() => soundTrack()),
			{ say: "press A on the keyboard: the track plays", act: u => { results.length = 0; document.activeElement?.blur?.(); u.key("a"); }, machine: () => ok(results.some(r => r.op === "noteOn" && r.ok), "results " + results.map(r => r.op + ":" + r.ok).join(",")) }
		]
	};

	/* ---------- more: the lock lane's comforts, paste to many, MUTATE's scope, the Song inspector and drag, RAM, PAN ---------- */
	const undoUntil = async (u, done, n = 6) => { for (let i = 0; i < n && !done(); i++) { u.key("z", { cmd: true }); await until(() => idle() && done(), 1500); } };
	const laneSteps = (t, i) => locksOf(t, i).map(([s]) => s);
	const lockRamp = {
		name: "md-seq-lock-ramp-erase-wheel",
		steps: [
			go("seq"),
			{ say: "pick a track with three trigs on the first page and the FLTF lock key", act: async (u, c) => { c.t = soundTracks().find(t => trigsOf(t).filter(s => s < 16).length >= 3) ?? soundTrack(); u.click(rail(c.t)); await sleep(200); const k = $1('#chips .pk[data-lane="FLTF"]') || $1("#chips .pk[data-lane]"); c.p = k.dataset.lane; u.click(k); await sleep(200); c.i = pidx(c.t, c.p); c.l0 = locksOf(c.t, c.i); c.on = trigsOf(c.t).filter(s => s < 16).slice(0, 3); },
				screen: c => ok(S.sel === c.t && S.lane === c.p, "lane " + S.lane), machine: c => ok(c.on.length === 3, "trigs " + trigsOf(c.t).join(",")) },
			{ say: "Shift-drag across the lane: a ramp over the trigs", act: async (u, c) => { await u.drag(`#lane .lb[data-s="${c.on[0]}"]`, [`#lane .lb[data-s="${c.on[1]}"]`, `#lane .lb[data-s="${c.on[2]}"]`], { shift: true }, { fy: 0.8 }); },
				machine: c => { const l = locksOf(c.t, c.i), v = s => l.find(([x]) => x === s)?.[1]; return ok(c.on.every(s => v(s) != null) && v(c.on[0]) !== v(c.on[2]), "locks " + JSON.stringify(l)); }, screen: c => ok(c.on.every(s => $1(`#lane .lb[data-s="${s}"] i`)), "no bars"), within: 8000 },
			{ say: "Alt-drag across the same steps: the locks are erased", act: async (u, c) => { await u.drag(`#lane .lb[data-s="${c.on[0]}"]`, [`#lane .lb[data-s="${c.on[1]}"]`, `#lane .lb[data-s="${c.on[2]}"]`], { alt: true }); },
				machine: c => ok(c.on.every(s => !laneSteps(c.t, c.i).includes(s)), "locks " + JSON.stringify(locksOf(c.t, c.i))), screen: c => ok(c.on.every(s => !$1(`#lane .lb[data-s="${s}"] i`)), "bars left"), within: 8000 },
			{ say: "scroll the wheel up over a trig's step: its lock moves up from the kit value", act: (u, c) => { c.base = grp(c.t, c.p)[c.p] ?? 0; u.wheel(cell(c.t, c.on[0]), 2); },
				machine: c => { const v = locksOf(c.t, c.i).find(([x]) => x === c.on[0])?.[1]; return ok(v === Math.min(127, c.base + 8), `lock ${v}, kit value ${c.base}`); }, within: 8000 }
		],
		async tidy(u, c) { await undoUntil(u, () => same(locksOf(c.t, c.i), c.l0)); }
	};
	const pasteMany = {
		name: "md-seq-paste-many",
		steps: [
			go("seq"),
			{ say: "pick a track with trigs and press Cmd+C", act: async (u, c) => { const st = soundTracks(); c.a = st.find(t => trigsOf(t).length) ?? 0; c.bs = st.filter(t => t !== c.a && !same(trigsOf(t), trigsOf(c.a))).slice(0, 2); if (c.bs.length < 2) throw new Error("no two other tracks"); c.b0 = c.bs.map(trigsOf); c.ta = trigsOf(c.a); u.click(rail(c.a)); await sleep(200); document.activeElement?.blur?.(); u.key("c", { cmd: true }); }, machine: () => ok(V.clipboard.steps, "nothing copied") },
			{ say: "Shift-click two other tracks' headers: marked", act: async (u, c) => { for (const t of c.bs) { u.click(rail(t), { shift: true }); await sleep(150); } }, screen: c => ok(c.bs.every(t => $1(`#rail .th[data-sel="${t}"]`)?.classList.contains("multi")), "not marked") },
			{ say: "press Cmd+V: both get the steps", act: u => u.key("v", { cmd: true }), machine: c => ok(c.bs.every((t, k) => same(trigsOf(t).filter(inLen), c.ta.filter(inLen)) && same(trigsOf(t).filter(s => !inLen(s)), c.b0[k].filter(s => !inLen(s)))), c.bs.map(t => `T${t + 1} ${trigsOf(t).join(",")}`).join("; ")), within: 8000 },
			{ ...undoKey, say: "press Cmd+Z once: both are back (one undo step)", machine: c => ok(c.bs.every((t, k) => same(trigsOf(t), c.b0[k])), c.bs.map(t => `T${t + 1} ${trigsOf(t).join(",")}`).join("; ")), within: 8000 }
		],
		async tidy(u, c) { if (S.multi?.size) { u.key("Escape"); await sleep(100); } if (c.bs) await undoUntil(u, () => c.bs.every((t, k) => same(trigsOf(t), c.b0[k])), 3); }
	};
	const mutScope = {
		name: "md-gen-mutate-scope",
		steps: [
			go("sound"), sel(() => soundTrack()),
			{ say: "click MUTATE's Fx chip on and Syn off", act: async (u, c) => { c.scope0 = [...S.mut.scope]; c.amt0 = S.mut.amount; if (!S.mut.scope.has("fx")) u.click('#mutband [data-mutg="fx"]'); await sleep(100); if (S.mut.scope.has("syn")) u.click('#mutband [data-mutg="syn"]'); await sleep(100); },
				screen: () => ok(pressed('#mutband [data-mutg="fx"]') && !pressed('#mutband [data-mutg="syn"]'), "chips " + [...S.mut.scope].join(",")) },
			{ say: "click the Amount value: one more", act: u => u.click('#mutband .gv[data-gv="amt"]'), screen: c => ok($1('#mutband .gv[data-gv="amt"] b')?.textContent === (c.amt0 + 1) + "%", "shows " + $1('#mutband .gv[data-gv="amt"] b')?.textContent) },
			{ say: "click MUTATE's Random key: only the effects move", act: (u, c) => { c.k0 = kitVals(c.t); u.click("#mutband [data-rand]"); },
				machine: c => { const k = kitVals(c.t); return ok(!same(k.slice(8, 16), c.k0.slice(8, 16)) && same(k.slice(0, 8), c.k0.slice(0, 8)) && same(k.slice(16), c.k0.slice(16)), `synth ${same(k.slice(0, 8), c.k0.slice(0, 8)) ? "same" : "moved"}, effects ${same(k.slice(8, 16), c.k0.slice(8, 16)) ? "same" : "moved"}, routing ${same(k.slice(16), c.k0.slice(16)) ? "same" : "moved"}`); }, within: 8000 },
			{ ...undoKey, machine: c => ok(same(kitVals(c.t), c.k0), "kit not back"), within: 8000 }
		],
		async tidy(u, c) { if (!c.scope0) return; for (const g of ["syn", "fx", "rt"]) if (S.mut.scope.has(g) !== c.scope0.includes(g)) { u.click(`#mutband [data-mutg="${g}"]`); await sleep(80); } if (S.mut.amount !== c.amt0) u.click('#mutband .gv[data-gv="amt"]', { shift: true }); }
	};
	const songInspector = {
		name: "md-song-row-inspector",
		steps: [
			go("song"),
			{ say: "add a row (Arrange, pad A01): it is selected", act: async (u, c) => { u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); c.r0 = JSON.stringify(songRows()); c.n0 = songRows().length; u.click('[data-addpat="0"]'); await until(() => songRows().length === c.n0 + 1, 4000); c.i = S.songSel; },
				machine: c => ok(songRows().length === c.n0 + 1 && songRows()[c.i]?.kind === "pattern", "rows " + songRows().length) },
			{ say: "click Repeat +", act: (u, c) => { c.rep = songRows()[c.i].repeats; u.click('[data-step="rep"][data-d="1"]'); }, machine: c => ok(songRows()[c.i].repeats === c.rep + 1, "repeats " + songRows()[c.i].repeats), screen: c => ok($1('[data-step="rep"][data-d="1"]')?.previousElementSibling?.textContent === String(c.rep + 2), "shows " + $1('[data-step="rep"][data-d="1"]')?.previousElementSibling?.textContent) },
			{ say: "click More and mute track 1 for the row", act: async u => { u.click("[data-rowmore]"); await sleep(250); u.click('[data-rowmute="0"]'); }, machine: c => ok((songRows()[c.i].mutes || []).includes(0), "mutes " + JSON.stringify(songRows()[c.i].mutes)), screen: () => ok($1('[data-rowmute="0"]')?.classList.contains("off"), "key not off") },
			{ say: "click Add loop: a loop row after it", act: u => u.click('[data-rowact="loop"]'), machine: c => ok(songRows()[c.i + 1]?.kind === "loop", "row " + JSON.stringify(songRows()[c.i + 1])), screen: () => ok($all("#tl .scell.loop").length >= 1, "no loop cell") }
		],
		async tidy(u, c) { if (S.songMore) u.click("[data-rowmore]"); if (c.r0) await undoUntil(u, () => JSON.stringify(songRows()) === c.r0, 8); }
	};
	const songDrag = {
		name: "md-song-drag-drop",
		steps: [
			go("song"),
			{ say: "drag pad A02 onto the empty end of the grid: a row is appended", act: async (u, c) => { u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); c.r0 = JSON.stringify(songRows()); c.n0 = songRows().length; await u.dragDrop('[data-addpat="1"]', `#tl .scell[data-i="${c.n0 + 1}"]`); },
				machine: c => ok(songRows().length === c.n0 + 1 && songRows()[c.n0 - 1]?.pattern === 1, "rows " + songRows().map(r => r.kind === "pattern" ? r.pattern : r.kind).join(",")), within: 6000 },
			{ say: "drag that row onto the first cell: it moves there", act: async (u, c) => { await u.dragDrop(`#tl .scell[data-i="${c.n0 - 1}"]`, '#tl .scell[data-i="0"]'); },
				machine: () => ok(songRows()[0]?.pattern === 1, "rows " + songRows().map(r => r.kind === "pattern" ? r.pattern : r.kind).join(",")), within: 6000 }
		],
		async tidy(u, c) { if (c.r0) await undoUntil(u, () => JSON.stringify(songRows()) === c.r0, 6); }
	};
	const ramView = {
		name: "md-sampler-ram-view",
		steps: [
			go("sampler"),
			{ say: "set up sampling in an unused RAM slot", act: async (u, c) => { c.n = [1, 2, 3, 4].find(k => !V.tracks.some(t => t.m === "RAM-R" + k || t.m === "RAM-P" + k)); if (!c.n) throw new Error("every RAM slot is used"); u.click(`.slotk[data-slot="RAM${c.n}"]`); await sleep(300); [c.r, c.p] = setupTracks(); c.m0 = [kit().tracks[c.r].machine, kit().tracks[c.p].machine]; c.t0 = trigsOf(c.r); u.click("[data-setupgo]"); },
				machine: c => ok(kit().tracks[c.r].machine === "RAM-R" + c.n, "track " + kit().tracks[c.r].machine), screen: () => ok(!!$1("[data-rc]") && !!$1('[data-slotmode="frozen"]'), "no RAM view"), within: 8000 },
			{ say: "click an empty step of the RAM view: a recorder trig", act: (u, c) => { c.s = [...Array(16).keys()].find(s => !trigsOf(c.r).includes(s)); u.click(`[data-rc="${c.s}"]`); }, machine: c => ok(trigsOf(c.r).includes(c.s), "recorder trigs " + trigsOf(c.r).join(",")), screen: c => ok(pressed(`[data-rc="${c.s}"]`), "not lit") },
			{ say: "click Freeze: the recorder is muted on the machine", act: u => u.click('[data-slotmode="frozen"]'), machine: c => ok(mutes().includes(c.r), "machine mutes " + mutes()) },
			{ say: "click Live: it records again", act: u => u.click('[data-slotmode="live"]'), machine: c => ok(!mutes().includes(c.r), "machine mutes " + mutes()) }
		],
		async tidy(u, c) { if (c.m0) await undoUntil(u, () => same([kit().tracks[c.r].machine, kit().tracks[c.p].machine], c.m0) && same(trigsOf(c.r), c.t0), 4); }
	};
	/* B-025: chops right after Set up sampling, the machine stopped: each is a pattern dump over the current pattern,
	   which reloads the kit from its slot; the unsaved RAM-R / RAM-P must come back after every one. */
	const setupChop = {
		name: "md-sampler-setup-chop",
		steps: [
			go("sampler"),
			{ say: "set up sampling in an unused RAM slot", act: async (u, c) => { c.n = [1, 2, 3, 4].find(k => !V.tracks.some(t => t.m === "RAM-R" + k || t.m === "RAM-P" + k)); if (!c.n) throw new Error("every RAM slot is used"); u.click(`.slotk[data-slot="RAM${c.n}"]`); await sleep(300); [c.r, c.p] = setupTracks(); c.m0 = [kit().tracks[c.r].machine, kit().tracks[c.p].machine]; c.t0 = trigsOf(c.p); u.click("[data-setupgo]"); },
				machine: c => ok(kit().tracks[c.r].machine === "RAM-R" + c.n && kit().tracks[c.p].machine === "RAM-P" + c.n, `tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.p].machine}`), within: 8000 },
			{ say: "click up to eight chop cells quickly", act: async (u, c) => { c.s = $all("#chop [data-cp]").map(e => +e.dataset.cp).filter(s => !trigsOf(c.p).includes(s)).slice(0, 8); c.done = []; for (const s of c.s) { const q = `#chop [data-cp="${s}"]`; await until(() => !!$1(q), 1000); if (!$1(q)) continue; u.click(q); c.done.push(s); await sleep(90); } },
				machine: c => ok(c.done.length >= 2 && c.done.every(s => trigsOf(c.p).includes(s)), "clicked " + c.done + ", player trigs " + trigsOf(c.p)), within: 8000 },
			{ say: "wait: the kit keeps RAM-R and RAM-P", act: () => sleep(4000),
				machine: c => ok(kit().tracks[c.r].machine === "RAM-R" + c.n && kit().tracks[c.p].machine === "RAM-P" + c.n, `tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.p].machine}`), within: 2000 }
		],
		async tidy(u, c) { if (c.m0) await undoUntil(u, () => same([kit().tracks[c.r].machine, kit().tracks[c.p].machine], c.m0) && same(trigsOf(c.p), c.t0), 8); }
	};
	const panBox = {
		name: "md-mix-pan-undo",
		steps: [
			go("mix"),
			{ say: "drag a track's PAN box sideways", act: async (u, c) => { c.t = soundTrack(); c.i = pidx(c.t, "PAN", "rt"); c.v0 = kitVals(c.t)[c.i]; await nudge(u, `.strip[data-sel="${c.t}"] .pc[data-n="PAN"]`, c.v0); },
				machine: c => ok(kitVals(c.t)[c.i] !== c.v0, "kit PAN " + kitVals(c.t)[c.i]), screen: c => ok($1(`.strip[data-sel="${c.t}"] .pc[data-n="PAN"] b`)?.textContent !== String(c.v0 - 64), "shows " + $1(`.strip[data-sel="${c.t}"] .pc[data-n="PAN"] b`)?.textContent) },
			{ ...undoKey, machine: c => ok(kitVals(c.t)[c.i] === c.v0, "kit PAN " + kitVals(c.t)[c.i]) }
		]
	};


	/* Screenshots of the key map's views (scripts/mdmm-shots.sh): each step sets a view up and logs "SHOT <name>", then
	   holds while the script captures the window by its id. Only when asked by name (journey-md-shots): skipped in a
	   run of every journey. */
	const shot = name => { Bridge.log("SHOT " + name); };
	const hold = 2500;
	const kvChip = mod => `#keyspop .kv-chip[data-kvmod="${mod}"]`;
	const shots = {
		name: "md-shots", needs: () => /md-shots/.test(location.search) ? null : "screenshots only when asked by name",
		steps: [
			go("seq"), sel(() => soundTracks().find(t => t < 14 && trigsOf(t).length >= 3) ?? soundTrack()),
			{ say: "Cmd-drag a block over three tracks: a selection", act: async (u, c) => { if (S.stepSel) clearSel(); await u.drag(cell(c.t, 4), [cell(c.t, 6), cell(c.t + 2, 11)], { cmd: true }); await sleep(400); shot("app-7-selection"); },
				screen: () => ok(!!S.stepSel, "no selection"), hold },
			{ say: "press ?: the keyboard view, R clicked", act: async u => { clearSel(); document.activeElement?.blur?.(); u.key("?", { shift: true }); await sleep(300); u.click('#keyspop .kv-cap[data-code="KeyR"]'); await sleep(300); shot("app-1-base"); },
				screen: () => ok(!$1("#keyspop").hidden, "no view"), hold },
			{ say: "the ⇧ layer, → clicked", act: async u => { u.click(kvChip("shift")); await sleep(200); u.click('#keyspop .kv-cap[data-code="ArrowRight"]'); await sleep(300); shot("app-2-shift"); }, hold },
			{ say: "the ⌥ / FN layer, R clicked", act: async u => { u.click(kvChip("shift")); u.click(kvChip("alt")); await sleep(200); u.click('#keyspop .kv-cap[data-code="KeyR"]'); await sleep(300); shot("app-3-alt-fn"); }, hold },
			{ say: "the ⌘ layer, A clicked", act: async u => { u.click(kvChip("alt")); u.click(kvChip("cmd")); await sleep(200); u.click('#keyspop .kv-cap[data-code="KeyA"]'); await sleep(300); shot("app-4-cmd"); }, hold },
			{ say: "the mouse's tricks", act: async () => { const p = $1("#keyspop"), c = p.querySelector(".kv-cols"); p.scrollTop = c.offsetTop - 8; await sleep(300); shot("app-5-mouse-tricks"); }, hold },
			{ say: "Esc, then right-click a step with a trig: its menu", act: async (u, c) => {
				u.key("Escape"); await sleep(300);
				const s = trigsOf(c.t).find(x => x < Math.min(V.length, V.len)) ?? 0; u.rightClick(cell(c.t, s)); await sleep(400); shot("app-6-stepmenu");
			}, screen: () => ok(!$1("#deskmenu").hidden, "no menu"), hold },
			{ say: "Esc: the menu closes", act: async u => { u.key("Escape"); await sleep(200); if (S.stepSel) clearSel(); shot("done"); } }
		],
		async tidy() { if (!$1("#deskmenu")?.hidden) closeDeskMenu(); if (!$1("#keyspop").hidden) toggleKeys(false); if (S.stepSel) clearSel(); }
	};
	/* B-019: a .syx imported as from a cable. The file is the run's (GEARMULATOR_MDMM_SYX_FILE, diagnostics builds: the
	   plug-in opens it where the chooser would be). Kits only; afterwards every kit the report does not list (taken as
	   in the file) has the file's name on the machine. It writes the machine's kits and an import has no Undo: last. */
	const syxKitIds = () => $all('#syxpop [data-syxitem^="kit:"]');
	const syxImportJ = {
		name: "md-lib-syx-import",
		needs: () => new URLSearchParams(location.search).get("syxfile") ? null : "no .syx for the run (GEARMULATOR_MDMM_SYX_FILE)",
		steps: [
			openKits,
			{ say: "click Import SysEx…: the file's preview", act: u => u.click('#libpop [data-syx="import"]'), screen: () => ok(!$1("#syxpop").hidden && syxKitIds().length > 0, "no preview with kits"), within: 8000 },
			{ say: "leave Kits ticked only, click Import: sent, read back, reported", act: (u, c) => {
				for (const b of $all("#syxpop [data-syxkind]")) if (b.checked !== (b.dataset.syxkind === "kit")) u.click(b);
				c.names = syxKitIds().map(b => [+b.dataset.syxitem.split(":")[1], b.textContent.replace(/^▶ /, "").trim()]);
				u.click('#syxpop [data-syxgo="start"]');
			}, screen: () => ok(/imported/.test($1("#syxpop .syxsum")?.textContent || ""), "progress: " + ($1("#syxpop .syxbar span")?.textContent || "")),
			machine: c => {
				const notTaken = new Set($all("#syxpop .syxreport .syxprob div").map(d => (/^kit (\d+)/.exec(d.textContent) || [])[1]).filter(Boolean).map(n => +n - 1));
				const off = c.names.filter(([k, n]) => !notTaken.has(k) && n !== "(no name)" && (kitName(k) || "").trim() !== n);
				return ok(!off.length, off.length + " kits not as in the file: " + off.slice(0, 4).map(([k, n]) => `K${k + 1} "${kitName(k)}" not "${n}"`).join(", "));
			}, within: 180000 },
			{ say: "click Done: the panel closes", act: u => u.click('#syxpop [data-syxgo="close"]'), screen: () => ok($1("#syxpop").hidden, "still open") },
			esc
		]
	};
	/* B-026: import a file with its globals, then mute on the rail and play: the machine must hold the mute (its memory,
	   not the page's wish). With the file's base channel OFF the M is refused, with the reason, and never lit. The file
	   is the run's (GEARMULATOR_MDMM_SYX_FILE; mdDeskFirmwareTest syxexport with SYX_BASE_CHANNEL makes one). */
	const syxImportMute = {
		name: "md-lib-syx-import-mute",
		needs: () => new URLSearchParams(location.search).get("syxfile") ? null : "no .syx for the run (GEARMULATOR_MDMM_SYX_FILE)",
		steps: [
			openKits,
			{ say: "click Import SysEx…, tick Globals too", act: async u => { u.click('#libpop [data-syx="import"]'); await until(() => !$1("#syxpop").hidden && $all("#syxpop [data-syxkind]").length, 8000); const g = $1('#syxpop [data-syxkind="global"]'); if (g && !g.checked) u.click(g); },
				screen: () => ok(!$1("#syxpop").hidden && !!$1('#syxpop [data-syxkind="global"]')?.checked, "no Globals in the preview"), within: 8000 },
			{ say: "click Import: sent, read back, reported", act: u => u.click('#syxpop [data-syxgo="start"]'), screen: () => ok(/imported/.test($1("#syxpop .syxsum")?.textContent || ""), "progress: " + ($1("#syxpop .syxbar span")?.textContent || "")), within: 180000 },
			{ say: "click Done, then the Sequence tab", act: async u => { u.click('#syxpop [data-syxgo="close"]'); await sleep(300); u.key("Escape"); await sleep(300); u.click(tab("seq")); },
				screen: () => onTab("seq"), machine: () => ok(!!Docs.global && desk().mutesSource === "memory", "global " + !!Docs.global + ", mutes from " + desk().mutesSource), within: 8000 },
			{ say: "click M on a track of the rail: its mute flips on the machine", act: (u, c) => { c.off = Docs.global.baseChannel > 12; c.m0 = mutes(); c.t = soundTracks().find(t => !c.m0.includes(t)) ?? soundTrack(); c.want = !c.m0.includes(c.t); u.click(railM(c.t)); },
				machine: c => ok(c.off ? same(mutes(), c.m0) : mutes().includes(c.t) === c.want, `base ${Docs.global.baseChannel}, track ${c.t}, machine mutes ${mutes()} (before ${c.m0})`),
				screen: c => ok(pressed(railM(c.t)) === (c.off ? c.m0.includes(c.t) : c.want), "M " + (pressed(railM(c.t)) ? "lit" : "not lit")), within: 6000 },
			{ say: "press PLAY: the mute holds", act: u => { tele.steps = []; u.click("#play"); },
				machine: c => ok(new Set(tele.steps).size >= 4 && (c.off ? same(mutes(), c.m0) : mutes().includes(c.t) === c.want), `machine mutes ${mutes()}, steps ${tele.steps.length}`),
				screen: c => ok(pressed(railM(c.t)) === (c.off ? c.m0.includes(c.t) : c.want), "M " + (pressed(railM(c.t)) ? "lit" : "not lit")), within: 8000 },
			{ say: "press STOP", act: u => u.click("#play"), machine: () => ok(tele.last && !tele.last.playing, "still playing") },
			/* B-031: after an import the pattern still changes, stopped and playing */
			{ say: "click › next to the pattern: the machine selects it (stopped)", act: async (u, c) => { c.p0 = currentPatternSlot(); u.click("#patNext"); await confirmIfAsked(u); },
				machine: c => ok(currentPatternSlot() === c.p0 + 1, "machine pattern " + currentPatternSlot()), within: 6000 },
			{ say: "press PLAY, click ‹: the machine moves to it at the pattern's end (playing)", act: async u => { u.click("#play"); await sleep(500); u.click("#patPrev"); await confirmIfAsked(u); },
				machine: c => ok(currentPatternSlot() === c.p0 && V.playing, "machine pattern " + currentPatternSlot() + (V.playing ? ", playing" : ", stopped")), within: 15000 },
			{ say: "press STOP", act: u => u.click("#play"), machine: () => ok(tele.last && !tele.last.playing, "still playing") }
		],
		async tidy(u, c) { if (V.playing) u.click("#play"); await sleep(300); if (c.t != null && !c.off && mutes().includes(c.t) !== c.m0.includes(c.t)) u.click(railM(c.t)); await sleep(500); }
	};
	/* 0.3.5: the Song page's PATTERN | SONG switch: what is lit is the status the machine reports */
	const songModeJ = {
		name: "md-song-mode",
		steps: [
			go("song"),
			{ say: "click SONG next to Plays: the machine reports song mode", act: (u, c) => { c.m0 = machineState().songMode === true; u.click('[data-seqmode="song"]'); },
				machine: () => ok(machineState().songMode === true, "songMode " + machineState().songMode), screen: () => ok(pressed('[data-seqmode="song"]') && !pressed('[data-seqmode="pattern"]'), "SONG not lit"), within: 6000 },
			{ say: "click PATTERN: the machine reports pattern mode", act: u => u.click('[data-seqmode="pattern"]'),
				machine: () => ok(machineState().songMode === false, "songMode " + machineState().songMode), screen: () => ok(pressed('[data-seqmode="pattern"]') && !pressed('[data-seqmode="song"]'), "PATTERN not lit"), within: 6000 }
		],
		async tidy(u, c) { if (c.m0 && machineState().songMode !== true) { u.click('[data-seqmode="song"]'); await sleep(1500); } }
	};
	/* 0.3.5: the Song page's playhead. An empty song slot (the song card's ‹ ›) gets two rows, A01 and A02; SONG mode,
	   PLAY: the arrangement marks the row the machine plays (telemetry songRow, RAM), the What plays line and the LCD
	   say it, and the mark moves on to the next row; STOP takes it away. The steps are shared with the screenshots. */
	const emptySong = () => { const cur = V.songSlot; for (let d = 1; d < 32; d++) for (const s of [(cur + d) % 32, (cur - d + 32) % 32]) { const g = Docs.songs[s]; if (g && g.rows.length === 1 && g.rows[0].kind === "end") return s; } return null; };
	const songTo = async (u, s) => { for (let n = 0; n < 40 && V.songSlot !== s; n++) { const was = V.songSlot; u.click(`[data-songslot="${(s - was + 32) % 32 <= 16 ? 1 : -1}"]`); await until(() => V.songSlot !== was, 3000); } };
	const marked = c => { const i = $1("#tl .scell.ph")?.dataset.i; if (i != null) c.seen.add(+i); return [...c.seen].join(","); };
	const songPlayheadSteps = shot => [
		go("song"),
		{ say: "stop, then pick an empty song with the song card's ‹ ›", act: async (u, c) => { if (V.playing) { u.click("#play"); await until(() => !V.playing, 3000); } c.s0 = V.songSlot; c.m0 = V.songMode; await until(() => Object.keys(Docs.songs).length >= 32, 20000); c.s = emptySong(); if (c.s == null) throw new Error("no empty song slot"); await songTo(u, c.s); },
			screen: c => ok(V.songSlot === c.s, "song " + (V.songSlot + 1)), machine: c => ok(songSlotOf(Docs) === c.s, "machine song " + (songSlotOf(Docs) + 1)), within: 20000 },
		{ say: "Arrange: click pads A01 and A02, two rows", act: async u => { u.click('[data-set="songpick"] button[data-v="arrange"]'); await sleep(200); u.click('[data-addpat="0"]'); await until(() => songRows().length === 2, 4000); u.click('[data-addpat="1"]'); },
			machine: () => ok(songRows().length === 3 && songRows()[0].pattern === 0 && songRows()[1].pattern === 1, "rows " + songRows().map(r => r.kind === "pattern" ? r.pattern : r.kind).join(",")), within: 8000 },
		{ say: "click PATTERN: the arrangement is dimmed (not playing)", act: async u => { u.click('[data-seqmode="pattern"]'); await until(() => V.songMode === false, 4000); await sleep(400); shot("song-1-pattern-mode"); },
			screen: () => ok(!!$1(".songui.patmode") && pressed('[data-seqmode="pattern"]') && /^PATTERN /.test($1("#songPlays")?.textContent || ""), "plays " + $1("#songPlays")?.textContent), machine: () => ok(V.songMode === false, "songMode " + V.songMode) },
		{ say: "click SONG: the arrangement is lit", act: async u => { u.click('[data-seqmode="song"]'); await until(() => V.songMode === true, 4000); await sleep(400); shot("song-2-song-mode-stopped"); },
			screen: () => ok(!!$1(".songui.songmode") && pressed('[data-seqmode="song"]') && $1("#lcd2 [data-l2=seqmode] b")?.textContent === "SONG", "LCD " + $1("#lcd2 [data-l2=seqmode]")?.textContent), machine: () => ok(V.songMode === true, "songMode " + V.songMode) },
		{ say: "click Reload song when the edits ask for it, then PLAY: row 001 is marked", act: async (u, c) => { c.seen = new Set(); if ($1("[data-reloadsong]")) { u.click("[data-reloadsong]"); await until(() => !$1("[data-reloadsong]"), 3000); await sleep(300); } if (!V.playing) u.click("#play"); },
			screen: c => ok(marked(c).length > 0 && c.seen.has(0) && /row 001 of 2/.test($1("#songPlays")?.textContent || ""), `marked ${[...c.seen]}; plays ${$1("#songPlays")?.textContent}`), machine: () => ok(tele.last?.playing && tele.last.songRow === 0, `songRow ${tele.last?.songRow}, rows seen ${tele.rows.join(" ")}`), within: 8000 },
		{ say: "the mark moves on to row 002 (the LCD reads 01·002)", act: async (u, c) => { await until(() => (marked(c), c.seen.has(1)), 30000); await sleep(150); shot("song-3-song-playing"); },
			screen: c => ok((marked(c), c.seen.has(1)) && /·002$/.test($1("#pat")?.textContent || ""), `marked ${[...c.seen]}; LCD ${$1("#pat")?.textContent}`), machine: () => ok(tele.last?.songRow === 1, "songRow " + tele.last?.songRow), within: 30000 },
		{ say: "press STOP (or the song ends): no row is marked", act: u => { if (V.playing) u.click("#play"); }, screen: () => ok(!$1("#tl .scell.ph"), "still marked"), machine: () => ok(!V.playing, "playing") }
	];
	const songPlayheadTidy = async (u, c) => {
		if (V.playing) { u.click("#play"); await until(() => !V.playing, 3000); }
		if (c.s != null && V.songSlot === c.s) await undoUntil(u, () => songRows().length === 1, 4);
		if (c.s0 != null) await songTo(u, c.s0);
		if (c.m0 != null && V.songMode !== c.m0) { u.click(`[data-seqmode="${c.m0 ? "song" : "pattern"}"]`); await sleep(1000); }
	};
	const songPlayhead = { name: "md-song-playhead", steps: songPlayheadSteps(() => { }), tidy: songPlayheadTidy };
	/* the same, as screenshots for a design review (scripts/mdmm-shots.sh with MDMM_SHOTS_JOURNEY=md-shots-song) */
	const shotsSong = { name: "md-shots-song", needs: () => /md-shots/.test(location.search) ? null : "screenshots only when asked by name",
		steps: [...songPlayheadSteps(name => Bridge.log("SHOT " + name)).map(s => s.act && /SHOT|shot/.test(String(s.act)) ? Object.assign({}, s, { hold: 2500 }) : s), { say: "done", act: () => Bridge.log("SHOT done") }], tidy: songPlayheadTidy };
	const all = [bootCard, firstBeat, spaceTransport, tempoDrag, tapTempo, tapTempoB, patStep, queuePattern, plate, wsKeys, helpKeys, osHelp, undoRedo,
		paintUndo, accentSlide, lockLane, pagesJ, copyPaste, selectCopyPaste, stepMenuJ, osCopyPaste, buttonsCopyPaste, clearPatternJ, fillEveryJ, rotateJ, rotateUndo, trackKeys, muteKeys, liveRec,
		genJourney("md-gen-mutate-undo", false), genJourney("md-gen-defaults-mutate-undo", true), genKeys,
		shapeSound, arrows, machinePick, soundCopy, editorDrag, controlAll, globalKey, songModeJ,
		mixSolo, shiftMutes, allOff, fader, outKey, masterFx,
		songArrange, songChain, songPlayhead, samplerSlots, samplerSetup, audition,
		libDialog, kitCopy, kitRename, kitClear, patGo, patClear, dialogEsc,
		globalJ, globalRouting, globalMapNote, audioPanel, romCard, notePlay,
		lockRamp, pasteMany, mutScope, songInspector, songDrag, ramView, setupChop, panBox, hwNoMachine, syxImportJ, syxImportMute, shots, shotsSong];

	/* ---------- demos: journeys played for a camera (doc/modern-ux/DEMO-VIDEOS.md) ---------- */
	/* Not in `all`: ?selftest=journey never runs them; ?selftest=demo-md-<name> does (Journey.demo), at a person's pace
	   with the drawn pointer. Each step is still checked on the screen and on the machine, so a demo that stops
	   working fails like a journey does. hold: how long the step is left to sound; caption: the video's line. */
	const look = async (u, q, m, fx, fy) => { await u.glide(q, fx, fy); u.click(q, m, fx, fy); };
	const trackCount = p => (Docs.patterns[p]?.tracks || []).filter(t => t.trigs.length).length;
	const trigCount = p => (Docs.patterns[p]?.tracks || []).reduce((n, t) => n + t.trigs.length, 0);
	/* the pattern that makes the fullest groove: the most tracks playing, then the most trigs */
	const fullest = () => Object.keys(Docs.patterns).map(Number).filter(hasPat).sort((a, b) => trackCount(b) - trackCount(a) || trigCount(b) - trigCount(a))[0];
	const busy = () => [...Array(16).keys()].filter(t => trigsOf(t).length && soundTracks().includes(t));
	const strip = (i, k) => `.strip[data-sel="${i}"] .ms.${k}`;
	/* demo-md-groove (about 28 s): opens on the fullest pattern already playing, GEN rolls a hi-hat, a drag mutes three
	   tracks and back, a solo, an Alt-drag filter sweep of the whole kit (undone), and the groove plays on under the
	   end card. Captions: about six words a line, two lines at most (the video script renders them). */
	const groove = {
		name: "demo-md-groove",
		card: "Machinedrum Editor|Free · pay what you want|Coming soon",
		/* before the camera: the fullest pattern and its kit, playing */
		setup: [
			{ say: "the Sequence workspace", act: async u => { u.click(tab("seq")); }, screen: () => onTab("seq") },
			{ say: "click the pattern on the LCD: the chooser opens", act: async u => { u.click("#pat"); },
				screen: () => ok(!$1("#libpop").hidden && $all("#libpop .ps").length === 128, "chooser closed"), machine: () => ok(Object.keys(Docs.patterns).length === 128, Object.keys(Docs.patterns).length + " patterns read"), within: 8000 },
			{ say: "click the fullest pattern: it and its kit load", act: async (u, c) => {
				c.p = fullest(); if (c.p == null) throw new Error("no pattern with trigs on this machine");
				u.click(psq(c.p)); await confirmIfAsked(u); c.note = `pattern ${patName(c.p)}: ${trackCount(c.p)} tracks, ${trigCount(c.p)} trigs`;
			}, machine: c => ok(currentPatternSlot() === c.p && idle(), "machine pattern " + currentPatternSlot()), within: 6000 },
			{ say: "press Escape: the chooser closes", act: async u => { u.key("Escape"); }, screen: () => ok($1("#libpop").hidden, "still open") },
			{ say: "press PLAY", act: async u => { tele.steps = []; u.click("#play"); }, screen: () => ok(V.playing, "not playing"), machine: () => ok(tele.last?.playing && new Set(tele.steps).size >= 2, "telemetry " + tele.steps.join(",")), within: 6000 }
		],
		steps: [
			{ say: "the groove plays", caption: "Your Machinedrum, on screen.", act: async u => { await u.glide("#seq .st[data-t=\"0\"][data-s=\"8\"]", 0.5, 0.5, 700); },
				machine: () => ok(tele.last?.playing, "not playing"), hold: 1700 },
			{ say: "pick a hi-hat (or snare) track on the rail", caption: "Roll a fresh hi-hat groove", act: async (u, c) => {
				const role = t => genRole(V.tracks[t].m), b = busy();
				c.t = b.find(t => role(t) === "hat") ?? b.find(t => role(t) === "snare") ?? b.find(t => role(t) !== "kick") ?? soundTrack();
				await look(u, rail(c.t)); c.p0 = trigsOf(c.t);
			}, screen: c => ok(S.sel === c.t && !!$1("#genband [data-rand]"), "selected " + S.sel), hold: 300 },
			{ say: "click GEN's Random key until the rhythm changes", act: async (u, c) => { await u.glide("#genband [data-rand]"); for (let i = 0; i < 5; i++) { u.click("#genband [data-rand]"); if (await until(() => !same(trigsOf(c.t), c.p0) && idle(), 2500)) break; } },
				screen: c => ok(gridShows(c.t), "grid differs"), machine: c => ok(!same(trigsOf(c.t), c.p0), "unchanged"), hold: 1500 },
			{ say: "roll it once more", act: async (u, c) => { c.p1 = trigsOf(c.t); for (let i = 0; i < 5; i++) { u.click("#genband [data-rand]"); if (await until(() => !same(trigsOf(c.t), c.p1) && idle(), 2500)) break; } },
				machine: c => ok(!same(trigsOf(c.t), c.p1), "unchanged"), hold: 1500 },
			{ say: "click the Mix tab", caption: "Mute tracks with one drag", act: async u => { await look(u, tab("mix")); }, screen: () => onTab("mix"), hold: 300 },
			{ say: "drag across three tracks' M keys: they mute", act: async (u, c) => {
				const keep = soloKeep(), b = busy().filter(t => genRole(V.tracks[t].m) !== "kick" && t !== c.t && !keep.has(t) && !mutes().includes(t));
				c.run = b.slice(0, 3); if (!c.run.length) throw new Error("no tracks to mute");
				await u.glide(strip(c.run[0], "m"));
				await u.drag(strip(c.run[0], "m"), c.run.slice(1).map(i => strip(i, "m")), {}, { captured: false, stepMs: 260 });
			}, machine: c => ok(c.run.every(i => mutes().includes(i)), "machine mutes " + mutes()), screen: c => ok(c.run.every(i => pressed(strip(i, "m"))), "M keys not lit"), hold: 1400 },
			{ say: "drag back across them: they play again", act: async (u, c) => {
				const back = c.run.slice().reverse();
				await u.glide(strip(back[0], "m"));
				await u.drag(strip(back[0], "m"), back.slice(1).map(i => strip(i, "m")), {}, { captured: false, stepMs: 260 });
			}, machine: c => ok(c.run.every(i => !mutes().includes(i)), "machine mutes " + mutes()), hold: 600 },
			{ say: "click S on the rolled track: solo", caption: "Solo in one click", act: async (u, c) => { await look(u, strip(c.t, "s")); },
				machine: c => ok(!mutes().includes(c.t) && mutes().length >= 2, "machine mutes " + mutes()), hold: 1400 },
			{ say: "click S again: everything plays", act: async (u, c) => { await look(u, strip(c.t, "s")); },
				machine: c => ok(!c.run.some(i => mutes().includes(i)) && !mutes().includes(c.t), "machine mutes " + mutes()), within: 8000, hold: 500 },
			{ say: "click the Sound tab", caption: "Sweep the whole kit at once", act: async u => { await look(u, tab("sound")); }, screen: () => onTab("sound"), hold: 300 },
			{ say: "Alt-drag the filter's FLTF up, hold, and back: the whole kit sweeps (one undo step)", act: async (u, c) => {
				const q = '#main .pc[data-g="fx"][data-n="FLTF"]'; if (!$1(q)) throw new Error("no FLTF value");
				c.k0 = allKitVals(); c.v0 = getV($1(q)); c.moved = 0;
				const up = Math.min(100, 120 - c.v0), path = [];
				for (let k = 1; k <= 14; k++) path.push([Math.round(up * k / 14), 0]);
				for (let k = 0; k < 8; k++) path.push([up, 0]);
				for (let k = 13; k >= 0; k--) path.push([Math.round(up * k / 14), 0]);
				/* how many tracks the sweep moved at its widest, read from the machine's documents while it runs */
				const watch = setInterval(() => { c.moved = Math.max(c.moved, allKitVals().filter((v, t) => !same(v, c.k0[t])).length); }, 50);
				await u.glide(q);
				try { await u.drag(q, path, { alt: true }, { stepMs: 100 }); } finally { clearInterval(watch); }
			}, machine: c => ok(c.moved >= 2, `the sweep moved ${c.moved} tracks`), within: 8000, hold: 200 },
			{ say: "press Cmd+Z: the kit is as it was", act: async (u, c) => { document.activeElement?.blur?.(); if (!same(allKitVals(), c.k0)) u.key("z", { cmd: true }); }, machine: c => ok(same(allKitVals(), c.k0), "not all back"), within: 10000, hold: 300 },
			/* the end card goes over this (the video script): the groove plays on and fades out */
			{ say: "the groove plays on", act: async u => { await u.glide("#seq, #main", 0.5, 0.35, 600); }, machine: () => ok(tele.last?.playing, "stopped"), hold: 3800 }
		],
		async tidy(u) { if (V.playing) u.click("#play"); if (S.soloSet.size) setSolo(new Set()); }
	};
	/* demo-md-full: one song through every page, as doc/modern-ux/DEMO-STORYBOARD.md times it (120 BPM, 16-step patterns,
	   a bar is 2 s). The bar clock counts the machine's steps from its telemetry (from PLAY, across pattern changes and a
	   STOP); an action meant for bar n starts in step 15 of bar n-1 so it lands on the "1". Each bar line is logged
	   ("DEMO BAR n clock ms") with every pattern change ("DEMO PATTERN p clock ms") for the video's timeline. */
	const clock = { total: -1, prev: -1, bar: 0, on: false, pat: -1 };
	Bridge.onMessage(m => {
		if (m.type !== "telemetry" || !clock.on) return;
		if (!m.playing) { clock.prev = -1; return; }
		const L = V.len || 16;
		if (clock.prev < 0) clock.total = clock.total < 0 ? m.step : Math.ceil(clock.total / 16) * 16 + m.step;
		else if (m.step !== clock.prev) clock.total += m.step > clock.prev ? m.step - clock.prev : L - clock.prev + m.step;
		clock.prev = m.step;
		const bar = Math.floor(clock.total / 16) + 1;
		if (bar !== clock.bar) { clock.bar = bar; Bridge.log(`DEMO BAR ${bar} clock ${Math.round(performance.now())}`); }
		const p = currentPatternSlot(); if (p !== clock.pat) { clock.pat = p; Bridge.log(`DEMO PATTERN ${patName(p)} clock ${Math.round(performance.now())}`); }
	});
	/* wait for bar n's line (lead ms before its "1": from step 15 of bar n-1), or for bar n itself (lead < 0) */
	const STEP = 125;
	/* the storyboard's bar n (moved by song.shift), or with abs bar n itself; returns the bar it waited for. Past it
	   already: the next bar line instead, so an action never lands inside a bar, and every later bar moves as much
	   (the song keeps its spacing) */
	const song = { shift: 0 };
	const atBar = async (n, lead = 70, abs = false) => {
		const early = lead < 0 ? 0 : Math.max(1, Math.ceil(lead / STEP));
		let bar = abs ? n : n + song.shift, want = (bar - 1) * 16 - early;
		if (clock.total > want) { const m = Math.floor((clock.total + early) / 16) + 2; Bridge.log(`DEMO moved: bar ${bar} -> ${m}`); if (!abs) song.shift += m - bar; bar = m; want = (m - 1) * 16 - early; }
		await until(() => V.playing && clock.total >= want, 30000);
		if (lead >= 0) await sleep(Math.max(0, early * STEP - lead));
		return bar;
	};
	const railM = i => `#rail .th[data-sel="${i}"] .ms.m`;
	const chopCell = s => `#chop [data-cp="${s}"]`;
	const role = (re) => [...Array(16).keys()].find(t => re.test(V.tracks[t].m));
	const tracksOf = () => ({ kick: role(/-(BD|B2)$/), snare: role(/-SD$/), clap: role(/-CP$/), rim: role(/-RS$/), bell: role(/-CB$/),
		hat: role(/-(CH|HH)$/), open: role(/-OH$/), tom: role(/-(XT|LT|MT|HT)$/) });
	const genRoll = async (u, t, n = 1) => { for (let k = 0; k < n; k++) { const t0 = trigsOf(t); u.click("#genband [data-rand]"); await until(() => !same(trigsOf(t), t0) && idle(), 2500); } };
	const pickEmpty = () => { const k = Docs.patterns[fullest()]?.kit; const pairs = [...Array(127).keys()].filter(p => (p & 15) < 15 && Docs.patterns[p] && Docs.patterns[p + 1] && !hasPat(p) && !hasPat(p + 1)); return pairs.find(p => Docs.patterns[p].kit === k) ?? pairs[0] ?? null; };
	const full = {
		name: "demo-md-full",
		card: "Machinedrum Editor|Free · pay what you want|Coming soon",
		setup: [
			{ say: "the Sequence workspace", act: async u => { u.click(tab("seq")); }, screen: () => onTab("seq") },
			{ say: "open the pattern chooser", act: async u => { u.click("#pat"); }, machine: () => ok(Object.keys(Docs.patterns).length === 128, "patterns read " + Object.keys(Docs.patterns).length), within: 8000 },
			{ say: "click an empty pattern whose next slot is empty too, on the fullest pattern's kit", act: async (u, c) => {
				c.A = pickEmpty(); if (c.A == null) throw new Error("no two empty patterns side by side on that kit"); c.B = c.A + 1; c.kit = Docs.patterns[c.A].kit;
				u.click(psq(c.A)); await confirmIfAsked(u); c.note = `A ${patName(c.A)}, B ${patName(c.B)}, kit ${c.kit + 1}`;
			}, machine: c => ok(currentPatternSlot() === c.A && idle(), "pattern " + currentPatternSlot()), within: 6000 },
			{ say: "press Escape", act: async u => { u.key("Escape"); }, screen: () => ok($1("#libpop").hidden, "open") },
			{ say: "tempo 120, swing 50 %", act: async () => { cmd("tempo", { bpm: 120 }); if (V.swing !== 50) l2set("swing", 50); }, machine: () => ok(Docs.global.tempo === 120, "tempo " + Docs.global.tempo), within: 6000 },
			{ say: "the tracks", act: async (u, c) => { Object.assign(c, { T: tracksOf() }); c.note = JSON.stringify(c.T); clock.on = true; song.shift = 0; }, machine: c => ok(Object.values(c.T).every(t => t != null) && V.len === 16, "tracks " + JSON.stringify(c.T) + ", length " + V.len) }
		],
		steps: [
			/* ---- bar 0: the kit, then PLAY ---- */
			{ section: "intro", say: "click KIT: the library; click the pattern's kit: it loads at once", caption: "Start from nothing.", act: async (u, c) => { await look(u, "#kitf"); await sleep(500); await look(u, ks(c.kit)); if (await until(dlgShown, 700)) u.click(dlgButton("Load without saving") || $1("#dlg .danger")); await sleep(400); u.key("Escape"); },
				machine: c => ok(currentKitSlot() === c.kit && idle(), "kit " + currentKitSlot()), screen: () => ok($1("#libpop").hidden, "library open"), within: 6000, hold: 300 },
			{ section: "intro", say: "press PLAY: bar 1", act: async u => { await look(u, "#play"); }, machine: () => ok(tele.last?.playing, "not playing"), within: 4000 },
			{ section: "intro", say: "kick: GEN Defaults and Random, Euclid 4/16 from bar 2", caption: "GEN writes the kick.", act: async (u, c) => { await look(u, rail(c.T.kick)); await sleep(150); await look(u, '#genband [data-gen="fill"]'); await u.glide("#genband [data-rand]"); await atBar(2, 200); await genUntil(u, c.T.kick, hits(3, 6)); },
				machine: c => ok(trigsOf(c.T.kick).length >= 3, "kick " + trigsOf(c.T.kick)), screen: c => ok(gridShows(c.T.kick), "grid") },
			{ section: "intro", say: "snare: GEN from bar 3", act: async (u, c) => { await look(u, rail(c.T.snare)); await u.glide("#genband [data-rand]"); await atBar(3, 200); await genRoll(u, c.T.snare); },
				machine: c => ok(trigsOf(c.T.snare).length >= 1, "snare " + trigsOf(c.T.snare)) },
			{ section: "intro", say: "hats: GEN from bar 4, R again for bar 5", caption: "Roll the hats until they groove.", act: async (u, c) => {
				await look(u, rail(c.T.hat)); await sleep(100); await look(u, '#genband [data-gen="fill"]');
				await u.glide("#genband [data-rand]"); await atBar(4, 200); await genRoll(u, c.T.hat);
				await atBar(5, 200); document.activeElement?.blur?.(); const h0 = trigsOf(c.T.hat); u.key("r"); await until(() => !same(trigsOf(c.T.hat), h0), 2000);
			}, machine: c => ok(trigsOf(c.T.hat).length >= 2, "hat " + trigsOf(c.T.hat)) },
			/* ---- build ---- */
			{ section: "build", say: "bar 6: rim clicks by hand, the open hat every 4th, an accent and a slide", caption: "Then play it by hand.", act: async (u, c) => {
				await atBar(6, 900);
				for (const s of [3, 11, 14]) { await u.glide(cell(c.T.rim, s), 0.5, 0.5, 220); u.click(cell(c.T.rim, s)); await sleep(60); }
				await u.glide(cell(c.T.open, 2)); u.rightClick(cell(c.T.open, 2)); u.cap("right-click: the step menu"); await sleep(500); await u.glide('#deskmenu [data-mid="step-fill-4"]'); u.click('#deskmenu [data-mid="step-fill-4"]'); u.cap("Fill every 4th"); clearSel(); await sleep(400);
				await u.glide(cell(c.T.snare, trigsOf(c.T.snare)[0] ?? 4)); u.click(cell(c.T.snare, trigsOf(c.T.snare)[0] ?? 4), { shift: true }); u.cap("⇧ accent"); await sleep(400);
				await u.glide(cell(c.T.rim, 14)); u.click(cell(c.T.rim, 14), { alt: true, shift: true }); u.cap("⌥⇧ slide");
			}, machine: c => ok([3, 11, 14].every(s => trigsOf(c.T.rim).includes(s)) && [2, 6, 10, 14].every(s => trigsOf(c.T.open).includes(s)), `rim ${trigsOf(c.T.rim)} open ${trigsOf(c.T.open)}`), within: 6000 },
			{ section: "build", say: "bar 8: REC, a tom fill on the QWERTY keys, REC off at bar 9", caption: "Record a fill from the keyboard.", act: async (u, c) => {
				await look(u, rail(c.T.tom)); c.tom0 = trigsOf(c.T.tom).length; document.activeElement?.blur?.();
				const b = await atBar(8, 200); u.key(" ", { alt: true });
				await until(() => clock.total >= (b - 1) * 16 + 11, 4000); for (const k of ["a", "s", "d", "f"]) { u.key(k); await sleep(125); }
				await atBar(b + 1, 60, true); u.key(" ", { alt: true });
				await sleep(300); if (!V.playing) { Bridge.log("DEMO note: REC off stopped the machine; PLAY again"); u.click("#play"); }
			}, machine: c => ok(trigsOf(c.T.tom).length > c.tom0 && tele.last?.playing && !V.rec, `tom ${c.tom0} -> ${trigsOf(c.T.tom).length}, playing ${tele.last?.playing}, rec ${V.rec}`), within: 6000 },
			{ section: "build", say: "bar 10: LOCK PARAMETER DEC on the hat, a ramp across the lane", caption: "Lock any parameter per step.", act: async (u, c) => {
				await look(u, rail(c.T.hat));
				const shown = e => e && e.getBoundingClientRect().width > 0, chip = () => [$1('#chips .pk[data-lane="DEC"]'), ...$all("#chips .pk[data-lane]")].find(shown);
				await until(() => chip(), 3000); const k = chip(); if (!k) throw new Error("no lock parameter key shown"); c.lane = k.dataset.lane; await sleep(300); await look(u, `#chips .pk[data-lane="${c.lane}"]`);
				c.on = trigsOf(c.T.hat).filter(s => s < 16); const a = c.on[0], b = c.on[c.on.length - 1];
				await u.glide(`#lane .lb[data-s="${a}"]`, 0.5, 0.9); await atBar(10, 900);
				await u.drag(`#lane .lb[data-s="${a}"]`, c.on.slice(1).map(s => `#lane .lb[data-s="${s}"]`), { shift: true }, { fy: 0.9, stepMs: 90 });
				c.li = pidx(c.T.hat, c.lane);
			}, machine: c => ok(locksOf(c.T.hat, c.li).length >= 2, "locks " + JSON.stringify(locksOf(c.T.hat, c.li))), within: 8000 },
			{ section: "build", say: "bar 11: rotate the hats twice", caption: "Rotate a track on the beat.", act: async (u, c) => { c.h0 = trigsOf(c.T.hat); document.activeElement?.blur?.(); await atBar(11, 200); u.key("ArrowRight", { alt: true }); await sleep(250); u.key("ArrowRight", { alt: true }); },
				machine: c => ok(!same(trigsOf(c.T.hat), c.h0), "hats unchanged") },
			{ section: "build", say: "bar 12: swing to 58 %, tap T on the beat", caption: "Swing it. Tap the tempo.", act: async (u, c) => {
				const q = '.l2.ed[data-l2="swing"]'; await u.glide(q); await atBar(12, 300); await u.drag(q, [[0, -6], [0, -12], [0, -18], [0, -24]], {}, { stepMs: 60 });
				await atBar(13, 120); document.activeElement?.blur?.(); for (let k = 0; k < 4; k++) { u.key("t"); await sleep(500); }
			}, machine: () => ok(V.swing >= 56 && Math.abs(Docs.global.tempo - 120) <= 2, `swing ${V.swing}, tempo ${Docs.global.tempo}`), within: 6000 },
			{ section: "build", say: "bar 14: Sound, the snare's filter screen dragged; the machine picker opened on the rim", caption: "Every sound, grouped by what it does.", act: async (u, c) => {
				await look(u, tab("sound")); await look(u, rail(c.T.snare)); c.k0 = kitVals(c.T.snare);
				const cv = $1('#main canvas.ed[data-ed="flt"]') || $1("#main canvas.ed"); const r = cv.getBoundingClientRect(), h = ED[cv.dataset.ed].handles(r.width, r.height, cv)[0];
				await u.glide(cv, h.x / r.width, h.y / r.height); await atBar(14, 600);
				await u.drag(cv, [[12, -4], [24, -8], [36, -12], [48, -14]], {}, { fx: h.x / r.width, fy: h.y / r.height, stepMs: 120 });
				await sleep(500); await look(u, rail(c.T.rim)); await look(u, "#machbtn"); await sleep(900); u.key("Escape");
			}, machine: c => ok(!same(kitVals(c.T.snare), c.k0), "snare unchanged"), screen: () => ok($1("#machpop").hidden, "picker open"), within: 6000 },
			{ section: "build", say: "bar 15: MUTATE the rim; bar 17: Cmd+Z; bar 18: MUTATE again and keep it", caption: "MUTATE a sound. Undo if you don't like it.", act: async (u, c) => {
				c.r0 = kitVals(c.T.rim); await u.glide("#mutband [data-rand]"); await atBar(15, 150); u.click("#mutband [data-rand]");
				await until(() => !same(kitVals(c.T.rim), c.r0), 3000); c.r1 = kitVals(c.T.rim);
				await atBar(17, 150); document.activeElement?.blur?.(); u.key("z", { cmd: true }); await until(() => same(kitVals(c.T.rim), c.r0), 3000); c.undone = same(kitVals(c.T.rim), c.r0);
				await atBar(18, 150); u.click("#mutband [data-rand]");
			}, machine: c => ok(c.undone && !same(kitVals(c.T.rim), c.r0), `undo back ${c.undone}, kept ${!same(kitVals(c.T.rim), c.r0)}`), within: 6000 },
			{ section: "build", say: "bar 19: Mix, PAN the clap; bar 20: the snare's DEL send up and the echo's feedback; bar 21: send down", caption: "Mix: pan, sends, an echo throw.", act: async (u, c) => {
				await look(u, tab("mix")); const pan = `.strip[data-sel="${c.T.clap}"] .pc[data-n="PAN"]`; await u.glide(pan); await atBar(19, 400); await u.drag(pan, [[-10, 0], [-20, 0], [-30, 0]], {}, { stepMs: 80 });
				const del = `.strip[data-sel="${c.T.snare}"] .pc[data-n="DEL"]`, fb = '#main .pc[data-g="mfx"][data-n="FB"]';
				c.d0 = getV($1(del)); await u.glide(del); await atBar(20, 500); await u.drag(del, [[30, 0], [60, 0], [90, 0]], {}, { stepMs: 60 });
				if ($1(fb)) { await u.glide(fb); await u.drag(fb, [[20, 0], [40, 0]], {}, { stepMs: 80 }); }
				await u.glide(del); await atBar(21, 400); await u.drag(del, [[-30, 0], [-60, 0], [-90, 0]], {}, { stepMs: 60 });
			}, machine: c => ok(kitVals(c.T.snare)[pidx(c.T.snare, "DEL", "rt")] <= c.d0 + 4, "DEL " + kitVals(c.T.snare)[pidx(c.T.snare, "DEL", "rt")]), within: 6000 },
			{ section: "build", say: "bars 22-24: the pattern chooser, A copied onto B; › queues B for bar 25", caption: "Copy the pattern. Queue the next one.", act: async (u, c) => {
				/* the chooser opens on the current pattern (A) selected; → selects the next slot (B); nothing switches */
				await look(u, "#pat"); await sleep(400); await u.glide(psq(c.A));
				await look(u, '#libpop [data-la="copy"]'); await sleep(300); await u.glide(psq(c.B)); u.key("ArrowRight"); await until(() => LIB.sel === c.B, 1000); await sleep(300);
				await look(u, '#libpop [data-la="paste"]'); if (await until(dlgShown, 1500)) { c.asked = $1("#dlg p")?.textContent; u.click($1("#dlg .danger") || dlgButton("Paste")); }
				await until(() => hasPat(c.B) && !dlgShown(), 4000); await sleep(300);
				for (let k = 0; k < 3 && !$1("#libpop").hidden; k++) { u.key("Escape"); await until(() => $1("#libpop").hidden, 600); }
				if (!$1("#libpop").hidden) closeLib(false);
				await u.glide("#patNext"); await atBar(24, 900); u.click("#patNext"); if (await until(dlgShown, 1200)) { c.asked2 = $1("#dlg p")?.textContent; u.click(dlgButton("Keep the edits") || dlgButton("Cancel")); }
				c.note = `paste asked: ${c.asked || "-"}; switch asked: ${c.asked2 || "-"}`;
			}, machine: c => ok(hasPat(c.B) && (desk().queued === c.B || currentPatternSlot() === c.B), `B ${hasPat(c.B)}, queued ${desk().queued}, pattern ${currentPatternSlot()}`), within: 4000 },
			{ section: "build", say: "B plays from bar 25 on the same kit", machine: c => ok(currentPatternSlot() === c.B && currentKitSlot() === c.kit, `pattern ${currentPatternSlot()} kit ${currentKitSlot()}`), within: 6000 },
			/* ---- break ---- */
			{ section: "break", say: "bar 26: one drag across tracks 1-5's M keys: the beat drops out", caption: "One drag: the beat drops out.", act: async (u, c) => {
				await look(u, tab("mix")); c.five = [0, 1, 2, 3, 4]; await u.glide(strip(0, "m"));
				await until(() => clock.total >= 25 * 16 - 3, 8000);
				await u.drag(strip(0, "m"), c.five.slice(1).map(i => strip(i, "m")), {}, { captured: false, stepMs: 45, settle: 50 });
			}, machine: c => ok(c.five.every(i => mutes().includes(i)), "mutes " + mutes()), hold: 300 },
			{ section: "break", say: "Sampler › RAM 1: Set up sampling (recorder 13, player 14, once on step 1)", caption: "Sample the groove into RAM.", act: async (u, c) => {
				await look(u, tab("sampler")); await sleep(200); await look(u, '.slotk[data-slot="RAM1"]'); await sleep(400); [c.r, c.pl] = setupTracks();
				const once = $1('[data-smponce="1"]'); if (once && getComputedStyle(once.parentElement).visibility !== "hidden") { await look(u, once); await sleep(250); }
				await look(u, "[data-setupgo]");
			}, machine: c => ok(kit().tracks[c.r].machine === "RAM-R1" && kit().tracks[c.pl].machine === "RAM-P1", `tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.pl].machine}`), within: 8000, hold: 200 },
			{ section: "break", say: "source Main mix, LEN one bar, RATE full", act: async (u, c) => {
				await look(u, '[data-recsrc="main"]'); await sleep(250);
				const q = '#main .pc[data-g="syn"][data-n="LEN"]'; const d = 64 - getV($1(q)); if (d) { await u.glide(q); await u.drag(q, [[Math.round(d / 3), 0], [Math.round(2 * d / 3), 0], [d, 0]], {}, { stepMs: 60 }); }
				const rt = '#main .pc[data-g="syn"][data-n="RATE"]'; if ($1(rt) && getV($1(rt)) < 127) { await u.glide(rt); await u.drag(rt, [[60, 0], [140, 0]], {}, { stepMs: 60 }); }
			}, machine: c => ok(sourceOf(c.r) === "main" && kitVals(c.r)[pidx(c.r, "LEN", "syn")] === 64, `source ${sourceOf(c.r)}, LEN ${kitVals(c.r)[pidx(c.r, "LEN", "syn")]}`), within: 6000 },
			{ section: "break", say: "Capture next loop, armed in bar 28: records bar 29, frozen at bar 30", act: async (u, c) => {
				await u.glide("[data-capture]"); await atBar(28); await until(() => clock.total % 16 >= 11, 3000); u.click("[data-capture]");
			}, screen: () => ok(slotState(1) === "cap", "slot " + slotState(1)), within: 3000 },
			{ section: "break", say: "it records the bar", act: async u => { await u.glide(".ramwave .wavecv", 0.5, 0.5, 900); }, screen: () => ok(slotState(1) === "frozen", "slot " + slotState(1)), within: 9000 },
			{ section: "break", say: "the take's waveform", machine: () => { const t = smpSlotOf("ram", 0); return ok(t && !t.empty, "take " + JSON.stringify(t && { empty: t.empty })); }, screen: () => { const n = inked($1("canvas.smpwave")); return ok(n > 2000, n + " pixels"); }, within: 10000 },
			{ section: "break", say: "chop trigs from bar 31", caption: "Chop it: slices, reverse, retrig.", act: async (u, c) => {
				c.chops = [0, 3, 6, 8, 10, 12, 14, 15]; await u.glide(chopCell(0)); await atBar(31, 900);
				for (const s of c.chops) { await u.glide(chopCell(s), 0.5, 0.5, 140); u.click(chopCell(s)); await sleep(50); }
			}, machine: c => ok(c.chops.every(s => trigsOf(c.pl).includes(s)), "player " + trigsOf(c.pl)) },
			{ section: "break", say: "drag four slices up", act: async (u, c) => {
				c.moved = [3, 6, 10, 12];
				for (const [k, s] of c.moved.entries()) { await u.glide(chopCell(s), 0.5, 0.5, 200); await u.drag(chopCell(s), [[0, -10], [0, -20], [0, -30 - 10 * k], [0, -40 - 14 * k]], {}, { stepMs: 45, settle: 80 }); }
			}, machine: c => { const st = locksOf(c.pl, pidx(c.pl, "STRT")); return ok(c.moved.every(s => st.some(x => x[0] === s && x[1] > 0)), "STRT " + JSON.stringify(st)); }, within: 6000 },
			{ section: "break", say: "alt-click two moved slices: reversed; shift-click the bar end: retrig", act: async (u, c) => {
				for (const s of [6, 12]) { await u.glide(chopCell(s), 0.5, 0.5, 200); u.click(chopCell(s), { alt: true }); u.cap("⌥ reverse"); await sleep(200); }
				for (const s of [14, 15]) { await u.glide(chopCell(s), 0.5, 0.5, 200); u.click(chopCell(s), { shift: true }); u.cap("⇧ retrig"); await sleep(200); }
			}, screen: () => ok([6, 12].every(s => /REV/.test($1(chopCell(s))?.textContent || "")) && [14, 15].every(s => /RTRG/.test($1(chopCell(s))?.textContent || "")), "REV / RTRG not shown"), within: 6000 },
			{ section: "break", say: "bar 35: the ROM slots' waveform tiles", caption: "48 ROM slots, real waveforms.", act: async u => { await atBar(35, -1); await look(u, ".slotk.rom.has, .slotk.rom"); await sleep(300); await u.glide(".romtiles", 0.5, 0.3, 900); },
				screen: () => ok($all(".romtile canvas.tw").length > 0, "no tiles") },
			/* ---- riser and drop ---- */
			{ section: "riser", say: "bars 37-39: Sound, Alt-drag the hat's FLTF up: every track sweeps", caption: "Sweep the whole kit. Arm the drop.", act: async (u, c) => {
				await look(u, tab("sound")); await look(u, rail(c.T.hat)); const q = '#main .pc[data-g="fx"][data-n="FLTF"]'; c.k0 = allKitVals(); c.v0 = getV($1(q));
				const up = Math.min(90, 120 - c.v0), path = []; for (let k = 1; k <= 48; k++) path.push([Math.round(up * k / 48), 0]);
				await u.glide(q); await atBar(37, 120); await u.drag(q, path, { alt: true }, { stepMs: 110 });
			}, machine: c => ok(allKitVals().filter((v, t) => !same(v, c.k0[t])).length >= 2, "the sweep moved too few tracks") },
			{ section: "riser", say: "bar 40: Sequence, Shift-click tracks 1-5's M keys: prepared", act: async (u, c) => {
				await look(u, tab("seq")); await atBar(40, -1); u.cap("⇧ hold"); for (const i of c.five) { await u.glide(railM(i), 0.5, 0.5, 180); u.click(railM(i), { shift: true }); await sleep(60); }
			}, machine: c => ok(c.five.every(i => mutes().includes(i)), "sent while held: " + mutes()), screen: c => ok(c.five.every(i => $1(railM(i)).classList.contains("prep")), "not prepared"), within: 2000 },
			{ section: "drop", say: "bar 41: let Shift go on the bar line, Cmd+Z the sweep", caption: "Drop it on the bar.", act: async (u, c) => {
				await atBar(41, 70); document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift", code: "ShiftLeft", bubbles: true })); u.cap("⇧ release");
				document.activeElement?.blur?.(); u.key("z", { cmd: true });
			}, machine: c => ok(c.five.every(i => !mutes().includes(i)) && same(allKitVals(), c.k0), `mutes ${mutes()}, kit back ${same(allKitVals(), c.k0)}`), within: 6000, hold: 4500 },
			{ section: "drop", say: "bar 45: Mix, solo the snare and the chops; bar 47: un-solo", caption: "Solo in one click.", act: async (u, c) => {
				await look(u, tab("mix")); await u.glide(strip(c.T.snare, "s")); await atBar(45, 150); u.click(strip(c.T.snare, "s")); await sleep(120); await look(u, strip(c.pl, "s"));
				await u.glide(strip(c.T.snare, "s")); await atBar(47, 150); u.click(strip(c.T.snare, "s")); await sleep(120); await look(u, strip(c.pl, "s"));
			}, machine: c => ok(!S.soloSet.size && same(mutes(), [c.r]), "solo " + [...S.soloSet] + " mutes " + mutes() + " (the frozen recorder stays muted)"), within: 6000 },
			{ section: "drop", say: "bar 49: Sequence, GEN rolls the open hat; bar 51: rotate it", caption: "Roll a new open hat.", act: async (u, c) => {
				await look(u, tab("seq")); await look(u, rail(c.T.open)); await u.glide("#genband [data-rand]"); await atBar(49, 200); await genRoll(u, c.T.open);
				c.o1 = trigsOf(c.T.open); document.activeElement?.blur?.(); await atBar(51, 150); u.key("ArrowRight", { alt: true });
			}, machine: c => ok(!same(trigsOf(c.T.open), c.o1), "open hat not rotated") },
			/* ---- outro: the chain, loaded and started ---- */
			{ section: "outro", say: "bar 52: Song, Arrange, then Chain", caption: "Load a chain and start it.", act: async u => { await look(u, tab("song")); await look(u, '[data-set="songpick"] button[data-v="arrange"]'); await sleep(600); await look(u, '[data-set="songpick"] button[data-v="chain"]'); },
				screen: () => ok(!!$1("[data-chainpad]"), "no pads") },
			{ section: "outro", say: "STOP on bar 53's line, chain pads A and B (loaded), PLAY: the chain starts at A", act: async (u, c) => {
				const b = c.A >> 4; if (S.bank !== b) { await look(u, `[data-bank="${b}"]`); await sleep(200); }
				await u.glide("#play"); await atBar(53, 30); u.click("#play"); await until(() => !V.playing, 2000);
				await look(u, `[data-chainpad="${c.A}"]`); await sleep(150); await look(u, `[data-chainpad="${c.B}"]`);
				await until(() => desk().chain?.active, 3000); c.loaded = JSON.stringify(desk().chain); await look(u, "#play");
			}, machine: c => ok(desk().chain?.active && same(desk().chain.patterns, [c.A, c.B]) && tele.last?.playing && currentPatternSlot() === c.A, `chain ${JSON.stringify(desk().chain)}, playing ${tele.last?.playing}, pattern ${currentPatternSlot()}`), within: 5000 },
			{ section: "outro", say: "the chain moves on: A, then B", act: async u => { await u.glide(".chainrow", 0.5, 0.5, 600); }, machine: c => ok(currentPatternSlot() === c.B, "pattern " + currentPatternSlot()), within: 6000 },
			{ section: "outro", say: "and back to A", machine: c => ok(currentPatternSlot() === c.A, "pattern " + currentPatternSlot()), within: 6000 },
			{ section: "outro", say: "and B again", caption: "The chain plays on. Strip it back.", machine: c => ok(currentPatternSlot() === c.B, "pattern " + currentPatternSlot()), within: 6000 },
			{ section: "outro", say: "Sequence: mute the rim and cowbell, the chops, the hats, the kick, a bar at a time", act: async (u, c) => {
				await look(u, tab("seq")); const b0 = clock.bar + 1;
				for (const [k, ts] of [[c.T.rim, c.T.bell], [c.pl], [c.T.hat, c.T.open], [c.T.kick]].entries()) { await u.glide(railM(ts[0])); await atBar(b0 + 2 * k, 150, true); for (const t of ts) { u.click(railM(t)); await sleep(90); } }
			}, machine: c => ok([c.T.kick, c.T.hat, c.pl].every(t => mutes().includes(t)), "mutes " + mutes()), within: 4000, hold: 1500 },
			{ section: "outro", say: "Shift-click the muted tracks, let Shift go on the bar line: everything back for the last two bars (the frozen recorder stays muted)", act: async (u, c) => {
				c.back = mutes().filter(t => t !== c.r); u.cap("⇧ hold"); for (const t of c.back) { await u.glide(railM(t), 0.5, 0.5, 160); u.click(railM(t), { shift: true }); await sleep(50); }
				await atBar(clock.bar + 1, 70, true); document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift", code: "ShiftLeft", bubbles: true })); u.cap("⇧ release");
			}, machine: c => ok(same(mutes(), [c.r]), "mutes " + mutes()), hold: 3600 },
			{ section: "outro", say: "STOP on the bar line", act: async u => { await u.glide("#play"); await atBar(clock.bar + 2, 30, true); u.click("#play"); }, machine: () => ok(tele.last && !tele.last.playing, "playing"), hold: 400 }
		],
		async tidy(u) { clock.on = false; if (desk().chain?.active) cmd("chainClear"); if (V.playing) u.click("#play"); if (S.soloSet.size) setSolo(new Set()); }
	};
	/* demo-md-sampler (about 60 s, one take): the sampler on its own, the chops in front. The fullest pattern (two bars
	   here) at 120 BPM, a bar is 2 s; every action starts on a bar line of the bar clock (atBar), and each change plays
	   for bars before the next one.
	     1-2   the groove plays; Sampler › RAM 1 shows the Set up sampling card            "Sample your own groove."
	     3     Set up sampling (recorder 13, player 14, once on step 1); source Main mix; LEN one bar; the player's PTCH
	           to 64 (no transpose), HOLD and DEC to the top, so each chop plays out at pitch until the next one: the
	           RAM-P machine keeps the values the track's machine had (here PTCH 52, DEC 64, STRT 25); STRT to 0 too, so a chop's slice n starts at 8n
	                                                                                      "Recorder and player in one click."
	     4-7   Capture next loop: the full groove (nothing muted) into RAM 1; the take    "Capture one bar of the groove."
	     8     sixteen chops, every 2nd step, all slice 1: a stutter                       "Chop it."
	     9-12  Mix: solo the player, its fader up: the chops alone                        "Solo the chops."
	     13-16 Sampler: six chops dragged to other slices                                 "Slice it up."
	     17-18 two reversed (⌥-click)                                                     "Reverse."
	     19-20 the bar ends retriggered (⇧-click)                                         "Retrig."
	     21-24 Mix: un-solo on the bar line: the groove back under the chops              "Bring the groove back."
	     25-27 Sound: ⌥-drag RTRG on the player: Control All, every track's 7th knob      "Alt-drag: retrig the whole kit."
	     28-29 ⌘Z: back                                                                    "Undo. Back in the pocket."
	     30-31 the groove and the chops under the end card */
	const sampler = {
		name: "demo-md-sampler",
		card: "Machinedrum Editor|Free · pay what you want|Coming soon",
		setup: [
			...groove.setup,
			{ say: "tempo 120, the Sampler on RAM 1 (its Set up sampling card)", act: async (u, c) => {
				cmd("tempo", { bpm: 120 }); u.click(tab("sampler")); await sleep(300); u.click('.slotk[data-slot="RAM1"]'); clock.total = -1; clock.prev = -1; clock.bar = 0; clock.on = true; song.shift = 0;
				c.p = currentPatternSlot(); c.note = `pattern ${patName(c.p)}, ${V.len} steps`;
			}, machine: () => ok(Docs.global.tempo === 120, "tempo " + Docs.global.tempo), screen: () => ok(!!$1("[data-setupgo]"), "no Set up sampling card (RAM 1 in use?)"), within: 6000 }
		],
		steps: [
			{ section: "hook", say: "the groove plays on the Sampler page", caption: "Sample your own groove.", act: async u => { await u.glide("[data-setupgo]", 0.5, 0.5, 900); await atBar(3, 1200); },
				machine: () => ok(tele.last?.playing, "not playing") },
			{ section: "setup", say: "bar 3: Once, on step 1 (when the recorder has trigs); Set up sampling", caption: "Recorder and player in one click.", act: async (u, c) => {
				[c.r, c.pl] = setupTracks(); const once = $1('[data-smponce="1"]');
				if (once && getComputedStyle(once.parentElement).visibility !== "hidden") { await look(u, once); await sleep(200); }
				await look(u, "[data-setupgo]");
			}, machine: c => ok(kit().tracks[c.r].machine === "RAM-R1" && kit().tracks[c.pl].machine === "RAM-P1", `tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.pl].machine}`), within: 8000 },
			{ section: "setup", say: "source Main mix, LEN one bar, RATE full; the player's HOLD and DEC to the top", act: async (u, c) => {
				await look(u, '[data-recsrc="main"]'); await sleep(200);
				const v = q => getV($1(q)), up = async (q, to) => { const d = to - v(q); if (d) { await u.glide(q); await u.drag(q, [[Math.round(d / 3), 0], [Math.round(2 * d / 3), 0], [d + (to === 127 || to === 0 ? 10 * Math.sign(d) : 0), 0]], {}, { stepMs: 60 }); } };
				const P = n => `#main .smp2 .pc[data-g="syn"][data-n="${n}"][data-t="${c.pl}"]`, R = n => `#main .smp2 .pc[data-g="syn"][data-n="${n}"][data-t="${c.r}"]`;
				const pv = n => $1(P(n)) ? P(n) : `#main .pc[data-g="syn"][data-n="${n}"]`;
				await up(R("LEN"), 64); if ($1(R("RATE"))) await up(R("RATE"), 127);
				c.before = Object.fromEntries(["PTCH", "DEC", "HOLD", "STRT", "END", "RTRG", "RTIM"].map(n => [n, kitVals(c.pl)[pidx(c.pl, n, "syn")]]));
				Bridge.log("DEMO player before: " + JSON.stringify(c.before) + " VOL " + kitVals(c.pl)[pidx(c.pl, "VOL", "rt")]);
				for (const [n, to] of [["DEC", 127], ["HOLD", 127], ["PTCH", 64], ["STRT", 0]]) { await up(pv(n), to); await until(idle, 2000); await sleep(150); }
				c.envWatch = setInterval(() => { const e = ["PTCH", "DEC", "HOLD"].map(n => kitVals(c.pl)[pidx(c.pl, n, "syn")]).join(","); if (e !== c.envLast) { Bridge.log("DEMO player PTCH,DEC,HOLD " + e + " at bar " + clock.bar); c.envLast = e; } }, 100);
				c.note = "player before: " + JSON.stringify(c.before) + ", after PTCH " + kitVals(c.pl)[pidx(c.pl, "PTCH", "syn")] + " HOLD " + kitVals(c.pl)[pidx(c.pl, "HOLD", "syn")] + " DEC " + kitVals(c.pl)[pidx(c.pl, "DEC", "syn")];
			}, machine: c => ok(sourceOf(c.r) === "main" && kitVals(c.r)[pidx(c.r, "VOL", "rt")] === 0 && kitVals(c.r)[pidx(c.r, "LEN", "syn")] === 64 && kitVals(c.pl)[pidx(c.pl, "HOLD", "syn")] === 127 && kitVals(c.pl)[pidx(c.pl, "DEC", "syn")] === 127 && kitVals(c.pl)[pidx(c.pl, "PTCH", "syn")] === 64,
				`PTCH ${kitVals(c.pl)[pidx(c.pl, "PTCH", "syn")]}, source ${sourceOf(c.r)}, LEN ${kitVals(c.r)[pidx(c.r, "LEN", "syn")]}, HOLD ${kitVals(c.pl)[pidx(c.pl, "HOLD", "syn")]}, DEC ${kitVals(c.pl)[pidx(c.pl, "DEC", "syn")]}`), within: 6000 },
			{ section: "capture", say: "Capture next loop, armed in the loop's last bar: it records the next loop's first bar", caption: "Capture one bar of the groove.", act: async (u, c) => {
				c.L = V.len; await u.glide("[data-capture]");
				await until(() => clock.total % c.L === c.L - 6, 8000); u.click("[data-capture]");
			}, screen: () => ok(slotState(1) === "cap", "slot " + slotState(1)), within: 3000 },
			{ section: "capture", say: "it records, then freezes", act: async u => { await u.glide(".ramwave .wavecv", 0.5, 0.5, 900); }, screen: () => ok(slotState(1) === "frozen", "slot " + slotState(1)), within: 12000 },
			{ section: "capture", say: "the take's waveform", machine: () => { const t = smpSlotOf("ram", 0); return ok(t && !t.empty, "take " + JSON.stringify(t && { empty: t.empty })); },
				screen: () => { const n = inked($1("canvas.smpwave")); return ok(n > 2000, n + " pixels"); }, within: 10000,
				act: async (u, c) => { const t = smpSlotOf("ram", 0); c.note = t ? smpInfo(t) : ""; } },
			{ section: "chop", say: "on a bar line: a chop on every 2nd step, all slice 1", caption: "Chop it.", act: async (u, c) => {
				c.chops = [...Array(c.L / 2).keys()].map(k => 2 * k); await u.glide(chopCell(0)); await atBar(clock.bar + 1, 300, true);
				for (const s of c.chops) { await u.glide(chopCell(s), 0.5, 0.5, 90); u.click(chopCell(s)); await sleep(30); }
			}, machine: c => ok(c.chops.every(s => trigsOf(c.pl).includes(s)), "player " + trigsOf(c.pl)), hold: 1500 },
			{ section: "solo", say: "Mix: solo the player and push its fader up, on a bar line", caption: "Solo the chops.", act: async (u, c) => {
				await look(u, tab("mix")); c.soloBar = await atBar(clock.bar + 1, 120, true); u.click(strip(c.pl, "s"));
				const f = `.strip[data-sel="${c.pl}"] .fader[data-g]`; c.v0 = kitVals(c.pl)[pidx(c.pl, "VOL", "rt")]; if (c.v0 < 127) { await u.glide(f); await u.drag(f, [[0, -20], [0, -50], [0, -90], [0, -140]], {}, { stepMs: 50 }); }
			}, machine: c => ok(mutes().length >= 12 && !mutes().includes(c.pl) && kitVals(c.pl)[pidx(c.pl, "VOL", "rt")] >= 120, `mutes ${mutes()}, VOL ${kitVals(c.pl)[pidx(c.pl, "VOL", "rt")]}`), within: 6000, hold: 4500 },
			{ section: "slice", say: "Sampler: six chops dragged to other slices, from a bar line", caption: "Slice it up.", act: async (u, c) => {
				await look(u, tab("sampler")); await sleep(200);
				c.slices = [[4, 9], [8, 3], [12, 13], [20, 5], [24, 11], [28, 15]].filter(([s]) => s < c.L);
				await u.glide(chopCell(c.slices[0][0])); await atBar(clock.bar + 1, 300, true);
				for (const [s, n] of c.slices) { await u.glide(chopCell(s), 0.5, 0.5, 160); const path = []; for (let k = 1; k <= n; k++) path.push([0, -6 * k - 2]); await u.drag(chopCell(s), path, {}, { stepMs: 18, settle: 60 }); }
			}, machine: c => { const st = locksOf(c.pl, pidx(c.pl, "STRT")); return ok(c.slices.every(([s, n]) => st.some(x => x[0] === s && Math.abs(x[1] - 8 * n) <= 8)), "STRT " + JSON.stringify(st)); }, within: 6000, hold: 6500 },
			{ section: "reverse", say: "on a bar line: two moved chops reversed", caption: "Reverse.", act: async (u, c) => {
				c.rev = [8, 24].filter(s => s < c.L); await u.glide(chopCell(c.rev[0])); await atBar(clock.bar + 1, 120, true);
				for (const s of c.rev) { await u.glide(chopCell(s), 0.5, 0.5, 200); u.click(chopCell(s), { alt: true }); u.cap("⌥ reverse"); await sleep(150); }
			}, screen: c => ok(c.rev.every(s => /REV/.test($1(chopCell(s))?.textContent || "")), "no REV shown"), within: 6000, hold: 3200 },
			{ section: "retrig", say: "on a bar line: the bar ends retriggered", caption: "Retrig.", act: async (u, c) => {
				c.rt = [14, 30].filter(s => s < c.L); await u.glide(chopCell(c.rt[0])); await atBar(clock.bar + 1, 120, true);
				for (const s of c.rt) { await u.glide(chopCell(s), 0.5, 0.5, 200); u.click(chopCell(s), { shift: true }); u.cap("⇧ retrig"); await sleep(150); }
			}, screen: c => ok(c.rt.every(s => /RTRG/.test($1(chopCell(s))?.textContent || "")), "no RTRG shown"), within: 6000, hold: 3200 },
			{ section: "groove", say: "Mix: un-solo on the bar line: the groove back under the chops", caption: "Bring the groove back.", act: async (u, c) => {
				await look(u, tab("mix")); await u.glide(strip(c.pl, "s")); c.unsoloBar = await atBar(clock.bar + 1, 70, true); u.click(strip(c.pl, "s"));
			}, machine: c => ok(same(mutes(), [c.r]), "mutes " + mutes() + " (the frozen recorder stays muted)"), within: 6000, hold: 7000 },
			{ section: "retrig-all", say: "Sound: Alt-drag the player's RTRG up over a bar: Control All, every track's same knob", caption: "Alt-drag: retrig the whole kit.", act: async (u, c) => {
				await look(u, tab("sound")); await look(u, rail(c.pl)); const q = '#main .pc[data-g="syn"][data-n="RTRG"]'; if (!$1(q)) throw new Error("no RTRG");
				c.k0 = allKitVals(); c.moved = 0; const path = []; for (let k = 1; k <= 8; k++) path.push([k, 0]);
				const watch = setInterval(() => { c.moved = Math.max(c.moved, allKitVals().filter((v, t) => !same(v, c.k0[t])).length); }, 50);
				await u.glide(q); c.allBar = await atBar(clock.bar + 1, 120, true);
				try { await u.drag(q, path, { alt: true }, { stepMs: 110 }); await sleep(800); } finally { clearInterval(watch); }
				c.note = `${c.moved} tracks moved; RTRG of the player ${kitVals(c.pl)[pidx(c.pl, "RTRG", "syn")]}`;
			}, machine: c => ok(c.moved >= 4, `the drag moved ${c.moved} tracks`), within: 6000, hold: 4500 },
			{ section: "retrig-all", say: "Cmd+Z on a bar line: back", caption: "Undo. Back in the pocket.", act: async (u, c) => { document.activeElement?.blur?.(); c.undoBar = await atBar(clock.bar + 1, 70, true); u.key("z", { cmd: true }); },
				machine: c => ok(same(allKitVals(), c.k0), "not all back"), within: 8000, hold: 3000 },
			{ section: "end", say: "the groove and the chops play on under the end card", act: async u => { await u.glide("#main", 0.5, 0.35, 700); }, machine: () => ok(tele.last?.playing, "stopped"), hold: 4000 }
		],
		async tidy(u, c) { clearInterval(c.envWatch); clock.on = false; if (V.playing) u.click("#play"); if (S.soloSet.size) setSolo(new Set()); }
	};
	/* demo-md-techniques: "Tight Sequencer", the Machinedrum techniques song (doc/modern-ux/DEMO-STORYBOARD.md, section 5).
	   Same bar clock and rules as demo-md-full; the steps say their storyboard bar. */
	const pickMachine = async (u, t, m) => {
		await look(u, rail(t)); await sleep(200); await look(u, "#machbtn"); await sleep(350);
		await look(u, `#machpop .mf[data-fam="${m.split("-")[0]}"]`); await sleep(300); await look(u, `#machpop .mk[data-mach="${m}"]`);
		await until(() => kit().tracks[t].machine === m && idle(), 6000);
	};
	/* drag values on the selected track, one after the other */
	const setSyn = async (u, t, vals) => { for (const [n, to] of vals) { await dragTo(u, snd(n), to); await until(idle, 2000); await sleep(150); } };
	const snd = n => `#main .pc[data-g="syn"][data-n="${n}"]`;
	const dragTo = async (u, q, to, ms = 60) => { const d = to - getV($1(q)); if (!d) return; await u.glide(q); await u.drag(q, [[Math.round(d / 3), 0], [Math.round(2 * d / 3), 0], [d + (to === 127 || to === 0 ? 10 * Math.sign(d) : 0), 0]], {}, { stepMs: ms }); };
	const synV = (t, n) => kitVals(t)[pidx(t, n, "syn")];
	/* GEN rolls until the rhythm suits the song (a generator is random): at most 4 rolls */
	const genUntil = async (u, t, good) => { for (let k = 0; k < 4; k++) { await genRoll(u, t); if (good(trigsOf(t).filter(s => s < 16))) return; await sleep(150); } };
	const hits = (lo, hi) => tr => tr.length >= lo && tr.length <= hi;
	const techniques = {
		name: "demo-md-techniques",
		card: "Machinedrum Editor|Free · pay what you want|Coming soon",
		setup: full.setup,
		steps: [
			/* ---- 0-4 intro ---- */
			{ section: "intro", say: "bar 0: the kit library, the pattern's kit loads", caption: "Start from an empty pattern.", act: async (u, c) => { await look(u, "#kitf"); await sleep(500); await look(u, ks(c.kit)); if (await until(dlgShown, 700)) u.click(dlgButton("Load without saving") || $1("#dlg .danger")); await sleep(400); u.key("Escape"); },
				machine: c => ok(currentKitSlot() === c.kit && idle(), "kit " + currentKitSlot()), within: 6000, hold: 300 },
			{ section: "intro", say: "PLAY: bar 1", act: async u => { await look(u, "#play"); }, machine: () => ok(tele.last?.playing, "not playing"), within: 4000 },
			{ section: "intro", say: "bar 2: GEN Euclid on the kick", caption: "Euclid kick, rotated snare.", act: async (u, c) => { await look(u, rail(c.T.kick)); await sleep(150); await look(u, '#genband [data-gen="fill"]'); await u.glide("#genband [data-rand]"); await atBar(2, 200); await genRoll(u, c.T.kick); },
				machine: c => ok(trigsOf(c.T.kick).length >= 3, "kick " + trigsOf(c.T.kick)) },
			{ section: "intro", say: "bar 3: GEN on the snare, rotated one step late", act: async (u, c) => { await look(u, rail(c.T.snare)); await sleep(150); await look(u, '#genband [data-gen="fill"]'); await u.glide("#genband [data-rand]"); await atBar(3, 200); await genUntil(u, c.T.snare, hits(1, 4)); c.s0 = trigsOf(c.T.snare); document.activeElement?.blur?.(); await sleep(250); u.key("ArrowRight", { alt: true }); },
				machine: c => ok(trigsOf(c.T.snare).length >= 1 && !same(trigsOf(c.T.snare), c.s0), "snare " + trigsOf(c.T.snare)) },
			{ section: "intro", say: "bar 4: Sound, the machine picker: GND-SIN on track 12, tuned down, a long decay; a note on the 1 and the 11", caption: "Pick the machine for the job.", act: async (u, c) => {
				c.bass = 11; await look(u, tab("sound")); await pickMachine(u, c.bass, "GND-SIN");
				await setSyn(u, c.bass, [["PTCH", 28], ["DEC", 90]]);
				Bridge.log(`DEMO bass after the drags: PTCH ${synV(c.bass, "PTCH")} DEC ${synV(c.bass, "DEC")}`);
				await look(u, tab("seq")); await atBar(5, 600); for (const s of [0, 10]) { await u.glide(cell(c.bass, s), 0.5, 0.5, 200); u.click(cell(c.bass, s)); await sleep(60); }
				await until(idle, 3000); await sleep(1500); Bridge.log(`DEMO bass after the trigs: PTCH ${synV(c.bass, "PTCH")} DEC ${synV(c.bass, "DEC")}`);
			}, machine: c => ok(kit().tracks[c.bass].machine === "GND-SIN" && [0, 10].every(s => trigsOf(c.bass).includes(s)) && synV(c.bass, "PTCH") === 28, `track 12 ${kit().tracks[c.bass].machine}, PTCH ${synV(c.bass, "PTCH")}, trigs ${trigsOf(c.bass)}`), within: 6000 },
			{ section: "intro", say: "bar 6: hats, GEN; R again for bar 7", act: async (u, c) => {
				await look(u, rail(c.T.hat)); await sleep(100); await look(u, '#genband [data-gen="fill"]');
				await u.glide("#genband [data-rand]"); await atBar(6, 200); await genUntil(u, c.T.hat, hits(4, 11));
				await atBar(7, 200); document.activeElement?.blur?.(); const h0 = trigsOf(c.T.hat); u.key("r"); await until(() => !same(trigsOf(c.T.hat), h0), 2000);
			}, machine: c => ok(trigsOf(c.T.hat).length >= 2, "hat " + trigsOf(c.T.hat)) },
			/* ---- 5-18 build ---- */
			{ section: "build", say: "bar 8: rim and cowbell by hand, an accent, a slide", caption: "Accent and slide by hand.", act: async (u, c) => {
				await atBar(8, 900);
				for (const [t, s] of [[c.T.rim, 3], [c.T.rim, 11], [c.T.bell, 6], [c.T.bell, 14]]) { await u.glide(cell(t, s), 0.5, 0.5, 200); u.click(cell(t, s)); await sleep(50); }
				await u.glide(cell(c.T.bell, 6)); u.click(cell(c.T.bell, 6), { shift: true }); u.cap("⇧ accent"); await sleep(350);
				await u.glide(cell(c.T.rim, 11)); u.click(cell(c.T.rim, 11), { alt: true, shift: true }); u.cap("⌥⇧ slide");
			}, machine: c => ok([3, 11].every(s => trigsOf(c.T.rim).includes(s)) && [6, 14].every(s => trigsOf(c.T.bell).includes(s)), `rim ${trigsOf(c.T.rim)} bell ${trigsOf(c.T.bell)}`), within: 6000 },
			{ section: "build", say: "bar 10: REC, a tom fill on the QWERTY keys, REC off at bar 11", caption: "Record the fill live.", act: async (u, c) => {
				await look(u, rail(c.T.tom)); c.tom0 = trigsOf(c.T.tom).length; document.activeElement?.blur?.();
				const b = await atBar(10, 200); u.key(" ", { alt: true });
				await until(() => clock.total >= (b - 1) * 16 + 11, 4000); for (const k of ["a", "s", "d", "f"]) { u.key(k); await sleep(125); }
				await atBar(b + 1, 60, true); u.key(" ", { alt: true });
				await sleep(300); if (!V.playing) u.click("#play");
			}, machine: c => ok(trigsOf(c.T.tom).length > c.tom0 && tele.last?.playing && !V.rec, `tom ${c.tom0} -> ${trigsOf(c.T.tom).length}`), within: 6000 },
			{ section: "build", say: "bar 12: LOCK PARAMETER DEC on the hat, a ramp across the lane", caption: "Lock decay to every step.", act: async (u, c) => {
				await look(u, rail(c.T.hat));
				const shown = e => e && e.getBoundingClientRect().width > 0, chip = () => [$1('#chips .pk[data-lane="DEC"]'), ...$all("#chips .pk[data-lane]")].find(shown);
				await until(() => chip(), 3000); const k = chip(); if (!k) throw new Error("no lock parameter key"); c.lane = k.dataset.lane; await sleep(300); await look(u, `#chips .pk[data-lane="${c.lane}"]`);
				c.on = trigsOf(c.T.hat).filter(s => s < 16); await u.glide(`#lane .lb[data-s="${c.on[0]}"]`, 0.5, 0.9); await atBar(12, 900);
				await u.drag(`#lane .lb[data-s="${c.on[0]}"]`, c.on.slice(1).map(s => `#lane .lb[data-s="${s}"]`), { shift: true }, { fy: 0.9, stepMs: 90 }); c.li = pidx(c.T.hat, c.lane);
			}, machine: c => ok(locksOf(c.T.hat, c.li).length >= 2, "locks " + JSON.stringify(locksOf(c.T.hat, c.li))), within: 8000 },
			{ section: "build", say: "bar 13: rotate the hats a step (off the beat); swing 58 %", caption: "Rotate. Add swing.", act: async (u, c) => {
				c.h0 = trigsOf(c.T.hat); document.activeElement?.blur?.(); await atBar(13, 200); u.key("ArrowRight", { alt: true });
				const q = '.l2.ed[data-l2="swing"]'; await u.glide(q); await u.drag(q, [[0, -6], [0, -12], [0, -18], [0, -24]], {}, { stepMs: 60 });
			}, machine: c => ok(!same(trigsOf(c.T.hat), c.h0) && V.swing >= 56, `hats ${trigsOf(c.T.hat)}, swing ${V.swing}`), within: 6000 },
			{ section: "build", say: "bar 15: Sound, the picker: E12-SD on track 15, its Retrig RTRG and RTIM; a trig on beat 4", caption: "Retrig makes the roll.", act: async (u, c) => {
				c.roll = 14; await look(u, tab("sound")); await pickMachine(u, c.roll, "E12-SD");
				await setSyn(u, c.roll, [["RTRG", 12], ["RTIM", 40], ["DEC", 60]]);
				c.rt = [synV(c.roll, "RTRG"), synV(c.roll, "RTIM")];
				await look(u, tab("seq")); await u.glide(cell(c.roll, 12)); await atBar(15, 300); u.click(cell(c.roll, 12));
			}, machine: c => ok(kit().tracks[c.roll].machine === "E12-SD" && trigsOf(c.roll).includes(12) && synV(c.roll, "RTRG") === c.rt[0] && c.rt[0] >= 8, `track 15 ${kit().tracks[c.roll].machine}, RTRG ${synV(c.roll, "RTRG")} RTIM ${synV(c.roll, "RTIM")}, trigs ${trigsOf(c.roll)}`), within: 6000, hold: 2500 },
			{ section: "build", say: "bar 17: Sound, the rim's LFO: target its own DEC, a ramp shape, SPD and DEPTH", caption: "An LFO moves the decay.", act: async (u, c) => {
				await look(u, tab("sound")); await look(u, rail(c.T.rim));
				/* the selects are the page's key-style dropdowns (a .kselbtn opens #kpop) */
				await u.glide('.kselbtn[data-for="lfoT"]'); await u.pick("lfoT", c.T.rim); await sleep(400);
				await u.glide('.kselbtn[data-for="lfoP"]'); await u.pick("lfoP", "DEC"); await sleep(400);
				await look(u, '[data-slot="SHP1"][data-shape="3"]'); await sleep(200);
				await dragTo(u, '#main .pc[data-g="lfo"][data-n="SPD"]', 60); await atBar(17, 300); await dragTo(u, '#main .pc[data-g="lfo"][data-n="DEPTH"]', 48, 90);
				c.lfo = JSON.stringify(V.tracks[c.T.rim].lfo);
			}, machine: c => { const l = V.tracks[c.T.rim].lfo || {}; return ok(l.TRCK === c.T.rim && l.PARAM === "DEC" && l.SHP1 === 3, "lfo " + JSON.stringify(l)); }, within: 6000, hold: 3000 },
			{ section: "build", say: "bar 19: MUTATE the cowbell; bar 21 ⌘Z; bar 22 again", caption: "Mutate it. Undo it.", act: async (u, c) => {
				await look(u, rail(c.T.bell)); c.b0 = kitVals(c.T.bell); await u.glide("#mutband [data-rand]"); await atBar(19, 150); u.click("#mutband [data-rand]");
				await until(() => !same(kitVals(c.T.bell), c.b0), 3000);
				await atBar(21, 150); document.activeElement?.blur?.(); u.key("z", { cmd: true }); await until(() => same(kitVals(c.T.bell), c.b0), 3000); c.undone = same(kitVals(c.T.bell), c.b0);
				await atBar(22, 150); u.click("#mutband [data-rand]");
			}, machine: c => ok(c.undone && !same(kitVals(c.T.bell), c.b0), `undo back ${c.undone}, kept ${!same(kitVals(c.T.bell), c.b0)}`), within: 6000 },
			/* ---- dub ---- */
			{ section: "dub", say: "bar 23: Mix, PAN the clap; bar 24 the snare's DEL send up and the echo's feedback; bar 25 down", caption: "One hit into the echo.", act: async (u, c) => {
				await look(u, tab("mix")); const pan = `.strip[data-sel="${c.T.clap}"] .pc[data-n="PAN"]`; await u.glide(pan); await atBar(23, 400); await u.drag(pan, [[-10, 0], [-20, 0], [-30, 0]], {}, { stepMs: 80 });
				const del = `.strip[data-sel="${c.T.snare}"] .pc[data-n="DEL"]`, fb = '#main .pc[data-g="mfx"][data-n="FB"]';
				c.d0 = getV($1(del)); await u.glide(del); await atBar(24, 500); await u.drag(del, [[30, 0], [60, 0], [90, 0]], {}, { stepMs: 60 });
				c.fb0 = $1(fb) ? getV($1(fb)) : null; if ($1(fb)) { await u.glide(fb); await u.drag(fb, [[6, 0], [12, 0]], {}, { stepMs: 80 }); }
				await u.glide(del); await atBar(25, 400); await u.drag(del, [[-30, 0], [-60, 0], [-90, 0]], {}, { stepMs: 60 }); if (c.fb0 != null) { await u.glide(fb); await u.drag(fb, [[-6, 0], [-12, 0]], {}, { stepMs: 80 }); }
			}, machine: c => ok(kitVals(c.T.snare)[pidx(c.T.snare, "DEL", "rt")] <= c.d0 + 4, "DEL " + kitVals(c.T.snare)[pidx(c.T.snare, "DEL", "rt")]), within: 6000 },
			{ section: "variation", say: "bars 26-28: A copied onto B; › queues B", caption: "Copy it. Queue the next pattern.", act: async (u, c) => {
				await look(u, "#pat"); await sleep(400); await u.glide(psq(c.A));
				await look(u, '#libpop [data-la="copy"]'); await sleep(300); await u.glide(psq(c.B)); u.key("ArrowRight"); await until(() => LIB.sel === c.B, 1000); await sleep(300);
				await look(u, '#libpop [data-la="paste"]'); if (await until(dlgShown, 1500)) u.click($1("#dlg .danger") || dlgButton("Paste"));
				await until(() => hasPat(c.B) && !dlgShown(), 4000); await sleep(300);
				for (let k = 0; k < 3 && !$1("#libpop").hidden; k++) { u.key("Escape"); await until(() => $1("#libpop").hidden, 600); }
				if (!$1("#libpop").hidden) closeLib(false);
				await u.glide("#patNext"); await atBar(28, 900); u.click("#patNext"); if (await until(dlgShown, 1200)) u.click(dlgButton("Keep the edits") || dlgButton("Cancel"));
			}, machine: c => ok(hasPat(c.B) && (desk().queued === c.B || currentPatternSlot() === c.B), `B ${hasPat(c.B)}, queued ${desk().queued}, pattern ${currentPatternSlot()}`), within: 4000 },
			{ section: "variation", say: "B plays on the same kit", machine: c => ok(currentPatternSlot() === c.B && currentKitSlot() === c.kit, `pattern ${currentPatternSlot()} kit ${currentKitSlot()}`), within: 6000 },
			/* ---- resample: the full groove first ---- */
			{ section: "resample", say: "Sampler › RAM 1: Set up sampling (recorder 13, player 14); Main mix, LEN one bar; the player's PTCH 64, HOLD and DEC 127", caption: "Sample your own beat.", act: async (u, c) => {
				await look(u, tab("sampler")); await sleep(200); await look(u, '.slotk[data-slot="RAM1"]'); await sleep(400); [c.r, c.pl] = setupTracks();
				const once = $1('[data-smponce="1"]'); if (once && getComputedStyle(once.parentElement).visibility !== "hidden") { await look(u, once); await sleep(200); }
				await look(u, "[data-setupgo]"); await until(() => kit().tracks[c.pl].machine === "RAM-P1" && idle(), 6000); await sleep(300);
				await look(u, '[data-recsrc="main"]'); await sleep(200);
				const R = n => `#main .smp2 .pc[data-g="syn"][data-n="${n}"][data-t="${c.r}"]`, P = n => `#main .smp2 .pc[data-g="syn"][data-n="${n}"][data-t="${c.pl}"]`;
				await dragTo(u, R("LEN"), 64); if ($1(R("RATE"))) await dragTo(u, R("RATE"), 127);
				for (const [n, to] of [["DEC", 127], ["HOLD", 127], ["PTCH", 64], ["STRT", 0]]) { await dragTo(u, P(n), to); await until(idle, 2000); await sleep(120); }
			}, machine: c => ok(kit().tracks[c.pl].machine === "RAM-P1" && sourceOf(c.r) === "main" && kitVals(c.r)[pidx(c.r, "VOL", "rt")] === 0 && synV(c.r, "LEN") === 64 && synV(c.pl, "PTCH") === 64 && synV(c.pl, "DEC") === 127, `player ${kit().tracks[c.pl].machine}, LEN ${synV(c.r, "LEN")}, PTCH ${synV(c.pl, "PTCH")} DEC ${synV(c.pl, "DEC")}`), within: 8000 },
			{ section: "resample", say: "Capture next loop on a bar's last steps: it records the next bar, then freezes", act: async u => { await u.glide("[data-capture]"); await until(() => clock.total % 16 === 10, 4000); u.click("[data-capture]"); },
				screen: () => ok(slotState(1) === "cap", "slot " + slotState(1)), within: 3000 },
			{ section: "resample", say: "it records the bar, freezes; the take", act: async u => { await u.glide(".ramwave .wavecv", 0.5, 0.5, 900); }, screen: () => ok(slotState(1) === "frozen" && inked($1("canvas.smpwave")) > 2000, "slot " + slotState(1)), machine: () => { const t = smpSlotOf("ram", 0); return ok(t && !t.empty, "no take"); }, within: 12000 },
			/* ---- break ---- */
			{ section: "break", say: "Mix: one drag across tracks 1-5's M keys on the bar line: the beat drops", caption: "One drag drops the beat.", act: async (u, c) => {
				await look(u, tab("mix")); c.five = [0, 1, 2, 3, 4]; await u.glide(strip(0, "m")); await until(() => clock.total % 16 === 13, 4000);
				await u.drag(strip(0, "m"), c.five.slice(1).map(i => strip(i, "m")), {}, { captured: false, stepMs: 45, settle: 50 });
			}, machine: c => ok(c.five.every(i => mutes().includes(i)), "mutes " + mutes()), hold: 1500 },
			{ section: "chops", say: "Sampler: a chop on every 2nd step, from a bar line", caption: "Chop it. Reverse. Retrig.", act: async (u, c) => {
				await look(u, tab("sampler")); c.chops = [0, 2, 4, 6, 8, 10, 12, 14]; await u.glide(chopCell(0)); await atBar(clock.bar + 1, 300, true);
				for (const s of c.chops) { await u.glide(chopCell(s), 0.5, 0.5, 110); u.click(chopCell(s)); await sleep(40); }
			}, machine: c => ok(c.chops.every(s => trigsOf(c.pl).includes(s)), "player " + trigsOf(c.pl)), hold: 1500 },
			{ section: "chops", say: "Mix: solo the player on a bar line: the chops alone", act: async (u, c) => { await look(u, tab("mix")); await u.glide(strip(c.pl, "s")); await atBar(clock.bar + 1, 120, true); u.click(strip(c.pl, "s")); },
				machine: c => ok(mutes().length >= 12 && !mutes().includes(c.pl), "mutes " + mutes()), hold: 3500 },
			{ section: "chops", say: "Sampler: four chops to other slices", act: async (u, c) => {
				await look(u, tab("sampler")); c.slices = [[2, 9], [6, 3], [10, 13], [12, 5]]; await u.glide(chopCell(2)); await atBar(clock.bar + 1, 300, true);
				for (const [s, n] of c.slices) { await u.glide(chopCell(s), 0.5, 0.5, 160); const path = []; for (let k = 1; k <= n; k++) path.push([0, -6 * k - 2]); await u.drag(chopCell(s), path, {}, { stepMs: 18, settle: 60 }); }
			}, machine: c => { const st = locksOf(c.pl, pidx(c.pl, "STRT")); return ok(c.slices.every(([s, n]) => st.some(x => x[0] === s && Math.abs(x[1] - 8 * n) <= 8)), "STRT " + JSON.stringify(st)); }, within: 6000, hold: 3500 },
			{ section: "chops", say: "on a bar line: two reversed, the last two retriggered", act: async (u, c) => {
				await u.glide(chopCell(6)); await atBar(clock.bar + 1, 120, true);
				for (const s of [6, 10]) { await u.glide(chopCell(s), 0.5, 0.5, 200); u.click(chopCell(s), { alt: true }); u.cap("⌥ reverse"); await sleep(120); }
				for (const s of [12, 14]) { await u.glide(chopCell(s), 0.5, 0.5, 200); u.click(chopCell(s), { shift: true }); u.cap("⇧ retrig"); await sleep(120); }
			}, screen: () => ok([6, 10].every(s => /REV/.test($1(chopCell(s))?.textContent || "")) && [12, 14].every(s => /RTRG/.test($1(chopCell(s))?.textContent || "")), "REV / RTRG not shown"), within: 6000, hold: 4000 },
			{ section: "chops", say: "the ROM slots' waveform tiles", caption: "48 ROM sounds, one click.", act: async u => { await look(u, ".slotk.rom.has, .slotk.rom"); await sleep(300); await u.glide(".romtiles", 0.5, 0.3, 900); },
				screen: () => ok($all(".romtile canvas.tw").length > 0, "no tiles"), hold: 1500 },
			/* ---- riser and drop ---- */
			{ section: "riser", say: "Mix: un-solo on a bar line (the break comes back with the chops); Sound: ⌥-drag the hat's FLTF up over 3 bars", caption: "Sweep the whole kit.", act: async (u, c) => {
				await look(u, tab("mix")); await u.glide(strip(c.pl, "s")); await atBar(clock.bar + 1, 70, true); u.click(strip(c.pl, "s"));
				await look(u, tab("sound")); await look(u, rail(c.T.hat)); const q = '#main .pc[data-g="fx"][data-n="FLTF"]'; c.k0 = allKitVals(); c.v0 = getV($1(q));
				const up = Math.min(50, 110 - c.v0), path = []; for (let k = 1; k <= 48; k++) path.push([Math.round(up * k / 48), 0]);
				await u.glide(q); await atBar(clock.bar + 1, 120, true); await u.drag(q, path, { alt: true }, { stepMs: 110 });
			}, machine: c => ok(allKitVals().filter((v, t) => !same(v, c.k0[t])).length >= 2 && !S.soloSet.size, "the sweep moved too few tracks") },
			{ section: "riser", say: "Sequence: ⇧-click tracks 1-5's M keys: armed", act: async (u, c) => { await look(u, tab("seq")); u.cap("⇧ hold"); for (const i of c.five) { await u.glide(railM(i), 0.5, 0.5, 160); u.click(railM(i), { shift: true }); await sleep(50); } },
				screen: c => ok(c.five.every(i => $1(railM(i)).classList.contains("prep")), "not armed"), within: 2000 },
			{ section: "drop", say: "⇧ up on the bar line, ⌘Z the sweep", caption: "Release on the bar.", act: async u => { await atBar(clock.bar + 1, 70, true); document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift", code: "ShiftLeft", bubbles: true })); u.cap("⇧ release"); document.activeElement?.blur?.(); u.key("z", { cmd: true }); },
				machine: c => ok(c.five.every(i => !mutes().includes(i)) && same(allKitVals(), c.k0), `mutes ${mutes()}, kit back ${same(allKitVals(), c.k0)}`), within: 6000, hold: 6000 },
			{ section: "drop", say: "Mix: solo the snare and the chops on a bar line; un-solo two bars later", caption: "Solo with a drag.", act: async (u, c) => {
				await look(u, tab("mix")); await u.glide(strip(c.T.snare, "s")); await atBar(clock.bar + 1, 150, true); u.click(strip(c.T.snare, "s")); await sleep(100); await look(u, strip(c.pl, "s"));
				await u.glide(strip(c.T.snare, "s")); await atBar(clock.bar + 2, 150, true); u.click(strip(c.T.snare, "s")); await sleep(100); await look(u, strip(c.pl, "s"));
			}, machine: c => ok(!S.soloSet.size && same(mutes(), [c.r]), "solo " + [...S.soloSet] + " mutes " + mutes()), within: 6000, hold: 2000 },
			{ section: "variation", say: "Sequence: GEN rolls the open hat; rotate it a bar later", caption: "Roll it again.", act: async (u, c) => {
				await look(u, tab("seq")); await look(u, rail(c.T.open)); await u.glide("#genband [data-rand]"); await atBar(clock.bar + 1, 200, true); await genRoll(u, c.T.open);
				c.o1 = trigsOf(c.T.open); document.activeElement?.blur?.(); await atBar(clock.bar + 1, 150, true); u.key("ArrowRight", { alt: true });
			}, machine: c => ok(!same(trigsOf(c.T.open), c.o1), "open hat not rotated"), hold: 2500 },
			/* ---- outro ---- */
			{ section: "outro", say: "Song: Chain", caption: "Chain the patterns.", act: async u => { await look(u, tab("song")); await look(u, '[data-set="songpick"] button[data-v="chain"]'); }, screen: () => ok(!!$1("[data-chainpad]"), "no pads") },
			{ section: "outro", say: "STOP on a bar line, chain pads A and B (loaded), PLAY: the chain starts at A", act: async (u, c) => {
				const b = c.A >> 4; if (S.bank !== b) { await look(u, `[data-bank="${b}"]`); await sleep(200); }
				await u.glide("#play"); await atBar(clock.bar + 1, 30, true); u.click("#play"); await until(() => !V.playing, 2000);
				await look(u, `[data-chainpad="${c.A}"]`); await sleep(150); await look(u, `[data-chainpad="${c.B}"]`);
				await until(() => desk().chain?.active, 3000); await look(u, "#play");
			}, machine: c => ok(desk().chain?.active && same(desk().chain.patterns, [c.A, c.B]) && tele.last?.playing && currentPatternSlot() === c.A, `chain ${JSON.stringify(desk().chain)}, pattern ${currentPatternSlot()}`), within: 5000 },
			{ section: "outro", say: "the chain moves on: B", act: async u => { await u.glide(".chainrow", 0.5, 0.5, 600); }, machine: c => ok(currentPatternSlot() === c.B, "pattern " + currentPatternSlot()), within: 6000 },
			{ section: "outro", say: "back to A", machine: c => ok(currentPatternSlot() === c.A, "pattern " + currentPatternSlot()), within: 6000 },
			{ section: "outro", say: "Sequence: mute perc, the chops, the hats and the bass, the kick, two bars apart", caption: "Strip it back.", act: async (u, c) => {
				await look(u, tab("seq")); const b0 = clock.bar + 1;
				for (const [k, ts] of [[c.T.rim, c.T.bell, c.roll], [c.pl], [c.T.hat, c.T.open, c.bass], [c.T.kick]].entries()) { await u.glide(railM(ts[0])); await atBar(b0 + 2 * k, 150, true); for (const t of ts) { u.click(railM(t)); await sleep(80); } }
			}, machine: c => ok([c.T.kick, c.T.hat, c.pl].every(t => mutes().includes(t)), "mutes " + mutes()), within: 4000, hold: 1500 },
			{ section: "outro", say: "⇧-click the muted tracks, ⇧ up on the bar line: everything back", caption: "End on the bar.", act: async (u, c) => {
				c.back = mutes().filter(t => t !== c.r); u.cap("⇧ hold"); for (const t of c.back) { await u.glide(railM(t), 0.5, 0.5, 140); u.click(railM(t), { shift: true }); await sleep(40); }
				await atBar(clock.bar + 1, 70, true); document.dispatchEvent(new KeyboardEvent("keyup", { key: "Shift", code: "ShiftLeft", bubbles: true })); u.cap("⇧ release");
			}, machine: c => ok(same(mutes(), [c.r]), "mutes " + mutes()), hold: 3600 },
			{ section: "outro", say: "STOP on the bar line", act: async u => { await u.glide("#play"); await atBar(clock.bar + 2, 30, true); u.click("#play"); }, machine: () => ok(tele.last && !tele.last.playing, "playing"), hold: 400 }
		],
		async tidy(u) { clock.on = false; if (desk().chain?.active) cmd("chainClear"); if (V.playing) u.click("#play"); if (S.soloSet.size) setSolo(new Set()); }
	};
	const demos = [groove, full, sampler, techniques];

	/* ---------- the page's neutral state between journeys ---------- */
	async function between(u) {
		for (let i = 0; i < 3 && dlgShown(); i++) { u.key("Escape"); await sleep(200); }
		if (LIB.open) { closeLib(false); await sleep(100); }
		for (const q of ["#globpop", "#keyspop", "#audiopop", "#machpop", "#kpop"]) if ($1(q) && !$1(q).hidden) { u.key("Escape"); await sleep(150); }
		if (!$1("#bootcard").hidden) $1('#bootcard [data-bootrom="close"]')?.click();
		if (V.rec) { u.click("#rec"); await sleep(300); }
		if (V.playing) { u.click("#play"); await until(() => !V.playing, 3000); }
		if (S.soloSet.size) { setSolo(new Set()); await sleep(200); }
		await until(idle, 4000);
		await sleep(500);
	}
	async function ready() {
		const up = await until(() => !!machineState().input && V.loaded && $1("#bootcard")?.hidden !== false, 120000);
		await until(() => !desk().loading, 90000);
		await sleep(1500);
		return up;
	}
	/* what the page last told the person: its toast and its error line */
	const context = () => [...[["toast", $1("#toast")], ["error", $1("#errline")]].filter(([, e]) => e && !e.hidden && e.textContent.trim()).map(([k, e]) => `${k} "${e.textContent.trim().slice(0, 160)}"`),
		...results.filter(r => r.ok === false).slice(-3).map(r => `refused ${r.op}: ${(r.errors || []).join("; ")}`)].join(", ");
	if (/[?&]selftest=journey/.test(location.search)) setTimeout(() => Journey.run(all, { log: t => Bridge.log(t), ready, between, context }), 0);
	if (/[?&]selftest=demo/.test(location.search)) setTimeout(() => Journey.demo(demos, { log: t => Bridge.log(t), ready, between, context }), 0);
	return { all, demos };
})();
