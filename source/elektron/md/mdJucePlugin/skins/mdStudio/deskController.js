"use strict";
/* The controller profile's panel (doc/modern-ux/DESIGN-tr06.md), one file for both editors' pages (the MM
   page loads it from here, as it does mdDeskBridge.js). A Roland TR-06 on one MIDI channel plays the
   machine: its voices trigger tracks, its knobs move the selected track's parameters. Opened from the
   engine menu (CONTROLLER…). It only renders the plug-in's "controller" document (Ctl.onDoc) and asks
   its host for changes; the host is the page's own sender, so the pages' contract checks see every op:
     Ctl.host = { set({profile, channel}), voice({voice, t, note}), knob({cc, pg, i}), reset() }
   The panel is #ctlpop (made here), a "panel" dialog of the MODAL layer (closeController). */
const Ctl = (() => {
	let doc = null, open = false, lastCc = -1, host = {};
	const esc = t => String(t ?? "").replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
	const pop = document.createElement("div");
	pop.className = "ampop ctlpop"; pop.id = "ctlpop"; pop.setAttribute("role", "dialog"); pop.setAttribute("aria-label", "Controller"); pop.hidden = true;
	document.body.appendChild(pop);
	const style = document.createElement("style");
	style.textContent = `.ctlpop .ctlgrid{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1.3fr);gap:10px}
.ctlpop table{width:100%;border-collapse:collapse;font:500 12px var(--sans)}
.ctlpop th{text-align:left;font:600 10px var(--pix);text-transform:uppercase;color:var(--print2);padding:3px 6px;border-bottom:1px solid var(--rule)}
.ctlpop td{padding:2px 6px;border-bottom:1px solid var(--rule);white-space:nowrap}
.ctlpop td:first-child{white-space:normal}
.ctlpop td .kselbtn,.ctlpop td select{max-width:200px}
.ctlpop tr.hot td{background:var(--lcd);color:var(--ink)}
.ctlpop tr.off td{opacity:.55}
.ctlpop td.mono,.ctlpop .mono{font-family:var(--mono)}
.ctlpop select,.ctlpop input{font:500 12px var(--sans)}
.ctlpop input[type=number]{width:52px}
.ctlpop .ctlwarn{margin:0;padding:6px 10px;border-radius:3px;background:var(--rec,#c43);color:#fff;font:600 12px var(--sans)}
.ctlpop .ctlscroll{max-height:46vh;overflow:auto}
@media (max-width:900px){.ctlpop .ctlgrid{grid-template-columns:1fr}}`;
	document.head.appendChild(style);

	const opts = (list, cur) => list.map(([v, l]) => `<option value="${esc(v)}"${String(v) === String(cur) ? " selected" : ""}>${esc(l)}</option>`).join("");
	const tracks = () => Array.from({ length: doc.tracks }, (_, t) => [t, "TRACK " + (t + 1)]);
	const targetKey = t => (t.pg ?? -1) + ":" + t.i;
	const targetList = () => [["-1:-1", "—"], ...doc.targets.map(t => [targetKey(t), t.name])];

	function draw() {
		if (!open) { pop.hidden = true; return; }
		const head = note => `<div class="libhead"><span class="cap">Controller</span>${note}<button class="libx" data-ctl="close" title="Close (Esc)">Esc</button></div>`;
		if (!doc) { pop.innerHTML = head(`<span class="note">Waiting for the editor…</span>`); pop.hidden = false; place(); return; }
		const on = doc.profile !== "off", mm = doc.machine === "mm";
		const voices = doc.voices.map(v => `<tr class="${on ? "" : "off"}"><td><b>${esc(v.voice)}</b> ${esc(v.label)}</td><td class="mono">${v.notes.join(", ")}</td>
			<td><select data-ctl="voice" data-voice="${esc(v.voice)}" aria-label="${esc(v.voice)} track">${opts(tracks(), v.t)}</select></td>
			${mm ? `<td><input type="number" min="0" max="127" value="${v.note}" data-ctl="note" data-voice="${esc(v.voice)}" data-t="${v.t}" aria-label="${esc(v.voice)} note"></td>` : ""}
			<td class="mono">${v.out ? "CH " + v.out.ch + " · " + v.out.note : "—"}</td></tr>`).join("");
		const knobs = doc.knobs.map(k => `<tr class="${k.cc === lastCc ? "hot" : ""} ${on && k.i >= 0 ? "" : "off"}" data-cc="${k.cc}"><td>${esc(k.label)}</td><td class="mono">CC ${k.cc}</td>
			<td><select data-ctl="knob" data-cc="${k.cc}" aria-label="${esc(k.label)} target">${opts(targetList(), targetKey(k))}</select></td></tr>`).join("");
		pop.innerHTML = head(`<span class="note">${esc(on ? "The " + doc.profiles.find(p => p.id === doc.profile)?.label + " plays the machine on channel " + doc.channel + ". Its knobs move track " + (doc.selected + 1) + " (the selected track)." : "Off: MIDI in reaches the machine as it always did.")}</span>`) + `
		<div class="grow2"><span class="ilab">Profile</span><select data-ctl="profile" aria-label="Controller profile">${opts(doc.profiles.map(p => [p.id, p.label]), doc.profile)}</select>
			<span class="ilab">Channel</span><select data-ctl="channel" aria-label="Controller channel">${opts(Array.from({ length: 16 }, (_, c) => [c + 1, String(c + 1)]), doc.channel)}</select>
			<button class="amkey" data-ctl="reset" title="The TR-06's shipped mapping and channel 10; the profile stays on or off">RESET TO DEFAULTS</button></div>
		${doc.warning ? `<p class="ctlwarn" role="alert">${esc(doc.warning)}</p>` : ""}
		${on && !doc.known ? `<p class="note">Waiting for the machine's global settings: the voices play once the editor has read them.</p>` : ""}
		<div class="ctlgrid">
			<section class="card"><header><h3>Voices</h3><span>notes → tracks</span></header><div class="ctlscroll"><table><tr><th>TR-06</th><th>Notes</th><th>Track</th>${mm ? "<th>Note</th>" : ""}<th>Plays</th></tr>${voices}</table></div></section>
			<section class="card"><header><h3>Knobs</h3><span>on the selected track</span></header><div class="ctlscroll" id="ctlknobs"><table><tr><th>TR-06</th><th>CC</th><th>Moves</th></tr>${knobs}</table></div></section>
		</div>
		<div class="libfoot"><span>${esc(doc.about)}</span><span>Turn a knob on the TR-06 to find its row · set the TR-06's Soft Thru off</span></div>`;
		if (typeof enhanceSelects === "function") enhanceSelects(pop);
		pop.hidden = false; place();
	}
	function place() {
		const lcd = document.querySelector(".lcdpanel"), top = Math.max(16, lcd ? lcd.getBoundingClientRect().bottom + 8 : 16);
		pop.style.top = (top + scrollY) + "px"; pop.style.maxHeight = Math.max(240, innerHeight - top - 12) + "px";
		pop.style.left = Math.max(16, (document.documentElement.clientWidth - pop.offsetWidth) / 2 + scrollX) + "px";
	}
	/* a knob the TR-06 moved: its row lights (and comes into view) without redrawing the panel */
	function light() {
		for (const r of pop.querySelectorAll("tr.hot")) r.classList.remove("hot");
		const row = pop.querySelector(`tr[data-cc="${lastCc}"]`); if (!row) return;
		row.classList.add("hot"); row.scrollIntoView({ block: "nearest" });
	}
	function onDoc(d) {
		const before = doc; doc = d;
		const cc = d.last && d.last.cc != null ? d.last.cc : -1;
		const onlyLast = before && JSON.stringify({ ...before, last: null }) === JSON.stringify({ ...d, last: null });
		if (cc !== lastCc) lastCc = cc;
		if (!open) return;
		if (onlyLast) light(); else draw();
	}
	/* a control of the panel changed: which host call, with what (pure: the page test checks it) */
	function intent(d, k, value, data) {
		const mm = d.machine === "mm";
		if (k === "profile") return ["set", { profile: value }];
		if (k === "channel") return ["set", { channel: +value }];
		if (k === "voice") { const v = d.voices.find(x => x.voice === data.voice); return ["voice", mm ? { voice: data.voice, t: +value, note: v ? v.note : 60 } : { voice: data.voice, t: +value }]; }
		if (k === "note") return ["voice", { voice: data.voice, t: +data.t, note: Math.max(0, Math.min(127, Math.round(+value || 0))) }];
		if (k === "knob") { const [pg, i] = String(value).split(":").map(Number); return ["knob", mm ? { cc: +data.cc, pg: Math.max(0, pg), i: i < 0 ? null : i } : { cc: +data.cc, i: i < 0 ? null : i }]; }
		return null;
	}
	/* the host call for a control's change */
	function change(k, value, data) {
		const it = doc && intent(doc, k, value, data);
		if (it && typeof host[it[0]] === "function") host[it[0]](it[1]);
	}
	pop.addEventListener("change", e => { const el = e.target.closest("[data-ctl]"); if (el) change(el.dataset.ctl, el.value, el.dataset); });
	pop.addEventListener("click", e => {
		const el = e.target.closest("[data-ctl]");
		if (el && el.dataset.ctl === "close") { closeController(); return; }
		if (el && el.dataset.ctl === "reset") { if (typeof host.reset === "function") host.reset(); return; }
		/* a click on a knob's row opens its target list */
		const row = e.target.closest("tr[data-cc]"); if (row && !e.target.closest("select,button,input")) { const s = row.querySelector("select,.kselbtn"); if (s) { s.focus(); if (s.click) s.click(); } }
	});
	return {
		get host() { return host; }, set host(_h) { host = _h || {}; },
		onDoc, intent, change, html: () => pop.innerHTML, doc: () => doc, isOpen: () => open,
		open() { open = true; draw(); }, close() { open = false; draw(); }
	};
})();
function openController() { Ctl.open(); }
function closeController() { Ctl.close(); }
