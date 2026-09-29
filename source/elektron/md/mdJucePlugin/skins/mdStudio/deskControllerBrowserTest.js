"use strict";
/* The CONTROL workspace's MIDI devices (deskController.js, DESIGN-tr06.md) in a real browser, on both shipped
   pages (mdStudio.html, mmStudio.html) in their dev mode (?dev=1: the bridge talks to window.gmDev, a fake
   host here). What node's stand-in document cannot show: the workspace opens on a tile per MIDI input, a
   tile opens its view and DEVICES comes back; a generic device shows the page's own mapping matrix (MIDI
   Learn); the TR-06's view: the page's key-style dropdowns (enhanceSelects, openK, the #kpop list) work and
   a pick becomes the host's command, the live MIDI monitor updates in place (nothing blinks), CLEAR, a
   found TR-06, another channel, a DAW's tiles; the engine menu does not list the controller. Simulated
   documents only; nothing of the user's is read or written (a throw-away browser profile in the temp
   folder).
     node deskControllerBrowserTest.js            (exits 77 when no Chrome is found; CHROME=<path> to choose)
*/
const fs = require("fs"), path = require("path"), http = require("http"), os = require("os");
const { spawn } = require("child_process");

const SKINS = path.join(__dirname, "..");
const CHROME = process.env.CHROME || ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
	"/Applications/Chromium.app/Contents/MacOS/Chromium", "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser"]
	.find(p => fs.existsSync(p));
if (!CHROME) { console.log("deskControllerBrowserTest: SKIP (no Chrome; set CHROME)"); process.exit(77); }

/* the plug-in's "controller" document, as deskController::pageDocument makes it (the node page test checks
   the same shape against both contracts) */
const VOICES = [["BD", "BASS DRUM", [36, 35]], ["SD", "SNARE DRUM", [38, 40]], ["LT", "LOW TOM", [47, 45]], ["HT", "HIGH TOM", [50, 48]], ["CY", "CYMBAL", [49]], ["OH", "OPEN HIHAT", [46]], ["CH", "CLOSED HIHAT", [42, 44]]];
function controllerDoc(m) {
	const mm = m === "mm";
	return {
		schema: "desk/controller", version: 1, machine: m, profile: "off", profiles: [{ id: "off", label: "Off" }, { id: "tr06", label: "Roland TR-06" }],
		channel: 10, knobMode: "relative", about: "shipped mapping", tracks: mm ? 6 : 16, selected: 0, machineName: mm ? "SWAVE-SAW" : "TRX-BD", known: true, warning: "",
		voices: VOICES.map(([voice, label, notes], v) => Object.assign({ voice, label, notes, t: mm ? Math.min(v, 5) : v, out: { ch: 1, note: 36 + v } }, mm ? { note: 60 } : {})),
		knobs: [{ cc: 24, label: "BD LEVEL", i: 0, name: mm ? "SYN A" : "SYN 1", own: mm ? "UNIL" : "PTCH" }, { cc: 20, label: "BD TUNE", i: -1, name: "", own: "" }].map(k => mm ? Object.assign({ pg: k.i < 0 ? -1 : 0 }, k) : k),
		targets: mm ? [{ pg: 0, i: 0, name: "SYN A", own: "UNIL" }, { pg: 2, i: 0, name: "FLTR BASE", own: "" }] : [{ i: 0, name: "SYN 1", own: "PTCH" }, { i: 16, name: "DIST", own: "" }],
		last: null, named: true, inputs: [{ name: "TR-06", on: true, tr06: true }, { name: "IAC Driver Bus 1", on: true, tr06: false }], device: "", activity: null
	};
}
/* the MD page renders a workspace only once it has the machine's documents (mdDeskModelTest.js's fixtures) */
const HEX62 = "0".repeat(62);
const mdTrack = () => ({ machine: "GND-EMPTY", model: 0, level: 100, synth: Array(8).fill(10), effects: Array(8).fill(20), routing: Array(8).fill(30),
	lfo: { track: 0, param: 0, shape1: 0, shape2: 0, update: 0 }, muteGroup: null, trigGroup: null });
const MD_DOCS = [
	{ type: "catalogue", doc: { schema: "md-desk/machines", version: 1, machines: [], enums: { tempoMultipliers: ["1X", "2X", "3/4X", "3/2X"], masterFx: ["rhythmEcho", "gateBox", "eq", "dynamix"],
		outputs: ["MAIN", "A", "B", "C", "D", "E", "F"], lfoFields: ["track", "param", "shape1", "shape2", "update"], lfoUpdates: ["FREE", "TRIG", "HOLD"], lfoParams: { SPD: 21, DEPTH: 22, SHMIX: 23 } } } },
	{ type: "doc", kind: "pattern", doc: { schema: "md-desk/pattern", version: 2, slot: 5, kit: 3, length: 16, totalLength: 16, tempoMultiplier: "1X", swingAmount: 0, accentAmount: 0,
		accent: { editAll: 0, steps: [] }, slide: { editAll: 0, steps: [] }, swing: { editAll: 0, steps: [] },
		tracks: Array.from({ length: 16 }, () => ({ trigs: [], accent: [], slide: [], swing: [] })), locks: [], firmware: { format: { version: 2, revision: 0 }, lockedRowsField: 0 } } },
	{ type: "doc", kind: "kit", doc: { schema: "md-desk/kit", version: 2, slot: 3, name: "KIT", tracks: Array.from({ length: 16 }, mdTrack),
		masterFx: { rhythmEcho: Array(8).fill(0), gateBox: Array(8).fill(1), eq: Array(8).fill(2), dynamix: Array(8).fill(3) }, firmware: { format: { version: 2, revision: 0 }, lfoState: Array(16).fill(HEX62) } } },
	{ type: "machine", doc: { schema: "md-desk/machine", version: 1, pattern: { current: 5 }, kit: { current: 3, working: "clean" }, song: {}, desk: {}, engines: [],
		history: { undo: false, redo: false, undoCount: 0, redoCount: 0 }, lifecycle: "ready", input: true, midi: true, lifecycleText: "",
		capabilities: { engine: "emu", label: "EMU OS 1.63", about: "", can: { transport: true, panelKeys: true, liveRecord: true, chains: false, lcd: true, workingKitMemory: true, mutesFromMemory: true, sampleNames: true, modulators: true },
			reasons: {}, values: { dumps: "direct" } }, clipboard: { steps: false, sound: true, songRow: false, kit: 7, pattern: null } } }];

/* runs in the page, after its own scripts: the scenario, its findings in window.__ctlResult */
const harness = m => `(() => {
	const out = { errors: [], checks: [] }, sent = [];
	addEventListener("error", e => out.errors.push(String(e.message || e)));
	window.gmDev = batch => { for (const c of batch) sent.push(c); };
	const check = (ok, what) => out.checks.push([!!ok, what]);
	const wait = ms => new Promise(r => setTimeout(r, ms));
	const doc = extra => Object.assign(${JSON.stringify(controllerDoc(m))}, extra || {});
	const recv = d => gm.recv([{ type: "controller", doc: d }]);
	const click = el => el && el.dispatchEvent(new MouseEvent("click", { bubbles: true, cancelable: true }));
	const $ = q => document.querySelector(q), text = q => ($(q) || {}).textContent || "";
	/* a pick in the page's own dropdown: its key (.kselbtn, data-for the select's id), then the option in #kpop */
	const pick = async (scope, id, value) => {
		const b = document.querySelector(scope + ' .kselbtn[data-for="' + id + '"]');
		if (!b) return "no key for " + id;
		click(b); await wait(20);
		if ($("#kpop").hidden) return "the list did not open for " + id;
		const o = document.querySelector('#kpop .kopt[data-v="' + value + '"]');
		if (!o) return "no option " + value + " for " + id;
		click(o); await wait(120);
		return "";
	};
	const sentOp = (op, f) => sent.some(c => c.op === op && (!f || f(c)));
	const act = (seq, last, elsewhere, ccs, notes) => ({ seq, last, elsewhere: elsewhere ?? null, ccs: ccs || [], notes: notes || [] });
	const tile = kind => document.querySelector('#ctlroot .ctltile[data-kind="' + kind + '"]');
	(async () => {
		try {
			await wait(300);
			${m === "md" ? `gm.recv(${JSON.stringify(MD_DOCS)}); await wait(100);` : ""}
			recv(doc());
			await wait(50);
			/* the engine menu no longer lists the controller */
			const eng = $("#engsel");
			check(eng && ![...eng.options].some(o => o.value === "ctl" || /CONTROLLER/.test(o.text)), "the engine menu has no CONTROLLER entry");
			/* the CONTROL workspace: the devices, a tile per MIDI input */
			click([...document.querySelectorAll("#tabs button")].find(t => t.dataset.ws === "control")); await wait(150);
			check($("#ctlroot") && document.querySelectorAll("#ctlroot .ctltile").length === 2 && tile("tr06") && /IAC Driver Bus 1/.test(text("#ctlroot")), "the CONTROL workspace opens on the devices: the TR-06 and IAC tiles");
			check(tile("tr06").querySelector("svg") && tile("tr06").querySelector(".led") && !tile("tr06").querySelector(".led.on") && /PROFILE OFF/.test(tile("tr06").textContent), "the TR-06 tile: its icon, its LED dark (the profile off)");
			check(!document.querySelector("[id=ctlbar],[id=ctlkey],[id=ctlpop]") && !$(".ctlui"), "no bar, no TR-06 MAP…, no mapping matrix on the devices view");
			check(!sentOp("ctlWatch", c => c.on === true), "the devices do not watch what arrives");
			/* a generic device: the page's own mapping matrix (MIDI Learn) */
			click(tile("dev")); await wait(150);
			check($("#ctlroot .ctlui") && /Mapping matrix/.test(text("#ctlroot .ctlui")) && $("#ctl-back") && /IAC Driver Bus 1/.test(text("#ctlroot .ctlhead")), "IAC: the mapping matrix (MIDI Learn) behind its tile, a DEVICES key");
			click($("#ctl-back")); await wait(150);
			check(tile("tr06") && !$("#ctlroot .ctlui"), "DEVICES: back to the tiles");
			/* the TR-06's view */
			click(tile("tr06")); await wait(150);
			check($("#ctl-profile") && $("#ctl-channel") && $("#ctl-knobmode") && $("#ctl-clear") && $("#ctlmon"), "the TR-06: profile, channel, knob mode, the MIDI monitor, CLEAR");
			check(sentOp("ctlWatch", c => c.on === true), "the TR-06's view shows: the page asks for what arrives (ctlWatch on)");
			sent.length = 0;
			let why = await pick("#ctlroot", "ctl-profile", "tr06");
			check(!why && sentOp("ctlSet", c => c.profile === "tr06"), "PROFILE: a pick sends ctlSet profile tr06 " + why);
			why = await pick("#ctlroot", "ctl-channel", "4");
			check(!why && sentOp("ctlSet", c => c.channel === 4), "CHANNEL: a pick sends ctlSet channel 4 " + why);
			why = await pick("#ctlroot", "ctl-knobmode", "absolute");
			check(!why && sentOp("ctlSet", c => c.knobMode === "absolute"), "KNOBS: a pick sends ctlSet knobMode absolute " + why);
			why = await pick("#ctlroot", "ctl-knob-24", ${JSON.stringify(m === "mm" ? "2:0" : "-1:16")});
			check(!why && sentOp("ctlKnob", c => c.cc === 24 && c.i === ${m === "mm" ? 0 : 16}), "a knob's target: a pick sends ctlKnob " + why);
			why = await pick("#ctlroot", "ctl-voice-SD", "4");
			check(!why && sentOp("ctlVoice", c => c.voice === "SD" && c.t === 4), "a voice's track: a pick sends ctlVoice " + why);
			/* a redraw while a list is open: the pick still lands (stable ids) */
			{
				click(document.querySelector('#ctlroot .kselbtn[data-for="ctl-channel"]')); await wait(20);
				recv(doc({ warning: "redrawn" })); await wait(20);
				click(document.querySelector('#kpop .kopt[data-v="7"]')); await wait(120);
				check(sentOp("ctlSet", c => c.channel === 7), "a pick after the view redrew still sends its command");
			}
			/* a TR-06 among the enabled MIDI inputs, the profile off: the view says so, one click turns it on */
			sent.length = 0;
			recv(doc({ device: "TR-06" })); await wait(60);
			check(/TR-06 connected/.test(text("#ctlroot")) && $("#ctlroot [data-ctl=use]"), "TR-06 connected — turn the profile on, USE TR-06");
			check(!sentOp("ctlSet"), "nothing turns the profile on by itself");
			click($("#ctlroot [data-ctl=use]")); await wait(120);
			check(sentOp("ctlSet", c => c.profile === "tr06"), "USE TR-06 sends ctlSet profile tr06");
			/* the live monitor: every CC with its value, name and what it moves; the last notes; in place, no blinking */
			recv(doc({ profile: "tr06", device: "TR-06", activity: act(1, { kind: "cc", n: 24, v: 87 }, null, [{ ch: 10, cc: 24, v: 87 }, { ch: 3, cc: 71, v: 12 }], [{ ch: 10, n: 38, v: 100 }]) })); await wait(60);
			const row = $('#ctlmon tr[data-ch="10"][data-cc="24"]');
			check(row && /CC 24/.test(row.textContent) && /87/.test(row.textContent) && /BD LEVEL/.test(row.textContent) && /→ T1 ${m === "mm" ? "SYN A · UNIL" : "SYN 1 · PTCH"}/.test(row.textContent),
				"the monitor: CC 24 · 87 · BD LEVEL → T1 with the machine's own name (" + (row ? row.textContent : "no row") + ")");
			check(/CH 3/.test(text('#ctlmon tr[data-ch="3"]')) && /not the TR-06's channel/.test(text('#ctlmon tr[data-ch="3"]')), "another channel's CC, labelled with its channel");
			check(text("#ctl-notes").includes("NOTE 38 · 100 (SD)"), "the last note: NOTE 38 · 100 (SD)");
			const kept = $("#ctl-profile");
			recv(doc({ profile: "tr06", device: "TR-06", activity: act(2, { kind: "cc", n: 24, v: 90 }, null, [{ ch: 10, cc: 24, v: 90 }, { ch: 3, cc: 71, v: 12 }], [{ ch: 10, n: 38, v: 100 }]) })); await wait(60);
			check(/90/.test(text('#ctlmon tr[data-cc="24"]')) && $("#ctl-profile") === kept, "a new value updates the monitor in place (the view is not redrawn)");
			check(!document.querySelector("#ctlroot .blink") && ![...document.querySelectorAll("#ctlroot *")].some(e => getComputedStyle(e).animationName !== "none"), "nothing blinks or animates");
			sent.length = 0;
			click($("#ctl-clear")); await wait(120);
			check(sentOp("ctlClear"), "CLEAR sends ctlClear");
			/* the TR-06 on another channel */
			sent.length = 0;
			recv(doc({ device: "TR-06", activity: act(3, null, 3) })); await wait(40);
			check(/TR-06 is sending on CH 3 — set CHANNEL to 3/.test(text("#ctlroot")), "another channel: TR-06 is sending on CH 3 — set CHANNEL to 3");
			click($("#ctlroot [data-ctl=setch]")); await wait(120);
			check(sentOp("ctlSet", c => c.channel === 3), "SET CH 3 sends ctlSet channel 3");
			/* a DAW: no device names; back on the devices: Host MIDI in and the TR-06 */
			recv(doc({ named: false, inputs: null, activity: act(4, null, 3) })); await wait(40);
			check(/MIDI in on CH 3/.test(text("#ctlroot")), "a DAW (no names): MIDI in on CH 3, if that is the TR-06");
			sent.length = 0;
			click($("#ctl-back")); await wait(150);
			check(/Host MIDI in/.test(text("#ctlroot")) && tile("tr06") && document.querySelectorAll("#ctlroot .ctltile").length === 2, "a DAW's devices: Host MIDI in and the TR-06");
			check(sentOp("ctlWatch", c => c.on === false), "back on the devices: the page stops watching (ctlWatch off)");
			/* the TR-06 again, then another workspace stops watching */
			click(tile("tr06")); await wait(150);
			sent.length = 0;
			click([...document.querySelectorAll("#tabs button")].find(t => t.dataset.ws === "seq")); await wait(150);
			check(sentOp("ctlWatch", c => c.on === false), "another workspace: the page stops watching (ctlWatch off)");
		} catch (e) { out.errors.push("harness: " + e); }
		window.__ctlResult = out;
	})();
})();`;

/* the skins over http (Chrome and file: do not mix); the MM page's shared files come from mdStudio/ */
function serve() {
	const server = http.createServer((req, res) => {
		const u = new URL(req.url, "http://x");
		const [, page, name] = u.pathname.split("/");
		const m = page === "mm" ? "mm" : "md";
		if (name === "__harness.js") { res.writeHead(200, { "content-type": "text/javascript" }); res.end(harness(m)); return; }
		const dirs = m === "mm" ? ["mmStudio", "mdStudio"] : ["mdStudio"];
		const file = dirs.map(d => path.join(SKINS, d, path.basename(decodeURIComponent(name || "")))).find(f => fs.existsSync(f) && fs.statSync(f).isFile());
		if (!file) { res.writeHead(404); res.end(); return; }
		let body = fs.readFileSync(file);
		const type = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".ttf": "font/ttf" }[path.extname(file)] || "application/octet-stream";
		if (path.extname(file) === ".html") body = Buffer.from(body.toString().replace("</body>", '<script src="__harness.js"></script></body>'));
		res.writeHead(200, { "content-type": type }); res.end(body);
	});
	return new Promise(r => server.listen(0, "127.0.0.1", () => r(server)));
}

/* one page in a headless Chrome of its own (a throw-away profile), driven over the DevTools protocol:
   the harness's findings are read back from window.__ctlResult */
async function run(url) {
	const profile = fs.mkdtempSync(path.join(os.tmpdir(), "ctlbrowser-"));
	const chrome = spawn(CHROME, ["--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--user-data-dir=" + profile,
		"--window-size=1500,1000", "--remote-debugging-port=0", url], { stdio: "ignore" });
	const wait = ms => new Promise(r => setTimeout(r, ms));
	const until = async (f, ms) => { const end = Date.now() + ms; while (Date.now() < end) { const v = await f(); if (v) return v; await wait(100); } return null; };
	try {
		const active = path.join(profile, "DevToolsActivePort");
		const port = await until(() => fs.existsSync(active) && fs.readFileSync(active, "utf8").split("\n")[0], 20000);
		if (!port) return { errors: ["Chrome did not start"], checks: [] };
		const target = await until(async () => {
			try { const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json(); return list.find(t => t.type === "page" && t.url.startsWith(url.split("?")[0])); } catch (_) { return null; }
		}, 20000);
		if (!target) return { errors: ["no page target"], checks: [] };
		const ws = new WebSocket(target.webSocketDebuggerUrl);
		await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
		let id = 0; const waiting = new Map();
		ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); } };
		const evaluate = expression => new Promise(r => { const n = ++id; waiting.set(n, r); ws.send(JSON.stringify({ id: n, method: "Runtime.evaluate", params: { expression, returnByValue: true } })); });
		const text = await until(async () => { const r = await evaluate("window.__ctlResult ? JSON.stringify(window.__ctlResult) : ''"); return r.result && r.result.result && r.result.result.value; }, 60000);
		ws.close();
		return text ? JSON.parse(text) : { errors: ["no result from the page"], checks: [] };
	} finally {
		chrome.kill("SIGKILL");
		await wait(300);
		fs.rmSync(profile, { recursive: true, force: true });
	}
}

(async () => {
	const server = await serve();
	const port = server.address().port;
	let failures = 0;
	for (const [m, page] of [["md", "mdStudio.html"], ["mm", "mmStudio.html"]]) {
		const r = await run(`http://127.0.0.1:${port}/${m}/${page}?dev=1`);
		for (const [ok, what] of r.checks) { console.log((ok ? "  ok   " : "  FAIL ") + m + ": " + what); if (!ok) failures++; }
		for (const e of r.errors) { console.log("  FAIL " + m + ": page error: " + e); failures++; }
		if (!r.checks.length) failures++;
	}
	server.close();
	console.log("deskControllerBrowserTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
	process.exit(failures ? 1 : 0);
})();
