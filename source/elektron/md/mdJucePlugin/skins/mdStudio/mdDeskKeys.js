"use strict";
/* The page's key map (P5): every shortcut is an entry here. The ? overlay is generated from it, so it
   lists what the keys really do. An entry with run() is dispatched by the one handler below (in order,
   the first match wins); an entry without run() is handled next to its own code (a panel, a focused
   control) and only described here. Nothing here knows the DOM of the workspaces. */
const Keys = (() => {
	const list = [];
	const norm = e => e.key === " " ? "Space" : e.key.length === 1 ? e.key.toUpperCase() : e.key;
	/* {keys: ["Z"], mod: "cmd"|"alt"|"shift"|"cmd+shift"|"", group, does, when?: () => bool, run?: e => void, field?: true} */
	function bind(entry) { list.push(Object.assign({ mod: "", when: null, field: false }, entry)); }
	function modOf(e) { return [e.metaKey || e.ctrlKey ? "cmd" : "", e.altKey ? "alt" : "", e.shiftKey ? "shift" : ""].filter(Boolean).join("+"); }
	document.addEventListener("keydown", e => {
		const inField = e.target.closest?.("input,select,textarea,[role=slider]"), k = norm(e), m = modOf(e);
		for (const b of list) {
			if (!b.run || !b.keys.includes(k)) continue;
			const want = b.mod || "", ok = want === m || (want === "" && m === "shift" && k.length === 1 && !/[A-Z]/.test(k));
			if (!ok || (inField && !b.field) || (b.when && !b.when())) continue;
			e.preventDefault(); b.run(e); return;
		}
	});
	const label = b => [...(b.mod ? b.mod.split("+").map(x => ({ cmd: "⌘", alt: "⌥", shift: "⇧" }[x])) : []), b.keys.map(k => ({ Space: "Space", ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓", Escape: "Esc", Delete: "Delete", Backspace: "⌫", Enter: "Enter" }[k] || k)).join(" / ")].join("");
	return { bind, list: () => list.slice(), label };
})();

/* ===== The ? overlay ===== */
function drawKeys() {
	const pop = $("#keyspop"); if (!pop) return;
	const groups = [];
	for (const b of Keys.list()) { if (b.mapping && !S.mapping) continue; let g = groups.find(x => x.name === b.group); if (!g) groups.push(g = { name: b.group, rows: [] }); g.rows.push(b); }
	pop.innerHTML = `<div class="libhead"><span class="cap">Keys</span><span class="note">Every shortcut of the editor, from its key map. ? or Esc closes.</span><button class="libx" data-keysx="1">Esc</button></div>
	<div class="keysgrid">${groups.map(g => `<section class="card"><header><h3>${g.name}</h3></header><div class="keyrows">${g.rows.map(b => `<div class="keyrow"><kbd>${Keys.label(b)}</kbd><span>${b.does}</span></div>`).join("")}</div></section>`).join("")}</div>`;
	pop.hidden = false;
	const r = $(".lcdpanel").getBoundingClientRect(), top = Math.max(16, r.bottom + 8);
	pop.style.top = (top + scrollY) + "px"; pop.style.maxHeight = Math.max(240, innerHeight - top - 12) + "px"; pop.style.left = Math.max(16, (document.documentElement.clientWidth - pop.offsetWidth) / 2 + scrollX) + "px";
}
function toggleKeys(on) { const pop = $("#keyspop"); if (!pop) return; if (on ?? pop.hidden) drawKeys(); else pop.hidden = true; }
Keys.bind({ keys: ["?"], group: "Help", does: "This list of keys", run: () => toggleKeys() });
Keys.bind({ keys: ["Escape"], group: "Help", does: "Close the list of keys", when: () => !$("#keyspop").hidden, run: () => toggleKeys(false) });
document.addEventListener("click", e => { const pop = $("#keyspop"); if (!pop || pop.hidden) return; if (e.target.closest("[data-keysx]") || !pop.contains(e.target)) pop.hidden = true; }, true);
