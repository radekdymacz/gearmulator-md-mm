"use strict";
/* The page bridge, both editors (skins/shared/): the page's only door to the plug-in.
   Two parts:
   - BridgeTransport: how a message travels, and nothing else (DESIGN-REVIEW-2026-10-02 finding 13). Today
     JUCE 7's: page -> C++ as the navigation of a throw-away iframe to gmbridge://c/<json> (JUCE 7 has no
     native-function bridge; an iframe navigation never cancels another one), a batch too long for one URL
     in pieces (gmbridge://p/<seq>/<i>/<n>/<piece>, joined by mdPageBridge.h); C++ -> page as
     gm.recv([...messages], seq) through a javascript: URL (the plug-in splits a long outbox into several calls at
     message boundaries, each with the next batch number). JUCE 7 goes to its last URL again when the view is
     shown again, so the newest batch can come twice: a numbered batch not newer than the last one is dropped
     (release review 2026-10-04, S5). A call without a number (a dev host, the tests) is always taken. Off the plug-in (a dev host, the node tests) window.gmDev takes the batch. JUCE 8's
     native bridge replaces this part only.
   - Bridge: the API the pages use (send with keyed merging and results, log, onMessage, gesture ids, ready).
     Batches use a 16 ms timer, not requestAnimationFrame: WebKit stops animation frames while the plug-in
     window is covered, and commands and documents must still flow.
   Nothing here knows about patterns or kits; that is each page's model. */
const BridgeTransport = (() => {
	const native = location.protocol === "file:" && !/[?&]dev=1/.test(location.search);
	/* Linux (?recv=file, mdPageBridge.h): the plug-in writes each gm.recv call as <this page's file>.recv-<seq>.js
	   beside the page instead of a javascript: URL (webkit2gtk's web process crashes on those next to the bridge
	   iframes). The page loads them in order, polling for the next one, and says how far it got
	   (gmbridge://a/<seq>; a/0 when it starts) so the plug-in deletes what was read. */
	const fileRecv = native && /[?&]recv=file(&|$)/.test(location.search);
	const POLL_MS = 8, ACK_MS = 250;
	/* The longest URL one navigation carries (encoded); a longer batch goes in pieces this long. Far above what
	   a gesture sends (a whole kit document is about 20 KB encoded), so today's batches go as one. */
	const MAX_URL = 256 * 1024;
	let nextSeq = 1;

	function navigate(url) {
		const f = document.createElement("iframe");
		f.style.display = "none";
		f.src = url;
		document.documentElement.appendChild(f);
		setTimeout(() => f.remove(), 1000);
	}
	/* An encoded text in pieces of at most _max characters, never cut inside a %XX escape (the plug-in joins
	   the pieces before it decodes them, so a character's UTF-8 bytes may span two pieces). */
	function pieces(encoded, max) {
		const out = [];
		for (let i = 0; i < encoded.length;) {
			let end = Math.min(encoded.length, i + max);
			if (end < encoded.length) {
				const p = encoded.lastIndexOf("%", end - 1);
				if (p > i && p >= end - 2) end = p;
			}
			out.push(encoded.slice(i, end));
			i = end;
		}
		return out;
	}
	/* The file the plug-in writes for batch seq, relative to the page (pure, for the node test). */
	function recvFile(pagePath, seq) {
		return pagePath.split("/").pop() + ".recv-" + seq + ".js";
	}
	/* Linux: load batch 1, 2, ... as scripts (each runs gm.recv); a missing one is asked for again shortly. */
	function pollFiles() {
		let next = 1, tries = 0, ackTimer = 0;
		const ack = () => {
			ackTimer = 0;
			navigate("gmbridge://a/" + (next - 1));
		};
		const load = () => {
			const s = document.createElement("script");
			s.src = recvFile(location.pathname, next) + "?t=" + (++tries);
			s.onload = () => {
				s.remove();
				++next;
				if (!ackTimer) ackTimer = setTimeout(ack, ACK_MS);
				load();
			};
			s.onerror = () => { s.remove(); setTimeout(load, POLL_MS); };
			(document.head || document.documentElement).appendChild(s);
		};
		navigate("gmbridge://a/0");
		load();
	}
	/* The URLs one batch travels as (the native transport's; pure, for the node test). */
	function urls(batch, max = MAX_URL) {
		const encoded = encodeURIComponent(JSON.stringify(batch));
		if (encoded.length <= max) return ["gmbridge://c/" + encoded];
		const ps = pieces(encoded, max), seq = nextSeq++;
		return ps.map((p, i) => "gmbridge://p/" + seq + "/" + i + "/" + ps.length + "/" + p);
	}
	return {
		/* the plug-in or a dev host takes messages now */
		up: () => native || !!window.gmDev,
		post(batch) {
			if (native) urls(batch).forEach(navigate);
			else if (window.gmDev) window.gmDev(batch);
		},
		log(text) {
			if (native) navigate("gmbridge://log/" + encodeURIComponent(String(text)));
			else console.log("[desk]", text);
		},
		/* what the plug-in says: gm.recv([...], seq); a batch already had (seq not above the last) is dropped */
		onReceive(fn) {
			let lastSeq = 0;
			const gm = {
				dropped: 0,	/* batches dropped as already had (for the tests and a look in the inspector) */
				recv(messages, seq) {
					if (typeof seq === "number") {
						if (seq <= lastSeq) { gm.dropped++; return; }
						lastSeq = seq;
					}
					fn(messages);
				}
			};
			window.gm = gm;
			if (fileRecv) pollFiles();
		},
		urls, pieces, recvFile
	};
})();

const Bridge = (() => {
	let nextId = 1, nextGesture = 1, queue = [], raf = 0;
	const handlers = [];
	const pending = new Map();	// id -> {op, onResult}
	const T = BridgeTransport;
	function flush() {
		raf = 0;
		if (!queue.length) return;
		if (!T.up()) { setTimeout(flush, 50); return; }	// dev host not up yet
		const batch = queue.map(q => q.msg);
		queue = [];
		T.post(batch);
	}
	/* send(msg, {key, onResult, merge}): a message with the same key still waiting in
	   this frame is replaced (a drag sends its latest value once per frame); the
	   replacement keeps its id, so one result answers both. merge(waiting, msg)
	   makes the replacement from both (a relative change sums its steps). */
	function send(msg, opt = {}) {
		const i = opt.key ? queue.findIndex(q => q.key === opt.key) : -1;
		if (i >= 0 && opt.merge) msg = opt.merge(queue[i].msg, msg);
		msg.id = i >= 0 ? queue[i].msg.id : nextId++;
		if (opt.onResult) pending.set(msg.id, { op: msg.op, onResult: opt.onResult });
		if (i >= 0) { queue[i] = { key: opt.key, msg }; return msg.id; }
		queue.push({ key: opt.key, msg });
		/* A click goes out at once; drag values (keyed) are batched per 16 ms. */
		if (!opt.key) { if (raf) { clearTimeout(raf); } flush(); }
		else if (!raf) raf = setTimeout(flush, 16);
		return msg.id;
	}
	const log = text => T.log(text);
	T.onReceive(messages => {
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
	});
	window.addEventListener("error", e => log("page error: " + e.message + " @" + e.lineno + " " + (e.error && e.error.stack || "")));
	return {
		send, log, onMessage: h => handlers.push(h),
		gesture: () => nextGesture++,
		ready: () => { queue.push({ msg: { op: "ready" } }); flush(); }
	};
})();
if (typeof module !== "undefined") module.exports = { BridgeTransport, Bridge };
