"use strict";
/* The older-WebKit rewrite (deskCompat.js, B-001): the mixes the stylesheets use become the same colours without
   color-mix(), a colour variable gets its channels, :focus-visible becomes :focus; and both editors' generated
   stylesheets keep no colour mix after it.
     node deskCompatTest.js */
const fs = require("fs"), path = require("path");
const C = require("./deskCompat.js");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const eq = (got, want, what) => { check(got === want, what + (got === want ? "" : `\n         got  ${got}\n         want ${want}`)); };
const all = { colorMix: true, focusVisible: true };

eq(C.mix("in srgb,var(--ink) 35%,transparent"), "rgba(var(--ink-r), var(--ink-g), var(--ink-b), 0.35)", "a variable over transparent: its channels at p");
eq(C.mix("in srgb, transparent, var(--led) 10%"), "rgba(var(--led-r), var(--led-g), var(--led-b), 0.1)", "transparent first, the percentage on the colour");
eq(C.mix("in srgb,var(--ink) 85%,var(--lcd)"), "rgba(calc(var(--ink-r) * 0.85 + var(--lcd-r) * 0.15), calc(var(--ink-g) * 0.85 + var(--lcd-g) * 0.15), calc(var(--ink-b) * 0.85 + var(--lcd-b) * 0.15), 1)",
	"two variables: per channel, opaque");
eq(C.mix("in srgb,#000 50%,#fff"), "rgba(128, 128, 128, 1)", "two literals: a literal");
eq(C.mix("in srgb,#ff0000,#0000ff"), "rgba(128, 0, 128, 1)", "no percentages: half each");
eq(C.mix("in srgb,#ff0000 20%,#0000ff 30%"), "rgba(102, 0, 153, 0.5)", "percentages under 100 %: the sum is the alpha");
eq(C.mix("in oklab,red,blue"), null, "another space: left as it is");
eq(C.rewrite("a{color:color-mix(in srgb,var(--x) 30%,transparent)!important}", all), "a{color:rgba(var(--x-r), var(--x-g), var(--x-b), 0.3)!important}", "!important kept");
eq(C.rewrite(":root{--ink:#38100a;--lcd:#e0472b}a{b:color-mix(in srgb,var(--ink),var(--lcd))}", all),
	":root{--ink:#38100a;--ink-r:56;--ink-g:16;--ink-b:10;--lcd:#e0472b;--lcd-r:224;--lcd-g:71;--lcd-b:43}a{b:rgba(calc(var(--ink-r) * 0.5 + var(--lcd-r) * 0.5), calc(var(--ink-g) * 0.5 + var(--lcd-g) * 0.5), calc(var(--ink-b) * 0.5 + var(--lcd-b) * 0.5), 1)}",
	"a colour variable gets its channels (in a stylesheet with mixes)");
eq(C.rewrite(":root{--ink:#38100a!important;}a{b:color-mix(in srgb,var(--ink) 9%,transparent)}", all),
	":root{--ink:#38100a!important;--ink-r:56!important;--ink-g:16!important;--ink-b:10!important;}a{b:rgba(var(--ink-r), var(--ink-g), var(--ink-b), 0.09)}", "an !important variable's channels too");
eq(C.rewrite(".l{--lrule:1px solid color-mix(in srgb,var(--ink) 34%,transparent)}", all), ".l{--lrule:1px solid rgba(var(--ink-r), var(--ink-g), var(--ink-b), 0.34)}",
	"a mix inside a custom property (the LCD's rules)");
eq(C.rewrite(".a{background:linear-gradient(color-mix(in srgb,var(--ink) 70%,transparent),color-mix(in srgb,var(--ink) 70%,transparent)) no-repeat}", all),
	".a{background:linear-gradient(rgba(var(--ink-r), var(--ink-g), var(--ink-b), 0.7),rgba(var(--ink-r), var(--ink-g), var(--ink-b), 0.7)) no-repeat}", "mixes inside a gradient");
eq(C.rewrite(".b:hover,.b:focus-visible{x:1}", all), ".b:hover,.b:focus{x:1}", ":focus-visible becomes :focus");
eq(C.rewrite(".b:focus-visible{x:1}", { colorMix: true }), ".b:focus-visible{x:1}", "only what the engine lacks is rewritten");
eq(C.rewrite("a{color:red}", all), "a{color:red}", "a stylesheet without mixes is unchanged");
eq(C.rewrite(".a{touch-action:none;user-select:none}.b{-webkit-user-select:text}", { userSelect: true }), ".a{touch-action:none;-webkit-user-select:none;user-select:none}.b{-webkit-user-select:text}",
	"user-select gets its prefixed twin (a prefixed one is left alone)");

/* WebKit 15 as a current engine can show it (?compat=safari15, scripts/mdmm-snap.py --safari15) */
eq(C.safari15(".a{display:grid;grid-template-rows:auto 1fr;grid-template-rows:subgrid}"), ".a{display:grid;grid-template-rows:auto 1fr;grid-template-rows:no-subgrid-in-webkit-15}",
	"subgrid is no value: the earlier lines stay");
eq(C.safari15(".x{a:1}.b:has(.c),.d{e:2}.f{g:3}"), ".x{a:1}.f{g:3}", "a rule with :has() goes whole (a selector list with it too)");
eq(C.safari15("@media (max-width:9px){.p{q:1}.b:has(i){e:2}}"), "@media (max-width:9px){.p{q:1}}", "inside an at-rule too");
eq(C.safari15("/* .b:has(x){} */.b:focus-visible{c:color-mix(in srgb,#000 50%,#fff)}"), ".b:focus{c:rgba(128, 128, 128, 1)}", "with the colour rewrite; comments go");

const v = { a: [1, { b: 2 }], m: new Map([["k", { z: 1 }]]), d: new Date(5), u: new Uint8Array([1, 2]), inf: Infinity, n: null };
const c = C.clone(v);
check(c !== v && c.a[1] !== v.a[1] && c.a[1].b === 2 && c.m.get("k").z === 1 && c.m.get("k") !== v.m.get("k") && c.d.getTime() === 5 && c.u[1] === 2 && c.u !== v.u
	&& c.inf === Infinity && c.n === null, "structuredClone's stand-in copies plain data deeply");

/* the shipped stylesheets: every colour mix understood, every variable a mix uses given its channels */
const skins = path.join(__dirname, "..");
for (const f of ["mdStudio/mdDesk.css", "mmStudio/mmStudio.css"]) {
	const css = fs.readFileSync(path.join(skins, f), "utf8");
	const out = C.rewrite(css, all);
	const left = (out.match(/color-mix\(/g) || []).length;
	check(left === 0, `${f}: ${(css.match(/color-mix\(/g) || []).length} colour mixes, ${left} left after the rewrite`);
	const used = new Set([...out.matchAll(/var\((--[\w-]+)-[rgb]\)/g)].map(m => m[1]));
	const missing = [...used].filter(n => !new RegExp(n + "-r:").test(out));
	check(missing.length === 0, `${f}: every variable in a mix has channels${missing.length ? " (missing " + missing.join(", ") + ")" : ""}`);
	check(!/:focus-visible/.test(out), `${f}: no :focus-visible left`);
	/* B-054: what WebKit 15 does not read is never what lays a page out: no :has() rule (a class from the markup
	   instead), each subgrid after lines of its own (deskSoundLayoutTest.js); the journeys md-old-webkit and
	   mm-old-webkit measure every view both ways */
	const has = [...css.replace(/\/\*[\s\S]*?\*\//g, "").matchAll(/([^{}]*:has\([^{}]*)\{/g)].map(m => m[1].trim());
	check(!has.length, `${f}: no :has() rule${has.length ? " (" + has.join(", ") + ")" : ""}`);
}
console.log(failures ? `FAIL (${failures})` : "PASS");
process.exit(failures ? 1 : 0);
