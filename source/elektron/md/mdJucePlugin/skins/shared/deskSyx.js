"use strict";
/* SysEx import and export (P7, B-019), one file for both editors and both mockups (skins/shared/).
   The host opens, parses and writes the files (the page never reads their bytes). A .syx chosen with
   "Import SysEx…" comes back as a preview, here a panel of the modal layer. The preview informs; it is no gate.
   "Import" sends the chosen messages to the machine as they are, like a MIDI cable (the firmware decides what it
   takes), then the host reads every document back and reports per item what the machine did. No Undo: Export
   SysEx… first keeps a way back.
   The panel (0.3.5): a tab per kind in the file (Kits, Patterns, Songs, Globals, Other), each with its LED (send this
   kind or not) and its counts; the tab shows the kind as the machine's own slot grid (patterns A-H x 16, kits 4 or
   8 x 16, songs 2 x 16, globals 1-8), a slot that holds data on the machine with a lit dot, the one that plays now
   cream with ▶. A click on a slot leaves it out or takes it again, Shift-click a range. Three states, one footer:
   preview (Cancel, Import n), importing (the phases and a bar; Stop, Hide), report (per slot what the machine did,
   the problems as a list; Done only).
     Syx.keys()            the library's two keys (markup)
     Syx.preview(m)        {type:"syxPreview", ok, text, file, model, fullBackup, items:{kind:[{slot,name,kit?,overwrites,plays,format,readable}]},
                            skipped, skippedCount, messages}
     Syx.progress(m)       {type:"syxProgress", phase, done, total, running, text, report?:{taken, converted, ignored, differs,
                            changed, unknown, "no reply", commands, unsent, items:[{kind, slot, name, outcome, text}]}}
     Syx.exported(m)       {type:"syxExport", ok, text}
     Syx.host = {choose(), exportAll(), start(kinds, skip), stop()}   the app's host calls
     Syx.model             the panel's pure part, as data (deskSyxTest.js) */
const Syx = (() => {
	/* ---- the pure part: the file's items in, what the panel shows out ---- */
	const KINDS = [["kit", "Kits"], ["pattern", "Patterns"], ["song", "Songs"], ["global", "Globals"], ["other", "Other"]];
	const OFF = { global: true, other: true };	// settings and commands: off unless ticked
	const nn = n => String(n).padStart(2, "0");
	const key = (k, slot) => k + ":" + slot;
	function slotLabel(k, slot) {
		if (k === "pattern") return "ABCDEFGH"[slot >> 4] + nn((slot & 15) + 1);
		return ({ kit: "K", song: "S", global: "G", other: "#" })[k] + (k === "global" || k === "other" ? slot + 1 : nn(slot + 1));
	}
	/* the machine's slots of a kind: kits 64 (MD) or 128 (MM), patterns 128, songs 32 (MD) or 24 (MM, its manual 1-73),
	   globals 8; more when a file holds more. Columns: patterns 16 (a bank a row, as the pattern palette); kits, songs and
	   globals 8, so a kit's name is whole (the MM's 128 kits scroll) */
	const columns = k => k === "pattern" ? 16 : 8;
	function capacity(model, k, list) {
		const top = list.reduce((a, i) => Math.max(a, i.slot + 1), 0), mm = /mono/i.test(model || "");
		const base = { kit: mm ? 128 : 64, pattern: 128, song: mm ? 24 : 32, global: 8 }[k] || 0;
		return Math.max(base, Math.ceil(top / columns(k)) * columns(k));
	}
	/* rows of slots as on the machine: {cols, rows:[{label, cells:[{slot, label, item|null}]}]}; "other" has no slots */
	function layout(model, k, list) {
		if (k === "other") return { cols: 0, rows: [{ label: "", cells: list.map(i => ({ slot: i.slot, label: slotLabel(k, i.slot), item: i })) }] };
		const n = capacity(model, k, list), by = new Map(list.map(i => [i.slot, i])), cols = columns(k), rows = [];
		for (let r = 0; r * cols < n; r++) {
			const cells = [];
			for (let c = 0; c < cols; c++) { const slot = r * cols + c; cells.push({ slot, label: slotLabel(k, slot), item: by.get(slot) || null }); }
			rows.push({ label: k === "pattern" ? "ABCDEFGH"[r] || String(r + 1) : k === "global" ? "" : nn(r * cols + 1), cells });
		}
		return { cols, rows };
	}
	/* a kind's counts: in the file, chosen, overwriting, into empty slots, playing now, left out */
	function kindSums(k, list, skip, on) {
		const inn = on ? list.filter(i => !skip.has(key(k, i.slot))) : [];
		return { total: list.length, n: inn.length, over: inn.filter(i => i.overwrites).length, fresh: inn.filter(i => !i.overwrites).length,
			plays: inn.filter(i => i.plays).length, out: on ? list.length - inn.length : list.length };
	}
	/* Shift-click: every item of the kind between the anchor and the clicked slot (by slot) takes the clicked one's new state */
	function toggleRange(k, list, skip, anchor, slot) {
		const next = new Set(skip), out = !skip.has(key(k, slot));
		const lo = Math.min(anchor ?? slot, slot), hi = Math.max(anchor ?? slot, slot);
		for (const i of list) if (i.slot >= lo && i.slot <= hi) { if (out) next.add(key(k, i.slot)); else next.delete(key(k, i.slot)); }
		return next;
	}
	/* per chosen item what the machine did: the report lists every item not taken as in the file; the rest were taken,
	   unless the counts do not add up (stopped, or more problems than the report lists): then "unclear" */
	function outcomes(picked, report) {
		const out = new Map(), listed = new Map((report?.items || []).map(i => [key(i.kind, i.slot), i]));
		const slots = picked.filter(p => p.kind !== "other");
		const whole = (report?.taken || 0) + listed.size === slots.length;
		for (const p of picked) {
			const id = key(p.kind, p.slot), l = listed.get(id);
			out.set(id, p.kind === "other" ? { outcome: "sent" } : l ? { outcome: l.outcome, text: l.text } : { outcome: whole ? "taken" : "unclear" });
		}
		return out;
	}
	const OUTCOME = { taken: ["taken", "✓"], converted: ["converted", "≈"], ignored: ["ignored", "✕"], differs: ["different", "≠"], changed: ["not comparable", "~"],
		unknown: ["not comparable", "~"], "no reply": ["no answer", "?"], unclear: ["not known", "?"], sent: ["sent", "→"] };
	const problem = o => o !== "taken" && o !== "sent";
	/* the footer's one line before the import */
	function footLine(t) {
		if (!t.n) return "Nothing chosen.";
		const s = [];
		if (t.over) s.push(`${t.over} slot${t.over > 1 ? "s" : ""} on the machine overwritten${t.plays ? `, ${t.plays} playing now` : ""}.`);
		else s.push(`${t.n} item${t.n > 1 ? "s" : ""}, into empty slots.`);
		if (t.globals) s.push("Globals change MIDI channels.");
		s.push("No undo: Export SysEx… first.");
		return s.join(" ");
	}
	/* a kind's line while importing, from syxProgress.kinds[kind] {done, total} of the phase: its words and its bar (%) */
	function kindLine(phase, kp, picked) {
		if (!picked) return { text: "not sent", pct: 0 };
		const verb = { before: "reading the slots", send: "sent", read: "read back" }[phase] || "";
		if (!kp || !kp.total) return { text: "waits", pct: 0 };
		const all = kp.done >= kp.total;
		return { text: `${all ? "✓ " : ""}${verb} ${kp.done} of ${kp.total}`, pct: Math.round(kp.done / kp.total * 100) };
	}
	const model = { KINDS, OFF, slotLabel, capacity, layout, kindSums, toggleRange, outcomes, footLine, kindLine, OUTCOME };

	/* ---- the panel ---- */
	const pop = document.createElement("div");
	pop.id = "syxpop"; pop.className = "syxpop libpop"; pop.hidden = true;
	pop.setAttribute("role", "dialog"); pop.setAttribute("aria-label", "Import SysEx");
	document.body.appendChild(pop);
	let last = null, state = "idle", tab = null, anchor = null, problemsOnly = true, steps = [], rep = null, picks = [], res = new Map(), follow = true;
	let skip = new Set();
	const on = {};	// kind -> ticked
	const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
	const q = s => pop.querySelector(s);
	const kindsIn = () => last ? KINDS.filter(([k]) => (last.items[k] || []).length) : [];
	const chosen = () => kindsIn().map(([k]) => k).filter(k => on[k]);
	const picked = () => chosen().flatMap(k => last.items[k].filter(i => !skip.has(key(k, i.slot))).map(i => Object.assign({ kind: k }, i)));
	const totals = () => {
		const s = kindsIn().map(([k]) => kindSums(k, last.items[k], skip, on[k]));
		return { n: s.reduce((a, x) => a + x.n, 0), over: s.reduce((a, x) => a + x.over, 0), plays: s.reduce((a, x) => a + x.plays, 0), globals: !!on.global && kindSums("global", last.items.global || [], skip, true).n > 0 };
	};
	const HELP = "Everything goes into the same slots as in the file, as from a MIDI cable: the machine decides what it takes. Afterwards the editor reads every item back and says what happened. ▶ plays now: the machine takes the dump over it, and unsaved edits of the kit that plays may be lost. Globals (MIDI channels, sync, machine settings) and other messages are off unless you tick them; the active global becomes active at once. There is no Undo: Export SysEx… first keeps a way back.";

	function tip(k, i, out) {
		const t = [slotLabel(k, i.slot) + (i.name && i.name !== slotLabel(k, i.slot) ? " " + i.name : ""), i.kit != null ? "kit " + nn(i.kit + 1) : "", i.format ? "format " + i.format : "",
			i.readable === false ? "the editor cannot read this dump (the machine may)" : "", i.plays ? "plays now" : "", i.overwrites ? "overwrites a slot that holds data" : "into an empty slot"];
		if (state === "preview") t.push(!on[k] ? "tick " + KINDS.find(x => x[0] === k)[1] + " to send these" : out ? "left out: click to take it" : "click to leave it out, Shift-click a range");
		const r = res.get(key(k, i.slot));
		if (r) t.push(r.text || OUTCOME[r.outcome]?.[0] || r.outcome);
		return t.filter(Boolean).join(" · ");
	}
	function cell(k, c) {
		if (!c.item) return `<span class="syxc none"><b>${c.label}</b></span>`;
		const i = c.item, id = key(k, i.slot), out = skip.has(id), r = res.get(id);
		const second = k === "pattern" && (!i.name || i.name === c.label) ? (i.kit != null ? "K" + nn(i.kit + 1) : "") : i.name || "—";
		const cls = ["syxc", i.overwrites ? "over" : "fresh", i.plays ? "plays" : "", out ? "off" : "", i.readable === false ? "unread" : "", r && problem(r.outcome) ? "bad" : ""].filter(Boolean).join(" ");
		const mark = r ? `<i class="syxo">${OUTCOME[r.outcome]?.[1] || "?"}</i>` : "";
		return `<button type="button" class="${cls}" data-syxitem="${esc(id)}" data-syxname="${esc(i.name || "")}"${r ? ` data-syxout="${esc(r.outcome)}"` : ""} title="${esc(tip(k, i, out))}"><b>${i.plays ? "▶ " : ""}${c.label}</b><span>${esc(second)}</span>${mark}</button>`;
	}
	function pane(k) {
		const list = last.items[k], g = layout(last.model, k, list);
		const style = g.cols ? `grid-template-columns:${k === "global" ? "" : "26px "}repeat(${g.cols},minmax(0,1fr))` : "grid-template-columns:repeat(auto-fill,minmax(110px,1fr))";
		const body = g.rows.map(r => (g.cols && k !== "global" ? `<span class="syxrl">${r.label}</span>` : "") + r.cells.map(c => cell(k, c)).join("")).join("");
		const note = k === "global" ? `<p class="syxnote"><b>Globals</b> hold the machine's MIDI channels, sync and settings: importing one changes the MIDI channels the machine listens on, and so which tracks the editor can mute, play and edit live (GLOBAL shows them). Off unless you tick them; the active one becomes active at once.</p>`
			: k === "other" ? `<p class="syxnote">Messages that are no kit, pattern, song or global (commands, sounds): sent as they are when ticked.</p>` : "";
		return `<div class="syxpane${on[k] ? "" : " kindoff"}" data-syxpane="${k}" role="tabpanel"${k === tab ? "" : " hidden"}>${note}<div class="syxgrid${k === "global" ? " g8" : ""}" style="${style}">${body}</div></div>`;
	}
	function tabSub(k) {
		const list = last.items[k];
		if (state === "running") {
			const m = lastProgress || {}, l = kindLine(m.phase, m.kinds?.[k], picks.some(p => p.kind === k));
			return { text: l.text, bad: 0, pct: l.pct, work: m.kind === k };
		}
		if (state === "report") {
			const mine = picks.filter(p => p.kind === k), bad = mine.filter(p => problem(res.get(key(k, p.slot))?.outcome)).length;
			if (!mine.length) return { text: "not sent", bad: 0 };
			if (k === "other") return { text: `${mine.length} sent`, bad: 0 };
			return { text: `${mine.length - bad} taken${bad ? ` · ${bad} problem${bad > 1 ? "s" : ""}` : ""}`, bad };
		}
		const s = kindSums(k, list, skip, on[k]);
		if (!on[k]) return { text: k === "global" ? "off · MIDI settings" : "off", bad: 0 };
		if (k === "other") return { text: `${s.n} to send${s.out ? ` · ${s.out} out` : ""}`, bad: 0 };
		const t = [s.over ? `<em>${s.over} overwrite</em>` : "", s.fresh ? `${s.fresh} new` : "", s.out ? `${s.out} out` : "", !s.n ? "none chosen" : ""].filter(Boolean).join(" · ");
		return { text: t, html: true, bad: 0, plays: s.plays };
	}
	function tabs() {
		return `<div class="syxtabs" role="tablist">${kindsIn().map(([k, label]) => {
			const s = tabSub(k), lock = state !== "preview";
			return `<div class="syxtab${k === tab ? " sel" : ""}${s.bad ? " bad" : ""}${s.work ? " work" : ""}" data-syxtabk="${k}"><input type="checkbox" data-syxkind="${k}" aria-label="Send ${label}"${on[k] ? " checked" : ""}${lock ? " disabled" : ""}><button type="button" class="syxtabb" role="tab" aria-selected="${k === tab}" data-syxtab="${k}"><span class="t">${label}<span class="syxct">${last.items[k].length}</span><span class="syxpl" title="plays now"${s.plays ? "" : " hidden"}>▶</span></span><span class="s">${s.html ? s.text : esc(s.text)}</span>${state === "running" && picks.some(p => p.kind === k) ? `<i class="syxtb"><b style="width:${s.pct || 0}%"></b></i>` : ""}</button></div>`;
		}).join("")}</div>`;
	}
	function tools() {
		if (state === "report") {
			const bad = [...res.values()].filter(r => problem(r.outcome)).length;
			return `<div class="syxtools"><label class="syxfilt"><input type="checkbox" data-syxgo="problems"${problemsOnly && bad ? " checked" : ""}${bad ? "" : " disabled"}> Show problems only</label>
				<span class="syxleg"><i class="lg">✓</i>taken as in the file <i class="lg bad">≈</i>converted <i class="lg bad">✕</i>ignored <i class="lg bad">≠</i>different</span></div>`;
		}
		return `<div class="syxtools"><span class="syxleg"><i class="lg over"></i>overwrites data <i class="lg"></i>empty slot <i class="lg plays">▶</i>plays now <i class="lg off"></i>left out</span>
			<span class="syxsel">${state === "preview" ? `<button type="button" data-syxsel="all">All</button><button type="button" data-syxsel="fresh">Empty slots only</button><button type="button" data-syxsel="none">None</button>` : ""}</span></div>`;
	}
	function problems() {
		const rows = picks.map(p => ({ p, r: res.get(key(p.kind, p.slot)) })).filter(x => x.r && problem(x.r.outcome));
		if (!rows.length) return `<p class="syxnote">Every item as in the file.</p>`;
		return `<div class="syxlist">${rows.map(({ p, r }) => `<div class="syxli" data-syxitem="${esc(key(p.kind, p.slot))}" data-syxout="${esc(r.outcome)}"><b>${slotLabel(p.kind, p.slot)}</b><span class="nm">${esc(p.name || "")}</span><span class="tag">${esc(OUTCOME[r.outcome]?.[0] || r.outcome)}</span><span class="why">${esc(r.text || "")}</span></div>`).join("")}</div>`;
	}
	function foot() {
		if (state === "report") return `<div class="syxfoot"><div class="syxmsg"><p class="syxsum">${esc(rep.text || "")}</p></div><div class="btnrow"><button type="button" class="go" data-syxgo="close">Done</button></div></div>`;
		if (state === "running") return `<div class="syxfoot"><div class="syxmsg"><div class="syxsteps"></div><div class="syxbar"><i></i><span></span></div></div><div class="btnrow"><button type="button" data-syxgo="close">Hide</button><button type="button" data-syxgo="stop">Stop</button></div></div>`;
		const skipped = last.skippedCount ? `<details class="syxprob"><summary>${last.skippedCount} message${last.skippedCount > 1 ? "s" : ""} cannot be sent</summary>${(last.skipped || []).map(p => `<div>${esc(p)}</div>`).join("")}</details>` : "";
		return `<div class="syxfoot"><div class="syxmsg"><p class="syxwarn"></p>${skipped}</div><div class="btnrow"><button type="button" data-syxgo="close">Cancel</button><button type="button" class="go" data-syxgo="start">Import</button></div></div>`;
	}
	function head() {
		const what = last.ok ? `<span class="lcdchip">${esc(last.model || "")}${last.fullBackup ? " · full backup" : ""}</span>` : "";
		return `<div class="libhead syxhead"><span class="cap">Import SysEx</span>${what}<span class="syxfile" title="${esc(last.file || "")}">${esc(last.file || "")}</span>${last.ok ? `<button type="button" class="libx syxq" data-syxgo="help" aria-expanded="false" title="How the import works">?</button>` : ""}<button type="button" class="libx" data-syxgo="close" title="Close (Esc)">Esc</button></div>
			<p class="syxhelp" hidden>${esc(HELP)}</p>`;
	}
	function draw() {
		if (!last) return;
		for (const st of ["preview", "error", "running", "report"]) pop.classList.toggle("syx-" + st, st === state);
		if (!last.ok) { pop.innerHTML = head() + `<p class="syxwhy">${esc(last.text)}</p><div class="syxfoot"><div class="syxmsg"></div><div class="btnrow"><button type="button" data-syxgo="close">Close</button></div></div>`; return; }
		const list = state === "report" && problemsOnly && [...res.values()].some(r => problem(r.outcome));
		pop.innerHTML = head() + tabs() + `<div class="syxbody">${list ? `<div class="syxpane">${problems()}</div>` : kindsIn().map(([k]) => pane(k)).join("")}</div>` + tools() + foot();
		sums(); bar();
	}
	function sums() {
		if (state !== "preview") return;
		const t = totals(), go = q('[data-syxgo="start"]'), w = q(".syxwarn");
		if (go) { go.textContent = t.n ? `Import ${t.n}` : "Import"; go.disabled = !t.n; go.classList.toggle("danger", !!t.over); }
		if (w) { w.textContent = footLine(t); w.classList.toggle("hot", !!t.over); }
		for (const [k] of kindsIn()) {
			const s = tabSub(k), el = q(`[data-syxtab="${k}"] .s`);
			if (el) el.innerHTML = s.html ? s.text : esc(s.text);
			const pl = q(`[data-syxtab="${k}"] .syxpl`); if (pl) pl.hidden = !s.plays;
			q(`[data-syxpane="${k}"]`)?.classList.toggle("kindoff", !on[k]);
		}
	}
	let lastProgress = null;
	function bar() {
		const m = lastProgress, box = q(".syxsteps");
		if (state !== "running" || !box || !m) return;
		const PH = [["before", "Read the slots"], ["send", "Send"], ["read", "Read back"]].filter(([p]) => p !== "before" || steps.includes("before"));
		const at = PH.findIndex(([p]) => p === m.phase);
		box.innerHTML = PH.map(([p, t], n) => `<span class="syxstep${n < at ? " done" : n === at ? " now" : ""}"><i class="led${n <= at ? " on" : ""}"></i>${t}</span>`).join("");
		const b = q(".syxbar");
		b.querySelector("i").style.width = (m.total ? m.done / m.total * 100 : 0) + "%";
		b.querySelector("span").textContent = m.text || (m.total ? `${m.done} of ${m.total}` : "");
		// per kind (0.3.5: syxProgress.kinds and .kind): each tab's line and bar; the shown tab follows the kind at work
		// until the person picks a tab
		for (const [k] of kindsIn()) {
			const s = tabSub(k), t = q(`[data-syxtabk="${k}"]`); if (!t) continue;
			t.classList.toggle("work", !!s.work);
			t.querySelector(".s").textContent = s.text;
			const w = t.querySelector(".syxtb b"); if (w) w.style.width = (s.pct || 0) + "%";
		}
		if (follow && m.kind && m.kind !== tab && last.items[m.kind]?.length) show(m.kind);
	}
	function show(k) {
		tab = k;
		for (const t of pop.querySelectorAll("[data-syxtabk]")) t.classList.toggle("sel", t.dataset.syxtabk === k);
		for (const p of pop.querySelectorAll("[data-syxpane]")) p.hidden = p.dataset.syxpane !== k;
		for (const b of pop.querySelectorAll("[data-syxtab]")) b.setAttribute("aria-selected", b.dataset.syxtab === k);
	}
	function preview(m) {
		last = m; state = m.ok ? "preview" : "error"; skip = new Set(); anchor = null; res = new Map(); rep = null; picks = []; steps = []; lastProgress = null;
		for (const [k] of KINDS) on[k] = !OFF[k];
		tab = kindsIn()[0]?.[0] || null;
		draw(); pop.hidden = false;
	}
	function progress(m) {
		if (!last || !last.ok) return;
		lastProgress = m;
		if (m.phase && !steps.includes(m.phase)) steps.push(m.phase);
		if (m.phase === "done") {
			rep = { text: m.text, report: m.report };
			res = outcomes(picks, m.report);
			problemsOnly = [...res.values()].some(r => problem(r.outcome));
			state = "report"; draw(); return;
		}
		if (m.running) { if (state !== "running") { state = "running"; draw(); } else bar(); return; }
		if (state === "running") { state = "preview"; draw(); }	// the host refused or ended without a report
	}
	pop.addEventListener("change", e => {
		const k = e.target.dataset?.syxkind;
		if (k && state === "preview") { on[k] = e.target.checked; show(k); sums(); return; }
		if (e.target.dataset?.syxgo === "problems") { problemsOnly = e.target.checked; draw(); }
	});
	function select(how) {
		const list = last.items[tab] || [];
		for (const i of list) { const id = key(tab, i.slot); if (how === "all" || (how === "fresh" && !i.overwrites)) skip.delete(id); else skip.add(id); }
		if (!on[tab] && how !== "none") { on[tab] = true; const b = q(`[data-syxkind="${tab}"]`); if (b) b.checked = true; }
		redrawCells(tab); sums();
	}
	function redrawCells(k) {
		for (const el of pop.querySelectorAll(`[data-syxpane="${k}"] [data-syxitem]`)) {
			const slot = +el.dataset.syxitem.split(":")[1], i = last.items[k].find(x => x.slot === slot), out = skip.has(el.dataset.syxitem);
			el.classList.toggle("off", out); el.title = tip(k, i, out);
		}
	}
	document.addEventListener("click", e => {
		const sk = e.target.closest?.("[data-syx]");
		if (sk && Syx.host) { if (sk.dataset.syx === "import") Syx.host.choose(); else Syx.host.exportAll(); return; }
		if (!pop.contains(e.target)) return;
		const t = e.target.closest("[data-syxtab]");
		if (t) {
			if (state === "running") { follow = false; show(t.dataset.syxtab); return; }
			tab = t.dataset.syxtab; if (state === "report" && problemsOnly) problemsOnly = false; draw(); return;
		}
		const it = e.target.closest("[data-syxitem]");
		if (it && it.tagName === "BUTTON" && state === "preview") {
			const [k, s] = it.dataset.syxitem.split(":"), slot = +s;
			if (!on[k]) return;
			skip = e.shiftKey && anchor != null ? toggleRange(k, last.items[k], skip, anchor, slot) : toggleRange(k, last.items[k], skip, slot, slot);
			anchor = slot; redrawCells(k); sums(); return;
		}
		const sel = e.target.closest("[data-syxsel]");
		if (sel && state === "preview") { select(sel.dataset.syxsel); return; }
		const g = e.target.closest("[data-syxgo]"); if (!g) return;
		const a = g.dataset.syxgo;
		if (a === "start" && Syx.host && state === "preview") {
			follow = true; picks = picked(); const kinds = chosen(), sk2 = [...skip];
			state = "running"; lastProgress = { phase: "send", done: 0, total: 0, running: true, text: "Starting" }; draw();
			Syx.host.start(kinds, sk2);
		}
		else if (a === "stop" && Syx.host) Syx.host.stop();
		else if (a === "help") { const h = q(".syxhelp"); h.hidden = !h.hidden; g.setAttribute("aria-expanded", String(!h.hidden)); }
		else if (a === "close") pop.hidden = true;
	});
	return {
		keys: () => `<span class="syxkeys"><button class="amkey" data-syx="import" title="Open a .syx (a backup, or dumps from any source) and choose what to send to the machine.">Import SysEx…</button><button class="amkey" data-syx="export" title="Every pattern, kit, song and the global the editor holds, as one .syx">Export SysEx…</button></span>`,
		preview, progress, exported: m => m, host: null, model
	};
})();
