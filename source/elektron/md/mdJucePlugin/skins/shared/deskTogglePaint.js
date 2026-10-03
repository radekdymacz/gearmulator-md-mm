"use strict";
/* A drag across toggle keys (the M and S keys), one module for both editors (skins/shared/): pure, values in,
   values out; each page wires it to its own pointer and its own commands (mdDeskLive.js, the MM mockup's
   130-main.js). The DAW convention: the pressed key toggles and its new state is what the drag paints; every
   other key of the same group the pointer crosses becomes that state, once (a key crossed again is not toggled
   back); a key already in that state is left alone, so nothing is sent for it. A click is a one-key paint.
     paint   {group, value, seen: Set of keys}: a value, replaced per key, never changed in place
     begin(group, key, on)        -> {paint, change}: the first key; change = its new state (always !on)
     visit(paint, group, key, on) -> {paint, change}: change = the state to set, or null (another group, a key
                                     already crossed, or one already in that state)
     changes(states, group, keys) -> [[key, value]...]: a whole drag over keys (states: key -> on), in order
     points(from, to, step)       -> [{x, y}...]: the points of the segment from one pointer position to the next,
                                     every step px, to (not from) included: a fast drag's events lie far apart
                                     and would skip the keys between them; each page looks under every point
   Tested in node by deskTogglePaintTest.js. */
const TogglePaint = (() => {
	function begin(group, key, on) {
		const value = !on;
		return { paint: { group, value, seen: new Set([key]) }, change: value };
	}
	function visit(paint, group, key, on) {
		if (!paint || group !== paint.group || paint.seen.has(key)) return { paint, change: null };
		const next = { group: paint.group, value: paint.value, seen: new Set(paint.seen).add(key) };
		return { paint: next, change: !!on === paint.value ? null : paint.value };
	}
	function changes(states, group, keys) {
		const on = new Map(Object.entries(states).map(([k, v]) => [String(k), !!v])), out = [];
		if (!keys.length) return out;
		let r = begin(group, String(keys[0]), on.get(String(keys[0])));
		out.push([String(keys[0]), r.change]); on.set(String(keys[0]), r.change);
		for (const k of keys.slice(1).map(String)) {
			r = visit(r.paint, group, k, on.get(k));
			if (r.change != null) { out.push([k, r.change]); on.set(k, r.change); }
		}
		return out;
	}
	function points(from, to, step = 4) {
		if (!from) return [{ x: to.x, y: to.y }];
		const dx = to.x - from.x, dy = to.y - from.y, n = Math.max(1, Math.ceil(Math.hypot(dx, dy) / step));
		return Array.from({ length: n }, (_, k) => ({ x: from.x + dx * (k + 1) / n, y: from.y + dy * (k + 1) / n }));
	}
	return { begin, visit, changes, points };
})();
