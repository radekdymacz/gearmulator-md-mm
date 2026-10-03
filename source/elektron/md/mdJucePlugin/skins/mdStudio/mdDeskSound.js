"use strict";
/* Sound: the selected track's sound in small groups (the tables: mdDeskSoundGroups.js), each with its screen
   (a canvas editor, mdDeskEditors.js) and its value boxes; the LFO and the track's relations; the MUTATE bar
   on top (mdDeskGenUi.js). pc() and the LFO shapes are the value box and the shape icons every page uses. */

/* ===== Sound ===== */
function pc(g, n, { t, f, color, unit } = {}) {
	if (!n) return `<div class="pc empty" aria-hidden="true"></div>`;
	return `<div class="pc" role="slider" tabindex="0" aria-label="${n}" aria-valuemin="0" aria-valuemax="127" data-g="${g}" data-n="${n}"${t != null ? ` data-t="${t}"` : ""}${f ? ` data-f="${f}"` : ""}${color ? ` style="--pc:${color}"` : ""}><span>${n}</span>${unit ? `<i class="pu" data-unit="${unit}"></i>` : ""}<b></b></div>`;
}
function shapeIcon(i, inv) { const pts = Array.from({ length: 25 }, (_, k) => { const x = k / 24; return [(x * 26 + 1).toFixed(1), (11 - 7 * shape(i, x, inv)).toFixed(1)]; }); return `<svg width="28" height="22" viewBox="0 0 28 22" aria-hidden="true"><polyline fill="none" stroke="currentColor" stroke-width="1.6" points="${pts.map(p => p.join(",")).join(" ")}"/></svg>`; }
const RND = [.35, -.7, .9, -.25, .55, -.9, .1, .7];
function shape(i, x, inv) { const v = [1 - 4 * Math.abs(x - .5), 2 * x - 1, x < .5 ? 1 : -1, 1 - 2 * x, 2 * Math.exp(-4 * x) - 1, RND[Math.floor(x * 8) % 8]][i] ?? 0; return inv ? -v : v; }
function machButton(tr) {
	const fk = famKey(tr.m, Cat), fam = FAMS.find(x => x[0] === fk) || ["", ""];
	return `<button class="machbtn" id="machbtn" aria-haspopup="dialog" aria-expanded="false" aria-label="Change machine"><span class="lcdtxt">${tr.m}</span><span class="mfam">${fam[0].replace("PI", "P-I")} · ${fam[1]}</span><svg viewBox="0 0 10 6" aria-hidden="true"><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5"/></svg></button>`;
}
const attr = s => String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
const sndPlot = (inner, tip, note) => `<div class="plot" title="${attr(tip)}">${inner}${note ? `<span class="plotnote">${note}</span>` : ""}</div>`;
/* a group's screen, or "": its canvas knows the group's knobs (data-k), so one editor serves every
   machine that has them */
function sndScreen(ed, tr, at, knobs = []) {
	if (!ed) return "";
	if (ed === "sample") return sndPlot(waveBox(`<canvas class="ed" data-ed="sample" aria-label="Sample with start and end markers. Drag the markers."></canvas>`, at), SND_TIP.sample, smpWhy(at));
	if (ed === "rec") return sndPlot(waveBox(`<canvas class="ed" data-ed="rec" aria-label="Recording window. Drag LEN."></canvas>`, at), SND_TIP.rec, smpWhy(at));
	const tip = SND_TIP[ed] || "Drag the dots.";
	return sndPlot(`<canvas class="ed" data-ed="${ed}" data-k="${knobs.join(" ")}" aria-label="${attr(tip)}"></canvas>`, tip);
}
/* The track's groups: { key, title, g, knobs, ed } per row. Synthesis: the machine's table (its knobs
   by their page names: SYN·DIST where the routing page has a DIST too), then any knob it leaves out. */
function sndGroups(tr) {
	const pg = pages(tr.m, Cat), have = names(pg.s), syn = [], fxrt = [], used = new Set();
	const qual = n => have.includes(n) ? n : have.includes("SYN·" + n) ? "SYN·" + n : null;
	for (const d of synTable(tr.m, Cat)) {
		const knobs = d.knobs.map(qual).filter(n => n && !used.has(n)); if (!knobs.length) continue;
		knobs.forEach(n => used.add(n));
		syn.push({ key: d.title.toLowerCase().replace(/[^a-z0-9]+/g, "-"), title: d.title, g: "syn", knobs, ed: d.ed, note: d.note });
	}
	const rest = have.filter(n => !used.has(n));
	if (rest.length) syn.push({ key: "syn", title: "Synthesis", g: "syn", knobs: rest, ed: null });
	const add = (list, key, title, g, n, ed) => { let x = list.find(a => a.key === key && a.g === g); if (!x) list.push(x = { key, title, g, knobs: [], ed }); x.knobs.push(n); };
	for (const [g, p] of [["fx", "e"], ["rt", "r"]]) for (const n of names(pg[p])) { if (g === "rt" && LFO_RT.includes(n)) continue; const [k, title, ed] = fxrtKey(n, g); add(fxrt, k, title, g, n, ed); }
	return [syn, fxrt];
}
const SND_COL = { fx: "var(--e12)", rt: "var(--gnd)", lfo: "var(--teal)" };
/* a group: its title on the rule, its screen (or what its knobs do), its boxes; its page's word only on
   the first group of that page in its row (tag false). Each part is a cell of the row's grid. */
function sgHtml(x, cols, color, tag, note) {
	const page = SND_TAG[x.g][1];
	const body = x.body || `<div class="ctl" style="grid-template-columns:repeat(${cols},minmax(0,1fr))">${x.knobs.map(n => pc(x.g, n, { color: SND_COL[x.g] || color })).join("")}</div>`;
	return `<section class="sg" data-sg="${x.key}"><header><h3>${mutTitle(x)}</h3>${tag ? `<span title="${attr(x.tagTip || "Its knobs are on the Machinedrum's " + page + " page")}">${SND_TAG[x.g][0]}</span>` : ""}</header>${x.plot || (note ? `<p class="sgnote">${note}</p>` : "")}${body}</section>`;
}
/* One row: a grid of columns over three lines (titles, screens, boxes). A group with a screen is a
   column of its own (as wide as its boxes, but never narrower than about two of them, a waveform
   wider); the ones without pair up in one column (a lone one says what its knobs do). A row without
   any screen is two lines, its groups side by side. */
function sgRow(list, cls, color) {
	if (!list.length) return "";
	let prev = ""; const first = x => prev !== x.g && (prev = x.g), n = x => x.n ?? x.knobs.length;
	const narrow = list.reduce((a, x) => a + n(x), 0) > 10 ? 1.8 : 2.6, w = x => x.w ?? (x.ed === "sample" ? 6 : x.ed === "rec" ? 5 : x.plot ? Math.max(n(x), narrow) : n(x));
	const tracks = cols => `grid-template-columns:${cols.map(c => `minmax(min-content,${c}fr)`).join(" ")}`;
	if (!list.some(x => x.plot)) {
		const ws = list.map(x => Math.max(1.4, w(x)));
		return `<div class="sgrow flat ${cls}" style="${tracks(list.about ? [...ws, 4] : ws)}">${list.map(x => `<div class="sgcol">${sgHtml(x, n(x), color, first(x))}</div>`).join("")}${list.about ? `<p class="sgabout">${list.about}</p>` : ""}</div>`;
	}
	const cols = [], stacks = [];
	for (const x of list) {
		if (x.plot) { cols.push([x]); continue; }
		const s = stacks[stacks.length - 1];
		if (s && s.length < 2) s.push(x); else { const c = [x]; stacks.push(c); cols.push(c); }
	}
	return `<div class="sgrow ${cls}" style="${tracks(cols.map(c => Math.max(...c.map(w))))}">${cols.map(c => {
		const cn = Math.max(...c.map(n)), kind = c[0].plot ? "" : c.length > 1 ? " stack" : " lone";
		return `<div class="sgcol${kind}">${c.map(x => sgHtml(x, cn, color, first(x), kind === " lone" ? x.note || about(V.tracks[S.sel].m, Cat) : "")).join("")}</div>`;
	}).join("")}</div>`;
}
function renderSound() {
	const t = S.sel, tr = V.tracks[t], l = tr.lfo, at = smpOfMachine(tr.m), color = FAMC[tr.fam];
	const relSel = (id, kind, word) => `<select id="${id}"><option value="">none</option>${V.tracks.map((x, i) => i !== t ? `<option value="${i}" ${tr[kind] === i ? "selected" : ""}>${word} ${i + 1} ${x.name}</option>` : "").join("")}</select>`;
	const [syn, fxrt] = sndGroups(tr);
	[...syn, ...fxrt].forEach(x => { x.plot = sndScreen(x.ed, tr, at, x.knobs.map(n => n.replace(/^SYN·/, ""))); });
	if (!syn.length) syn.push({ key: "syn", title: "Synthesis", g: "syn", knobs: [], n: 0, w: 8, body: `<p class="sgabout">${about(tr.m, Cat) || "This machine has no synthesis knobs."}</p>` });
	else if (!syn.some(x => x.plot)) syn.about = about(tr.m, Cat);
	const lfoTip = "SPD DEPTH SHMIX are LFOS LFOD LFOM on the Machinedrum's ROUTING page; the shapes, the target and UPDTE are its LFO page";
	const shapeRow = sl => `<div class="kv"><span class="mono">${sl}</span><div class="shapes">${SHAPES.map((n, i) => `<button data-slot="${sl}" data-shape="${i}" aria-pressed="${l[sl] === i}" title="${n}${sl === "SHP2" ? ", inverted" : ""}" aria-label="${sl} ${n}">${shapeIcon(i, sl === "SHP2")}</button>`).join("")}</div></div>`;
	const lfo = [
		{ key: "lshape", title: `LFO ${t + 1} shape`, g: "lfo", knobs: ["SHMIX"], w: 5, tagTip: lfoTip,
			plot: sndPlot(`<canvas class="ed" data-ed="lshape" aria-label="One cycle of the LFO's shape. Drag the dot for SHMIX."></canvas>`, SND_TIP.lshape),
			body: `<div class="sgline lfoshape">${shapeRow("SHP1")}${shapeRow("SHP2")}${pc("lfo", "SHMIX", { color: SND_COL.lfo })}</div>` },
		{ key: "lmotion", title: "LFO motion", g: "lfo", knobs: ["SPD", "DEPTH"], w: 3.4, tagTip: lfoTip,
			plot: sndPlot(`<canvas class="ed" data-ed="lmotion" aria-label="The LFO across one bar. Drag the dot for SPD and DEPTH."></canvas>`, SND_TIP.lmotion),
			body: `<div class="sgline">${["SPD", "DEPTH"].map(n => pc("lfo", n, { color: SND_COL.lfo })).join("")}<span class="seg" data-set="upd" title="UPDTE: FREE runs on, TRIG restarts it on every trig, HOLD keeps the value a trig takes">${["FREE", "TRIG", "HOLD"].map(u => `<button data-v="${u}" aria-pressed="${l.UPDTE === u}">${u}</button>`).join("")}</span></div>` },
		{ key: "ltarget", title: "LFO target", g: "lfo", knobs: [], w: 2, tagTip: lfoTip,
			body: `<div class="sgsel" title="The parameter this LFO moves: a track, then one of its parameters"><select id="lfoT">${V.tracks.map((x, i) => `<option value="${i}" ${i === l.TRCK ? "selected" : ""}>T${i + 1} ${x.name}</option>`).join("")}</select><select id="lfoP">${params(l.TRCK).map(p => `<option ${p === l.PARAM ? "selected" : ""}>${p}</option>`).join("")}</select></div>` },
		{ key: "rel", title: "Relations", g: "kit", knobs: [], w: 2, tagTip: "Kit relations: EDIT KIT → RELATE",
			body: `<div class="sgsel pair"><label title="Mute group: this track's trig mutes the chosen track, as open and closed hihats.">Mute${relSel("mg", "muteGroup", "mutes")}</label>
			<label title="Trig group: this track's trig also trigs the chosen track. Trig relations do not chain.">Trig${relSel("tg", "trigGroup", "trigs")}</label></div>` }];
	$("#main").innerHTML = `<div class="snd mutating">
  <div class="sndhead">${machButton(tr)}
   <div class="genband mutband" id="mutband" title="The groups follow the sound's path: synthesis, effects, routing, then the LFO and the track's relations. Drag a dot or a box; hold Alt to move the same knob on every track (Control All).">${mutStripHtml()}</div></div>
  ${sgRow(syn, "synrow", color)}
  ${sgRow(fxrt, "fxrow", color)}
  ${sgRow(lfo, "lforow", color)}
 </div>`;
	syncControls(); redraw();
}

/* the LFO's shape keys (the router's, mdDeskRender.js CLICKS): true when the click was theirs */
function clickShape(e) {
	const sh = e.target.closest("[data-shape]"); if (!sh) return false;
	sendLfo(S.sel, sh.dataset.slot, +sh.dataset.shape); $$(`[data-slot="${sh.dataset.slot}"]`).forEach(b => b.setAttribute("aria-pressed", b === sh)); redraw(); return true;
}
/* the LFO's target and the track's relations */
document.addEventListener("change", e => {
	const id = e.target.id, v = e.target.value; if (id !== "lfoT" && id !== "lfoP" && id !== "mg" && id !== "tg") return;
	const l = V.tracks[S.sel].lfo;
	if (id === "lfoT") { const p = params(+v).includes(l.PARAM) ? l.PARAM : params(+v)[0]; sendLfo(S.sel, "TRCK", +v); sendLfo(S.sel, "PARAM", p); renderSound(); enhanceSelects($("#main")); }
	if (id === "lfoP") sendLfo(S.sel, "PARAM", v);
	if (id === "mg") sendGroup(S.sel, "mute", v === "" ? null : +v);
	if (id === "tg") sendGroup(S.sel, "trig", v === "" ? null : +v);
});
