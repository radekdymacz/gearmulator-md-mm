"use strict";
/* The Monomachine Editor's Sound page by function (MM-PORT-PLAN e) on the generated page (mmMockup.js): the
   groups table (85-sound-groups.js) puts every knob of every machine's SYNTHESIS page, and of the AMP, FILTER and
   EFFECTS pages, in exactly one group; every name in the table is a knob of its machine; every group's screen is
   an editor the page has (90-sound.js, ED.<name>) with its help; a group's knobs follow the manual's slots; and
   MUTATE's group scope (a group's title) moves those knobs and only those, by the page's rules (VOL TUNE INP stay).
   The layout (titles, screens and boxes level in a row, equal screens, no scroll at 1440 × 900 and 1280 × 760)
   needs a browser: the page measures it (MM-PORT-PLAN.md, phase 3).
     node mmSoundTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const page = fs.readFileSync(path.join(__dirname, "mmMockup.js"), "utf8");
const HEAD = /\n\/\* ---- (?:\d+-[\w-]+\.js|shared\/[\w-]+\.js) ---- \*\/\n/g;
const part = name => { const a = page.indexOf(`/* ---- ${name} ---- */\n`); if (a < 0) return ""; HEAD.lastIndex = a + 10; const m = HEAD.exec(page); return page.slice(a, m ? m.index : undefined); };
const data = part("40-data.js"), groups = part("85-sound-groups.js"), sound = part("90-sound.js"), genBlock = part("shared/deskGen.js"), mmGen = part("52-gen.js");
check(data && groups && sound && genBlock && mmGen, "the page has 40-data.js, 85-sound-groups.js, 90-sound.js and the generators");

const ctx = vm.createContext({ console });
vm.runInContext(data.replace(/^"use strict";/m, "") + "\n" + groups + "\n" + genBlock + "\n" + mmGen
	+ "\nthis.T = { MACH, FIXED, MM_SYN_TAB, MM_FIXED_TAB, mmSoundGroups, mmGroupCheck, mmMutate, synDefaults: typeof synDefaults === 'function' ? synDefaults : null };", ctx);
const T = ctx.T;
const machines = Object.keys(T.MACH);

/* ---- every knob once ---- */
const errs = machines.flatMap(m => T.mmGroupCheck(m));
check(!errs.length, `every knob of every page in exactly one group, for all ${machines.length} machines` + (errs.length ? ": " + errs.slice(0, 6).join("; ") : ""));
check(machines.every(m => m in T.MM_SYN_TAB) && Object.keys(T.MM_SYN_TAB).every(m => m in T.MACH), "the table has every machine of the page, and no other");
const noTable = machines.filter(m => T.mmSoundGroups(m).SYN.some(g => g.key === "syn"));
check(!noTable.length, "no machine's knob falls to the catch-all group" + (noTable.length ? ": " + noTable.join(" ") : ""));
/* a group is a few knobs (the boxes of one line), in the manual's slot order */
const big = [], order = [];
for (const m of machines) for (const pg of ["SYN", "AMP", "FLT", "EFX"]) for (const g of T.mmSoundGroups(m)[pg]) {
	if (g.idx.length > 4) big.push(`${m} ${pg} ${g.title}`);
	if (g.idx.some((i, k) => k && i < g.idx[k - 1])) order.push(`${m} ${g.title}`);
}
check(!big.length, "no group holds more than four knobs" + (big.length ? ": " + big.join(", ") : ""));
check(!order.length, "a group's knobs follow the page's slots" + (order.length ? ": " + order.join(", ") : ""));
/* the fixed pages: the same groups on every machine */
const fixedOf = m => ["AMP", "FLT", "EFX"].map(pg => T.mmSoundGroups(m)[pg].map(g => g.title + ":" + g.idx.join(",")).join("|")).join("/");
check(machines.every(m => fixedOf(m) === fixedOf(machines[0])), "AMP, FILTER and EFFECTS have the same groups on every machine: " + fixedOf(machines[0]).replace(/:[\d,]+/g, ""));

/* ---- every screen is an editor of the page, with its help ---- */
const eds = new Set([...sound.matchAll(/^ED\.(\w+)=/gm)].map(m => m[1]));
const tips = new Set([...(sound.match(/const SND_TIP=\{[\s\S]*?\};/) || [""])[0].matchAll(/(?:^|[{,\s])(\w+):"/g)].map(m => m[1]));
const used = new Set();
for (const m of machines) for (const pg of ["SYN", "AMP", "FLT", "EFX"]) for (const g of T.mmSoundGroups(m)[pg]) if (g.ed) used.add(g.ed);
const missing = [...used].filter(e => !eds.has(e)), untipped = [...used, "lfo"].filter(e => !tips.has(e));
check(!missing.length, `every group's screen is an editor of the page (${used.size} kinds)` + (missing.length ? ": missing " + missing.join(" ") : ""));
check(!untipped.length, "every screen has its help (SND_TIP)" + (untipped.length ? ": " + untipped.join(" ") : ""));
const plotted = machines.filter(m => T.mmSoundGroups(m).SYN.some(g => g.ed));
check(plotted.length >= machines.length - 2, `the synthesis page draws a picture on ${plotted.length} of ${machines.length} machines (GND-GND and GND-SIN have nothing to draw)`);

/* ---- MUTATE by group: the group's knobs, only those, by the page's rules ---- */
{
	const m = "SWAVE-SAW", g = T.mmSoundGroups(m), uni = g.SYN.find(x => x.key === "unison"), env = g.AMP.find(x => x.key === "envelope");
	const v = { SYN: [10, 20, 30, 0, 40, 50, 60, 64], AMP: [10, 20, 30, 40, 64, 100, 64, 0], FLT: Array(8).fill(30), EFX: Array(8).fill(30), LF1: Array(8).fill(1), LF2: Array(8).fill(1), LF3: Array(8).fill(1) };
	const base = [{ m, v }];
	const info = (t, pg, i) => { const n = pg === "SYN" ? T.MACH[m].p[i] : T.FIXED[pg][i]; return n ? { name: n, max: 127 } : null; };
	const spec = { tracks: [0], pages: [], extra: { 0: [...uni.idx.map(i => ["SYN", i]), ...env.idx.map(i => ["AMP", i]), ["SYN", 7], ["AMP", 5]] }, amount: 80, seed: 99, protect: ["VOL", "TUNE"] };
	const out = T.mmMutate(spec, base, info), keys = out.map(([, pg, i]) => pg + "." + i).sort();
	const want = [...uni.idx.map(i => "SYN." + i), ...env.idx.map(i => "AMP." + i)].sort();
	check(JSON.stringify(keys) === JSON.stringify(want), "a group scope moves its knobs only: " + keys.join(" ") + " (TUNE and VOL stay)");
	const both = T.mmMutate({ ...spec, pages: ["SYN"] }, base, info);
	check(both.length === new Set(both.map(([, pg, i]) => pg + i)).size, "a page and one of its groups in scope: each knob once");
	check(JSON.stringify(T.mmMutate(spec, base, info)) === JSON.stringify(out), "the same seed, the same values");
}

console.log(failures ? `FAIL (${failures})` : "PASS");
process.exit(failures ? 1 : 0);
