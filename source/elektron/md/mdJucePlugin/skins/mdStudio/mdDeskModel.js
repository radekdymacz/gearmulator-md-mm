"use strict";
/* MD Desk model: the contract documents the plug-in publishes, and the view
   the page renders from them. Documents are firmware truth (or the desk's
   working kit); the view is derived from them after every change and may be
   edited optimistically, but every edit is also sent as a command and the
   next document replaces the view. See doc/modern-ux/data-contract.md. */
/* kits: the stored slots; workingKit: the kit that plays ({slot, source, pending, doc}, the slot it was
   loaded from); sources: each stored document's "source" ("kit:3" -> "dump"). */
const Docs = { patterns: {}, kits: {}, songs: {}, global: null, machine: null, catalogue: null, workingKit: null, sources: {}, learn: null, telemetry: null };
const Tele = { step: -1, pattern: -1, playing: false, valid: false };

/* ---- machine catalogue (md-desk/machines, from elektronData::mdMachineParamNames) ----
   Its index is derived from the document (once per document); Cat is the current one. */
const EMPTY_CAT = { byName: {}, byModel: {}, list: [], qparams: new Map() };
const catIndexes = new WeakMap();
function catalogueIndex(doc) {
	if (!doc) return EMPTY_CAT;
	let c = catIndexes.get(doc);
	if (c) return c;
	c = { byName: {}, byModel: {}, list: doc.machines, qparams: new Map() };
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
function slots(m, cat = Cat) {
	const c = cat.byName[m];
	if (!c) return [null, null, null, null, null, null, null, null, ...FX, ...RT];
	/* P5: a synthesis parameter with the name of an effects or routing one (TRX-XT DIST, TRX-MA REV,
	   INP-GA VOL) is its own parameter: the page addresses it as SYN·NAME, so every lock lane, value and
	   command maps to one (page, index). The other is shown as RTG·NAME / FX·NAME (laneLabel). */
	if (!cat.qparams.has(m)) { const later = c.params.slice(8); cat.qparams.set(m, c.params.map((n, i) => i < 8 && n && later.includes(n) ? "SYN·" + n : n)); }
	return cat.qparams.get(m);
}
function laneLabel(t, n) { const a = slots(V.tracks[t].m); if (!a.includes("SYN·" + n)) return n; return (a.indexOf(n) >= 16 ? "RTG·" : "FX·") + n; }
/* Three pages of 8 slots; an unused slot is null. */
function pages(m) { const a = slots(m); return { s: a.slice(0, 8), e: a.slice(8, 16), r: a.slice(16, 24) }; }
const names = list => list.filter(Boolean);
/* Family keys: the picker's (TRX EFM E12 PI GND INP MID CTR ROM RAM) and the colour's (ROM/RAM = SMP). */
function famKey(m, cat = Cat) { const c = cat.byName[m]; const f = c ? c.family : m.split("-")[0]; return f === "P-I" ? "PI" : f; }
function famOf(m, cat = Cat) { const f = famKey(m, cat); return f === "ROM" || f === "RAM" ? "SMP" : f; }
function codeOf(m, cat = Cat) { const c = cat.byName[m]; return c ? m.slice(c.family.length + 1) : m.split("-").slice(1).join("-"); }

/* ---- units (mdValidate.h) ---- */
const swingPercent = v => 50 + Math.round(v * 50 / 16384);
const accentDisplay = v => Math.round(v * 15 / 127);
const patName = p => "ABCDEFGH"[p >> 4] + String((p & 15) + 1).padStart(2, "0");
const OUTS = ["MAIN", "A", "B", "C", "D", "E", "F"];
const UPDATES = ["FREE", "TRIG", "HOLD"];
const MFX = { echo: "rhythmEcho", gate: "gateBox", eq: "eq", dyn: "dynamix" };

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
function kitDocOf(docs) { const w = workingKitOf(docs); return w ? w.doc : docs.kits[kitSlotOf(docs)]; }
function kitSourceOf(docs) { const w = workingKitOf(docs); return w ? w.source : docs.sources["kit:" + kitSlotOf(docs)] || "none"; }
function songSlotOf(docs) { const m = machineOf(docs); return m.song && m.song.current != null ? m.song.current : 0; }
function lengthIn(docs, p) { const d = docs.patterns[p]; return d ? d.length : 16; }
/* the transport: the telemetry's when it has come (newer than the machine document), else the desk's */
function transportOf(docs) {
	const t = docs.telemetry, desk = machineOf(docs).desk || {};
	return t ? { playing: !!t.playing, rec: !!t.recording } : { playing: !!desk.playing, rec: !!desk.recording };
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
	const v = { loaded: !!(P && K), pat, queued: desk.queued != null ? desk.queued : null, kit,
		kitState: M.kit && M.kit.working || "unknown", kitSource: kitSourceOf(docs), kitNames: {},
		patKit: Array.from({ length: 128 }, (_, p) => docs.patterns[p] ? docs.patterns[p].kit : null),
		mode: (M.extendedMode != null ? M.extendedMode : G ? G.extendedMode : true) ? "EXTENDED" : "CLASSIC",
		bpm: G ? G.tempo : 120, playing: transport.playing, rec: transport.rec, gridEdit: !!desk.gridEdit, tx: !!desk.tx,
		roundTrip: desk.roundTripMs, lifecycle: M.lifecycle || "booting", caps: M.capabilities || {},
		canUndo: !!hist.undo, canRedo: !!hist.redo, undoCount: hist.undoCount || 0, redoCount: hist.redoCount || 0,
		songReload: !!(M.song && M.song.reloadNeeded), len: 16, length: 16, mult: "1X", swing: 50, accAmt: 0, accAll: false, slideAll: false };
	for (const k in docs.kits) v.kitNames[k] = docs.kits[k].name;
	if (K) v.kitNames[kit] = K.name;
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
		const a = slots(m, cat);
		const t = { name: nameOf(m, cat), m, model: kt ? kt.model : 0, fam: famOf(m, cat), syn: {}, fx: {}, rt: {}, level: kt ? kt.level : 0,
			mute: mutes.has(i), solo: ui.soloSet ? ui.soloSet.has(i) : false, out: G ? (G.routing[i] === "MAIN" ? "MAIN" : G.routing[i]) : "MAIN",
			trigs: Array(64).fill(false), acc: new Set(), slide: new Set(), muteGroup: kt ? kt.muteGroup : null, trigGroup: kt ? kt.trigGroup : null };
		if (kt) {
			a.forEach((n, j) => {
				if (!n) return;
				const val = j < 8 ? kt.synth[j] : j < 16 ? kt.effects[j - 8] : kt.routing[j - 16];
				(j < 8 ? t.syn : j < 16 ? t.fx : t.rt)[n] = val;
			});
			const tn = K.tracks[kt.lfo.track] ? slots(K.tracks[kt.lfo.track].machine || "GND-EMPTY", cat) : a;
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
		const n = slots(v.tracks[l.track].m, cat)[l.param] || "#" + (l.param + 1);
		v.locks.set(l.track + ":" + n, new Map(l.steps.map(([s, val]) => [s, val])));
	}
	v.mfx = {};
	for (const id in MFXD) {
		const vals = K ? K.masterFx[MFX[id]] : null;
		v.mfx[id] = { ...MFXD[id], v: {} };
		MFXD[id].k.forEach((n, j) => { v.mfx[id].v[n] = vals ? vals[j] : 0; });
	}
	const song = docs.songs[songSlot];
	v.songSlot = songSlot;
	v.song = song ? song.rows.map((r, i) => rowFromContract(r, i, p => lengthIn(docs, p))) : [{ type: "end" }];
	v.songName = song ? song.name : "";
	return v;
}
/* ---- optimistic edits (P6): an explicit overlay over the derived view ----
   A gesture that shows its edit at once says so in its command: cmd(op, args, key, optimistic),
   where optimistic is a list of [path, value] into the view. The page applies them at once and
   keeps them as overlay entries owned by that command's id; view() is the overlay applied, in
   order, to deriveView(Docs, S). An entry leaves when the plug-in answers its command (the result
   comes after the documents it changed), so a refused edit disappears and a taken one is in the
   documents. A keyed command that replaces a waiting one keeps its id, so it takes over the
   entries by path. Entries hold values, never toggles: applying one to a view that already shows
   the edit changes nothing. Paths go through objects, arrays, Maps and Sets:
   - an object or array member, or a Map entry: the value, or Overlay.DELETE;
   - a Set member: true (in) or false (out);
   - a Map of Maps (the lock lanes): a write into a missing inner Map makes it, and a delete that
     empties one removes it, so the view's inner Maps are never empty. */
const Overlay = (() => {
	const DELETE = Symbol("delete");
	const entries = new Map();	// path key -> { path, value, id }
	const keyOf = path => path.map(String).join("\u241f");
	const tag = v => Object.prototype.toString.call(v), isMap = v => tag(v) === "[object Map]", isSet = v => tag(v) === "[object Set]";
	const clone = v => {
		if (isMap(v)) return new Map([...v].map(([k, x]) => [k, clone(x)]));
		if (isSet(v)) return new Set(v);
		if (Array.isArray(v)) return v.map(clone);
		if (v && typeof v === "object") return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, clone(x)]));
		return v;
	};
	function setIn(root, path, value) {
		const up = [];	// [container, key] on the way, for the Map-of-Maps rule
		let o = root;
		for (let i = 0; i < path.length - 1; i++) {
			let next = isMap(o) ? o.get(path[i]) : o[path[i]];
			if (next == null && isMap(o) && value !== DELETE && i === path.length - 2) { next = new Map(); o.set(path[i], next); }
			if (next == null || typeof next !== "object") return;	// that part of the view is not there (another pattern, a reset)
			up.push([o, path[i]]);
			o = next;
		}
		const k = path[path.length - 1];
		if (isSet(o)) value ? o.add(k) : o.delete(k);
		else if (isMap(o)) {
			if (value !== DELETE) { o.set(k, clone(value)); return; }
			o.delete(k);
			const [parent, pk] = up[up.length - 1] || [];
			if (!o.size && isMap(parent)) parent.delete(pk);
		}
		else if (value === DELETE) delete o[k];
		else o[k] = clone(value);
	}
	return {
		DELETE,
		/* the writes a command shows at once, owned by its id (a later write of a path owns it) */
		add(id, writes) { for (const [path, value] of writes) { const k = keyOf(path); entries.delete(k); entries.set(k, { path, value: clone(value), id }); } },
		/* its answer came: its entries leave; true when there were any */
		answered(id) { let any = false; for (const [k, e] of entries) if (e.id === id) { entries.delete(k); any = true; } return any; },
		clear() { entries.clear(); },
		size: () => entries.size,
		/* the overlay applied to a freshly derived view (which it changes and returns) */
		over(v) { for (const e of entries.values()) setIn(v, e.path, e.value); return v; }
	};
})();
const EMPTY_DOCS = { patterns: {}, kits: {}, songs: {}, global: null, machine: null, catalogue: null, workingKit: null, sources: {}, telemetry: null };
function view() { return Overlay.over(deriveView(Docs, S)); }
/* The view the renderers read: view(), replaced on every document and command. Before the first
   document it is the overlay over deriveView(EMPTY_DOCS, S), set by the page once its UI state
   (S) and names (nameOf) exist (mdDeskApp.js). */
let V = null;
