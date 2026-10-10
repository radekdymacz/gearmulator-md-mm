"use strict";
/* The pages on an older WebKit (B-001, 2026-10-07). On a Mac the editor's web view is the system's WebKit, the one
   the installed Safari brought: macOS 12 with Safari 15 is WebKit 15. The stylesheets use colour mixes
   (color-mix(), WebKit 16.2): an engine without them drops every declaration that has one, and a custom property
   holding one (the LCD's --lrule) makes each declaration that uses it invalid. The LCD then lost its rules, the
   transport keys their boxes. This file runs first, in the page's <head> after the stylesheet, and only where the
   engine lacks a feature rewrites the page's stylesheets in place, before the first paint:
     color-mix(in srgb, A p%, B)  an rgba() of the same colour. A colour custom property set to a hex value
                                  (--ink: #38100a) gets its channels beside it (--ink-r, --ink-g, --ink-b), so a
                                  mix of variables follows the theme and plate rules exactly as the mix did:
                                  A p%, transparent -> rgba(var(--ink-r), ..., p); A p%, B -> per channel
                                  calc(A * p + B * (1 - p)). Mixes of literal colours become literal colours.
     :focus-visible (WebKit 15.4) :focus, so a rule listing it (".x:hover, .x:focus-visible") is not dropped.
     user-select                  -webkit-user-select beside it, where the engine reads only the prefixed one.
   It also adds structuredClone (WebKit 15.4) where it is missing (the Monomachine page copies plain documents).
   A modern engine keeps the stylesheets as written. ?compat=force in the page's address applies the rewrite
   anyway (to compare it with the original in a current browser). Pure text in, text out: deskCompatTest.js.
   Not rewritten: the stylesheets do without :has() and give each subgrid lines of its own first (B-054,
   deskCompatTest.js, deskSoundLayoutTest.js); scrollbar-gutter, overscroll-behavior and accent-color an older engine
   skips without a change to the layout. safari15() reads a stylesheet as WebKit 15 does, to see that in a current
   engine (the journeys md-old-webkit / mm-old-webkit, scripts/mdmm-snap.py --safari15). */
const DeskCompat = (() => {
	const NAMED = { transparent: [0, 0, 0, 0], black: [0, 0, 0, 1], white: [255, 255, 255, 1] };
	function hex(h) {
		const s = h.replace("#", "");
		if (!/^([0-9a-f]{3}|[0-9a-f]{4}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(s)) return null;
		const d = s.length <= 4 ? [...s].map(c => c + c) : s.match(/../g);
		const v = d.map(x => parseInt(x, 16));
		return [v[0], v[1], v[2], v.length > 3 ? v[3] / 255 : 1];
	}
	/* the text between "(" at _open and its ")" */
	function closing(s, open) {
		let depth = 0;
		for (let i = open; i < s.length; i++) {
			if (s[i] === "(") depth++;
			else if (s[i] === ")" && --depth === 0) return i;
		}
		return -1;
	}
	function splitTop(s) {
		const out = []; let depth = 0, start = 0;
		for (let i = 0; i < s.length; i++) {
			if (s[i] === "(") depth++;
			else if (s[i] === ")") depth--;
			else if (s[i] === "," && !depth) { out.push(s.slice(start, i)); start = i + 1; }
		}
		out.push(s.slice(start));
		return out.map(x => x.trim());
	}
	const num = x => +x.toFixed(4);
	/* one operand: {c: [r, g, b] as numbers or channel expressions, a: alpha or null when it is a variable's, p} */
	function operand(text) {
		const m = text.match(/^(.*?)(?:\s+([\d.]+)%)?$/s);
		if (!m) return null;
		let col = m[1].trim(), p = m[2] !== undefined ? +m[2] / 100 : null;
		const pre = col.match(/^([\d.]+)%\s+(.*)$/s);	/* the percentage may come first */
		if (pre && p === null) { p = +pre[1] / 100; col = pre[2].trim(); }
		const v = col.match(/^var\(\s*(--[\w-]+)\s*(?:,.*)?\)$/s);
		if (v) return { c: ["r", "g", "b"].map(k => `var(${v[1]}-${k})`), a: 1, p, isVar: true };
		if (col[0] === "#") { const h = hex(col); return h ? { c: h.slice(0, 3), a: h[3], p } : null; }
		if (NAMED[col.toLowerCase()]) { const n = NAMED[col.toLowerCase()]; return { c: n.slice(0, 3), a: n[3], p }; }
		const rgb = col.match(/^rgba?\(([^()]*)\)$/);
		if (rgb) {
			const parts = rgb[1].split(/[\s,/]+/).filter(Boolean).map(x => x.endsWith("%") ? +x.slice(0, -1) / 100 : +x);
			if (parts.length < 3 || parts.some(x => isNaN(x))) return null;
			return { c: parts.slice(0, 3), a: parts.length > 3 ? parts[3] : 1, p };
		}
		return null;
	}
	/* color-mix's arguments ("in srgb, A p%, B") as an rgba(), or null when it is not one this knows */
	function mix(args) {
		const parts = splitTop(args);
		if (parts.length !== 3 || !/^in\s+srgb$/i.test(parts[0])) return null;
		const a = operand(parts[1]), b = operand(parts[2]);
		if (!a || !b) return null;
		let p1 = a.p, p2 = b.p;
		if (p1 === null && p2 === null) p1 = p2 = .5;
		else if (p1 === null) p1 = 1 - p2;
		else if (p2 === null) p2 = 1 - p1;
		const sum = p1 + p2;
		if (sum <= 0) return null;
		const scale = Math.min(1, sum);
		p1 /= sum; p2 /= sum;
		/* premultiplied: alpha = a1 p1 + a2 p2, channel = (c1 a1 p1 + c2 a2 p2) / alpha */
		const alpha = a.a * p1 + b.a * p2;
		if (alpha <= 0) return "rgba(0,0,0,0)";
		const w1 = a.a * p1 / alpha, w2 = b.a * p2 / alpha;
		const ch = i => {
			const x = a.c[i], y = b.c[i];
			if (!w2) return typeof x === "number" ? String(Math.round(x)) : x;
			if (!w1) return typeof y === "number" ? String(Math.round(y)) : y;
			if (typeof x === "number" && typeof y === "number") return String(Math.round(x * w1 + y * w2));
			const t = (v, w) => typeof v === "number" ? String(num(v * w)) : `${v} * ${num(w)}`;
			return `calc(${t(x, w1)} + ${t(y, w2)})`;
		};
		return `rgba(${ch(0)}, ${ch(1)}, ${ch(2)}, ${num(alpha * scale)})`;
	}
	/* every color-mix() in _css replaced; one this does not know stays (and is dropped as before) */
	function replaceMixes(css) {
		let out = "", pos = 0;
		for (;;) {
			const at = css.indexOf("color-mix(", pos);
			if (at < 0) break;
			const open = at + "color-mix".length, end = closing(css, open);
			if (end < 0) break;
			const inner = replaceMixes(css.slice(open + 1, end));	/* a mix inside a mix */
			const r = mix(inner);
			out += css.slice(pos, at) + (r || "color-mix(" + inner + ")");
			pos = end + 1;
		}
		return out + css.slice(pos);
	}
	/* a colour custom property with a hex value gets its channels beside it */
	function addChannels(css) {
		return css.replace(/(--[\w-]+)(\s*:\s*)(#[0-9a-fA-F]{3,8})\b(\s*!important)?/g, (m, name, colon, h, imp) => {
			const c = hex(h);
			if (!c) return m;
			const i = imp || "";
			return `${m};${name}-r:${c[0]}${i};${name}-g:${c[1]}${i};${name}-b:${c[2]}${i}`;
		});
	}
	/* The stylesheet as WebKit 15 reads it, to see the page as macOS 12 does in a current engine (?compat=safari15,
	   scripts/mdmm-snap.py --safari15, the Sound rows' journeys): subgrid is no value (the declaration is dropped, an
	   earlier fallback stays) and a rule with :has() in its selector is dropped whole; with the colour rewrite above. */
	function safari15(css) {
		let out = "", pos = 0;
		css = css.replace(/\/\*[\s\S]*?\*\//g, "").replace(/(grid-template-(?:rows|columns)\s*:\s*)subgrid\b/g, "$1no-subgrid-in-webkit-15");
		/* a style rule is "selector{declarations}" with no brace inside its declarations; an at-rule's block is walked */
		const re = /([^{}]*)\{([^{}]*)\}/g;
		let m;
		while ((m = re.exec(css))) {
			if (!m[1].includes(":has(")) continue;
			out += css.slice(pos, m.index) + m[1].replace(/[^;{}]*$/, "");
			pos = re.lastIndex;
		}
		return rewrite(out + css.slice(pos), { colorMix: true, focusVisible: true, userSelect: true });
	}
	function rewrite(css, need) {
		let out = css;
		if (need.colorMix && out.includes("color-mix(")) out = replaceMixes(addChannels(out));
		if (need.focusVisible) out = out.replace(/:focus-visible\b/g, ":focus");
		/* WebKit reads user-select with its prefix only (a drag on a value box would select the page's text) */
		if (need.userSelect) out = out.replace(/(^|[{;\s])user-select\s*:\s*([\w-]+)/g, "$1-webkit-user-select:$2;user-select:$2");
		return out;
	}
	/* a deep copy of plain data (objects, arrays, Maps, Sets, dates, typed arrays), as structuredClone gives */
	function clone(v, seen = new Map()) {
		if (v === null || typeof v !== "object") return v;
		if (seen.has(v)) return seen.get(v);
		let out;
		if (Array.isArray(v)) { out = []; seen.set(v, out); v.forEach((x, i) => { out[i] = clone(x, seen); }); return out; }
		if (v instanceof Date) return new Date(v.getTime());
		if (ArrayBuffer.isView(v)) return v.slice();
		if (v instanceof Map) { out = new Map(); seen.set(v, out); v.forEach((x, k) => out.set(clone(k, seen), clone(x, seen))); return out; }
		if (v instanceof Set) { out = new Set(); seen.set(v, out); v.forEach(x => out.add(clone(x, seen))); return out; }
		out = {}; seen.set(v, out);
		for (const k of Object.keys(v)) out[k] = clone(v[k], seen);
		return out;
	}
	const api = { rewrite, safari15, mix, hex, clone, applied: [] };
	if (typeof document === "undefined" || typeof window === "undefined") return api;
	if (typeof window.structuredClone !== "function") { window.structuredClone = v => clone(v); api.applied.push("structuredClone"); }
	const supports = (...a) => { try { return !!(window.CSS && CSS.supports && CSS.supports(...a)); } catch (e) { return false; } };
	const old15 = /[?&]compat=safari15\b/.test(location.search), force = old15 || /[?&]compat=force\b/.test(location.search);
	const need = {
		colorMix: force || !supports("color", "color-mix(in srgb, red 50%, blue)"),
		focusVisible: force || !supports("selector(:focus-visible)"),
		userSelect: force || !supports("user-select", "none"),
	};
	if (!need.colorMix && !need.focusVisible && !need.userSelect) return api;
	const fix = css => old15 ? safari15(css) : rewrite(css, need);
	if (old15) api.applied.push("safari15");
	for (const el of [...document.querySelectorAll('style, link[rel="stylesheet"]')]) {
		if (el.tagName === "STYLE") { el.textContent = fix(el.textContent); continue; }
		/* a page served as files (a dev host): the linked stylesheet, read and put in place as a <style> */
		try {
			const x = new XMLHttpRequest();
			x.open("GET", el.href, false); x.send();
			if (x.status !== 200 && x.status !== 0) continue;
			const st = document.createElement("style");
			st.textContent = fix(x.responseText);
			el.replaceWith(st);
		} catch (e) { /* stays as linked */ }
	}
	for (const k of Object.keys(need)) if (need[k]) api.applied.push(k);
	document.documentElement.dataset.compat = api.applied.join(" ");
	return api;
})();
if (typeof module !== "undefined") module.exports = DeskCompat;
