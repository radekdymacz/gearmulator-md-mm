"use strict";
/* Monomachine Editor: contract documents -> the page's own state, as plain data (DESIGN-UNIFY.md 4.3).
   The page (the approved mockup, mmMockup.js) keeps its state in `S` in editor
   units: enumerations as list indices, swing 50-80 %, transposes centred on 64,
   arp speed and range 1-based, ASSIGN amounts centred on 64. The firmware's
   documents (doc/modern-ux/mm-data-contract.md) are in firmware units. These
   functions translate the documents into the page's units (the view, mmView.js)
   and nothing else: no DOM, no messages. The page never builds a document: an edit
   is an intent (mmDeskEdit.cpp); only an intent's own values come back here
   (valueToFw: a knob's or a lock's value; rowToFw: a song row), and the Control
   workspace's setup (modToFw), which is the plug-in's, not a document.

   Round trip rule: valueToFw(valueToPage(raw)) keeps the raw value inside its
   enumeration band (a value the page cannot show rides along), and
   rowToFw(rowToPage(row), row) == row for every factory and programmed song
   (mmConvertTest.js).

   Depends on the mockup's tables: MACH, PAGES, EN, INPUTS, machName, clamp. The enumeration
   counts (which values are list indices, and how many) are the catalogue's (useCatalogue): the
   plug-in's one table (MmModel::catalogue), given before any document is converted.
   The modulators (modToFw, modToPage) are the Control workspace's setup <-> md-desk/modulators. */
const MmConvert = (() => {
	const clone = o => JSON.parse(JSON.stringify(o));
	const cl = (v, a = 0, b = 127) => Math.max(a, Math.min(b, Math.round(v)));
	const ID2M = {};
	for (const [name, m] of Object.entries(MACH)) ID2M[m.id] = name;
	/* ASSIGN tabs -> firmware source (6 sources x 2 rows; source 1, JOY L, is
	   what JOY R/L becomes with mirror off and has no tab of its own). */
	const TABS = [["JOY RL", 0], ["JOY U", 2], ["JOY D", 3], ["VEL", 4], ["KEY", 5]];
	/* the catalogue's enumeration counts: lfo = the LFO page's PAGE DEST TRIG WAVE MULT, synth =
	   {machine: {slot: n}} (none before the catalogue) */
	let COUNTS = { lfo: [], synth: {} };
	/* B-049: the AMPLIFICATION envelope's times as OS 1.32B plays them, per value (the catalogue's ampEnvelope,
	   measured; none before the catalogue) */
	let AMP = null;
	/* takes the catalogue ("mm-desk/catalogue"); returns where the mockup's own tables (MACH, EN: its
	   names and labels) differ from it, for a log line and the tests */
	function useCatalogue(cat) {
		const lfo = cat.lfo, synth = {}, off = [];
		for (const m of cat.machines) {
			synth[m.name] = {};
			for (const [i, list] of Object.entries(m.enums || {})) synth[m.name][i] = list.length;
			const mine = MACH[m.name];
			if (!mine) { off.push("machine " + m.name + " is not in the mockup"); continue; }
			m.synth.forEach((n, i) => {
				const name = mine.p[i] || "";
				if (name !== n) off.push(`${m.name} slot ${i + 1}: mockup ${name || "-"}, catalogue ${n || "-"}`);
				const en = name && (EN[m.name + "." + name] || EN[name]);
				if (en && en.length !== (synth[m.name][i] || 0)) off.push(`${m.name} ${name}: mockup ${en.length} values, catalogue ${synth[m.name][i] || 0}`);
			});
		}
		/* the LFO page: PAGE, DEST (a page's parameters), TRIG, WAVE, MULT */
		COUNTS = { lfo: [lfo.pages.length, cat.machines[0].synth.length, lfo.trigs.length, lfo.waves.length, lfo.mults.length], synth };
		AMP = cat.ampEnvelope || null;
		if (!AMP) off.push("no ampEnvelope: the amp envelope's times are not drawn");
		return off;
	}

	/* enumerations: index = floor(v * n / 128), value = ceil(i * 128 / n) (MM-P1, the catalogue's enumRule) */
	const eIdx = (v, n) => Math.min(n - 1, Math.floor(v * n / 128));
	const eVal = (i, n) => Math.min(127, Math.ceil(i * 128 / n));
	function enumN(machine, page, i) {
		if (page.startsWith("LF")) return COUNTS.lfo[i] || 0;
		if (page !== "SYN") return 0;
		return (COUNTS.synth[machine] || {})[i] || 0;
	}
	const valueToPage = (m, pg, i, raw) => { const n = enumN(m, pg, i); return n ? eIdx(raw, n) : raw; };
	/* back to firmware units; an unchanged index keeps the base's raw value */
	function valueToFw(m, pg, i, v, baseRaw) {
		const n = enumN(m, pg, i);
		if (!n) return cl(v);
		if (baseRaw != null && eIdx(baseRaw, n) === v) return baseRaw;
		return eVal(cl(v, 0, n - 1), n);
	}
	/* B-049: the AMP page's envelope in time. ATK, DEC and REL are times, not the tempo's; HOLD counts sixteenths of
	   the tempo (15000 / bpm ms each). attackMs: the trig to 90 %. fallMs: DEC after HOLD and REL after a NOTE OFF, the
	   ms to fall 20 dB (the fall is exponential), Infinity at 127 (DEC 127 holds until the NOTE OFF, REL 127 never
	   fades). valueFor: the value whose time is nearest (a dragged handle). All null before the catalogue. */
	const ampEnv = {
		ready: () => !!AMP,
		attackMs: v => AMP ? AMP.attackMs[cl(v)] : null,
		holdMs: (v, bpm) => AMP ? AMP.holdSixteenths[cl(v)] * 15000 / (bpm || 120) : null,
		fallMs: v => AMP ? (AMP.fallMs[cl(v)] ?? Infinity) : null,
		valueFor(kind, ms, bpm) {
			if (!AMP) return null;
			const at = kind === "attack" ? ampEnv.attackMs : kind === "hold" ? v => ampEnv.holdMs(v, bpm) : ampEnv.fallMs;
			let best = 0;
			for (let v = 1; v < 128; v++) if (Math.abs(at(v) - ms) < Math.abs(at(best) - ms)) best = v;
			return best;
		}
	};
	const machineName = id => ID2M[id] ?? null;
	const bit = (mask, t) => (mask >> t) & 1;

	/* ---------------- kit ---------------- */
	/* -> the page's kit (captureKit() shape). `global` gives the MIDI tracks' channel and CCs. */
	function kitToPage(d, global) {
		const M = d.trackMasks;
		const tracks = d.tracks.map((x, t) => {
			const m = machineName(x.machine) ?? "GND-GND";
			const v = {};
			PAGES.forEach((pg, p) => v[pg] = x.pages[p].map((r, i) => valueToPage(m, pg, i, r)));
			const tabs = {};
			TABS.forEach(([tab, src]) => tabs[tab] = [0, 1].map(r => {
				const k = src * 2 + r;
				return { pg: x.assign.page[k], d: x.assign.dest[k], add: cl(x.assign.add[k] + 64) };
			}));
			return {
				m, name: machName(m), v, lev: d.levels[t],
				out: { AB: !!(x.outputs & 1), CD: !!(x.outputs & 2), EF: !!(x.outputs & 4) },
				inp: INPUTS[x.input] ?? INPUTS[0], trigpos: x.trigPos ?? null, port: bit(M.portamento, t) ? 0 : 1,
				leg: { amp: bit(M.legatoAmp, t), flt: bit(M.legatoFilter, t), lfo: bit(M.legatoLfo, t) },
				assign: { mirr: !!bit(M.mirror, t), hpf: !!bit(M.hpf, t), lpf: !!bit(M.lpf, t), tabs }
			};
		});
		const midi = d.tracks.map((x, t) => ({
			v: { MID: [...x.pages[7]] },
			cc: global ? [...global.midiSeq.ccs[t]] : [1, 2, 7, 10],
			ch: global ? global.midiSeq.channels[t] + 1 : 11 + t,
			name: "MIDI " + (t + 1)
		}));
		const e = d.tracks[0].multiEnv || [0, 0, 0, 0, 0, 0];
		/* MULTI TRIG (MM-P4): the page counts the split track from 1 */
		const mt = d.multiTrig;
		const multi = mt ? { mode: mt.mode, splitKey: mt.splitKey, splitTrack: mt.splitTrack + 1, timing: mt.timing } : undefined;
		return { tracks, midi, multi, menv: { ATK: e[0], DEC: e[1], SUS: e[2], REL: e[3], PORT: e[4] } };
	}
	const kitName = d => d.nameBytes && /^ff/i.test(d.nameBytes) ? "" : d.name;
	/* An unused slot: the firmware marks it with a first name byte of 0xff. */
	const kitEmpty = d => !!(d.nameBytes && /^ff/i.test(d.nameBytes)) || !d.name.trim();

	/* ---------------- pattern ---------------- */
	function arpToPage(a, synth) {
		return {
			MODE: a.mode, PLAY: a.play, SPD: a.speed + 1, RNGE: a.range + 1, OJMP: a.ojmp,
			amp: synth ? a.trigs & 1 : 1, flt: synth ? (a.trigs >> 1) & 1 : 1, lfo: synth ? (a.trigs >> 2) & 1 : 1,
			len: a.length, rhy: a.steps.map(v => v !== 255), ofs: a.steps.map(v => v === 255 ? 0 : v - 64)
		};
	}
	const trnToPage = x => ({ TRACK: x.transpose + 64, SCALE: x.scale, KEY: x.key });
	/* lock key as the page writes it: synth track "t|PG.i", a MIDI track's page "(t+6)|MID.i" */
	const lockKey = (track, page, param) => page === 7 ? (track + 6) + "|MID." + param : track + "|" + PAGES[page] + "." + param;
	function parseLockKey(k) {
		const [t, pid] = k.split("|"), [pg, i] = pid.split(".");
		return pg === "MID" ? { track: +t - 6, page: 7, param: +i } : { track: +t, page: PAGES.indexOf(pg), param: +i };
	}
	const hasTrigs = d => d.tracks.some(x => x.trig.length || x.amp.length || x.filter.length || x.lfo.length)
		|| d.midiTracks.some(x => x.trig.length);

	/* -> the page's pattern (capturePat() shape). `kit` (a page kit) names the machines for the locks. */
	function patternToPage(d, kit) {
		const tr = [];
		d.tracks.forEach((x, t) => {
			const T = new Set(x.trig), A = new Set(x.amp), F = new Set(x.filter), L = new Set(x.lfo), O = new Set(x.noteOff);
			const N = new Map(x.notes), C = new Set(x.chord), chord = {};
			d.chordNotes.forEach(([ct, s, n]) => { if (ct === t) (chord[s] = chord[s] || []).push(n); });
			const steps = Array(64).fill(null);
			for (let s = 0; s < 64; s++) {
				if (O.has(s)) { steps[s] = { off: 1 }; continue; }
				if (!(T.has(s) || A.has(s) || F.has(s) || L.has(s))) continue;
				const st = { a: +A.has(s), f: +F.has(s), l: +L.has(s) };
				if (N.has(s)) st.n = [N.get(s), ...(C.has(s) ? chord[s] || [] : [])];	// no note: pitchless
				if (!T.has(s)) st.notrig = 1;	// envelope bits without the trig bit (TRIG SELECT)
				steps[s] = st;
			}
			tr.push({ steps, slide: [...x.slide], swing: [...x.swing], arp: arpToPage(x.arp, true), tr: trnToPage(x) });
		});
		d.midiTracks.forEach((x, t) => {
			const T = new Set(x.trig), O = new Set(x.noteOff), notes = {};
			d.midiNotes.forEach(([mt, s, n]) => { if (mt === t) (notes[s] = notes[s] || []).push(n); });
			const steps = Array(64).fill(null);
			for (let s = 0; s < 64; s++) {
				if (O.has(s)) { steps[s] = { off: 1 }; continue; }
				if (!T.has(s)) continue;
				const st = { a: 1, f: 1, l: 1 };
				if (notes[s]) st.n = notes[s];
				steps[s] = st;
			}
			tr.push({ steps, slide: [...x.slide], swing: [...x.swing], arp: arpToPage(x.arp, false), tr: trnToPage(x) });
		});
		const locks = d.locks.map(l => {
			const m = kit?.tracks[l.track]?.m, pg = l.page === 7 ? "MID" : PAGES[l.page];
			return [lockKey(l.track, l.page, l.param), l.steps.map(([s, v]) => [s, l.page === 7 ? v : valueToPage(m, pg, l.param, v)])];
		});
		return { len: d.length, mult: d.multiplier, swingAmt: d.swingAmount + 50, patTrn: d.patternTranspose + 64, tr, locks };
	}

	/* ---------------- song ---------------- */
	/* a song row -> the page's (`i` its place: a HALT row's target; `lenOf(pattern)`: that pattern's length, for
	   "full pattern" rows) */
	function rowToPage(r, i, lenOf) {
		if (r.kind === "end") return { type: "end" };
		if (r.kind === "loop") return { type: "loop", to: r.target, count: r.repeats || Infinity };
		if (r.kind === "jump") return { type: "jump", to: r.target };
		if (r.kind === "halt") return { type: "halt", to: i };
		const o = { pat: r.pattern, rep: r.repeats + 1 };
		if (r.transpose) o.trn = r.transpose + 64;
		const tt = [...r.trackTranspose, ...r.midiTranspose];
		if (tt.some(Boolean)) o.ttr = tt.map(v => v + 64);
		if (r.offset) o.ofs = r.offset;
		if (r.length !== lenOf(r.pattern) - r.offset) o.len = r.length;
		if (r.tempo != null) o.bpm = r.tempo;
		const mu = [];
		for (let k = 0; k < 6; k++) if (bit(r.mutes, k)) mu.push(k);
		for (let k = 0; k < 6; k++) if (bit(r.midiMutes, k)) mu.push(k + 6);
		if (mu.length) o.mutes = mu;
		return o;
	}
	const songToPage = (d, lenOf) => d.rows.map((r, i) => rowToPage(r, i, lenOf));
	/* the page's row -> a song row for the row intents (rowSet, rowInsert), at place `i`, on top of `b` (the row it
	   replaces: what the page does not show rides along) */
	const ROW0 = { kind: "pattern", pattern: 0, target: 0, repeats: 0, mutes: 0, midiMutes: 0, offset: 0, length: 16, transpose: 0,
		trackTranspose: [0, 0, 0, 0, 0, 0], midiTranspose: [0, 0, 0, 0, 0, 0], tempo: null, x3: 0, x21: 0 };
	function rowToFw(r, b, i, lenOf) {
		const o = clone(b || ROW0);
		const kind = r.type || "pattern";
		o.kind = kind;
		if (kind === "end") { o.pattern = 255; return o; }
		if (kind !== "pattern") {
			o.pattern = 254;
			o.target = cl(kind === "halt" ? i : r.to, 0, 199);
			o.repeats = kind === "loop" ? (r.count === Infinity ? 0 : cl(r.count, 1, 63)) : o.repeats;
			return o;
		}
		o.pattern = cl(r.pat);
		o.repeats = cl(r.rep - 1, 0, 63);
		o.transpose = r.trn != null ? cl(r.trn) - 64 : 0;
		const tt = r.ttr || Array(12).fill(64);
		o.trackTranspose = tt.slice(0, 6).map(v => cl(v) - 64);
		o.midiTranspose = tt.slice(6, 12).map(v => cl(v) - 64);
		o.offset = cl(r.ofs || 0, 0, 63);
		o.length = cl(r.len ?? (lenOf(o.pattern) - o.offset), 1, 64);
		o.tempo = r.bpm != null ? r.bpm : null;
		const mu = r.mutes || [];
		o.mutes = mu.filter(k => k < 6).reduce((m, k) => m | 1 << k, 0);
		o.midiMutes = mu.filter(k => k >= 6).reduce((m, k) => m | 1 << (k - 6), 0);
		return o;
	}

	/* ---------------- global ---------------- */
	/* MULTI MAP (MULTIMAP EDIT, MM-P4): the page's rows {hi, pat (-1 = CUR), ofs (0 = ---, else offset + 1),
	   len, trn (64-centred), tim (0 DIR, 1 2 4 8 16 32)}; the ranges end where an upper key repeats */
	const s8 = v => v > 127 ? v - 256 : v;
	function mapToPage(g) {
		const [hi, pat, ofs, len, trn, tim] = g.multiMap, rows = [];
		for (let r = 0; r < hi.length; r++) {
			if (r && hi[r] <= hi[r - 1]) break;
			rows.push({ hi: hi[r], pat: pat[r] === 255 ? -1 : pat[r], ofs: ofs[r] === 255 ? 0 : ofs[r] + 1, len: len[r], trn: cl(s8(trn[r]) + 64), tim: tim[r] });
		}
		return rows;
	}
	/* ---------------- modulators (md-desk/modulators, run by the plug-in) ---------------- */
	/* The Control workspace's app sources and links (ctlSetup(): LFO {SHAPE, RATE, DEPTH}, Random
	   {RATE, SMOOTH}; links {src, t, pid "SYN.3", min, max, curve, inv}) <-> the plug-in's setup.
	   A link's param is the kit document's page index * 8 + the parameter (PAGES order). The page's
	   LFO shapes are LWAVE indices (TRI SAW SQR EXP RMP RND, the page's SAW falls); the ModEngine's
	   are 0 triangle, 1 saw (rising), 2 square, 3 linear decay, 4 exp decay, 5 random. Targets on
	   MIDI tracks (t 6-11, the MID page) are not modulated by the plug-in: they are left out. */
	const SHAPE_FW = { 0: 0, 2: 3, 4: 2, 6: 4, 8: 1, 10: 5 };
	const SHAPE_PAGE = { 0: 0, 1: 8, 2: 4, 3: 2, 4: 6, 5: 10 };
	const RATES = ["1/16", "1/8", "1/4", "1/2", "1", "2", "4"];
	function modToFw(setup) {
		const sources = setup.sources.map(x => ({
			id: x.id, label: x.label, kind: x.kind === "lfo" ? "lfo" : "random",
			shape: x.kind === "lfo" ? SHAPE_FW[x.SHAPE] ?? SHAPE_FW[x.SHAPE & ~1] ?? 0 : 0,
			rate: RATES.includes(x.RATE) ? x.RATE : "1/2",
			depth: x.kind === "lfo" ? cl(x.DEPTH ?? 100, 0, 100) : 100,
			smooth: x.kind === "lfo" ? 30 : cl(x.SMOOTH ?? 30)
		}));
		const links = setup.links.map(l => {
			const [pg, i] = l.pid.split("."), p = PAGES.indexOf(pg);
			if (l.t < 0 || l.t > 5 || p < 0) return null;
			return { source: l.src, track: l.t, param: p * 8 + +i, min: cl(l.min), max: cl(l.max), curve: l.curve, invert: !!l.inv };
		}).filter(Boolean);
		return { schema: "mm-desk/modulators", version: 1, sources, links };
	}
	function modToPage(d) {
		return {
			sources: d.sources.map(x => x.kind === "lfo"
				? { id: x.id, kind: "lfo", label: x.label, val: 64, SHAPE: SHAPE_PAGE[x.shape] ?? 0, RATE: x.rate, DEPTH: x.depth }
				: { id: x.id, kind: "rnd", label: x.label, val: 64, RATE: x.rate, SMOOTH: x.smooth, _t: 64 }),
			links: d.links.map(l => ({ src: l.source, t: l.track, pid: PAGES[l.param >> 3] + "." + (l.param & 7), min: l.min, max: l.max, curve: l.curve, inv: !!l.invert }))
		};
	}

	return {
		kitToPage, kitName, kitEmpty, patternToPage, songToPage, rowToPage, rowToFw, mapToPage,
		modToFw, modToPage, hasTrigs, machineName, useCatalogue, ampEnv, enumN, valueToPage, valueToFw, lockKey, parseLockKey, TABS
	};
})();
