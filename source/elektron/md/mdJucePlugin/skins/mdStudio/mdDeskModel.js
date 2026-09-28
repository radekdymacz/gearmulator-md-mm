"use strict";
/* MD Desk model: the contract documents the plug-in publishes, and the view
   the page renders from them. Documents are firmware truth (or the desk's
   working kit); the view is derived from them after every change and may be
   edited optimistically, but every edit is also sent as a command and the
   next document replaces the view. See doc/modern-ux/data-contract.md. */
const Docs = { patterns: {}, kits: {}, songs: {}, global: null, machine: null, catalogue: null, learn: null };
const Tele = { step: -1, pattern: -1, playing: false, valid: false };

/* ---- machine catalogue (md-desk/machines, from elektronData::mdMachineParamNames) ---- */
const Cat = { byName: {}, byModel: {}, list: [] };
function setCatalogue(doc) {
	Docs.catalogue = doc;
	Cat.list = doc.machines;
	Cat.byName = {}; Cat.byModel = {};
	for (const m of doc.machines) { Cat.byName[m.machine] = m; Cat.byModel[m.model] = m; }
}
const FX = ["AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR"];
const RT = ["DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"];
const SMPL = ["PTCH", "DEC", "HOLD", "BRR", "STRT", "END", "RTRG", "RTIM"];
const RAMR = ["MLEV", "MBAL", "ILEV", "IBAL", "CUE1", "CUE2", "LEN", "RATE"];
function slots(m) {
	const c = Cat.byName[m];
	if (!c) return [null, null, null, null, null, null, null, null, ...FX, ...RT];
	/* P5: a synthesis parameter with the name of an effects or routing one (TRX-XT DIST, TRX-MA REV,
	   INP-GA VOL) is its own parameter: the page addresses it as SYN·NAME, so every lock lane, value and
	   command maps to one (page, index). The other is shown as RTG·NAME / FX·NAME (laneLabel). */
	if (!c.qparams) { const later = c.params.slice(8); c.qparams = c.params.map((n, i) => i < 8 && n && later.includes(n) ? "SYN·" + n : n); }
	return c.qparams;
}
function laneLabel(t, n) { const a = slots(V.tracks[t].m); if (!a.includes("SYN·" + n)) return n; return (a.indexOf(n) >= 16 ? "RTG·" : "FX·") + n; }
/* Three pages of 8 slots; an unused slot is null. */
function pages(m) { const a = slots(m); return { s: a.slice(0, 8), e: a.slice(8, 16), r: a.slice(16, 24) }; }
const names = list => list.filter(Boolean);
/* Family keys: the picker's (TRX EFM E12 PI GND INP MID CTR ROM RAM) and the colour's (ROM/RAM = SMP). */
function famKey(m) { const c = Cat.byName[m]; const f = c ? c.family : m.split("-")[0]; return f === "P-I" ? "PI" : f; }
function famOf(m) { const f = famKey(m); return f === "ROM" || f === "RAM" ? "SMP" : f; }
function codeOf(m) { const c = Cat.byName[m]; return c ? m.slice(c.family.length + 1) : m.split("-").slice(1).join("-"); }

/* ---- units (mdValidate.h) ---- */
const swingPercent = v => 50 + Math.round(v * 50 / 16384);
const accentDisplay = v => Math.round(v * 15 / 127);
const patName = p => "ABCDEFGH"[p >> 4] + String((p & 15) + 1).padStart(2, "0");
const OUTS = ["MAIN", "A", "B", "C", "D", "E", "F"];
const UPDATES = ["FREE", "TRIG", "HOLD"];
const MFX = { echo: "rhythmEcho", gate: "gateBox", eq: "eq", dyn: "dynamix" };

/* ---- document accessors ---- */
function machineState() { return Docs.machine || { pattern: {}, kit: {}, song: {}, desk: {} }; }
function currentPatternSlot() { const m = machineState(); return m.pattern && m.pattern.current != null ? m.pattern.current : 0; }
function currentKitSlot() {
	const m = machineState();
	if (m.kit && m.kit.current != null) return m.kit.current;
	const p = Docs.patterns[currentPatternSlot()];
	return p ? p.kit : 0;
}
function currentSongSlot() { const m = machineState(); return m.song && m.song.current != null ? m.song.current : 0; }
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
   deriveView(Docs, ui) is pure: the documents and the few UI facts it needs (the page-only solos)
   in, a new view out; the page replaces V with it on every document. A gesture may edit the
   current V for immediate feedback; the command it sends goes to the plug-in, and the next
   document replaces the whole view. The UI's own state (workspace, selection, zoom...) is S. */
const MFXD = {
	echo: { name: "Rhythm Echo", sub: "Tempo-synced delay. TIME is in 128th notes, so 64 is two beats.", k: ["TIME", "MOD", "MFRQ", "FB", "FILTF", "FILTW", "MONO", "LEV"] },
	gate: { name: "Gate Box", sub: "Reverb for percussion. GATE at 127 turns the gate off.", k: ["DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"] },
	eq: { name: "Master EQ", sub: "Low shelf, high shelf, one parametric band. Drag the points.", k: ["LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"] },
	dyn: { name: "Dynamix", sub: "Compressor on the main out. Drag threshold and ratio.", k: ["ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"] } };
function lengthOfPattern(p) { const d = Docs.patterns[p]; return d ? d.length : 16; }
function deriveView(docs, ui) {
	const P = docs.patterns[currentPatternSlot()];
	const K = docs.kits[currentKitSlot()];
	const G = docs.global;
	const M = machineState();
	const desk = M.desk || {};
	const hist = M.history || {};
	const v = { loaded: !!(P && K), pat: currentPatternSlot(), queued: desk.queued != null ? desk.queued : null, kit: currentKitSlot(),
		kitState: M.kit && M.kit.working || "unknown", kitSource: desk.kitSource || "tracked", kitNames: {},
		patKit: Array.from({ length: 128 }, (_, p) => docs.patterns[p] ? docs.patterns[p].kit : null),
		mode: (M.extendedMode != null ? M.extendedMode : G ? G.extendedMode : true) ? "EXTENDED" : "CLASSIC",
		bpm: G ? G.tempo : 120, playing: !!desk.playing, rec: !!desk.recording, gridEdit: !!desk.gridEdit, tx: !!desk.tx,
		roundTrip: desk.roundTripMs, firmware: desk.firmware || "booting", lifecycle: M.lifecycle || "booting", caps: M.capabilities || {},
		canUndo: !!hist.undo, canRedo: !!hist.redo, undoCount: hist.undoCount || 0, redoCount: hist.redoCount || 0,
		songReload: !!(M.song && M.song.reloadNeeded), len: 16, length: 16, mult: "1X", swing: 50, accAmt: 0, accAll: false, slideAll: false };
	for (const k in docs.kits) v.kitNames[k] = docs.kits[k].name;
	const mutes = new Set(desk.mutes || []);
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
		const a = slots(m);
		const t = { name: nameOf(m), m, model: kt ? kt.model : 0, fam: famOf(m), syn: {}, fx: {}, rt: {}, level: kt ? kt.level : 0,
			mute: mutes.has(i), solo: ui.soloSet ? ui.soloSet.has(i) : false, out: G ? (G.routing[i] === "MAIN" ? "MAIN" : G.routing[i]) : "MAIN",
			trigs: Array(64).fill(false), acc: new Set(), slide: new Set(), muteGroup: kt ? kt.muteGroup : null, trigGroup: kt ? kt.trigGroup : null };
		if (kt) {
			a.forEach((n, j) => {
				if (!n) return;
				const val = j < 8 ? kt.synth[j] : j < 16 ? kt.effects[j - 8] : kt.routing[j - 16];
				(j < 8 ? t.syn : j < 16 ? t.fx : t.rt)[n] = val;
			});
			const tn = K.tracks[kt.lfo.track] ? slots(K.tracks[kt.lfo.track].machine || "GND-EMPTY") : a;
			t.lfo = { TRCK: kt.lfo.track, PARAM: tn[kt.lfo.param] || "#" + (kt.lfo.param + 1), PARAMI: kt.lfo.param, SHP1: kt.lfo.shape1, SHP2: kt.lfo.shape2,
				UPDTE: UPDATES[kt.lfo.update] || "FREE", SPD: kt.routing[5], DEPTH: kt.routing[6], SHMIX: kt.routing[7] };
		} else t.lfo = { TRCK: i, PARAM: "", PARAMI: 0, SHP1: 0, SHP2: 0, UPDTE: "FREE", SPD: 0, DEPTH: 0, SHMIX: 0 };
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
		const n = slots(v.tracks[l.track].m)[l.param] || "#" + (l.param + 1);
		v.locks.set(l.track + ":" + n, new Map(l.steps.map(([s, val]) => [s, val])));
	}
	v.mfx = {};
	for (const id in MFXD) {
		const vals = K ? K.masterFx[MFX[id]] : null;
		v.mfx[id] = { ...MFXD[id], v: {} };
		MFXD[id].k.forEach((n, j) => { v.mfx[id].v[n] = vals ? vals[j] : 0; });
	}
	const song = docs.songs[currentSongSlot()];
	v.songSlot = currentSongSlot();
	v.song = song ? song.rows.map((r, i) => rowFromContract(r, i, lengthOfPattern)) : [{ type: "end" }];
	v.songName = song ? song.name : "";
	return v;
}
/* The view before the first document (the renderers run once before it). */
let V = { loaded: false, pat: 0, queued: null, kit: 0, kitState: "unknown", kitSource: "tracked", kitNames: {}, patKit: [], mode: "EXTENDED", bpm: 120,
	playing: false, rec: false, gridEdit: false, tx: false, roundTrip: -1, firmware: "booting", lifecycle: "booting", caps: {}, canUndo: false, canRedo: false,
	undoCount: 0, redoCount: 0, songReload: false, len: 16, length: 16, mult: "1X", swing: 50, accAmt: 0, accAll: false, slideAll: false, tracks: [],
	locks: new Map(), mfx: Object.fromEntries(Object.entries(MFXD).map(([id, d]) => [id, { ...d, v: {} }])), songSlot: 0, song: [{ type: "end" }], songName: "" };
