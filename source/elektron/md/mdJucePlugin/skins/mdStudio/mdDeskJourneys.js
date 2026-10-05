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
	const tele = { last: null, steps: [] }, results = [];
	/* the documents of one slot as they arrive (a failing step says what came) */
	const trace = { kind: null, slot: null, seen: [] };
	const traceDocs = (kind, slot) => Object.assign(trace, { kind, slot, seen: [] });
	const traced = () => trace.seen.join(" | ") || "no document arrived";
	Bridge.onMessage(m => {
		if (m.type === "doc" && m.kind === trace.kind && m.slot === trace.slot && trace.seen.length < 12) trace.seen.push(`${Math.round(performance.now())} ms${m.pending ? " pending" : ""}: ${trace.kind === "kit" ? JSON.stringify(m.doc?.name) : (m.doc?.tracks || []).reduce((n, t) => n + t.trigs.length, 0) + " trigs"}`);
		if (m.type === "telemetry") { tele.last = m; if (m.playing) { tele.steps.push(m.step); if (tele.steps.length > 64) tele.steps.shift(); } }
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
			{ say: "open the editor: the start-up card covers the window while the firmware starts", within: 30000, screen: () => ok(Boot.state() === "booting" && !$1("#bootcard").hidden, "card " + Boot.state()) },
			{ say: "click PLAY under the card: it is not pressed", act: (u, c) => { const r = $1("#play").getBoundingClientRect(), top = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); c.cover = top?.closest("#bootcard") ? "card" : top?.id === "modalbg" ? "backdrop" : (top?.id || top?.className); if (c.cover !== "card" && c.cover !== "backdrop") throw new Error("PLAY is reachable: " + c.cover); top.dispatchEvent(new MouseEvent("click", { bubbles: true })); c.note = "covered by the " + c.cover; },
				screen: () => ok(!V.playing, "playing"), machine: () => ok(!tele.last?.playing, "the machine plays"), within: 800 },
			{ say: "the card shows the firmware's own LCD", screen: () => { const g = $1("#bootlcd")?.getContext("2d")?.getImageData(0, 0, 128, 64).data; if (!g) return "no LCD"; let n = 0; for (let i = 0; i < g.length; i += 4) if (g[i] !== g[0] || g[i + 1] !== g[1] || g[i + 2] !== g[2]) n++; return ok(n > 50, n + " pixels"); }, within: 15000 },
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
	const tempoDrag = {
		name: "md-top-tempo-drag",
		steps: [
			{ say: "drag the BPM on the LCD up", act: async (u, c) => { c.b0 = Docs.global.tempo; await u.drag("#bpm", [[0, -6], [0, -12], [0, -18], [0, -24]]); c.note = `${c.b0} -> ${V.bpm}`; },
				screen: c => ok(parseFloat($1("#bpm").textContent) === V.bpm && V.bpm > c.b0, "LCD " + $1("#bpm").textContent), machine: c => ok(Docs.global.tempo > c.b0 && Docs.global.tempo === V.bpm, "tempo " + Docs.global.tempo) },
			{ say: "drag it back down as far", act: u => u.drag("#bpm", [[0, 6], [0, 12], [0, 18], [0, 24]]), screen: c => ok(parseFloat($1("#bpm").textContent) === c.b0, "LCD " + $1("#bpm").textContent), machine: c => ok(Docs.global.tempo === c.b0, "tempo " + Docs.global.tempo) }
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
	const helpKeys = {
		name: "md-keys-help",
		steps: [
			{ say: "press ?: the list of keys", act: u => { document.activeElement?.blur?.(); u.key("?", { shift: true }); }, screen: () => ok(!$1("#keyspop").hidden && $all("#keyspop .keyrow").length > 20 && $all("#keyspop h3").length >= 6, $all("#keyspop .keyrow").length + " keys") },
			{ say: "press Escape: it closes", act: u => u.key("Escape"), screen: () => ok($1("#keyspop").hidden, "still open") }
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
			{ say: "alt-click it: slide", act: (u, c) => u.click(cell(c.t, c.s), { alt: true }), screen: c => ok($1(cell(c.t, c.s)).classList.contains("sl"), "no slide shown"), machine: c => ok(flagsOf(c.t, "slide").includes(c.s) || (V.slideAll && (pat().slide?.steps || []).includes(c.s)), `slide ${flagsOf(c.t, "slide").join(",")}, pattern-wide ${V.slideAll ? (pat().slide?.steps || []).join(",") : "off"}`) },
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
	const copyPaste = {
		name: "md-seq-copy-paste-clear",
		steps: [
			go("seq"),
			{ say: "pick a track with trigs and press Cmd+C", act: async (u, c) => { c.a = soundTracks().find(t => trigsOf(t).length) ?? 0; c.b = soundTracks().find(t => t !== c.a && !same(trigsOf(t), trigsOf(c.a))) ?? (c.a + 1) % 16; c.ta = trigsOf(c.a); c.tb = trigsOf(c.b); u.click(rail(c.a)); await sleep(200); document.activeElement?.blur?.(); u.key("c", { cmd: true }); }, machine: () => ok(V.clipboard.steps, "nothing copied") },
			{ say: "pick another track and press Cmd+V", act: async (u, c) => { u.click(rail(c.b)); await sleep(200); document.activeElement?.blur?.(); u.key("v", { cmd: true }); }, screen: c => ok(gridShows(c.b) && same(lit(c.b), c.ta.filter(s => s < V.len)), "grid " + lit(c.b).join(",")), machine: c => ok(same(trigsOf(c.b), c.ta), "pattern " + trigsOf(c.b).join(",")) },
			{ say: "press Delete: the track's steps are cleared", act: u => u.key("Delete"), screen: c => ok(!lit(c.b).length, "lit " + lit(c.b).join(",")), machine: c => ok(!trigsOf(c.b).length, "pattern " + trigsOf(c.b).join(",")) },
			{ say: "Cmd+Z twice: the track is as before", act: async u => { u.key("z", { cmd: true }); await sleep(800); u.key("z", { cmd: true }); }, machine: c => ok(same(trigsOf(c.b), c.tb), "pattern " + trigsOf(c.b).join(",")), within: 8000 }
		]
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
			{ say: "Cmd-click an empty step: every 2nd step from there comes on", act: (u, c) => { c.t0 = trigsOf(c.t); c.s = [...Array(V.len).keys()].find(s => s >= V.len - 8 && !c.t0.includes(s)) ?? V.len - 8; u.click(cell(c.t, c.s), { cmd: true }); },
				machine: c => { const want = []; for (let s = c.s; s < V.len; s += 2) want.push(s); return ok(want.every(s => trigsOf(c.t).includes(s)), "pattern " + trigsOf(c.t).join(",")); }, screen: c => ok(pressed(cell(c.t, c.s)) && pressed(cell(c.t, c.s + 2)), "not lit") },
			{ ...undoKey, machine: c => ok(same(trigsOf(c.t), c.t0), "pattern " + trigsOf(c.t).join(",")) }
		]
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
			{ say: "Alt-drag an effects value: every track's knob moves", act: async (u, c) => { c.k0 = allKitVals(); const el = $1('#main .pc[data-g="fx"]'), d = getV(el) > 64 ? -1 : 1; await u.drag(el, [[d * 4, 0], [d * 8, 0], [d * 16, 0], [d * 24, 0]], { alt: true }); },
				machine: c => { const moved = allKitVals().filter((v, t) => !same(v, c.k0[t])).length; return ok(moved >= 2, moved + " tracks moved"); }, within: 10000 },
			{ ...undoKey, say: "press Cmd+Z: one step back for all", machine: c => ok(same(allKitVals(), c.k0), "not all back"), within: 10000 }
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
			{ say: "click Set up sampling: a recorder and a player", act: (u, c) => { [c.r, c.p] = setupTracks(); c.m0 = [kit().tracks[c.r].machine, kit().tracks[c.p].machine]; u.click("[data-setupgo]"); },
				machine: c => ok(kit().tracks[c.r].machine === "RAM-R" + c.n && kit().tracks[c.p].machine === "RAM-P" + c.n, `tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.p].machine}`), within: 8000 },
			{ ...undoKey, say: "press Cmd+Z: one step back", machine: c => ok(same([kit().tracks[c.r].machine, kit().tracks[c.p].machine], c.m0), `tracks ${kit().tracks[c.r].machine} ${kit().tracks[c.p].machine}`), within: 8000 }
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
			{ say: "press Cmd+V: both get the steps", act: u => u.key("v", { cmd: true }), machine: c => ok(c.bs.every(t => same(trigsOf(t), c.ta)), c.bs.map(t => `T${t + 1} ${trigsOf(t).join(",")}`).join("; ")), within: 8000 },
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
	const panBox = {
		name: "md-mix-pan-undo",
		steps: [
			go("mix"),
			{ say: "drag a track's PAN box sideways", act: async (u, c) => { c.t = soundTrack(); c.i = pidx(c.t, "PAN", "rt"); c.v0 = kitVals(c.t)[c.i]; await nudge(u, `.strip[data-sel="${c.t}"] .pc[data-n="PAN"]`, c.v0); },
				machine: c => ok(kitVals(c.t)[c.i] !== c.v0, "kit PAN " + kitVals(c.t)[c.i]), screen: c => ok($1(`.strip[data-sel="${c.t}"] .pc[data-n="PAN"] b`)?.textContent !== String(c.v0 - 64), "shows " + $1(`.strip[data-sel="${c.t}"] .pc[data-n="PAN"] b`)?.textContent) },
			{ ...undoKey, machine: c => ok(kitVals(c.t)[c.i] === c.v0, "kit PAN " + kitVals(c.t)[c.i]) }
		]
	};


	const all = [bootCard, firstBeat, spaceTransport, tempoDrag, tapTempo, tapTempoB, patStep, queuePattern, plate, wsKeys, helpKeys, undoRedo,
		paintUndo, accentSlide, lockLane, pagesJ, copyPaste, clearPatternJ, fillEveryJ, rotateJ, rotateUndo, trackKeys, muteKeys, liveRec,
		genJourney("md-gen-mutate-undo", false), genJourney("md-gen-defaults-mutate-undo", true), genKeys,
		shapeSound, arrows, machinePick, soundCopy, editorDrag, controlAll,
		mixSolo, shiftMutes, allOff, fader, outKey, masterFx,
		songArrange, songChain, samplerSlots, samplerSetup, audition,
		libDialog, kitCopy, kitRename, kitClear, patGo, patClear, dialogEsc,
		globalJ, globalRouting, audioPanel, romCard, notePlay,
		lockRamp, pasteMany, mutScope, songInspector, songDrag, ramView, panBox, hwNoMachine];

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
	return { all };
})();
