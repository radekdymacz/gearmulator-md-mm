"use strict";
/* The page's documents (DESIGN-UNIFY.md 4.3): one copy of what the plug-in published, never written by a
   gesture, shared by both editors. A "doc" message is stored where its kind keeps it, one entry a kind,
   from a list of kinds (the page's own, DOC_KINDS for the documents both machines have):
     { kind, at }       a map of slots: docs[at][slot]
     { kind, one }      one document: docs[one]
     { kind, working }  the kit that plays: docs[working] = { slot, source, pending, doc }
   The message names the slot (m.slot; the document's own slot field where it has one) and where the document
   came from (m.source, kept in docs.sources["kind:slot"] for the stored kinds). Pure but for docs. */
const DOC_KINDS = [
	{ kind: "pattern", at: "patterns" },
	{ kind: "kit", at: "kits" },
	{ kind: "song", at: "songs" },
	{ kind: "global", one: "global" },
	{ kind: "workingKit", working: "workingKit" }];
/* kind -> (docs, slot, m) that stores it */
function docStore(kinds) {
	const put = r => r.at ? (docs, slot, m) => { docs[r.at][slot] = m.doc; }
		: r.one ? (docs, slot, m) => { docs[r.one] = m.doc; }
		: (docs, slot, m) => { docs[r.working] = { slot, source: m.source, pending: !!m.pending, doc: m.doc }; };
	return Object.fromEntries(kinds.map(r => [r.kind, Object.assign(put(r), { sourced: !r.working })]));
}
const DOC_STORE = docStore(DOC_KINDS);
/* stores the message's document; returns its slot, or null for a kind the page does not know */
function storeDoc(docs, m, store = DOC_STORE) {
	const put = store[m.kind]; if (!put) return null;
	const slot = m.slot != null ? m.slot : m.doc.slot;
	put(docs, slot, m);
	if (put.sourced && m.source) docs.sources[m.kind + ":" + slot] = m.source;
	return slot;
}
