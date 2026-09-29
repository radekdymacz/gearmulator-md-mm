"use strict";
/* The controller profile (doc/modern-ux/DESIGN-tr06.md) on the page, one file for both editors' pages (the
   MM page loads it from here, as it does mdDeskBridge.js). A Roland TR-06 on one MIDI channel plays the
   machine: its voices trigger tracks, its knobs move the selected track's parameters. It only renders the
   plug-in's "controller" document (Ctl.onDoc) and asks its host for changes; the host is the page's own
   sender, so the pages' contract checks see every op:
     Ctl.host = { set({profile, channel}), voice({voice, t, note}), knob({cc, pg, i}), reset(), watch({on}) }
   Two places:
   - the bar (Ctl.barHtml(), #ctlbar), under the CONTROL workspace's mapping matrix heading: the profile
     and the channel, the TR-06 found among the MIDI inputs (Use TR-06), what arrives, and the key
     (#ctlkey, TR-06 MAP…, its LED lit while the profile is on) that opens the panel. The page puts it
     after the matrix card's header when it renders and calls Ctl.rendered() after every render;
   - the panel (#ctlpop, made here), a "panel" dialog of the MODAL layer (closeController): the same, and
     the voices' tracks and the knobs' targets.
   While either shows, the plug-in reports what arrives (host.watch: ctlWatch on); the profile is never
   turned on by it. Every select and input has a stable id (ctl-profile, ctl-channel, ctl-bar-profile,
   ctl-bar-channel, ctl-voice-<voice>, ctl-knob-<cc>): the pages' key-style dropdowns (enhanceSelects,
   openK) find a select by its id, and a redraw while a list is open keeps it pointing at the new select. */
const Ctl = (() => {
	let doc = null, open = false, lastCc = -1, host = {}, barShown = false, watching = false, seq = -1, barKey = "";
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
.ctlpop tr.blink td{background:var(--led);color:var(--ink)}
.ctlpop tr.off td{opacity:.55}
.ctlpop td.mono,.ctlpop .mono{font-family:var(--mono)}
.ctlpop select,.ctlpop input{font:500 12px var(--sans)}
.ctlpop input[type=number]{width:52px}
.ctlpop .ctlwarn{margin:0;padding:6px 10px;border-radius:3px;background:var(--rec,#c43);color:#fff;font:600 12px var(--sans)}
.ctlpop .ctlscroll{max-height:46vh;overflow:auto}
.ctlhint,.ctlelse{display:inline-flex;align-items:center;gap:6px;margin:0;font:600 12px var(--sans);color:var(--body)}
.ctlpop .ctlhint,.ctlpop .ctlelse{display:flex;padding:5px 8px;border-radius:3px;background:var(--lcd);color:var(--ink)}
.ctlact{font:500 11px var(--mono);color:var(--print);white-space:nowrap}
.ctlact.blink{color:var(--led)}
.ctlbar{display:flex;flex-wrap:wrap;align-items:center;gap:4px 8px;margin:0 0 8px;padding:4px 0 6px;border-bottom:1px solid var(--rule);font:600 11px var(--sans)}
.ctlbar .ilab{width:auto;min-width:0;font:600 10px var(--pix);text-transform:uppercase;color:var(--print2)}
.ctlbar .kselbtn{width:auto;min-width:62px;height:24px;padding:0 8px;gap:6px}
.ctlactw{display:inline-flex;flex-wrap:wrap;align-items:center;gap:4px 10px}
.ctlbar .amkey,.ctlhint .amkey,.ctlelse .amkey{padding:2px 8px}
.ctlbar .ctlkey,.ctlpop .ctlkey{white-space:nowrap}
.ctlkey.hint .led{animation:pgblink .5s steps(2) infinite;background:var(--led)}
.ctlkey .led.on{background:var(--led);box-shadow:0 0 6px 1px var(--led)}
@media (max-width:900px){.ctlpop .ctlgrid{grid-template-columns:1fr}}`;
	document.head.appendChild(style);

	const opts = (list, cur) => list.map(([v, l]) => `<option value="${esc(v)}"${String(v) === String(cur) ? " selected" : ""}>${esc(l)}</option>`).join("");
	const tracks = () => Array.from({ length: doc.tracks }, (_, t) => [t, "TRACK " + (t + 1)]);
	const targetKey = t => (t.pg ?? -1) + ":" + t.i;
	const targetList = () => [["-1:-1", "—"], ...doc.targets.map(t => [targetKey(t), t.name])];
	const profileOpts = () => opts(doc.profiles.map(p => [p.id, p.id === "off" ? "Off" : "TR-06"]), doc.profile);
	const channelOpts = () => opts(Array.from({ length: 16 }, (_, c) => [c + 1, String(c + 1)]), doc.channel);
	const isOn = () => !!doc && doc.profile !== "off";
	/* a TR-06 among the enabled MIDI inputs while the profile is off: say so, offer it (never turn it on) */
	const found = () => !!doc && !isOn() && !!doc.device;

	/* ---- what arrives (doc.activity, while the bar or the panel shows) ---- */
	function lastText() {
		const a = doc && doc.activity; if (!a) return "";
		const l = a.last, ch = "CH " + doc.channel + " · ";
		if (!l) return ch + "nothing yet";
		if (l.kind === "cc") return ch + "CC " + l.n + " = " + l.v;
		if (l.kind === "note") return ch + "NOTE " + l.n;
		if (l.kind === "off") return ch + "NOTE " + l.n + " OFF";
		return ch + "MSG " + l.n.toString(16).toUpperCase();
	}
	function elsewhereHtml() {
		const a = doc && doc.activity; if (!a || a.elsewhere == null) return "";
		const x = a.elsewhere;
		const text = doc.device ? `TR-06 is sending on CH ${x} — set CHANNEL to ${x}` : `MIDI in on CH ${x}, nothing on CH ${doc.channel}: if that is the TR-06, set CHANNEL to ${x}`;
		return `<span class="ctlelse" role="status">${esc(text)} <button class="amkey" data-ctl="setch" data-ch="${x}">SET CH ${x}</button></span>`;
	}
	const hintHtml = () => found() ? `<span class="ctlhint" role="status">${esc(doc.device)} connected — turn the profile on <button class="amkey" data-ctl="use">USE TR-06</button></span>` : "";
	const actHtml = () => `<span class="ctlact" aria-live="off">${esc(lastText())}</span>${elsewhereHtml()}`;
	function keyHtml() {
		const on = isOn(), hint = found();
		const title = hint ? "TR-06 connected — turn the profile on. Opens the TR-06's mapping." : on ? "The TR-06 plays the machine. Opens its mapping: voices to tracks, knobs to parameters."
			: "The Roland TR-06 controller profile is off. Opens its mapping.";
		return `<button class="ptog ctlkey${on ? " on" : ""}${hint ? " hint" : ""}" id="ctlkey" type="button" data-ctl="map" aria-haspopup="dialog" title="${esc(title)}"><i class="led${on ? " on" : ""}"></i>TR-06 MAP…</button>`;
	}

	/* ---- the bar, in the CONTROL workspace's header ---- */
	function barInner() {
		if (!doc) return `<span class="ctlact">TR-06: waiting for the editor…</span>`;
		return `<span class="ilab">TR-06</span><select id="ctl-bar-profile" data-ctl="profile" aria-label="Controller profile">${profileOpts()}</select>
			<span class="ilab">CH</span><select id="ctl-bar-channel" data-ctl="channel" aria-label="Controller channel">${channelOpts()}</select>
			${hintHtml()}<span class="ctlactw">${actHtml()}</span>${keyHtml()}`;
	}
	const barState = () => doc ? JSON.stringify([doc.profile, doc.channel, doc.device, !!doc.activity]) : "";
	function barHtml() { barKey = barState(); return `<div class="ctlbar" id="ctlbar">${barInner()}</div>`; }
	function syncBar() {
		const bar = document.getElementById("ctlbar"); if (!bar) return;
		if (barState() !== barKey) {
			barKey = barState(); bar.innerHTML = barInner();
			if (typeof enhanceSelects === "function") enhanceSelects(bar);
			return;
		}
		const w = bar.querySelector(".ctlactw"); if (w) w.innerHTML = actHtml();
	}

	/* ---- the panel ---- */
	function draw() {
		if (!open) { pop.hidden = true; return; }
		const head = note => `<div class="libhead"><span class="cap">Controller</span>${note}<button class="libx" data-ctl="close" title="Close (Esc)">Esc</button></div>`;
		if (!doc) { pop.innerHTML = head(`<span class="note">Waiting for the editor…</span>`); pop.hidden = false; place(); return; }
		const on = isOn(), mm = doc.machine === "mm";
		const voices = doc.voices.map(v => `<tr class="${on ? "" : "off"}" data-voice="${esc(v.voice)}"><td><b>${esc(v.voice)}</b> ${esc(v.label)}</td><td class="mono">${v.notes.join(", ")}</td>
			<td><select id="ctl-voice-${esc(v.voice)}" data-ctl="voice" data-voice="${esc(v.voice)}" aria-label="${esc(v.voice)} track">${opts(tracks(), v.t)}</select></td>
			${mm ? `<td><input id="ctl-note-${esc(v.voice)}" type="number" min="0" max="127" value="${v.note}" data-ctl="note" data-voice="${esc(v.voice)}" data-t="${v.t}" aria-label="${esc(v.voice)} note"></td>` : ""}
			<td class="mono">${v.out ? "CH " + v.out.ch + " · " + v.out.note : "—"}</td></tr>`).join("");
		const knobs = doc.knobs.map(k => `<tr class="${k.cc === lastCc ? "hot" : ""} ${on && k.i >= 0 ? "" : "off"}" data-cc="${k.cc}"><td>${esc(k.label)}</td><td class="mono">CC ${k.cc}</td>
			<td><select id="ctl-knob-${k.cc}" data-ctl="knob" data-cc="${k.cc}" aria-label="${esc(k.label)} target">${opts(targetList(), targetKey(k))}</select></td></tr>`).join("");
		pop.innerHTML = head(`<span class="note">${esc(on ? "The " + doc.profiles.find(p => p.id === doc.profile)?.label + " plays the machine on channel " + doc.channel + ". Its knobs move track " + (doc.selected + 1) + " (the selected track)." : "Off: MIDI in reaches the machine as it always did.")}</span>`) + `
		<div class="grow2"><span class="ilab">Profile</span><select id="ctl-profile" data-ctl="profile" aria-label="Controller profile">${opts(doc.profiles.map(p => [p.id, p.label]), doc.profile)}</select>
			<span class="ilab">Channel</span><select id="ctl-channel" data-ctl="channel" aria-label="Controller channel">${channelOpts()}</select>
			<button class="amkey" data-ctl="reset" title="The TR-06's shipped mapping and channel 10; the profile stays on or off">RESET TO DEFAULTS</button></div>
		${hintHtml()}
		<div class="ctlactw">${actHtml()}</div>
		${doc.warning ? `<p class="ctlwarn" role="alert">${esc(doc.warning)}</p>` : ""}
		${on && !doc.known ? `<p class="note">Waiting for the machine's global settings: the voices play once the editor has read them.</p>` : ""}
		<div class="ctlgrid">
			<section class="card"><header><h3>Voices</h3><span>notes → tracks</span></header><div class="ctlscroll"><table><tr><th>TR-06</th><th>Notes</th><th>Track</th>${mm ? "<th>Note</th>" : ""}<th>Plays</th></tr>${voices}</table></div></section>
			<section class="card"><header><h3>Knobs</h3><span>on the selected track</span></header><div class="ctlscroll" id="ctlknobs"><table><tr><th>TR-06</th><th>CC</th><th>Moves</th></tr>${knobs}</table></div></section>
		</div>
		<div class="libfoot"><span>${esc(doc.about)}</span><span>${doc.named ? "Turn a knob or hit a pad on the TR-06: its row lights" : "In a DAW, route the TR-06's MIDI to this track; turn a knob: its row lights"} · set the TR-06's Soft Thru off</span></div>`;
		if (typeof enhanceSelects === "function") enhanceSelects(pop);
		pop.hidden = false; place();
	}
	function place() {
		const lcd = document.querySelector(".lcdpanel"), top = Math.max(16, lcd ? lcd.getBoundingClientRect().bottom + 8 : 16);
		pop.style.top = (top + scrollY) + "px"; pop.style.maxHeight = Math.max(240, innerHeight - top - 12) + "px";
		pop.style.left = Math.max(16, (document.documentElement.clientWidth - pop.offsetWidth) / 2 + scrollX) + "px";
	}
	/* a knob the TR-06 moved (profile on): its row lights (and comes into view) without redrawing the panel */
	function light() {
		for (const r of pop.querySelectorAll("tr.hot")) r.classList.remove("hot");
		const row = pop.querySelector(`tr[data-cc="${lastCc}"]`); if (!row) return;
		row.classList.add("hot"); row.scrollIntoView({ block: "nearest" });
	}
	/* what arrived: the panel's and the bar's line in place, and the row of the message blinks */
	function blink(el) { if (!el) return; el.classList.remove("blink"); void el.offsetWidth; el.classList.add("blink"); setTimeout(() => el.classList.remove("blink"), 180); }
	function arrived() {
		const w = pop.querySelector(".ctlactw"); if (w) w.innerHTML = actHtml();
		syncBar();
		const a = doc.activity; if (!a || a.seq === seq) return;
		seq = a.seq;
		const l = a.last; if (!l) return;
		if (l.kind === "cc") blink(pop.querySelector(`tr[data-cc="${l.n}"]`));
		else if (l.kind === "note" || l.kind === "off") { const v = doc.voices.find(x => x.notes.includes(l.n)); if (v) blink(pop.querySelector(`tr[data-voice="${v.voice}"]`)); }
		for (const e of document.querySelectorAll(".ctlact")) blink(e);
	}
	const without = d => JSON.stringify({ ...d, last: null, activity: null });
	function onDoc(d) {
		const before = doc; doc = d;
		lastCc = d.last && d.last.cc != null ? d.last.cc : -1;
		const onlyLive = before && without(before) === without(d);
		if (open && !onlyLive) draw();
		else if (open && JSON.stringify(before.last) !== JSON.stringify(d.last)) light();
		if (!onlyLive) syncBar();
		arrived();
	}
	/* whether the plug-in should report what arrives: while the bar or the panel shows */
	function setWatch() {
		const w = open || barShown;
		if (w === watching) return;
		watching = w;
		if (typeof host.watch === "function") host.watch({ on: w });
	}
	/* a control of the bar or the panel changed: which host call, with what (pure: the page test checks it) */
	function intent(d, k, value, data) {
		const mm = d.machine === "mm";
		if (k === "profile") return ["set", { profile: value }];
		if (k === "use") return ["set", { profile: "tr06" }];
		if (k === "channel") return ["set", { channel: +value }];
		if (k === "setch") return ["set", { channel: +data.ch }];
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
	function click(el) {
		const k = el.dataset.ctl;
		if (k === "close") { closeController(); return true; }
		if (k === "map") { Ctl.open(); return true; }
		if (k === "reset") { if (typeof host.reset === "function") host.reset(); return true; }
		if (k === "use" || k === "setch") { change(k, "", el.dataset); return true; }
		return false;
	}
	pop.addEventListener("change", e => { const el = e.target.closest("[data-ctl]"); if (el) change(el.dataset.ctl, el.value, el.dataset); });
	pop.addEventListener("click", e => {
		const el = e.target.closest("[data-ctl]");
		if (el && el.tagName === "BUTTON" && click(el)) return;
		/* a click on a knob's row opens its target list */
		const row = e.target.closest("tr[data-cc]"); if (row && !e.target.closest("select,button,input")) { const s = row.querySelector(".kselbtn") || row.querySelector("select"); if (s) { s.focus(); if (s.click) s.click(); } }
	});
	/* the bar lives in the page's markup (rendered again with the workspace): listened to at the document */
	document.addEventListener("change", e => { const el = e.target.closest && e.target.closest("#ctlbar [data-ctl]"); if (el) change(el.dataset.ctl, el.value, el.dataset); });
	document.addEventListener("click", e => { const el = e.target.closest && e.target.closest("#ctlbar button[data-ctl]"); if (el) click(el); });
	return {
		get host() { return host; }, set host(_h) { host = _h || {}; watching = false; setWatch(); },
		onDoc, intent, change, barHtml, html: () => pop.innerHTML, doc: () => doc, isOpen: () => open,
		/* after every render of the page: whether the bar shows now */
		rendered() { barShown = !!document.getElementById("ctlbar"); setWatch(); },
		open() { open = true; draw(); setWatch(); }, close() { open = false; draw(); setWatch(); }
	};
})();
function openController() { Ctl.open(); }
function closeController() { Ctl.close(); }
