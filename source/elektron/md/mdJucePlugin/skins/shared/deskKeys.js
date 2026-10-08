"use strict";
/* The key map's dispatcher (P5), one file for both editors and the MM mockup (skins/shared/): every shortcut
   is a Keys.bind entry; the pages' own maps are mdDeskKeys.js and the MM mockup's 56-keys.js. */
const Keys = (() => {
	const list = [];
	const norm = e => e.key === " " ? "Space" : e.key.length === 1 ? e.key.toUpperCase() : e.key;
	/* {keys: ["Z"], mod: "cmd"|"alt"|"shift"|"cmd+shift"|"", group, does (text, or () => text when it shows state), when?: () => bool,
	   run?: e => void, field?: true, hidden?: true (dispatched, but another entry describes it in the ? overlay),
	   modal?: "keyspop" (it also runs while that dialog is the top one; every other entry is off while one is open)
	     or "panel" (it also runs over any panel: the library, GLOBAL, AUDIO / MIDI; never over a question),
	   code?: "KeyR" (matched on the physical key, e.code: with Alt or Cmd held macOS types another character)} */
	function bind(entry) { list.push(Object.assign({ mod: "", when: null, field: false }, entry)); }
	function modOf(e) { return [e.metaKey || e.ctrlKey ? "cmd" : "", e.altKey ? "alt" : "", e.shiftKey ? "shift" : ""].filter(Boolean).join("+"); }
	/* The one gating rule of both editors: while a dialog or a panel is open (the modal layer, deskModal.js:
	   GLOBAL, AUDIO / MIDI, the library, the machine picker, the list of keys, a question, the start-up card)
	   the page's shortcuts are off and the keys are the dialog's own: Space and Enter press its focused
	   button, Backspace and the digits are its field's. Only an entry naming that dialog (modal) runs.
	   free(): no dialog open and no text field focused, the rule of the keys that play and act on tracks. */
	const modalTop = () => typeof Modal !== "undefined" && Modal.top ? Modal.top() : null;
	const modalKind = () => typeof Modal !== "undefined" && Modal.kind ? Modal.kind() : null;
	const passes = (b, top) => b.modal === top || (b.modal === "panel" && modalKind() === "panel");
	const free = () => !modalTop() && !document.activeElement?.closest?.("input,select,textarea,[contenteditable]");
	/* The keys that reached the page, newest last ("cmd+C", "shift+?", "Space"): every keydown (noted first, before a
	   dialog keeps it from the shortcuts) and every edit command taken as its key (below). The key probe's text. */
	const seen = [];
	function note(e) {
		const k = norm(e), m = modOf(e); if (/^(Meta|Control|Alt|Shift|CapsLock)$/.test(k)) return;
		seen.push((m ? m + "+" : "") + k); if (seen.length > 24) seen.shift(); probe();
	}
	if (typeof addEventListener === "function") addEventListener("keydown", note, true);
	/* One key, from either way in (a keydown, or an edit command below); true when an entry ran. */
	function dispatch(e, inField) {
		const k = norm(e), m = modOf(e), top = modalTop();
		for (const b of list) {
			if (!b.run || !(b.code ? e.code === b.code : b.keys.includes(k))) continue;
			if (top && !passes(b, top)) continue;
			const want = b.mod || "", ok = want === m || (want === "" && m === "shift" && k.length === 1 && !/[A-Z]/.test(k));
			if (!ok || (inField && !b.field) || (b.when && !b.when())) continue;
			e.preventDefault(); b.run(e); return true;
		}
		return false;
	}
	let lastDown = null;	/* {k, at} of the last ⌘ keydown: an edit command right after it is the same press */
	document.addEventListener("keydown", e => {
		if (e.metaKey || e.ctrlKey) lastDown = { k: norm(e), at: Date.now() };
		dispatch(e, e.target.closest?.("input,select,textarea,[role=slider]"));
	});
	/* ⌘C, ⌘X and ⌘V may never come as keys. On macOS the web view the plug-in sits in (JUCE's WebBrowserComponent,
	   juce_WebBrowserComponent_mac.mm, WebViewKeyEquivalentResponder) turns them into the edit commands copy:,
	   cut: and paste: before WebKit sees a key, in the standalone and in every DAW alike, and the page gets the
	   document's copy, cut and paste events instead of a keydown; a host's Edit menu does the same. So such an event
	   is that key press: dispatched as ⌘C / ⌘X / ⌘V, unless a text field has the focus (its own copy and paste) or
	   the keydown of the same press came first (a browser, WebView2: then that was the dispatch). */
	const EDITS = { copy: "c", cut: "x", paste: "v" };
	for (const type of Object.keys(EDITS)) document.addEventListener(type, e => {
		const key = EDITS[type], a = document.activeElement;
		if (a?.closest?.("input,select,textarea,[contenteditable]")) return;
		if (lastDown && lastDown.k === key.toUpperCase() && Date.now() - lastDown.at < 500) return;
		const k = { key, code: "Key" + key.toUpperCase(), metaKey: true, ctrlKey: false, altKey: false, shiftKey: false, target: a || document.body,
			preventDefault: () => e.preventDefault() };
		note(k); dispatch(k, a?.closest?.("[role=slider]"));
	});
	/* The key probe (the start tests, scripts/macos|windows|linux/smoke_mdmm.*): with ?keyprobe=1 in the page's
	   address (the plug-in adds it when GEARMULATOR_MDMM_KEYPROBE=1) a line at the bottom left says which keys reached
	   the page, a text the operating system's accessibility API reads. Nothing is shown without it. While a dialog
	   marked aria-modal is open (the start-up card is one) the line lives inside it: WebKit's accessibility tree leaves
	   out everything outside an open modal dialog. */
	const probing = typeof location !== "undefined" && /[?&]keyprobe=1\b/.test(location.search || "");
	function probe() {
		if (!probing || !document.body) return;
		let p = document.getElementById("keyprobe");
		if (!p) {
			p = document.createElement("div"); p.id = "keyprobe"; p.setAttribute("role", "status");
			p.style.cssText = "position:fixed;left:8px;bottom:8px;z-index:2147483647;padding:6px 10px;background:#000;color:#fff;font:12px monospace";
		}
		const modal = [...document.querySelectorAll('[aria-modal="true"]')].filter(d => !d.closest("[hidden]")).pop();
		const home = modal || document.body;
		if (p.parentNode !== home) home.appendChild(p);
		const text = "Keys seen: " + (seen.length ? seen.join(" ") : "none");
		if (p.textContent !== text) p.textContent = text;
	}
	if (probing) setInterval(probe, 500);
	const label = b => [...(b.mod ? b.mod.split("+").map(x => ({ cmd: "⌘", alt: "⌥", shift: "⇧" }[x])) : []), b.keys.map(k => ({ Space: "Space", ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓", Escape: "Esc", Delete: "Delete", Backspace: "⌫", Enter: "Enter" }[k] || k)).join(" / ")].join("");
	return { bind, list: () => list.slice(), label, free, seen: () => seen.slice() };
})();
