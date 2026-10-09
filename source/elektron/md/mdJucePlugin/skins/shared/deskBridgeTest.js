"use strict";
/* The page bridge's transport (BridgeTransport, deskBridge.js; DESIGN-REVIEW-2026-10-02 finding 13): a batch is one
   gmbridge://c/ URL while it fits, else pieces never cut inside an escape that join (in index order) to the one
   encoded text; Bridge hands batches to the transport and what the plug-in says to its handlers. Over the real
   bridge (codex review 2026-10): a notice's answer keeps the notice's number apart from the request's id (deskModal.js
   noticeAnswer).
     node deskBridgeTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");

const sent = [];
const win = { gmDev: batch => sent.push(batch), addEventListener() {} };
const ctx = vm.createContext({ console, location: { protocol: "http:", search: "" }, setTimeout, clearTimeout, window: win, document: {} });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskBridge.js"), "utf8") + "\nthis.T = { BridgeTransport, Bridge };", ctx);
const { BridgeTransport, Bridge } = ctx.T;
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

/* ---- one URL while it fits ---- */
{
	const batch = [{ op: "param", t: 1, i: 2, v: 3, id: 4 }];
	const urls = BridgeTransport.urls(batch);
	check(urls.length === 1 && urls[0] === "gmbridge://c/" + encodeURIComponent(JSON.stringify(batch)), "a batch that fits: one gmbridge://c/ URL, as before");
}
/* ---- pieces: never inside an escape, joined they are the encoded text ---- */
{
	const batch = [{ op: "kitName", n: "€uro – ünïcødé ".repeat(40), id: 9 }, { op: "set", kind: "kit", doc: { name: "x".repeat(500) } }];
	const encoded = encodeURIComponent(JSON.stringify(batch));
	for (const max of [3, 7, 64, 333]) {
		const urls = BridgeTransport.urls(batch, max);
		const parts = urls.map(u => /^gmbridge:\/\/p\/(\d+)\/(\d+)\/(\d+)\/(.*)$/.exec(u));
		const seqs = new Set(parts.map(p => p && p[1]));
		const ok = parts.every((p, i) => p && +p[2] === i && +p[3] === urls.length && p[4].length <= max && !/%.?$/.test(p[4]));
		check(ok && seqs.size === 1 && parts.map(p => p[4]).join("") === encoded
			&& JSON.stringify(JSON.parse(decodeURIComponent(parts.map(p => p[4]).join("")))) === JSON.stringify(batch),
			"pieces of at most " + max + ": one sequence, in order, never cut in an escape, joined they decode to the batch");
	}
	const a = BridgeTransport.urls(batch, 64), b = BridgeTransport.urls(batch, 64);
	check(a[0].split("/")[3] !== b[0].split("/")[3], "each long batch has its own sequence number");
}
/* ---- Linux: the plug-in's batches as script files beside the page (mdPageBridge.h recvFileName) ---- */
check(BridgeTransport.recvFile("/tmp/gearmulator-mdStudio-1a2b.html", 12) === "gearmulator-mdStudio-1a2b.html.recv-12.js",
	"a batch's file: the page file's name, .recv-<seq>.js, relative to the page");
/* ---- Bridge over the transport (a dev host: window.gmDev) ---- */
{
	let result = null, seen = [];
	Bridge.onMessage(m => seen.push(m.type));
	const id = Bridge.send({ op: "play" }, { onResult: r => { result = r; } });
	check(sent.length === 1 && sent[0][0].op === "play" && sent[0][0].id === id, "a click goes to the transport at once, with its id");
	win.gm.recv([{ type: "result", id, ok: true }, { type: "machine", doc: {} }]);
	check(result && result.ok && seen.join() === "result,machine", "what the plug-in says reaches the result handler and every handler, in order");
}
/* ---- numbered batches: one the page has had is dropped (JUCE 7 replays its last javascript: URL) ---- */
{
	const seen = [];
	Bridge.onMessage(m => { if (m.type === "notice") seen.push(m.id); });
	win.gm.recv([{ type: "notice", id: 1 }], 1);
	win.gm.recv([{ type: "notice", id: 2 }], 2);
	win.gm.recv([{ type: "notice", id: 2 }], 2);
	check(seen.join() === "1,2" && win.gm.dropped === 1, "a replayed batch (the same number again) is dropped");
	win.gm.recv([{ type: "notice", id: 1 }], 1);
	check(seen.join() === "1,2" && win.gm.dropped === 2, "an older batch is dropped");
	win.gm.recv([{ type: "notice", id: 3 }], 3);
	win.gm.recv([{ type: "notice", id: 4 }]);
	win.gm.recv([{ type: "notice", id: 4 }]);
	check(seen.join() === "1,2,3,4,4", "the next number is taken; a call without a number (a dev host) always is");
}
/* ---- Windows: the page in WebView2 posts the same gmbridge:// texts, in order, and makes no iframes ---- */
{
	const posted = [];
	const w = { chrome: { webview: { postMessage: t => posted.push(t) } }, addEventListener() {} };
	const doc = { createElement() { throw new Error("no iframe on WebView2"); } };
	const c = vm.createContext({ console, location: { protocol: "file:", search: "" }, setTimeout, clearTimeout, window: w, document: doc });
	vm.runInContext(fs.readFileSync(path.join(__dirname, "deskBridge.js"), "utf8") + "\nthis.T = { BridgeTransport, Bridge };", c);
	const T = c.T.BridgeTransport;
	T.post([{ op: "a" }]);
	T.log("hello");
	T.post([{ op: "b" }]);
	check(posted.length === 3 && posted[0] === "gmbridge://c/" + encodeURIComponent(JSON.stringify([{ op: "a" }]))
		&& posted[1] === "gmbridge://log/hello" && posted[2].endsWith(encodeURIComponent(JSON.stringify([{ op: "b" }]))),
		"WebView2: each message is one postMessage of its gmbridge:// text, in order");
}

/* ---- the bridge on a stand-in page: deskBridge.js and the shared files given, a document that answers everything ---- */
function standIn(files, extra = {}) {
	const posted = [], logged = [];
	const el = () => ({ style: {}, classList: { add() { }, remove() { }, toggle() { } }, setAttribute() { }, hasAttribute: () => true, appendChild() { },
		replaceChildren() { }, addEventListener() { }, querySelector: () => null, querySelectorAll: () => [], remove() { } });
	const document = Object.assign({ addEventListener() { }, createElement: el, body: el(), documentElement: el(), head: el(), readyState: "complete",
		querySelector: () => null, activeElement: null }, extra.document || {});
	const window = Object.assign({ gmDev: batch => posted.push(...JSON.parse(JSON.stringify(batch))), addEventListener() { } }, extra.window || {});
	const c = vm.createContext(Object.assign({ console: { log: (...a) => logged.push(a.join(" ")), error: console.error }, location: { protocol: "http:", search: "" },
		setTimeout, clearTimeout, window, document, navigator: { platform: "MacIntel" }, MutationObserver: class { observe() { } }, Date, innerWidth: 1440 }, extra.globals || {}));
	vm.runInContext(files.map(f => fs.readFileSync(path.join(__dirname, f), "utf8")).join("\n;\n")
		+ "\nthis.P = { Bridge, noticeAnswer: typeof noticeAnswer !== 'undefined' ? noticeAnswer : null, noticeRefused: typeof noticeRefused !== 'undefined' ? noticeRefused : null };", c);
	c.gm = window.gm;	/* a browser's window is its global: the plug-in's scripts say gm */
	return Object.assign(c.P, { posted, logged, window, document, ctx: c, recv: (m, seq) => window.gm.recv(m, seq) });
}
/* ---- a notice's answer: the notice's number as "notice", the request's own id apart (deskModal.js noticeAnswer) ---- */
{
	const p = standIn(["deskModal.js", "deskBridge.js"]);
	p.Bridge.send({ op: "play" });	/* an earlier command: the bridge's numbers are on */
	const notice = { type: "notice", id: 77, title: "Update available", text: "", buttons: ["Update", "Later"], modal: false };
	const id = p.Bridge.send(p.noticeAnswer(notice, 1), { onResult: p.noticeRefused });
	const a = p.posted[p.posted.length - 1];
	check(a.op === "noticeAnswer" && a.notice === 77 && a.button === 1 && a.id === id && a.id !== 77 && Object.keys(a).sort().join() === "button,id,notice,op",
		"noticeAnswer: the notice's number as notice, the request's own id as id, nothing else: " + JSON.stringify(a));
	p.recv([{ type: "result", op: "noticeAnswer", id, ok: false, errors: ["notice 77 is not waiting for an answer"], note: "" }]);
	check(p.logged.some(l => /noticeAnswer refused: notice 77 is not waiting/.test(l)), "a refused answer is logged (noticeRefused), nothing else");
	const before = p.logged.length;
	p.recv([{ type: "result", op: "noticeAnswer", id: p.Bridge.send(p.noticeAnswer({ id: 3 }, 0), { onResult: p.noticeRefused }), ok: true, errors: [], note: "" }]);
	check(p.logged.length === before, "a taken answer logs nothing");
	/* a caller's own id on a new message is replaced by the request's: said in the log */
	const own = p.Bridge.send({ op: "noticeAnswer", id: 77, button: 0 });
	check(p.posted[p.posted.length - 1].id === own && own !== 77 && p.logged.some(l => /send noticeAnswer: its own id 77 is replaced/.test(l)),
		"a message with its own id: the bridge's id goes out, and the log says the caller's was lost");
}

if (failures) { console.error("deskBridgeTest: " + failures + " failure(s)"); process.exit(1); }
console.log("deskBridgeTest: PASS");
