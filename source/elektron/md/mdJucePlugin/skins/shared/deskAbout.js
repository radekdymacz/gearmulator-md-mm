"use strict";
/* Which editor this is (0.3.4), both editors: the plug-in opens the page with ?version=<MDMM_EDITOR_VERSION>
   (mdWebPageHost.cpp); the page shows it as a small "v0.3.4" beside the Setup group's label and in the keyboard
   view's head (deskKeyView.js). A page opened without it (the mockups, a browser) shows nothing.
     About.version   "0.3.4", or "" when the page was not told
     About.label     "v0.3.4", or ""
     About.paint()   puts the label beside the Setup group's label (again: does nothing) */
const About = (() => {
	const search = typeof location !== "undefined" ? location.search || "" : "";
	const m = /[?&]version=([0-9A-Za-z.+-]+)/.exec(search);
	const version = m ? decodeURIComponent(m[1]) : "";
	const label = version ? "v" + version : "";
	function paint(root) {
		const doc = root || (typeof document !== "undefined" ? document : null);
		if (!label || !doc) return;
		const g = doc.querySelector(".setgroup .grplabel");
		if (!g || g.querySelector(".aboutver")) return;
		const s = doc.createElement("span");
		s.className = "aboutver";
		s.textContent = label;
		s.title = "This editor's version";
		s.style.cssText = "margin-left:.5em;opacity:.55;font-size:.85em;letter-spacing:0;text-transform:none";
		g.appendChild(s);
	}
	if (typeof document !== "undefined") {
		if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", () => paint());
		else paint();
	}
	return { version, label, paint };
})();
if (typeof module !== "undefined") module.exports = About;
