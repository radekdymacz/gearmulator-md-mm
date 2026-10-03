"use strict";
/* Control (hidden until the plug-in's learn document says enabled): the mapping matrix of controller knobs,
   learned CCs and the app modulators (mdDeskMod.js) against the tracks, and the selected row's inspector. */

/* ===== Control: the plug-in's MIDI Learn (jucePluginLib) as the backend ===== */
function toggleLearn() {
	S.ctl.learn = !S.ctl.learn; S.ctl.learnT = null; document.body.classList.toggle("learn", S.ctl.learn); renderTop(); syncControls();
	if (S.ctl.learn) toast("LEARN: click a value, then turn a knob on your MIDI controller."); else cmd("learnCancel");
}
/* App modulators: the page edits build a new Mods.doc (mdDeskMod.js) and it is sent whole; values
   and the CC rate come back. modInFlight is the id of a modSet not answered yet (the MM page's
   pattern): while it is set, an incoming "mod" message does not overwrite the pending edit. */
let modInFlight = 0;
function sendMods() { modInFlight = cmd("modSet", { doc: Mods.doc }, "modSet", undefined, r => { if (r.id === modInFlight) modInFlight = 0; }); }
function paramName(t, i) { return slots(V.tracks[t].m, Cat)[i] || "#" + (i + 1); }
function modRow(sr) {
	const C = S.ctl, id = "app:" + sr.id;
	return `<button class="srch k-${sr.kind === "lfo" ? "lfo" : "rnd"} ${id === C.sel ? "sel" : ""}" data-src="${id}" style="--f:${Mods.valueOf(sr.id) / 127 * 100}%" title="App only: runs in the editor on the machine's steps and sends CCs"><span class="sk">APP</span><b>${sr.label}</b><span class="sv mono">${Mods.valueOf(sr.id)}</span></button>
	 ${V.tracks.map((t, i) => { const ls = Mods.linksOf(sr.id).filter(o => o.l.track === i); return `<button class="mxc ${ls.length ? "on" : ""} ${id === C.sel && C.selT === i ? "sel" : ""}" data-mxsrc="${id}" data-mxt="${i}" title="${ls.map(o => paramName(i, o.l.param)).join(", ") || "no link"}">${ls.slice(0, 2).map(o => `<i>${paramName(i, o.l.param)}</i>`).join("")}${ls.length > 2 ? `<i>+${ls.length - 2}</i>` : ""}</button>`; }).join("")}`;
}
function modInspector(sr) {
	const C = S.ctl, links = Mods.linksOf(sr.id).filter(o => C.selT == null || o.l.track === C.selT);
	const rate = `<div class="irow"><span class="ilab">Rate</span><span class="seg" data-set="srcrate">${Mods.RATES.map(x => `<button data-v="${x}" aria-pressed="${sr.rate === x}">${x}</button>`).join("")}</span></div>`;
	const params = sr.kind === "lfo" ? `<div class="irow"><span class="ilab">Shape</span><div class="shapes">${SHAPES.map((n, i) => `<button data-srcshape="${i}" aria-pressed="${sr.shape === i}" title="${n}">${shapeIcon(i, false)}</button>`).join("")}</div></div>${rate}
	 <div class="irow"><span class="ilab">Depth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="Depth" aria-valuemax="100" data-g="src" data-n="depth" data-src="${sr.id}"><span>DEPTH</span><b></b></div></div></div>`
		: `${rate}<div class="irow"><span class="ilab">Smooth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="Smooth" data-g="src" data-n="smooth" data-src="${sr.id}"><span>SMOOTH</span><b></b></div></div></div>`;
	return `<section class="card"><header><h3>${sr.label}</h3><span><b class="apponly">App only</b> · not in the kit · <span title="${Mods.runs === "plug-in" ? "Run by the plug-in: they keep moving with the editor closed, saved with the project" : "Run by the editor while it is open"}">${Mods.runs === "plug-in" ? "runs with the editor closed" : "runs while the editor is open"}</span> · <span class="mono" id="ccrate" title="CCs the app modulators sent in the last second; the budget is ${Mods.limit}">${Mods.cc}/s of ${Mods.limit}</span></span></header>
	 <div class="note">Runs in the editor and moves with the machine's own steps while it plays, sent as CCs like host automation (at most ${Mods.limit} a second). A real Machinedrum does not play it, a lock wins on its step, and live recording does not record it. The setup is not saved with the project yet.</div>
	 ${params}
	 <div class="lhead"><span class="cap">Targets</span>${C.selT != null ? `<button class="ptog on" data-selt="all"><i class="led"></i>Track ${C.selT + 1} only</button>` : `<span class="note">${links.length} target${links.length === 1 ? "" : "s"}</span>`}</div>
	 <div class="lnks">${links.map(({ l, li }) => `<div class="lnk"><span class="lcdchip" title="${V.tracks[l.track].name}">T${l.track + 1} ${paramName(l.track, l.param)}</span>
	  <div class="pc" role="slider" tabindex="0" aria-label="Min" data-g="link" data-n="min" data-li="${li}"><span>MIN</span><b></b></div><div class="pc" role="slider" tabindex="0" aria-label="Max" data-g="link" data-n="max" data-li="${li}"><span>MAX</span><b></b></div>
	  <span class="seg" data-set="lcurve" data-li="${li}">${["lin", "exp", "log"].map(c => `<button data-v="${c}" aria-pressed="${l.curve === c}">${c.toUpperCase()}</button>`).join("")}</span>
	  <button class="ptog ${l.invert ? "on" : ""}" data-minv="${li}"><i class="led"></i>Inv</button><button class="iconkey" data-mdel="${li}" aria-label="Remove target" title="Remove">×</button></div>`).join("") || `<div class="note">No targets yet. Add one below.</div>`}</div>
	 <div class="irow addrow"><span class="ilab">Add</span><select id="mt">${V.tracks.map((t, i) => `<option value="${i}" ${i === C.addT ? "selected" : ""}>Track ${i + 1} · ${t.m}</option>`).join("")}</select>
	  <select id="mp">${slots(V.tracks[C.addT].m, Cat).map((p, i) => p ? `<option value="${i}">${p}</option>` : "").join("")}</select><button class="cream" id="maddl">Add target</button></div>
	 <div class="irow"><span class="ilab"></span><button data-addsrc="lfo">+ App LFO</button><button data-addsrc="random">+ Random</button><button class="danger" data-delsrc="${sr.id}">Remove ${sr.label}</button></div></section>`;
}
/* Controller rows: 8 knobs (CC 21-28 by default, the CC number is editable per row; kept per
   viewer), plus any other CC the plug-in's MIDI Learn preset maps. A row id is "ch:cc" (ch 255 =
   any channel). Mapping works with LEARN or here: a cell opens the row with that track's picker. */
/* The eight knob rows' CCs are part of the editor's setup (md-desk/setup), kept with the project by the
   plug-in (P4): the desk publishes it ("setup", kept as Docs.setup) and the rows are read from it (the
   defaults before it). A change is sent ("knobs") and shown until its answer (S.ctl.knobs, the CCs sent);
   the setup document comes before the answer. */
const KNOB_CCS_DEFAULT = Object.freeze([21, 22, 23, 24, 25, 26, 27, 28]);
function knobCcs() {
	if (S.ctl.knobs) return S.ctl.knobs.ccs;
	const k = Docs.setup && Docs.setup.knobCcs;
	return Array.isArray(k) ? KNOB_CCS_DEFAULT.map((c, i) => k[i] ?? c) : KNOB_CCS_DEFAULT;
}
/* knob row k (0-7) on CC to, sent */
function setKnobCc(k, to) {
	const ccs = knobCcs().slice(); ccs[k] = to;
	const pending = Object.freeze({ ccs: Object.freeze(ccs) });
	S.ctl.knobs = pending;
	cmd("knobs", { ccs }, undefined, undefined, () => { if (S.ctl.knobs === pending) S.ctl.knobs = null; });
}
function ccRows(maps) {
	const rows = knobCcs().map((cc, k) => ({ id: "255:" + cc, ch: 255, cc, label: "Knob " + (k + 1), knob: k }));
	for (const m of maps) { const id = m.ch + ":" + m.cc; if (!rows.some(r => r.id === id)) rows.push({ id, ch: m.ch, cc: m.cc, label: m.ch === 255 ? "CC " + m.cc : "CC " + m.cc + " ch " + (m.ch + 1), knob: -1 }); }
	return rows;
}
function renderControl() {
	const L = Docs.learn || { mappings: [], learning: null }, maps = L.mappings, rows = ccRows(maps);
	const C = S.ctl;
	if (C.sel == null || (C.sel.startsWith("app:") && !Mods.source(C.sel.slice(4))) || (!C.sel.startsWith("app:") && !rows.some(r => r.id === C.sel))) C.sel = rows[0].id;
	const mapsOf = id => maps.filter(m => m.ch + ":" + m.cc === id);
	const mx = `<div class="mx"><span></span>${V.tracks.map((t, i) => `<span class="mxh" title="${t.name}"><b>${i + 1}</b><small>${t.m}</small></span>`).join("")}
  ${rows.map(src => { const ms = mapsOf(src.id); return `<button class="srch ${src.id === C.sel ? "sel" : ""} k-cc" data-src="${src.id}" style="--f:0%" title="${ms.length ? ms.length + " target" + (ms.length > 1 ? "s" : "") : "Not mapped: does nothing yet"}"><span class="sk">CC ${src.cc}</span><b>${src.label}</b><span class="sv mono">${ms.length ? ms.length : "–"}</span></button>
   ${V.tracks.map((t, i) => { const ls = ms.filter(m => m.t === i); return `<button class="mxc ${ls.length ? "on" : ""} ${src.id === C.sel && C.selT === i ? "sel" : ""}" data-mxsrc="${src.id}" data-mxt="${i}" title="${ls.map(l => slots(t.m, Cat)[l.i] || l.name).join(", ") || "Map " + src.label + " to track " + (i + 1)}">${ls.slice(0, 2).map(l => `<i>${slots(t.m, Cat)[l.i] || l.name}</i>`).join("")}${ls.length > 2 ? `<i>+${ls.length - 2}</i>` : ""}</button>`; }).join("")}`; }).join("")}
  ${Mods.doc.sources.map(modRow).join("")}</div>`;
	const app = C.sel.startsWith("app:") ? Mods.source(C.sel.slice(4)) : null;
	let insp;
	if (app) insp = modInspector(app);
	else {
		const row = rows.find(r => r.id === C.sel), selMaps = mapsOf(C.sel).filter(m => C.selT == null || m.t === C.selT);
		const t = C.selT != null ? C.selT : C.addT;
		insp = `<section class="card"><header><h3>${row.label} · CC ${row.cc}</h3><span>${row.ch === 255 ? "any channel" : "channel " + (row.ch + 1)} · from your MIDI controller · saved with the plug-in's MIDI Learn preset</span></header>
  ${row.knob >= 0 ? `<div class="irow"><span class="ilab">CC</span><span class="stepper"><button data-knobcc="-1" aria-label="Lower CC number">−</button><b class="mono">${row.cc}</b><button data-knobcc="1" aria-label="Higher CC number">+</button></span><span class="note">the CC your controller's knob ${row.knob + 1} sends; its targets follow</span></div>` : ""}
  <div class="note">${L.learning ? `Learning <b>track ${L.learning.t + 1} ${slots(V.tracks[L.learning.t].m, Cat)[L.learning.i] || L.learning.name}</b>: turn a knob both ways.` : "Pick a target below, or press LEARN, click any value and turn a knob. The mapping moves the value through the plug-in's parameters, like host automation."}</div>
  <div class="lhead"><span class="cap">Targets</span>${C.selT != null ? `<button class="ptog on" data-selt="all"><i class="led"></i>Track ${C.selT + 1} only</button>` : `<span class="note">${selMaps.length} target${selMaps.length === 1 ? "" : "s"}</span>`}</div>
  <div class="lnks">${selMaps.map(l => `<div class="lnk"><span class="lcdchip" title="${V.tracks[l.t].name}">T${l.t + 1} ${slots(V.tracks[l.t].m, Cat)[l.i] || l.name}</span><span class="note">${l.mode}</span><span></span>
   <button class="ptog ${l.invert ? "on" : ""}" data-linv="${l.index}"><i class="led"></i>Inv</button><button class="iconkey" data-ldel="${l.index}" aria-label="Remove mapping" title="Remove">×</button></div>`).join("") || `<div class="note">Not mapped: this row does nothing yet.</div>`}</div>
  <div class="irow addrow"><span class="ilab">Add</span><select id="ct">${V.tracks.map((x, i) => `<option value="${i}" ${i === t ? "selected" : ""}>Track ${i + 1} · ${x.m}</option>`).join("")}</select>
   <select id="cp">${slots(V.tracks[t].m, Cat).map((p, i) => p ? `<option value="${i}">${p}</option>` : "").join("")}<option value="24">LEVEL</option></select><button class="cream" id="caddl">Add target</button></div>
  <div class="irow"><span class="ilab"></span><button data-addsrc="lfo">+ App LFO</button><button data-addsrc="random">+ Random</button></div></section>`;
	}
	$("#main").innerHTML = `<div class="ctlui"><section class="card"><header><h3>Mapping matrix</h3><span>rows = controller knobs, learned CCs and app modulators · columns = tracks</span></header>${mx}</section>${insp}</div>`;
	syncControls();
}
/* Values and the CC rate while the machine plays: in place, no re-render. */
function syncMods() {
	$$(".srch[data-src^='app:']").forEach(h => { const v = Mods.valueOf(h.dataset.src.slice(4)); h.style.setProperty("--f", v / 127 * 100 + "%"); const sv = h.querySelector(".sv"); if (sv) sv.textContent = v; });
	const r = $("#ccrate"); if (r) { r.textContent = `${Mods.cc}/s of ${Mods.limit}`; r.classList.toggle("hot", Mods.cc >= Mods.limit); }
}
document.addEventListener("pointerdown", e => {
	if (!S.ctl.learn) return;
	const el = e.target.closest(".pc[data-g],.fader[data-g]");
	if (el && ["syn", "fx", "rt", "lfo"].includes(el.dataset.g)) {
		e.stopPropagation(); e.preventDefault();
		const t = el.dataset.t != null ? +el.dataset.t : S.sel, n = el.dataset.n;
		const i = el.dataset.g === "lfo" ? Enums().lfoParams[n] : pidx(t, n, el.dataset.g);
		if (i == null || i < 0) return;
		/* only the targets the plug-in's learn takes (the learn document's limits) */
		const lim = Docs.learn && Docs.learn.limits;
		if (lim && (t >= lim.tracks || !lim.params.some(q => q.i === i))) { toast(`Track ${t + 1} ${n} cannot be learned.`); return; }
		S.ctl.learnT = { t, p: n }; syncControls(); cmd("learnStart", { t, i }); toast(`Target: track ${t + 1} ${n}. Now turn a knob on your controller.`);
	}
}, true);

/* the Control page's clicks (the router's, mdDeskRender.js CLICKS): true when the click was theirs */
function clickControl(e) {
	if (S.ws !== "control") return false;
	const C = S.ctl;
	const shh = e.target.closest(".srch.k-cc"); if (shh) { C.sel = shh.dataset.src; C.selT = null; render(); return true; }
	const mc = e.target.closest(".mxc[data-mxsrc]"); if (mc) { C.sel = mc.dataset.mxsrc; C.selT = C.addT = +mc.dataset.mxt; render(); return true; }
	if (e.target.closest("#caddl")) { const [ch, cc] = C.sel.split(":").map(Number); cmd("learnAdd", Object.assign({ cc, t: +$("#ct").value, i: +$("#cp").value }, ch === 255 ? {} : { ch })); return true; }	/* no ch: any channel */
	const kc = e.target.closest("[data-knobcc]"); if (kc) {
		const [, cc] = C.sel.split(":").map(Number), ccs = knobCcs(), k = ccs.indexOf(cc), to = clamp(cc + +kc.dataset.knobcc, 0, 127);
		if (k < 0 || ccs.includes(to)) { toast("Another knob row already uses CC " + to + "."); return true; }
		setKnobCc(k, to); cmd("learnSetCc", { from: cc, to }); C.sel = "255:" + to; render(); return true;
	}
	if (e.target.closest("[data-selt]")) { C.selT = null; render(); return true; }
	const li = e.target.closest("[data-linv]"); if (li) { cmd("learnInvert", { index: +li.dataset.linv }); return true; }
	const as = e.target.closest("[data-addsrc]"); if (as) { const id = Mods.add(as.dataset.addsrc); C.sel = "app:" + id; C.selT = null; sendMods(); render(); return true; }
	const ds = e.target.closest("[data-delsrc]"); if (ds) { Mods.remove(ds.dataset.delsrc); C.sel = null; sendMods(); render(); return true; }
	const mi = e.target.closest("[data-minv]"); if (mi) { const li = +mi.dataset.minv, l = Mods.doc.links[li]; if (l) { Mods.setLink(li, { invert: !l.invert }); sendMods(); render(); } return true; }
	const md = e.target.closest("[data-mdel]"); if (md) { Mods.removeLink(+md.dataset.mdel); sendMods(); render(); return true; }
	const ss = e.target.closest("[data-srcshape]"); if (ss) { const id = C.sel.slice(4); if (Mods.source(id)) { Mods.setSource(id, { shape: +ss.dataset.srcshape }); sendMods(); render(); } return true; }
	if (e.target.closest("#maddl")) { const s = Mods.source(C.sel.slice(4)), p = +$("#mp").value; if (s && Mods.link(s.id, C.addT, p)) { sendMods(); render(); } else toast("That target is already linked."); return true; }
	const asrc = e.target.closest(".srch[data-src^='app:']"); if (asrc) { C.sel = asrc.dataset.src; C.selT = null; render(); return true; }
	const ld = e.target.closest("[data-ldel]"); if (ld) { cmd("learnRemove", { index: +ld.dataset.ldel }); return true; }
	return false;
}
/* the inspector's track pickers */
document.addEventListener("change", e => {
	const id = e.target.id; if (id !== "mt" && id !== "ct") return;
	S.ctl.addT = +e.target.value; if (id === "ct" && S.ctl.selT != null) S.ctl.selT = +e.target.value; render();
});
