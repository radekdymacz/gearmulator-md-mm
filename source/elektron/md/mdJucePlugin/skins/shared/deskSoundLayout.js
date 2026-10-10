"use strict";
/* The Sound page's rows, both editors (the look: deskSound.css). A row is a grid of columns over three lines
   (titles, screens, boxes). A group with a screen (x.plot) is a column of its own, as wide as its boxes but never
   narrower than about two of them; the groups without a screen pair up in one column (the upper one's boxes at the
   screens' top, the lower one's title at their foot); a lone one says what its knobs do where the screen would be.
   A row without any screen is two lines, its groups side by side (list.about: a note in a column after them).
   Pure: each page says how to draw a group and what its machine needs (mdDeskSound.js; the MM mockup's 90-sound.js).
     o.group(x, cols, first, note)  a group's markup: cols its box columns, first true for the first group of its
                                    page in the row (the page's word goes there), note what a lone group says
     o.page(x)    the group's page (first changes with it)       o.count(x)  its boxes (x.n wins)
     o.narrow     [over, w, w2]: a screen's least width in boxes, w when the row holds more than `over` boxes, else w2
     o.min        a column's least width ("min-content" or "0")  o.edWidth   {editor: width} for a wide screen
     o.lone(x)    what a lone group says */
function soundRow(list, cls, o) {
	if (!list.length) return "";
	let prev = null;
	const first = x => { const p = o.page(x); if (prev === p) return false; prev = p; return true; };
	const n = x => x.n ?? o.count(x);
	const narrow = list.reduce((a, x) => a + n(x), 0) > o.narrow[0] ? o.narrow[1] : o.narrow[2];
	const w = x => x.w ?? (o.edWidth && o.edWidth[x.ed]) ?? (x.plot ? Math.max(n(x), narrow) : n(x));
	const tracks = cols => `grid-template-columns:${cols.map(c => `minmax(${o.min},${c}fr)`).join(" ")}`;
	if (!list.some(x => x.plot)) {
		const ws = list.map(x => Math.max(1.4, w(x)));
		return `<div class="sgrow flat ${cls}" style="${tracks(list.about ? [...ws, 4] : ws)}">${list.map(x => `<div class="sgcol">${o.group(x, n(x), first(x), "")}</div>`).join("")}${list.about ? `<p class="sgabout">${list.about}</p>` : ""}</div>`;
	}
	const cols = [], stacks = [];
	for (const x of list) {
		if (x.plot) { cols.push([x]); continue; }
		const s = stacks[stacks.length - 1];
		if (s && s.length < 2) s.push(x); else { const c = [x]; stacks.push(c); cols.push(c); }
	}
	return `<div class="sgrow ${cls}" style="${tracks(cols.map(c => Math.max(...c.map(w))))}">${cols.map(c => {
		const cn = Math.max(...c.map(n)), kind = c[0].plot ? "" : c.length > 1 ? " stack" : " lone";
		return `<div class="sgcol${kind}">${c.map(x => o.group(x, cn, first(x), kind === " lone" ? o.lone(x) : "")).join("")}</div>`;
	}).join("")}</div>`;
}
/* The rows as drawn (a journey's or scripts/mdmm-snap.py's check, B-054): in a row with screens the screens take the
   row's free height, the title and the box line keep theirs. A problem each: a screen more than `slack` px below its
   title or above its boxes (0.4.0 on WebKit 15: the title at the top of a tall row, an empty band, a short screen),
   or screens of one page not as tall as each other. Rows laid out as wrapping boxes (a narrow window) are skipped. */
function soundRowsCheck(root, slack = 12) {
	const out = [], hs = [];
	const r = e => e.getBoundingClientRect();
	for (const row of root.querySelectorAll(".snd .sgrow:not(.flat)")) {
		if (getComputedStyle(row).display !== "grid" || !row.getClientRects().length) continue;
		for (const plot of row.querySelectorAll(".sg>.plot")) {
			/* the title's words, not its header: a stretched header reaches down to the screen */
			const sg = plot.parentElement, head = sg.querySelector(":scope>header h3"), boxes = sg.querySelector(":scope>.ctl,:scope>.sgline,:scope>.sgsel,:scope>.lfobody");
			const name = (head && head.textContent.trim()) || "?", p = r(plot);
			hs.push([name, Math.round(p.height)]);
			if (head && p.top - r(head).bottom > slack) out.push(`${name}: screen ${Math.round(p.top - r(head).bottom)} px below its title`);
			if (boxes && r(boxes).top - p.bottom > slack) out.push(`${name}: screen ${Math.round(r(boxes).top - p.bottom)} px above its boxes`);
		}
	}
	const lo = Math.min(...hs.map(x => x[1])), hi = Math.max(...hs.map(x => x[1]));
	if (hs.length && hi - lo > 2) out.push(`screens from ${lo} to ${hi} px tall: ${hs.map(x => x.join(" ")).join(", ")}`);
	return { screens: hs.map(x => x[1]), problems: out };
}
if (typeof module !== "undefined") module.exports = { soundRow, soundRowsCheck };
