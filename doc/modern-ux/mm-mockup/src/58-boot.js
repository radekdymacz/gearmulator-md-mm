/* BOOT BEGIN (P7): the start-up card, the same text in both editors (checked by the sync scripts). While
   the firmware starts, a modal card over the whole window (the modal layer's "boot" kind: nothing behind it
   takes a key or a click) shows the machine's own LCD, mirrored big and pixel for pixel, a progress bar and
   what to wait for; it fades out when the machine takes input. The same card is the first run: NO ROM and
   ROM ERROR show a clear "choose your ROM" button (the host's native file chooser) and the ROM folder. The page
   reads no ROM bytes: the host opens, checks and copies the file (Boot.host).
     Boot.update({state, machine, text})  state: "loading" | "booting" | "missing" | "unsupported" | "ready"
     Boot.lcd(bytes)                     the firmware's 128 x 64 screen (1024 bytes, rows of 16, MSB first)
     Boot.rom({ok, text})                the host's verdict on a chosen file
     Boot.showInstalled({machine, os, name, size, inFolder})   the same card while a firmware runs (LOAD ROM):
                                         the current image, a button to choose another, Remove, Show the ROM folder, Close
     Boot.host = {chooseRom(), revealRom(), recheck(), removeRom(info), say(text)}   the app's host calls
   A file dragged onto the page is not opened by the web view (that would replace the page): the card says to click
   the button instead. */
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
  <button class="bootzone" data-bootrom="choose" aria-describedby="bootres"><b id="bootzone"></b><span>Click here to choose the file. The editor checks it and copies it into its ROM folder.</span></button>
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
		$b("bootzone").textContent = `Choose another ${romName(machine)} ROM (.bin or .zip) to replace it`;
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
		if (rom) $b("bootzone").textContent = `Choose your ${romName(machine)} ROM (.bin or .zip, 8 MB)`;
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
	/* a dragged file: the web view must not open it (it would replace the page), and the card points at the button */
	const files = e => e.dataTransfer && Array.prototype.indexOf.call(e.dataTransfer.types || [], "Files") >= 0;
	document.addEventListener("dragover", e => { if (files(e)) { e.preventDefault(); e.dataTransfer.dropEffect = "none"; } }, true);
	document.addEventListener("drop", e => {
		if (!files(e)) return;
		e.preventDefault();
		const f = e.dataTransfer.files && e.dataTransfer.files[0], open = !card.hidden && !$b("bootrom").hidden;
		const text = f && /\.syx$/i.test(f.name || "") ? "Use Import SysEx… in the menu to open a .syx." : open ? "Click here to choose the ROM." : "Use LOAD ROM in the engine menu to choose a ROM.";
		if (!open) { if (Boot.host && Boot.host.say) Boot.host.say(text); } else rom({ ok: false, text });
	}, true);
	return { update, lcd, rom, showInstalled, host: null, state: () => shown };
})();
/* BOOT END */
