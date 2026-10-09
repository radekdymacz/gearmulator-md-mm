"use strict";
/* A context menu (K3, doc/modern-ux/DESIGN-keymap.md P5), one for both editors: a short list of actions at the
   pointer, each with the key that does the same, so nothing is only behind a key (a host that eats ⌘C, a person
   on a trackpad or a touch screen). It is a dialog of the modal layer (deskModal.js, kind "menu": no dimming, Esc
   and a click outside close it; the page's keys are off while it is open). Items are data:
     {id?, label, key? (the key's words, shown at the right), enabled? (default true), checked? (true / false: a tick
      column), run: () => void}
     {id?, label, key?, items: [...]}   a submenu, opened beside its entry
     {heading: "words"}                 a section's name, not chosen
     {note: "words"}                    a line of information (a version, a status), not chosen
     "-"                                a separator
   open(items, x, y, {title?}) shows it at the point: below and right of it where it fits, else above or left (kept
   inside the window); a submenu opens right of its entry, or left where the right has no room. A chosen item closes
   the menu, then runs. Arrow keys, Home and End move between the items, → or Enter opens a submenu, ← or Esc closes
   it, Enter or Space chooses (the focused button's own key); the pointer focuses what it is over.
   I-008: the editor's menu (zoom, updates, the log folder, the developer's entries) is the plug-in's, as data: the
   page asks for it (openMenu), the window answers with an editorMenu message (mdEditorMenu.h), showEditor() draws
   it here and sends the chosen entry's number back (menuPick). fromEditor() is the translation, pure. */
const DeskMenu = (() => {
	let el = null, levels = [], hoverT = 0;	// levels[i]: {panel, items, owner: the entry that opened it}
	const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
	const M = 4;	// the margin kept to the window's edges
	const clamp = (v, lo, hi) => Math.max(lo, Math.min(v, hi));
	/* Where a panel w x h goes in a window vw x vh (pure): at a point {x, y} below and right of it where that fits,
	   else above or left of it; beside an entry {left, right, top}, right of it where that fits, else left. */
	function place(w, h, vw, vh, at) {
		let left, top;
		if (at.right != null) {
			left = at.right - 2 + w + M <= vw ? at.right - 2 : at.left - w + 2;
			top = at.top - 5;
		} else {
			left = at.x + w + M <= vw ? at.x : at.x - w;
			top = at.y + h + M <= vh ? at.y : at.y - h;
		}
		return { left: Math.round(clamp(left, M, Math.max(M, vw - w - M))), top: Math.round(clamp(top, M, Math.max(M, vh - h - M))) };
	}
	/* The entry a key moves to among n entries from i (pure): -1 for a key that moves nothing. */
	function step(n, i, key) {
		if (!n) return -1;
		const to = { ArrowDown: i + 1, ArrowUp: i < 0 ? n - 1 : i - 1, Home: 0, End: n - 1 }[key];
		return to == null ? -1 : (to + n) % n;
	}
	const isEntry = it => it && it !== "-" && it.heading == null && it.note == null;
	const can = it => isEntry(it) && it.enabled !== false && (it.items ? it.items.length > 0 : !!it.run);
	function row(it, i, ticks) {
		if (it === "-") return "<hr>";
		if (it.heading != null) return `<div class="mhead" role="presentation">${esc(it.heading)}</div>`;
		if (it.note != null) return `<div class="mnote" role="presentation">${esc(it.note)}</div>`;
		const sub = !!it.items, role = typeof it.checked === "boolean" ? "menuitemcheckbox" : "menuitem";
		return `<button type="button" role="${role}" data-mi="${i}"${it.id ? ` data-mid="${esc(it.id)}"` : ""}${can(it) ? "" : " disabled"}`
			+ `${role === "menuitemcheckbox" ? ` aria-checked="${!!it.checked}"` : ""}${sub ? ' aria-haspopup="menu" aria-expanded="false"' : ""}>`
			+ `${ticks ? `<i class="mck" aria-hidden="true">${it.checked ? "✓" : ""}</i>` : ""}<span>${esc(it.label)}</span>`
			+ `${it.key ? `<kbd>${esc(it.key)}</kbd>` : ""}${sub ? '<i class="msub" aria-hidden="true">›</i>' : ""}</button>`;
	}
	const vw = () => document.documentElement.clientWidth || innerWidth, vh = () => document.documentElement.clientHeight || innerHeight;
	function panel(list, level, at, title) {
		const p = document.createElement("div");
		p.className = "mpanel"; p.setAttribute("role", "menu"); p.dataset.lv = level;
		const ticks = list.some(it => isEntry(it) && typeof it.checked === "boolean");
		p.innerHTML = (title ? `<div class="mtitle">${esc(title)}</div>` : "") + list.map((it, i) => row(it, i, ticks)).join("");
		p.style.left = "0px"; p.style.top = "0px"; p.style.visibility = "hidden";
		el.appendChild(p);
		const pos = place(p.offsetWidth, p.offsetHeight, vw(), vh(), at);
		p.style.left = pos.left + "px"; p.style.top = pos.top + "px"; p.style.visibility = "";
		return p;
	}
	const buttons = p => [...p.querySelectorAll("[data-mi]:not([disabled])")];
	const focus = b => { if (b) b.focus({ preventScroll: true }); };
	const levelOf = node => { const p = node && node.closest && node.closest(".mpanel"); return p ? +p.dataset.lv : -1; };
	function closeFrom(level) {
		while (levels.length > level) {
			const l = levels.pop();
			if (l.owner) l.owner.setAttribute("aria-expanded", "false");
			l.panel.remove();
		}
	}
	function openSub(level, b, focusFirst) {
		const it = levels[level] && levels[level].items[+b.dataset.mi];
		if (!it || !it.items || b.disabled) return;
		if (levels[level + 1] && levels[level + 1].owner === b) { if (focusFirst) focus(buttons(levels[level + 1].panel)[0]); return; }
		closeFrom(level + 1);
		const r = b.getBoundingClientRect();
		const p = panel(it.items, level + 1, { left: r.left, right: r.right, top: r.top });
		levels.push({ panel: p, items: it.items, owner: b });
		b.setAttribute("aria-expanded", "true");
		if (focusFirst) focus(buttons(p)[0]);
	}
	function choose(b) {
		const level = levelOf(b), it = levels[level] && levels[level].items[+b.dataset.mi];
		if (!it || b.disabled) return;
		if (it.items) { openSub(level, b, true); return; }
		close();
		if (it.run) it.run();
	}
	function ensure() {
		if (el) return el;
		el = document.createElement("div"); el.id = "deskmenu"; el.className = "deskmenu"; el.hidden = true;
		document.body.appendChild(el);
		el.addEventListener("click", e => { const b = e.target.closest("[data-mi]"); if (b) choose(b); });
		/* the pointer focuses the entry it is over; over a submenu's entry for a moment, it opens; over another, a
		   deeper submenu closes (as a native menu does) */
		el.addEventListener("mouseover", e => {
			const b = e.target.closest("[data-mi]"), level = levelOf(e.target);
			if (level < 0) return;
			clearTimeout(hoverT);
			if (b && !b.disabled && document.activeElement !== b) focus(b);
			hoverT = setTimeout(() => {
				if (!levels[level]) return;
				if (b && !b.disabled && levels[level].items[+b.dataset.mi]?.items) openSub(level, b, false);
				else if (b || !levels[level + 1]) closeFrom(level + 1);
			}, 160);
		});
		el.addEventListener("keydown", e => {
			const level = levelOf(document.activeElement); if (level < 0 || !levels[level]) return;
			const b = document.activeElement.closest("[data-mi]");
			if (e.key === "ArrowRight" && b && levels[level].items[+b.dataset.mi]?.items) { e.preventDefault(); openSub(level, b, true); return; }
			if (e.key === "ArrowLeft" && level > 0) { e.preventDefault(); const owner = levels[level].owner; closeFrom(level); focus(owner); return; }
			const keys = buttons(levels[level].panel), to = step(keys.length, keys.indexOf(document.activeElement), e.key);
			if (to < 0) return;
			e.preventDefault(); focus(keys[to]);
		});
		/* Esc in a submenu closes that submenu only (the modal layer's Esc, which closes the whole menu, comes after) */
		window.addEventListener("keydown", e => {
			if (e.key !== "Escape" || !shown() || levels.length < 2) return;
			e.preventDefault(); e.stopImmediatePropagation();
			const owner = levels[levels.length - 1].owner; closeFrom(levels.length - 1); focus(owner);
		}, true);
		/* a right-click anywhere while it is open: no system menu over it */
		document.addEventListener("contextmenu", e => { if (shown() && !el.contains(e.target)) { e.preventDefault(); close(); } }, true);
		return el;
	}
	const shown = () => !!el && !el.hidden;
	function open(list, x, y, opts = {}) {
		ensure(); closeFrom(0); clearTimeout(hoverT);
		el.hidden = false;
		const p = panel(list, 0, { x, y }, opts.title);
		levels.push({ panel: p, items: list, owner: null });
		focus(buttons(p)[0]);
	}
	function close() { clearTimeout(hoverT); if (!el) return; closeFrom(0); el.hidden = true; }
	/* I-008: the plug-in's editorMenu message as items (pure): each entry the window can run sends its number */
	function fromEditor(list, pick) {
		return (list || []).map(x => {
			if (x.kind === "separator") return "-";
			if (x.kind === "heading") return { heading: x.label || "" };
			if (x.kind === "note") return { note: x.label || "" };
			const it = { id: x.id, label: x.label || "", key: x.key || "" };
			if (x.kind === "submenu") { it.items = fromEditor(x.items, pick); it.enabled = x.enabled !== false; return it; }
			it.checked = !!x.checked;
			it.enabled = x.enabled !== false && typeof x.n === "number";
			if (it.enabled) it.run = () => pick(x.n);
			return it;
		});
	}
	/* I-008: whether a right-click (a contextmenu event) asks for the editor's menu: anywhere the page has no menu of
	   its own (an earlier handler took the event: a step's menu), not in a text field (its own edit menu), not while a
	   dialog or panel is open (its backdrop) and not over this menu. Each page's listener runs after its own menus'. */
	function wantsEditor(e) {
		if (e.defaultPrevented) return false;
		const t = e.target && e.target.closest ? e.target : null;
		if (t && t.closest("input,textarea,select,[contenteditable],[contenteditable=''],#deskmenu")) return false;
		return !(typeof Modal !== "undefined" && Modal.top());
	}
	/* the editor's menu at the point, its choice sent with send({op: "menuPick", menu, n}) */
	function showEditor(m, x, y, send) {
		api.open(fromEditor(m.items, n => send({ op: "menuPick", menu: m.menu, n })), x, y, { title: m.title });
	}
	/* made at load, so the modal layer finds it when it starts watching its dialogs */
	if (typeof document !== "undefined" && document.body) ensure();
	const api = { open, close, showEditor, wantsEditor, fromEditor, place, step, get shown() { return shown(); }, items: () => (levels[0] ? levels[0].items.slice() : []),
		depth: () => levels.length };
	return api;
})();
/* the modal layer's close for it (deskModal.js DIALOGS: Esc, a click outside) */
function closeDeskMenu() { DeskMenu.close(); }
if (typeof module !== "undefined") module.exports = { DeskMenu };
