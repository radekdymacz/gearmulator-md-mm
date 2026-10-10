"use strict";
/* The GEN bar (Sequence) and the MUTATE bar (Sound), both editors (DESIGN-generators.md §5; the look:
   deskGenBar.css): the bars' pieces as markup, and their clicks, keys and wheel. Each page builds its bars from
   these pieces (mdDeskGenUi.js; the MM mockup's 76-gen.js) and keeps what is its machine's: the specs, the run, the
   trial, what a change writes. The handlers call the page's functions by name, at the event, so the page defines
   them: genDefaults(), randomise(all), genKind(kind), genMode(mode), genVal(key, d) and, for a scope chip,
   S.mut.scope (a Set, replaced per change), renderMutStrip(), mutLive(). A value's drag is each page's own (the MD
   holds it in its one gesture slot, Held). */

/* a group: a title on a thin rule over its controls */
const gbg = (label, body, cls = "", tip = "") => `<div class="gbg ${cls}"${tip ? ` title="${tip}"` : ""}><span class="gbl">${label}</span><div class="gbc">${body}</div></div>`;
/* a value: an LCD window, its label inside (drag up or down, wheel, arrows, click) */
const gv = (k, label, v, tip) => `<span class="gv" data-gv="${k}" role="spinbutton" tabindex="0" aria-label="${label}" aria-valuenow="${parseInt(v) || 0}" title="${tip}. Drag up or down, scroll, or click (⇧-click: down)."><small>${label}</small><b>${v}</b></span>`;
/* a key of a bar: a small square cap with its key on it (the keyboard shortcut too) and a tiny label over it */
const kc = (attr, act, cap, label, tip, off = false, cls = "", on = null) => `<button class="kc ${cls}${on ? " on" : ""}" ${attr}="${act}" ${off ? "disabled" : ""}${on != null ? ` aria-pressed="${on}"` : ""} title="${tip}" aria-label="${label}"><small>${label}</small><kbd>${cap}</kbd></button>`;
/* the bars' one randomise key: R, with its "all" chord over it */
const randKey = tip => `<button class="kc cream krand" data-rand="1" title="${tip}" aria-label="Randomise"><small>Random <em>⌥R all</em></small><kbd>R</kbd></button>`;
/* the bar's title and what it acts on (Alt: all, in the LED's colour) */
const gtitle = (name, target, all, tip) => `<div class="gbt" title="${tip}"><b>${name}</b><span class="${all ? "all" : ""}">${target}</span></div>`;
/* an LED radio row: items [[key, words]], tips {key: tip} */
const gseg = (attr, cur, items, tips) => `<span class="seg">${items.map(([k, n]) => `<button ${attr}="${k}" aria-pressed="${cur === k}" title="${tips[k]}">${n}</button>`).join("")}</span>`;

/* the bars' clicks: Defaults, the R key (⌥ or FN: every track; ⌘ is not "all", DESIGN-keymap.md P3), MODE, WRITE,
   a value (click: up, ⇧-click: down; not after its drag), a scope chip or a group's title (MUTATE's scope) */
document.addEventListener("click", e => {
	const g = e.target.closest("[data-gen]"); if (g && !g.disabled) { if (g.dataset.gen === "fill") genDefaults(); return; }
	const rk = e.target.closest("[data-rand]"); if (rk && !rk.disabled) { randomise(e.altKey); return; }
	const k = e.target.closest("[data-genkind]"); if (k) { genKind(k.dataset.genkind); return; }
	const md = e.target.closest("[data-genmode]"); if (md) { genMode(md.dataset.genmode); return; }
	const v = e.target.closest(".gv[data-gv]"); if (v && !v.dataset.dragged) { genVal(v.dataset.gv, e.shiftKey ? -1 : 1); return; }
	const c = e.target.closest("[data-mutg],[data-mutsg]"); if (c) {
		const id = c.dataset.mutg || c.dataset.mutsg, scope = new Set(S.mut.scope); scope.has(id) ? scope.delete(id) : scope.add(id); S.mut.scope = scope;
		renderMutStrip(); mutLive(); e.stopPropagation();
	}
}, true);
/* a focused value: the arrows move it (⇧: by 10), and it keeps the focus across the bar's redraw */
document.addEventListener("keydown", e => {
	const v = e.target.closest?.(".gv[data-gv]"); if (!v) return; const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key]; if (d == null) return;
	e.preventDefault(); e.stopPropagation(); const k = v.dataset.gv; genVal(k, d * (e.shiftKey ? 10 : 1)); document.querySelector(`.gv[data-gv="${k}"]`)?.focus();
}, true);
/* the wheel over a value (⇧: by 10) */
document.addEventListener("wheel", e => { const v = e.target.closest?.(".gv[data-gv]"); if (!v) return; e.preventDefault(); genVal(v.dataset.gv, ((e.deltaY || e.deltaX) < 0 ? 1 : -1) * (e.shiftKey ? 10 : 1)); }, { passive: false });
/* how much a dragged value moves a notch: the percentages two */
const gvDragStep = k => k === "dens" || k === "amt" || k === "racc" ? 2 : 1;
