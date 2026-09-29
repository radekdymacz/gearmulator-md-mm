"use strict";
/* The CONTROL workspace's MIDI devices and the controller profile (doc/modern-ux/DESIGN-tr06.md) on the page,
   one file for both editors' pages (the MM page loads it from here, as it does mdDeskBridge.js). It only
   renders the plug-in's "controller" document (Ctl.onDoc) and asks its host for changes; the host is the
   page's own sender, so the pages' contract checks see every op:
     Ctl.host = { set({profile, channel, knobMode}), voice({voice, t, note}), knob({cc, pg, i}), reset(), watch({on}), clear() }
     Ctl.rerender = the page's render (a tile or DEVICES changes the view)
   The workspace (Ctl.controlHtml(learnHtml), #ctlroot) has three views:
   - the devices: one tile per MIDI input (the standalone app: doc.inputs, enabled or not; in a DAW, where the
     host owns MIDI, "Host MIDI in" and the TR-06). A TR-06 tile shows the profile's state (its LED);
   - the TR-06: the profile, the channel, the knob mode, the live MIDI monitor (every CC on the TR-06's channel
     and on others, with its latest value, the TR-06's name for it and what it moves on the selected track;
     the last notes; CLEAR), the voices' tracks, the knobs' targets, reset to defaults. Nothing blinks: values
     update in place. While it shows, the plug-in reports what arrives (host.watch: ctlWatch on);
   - any other device: the page's own mapping matrix (MIDI Learn, App LFO / Random: learnHtml), unchanged.
   The page calls Ctl.rendered() after every render. Every select and input has a stable id (ctl-profile,
   ctl-channel, ctl-knobmode, ctl-voice-<voice>, ctl-note-<voice>, ctl-knob-<cc>): the pages' key-style
   dropdowns (enhanceSelects, openK) find a select by its id, and a redraw while a list is open keeps it
   pointing at the new select. */
const Ctl = (() => {
	let doc = null, host = {}, view = "", watching = false, shown = false, drawnKey = "";
	const noteFrom = {};	/* voice -> the note it played before NOTE last moved it (MM) */
	const esc = t => String(t ?? "").replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
	const style = document.createElement("style");
	style.textContent = `.ctlroot{display:flex;flex-direction:column;gap:10px}
.ctlroot table{width:100%;border-collapse:collapse;font:500 12px var(--sans)}
.ctlroot th{text-align:left;font:600 10px var(--pix);text-transform:uppercase;color:var(--print2);padding:3px 6px;border-bottom:1px solid var(--rule)}
.ctlroot td{padding:2px 6px;border-bottom:1px solid var(--rule);white-space:nowrap}
.ctlroot td .kselbtn,.ctlroot td select{max-width:220px}
.ctlroot tr.off td{opacity:.55}
.ctlroot td.mono,.ctlroot .mono{font-family:var(--mono)}
.ctlroot select,.ctlroot input{font:500 12px var(--sans)}
.ctlroot input[type=number]{width:52px}
.ctlroot .ctlwarn{margin:0;padding:6px 10px;border-radius:3px;background:var(--rec,#c43);color:#fff;font:600 12px var(--sans)}
.ctlroot .ctlscroll{max-height:52vh;overflow:auto}
.ctlhint,.ctlelse{display:flex;align-items:center;gap:6px;margin:0;padding:5px 8px;border-radius:3px;background:var(--lcd);color:var(--ink);font:600 12px var(--sans)}
.ctlroot .amkey{padding:2px 8px}
.ctlhead{display:flex;flex-wrap:wrap;align-items:center;gap:6px 12px}
.ctlhead h3{margin:0;font:600 14px var(--sans)}
.ctlhead .note{font:500 12px var(--sans);color:var(--print2)}
.ctlset{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px}
.ctlset .ilab{width:auto;min-width:0;font:600 10px var(--pix);text-transform:uppercase;color:var(--print2)}
.ctlset .kselbtn{width:auto;min-width:72px;height:24px;padding:0 8px;gap:6px}
.ctlgrid{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(0,1fr) minmax(0,1.2fr);gap:10px}
.ctltiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px}
.ctltile{display:flex;flex-direction:column;align-items:center;gap:6px;padding:14px 10px 10px;border:1px solid var(--rule);border-radius:4px;background:transparent;color:var(--body);cursor:pointer;font:600 12px var(--sans);text-align:center}
.ctltile:hover,.ctltile:focus-visible{border-color:var(--print);outline:none}
.ctltile svg{width:96px;height:56px;color:var(--print)}
.ctltile .tname{font:600 13px var(--sans)}
.ctltile .tstate{display:flex;align-items:center;gap:6px;font:600 10px var(--pix);text-transform:uppercase;color:var(--print2)}
.ctltile.dis{opacity:.6}
.ctlroot .led{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--rule)}
.ctlroot .led.on{background:var(--led);box-shadow:0 0 6px 1px var(--led)}
.ctlroot .led.dim{background:var(--led);opacity:.45}
.ctlmon td.v{text-align:right;width:3em}
.ctlmon .notes{margin:0 0 6px;font:500 12px var(--mono)}
@media (max-width:1100px){.ctlgrid{grid-template-columns:1fr}}`;
	document.head.appendChild(style);

	const opts = (list, cur) => list.map(([v, l]) => `<option value="${esc(v)}"${String(v) === String(cur) ? " selected" : ""}>${esc(l)}</option>`).join("");
	const tracks = () => Array.from({ length: doc.tracks }, (_, t) => [t, "TRACK " + (t + 1)]);
	const targetKey = t => (t.pg ?? -1) + ":" + t.i;
	/* a target's name on the selected track's machine: the slot, and the machine's own name where it differs */
	const named = t => t.name + (t.own ? " · " + (t.own === "-" ? "—" : t.own) : "");
	const targetList = () => [["-1:-1", "—"], ...doc.targets.map(t => [targetKey(t), named(t)])];
	const channelOpts = () => opts(Array.from({ length: 16 }, (_, c) => [c + 1, String(c + 1)]), doc.channel);
	const isOn = () => !!doc && doc.profile !== "off";
	/* a TR-06 among the enabled MIDI inputs while the profile is off: say so, offer it (never turn it on) */
	const found = () => !!doc && !isOn() && !!doc.device;
	const isNote = k => k.pg === 8 && k.i === 0;
	const ledHtml = () => `<i class="led${isOn() ? " on" : doc && doc.device ? " dim" : ""}"></i>`;

	/* ---- icons (inline, in the page's colours) ---- */
	const ICON_TR06 = `<svg viewBox="0 0 96 56" aria-hidden="true"><rect x="2" y="6" width="92" height="44" rx="4" fill="none" stroke="currentColor" stroke-width="2"/>
		${[0, 1, 2, 3, 4, 5, 6].map(i => `<circle cx="${12 + i * 10}" cy="18" r="3.5" fill="none" stroke="currentColor" stroke-width="1.6"/>`).join("")}
		${Array.from({ length: 16 }, (_, i) => `<rect x="${7 + i * 5.2}" y="36" width="3.6" height="7" rx="1" fill="currentColor" opacity="${i % 4 ? .45 : .9}"/>`).join("")}
		<rect x="80" y="14" width="10" height="8" rx="1" fill="none" stroke="currentColor" stroke-width="1.2"/></svg>`;
	const ICON_MIDI = `<svg viewBox="0 0 96 56" aria-hidden="true"><rect x="2" y="6" width="92" height="44" rx="4" fill="none" stroke="currentColor" stroke-width="2"/>
		${[0, 1, 2, 3].map(i => `<circle cx="${14 + i * 14}" cy="19" r="4.5" fill="none" stroke="currentColor" stroke-width="1.6"/><line x1="${14 + i * 14}" y1="19" x2="${14 + i * 14}" y2="15" stroke="currentColor" stroke-width="1.6"/>`).join("")}
		${Array.from({ length: 10 }, (_, i) => `<rect x="${8 + i * 8}" y="32" width="6.5" height="13" rx="1" fill="none" stroke="currentColor" stroke-width="1.2"/>`).join("")}
		<circle cx="80" cy="19" r="7" fill="none" stroke="currentColor" stroke-width="1.4"/>${[[76, 18], [84, 18], [80, 14], [77.5, 22], [82.5, 22]].map(([x, y]) => `<circle cx="${x}" cy="${y}" r="1" fill="currentColor"/>`).join("")}</svg>`;

	/* ---- the devices ---- */
	function tiles() {
		const list = [], inputs = doc && doc.inputs;
		if (!doc || inputs == null) {
			/* a DAW: the host owns MIDI and does not name it; the TR-06 tile is how the profile is reached there */
			list.push({ kind: "dev", id: "dev:host", name: "Host MIDI in", state: "the track's MIDI" });
			list.push({ kind: "tr06", id: "tr06", name: "TR-06", state: "" });
			return list;
		}
		for (const d of inputs) {
			if (d.tr06 && list.some(t => t.kind === "tr06")) continue;	/* one profile, one tile */
			list.push({ kind: d.tr06 ? "tr06" : "dev", id: d.tr06 ? "tr06" : "dev:" + d.name, name: d.name, on: d.on, state: d.on ? "enabled" : "not enabled" });
		}
		if (!list.some(t => t.kind === "tr06") && isOn()) list.push({ kind: "tr06", id: "tr06", name: "TR-06", state: "not connected" });
		/* the mapping matrix stays reachable without another controller */
		if (!list.some(t => t.kind === "dev")) list.push({ kind: "dev", id: "dev:all", name: "MIDI Learn", state: inputs.length ? "every MIDI input" : "no MIDI input found" });
		return list;
	}
	function tileHtml(t, n) {
		const tr = t.kind === "tr06";
		const state = [t.state ? esc(t.state.toUpperCase()) : "", tr ? ledHtml() + "PROFILE " + (isOn() ? "ON" : "OFF") : ""].filter(Boolean).join(" · ");
		return `<button class="ctltile${t.on === false ? " dis" : ""}" type="button" id="ctl-tile-${n}" data-ctl="tile" data-view="${esc(t.id)}" data-kind="${t.kind}"
			title="${esc(tr ? "Roland TR-06: the controller profile, its MIDI monitor and mapping" : "MIDI Learn: map this controller's CCs to the machine's parameters")}">
			${tr ? ICON_TR06 : ICON_MIDI}<span class="tname">${esc(t.name)}</span><span class="tstate">${state}</span></button>`;
	}
	function devicesHtml() {
		const named = doc && doc.inputs != null;
		return `<div class="ctlhead"><h3>MIDI devices</h3><span class="note">${named ? "The MIDI inputs of AUDIO / MIDI. Open one to set it up." : doc ? "In a DAW the host owns MIDI: route the controller to this track." : "Waiting for the editor…"}</span></div>
			<div class="ctltiles">${tiles().map(tileHtml).join("")}</div>`;
	}
	const backHtml = title => `<div class="ctlhead"><button class="amkey" type="button" id="ctl-back" data-ctl="back" title="The MIDI devices">‹ DEVICES</button><h3>${title}</h3>`;

	/* ---- the TR-06's view ---- */
	function elsewhereHtml() {
		const a = doc && doc.activity; if (!a || a.elsewhere == null) return "";
		const x = a.elsewhere;
		const text = doc.device ? `TR-06 is sending on CH ${x} — set CHANNEL to ${x}` : `MIDI in on CH ${x}, nothing on CH ${doc.channel}: if that is the TR-06, set CHANNEL to ${x}`;
		return `<p class="ctlelse" role="status">${esc(text)} <button class="amkey" type="button" data-ctl="setch" data-ch="${x}">SET CH ${x}</button></p>`;
	}
	const hintHtml = () => found() ? `<p class="ctlhint" role="status">${esc(doc.device)} connected — turn the profile on <button class="amkey" type="button" data-ctl="use">USE TR-06</button></p>` : "";
	/* the notes the voices on the selected track play (MM), with where NOTE moved them from */
	function trackNotes() {
		const vs = doc.voices.filter(v => v.t === doc.selected && v.note != null);
		if (!vs.length) return "(no voice on it)";
		return vs.map(v => (noteFrom[v.voice] != null && noteFrom[v.voice] !== v.note ? noteFrom[v.voice] + "→" : "") + v.note).filter((x, i, a) => a.indexOf(x) === i).join(", ");
	}
	/* what a knob moves on the selected track: "→ T3 SYN H · TUNE", "→ NOTE T1 60→62", "not mapped" */
	function moves(k) {
		if (!k || k.i < 0) return "not mapped";
		const t = "T" + (doc.selected + 1);
		if (isNote(k) && doc.machine === "mm") return "→ NOTE " + t + " " + trackNotes();
		return "→ " + t + " " + named(k);
	}
	function monitorHtml() {
		const a = doc && doc.activity;
		if (!a) return `<p class="note">Waiting for what arrives…</p>`;
		const label = cc => { const k = doc.knobs.find(x => x.cc === cc); return k ? k.label : cc === 120 ? "ALL SOUND OFF" : cc === 121 ? "RESET CONTROLLERS" : ""; };
		const notes = (a.notes || []).map(n => {
			const v = n.ch === doc.channel ? doc.voices.find(x => x.notes.includes(n.n)) : null;
			return (n.ch === doc.channel ? "" : "CH " + n.ch + " ") + "NOTE " + n.n + " · " + n.v + (v ? " (" + v.voice + ")" : "");
		});
		const rows = (a.ccs || []).map(c => {
			const mine = c.ch === doc.channel, k = mine ? doc.knobs.find(x => x.cc === c.cc) : null;
			const what = !mine ? "not the TR-06's channel (" + doc.channel + ")" : c.cc === 120 || c.cc === 121 ? "blocked" : k ? moves(k) : "not a TR-06 knob";
			return `<tr class="${mine && isOn() ? "" : "off"}" data-ch="${c.ch}" data-cc="${c.cc}"><td class="mono">${mine ? "" : "CH " + c.ch}</td><td class="mono">CC ${c.cc}</td><td class="mono v">${c.v}</td><td>${esc(label(c.cc))}</td><td>${esc(what)}</td></tr>`;
		}).join("");
		return `<p class="notes" id="ctl-notes">${notes.length ? esc(notes.join("  ·  ")) : "No notes yet."}</p>` + (rows ? `<table><tr><th>CH</th><th>CC</th><th>Value</th><th>TR-06</th><th>Moves</th></tr>${rows}</table>`
			: `<p class="note">No CC yet: turn a knob on the TR-06.${isOn() ? "" : " With the profile off the machine gets it as ordinary MIDI."}</p>`);
	}
	function tr06Html() {
		if (!doc) return backHtml("TR-06") + `<span class="note">Waiting for the editor…</span></div>`;
		const on = isOn(), mm = doc.machine === "mm", sel = "track " + (doc.selected + 1) + (doc.machineName ? " (" + doc.machineName + ")" : "");
		const voices = doc.voices.map(v => `<tr class="${on ? "" : "off"}" data-voice="${esc(v.voice)}"><td><b>${esc(v.voice)}</b> ${esc(v.label)}</td><td class="mono">${v.notes.join(", ")}</td>
			<td><select id="ctl-voice-${esc(v.voice)}" data-ctl="voice" data-voice="${esc(v.voice)}" aria-label="${esc(v.voice)} track">${opts(tracks(), v.t)}</select></td>
			${mm ? `<td><input id="ctl-note-${esc(v.voice)}" type="number" min="0" max="127" value="${v.note}" data-ctl="note" data-voice="${esc(v.voice)}" data-t="${v.t}" aria-label="${esc(v.voice)} note"></td>` : ""}
			<td class="mono">${v.out ? "CH " + v.out.ch + " · " + v.out.note : "—"}</td></tr>`).join("");
		const knobs = doc.knobs.map(k => `<tr class="${on && k.i >= 0 ? "" : "off"}" data-knob="${k.cc}"><td>${esc(k.label)}</td><td class="mono">CC ${k.cc}</td>
			<td><select id="ctl-knob-${k.cc}" data-ctl="knob" data-cc="${k.cc}" aria-label="${esc(k.label)} target">${opts(targetList(), targetKey(k))}</select></td><td>${esc(moves(k))}</td></tr>`).join("");
		const about = on ? "The TR-06 plays the machine on channel " + doc.channel + ". Its knobs move " + sel + "." : "Off: MIDI in reaches the machine as it always did. The monitor still shows what arrives.";
		return backHtml(esc(doc.profiles.find(p => p.id === "tr06")?.label || "TR-06")) + `${ledHtml()}<span class="note">${esc(about)}</span></div>
		<section class="card ctlset"><span class="ilab">Profile</span><select id="ctl-profile" data-ctl="profile" aria-label="Controller profile">${opts(doc.profiles.map(p => [p.id, p.label]), doc.profile)}</select>
			<span class="ilab">Channel</span><select id="ctl-channel" data-ctl="channel" aria-label="Controller channel">${channelOpts()}</select>
			<span class="ilab">Knobs</span><select id="ctl-knobmode" data-ctl="knobmode" aria-label="How the knobs apply" title="Relative: a turn moves the value from where it is. Absolute: the value jumps to the knob's position.">${opts([["relative", "Relative"], ["absolute", "Absolute"]], doc.knobMode || "relative")}</select>
			<button class="amkey" type="button" data-ctl="reset" title="The TR-06's shipped mapping and channel 10; the profile stays on or off">RESET TO DEFAULTS</button></section>
		${hintHtml()}<div class="ctlactw">${elsewhereHtml()}</div>
		${doc.warning ? `<p class="ctlwarn" role="alert">${esc(doc.warning)}</p>` : ""}
		${on && !doc.known ? `<p class="note">Waiting for the machine's global settings: the voices play once the editor has read them.</p>` : ""}
		<div class="ctlgrid">
			<section class="card ctlmon"><header><h3>MIDI monitor</h3><span>what arrives · moves ${esc(sel)}</span><button class="amkey" type="button" id="ctl-clear" data-ctl="clear" title="Empty the monitor">CLEAR</button></header><div class="ctlscroll" id="ctlmon">${monitorHtml()}</div></section>
			<section class="card"><header><h3>Voices</h3><span>notes → tracks</span></header><div class="ctlscroll"><table><tr><th>TR-06</th><th>Notes</th><th>Track</th>${mm ? "<th>Note</th>" : ""}<th>Plays</th></tr>${voices}</table></div></section>
			<section class="card"><header><h3>Knobs</h3><span>on the selected track</span></header><div class="ctlscroll" id="ctlknobs"><table><tr><th>TR-06</th><th>CC</th><th>Target</th><th>Moves</th></tr>${knobs}</table></div></section>
		</div>
		<p class="note">${esc(doc.about)} · ${doc.named ? "Turn each knob: the monitor shows which CC it sends and what it moves" : "In a DAW, route the TR-06's MIDI to this track"} · set the TR-06's Soft Thru off.</p>`;
	}

	/* ---- the workspace ---- */
	function inner(learnHtml) {
		if (view === "tr06") return tr06Html();
		if (view.startsWith("dev:")) {
			const name = view === "dev:host" ? "Host MIDI in" : view === "dev:all" ? "MIDI Learn" : view.slice(4);
			return backHtml(esc(name)) + `<span class="note">MIDI Learn: CCs from every MIDI input map to the machine's parameters.</span></div>${learnHtml || ""}`;
		}
		return devicesHtml();
	}
	/* what a redraw of the view depends on (the monitor and the other-channel line update in place) */
	const keyOf = () => view === "tr06" ? "tr06:" + JSON.stringify(doc ? { ...doc, activity: !!doc.activity, last: null } : null) + JSON.stringify(noteFrom)
		: view === "" ? "dev:" + JSON.stringify(tiles().map(t => [t.id, t.name, t.state, t.on])) + isOn() + !!(doc && doc.device) + !!doc : view;
	function controlHtml(learnHtml) {
		drawnKey = keyOf();
		return `<div class="ctlroot" id="ctlroot" data-view="${esc(view || "devices")}">${inner(learnHtml)}</div>`;
	}
	/* a new document while the workspace shows: the devices or the TR-06's view again, or the monitor in place */
	function sync() {
		const root = document.getElementById("ctlroot"); if (!root || view.startsWith("dev:")) return;
		if (keyOf() !== drawnKey) {
			drawnKey = keyOf(); root.innerHTML = inner("");
			if (typeof enhanceSelects === "function") enhanceSelects(root);
			return;
		}
		const m = document.getElementById("ctlmon"); if (m && doc) m.innerHTML = monitorHtml();
		const w = root.querySelector(".ctlactw"); if (w) w.innerHTML = elsewhereHtml();
	}
	function onDoc(d) {
		if (doc && d) for (const v of d.voices) { const b = doc.voices.find(x => x.voice === v.voice); if (b && b.note != null && b.note !== v.note) noteFrom[v.voice] = b.note; }
		doc = d;
		sync();
	}
	/* whether the plug-in should report what arrives: while the TR-06's view shows */
	function setWatch() {
		const w = shown && view === "tr06";
		if (w === watching) return;
		watching = w;
		if (typeof host.watch === "function") host.watch({ on: w });
	}
	function show(v) {
		view = v || "";
		if (typeof api.rerender === "function") api.rerender();
		setWatch();
	}
	/* a control changed: which host call, with what (pure: the page test checks it) */
	function intent(d, k, value, data) {
		const mm = d.machine === "mm";
		if (k === "profile") return ["set", { profile: value }];
		if (k === "use") return ["set", { profile: "tr06" }];
		if (k === "channel") return ["set", { channel: +value }];
		if (k === "setch") return ["set", { channel: +data.ch }];
		if (k === "knobmode") return ["set", { knobMode: value === "absolute" ? "absolute" : "relative" }];
		if (k === "clear") return ["clear", {}];
		if (k === "voice") { const v = d.voices.find(x => x.voice === data.voice); return ["voice", mm ? { voice: data.voice, t: +value, note: v ? v.note : 60 } : { voice: data.voice, t: +value }]; }
		if (k === "note") return ["voice", { voice: data.voice, t: +data.t, note: Math.max(0, Math.min(127, Math.round(+value || 0))) }];
		if (k === "knob") {
			const [pg, i] = String(value).split(":").map(Number);
			if (i < 0) return ["knob", { cc: +data.cc, i: null }];
			return ["knob", mm || pg === 8 ? { cc: +data.cc, pg: Math.max(0, pg), i } : { cc: +data.cc, i }];
		}
		return null;
	}
	/* the host call for a control's change */
	function change(k, value, data) {
		const it = doc && intent(doc, k, value, data);
		if (it && typeof host[it[0]] === "function") host[it[0]](it[1]);
	}
	function click(el) {
		const k = el.dataset.ctl;
		if (k === "tile") { show(el.dataset.view); return true; }
		if (k === "back") { show(""); return true; }
		if (k === "reset") { if (typeof host.reset === "function") host.reset(); return true; }
		if (k === "use" || k === "setch" || k === "clear") { change(k, "", el.dataset); return true; }
		return false;
	}
	/* the workspace lives in the page's markup (rendered again with it): listened to at the document */
	document.addEventListener("change", e => { const el = e.target.closest && e.target.closest("#ctlroot [data-ctl]"); if (el && el.tagName !== "BUTTON") change(el.dataset.ctl, el.value, el.dataset); });
	document.addEventListener("click", e => { const el = e.target.closest && e.target.closest("#ctlroot button[data-ctl]"); if (el) click(el); });
	const api = {
		get host() { return host; }, set host(_h) { host = _h || {}; watching = false; setWatch(); },
		rerender: null,
		onDoc, intent, change, click, controlHtml, show, tiles, monitorHtml, doc: () => doc, view: () => view,
		/* after every render of the page: whether the workspace shows now */
		rendered() { shown = !!document.getElementById("ctlroot"); setWatch(); }
	};
	return api;
})();
