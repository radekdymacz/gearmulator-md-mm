/* SYX BEGIN (P7): SysEx import and export, the same text in both editors (checked by the sync scripts).
   The host opens, parses and writes the files (the page never reads their bytes): a .syx chosen with
   "Import SysEx…" comes back as a preview, here a panel of the modal layer with
   what is in the file and what it overwrites; "Import" sends the chosen kinds as ordinary document writes
   (one undo step), a few at a time, with progress and a Stop key.
     Syx.keys()            the library's two keys (markup)
     Syx.preview(m)        {type:"syxPreview", ok, text, file, model, fullBackup, items:{kind:[{slot,name,overwrites}]}, problems}
     Syx.progress(m)       {type:"syxProgress", done, total, running, text}
     Syx.exported(m)       {type:"syxExport", ok, text}
     Syx.host = {choose(), exportAll(), start(kinds), stop()}   the app's host calls */
const Syx = (() => {
	const KINDS = [["kit", "Kits"], ["pattern", "Patterns"], ["song", "Songs"], ["global", "Globals"]];
	const pop = document.createElement("div");
	pop.id = "syxpop"; pop.className = "syxpop libpop"; pop.hidden = true;
	pop.setAttribute("role", "dialog"); pop.setAttribute("aria-label", "Import SysEx");
	document.body.appendChild(pop);
	let last = null, running = false;
	const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
	const chosen = () => [...pop.querySelectorAll("[data-syxkind]:checked")].map(b => b.dataset.syxkind);
	function sums() {
		const k = chosen(), items = last?.items || {};
		const n = k.reduce((a, x) => a + (items[x] || []).length, 0), over = k.reduce((a, x) => a + (items[x] || []).filter(i => i.overwrites).length, 0);
		const go = pop.querySelector('[data-syxgo="start"]');
		if (go) { go.textContent = n ? `Import ${n} item${n > 1 ? "s" : ""}` : "Nothing chosen"; go.disabled = !n || running; go.className = over ? "danger" : "cream"; }
		const w = pop.querySelector(".syxwarn");
		if (w) w.textContent = over ? `${over} slot${over > 1 ? "s" : ""} on the machine hold data and are overwritten, the pattern and kit that play too if they are among them. One Undo takes the whole import back.` : n ? "Only empty slots are written." : "";
	}
	function preview(m) {
		last = m; running = false;
		if (!m.ok) { pop.innerHTML = `<div class="libhead"><span class="cap">Import SysEx</span><span class="note">${esc(m.file || "")}</span><button class="libx" data-syxgo="close">Esc</button></div><p class="syxwhy">${esc(m.text)}</p>`; pop.hidden = false; return; }
		const rows = KINDS.map(([k, label]) => {
			const list = m.items[k] || [];
			if (!list.length) return "";
			const over = list.filter(i => i.overwrites).length;
			const names = list.slice(0, 64).map(i => `<span class="syxname ${i.overwrites ? "over" : ""}" title="${i.overwrites ? "overwrites a slot that holds data" : "an empty slot"}">${esc(k === "pattern" ? i.name + " · K" + String(i.kit + 1).padStart(2, "0") : (i.name || "(no name)"))}</span>`).join("") + (list.length > 64 ? `<span class="syxname">+${list.length - 64}</span>` : "");
			return `<label class="syxrow"><input type="checkbox" data-syxkind="${k}" ${k === "global" ? "" : "checked"}><b>${label}</b><span class="syxn">${list.length}${over ? ` · ${over} overwrite` : ""}</span></label><div class="syxnames">${names}</div>`;
		}).join("");
		pop.innerHTML = `<div class="libhead"><span class="cap">Import SysEx</span><span class="lcdchip">${esc(m.model)}${m.fullBackup ? " · full backup" : ""}</span><span class="note">${esc(m.file)}. Into the same slots as in the file. Globals are settings (MIDI channels, sync): off unless you tick them.</span><button class="libx" data-syxgo="close">Esc</button></div>
 <div class="syxbody">${rows}</div>
 ${m.problemCount ? `<details class="syxprob"><summary>${m.problemCount} message${m.problemCount > 1 ? "s" : ""} could not be read</summary>${m.problems.map(p => `<div>${esc(p)}</div>`).join("")}</details>` : ""}
 ${m.leftOutCount ? `<details class="syxprob"><summary>${m.leftOutCount} document${m.leftOutCount > 1 ? "s" : ""} left out: this ${esc(m.model)}'s OS does not take them as they are</summary>${(m.leftOut || []).map(p => `<div>${esc(p)}</div>`).join("")}</details>` : ""}
 <p class="syxwarn"></p>
 <div class="syxbar" hidden><i></i><span></span></div>
 <div class="btnrow"><button class="cream" data-syxgo="start">Import</button><button data-syxgo="stop" hidden>Stop</button><button data-syxgo="close">Cancel</button></div>`;
		pop.hidden = false; sums();
	}
	function progress(m) {
		if (pop.hidden) return;
		running = !!m.running;
		const bar = pop.querySelector(".syxbar"); if (!bar) return;
		bar.hidden = !m.total;
		bar.querySelector("i").style.width = (m.total ? m.done / m.total * 100 : 0) + "%";
		bar.querySelector("span").textContent = m.text || (m.total ? `${m.done} of ${m.total} sent` : "");
		pop.querySelector('[data-syxgo="stop"]').hidden = !running;
		pop.querySelector('[data-syxgo="close"]').textContent = running ? "Hide" : m.total && m.done >= m.total ? "Done" : "Cancel";
		sums();
	}
	pop.addEventListener("change", e => { if (e.target.matches("[data-syxkind]")) sums(); });
	document.addEventListener("click", e => {
		const k = e.target.closest?.("[data-syx]");
		if (k && Syx.host) { if (k.dataset.syx === "import") Syx.host.choose(); else Syx.host.exportAll(); return; }
		const g = e.target.closest?.("[data-syxgo]"); if (!g || !pop.contains(g)) return;
		const a = g.dataset.syxgo;
		if (a === "start" && Syx.host) { running = true; Syx.host.start(chosen()); sums(); }
		else if (a === "stop" && Syx.host) Syx.host.stop();
		else if (a === "close") pop.hidden = true;
	});
	return {
		keys: () => `<span class="syxkeys"><button class="amkey" data-syx="import" title="Open a .syx (a backup, or dumps from any source) and choose what to import.">Import SysEx…</button><button class="amkey" data-syx="export" title="Every pattern, kit, song and the global the editor holds, as one .syx">Export SysEx…</button></span>`,
		preview, progress, exported: m => m, host: null
	};
})();
/* SYX END */
