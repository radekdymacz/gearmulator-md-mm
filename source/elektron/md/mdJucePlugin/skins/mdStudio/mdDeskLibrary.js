"use strict";
/* P4: the kit library and the pattern chooser (mockup v50): one big panel over the workspace, opened
   from the LCD's KIT field or pattern name. Everything is the machine's: 64 kits and 128 patterns from
   the desk's documents (it loads them all in the background), and every action a firmware command or
   dump (mdDesk/mdDeskLibrary.h, measured in mdP4ProbeFirmwareTest library). What the firmware has no
   command for says so in its tooltip. Loaded after mdDeskApp.js; uses its state and commands. */

const LIB = { open: null, sel: 0, renaming: null, drag: null, html: "" };
let CLIP = null;	// what the desk's library clipboard holds: {type: "kit"|"pat", from, name}
const nn = k => String(k + 1).padStart(2, "0");
const escH = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
/* Kit names: SysEx 0x55 takes 16 ASCII bytes (7-bit). Which of them the MD font draws is not checked, so
   the editor keeps to a safe set. */
const KBAD = /[^A-Z0-9 \-.\/+&!?'#*():]/g;

/* ----- the machine's facts, as tooltips ----- */
const KTIP = {
	load: "LOAD KIT (SysEx 0x58). Unsaved edits are lost; the machine keeps them in its UNDO KIT. EXTENDED: the current pattern is relinked to this kit (measured)",
	save: "SAVE KIT to the current slot (SysEx 0x59). The overwritten slot goes to the machine's UNDO KIT",
	saveas: "SAVE KIT n (SysEx 0x59): n becomes the current kit and, in EXTENDED, the current pattern links to it (measured). The old slot keeps what it had",
	copy: "Copies the kit the editor holds: a stored slot, or for the current kit its working copy read from the machine's memory, unsaved edits included",
	paste: "A kit dump (0x52) into the slot. Into the current kit it is also LOAD KIT, because a dump alone only writes the slot (measured): heard at once, unsaved edits lost",
	clear: "There is no CLEAR KIT over SysEx: the editor writes an empty kit (every track GND-EMPTY, neutral values, no name). What the machine's own CLEAR KIT leaves is not known",
	rename: "The current kit: SysEx 0x55 renames the working kit; SAVE stores it. Other slots: their dump is written back with the new name",
	reload: "LOAD KIT on the current slot (0x58): the edits are lost. The machine keeps them in its UNDO KIT",
	drag: "Drag onto another slot to copy it (a kit dump into the target)" };
const PTIP = {
	go: "LOAD PATTERN (SysEx 0x57). While playing the machine switches at the pattern end. A chain ends here",
	now: "Switch now: stopped, the same as Go. Playing: STOP, LOAD PATTERN, PLAY (measured: plays the new pattern after about 370 ms)",
	copy: "Copies the pattern: notes, locks and the kit link, like the machine's COPY PATTERN",
	paste: "A pattern dump (0x67) into the slot; pattern dumps are live. Over the current pattern with another kit link the machine loads that kit (measured): the editor asks first when the kit has unsaved edits",
	clear: "There is no CLEAR PATTERN over SysEx: the editor writes the pattern without trigs or locks, keeping its length, speed, swing, accent and kit link. The machine's own CLEAR PATTERN may reset more" };

/* ----- kits and patterns from the documents ----- */
/* the kit that plays shows as it sounds (the working kit), the others as stored */
function kitDoc(k) { return k === currentKitSlot() ? kitDocOf(Docs) : Docs.kits[k]; }
function kitLoaded(k) { return !!kitDoc(k); }
function kitEmpty(k) { const d = kitDoc(k); return !!d && !d.name && d.tracks.every(t => t.machine === "GND-EMPTY"); }
function kDisp(k) { const d = kitDoc(k); return d ? d.name : ""; }
function linked(k) { if (V.mode !== "EXTENDED") return []; const out = []; for (let p = 0; p < 128; p++) { const d = Docs.patterns[p]; if (d && d.kit === k && hasPat(p)) out.push(p); } return out; }
function patKitOf(p) { const d = Docs.patterns[p]; return d ? d.kit : null; }

function kitSlot(k) {
	const cur = k === V.kit, loaded = kitLoaded(k), empty = kitEmpty(k) && !cur, name = kDisp(k), lp = linked(k), ed = cur && V.kitState === "edited";
	const links = V.mode !== "EXTENDED" || !loaded ? "" : lp.length ? lp.slice(0, 3).map(patName).join(" ") + (lp.length > 3 ? " +" + (lp.length - 3) : "") : empty ? "" : "no pattern";
	const tip = `K${nn(k)} ${loaded ? name || "(no name)" : "(loading)"}${cur ? " · current kit, " + (ed ? "edited: not saved on the machine" : "saved") : ""}${V.mode === "EXTENDED" && loaded ? " · " + (lp.length ? "linked to " + lp.map(patName).join(" ") : "no pattern links to it") : ""}. ${KTIP.drag}`;
	const nm = LIB.renaming === k ? `<input class="lsin" id="lsin" maxlength="16" value="${escH(name)}" aria-label="Kit name, up to 16 characters" spellcheck="false" autocomplete="off">`
		: !loaded ? "…" : empty && !name ? "EMPTY" : escH(name || "EMPTY") + (V.mode === "EXTENDED" && !lp.length && !empty ? "*" : "");
	return `<button class="ls ks${empty ? " empty" : ""}${cur ? " cur" : ""}${ed ? " edited" : ""}" data-ks="${k}" draggable="${LIB.renaming === k || !loaded ? "false" : "true"}" aria-selected="${k === LIB.sel}" title="${escH(tip)}"><span class="lsh"><b>K${nn(k)}</b>${cur ? `<em><i class="led${ed ? " on" : ""}"></i>${ed ? "edited" : "saved"}</em>` : ""}</span><span class="lsn">${nm}</span><span class="lsl">${links}</span></button>`;
}
function drawKitLib() {
	const k = LIB.sel, cur = V.kit, ed = V.kitState === "edited", empty = kitEmpty(k) && k !== cur, dis = c => c ? " disabled" : "", loaded = kitLoaded(k);
	const n = Object.keys(Docs.kits).length;
	return `<div class="libhead"><span class="cap">Kit library</span><span class="lcdchip">K${nn(cur)} ${escH(kDisp(cur) || "EMPTY")} · ${ed ? "edited" : "saved"}</span><span class="note">${V.mode === "EXTENDED" ? "EXTENDED: every pattern recalls its kit." : "CLASSIC: patterns do not recall kits."} 64 slots${n < 64 ? ` · reading ${n} of 64 from the machine` : ""}.</span><button class="libx" data-la="close" title="Close (Esc)">Esc</button></div>
 <div class="libacts"><div class="grp"><span class="ilab">Current K${nn(cur)}</span><button data-la="save" title="${KTIP.save}">Save</button><button class="danger" data-la="reload"${dis(!ed)} title="${KTIP.reload}">Reload</button></div>
  <div class="grp"><span class="ilab">Slot K${nn(k)}</span><button data-la="load"${dis((k === cur && !ed) || !loaded)} title="${k === cur ? "Already the current kit. Enter reloads it when it is edited. " : ""}${KTIP.load} (Enter)">Load</button><button data-la="saveas"${dis(k === cur)} title="${k === cur ? "This is the current slot: use Save. " : ""}${KTIP.saveas}">Save as K${nn(k)}</button>
  <button data-la="copy"${dis(!loaded)} title="${KTIP.copy} (Cmd+C)">Copy</button><button data-la="paste"${dis(CLIP?.type !== "kit" || !loaded)} title="${CLIP?.type === "kit" ? "Paste K" + nn(CLIP.from) + " " + escH(CLIP.name) + ". " : "Copy a kit first. "}${KTIP.paste} (Cmd+V)">Paste</button>
  <button class="danger" data-la="clear"${dis(empty || !loaded)} title="${KTIP.clear} (Delete)">Clear</button><button data-la="rename"${dis(!loaded)} title="${KTIP.rename} (F2 or double-click)">Rename</button></div></div>
 <div class="libgrid kits" aria-label="64 kit slots">${Array.from({ length: 64 }, (_, i) => kitSlot(i)).join("")}</div>
 <div class="libfoot"><span>Arrows move · Enter loads · F2 or double-click renames · Delete clears · drag a slot onto another to copy · Cmd+Z undoes a paste, clear or rename · Esc closes</span><span class="fw" title="The machine keeps one UNDO KIT as the first entry of its kit list: the kit lost to the last load or overwrite. The editor's own undo works on top of it for slot writes; LOAD and SAVE are the machine's.">The machine also keeps an UNDO KIT</span></div>`;
}
function patSlot(p) {
	const cur = p === V.pat, q = p === V.queued && !cur, has = hasPat(p), ext = V.mode === "EXTENDED", k = patKitOf(p), loaded = !!Docs.patterns[p];
	const tip = `${patName(p)} · ${!loaded ? "loading" : has ? patLen(p) + " steps" + (ext && k != null ? " · kit K" + nn(k) : "") : "empty"}${cur ? " · current" : ""}${q ? " · queued: starts at the pattern end" : ""}. Click queues, Shift+click switches now. Drag onto another slot to copy.`;
	return `<button class="ls ps${has ? "" : " empty"}${cur ? " cur" : ""}${q ? " q" : ""}" data-ps="${p}" draggable="${has}" aria-selected="${p === LIB.sel}" title="${escH(tip)}"><b>${patName(p)}</b><span>${!loaded ? "…" : has ? patLen(p) + (ext && k != null ? " · K" + nn(k) : "") : "EMPTY"}</span></button>`;
}
function drawPatLib() {
	const p = LIB.sel, cur = V.pat, dis = c => c ? " disabled" : "", ext = V.mode === "EXTENDED", loaded = !!Docs.patterns[p];
	const rows = [..."ABCDEFGH"].map((b, i) => `<button class="bank lbank${i === cur >> 4 ? " on" : ""}" data-lb="${i}" title="Bank ${b} (key ${b})"><i class="led"></i>${b}</button>` + Array.from({ length: 16 }, (_, j) => patSlot(i * 16 + j)).join("")).join("");
	const ck = patKitOf(cur);
	return `<div class="libhead"><span class="cap">Patterns</span><span class="lcdchip">${patName(cur)} · ${V.len} steps${ext && ck != null ? " · K" + nn(ck) : ""}${V.queued != null && V.queued !== cur ? " → " + patName(V.queued) : ""}</span><span class="note">8 banks × 16. ${V.playing ? "Playing: a new pattern starts at the pattern end." : "Stopped: a click switches at once."}</span><button class="libx" data-la="close" title="Close (Esc)">Esc</button></div>
 <div class="libacts"><div class="grp"><span class="ilab">Slot ${patName(p)}</span><button data-la="go"${dis(p === cur && V.queued == null)} title="${PTIP.go} (Enter)">${V.playing ? "Queue" : "Go"}</button><button data-la="now"${dis(p === cur && V.queued == null)} title="${PTIP.now}">Now</button>
  <button data-la="copy"${dis(!loaded)} title="${PTIP.copy} (Cmd+C)">Copy</button><button data-la="paste"${dis(CLIP?.type !== "pat" || !loaded)} title="${CLIP?.type === "pat" ? "Paste " + patName(CLIP.from) + ". " : "Copy a pattern first. "}${PTIP.paste} (Cmd+V)">Paste</button><button class="danger" data-la="clear"${dis(!hasPat(p))} title="${PTIP.clear} (Delete)">Clear</button></div></div>
 <div class="libgrid pats" aria-label="128 patterns">${rows}</div>
 <div class="libfoot"><span>Arrows move · A–H jump to a bank · Enter queues · Shift+Enter switches now · Delete clears · drag a slot onto another to copy · Cmd+Z undoes a paste or clear · Esc closes</span><span class="fw" title="${PTIP.go}">Queued = blinking</span></div>`;
}
/* The library is redrawn when what it shows changed: its markup is the value compared. */
function drawLib(focus) {
	if (!LIB.open || LIB.drag) return; const html = LIB.open === "kit" ? drawKitLib() : drawPatLib(), pop = $("#libpop"), had = focus || pop.contains(document.activeElement);
	if (html === LIB.html && pop.firstChild && pop.dataset.kind === LIB.open) { if (focus && !pop.contains(document.activeElement)) libFocus(); return; }
	LIB.html = html;
	pop.innerHTML = html; pop.dataset.kind = LIB.open; pop.setAttribute("aria-label", LIB.open === "kit" ? "Kit library" : "Pattern chooser");
	const inp = $("#lsin"); if (inp) { inp.focus({ preventScroll: true }); inp.select(); return; } if (had) libFocus();
}
function libFocus() {
	const pop = $("#libpop"), el = pop.querySelector(".ls[aria-selected=true]"); if (!el) return; el.focus({ preventScroll: true });
	if (pop.scrollHeight > pop.clientHeight) { const a = el.getBoundingClientRect(), b = pop.getBoundingClientRect(); if (a.top < b.top) pop.scrollTop -= b.top - a.top + 8; else if (a.bottom > b.bottom) pop.scrollTop += a.bottom - b.bottom + 8; }
}
function placeLib() { const pop = $("#libpop"), r = $(".lcdpanel").getBoundingClientRect(), top = Math.max(16, r.bottom + 8); pop.style.top = (top + scrollY) + "px"; pop.style.maxHeight = Math.max(240, innerHeight - top - 12) + "px"; pop.style.left = Math.max(16, (document.documentElement.clientWidth - pop.offsetWidth) / 2 + scrollX) + "px"; }
function openLib(kind) { closePicker(); closeK(); if (LIB.open) closeLib(false); LIB.open = kind; LIB.sel = kind === "kit" ? V.kit : (V.queued ?? V.pat); LIB.renaming = null; $("#libpop").hidden = false; drawLib(); placeLib(); libFocus(); $(kind === "kit" ? "#kitf" : "#pat").setAttribute("aria-expanded", "true"); }
function closeLib(back) { if (!LIB.open) return; const k = LIB.open; LIB.open = null; LIB.html = ""; LIB.renaming = null; LIB.drag = null; $("#libpop").hidden = true; const t = $(k === "kit" ? "#kitf" : "#pat"); t.setAttribute("aria-expanded", "false"); if (back) t.focus(); }
function toggleLib(kind) { LIB.open === kind ? closeLib(false) : openLib(kind); }
addEventListener("resize", () => { if (LIB.open) placeLib(); });

/* ----- kit actions: machine commands (load, save) and slot writes (the rest, undoable) ----- */
const edits = () => V.kitState === "edited";
function kitLoad(k) {
	if (k === V.kit) { if (edits()) kitReload(); else toast(kitName(k) + " is already the current kit."); return; }
	cmd("kitLoad", { k });	/* the desk asks first when the kit that plays has unsaved edits */
}
function kitSaveAs(k) {
	if (k === V.kit) { saveKit(); return; }
	const go = () => cmd("kitSaveAs", { k });
	if (!kitEmpty(k)) { ask(`Overwrite <b>${kitName(k)}</b> with the current kit <b>${kitName(V.kit)}</b>? The machine keeps the overwritten kit in its UNDO KIT. K${nn(k)} becomes the current kit.`, [["Overwrite", "danger", go], ["Cancel", "", () => drawLib(true)]]); return; }
	go();
}
function kitCopy(k) { cmd("kitCopy", { k }); CLIP = { type: "kit", from: k, name: kDisp(k) }; drawLib(true); }
function kitPut(k, from, verb) {
	if (from === k) { toast("That is the same slot."); return; }
	const send = () => cmd(verb === "Paste" ? "kitPaste" : "kitCopyTo", verb === "Paste" ? { k, force: true } : { from, to: k, force: true });
	const src = verb === "Paste" ? CLIP.name : kDisp(from), busy = !kitEmpty(k) || k === V.kit;
	if (busy) { ask(`${verb} <b>K${nn(from)} ${escH(src || "EMPTY")}</b> over <b>${kitName(k)}</b>?${k === V.kit ? " It is the current kit, so it is loaded too" + (edits() ? ": its unsaved edits are lost." : ".") : ""}`, [["Overwrite", "danger", send], ["Cancel", "", () => drawLib(true)]]); return; }
	send();
}
function kitPaste(k) { if (CLIP?.type !== "kit") { toast("Copy a kit first."); return; } kitPut(k, CLIP.from, "Paste"); }
function kitClear(k) {
	if (kitEmpty(k) && k !== V.kit) { toast(kitName(k) + " is already empty."); return; }
	ask(`Clear <b>${kitName(k)}</b>? Every track becomes GND-EMPTY.${k === V.kit ? " It is the current kit: it is loaded" + (edits() ? " and the unsaved edits are lost." : ".") : ""}${linked(k).length ? " " + linked(k).length + " pattern(s) link to it." : ""}`,
		[["Clear kit", "danger", () => cmd("kitClear", { k, force: true })], ["Cancel", "", () => drawLib(true)]]);
}
function kitReload() {
	if (!edits()) { toast(kitName(V.kit) + " matches its saved slot. Nothing to reload."); return; }
	ask(`Reload <b>${kitName(V.kit)}</b> from the machine? Your edits are lost.`, [["Reload (discard edits)", "danger", () => cmd("reloadKit")], ["Cancel", "", () => drawLib(true)]]);
}
function startRename(k) { if (LIB.open !== "kit" || !kitLoaded(k)) return; LIB.sel = k; LIB.renaming = k; drawLib(true); }
function finishRename(ok) {
	const k = LIB.renaming; if (k == null) return;
	const v = ($("#lsin")?.value || "").toUpperCase().replace(KBAD, "").slice(0, 16).trimEnd(); LIB.renaming = null;
	if (ok && v !== kDisp(k)) cmd("kitRename", { k, name: v });
	drawLib(true);
}
/* ----- pattern actions ----- */
function patCopy(p) { cmd("patCopy", { p }); CLIP = { type: "pat", from: p }; drawLib(true); }
function patPut(p, from, verb) {
	if (from === p) { toast("That is the same slot."); return; }
	const send = () => cmd(verb === "Paste" ? "patPaste" : "patCopyTo", verb === "Paste" ? { p } : { from, to: p });
	if (hasPat(p)) { ask(`${verb} <b>${patName(from)}</b> over <b>${patName(p)}</b>? Its notes and locks are replaced.`, [["Overwrite", "danger", send], ["Cancel", "", () => drawLib(true)]]); return; }
	send();
}
function patPaste(p) { if (CLIP?.type !== "pat") { toast("Copy a pattern first."); return; } patPut(p, CLIP.from, "Paste"); }
function patClear(p) {
	if (!hasPat(p)) { toast(patName(p) + " is already empty."); return; }
	ask(`Clear <b>${patName(p)}</b>? Its notes and locks are removed.`, [["Clear pattern", "danger", () => cmd("patClear", { p })], ["Cancel", "", () => drawLib(true)]]);
}
function patGo(p, now) { LIB.sel = p; if (p === V.pat && V.queued == null) { drawLib(true); return; } if (now) cmd("select", { p, now: true }); else goPattern(p); drawLib(true); }
function libAct(a) {
	const k = LIB.sel, kit = LIB.open === "kit";
	({ close: () => closeLib(true), load: () => kitLoad(k), save: () => saveKit(), saveas: () => kitSaveAs(k), reload: kitReload, rename: () => startRename(k),
		copy: () => kit ? kitCopy(k) : patCopy(k), paste: () => kit ? kitPaste(k) : patPaste(k), clear: () => kit ? kitClear(k) : patClear(k), go: () => patGo(k, false), now: () => patGo(k, true) })[a]?.();
}

/* The desk asks before a load or write would lose unsaved kit edits on the machine. */
Bridge.onMessage(m => {
	if (m.type === "ask" && (m.ask === "loadKit" || m.ask === "overwriteKit" || m.ask === "relinkKit")) {
		const c = Object.assign({}, m.command, { force: true }); delete c.id;
		const what = m.ask === "loadKit" ? `Load <b>${kitName(c.k)}</b>?` : m.ask === "overwriteKit" ? `Write over <b>${kitName(m.kit)}</b>, the kit that plays?` : `This pattern links another kit, so the machine loads it.`;
		ask(`${what} Your edits to <b>${kitName(m.kit)}</b> are not saved on the machine and will be lost.`,
			[["Save kit, then go on", "cream", () => { saveKit(); cmd(c.op, c); }], ["Go on and lose edits", "danger", () => cmd(c.op, c)], ["Cancel", "", () => drawLib(true)]]);
	}
	if (LIB.open && (m.type === "doc" || m.type === "machine")) setTimeout(() => drawLib(), 30);
});

/* ----- mouse ----- */
document.addEventListener("click", e => {
	if (e.target.closest?.("#kitf")) { e.stopImmediatePropagation(); toggleLib("kit"); return; }
	if (e.target.closest?.("#pat")) { e.stopImmediatePropagation(); toggleLib("pat"); return; }
	if (!LIB.open) return; if ($("#libpop").contains(e.target) || e.target.closest?.("#dlg,#undo,#redo")) return; closeLib(false);
}, true);
document.addEventListener("click", e => {
	if (!LIB.open || !$("#libpop").contains(e.target) || e.target.closest?.("#lsin")) return;
	const a = e.target.closest("[data-la]"); if (a) { if (!a.disabled) libAct(a.dataset.la); return; }
	const ks = e.target.closest("[data-ks]"); if (ks) { const k = +ks.dataset.ks; if (LIB.renaming != null) finishRename(true); LIB.sel = k; drawLib(true); return; }
	const ps = e.target.closest("[data-ps]"); if (ps) { patGo(+ps.dataset.ps, e.shiftKey); return; }
	const lb = e.target.closest("[data-lb]"); if (lb) { LIB.sel = +lb.dataset.lb * 16 + (LIB.sel & 15); drawLib(true); }
});
document.addEventListener("dblclick", e => { const ks = e.target.closest("#libpop [data-ks]"); if (ks && !e.target.closest("#lsin")) startRename(+ks.dataset.ks); });
document.addEventListener("input", e => { if (e.target.id !== "lsin") return; const i = e.target, c = i.selectionStart, v = i.value.toUpperCase().replace(KBAD, "").slice(0, 16); if (v !== i.value) { i.value = v; i.setSelectionRange(Math.min(c, v.length), Math.min(c, v.length)); } });
document.addEventListener("focusout", e => { if (e.target.id === "lsin" && LIB.renaming != null) setTimeout(() => { if (document.activeElement?.id !== "lsin") finishRename(true); }, 0); });
document.addEventListener("dragstart", e => { const s = e.target.closest?.("#libpop .ls[data-ks],#libpop .ls[data-ps]"); if (!s) return; LIB.drag = { kit: "ks" in s.dataset, v: +(s.dataset.ks ?? s.dataset.ps) }; s.classList.add("dragging"); e.dataTransfer.effectAllowed = "copy"; try { e.dataTransfer.setData("text/plain", String(LIB.drag.v)); } catch (_) { } });
const libTarget = el => el.closest?.(LIB.drag?.kit ? "#libpop [data-ks]" : "#libpop [data-ps]");
document.addEventListener("dragover", e => { if (!LIB.drag) return; const t = libTarget(e.target); $$("#libpop .ls.over").forEach(x => x !== t && x.classList.remove("over")); if (!t) return; e.preventDefault(); e.dataTransfer.dropEffect = "copy"; t.classList.add("over"); });
document.addEventListener("drop", e => {
	if (!LIB.drag) return; const t = libTarget(e.target), d = LIB.drag; LIB.drag = null; if (!t) return; e.preventDefault(); const to = +(t.dataset.ks ?? t.dataset.ps); LIB.sel = to;
	if (d.kit) kitPut(to, d.v, "Copy"); else patPut(to, d.v, "Copy"); drawLib(true);
});
document.addEventListener("dragend", () => { if (!LIB.drag && !$$("#libpop .dragging").length) return; LIB.drag = null; drawLib(true); });

/* ----- keyboard (capture, so the panel owns its keys while it is open) ----- */
[["Enter / Space on KIT or the pattern", "", "Open the kit library / pattern chooser"], ["Arrows", "", "Move"], ["Enter", "", "Kits: load. Patterns: queue"],
 ["Enter", "shift", "Patterns: switch now"], ["A–H", "", "Patterns: jump to a bank"], ["F2", "", "Kits: rename (or double-click)"], ["Delete", "", "Clear the slot"],
 ["C / V", "cmd", "Copy / paste the slot"], ["Z", "cmd", "Undo a paste, clear or rename"], ["Escape", "", "Close"]]
	.forEach(([k, m, d]) => Keys.bind({ keys: [k], mod: m, group: "Kit library, pattern chooser", does: d }));
document.addEventListener("keydown", e => {
	if (!LIB.open) { if ((e.key === "Enter" || e.key === " ") && (e.target.id === "kitf" || e.target.id === "pat")) { e.preventDefault(); e.stopImmediatePropagation(); openLib(e.target.id === "kitf" ? "kit" : "pat"); } return; }
	if (!$("#dlg").hidden) return;
	if (e.target.id === "lsin") { if (e.key === "Enter" || e.key === "Escape") { e.preventDefault(); finishRename(e.key === "Enter"); } e.stopImmediatePropagation(); return; }
	const mod = e.metaKey || e.ctrlKey, kit = LIB.open === "kit", n = kit ? 64 : 128, cols = kit ? 8 : 16, key = e.key; let h = true;
	if (key === "Escape") closeLib(true);
	else if (!mod && !e.altKey && /^Arrow/.test(key)) { LIB.sel = (LIB.sel + { ArrowLeft: -1, ArrowRight: 1, ArrowUp: -cols, ArrowDown: cols }[key] + n) % n; drawLib(true); }
	else if (key === "Enter") { if (kit) kitLoad(LIB.sel); else patGo(LIB.sel, e.shiftKey || mod); }
	else if (key === "F2" && kit) startRename(LIB.sel);
	else if (key === "Delete" || key === "Backspace") (kit ? kitClear : patClear)(LIB.sel);
	else if (mod && (key === "c" || key === "C")) (kit ? kitCopy : patCopy)(LIB.sel);
	else if (mod && (key === "v" || key === "V")) (kit ? kitPaste : patPaste)(LIB.sel);
	else if (mod && (key === "z" || key === "Z")) { cmd(e.shiftKey ? "redo" : "undo"); }
	else if (!kit && !mod && !e.altKey && /^[a-h]$/i.test(key)) { LIB.sel = "abcdefgh".indexOf(key.toLowerCase()) * 16 + (LIB.sel & 15); drawLib(true); }
	else h = false;
	if (h) { e.preventDefault(); e.stopImmediatePropagation(); }
}, true);
