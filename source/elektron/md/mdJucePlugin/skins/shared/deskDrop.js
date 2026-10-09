"use strict";
/* Files dropped on the window (FOUNDATION.md, "Files dropped on the window"; macOS for now), one file for both
   editors (skins/shared/). The plug-in keeps the files and says what each is,
     {type:"drop", drop, items:[{n, kind:"rom"|"sysex"|"sample"|"unknown", name}], x, y}   x, y: the page's CSS pixels
   and the page decides what each becomes, in this order, with the window's commands, which name a file by its drop and
   its number, never by a path (deskHost.cpp):
     rom      dropRom {drop, n}, one ROM a drop. While a firmware runs the page asks first (the machine starts again
              with the new one); while the start-up card asks for a firmware it installs at once. A ROM installed ends
              the drop: the other files would reach a machine that is starting again.
     sysex    dropSyx {drop, n}: the SysEx import window for that file, as Import SysEx… after its chooser; one a drop.
     sample   the page's own (Drop.host.samples): the Machinedrum's Sampler loads them into ROM slots (dropSample
              {drop, n, slot}, mdDeskSampler.js); the Monomachine has none.
     unknown  said.
   What the page has to say about a drop is one toast at its end.
   {type:"dragFiles", active}: files the window takes are over it (true) or have left (false): a frame and a line say
   what can be dropped (the drop takes them away too).
   Drop.host = {send(command), ask(html, buttons), toast(text), romWanted(), samples(drop, items, x, y) -> note, hint}
   is the page's (mdDeskRom.js, mmAdapter.js): send shows a refusal its own way; buttons as the pages' ask,
   [[label, class, run], ...]; romWanted: the start-up card asks for a firmware; hint: the frame's line. */
const Drop = (() => {
	const esc = t => String(t).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
	const names = list => list.map(i => i.name).join(", ");
	function onDrop(m) {
		hint(false);
		const h = Drop.host; if (!h) return;
		const of = kind => (m.items || []).filter(i => i.kind === kind);
		const roms = of("rom"), syx = of("sysex"), samples = of("sample"), unknown = of("unknown"), notes = [];
		const say = () => { if (notes.length) h.toast(notes.join(" ")); };
		/* everything but the ROM, in order */
		const rest = () => {
			if (syx.length) {
				h.send({ op: "dropSyx", drop: m.drop, n: syx[0].n });
				if (syx.length > 1)
					notes.push(`One SysEx file at a time: ${syx[0].name} opens; `
						+ `drop ${names(syx.slice(1))} after its import.`);
			}
			if (samples.length) {
				const note = h.samples ? h.samples(m.drop, samples, m.x, m.y) : "This editor takes no samples.";
				if (note) notes.push(note);
			}
			if (unknown.length)
				notes.push(`Not a ROM (.bin, .zip), a SysEx file (.syx) or a sample (.wav, .aif): ${names(unknown)}.`);
			say();
		};
		if (!roms.length) { rest(); return; }
		if (roms.length > 1) notes.push(`One ROM at a time: ${names(roms.slice(1))} not used.`);
		const install = () => {
			h.send({ op: "dropRom", drop: m.drop, n: roms[0].n });
			if (syx.length + samples.length + unknown.length)
				notes.push("The machine starts again with the new ROM: drop the other files once it runs.");
			say();
		};
		if (h.romWanted()) { install(); return; }
		h.ask(`Install <b>${esc(roms[0].name)}</b> as the firmware? The machine starts again with it.`,
			[["Install", "danger", install], ["Cancel", "", rest]]);
	}
	/* the frame over the window while files the window takes are over it; made when first needed */
	let frame = null;
	function hint(on) {
		if (!frame && !on) return;
		if (!frame) {
			const style = document.createElement("style");
			style.textContent = ".deskdrop{position:fixed;inset:6px;z-index:90;pointer-events:none;border:2px dashed "
				+ "var(--print,#ccc);border-radius:8px;display:grid;place-items:end center;padding-bottom:32px;"
				+ "box-sizing:border-box}"
				+ ".deskdrop[hidden]{display:none}"
				+ ".deskdrop span{font:600 15px/1.3 var(--sans,system-ui,sans-serif);padding:8px 14px;"
				+ "border-radius:6px;background:var(--plate,#222);color:var(--body,var(--print,#eee));"
				+ "box-shadow:0 8px 24px rgba(0,0,0,.35)}";
			document.head.appendChild(style);
			frame = document.createElement("div");
			frame.className = "deskdrop"; frame.hidden = true; frame.setAttribute("role", "status");
			frame.appendChild(document.createElement("span"));
			document.body.appendChild(frame);
		}
		if (on) frame.firstChild.textContent = (Drop.host && Drop.host.hint)
			|| "Drop a ROM (.bin, .zip) or a SysEx file (.syx)";
		frame.hidden = !on;
	}
	Bridge.onMessage(m => {
		if (m.type === "drop") onDrop(m);
		else if (m.type === "dragFiles") hint(!!m.active);
	});
	return { host: null, onDrop, hint, shown: () => !!frame && !frame.hidden };
})();
