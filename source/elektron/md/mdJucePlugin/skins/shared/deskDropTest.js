"use strict";
/* Files dropped on the window (deskDrop.js, both editors): what the page makes of a drop message, on a stand-in Bridge
   and a stand-in host (the page's question, toast and Sampler): a SysEx file opens its import window (dropSyx), one a
   drop; a ROM is asked about while a firmware runs and installed at once while the start-up card asks for one, one a
   drop, and an installed ROM ends the drop; samples go to the page's Sampler (the Monomachine has none); a file of no
   kind the editor takes is said; everything the page has to say is one toast, in the kinds' order; the frame shows
   while files are over the window. Every command sent is the contract's (both editors' schemas,
   page_contract_check.py).
     node deskDropTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
const { execFileSync } = require("child_process");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

/* the stand-ins: a document that makes plain objects, a Bridge that hands its listener the plug-in's messages */
const el = () => ({ hidden: false, className: "", textContent: "", children: [], setAttribute() { },
	appendChild(c) { this.children.push(c); return c; },
	get firstChild() { return this.children[0]; } });
const head = el(), body = el();
let listener = null;
const ctx = vm.createContext({ document: { createElement: el, head, body },
	Bridge: { onMessage: f => { listener = f; } }, console });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskDrop.js"), "utf8") + "\nthis.Drop = Drop;", ctx);
const Drop = ctx.Drop;
check(typeof listener === "function", "the page listens on the bridge at load");

/* a page's host: what it was asked to send, ask and say */
function host(over = {}) {
	const h = { sent: [], asked: [], toasts: [], samplesCalls: [], wanted: false,
		send(c) { h.sent.push(c); }, ask(html, btns) { h.asked.push({ html, btns }); }, toast(t) { h.toasts.push(t); },
		romWanted: () => h.wanted, samples(drop, items, x, y) { h.samplesCalls.push({ drop, items, x, y });
			return h.sampleNote; }, sampleNote: "", hint: "Drop here" };
	Drop.host = Object.assign(h, over);
	return h;
}
let next = 0;
const drop = (...files) => {
	const kinds = { bin: "rom", zip: "rom", syx: "sysex", wav: "sample", aif: "sample" };
	const m = { type: "drop", drop: ++next, items: files.map((f, n) => ({ n,
		kind: kinds[f.split(".").pop().toLowerCase()] || "unknown", name: f })), x: 400, y: 300 };
	listener(m);
	return m;
};
const ops = h => h.sent.map(c => c.op + ":" + c.n).join(" ");
const button = (a, label) => a.btns.find(b => b[0] === label)[2];

/* ---- a .syx: its import window ---- */
{
	const h = host(), m = drop("kits.syx");
	check(ops(h) === "dropSyx:0" && h.sent[0].drop === m.drop && !h.toasts.length && !h.asked.length,
		"a .syx: dropSyx {drop, n}, nothing asked, nothing said");
	const h2 = host(); drop("a.syx", "b.syx", "c.SYX");
	check(ops(h2) === "dropSyx:0"
		&& /One SysEx file at a time: a\.syx opens; drop b\.syx, c\.SYX after its import/.test(h2.toasts.join()),
		"three: the first opens, the others are named");
}

/* ---- a ROM ---- */
{
	const h = host(), m = drop("OS 1.63.bin");
	check(!h.sent.length && h.asked.length === 1
		&& /Install <b>OS 1\.63\.bin<\/b> as the firmware\? The machine starts again with it\./.test(h.asked[0].html),
		"while a firmware runs: asked first, nothing sent yet");
	button(h.asked[0], "Install")();
	check(ops(h) === "dropRom:0" && h.sent[0].drop === m.drop && !h.toasts.length, "Install: dropRom {drop, n}");
	const c = host(); drop("x.zip");
	button(c.asked[0], "Cancel")();
	check(!c.sent.length && !c.toasts.length, "Cancel: nothing sent, nothing said");
	const w = host({ wanted: true }); drop("rom.bin");
	check(ops(w) === "dropRom:0" && !w.asked.length, "while the start-up card asks for a firmware: installed at once");
	const e = host({ wanted: true }); drop("a.bin", "b.zip");
	check(ops(e) === "dropRom:0" && /One ROM at a time: b\.zip not used\./.test(e.toasts.join()),
		"two ROMs: the first, the other named");
	const n = host(); drop("<img src=x>.bin");
	check(!/<img/.test(n.asked[0].html) && /&lt;img src=x&gt;\.bin/.test(n.asked[0].html),
		"the file's name is text in the question, never markup");
}

/* ---- samples: the page's Sampler, or none ---- */
{
	const h = host({ sampleNote: "" }), m = drop("kick.wav", "snare.aif");
	check(h.samplesCalls.length === 1 && h.samplesCalls[0].drop === m.drop
		&& h.samplesCalls[0].items.map(i => i.n).join() === "0,1"
		&& h.samplesCalls[0].x === 400 && h.samplesCalls[0].y === 300 && !h.sent.length && !h.toasts.length,
		"samples: the page's Sampler gets the drop, its files and where they were dropped");
	const closed = host({ sampleNote: "Open the Sampler and select a ROM slot, then drop the samples." });
	drop("kick.wav");
	check(!closed.sent.length
		&& closed.toasts.join() === "Open the Sampler and select a ROM slot, then drop the samples.",
		"the Sampler's word, when it cannot take them, is the toast");
	/* the Monomachine's host (mmAdapter.js): no samples */
	const mm = host({ samples: () => "The Monomachine has no samples." }); drop("kick.wav");
	check(!mm.sent.length && mm.toasts.join() === "The Monomachine has no samples.",
		"the Monomachine: a toast, nothing sent");
	const none = host({ samples: undefined }); drop("kick.wav");
	check(!none.sent.length && none.toasts.join() === "This editor takes no samples.",
		"a page with no Sampler at all says so");
}

/* ---- unknown ---- */
{
	const h = host(); drop("notes.txt", "kits.syx", "x.pdf");
	check(ops(h) === "dropSyx:1" && h.toasts.length === 1
		&& /Not a ROM \(\.bin, \.zip\), a SysEx file \(\.syx\) or a sample \(\.wav, \.aif\): notes\.txt, x\.pdf\./
			.test(h.toasts[0]),
		"unknown files are named in the toast; the .syx among them still opens");
}

/* ---- a mixed drop: the kinds in order, one toast ---- */
{
	const h = host({ sampleNote: "Select a ROM slot, then drop the samples." });
	drop("song.syx", "kick.wav", "a.txt", "OS.bin");
	check(!h.sent.length && h.asked.length === 1, "a ROM first: asked, nothing else done before the answer");
	button(h.asked[0], "Cancel")();
	check(ops(h) === "dropSyx:0" && h.samplesCalls.length === 1 && h.toasts.length === 1
		&& /^Select a ROM slot, then drop the samples\. Not a ROM .*a\.txt\.$/.test(h.toasts[0]),
			"Cancel: then the .syx, the samples, the unknown, one toast in that order");
	const i = host(); drop("song.syx", "kick.wav", "OS.bin");
	button(i.asked[0], "Install")();
	check(ops(i) === "dropRom:2" && !i.samplesCalls.length
		&& /The machine starts again with the new ROM: drop the other files once it runs\./.test(i.toasts.join()),
		"Install: only the ROM; the others are not sent to a machine that starts again, and the toast says so");
}

/* ---- the frame while files are over the window ---- */
{
	host();
	listener({ type: "dragFiles", active: true });
	check(Drop.shown() && head.children.length === 1 && body.children[0].firstChild.textContent === "Drop here",
		"dragFiles active: the frame with the page's line");
	listener({ type: "dragFiles", active: false });
	check(!Drop.shown(), "dragFiles not active: gone");
	listener({ type: "dragFiles", active: true });
	drop("a.syx");
	check(!Drop.shown() && head.children.length === 1, "a drop takes it away too (made once)");
}

/* ---- what the page sends is each editor's contract (generated from deskHost's table, mdDeskTest / mmDeskTest) ---- */
{
	const root = path.join(__dirname, "..", "..", "..", "..", "..", "..");
	const onContract = (m, which) => { try { execFileSync("python3",
		[path.join(root, "doc/modern-ux/page_contract_check.py"),
		path.join(root, `doc/modern-ux/${which}-data-contract.schema.json`), "command"],
		{ input: JSON.stringify(m) }); return true; } catch (e) { return false; } };
	const h = host({ wanted: true }); drop("OS.bin"); const r = h.sent[0];
	const s = host(); drop("k.syx"); const x = s.sent[0];
	for (const which of ["md", "mm"]) {
		check(onContract(Object.assign({ id: 1 }, r), which) && onContract(Object.assign({ id: 2 }, x), which),
			`${which.toUpperCase()}: dropRom and dropSyx as sent are on the contract`);
		check(!onContract({ op: "dropSyx", id: 3, drop: 1, n: 0, path: "/tmp/k.syx" }, which),
			`${which.toUpperCase()}: a command with a path is not`);
	}
}

console.log(failures ? `${failures} failure(s)` : "deskDropTest: all passed");
process.exit(failures ? 1 : 0);
