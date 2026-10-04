"use strict";
/* The firmware and the plug-in's own words: the firmware screen and LOAD ROM, the plug-in's notices (its
   modal, one at a time), the start-up card's and SysEx import's hosts, and the page's dialog (ask, in
   mdDeskApp.js) keys. */

/* ===== First run: firmware needed ===== */
/* The firmware screen. Opened by itself while no MD OS 1.63 runs (it cannot be closed then), or
   from LOAD ROM in the engine menu (then it has a Close key while the firmware runs). */
function firstRun(manual) {
	const mode = manual && V.lifecycle !== "missing" ? "manual" : "1";
	Dlg.show({ key: "first:" + mode, draw: () => drawFirstRun(mode) });
}
function drawFirstRun(mode) {
	const d = $("#dlg"), m = machineState().desk || {};
	d.innerHTML = `<div class="dlgbox first" role="dialog" aria-modal="true" aria-label="Firmware needed">
 <div class="lcdbig">MACHINEDRUM FIRMWARE NEEDED</div>
 <p>Machinedrum Editor runs the real Machinedrum operating system. Elektron's firmware cannot be shipped with the app, so you add the one from your own machine.</p>
 <ol><li>Dump the <b>OS 1.63</b> flash image from your Machinedrum (8 MiB, <span class="mono">.bin</span>).</li><li>Put it in the ROM folder${m.romFolder ? `: <span class="mono">${m.romFolder}</span>` : ""}.</li><li>Press <b>Check again</b>. Machinedrum Editor checks its size and version and keeps it on this computer only.</li></ol>
 <div class="btnrow"><button class="cream" data-choose-rom="1">Choose ROM file…</button><button data-romfolder="1">Show the ROM folder</button><button data-recheck="1">Check again</button><span class="note">UW, MKII and MKI units all use the same OS 1.63 image.${V.midi ? " OS 1.63 runs now. A new ROM is used after you reopen the plug-in." : ""}</span></div></div>`;
	if (mode === "manual") d.querySelector(".btnrow").insertAdjacentHTML("beforeend", `<button data-firstclose="1">Close</button>`);
	d.hidden = false; d.dataset.first = mode;
}

/* LOAD ROM with a firmware installed: which one, and REPLACE or REMOVE it (the ROM folder is the editor's:
   nothing outside it is touched). Without one, the start-up card's own words. */
function romMenu() { if (V.lifecycle === "missing") { firstRun(true); return; } cmd("romInfo"); }
function showRomInfo(m) {
	if (!m.installed) { firstRun(true); return; }
	Boot.showInstalled({ machine: "Machinedrum", os: m.os, name: m.name, size: m.size, inFolder: m.inFolder, folder: m.folder });
}
function askRemoveRom(m) {
	if (!m.inFolder) { toast("This image is outside the editor's ROM folder (" + m.folder + "); remove it there yourself."); return; }
	ask(`Remove <b>${escH(m.os)}</b> from the ROM folder? The machine stops and the editor asks for a firmware again. Your project stays.`,
		[["Remove", "danger", () => cmd("removeRom")], ["Cancel", "", () => {}]]);
}
/* what the plug-in has to say to the user (a question, a warning): its modal, never a native alert. The plug-in
   waits for the answer, so a notice goes before the page's own questions, is never replaced by one, and is
   always answered: closed any other way, by its last key (Dlg, skins/shared/deskModal.js). */
function showNotice(m) {
	const names = m.buttons && m.buttons.length ? m.buttons : ["OK"], item = { notice: true };
	const answer = i => { if (item.done) return; item.done = true; cmd("noticeAnswer", { id: m.id, button: i }); };
	item.cancel = () => answer(names.length - 1);
	ask(`<b>${escH(m.title)}</b><br>${escH(m.text).replace(/\n/g, "<br>")}`,
		names.map((t, i) => [escH(t), i === 0 && names.length > 1 ? "cream" : "", () => answer(i)]), item);
}
Bridge.onMessage(m => { if (m.type === "romInfo") showRomInfo(m); else if (m.type === "notice") showNotice(m); });

/* the start-up card's keys: the host's native file chooser, the ROM folder, a new look (the ROM stays on this computer) */
Boot.host = { chooseRom: () => cmd("chooseRom"), revealRom: () => cmd("revealRomFolder"), recheck: () => cmd("recheckFirmware"),
	removeRom: askRemoveRom, say: toast };
Bridge.onMessage(m => { if (m.type === "romInstall") { Boot.rom(m); toast(m.text); } });
/* SysEx import and export: the host's file dialogs and document writes (the page never reads the file) */
Syx.host = { choose: () => cmd("chooseSyx"), exportAll: () => cmd("syxExport"), start: kinds => cmd("syxImport", { kinds }), stop: () => cmd("syxCancel") };
Bridge.onMessage(m => {
	if (m.type === "syxPreview") Syx.preview(m);
	else if (m.type === "syxProgress") Syx.progress(m);
	else if (m.type === "syxExport") toast(m.text);
});

/* the dialog's and the firmware screen's keys (the router's, mdDeskRender.js CLICKS): true when the click was theirs */
function clickDialogs(e) {
	const dl = e.target.closest("[data-dlg]"); if (dl) { const d = $("#dlg"), f = d._btns[+dl.dataset.dlg][2]; d.hidden = true; f(); return true; }
	if (e.target.closest("[data-romfolder]")) { cmd("revealRomFolder"); return true; }
	if (e.target.closest("[data-choose-rom]")) { cmd("chooseRom"); return true; }
	if (e.target.closest("[data-recheck]")) { cmd("recheckFirmware"); return true; }
	if (e.target.closest("[data-firstclose]")) { const d = $("#dlg"); d.hidden = true; d.dataset.first = ""; return true; }
	if ((e.target.closest("[data-dlgclose]") || e.target.id === "dlg") && $("#dlg").dataset.first !== "1") { $("#dlg").hidden = true; $("#dlg").dataset.first = ""; return true; }
	return false;
}
