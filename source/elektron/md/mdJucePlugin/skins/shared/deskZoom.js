"use strict";
/* The page's zoom keys (B-001), both editors: Cmd - smaller, Cmd + (or Cmd =) larger, Cmd 0 back to 100 % (Ctrl off a
   Mac), as in a browser. The plug-in zooms its web view and remembers the zoom (pageZoom, mdPageZoom.h); the
   editor's menu (a right-click on the page's header, the standalone's menu bar) has the same as Page Zoom. Taken
   before the page's own keys, so no shortcut of the page sees them.
   Where the web view has no page zoom of its own (Linux's webkit2gtk; mdWebPageHost.cpp layout) the plug-in sends the
   zoom that fits the window as a message, {type: "zoom", zoom}, and the page zooms itself (CSS zoom lays it out the
   same way). */
(() => {
	const mac = /Mac/.test(navigator.platform || navigator.userAgent || "");
	const stepOf = e => {
		if (e.key === "-" || e.key === "_" || e.code === "Minus" || e.code === "NumpadSubtract") return -1;
		if (e.key === "=" || e.key === "+" || e.code === "Equal" || e.code === "NumpadAdd") return 1;
		if (e.key === "0" || e.code === "Digit0" || e.code === "Numpad0") return 0;
		return null;
	};
	document.addEventListener("keydown", e => {
		/* ⌘ as the key map reads it (deskKeys.js Modifiers; the MM page loads this file before its map) */
		const cmd = typeof Modifiers !== "undefined" ? Modifiers.cmd(e) : (mac ? e.metaKey : e.ctrlKey);
		if (!cmd || e.altKey) return;
		const step = stepOf(e);
		if (step === null) return;
		e.preventDefault(); e.stopPropagation();
		if (!e.repeat || step !== 0) Bridge.send({ op: "pageZoom", step });
	}, true);
	Bridge.onMessage(m => { if (m.type === "zoom" && typeof m.zoom === "number" && m.zoom > 0)
		document.documentElement.style.zoom = String(m.zoom); });
})();
