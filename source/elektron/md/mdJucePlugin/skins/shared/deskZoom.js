"use strict";
/* The page's zoom keys (B-001), both editors: Cmd - smaller, Cmd + (or Cmd =) larger, Cmd 0 back to 100 % (Ctrl off a
   Mac), as in a browser. The plug-in zooms its web view and remembers the zoom (pageZoom, mdPageZoom.h); the
   editor's menu (a right-click on the page's header, the standalone's menu bar) has the same as Page Zoom. Taken
   before the page's own keys, so no shortcut of the page sees them. */
(() => {
	const mac = /Mac/.test(navigator.platform || navigator.userAgent || "");
	const stepOf = e => {
		if (e.key === "-" || e.key === "_" || e.code === "Minus" || e.code === "NumpadSubtract") return -1;
		if (e.key === "=" || e.key === "+" || e.code === "Equal" || e.code === "NumpadAdd") return 1;
		if (e.key === "0" || e.code === "Digit0" || e.code === "Numpad0") return 0;
		return null;
	};
	document.addEventListener("keydown", e => {
		if (!(mac ? e.metaKey : e.ctrlKey) || e.altKey) return;
		const step = stepOf(e);
		if (step === null) return;
		e.preventDefault(); e.stopPropagation();
		if (!e.repeat || step !== 0) Bridge.send({ op: "pageZoom", step });
	}, true);
})();
