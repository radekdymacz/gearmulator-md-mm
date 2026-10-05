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
   * matches any run of characters (journey-seq-*, journey-md-seq-first-beat,md-mix-*). */
const Journey = (() => {
	const sleep = ms => new Promise(r => setTimeout(r, ms));
	const until = async (f, ms) => { const end = performance.now() + ms; while (performance.now() < end) { if (f()) return true; await sleep(40); } return !!f(); };
	/* a check's answer: true passes; anything else is what was seen */
	const ok = (pass, seen) => pass ? true : (seen == null ? "no" : String(seen));
	const describe = el => !el ? "nothing" : (el.id ? "#" + el.id : el.tagName.toLowerCase() + (el.className && typeof el.className === "string" ? "." + el.className.trim().split(/\s+/).join(".") : ""));

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
	const mods = (m = {}) => ({ shiftKey: !!m.shift, altKey: !!m.alt, metaKey: !!m.cmd, ctrlKey: !!m.ctrl });
	const pe = (type, target, x, y, m, buttons) => target.dispatchEvent(new PointerEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true,
		pointerId: 1, pointerType: "mouse", isPrimary: true, button: 0, buttons, clientX: x, clientY: y }, mods(m))));
	const me = (type, target, x, y, m, buttons, detail = 1) => target.dispatchEvent(new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, composed: true,
		button: 0, buttons, clientX: x, clientY: y, detail, view: window }, mods(m))));
	const u = {
		el, sleep, until,
		/* a mouse click at the element's centre (or at fx, fy of its box): down, up, click, as the browser sends them */
		click(q, m = {}, fx, fy) {
			const e = el(q), { x, y } = pointIn(e, fx, fy), t = document.elementFromPoint(x, y);
			pe("pointerdown", t, x, y, m, 1); me("mousedown", t, x, y, m, 1);
			pe("pointerup", t, x, y, m, 0); me("mouseup", t, x, y, m, 0); me("click", t, x, y, m, 0);
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
				pe("pointerdown", down, a.x, a.y, m, 1); me("mousedown", down, a.x, a.y, m, 1);
				for (const p of path) {
					at = Array.isArray(p) ? { x: a.x + p[0], y: a.y + p[1] } : pointIn(el(p));
					const over = document.elementFromPoint(at.x, at.y) || document.body;
					pe("pointermove", target(), at.x, at.y, m, 1); me("mousemove", over, at.x, at.y, m, 1);
					await sleep(o.stepMs ?? 30);
				}
				const up = target();
				pe("pointerup", up, at.x, at.y, m, 0); me("mouseup", up, at.x, at.y, m, 0);
			} finally { Element.prototype.setPointerCapture = cap0; }
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
			const go = t.dispatchEvent(new KeyboardEvent("keydown", o));
			/* the browser's own default action on a focused button: Enter clicks it on keydown, Space on keyup, unless
			   the page prevented the keydown (a synthetic event has no default action, so it is done here) */
			const button = t.closest?.("button,[role=button]") && !t.disabled;
			if (go && button && key === "Enter") t.click();
			t.dispatchEvent(new KeyboardEvent("keyup", o));
			if (go && button && key === " ") t.click();
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
	/* the journeys this page was asked for: null when none */
	function wanted(all) {
		const m = location.search.match(/[?&]selftest=(journey[^&]*)/); if (!m) return null;
		const kind = decodeURIComponent(m[1]);
		if (kind === "journey") return all;
		const pats = kind.replace(/^journey-/, "").split(",").filter(Boolean).map(p => new RegExp("^" + p.replace(/[.+?^${}()|[\]\\]/g, "\\$&").replace(/\*/g, ".*") + "$"));
		return all.filter(j => pats.some(r => r.test(j.name) || r.test(j.name.replace(/^(md|mm)-/, ""))));
	}
	async function runOne(j, log, context) {
		const c = {}, n = j.steps.length;
		let reason = "";
		/* what the journey needs that this run may not have (the window on screen): it is skipped, not failed */
		const missing = j.needs ? j.needs() : null;
		if (missing) { log(`JOURNEY ${j.name} SKIP ${missing}`); return "skip"; }
		for (let i = 0; i < n && !reason; i++) {
			const st = j.steps[i], t0 = performance.now(), head = `JOURNEY ${j.name} ${i + 1}/${n}`;
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
			if (pass) { log(`${head} ok ${st.say} (${ms} ms)${c.note ? " · " + c.note : ""}`); c.note = ""; continue; }
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
	/* for needs: the page draws its canvases on animation frames, which WebKit runs only while the window is on screen */
	const onScreen = () => document.visibilityState === "visible" ? null : "the editor window is not on screen (display asleep or covered): its canvases are not drawn";
	return { run, ok, sleep, until, u, onScreen };
})();
