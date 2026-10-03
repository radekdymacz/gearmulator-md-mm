"use strict";
/* The editors' optimistic layers (DESIGN-UNIFY.md 4.4), shared by both pages: pure, no page, no machine.
   The page renders V = Overlay.over(deriveView(DocOverlay.over(Docs), ui)).
   - Overlay: [path, value] writes into the derived view, owned by a command's id.
   - DocOverlay: whole documents the page sent (set {kind, doc}), owned by a command's id, applied to the
     documents before the view is derived.
   Both leave when the plug-in answers the command: the result comes after the documents it changed, so no
   clock is needed. Loaded after deskDocs.js (DocOverlay stores through storeDoc). */
/* ---- optimistic edits (P6): an explicit overlay over the derived view ----
   A gesture that shows its edit at once says so in its command: cmd(op, args, key, optimistic),
   where optimistic is a list of [path, value] into the view. The page applies them at once and
   keeps them as overlay entries owned by that command's id; view() is the overlay applied, in
   order, to deriveView(Docs, S). An entry leaves when the plug-in answers its command (the result
   comes after the documents it changed), so a refused edit disappears and a taken one is in the
   documents. A keyed command that replaces a waiting one keeps its id, so it takes over the
   entries by path. An entry carries the document its command edits ({kind, slot}, docOf): it is
   applied only while the view shows that document, so a pattern or kit switch under a waiting edit
   never shows it on the new one. Entries hold values, never toggles: applying one to a view that already shows
   the edit changes nothing. Paths go through objects, arrays, Maps and Sets:
   - an object or array member, or a Map entry: the value, or Overlay.DELETE;
   - a Set member: true (in) or false (out);
   - a Map of Maps (the lock lanes): a write into a missing inner Map makes it, and a delete that
     empties one removes it, so the view's inner Maps are never empty. */
/* The document a command edits, from its arguments: p a pattern, k the kit (the working kit when it
   plays), the song row ops' s a song; null: none of them (the global, the machine). */
const SONG_ROW_OPS = new Set(["rowSet", "rowInsert", "rowDelete", "rowMove", "copyRow", "pasteRow"]);
function docOf(op, args) {
	if (args.p != null) return { kind: "pattern", slot: args.p };
	if (args.k != null) return { kind: "kit", slot: args.k };
	if (SONG_ROW_OPS.has(op) && args.s != null) return { kind: "song", slot: args.s };
	return null;
}
/* the view shows that document (a kind the view has no slot for: always) */
function shows(v, doc) { const at = { pattern: v.pat, kit: v.kit, song: v.songSlot }[doc.kind]; return at === undefined || at === doc.slot; }
const Overlay = (() => {
	const DELETE = Symbol("delete");
	const entries = new Map();	// path key -> { path, value, id, doc }
	const keyOf = path => path.map(String).join("\u241f");
	const tag = v => Object.prototype.toString.call(v), isMap = v => tag(v) === "[object Map]", isSet = v => tag(v) === "[object Set]";
	const clone = v => {
		if (isMap(v)) return new Map([...v].map(([k, x]) => [k, clone(x)]));
		if (isSet(v)) return new Set(v);
		if (Array.isArray(v)) return v.map(clone);
		if (v && typeof v === "object") return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, clone(x)]));
		return v;
	};
	function setIn(root, path, value) {
		const up = [];	// [container, key] on the way, for the Map-of-Maps rule
		let o = root;
		for (let i = 0; i < path.length - 1; i++) {
			let next = isMap(o) ? o.get(path[i]) : o[path[i]];
			if (next == null && isMap(o) && value !== DELETE && i === path.length - 2) { next = new Map(); o.set(path[i], next); }
			if (next == null || typeof next !== "object") return;	// that part of the view is not there (another pattern, a reset)
			up.push([o, path[i]]);
			o = next;
		}
		const k = path[path.length - 1];
		if (isSet(o)) value ? o.add(k) : o.delete(k);
		else if (isMap(o)) {
			if (value !== DELETE) { o.set(k, clone(value)); return; }
			o.delete(k);
			const [parent, pk] = up[up.length - 1] || [];
			if (!o.size && isMap(parent)) parent.delete(pk);
		}
		else if (value === DELETE) delete o[k];
		else o[k] = clone(value);
	}
	return {
		DELETE,
		/* the writes a command shows at once, owned by its id (a later write of a path owns it), on the
		   document it edits (null: whatever the view shows) */
		add(id, writes, doc = null) { for (const [path, value] of writes) { const k = keyOf(path); entries.delete(k); entries.set(k, { path, value: clone(value), id, doc }); } },
		/* its answer came: its entries leave; true when there were any */
		answered(id) { let any = false; for (const [k, e] of entries) if (e.id === id) { entries.delete(k); any = true; } return any; },
		clear() { entries.clear(); },
		size: () => entries.size,
		/* the overlay applied to a freshly derived view (which it changes and returns) */
		over(v) { for (const e of entries.values()) if (!e.doc || shows(v, e.doc)) setIn(v, e.path, e.value); return v; }
	};
})();
/* ---- documents the page sent, shown until their command is answered ----
   Entries {kind, slot, doc, id} (plus a working kit's source and pending), one a document: a later document
   for the same kind and slot takes its place and goes last. over(docs) leaves docs alone and returns a copy
   with every entry stored in it, in order (storeDoc, deskDocs.js; store: the page's kinds). */
const DocOverlay = (() => {
	const entries = new Map();	// "kind:slot" -> { kind, slot, doc, id, source, pending }
	const copy = docs => Object.fromEntries(Object.entries(docs).map(([k, v]) =>
		[k, Object.prototype.toString.call(v) === "[object Object]" ? Object.assign({}, v) : v]));
	return {
		add(id, kind, slot, doc, extra = {}) { const k = kind + ":" + slot; entries.delete(k); entries.set(k, Object.assign({ kind, slot, doc, id }, extra)); },
		answered(id) { let any = false; for (const [k, e] of entries) if (e.id === id) { entries.delete(k); any = true; } return any; },
		clear() { entries.clear(); },
		size: () => entries.size,
		over(docs, store = DOC_STORE) {
			if (!entries.size) return docs;
			const out = copy(docs);
			for (const e of entries.values()) storeDoc(out, { kind: e.kind, slot: e.slot, doc: e.doc, source: e.source, pending: e.pending }, store);
			return out;
		}
	};
})();
