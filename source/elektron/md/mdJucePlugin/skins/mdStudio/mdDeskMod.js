"use strict";
/* App-only modulators (Control workspace): App LFO and Random sources and their links to kit
   parameters, as the "md-desk/modulators" document (doc/modern-ux/data-contract.md). Every edit
   builds a new doc (mutate) and the page sends it whole ("modSet"); the desk runs it on the
   machine's own steps and sends CCs within the CC budget (mdDesk/mdDeskMod.h). The page's own
   in-flight guard (mdDeskApp.js's modInFlight, the MM page's pattern) keeps an incoming "mod"
   message from overwriting an edit still on its way to the desk. Nothing here knows the DOM. */
const Mods = {
	doc: { schema: "md-desk/modulators", version: 1, sources: [], links: [] },
	values: [], cc: 0, limit: 300, runs: "editor",
	RATES: ["1/16", "1/8", "1/4", "1/2", "1", "2", "4"],
	source(id) { return this.doc.sources.find(s => s.id === id); },
	valueOf(id) { const i = this.doc.sources.findIndex(s => s.id === id); return i >= 0 && this.values[i] != null ? this.values[i] : 64; },
	linksOf(id) { return this.doc.links.map((l, li) => ({ l, li })).filter(o => o.l.source === id); },
	/* a new doc (shallow-copied sources and links) with fn's writes on it; never the live one in place */
	mutate(fn) {
		const doc = { schema: this.doc.schema, version: this.doc.version, sources: this.doc.sources.map(s => ({ ...s })), links: this.doc.links.map(l => ({ ...l })) };
		fn(doc);
		this.doc = doc;
		return doc;
	},
	setSource(id, patch) { return this.mutate(doc => { const s = doc.sources.find(x => x.id === id); if (s) Object.assign(s, patch); }); },
	setLink(li, patch) { return this.mutate(doc => { if (doc.links[li]) Object.assign(doc.links[li], patch); }); },
	removeLink(li) { this.mutate(doc => { doc.links.splice(li, 1); }); },
	add(kind) {
		const pre = kind === "lfo" ? "lfo" : "rnd"; let n = 1;
		while (this.source(pre + n)) n++;
		const s = kind === "lfo" ? { id: pre + n, label: "LFO " + String.fromCharCode(64 + n), kind: "lfo", shape: 0, rate: "1/2", depth: 100, smooth: 30 }
			: { id: pre + n, label: "Random " + String.fromCharCode(64 + n), kind: "random", shape: 0, rate: "1/16", depth: 100, smooth: 30 };
		this.mutate(doc => doc.sources.push(s));
		return s.id;
	},
	remove(id) { this.mutate(doc => { doc.sources = doc.sources.filter(s => s.id !== id); doc.links = doc.links.filter(l => l.source !== id); }); },
	link(id, track, param) {
		if (this.doc.links.some(l => l.source === id && l.track === track && l.param === param)) return false;
		this.mutate(doc => doc.links.push({ source: id, track, param, min: 0, max: 127, curve: "lin", invert: false }));
		return true;
	},
	/* the values and CC rate a "mod" message brings (always live, in flight or not) */
	applyLive(m) { this.values = m.values || []; this.cc = m.ccPerSecond || 0; this.limit = m.ccLimit || 300; this.runs = m.runs || this.runs || "editor"; },
	onMessage(m) { this.applyLive(m); this.doc = m.doc; }
};
