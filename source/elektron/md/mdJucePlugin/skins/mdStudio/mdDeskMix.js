"use strict";
/* Mix: a strip a track (level, pan, drive, sends, output, mute and solo), the master effects with their
   editors. */

/* ===== Mix ===== */
function renderMix() {
	$("#main").innerHTML = `<div class="panel pad">
 <div class="scroll"><div class="strips">${V.tracks.map((t, i) => {
		const out = t.out || "MAIN", direct = out !== "MAIN"; return `<div class="strip ${i === S.sel ? "sel" : ""} ${direct ? "direct" : ""}" data-sel="${i}" style="${audible(i) ? "" : "opacity:.5"}">
  <div class="top2"><i class="led act" data-act="${i}"></i><span>${i + 1}</span></div>
  ${"VOL" in t.rt ? `<div class="fader" role="slider" tabindex="0" aria-label="Track ${i + 1} volume" data-g="rt" data-n="VOL" data-t="${i}"><div class="tr"><i></i></div><div class="cap2"></div></div>` : `<div class="fader"></div>`}
  <div class="v" data-show="${i}"></div>
  ${["PAN", "DIST", "DEL", "REV"].map(n => n in t.rt ? (direct && (n === "DEL" || n === "REV") ? pc("rt", n, { t: i }).replace('class="pc"', `class="pc mainonly" title="${n === "DEL" ? "Delay" : "Reverb"} sends only reach the main outputs; this track goes to OUT ${out}. The value is kept."`) : pc("rt", n, { t: i })) : `<div class="pc empty" aria-hidden="true"></div>`).join("")}
  <button class="outk ${direct ? "on" : ""}" data-out="${i}" title="${direct ? "Individual output " + out + ": skips the master effects" : "Main output, through the master effects"}">OUT ${out}</button>
  <div class="mrow"><button class="ms m" data-mute="${i}" aria-pressed="${t.mute}" aria-label="Mute track ${i + 1}">M</button><button class="ms s" data-solo="${i}" aria-pressed="${t.solo}" aria-label="Solo track ${i + 1}">S</button></div>
  <div class="nm" title="${t.name}">${t.m}</div></div>`;
	}).join("")}</div></div></div>
 <div class="flow" aria-label="Signal path">SENDS <i>→</i> RHYTHM ECHO <i>→</i> GATE BOX <i>→</i> MASTER EQ <i>→</i> DYNAMIX <i>→</i> MAIN OUT <span>tracks on A–F skip this chain</span></div>
 <div class="fx4">${Object.entries(V.mfx).map(([id, f]) => `<section class="card"><header><h3>${f.name}</h3><span>${{ echo: "DEL send", gate: "REV send + DVOL", eq: "main out", dyn: "main out" }[id]}</span></header>
  <canvas class="ed sm" data-ed="${id}" aria-label="${f.name} screen. Drag the dots."></canvas>
  <div class="ctl four">${f.k.map(n => pc("mfx", n, { f: id })).join("")}</div></section>`).join("")}</div>`;
	syncControls(); redraw();
}
/* The master effects' editors (their values: V.mfx; sendEditor sends them as masterFx) */
ED.eq = {
	to: toMfx("eq"),
	resp(u) { const v = V.mfx.eq.v; return (v.LG - 64) / 64 / (1 + Math.exp((u - v.LF / 127) * 18)) + (v.HG - 64) / 64 / (1 + Math.exp(-(u - v.HF / 127) * 18)) + (v.PG - 64) / 64 * Math.exp(-Math.pow((u - v.PF / 127) * (4 + v.PQ / 8), 2)); },
	draw(g, W, H) { grid(g, W, H); line(g, W, x => H / 2 - this.resp(x / W) * H / 3, cssv("--ink"), 2); label(g, "master EQ"); },
	handles(W, H) {
		const v = V.mfx.eq.v, yy = u => H / 2 - this.resp(u) * H / 3, gy = y => clamp(Math.round(64 + (H / 2 - y) / (H / 3) * 64));
		return [["LF", "LG"], ["PF", "PG"], ["HF", "HG"]].map(([fk, gk]) => ({ x: v[fk] / 127 * W, y: yy(v[fk] / 127), k: fk, c: cssv("--ink"), drag: (x, y) => ({ [fk]: clamp(Math.round(x / W * 127)), [gk]: gy(y) }) }));
	}
};
ED.dyn = {
	to: toMfx("dyn"),
	out(i) { const v = V.mfx.dyn.v, th = v.TRHD / 127, r = 1 + v.RTIO / 127 * 9, kn = Math.max(.001, v.KNEE / 127 * .25); return i <= th - kn ? i : i >= th + kn ? th + (i - th) / r : i + ((1 / r - 1) * Math.pow(i - th + kn, 2)) / (4 * kn); },
	draw(g, W, H) {
		grid(g, W, H); g.strokeStyle = inkA(0.45); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 6); g.lineTo(W, 6); g.stroke(); g.setLineDash([]);
		line(g, W, x => H - 6 - this.out(x / W) * (H - 12), cssv("--ink"), 2); label(g, "in → out");
	},
	handles(W, H) {
		const v = V.mfx.dyn.v, th = v.TRHD / 127; return [{ x: th * W, y: H - 6 - this.out(th) * (H - 12), k: "TRHD", c: cssv("--ink"), drag: (x) => ({ TRHD: clamp(Math.round(x / W * 127)) }) },
		{ x: W - 6, y: H - 6 - this.out(1) * (H - 12), k: "RTIO", c: cssv("--ink"), drag: (x, y) => { const o = (H - 6 - y) / (H - 12), th = v.TRHD / 127; const r = (1 - th) / Math.max(.01, o - th); return { RTIO: clamp(Math.round((r - 1) / 9 * 127)) }; } }];
	}
};
ED.echo = {
	to: toMfx("echo"),
	draw(g, W, H) {
		const v = V.mfx.echo.v, dt = Math.max(1, v.TIME) / 256, fb = v.FB / 64; grid(g, W, H);
		g.strokeStyle = inkA(.35); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(0, H - 8 - (H - 22)); g.lineTo(W, H - 8 - (H - 22)); g.stroke(); g.setLineDash([]);
		let a = 1, x = 0, k = 0; g.fillStyle = cssv("--ink"); while (x <= 1 && k < 40) { const h = Math.min(1.25, a) * (H - 22); g.fillRect(Math.round(x * W), H - 8 - h, k ? 4 : 6, h); x += dt; a *= fb; k++; if (a < .02) break; }
		label(g, fb > 1 ? "echo taps · feedback grows!" : "echo taps · 2 bars");
	},
	handles(W, H) { const v = V.mfx.echo.v, dt = Math.max(1, v.TIME) / 256, a2 = Math.min(1.25, v.FB / 64); return [{ x: dt * W + 2, y: H - 8 - a2 * (H - 22), k: "TIME · FB", c: cssv("--ink"), drag: (x, y) => ({ TIME: clamp(Math.round(x / W * 256)), FB: clamp(Math.round((H - 8 - y) / (H - 22) * 64)) }) }]; }
};
ED.gate = {
	to: toMfx("gate"),
	draw(g, W, H) {
		const v = V.mfx.gate.v, pre = v.PRED / 127 * .15, dec = .5 + v.DEC / 127 * 2.5, gate = v.GATE >= 127 ? 9 : v.GATE / 127 * 3, span = 3.3; grid(g, W, H);
		line(g, W, x => { const t = x / W * span; if (t < pre) return H - 8; if (t > pre + gate) return H - 8; return H - 8 - (H - 22) * Math.exp(-(t - pre) / (dec / 4)); }, cssv("--ink"), 2.2);
		if (v.GATE < 127) { const gx = (pre + gate) / span * W; g.strokeStyle = inkA(.6); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(gx, 6); g.lineTo(gx, H); g.stroke(); g.setLineDash([]); }
		label(g, v.GATE >= 127 ? "reverb · gate off" : "reverb · gated");
	},
	handles(W, H) {
		const v = V.mfx.gate.v, span = 3.3, pre = v.PRED / 127 * .15, dec = .5 + v.DEC / 127 * 2.5, gate = v.GATE >= 127 ? 3.2 : v.GATE / 127 * 3;
		return [{ x: pre / span * W + 6, y: H - 10, k: "PRED", c: cssv("--ink"), drag: x => ({ PRED: clamp(Math.round((x / W * span) / .15 * 127)) }) },
		{ x: (pre + dec / 4) / span * W, y: H - 8 - (H - 22) / Math.E, k: "DEC", c: cssv("--ink"), drag: x => ({ DEC: clamp(Math.round(((x / W * span - pre) * 4 - .5) / 2.5 * 127)) }) },
		{ x: Math.min(W - 6, (pre + gate) / span * W), y: 14, k: "GATE", c: cssv("--ink"), drag: x => { const t = x / W * span - pre; return { GATE: t >= 3.05 ? 127 : clamp(Math.round(t / 3 * 127)) }; } }];
	}
};

/* an output key (the router's, mdDeskRender.js CLICKS): true when the click was its */
function clickOut(e) {
	const ok = e.target.closest("[data-out]"); if (!ok) return false;
	const i = +ok.dataset.out, O = Enums().outputs, out = O[(O.indexOf(V.tracks[i].out || O[0]) + 1) % O.length]; if (!out) return true;
	cmd("route", { t: i, out }, undefined, [[["tracks", i, "out"], out]]); render(); return true;
}
