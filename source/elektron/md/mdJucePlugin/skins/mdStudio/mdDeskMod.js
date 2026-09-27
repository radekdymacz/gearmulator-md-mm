"use strict";
/* App-only modulators (Control workspace): App LFO and Random sources and their links to kit
   parameters, as the "md-desk/modulators" document (doc/modern-ux/data-contract.md). The page
   edits a copy and sends it whole ("modSet"); the desk runs it on the machine's own steps and
   sends CCs within the CC budget (mdDesk/mdDeskMod.h). Nothing here knows the DOM. */
const Mods = {
	doc: { schema: "md-desk/modulators", version: 1, sources: [], links: [] },
	values: [], cc: 0, limit: 300,
	RATES: ["1/16", "1/8", "1/4", "1/2", "1", "2", "4"],
	source(id) { return this.doc.sources.find(s => s.id === id); },
	valueOf(id) { const i = this.doc.sources.findIndex(s => s.id === id); return i >= 0 && this.values[i] != null ? this.values[i] : 64; },
	linksOf(id) { return this.doc.links.map((l, li) => ({ l, li })).filter(o => o.l.source === id); },
	add(kind) {
		const pre = kind === "lfo" ? "lfo" : "rnd"; let n = 1;
		while (this.source(pre + n)) n++;
		const s = kind === "lfo" ? { id: pre + n, label: "LFO " + String.fromCharCode(64 + n), kind: "lfo", shape: 0, rate: "1/2", depth: 100, smooth: 30 }
			: { id: pre + n, label: "Random " + String.fromCharCode(64 + n), kind: "random", shape: 0, rate: "1/16", depth: 100, smooth: 30 };
		this.doc.sources.push(s); return s.id;
	},
	remove(id) { this.doc.sources = this.doc.sources.filter(s => s.id !== id); this.doc.links = this.doc.links.filter(l => l.source !== id); },
	link(id, track, param) {
		if (this.doc.links.some(l => l.source === id && l.track === track && l.param === param)) return false;
		this.doc.links.push({ source: id, track, param, min: 0, max: 127, curve: "lin", invert: false }); return true;
	},
	onMessage(m) { this.doc = m.doc; this.values = m.values || []; this.cc = m.ccPerSecond || 0; this.limit = m.ccLimit || 300; this.runs = m.runs || "editor"; }
};
