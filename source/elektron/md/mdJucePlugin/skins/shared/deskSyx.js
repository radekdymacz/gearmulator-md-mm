"use strict";
/* SysEx import and export (P7, B-019), one file for both editors and both mockups (skins/shared/).
   The host opens, parses and writes the files (the page never reads their bytes). A .syx chosen with
   "Import SysEx…" comes back as a preview, here a panel of the modal layer: what is in the file, which slots
   it writes, what plays now, what cannot be sent and why. The preview informs; it is no gate. "Import" sends
   the chosen messages to the machine as they are, like a MIDI cable (the firmware decides what it takes),
   then the host reads every document back and reports per item what the machine did. Kinds are ticked
   off as a whole, single items by clicking their name. No Undo: Export SysEx… first keeps a way back.
     Syx.keys()            the library's two keys (markup)
     Syx.preview(m)        {type:"syxPreview", ok, text, file, model, fullBackup, items:{kind:[{slot,name,kit?,overwrites,plays,format,readable}]},
                            skipped, skippedCount, messages}
     Syx.progress(m)       {type:"syxProgress", phase, done, total, running, text, report?:{taken, converted, ignored, differs,
                            changed, unknown, "no reply", commands, unsent, items:[{kind, slot, name, outcome, text}]}}
     Syx.exported(m)       {type:"syxExport", ok, text}
     Syx.host = {choose(), exportAll(), start(kinds, skip), stop()}   the app's host calls */
const Syx = (() => {
	const KINDS = [["kit", "Kits"], ["pattern", "Patterns"], ["song", "Songs"], ["global", "Globals"], ["other", "Other messages"]];
	const OFF = { global: true, other: true };	// settings and commands: off unless ticked
	const pop = document.createElement("div");
	pop.id = "syxpop"; pop.className = "syxpop libpop"; pop.hidden = true;
	pop.setAttribute("role", "dialog"); pop.setAttribute("aria-label", "Import SysEx");
	document.body.appendChild(pop);
	let last = null, running = false;
	const skip = new Set();
	const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
	const chosen = () => [...pop.querySelectorAll("[data-syxkind]:checked")].map(b => b.dataset.syxkind);
	const picked = () => { const k = chosen(), items = last?.items || {}; return k.flatMap(x => (items[x] || []).filter(i => !skip.has(x + ":" + i.slot))); };
	function sums() {
		const list = picked(), n = list.length, over = list.filter(i => i.overwrites).length, plays = list.filter(i => i.plays).length;
		const go = pop.querySelector('[data-syxgo="start"]');
		if (go) { go.textContent = n ? `Import ${n} item${n > 1 ? "s" : ""}` : "Nothing chosen"; go.disabled = !n || running; go.className = over ? "danger" : "cream"; }
		const w = pop.querySelector(".syxwarn");
		if (!w || running) return;
		const parts = [];
		if (over) parts.push(`${over} slot${over > 1 ? "s" : ""} on the machine hold data and are overwritten.`);
		if (plays) parts.push(`${plays} of them play${plays > 1 ? "" : "s"} now (marked ▶): the machine takes the dump over it as from a cable, and unsaved edits of the kit that plays may be lost.`);
		if (n) parts.push("The machine decides what it takes; afterwards the editor reads every item back and says what happened. There is no Undo: Export SysEx… first keeps a way back.");
		w.textContent = parts.join(" ");
	}
	function chip(k, i) {
		const off = skip.has(k + ":" + i.slot);
		const label = k === "pattern" ? i.name + (i.kit != null ? " · K" + String(i.kit + 1).padStart(2, "0") : "") : (i.name || "(no name)");
		const tip = [i.format ? "format " + i.format : "", i.readable === false ? "the editor cannot read this dump (the machine may)" : "",
			i.plays ? "plays now" : "", i.overwrites ? "overwrites a slot that holds data" : "an empty slot", off ? "left out: click to take it" : "click to leave it out"].filter(Boolean).join(" · ");
		return `<button type="button" class="syxname ${i.overwrites ? "over" : ""} ${off ? "off" : ""}" data-syxitem="${esc(k + ":" + i.slot)}" title="${esc(tip)}">${i.plays ? "▶ " : ""}${esc(label)}</button>`;
	}
	function rows(m) {
		return KINDS.map(([k, label]) => {
			const list = m.items[k] || [];
			if (!list.length) return "";
			const over = list.filter(i => i.overwrites).length;
			const formats = [...new Set(list.map(i => i.format).filter(Boolean))];
			const names = list.map(i => chip(k, i)).join("");
			return `<label class="syxrow"><input type="checkbox" data-syxkind="${k}" ${OFF[k] ? "" : "checked"}><b>${label}</b><span class="syxn">${list.length}${over ? ` · ${over} overwrite` : ""}${formats.length ? ` · format ${formats.join(", ")}` : ""}${k === "global" ? " · changes MIDI channels and machine settings; the active one is made active at once" : ""}</span></label><div class="syxnames">${names}</div>`;
		}).join("");
	}
	function preview(m) {
		last = m; running = false; skip.clear();
		if (!m.ok) { pop.innerHTML = `<div class="libhead"><span class="cap">Import SysEx</span><span class="note">${esc(m.file || "")}</span><button class="libx" data-syxgo="close">Esc</button></div><p class="syxwhy">${esc(m.text)}</p>`; pop.hidden = false; return; }
		pop.innerHTML = `<div class="libhead"><span class="cap">Import SysEx</span><span class="lcdchip">${esc(m.model)}${m.fullBackup ? " · full backup" : ""}</span><span class="note">${esc(m.file)}. Into the same slots as in the file, as from a MIDI cable. Globals (MIDI channels, sync) and other messages are off unless you tick them. Click a name to leave it out.</span><button class="libx" data-syxgo="close">Esc</button></div>
 <div class="syxbody">${rows(m)}</div>
 ${m.skippedCount ? `<details class="syxprob"><summary>${m.skippedCount} message${m.skippedCount > 1 ? "s" : ""} cannot be sent and are left out</summary>${(m.skipped || []).map(p => `<div>${esc(p)}</div>`).join("")}</details>` : ""}
 <p class="syxwarn"></p>
 <div class="syxbar" hidden><i></i><span></span></div>
 <div class="syxreport" hidden></div>
 <div class="btnrow"><button class="cream" data-syxgo="start">Import</button><button data-syxgo="stop" hidden>Stop</button><button data-syxgo="close">Cancel</button></div>`;
		pop.hidden = false; sums();
	}
	function report(m) {
		const box = pop.querySelector(".syxreport"); if (!box) return;
		const r = m.report;
		box.hidden = !r;
		if (!r) return;
		const lines = (r.items || []).map(i => `<div class="syx-${esc(i.outcome.replace(" ", "-"))}">${esc(i.text)}</div>`).join("");
		box.innerHTML = `<p class="syxsum">${esc(m.text)}</p>${lines ? `<details class="syxprob" open><summary>${r.items.length} item${r.items.length > 1 ? "s" : ""} the machine did not take as in the file</summary>${lines}</details>` : ""}`;
	}
	function progress(m) {
		if (pop.hidden) return;
		running = !!m.running;
		const bar = pop.querySelector(".syxbar"); if (!bar) return;
		bar.hidden = !m.total && !m.text;
		bar.querySelector("i").style.width = (m.total ? m.done / m.total * 100 : 0) + "%";
		bar.querySelector("span").textContent = m.phase === "done" ? "Done" : m.text || (m.total ? `${m.done} of ${m.total}` : "");
		pop.querySelector('[data-syxgo="stop"]').hidden = !running;
		pop.querySelector('[data-syxgo="close"]').textContent = running ? "Hide" : m.phase === "done" ? "Done" : "Cancel";
		if (m.phase === "done") { const w = pop.querySelector(".syxwarn"); if (w) w.textContent = ""; }
		report(m);
		sums();
	}
	pop.addEventListener("change", e => { if (e.target.matches("[data-syxkind]")) sums(); });
	document.addEventListener("click", e => {
		const k = e.target.closest?.("[data-syx]");
		if (k && Syx.host) { if (k.dataset.syx === "import") Syx.host.choose(); else Syx.host.exportAll(); return; }
		const it = e.target.closest?.("[data-syxitem]");
		if (it && pop.contains(it) && !running) { const id = it.dataset.syxitem; if (skip.has(id)) skip.delete(id); else skip.add(id); it.classList.toggle("off", skip.has(id)); sums(); return; }
		const g = e.target.closest?.("[data-syxgo]"); if (!g || !pop.contains(g)) return;
		const a = g.dataset.syxgo;
		if (a === "start" && Syx.host) { running = true; Syx.host.start(chosen(), [...skip]); sums(); }
		else if (a === "stop" && Syx.host) Syx.host.stop();
		else if (a === "close") pop.hidden = true;
	});
	return {
		keys: () => `<span class="syxkeys"><button class="amkey" data-syx="import" title="Open a .syx (a backup, or dumps from any source) and choose what to send to the machine.">Import SysEx…</button><button class="amkey" data-syx="export" title="Every pattern, kit, song and the global the editor holds, as one .syx">Export SysEx…</button></span>`,
		preview, progress, exported: m => m, host: null
	};
})();
