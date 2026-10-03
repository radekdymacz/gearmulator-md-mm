"use strict";
/* The key map's dispatcher (P5), one file for both editors and the MM mockup (skins/shared/): every shortcut
   is a Keys.bind entry; the pages' own maps are mdDeskKeys.js and the MM mockup's 56-keys.js. */
const Keys = (() => {
	const list = [];
	const norm = e => e.key === " " ? "Space" : e.key.length === 1 ? e.key.toUpperCase() : e.key;
	/* {keys: ["Z"], mod: "cmd"|"alt"|"shift"|"cmd+shift"|"", group, does (text, or () => text when it shows state), when?: () => bool,
	   run?: e => void, field?: true, hidden?: true (dispatched, but another entry describes it in the ? overlay),
	   code?: "KeyR" (matched on the physical key, e.code: with Alt or Cmd held macOS types another character)} */
	function bind(entry) { list.push(Object.assign({ mod: "", when: null, field: false }, entry)); }
	function modOf(e) { return [e.metaKey || e.ctrlKey ? "cmd" : "", e.altKey ? "alt" : "", e.shiftKey ? "shift" : ""].filter(Boolean).join("+"); }
	document.addEventListener("keydown", e => {
		const inField = e.target.closest?.("input,select,textarea,[role=slider]"), k = norm(e), m = modOf(e);
		for (const b of list) {
			if (!b.run || !(b.code ? e.code === b.code : b.keys.includes(k))) continue;
			const want = b.mod || "", ok = want === m || (want === "" && m === "shift" && k.length === 1 && !/[A-Z]/.test(k));
			if (!ok || (inField && !b.field) || (b.when && !b.when())) continue;
			e.preventDefault(); b.run(e); return;
		}
	});
	const label = b => [...(b.mod ? b.mod.split("+").map(x => ({ cmd: "⌘", alt: "⌥", shift: "⇧" }[x])) : []), b.keys.map(k => ({ Space: "Space", ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓", Escape: "Esc", Delete: "Delete", Backspace: "⌫", Enter: "Enter" }[k] || k)).join(" / ")].join("");
	return { bind, list: () => list.slice(), label };
})();
