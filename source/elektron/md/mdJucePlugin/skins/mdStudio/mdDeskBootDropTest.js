"use strict";
/* The start-up card's file drop (the BOOT block, the same text in both editors): what a dropped file must be,
   the pieces it goes in, and the command the page sends for a fake File. Run with node; the block is read from
   mdDeskBoot.js and run against a stub document. */
const fs = require("fs"), path = require("path");
const src = fs.readFileSync(path.join(__dirname, "mdDeskBoot.js"), "utf8");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const handlers = {}, timers = [];
const stubs = new Map();
const stub = () => {
	const kids = new Map();
	return { hidden: false, textContent: "", innerHTML: "", className: "", dataset: {}, style: {}, children: [],
		classList: { toggle() {}, add() {}, remove() {}, contains: () => false },
		addEventListener() {}, setAttribute() {}, appendChild() {}, getContext: () => ({ fillRect() {} }),
		querySelector(sel) { if (!kids.has(sel)) kids.set(sel, stub()); return kids.get(sel); } };
};
const doc = { createElement: () => stub(), body: stub(), documentElement: stub(),
	addEventListener: (t, f) => { handlers[t] = f; } };
let card = null;
doc.createElement = () => (card = stub());
const setT = (f, ms) => { timers.push({ f, ms }); return timers.length; };
const Boot = new Function("document", "performance", "requestAnimationFrame", "getComputedStyle", "btoa", "setTimeout", "clearTimeout",
	src.slice(src.indexOf("const Boot")) + "\nreturn Boot;")(doc, { now: () => Date.now() }, () => 1, () => ({ getPropertyValue: () => "" }), btoa, setT, () => {});
let saidList = [];
const shownText = () => saidList[saidList.length - 1] || "";

const fakeFile = (name, bytes) => ({ name, size: bytes.length, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) });
const rom = new Uint8Array(0x800000).map((_, i) => (i * 31 + 7) & 255);
const tick = () => new Promise(r => setImmediate(r));

(async () => {
	// the plan
	let p = await Boot.romDropPlan(fakeFile("a.bin", rom), "Machinedrum OS 1.63");
	check(p.ok && p.size === rom.length && p.count === 32, "an 8 MiB .bin is planned in 32 pieces of 256 KiB");
	const back = Buffer.alloc(rom.length);
	for (let i = 0; i < p.count; i++) Buffer.from(p.data(i), "base64").copy(back, p.at(i));
	check(back.equals(Buffer.from(rom)), "the pieces put at their offsets are the file, byte for byte");
	p = await Boot.romDropPlan(fakeFile("small.bin", new Uint8Array(1000)), "Machinedrum OS 1.63");
	check(!p.ok && /exactly 8 MiB/.test(p.text), "a .bin of another size is refused in the card: " + p.text);
	p = await Boot.romDropPlan(fakeFile("notes.txt", new Uint8Array(10)), "Machinedrum OS 1.63");
	check(!p.ok && /not a firmware image/.test(p.text), "a text file is refused: " + p.text);
	p = await Boot.romDropPlan(fakeFile("rom.zip", new Uint8Array(500)), "Machinedrum OS 1.63");
	check(p.ok && p.count === 1, "a .zip of any size below the limit goes to the host to open");
	p = await Boot.romDropPlan(null, "x");
	check(!p.ok && /Drop the firmware/.test(p.text), "no file (a dragged text): a clear message");

	// the drop handler: the commands it sends for a fake File, four in flight, the next on each acknowledgement
	const sent = [], logs = [];
	const said = saidList;
	Boot.host = { romBytes: m => sent.push(m), log: t => logs.push(t), say: t => said.push(t) };
	const drop = f => handlers.drop({ dataTransfer: { types: ["Files"], files: [f] }, preventDefault() {} });
	drop(fakeFile("elektron.bin", rom));
	await tick(); await tick();
	check(sent.length === 4 && sent.every((m, i) => m.index === i && m.count === 32 && m.name === "elektron.bin" && m.size === rom.length && m.offset === i * 262144 && typeof m.data === "string" && m.tid > 0),
		"a dropped file goes out as romBytes {tid, name, size, count, index, offset, data}: four in flight");
	const tid1 = sent[0].tid;
	Boot.ack({ tid: tid1, index: 0, ok: true });
	check(sent.length === 5 && sent[4].index === 4, "an acknowledgement lets the next piece go");
	check(/Sending elektron\.bin.* 1\/32/.test(shownText()), "the card shows the progress: " + shownText());

	// a second drop while the first runs: the first is cancelled, the second has a newer id
	drop(fakeFile("second.bin", rom));
	await tick(); await tick();
	const second = sent.filter(m => m.name === "second.bin");
	check(second.length === 4 && second[0].tid > tid1 && logs.some(l => /earlier transfer was replaced/.test(l)), "a new drop cancels the running transfer and starts with a newer id");
	const before = sent.length;
	Boot.ack({ tid: tid1, index: 1, ok: true });
	check(sent.length === before, "an acknowledgement of the cancelled transfer sends nothing");
	for (let i = 0; i < 32; i++) Boot.ack({ tid: second[0].tid, index: i, ok: true });
	check(sent.filter(m => m.name === "second.bin").length === 32 && logs.filter(l => /^drop: 8388608 bytes in 32 pieces, \d+\.\d\d s/.test(l)).length === 1, "all pieces go out and the total is logged once");

	// silence: 5 s without an acknowledgement ends it with a message in the card
	sent.length = 0;
	drop(fakeFile("hang.bin", rom));
	await tick(); await tick();
	const timer = timers[timers.length - 1];
	check(timer.ms === 5000, "an unanswered piece starts a 5 s timer");
	timer.f();
	check(/did not answer/.test(shownText()), "and its end says so in the card: " + shownText());

	// a refusal from the plug-in ends the transfer with its text
	sent.length = 0;
	drop(fakeFile("refused.bin", rom));
	await tick(); await tick();
	Boot.ack({ tid: sent[0].tid, index: 0, ok: false, text: "The file did not arrive as announced." });
	check(/did not arrive/.test(shownText()) && logs.some(l => /drop: failed: The file did not arrive/.test(l)), "a refusal stops the sending and is shown and logged");

	sent.length = 0;
	drop(fakeFile("sound.syx", new Uint8Array(10)));
	await tick();
	check(sent.length === 0, "a .syx is left to the window (nothing sent as firmware)");
	let prevented = false;
	handlers.dragover({ dataTransfer: { types: ["Files"] }, preventDefault() { prevented = true; } });
	check(prevented, "dragover over a file is accepted (the page takes the drop)");
	prevented = false;
	handlers.dragover({ dataTransfer: { types: ["text/plain"] }, preventDefault() { prevented = true; } });
	check(!prevented, "and an internal drag (no file) is left alone");
	console.log(failures ? "mdDeskBootDropTest: FAIL" : "mdDeskBootDropTest: PASS");
	process.exit(failures ? 1 : 0);
})();
