/* BOOT BEGIN (P7): the start-up card, the same text in both editors (checked by the sync scripts). While
   the firmware starts, a modal card over the whole window (the modal layer's "boot" kind: nothing behind it
   takes a key or a click) shows the machine's own LCD, mirrored big and pixel for pixel, a progress bar and
   what to wait for; it fades out when the machine takes input. The same card is the first run: NO ROM and
   ROM ERROR show the firmware steps, a native file chooser, a drop target for the whole window, and the
   ROM folder. The page reads no ROM bytes: the host opens, checks and copies the file (Boot.host).
     Boot.update({state, machine, text})  state: "loading" | "booting" | "missing" | "unsupported" | "ready"
     Boot.lcd(bytes)                     the firmware's 128 x 64 screen (1024 bytes, rows of 16, MSB first)
     Boot.rom({ok, text})                the host's verdict on a chosen or dropped file
     Boot.showInstalled({machine, os, name, size, inFolder})   the same card while a firmware runs (LOAD ROM):
                                         the current image, a drop zone to replace it, Remove, Show the ROM folder, Close
     Boot.host = {chooseRom(), revealRom(), recheck(), romBytes(msg), removeRom(info), log(text), say(text)}   the app's host calls;
     Boot.ack(m) hands it the plug-in's romBytesAck
   A file dropped anywhere on the page (HTML5 drag and drop; the web view takes the drag, the page reads the file
   and sends it in pieces, romBytes, a few in flight, each acknowledged) is checked here and installed by the host like a chosen one. */
const Boot = (() => {
	const EXPECT = { Machinedrum: 14000, Monomachine: 11000 };	// ms from the first BOOTING OS to input (measured)
	const card = document.createElement("div");
	card.id = "bootcard"; card.className = "bootcard"; card.hidden = true;
	card.setAttribute("role", "dialog"); card.setAttribute("aria-modal", "true"); card.setAttribute("aria-labelledby", "boott");
	card.innerHTML = `<div class="bootbox" tabindex="-1">
 <div class="bootlcd"><canvas id="bootlcd" width="128" height="64" aria-label="The machine's own screen"></canvas></div>
 <h2 id="boott">Starting…</h2>
 <div class="bootbar" id="bootbar" role="progressbar" aria-valuemin="0" aria-valuemax="100"><i></i></div>
 <p class="bootline" id="bootline">Keys work when the start-up animation ends.</p>
 <div class="bootrom" id="bootrom" hidden>
  <p class="bootcur" id="bootcur" hidden></p>
  <button class="bootzone" data-bootrom="choose" aria-describedby="bootres"><b id="bootzone"></b><span>A <span class="mono">.bin</span>, or a <span class="mono">.zip</span> with it. Drop it anywhere on this window, or click here to choose it.</span></button>
  <p class="bootres" id="bootres" role="status"></p>
  <p class="bootpriv">The firmware stays on this computer: the editor checks it and copies it into its ROM folder, and sends it nowhere. <button class="linkkey" data-bootrom="folder">Show the ROM folder</button> <button class="linkkey" data-bootrom="recheck">Check again</button> <button class="linkkey" data-bootrom="remove" hidden>Remove the ROM</button> <button class="linkkey" data-bootrom="close" hidden>Close</button></p>
 </div>
</div>`;
	document.body.appendChild(card);
	const $b = id => card.querySelector("#" + id);
	let shown = null, t0 = 0, raf = 0, machine = "Machinedrum", lastBits = null, manage = null;
	const cssv = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
	function draw() {
		const c = $b("bootlcd"), g = c.getContext("2d");
		g.fillStyle = cssv("--lcd") || "#b8c4b0"; g.fillRect(0, 0, 128, 64);
		if (!lastBits) return;
		g.fillStyle = cssv("--ink") || "#1a2418";
		for (let y = 0; y < 64; y++) for (let x = 0; x < 128; x++) if (lastBits[y * 16 + (x >> 3)] & (0x80 >> (x & 7))) g.fillRect(x, y, 1, 1);
	}
	function tick() {
		raf = 0;
		if (shown !== "booting") return;
		const f = Math.min(.95, (performance.now() - t0) / (EXPECT[machine] || 14000));
		$b("bootbar").querySelector("i").style.width = (f * 100).toFixed(1) + "%";
		$b("bootbar").setAttribute("aria-valuenow", Math.round(f * 100));
		raf = requestAnimationFrame(tick);
	}
	function romName(m) { return m === "Monomachine" ? "Monomachine SFX-60 OS 1.32B" : "Machinedrum OS 1.63"; }
	const esc = t => String(t).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
	const links = (m) => {	// which of the card's links show: the first-run ones, or the installed firmware's
		card.querySelector('[data-bootrom="recheck"]').hidden = m;
		card.querySelector('[data-bootrom="remove"]').hidden = !m;
		card.querySelector('[data-bootrom="close"]').hidden = !m;
	};
	function leaveManage() { if (!manage) return; manage = null; card.classList.remove("manage"); $b("bootcur").hidden = true; links(false); }
	/* LOAD ROM with a firmware running: the same card, the current image on one line */
	function showInstalled(o) {
		machine = o.machine || machine; manage = o; shown = "manage";
		card.classList.remove("out", "ind"); card.classList.add("rom", "manage");
		$b("boott").textContent = `${machine} firmware`;
		$b("bootline").hidden = true; $b("bootrom").hidden = false;
		$b("bootzone").textContent = "Drop another ROM here to replace it, or choose it";
		const cur = $b("bootcur");
		cur.innerHTML = `<b>${esc(o.os)}</b><span class="bootfile" title="${esc(o.name)}${o.inFolder ? "" : " (outside the editor's ROM folder)"}">${esc(o.name)}</span><i>${(o.size / 1048576).toFixed(1)} MiB</i>`;
		cur.hidden = false;
		$b("bootres").textContent = ""; $b("bootres").className = "bootres";
		links(true);
		card.hidden = false;
	}
	function update(o) {
		machine = o.machine || machine;
		const st = o.state === "animating" ? "booting" : o.state;
		if (manage) { if (st === "ready" || !st) return; leaveManage(); shown = null; }
		if (st === "ready" || !st) {
			if (shown && !card.hidden) { card.classList.add("out"); setTimeout(() => { if (shown === null) { card.hidden = true; card.classList.remove("out"); } }, 380); }
			shown = null; return;
		}
		card.classList.remove("out");
		if (st !== shown) {
			if (st === "booting" || st === "loading") t0 = performance.now();
			shown = st;
		}
		const rom = st === "missing" || st === "unsupported";
		card.classList.toggle("rom", rom);
		card.classList.toggle("ind", st === "loading");
		$b("boott").textContent = rom ? (st === "missing" ? `${machine} firmware needed` : `This is not the ${machine}'s firmware`) : st === "loading" ? `Preparing the ${machine}…` : `Starting the ${machine}…`;
		$b("bootline").textContent = st === "unsupported" ? "The ROM in the ROM folder is another OS or a damaged dump." : rom ? "" : "Keys work when the start-up animation ends.";
		$b("bootline").hidden = st === "missing";
		$b("bootrom").hidden = !rom;
		if (rom) $b("bootzone").textContent = `Drop your ${romName(machine)} ROM (.bin, 8 MB) here, or choose it`;
		card.hidden = false;
		draw();
		if (!raf && st === "booting") raf = requestAnimationFrame(tick);
	}
	function lcd(bits) { lastBits = bits; if (!card.hidden) draw(); }
	function rom(r) {
		if (manage && r.ok) { leaveManage(); shown = null; card.hidden = true; return; }	// the machine starts again on the new image
		const el = $b("bootres");
		el.textContent = r.text || ""; el.className = "bootres " + (r.ok ? "ok" : "bad");
		if (r.ok) { $b("boott").textContent = `Starting the ${machine}…`; t0 = performance.now(); }
	}
	card.addEventListener("click", e => {
		const k = e.target.closest("[data-bootrom]"); if (!k || !Boot.host) return;
		const a = k.dataset.bootrom;
		if (a === "choose") Boot.host.chooseRom(); else if (a === "recheck") Boot.host.recheck(); else if (a === "folder") Boot.host.revealRom();
		else if (a === "remove" && manage && Boot.host.removeRom) Boot.host.removeRom(manage);
		else if (a === "close") { leaveManage(); shown = null; card.hidden = true; }
	});
	/* ROMDROP BEGIN: what a dropped file must be, and its pieces for the bridge (pure: a File in, a plan out) */
	function romDropPlan(file, want, piece = 262144) {
		const ROM = 0x800000, LIMIT = 64 * 1048576;
		if (!file) return Promise.resolve({ ok: false, text: "Drop the firmware image: a .bin (or a .zip with it) from your own machine." });
		const name = file.name || "", ext = ((/\.([A-Za-z0-9]+)$/.exec(name) || [])[1] || "").toLowerCase();
		if (ext !== "bin" && ext !== "zip") return Promise.resolve({ ok: false, text: `"${name || "This"}" is not a firmware image. Drop the ${want} .bin (or a .zip with it).` });
		if (ext === "bin" && file.size !== ROM) return Promise.resolve({ ok: false, text: `This file is ${(file.size / 1048576).toFixed(2)} MiB. The ${want} image is exactly 8 MiB.` });
		if (file.size > LIMIT) return Promise.resolve({ ok: false, text: "This file is far larger than a firmware image." });
		return file.arrayBuffer().then(ab => {
			const buf = new Uint8Array(ab), count = Math.max(1, Math.ceil(buf.length / piece));
			const at = i => i * piece;
			const data = i => { const p = buf.subarray(at(i), at(i) + piece); let s = ""; for (let k = 0; k < p.length; k += 0x8000) s += String.fromCharCode.apply(null, p.subarray(k, k + 0x8000)); return btoa(s); };
			return { ok: true, name, size: buf.length, count, at, data };
		}, () => ({ ok: false, text: "The file could not be read." }));
	}
	/* ROMDROP END */
	function said(text, ok) { if (card.hidden || $b("bootrom").hidden) { if (Boot.host && Boot.host.say) Boot.host.say(text); } else rom({ ok, text }); }
	/* One transfer at a time: a small window of pieces in flight, each acknowledged by the plug-in (romBytesAck);
	   a new drop cancels the running one (its id is older, the plug-in drops its pieces), and 5 s without an
	   acknowledgement ends it with a message in the card instead of hanging. */
	const WINDOW = 4, ACK_MS = 5000;
	let xfer = null, tidLast = 0;
	function endTransfer(text, ok) {
		if (!xfer) return;
		clearTimeout(xfer.timer); const x = xfer; xfer = null;
		if (text) { if (Boot.host.log) Boot.host.log(`drop: ${ok ? "sent" : "failed"}: ${text}`); said(text, ok); }
		return x;
	}
	function pump() {
		const x = xfer; if (!x) return;
		while (x.next < x.plan.count && x.next - x.done < WINDOW) {
			const i = x.next++;
			Boot.host.romBytes({ tid: x.tid, name: x.plan.name, size: x.plan.size, count: x.plan.count, index: i, offset: x.plan.at(i), data: x.plan.data(i) });
		}
		clearTimeout(x.timer);
		x.timer = setTimeout(() => { if (xfer === x) endTransfer("The plug-in did not answer while the file was sent. Drop it again.", false); }, ACK_MS);
	}
	/* the plug-in's acknowledgement of a piece: {tid, index, ok, text} */
	function ack(m) {
		const x = xfer; if (!x || m.tid !== x.tid) return;
		if (!m.ok) { endTransfer(m.text || "The plug-in refused the file.", false); return; }
		x.done++;
		if (x.done >= x.plan.count) {
			const ms = Math.round(performance.now() - x.t0);
			if (Boot.host.log) Boot.host.log(`drop: ${x.plan.size} bytes in ${x.plan.count} pieces, ${(ms / 1000).toFixed(2)} s`);
			endTransfer("", true); return;
		}
		said(`Sending ${x.plan.name}… ${x.done}/${x.plan.count}`, true);
		pump();
	}
	async function dropped(file, opt = {}) {
		const h = Boot.host || {}, log = t => h.log && h.log(t);
		if (xfer) endTransfer("", false), log("drop: an earlier transfer was replaced");
		log(`drop: page got ${file ? file.name + " " + file.size : "no file"}`);
		const plan = await romDropPlan(file, romName(machine), opt.piece);
		if (!plan.ok) { log("drop: refused: " + plan.text); said(plan.text, false); return; }
		if (!h.romBytes) { said("This page has no host to install the firmware.", false); return; }
		if (xfer) endTransfer("", false);
		tidLast = Math.max(tidLast + 1, Date.now() % 1000000000);
		xfer = { tid: tidLast, plan, next: 0, done: 0, t0: performance.now(), timer: 0 };
		said(`Sending ${plan.name}…`, true);
		pump();
	}
	/* the web view takes the drag: it shows on the card and the page reads the file (Finder puts a file, no path, on the pasteboard) */
	let over = 0;
	const files = e => e.dataTransfer && Array.prototype.indexOf.call(e.dataTransfer.types || [], "Files") >= 0;
	const hover = on => { card.classList.toggle("dropping", on); document.documentElement.classList.toggle("romdrop", on); };
	document.addEventListener("dragenter", e => { if (!files(e)) return; e.preventDefault(); over++; hover(true); }, true);
	document.addEventListener("dragover", e => { if (!files(e)) return; e.preventDefault(); e.dataTransfer.dropEffect = "copy"; }, true);
	document.addEventListener("dragleave", e => { if (!files(e)) return; over = Math.max(0, over - 1); if (!over) hover(false); }, true);
	document.addEventListener("drop", e => {
		if (!files(e)) return;
		over = 0; hover(false);
		const f = e.dataTransfer.files && e.dataTransfer.files[0];
		if (f && /\.syx$/i.test(f.name || "")) return;	// a SysEx file: the window opens it (the web view's own navigation, cancelled by the host)
		e.preventDefault();
		dropped(f);
	}, true);
	return { update, lcd, rom, showInstalled, romDropPlan, ack, drop: dropped, host: null, state: () => shown };
})();
/* BOOT END */
