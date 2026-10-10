"use strict";
/* The Sound page's rows and the GEN / MUTATE bars, one source for both editors (0.5 slice 5):
   - soundRow (deskSoundLayout.js): a group with a screen is a column of its own, two without pair up, a lone one gets
     its note, a row without screens is flat, the page's word only on the first group of a page, the widths;
   - the bars' clicks (deskGenBar.js): the R key's "all" is ⌥ only (DESIGN-keymap.md P3: ⌘ is not "all"), MODE,
     WRITE, a value's click and ⇧-click, a scope chip replaces the scope;
   - neither page keeps a copy: the Machinedrum page loads both files, the Monomachine page (mmMockup.js) holds them as
     its shared parts, and their own files no longer define the pieces.
     node deskSoundLayoutTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const { soundRow } = require("./deskSoundLayout.js");

/* ---- soundRow ---- */
const firsts = [];
const o = { page: x => x.g, count: x => x.knobs.length, narrow: [10, 1.8, 2.6], min: "min-content", edWidth: { sample: 6 }, lone: x => x.note || "about",
	group: (x, cols, first, note) => { firsts.push([x.key, first]); return `[${x.key} ${cols}${note ? " " + note : ""}]`; } };
const G = (key, g, n, plot, extra = {}) => Object.assign({ key, g, knobs: Array(n).fill("K"), plot: plot ? "<plot>" : "" }, extra);
const row = soundRow([G("a", "syn", 2, true), G("b", "syn", 3, false), G("c", "syn", 1, false), G("d", "fx", 2, false), G("e", "fx", 4, true, { ed: "sample" })], "synrow", o);
check(/^<div class="sgrow synrow" style="grid-template-columns:/.test(row), "a row with screens: one grid, the row's class");
check(row.includes('<div class="sgcol">[a 2]</div>') && row.includes('<div class="sgcol stack">[b 3][c 3]</div>') && row.includes('<div class="sgcol lone">[d 2 about]</div>'),
	"a screen is its own column; two without pair up (as wide as the wider); a lone one says its note");
check(row.includes("minmax(min-content,2fr) minmax(min-content,3fr) minmax(min-content,2fr) minmax(min-content,6fr)"),
	"widths: a screen as its boxes (a busy row: at least 1.8), a stack its wider group, a wide editor its own: " + row.match(/columns:([^"]*)/)[1]);
check(JSON.stringify(firsts) === JSON.stringify([["a", true], ["b", false], ["c", false], ["d", true], ["e", false]]), "the page's word only on the first group of each page");
const busy = soundRow([G("a", "syn", 6, true), G("b", "syn", 6, true)], "x", o);
check(busy.includes("minmax(min-content,6fr) minmax(min-content,6fr)"), "a busy row (over ten boxes) keeps the boxes' widths");
check(soundRow([G("a", "syn", 1, true)], "x", o).includes("minmax(min-content,2.6fr)") && soundRow([G("a", "syn", 1, true), G("b", "syn", 10, false)], "x", o).includes("minmax(min-content,1.8fr)"),
	"a screen's least width: 2.6 boxes, 1.8 in a busy row");
const flatList = [G("a", "lfo", 1, false), G("b", "kit", 2, false)]; flatList.about = "what it does";
const flat = soundRow(flatList, "lforow", Object.assign({}, o, { min: "0" }));
check(flat.startsWith('<div class="sgrow flat lforow" style="grid-template-columns:minmax(0,1.4fr) minmax(0,2fr) minmax(0,4fr)">') && flat.endsWith('<p class="sgabout">what it does</p></div>'),
	"a row without screens: flat, at least 1.4 boxes a group, its note in a column after them");
check(soundRow([], "x", o) === "", "no groups, no row");

/* ---- the bars' clicks ---- */
const handlers = {};
const calls = [];
const ctx = vm.createContext({ console, S: { mut: { scope: new Set(["syn"]) } },
	document: { addEventListener: (type, fn) => { handlers[type] = fn; }, querySelector: () => null },
	genDefaults: () => calls.push("defaults"), randomise: all => calls.push("rand " + all), genKind: k => calls.push("kind " + k), genMode: m => calls.push("mode " + m),
	genVal: (k, d) => calls.push("val " + k + " " + d), renderMutStrip: () => calls.push("strip"), mutLive: () => calls.push("live") });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskGenBar.js"), "utf8") + "\nthis.B = { gbg, gv, kc, randKey, gtitle, gseg, gvDragStep };", ctx);
const click = (attr, dataset, mods = {}) => { const t = { dataset, disabled: false }; t.closest = q => q.split(",").some(s => s.includes(attr)) ? t : null;
	handlers.click(Object.assign({ target: { closest: q => t.closest(q) }, stopPropagation: () => { }, altKey: false, shiftKey: false, metaKey: false, ctrlKey: false }, mods)); return calls[calls.length - 1]; };
check(click("data-rand", {}) === "rand false" && click("data-rand", {}, { altKey: true }) === "rand true" && click("data-rand", {}, { metaKey: true }) === "rand false" && click("data-rand", {}, { ctrlKey: true }) === "rand false",
	"the R key: a click randomises the track, ⌥-click every track, ⌘ / Ctrl-click the track (P3)");
check(click("data-gen", { gen: "fill" }) === "defaults" && click("data-genkind", { genkind: "random" }) === "kind random" && click("data-genmode", { genmode: "thin" }) === "mode thin",
	"Defaults, MODE and WRITE call the page's own");
check(click(".gv[data-gv]", { gv: "k" }) === "val k 1" && click(".gv[data-gv]", { gv: "k" }, { shiftKey: true }) === "val k -1" && (calls.length = 0, click(".gv[data-gv]", { gv: "k", dragged: "1" }) === undefined),
	"a value: click up, ⇧-click down, nothing after its drag");
const before = ctx.S.mut.scope; calls.length = 0; click("data-mutsg", { mutsg: "syn:pitch" });
check(ctx.S.mut.scope !== before && ctx.S.mut.scope.has("syn:pitch") && before.size === 1 && calls.join() === "strip,live", "a group's title joins MUTATE's scope (a new value), the bar is drawn again, a live trial follows");
check(ctx.B.gvDragStep("dens") === 2 && ctx.B.gvDragStep("amt") === 2 && ctx.B.gvDragStep("k") === 1, "a drag notch: 2 for the percentages, else 1");
check(ctx.B.randKey("t").includes('data-rand="1"') && ctx.B.kc("data-gen", "fill", "↺", "Defaults", "t").includes('class="kc "') && ctx.B.gseg("data-genkind", "euclid", [["euclid", "Euclid"]], { euclid: "x" }).includes('aria-pressed="true"'),
	"the pieces' markup");

/* ---- one source ---- */
const SK = path.join(__dirname, "..");
const html = fs.readFileSync(path.join(SK, "mdStudio/mdStudio.html"), "utf8");
check(/deskGenBar\.js/.test(html) && /deskSoundLayout\.js/.test(html), "the Machinedrum page loads deskGenBar.js and deskSoundLayout.js");
const mm = fs.readFileSync(path.join(SK, "mmStudio/mmMockup.js"), "utf8");
check(mm.includes("/* ---- shared/deskGenBar.js ---- */") && mm.includes("/* ---- shared/deskSoundLayout.js ---- */"), "the Monomachine page holds them as its shared parts");
const own = [["mdStudio/mdDeskGenUi.js", fs.readFileSync(path.join(SK, "mdStudio/mdDeskGenUi.js"), "utf8")], ["mdStudio/mdDeskSound.js", fs.readFileSync(path.join(SK, "mdStudio/mdDeskSound.js"), "utf8")],
	["mm 76-gen.js / 90-sound.js", mm.slice(mm.indexOf("/* ---- 76-gen.js ---- */"), mm.indexOf("/* ---- 77-select.js ---- */")) + mm.slice(mm.indexOf("/* ---- 90-sound.js ---- */"), mm.indexOf("/* ---- 100-mix.js ---- */"))]];
const copies = own.filter(([, t]) => /const (gbg|gv|kc|gkc|randKey|gtitle|gseg)\s*=/.test(t) || /const stacks\s*=/.test(t) || /addEventListener\("wheel"/.test(t));
check(!copies.length, "no page keeps its own copy of the pieces, the row layout or the values' wheel" + (copies.length ? ": " + copies.map(c => c[0]).join(", ") : ""));
const mdCss = fs.readFileSync(path.join(SK, "mdStudio/mdDesk.css"), "utf8"), mmCss = fs.readFileSync(path.join(SK, "mmStudio/mmStudio.css"), "utf8");
const shared = f => fs.readFileSync(path.join(__dirname, f), "utf8").trim();
check(mdCss.includes(shared("deskGenBar.css")) && mmCss.includes(shared("deskGenBar.css")) && mdCss.includes(shared("deskSound.css")) && mmCss.includes(shared("deskSound.css")),
	"both stylesheets hold deskGenBar.css and deskSound.css as they are (run the sync scripts after a change)");
const sub = shared("deskSound.css"), all = (sub.match(/grid-template-rows:subgrid/g) || []).length, guarded = (sub.match(/grid-template-rows:[^;}]*;grid-template-rows:subgrid/g) || []).length;
check(all > 0 && all === guarded, `every subgrid in deskSound.css has its lines first, for an engine without subgrid (macOS 12): ${guarded} of ${all}`);
/* B-054: 0.4.0's Monomachine Sound rows took subgrid with no lines of their own; WebKit 15 (macOS 12) then put each
   title at the top of a tall row and a short screen at its foot. Every subgrid in both whole stylesheets has its own
   lines first (the rows as drawn: soundRowsCheck in the Sound rows' journeys and scripts/mdmm-snap.py --check). */
for (const [f, css] of [["mdDesk.css", mdCss], ["mmStudio.css", mmCss]]) {
	const rules = [...css.replace(/\/\*[\s\S]*?\*\//g, "").matchAll(/([^{}]*)\{([^{}]*subgrid[^{}]*)\}/g)];
	const bare = rules.filter(m => [...m[2].matchAll(/grid-template-(rows|columns)\s*:\s*subgrid/g)].some(d => !new RegExp(`grid-template-${d[1]}\\s*:(?!\\s*subgrid)[^;]+;[^}]*grid-template-${d[1]}\\s*:\\s*subgrid`).test(m[2])));
	check(rules.length > 0 && !bare.length, `${f}: every subgrid has its own lines first (${rules.length} rules)` + (bare.length ? ": " + bare.map(m => m[1].trim()).join(", ") : ""));
}

console.log(failures ? `${failures} FAILED` : "all passed");
process.exit(failures ? 1 : 0);
