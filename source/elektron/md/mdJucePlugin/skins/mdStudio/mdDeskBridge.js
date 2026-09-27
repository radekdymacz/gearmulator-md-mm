"use strict";
/* MD Desk bridge: the page's only door to the plug-in.
   Page -> C++: small JSON commands, batched per animation frame, sent as a
   navigation of a throw-away iframe to gmbridge://c/<json> (JUCE 7 has no
   native-function bridge; an iframe navigation never cancels another one).
   C++ -> page: gm.recv([...messages]) through evaluateJavaScript.
   Batches use a 16 ms timer, not requestAnimationFrame: WebKit stops
   animation frames while the plug-in window is covered, and commands and
   documents must still flow.
   Nothing here knows about patterns or kits; that is mdDeskModel.js. */
const Bridge = (() => {
	let nextId = 1, nextGesture = 1, queue = [], raf = 0;
	const handlers = [];
	const pending = new Map();	// id -> {op, onResult}
	const native = location.protocol === "file:" && !/[?&]dev=1/.test(location.search);

	function navigate(url) {
		const f = document.createElement("iframe");
		f.style.display = "none";
		f.src = url;
		document.documentElement.appendChild(f);
		setTimeout(() => f.remove(), 1000);
	}
	function flush() {
		raf = 0;
		if (!queue.length) return;
		if (!native && !window.gmDev) { setTimeout(flush, 50); return; }	// dev host not up yet
		const batch = queue.map(q => q.msg);
		queue = [];
		const text = JSON.stringify(batch);
		if (native) navigate("gmbridge://c/" + encodeURIComponent(text));
		else if (window.gmDev) window.gmDev(batch);
	}
	/* send(msg, {key, onResult}): a message with the same key still waiting in
	   this frame is replaced (a drag sends its latest value once per frame). */
	function send(msg, opt = {}) {
		msg.id = nextId++;
		if (opt.onResult) pending.set(msg.id, { op: msg.op, onResult: opt.onResult });
		if (opt.key) {
			const i = queue.findIndex(q => q.key === opt.key);
			if (i >= 0) { queue[i] = { key: opt.key, msg }; return msg.id; }
		}
		queue.push({ key: opt.key, msg });
		/* A click goes out at once; drag values (keyed) are batched per 16 ms. */
		if (!opt.key) { if (raf) { clearTimeout(raf); } flush(); }
		else if (!raf) raf = setTimeout(flush, 16);
		return msg.id;
	}
	function log(text) {
		if (native) navigate("gmbridge://log/" + encodeURIComponent(String(text)));
		else console.log("[desk]", text);
	}
	window.gm = {
		recv(messages) {
			for (const m of messages) {
				if (m.type === "result" && pending.has(m.id)) {
					const p = pending.get(m.id);
					pending.delete(m.id);
					try { p.onResult(m); } catch (e) { log("result handler: " + e); }
				}
				for (const h of handlers) {
					try { h(m); } catch (e) { log("handler " + m.type + ": " + (e && e.stack || e)); }
				}
			}
		}
	};
	window.addEventListener("error", e => log("page error: " + e.message + " @" + e.lineno));
	return {
		send, log, onMessage: h => handlers.push(h),
		gesture: () => nextGesture++,
		ready: () => { queue.push({ msg: { op: "ready" } }); flush(); }
	};
})();
