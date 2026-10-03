"use strict";
/* Monomachine Editor: the view of the machine's documents (DESIGN-UNIFY.md 4.3, phase 1). Pure: no DOM, no
   messages, no state but a memo of the translations.
     V = Overlay.over(MmView.derive(DocOverlay.over(Docs), ui))     (mmAdapter.js)
   derive(docs, ui) builds the document members of the mockup's state exactly as its renderers read them
   (S.tracks, S.midi, S.locks, S.len ... S.song, S.mmap, S.bpm, the mutes and POLY), from the contract's
   documents through MmConvert's firmware-to-page functions; the mockup's MMView.show(V) writes them into S
   (the one place they reach it). A member whose documents are not there yet is left out (undefined).
     docs   the page's documents (deskDocs.js, the MM's kinds: patterns, kits, songs, globals by slot, workingKit)
            plus docs.machine, the machine document
     ui     what of the page the view needs: songEdit (the song the Song workspace edits; null: the machine's)
   The translations are kept per document value (a document is a value: the same object is the same page), so
   the background library read, which changes no current document, derives at no cost; the members built from
   them are shared with that memo, so a view the overlay writes deep into is own()ed first (a copy of the members
   built from documents). writes(view, command, clip) is what an edit intent shows at once, copied(view, command) the
   page's copy of what a copy puts on the core's clipboard, toFw(command, view, docs) an intent's values in the
   contract's units, kindOf(op) the document an op edits. Depends on MmConvert and the mockup's tables (MACH, FIXED,
   PAGES, INPUTS, DEFV, synDefaults, isFx, machName), and Overlay, all loaded before it is called. */
const MmView = (() => {
	const C = () => MmConvert;
	const SONGS = 24;
	/* the current slot of a kind, from a machine document ({current} per kind; null: not known yet).
	   One song and one global are always selected on the machine: slot 0 until it says. */
	function slotIn(m, kind) {
		const c = m && m[kind] ? m[kind].current : null;
		return c != null ? c : kind === "song" || kind === "global" ? (m ? 0 : null) : null;
	}
	const queuedIn = m => { const q = m?.pattern?.queued; return q != null && q !== m.pattern.current ? q : null; };
	/* the kit that plays: the working kit when it is the current slot's, else the stored slot (until the first
	   working kit arrives, and while a kit change has not brought the new one yet) */
	const kitDocOf = (docs, k) => k == null ? null : docs.workingKit && docs.workingKit.slot === k ? docs.workingKit.doc : docs.kits[k] || null;

	/* the last result of f for the same arguments (by identity) */
	function memo(f) {
		let args = null, out;
		return (...a) => {
			if (args && a.length === args.length && a.every((x, i) => x === args[i])) return out;
			args = a;
			return out = f(...a);
		};
	}
	/* a lock the track's machine has no parameter for is not shown (the mockup's pruneLocks) */
	function lockShown(key, kit) {
		const [t, pid] = key.split("|"), [pg, i] = pid.split("."), n = +t;
		const names = n >= 6 ? FIXED[pg] : pg === "SYN" ? MACH[kit.tracks[n].m]?.p : FIXED[pg];
		return !!(names && names[+i]);
	}
	/* the kit that plays as the page's tracks (captureKit() shape), its MIDI tracks' channels and CCs from the global */
	const kitPage = memo((d, global) => {
		const k = C().kitToPage(d, global);
		return Object.assign(k, { name: C().kitName(d) });
	});
	/* the current pattern as the page's (capturePat() shape: Sets for SLIDE and SWING, a Map of Maps for the locks),
	   its locks' values in the kit's machines */
	const patternPage = memo((d, kit) => {
		const p = C().patternToPage(d, kit);
		return {
			len: p.len, mult: p.mult, swingAmt: p.swingAmt, patTrn: p.patTrn,
			tr: p.tr.map(x => ({ steps: x.steps, slide: new Set(x.slide), swing: new Set(x.swing), arp: x.arp, tr: x.tr })),
			locks: new Map(p.locks.filter(([k]) => lockShown(k, kit)).map(([k, steps]) => [k, new Map(steps)])),
			slot: { kit: d.kit, has: C().hasTrigs(d), len: d.length }
		};
	});
	const mapPage = memo(g => g.multiMap ? C().mapToPage(g) : undefined);

	/* the 24 songs for the Song workspace's picker: names, the one edited and the machine's */
	function songsOf(docs, slot, current) {
		const names = Array.from({ length: SONGS }, (_, i) => { const s = docs.songs[i]; return s ? (C().kitEmpty(s) ? "EMPTY" : C().kitName(s)) : "…"; });
		return { names, slot, current };
	}
	/* the machine's own mutes, T1-T6 then M1-M6 (null: not known) */
	function mutesOf(m) {
		const bits = [m.mutes?.synth, m.mutes?.midi];
		return Array.from({ length: 12 }, (_, i) => { const b = bits[i < 6 ? 0 : 1]; return b == null ? null : !!((b >> (i % 6)) & 1); });
	}

	function derive(docs, ui = {}) {
		const m = docs.machine || null;
		const pat = slotIn(m, "pattern"), kit = slotIn(m, "kit"), song = ui.songEdit ?? slotIn(m, "song"), glob = slotIn(m, "global");
		const v = { pat, kit, songSlot: song, queued: queuedIn(m) };
		if (m) {
			v.kitState = m.kit?.working || "unknown";
			if (m.tempo != null) v.bpm = m.tempo;
			v.poly = m.poly ?? null;
			v.mutes = mutesOf(m);
			v.plays = { chain: m.desk?.chain ?? null, songMode: m.song?.songMode === true, song: m.song?.current ?? 0 };
			v.songs = songsOf(docs, song, slotIn(m, "song"));
		}
		const global = glob != null ? docs.globals[glob] || null : null;
		const kd = kitDocOf(docs, kit), pd = pat != null ? docs.patterns[pat] || null : null;
		/* the view shows the machine once the current pattern and the kit that plays are there */
		v.ready = !!(kd && pd);
		if (!v.ready) return v;
		const k = kitPage(kd, global), p = patternPage(pd, k);
		v.workName = k.name;
		if (k.multi !== undefined) v.multi = k.multi;
		v.menv = k.menv;
		v.tracks = k.tracks.map((x, t) => Object.assign({}, x, p.tr[t]));
		v.midi = k.midi.map((x, t) => Object.assign({}, x, p.tr[6 + t]));
		Object.assign(v, { len: p.len, mult: p.mult, swingAmt: p.swingAmt, patTrn: p.patTrn, locks: p.locks });
		v.patSlot = Object.assign({ p: pat }, p.slot);
		/* the patterns' lengths the page knows (a song row of a whole pattern is as long as its pattern) */
		v.patLens = Object.fromEntries(Object.entries(docs.patterns).map(([q, d]) => [q, d.length]));
		const sd = song != null ? docs.songs[song] : null;
		if (sd) v.song = C().songToPage(sd, q => docs.patterns[q]?.length ?? 16);
		if (global) {
			v.routing = global.routingMode;
			const map = mapPage(global);
			if (map) v.mmap = map;
		}
		return v;
	}

	/* ---------------- what an edit intent shows at once (DESIGN-UNIFY.md 4.4) ----------------
	   writes(view, command): the [path, value] writes into the derived view that show the command's effect before
	   its result comes (Overlay, owned by the command's id). Values, never toggles; a command in the contract's units
	   (the firmware's: a lock's or a knob's raw value), written in the view's (MmConvert's page units). The intent
	   cases (doc/modern-ux/intent-cases.json) pin them to the core's edit (mmDeskEdit.cpp): applied over the view of
	   a case's documents they make the view of the documents after it. A command whose effect the view cannot know
	   (pasteSteps: the core's clipboard) writes nothing; the result brings it. */
	const DELETE = () => Overlay.DELETE;
	const trackPath = t => t < 6 ? ["tracks", t] : ["midi", t - 6];
	const trackOf = (v, t) => t < 6 ? v.tracks[t] : v.midi[t - 6];
	const lockKeyOf = (t, page, i) => t < 6 ? C().lockKey(t, page, i) : C().lockKey(t - 6, 7, i);
	const keysOf = (v, t) => [...v.locks.keys()].filter(k => +k.split("|")[0] === t);
	/* a lock the view shows (the track's machine has the parameter), in page units */
	function lockWrite(v, t, page, i, raw) {
		const key = lockKeyOf(t, page, i);
		if (t < 6 && !lockShown(key, { tracks: v.tracks })) return null;
		return [key, t < 6 ? C().valueToPage(v.tracks[t].m, PAGES[page], i, raw) : raw];
	}
	/* a step value of the intent as the view holds it (MmConvert.patternToPage's shape) */
	function pageStep(x, t) {
		if (x == null) return null;
		if (x.off) return { off: 1 };
		const n = Array.isArray(x.n) && x.n.length ? x.n.map(Number) : null;
		if (t >= 6) return n ? { a: 1, f: 1, l: 1, n } : { a: 1, f: 1, l: 1 };
		const trig = !x.notrig || !!n, st = { a: x.a ? 1 : 0, f: x.f ? 1 : 0, l: x.l ? 1 : 0 };
		if (!trig && !st.a && !st.f && !st.l) return null;
		if (n) st.n = n;
		if (!trig) st.notrig = 1;
		return st;
	}
	/* step s of track t becomes x; an empty step or a NOTE OFF loses its locks */
	function stepWrites(v, t, s, x) {
		const st = pageStep(x, t), out = [[[...trackPath(t), "steps", s], st]];
		if (!st || st.off) for (const k of keysOf(v, t)) if (v.locks.get(k).has(s)) out.push([["locks", k, s], DELETE()]);
		return out;
	}
	const ARP = { play: "PLAY", ojmp: "OJMP", mode: "MODE", length: "len" };
	const rotStep = (s, by, len) => ((s + by) % len + len) % len;
	const WRITES = {
		level: (v, c) => [[["tracks", c.t, "lev"], c.v]],
		route: (v, c) => [[["tracks", c.t, "out"], { AB: !!(c.out & 1), CD: !!(c.out & 2), EF: !!(c.out & 4) }]],
		input: (v, c) => [[["tracks", c.t, "inp"], INPUTS[c.v] ?? INPUTS[0]]],
		param: (v, c) => c.t < 6 ? [[["tracks", c.t, "v", PAGES[c.page], c.i], C().valueToPage(v.tracks[c.t].m, PAGES[c.page], c.i, c.v)]]
			: [[["midi", c.t - 6, "v", "MID", c.i], c.v]],
		trigPos: (v, c) => [[["tracks", c.t, "trigpos"], c.v ?? null]],
		legato: (v, c) => [[["tracks", c.t, "leg", { amp: "amp", filter: "flt", lfo: "lfo" }[c.env]], c.on ? 1 : 0]],
		portamento: (v, c) => [[["tracks", c.t, "port"], c.v === "always" ? 0 : 1]],
		routing: (v, c) => [[["routing"], c.v]],
		midiTrack: (v, c) => [...(c.ch != null ? [[["midi", c.t, "ch"], c.ch + 1]] : []), ...(c.cc ? [[["midi", c.t, "cc"], c.cc.map(Number)]] : [])],
		step: (v, c) => stepWrites(v, c.t, c.s, c.v),
		slide: (v, c) => [[[...trackPath(c.t), "slide", c.s], !!c.on]],
		swingStep: (v, c) => [[[...trackPath(c.t), "swing", c.s], !!c.on]],
		lock: (v, c) => {
			if (c.v == null) return [[["locks", lockKeyOf(c.t, c.page, c.i), c.s], DELETE()]];
			const w = lockWrite(v, c.t, c.page, c.i, c.v);
			return w ? [[["locks", w[0], c.s], w[1]]] : [];
		},
		clearLane: (v, c) => [[["locks", lockKeyOf(c.t, c.page, c.i)], DELETE()]],
		clearLocks: (v, c) => keysOf(v, c.t).map(k => [["locks", k], DELETE()]),
		clearPattern: v => [...Array(12).keys()].flatMap(t => [[[...trackPath(t), "steps"], Array(64).fill(null)], [[...trackPath(t), "slide"], new Set()]])
			.concat([[["locks"], new Map()]]),
		steps: (v, c) => c.rows.flatMap(r => {
			const out = [], want = new Map(r.steps.map(([s, x]) => [s, x]));
			for (let s = c.from; s < c.to; s++) out.push(...(r.locks ? [[[...trackPath(r.t), "steps", s], pageStep(want.get(s), r.t)]] : stepWrites(v, r.t, s, want.get(s))));
			if (r.slide) { const on = new Set(r.slide); for (let s = c.from; s < c.to; s++) out.push([[...trackPath(r.t), "slide", s], on.has(s)]); }
			if (r.locks) {
				for (const k of keysOf(v, r.t)) for (const s of v.locks.get(k).keys()) if (s >= c.from && s < c.to) out.push([["locks", k, s], DELETE()]);
				for (const [page, i, s, raw] of r.locks) { const w = lockWrite(v, r.t, page, i, raw); if (w) out.push([["locks", w[0], s], w[1]]); }
			}
			return out;
		}),
		rotate: (v, c) => {
			const len = Math.max(2, Math.min(64, v.len)), tr = trackOf(v, c.t);
			if (c.by % len === 0) return [];
			const steps = [...tr.steps], slide = new Set();
			for (let s = 0; s < len; s++) steps[rotStep(s, c.by, len)] = tr.steps[s];
			for (const s of tr.slide) slide.add(s < len ? rotStep(s, c.by, len) : s);
			return [[[...trackPath(c.t), "steps"], steps], [[...trackPath(c.t), "slide"], slide],
				...keysOf(v, c.t).map(k => [["locks", k], new Map([...v.locks.get(k)].map(([s, x]) => [s < len ? rotStep(s, c.by, len) : s, x]))])];
		},
		doublePattern: v => {
			const len = v.len, out = [[["len"], len * 2]];
			if (len * 2 > 64) return [];
			for (let t = 0; t < 12; t++) {
				const tr = trackOf(v, t), steps = [...tr.steps], dbl = set => { const o = new Set([...set].filter(s => s < len || s >= 2 * len)); for (const s of set) if (s < len) o.add(s + len); return o; };
				for (let s = 0; s < len; s++) steps[s + len] = tr.steps[s];
				out.push([[...trackPath(t), "steps"], steps], [[...trackPath(t), "slide"], dbl(tr.slide)], [[...trackPath(t), "swing"], dbl(tr.swing)]);
			}
			for (const [k, m] of v.locks) {
				const o = new Map([...m].filter(([s]) => s < len || s >= 2 * len));
				for (const [s, x] of m) if (s < len) o.set(s + len, x);
				out.push([["locks", k], o]);
			}
			return out.concat(rowLens(v, len * 2));
		},
		length: (v, c) => [[["len"], c.v], ...rowLens(v, c.v)],
		speed: (v, c) => [[["mult"], c.v]],
		swing: (v, c) => [[["swingAmt"], c.v]],
		transpose: (v, c) => c.t == null ? [[["patTrn"], c.v + 64]] : [
			...(c.v != null ? [[[...trackPath(c.t), "tr", "TRACK"], c.v + 64]] : []),
			...(c.scale != null ? [[[...trackPath(c.t), "tr", "SCALE"], c.scale]] : []),
			...(c.key != null ? [[[...trackPath(c.t), "tr", "KEY"], c.key]] : [])],
		arp: (v, c) => {
			const at = [...trackPath(c.t), "arp"];
			if (ARP[c.field]) return [[[...at, ARP[c.field]], c.v]];
			if (c.field === "range") return [[[...at, "RNGE"], c.v + 1]];
			if (c.field === "speed") return [[[...at, "SPD"], c.v + 1]];
			if (c.field === "trigs") return [[[...at, "amp"], c.v & 1], [[...at, "flt"], (c.v >> 1) & 1], [[...at, "lfo"], (c.v >> 2) & 1]];
			return [[[...at, "rhy", c.i], c.v !== 255], [[...at, "ofs", c.i], c.v === 255 ? 0 : c.v - 64]];
		},
		clearSteps: (v, c) => {
			const out = [];
			for (let s = c.from; s < c.to; s++) out.push(...stepWrites(v, c.t, s, null), [[...trackPath(c.t), "slide", s], false]);
			return out;
		},
		copySteps: () => [],
		/* the core's clipboard; the page shows the paste from its own copy of it (clip: MmView.copied) */
		pasteSteps: (v, c, clip) => {
			if (!clip || clip.kind !== "steps" || clip.midi !== c.t >= 6) return [];
			const to = Math.min(64, c.to ?? c.from + clip.steps.length), out = [], on = new Set();
			for (let s = c.from; s < to; s++) {
				const rel = s - c.from, x = rel < clip.steps.length ? stepVal(clip.steps[rel]) : null;
				out.push(...stepWrites(v, c.t, s, x), [[...trackPath(c.t), "slide", s], clip.slide.includes(rel)]);
				if (x && !x.off) on.add(s);
			}
			for (const [pid, steps] of clip.locks) {
				const [pg, i] = pid.split(".");
				if (c.t < 6 && pg === "SYN" && !MACH[v.tracks[c.t].m]?.p[+i]) continue;	// that machine has no such parameter
				for (const [rel, x] of steps) if (c.from + rel < to && on.has(c.from + rel)) out.push([["locks", c.t + "|" + pid, c.from + rel], x]);
			}
			return out;
		},
		/* ---- Sound and Perform: the kit that plays ---- */
		machine: (v, c) => machineWrites(v, c.t, C().machineName(c.model), c.keepFx !== false),
		clearSound: (v, c) => machineWrites(v, c.t, "GND-SIN", false),
		copySound: () => [],
		pasteSound: (v, c, clip) => clip && clip.kind === "sound" ? soundWrites(v, c.t, clip.m, clip.v) : [],
		params: (v, c) => c.values.flatMap(([t, page, i, x]) => WRITES.param(v, { t, page, i, v: x })),
		assign: (v, c) => {
			const at = ["tracks", c.t, "assign"], out = [], tab = (C().TABS.find(([, src]) => src === c.src) || [])[0];
			if (tab) for (const [k, n, f] of [["page", "pg", x => x], ["dest", "d", x => x], ["add", "add", x => Math.max(0, Math.min(127, x + 64))]])
				if (c[k] != null) out.push([[...at, "tabs", tab, c.row, n], f(c[k])]);
			for (const [k, n] of [["mirror", "mirr"], ["hpf", "hpf"], ["lpf", "lpf"]]) if (c[k] != null) out.push([[...at, n], !!c[k]]);
			return out;
		},
		multiEnv: (v, c) => c.i < MENV.length ? [[["menv", MENV[c.i]], c.v]] : [],
		multiTrig: (v, c) => !v.multi ? [] : [["mode", c.mode], ["splitKey", c.splitKey], ["splitTrack", c.splitTrack != null ? c.splitTrack + 1 : null], ["timing", c.timing]]
			.filter(([, x]) => x != null).map(([k, x]) => [["multi", k], x]),
		kitName: (v, c) => [[["workName"], String(c.name).toUpperCase()]],
		/* ---- the MULTI MAP: the page's ranges, as a whole ---- */
		multiMap: (v, c) => mapWrites(v, rows => {
			const r = rows[c.i];
			if (!r) return false;
			if (c.hi != null) r.hi = c.hi;
			if (c.pat != null) r.pat = c.pat === 255 ? -1 : c.pat;
			if (c.ofs != null) r.ofs = c.ofs === 255 ? 0 : c.ofs + 1;
			if (c.len != null) r.len = c.len;
			if (c.trn != null) r.trn = c.trn + 64;
			if (c.tim != null) r.tim = c.tim;
		}),
		multiMapSplit: (v, c) => mapWrites(v, rows => {
			const r = rows[c.i], lo = c.i ? rows[c.i - 1].hi + 1 : 0;
			if (!r || r.hi - lo < 1 || rows.length >= 32) return false;
			rows.splice(c.i, 0, Object.assign({}, r, { hi: Math.floor((lo + r.hi) / 2) }));
		}),
		multiMapDelete: (v, c) => mapWrites(v, rows => {
			if (!rows[c.i] || rows.length < 2) return false;
			const last = c.i === rows.length - 1;
			rows.splice(c.i, 1);
			if (last) rows[rows.length - 1].hi = 127;
		}),
		/* ---- Song rows: the rows the Song workspace shows, as a whole (loop and jump targets follow their rows) ---- */
		rowSet: (v, c) => songWrites(v, rows => { if (!rows[c.i]) return false; rows[c.i] = C().rowToPage(c.row, c.i, lenOf(v)); }),
		rowInsert: (v, c) => songWrites(v, rows => insertRow(rows, c.i, C().rowToPage(c.row, c.i, lenOf(v)))),
		pasteRow: (v, c, clip) => clip && clip.kind === "row" ? songWrites(v, rows => insertRow(rows, c.i, structuredClone(clip.row))) : [],
		copyRow: () => [],
		rowDelete: (v, c) => songWrites(v, rows => {
			if (!rows[c.i] || rows[c.i].type === "end") return false;
			rows.splice(c.i, 1);
			retarget(rows, j => j <= c.i ? j : j - 1);
		}),
		rowMove: (v, c) => songWrites(v, rows => {
			if (!rows[c.from] || !rows[c.to] || rows[c.from].type === "end" || rows[c.to].type === "end") return false;
			const order = rows.map((_, j) => j), [moved] = order.splice(c.from, 1);
			order.splice(c.to, 0, moved);
			const map = [], was = rows.slice();
			order.forEach((j, n) => { map[j] = n; rows[n] = was[j]; });
			retarget(rows, j => map[j] ?? j);
		})
	};
	/* the library's slot ops change stored slots: the result brings them (the library's own setters) */
	for (const op of ["patCopy", "patPaste", "patCopyTo", "patClear", "kitCopy", "kitPaste", "kitCopyTo", "kitClear", "kitRename"]) WRITES[op] = () => [];
	const MENV = ["ATK", "DEC", "SUS", "REL", "PORT"];
	/* a step of the view as the step intent's value: null, {off:true} or {n, a, f, l, notrig} */
	function stepVal(st) {
		if (!st) return null;
		if (st.off) return { off: true };
		const x = { a: st.a ? 1 : 0, f: st.f ? 1 : 0, l: st.l ? 1 : 0 };
		if (st.n && st.n.length) x.n = [...st.n];
		if (st.notrig) x.notrig = true;
		return x;
	}
	/* track t gets machine m with its start values (the core's, mmDeskEdit.cpp assignMachine: the SYN defaults; without
	   keepFx the other pages' neutral values; a processing machine on a track that had none listens and lets the sound
	   through) */
	function machineWrites(v, t, m, keepFx) {
		if (!m || !MACH[m]) return [];
		const pages = Object.assign({}, v.tracks[t].v, { SYN: synDefaults(m) }), out = [];
		if (!keepFx) Object.assign(pages, { AMP: [...DEFV.AMP], FLT: [...DEFV.FLT], EFX: [...DEFV.EFX], LF1: [...DEFV.LFO], LF2: [...DEFV.LFO], LF3: [...DEFV.LFO] });
		if (isFx(m) && !isFx(v.tracks[t].m)) {
			pages.AMP = [...pages.AMP];
			pages.AMP[2] = pages.AMP[3] = 127;
			out.push([["tracks", t, "inp"], INPUTS[t === 0 ? 3 : 0]]);
		}
		return out.concat(soundWrites(v, t, m, pages));
	}
	/* track t's machine and pages; its SYN locks follow the machine's parameters */
	function soundWrites(v, t, m, pages) {
		const out = [[["tracks", t, "m"], m], [["tracks", t, "name"], machName(m)]], was = v.tracks[t].m;
		for (const pg of PAGES) out.push([["tracks", t, "v", pg], [...pages[pg]]]);
		for (const k of keysOf(v, t)) {
			const [pg, i] = k.split("|")[1].split(".");
			if (pg !== "SYN") continue;
			if (!MACH[m].p[+i]) { out.push([["locks", k], DELETE()]); continue; }
			/* a lock's value in the new machine's units (from its band's value: a preview, the result brings the core's) */
			if (was !== m) out.push([["locks", k], new Map([...v.locks.get(k)].map(([s, x]) => [s, C().valueToPage(m, "SYN", +i, C().valueToFw(was, "SYN", +i, x))]))]);
		}
		return out;
	}
	function mapWrites(v, f) {
		if (!v.mmap) return [];
		const rows = v.mmap.map(r => Object.assign({}, r));
		return f(rows) === false ? [] : [[["mmap"], rows]];
	}
	const lenOf = v => q => q === v.pat ? v.len : v.patLens?.[q] ?? 16;
	/* the pattern that plays gets another length: a song row of it shows its own length unless it is the whole pattern */
	function rowLens(v, len) {
		if (!v.song || !v.song.some(r => !r.type && r.pat === v.pat)) return [];
		return [[["song"], v.song.map(r => {
			if (r.type || r.pat !== v.pat) return r;
			const o = Object.assign({}, r), l = r.len ?? v.len - (r.ofs || 0);
			if (l !== len - (r.ofs || 0)) o.len = l; else delete o.len;
			return o;
		})]];
	}
	/* loop and jump targets follow their rows (map: old place -> new); a HALT row's target is its own place */
	function retarget(rows, map) {
		rows.forEach((r, j) => { if (r.type === "loop" || r.type === "jump") r.to = map(r.to); else if (r.type === "halt") r.to = j; });
	}
	function insertRow(rows, at, row) {
		if (!rows[at] || rows.length >= 200) return false;
		rows.splice(at, 0, row);
		retarget(rows, j => j < at ? j : j + 1);
	}
	function songWrites(v, f) {
		if (!v.song) return [];
		const rows = v.song.map(r => Object.assign({}, r));
		return f(rows) === false ? [] : [[["song"], rows]];
	}
	/* the command's writes over the view (none while the view does not show the machine); clip: the page's copy of
	   the core's clipboard (copied), for a paste */
	function writes(v, cmd, clip) {
		const f = WRITES[cmd.op];
		return f && v && v.ready ? f(v, cmd, clip) : [];
	}
	/* what a copy command puts on the core's clipboard, in the view's units: the page's own copy of it, so a paste
	   shows at once (null: not a copy) */
	function copied(v, cmd) {
		if (!v || !v.ready) return null;
		if (cmd.op === "copySteps") {
			const tr = trackOf(v, cmd.t), locks = [];
			for (const k of keysOf(v, cmd.t)) {
				const steps = [...v.locks.get(k)].filter(([s]) => s >= cmd.from && s < cmd.to).map(([s, x]) => [s - cmd.from, x]);
				if (steps.length) locks.push([k.split("|")[1], steps]);
			}
			return { kind: "steps", midi: cmd.t >= 6, steps: structuredClone(tr.steps.slice(cmd.from, cmd.to)),
				slide: [...tr.slide].filter(s => s >= cmd.from && s < cmd.to).map(s => s - cmd.from), locks };
		}
		if (cmd.op === "copySound") return { kind: "sound", m: v.tracks[cmd.t].m, v: structuredClone(v.tracks[cmd.t].v) };
		if (cmd.op === "copyRow" && v.song?.[cmd.i] && v.song[cmd.i].type !== "end") return { kind: "row", row: structuredClone(v.song[cmd.i]) };
		return null;
	}
	/* a view the overlay may write into: its members built from the documents are shared with the memo above, so
	   the ones an edit writes deep into are copied first (only while there are writes) */
	function own(v) {
		if (!v || !v.ready) return v;
		for (const k of ["tracks", "midi", "locks", "menv", "multi", "mmap", "song"]) if (v[k] !== undefined) v[k] = structuredClone(v[k]);
		return v;
	}

	/* ---------------- an intent's values in the contract's units (DESIGN-UNIFY.md 4.1) ----------------
	   A gesture gives knob and lock values in the page's units and a song row as the page shows it; the command carries
	   the firmware's: an enumeration's value (MmConvert.valueToFw; an unchanged index keeps the raw value the documents
	   hold), a song row (MmConvert.rowToFw, on top of the row it replaces). docs: the documents the values come from
	   ({pattern, kit, song}; none: the bands' own values and a new row's defaults). */
	function toFw(c, v, docs = {}) {
		const p = docs.pattern, k = docs.kit, m = t => v.tracks?.[t]?.m;
		const lockFw = (t, page, i, s, x) => {
			if (t >= 6) return x;
			const l = p?.locks?.find(y => y.track === t && y.page === page && y.param === i), base = l?.steps.find(([y]) => y === s)?.[1];
			return C().valueToFw(m(t), PAGES[page], i, x, base);
		};
		const knobFw = (t, page, i, x) => t < 6 ? C().valueToFw(m(t), PAGES[page], i, x, k ? k.tracks[t].pages[page][i] : null) : x;
		if (c.op === "param") c.v = knobFw(c.t, c.page, c.i, c.v);
		if (c.op === "params") c.values = c.values.map(([t, page, i, x]) => [t, page, i, knobFw(t, page, i, x)]);
		if (c.op === "lock" && c.v != null) c.v = lockFw(c.t, c.page, c.i, c.s, c.v);
		if (c.op === "steps") c.rows = c.rows.map(r => r.locks ? Object.assign({}, r, { locks: r.locks.map(([page, i, s, x]) => [page, i, s, lockFw(r.t, page, i, s, x)]) }) : r);
		if (c.op === "rowSet" || c.op === "rowInsert") c.row = C().rowToFw(c.row, c.op === "rowSet" ? docs.song?.rows?.[c.i] : null, c.i, lenOf(v));
		return c;
	}

	/* ---------------- which document an op edits ----------------
	   the kit that plays (k), the active global (nothing to name), a song (s: the one the Song workspace edits), the
	   library's stored slots (the command names them), else the pattern that plays (p) */
	const OPS = {
		kit: ["level", "route", "input", "param", "trigPos", "legato", "portamento", "machine", "clearSound", "copySound", "pasteSound", "params",
			"assign", "multiEnv", "multiTrig", "kitName"],
		global: ["routing", "midiTrack", "multiMap", "multiMapSplit", "multiMapDelete"],
		song: ["rowSet", "rowInsert", "rowDelete", "rowMove", "copyRow", "pasteRow"],
		library: ["patCopy", "patPaste", "patCopyTo", "patClear", "kitCopy", "kitPaste", "kitCopyTo", "kitClear", "kitRename"]
	};
	const KIND = new Map(Object.entries(OPS).flatMap(([kind, ops]) => ops.map(op => [op, kind])));
	const kindOf = op => KIND.get(op) || "pattern";

	return { derive, slotIn, queuedIn, kitDocOf, writes, copied, toFw, kindOf, own };
})();
