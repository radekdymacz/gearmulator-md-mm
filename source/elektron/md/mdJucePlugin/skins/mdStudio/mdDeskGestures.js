"use strict";
/* The pointer gestures (and the two key runs that are one undo step): each handler begins its own kind of the
   one gesture slot (Held, mdDeskApp.js), replaces it as the pointer moves and ends only its own. What a gesture
   edits is the workspaces' (setLock, setV, sendEditor, songCmd...); here is only how the pointer holds it.
   The kinds that hold the page's renders (interacting(), mdDeskRender.js): value, editor, lane, l2, chop, song,
   paint. */

/* ===== The lock lane: draw (Alt erases), Shift: a ramp ===== */
function laneAt(e) {
	const d = Held.as("lane"); if (!d) return;
	if (d.ramp) { rampAt(e); return; }
	const lane = $("#lane"); if (!lane) return; const el = document.elementFromPoint(e.clientX, e.clientY)?.closest(".lb"); if (!el || !lane.contains(el)) return;
	const s = +el.dataset.s, t = S.sel; if (!V.tracks[t].trigs[s]) return; const r = el.getBoundingClientRect(); const v = clamp(Math.round((r.bottom - 3 - e.clientY) / (r.height - 6) * 127));
	if (d.erase) eraseLock(t, S.lane, s); else if (!setLock(t, S.lane, s, v)) return;
	/* One shape for render and drag (mockup's barHTML): a bipolar bar redraws while it is dragged. */
	el.querySelector("i")?.remove(); if (!d.erase) el.insertAdjacentHTML("beforeend", barHTML(v));
	Held.with("lane", { touched: new Set(d.touched).add(s) }); renderTop();
}
function endLaneDraw() { const d = Held.as("lane"); if (!d) return; if (d.ramp) rampSend(); Held.end("lane"); Gesture.end(); refreshRow(S.sel); renderLane(); }

/* ===== Song: a palette pad or an arrangement row dragged onto the grid ===== */
document.addEventListener("dragstart", e => {
	const b = e.target.closest(".scell:not(.empty)"), pk = e.target.closest(".padd");
	let d;
	if (b) { d = Held.begin("song", { item: "row", v: +b.dataset.row }); b.classList.add("dragging"); } else if (pk) { d = Held.begin("song", { item: "pat", v: +pk.dataset.addpat }); pk.classList.add("dragging"); } else return;
	e.dataTransfer.effectAllowed = d.item === "row" ? "move" : "copy"; try { e.dataTransfer.setData("text/plain", String(d.v)); } catch (_) { }
});
document.addEventListener("dragover", e => { if (!Held.as("song")) return; const t = dropTarget(e.target); if (!t) return; e.preventDefault(); showTarget(t); });
document.addEventListener("drop", e => {
	const d = Held.as("song"); if (!d) return; const t = dropTarget(e.target); if (!t) return; e.preventDefault();
	if (d.item === "pat") {
		if (V.song.length >= 256) { toast("A song holds 256 rows."); }
		else if (t.mode === "onto" && !V.song[t.i].type) { rowSet(t.i, { ...V.song[t.i], pat: d.v }); S.songSel = t.i; }
		else { const at = t.mode === "onto" ? t.i : V.song.length - 1; songCmd("rowInsert", { i: at, row: rowToContract({ pat: d.v, rep: 1 }, patLen) }); S.songSel = at; }
	}
	else { const from = d.v; let to = t.mode === "onto" ? t.i : V.song.length - 2; if (from !== to && V.song[from].type !== "end") { songCmd("rowMove", { from, to }); S.songSel = to; } }
	Held.end("song"); render();
});
document.addEventListener("dragend", () => { Held.end("song"); showTarget(null); $$(".dragging").forEach(x => x.classList.remove("dragging")); });

/* ===== Sampler: a chop slice's start, dragged up or down ===== */
document.getElementById("main").addEventListener("pointerdown", e => { const c = e.target.closest("[data-cp].on"); if (!c || e.altKey || e.shiftKey) return; const p = +$("#chop").dataset.p, s = +c.dataset.cp, cur = V.locks.get(lk(p, "STRT"))?.get(s) ?? V.tracks[p].syn.STRT; Held.begin("chop", { c, p, s, y: e.clientY, v: cur, moved: false }); Gesture.begin(); grabPointer(c, e); });
document.getElementById("main").addEventListener("pointermove", e => {
	let d = Held.as("chop"); if (!d) return; if (e.buttons === 0 && e.pointerType === "mouse") { Held.end("chop"); Gesture.end(); return; }
	const dv = Math.round((d.y - e.clientY) / 6) * 8; if (!dv && !d.moved) return; d = Held.with("chop", { moved: true });
	const v = clamp(d.v + dv); if (setLock(d.p, "STRT", d.s, v)) { d.c.innerHTML = chopInner(d.p, d.s); renderTop(); redraw(); }
});
document.addEventListener("pointerup", () => { const d = Held.end("chop"); if (d) { d.c.dataset.moved = d.moved ? "1" : ""; Gesture.end(); } });

/* ===== GEN and MUTATE values: dragged up or down, the wheel ===== */
document.addEventListener("pointerdown", e => { const v = e.target.closest(".gv[data-gv]"); if (!v || e.button !== 0) return; Held.begin("gv", { v, y: e.clientY, k: v.dataset.gv, acc: 0 }); delete v.dataset.dragged; grabPointer(v, e); });
document.addEventListener("pointermove", e => {
	const g = Held.as("gv"); if (!g) return; const d = Math.trunc((g.y - e.clientY) / 6) - g.acc; if (!d) return;
	Held.with("gv", { acc: g.acc + d }); g.v.dataset.dragged = "1"; genVal(g.k, d * (g.k === "dens" || g.k === "amt" || g.k === "racc" ? 2 : 1));
	/* the bar was drawn again: the value's new element */
	const n = document.querySelector(`.gv[data-gv="${g.k}"]`); if (n) { n.dataset.dragged = "1"; Held.with("gv", { v: n }); }
});
document.addEventListener("pointerup", () => { const g = Held.end("gv"); if (!g) return; const k = g.k; setTimeout(() => { const n = document.querySelector(`.gv[data-gv="${k}"]`); if (n) delete n.dataset.dragged; }, 0); });
document.addEventListener("wheel", e => { const v = e.target.closest(".gv[data-gv]"); if (!v) return; e.preventDefault(); genVal(v.dataset.gv, ((e.deltaY || e.deltaX) < 0 ? 1 : -1) * (e.shiftKey ? 10 : 1)); }, { passive: false });

/* ===== Rotate (mdDeskComforts.js): the presses while Alt is down are one run, one undo step ===== */
addEventListener("keyup", e => { if (e.key === "Alt") Held.end("rotate"); }, true);
addEventListener("blur", () => { Held.end("rotate"); });

/* ===== The wheel over a step with a trig moves its lock in the lane's parameter (from the kit value when it has
   none): 4 a notch, Shift 1. A run of notches on one step is one undo step (the slot's "wheel": its step and
   the time of its last notch). Only the vertical wheel (and Shift's sideways one), so a sideways scroll of the
   grid still scrolls. ===== */
$("#main").addEventListener("wheel", e => {
	const st = e.target.closest("#seq .st"); if (!st || S.ws !== "seq") return;
	const t = +st.dataset.t, s = +st.dataset.s, d = e.deltaY || (e.shiftKey ? e.deltaX : 0);
	if (!d || !V.tracks[t].trigs[s] || V.rec || !V.loaded || !params(t).includes(S.lane)) return;
	e.preventDefault();
	if (t !== S.sel) select(t);
	const now = performance.now(), key = t + ":" + s + ":" + S.lane, run = Held.as("wheel");
	const w = !run || run.key !== key || now - run.at > 700 ? Held.begin("wheel", { key, g: Bridge.gesture(), at: now }) : Held.with("wheel", { at: now });
	const had = V.locks.get(lk(t, S.lane))?.get(s), cur = had ?? grp(t, S.lane)[S.lane] ?? 0, v = clamp(cur + (d < 0 ? 1 : -1) * (e.shiftKey ? 1 : 4));
	if (had != null && v === had) return;
	const ok = setLock(t, S.lane, s, v, false, w.g);
	if (!ok) return;
	renderTop(); refreshRow(t); renderLane();
	toast(`${laneLabel(V.tracks[t].m, S.lane, Cat)} ${v} · track ${t + 1}, step ${s + 1}`);
}, { passive: false });

/* ---- a ramp in the lock lane: Shift-drag draws a straight line from the press to the pointer, shown as it moves;
   at the release every step with a trig under it is locked on the line, one gesture (one undo step). */
function lanePoint(e) {
	const cells = $$("#lane .lb"); if (!cells.length) return null;
	const el = cells.find(c => e.clientX < c.getBoundingClientRect().right) || cells[cells.length - 1], r = el.getBoundingClientRect();
	return { s: +el.dataset.s, v: clamp(Math.round((r.bottom - 3 - e.clientY) / (r.height - 6) * 127)) };
}
function rampPoints() { const d = Held.as("lane"), tr = V.tracks[S.sel]; return d && d.from ? genRamp(d.from.s, d.from.v, d.to.s, d.to.v, s => !!tr.trigs[s]) : []; }
function rampAt(e) {
	const p = lanePoint(e); if (!p) return;
	const d = Held.as("lane");
	Held.with("lane", { from: d.from || p, to: p });
	renderLane();
	for (const [s, v] of rampPoints()) {
		const el = document.querySelector(`#lane .lb[data-s="${s}"]`); if (!el) continue;
		el.querySelector("i")?.remove(); el.insertAdjacentHTML("beforeend", barHTML(v)); el.classList.add("ramp");
	}
}
function rampSend() {
	let first = true;
	for (const [s, v] of rampPoints()) { if (!setLock(S.sel, S.lane, s, v, !first)) break; first = false; }
	renderTop();
}

/* ===== LCD line 2: its values dragged up or down (B-006). SWG and ACC move by their unit (a click without a move
   steps them); LEN, SPD and SONG step through their values, one every 12 px, from where the press found them and
   without wrapping (⌥ held at the press: LEN's inner length, as ⌥-click). A click without a move is still the click's
   step (mdDeskTop.js), a drag's own click is not. One drag, one undo step. ===== */
const L2_DRAG = { len: 12, mult: 12, song: 12 };
document.addEventListener("pointerdown", e => {
	const el = e.target.closest(".l2.ed"); if (!el || e.button !== 0) return; const k = el.dataset.l2;
	if (k === "swing" || k === "accAmt") Held.begin("l2", { k, y: e.clientY, v: V[k], moved: false });
	else if (L2_DRAG[k]) Held.begin("l2", { k, y: e.clientY, v: k === "len" ? (e.altKey ? V.length : V.len) : k === "mult" ? V.mult : V.songSlot, alt: e.altKey, moved: false, stepped: true });
	else return;
	Gesture.begin(); grabPointer(el, e); e.preventDefault();
});
/* a stepped value n steps from where the drag began, clamped to its range */
function l2dragTo(l, n) {
	if (l.k === "len" && l.alt) { const v = clamp(l.v + n, 1, V.len); if (v !== V.length) cmd("length", { p: V.pat, v }, "l2length", [[["length"], v]]); return; }
	if (l.k === "len") { const o = [16, 32, 48, 64], v = o[clamp(o.indexOf(l.v) + n, 0, 3)]; if (v !== V.len) cmd("totalLength", { p: V.pat, v }, "l2total", [[["len"], v]]); return; }
	if (l.k === "mult") { const o = Enums().tempoMultipliers; if (!o.length) return; const v = o[clamp(o.indexOf(l.v) + n, 0, o.length - 1)]; if (v !== V.mult) cmd("speed", { p: V.pat, v }, "l2mult", [[["mult"], v]]); return; }
	if (l.k === "song") { const s = clamp(l.v + n, 0, 31); if (s !== l.at) { Held.with("l2", { at: s }); cmd("selectSong", { s }); } }
}
document.addEventListener("pointermove", e => {
	let l = Held.as("l2"); if (!l) return; if (e.buttons === 0 && e.pointerType === "mouse") { Held.end("l2"); Gesture.end(); return; }
	if (l.stepped) {
		const n = Math.trunc((l.y - e.clientY) / L2_DRAG[l.k]); if (!n && !l.moved) return;
		l = Held.with("l2", { moved: true }); const before = [V.len, V.length, V.mult]; l2dragTo(l, n);
		if (before.join() !== [V.len, V.length, V.mult].join()) renderSub();
		return;
	}
	const d = Math.round((l.y - e.clientY) / (l.k === "swing" ? 3 : 6)); if (d) l = Held.with("l2", { moved: true });
	const before = V[l.k]; l2set(l.k, l.v + d); if (V[l.k] !== before) renderSub();
});
document.addEventListener("pointerup", e => {
	const k = Held.end("l2"); if (!k) return; Gesture.end();
	if (k.stepped) { if (k.moved) { const eat = c => { c.stopPropagation(); c.preventDefault(); }; addEventListener("click", eat, { capture: true, once: true }); setTimeout(() => removeEventListener("click", eat, true), 0); render(); } return; }
	if (!k.moved) l2step(k.k, 1);
});

/* ===== Input: a value box or fader ("value"), a curve editor's dot ("editor"), the lock lane ("lane") ===== */
const main = $("#main");
const DRAGS = ["value", "editor", "lane"], dragging = () => DRAGS.some(k => Held.as(k));
main.addEventListener("pointerdown", e => {
	const c = e.target.closest("canvas.ed"); if (c) { const h = nearest(c, e); if (!h) return; Held.begin("editor", { c, k: h.k }); Gesture.begin(); grabPointer(c, e); e.preventDefault(); redraw(); return; }
	const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (el) { Held.begin("value", { el, x: e.clientX, y: e.clientY, v: getV(el), vert: el.classList.contains("fader") }); Gesture.begin(); grabPointer(el, e); el.classList.add("act"); e.preventDefault(); return; }
	const lb = e.target.closest(".lb"); if (lb) { Held.begin("lane", { erase: e.altKey, ramp: e.shiftKey && !e.altKey, touched: new Set() }); Gesture.begin(); grabPointer($("#lane"), e); laneAt(e); e.preventDefault(); }
});
main.addEventListener("pointermove", e => {
	if (e.buttons === 0 && e.pointerType === "mouse" && dragging()) { endDrag(); return; }
	const a = Held.as("editor");
	if (a) { const r = a.c.getBoundingClientRect(), h = ED[a.c.dataset.ed].handles(r.width, r.height, a.c).find(h => h.k === a.k); if (h) { sendEditor(a.c, h.drag(clamp(e.clientX - r.left, 0, r.width), clamp(e.clientY - r.top, 0, r.height))); syncControls(); redraw(); } return; }
	/* Mockup v60: a value box follows the axis that moved more (sideways or up/down), one value a pixel. */
	const drag = Held.as("value");
	if (drag) { const fine = e.shiftKey ? .25 : 1, dx = e.clientX - drag.x, dy = drag.y - e.clientY; const d = drag.vert ? dy * 127 / 132 : (Math.abs(dx) >= Math.abs(dy) ? dx : dy); setV(drag.el, drag.v + d * fine); return; }
	if (Held.as("lane")) { laneAt(e); return; }
	const c = e.target.closest("canvas.ed"); if (c && ED[c.dataset.ed]) c.style.cursor = nearest(c, e) ? "grab" : "default";
});
function endDrag() { if (Held.end("editor")) redraw(); const drag = Held.end("value"); if (drag) drag.el.classList.remove("act"); endLaneDraw(); Gesture.end(); }
/* P7: a gesture ends wherever the button comes up (a pointerup outside #main left the drag on: every later
   mouse move edited the value, and the page waited for the gesture to end before showing the machine again).
   A move with no button down ends it too, and so does leaving the window. */
document.addEventListener("pointerup", endDrag); document.addEventListener("pointercancel", endDrag);
window.addEventListener("blur", () => { if (dragging()) endDrag(); });
/* A gesture ends with any pointer release, wherever it lands. */
document.addEventListener("pointerup", () => setTimeout(() => { if (!interacting()) Gesture.end(); }), true);
main.addEventListener("wheel", e => { const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (!el) return; e.preventDefault(); const d = (e.deltaY || e.deltaX) < 0 ? 1 : -1; setV(el, getV(el) + d * (e.shiftKey ? 10 : 1)); }, { passive: false });
main.addEventListener("dblclick", e => { const el = e.target.closest(".pc[data-g],.fader[data-g]"); if (el) setV(el, el.dataset.n === "VOL" ? 100 : 64); });
main.addEventListener("keydown", e => { const el = e.target.closest("[data-g]"); if (!el) return; const d = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 10, PageDown: -10 }[e.key]; if (d == null) return; e.preventDefault(); setV(el, getV(el) + d * (e.shiftKey ? 10 : 1)); });

/* ===== Steps: a drag across them paints them (P7): the first step decides (on or off) and every step the pointer
   crosses becomes that, one undo step for the whole drag. A click is a one-step paint. ===== */
function paintStep(el) {
	const p = Held.as("paint"); if (!p) return;
	const i = +el.dataset.t, s = +el.dataset.s, t = V.tracks[i], k = i + ":" + s;
	if (p.done.has(k)) return;
	Held.with("paint", { done: new Set(p.done).add(k) });
	if (t.trigs[s] === p.on) return;
	const w = [[["tracks", i, "trigs", s], p.on]];
	if (!p.on) w.push([["tracks", i, "acc", s], false], [["tracks", i, "slide", s], false], ...clearStep(i, s));
	cmd("trig", { p: V.pat, t: i, s, on: p.on }, undefined, w);
	refreshRow(i);
	if (!p.on) Held.with("paint", { locks: true });
}
main.addEventListener("pointerdown", e => {
	const st = e.target.closest("#seq .st"); if (!st || e.button !== 0 || e.shiftKey || e.altKey || e.metaKey || e.ctrlKey || V.rec) return;
	if (!V.loaded) { toast("The pattern is not loaded yet."); return; }
	clearSel();	/* a plain press on a step paints, and ends the selection (DESIGN-step-selection.md §3) */
	const i = +st.dataset.t, s = +st.dataset.s;
	Held.begin("paint", { on: !V.tracks[i].trigs[s], done: new Set(), first: i });
	Gesture.begin(); grabPointer(main, e); e.preventDefault();
	paintStep(st);
}, true);
main.addEventListener("pointermove", e => {
	if (!Held.as("paint")) return;
	if (e.buttons === 0 && e.pointerType === "mouse") { endPaint(); return; }
	const st = document.elementFromPoint(e.clientX, e.clientY)?.closest("#seq .st"); if (st) paintStep(st);
});
function endPaint() {
	const p = Held.end("paint"); if (!p) return; Gesture.end();
	if (p.locks) renderTop();
	if (p.first !== S.sel) select(p.first); else renderLane();
}
document.addEventListener("pointerup", endPaint); document.addEventListener("pointercancel", endPaint);
window.addEventListener("blur", endPaint);

/* ===== Steps: a selection (DESIGN-step-selection.md, mdDeskSelect.js; on ⌘ since K2, DESIGN-keymap.md). ⌘-press a
   step (Ctrl off a Mac), or press a number of the step ruler: the release decides (selStart says which presses are
   one). No other cell crossed: that one step (⌘⇧ on a step, ⇧ in the ruler, extends the selection to it); cells
   crossed: the block between the press and the release, steps × tracks. A ⌘-press inside a selection of more than one
   step that moves drops a copy of it where it lets go ("drop": its target shown dashed). One slot kind, "select"
   {from: {t, s}, at: {t, s}, ruler, extend, drop, moved}. A press in the workspace outside the grid ends the
   selection. ===== */
function selCell(e, d) {
	const el = document.elementFromPoint(e.clientX, e.clientY);
	const st = el?.closest("#seq .st"); if (st) return { t: +st.dataset.t, s: +st.dataset.s };
	const r = el?.closest("#seq .rul[data-s]"); return r ? { t: d.at.t, s: +r.dataset.s } : null;
}
/* where a dragged selection lands: moved by the drag, within the tracks and the steps shown */
function selDropAt(d) {
	const x = S.stepSel, t = clamp(x.t + d.at.t - d.from.t, 0, 16 - x.n), from = clamp(x.from + d.at.s - d.from.s, 0, V.len - 1);
	return { t, n: x.n, from, to: Math.min(from + x.to - x.from, V.len) };
}
main.addEventListener("pointerdown", e => {
	if (e.button !== 0 || V.rec || S.ws !== "seq") return;
	const st = e.target.closest("#seq .st"), ru = e.target.closest("#seq .rul[data-s]");
	if (!st && !ru) { if (!e.target.closest("#seq")) clearSel(); return; }
	const d = selStart(e, st ? { t: +st.dataset.t, s: +st.dataset.s } : { s: +ru.dataset.s }, !st);
	if (!d) return;
	if (!V.loaded) { toast("The pattern is not loaded yet."); return; }
	Held.begin("select", d);
	e.preventDefault();
}, true);
document.addEventListener("pointermove", e => {
	const d = Held.as("select"); if (!d) return;
	if (e.buttons === 0 && e.pointerType === "mouse") { endSelect(); return; }
	const c = selCell(e, d); if (!c || (c.t === d.at.t && c.s === d.at.s)) return;
	const n = Held.with("select", { at: c, moved: true });
	if (n.drop) syncSel(selDropAt(n)); else { S.stepSel = selBetween(n.from, n.at); syncSel(); }
});
function endSelect() {
	const d = Held.end("select"); if (!d) return;
	if (d.moved) {	/* the drag's own click (it follows the release at once, if at all) is not a click on what it lands on */
		const eat = e => { e.stopPropagation(); e.preventDefault(); };
		addEventListener("click", eat, { capture: true, once: true }); setTimeout(() => removeEventListener("click", eat, true), 0);
	}
	if (d.drop && d.moved) { const g = selDropAt(d); syncSel(); selCopyTo(g.t, g.from); return; }
	const x = S.stepSel;
	const sel = d.extend && !d.moved && x ? selExtend(x, d.at) : selBetween(d.from, d.at);
	setSel(sel);
	if (sel.t !== S.sel && sel.n === 1) select(sel.t);
	toast(`Selected ${selSay(sel)} · ⌘C copy · ⌘X cut · ⌘V paste here · ⌘D duplicate · Delete (or Copy, Clr, Paste above) · right-click: more`);
}
document.addEventListener("pointerup", endSelect); document.addEventListener("pointercancel", endSelect);
window.addEventListener("blur", () => { if (Held.as("select")) endSelect(); });

/* ===== BPM: drag up or down, arrows -> global tempo (0x61) ===== */
(() => {
	const b = $("#bpm");
	const set = v => { if (hostTempoRefused()) return; const bpm = clamp(Math.round(v * 10) / 10, 30, 300); cmd("tempo", { bpm }, "tempo", [[["bpm"], bpm]]); renderTop(); };
	b.addEventListener("pointerdown", e => { if (hostTempoRefused()) return; Held.begin("bpm", { y: e.clientY, v: V.bpm }); Gesture.begin(); grabPointer(b, e); });
	b.addEventListener("pointermove", e => { const d = Held.as("bpm"); if (!d) return; if (e.buttons === 0 && e.pointerType === "mouse") { Held.end("bpm"); Gesture.end(); return; } const v = d.v + (d.y - e.clientY) * (e.shiftKey ? .1 : .5); if (Math.abs(v - V.bpm) >= .05) set(v); });
	b.addEventListener("pointerup", () => { Held.end("bpm"); Gesture.end(); });
	b.addEventListener("keydown", e => { const k = { ArrowUp: 1, ArrowDown: -1 }[e.key]; if (!k) return; e.preventDefault(); set(V.bpm + k * (e.shiftKey ? .1 : 1)); });
})();
