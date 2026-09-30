"use strict";
/* The start-up card's file drop (the BOOT block, the same text in both editors): what a dropped file must be,
   the pieces it goes in, and the command the page sends for a fake File. Run with node; the block is read from
   mdDeskBoot.js and run against a stub document. */
const fs = require("fs"), path = require("path");
const src = fs.readFileSync(path.join(__dirname, "mdDeskBoot.js"), "utf8");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

const handlers = {};
const stub = () => {
	const o = { hidden: false, textContent: "", innerHTML: "", className: "", dataset: {}, style: {}, children: [],
		classList: { toggle() {}, add() {}, remove() {}, contains: () => false },
		addEventListener() {}, setAttribute() {}, appendChild() {}, getContext: () => ({ fillRect() {} }),
		querySelector: () => stub() };
	return o;
};
const doc = { createElement: stub, body: stub(), documentElement: stub(),
	addEventListener: (t, f) => { handlers[t] = f; } };
const Boot = new Function("document", "performance", "requestAnimationFrame", "getComputedStyle", "btoa",
	src.slice(src.indexOf("const Boot")) + "\nreturn Boot;")(doc, { now: () => 0 }, () => 1, () => ({ getPropertyValue: () => "" }), btoa);

const fakeFile = (name, bytes) => ({ name, size: bytes.length, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) });
const rom = new Uint8Array(0x800000).map((_, i) => (i * 31 + 7) & 255);

(async () => {
	// the plan
	let p = await Boot.romDropPlan(fakeFile("a.bin", rom), "Machinedrum OS 1.63");
	check(p.ok && p.size === rom.length && p.count === Math.ceil(rom.length / 196608), "an 8 MiB .bin is planned in pieces of 192 KiB (" + p.count + ")");
	const back = Buffer.concat([...Array(p.count).keys()].map(i => Buffer.from(p.piece(i), "base64")));
	check(back.length === rom.length && back.equals(Buffer.from(rom)), "the pieces put together are the file, byte for byte");
	p = await Boot.romDropPlan(fakeFile("small.bin", new Uint8Array(1000)), "Machinedrum OS 1.63");
	check(!p.ok && /exactly 8 MiB/.test(p.text), "a .bin of another size is refused in the card: " + p.text);
	p = await Boot.romDropPlan(fakeFile("notes.txt", new Uint8Array(10)), "Machinedrum OS 1.63");
	check(!p.ok && /not a firmware image/.test(p.text), "a text file is refused: " + p.text);
	p = await Boot.romDropPlan(fakeFile("rom.zip", new Uint8Array(500)), "Machinedrum OS 1.63");
	check(p.ok && p.count === 1, "a .zip of any size below the limit goes to the host to open");
	p = await Boot.romDropPlan(null, "x");
	check(!p.ok && /Drop the firmware/.test(p.text), "no file (a dragged text): a clear message");

	// the drop handler: the command it sends for a fake File
	const sent = [], logs = [], said = [];
	Boot.host = { romBytes: (m, done) => { sent.push(m); done({ ok: true }); }, log: t => logs.push(t), say: t => said.push(t) };
	const drop = f => handlers.drop({ dataTransfer: { types: ["Files"], files: [f] }, preventDefault() {} });
	drop(fakeFile("elektron.bin", rom));
	for (let i = 0; i < 50 && sent.length < Math.ceil(rom.length / 196608); i++) await new Promise(r => setTimeout(r, 5));
	check(sent.length === 43 && sent.every((m, i) => m.index === i && m.count === 43 && m.name === "elektron.bin" && m.size === rom.length && typeof m.data === "string"),
		"a dropped file is sent as romBytes {name, size, index, count, data}, in order (" + sent.length + " messages)");
	check(logs.some(l => /^drop: page got elektron\.bin 8388608/.test(l)) && logs.some(l => /all pieces sent/.test(l)), "and the steps are logged");
	sent.length = 0;
	drop(fakeFile("sound.syx", new Uint8Array(10)));
	await new Promise(r => setTimeout(r, 20));
	check(sent.length === 0, "a .syx is left to the window (nothing sent as firmware)");
	let prevented = false;
	handlers.dragover({ dataTransfer: { types: ["Files"] }, preventDefault() { prevented = true; } });
	check(prevented, "dragover over a file is accepted (the page takes the drop)");
	prevented = false;
	handlers.dragover({ dataTransfer: { types: ["text/plain"] }, preventDefault() { prevented = true; } });
	check(!prevented, "and an internal drag (no file) is left alone");
	Boot.host.romBytes = (m, done) => done({ ok: false, errors: ["The file did not arrive in order."] });
	drop(fakeFile("elektron.bin", rom));
	await new Promise(r => setTimeout(r, 20));
	check(logs.some(l => /drop: failed: The file did not arrive/.test(l)), "a refusal from the plug-in stops the sending and is logged");
	console.log(failures ? "mdDeskBootDropTest: FAIL" : "mdDeskBootDropTest: PASS");
	process.exit(failures ? 1 : 0);
})();
