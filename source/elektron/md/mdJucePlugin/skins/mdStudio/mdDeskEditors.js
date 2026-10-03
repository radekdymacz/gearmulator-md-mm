"use strict";
/* The canvas editors (ED, the mockup's): one model per kind of screen, keyed by a canvas's data-ed. An editor
   draws (draw), gives its dots (handles, each with the values its drag moves) and says where the values go
   (to: a track's page, or a master effect; sendEditor, mdDeskApp.js). Here: the LFO's two screens, the effects
   and routing groups' screens, and the synthesis groups' screens (one per kind of picture, for every machine
   whose group has its knobs); the master effects' editors are mdDeskMix.js's, the sample screens
   mdDeskSampler.js's. redraw() paints every editor on the page once a frame. */

/* ===== Curve editors (the mockup's): a handle's drag gives the values it moves, the editor's "to"
   where they go (sendEditor). ===== */
const toTrack = g => () => ({ t: S.sel, g }), toMfx = f => () => ({ f });
const cssv = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const ED = {
	/* LFO SHAPE: one cycle of what the LFO sends: SHP1 and SHP2 (inverted, faint) mixed by SHMIX (solid).
	   Dot = SHMIX sideways. */
	lshape: {
		to: () => ({ t: S.sel, g: "lfo" }),
		draw(g, W, H) {
			const l = V.tracks[S.sel].lfo, mix = l.SHMIX / 127, A = H / 2 - 16, y = (p, w) => H / 2 + 4 - ((1 - w) * shape(l.SHP1, p, false) + w * shape(l.SHP2, p, true)) * A; grid(g, W, H);
			line(g, W, x => y(x / W, 0), inkA(.3), 1.2); line(g, W, x => y(x / W, 1), inkA(.3), 1.2, [3, 3]); line(g, W, x => y(x / W, mix), cssv("--ink"), 2.2);
			label(g, `${SHAPES[l.SHP1] || "?"} ${Math.round((1 - mix) * 100)}% · ${(SHAPES[l.SHP2] || "?").toLowerCase()} inv ${Math.round(mix * 100)}%`);
		},
		handles(W, H) { return [{ x: 8 + V.tracks[S.sel].lfo.SHMIX / 127 * (W - 16), y: H - 10, k: "SHMIX", c: cssv("--ink"), drag: x => ({ SHMIX: clamp(Math.round((x - 8) / (W - 16) * 127)) }) }]; }
	},
	/* LFO MOTION: the LFO across one bar of this track: SPD cycles (more as it rises), DEPTH high (dashed
	   bounds); UPDTE TRIG restarts it on every trig (the ticks), HOLD keeps the value a trig takes. Dot = SPD
	   sideways, DEPTH up. */
	lmotion: {
		to: () => ({ t: S.sel, g: "lfo" }),
		geo(W, H) { return { x0: 8, x1: W - 8, A: H / 2 - 18, mid: H / 2 + 2 }; },
		draw(g, W, H) {
			const tr = V.tracks[S.sel], l = tr.lfo, G = this.geo(W, H), mix = l.SHMIX / 127, dep = l.DEPTH / 127, cyc = .5 + l.SPD / 127 * 7.5, ink = cssv("--ink"); grid(g, W, H);
			const wave = u => ((1 - mix) * shape(l.SHP1, ((u % 1) + 1) % 1, false) + mix * shape(l.SHP2, ((u % 1) + 1) % 1, true)) * dep;
			const trigs = Array.from({ length: 16 }, (_, s) => tr.trigs[s] ? s : -1).filter(s => s >= 0), last = s => trigs.filter(x => x <= s).pop();
			const at = x => { const s = x / W * 16, k = last(s); if (l.UPDTE === "FREE" || k == null) return l.UPDTE === "HOLD" ? 0 : wave(s / 16 * cyc); return l.UPDTE === "TRIG" ? wave((s - k) / 16 * cyc) : wave(k / 16 * cyc); };
			g.strokeStyle = inkA(.45); g.lineWidth = 1; g.setLineDash([2, 4]); g.beginPath(); g.moveTo(0, G.mid - dep * G.A); g.lineTo(W, G.mid - dep * G.A); g.moveTo(0, G.mid + dep * G.A); g.lineTo(W, G.mid + dep * G.A); g.stroke(); g.setLineDash([]);
			line(g, W, x => G.mid - at(x) * G.A, ink, 2.2);
			g.fillStyle = ink; trigs.forEach(s => g.fillRect(Math.round(s / 16 * W) + 1, H - 6, 3, 6));
			label(g, `${l.UPDTE} · one bar`);
		},
		handles(W, H) {
			const l = V.tracks[S.sel].lfo, G = this.geo(W, H);
			return [{ x: G.x0 + l.SPD / 127 * (G.x1 - G.x0), y: G.mid - l.DEPTH / 127 * G.A, k: "SPD · DEPTH", c: cssv("--ink"),
				drag: (x, y) => ({ SPD: clamp(Math.round((x - G.x0) / (G.x1 - G.x0) * 127)), DEPTH: clamp(Math.round((G.mid - y) / G.A * 127)) }) }];
		}
	},
};
function inkA(a) { const h = cssv("--ink").replace("#", ""); const n = parseInt(h.length === 3 ? h.split("").map(c => c + c).join("") : h, 16); return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`; }
/* The Sound chain's small screens, one per module (renderSound): each draws one stage of the track's
   sound and its dots move that stage's knobs. */
const fxOf = () => V.tracks[S.sel].fx, rtOf = () => V.tracks[S.sel].rt;
/* AMP MOD: the level the tremolo leaves (1 down to 1 - AMD), AMF cycles across. Dot = AMF sideways, AMD up. */
ED.am = {
	to: toTrack("fx"),
	geo(W, H) { return { x0: 8, x1: W - 8, y0: 24, y1: H - 8 }; },
	draw(g, W, H) {
		const f = fxOf(), G = this.geo(W, H), dep = f.AMD / 127, cyc = 1 + f.AMF / 127 * 7; grid(g, W, H);
		line(g, W, x => { const u = clamp((x - G.x0) / (G.x1 - G.x0), 0, 1); return G.y1 - (1 - dep * (.5 - .5 * Math.cos(2 * Math.PI * cyc * u))) * (G.y1 - G.y0); }, cssv("--ink"), 2);
		label(g, "tremolo");
	},
	handles(W, H) {
		const f = fxOf(), G = this.geo(W, H);
		return [{ x: G.x0 + f.AMF / 127 * (G.x1 - G.x0), y: G.y1 - (1 - f.AMD / 127) * (G.y1 - G.y0), k: "AM", c: cssv("--ink"),
			drag: (x, y) => ({ AMF: clamp(Math.round((x - G.x0) / (G.x1 - G.x0) * 127)), AMD: 127 - clamp(Math.round((G.y1 - y) / (G.y1 - G.y0) * 127)) }) }];
	}
};
/* EQ: one bell at EQF, EQG up = boost, down = cut. */
ED.peq = {
	to: toTrack("fx"),
	y(u, H) { const f = fxOf(); return H / 2 - (f.EQG - 64) / 64 * (H / 2 - 18) * Math.exp(-Math.pow((u - f.EQF / 127) * 7, 2)); },
	draw(g, W, H) {
		grid(g, W, H); g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H / 2 + .5); g.lineTo(W, H / 2 + .5); g.stroke(); g.setLineDash([]);
		line(g, W, x => this.y(x / W, H), cssv("--ink"), 2); label(g, "eq");
	},
	handles(W, H) {
		const f = fxOf();
		return [{ x: f.EQF / 127 * W, y: this.y(f.EQF / 127, H), k: "EQ", c: cssv("--ink"), drag: (x, y) => ({ EQF: clamp(Math.round(x / W * 127)), EQG: clamp(Math.round(64 + (H / 2 - y) / (H / 2 - 18) * 64)) }) }];
	}
};
/* FILTER: the response of the 24 dB filter, a pass band from FLTF to FLTF + FLTW with FLTQ peaks at its edges. */
ED.flt = {
	to: toTrack("fx"),
	resp(u) {
		const f = fxOf(), hp = f.FLTF / 127, lp = Math.min(1, hp + f.FLTW / 127), q = f.FLTQ / 127; let d = 0;
		if (u < hp) d -= Math.pow((hp - u) * 7, 2); if (u > lp) d -= Math.pow((u - lp) * 7, 2);
		d += q * 1.6 * (Math.exp(-Math.pow((u - hp) * 28, 2)) * (hp > .01 ? 1 : 0) + Math.exp(-Math.pow((u - lp) * 28, 2)) * (lp < .99 ? 1 : 0)); return d;
	},
	yy(u, H) { return clamp(H / 2 - this.resp(u) * (H / 4), 6, H - 6); },
	draw(g, W, H) { grid(g, W, H); line(g, W, x => this.yy(x / W, H), cssv("--ink"), 2.2); label(g, "filter"); },
	handles(W, H) {
		const f = fxOf(), hp = f.FLTF / 127, lp = Math.min(1, hp + f.FLTW / 127);
		return [{ x: Math.max(6, hp * W), y: this.yy(hp, H), k: "FLTF", c: cssv("--ink"), drag: (x, y) => ({ FLTF: clamp(Math.round(x / W * 127)), FLTQ: clamp(Math.round((H / 2 - y) / (H / 2) * 127)) }) },
		{ x: Math.min(W - 6, lp * W), y: this.yy(lp, H), k: "FLTW", c: cssv("--ink"), drag: x => ({ FLTW: clamp(Math.round((x / W - fxOf().FLTF / 127) * 127)) }) }];
	}
};
/* CRUSH: a sine held for longer steps as SRR rises (0 = smooth). Dot = SRR sideways. */
ED.srr = {
	to: toTrack("fx"),
	draw(g, W, H) {
		const f = fxOf(), step = 1 + f.SRR / 127 * W / 5, y = h => H / 2 + 4 - .4 * (H - 30) * Math.sin(2 * Math.PI * 1.5 * h / W); grid(g, W, H);
		line(g, W, x => y(Math.floor(x / step) * step), cssv("--ink"), 2); label(g, "srr");
	},
	handles(W, H) { return [{ x: 8 + fxOf().SRR / 127 * (W - 16), y: H - 12, k: "SRR", c: cssv("--ink"), drag: x => ({ SRR: clamp(Math.round((x - 8) / (W - 16) * 127)) }) }]; }
};
/* DRIVE: DIST's transfer curve, input to output (dashed = clean). */
ED.dist = {
	to: toTrack("rt"),
	g() { return 1 + (rtOf().DIST || 0) / 127 * 8; }, sh(x, g) { return Math.tanh(x * g) / Math.tanh(g); },
	draw(g, W, H) {
		const G = this.g(); grid(g, W, H);
		g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 8); g.lineTo(W, 8); g.stroke(); g.setLineDash([]);
		line(g, W, x => { const u = x / W * 2 - 1; return H / 2 - this.sh(u, G) * (H / 2 - 8); }, cssv("--ink"), 2.2);
		label(g, "drive");
	},
	handles(W, H) {
		const self = this, G = this.g(), u = .4, yD = H / 2 - this.sh(u, G) * (H / 2 - 8);
		return [{ x: (u + 1) / 2 * W, y: yD, k: "DIST", c: cssv("--ink"), drag: (x, y) => { const want = (H / 2 - y) / (H / 2 - 8); let best = 0, bd = 9; for (let d = 0; d <= 127; d++) { const o = self.sh(u, 1 + d / 127 * 8); if (Math.abs(o - want) < bd) { bd = Math.abs(o - want); best = d; } } return { DIST: best }; } }];
	}
};
/* MIX: the track in the stereo field: PAN sideways, VOL up. */
ED.pan = {
	to: toTrack("rt"),
	geo(W, H) { return { x0: 12, x1: W - 12, yt: 26, yb: H - 18 }; },
	draw(g, W, H) {
		const r = rtOf(), G = this.geo(W, H), ink = cssv("--ink"), x = G.x0 + (r.PAN ?? 64) / 127 * (G.x1 - G.x0), y = G.yb - (r.VOL ?? 0) / 127 * (G.yb - G.yt), pan = (r.PAN ?? 64) - 64; grid(g, W, H);
		g.strokeStyle = inkA(.45); g.lineWidth = 1; g.setLineDash([3, 3]); g.beginPath(); g.moveTo(W / 2 + .5, G.yt - 6); g.lineTo(W / 2 + .5, G.yb); g.stroke(); g.setLineDash([]);
		g.fillStyle = inkA(.22); g.fillRect(x - 7, y, 14, G.yb - y); g.fillStyle = ink; g.fillRect(G.x0, G.yb, G.x1 - G.x0, 1.5);
		g.font = "10px Silkscreen, ui-monospace, monospace"; g.fillText("L", G.x0, H - 5); g.fillText("R", G.x1 - 6, H - 5);
		label(g, "pan " + (pan ? (pan < 0 ? "L" : "R") + Math.abs(pan) : "C"));
	},
	handles(W, H) {
		const r = rtOf(), G = this.geo(W, H);
		return [{ x: G.x0 + (r.PAN ?? 64) / 127 * (G.x1 - G.x0), y: G.yb - (r.VOL ?? 0) / 127 * (G.yb - G.yt), k: "PAN · VOL", c: cssv("--ink"),
			drag: (x, y) => ({ ...("PAN" in r ? { PAN: clamp(Math.round((x - G.x0) / (G.x1 - G.x0) * 127)) } : {}), ...("VOL" in r ? { VOL: clamp(Math.round((G.yb - y) / (G.yb - G.yt) * 127)) } : {}) }) }];
	}
};
/* SENDS: the DEL and REV send levels as two bars. */
ED.sends = {
	to: toTrack("rt"),
	geo(W, H) { return { yt: 26, yb: H - 18, col: i => W * (i ? .7 : .3) }; },
	draw(g, W, H) {
		const r = rtOf(), G = this.geo(W, H), ink = cssv("--ink"); grid(g, W, H); g.font = "10px Silkscreen, ui-monospace, monospace";
		[["DEL", r.DEL], ["REV", r.REV]].forEach(([k, v], i) => {
			const cx = G.col(i), w = Math.min(22, W / 6), y = G.yb - (v ?? 0) / 127 * (G.yb - G.yt);
			g.fillStyle = inkA(.18); g.fillRect(cx - w / 2, G.yt, w, G.yb - G.yt); g.fillStyle = ink; g.fillRect(cx - w / 2, y, w, G.yb - y);
			g.fillText(k, cx - g.measureText(k).width / 2, H - 5);
		});
		label(g, "sends");
	},
	handles(W, H) {
		const r = rtOf(), G = this.geo(W, H), vy = y => clamp(Math.round((G.yb - y) / (G.yb - G.yt) * 127));
		return ["DEL", "REV"].map((k, i) => k in r && { x: G.col(i), y: G.yb - r[k] / 127 * (G.yb - G.yt), k, c: cssv("--ink"), drag: (x, y) => ({ [k]: vy(y) }) }).filter(Boolean);
	}
};
/* RETRIG: the trig, then RTRG more hits RTIM apart, fading. Dots = RTIM (the second hit) and RTRG (the last). */
ED.rtrg = {
	to: toTrack("syn"),
	geo(W, H) { const s = V.tracks[S.sel].syn, span = (W - 20) / 3; return { x0: 10, span, sp: 6 + (s.RTIM ?? 0) / 127 * span, n: s.RTRG ?? 0, yt: 24, yb: H - 10 }; },
	draw(g, W, H) {
		const G = this.geo(W, H); grid(g, W, H);
		for (let i = 0; i <= G.n; i++) {
			const x = G.x0 + i * G.sp; if (x > W - 4) break; const a = 1 - .7 * i / Math.max(1, G.n), top = G.yt + (1 - a) * (G.yb - G.yt);
			g.fillStyle = i ? inkA(.3 + .6 * a) : cssv("--ink"); g.fillRect(Math.round(x) - 1.5, top, 3, G.yb - top);
		}
		label(g, G.n ? "retrig ×" + G.n : "no retrig");
	},
	handles(W, H) {
		const G = this.geo(W, H);
		return [{ x: G.x0 + G.sp, y: G.yt + 4, k: "RTIM", c: cssv("--ink"), drag: x => ({ RTIM: clamp(Math.round((x - G.x0 - 6) / G.span * 127)) }) },
		{ x: Math.min(W - 6, G.x0 + G.n * G.sp), y: G.yb - 6, k: "RTRG", c: cssv("--ink"), drag: x => ({ RTRG: clamp(Math.round((x - G.x0) / G.sp)) }) }];
	}
};
(() => {	/* the synthesis screens and their painting helpers: only ED's entries leave this scope */
/* ===== The synthesis groups' screens (SYN_TAB): one editor per kind of picture, for every machine
   whose group has its knobs (the canvas's data-k, the page's names). An editor is a model: from the
   knobs' values (P, with one value tried in place: P.with) it gives what to paint and its dots, each
   a knob ("x", "y") or two ([nx, ny]) at a point of the picture. A dot's drag tries the knob's 128
   values and keeps the one whose point is nearest the pointer, so every dot stays on its picture. ===== */
const synOf = () => V.tracks[S.sel].syn;
/* a knob's value by its plain name (the page may call it SYN·NAME), and the name it is sent by */
const synQ = n => ("SYN·" + n) in synOf() ? "SYN·" + n : n;
const synV = n => synOf()[synQ(n)];
function synEd(model) {
	const run = (c, W, H, over) => {
		const ks = (c.dataset.k || "").split(" ").filter(Boolean), P = n => over && n in over ? over[n] : synV(n) ?? 64;
		const role = (...l) => l.find(n => ks.includes(n)) || null;
		const G = { W, H, X: u => 8 + u * (W - 16), Y: v => H - 10 - v * (H - 34), mid: H / 2 + 7, amp: H / 2 - 19 };
		return model(P, role, G, ks);
	};
	const pick = (c, W, H, key, part, n, px, py) => {
		let best = synV(n) ?? 0, bd = Infinity;
		for (let v = 0; v < 128; v++) { const h = run(c, W, H, { [n]: v }).h.find(e => String(e[0]) === key); if (!h) continue; const d = part === "x" ? Math.abs(h[1] - px) : Math.abs(h[2] - py); if (d < bd) { bd = d; best = v; } }
		return best;
	};
	return {
		to: toTrack("syn"),
		draw(g, W, H, c) { grid(g, W, H); const m = run(c, W, H); m.paint(g); if (m.label) label(g, m.label); },
		handles(W, H, c) {
			return run(c, W, H).h.map(([n, x, y, ax]) => ({ x, y, k: [].concat(n).join(" · "), c: cssv("--ink"),
				drag: (px, py) => Array.isArray(n) ? { [synQ(n[0])]: pick(c, W, H, String(n), "x", n[0], px, py), [synQ(n[1])]: pick(c, W, H, String(n), "y", n[1], px, py) } : { [synQ(n)]: pick(c, W, H, String(n), ax, n, px, py) } }));
		}
	};
}
/* painting helpers: a curve over u = 0..1, a dashed level, noise that is the same on every draw */
const inkL = () => cssv("--ink");
function curveU(g, G, f, c, w, dash) { line(g, G.W, x => f(clamp((x - 8) / (G.W - 16), 0, 1)), c, w, dash); }
function hLine(g, y, W, a = .35) { g.strokeStyle = inkA(a); g.lineWidth = 1; g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, Math.round(y) + .5); g.lineTo(W, Math.round(y) + .5); g.stroke(); g.setLineDash([]); }
const rnd = i => { const s = Math.sin(i * 12.9898 + 78.233) * 43758.5453; return (s - Math.floor(s)) * 2 - 1; };
/* a knob without a place in the picture: a dot on a short rail at the screen's foot, its name beside */
function rail(G, i, n, P) { const y = G.H - 10 - i * 14, x0 = G.W * .62, x1 = G.W - 10; return { n, x0, x1, y, x: x0 + P(n) / 127 * (x1 - x0) }; }
function paintRails(g, rails) {
	g.font = "9px Silkscreen, ui-monospace, monospace";
	if (!rails.length) return;
	const lw = Math.max(...rails.map(r => g.measureText(r.n).width)), top = Math.min(...rails.map(r => r.y)) - 8;
	g.fillStyle = cssv("--lcd"); g.globalAlpha = .85; g.fillRect(rails[0].x0 - lw - 14, top, rails[0].x1 - rails[0].x0 + lw + 22, rails[0].y + 7 - top); g.globalAlpha = 1;
	rails.forEach(r => { g.fillStyle = inkA(.25); g.fillRect(r.x0, r.y - 1, r.x1 - r.x0, 2); g.fillStyle = inkA(.7); g.fillText(r.n, r.x0 - g.measureText(r.n).width - 8, r.y + 3); });
}
/* PITCH: the pitch after a trig: PTCH the note (dashed), RAMP or BUMP above it falling back at RDEC or
   BENV, BEND up or down into it. */
ED.pitch = synEd((P, role, G) => {
	const p = role("PTCH"), a = role("RAMP", "BUMP", "BEND"), t = role("RDEC", "BENV");
	const b = p ? .12 + .5 * P(p) / 127 : .3, amt = !a ? 0 : a === "BEND" ? (P(a) - 64) / 64 * .3 : P(a) / 127 * .38, tau = t ? .01 + P(t) / 127 * .3 : a === "BEND" ? .12 : .05;
	const f = u => b + amt * Math.exp(-u / tau), h = [];
	if (p) h.push([p, G.X(1) - 4, G.Y(b), "y"]);
	if (a) h.push([a, G.X(0) + 2, G.Y(f(0)), "y"]);
	if (t) h.push([t, G.X(tau), G.Y(b + amt / Math.E), "x"]);
	return { h, label: a === "BEND" ? "pitch bend" : a ? "pitch " + a.toLowerCase() : "pitch", paint(g) { hLine(g, G.Y(b), G.W); curveU(g, G, u => G.Y(f(u)), inkL(), 2.2); } };
});
/* ENVELOPE: a level after a trig (or the input's gate): ATT rises, HOLD keeps it, DEC lets it fall;
   DAMP shortens the tail, STOP CLOS GRAB cut it (127 = never), CLIC adds a click, STRT skips the start
   (dashed: what is skipped), REV plays the shake backwards. AVOL FDPH set its height. */
ED.env = synEd((P, role, G) => {
	const at = role("ATT", "ATCK", "FATK"), ho = role("HOLD", "SUS", "HLD", "AHLD", "FHLD"), de = role("DEC", "ADEC", "FDEC", "REL"), lv = role("AVOL", "FDPH");
	const cu = role("STOP", "CLOS", "GRAB"), dm = role("DAMP"), ck = role("CLIC"), st = role("STRT", "START"), rv = role("REV");
	const a = at ? .004 + P(at) / 127 * .25 : .008, hd = ho ? P(ho) / 127 * .4 : 0, k = (.03 + (de ? P(de) : 64) / 127 * .35) * (dm ? 1 - .7 * P(dm) / 127 : 1);
	const L = (lv ? P(lv) / 127 : 1) * (ck ? .78 : 1), cut = cu && P(cu) < 127 ? .04 + P(cu) / 127 * .9 : 2, s = st ? P(st) / 127 * .35 : 0;
	const lvl0 = u => u < 0 ? 0 : u < a ? u / a : u < a + hd ? 1 : Math.exp(-(u - a - hd) / k), lvl = u => (u > cut ? 0 : lvl0(u + s)) * L;
	const h = [], rails = [];
	if (at) h.push([at, G.X(a), G.Y(L), "x"]);
	if (ho) h.push([ho, G.X(a + hd), G.Y(L), "x"]);
	if (de) h.push([de, G.X(clamp(a + hd + k * Math.log(4) - s, 0, 1)), G.Y(L / 4), "x"]);
	if (lv) h.push([lv, G.X(a + hd * .5) + (ho ? 0 : 10), G.Y(L), "y"]);
	if (cu) h.push([cu, Math.min(G.X(cut), G.W - 6), G.Y(0) - 4, "x"]);
	if (ck) h.push([ck, G.X(0) + 2, G.Y(L + P(ck) / 127 * .22), "y"]);
	if (st) h.push([st, G.X(s), G.Y(lvl0(s) * L), "x"]);
	if (dm) rails.push(rail(G, 0, dm, P));
	if (rv) rails.push(rail(G, rails.length, rv, P));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	const parts = [st && "start", at && "attack", ho && "hold", de === "REL" ? "release" : "decay", cu && cu.toLowerCase()].filter(Boolean);
	return { h, label: parts.join(" · "), paint(g) {
		if (s) curveU(g, G, u => G.Y(lvl0(u) * L), inkA(.35), 1.2, [3, 3]);
		if (rv) curveU(g, G, u => G.Y(lvl(1 - u) * P(rv) / 127), inkA(.4), 1.2, [2, 3]);
		curveU(g, G, u => G.Y(lvl(u)), inkL(), 2.2);
		if (ck) { g.fillStyle = inkL(); g.fillRect(G.X(0), G.Y(L + P(ck) / 127 * .22), 3, G.Y(0) - G.Y(L + P(ck) / 127 * .22)); }
		paintRails(g, rails);
	} };
});
/* ATTACK: the first moments of the hit: a click (STRT TICK CLIC SNAP) SPLEN long, a noise burst (NOIS),
   a second attack (DUAL) over the body's first cycles (faint). */
ED.atk = synEd((P, role, G) => {
	const ck = role("STRT", "TICK", "CLIC", "SNAP"), nz = role("NOIS"), du = role("DUAL"), ln = role("SPLEN");
	const c = ck ? P(ck) / 127 : 0, w = .012 + (ln ? P(ln) / 127 * .12 : .025), z = nz ? P(nz) / 127 : 0, d = du ? P(du) / 127 : 0;
	const body = u => .5 * Math.exp(-u / .7) * Math.sin(2 * Math.PI * 6 * u), clk = u => u < w ? c * (1 - u / w) : 0, ne = u => z * Math.exp(-u / .08), dl = u => u > .09 && u < .09 + w ? d * .8 * (1 - (u - .09) / w) : 0;
	const h = [];
	if (ck) h.push([ck, G.X(0) + 2, G.mid - G.amp * c, "y"]);
	if (ln) h.push([ln, G.X(w), G.mid, "x"]);
	if (nz) h.push([nz, G.X(.14), G.mid - G.amp * ne(.14), "y"]);
	if (du) h.push([du, G.X(.09) + 2, G.mid - G.amp * d * .8, "y"]);
	return { h, label: "attack", paint(g) {
		curveU(g, G, u => G.mid - G.amp * body(u), inkA(.35), 1.2);
		if (nz) curveU(g, G, u => G.mid - G.amp * ne(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * Math.max(-1, Math.min(1, body(u) + clk(u) + dl(u) + ne(u) * rnd(Math.round(u * 400)))), inkL(), 1.8);
	} };
});
/* WAVE: two cycles through the tone stage: HARM TONE add harmonics, CLIP DIST drive it, DTYP from soft
   to hard, DIRT reduces its bits (dashed = clean). */
ED.wave = synEd((P, role, G) => {
	const hm = role("HARM", "TONE"), cl = role("CLIP", "DIST"), ht = role("DTYP"), dr = role("DIRT");
	const H_ = hm ? P(hm) / 127 : 0, gain = 1 + (cl ? P(cl) / 127 : 0) * 7, hard = ht ? P(ht) / 127 : 0, bits = dr ? 2 + (1 - P(dr) / 127) * 30 : 0;
	const src = u => (Math.sin(2 * Math.PI * 2 * u) + H_ * .45 * Math.sin(2 * Math.PI * 6 * u)) / (1 + H_ * .3);
	const shp = x => { const s = Math.tanh(gain * x) / Math.tanh(gain), q = clamp(gain * x, -1, 1); let y = (1 - hard) * s + hard * q; if (bits) y = Math.round(y * bits) / bits; return y; };
	const rails = [], h = [];
	if (cl) h.push([cl, G.X(.06), G.mid - G.amp * shp(src(.06)), "y"]);
	if (hm) h.push([hm, G.X(.208), G.mid - G.amp * shp(src(.208)), "y"]);
	if (ht) rails.push(rail(G, 0, ht, P));
	if (dr) rails.push(rail(G, rails.length, dr, P));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: dr ? "bits · drive" : cl && !hm ? "drive" : "tone", paint(g) {
		curveU(g, G, u => G.mid - G.amp * src(u), inkA(.35), 1.2, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * shp(src(u)), inkL(), 2.2); paintRails(g, rails);
	} };
});
/* CLAPS: the hands one after another: CLPY CLPS how many, RATE how far apart, HARD how sharp, CDEC the
   last one's tail. */
function clapsOf(P, cnt, rate, hard, cdec) {
	const n = 1 + Math.round((cnt ? P(cnt) : 64) / 127 * 7), sp = .025 + (rate ? P(rate) / 127 : .4) * .1, hd = hard ? P(hard) / 127 : .5, dk = .02 + (cdec ? P(cdec) / 127 : .4) * .2;
	const env = u => { let e = 0; for (let i = 0; i < n; i++) { const d = u - i * sp; if (d < 0) break; const last = i === n - 1; e = Math.max(e, (last ? 1 : .55 + .35 * hd) * Math.exp(-d / (last ? dk : .006 + .012 * (1 - hd)))); } return e; };
	return { n, sp, hd, dk, env, end: (n - 1) * sp };
}
ED.claps = synEd((P, role, G) => {
	const cnt = role("CLPY", "CLPS"), rate = role("RATE"), hard = role("HARD"), cdec = role("CDEC"), C = clapsOf(P, cnt, rate, hard, cdec), h = [];
	if (cnt) h.push([cnt, G.X(C.end), G.mid - G.amp, "x"]);
	if (rate && C.n > 1) h.push([rate, G.X(C.sp), G.mid - G.amp * (.55 + .35 * C.hd), "x"]);
	if (hard) h.push([hard, G.X(0) + 2, G.mid - G.amp * (C.n > 1 ? .55 + .35 * C.hd : 1), "y"]);
	if (cdec) h.push([cdec, G.X(C.end + C.dk), G.mid - G.amp / Math.E, "x"]);
	return { h, label: `${C.n} ${C.n > 1 ? "claps" : "clap"}`, paint(g) {
		curveU(g, G, u => G.mid - G.amp * C.env(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * C.env(u) * rnd(Math.round(u * 500)), inkL(), 1.6);
	} };
});
/* ROOM: the claps (faint), then the room: ROOM how loud, RSIZ how long, RTUN its tone (smoother lower). */
ED.room = synEd((P, role, G) => {
	const C = clapsOf(P, "CLPY", "RATE", "HARD"), R = P("ROOM") / 127 * .8, len = .06 + P("RSIZ") / 127 * .5, sm = 1 + Math.round((1 - P("RTUN") / 127) * 6), t0 = C.end + .02;
	const tail = u => u < t0 ? 0 : R * Math.exp(-(u - t0) / len) * Math.min(1, (u - t0) / .03);
	const nz = u => { let s = 0; const i = Math.round(u * 500); for (let k = 0; k < sm; k++) s += rnd(i - k); return s / Math.sqrt(sm); };
	const r = rail(G, 0, "RTUN", P);
	return { h: [["ROOM", G.X(t0 + .05), G.mid - G.amp * tail(t0 + .05), "y"], ["RSIZ", G.X(t0 + .03 + len), G.mid - G.amp * tail(t0 + .03 + len), "x"], ["RTUN", r.x, r.y, "x"]], label: "room", paint(g) {
		curveU(g, G, u => G.mid - G.amp * C.env(u) * rnd(Math.round(u * 500)), inkA(.35), 1.2);
		curveU(g, G, u => G.mid - G.amp * tail(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * clamp(tail(u) * nz(u), -1, 1), inkL(), 1.6); paintRails(g, [r]);
	} };
});
/* SPECTRUM: the sound's tone, low to high: TONE TTUN where its colour sits, RICH TOP ENH HARD how much
   of it; BR the lows, AU less highs, AG more highs. */
ED.spec = synEd((P, role, G) => {
	const ce = role("TONE", "TTUN"), lv = role("RICH", "TOP", "ENH", "HARD"), br = role("BR"), au = role("AU"), ag = role("AG");
	const c = ce ? .15 + P(ce) / 127 * .75 : .5, L = lv ? P(lv) / 127 : 0, sg = x => 1 / (1 + Math.exp(-x));
	const f = u => .45 - .2 * u + (ce || lv ? L * .4 * Math.exp(-Math.pow((u - c) / .1, 2)) : 0) + (br ? P(br) / 127 * .3 * sg((.25 - u) * 18) : 0) - (au ? P(au) / 127 * .3 * sg((u - .6) * 14) : 0) + (ag ? P(ag) / 127 * .3 * sg((u - .8) * 22) : 0);
	const h = [];
	if (ce && lv) h.push([[ce, lv], G.X(c), G.Y(f(c)), "xy"]);
	if (br) h.push([br, G.X(.06), G.Y(f(.06)), "y"]);
	if (au) h.push([au, G.X(.72), G.Y(f(.72)), "y"]);
	if (ag) h.push([ag, G.X(.97), G.Y(f(.97)), "y"]);
	return { h, label: br ? "low · high" : "tone", paint(g) {
		g.fillStyle = inkA(.12); g.beginPath(); g.moveTo(0, G.H); for (let x = 0; x <= G.W; x++) g.lineTo(x, G.Y(f(clamp((x - 8) / (G.W - 16), 0, 1)))); g.lineTo(G.W, G.H); g.fill();
		curveU(g, G, u => G.Y(f(u)), inkL(), 2.2);
	} };
});
/* METAL: the partials of a metal or a shell, low to high: PTCH moves them, GAP TUNE spread them, SIZE
   makes the body bigger (lower), MTAL RICH CLSN HARD lift the upper ones, RING PEAK add the highest. */
ED.metal = synEd((P, role, G) => {
	const pt = role("PTCH"), sp = role("GAP", "TUNE"), sz = role("SIZE"), a1 = role("MTAL", "RICH", "CLSN", "HARD"), a2 = role("RING", "PEAK");
	const R = [1, 1.47, 1.93, 2.41, 2.88, 3.36, 3.83, 4.3, 4.9, 5.4, 5.9, 6.5];
	const f0 = (.05 + (pt ? P(pt) / 127 : .3) * .2) * (sz ? 1.4 - P(sz) / 127 * .8 : 1), spr = .45 + (sp ? P(sp) / 127 : .4) * .7;
	const up = a1 ? P(a1) / 127 : .5, hi = a2 ? P(a2) / 127 : 0;
	const parts = R.map((r, k) => ({ u: f0 * (1 + (r - 1) * spr), v: k === 0 ? .85 : k < 8 ? (.2 + .65 * up) * (1 - k / 11) * (.75 + .25 * Math.abs(rnd(k))) : hi * (.8 - (k - 8) * .12) })).filter(p => p.u <= 1);
	const at = k => parts[Math.min(k, parts.length - 1)], h = [];
	if (pt) h.push([pt, G.X(at(0).u), G.Y(at(0).v), "x"]);
	if (sz) h.push([sz, G.X(at(0).u), G.Y(at(0).v), "x"]);
	if (sp) h.push([sp, G.X(at(3).u), G.Y(at(3).v), "x"]);
	if (a1) h.push([a1, G.X(at(2).u), G.Y(at(2).v), "y"]);
	if (a2 && parts.length > 8) h.push([a2, G.X(parts[8].u), G.Y(parts[8].v), "y"]);
	else if (a2) h.push([a2, G.X(.98), G.Y(hi * .8), "y"]);
	return { h, label: "partials", paint(g) { g.fillStyle = inkL(); parts.forEach(p => { const x = G.X(p.u), y = G.Y(p.v); g.fillRect(Math.round(x) - 1.5, y, 3, G.Y(0) - y); }); g.fillStyle = inkA(.4); g.fillRect(0, G.Y(0), G.W, 1.5); } };
});
/* OSC: the drum's body after a trig: PTCH how fast it swings, DEC how long (dashed), DAMP shortens it,
   TUNE beats against a second skin, RING ENH add overtones, SNAP a snap at the start. */
ED.osc = synEd((P, role, G) => {
	const pt = role("PTCH"), de = role("DEC"), dm = role("DAMP"), tu = role("TUNE"), rg = role("RING", "ENH"), sn = role("SNAP");
	const cyc = 2 + (pt ? P(pt) : 64) / 127 * 14, k = (.04 + (de ? P(de) : 64) / 127 * .55) * (dm ? 1 - .65 * P(dm) / 127 : 1);
	const tn = tu ? P(tu) / 127 : 0, rr = rg ? P(rg) / 127 : 0, s = sn ? P(sn) / 127 : 0, e = u => Math.exp(-u / k);
	const w = u => e(u) * ((Math.sin(2 * Math.PI * cyc * u) * (1 - .3 * tn) + .3 * tn * Math.sin(2 * Math.PI * cyc * (1 + .08 * tn) * u) + rr * .35 * Math.sin(2 * Math.PI * cyc * 2.7 * u)) / (1 + rr * .35)) + (u < .02 ? s * .6 * (1 - u / .02) : 0);
	const h = [], rails = [], q = 1 / (4 * cyc);
	if (pt) h.push([pt, G.X(q), G.mid - G.amp * w(q), "x"]);
	if (de) h.push([de, G.X(Math.min(1, k * Math.log(4))), G.mid - G.amp * .25, "x"]);
	if (sn) h.push([sn, G.X(0) + 2, G.mid - G.amp * clamp(w(0), -1, 1), "y"]);
	[dm, tu, rg].filter(Boolean).forEach(n => rails.push(rail(G, rails.length, n, P)));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: "body", paint(g) {
		curveU(g, G, u => G.mid - G.amp * e(u), inkA(.45), 1, [3, 3]); curveU(g, G, u => G.mid + G.amp * e(u), inkA(.45), 1, [3, 3]);
		curveU(g, G, u => G.mid - G.amp * clamp(w(u), -1, 1), inkL(), 1.8); paintRails(g, rails);
	} };
});
/* FM: the carrier after a trig, its tone moved by the modulator: MOD SMOD how much (dashed: the
   modulation index), MDEC how long, MFRQ its frequency, MFB FB its feedback; SNAR SPTC SDEC the snare
   voice's level, pitch and decay. The carrier's pitch and decay are the track's PTCH and DEC. */
ED.fm = synEd((P, role, G) => {
	const md = role("MOD", "SMOD"), mf = role("MFRQ"), mk = role("MDEC"), fb = role("MFB", "FB"), lv = role("SNAR"), pt = role("SPTC", "PTCH"), de = role("SDEC", "DEC"), hp = role("HPF");
	const cyc = 2 + (pt ? P(pt) : synV("PTCH") ?? 64) / 127 * 12, k = .05 + (de ? P(de) : synV("DEC") ?? 64) / 127 * .55, L = lv ? .15 + P(lv) / 127 * .85 : 1;
	const I = u => (md ? P(md) : 64) / 127 * 5 * Math.exp(-u / (mk ? .02 + P(mk) / 127 * .5 : .15)), ratio = .5 + (mf ? P(mf) : 40) / 127 * 7.5, F = fb ? P(fb) / 127 * 1.4 : 0;
	const N = 600, ys = []; let prev = 0;
	for (let i = 0; i <= N; i++) { const u = i / N, y = L * Math.exp(-u / k) * Math.sin(2 * Math.PI * cyc * u + I(u) * Math.sin(2 * Math.PI * cyc * ratio * u) + F * prev); ys.push(y); prev = y; }
	const yI = u => G.Y(.55 + .45 * Math.min(1, I(u) / 5)), h = [], rails = [];
	if (md) h.push([md, G.X(0) + 2, yI(0), "y"]);
	if (mk) { const t = .02 + P(mk) / 127 * .5; h.push([mk, G.X(Math.min(1, t)), yI(t), "x"]); }
	if (lv) h.push([lv, G.X(.5), G.mid - G.amp * L * Math.exp(-.5 / k), "y"]);
	if (de) h.push([de, G.X(Math.min(1, k * Math.log(4))), G.mid + G.amp * L * .25, "x"]);
	[mf, fb, pt, hp].filter(Boolean).forEach(n => rails.push(rail(G, rails.length, n, P)));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: lv ? "snare voice · fm" : "fm", paint(g) {
		curveU(g, G, u => G.mid + G.amp * L * Math.exp(-u / k), inkA(.4), 1, [3, 3]);
		curveU(g, G, yI, inkA(.55), 1.2, [2, 3]);
		curveU(g, G, u => G.mid - G.amp * ys[Math.round(u * N)], inkL(), 1.6); paintRails(g, rails);
	} };
});
/* NOISE: a noise burst after a trig: NOISE RVOL SNAP how loud, NDEC RDEC how long, HPF thins it. */
ED.noise = synEd((P, role, G) => {
	const lv = role("NOISE", "RVOL", "SNAP"), de = role("NDEC", "RDEC"), hp = role("HPF");
	const L = lv ? P(lv) / 127 : .6, k = de ? .02 + P(de) / 127 * .4 : .07, sm = hp ? 1 + Math.round((1 - P(hp) / 127) * 5) : 3, e = u => L * Math.exp(-u / k);
	const nz = u => { let s = 0; const i = Math.round(u * 500); for (let j = 0; j < sm; j++) s += rnd(i - j); return s / Math.sqrt(sm); };
	const h = [], rails = [];
	if (lv) h.push([lv, G.X(0) + 2, G.mid - G.amp * L, "y"]);
	if (de) h.push([de, G.X(Math.min(1, k)), G.mid - G.amp * e(k), "x"]);
	if (hp) { rails.push(rail(G, 0, hp, P)); h.push([hp, rails[0].x, rails[0].y, "x"]); }
	return { h, label: "noise", paint(g) { curveU(g, G, u => G.mid - G.amp * e(u), inkA(.45), 1, [3, 3]); curveU(g, G, u => G.mid - G.amp * clamp(e(u) * nz(u), -1, 1), inkL(), 1.4); paintRails(g, rails); } };
});
/* STRIKE: the mallet's hit: HARD how hard (taller), HAMR a softer mallet (wider), TENS the skin's pitch
   rising under a hard hit (dashed), POS from the centre to the edge. */
ED.strike = synEd((P, role, G) => {
	const hd = role("HARD"), hm = role("HAMR"), te = role("TENS"), po = role("POS");
	const A = .3 + (hd ? P(hd) : 64) / 127 * .65, w = .02 + (hm ? P(hm) / 127 : .3) * .2, T = te ? P(te) / 127 : 0, ps = po ? P(po) / 127 : 0;
	const pulse = u => u < w ? A * Math.sin(Math.PI * u / w) : 0, ring = u => u < w ? 0 : A * .35 * Math.exp(-(u - w) / .25) * Math.sin(2 * Math.PI * (5 + 6 * ps) * (u - w)) * (1 - .5 * ps) + A * .15 * ps * Math.exp(-(u - w) / .25) * Math.sin(2 * Math.PI * 17 * (u - w));
	const pit = u => .62 + T * .3 * Math.exp(-u / .08), h = [], rails = [];
	if (hd) h.push([hd, G.X(w / 2), G.Y(A), "y"]);
	if (hm) h.push([hm, G.X(w), G.Y(.2), "x"]);
	if (te) h.push([te, G.X(0) + 2, G.Y(pit(0)), "y"]);
	if (po) { rails.push(rail(G, 0, po, P)); h.push([po, rails[0].x, rails[0].y, "x"]); }
	return { h, label: "strike", paint(g) {
		if (te) curveU(g, G, u => G.Y(pit(u)), inkA(.55), 1.2, [3, 3]);
		curveU(g, G, u => G.Y(.2 + pulse(u) * .78 + ring(u) * .5), inkL(), 2); hLine(g, G.Y(.2), G.W, .25); paintRails(g, rails);
	} };
});
/* GRAINS: the maraca's grains or rattle: GRNS RATL how many, DAMP fewer, GLEN how long each, SIZE how
   long the shake, HARD how loud each, RTYP the kind of rattle. */
ED.grains = synEd((P, role, G) => {
	const cn = role("GRNS", "RATL"), dm = role("DAMP"), gl = role("GLEN"), sz = role("SIZE"), hd = role("HARD"), ty = role("RTYP");
	const n = Math.round(6 + (cn ? P(cn) : 64) / 127 * 60 * (dm ? 1 - .75 * P(dm) / 127 : 1)), span = .2 + (sz ? P(sz) / 127 : .6) * .78, lw = 1 + (gl ? P(gl) / 127 : .3) * 5;
	const A = .3 + (hd ? P(hd) : 64) / 127 * .65, seed = ty ? Math.round(P(ty) / 16) * 31 : 0;
	const gr = Array.from({ length: n }, (_, i) => { const u = (i + .5 + rnd(i + seed) * .45) / n * span; return { u, v: A * Math.exp(-u / span * 1.4) * (.55 + .45 * Math.abs(rnd(i * 7 + seed))) }; });
	const h = [], rails = [];
	if (sz) h.push([sz, G.X(span), G.Y(0) - 2, "x"]);
	if (hd) h.push([hd, G.X(gr[0].u), G.Y(gr[0].v), "y"]);
	[cn, dm, gl, ty].filter(Boolean).forEach(x => rails.push(rail(G, rails.length, x, P)));
	rails.forEach(r => h.push([r.n, r.x, r.y, "x"]));
	return { h, label: `${n} grains`, paint(g) { g.fillStyle = inkL(); gr.forEach(p => { const y = G.Y(p.v); g.fillRect(Math.round(G.X(p.u)), y, lw, G.Y(0) - y); }); g.fillStyle = inkA(.4); g.fillRect(0, G.Y(0), G.W, 1.5); paintRails(g, rails); } };
});
/* TREMOLO: the level the modulation leaves: TREM MOD how deep (the dot's height), TFRQ MFRQ how fast. */
ED.trem = synEd((P, role, G) => {
	const dp = role("TREM", "MOD"), fq = role("TFRQ", "MFRQ"), d = P(dp) / 127, cyc = 1 + P(fq) / 127 * 9;
	return { h: [[[fq, dp], G.X(P(fq) / 127), G.Y(1 - d) + 2, "xy"]], label: "tremolo", paint(g) { curveU(g, G, u => G.Y(1 - d * (.5 - .5 * Math.cos(2 * Math.PI * cyc * u))), inkL(), 2); hLine(g, G.Y(1 - d), G.W, .3); } };
});
/* RESPONSE: a filter, low to high: HP HPF FILTF from below, LPF LP FFRQ from above (FILTW the band's
   width), HPQ FQ the peak at its edge. */
ED.resp = synEd((P, role, G) => {
	const hpn = role("HP", "HPF", "FILTF"), lpn = role("LPF", "LP", "FFRQ"), bw = role("FILTW"), qn = role("HPQ", "FQ");
	const hp = hpn ? P(hpn) / 127 : 0, lp = bw ? Math.min(1, hp + P(bw) / 127) : lpn ? P(lpn) / 127 : 1, q = qn ? P(qn) / 127 : 0, qAt = hpn && !lpn ? hp : lp;
	const d = u => (u < hp ? -Math.pow((hp - u) * 7, 2) : 0) + (u > lp ? -Math.pow((u - lp) * 7, 2) : 0) + q * 1.6 * Math.exp(-Math.pow((u - qAt) * 26, 2));
	const yy = u => clamp(G.H / 2 + 4 - d(u) * (G.H / 4), 6, G.H - 6), h = [];
	if (hpn) h.push(qn && !lpn ? [[hpn, qn], G.X(hp), yy(hp), "xy"] : [hpn, G.X(hp), yy(hp), "x"]);
	if (lpn) h.push(qn ? [[lpn, qn], G.X(lp), yy(lp), "xy"] : [lpn, G.X(lp), yy(lp), "x"]);
	if (bw) h.push([bw, G.X(lp), yy(lp), "x"]);
	return { h, label: hpn && (lpn || bw) ? "band" : hpn ? "high pass" : "low pass", paint(g) { curveU(g, G, yy, inkL(), 2.2); } };
});
/* IMPULSE: UP and DOWN how long it stays positive and negative, UVAL and DVAL how far. */
ED.imp = synEd((P, role, G) => {
	const u0 = .05, up = .02 + P("UP") / 127 * .42, dn = .02 + P("DOWN") / 127 * .42, uv = P("UVAL") / 127, dv = P("DVAL") / 127;
	const f = u => u < u0 ? 0 : u < u0 + up ? uv : u < u0 + up + dn ? -dv : 0;
	return { h: [[["UP", "UVAL"], G.X(u0 + up), G.mid - G.amp * uv, "xy"], [["DOWN", "DVAL"], G.X(Math.min(1, u0 + up + dn)), G.mid + G.amp * dv, "xy"]], label: "impulse",
		paint(g) { hLine(g, G.mid, G.W, .3); curveU(g, G, u => G.mid - G.amp * f(u), inkL(), 2.2); } };
});
/* INPUT GATE: the input's level (faint) and what passes: above GATE (dashed), at VOL ALEV. */
ED.ingate = synEd((P, role, G) => {
	const vo = role("VOL", "ALEV"), ga = role("GATE"), th = P(ga) / 127, V_ = P(vo) / 127;
	const hits = [[.04, 1], [.3, .45], [.52, .8], [.78, .3]], e = u => Math.max(0, ...hits.map(([t, a]) => u < t ? 0 : a * Math.exp(-(u - t) / .07))), o = u => e(u) > th ? e(u) * V_ : 0;
	return { h: [[ga, G.X(.97), G.Y(th * .9), "y"], [vo, G.X(.04) + 3, G.Y(o(.041) * .9), "y"]], label: "input gate",
		paint(g) { curveU(g, G, u => G.Y(e(u) * .9), inkA(.35), 1.2); hLine(g, G.Y(th * .9), G.W, .5); curveU(g, G, u => G.Y(o(u) * .9), inkL(), 2); } };
});
const NOTE_N = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = v => NOTE_N[v % 12] + (Math.floor(v / 12) - 2);
/* CHORD: the notes a trig sends: NOTE (the keyboard at the foot), N2 and N3 semitones above it. */
ED.chord = synEd((P, role, G) => {
	const nt = P("NOTE"), top = 26, bot = G.H - 22, sy = s => bot - Math.min(24, s) / 24 * (bot - top), len = .15 + synV("LEN") / 127 * .8;
	const h = [["NOTE", G.X(nt / 127), G.H - 8, "x"], ["N2", G.X(.5), sy(P("N2")), "y"], ["N3", G.X(.7), sy(P("N3")), "y"]];
	return { h, label: [noteName(nt), P("N2") && "+" + P("N2"), P("N3") && "+" + P("N3")].filter(Boolean).join(" "), paint(g) {
		for (let s = 0; s <= 24; s++) { g.fillStyle = inkA([1, 3, 6, 8, 10].includes((nt + s) % 12) ? .1 : .04); g.fillRect(0, sy(s) - (bot - top) / 48, G.W, (bot - top) / 24); }
		g.fillStyle = inkL(); [0, P("N2"), P("N3")].forEach((s, i) => { if (i && !s) return; g.fillRect(G.X(.04), sy(s) - 3, G.X(len) - G.X(.04), 6); });
		for (let k = 0; k < 128; k++) { g.fillStyle = [1, 3, 6, 8, 10].includes(k % 12) ? inkA(.6) : inkA(.18); g.fillRect(G.X(k / 127), G.H - 12, Math.max(1, (G.W - 16) / 128 - .4), 8); }
	} };
});
/* NOTE: one note as a bar: LEN how long, VEL how loud (the corner). */
ED.note = synEd((P, role, G) => {
	const x1 = G.X(.04 + P("LEN") / 127 * .92), y = G.Y(P("VEL") / 127);
	return { h: [[["LEN", "VEL"], x1, y, "xy"]], label: "note", paint(g) { g.fillStyle = inkA(.22); g.fillRect(G.X(.04), y, x1 - G.X(.04), G.Y(0) - y); g.fillStyle = inkL(); g.fillRect(G.X(.04), y, x1 - G.X(.04), 2.5); g.fillRect(G.X(.04), G.Y(0), G.W - 16, 1.5); } };
});
/* BARS: one bar a knob (PB from the middle, as the wheel). */
ED.bars = synEd((P, role, G, ks) => {
	const bip = n => n === "PB", xs = ks.map((_, i) => G.X((i + .5) / ks.length)), bw = Math.min(26, (G.W - 16) / ks.length * .45);
	const top = n => bip(n) ? G.Y(.5 + (P(n) - 64) / 127) : G.Y(P(n) / 127), base = n => bip(n) ? G.Y(.5) : G.Y(0);
	return { h: ks.map((n, i) => [n, xs[i], top(n), "y"]), label: ks.length > 2 ? "controllers" : "cue sends", paint(g) {
		g.font = "10px Silkscreen, ui-monospace, monospace";
		ks.forEach((n, i) => { g.fillStyle = inkA(.15); g.fillRect(xs[i] - bw / 2, G.Y(1), bw, G.Y(0) - G.Y(1)); g.fillStyle = inkL(); const a = top(n), b = base(n); g.fillRect(xs[i] - bw / 2, Math.min(a, b), bw, Math.max(2, Math.abs(b - a))); });
	} };
});
/* LEVEL · BALANCE: an input in the stereo field: BAL sideways, LEV up. */
ED.lvbal = synEd((P, role, G, ks) => {
	const lv = ks.find(n => /LEV$/.test(n)), bl = ks.find(n => /BAL$/.test(n)), x = G.X(P(bl) / 127), y = G.Y(P(lv) / 127);
	return { h: [[[bl, lv], x, y, "xy"]], label: lv.startsWith("M") ? "main out" : "inputs a/b", paint(g) {
		hLine(g, G.Y(.5), G.W, .2); g.strokeStyle = inkA(.45); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(G.W / 2 + .5, G.Y(1)); g.lineTo(G.W / 2 + .5, G.Y(0)); g.stroke(); g.setLineDash([]);
		g.fillStyle = inkA(.22); g.fillRect(x - 7, y, 14, G.Y(0) - y); g.fillStyle = inkL(); g.fillRect(8, G.Y(0), G.W - 16, 1.5);
	} };
});
/* ECHO: the repeats: TIME apart, FB how much each keeps (the second tap). */
ED.taps = synEd((P, role, G) => {
	const t = .03 + P("TIME") / 127 * .3, fb = P("FB") / 127 * .95, taps = [];
	for (let k = 0; .04 + k * t <= 1 && k < 40; k++) { const a = .9 * Math.pow(fb, k); if (k && a < .02) break; taps.push([.04 + k * t, a]); }
	return { h: [[["TIME", "FB"], G.X(.04 + t), G.Y(.9 * fb), "xy"]], label: "echo", paint(g) { g.fillStyle = inkL(); taps.forEach(([u, a], i) => { const y = G.Y(a); g.fillStyle = i ? inkL() : inkA(.5); g.fillRect(Math.round(G.X(u)) - 2, y, 4, G.Y(0) - y); }); } };
});
/* REVERB: the dry hit (DVOL), PRED later the tail, DEC long, DAMP smoother. */
ED.verb = synEd((P, role, G) => {
	const dry = P("DVOL") / 127 * .9, s = .05 + P("PRED") / 127 * .3, k = .03 + P("DEC") / 127 * .45, sm = 1 + Math.round(P("DAMP") / 127 * 6), tail = u => u < s ? 0 : .6 * Math.exp(-(u - s) / k) * Math.min(1, (u - s) / .02);
	const nz = u => { let a = 0; const i = Math.round(u * 500); for (let j = 0; j < sm; j++) a += Math.abs(rnd(i - j)); return a / sm * 1.6; }, r = rail(G, 0, "DAMP", P);
	return { h: [["DVOL", G.X(.03), G.Y(dry), "y"], ["PRED", G.X(s), G.Y(.6), "x"], ["DEC", G.X(Math.min(1, s + k)), G.Y(tail(s + k)), "x"], ["DAMP", r.x, r.y, "x"]], label: "gate box", paint(g) {
		g.fillStyle = inkL(); g.fillRect(G.X(.03) - 2, G.Y(dry), 4, G.Y(0) - G.Y(dry));
		curveU(g, G, u => G.Y(tail(u)), inkA(.45), 1, [3, 3]); curveU(g, G, u => G.Y(Math.min(1, tail(u) * nz(u))), inkL(), 1.4); paintRails(g, [r]);
	} };
});
/* EQ (CTR-EQ): the master EQ's curve from this machine's knobs: the shelves at LF and HF, the peak at
   PF with its width PQ; the gains LG PG HG up to boost. */
ED.eq3 = synEd((P, role, G) => {
	const r = u => (P("LG") - 64) / 64 / (1 + Math.exp((u - P("LF") / 127) * 18)) + (P("HG") - 64) / 64 / (1 + Math.exp(-(u - P("HF") / 127) * 18)) + (P("PG") - 64) / 64 * Math.exp(-Math.pow((u - P("PF") / 127) * (4 + P("PQ") / 8), 2));
	const y = u => G.H / 2 + 4 - r(u) * (G.H / 3), rq = rail(G, 0, "PQ", P);
	return { h: [[["LF", "LG"], G.X(P("LF") / 127), y(P("LF") / 127), "xy"], [["PF", "PG"], G.X(P("PF") / 127), y(P("PF") / 127), "xy"], [["HF", "HG"], G.X(P("HF") / 127), y(P("HF") / 127), "xy"], ["PQ", rq.x, rq.y, "x"]],
		label: "master eq", paint(g) { hLine(g, G.H / 2 + 4, G.W, .3); curveU(g, G, y, inkL(), 2.2); paintRails(g, [rq]); } };
});
/* CURVE (CTR-DX): the compressor, input to output (dashed = unchanged): TRHD where it starts, RTIO how
   much it holds back, KNEE how softly. */
ED.comp = synEd((P, role, G) => {
	const th = P("TRHD") / 127, ra = 1 + P("RTIO") / 127 * 9, kn = Math.max(.001, P("KNEE") / 127 * .25);
	const o = i => i <= th - kn ? i : i >= th + kn ? th + (i - th) / ra : i + ((1 / ra - 1) * Math.pow(i - th + kn, 2)) / (4 * kn), rk = rail(G, 0, "KNEE", P);
	return { h: [["TRHD", G.X(th), G.Y(o(th)), "x"], ["RTIO", G.X(1) - 2, G.Y(o(1)), "y"], ["KNEE", rk.x, rk.y, "x"]], label: "in → out", paint(g) {
		g.strokeStyle = inkA(.45); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(G.X(0), G.Y(0)); g.lineTo(G.X(1), G.Y(1)); g.stroke(); g.setLineDash([]);
		curveU(g, G, u => G.Y(o(u)), inkL(), 2.2); paintRails(g, [rk]);
	} };
});
})();
function grid(g, W, H) { g.strokeStyle = inkA(0.13); g.lineWidth = 1; for (let i = 1; i < 4; i++) { g.beginPath(); g.moveTo(0, Math.round(H * i / 4) + .5); g.lineTo(W, Math.round(H * i / 4) + .5); g.stroke(); } for (let i = 1; i < 8; i++) { g.beginPath(); g.moveTo(Math.round(W * i / 8) + .5, 0); g.lineTo(Math.round(W * i / 8) + .5, H); g.stroke(); } }
function line(g, W, fy, c, w, dash) { g.strokeStyle = c; g.lineWidth = w; g.setLineDash(dash || []); g.beginPath(); for (let x = 0; x <= W; x += 1) { const y = fy(x); x ? g.lineTo(x, y) : g.moveTo(x, y); } g.stroke(); g.setLineDash([]); }
function label(g, t) { g.fillStyle = cssv("--ink"); g.font = "10px Silkscreen, ui-monospace, monospace"; g.fillText(t.toUpperCase(), 8, 14); }
let raf = 0;
/* P7: the editors and the playhead follow the window */
addEventListener("resize", () => { redraw(); alignLock(); if (V && V.playing) movePH(); });
function redraw() { if (raf) return; raf = requestAnimationFrame(() => { raf = 0; $$("canvas.ed").forEach(drawEd); $$("canvas.tw").forEach(drawTile); }); }
function drawEd(c) {
	const ed = ED[c.dataset.ed], dpr = devicePixelRatio || 1, W = c.clientWidth, H = c.clientHeight; if (!W || !ed) return;
	if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
	const g = c.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, W, H); ed.draw(g, W, H, c);
	ed.handles(W, H, c).forEach(h => {
		const a = Held.as("editor"), on = !!a && a.c === c && a.k === h.k; g.beginPath(); g.arc(h.x, clamp(h.y, 6, H - 6), on ? 7 : 5.5, 0, 7); g.fillStyle = cssv("--lcd"); g.fill(); g.lineWidth = 2.5; g.strokeStyle = h.c; g.stroke();
		if (on) { g.fillStyle = cssv("--ink"); g.font = "10px Silkscreen, monospace"; g.fillText(h.k, Math.min(W - 40, h.x + 10), Math.max(14, h.y - 10)); }
	});
}
function nearest(c, e) { const r = c.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top; let best = null, bd = 16; ED[c.dataset.ed].handles(r.width, r.height, c).forEach(h => { const d = Math.hypot(h.x - x, clamp(h.y, 6, r.height - 6) - y); if (d < bd) { bd = d; best = h; } }); return best; }
