"use strict";
/* The platform's modifiers (K1, doc/modern-ux/DESIGN-keymap.md P4), the one place both editors read them: ⌘ is the
   command key on macOS (metaKey) and Ctrl elsewhere (ctrlKey). On a Mac, Ctrl is not ⌘ (Ctrl-click is the
   system's right-click), and off a Mac the Windows key is not Ctrl: of(e) names them "ctrl" / "meta", which no
   entry wants. say(text) puts a text the page shows into the platform's words (⌘C stays ⌘C on a Mac, is Ctrl+C
   elsewhere; ⌥ is Alt, ⇧ Shift); off a Mac the page's titles and texts are put so as they are drawn (localise).
   setPlatform(mac) is for the tests. */
const Modifiers = (() => {
	let mac = typeof navigator !== "undefined" && /Mac|iPhone|iPad|iPod/.test(navigator.platform || navigator.userAgent || "");
	const cmd = e => !!(mac ? e.metaKey : e.ctrlKey);
	const of = e => [cmd(e) ? "cmd" : "", e.altKey ? "alt" : "", e.shiftKey ? "shift" : "",
		mac && e.ctrlKey ? "ctrl" : "", !mac && e.metaKey ? "meta" : ""].filter(Boolean).join("+");
	const NAME = { "⌘": "Ctrl", "⌥": "Alt", "⇧": "Shift" };
	/* off a Mac: each run of symbols becomes its names joined with +, and a + before the key it holds ("⌘⇧Z" ->
	   "Ctrl+Shift+Z", "⌘-click" -> "Ctrl-click", "(⇧: 1)" -> "(Shift: 1)"); "Cmd" and "Option" as words too */
	const say = text => mac || text == null ? text : String(text)
		.replace(/[⌘⌥⇧]+/g, (m, at, s) => [...m].map(c => NAME[c]).join("+") + (/^[^\s\-:;,.)/]/.test(s.slice(at + m.length)) ? "+" : ""))
		.replace(/\bCmd\b/g, "Ctrl").replace(/\bOption\b/g, "Alt");
	const odd = t => !!t && /[⌘⌥⇧]|\bCmd\b|\bOption\b/.test(t);
	const ATTRS = ["title", "aria-label", "data-tip"];
	function localiseNode(n) {
		if (mac || !n) return;
		if (n.nodeType === 3) { if (odd(n.nodeValue)) n.nodeValue = say(n.nodeValue); return; }
		if (n.nodeType !== 1) return;
		for (const el of [n, ...n.querySelectorAll("[title],[aria-label],[data-tip]")]) for (const a of ATTRS) { const v = el.getAttribute(a); if (odd(v)) el.setAttribute(a, say(v)); }
		if (!odd(n.textContent)) return;
		const it = document.createNodeIterator(n, 4); for (let t = it.nextNode(); t; t = it.nextNode()) if (odd(t.nodeValue)) t.nodeValue = say(t.nodeValue);
	}
	/* off a Mac, whatever the page draws or retitles is said with Ctrl / Alt / Shift */
	function localise() {
		if (mac || typeof MutationObserver === "undefined" || !document.body) return;
		localiseNode(document.body);
		new MutationObserver(ms => {
			for (const m of ms) {
				if (m.type !== "attributes") { m.addedNodes.forEach(localiseNode); continue; }
				const v = m.target.getAttribute(m.attributeName); if (odd(v)) m.target.setAttribute(m.attributeName, say(v));
			}
		}).observe(document.body, { subtree: true, childList: true, attributes: true, attributeFilter: ATTRS });
	}
	if (typeof document !== "undefined" && document.addEventListener) { if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", localise); else localise(); }

	/* FN (K4, DESIGN-keymap.md P5): the machine's FUNCTION key on the screen, for a mouse or a touch screen, for a
	   host or a desktop that takes Alt (Windows' menu bar, Linux's Alt-drag): while it is on, every pointer and key
	   event the page sees says ⌥ (altKey), so every ⌥ gesture and key has a route without a held key. "once": the next
	   click, drag or key gets it, then it goes off; "latch" (a double-click on its key) until it is clicked again or
	   Esc. Events on the FN key itself are left as they are. fn(state) sets it; onFn(f) hears every change. */
	let fn = "off";
	const fnHear = [];
	const setFn = s => { if (s === fn) return; fn = s; fnHear.forEach(f => f(fn)); };
	const onFnKey = e => !!e.target?.closest?.("[data-fnkey]");
	const MODKEYS = new Set(["Alt", "Shift", "Meta", "Control", "CapsLock", "Fn", "AltGraph"]);
	let spend = 0;
	/* "once" is spent after the action it was for: a click, the end of a drag, a key (a timer: after the page's own handlers) */
	const spendSoon = () => { if (fn !== "once" || spend) return; spend = setTimeout(() => { spend = 0; if (fn === "once") setFn("off"); }, 0); };
	if (typeof addEventListener === "function") {
		for (const type of ["pointerdown", "pointermove", "pointerup", "mousedown", "mousemove", "mouseup", "click", "dblclick", "contextmenu", "wheel", "keydown", "keyup"]) addEventListener(type, e => {
			if (fn === "off" || onFnKey(e)) return;
			if (type === "keydown" && e.key === "Escape") { setFn("off"); e.preventDefault(); e.stopImmediatePropagation(); return; }
			if (!e.altKey) Object.defineProperty(e, "altKey", { value: true, configurable: true });
			if (type === "click" || type === "pointerup" || (type === "keydown" && !MODKEYS.has(e.key))) spendSoon();
		}, true);
	}
	return { get mac() { return mac; }, setPlatform(m) { mac = !!m; }, cmd, of, say,
		get fn() { return fn; }, setFn, onFn: f => { fnHear.push(f); } };
})();
/* The key map's dispatcher (P5), one file for both editors and the MM mockup (skins/shared/): every shortcut
   is a Keys.bind entry; the pages' own maps are mdDeskKeys.js and the MM mockup's 56-keys.js. */
const Keys = (() => {
	const list = [];
	const norm = e => e.key === " " ? "Space" : e.key.length === 1 ? e.key.toUpperCase() : e.key;
	/* {id: "undo" (stable and unique in a page's map: the guide, the parity test of both editors and a later rebinding
	   name an entry by it), scope: "any" or the workspaces it acts in ("seq sampler"; "library": the kit library and
	   the pattern chooser; "control"), area?: "Steps" (a pointer gesture: where on the page; keys then name what is
	   pressed, ["step"]), keys: ["Z"], mod: "cmd"|"alt"|"shift"|"cmd+shift"|"", group, does (text, or () => text when it shows state), when?: () => bool,
	   run?: e => void, field?: true, hidden?: true (dispatched, but another entry describes it in the ? overlay),
	   modal?: "keyspop" (it also runs while that dialog is the top one; every other entry is off while one is open)
	     or "panel" (it also runs over any panel: the library, GLOBAL, AUDIO / MIDI; never over a question),
	   code?: "KeyR" (matched on the physical key, e.code: with Alt or Cmd held macOS types another character)} */
	function bind(entry) { list.push(Object.assign({ mod: "", when: null, field: false }, entry)); }
	const modOf = e => Modifiers.of(e);
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
		if (Modifiers.cmd(e)) lastDown = { k: norm(e), at: Date.now() };
		dispatch(e, e.target.closest?.("input,select,textarea,[role=slider]"));
	});
	/* ⌘C, ⌘X and ⌘V may never come as keys (B-015). On macOS the web view the plug-in sits in (JUCE's
	   WebBrowserComponent, juce_WebBrowserComponent_mac.mm, WebViewKeyEquivalentResponder) turns them into the edit
	   commands copy:, cut: and paste: before WebKit sees a key, in the standalone and in every DAW alike, and the page
	   gets the document's copy, cut and paste events instead of a keydown; a host's Edit menu does the same. So such an
	   event is that key press: dispatched as ⌘C / ⌘X / ⌘V (the platform's ⌘: Ctrl off a Mac), so it runs the map's
	   entries copy, cut and paste, unless a text field has the focus (its own copy and paste) or the keydown of the
	   same press came first (a browser, WebView2: then that was the dispatch). */
	const EDITS = { copy: "c", cut: "x", paste: "v" };
	for (const type of Object.keys(EDITS)) document.addEventListener(type, e => {
		const key = EDITS[type], a = document.activeElement;
		if (a?.closest?.("input,select,textarea,[contenteditable]")) return;
		if (lastDown && lastDown.k === key.toUpperCase() && Date.now() - lastDown.at < 500) return;
		const k = { key, code: "Key" + key.toUpperCase(), metaKey: Modifiers.mac, ctrlKey: !Modifiers.mac, altKey: false, shiftKey: false, target: a || document.body,
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
	/* an entry's keys as the platform writes them: ⌘⇧Z on a Mac, Ctrl+Shift+Z elsewhere; a pointer gesture's
	   modifiers before what is pressed (⇧ step, Shift + step) */
	const KEYNAME = { Space: "Space", ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓", Escape: "Esc", Delete: "Delete", Backspace: "⌫", Enter: "Enter" };
	const modsLabel = mod => !mod ? "" : Modifiers.mac ? mod.split("+").map(x => ({ cmd: "⌘", alt: "⌥", shift: "⇧" }[x])).join("")
		: mod.split("+").map(x => ({ cmd: "Ctrl", alt: "Alt", shift: "Shift" }[x])).join("+");
	const label = b => {
		const m = modsLabel(b.mod), k = b.keys.map(x => KEYNAME[x] || x).join(" / ");
		if (!m) return k;
		return b.area ? m + (Modifiers.mac ? " " : " + ") + k : m + (Modifiers.mac ? "" : "+") + k;
	};
	const byId = id => list.find(b => b.id === id) || null;
	return { bind, list: () => list.slice(), byId, label, modsLabel, free, seen: () => seen.slice() };
})();
