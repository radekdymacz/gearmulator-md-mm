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
	return c ? c.params : [null, null, null, null, null, null, null, null, ...FX, ...RT];
}
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

/* ---- the view: what the renderers read (the mockup's S, now derived) ---- */
function lengthOfPattern(p) { const d = Docs.patterns[p]; return d ? d.length : 16; }
function deriveView(S) {
	const P = Docs.patterns[currentPatternSlot()];
	const K = Docs.kits[currentKitSlot()];
	const G = Docs.global;
	const M = machineState();
	const desk = M.desk || {};
	S.loaded = !!(P && K);
	S.pat = currentPatternSlot();
	S.queued = desk.queued != null ? desk.queued : null;
	S.kit = currentKitSlot();
	S.kitState = M.kit && M.kit.working || "unknown";
	S.kitSource = desk.kitSource || "tracked";
	S.kitNames = {};
	for (const k in Docs.kits) S.kitNames[k] = Docs.kits[k].name;
	S.patKit = Array.from({ length: 128 }, (_, p) => Docs.patterns[p] ? Docs.patterns[p].kit : null);
	S.mode = (M.extendedMode != null ? M.extendedMode : G ? G.extendedMode : true) ? "EXTENDED" : "CLASSIC";
	S.bpm = G ? G.tempo : 120;
	S.playing = !!desk.playing;
	S.rec = !!desk.recording;		/* live recording, from the machine (RECORD LED blinking) */
	S.gridEdit = !!desk.gridEdit;
	S.tx = !!desk.tx;
	S.roundTrip = desk.roundTripMs;
	S.firmware = desk.firmware || "booting";
	S.canUndo = !!desk.undo; S.canRedo = !!desk.redo; S.undoCount = desk.undoCount || 0; S.redoCount = desk.redoCount || 0;
	S.songReload = !!(M.song && M.song.reloadNeeded);
	const mutes = new Set(desk.mutes || []);
	if (P) {
		S.len = P.totalLength;
		S.length = P.length;
		S.mult = P.tempoMultiplier;
		S.swing = swingPercent(P.swingAmount);
		S.accAmt = accentDisplay(P.accentAmount);
		S.accAll = P.accent.editAll === 1;
		S.slideAll = P.slide.editAll === 1;
	}
	const accAll = P ? stepSet(P.accent.steps) : new Set(), slideAll = P ? stepSet(P.slide.steps) : new Set();
	S.tracks = Array.from({ length: 16 }, (_, i) => {
		const kt = K ? K.tracks[i] : null;
		const m = kt ? kt.machine || "GND-EMPTY" : "GND-EMPTY";
		const a = slots(m);
		const t = { name: nameOf(m), m, model: kt ? kt.model : 0, fam: famOf(m), syn: {}, fx: {}, rt: {}, level: kt ? kt.level : 0,
			mute: mutes.has(i), solo: S.soloSet ? S.soloSet.has(i) : false, out: G ? (G.routing[i] === "MAIN" ? "MAIN" : G.routing[i]) : "MAIN",
			trigs: Array(64).fill(false), acc: new Set(), slide: new Set(), muteGroup: kt ? kt.muteGroup : null, trigGroup: kt ? kt.trigGroup : null };
		if (kt) {
			a.forEach((n, j) => {
				if (!n) return;
				const v = j < 8 ? kt.synth[j] : j < 16 ? kt.effects[j - 8] : kt.routing[j - 16];
				(j < 8 ? t.syn : j < 16 ? t.fx : t.rt)[n] = v;
			});
			const tn = K.tracks[kt.lfo.track] ? slots(K.tracks[kt.lfo.track].machine || "GND-EMPTY") : a;
			t.lfo = { TRCK: kt.lfo.track, PARAM: tn[kt.lfo.param] || "#" + (kt.lfo.param + 1), PARAMI: kt.lfo.param, SHP1: kt.lfo.shape1, SHP2: kt.lfo.shape2,
				UPDTE: UPDATES[kt.lfo.update] || "FREE", SPD: kt.routing[5], DEPTH: kt.routing[6], SHMIX: kt.routing[7] };
		} else t.lfo = { TRCK: i, PARAM: "", PARAMI: 0, SHP1: 0, SHP2: 0, UPDTE: "FREE", SPD: 0, DEPTH: 0, SHMIX: 0 };
		if (P) {
			const pt = P.tracks[i];
			for (const s of pt.trigs) t.trigs[s] = true;
			t.acc = S.accAll ? new Set([...accAll].filter(s => t.trigs[s])) : stepSet(pt.accent);
			t.slide = S.slideAll ? new Set([...slideAll].filter(s => t.trigs[s])) : stepSet(pt.slide);
		}
		return t;
	});
	S.locks = new Map();
	if (P) for (const l of P.locks) {
		const n = slots(S.tracks[l.track].m)[l.param] || "#" + (l.param + 1);
		S.locks.set(l.track + ":" + n, new Map(l.steps.map(([s, v]) => [s, v])));
	}
	S.mfx = S.mfx || {
		echo: { name: "Rhythm Echo", sub: "Tempo-synced delay. TIME is in 128th notes, so 64 is two beats.", k: ["TIME", "MOD", "MFRQ", "FB", "FILTF", "FILTW", "MONO", "LEV"], v: {} },
		gate: { name: "Gate Box", sub: "Reverb for percussion. GATE at 127 turns the gate off.", k: ["DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"], v: {} },
		eq: { name: "Master EQ", sub: "Low shelf, high shelf, one parametric band. Drag the points.", k: ["LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"], v: {} },
		dyn: { name: "Dynamix", sub: "Compressor on the main out. Drag threshold and ratio.", k: ["ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"], v: {} } };
	for (const id in S.mfx) {
		const vals = K ? K.masterFx[MFX[id]] : null;
		S.mfx[id].v = {};
		S.mfx[id].k.forEach((n, j) => { S.mfx[id].v[n] = vals ? vals[j] : 0; });
	}
	const song = Docs.songs[currentSongSlot()];
	S.songSlot = currentSongSlot();
	S.song = song ? song.rows.map((r, i) => rowFromContract(r, i, lengthOfPattern)) : [{ type: "end" }];
	S.songName = song ? song.name : "";
	/* The baseline the value diff (syncKitValues) compares against. */
	S.base = snapshotKit(S);
}
function snapshotKit(S) {
	return JSON.stringify({ tracks: S.tracks.map(t => ({ m: t.m, syn: t.syn, fx: t.fx, rt: t.rt, lfo: t.lfo, mg: t.muteGroup, tg: t.trigGroup, out: t.out })), mfx: Object.fromEntries(Object.entries(S.mfx).map(([k, f]) => [k, f.v])) });
}
