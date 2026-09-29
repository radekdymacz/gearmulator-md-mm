"use strict";
/* The controller profile's panel (deskController.js, DESIGN-tr06.md) in a real browser, on both shipped
   pages (mdStudio.html, mmStudio.html) in their dev mode (?dev=1: the bridge talks to window.gmDev, a
   fake host here). What node's stand-in document cannot show: the page's own key-style dropdowns
   (enhanceSelects, openK, the #kpop list) work in the panel, and a pick becomes the host's command. Simulated documents only; nothing of the user's is read or written (a throw-away browser
   profile in the temp folder).
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
function controllerDoc(m, extra) {
	const mm = m === "mm";
	return Object.assign({
		schema: "desk/controller", version: 1, machine: m, profile: "off", profiles: [{ id: "off", label: "Off" }, { id: "tr06", label: "Roland TR-06" }],
		channel: 10, about: "shipped mapping", tracks: mm ? 6 : 16, selected: 0, known: true, warning: "",
		voices: VOICES.map(([voice, label, notes], v) => Object.assign({ voice, label, notes, t: mm ? Math.min(v, 5) : v, out: { ch: 1, note: 36 + v } }, mm ? { note: 60 } : {})),
		knobs: [{ cc: 24, label: "BD LEVEL", i: 0, name: mm ? "SYN A" : "SYN 1" }, { cc: 20, label: "BD TUNE", i: -1, name: "" }].map(k => mm ? Object.assign({ pg: k.i < 0 ? -1 : 0 }, k) : k),
		targets: mm ? [{ pg: 0, i: 0, name: "SYN A" }, { pg: 2, i: 0, name: "FLTR BASE" }] : [{ i: 0, name: "SYN 1" }, { i: 16, name: "DIST" }],
		last: null, devices: [], activity: null
	}, extra || {});
}

/* runs in the page, after its own scripts: the scenario, its findings in <pre id="ctltest"> */
const harness = m => `(() => {
	const out = { errors: [], checks: [] }, sent = [];
	addEventListener("error", e => out.errors.push(String(e.message || e)));
	window.gmDev = batch => { for (const c of batch) sent.push(c); };
	const check = (ok, what) => out.checks.push([!!ok, what]);
	const wait = ms => new Promise(r => setTimeout(r, ms));
	const doc = extra => Object.assign(${JSON.stringify(controllerDoc(m))}, extra || {});
	const recv = d => gm.recv([{ type: "controller", doc: d }]);
	const click = el => el && el.dispatchEvent(new MouseEvent("click", { bubbles: true, cancelable: true }));
	const pick = async (id, value) => {
		const b = document.querySelector('#ctlpop .kselbtn[data-for="' + id + '"]');
		if (!b) return "no key for " + id;
		click(b); await wait(20);
		if (document.querySelector("#kpop").hidden) return "the list did not open for " + id;
		const o = document.querySelector('#kpop .kopt[data-v="' + value + '"]');
		if (!o) return "no option " + value + " for " + id;
		click(o); await wait(120);
		return "";
	};
	const sentOp = (op, f) => sent.some(c => c.op === op && (!f || f(c)));
	const ctlKey = () => document.querySelector("#ctlkey");
	const toControl = async () => {
		const tab = [...document.querySelectorAll("[data-ws]")].find(t => t.dataset.ws === "control");
		click(tab); await wait(80);
	};
	(async () => {
		try {
			await wait(300);
			recv(doc());
			await wait(50);
			openController(); await wait(50);
			check(!document.querySelector("#ctlpop").hidden, "the Controller panel is open");
			/* the panel's dropdowns: a pick in the page's own list is the host's command */
			let why = await pick("ctl-channel", "5");
			check(!why && sentOp("ctlSet", c => c.channel === 5), "CHANNEL: the list opens and a pick sends ctlSet channel 5 " + why);
			why = await pick("ctl-profile", "tr06");
			check(!why && sentOp("ctlSet", c => c.profile === "tr06"), "PROFILE: a pick sends ctlSet profile tr06 " + why);
			why = await pick("ctl-knob-24", ${JSON.stringify(m === "mm" ? "2:0" : "-1:16")});
			check(!why && sentOp("ctlKnob", c => c.cc === 24 && c.i === ${m === "mm" ? 0 : 16}), "a knob's target: a pick sends ctlKnob " + why);
			why = await pick("ctl-voice-SD", "4");
			check(!why && sentOp("ctlVoice", c => c.voice === "SD" && c.t === 4), "a voice's track: a pick sends ctlVoice " + why);
			/* a redraw while a list is open: the pick still lands (stable ids) */
			{
				const b = document.querySelector('#ctlpop .kselbtn[data-for="ctl-channel"]');
				click(b); await wait(20);
				recv(doc({ warning: "redrawn" })); await wait(20);
				click(document.querySelector('#kpop .kopt[data-v="7"]')); await wait(120);
				check(sentOp("ctlSet", c => c.channel === 7), "a pick after the panel redrew still sends its command");
			}
		} catch (e) { out.errors.push("harness: " + e); }
		const pre = document.createElement("pre"); pre.id = "ctltest"; pre.textContent = JSON.stringify(out); document.body.appendChild(pre); window.__ctlResult = out;
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
