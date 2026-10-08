"use strict";
/* The keyboard view (K-view, doc/modern-ux/DESIGN-keymap.md), one for both editors: what ? opens. A drawn computer
   keyboard (its physical layout, macOS or Windows / Linux legends: ⌘ / Ctrl, ⌥ / Alt) where each key shows what it
   does on the page shown; holding or clicking a modifier (⇧, ⌥, ⌘, or FN for ⌥) shows that layer; the keys that play
   are drawn as piano keys with their notes; under it the mouse's tricks per area (steps, lock lane, values,
   library...), a few tips, and every entry of the map by group (the list ? showed before). Everything comes from the
   page's key map (Keys.list(), K0), so it cannot go stale: a key the map binds is on a key here, a gesture it
   describes is in the tricks. This page or every page; a search over all of it.
   Pure parts (no DOM): layout(mac), capsOf(entry), model(entries, opts), html(model). mount(el, opts) draws into a
   panel and wires it (its own clicks, the search, the real keyboard's modifiers while it is open). */
const KeyView = (() => {
	/* ---- the keyboard: rows of [code, legend, width in key units]; "" legend: the code's own ---- */
	const F = Array.from({ length: 12 }, (_, i) => ["F" + (i + 1), "F" + (i + 1), 1]);
	const L = s => [...s].map(c => ["Key" + c, c, 1]);
	function layout(mac) {
		const row0 = [["Escape", "esc", 1.5], ...F, ...(mac ? [] : [["Delete", "Del", 1]])];
		const row1 = [["Backquote", "`", 1], ...[..."1234567890"].map(d => ["Digit" + d, d, 1]), ["Minus", "-", 1], ["Equal", "=", 1], ["Backspace", mac ? "delete" : "⌫ Backspace", 2]];
		const row2 = [["Tab", "tab", 1.5], ...L("QWERTYUIOP"), ["BracketLeft", "[", 1], ["BracketRight", "]", 1], ["Backslash", "\\", 1.5]];
		const row3 = [["CapsLock", "caps", 1.75], ...L("ASDFGHJKL"), ["Semicolon", ";", 1], ["Quote", "'", 1], ["Enter", mac ? "return" : "Enter", 2.25]];
		const row4 = [["ShiftLeft", mac ? "⇧ shift" : "Shift", 2.25], ...L("ZXCVBNM"), ["Comma", ",", 1], ["Period", ".", 1], ["Slash", "/ ?", 1], ["ShiftRight", mac ? "⇧ shift" : "Shift", 2.75]];
		const arrows = [["ArrowLeft", "←", 1], ["Arrows", "", 1], ["ArrowRight", "→", 1]];
		const row5 = mac ? [["ControlLeft", "⌃ control", 1.25], ["AltLeft", "⌥ option", 1.25], ["MetaLeft", "⌘ command", 1.5], ["Space", "", 6], ["MetaRight", "⌘ command", 1.5], ["AltRight", "⌥ option", 1.25], ...arrows]
			: [["ControlLeft", "Ctrl", 1.5], ["MetaLeft", "Win", 1.25], ["AltLeft", "Alt", 1.25], ["Space", "", 6], ["AltRight", "Alt", 1.25], ["ControlRight", "Ctrl", 1.5], ...arrows];
		return [row0, row1, row2, row3, row4, row5];
	}
	/* the modifier a key is, on this platform (a Mac's Ctrl and the Windows key are none of the map's) */
	const modOfCode = (code, mac) => /^Shift/.test(code) ? "shift" : /^Alt/.test(code) ? "alt" : (mac ? /^Meta/ : /^Control/).test(code) ? "cmd" : null;
	/* ---- an entry's physical keys and the modifiers it wants: [{code, mod}] (none for a gesture or a description) ---- */
	const NAMED = { Space: "Space", Escape: "Escape", Enter: "Enter", Delete: "Delete", Backspace: "Backspace", Tab: "Tab",
		ArrowLeft: "ArrowLeft", ArrowRight: "ArrowRight", ArrowUp: "ArrowUp", ArrowDown: "ArrowDown",
		",": "Comma", ".": "Period", "/": "Slash", "?": "Slash", "[": "BracketLeft", "]": "BracketRight", "-": "Minus", "=": "Equal", ";": "Semicolon", "'": "Quote", "`": "Backquote" };
	function codeOf(k) {
		if (/^[A-Z]$/.test(k)) return "Key" + k;
		if (/^[0-9]$/.test(k)) return "Digit" + k;
		if (/^F\d{1,2}$/.test(k)) return k;
		return NAMED[k] || null;
	}
	const modSet = m => (m || "").split("+").filter(Boolean).sort().join("+");
	function capsOf(b) {
		if (b.area || b.tip) return [];
		const mod = modSet(b.mod);
		if (b.code) return [{ code: b.code, mod }];
		const out = [];
		for (const k of b.keys) {
			/* "A S D F G H J K L": the keys a description names */
			const parts = /^[A-Z0-9](?: [A-Z0-9])+$/.test(k) ? k.split(" ") : [k];
			for (const p of parts) {
				const code = codeOf(p); if (!code) continue;
				/* a plain character typed with ⇧ (?) is on its key's ⇧ layer */
				out.push({ code, mod: p === "?" ? modSet(mod ? mod + "+shift" : "shift") : mod, part: p });
			}
		}
		return out;
	}
	const textOf = b => { try { return String(typeof b.does === "function" ? b.does() : b.does || ""); } catch (_) { return ""; } };
	/* a key's words on its cap: the entry's short (one part per key when it names one for each: "Oct − / Oct +"), or
	   the first words of what it does */
	function shortOf(b, i) {
		if (b.short) { const parts = b.short.split(" / "); return parts.length === b.keys.length || (parts.length > 1 && parts.length === capsOf(b).length) ? parts[Math.min(i, parts.length - 1)] : b.short; }
		const t = textOf(b).replace(/^(Sequence|Song|Sound|Click|Drag): /, "").split(/[.:;(,]/)[0].trim();
		return t.length > 22 ? t.slice(0, 20).trim() + "…" : t;
	}
	const inScope = (b, page, all) => all || !b.scope || b.scope === "any" || b.scope.split(" ").includes(page);
	const matches = (b, q) => !q || (textOf(b) + " " + (b.short || "") + " " + b.keys.join(" ") + " " + (b.id || "") + " " + (b.area || "") + " " + (b.group || "")).toLowerCase().includes(q);

	/* ---- the model: what is drawn, as data ----
	   opts {mac, layer: "cmd+shift" (sorted mod names), page, all, query, label(b) (the key's words), mapping} */
	function model(entries, opts) {
		const mac = !!opts.mac, layer = modSet(opts.layer), q = (opts.query || "").trim().toLowerCase();
		const usable = entries.filter(b => !(b.mapping && !opts.mapping));
		const keyed = usable.filter(b => !b.area && !b.tip);
		/* every keyboard entry by its key and layer */
		const at = new Map();
		for (const b of keyed) capsOf(b).forEach((c, i) => { const k = c.mod + " " + c.code; at.set(k, [...(at.get(k) || []), { b, i }]); });
		/* the piano: a description naming the keys with their notes (notes: "C D E ..."), white or black */
		const piano = new Map();
		for (const b of keyed) if (b.notes) { const n = b.notes.split(" "); capsOf(b).forEach((c, i) => piano.set(c.code, { note: n[i] || "", black: /♯|#|♭/.test(n[i] || "") })); }
		/* the entries on a key, the one its words show first: a key the map dispatches before a described one, then the
		   page's own; a key with many meanings by what is open (Esc) shows its general one, scope any */
		const run = b => !!(b.run || b.dispatched);
		const pick = list => {
			const ok = list.filter(({ b }) => inScope(b, opts.page, opts.all));
			const shown = ok.filter(({ b }) => !b.hidden), l = (shown.length ? shown : ok).slice();
			const many = l.length >= 3, rank = ({ b }) => (run(b) ? 0 : 2) + (many ? (b.scope === "any" ? 0 : 1) : (b.scope === opts.page ? 0 : 1));
			return l.sort((x, y) => rank(x) - rank(y));
		};
		const rows = layout(mac).map(row => row.map(([code, legend, w]) => {
			if (code === "Arrows") {
				const half = c => { const e = pick(at.get(layer + " " + c) || []); return { code: c, legend: c === "ArrowUp" ? "↑" : "↓", entries: e.map(x => x.b.id), action: e[0] ? shortOf(e[0].b, e[0].i) : "", hit: !!q && e.some(x => matches(x.b, q)) }; };
				return { code, w, stack: [half("ArrowUp"), half("ArrowDown")] };
			}
			const mod = modOfCode(code, mac);
			if (mod) return { code, legend, w, mod, on: layer.split("+").includes(mod) };
			const e = pick(at.get(layer + " " + code) || []), p = layer === "" ? piano.get(code) : null;
			return { code, legend, w, entries: e.map(x => x.b.id), action: e[0] ? shortOf(e[0].b, e[0].i) : "", more: Math.max(0, e.length - 1),
				piano: p ? (p.black ? "black" : "white") : null, note: p ? p.note : "", hit: !!q && e.some(x => matches(x.b, q)), inert: /^(Control|Meta)/.test(code) && !mod };
		}));
		const label = opts.label || (b => b.keys.join(" / "));
		const pointer = usable.filter(b => b.area && inScope(b, opts.page, opts.all) && matches(b, q));
		const areas = [...new Set(pointer.map(b => b.area))].map(area => ({ area, rows: pointer.filter(b => b.area === area).map(b => ({ id: b.id, label: label(b), does: textOf(b) })) }));
		const tips = usable.filter(b => b.tip && inScope(b, opts.page, opts.all) && matches(b, q)).map(b => ({ id: b.id, does: textOf(b) }));
		const listed = keyed.filter(b => !b.hidden && inScope(b, opts.page, opts.all) && matches(b, q));
		const order = opts.groups || [], rank = g => { const i = order.indexOf(g); return i < 0 ? order.length : i; };
		const groups = [...new Set(listed.map(b => b.group))].sort((a, b) => rank(a) - rank(b)).map(group => ({ group, rows: listed.filter(b => b.group === group).map(b => ({ id: b.id, label: label(b), does: textOf(b) })) }));
		return { mac, layer, rows, areas, tips, groups, query: q, all: !!opts.all, page: opts.page };
	}

	/* ---- the drawing (a string; the page puts it into its panel) ---- */
	const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
	function capHtml(c) {
		if (c.stack) return `<span class="kv-stack" style="flex:${c.w} ${c.w} 0">${c.stack.map(h => `<span class="kv-cap kv-half${h.action ? " has" : ""}${h.hit ? " hit" : ""}" data-code="${h.code}" data-ids="${esc(h.entries.join(" "))}" title="${esc(h.action)}"><b>${h.legend}</b><i class="kv-act">${esc(h.action)}</i></span>`).join("")}</span>`;
		const cls = ["kv-cap", c.mod ? "mod" : "", c.on ? "on" : "", c.action ? "has" : "", c.piano ? "piano " + c.piano : "", c.hit ? "hit" : "", c.inert ? "inert" : ""].filter(Boolean).join(" ");
		const tag = c.mod ? "button" : "span";
		return `<${tag} class="${cls}" style="flex:${c.w} ${c.w} 0" data-code="${c.code}"${c.mod ? ` data-kvmod="${c.mod}" type="button" aria-pressed="${!!c.on}"` : ""}${c.entries ? ` data-ids="${esc(c.entries.join(" "))}"` : ""}${c.action ? ` title="${esc(c.action)}"` : ""}>`
			+ `<b>${esc(c.legend)}</b>${c.note ? `<em class="kv-note">${esc(c.note)}</em>` : ""}<i class="kv-act">${esc(c.action || "")}${c.more ? ` <small>+${c.more}</small>` : ""}</i></${tag}>`;
	}
	/* the head (the search, this page / all) and the body (the layers, the board, the tricks, the tips, the list): a
	   search or a layer draws the body again only, so the search field keeps its focus and what is typed */
	function parts(m, o = {}) {
		const sym = m.mac ? { shift: "⇧", alt: "⌥", cmd: "⌘" } : { shift: "Shift", alt: "Alt", cmd: "Ctrl" };
		const lay = m.layer.split("+").filter(Boolean);
		const chip = (mod, text) => `<button type="button" class="kv-chip${lay.includes(mod) ? " on" : ""}" data-kvmod="${mod}" aria-pressed="${lay.includes(mod)}">${text}</button>`;
		const rowsHtml = list => list.map(r => `<div class="kv-row"><kbd>${esc(r.label)}</kbd><span>${esc(r.does)}</span></div>`).join("");
		/* the editor's version (deskAbout.js) after the title, when the plug-in told the page */
		const ver = typeof About !== "undefined" && About.label ? `<span class="note kv-ver" title="This editor's version">${esc(About.label)}</span>` : "";
		const head = `<div class="libhead kv-head"><span class="cap">Keys</span>${ver}<span class="note">${esc(o.note || "")}</span>`
			+ `<input class="kv-search" type="search" placeholder="Search keys and gestures" value="${esc(m.query)}" aria-label="Search keys and gestures" data-kvsearch="1">`
			+ `<span class="kv-scope"><button type="button" class="kv-chip${m.all ? "" : " on"}" data-kvall="0" aria-pressed="${!m.all}">${esc(o.pageName || "This page")}</button><button type="button" class="kv-chip${m.all ? " on" : ""}" data-kvall="1" aria-pressed="${m.all}">All</button></span>`
			+ `<button class="libx" data-keysx="1">Esc</button></div>`;
		const body = `<div class="kv-layers"><span class="kv-lab">Layer</span><button type="button" class="kv-chip${lay.length ? "" : " on"}" data-kvmod="" aria-pressed="${!lay.length}">Plain</button>${chip("shift", sym.shift)}${chip("alt", sym.alt + " / FN")}${chip("cmd", sym.cmd)}`
			+ `<span class="kv-hint">Hold a key or click one to see its layer. Piano keys play the selected track.</span></div>`
			+ `<div class="kv-board ${m.mac ? "kv-osmac" : "kv-ospc"}" role="img" aria-label="The keyboard: what each key does">${m.rows.map(r => `<div class="kv-keys">${r.map(capHtml).join("")}</div>`).join("")}</div>`
			+ `<div class="kv-detail" aria-live="polite"></div>`
			+ `<div class="kv-cols"><section class="card kv-mouse"><header><h3>Mouse tricks</h3></header>${m.areas.length ? m.areas.map(a => `<h4>${esc(a.area)}</h4>${rowsHtml(a.rows)}`).join("") : `<p class="kv-none">None here.</p>`}</section>`
			+ `<section class="card kv-tips"><header><h3>Tips</h3></header>${m.tips.length ? `<ul>${m.tips.map(t => `<li>${esc(t.does)}</li>`).join("")}</ul>` : `<p class="kv-none">None here.</p>`}</section></div>`
			+ `<section class="kv-list"><h3>Every key${m.all ? "" : " on this page"}</h3><div class="keysgrid">${m.groups.map(g => `<section class="card"><header><h3>${esc(g.group)}</h3></header><div class="keyrows">${g.rows.map(r => `<div class="keyrow" data-id="${esc(r.id)}"><kbd>${esc(r.label)}</kbd><span>${esc(r.does)}</span></div>`).join("")}</div></section>`).join("")}</div></section>`;
		return { head, body };
	}
	function html(m, o = {}) { const p = parts(m, o); return p.head + `<div class="kv-body">${p.body}</div>`; }

	/* ---- the panel: draws and wires it. opts {entries(), page(), pageName(), mapping(), mac(), fn() (FN on), groups, note()} ---- */
	function mount(el, opts) {
		const st = { clicked: "", held: new Set(), all: false, query: "" };
		const layerNow = () => {
			const s = new Set(st.clicked.split("+").filter(Boolean)); st.held.forEach(m => s.add(m));
			if (opts.fn && opts.fn()) s.add("alt");
			return [...s].join("+");
		};
		const label = b => Keys.label(b);
		/* body: only the body again (a search, a layer), the head as it is */
		function draw(bodyOnly) {
			const m = model(opts.entries(), { mac: opts.mac(), layer: layerNow(), page: opts.page(), all: st.all, query: st.query, mapping: opts.mapping(), label, groups: opts.groups });
			const p = parts(m, { note: opts.note ? opts.note() : "", pageName: opts.pageName() }), b = el.querySelector(".kv-body");
			if (bodyOnly && b) b.innerHTML = p.body; else el.innerHTML = p.head + `<div class="kv-body">${p.body}</div>`;
		}
		/* a key's detail under the board: every entry on it, in words */
		function detail(code) {
			const d = el.querySelector(".kv-detail"), cap = el.querySelector(`[data-code="${code}"]`); if (!d) return;
			const ids = (cap?.dataset.ids || "").split(" ").filter(Boolean), list = opts.entries();
			d.innerHTML = ids.length ? ids.map(id => list.find(b => b.id === id)).filter(Boolean).map(b => `<div class="kv-row"><kbd>${esc(label(b))}</kbd><span>${esc(textOf(b))}</span></div>`).join("")
				: `<span class="kv-none">${esc(cap ? (cap.querySelector("b")?.textContent || code) : code)}: nothing on this layer${st.all ? "" : " here"}.</span>`;
			el.querySelectorAll(".kv-cap.sel").forEach(c => c.classList.remove("sel")); cap?.classList.add("sel");
		}
		el.addEventListener("click", e => {
			const mod = e.target.closest("[data-kvmod]");
			if (mod) { const m = mod.dataset.kvmod, s = new Set(st.clicked.split("+").filter(Boolean)); if (!m) s.clear(); else s.has(m) ? s.delete(m) : s.add(m); st.clicked = [...s].join("+"); draw(true); return; }
			const all = e.target.closest("[data-kvall]"); if (all) { st.all = all.dataset.kvall === "1"; draw(); return; }
			const cap = e.target.closest(".kv-cap[data-code]"); if (cap) detail(cap.dataset.code);
		});
		el.addEventListener("input", e => { if (e.target.matches("[data-kvsearch]")) { st.query = e.target.value; draw(true); } });
		/* the real keyboard while the panel is open (before the modal layer keeps the keys from the page): a modifier
		   shows its layer; any other key shows what it does */
		const MODS = { Shift: "shift", Alt: "alt", Meta: opts.mac() ? "cmd" : null, Control: opts.mac() ? null : "cmd" };
		for (const type of ["keydown", "keyup"]) addEventListener(type, e => {
			if (el.hidden) return;
			const m = MODS[e.key];
			if (m) { const was = layerNow(); if (type === "keydown") st.held.add(m); else st.held.delete(m); if (layerNow() !== was) draw(true); return; }
			if (type === "keydown" && !e.target.closest?.("[data-kvsearch]") && e.key !== "Escape" && e.key !== "?") detail(e.code);
		}, true);
		addEventListener("blur", () => { if (st.held.size) { st.held.clear(); if (!el.hidden) draw(); } });
		return { draw, reset() { st.held.clear(); st.clicked = ""; st.query = ""; }, state: st };
	}
	return { layout, capsOf, model, html, mount, codeOf };
})();
