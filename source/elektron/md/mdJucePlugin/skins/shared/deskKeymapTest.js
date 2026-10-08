"use strict";
/* Both editors' key maps as data (K0, doc/modern-ux/DESIGN-keymap.md §6): every entry of each page's map has an id,
   unique in its map, and a scope; an id both editors use means the same keys and modifiers on both (the parity rule,
   P7), unless it is listed below as a known difference with its reason; the committed doc/modern-ux/keymap.json and
   the guide's key tables (site/public/guide/index.html) are what scripts/keymap-export.js makes from the maps now.
     node deskKeymapTest.js */
const path = require("path");
const X = require(path.join(__dirname, "../../../../../../scripts/keymap-export.js"));
const fs = require("fs");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const md = X.entries(X.load("md")), mm = X.entries(X.load("mm"));
check(md.length > 60 && mm.length > 50, `both maps load: ${md.length} MD, ${mm.length} MM entries`);

/* ---- ids and scopes ---- */
const SCOPES = new Set(["any", "seq", "sound", "mix", "sampler", "song", "control", "perform", "library"]);
for (const [name, list] of [["MD", md], ["MM", mm]]) {
	const noId = list.filter(e => !e.id || !/^[a-z0-9]+(-[a-z0-9]+)*$/.test(e.id));
	check(!noId.length, `${name}: every entry has an id (lower-case words joined by -)` + (noId.length ? ": " + noId.map(e => e.keys.join(" ") + " " + e.does.slice(0, 30)).join("; ") : ""));
	const ids = list.map(e => e.id), twice = [...new Set(ids.filter((id, i) => ids.indexOf(id) !== i))];
	check(!twice.length, `${name}: no id twice` + (twice.length ? ": " + twice.join(", ") : ""));
	const badScope = list.filter(e => !e.scope || !e.scope.split(" ").every(s => SCOPES.has(s)));
	check(!badScope.length, `${name}: every entry has a scope (any, or the workspaces it acts in)` + (badScope.length ? ": " + badScope.map(e => e.id + "=" + e.scope).join(", ") : ""));
	const pointerRun = list.filter(e => e.area && e.dispatched);
	check(!pointerRun.length, `${name}: a pointer gesture (area) is described, never dispatched as a key` + (pointerRun.length ? ": " + pointerRun.map(e => e.id).join(", ") : ""));
}

/* ---- parity: an id on both editors is the same keys and modifiers ---- */
/* the known differences, each with its reason (DESIGN-keymap.md); remove a line when the slice that ends it lands */
const DIFFER = {
	"tap-tempo": "D6 / K6: the MD keeps T (and B) until its black keys come; MM taps on B only (T is F♯)",
	"piano-run": "D6 / K6: the MD plays the white keys only until its black keys come"
};
const sig = e => (e.mod || "") + " " + (e.code ? [e.code] : e.keys).join(",");
const mmById = new Map(mm.map(e => [e.id, e]));
const both = md.filter(e => mmById.has(e.id));
check(both.length > 30, `the editors share ${both.length} ids`);
const unlike = both.filter(e => sig(e) !== sig(mmById.get(e.id)) && !DIFFER[e.id]);
check(!unlike.length, "an id both editors use has the same keys and modifiers on both" + (unlike.length ? ": " + unlike.map(e => `${e.id} (MD ${sig(e)} / MM ${sig(mmById.get(e.id))})`).join("; ") : ""));
const stale = Object.keys(DIFFER).filter(id => { const a = md.find(e => e.id === id), b = mmById.get(id); return !a || !b || sig(a) === sig(b); });
check(!stale.length, "every listed difference is still one" + (stale.length ? ": " + stale.join(", ") : ""));
/* the same key and modifiers under two ids (one concept, two names) would slip past the rule above */
const sigsMd = new Map();
for (const e of md.filter(e => e.dispatched && !e.hidden)) { const k = sig(e) + " " + e.scope; sigsMd.set(k, [...(sigsMd.get(k) || []), e.id]); }
const renamed = mm.filter(e => e.dispatched && !e.hidden && sigsMd.has(sig(e) + " " + e.scope) && !sigsMd.get(sig(e) + " " + e.scope).includes(e.id));
check(!renamed.length, "the same key in the same scope has the same id on both" + (renamed.length ? ": " + renamed.map(e => `${e.id} / ${sigsMd.get(sig(e) + " " + e.scope).join(" or ")}`).join(", ") : ""));

/* ---- the exported data and the guide are what the maps make now ---- */
const out = X.build();
check(fs.existsSync(X.JSON_OUT) && fs.readFileSync(X.JSON_OUT, "utf8") === out.json, "doc/modern-ux/keymap.json is up to date (node scripts/keymap-export.js)");
check(fs.readFileSync(X.GUIDE, "utf8") === out.html, "the guide's key tables are the maps' (node scripts/keymap-export.js)");
const guideIds = ["undo", "play-stop", "mute-all", "step-accent"].filter(id => !md.some(e => e.id === id));
check(!guideIds.length, "the guide's staples are in the MD map" + (guideIds.length ? ": missing " + guideIds.join(", ") : ""));

console.log(failures ? `${failures} failure(s)` : "deskKeymapTest: all passed");
process.exit(failures ? 1 : 0);
