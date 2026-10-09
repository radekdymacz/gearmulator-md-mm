"use strict";
/* MD Desk model: the contract documents the plug-in publishes, and the view
   the page renders from them. Documents are firmware truth (or the desk's
   working kit); the view is derived from them after every change and may be
   edited optimistically, but every edit is also sent as a command and the
   next document replaces the view. See doc/modern-ux/data-contract.md. */
/* The document shape, written once (P6): what an engine publishes, and what starts over when the
   engine changes ("reset"). kits: the stored slots; workingKit: the kit that plays ({slot, source,
   pending, doc}, the slot it was loaded from); sources: each stored document's "source"
   ("kit:3" -> "dump"); telemetry: the last telemetry notice (the transport and the playhead).
   The catalogue and the MIDI learn document are the model's and the plug-in's: they stay. */
const emptyDocs = () => ({ patterns: {}, kits: {}, songs: {}, global: null, machine: null, workingKit: null, sources: {}, telemetry: null, samples: null });
const EMPTY_DOCS = Object.freeze(emptyDocs());
/* host: the plug-in's "host" message (B-030: a DAW's tempo, {bpm, follows}); like the catalogue it stays */
const Docs = Object.assign(emptyDocs(), { catalogue: null, learn: null, host: null });
/* the engine changed: its documents start over */
function resetDocs(docs) { Object.assign(docs, emptyDocs()); }
/* storeDoc and DOC_STORE (where an incoming document is kept) are skins/shared/deskDocs.js */

/* ---- machine catalogue (md-desk/machines, from elektronData::mdMachineParamNames) ----
   Its index is derived from the document (once per document); Cat is the page's current one. The functions
   over it take it as an argument (cat): the model reads no catalogue of its own. */
const EMPTY_CAT = { byName: {}, byModel: {}, list: [], qparams: new Map(), facts: new Map() };
const catIndexes = new WeakMap();
function catalogueIndex(doc) {
	if (!doc) return EMPTY_CAT;
	let c = catIndexes.get(doc);
	if (c) return c;
	c = { byName: {}, byModel: {}, list: doc.machines, qparams: new Map(), facts: new Map() };
	for (const m of doc.machines) { c.byName[m.machine] = m; c.byModel[m.model] = m; }
	catIndexes.set(doc, c);
	return c;
}
let Cat = EMPTY_CAT;
function setCatalogue(doc) {
	Docs.catalogue = doc;
	Cat = catalogueIndex(doc);
}
const FX = ["AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR"];
const RT = ["DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"];
const SMPL = ["PTCH", "DEC", "HOLD", "BRR", "STRT", "END", "RTRG", "RTIM"];
const RAMR = ["MLEV", "MBAL", "ILEV", "IBAL", "CUE1", "CUE2", "LEN", "RATE"];
function slots(m, cat) {
	const c = cat.byName[m];
	if (!c) return [null, null, null, null, null, null, null, null, ...FX, ...RT];
	/* P5: a synthesis parameter with the name of an effects or routing one (TRX-XT DIST, TRX-MA REV,
	   INP-GA VOL) is its own parameter: the page addresses it as SYN·NAME, so every lock lane, value and
	   command maps to one (page, index). The other is shown as RTG·NAME / FX·NAME (laneLabel). */
	if (!cat.qparams.has(m)) { const later = c.params.slice(8); cat.qparams.set(m, c.params.map((n, i) => i < 8 && n && later.includes(n) ? "SYN·" + n : n)); }
	return cat.qparams.get(m);
}
/* a lane's (or a knob's) label on machine m: n, or FX·n / RTG·n where its synthesis page has an n too */
function laneLabel(m, n, cat) { const a = slots(m, cat); if (!a.includes("SYN·" + n)) return n; return (a.indexOf(n) >= 16 ? "RTG·" : "FX·") + n; }
/* Three pages of 8 slots; an unused slot is null. */
function pages(m, cat) { const a = slots(m, cat); return { s: a.slice(0, 8), e: a.slice(8, 16), r: a.slice(16, 24) }; }
const names = list => list.filter(Boolean);
/* Control All (the tweak command, manual p.37: FUNCTION + a DATA ENTRY knob): the view's writes for knob 0-7
   of group g ("syn", "fx", "rt") moved by d on every track the machine's gesture reaches, as the model's
   controlAllReaches (mdDeskEdit.cpp, measured on the firmware; machineFacts' reach): never MIDI or CTR
   machines, a RAM recorder not on its synthesis page; held at 0..127. The view writes the values it shows (a machine without a name
   for the knob moves too; its value comes with the document). The LFO section shows LFOS, LFOD, LFOM as
   SPD, DEPTH, SHMIX (lfoParams). */
const TWEAK_PAGES = { syn: "s", fx: "e", rt: "r" }, TWEAK_BASE = { syn: 0, fx: 8, rt: 16 };
function tweakWrites(v, g, knob, d, E, cat) {
	const out = [], lfoName = Object.keys(E.lfoParams).find(n => E.lfoParams[n] === 16 + knob);
	v.tracks.forEach((tr, t) => {
		if (!machineFacts(tr.m, cat).reach[g]) return;
		const n = slots(tr.m, cat)[TWEAK_BASE[g] + knob];
		if (!n || !(n in tr[g])) return;
		const to = Math.max(0, Math.min(127, tr[g][n] + d));
		out.push([["tracks", t, g, n], to]);
		if (g === "rt" && lfoName && tr.lfo) out.push([["tracks", t, "lfo", lfoName], to]);
	});
	return out;
}
/* Family keys: the picker's (TRX EFM E12 PI GND INP MID CTR ROM RAM) and the colour's (ROM/RAM = SMP). */
function famKey(m, cat) { const c = cat.byName[m]; const f = c ? c.family : m.split("-")[0]; return f === "P-I" ? "PI" : f; }
function famOf(m, cat) { const f = famKey(m, cat); return f === "ROM" || f === "RAM" ? "SMP" : f; }
function codeOf(m, cat) { const c = cat.byName[m]; return c ? m.slice(c.family.length + 1) : m.split("-").slice(1).join("-"); }
/* What the editor asks of a machine, derived once per catalogue (finding 16 of DESIGN-REVIEW-2026-10-02.md:
   a record, not a test of the name at each place that asks):
     family  the catalogue's ("TRX" "EFM" "E12" "P-I" "GND" "INP" "MID" "CTR" "ROM" "RAM"); key: the picker's
             (P-I = PI, famKey); code: the name without its family ("BD", "05", "R1")
     sampler plays a sample slot (ROM-nn, RAM-Pn); player: a RAM-Pn; recorder: a RAM-Rn
     slot    the sample slot it plays or records: ["rom", 0-47] or ["ram", 0-3]; null for the rest
     audio   makes sound of its own (not MID or CTR); masterFx: a CTR machine that drives a master effect
     reach   the pages Control All moves on it, {syn, fx, rt} (mdDeskEdit.cpp controlAllReaches: never MID or
             CTR, a RAM recorder not on its synthesis page)
   A machine the catalogue does not know (none yet) gets the same record from its name, not kept. */
function machineFacts(m, cat) {
	const known = cat.facts.get(m); if (known) return known;
	const family = cat.byName[m] ? cat.byName[m].family : m.split("-")[0], key = famKey(m, cat), code = codeOf(m, cat);
	const rom = family === "ROM", ram = family === "RAM", player = ram && code[0] === "P", recorder = ram && code[0] === "R";
	const audio = family !== "MID" && family !== "CTR";
	const f = Object.freeze({ family, key, code, sampler: rom || player, player, recorder,
		slot: rom ? ["rom", +code - 1] : ram ? ["ram", +code.slice(1) - 1] : null,
		audio, masterFx: family === "CTR" && ["RE", "GB", "EQ", "DX"].includes(code),
		reach: Object.freeze({ syn: audio && !recorder, fx: audio, rt: audio }) });
	if (cat.byName[m]) cat.facts.set(m, f);
	return f;
}
/* Full English names of the machines' codes (P6: moved from mdDeskApp.js so the model never calls
   up into the app; mdDeskModelTest.js then exercises this nameOf, not a stub). */
const FULL = { BD: "Bass drum", B2: "Bass drum 2", SD: "Snare drum", XT: "Tom", MT: "Tom", CP: "Clap", RS: "Rim shot", CB: "Cow bell", CH: "Closed hihat", OH: "Open hihat", CY: "Cymbal", MA: "Maracas", CL: "Claves", XC: "Congas", HH: "Hihat", HT: "High tom", LT: "Low tom", RC: "Ride cymbal", CC: "Crash cymbal", BR: "Brushed snare", TA: "Tambourine", TR: "Triangle", SH: "Shaker", BC: "Bongo conga", ML: "Metallica", SIN: "Sinus", NS: "Noise", IM: "Impulse", EMPTY: "Empty", GA: "Input gate A", GB: "Input gate B", FA: "Filter follower A", FB: "Filter follower B", EA: "Input envelope A", EB: "Input envelope B", AL: "Control all", "8P": "Control 8 parameters", RE: "Control rhythm echo", GB2: "Control gate box", EQ: "Control master EQ", DX: "Control Dynamix" };
function nameOf(m, cat) {
	const f = famKey(m, cat), c = codeOf(m, cat);
	if (f === "MID") return "MIDI channel " + (+c);
	if (f === "ROM") return "ROM sample " + c;
	if (/^R\d/.test(c) && f === "RAM") return "RAM record " + c.slice(1);
	if (/^P\d/.test(c) && f === "RAM") return "RAM play " + c.slice(1);
	if (f === "CTR" && c === "GB") return FULL.GB2;
	return FULL[c] || m;
}

/* ---- units (mdValidate.h) ---- */
const swingPercent = v => 50 + Math.round(v * 50 / 16384);
const accentDisplay = v => Math.round(v * 15 / 127);
const patName = p => "ABCDEFGH"[p >> 4] + String((p & 15) + 1).padStart(2, "0");
/* ---- the catalogue's enumerations (P6: the C++ tables, one source) ----
   outputs, masterFx (the kit's effect names), lfoFields (the lfo command's fields), lfoUpdates,
   lfoParams ({SPD, DEPTH, SHMIX}: kit parameter indexes), tempoMultipliers. The page pairs its own
   words with them by position: its master effects echo gate eq dyn are masterFx in order, and its
   LFO fields TRCK PARAM SHP1 SHP2 UPDTE are lfoFields in order. Before the catalogue: empty. */
const NO_ENUMS = Object.freeze({ outputs: [], masterFx: [], lfoFields: [], lfoUpdates: [], lfoParams: {}, tempoMultipliers: [] });
const enumsOf = docs => (docs.catalogue && docs.catalogue.enums) || NO_ENUMS;
const Enums = () => enumsOf(Docs);
const MFX_IDS = ["echo", "gate", "eq", "dyn"], LFO_NAMES = ["TRCK", "PARAM", "SHP1", "SHP2", "UPDTE"];
const mfxName = (id, E = Enums()) => E.masterFx[MFX_IDS.indexOf(id)];
const lfoField = (n, E = Enums()) => { const i = LFO_NAMES.indexOf(n); return i < 0 ? undefined : E.lfoFields[i]; };
/* a tempo multiplier's factor: "3/4X" -> 0.75 */
const multFactor = m => { const [a, b] = String(m || "1X").replace("X", "").split("/").map(Number); return b ? a / b : a || 1; };
/* a kit track's parameter 0-23 (synthesis, effects, routing) */
const paramOf = (kt, i) => i < 8 ? kt.synth[i] : i < 16 ? kt.effects[i - 8] : kt.routing[i - 16];

/* ---- document accessors: pure, of a set of documents; the page's are of Docs ---- */
function machineOf(docs) { return docs.machine || { pattern: {}, kit: {}, song: {}, desk: {} }; }
function patternSlotOf(docs) { const m = machineOf(docs); return m.pattern && m.pattern.current != null ? m.pattern.current : 0; }
function kitSlotOf(docs) {
	const m = machineOf(docs);
	if (m.kit && m.kit.current != null) return m.kit.current;
	const p = docs.patterns[patternSlotOf(docs)];
	return p ? p.kit : 0;
}
/* The current kit's document: the working kit when it is the current slot's, else the stored slot
   (until the first working kit arrives, and while a kit change has not brought the new one yet). */
function workingKitOf(docs) { const w = docs.workingKit; return w && w.slot === kitSlotOf(docs) ? w : null; }
/* A kit name as text: printable ASCII only (the core's kitNameText, mdDeskLibrary.h). A slot never
   written holds the battery RAM's bytes (OS 1.63: DEL 0x7f, or 0x7f then left-overs like "MX KIT 1"):
   no name, so the library shows it EMPTY. */
function kitNameText(n) { return typeof n === "string" && /^[\x20-\x7e]*$/.test(n) ? n : ""; }
function kitDocOf(docs) { const w = workingKitOf(docs); return w ? w.doc : docs.kits[kitSlotOf(docs)]; }
function kitSourceOf(docs) { const w = workingKitOf(docs); return w ? w.source : docs.sources["kit:" + kitSlotOf(docs)] || "none"; }
function songSlotOf(docs) { const m = machineOf(docs); return m.song && m.song.current != null ? m.song.current : 0; }
function lengthIn(docs, p) { const d = docs.patterns[p]; return d ? d.length : 16; }
/* the transport: one source, the telemetry (stopped until it comes) */
function transportOf(docs) { const t = docs.telemetry; return { playing: !!(t && t.playing), rec: !!(t && t.recording) }; }
/* What the machine plays (the Song page says it): its own chain (machine.desk.chain) while one is
   active, also in SONG mode (the firmware plays a chain there and stays in SONG mode); else the song
   in SONG mode (songMode, song.current); else the current pattern. A long chain is shortened in the
   label (first two, last); patterns has it all. songMode null (not reported yet) counts as pattern. */
function playsOf(docs) {
	const m = machineOf(docs), c = m.desk && m.desk.chain;
	if (c && c.active && c.patterns.length) {
		const p = c.patterns.map(patName), short = p.length > 4 ? [p[0], p[1], "…", p[p.length - 1]] : p;
		return { kind: "chain", label: "CHAIN " + short.join("»"), patterns: c.patterns.slice() };
	}
	if (m.songMode === true) { const s = songSlotOf(docs); return { kind: "song", label: "SONG " + String(s + 1).padStart(2, "0"), song: s }; }
	const p = patternSlotOf(docs);
	return { kind: "pattern", label: "PATTERN " + patName(p), pattern: p };
}
const machineState = () => machineOf(Docs);
const currentPatternSlot = () => patternSlotOf(Docs);
const currentKitSlot = () => kitSlotOf(Docs);
const currentSongSlot = () => songSlotOf(Docs);
function stepSet(list) { return new Set(list || []); }

/* ---- contract song rows <-> the view's rows ---- */
function rowFromContract(r, i, lengthOf) {
	switch (r.kind) {
	case "pattern": {
		const full = lengthOf(r.pattern);
		const row = { pat: r.pattern, rep: r.repeats + 1 };
		if (r.start !== 0 || r.end !== full) { row.ofs = r.start; row.len = r.end - r.start; }
		if (r.tempo != null) row.bpm = r.tempo;
		if (r.mutes && r.mutes.length) row.mutes = r.mutes.slice();
		return row;
	}
	case "loop": return { type: "loop", to: r.target, count: r.repeats === 0 ? Infinity : r.repeats + 1 };
	case "jump": return { type: "jump", to: r.target };
	case "halt": return { type: "halt", to: i };
	default: return { type: "end" };
	}
}
function rowToContract(r, lengthOf) {
	if (!r.type) {
		const start = r.ofs || 0, full = lengthOf(r.pat);
		const end = r.len != null ? start + r.len : full;
		return { kind: "pattern", pattern: r.pat, repeats: Math.max(0, Math.min(63, r.rep - 1)), start, end: Math.max(start + 1, Math.min(64, end)),
			tempo: r.bpm != null ? r.bpm : null, mutes: (r.mutes || []).slice().sort((a, b) => a - b) };
	}
	if (r.type === "loop") return { kind: "loop", target: r.to, repeats: r.count === Infinity ? 0 : Math.max(1, Math.min(63, r.count - 1)) };
	if (r.type === "jump") return { kind: "jump", target: r.to };
	if (r.type === "halt") return { kind: "halt" };
	return { kind: "end" };
}

/* ---- the view (P6): a value derived from the documents, what the renderers read ----
   deriveView(docs, ui) is pure: the documents (the catalogue and the telemetry included) and the
   few UI facts it needs (the page-only solos) in, a new view out. The page renders
   V = view(): the optimistic overlay (below) over deriveView(Docs, S). The UI's own state
   (workspace, selection, zoom...) is S. */
const MFXD = {
	echo: { name: "Rhythm Echo", sub: "Tempo-synced delay. TIME is in 128th notes, so 64 is two beats.", k: ["TIME", "MOD", "MFRQ", "FB", "FILTF", "FILTW", "MONO", "LEV"] },
	gate: { name: "Gate Box", sub: "Reverb for percussion. GATE at 127 turns the gate off.", k: ["DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"] },
	eq: { name: "Master EQ", sub: "Low shelf, high shelf, one parametric band. Drag the points.", k: ["LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"] },
	dyn: { name: "Dynamix", sub: "Compressor on the main out. Drag threshold and ratio.", k: ["ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"] } };
function lengthOfPattern(p) { return lengthIn(Docs, p); }
/* what the engine can do: only a capability published as true (an unknown name is not allowed) */
const canDo = (v, cap) => v.caps.can[cap] === true;
function deriveView(docs, ui) {
	const cat = catalogueIndex(docs.catalogue);
	const pat = patternSlotOf(docs), kit = kitSlotOf(docs), songSlot = songSlotOf(docs);
	const P = docs.patterns[pat];
	const K = kitDocOf(docs);
	const G = docs.global;
	const M = machineOf(docs);
	const desk = M.desk || {};
	const hist = M.history || {};
	const transport = transportOf(docs);
	const E = enumsOf(docs);
	/* capabilities (nested): what the engine can do (can), why not (reasons), named values; a name
	   missing from can is not allowed (canDo) */
	const caps = Object.assign({ engine: "", label: "", about: "" }, M.capabilities || {});
	caps.can = caps.can || {}; caps.reasons = caps.reasons || {}; caps.values = caps.values || {};
	const v = { loaded: !!(P && K), pat, queued: desk.queued != null ? desk.queued : null, kit,
		kitState: M.kit && M.kit.working || "unknown", kitSource: kitSourceOf(docs), kitNames: {},
		patKit: Array.from({ length: 128 }, (_, p) => docs.patterns[p] ? docs.patterns[p].kit : null),
		mode: (G ? G.extendedMode : true) ? "EXTENDED" : "CLASSIC",
		bpm: G ? G.tempo : 120, playing: transport.playing, rec: transport.rec, gridEdit: !!desk.gridEdit, tx: !!desk.tx,
		songMode: M.songMode === true ? true : M.songMode === false ? false : null,
		roundTrip: desk.roundTripMs, lifecycle: M.lifecycle || "booting", lifecycleText: M.lifecycleText || "",
		input: !!M.input, midi: !!M.midi, caps,
		clipboard: Object.assign({ steps: false, sound: false, songRow: false, kit: null, pattern: null }, M.clipboard || {}),
		canUndo: !!hist.undo, canRedo: !!hist.redo, undoCount: hist.undoCount || 0, redoCount: hist.redoCount || 0,
		songReload: !!(M.song && M.song.reloadNeeded), len: 16, length: 16, mult: "1X", swing: 50, accAmt: 0, accAll: false, slideAll: false };
	/* B-030: in a DAW whose clock the machine follows (its global: TEMPO IN external, set by the plug-in), the tempo
	   is the host's: TEMPO shows it (also while the host is stopped) and is not edited here */
	const H = docs.host;
	v.hostTempo = !!(H && H.follows && H.bpm > 0 && G && G.control && G.control.tempoIn === "external");
	if (v.hostTempo) v.bpm = H.bpm;
	for (const k in docs.kits) v.kitNames[k] = kitNameText(docs.kits[k].name);
	if (K) v.kitNames[kit] = kitNameText(K.name);
	const mutes = new Set(desk.mutes || []), soloing = !!(ui.soloSet && ui.soloSet.size);
	/* the machine's own mutes (16 booleans); a track's mute is the user's (below, soloMutes) */
	v.mutes = Array.from({ length: 16 }, (_, i) => mutes.has(i));
	if (P) {
		v.len = P.totalLength;
		v.length = P.length;
		v.mult = P.tempoMultiplier;
		v.swing = swingPercent(P.swingAmount);
		v.accAmt = accentDisplay(P.accentAmount);
		v.accAll = P.accent.editAll === 1;
		v.slideAll = P.slide.editAll === 1;
	}
	const accAll = P ? stepSet(P.accent.steps) : new Set(), slideAll = P ? stepSet(P.slide.steps) : new Set();
	v.tracks = Array.from({ length: 16 }, (_, i) => {
		const kt = K ? K.tracks[i] : null;
		const m = kt ? kt.machine || "GND-EMPTY" : "GND-EMPTY";
		const a = slots(m, cat);
		const t = { name: nameOf(m, cat), m, model: kt ? kt.model : 0, fam: famOf(m, cat), syn: {}, fx: {}, rt: {}, level: kt ? kt.level : 0,
			mute: soloing ? !!(ui.userMutes && ui.userMutes.has(i)) : mutes.has(i), solo: soloing && ui.soloSet.has(i), out: G ? (G.routing[i] === "MAIN" ? "MAIN" : G.routing[i]) : "MAIN",
			trigs: Array(64).fill(false), acc: new Set(), slide: new Set(), muteGroup: kt ? kt.muteGroup : null, trigGroup: kt ? kt.trigGroup : null };
		if (kt) {
			a.forEach((n, j) => {
				if (!n) return;
				const val = j < 8 ? kt.synth[j] : j < 16 ? kt.effects[j - 8] : kt.routing[j - 16];
				(j < 8 ? t.syn : j < 16 ? t.fx : t.rt)[n] = val;
			});
			const tn = K.tracks[kt.lfo.track] ? slots(K.tracks[kt.lfo.track].machine || "GND-EMPTY", cat) : a;
			const lp = E.lfoParams, at = n => lp[n] != null ? paramOf(kt, lp[n]) : 0;
			t.lfo = { TRCK: kt.lfo.track, PARAM: tn[kt.lfo.param] || "#" + (kt.lfo.param + 1), PARAMI: kt.lfo.param, SHP1: kt.lfo.shape1, SHP2: kt.lfo.shape2,
				UPDTE: E.lfoUpdates[kt.lfo.update] || "", SPD: at("SPD"), DEPTH: at("DEPTH"), SHMIX: at("SHMIX") };
		} else t.lfo = { TRCK: i, PARAM: "", PARAMI: 0, SHP1: 0, SHP2: 0, UPDTE: E.lfoUpdates[0] || "", SPD: 0, DEPTH: 0, SHMIX: 0 };
		if (P) {
			const pt = P.tracks[i];
			for (const s of pt.trigs) t.trigs[s] = true;
			t.acc = v.accAll ? new Set([...accAll].filter(s => t.trigs[s])) : stepSet(pt.accent);
			t.slide = v.slideAll ? new Set([...slideAll].filter(s => t.trigs[s])) : stepSet(pt.slide);
		}
		return t;
	});
	v.locks = new Map();
	if (P) for (const l of P.locks) {
		const n = slots(v.tracks[l.track].m, cat)[l.param] || "#" + (l.param + 1);
		v.locks.set(l.track + ":" + n, new Map(l.steps.map(([s, val]) => [s, val])));
	}
	v.mfx = {};
	for (const id in MFXD) {
		const vals = K ? K.masterFx[mfxName(id, E)] : null;
		v.mfx[id] = { ...MFXD[id], v: {} };
		MFXD[id].k.forEach((n, j) => { v.mfx[id].v[n] = vals ? vals[j] : 0; });
	}
	const song = docs.songs[songSlot];
	v.songSlot = songSlot;
	v.song = song ? song.rows.map((r, i) => rowFromContract(r, i, p => lengthIn(docs, p))) : [{ type: "end" }];
	v.songName = song ? song.name : "";
	return v;
}
/* ---- mutes and solos (as the MM page): a track's mute is the user's. While no track is soloed the machine's
   mutes are the user's (the view's track.mute is the machine's). A solo is the page's: it drives the machine's
   mutes (every other track muted); the user's mutes are taken from the machine when the first solo begins, M
   then edits only them (the M key shows them), and the last solo let go gives them back to the machine. Pure:
   ui is {soloSet, userMutes}, mutes the machine's 16 booleans (view.mutes). ---- */
const mutedSet = mutes => new Set(mutes.map((m, i) => m ? i : -1).filter(i => i >= 0));
/* the solo set becomes next: the UI's new {soloSet, userMutes} */
function soloTo(ui, mutes, next) { return { soloSet: next, userMutes: ui.soloSet.size ? ui.userMutes : mutedSet(mutes) }; }
/* M on track i (on: muted): during a solo the user's new mutes (nothing goes to the machine), else null (the machine's mute) */
function muteTo(ui, i, on) { if (!ui.soloSet.size) return null; const u = new Set(ui.userMutes); on ? u.add(i) : u.delete(i); return u; }
/* the machine mutes to send so the machine plays what ui says: [[t, on]] for every track that differs. keep: the
   tracks a solo leaves alone (a RAM recorder: its mute is its capture and freeze, not what is heard) */
function soloWrites(ui, mutes, keep = new Set()) {
	const any = ui.soloSet.size > 0;
	return mutes.map((m, i) => [i, any ? !ui.soloSet.has(i) : ui.userMutes.has(i)]).filter(([i, want]) => !keep.has(i) && !!mutes[i] !== want);
}
/* ---- optimistic edits (P6): Overlay, docOf and shows are skins/shared/deskOverlay.js (both editors) ---- */
function view() { return Overlay.over(deriveView(Docs, S)); }

/* P9: a slot's waveform at the canvas' device pixels (data-contract.md 4.9). The overview (Docs.samples:
   128 min, max pairs a slot, -127..127) draws at once; the detail (sampleWave, asked for one slot at the
   bins it is drawn at, 16-bit) takes its place when it comes. Pure. */
const WAVE_MAX_BINS = 8192;
/* the bins to ask for a canvas cols device pixels wide: a step of 256 (a small resize asks nothing),
   at most a bin a sample and WAVE_MAX_BINS */
function waveBins(cols, length) { return Math.max(1, Math.min(WAVE_MAX_BINS, length || 1, Math.ceil(Math.max(1, cols) / 256) * 256)); }
/* the detail to ask for slot (its overview entry) at cols, or 0 when held (a detail held or asked for:
   {bins, length}) does: the same sample (length) and enough bins */
function waveWant(held, slot, cols) {
	if (!slot || slot.empty || !slot.length) return 0;
	const want = waveBins(cols, slot.length);
	return held && held.length === slot.length && held.bins >= want ? 0 : want;
}
/* min, max pairs at full scale scale onto cols columns: [lo, hi] per column, -1..1. More bins than
   columns: a column is the min and max of its bins; fewer: each column shows the bin under it. */
function waveColumns(peaks, scale, cols) {
	const bins = Math.floor(peaks.length / 2), out = new Float32Array(cols * 2);
	if (!bins) return out;
	for (let x = 0; x < cols; x++) {
		const a = Math.min(bins - 1, Math.floor(x * bins / cols)), b = Math.min(bins, Math.max(a + 1, Math.floor((x + 1) * bins / cols)));
		let lo = peaks[2 * a], hi = peaks[2 * a + 1];
		for (let i = a + 1; i < b; i++) { if (peaks[2 * i] < lo) lo = peaks[2 * i]; if (peaks[2 * i + 1] > hi) hi = peaks[2 * i + 1]; }
		out[2 * x] = lo / scale; out[2 * x + 1] = hi / scale;
	}
	/* A column of about one sample is a point: the wave passes between two neighbours, so each reaches
	   halfway to the other and the trace is one joined line, not dots. */
	for (let x = 1; x < cols; x++) {
		const pl = out[2 * x - 2], ph = out[2 * x - 1], l = out[2 * x], h = out[2 * x + 1];
		if (l > ph) { const m = (l + ph) / 2; out[2 * x] = m; out[2 * x - 1] = m; }
		else if (h < pl) { const m = (h + pl) / 2; out[2 * x + 1] = m; out[2 * x - 2] = m; }
	}
	return out;
}
/* the audition's playhead, 0..1 of the slot, ms after it started at the sample's own rate */
function auditionAt(aud, ms) { return aud && aud.length && aud.rate ? Math.min(1, Math.max(0, ms / 1000 * aud.rate / aud.length)) : 0; }
/* The view the renderers read: view(), replaced on every document and command. Before the first
   document it is the overlay over deriveView(Docs, S) with Docs still empty (its shape is
   EMPTY_DOCS's); nameOf is already this file's own, and V is first set once S (the page's UI
   state, mdDeskApp.js) exists. */
let V = null;

/* ===== The keyboard (P10): the home row plays the selected track, pure =====
   A S D F G H J K L are white keys C D E F G A B C D; Z / X move the octave (KEYS_OCT), C / V the velocity
   a step down / up (KEYS_VELS, from KEYS_VEL; keyVel). A key is the note intent noteOn {t, vel, pitch}
   (keyPitch: semitones from the track's sound); what a pitch is on the machine (the MAP EDITOR note, a
   PTCH held on the sample machines, which machines the keys leave alone) is the core's (mdDesk/mdDeskKeys.h). */
const KEYS_WHITE = "ASDFGHJKL", KEYS_SEMIS = [0, 2, 4, 5, 7, 9, 11, 12, 14], KEYS_OCT = [-2, 2], KEYS_VEL = 100,
	KEYS_VELS = [20, 40, 60, 80, 100, 127];
/* the velocity a step d (−1 / +1) from v: the next of KEYS_VELS, held at the ends (a v between steps goes to the nearer one first) */
function keyVel(v, d) {
	const up = KEYS_VELS.find(x => x > v), down = [...KEYS_VELS].reverse().find(x => x < v);
	return d > 0 ? up ?? KEYS_VELS[KEYS_VELS.length - 1] : down ?? KEYS_VELS[0];
}
/* a white key's pitch at octave oct, in semitones from the track's sound; null for a key that is not one */
function keyPitch(key, oct) {
	const k = KEYS_WHITE.indexOf(key);
	return k < 0 ? null : 12 * oct + KEYS_SEMIS[k];
}
