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
	document.addEventListener("keydown", e => {
		const inField = e.target.closest?.("input,select,textarea,[role=slider]"), k = norm(e), m = modOf(e), top = modalTop();
		for (const b of list) {
			if (!b.run || !(b.code ? e.code === b.code : b.keys.includes(k))) continue;
			if (top && !passes(b, top)) continue;
			const want = b.mod || "", ok = want === m || (want === "" && m === "shift" && k.length === 1 && !/[A-Z]/.test(k));
			if (!ok || (inField && !b.field) || (b.when && !b.when())) continue;
			e.preventDefault(); b.run(e); return;
		}
	});
	const label = b => [...(b.mod ? b.mod.split("+").map(x => ({ cmd: "⌘", alt: "⌥", shift: "⇧" }[x])) : []), b.keys.map(k => ({ Space: "Space", ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓", Escape: "Esc", Delete: "Delete", Backspace: "⌫", Enter: "Enter" }[k] || k)).join(" / ")].join("");
	return { bind, list: () => list.slice(), label, free };
})();
