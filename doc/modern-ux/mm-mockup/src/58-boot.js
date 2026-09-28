/* BOOT BEGIN (P7): the start-up card, the same text in both editors (checked by the sync scripts). While
   the firmware starts, a modal card over the whole window (the modal layer's "boot" kind: nothing behind it
   takes a key or a click) shows the machine's own LCD, mirrored big and pixel for pixel, a progress bar and
   what to wait for; it fades out when the machine takes input. The same card is the first run: NO ROM and
   ROM ERROR show the firmware steps, a native file chooser, a drop target for the whole window, and the
   ROM folder. The page reads no ROM bytes: the host opens, checks and copies the file (Boot.host).
     Boot.update({state, machine, text})  state: "loading" | "booting" | "missing" | "unsupported" | "ready"
     Boot.lcd(bytes)                     the firmware's 128 x 64 screen (1024 bytes, rows of 16, MSB first)
     Boot.rom({ok, text})                the host's verdict on a chosen or dropped file
     Boot.host = {chooseRom(), revealRom(), recheck()}   the app's host calls */
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
  <ol class="bootsteps" id="bootsteps"></ol>
  <div class="btnrow"><button class="cream" data-bootrom="choose">Choose ROM file…</button><button data-bootrom="recheck">Check again</button></div>
  <p class="bootdrop">…or drop the <span class="mono">.bin</span>, or a <span class="mono">.zip</span> with it, anywhere on this window.</p>
  <p class="bootres" id="bootres" role="status"></p>
  <p class="bootpriv">The firmware stays on this computer: the editor checks it and copies it into its ROM folder, and sends it nowhere. <button class="linkkey" data-bootrom="folder">Show the ROM folder</button></p>
 </div>
</div>`;
	document.body.appendChild(card);
	const $b = id => card.querySelector("#" + id);
	let shown = null, t0 = 0, raf = 0, machine = "Machinedrum", lastBits = null;
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
	function steps(m) {
		const os = m === "Monomachine" ? "OS 1.32B" : "OS 1.63", units = m === "Monomachine" ? "SFX-6, SFX-60 MKI and MKII use the same image" : "UW, MKII and MKI units use the same image";
		return `<li>Dump the <b>${os}</b> flash image from your ${m} (8 MiB, <span class="mono">.bin</span>). ${units}.</li><li>Choose it here, or drop it on this window. The editor checks that it is the ${m}'s ${os} before it copies it.</li><li>The machine starts at once: no need to reopen the editor.</li>`;
	}
	function update(o) {
		machine = o.machine || machine;
		const st = o.state === "animating" ? "booting" : o.state;
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
		$b("bootline").textContent = rom ? (st === "unsupported" ? "The ROM in the folder is another OS or a damaged dump. Choose the right image." : "The editor runs the real firmware, which cannot ship with it: you add the one from your own machine.")
			: "Keys work when the start-up animation ends.";
		$b("bootrom").hidden = !rom;
		if (rom) $b("bootsteps").innerHTML = steps(machine);
		card.hidden = false;
		draw();
		if (!raf && st === "booting") raf = requestAnimationFrame(tick);
	}
	function lcd(bits) { lastBits = bits; if (!card.hidden) draw(); }
	function rom(r) {
		const el = $b("bootres");
		el.textContent = r.text || ""; el.className = "bootres " + (r.ok ? "ok" : "bad");
		if (r.ok) { $b("boott").textContent = `Starting the ${machine}…`; t0 = performance.now(); }
	}
	card.addEventListener("click", e => {
		const k = e.target.closest("[data-bootrom]"); if (!k || !Boot.host) return;
		const a = k.dataset.bootrom;
		if (a === "choose") Boot.host.chooseRom(); else if (a === "recheck") Boot.host.recheck(); else if (a === "folder") Boot.host.revealRom();
	});
	return { update, lcd, rom, host: null, state: () => shown };
})();
/* BOOT END */
