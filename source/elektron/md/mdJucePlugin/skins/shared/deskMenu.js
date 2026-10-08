"use strict";
/* A context menu (K3, doc/modern-ux/DESIGN-keymap.md P5), one for both editors: a short list of actions at the
   pointer, each with the key that does the same, so nothing is only behind a key (a host that eats ⌘C, a person
   on a trackpad or a touch screen). It is a dialog of the modal layer (deskModal.js, kind "menu": no dimming, Esc
   and a click outside close it; the page's keys are off while it is open). Items are data:
     {id?, label, key? (the key's words, shown at the right), enabled? (default true), run: () => void} or "-"
   open(items, x, y, {title?}) shows it at the point (kept inside the window); a chosen item closes it, then runs.
   Arrow keys, Home and End move between the items, Enter or Space chooses (the focused button's own key). */
const DeskMenu = (() => {
	let el = null, items = [];
	const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
	function ensure() {
		if (el) return el;
		el = document.createElement("div"); el.id = "deskmenu"; el.className = "deskmenu"; el.setAttribute("role", "menu"); el.hidden = true;
		document.body.appendChild(el);
		el.addEventListener("click", e => {
			const b = e.target.closest("[data-mi]"); if (!b || b.disabled) return;
			const it = items[+b.dataset.mi]; close();
			if (it && it.run) it.run();
		});
		el.addEventListener("keydown", e => {
			const keys = [...el.querySelectorAll("[data-mi]:not([disabled])")], i = keys.indexOf(document.activeElement);
			const to = { ArrowDown: i + 1, ArrowUp: i - 1, Home: 0, End: keys.length - 1 }[e.key];
			if (to == null || !keys.length) return;
			e.preventDefault(); keys[(to + keys.length) % keys.length].focus();
		});
		/* a right-click anywhere while it is open: no system menu over it */
		document.addEventListener("contextmenu", e => { if (shown() && !el.contains(e.target)) { e.preventDefault(); close(); } }, true);
		return el;
	}
	const shown = () => !!el && !el.hidden;
	function open(list, x, y, opts = {}) {
		ensure(); items = list;
		el.innerHTML = (opts.title ? `<div class="mtitle">${esc(opts.title)}</div>` : "") + list.map((it, i) => it === "-" ? `<hr>`
			: `<button type="button" role="menuitem" data-mi="${i}"${it.id ? ` data-mid="${esc(it.id)}"` : ""}${it.enabled === false ? " disabled" : ""}><span>${esc(it.label)}</span>${it.key ? `<kbd>${esc(it.key)}</kbd>` : ""}</button>`).join("");
		el.style.left = "0px"; el.style.top = "0px"; el.hidden = false;
		const w = el.offsetWidth, h = el.offsetHeight, vw = document.documentElement.clientWidth || innerWidth, vh = innerHeight;
		el.style.left = Math.max(4, Math.min(x, vw - w - 4)) + "px";
		el.style.top = Math.max(4, Math.min(y, vh - h - 4)) + "px";
		const first = el.querySelector("[data-mi]:not([disabled])"); if (first) first.focus({ preventScroll: true });
	}
	function close() { if (el) el.hidden = true; }
	/* made at load, so the modal layer finds it when it starts watching its dialogs */
	if (typeof document !== "undefined" && document.body) ensure();
	return { open, close, get shown() { return shown(); }, items: () => items.slice() };
})();
/* the modal layer's close for it (deskModal.js DIALOGS: Esc, a click outside) */
function closeDeskMenu() { DeskMenu.close(); }
