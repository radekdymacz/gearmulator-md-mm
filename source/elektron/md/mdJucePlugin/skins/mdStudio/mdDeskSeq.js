"use strict";
/* Sequence: the step grid, its ruler and page control, the lock lane and its parameter keys. The grid's and the
   lane's pointer gestures (paint, draw, ramp, the wheel) are mdDeskGestures.js; the GEN bar mdDeskGenUi.js. */

/* ===== Sequence ===== */
function pages16() { return V.len / 16; }
function vis() { if (S.viewAll) return [0, V.len]; S.page = Math.min(S.page, pages16() - 1); return [S.page * 16, S.page * 16 + 16]; }
function steps() { const [a, b] = vis(); return Array.from({ length: b - a }, (_, k) => a + k); }
/* Mockup v51: ALL fits every step (minmax 0, a 2 px gap, class "viewall" on html); the page
   boundary is a 2 px shadow in the cell gap, so the lock lane lines up with the steps. */
function cols() { return S.viewAll ? `repeat(${steps().length},minmax(0,1fr))` : `repeat(${steps().length},minmax(18px,1fr))`; }
function stepCls(i, s) {
	const t = V.tracks[i], c = ["st"]; if (s % 4 === 0) c.push("q"); if (s % 16 === 0 && s !== vis()[0]) c.push("gap"); if (s >= V.length) c.push("past");
	if (t.trigs[s]) { c.push("on"); if (t.acc.has(s)) c.push("acc"); if (t.slide.has(s)) c.push("sl"); if (stepLocked(i, s)) c.push("lk"); }
	if (inSel(i, s)) c.push("selx");
	if (V.playing && s === S.step) c.push("ph"); return c.join(" ");
}
/* The PAGE control sits on the right, above the grid, on the ruler row. */
/* the step gestures, behind a small ? key at the right of the bar under the grid (a click: the list of keys) */
function stepLegend() {
	const row = (cls, what, how) => `<span>${cls != null ? `<i class="lg on ${cls}"></i>` : `<i class="lg none"></i>`}<b>${what}</b>${how}</span>`;
	return `<span class="steplegend"><button class="glegkey" id="steplegend" aria-label="Step gestures" aria-describedby="steplegpop">?</button><span class="legend glegpop" id="steplegpop" role="tooltip">${row("", "Trig", "click")}${row("acc", "Accent", "shift-click" + (V.accAll ? " (all)" : ""))}${row("sl", "Slide", "alt-shift-click" + (V.slideAll ? " (all)" : ""))}${row("lk", "Has locks", "a lock on the step")}${row(null, "Fill", "⌘-click: every 2nd step from there to the end comes on (from a trig: off); ⌘⇧-click: every 4th")}${row(null, "Select", "alt-click a step, alt-drag steps, or drag in the step numbers; then ⌘C ⌘X ⌘V ⌘D, Delete, Esc. Alt-drag the selection: a copy")}<small>? the list of keys</small></span></span>`;
}
function pageCtl() { return `<span class="pagectl seqpage"><button class="pgkey" id="pgkey" ${pages16() < 2 ? "disabled" : ""} title="Next page. Shift-click = previous. Keys [ and ].">Page</button><span class="pleds" aria-hidden="true">${[0, 1, 2, 3].map(k => `<span class="pl ${k < pages16() ? "" : "na"} ${!S.viewAll && k === S.page ? "cur" : ""}" data-plp="${k}"><i class="led"></i></span>`).join("")}</span><button class="ptog ${S.viewAll ? "on" : ""}" id="pgall" aria-pressed="${S.viewAll}" title="Show all steps"><i class="led"></i>All</button><button class="ptog ${S.follow ? "on" : ""}" id="pgfollow" aria-pressed="${S.follow}" title="Page follows the play position"><i class="led"></i>Fol</button></span>`; }
function renderSeq() {
	document.documentElement.classList.toggle("viewall", !!S.viewAll);
	let h = `<div class="panel ${V.mode === "CLASSIC" ? "classic" : ""}" id="seqp"><div class="scroll" id="seqscroll"><div class="seq" id="seq">
  <div class="r" style="grid-template-columns:${cols()}">${steps().map(s => `<div class="rul ${s % 16 === 0 && s !== vis()[0] ? "gap" : ""}${S.stepSel && s >= S.stepSel.from && s < S.stepSel.to ? " selx" : ""}" data-s="${s}">${s % 4 === 0 ? s + 1 : ""}</div>`).join("")}</div>`;
	V.tracks.forEach((t, i) => { h += `<div class="r ${i === S.sel ? "sel" : ""} ${audible(i) ? "" : "off"}" data-row="${i}" style="grid-template-columns:${cols()};--c:${FAMC[t.fam]}">${steps().map(s => `<button class="${stepCls(i, s)}" data-t="${i}" data-s="${s}" aria-label="Track ${i + 1} step ${s + 1}" aria-pressed="${t.trigs[s]}"></button>`).join("")}</div>`; });
	h += `</div></div><div class="genbar"><div class="genband" id="genband">${genStripHtml()}</div><span class="gdiv" aria-hidden="true"></span>${stepLegend()}${pageCtl()}</div><div class="lanewrap"><div class="lanetop"><span class="cap">Lock lane · ${S.sel + 1} ${V.tracks[S.sel].name} · <b id="lanename">${laneLabel(V.tracks[S.sel].m, S.lane, Cat)}</b> <span class="lanescale">${bipLane() ? "L 64 · centre · R 63" : "0–127"}</span></span><span class="lockbudget" id="lockbudget"></span>${V.mode === "CLASSIC" ? `<span class="warnline" title="Locks stay in the pattern but do nothing until you switch to EXTENDED.">CLASSIC: locks muted</span>` : ""}<span class="lanehelp" title="Draw across the bars to lock this parameter per step. Alt-drag erases. Shift-drag draws a ramp, a straight line from where you press to where you let go. The wheel over a step with a trig moves its lock (Shift: fine). Hatched steps have no trig, so they cannot hold a lock. Dashed line = kit value.">Draw to lock · ⇧ ramp · alt erases</span></div>
</div>
  <div class="scroll" id="lanescroll"><div class="lane" id="lane" style="grid-template-columns:${cols()}"></div></div></div>`;
	$("#main").innerHTML = h; renderLane(); syncScroll();
}
/* Mockup v57: bipolar lanes draw from the centre (64): up = right / boost / louder, down = left / cut.
   The machine's signed parameters (manual: displayed -64..+63): PAN, EQG, the RAM-R levels and balances
   MLEV MBAL ILEV IBAL, and the master EQ gains LG HG PG (CTR machines). */
const BIP = SIGNED;
function bipLane() { return BIP.has(S.lane); }
function barHTML(v) { return bipLane() ? (v >= 64 ? `<i class="bp up" style="height:${(v - 64) / 63 * 50}%"></i>` : `<i class="bp dn" style="height:${(64 - v) / 64 * 50}%"></i>`) : `<i style="--f:${(v / 127).toFixed(4)}"></i>`; }
function renderLane() {
	const lane = $("#lane"); if (!lane) return;
	const t = S.sel, tr = V.tracks[t], m = V.locks.get(lk(t, S.lane)), g = grp(t, S.lane), base = g[S.lane] ?? 0;
	altLabels(); syncLockBudget();
	const pg = pages(tr.m, Cat); $("#chips").innerHTML = [["Synth", pg.s], ["Effects", pg.e], ["Routing", pg.r]].map(([lab, ps]) => {
		return `<span class="plab">${lab}</span>` + ps.map(p => {
			if (!p) return `<span class="pk empty"></span>`; const n = V.locks.get(lk(t, p))?.size || 0;
			return `<button class="pk ${n ? "has" : ""}" data-lane="${p}" aria-pressed="${p === S.lane}" title="${n ? n + " locked step" + (n > 1 ? "s" : "") : "No locks yet"}">${laneLabel(tr.m, p, Cat)}${n ? `<i>${n}</i>` : ""}</button>`;
		}).join("");
	}).join("");
	lane.innerHTML = steps().map(s => {
		const on = tr.trigs[s], v = m?.get(s);
		return `<div class="lb ${on ? "" : "none"} ${s % 16 === 0 && s !== vis()[0] ? "gap" : ""} ${V.playing && s === S.step ? "ph" : ""}" data-s="${s}">${on ? `<div class="base" style="--bf:${(base / 127).toFixed(4)}"></div>${bipLane() ? `<div class="mid"></div>` : ""}${v != null ? barHTML(v) : ""}` : ""}</div>`;
	}).join("");
}
function syncScroll() { const a = $("#seqscroll"), b = $("#lanescroll"); if (!a || !b) return; a.onscroll = () => { b.scrollLeft = a.scrollLeft; }; b.onscroll = () => { a.scrollLeft = b.scrollLeft; }; }
function refreshRow(i) { $$(`.st[data-t="${i}"]`).forEach(b => { const s = +b.dataset.s; b.className = stepCls(i, s); b.setAttribute("aria-pressed", V.tracks[i].trigs[s]); }); }


/* the Sequence's clicks (the router's, mdDeskRender.js CLICKS): true when the click was theirs */
function clickSteps(e) {
	const st = e.target.closest("#seq .st"); if (!st) return false;
	const i = +st.dataset.t, s = +st.dataset.s, t = V.tracks[i];
	if (!V.loaded) { toast("The pattern is not loaded yet."); return true; }
	/* Live recording: a click plays the track like its TRIG key; the machine records it. */
	if (V.rec) { cmd("recTrig", { t: i }); if (i !== S.sel) select(i); return true; }
	if (e.metaKey || e.ctrlKey) { fillEvery(i, s, e.shiftKey ? 4 : 2); return true; }
	/* a plain mouse click was the paint gesture's (pointerdown); the keyboard's click toggles here */
	if (!e.shiftKey && !e.altKey && e.detail > 0) return true;
	/* ⌥-click selects the step (DESIGN-step-selection.md): the pointer's select gesture did it; the keyboard's click here */
	if (e.altKey && !e.shiftKey) { if (e.detail === 0) setSel({ t: i, n: 1, from: s, to: s + 1 }); return true; }
	if (e.altKey && t.trigs[s]) cmd("slide", { p: V.pat, t: i, s }, undefined, [[["tracks", i, "slide", s], !t.slide.has(s)]]);
	else if (e.shiftKey && t.trigs[s]) cmd("accent", { p: V.pat, t: i, s }, undefined, [[["tracks", i, "acc", s], !t.acc.has(s)]]);
	else {
		const on = !t.trigs[s], w = [[["tracks", i, "trigs", s], on]];
		if (!on) w.push([["tracks", i, "acc", s], false], [["tracks", i, "slide", s], false], ...clearStep(i, s));
		cmd("trig", { p: V.pat, t: i, s, on }, undefined, w);
		if (!on) renderTop();
	}
	refreshRow(i); if (i !== S.sel) select(i); else renderLane(); return true;
}
function clickLane(e) {
	const ch = e.target.closest("[data-lane]"); if (ch) { S.lane = ch.dataset.lane; render(); return true; }
	if (e.target.closest("#clearLane") && e.altKey) { clearTrackLocks(S.sel); return true; }
	if (e.target.closest("#clearLane")) { const i = pidx(S.sel, S.lane); if (i >= 0) cmd("clearLane", { p: V.pat, t: S.sel, i }, undefined, [[["locks", lk(S.sel, S.lane)], DELETE]]); renderTop(); refreshRow(S.sel); renderLane(); return true; }
	return false;
}
/* the page control (Sequence and the Sampler's RAM steps) and the step gestures' ? key */
function clickPage(e) {
	if (e.target.closest("#steplegend")) { toggleKeys(true); return true; }
	const pgk2 = e.target.closest("#pgkey"); if (pgk2 && !pgk2.disabled) { const n = pages16(); S.viewAll = false; S.page = (S.page + (e.shiftKey ? -1 : 1) + n) % n; render(); return true; }
	const plp = e.target.closest(".pl[data-plp]"); if (plp && !plp.classList.contains("na")) { S.page = +plp.dataset.plp; S.viewAll = false; render(); return true; }
	if (e.target.closest("#pgall")) { S.viewAll = !S.viewAll; render(); return true; }
	if (e.target.closest("#pgfollow")) { S.follow = !S.follow; render(); return true; }
	return false;
}
