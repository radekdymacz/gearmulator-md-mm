"use strict";
/* User journeys (diagnostics builds only, like the self-tests): a journey is what a person does with the
   editor, as data. Each step is a value {say, act, screen, machine, within}:
     say      what the person does, in words (the log line)
     act      async (u, c): the doing, only through the page's own controls (u: clicks, keys, drags and the wheel
              at an element's centre, refused when something else covers it); c is the journey's own scratch value
     screen   (c) => true, or what was seen instead: what the page shows afterwards
     machine  (c) => true, or what was seen instead: what the plug-in's documents (the firmware's read-back) say
     within   how long the two may take to agree (ms, default 6000)
   A journey is {name, steps, tidy, boot, needs}: needs (optional) () => why it cannot run here, or null (it is then
   logged "JOURNEY <name> SKIP <why>", neither passed nor failed); tidy (async, optional) puts back what the steps changed and runs pass or
   fail; boot: true runs it from the page's start, before the machine takes input (the start-up card).
   One runner (Journey.run) plays them in order, logs a line per step, then "JOURNEY <name> PASS|FAIL <reason>"
   and at the end "JOURNEYS DONE <passed>/<run>". The editors' pages pass their journeys and a ready test
   (mdDeskJourneys.js, mmJourneys.js). Chosen by ?selftest=journey (all) or journey-<names>: comma-separated,
   * matches any run of characters (journey-seq-*, journey-md-seq-first-beat,md-mix-*); "@k/n" after either takes every
   n-th of those, from the k-th (journey@2/4: scripts/mdmm-journeys.sh --jobs 4 gives each of its editors a share).
   Demos (doc/modern-ux/DEMO-VIDEOS.md) are journeys too, played for a camera: ?selftest=demo-<names> (demo-md-*) runs
   them through Journey.demo at a person's pace, with a drawn pointer that glides to each control and rings on a click,
   and a key cap for each key. A step may also have caption (a line for the video, logged with its time) and hold (ms
   to wait after the step passed, so the machine is heard). A step may have section (its part of the song, for the video's timeline). A demo may have setup (steps played before the camera
   starts, so the video opens on the machine already playing) and card ("name|line|url", the video's end card). The video script (scripts/mdmm-demo-video.sh) starts
   recording on "DEMO READY" and cuts the video by the log's "DEMO <name> START" and "DEMO <name> at <ms> …" lines. */
const Journey = (() => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const until = async (f, ms) => { const end = performance.now() + ms; while (performance.now() < end) { if (f()) return true; await sleep(40); } return !!f(); };
	/* a check's answer: true passes; anything else is what was seen */
	const ok = (pass, seen) => pass ? true : (seen == null ? "no" : String(seen));
	const describe = el => !el ? "nothing" : (el.id ? "#" + el.id : el.tagName.toLowerCase() + (el.className && typeof el.className === "string" ? "." + el.className.trim().split(/\s+/).join(".") : ""));

	/* ---------- the drawn pointer (demos only) ---------- */
	/* A demo is filmed: synthetic events have no visible pointer, so one is drawn. It never takes a hit test
	   (pointer-events: none), so the hands' covered-element check is the same with it on screen. Off unless a demo runs. */
	const Pointer = (() => {
		let dot = null, at = null, keycap = null, keyTimer = 0;
		const style = (e, css) => { e.style.cssText = css; return e; };
		function ensure() {
			if (dot) return dot;
			dot = style(document.createElement("div"), "position:fixed;left:0;top:0;width:28px;height:28px;margin:-14px 0 0 -14px;border-radius:50%;"
				+ "background:rgba(255,255,255,.28);border:2px solid rgba(255,255,255,.95);box-shadow:0 0 0 2px rgba(0,0,0,.45),0 3px 12px rgba(0,0,0,.45);"
				+ "pointer-events:none;z-index:2147483647;transition:transform 450ms cubic-bezier(.3,.7,.2,1),background 120ms;will-change:transform");
			dot.id = "journey-pointer";
			document.body.appendChild(dot);
			return dot;
		}
		function place(x, y, ms) { const d = ensure(); d.style.transitionDuration = `${ms}ms, 120ms`; d.style.transform = `translate(${x}px, ${y}px)` + (d.dataset.down ? " scale(.78)" : ""); at = { x, y }; }
		return {
			on: false,
			/* glide to a point (ms: at most this long), resolved when there */
			async glide(x, y, ms = 520) {
				if (!at) { at = { x: innerWidth / 2, y: innerHeight / 2 }; place(at.x, at.y, 0); await sleep(30); }
				const dist = Math.hypot(x - at.x, y - at.y), t = Math.round(Math.min(ms, 180 + dist * 0.9));
				place(x, y, t); await sleep(t + 40);
			},
			move(x, y) { if (this.on) place(x, y, 40); },
			down(x, y, isDown) {
				if (!this.on) return;
				const d = ensure(); if (isDown) d.dataset.down = "1"; else delete d.dataset.down;
				d.style.background = isDown ? "rgba(255,190,60,.55)" : "rgba(255,255,255,.28)"; place(x, y, 60);
			},
			/* a ring where a click lands */
			ring(x, y) {
				if (!this.on) return;
				this.move(x, y);
				const r = style(document.createElement("div"), `position:fixed;left:${x}px;top:${y}px;width:44px;height:44px;margin:-22px 0 0 -22px;border-radius:50%;`
					+ "border:3px solid rgba(255,190,60,.95);pointer-events:none;z-index:2147483646;transform:scale(.4);opacity:1;transition:transform 520ms ease-out,opacity 520ms ease-out");
				document.body.appendChild(r);
				requestAnimationFrame(() => requestAnimationFrame(() => { r.style.transform = "scale(1.6)"; r.style.opacity = "0"; }));
				setTimeout(() => r.remove(), 800);
			},
			/* the key pressed, as a key cap at the window's bottom right (the video's captions go bottom centre) */
			key(label) {
				if (!this.on) return;
				if (!keycap) {
					keycap = style(document.createElement("div"), "position:fixed;right:28px;bottom:28px;padding:10px 22px;border-radius:12px;"
						+ "background:rgba(10,10,12,.85);color:#fff;font:700 26px/1.1 -apple-system,system-ui,sans-serif;letter-spacing:.04em;border:1px solid rgba(255,255,255,.25);"
						+ "box-shadow:0 6px 24px rgba(0,0,0,.45);pointer-events:none;z-index:2147483647;transition:opacity 200ms;opacity:0");
					document.body.appendChild(keycap);
				}
				keycap.textContent = label; keycap.style.opacity = "1";
				clearTimeout(keyTimer); keyTimer = setTimeout(() => { keycap.style.opacity = "0"; }, 1100);
			}
		};
	})();
	const keyLabel = (key, m = {}) => (m.cmd ? "⌘ " : "") + (m.alt ? "⌥ " : "") + (m.shift && key.length > 1 ? "⇧ " : "")
		+ ({ " ": "Space", Escape: "Esc", ArrowUp: "↑", ArrowDown: "↓", ArrowLeft: "←", ArrowRight: "→", Delete: "Del", Backspace: "⌫", Enter: "↵" }[key] || (key.length === 1 ? key.toUpperCase() : key));

	/* ---------- the person's hands ---------- */
	/* the element itself (a selector or a node), on screen and on top at the point used */
	function el(q) {
		const e = typeof q === "string" ? document.querySelector(q) : q;
		if (!e) throw new Error("not on the page: " + q);
		return e;
	}
	function pointIn(e, fx = 0.5, fy = 0.5) {
		e.scrollIntoView?.({ block: "nearest", inline: "nearest" });
		const r = e.getBoundingClientRect();
		if (r.width < 1 || r.height < 1) throw new Error("not shown: " + describe(e));
		const x = r.left + r.width * fx, y = r.top + r.height * fy, top = document.elementFromPoint(x, y);
		if (!top || (top !== e && !e.contains(top))) throw new Error(`${describe(e)} is covered by ${describe(top)}`);
		return { x, y };
	}
	/* the macOS plug-in: WKWebView over the page file (not a browser, not WebView2), with mdOsKeys.h behind the log */
	const osKeys = () => typeof BridgeTransport !== "undefined" && BridgeTransport.up() && location.protocol === "file:"
		&& !(window.chrome && window.chrome.webview) && /Mac/.test(navigator.platform);
	const mods = (m = {}) => ({ shiftKey: !!m.shift, altKey: !!m.alt, metaKey: !!m.cmd, ctrlKey: !!m.ctrl });
	const pe = (type, target, x, y, m, buttons) => target.dispatchEvent(new PointerEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true,
		pointerId: 1, pointerType: "mouse", isPrimary: true, button: 0, buttons, clientX: x, clientY: y }, mods(m))));
	const me = (type, target, x, y, m, buttons, detail = 1) => target.dispatchEvent(new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true,
		button: 0, buttons, clientX: x, clientY: y, detail, view: window }, mods(m))));
	const u = {
		el, sleep, until,
		/* demos: the drawn pointer glides to the element's point first (nothing is pressed); nothing otherwise */
		async glide(q, fx, fy, ms) { if (!Pointer.on) return; const { x, y } = pointIn(el(q), fx, fy); await Pointer.glide(x, y, ms); },
		/* demos: a key cap shown without a key sent (a held modifier); nothing otherwise */
		cap(label) { Pointer.key(label); },
		/* a mouse click at the element's centre (or at fx, fy of its box): down, up, click, as the browser sends them */
		click(q, m = {}, fx, fy) {
			const e = el(q), { x, y } = pointIn(e, fx, fy), t = document.elementFromPoint(x, y);
			Pointer.ring(x, y);
			pe("pointerdown", t, x, y, m, 1); me("mousedown", t, x, y, m, 1);
			pe("pointerup", t, x, y, m, 0); me("mouseup", t, x, y, m, 0); me("click", t, x, y, m, 0);
		},
		/* a right-click at the element's centre: the second button down and up, then the contextmenu event the browser
		   sends (a step's menu, DESIGN-keymap.md K3) */
		rightClick(q, m = {}, fx, fy) {
			const e = el(q), { x, y } = pointIn(e, fx, fy), t = document.elementFromPoint(x, y);
			Pointer.ring(x, y);
			const o = (type, buttons) => Object.assign({ bubbles: true, cancelable: true, composed: true, button: 2, buttons, clientX: x, clientY: y, view: window }, mods(m));
			t.dispatchEvent(new PointerEvent("pointerdown", Object.assign(o("pointerdown", 2), { pointerId: 1, pointerType: "mouse", isPrimary: true })));
			t.dispatchEvent(new MouseEvent("mousedown", o("mousedown", 2)));
			t.dispatchEvent(new MouseEvent("contextmenu", o("contextmenu", 2)));
			t.dispatchEvent(new PointerEvent("pointerup", Object.assign(o("pointerup", 0), { pointerId: 1, pointerType: "mouse", isPrimary: true })));
			t.dispatchEvent(new MouseEvent("mouseup", o("mouseup", 0)));
		},
		dblclick(q, m = {}) { u.click(q, m); const e = el(q), { x, y } = pointIn(e); me("dblclick", document.elementFromPoint(x, y), x, y, m, 0, 2); },
		/* a press and drag: from the element's point through each [dx, dy] offset (or element), released at the last */
		async drag(q, path, m = {}, o = {}) {
			const e = el(q), a = pointIn(e, o.fx, o.fy), down = document.elementFromPoint(a.x, a.y);
			/* pointer capture as the browser keeps it: the element the page captured gets the moves and the release,
			   even when a redraw replaced the element pressed (a synthetic pointer has no real capture) */
			let captured = null; const cap0 = Element.prototype.setPointerCapture;
			Element.prototype.setPointerCapture = function () { captured = this; };
			let at = a;
			const target = () => o.captured === false ? (document.elementFromPoint(at.x, at.y) || document.body)
				: captured && captured.isConnected ? captured : down.isConnected ? down : document.body;
			try {
				Pointer.down(a.x, a.y, true);
				pe("pointerdown", down, a.x, a.y, m, 1); me("mousedown", down, a.x, a.y, m, 1);
				for (const p of path) {
					at = Array.isArray(p) ? { x: a.x + p[0], y: a.y + p[1] } : pointIn(el(p));
					const over = document.elementFromPoint(at.x, at.y) || document.body;
					Pointer.move(at.x, at.y);
					pe("pointermove", target(), at.x, at.y, m, 1); me("mousemove", over, at.x, at.y, m, 1);
					await sleep(o.stepMs ?? 30);
				}
				const up = target();
				pe("pointerup", up, at.x, at.y, m, 0); me("mouseup", up, at.x, at.y, m, 0);
			} finally { Element.prototype.setPointerCapture = cap0; Pointer.down(at.x, at.y, false); }
			if (o.click !== false) me("click", document.elementFromPoint(at.x, at.y) || document.body, at.x, at.y, m, 0);
			await sleep(o.settle ?? 150);
		},
		/* an HTML drag and drop (draggable=true): dragstart on the source, dragover and drop on the target, dragend,
		   with one DataTransfer as the browser hands it */
		async dragDrop(from, to) {
			const a = el(from), b = el(to), pa = pointIn(a), pb = pointIn(b);
			let dt = null; try { dt = new DataTransfer(); } catch (_) { const d = {}; dt = { effectAllowed: "all", dropEffect: "none", setData: (k, v) => { d[k] = v; }, getData: k => d[k] ?? "", types: [], files: [] }; }
			const de = (type, target, p) => { const e = new DragEvent(type, { bubbles: true, cancelable: true, composed: true, clientX: p.x, clientY: p.y, dataTransfer: dt });
				if (!e.dataTransfer) Object.defineProperty(e, "dataTransfer", { value: dt }); return target.dispatchEvent(e); };
			const src = document.elementFromPoint(pa.x, pa.y), dst = document.elementFromPoint(pb.x, pb.y);
			de("dragstart", src, pa); await sleep(30);
			de("dragenter", dst, pb); de("dragover", dst, pb); await sleep(30);
			de("drop", dst, pb); de("dragend", src, pb); await sleep(150);
		},
		/* scroll wheel notches over the element (up: positive n) */
		wheel(q, n, m = {}) {
			const e = el(q), { x, y } = pointIn(e), t = document.elementFromPoint(x, y);
			for (let i = 0; i < Math.abs(n); i++) t.dispatchEvent(new WheelEvent("wheel", Object.assign({ bubbles: true, cancelable: true, deltaY: n > 0 ? -100 : 100, clientX: x, clientY: y }, mods(m))));
		},
		/* a key pressed and let go where the focus is (key: "a", "Delete", " "; code when it matters), with the
		   browser's default action on a focused button */
		key(key, m = {}, code) {
			const t = document.activeElement && document.activeElement !== document.documentElement ? document.activeElement : document.body;
			const c = code || (key.length === 1 ? (/[a-z]/i.test(key) ? "Key" + key.toUpperCase() : /[0-9]/.test(key) ? "Digit" + key : key === " " ? "Space" : "") : key);
			const o = Object.assign({ key, code: c, bubbles: true, cancelable: true, composed: true }, mods(m));
			Pointer.key(keyLabel(key, m));
			const go = t.dispatchEvent(new KeyboardEvent("keydown", o));
			/* the browser's own default action on a focused button: Enter clicks it on keydown, Space on keyup, unless
			   the page prevented the keydown (a synthetic event has no default action, so it is done here) */
			const button = t.closest?.("button,[role=button]") && !t.disabled;
			if (go && button && key === "Enter") t.click();
			t.dispatchEvent(new KeyboardEvent("keyup", o));
			if (go && button && key === " ") t.click();
		},
		/* keys as the operating system delivers them (spec: "focus cmd+c", tokens as mdOsKeys.h reads them): in the
		   macOS plug-in real key events routed as AppKit routes them (the window's views and JUCE, the menu bar, the
		   first responder), so a key the host side keeps from the page is kept here too; "focus" is what a click on the
		   page does to the web view, "activate" what JUCE does when the window becomes the key window (B-018). Elsewhere (a
		   browser, Windows) the page's own keys (u.key), "focus" and "activate" nothing. */
		async osKey(spec) {
			const toks = spec.split(/\s+/).filter(Boolean);
			Pointer.key(toks.filter(t => t !== "focus" && t !== "activate").join(" "));
			if (osKeys()) { Bridge.log("oskeys " + spec); await sleep(350); return; }
			for (const t of toks) {
				if (t === "focus" || t === "activate") continue;
				const p = t.split("+"), k = p.pop(), m = Object.fromEntries(p.map(x => [x, true]));
				u.key({ escape: "Escape", delete: "Delete", space: " ", return: "Enter", tab: "Tab" }[k] || k, m);
			}
		},
		/* a value typed into a field and committed (input events per character, then change) */
		type(q, text) {
			const e = el(q); e.focus(); e.value = "";
			for (const ch of text) { e.value += ch; e.dispatchEvent(new InputEvent("input", { bubbles: true, data: ch, inputType: "insertText" })); }
			e.dispatchEvent(new Event("change", { bubbles: true }));
		},
		/* a key-style dropdown (the page's own popup list for a hidden <select>): its key, then the option */
		async pick(id, value) { u.click(`.kselbtn[data-for="${id}"]`); await sleep(150); u.click(`#kpop .kopt[data-v="${value}"]`); await sleep(150); },
		/* a <select> set as the person picks from it */
		choose(q, value) { const e = el(q); e.focus(); e.value = String(value); e.dispatchEvent(new Event("input", { bubbles: true })); e.dispatchEvent(new Event("change", { bubbles: true })); }
	};

	/* ---------- the runner ---------- */
	/* the journeys (what: "journey") or demos ("demo") this page was asked for: null when none */
	function wanted(all, what = "journey") {
		const m = location.search.match(new RegExp(`[?&]selftest=(${what}[^&]*)`)); if (!m) return null;
		let kind = decodeURIComponent(m[1]);
		/* "@k/n" at the end (scripts/mdmm-journeys.sh --jobs n runs n editors at once): every n-th of the chosen ones,
		   from the k-th, so the n editors share them without a list of names */
		const shard = kind.match(/@(\d+)\/(\d+)$/);
		if (shard) kind = kind.slice(0, shard.index);
		const take = list => shard ? list.filter((_, i) => i % +shard[2] === +shard[1] - 1) : list;
		if (kind === what) return take(all);
		const pats = kind.slice(what.length).replace(/^-/, "").split(",").filter(Boolean).map(p => new RegExp("^" + p.replace(/[.+?^${}()|[\]\\]/g, "\\$&").replace(/\*/g, ".*") + "$"));
		/* a name matches whole, or without its "demo-" and its "md-"/"mm-" (demo-md-groove: md-groove, groove) */
		const forms = n => { const bare = n.replace(new RegExp(`^${what}-`), ""); return [n, bare, bare.replace(/^(md|mm)-/, "")]; };
		return take(all.filter(j => pats.some(r => forms(j.name).some(f => r.test(f)))));
	}
	/* film: {t0} when a demo is filmed (its steps' times are logged against t0) */
	async function runOne(j, log, context, film, c0) {
		const c = c0 || {}, n = j.steps.length;
		let reason = "";
		/* what the journey needs that this run may not have (the window on screen): it is skipped, not failed */
		const missing = j.needs ? j.needs() : null;
		if (missing) { log(`JOURNEY ${j.name} SKIP ${missing}`); return "skip"; }
		for (let i = 0; i < n && !reason; i++) {
			const st = j.steps[i], t0 = performance.now(), head = `JOURNEY ${j.name} ${i + 1}/${n}`;
			if (film) log(`DEMO ${j.name} at ${Math.round(t0 - film.t0)} step ${i + 1}/${n}${st.section ? " section " + st.section : ""}${st.caption ? " caption " + st.caption : ""}`);
			try { if (st.act) await st.act(u, c); }
			catch (e) { reason = `step ${i + 1} "${st.say}": could not do it: ${e.message}`; log(`${head} FAIL ${st.say}: could not do it: ${e.message}${context ? " (page: " + context() + ")" : ""}`); break; }
			let scr = true, mac = true;
			const both = () => {
				try { scr = st.screen ? st.screen(c) : true; } catch (e) { scr = "check threw: " + e.message; }
				try { mac = st.machine ? st.machine(c) : true; } catch (e) { mac = "check threw: " + e.message; }
				return scr === true && mac === true;
			};
			const pass = await until(both, st.within ?? 6000);
			const ms = Math.round(performance.now() - t0);
			if (pass) { log(`${head} ok ${st.say} (${ms} ms)${c.note ? " · " + c.note : ""}`); c.note = ""; if (st.hold) await sleep(st.hold); continue; }
			let around = ""; try { around = context ? context() : ""; } catch (_) { }
			const seen = [scr !== true ? "screen: " + scr : "", mac !== true ? "machine: " + mac : "", around ? "page: " + around : ""].filter(Boolean).join("; ");
			reason = `step ${i + 1} "${st.say}": ${seen}`;
			log(`${head} FAIL ${st.say} after ${ms} ms: ${seen}`);
		}
		if (j.tidy) { try { await j.tidy(u, c); } catch (e) { log(`JOURNEY ${j.name} tidy: ${e.message}`); } }
		log(`JOURNEY ${j.name} ${reason ? "FAIL " + reason : "PASS"}`);
		return !reason;
	}
	/* page: {log, ready: async () => true when the machine takes input, between: async (u) => the page's neutral
	   state before each journey, context: () => what the page last said (its toast, its error line), added to a FAIL} */
	async function run(all, page) {
		const list = wanted(all); if (!list) return;
		const log = page.log;
		log(`JOURNEYS start: ${list.length} of ${all.length}: ${list.map(j => j.name).join(" ")}`);
		if (!list.length) { log("JOURNEYS DONE 0/0"); return; }
		let passed = 0, skipped = 0;
		const count = r => { if (r === "skip") skipped++; else if (r) passed++; };
		/* a journey about the start itself ({boot: true}) runs from the page's start, before the machine is ready */
		for (const j of list.filter(j => j.boot)) count(await runOne(j, log, page.context));
		if (!await page.ready()) { log(`JOURNEYS DONE ${passed}/${list.length} (the machine was not ready)`); return; }
		for (const j of list.filter(j => !j.boot)) {
			try { if (page.between) await page.between(u); } catch (e) { log(`JOURNEY ${j.name} before: ${e.message}`); }
			count(await runOne(j, log, page.context));
		}
		log(`JOURNEYS DONE ${passed}/${list.length - skipped}${skipped ? `, ${skipped} skipped` : ""}`);
	}
	/* Demos: page as for run, plus preroll (ms between "DEMO READY" and the first demo: the recorder starts then). They
	   run once the machine is ready and the page is neutral, with the drawn pointer, each between "DEMO <name> START"
	   and "DEMO <name> END <ms>", then "DEMOS DONE <passed>/<run>". */
	async function demo(all, page) {
		const list = wanted(all, "demo"); if (!list) return;
		const log = page.log;
		log(`DEMOS start: ${list.length} of ${all.length}: ${list.map(j => j.name).join(" ")}`);
		if (!list.length) { log("DEMOS DONE 0/0"); return; }
		if (!await page.ready()) { log(`DEMOS DONE 0/${list.length} (the machine was not ready)`); return; }
		try { if (page.between) await page.between(u); } catch (e) { log(`DEMO before: ${e.message}`); }
		/* a demo's setup (steps, optional) gets the machine going before the camera does, so the video opens on it
		   already playing: the first demo's runs before "DEMO READY", a later one's just before that demo */
		const scratch = list.map(() => ({}));
		const setup = async i => !list[i].setup || await runOne({ name: list[i].name + "-setup", steps: list[i].setup }, log, page.context, null, scratch[i]);
		const ready0 = await setup(0);
		Pointer.on = true;
		await Pointer.glide(innerWidth * 0.5, innerHeight * 0.55, 0);
		log("DEMO READY");
		await sleep(page.preroll ?? 4000);
		let passed = 0;
		for (let i = 0; i < list.length; i++) {
			const j = list[i];
			if (!(i === 0 ? ready0 : await setup(i))) { log(`JOURNEY ${j.name} FAIL its setup failed`); continue; }
			const t0 = performance.now();
			/* START with the page clock (ms), so the page's own timed lines (a demo's bar lines) can be placed on the video */
			log(`DEMO ${j.name} START clock ${Math.round(t0)}`);
			/* the end card's text, for the video script: name|line|url */
			if (j.card) log(`DEMO ${j.name} card ${j.card}`);
			if (await runOne(j, log, page.context, { t0 }, scratch[i])) passed++;
			log(`DEMO ${j.name} END ${Math.round(performance.now() - t0)}`);
		}
		Pointer.on = false;
		log(`DEMOS DONE ${passed}/${list.length}`);
	}
	/* for needs: the page draws its canvases on animation frames, which WebKit runs only while the window is on screen */
	/* I-008: the editor's menu, both editors: drawn by the page from the plug-in's entries (editorMenu, deskMenu.js),
	   opened by a right-click anywhere the page has no menu of its own (the header, the rail). menu.via(u, path) opens it afresh (the plug-in's entries as
	   they are now) and clicks the entries of a path in turn. editorMenuJourney(name, product): a submenu by the
	   keyboard, a zoom step and Updates › Check Daily by the pointer; each choice is the plug-in's to run (menuPick),
	   its effect read back from the plug-in in the menu itself (the zoom it says, the tick it sends); Esc closes the
	   submenu, then the menu. The tidy puts the zoom and Check Daily back through the same menu. */
	const $q = q => document.querySelector(q);
	const menu = {
		at: ".top .brand .ed",
		on: () => !!$q("#deskmenu") && !$q("#deskmenu").hidden,
		id: id => `#deskmenu [data-mid="${id}"]`,
		zoomSays: () => $q(menu.id("zoom"))?.querySelector("kbd")?.textContent || "",
		dailyOn: () => $q(menu.id("update-daily"))?.getAttribute("aria-checked") === "true",
		async via(hands, path = []) {
			if (menu.on()) closeDeskMenu();
			hands.rightClick(menu.at);
			if (!await until(() => menu.on() && !!$q(menu.id("zoom")), 4000)) throw new Error("the editor's menu did not open");
			for (const id of path) { await until(() => !!$q(menu.id(id)), 1000); hands.click(menu.id(id)); await sleep(120); }
		},
		titled: product => ok(menu.on() && new RegExp(product + " \\d").test($q("#deskmenu .mtitle")?.textContent || "") && !!$q(menu.id("developer")),
			"no editor menu: " + ($q("#deskmenu .mtitle")?.textContent || "(none)"))
	};
	const editorMenuJourney = (name, product) => ({
		name,
		steps: [
			{ say: "right-click the header's empty part: the editor's menu, its title the editor and version", act: hands => { if (menu.on()) closeDeskMenu(); hands.rightClick(menu.at); },
				screen: () => menu.titled(product) },
			/* the rail is on Sequence and Sound, not on Mix, Song or Control (where an earlier journey may have left the page) */
			{ say: "Esc, then right-click the track rail (outside the header, no menu of its own; on Sequence when the workspace shown has no rail): the editor's menu there too",
				act: async hands => { hands.key("Escape"); await until(() => !menu.on(), 1000);
					const shown = () => { const r = $q("#rail"); return !!r && !r.hidden && r.getBoundingClientRect().height > 0; };
					if (!shown()) { hands.click('#tabs [data-ws="seq"]'); if (!await until(shown, 3000)) throw new Error("no track rail on Sequence"); }
					hands.rightClick("#rail", {}, 0.5, 0.97); },
				screen: () => menu.titled(product) },
			{ say: "focus Zoom, press →: its submenu, its first entry focused", act: (hands, c) => { c.z0 = menu.zoomSays(); $q(menu.id("zoom")).focus(); hands.key("ArrowRight"); },
				screen: () => ok(DeskMenu.depth() === 2 && document.activeElement?.closest?.(".mpanel")?.dataset.lv === "1" && $q(menu.id("zoom")).getAttribute("aria-expanded") === "true",
					"depth " + DeskMenu.depth()) },
			{ say: "click a zoom step: the menu closes, the plug-in zooms the page", act: (hands, c) => { c.to = c.z0 === "110 %" ? "125" : "110"; hands.click(menu.id("zoom-" + c.to)); },
				screen: () => ok(!menu.on(), "menu still open") },
			{ say: "right-click again: Zoom says the new step", act: hands => menu.via(hands), screen: c => ok(menu.zoomSays() === c.to + " %", "Zoom says " + menu.zoomSays()) },
			{ say: "click Updates, then Check Daily: the menu closes", act: async (hands, c) => {
				hands.click(menu.id("updates")); await until(() => !!$q(menu.id("update-daily")), 1000); c.d0 = menu.dailyOn(); hands.click(menu.id("update-daily")); },
				screen: () => ok(!menu.on(), "menu still open") },
			{ say: "open Updates again: Check Daily's tick turned", act: hands => menu.via(hands, ["updates"]),
				screen: c => ok(!!$q(menu.id("update-daily")) && menu.dailyOn() !== c.d0, "ticked " + menu.dailyOn()) },
			{ say: "press Esc: the submenu closes, the menu stays", act: hands => hands.key("Escape"),
				screen: () => ok(menu.on() && DeskMenu.depth() === 1, "depth " + DeskMenu.depth() + (menu.on() ? "" : ", menu closed")) },
			{ say: "press Esc again: the menu closes", act: hands => hands.key("Escape"), screen: () => ok(!menu.on(), "menu still open") }
		],
		async tidy(hands, c) {
			if (c.to && /^\d+ %$/.test(c.z0 || "")) await menu.via(hands).then(() => menu.zoomSays() !== c.z0 && menu.via(hands, ["zoom", "zoom-" + parseInt(c.z0, 10)])).catch(() => { });
			if (c.d0 !== undefined) {
				await menu.via(hands, ["updates"]).catch(() => { });
				if (menu.dailyOn() !== c.d0) hands.click(menu.id("update-daily"));
			}
			if (menu.on()) closeDeskMenu();
		}
	});
	const onScreen = () => document.visibilityState === "visible" ? null : "the editor window is not on screen (display asleep or covered): its canvases are not drawn";
	/* for needs: real key events through the operating system (u.osKey) exist in the macOS plug-in only */
	const osKeyPath = () => osKeys() ? null : "no way in for real key events here (the macOS plug-in has one, mdOsKeys.h)";
	/* The page as it looks now, for a screenshot taken outside the editor (scripts/mdmm-snap.py renders it in headless
	   Chrome, where macOS gives the shell no Screen Recording): the DOM with each canvas's drawing as its background,
	   the form values as attributes, the window's size; logged in pieces as "SNAP <name> <i>/<n> <text>". */
	function snapshot(name) {
		const doc = document.documentElement.cloneNode(true), live = [...document.querySelectorAll("canvas")], copies = [...doc.querySelectorAll("canvas")];
		live.forEach((c, i) => { try { const d = c.width && c.height ? c.toDataURL() : ""; if (d && copies[i]) copies[i].style.backgroundImage = `url(${d})`, copies[i].style.backgroundSize = "100% 100%"; } catch (_) { } });
		const liveSel = [...document.querySelectorAll("select")], copySel = [...doc.querySelectorAll("select")];
		liveSel.forEach((x, i) => { const c = copySel[i]; if (c) [...c.options].forEach((o, k) => k === x.selectedIndex ? o.setAttribute("selected", "") : o.removeAttribute("selected")); });
		doc.querySelectorAll("script").forEach(x => x.remove());
		doc.setAttribute("data-snap-size", innerWidth + "x" + innerHeight);
		doc.querySelector("body")?.setAttribute("style", (document.body.getAttribute("style") || "") + `;--snapzoom:${document.body.style.zoom || ""}`);
		const text = JSON.stringify("<!doctype html>\n" + doc.outerHTML), size = 120000, n = Math.ceil(text.length / size);
		for (let i = 0; i < n; i++) Bridge.log(`SNAP ${name} ${i + 1}/${n} ${text.slice(i * size, (i + 1) * size)}`);
	}
	/* B-054: the page as macOS 12's WebKit 15 lays it out, measured in this engine. Every <style> is read as WebKit 15
	   reads it (DeskCompat.safari15: no subgrid, no :has(), the colour rewrite), the boxes measured, the stylesheets
	   put back. What moved or changed size is listed, outermost first (an element inside one already listed and
	   moved with it is left out): [] when the old engine lays the page out as this one. */
	async function safari15Diff(slack = 2, tries = 3) {
		const all = [...document.body.querySelectorAll("*")].filter(e => !e.closest("#journey-pointer"));
		const boxes = () => all.map(e => { const r = e.getClientRects().length ? e.getBoundingClientRect() : null; return r && [r.left, r.top, r.width, r.height]; });
		/* two frames and the fonts (a stylesheet read again declares its fonts again) */
		const frame = async () => { await new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))); if (document.fonts) await document.fonts.ready; await sleep(60); };
		const before = boxes(), sheets = [...document.querySelectorAll("style")], kept = sheets.map(x => x.textContent);
		let after;
		try { sheets.forEach(x => { x.textContent = DeskCompat.safari15(x.textContent); }); await frame(); after = boxes(); }
		finally { sheets.forEach((x, i) => { x.textContent = kept[i]; }); await frame(); }
		/* the page drew itself again meanwhile (its elements replaced): measure again */
		if (all.some(e => !e.isConnected) && tries > 1) { await sleep(400); return safari15Diff(slack, tries - 1); }
		const moved = [], seen = new Map();
		all.forEach((e, i) => {
			const a = before[i], b = after[i];
			if (!a && !b) return;
			if (!a || !b) { moved.push([e, b ? "shown" : "gone"]); seen.set(e, null); return; }
			const d = b.map((v, k) => Math.round(v - a[k]));
			if (d.every(v => Math.abs(v) <= slack)) return;
			seen.set(e, d);
			/* one already listed holds it and it only moved along (same size, same shift) */
			for (let p = e.parentElement; p; p = p.parentElement) if (seen.has(p)) { const q = seen.get(p); if (!q || (Math.abs(d[2]) <= slack && Math.abs(d[3]) <= slack) || (q[0] === d[0] && q[1] === d[1])) return; }
			moved.push([e, `${Math.round(a[2])}x${Math.round(a[3])} at ${Math.round(a[0])},${Math.round(a[1])} -> ${Math.round(b[2])}x${Math.round(b[3])} at ${Math.round(b[0])},${Math.round(b[1])}`]);
		});
		return moved.map(([e, what]) => describe(e) + " " + what);
	}
	/* Every view of an editor, two journeys from one list of {name, open(u, c), close(u, c), wait}:
	     <prefix>-old-webkit   in every run: each view laid out as WebKit 15 does is the same as here (safari15Diff)
	     <prefix>-shots-views  asked by name only: each view as a snapshot (scripts/mdmm-snap.py [--safari15] [--size])
	   A view that cannot open here (needs) is skipped. */
	function viewJourneys(prefix, views) {
		/* each view: open, look, close (in one step, so the next view starts from a closed page); every view's
		   differences are logged ("OLDWEBKIT <view> ...") and the last step fails on any of them */
		const step = (v, shots) => ({ say: v.name, act: async (u, c) => {
			c.diffs = c.diffs || {};
			const why = v.needs ? v.needs() : null;
			if (why) { Bridge.log(`OLDWEBKIT ${prefix}-${v.name} skipped: ${why}`); return; }
			try {
				await v.open(u, c); await sleep(v.wait ?? 700);
				if (shots) snapshot(`${prefix}-${v.name}`);
				else {
					const d = await safari15Diff();
					if (d.length) c.diffs[v.name] = d;
					Bridge.log(`OLDWEBKIT ${prefix}-${v.name} ${d.length ? d.length + ": " + d.join("; ") : "same"}`);
				}
			} finally { if (v.close) { await v.close(u, c); await sleep(300); } }
		} });
		const verdict = { say: "every view as here", screen: c => { const k = Object.keys(c.diffs || {});
			return ok(!k.length, k.map(n => `${n}: ${c.diffs[n].length} boxes laid out otherwise by WebKit 15 (${c.diffs[n].slice(0, 3).join("; ")})`).join(" | ")); } };
		return [
			{ name: `${prefix}-old-webkit`, needs: () => typeof DeskCompat === "undefined" || !DeskCompat.safari15 ? "no DeskCompat.safari15" : null, steps: [...views.map(v => step(v, false)), verdict] },
			{ name: `${prefix}-shots-views`, needs: () => location.search.includes(`${prefix}-shots`) ? null : "screenshots only when asked by name", steps: views.map(v => step(v, true)) }
		];
	}
	return { run, demo, ok, sleep, until, u, onScreen, osKeyPath, menu, editorMenuJourney, snapshot, safari15Diff, viewJourneys };
})();
