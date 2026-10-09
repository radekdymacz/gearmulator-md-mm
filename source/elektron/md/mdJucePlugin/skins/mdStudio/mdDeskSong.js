"use strict";
/* Song: the palette (ARRANGE adds to the song, CHAIN numbers the machine's own chain), the selected row's
   inspector and the arrangement grid. Dragging pads and rows onto the grid is mdDeskGestures.js's. */

/* ===== Song ===== */
function patLen(p) { return lengthOfPattern(p); }
function hasPat(p) { const d = Docs.patterns[p]; return !!d && d.tracks.some(t => t.trigs.length); }
const rowLen = r => r.len ?? (patLen(r.pat) - (r.ofs || 0));
function songSteps() { let n = 0; V.song.forEach(r => { if (!r.type) n += rowLen(r) * r.rep; }); V.song.forEach((r, i) => { if (r.type === "loop" && r.count !== Infinity) { let seg = 0; for (let k = r.to; k < i; k++) { const q = V.song[k]; if (!q.type) seg += rowLen(q) * q.rep; } n += seg * (r.count - 1); } }); return n; }
function songTime() { const st = songSteps(), sec = st * 60 / V.bpm / 4; return `${Math.floor(sec / 60)}:${String(Math.round(sec % 60)).padStart(2, "0")}`; }
function loopOf(i) { return V.song.findIndex((r, k) => r.type === "loop" && k > i && r.to <= i); }
function songCmd(op, args, optimistic) { cmd(op, Object.assign({ s: V.songSlot }, args), undefined, optimistic); }
/* row i becomes r (a view row): sent, and shown at once */
function rowSet(i, r) { songCmd("rowSet", { i, row: rowToContract(r, patLen) }, [[["song", i], r]]); }
/* One palette, two ways to play its pads (S.songPick): ARRANGE adds the pad after the selected row of the
   song (stored, any bank, loops and jumps; heard after STOP + reload), CHAIN numbers it into the machine's
   own chain (live, one bank, loops; chainFooter, the Plays line). The header says which one the machine
   plays (playsOf: CHAIN, SONG or PATTERN). */
S.songPick = "arrange"; S.songMore = false; S.chainDraft = []; S.chainTimer = 0; S.chainSent = false;
function chainDoc() { const d = machineState().desk || {}; return d.chain || null; }
/* Every pad (and BACK) chains at once: the pads are sent as the machine's chain 150 ms after the last
   click (the latest wins; the desk also holds a chain back while the keys of the one before are on
   their way, MdMachine::cmdChain). Fewer than two pads: the chain the machine plays ends. */
function chainSoon() {
	clearTimeout(S.chainTimer);
	S.chainTimer = setTimeout(() => {
		const d = S.chainDraft.slice(), c = chainDoc();
		if (!canDo(V, "chains")) return;
		if (d.length >= 2) { if (!(c && c.active && c.patterns.length === d.length && c.patterns.every((p, i) => p === d[i]))) { cmd("chain", { patterns: d }); S.chainSent = true; } }
		else if ((c && c.active) || S.chainSent) { cmd("chainClear"); S.chainSent = false; }
	}, 150);
}
function chainFooter() {
	const c = chainDoc(), bn = "ABCDEFGH"[S.bank], d = S.chainDraft;
	const known = !!c, active = known && c.active && c.patterns.length > 0;
	const playing = V.pat, list = active ? c.patterns : [], at = list.indexOf(playing), next = active ? list[(at + 1) % list.length] : null;
	/* what the engine can do (machine.capabilities.chains, with its reason) */
	const can = canDo(V, "chains"), why = V.caps.reasons.chains || "";
	/* what plays is the What plays card's (whatPlays); here: the pads' own state and the gestures */
	const state = !can ? why : !known ? "The chain is not readable on this firmware." : active ? `The machine plays the chain${at >= 0 && V.playing ? `: now ${patName(playing)}, next ${patName(next)}` : ""}.` : "";
	return chainRows(d, bn, active, state);
}
/* the PATTERN | SONG switch, as the machine reports it (lit: machine.songMode); big in the What plays card */
function seqModeKeys() {
	const sm = V.songMode;
	return `<span class="seg seqmode" title="PATTERN: the machine plays the pattern (and its chain). SONG: it plays the song. What is lit is what the machine reports">${[["pattern", "PATTERN", false], ["song", "SONG", true]].map(([k, l, v]) => `<button data-seqmode="${k}" aria-pressed="${sm === v}"><span>${l}</span></button>`).join("")}</span>`;
}
function chainRows(d, bn, active, state) {
	return `<div class="chainfoot">${state ? `<div class="irow"><span class="ilab"></span><span class="note">${state}</span></div>` : ""}
  <div class="irow"><span class="ilab"></span><span class="chainacts"><button data-chain="undo"${d.length ? "" : " disabled"} title="Takes the last pad out and chains the rest at once">Back</button><button class="danger" data-chain="clear"${active || d.length ? "" : " disabled"} title="LOAD PATTERN of the current pattern: the machine's way to end a chain. The pads start over">Clear</button></span>
  <span class="note">${d.length === 1 ? `One more pad and the machine plays the chain (BANK ${bn} held, the TRIG keys in order; ${V.playing ? "from the pattern end" : "PLAY starts at the first"}).` : "Each pad chains at once: the machine plays them in order and loops."} One bank, each pattern once. Picking a pattern ends the chain; editing its patterns does not.</span></div></div>`;
}
document.addEventListener("click", e => { const b = e.target.closest?.("[data-seqmode]"); if (b) cmd("seqMode", { song: b.dataset.seqmode === "song" }); });
function renderSong() {
	/* another song (the song card, the LCD, the machine): the selection stays inside it */
	S.songSel = Math.max(0, Math.min(S.songSel, V.song.length - 1));
	const sel = V.song[S.songSel] || V.song[0], chain = S.songPick === "chain", plays = playsOf(Docs);
	/* a chain is one bank's: another bank starts the draft over */
	S.chainDraft = S.chainDraft.filter(p => p >> 4 === S.bank);
	const pad = p => {
		const info = `${patName(p)}<small>${Docs.patterns[p] ? (hasPat(p) ? patLen(p) : "empty") : "…"}</small>`;
		if (!chain) return `<button class="padd ${hasPat(p) ? "has" : ""} ${!sel.type && sel.pat === p ? "cur" : ""}" data-addpat="${p}" draggable="true" title="Drag into the arrangement. Click adds after the selected row.">${info}</button>`;
		const n = S.chainDraft.indexOf(p);
		return `<button class="padd ${hasPat(p) ? "has" : ""}${n >= 0 ? " in" : ""}" data-chainpad="${p}" title="${patName(p)}${n >= 0 ? ": number " + (n + 1) + " in the chain. Click takes it out, and the machine plays the rest." : ". Click adds it: the machine plays the chain at once."}">${info}${n >= 0 ? `<em>${n + 1}</em>` : ""}</button>`;
	};
	const palette = `<div class="banks">${[..."ABCDEFGH"].map((b, k) => `<button class="bank ${k === S.bank ? "on" : ""}" data-bank="${k}"><i class="led"></i>${b}</button>`).join("")}</div>
  <div class="pgridp">${Array.from({ length: 16 }, (_, k) => pad(S.bank * 16 + k)).join("")}</div>
  ${chain ? chainFooter() : `<p class="note pnote">Click adds after row ${String(S.songSel + 1).padStart(3, "0")} · drag onto the grid</p>`}`;
	const head = `<header class="phead"><h3>Patterns</h3><span class="seg" data-set="songpick" title="ARRANGE: a click adds the pattern to the song. CHAIN: a click numbers it into the machine's chain.">${[["arrange", "Arrange"], ["chain", "Chain"]].map(([v, t]) => `<button data-v="${v}" aria-pressed="${S.songPick === v}">${t}</button>`).join("")}</span></header>`;
	let insp = "";
	if (!sel.type) {
		const L = patLen(sel.pat), o = sel.ofs || 0, ln = rowLen(sel);
		insp = `<div class="irow"><span class="ilab">Row</span><span class="lcdchip">${String(S.songSel + 1).padStart(3, "0")} · ${patName(sel.pat)}</span>
    <span class="stepper"><button data-step="pat" data-d="-1" aria-label="Previous pattern">‹</button><button data-step="pat" data-d="1" aria-label="Next pattern">›</button></span></div>
   <div class="irow"><span class="ilab">Repeat</span><span class="stepper"><button data-step="rep" data-d="-1">−</button><b class="mono">${sel.rep}</b><button data-step="rep" data-d="1">+</button></span>
    <span class="ilab" style="margin-left:18px">Tempo</span><button class="ptog ${sel.bpm ? "" : "on"}" data-bpmkeep="1"><i class="led"></i>Keep</button>
    ${sel.bpm ? `<span class="stepper"><button data-step="bpm" data-d="-1">−</button><b class="mono">${sel.bpm}</b><button data-step="bpm" data-d="1">+</button></span>` : `<span class="note">uses the tempo before it</span>`}</div>
   <div class="irow"><span class="ilab"></span><button class="ptog morebtn ${S.songMore ? "on" : ""}" data-rowmore="1" aria-expanded="${S.songMore}" title="Play part of the pattern, or mute tracks for this row"><i class="led"></i>More<small>${[sel.ofs || sel.len ? `part ${o + 1}–${o + ln}` : "", (sel.mutes || []).length ? `${sel.mutes.length} muted` : ""].filter(Boolean).map(x => " · " + x).join("") || " · part, mutes"}</small></button></div>
   ${S.songMore ? `   <div class="irow"><span class="ilab">Part</span><div class="partbar" style="grid-template-columns:repeat(${L},1fr)">${Array.from({ length: L }, (_, k) => `<i class="${k >= o && k < o + ln ? "on" : ""} ${k % 16 === 0 && k ? "pg" : ""}"></i>`).join("")}</div></div>
   <div class="irow"><span class="ilab"></span><span class="stepper"><span class="ilab">Start</span><button data-step="ofs" data-d="-1">−</button><b class="mono">${o + 1}</b><button data-step="ofs" data-d="1">+</button></span>
    <span class="stepper"><span class="ilab">Length</span><button data-step="len" data-d="-1">−</button><b class="mono">${ln}</b><button data-step="len" data-d="1">+</button></span>
    <button class="ptog ${sel.ofs || sel.len ? "" : "on"}" data-fullpat="1"><i class="led"></i>Whole pattern</button></div>
   <div class="irow"><span class="ilab">Mutes</span><div class="mkeys">${Array.from({ length: 16 }, (_, k) => `<button class="mkey ${(sel.mutes || []).includes(k) ? "off" : ""}" data-rowmute="${k}" title="Track ${k + 1} ${V.tracks[k].m}">${k + 1}</button>`).join("")}</div></div>` : ""}`;
	}
	else if (sel.type === "end") insp = `<div class="irow"><span class="ilab">End</span><span class="note">The song stops here. Add patterns before it from the palette.</span></div>`;
	else insp = `<div class="irow"><span class="ilab">Command</span><span class="seg" data-set="loopkind">${["loop", "jump", "halt"].map(k => `<button data-v="${k}" aria-pressed="${sel.type === k}">${k.toUpperCase()}</button>`).join("")}</span></div>
   ${sel.type !== "halt" ? `<div class="irow"><span class="ilab">${sel.type === "loop" ? "Back to" : "Jump to"}</span><span class="stepper"><button data-step="to" data-d="-1">−</button><b class="mono">${String(sel.to + 1).padStart(3, "0")}</b><button data-step="to" data-d="1">+</button></span></div>` : ""}
   ${sel.type === "loop" ? `<div class="irow"><span class="ilab">Times</span><span class="stepper"><button data-step="count" data-d="-1">−</button><b class="mono">${sel.count === Infinity ? "∞" : sel.count}</b><button data-step="count" data-d="1">+</button></span><button class="ptog ${sel.count === Infinity ? "on" : ""}" data-inf="1"><i class="led"></i>Forever</button></div>` : ""}
   <div class="irow"><span class="ilab"></span><span class="note">${sel.type === "loop" ? "Loops can be nested. Forever loops are good live: pick the next row while it plays." : sel.type === "jump" ? "Jumps the song pointer to another row." : "Pauses playback until you pick a row to go on from."}</span></div>`;
	const mode = V.songMode === true ? "songmode" : V.songMode === false ? "patmode" : "";
	$("#main").innerHTML = `<div class="songui lay2 ${mode}"><div class="songleft">${whatPlays(plays)}${songCard()}<section class="card ${chain ? "chainmode" : ""}">${head}${palette}</section>
   <section class="card"><header><h3>Selected row</h3><span class="rowacts"><button data-rowact="up" title="Move left">←</button><button data-rowact="down" title="Move right">→</button><button data-rowact="dup">Duplicate</button><button data-rowact="loop">Add loop</button><button data-rowact="del" class="danger">Delete</button></span></header><div class="insp">${insp}</div></section></div>
  <section class="card arrcard"><header><h3>Arrangement</h3><span class="arrstate">${V.songMode === true ? "Playing" : V.songMode === false ? "Not playing: pattern mode" : ""}</span><span class="note">${V.song.length} of 256 rows · drag patterns onto the grid · drag cells to move · Delete removes</span></header>
    <div class="durbar" title="Song shape by time (length × repeats); in SONG mode it fills up to the row that plays">${V.song.map((r, i) => r.type ? `<i class="db dbm" data-i="${i}"></i>` : `<i class="db ${i === S.songSel ? "sel" : ""}" data-row="${i}" data-i="${i}" style="flex:${rowLen(r) * r.rep} 1 0"></i>`).join("")}</div>
    <div class="slotgrid" id="tl">${Array.from({ length: 16 }, (_, line) => `<span class="sglab">${String(line * 16 + 1).padStart(3, "0")}</span>${Array.from({ length: 16 }, (_, c) => {
		const i = line * 16 + c, r = V.song[i];
		if (!r) return `<div class="scell empty" data-i="${i}"></div>`;
		const cls = `scell ${i === S.songSel ? "sel" : ""} ${r.type ? "cmd " + r.type : ""} ${!r.type && loopOf(i) >= 0 ? "inloop" : ""}`;
		const txt = r.type === "end" ? "END" : r.type === "loop" ? `↺${String(r.to + 1).padStart(3, "0")}` : r.type === "jump" ? `→${String(r.to + 1).padStart(3, "0")}` : r.type === "halt" ? "HALT" : patName(r.pat);
		const sub = r.type === "loop" ? (r.count === Infinity ? "∞" : "×" + r.count) : !r.type ? `${r.rep > 1 ? "×" + r.rep : ""}${r.ofs || r.len ? "~" : ""}` : "";
		return `<button class="${cls}" data-row="${i}" data-i="${i}" draggable="${r.type === "end" ? "false" : "true"}" title="Row ${String(i + 1).padStart(3, "0")}${r.type ? "" : " · " + patName(r.pat) + " ×" + r.rep + " · " + rowLen(r) + " steps"}"><b>${txt}</b><small>${sub}</small></button>`;
	}).join("")}`).join("")}</div></section></div>`;
	markSongRow(true);
}
/* (a) What plays: the PATTERN | SONG switch first, then one live line from the machine (playsText, mdDeskTop.js;
   the song row is moved by markSongRow without a render) */
function whatPlays(plays) {
	const tip = { chain: "The machine plays its own chain (live, one bank, loops). Clear it in CHAIN, or pick a pattern.", song: "SONG mode: the machine plays the stored song (edits are heard after STOP + reload).", pattern: "PATTERN mode: the machine plays this pattern and stays on it." }[plays.kind];
	return `<section class="card whatplays ${V.songMode === true ? "song" : V.songMode === false ? "pattern" : ""}"><header><h3>What plays</h3></header>
   <div class="modebig">${seqModeKeys()}</div>
   <div class="chainrow playsline"><span class="lcdchip playschip ${plays.kind}" id="songPlays" title="${tip}">${playsText()}</span></div></section>`;
}
/* (b) the song itself: its slot (the machine loads it when stopped) and the reload its edits need */
function songCard() {
	const n = String(V.songSlot + 1).padStart(2, "0");
	return `<section class="card songslot"><header><h3>Song</h3><span class="stepper"><button data-songslot="-1" aria-label="Previous song" title="Previous song (the machine loads it when stopped)">‹</button><b class="lcdchip">SONG ${n}</b><button data-songslot="1" aria-label="Next song" title="Next song (the machine loads it when stopped)">›</button></span>
   ${V.songReload ? `<span class="songwarn"><span class="lcdchip warnchip">Edits heard after STOP + reload</span><button class="cream" data-reloadsong="1">Reload song</button></span>` : `<span class="note">${songRows()} rows · ${Math.round(songSteps() / 16)} bars · ${songTime()}</span>`}</header></section>`;
}
function songAction(a) {
	const i = S.songSel, r = V.song[i];
	if (a === "del") { if (r.type === "end") return; songCmd("rowDelete", { i }); S.songSel = Math.max(0, Math.min(i, V.song.length - 2)); }
	if (a === "dup" && !r.type) { songCmd("rowInsert", { i: i + 1, row: rowToContract(r, patLen) }); S.songSel = i + 1; }
	if (a === "up" && i > 0 && r.type !== "end") { songCmd("rowMove", { from: i, to: i - 1 }); S.songSel = i - 1; }
	if (a === "down" && i < V.song.length - 2 && r.type !== "end") { songCmd("rowMove", { from: i, to: i + 1 }); S.songSel = i + 1; }
	if (a === "loop") { const at = r.type === "end" ? i : i + 1; songCmd("rowInsert", { i: at, row: { kind: "loop", target: Math.max(0, at - 1), repeats: 1 } }); S.songSel = at; }
}
function songStep(k, d) {
	const r = { ...V.song[S.songSel] };
	if (k === "pat") r.pat = (r.pat + d + 128) % 128;
	if (k === "rep") r.rep = Math.max(1, Math.min(64, r.rep + d));
	if (k === "bpm") r.bpm = Math.max(30, Math.min(300, (r.bpm || Math.round(V.bpm)) + d));
	if (k === "ofs") { const L = patLen(r.pat); r.ofs = Math.max(0, Math.min(L - 1, (r.ofs || 0) + d)); if (r.len == null) r.len = L - r.ofs; if (r.ofs + r.len > L) r.len = L - r.ofs; }
	if (k === "len") { const L = patLen(r.pat); r.len = Math.max(1, Math.min(L - (r.ofs || 0), rowLen(r) + d)); }
	if (k === "to") r.to = Math.max(r.type === "jump" ? S.songSel + 1 : 0, Math.min(r.type === "loop" ? S.songSel - 1 : V.song.length - 1, r.to + d));
	/* The firmware plays a loop repeats + 1 times and 0 is infinite, so a finite loop plays at least twice. */
	if (k === "count") r.count = r.count === Infinity ? (d < 0 ? 64 : Infinity) : Math.max(2, Math.min(64, r.count + d));
	rowSet(S.songSel, r); render();
}
/* where a dragged pad or row lands (the drag: mdDeskGestures.js) */
function dropTarget(el) { const c = el?.closest?.(".scell"); if (!c) return null; const i = +c.dataset.i, endI = V.song.length - 1; return i < endI ? { i, mode: "onto" } : { i: endI, mode: "append" }; }
function showTarget(t) {
	$$(".scell.over,.scell.appendto").forEach(x => x.classList.remove("over", "appendto")); if (!t) return;
	const c = document.querySelector(`.scell[data-i="${t.mode === "append" ? V.song.length : t.i}"]`); c && c.classList.add(t.mode === "append" ? "appendto" : "over");
}

/* the Song's clicks (the router's, mdDeskRender.js CLICKS): true when the click was theirs */
function clickSong(e) {
	if (S.ws !== "song") return false;
	const bk = e.target.closest("[data-bank]"); if (bk) { S.bank = +bk.dataset.bank; render(); return true; }
	const ss = e.target.closest("[data-songslot]"); if (ss) { cmd("selectSong", { s: (V.songSlot + +ss.dataset.songslot + 32) % 32 }); return true; }
	const cp = e.target.closest("[data-chainpad]"); if (cp) { if (cp.disabled) return true; const n = +cp.dataset.chainpad, i = S.chainDraft.indexOf(n); if (i >= 0) S.chainDraft.splice(i, 1); else if (S.chainDraft.length < 16) S.chainDraft.push(n); render(); chainSoon(); return true; }
	const ca = e.target.closest("[data-chain]"); if (ca) {
		if (ca.disabled) return true;
		if (ca.dataset.chain === "undo") { S.chainDraft.pop(); render(); chainSoon(); }
		else if (ca.dataset.chain === "clear") { clearTimeout(S.chainTimer); S.chainDraft = []; render(); if (chainDoc()?.active || S.chainSent) { cmd("chainClear"); S.chainSent = false; } }
		return true;
	}
	if (e.target.closest("[data-rowmore]")) { S.songMore = !S.songMore; render(); return true; }
	const ap = e.target.closest("[data-addpat]"); if (ap) { if (V.song.length >= 256) { toast("A song holds 256 rows."); return true; } let at = S.songSel + 1; if (V.song[S.songSel]?.type === "end") at = S.songSel; songCmd("rowInsert", { i: at, row: rowToContract({ pat: +ap.dataset.addpat, rep: 1 }, patLen) }); S.songSel = at; return true; }
	const rw = e.target.closest(".scell:not(.empty),.db[data-row]"); if (rw) { S.songSel = +rw.dataset.row; render(); return true; }
	const ra = e.target.closest("[data-rowact]"); if (ra) { songAction(ra.dataset.rowact); return true; }
	const stp = e.target.closest("[data-step]"); if (stp) { songStep(stp.dataset.step, +stp.dataset.d * (e.shiftKey ? 10 : 1)); return true; }
	const mk = e.target.closest("[data-rowmute]"); if (mk) { const r = V.song[S.songSel], k = +mk.dataset.rowmute, m = r.mutes || []; rowSet(S.songSel, { ...r, mutes: m.includes(k) ? m.filter(x => x !== k) : [...m, k] }); render(); return true; }
	if (e.target.closest("[data-bpmkeep]")) { const r = V.song[S.songSel]; rowSet(S.songSel, { ...r, bpm: r.bpm ? undefined : Math.round(V.bpm) }); render(); return true; }
	if (e.target.closest("[data-fullpat]")) { const { ofs, len, ...r } = V.song[S.songSel]; rowSet(S.songSel, r); render(); return true; }
	if (e.target.closest("[data-inf]")) { const r = V.song[S.songSel]; rowSet(S.songSel, { ...r, count: r.count === Infinity ? 2 : Infinity }); render(); return true; }
	return false;
}
