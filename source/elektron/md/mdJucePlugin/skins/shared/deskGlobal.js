"use strict";
/* The GLOBAL panel both editors share (B-051, F3; the Machinedrum Editor's v61 panel, P5, made shared): the machine's
   own settings, kept in its active global slot, in one panel opened by the GLOBAL key in the top bar or GLOBAL… in the
   engine menu (and the AUDIO panel's link). This file is the frame and the controls: the header with the 8 slots (the
   active one lit; a click makes that one active), Reset to defaults (asked first), the cards' controls (GlobalUi), the
   footer, placing, the GLOBAL key's light, Esc and a click outside. What a machine's global holds is its host's
   (DeskGlobal.host, set by mdDeskGlobal.js and the Monomachine mockup's 117-global.js):
     view()        the active global as the page shows it ({slot, ...}), or null while it is read
     cards(G)      the cards' HTML; their controls carry data-ga (a click) or data-gsel (a select)
     click(a, G)   a card's control was clicked (a: the element with data-ga)
     change(s, G)  a card's select changed (optional)
     slot(n)       make slot n the active one
     reset()       the active slot becomes the machine's factory global
     ask(html, yes)  the editor's question dialog (yes runs on the confirm key)
     menu          where the settings are on the machine, for the header ("FUNCTION + PATTERN/SONG") */
var GP = { open: false, note: 64 };
const DeskGlobal = { host: null };
const GlobalUi = {
	tog: (f, on, tip, a = "ON", b = "OFF") => `<span class="seg" title="${tip}"><button data-ga="${f}" data-v="1" aria-pressed="${on}">${a}</button><button data-ga="${f}" data-v="0" aria-pressed="${!on}">${b}</button></span>`,
	step: (f, label, tip) => `<span class="stepper" title="${tip}"><button data-ga="${f}" data-d="-1" aria-label="Less">−</button><b class="mono">${label}</b><button data-ga="${f}" data-d="1" aria-label="More">+</button></span>`,
	row: (label, control, extra = "") => `<div class="grow2"><span class="ilab">${label}</span>${control}${extra}</div>`
};
const GRESET_TIP = "The active global slot back to how the machine shipped: MIDI channels, sync and the rest (asked first)";
function drawGlobal() {
	const pop = document.getElementById("globpop"), H = DeskGlobal.host;
	if (!pop) return;
	if (!GP.open || !H) { pop.hidden = true; return; }
	const G = H.view();
	if (!G) { pop.innerHTML = `<div class="libhead"><span class="cap">Global</span><span class="note">Reading the global settings from the machine…</span><button class="libx" data-ga="close">Esc</button></div>`; pop.hidden = false; placeGlobal(); return; }
	pop.innerHTML = `<div class="libhead"><span class="cap">Global</span><span class="lcdchip">GLOBAL ${G.slot + 1}</span><span class="gslots" title="8 global setups; the lit one is active (SysEx 0x56)">${Array.from({ length: 8 }, (_, k) => `<button data-ga="slot" data-v="${k}" aria-pressed="${k === G.slot}">${k + 1}</button>`).join("")}</span><span class="note">${H.menu} on the machine. A change is stored and made active at once.</span><button class="amkey greset" data-ga="reset" title="${GRESET_TIP}">Reset to defaults</button><button class="libx" data-ga="close" title="Close (Esc)">Esc</button></div>
 <div class="globgrid">${H.cards(G)}</div>
 <div class="libfoot"><span>The GLOBAL key in the top bar or the engine menu opens it · Esc closes · every change is stored on the machine and made active</span><span class="fw" title="The machine keeps 8 global setups">GLOBAL ${G.slot + 1} of 8</span></div>`;
	if (typeof enhanceSelects === "function") enhanceSelects(pop);
	pop.hidden = false; placeGlobal();
}
function placeGlobal() {
	const pop = document.getElementById("globpop"), r = document.querySelector(".lcdpanel").getBoundingClientRect(), top = Math.max(16, r.bottom + 8);
	pop.style.top = (top + scrollY) + "px"; pop.style.maxHeight = Math.max(240, innerHeight - top - 12) + "px";
	pop.style.left = Math.max(16, (document.documentElement.clientWidth - pop.offsetWidth) / 2 + scrollX) + "px";
}
function globKeyLit() { const k = document.getElementById("globkey"); if (k) { k.setAttribute("aria-pressed", String(GP.open)); k.classList.toggle("on", GP.open); const l = k.querySelector(".led"); if (l) l.classList.toggle("on", GP.open); } }
function openGlobal() { if (typeof closeLib === "function") closeLib(false); GP.open = true; drawGlobal(); globKeyLit(); }
function closeGlobal() { GP.open = false; drawGlobal(); globKeyLit(); }
function globalClick(a) {
	const H = DeskGlobal.host, f = a.dataset.ga;
	if (f === "close") { closeGlobal(); return; }
	const G = H && H.view(); if (!G) return;
	if (f === "slot") { if (+a.dataset.v !== G.slot) H.slot(+a.dataset.v); return; }
	if (f === "reset") {
		H.ask(`Reset GLOBAL ${G.slot + 1} to the factory settings? MIDI channels, sync and the rest go back to how the machine shipped.`, () => H.reset());
		return;
	}
	H.click(a, G);
}
document.addEventListener("click", e => {
	if (e.target.closest && e.target.closest("#globkey")) { GP.open ? closeGlobal() : openGlobal(); e.stopPropagation(); return; }
	if (!GP.open) return;
	const pop = document.getElementById("globpop");
	if (pop.contains(e.target)) { const a = e.target.closest("[data-ga]"); if (a) globalClick(a); return; }
	if (!(e.target.closest && e.target.closest("#dlg,.kpop,#kpop,.lcdeng"))) closeGlobal();
}, true);
document.addEventListener("change", e => {
	const s = e.target.closest && e.target.closest("#globpop [data-gsel]"), H = DeskGlobal.host;
	if (s && H && H.change && H.view()) H.change(s, H.view());
});
addEventListener("resize", () => { if (GP.open) placeGlobal(); });
Keys.bind({ id: "global-key", scope: "any", area: "Top bar", keys: ["GLOBAL key"], group: "Anywhere", does: "The machine's own settings, kept in its memory: MIDI channels, sync and the rest (also the engine menu: GLOBAL…)" });
Keys.bind({ id: "close-global", short: "Close", scope: "any", keys: ["Escape"], group: "Anywhere", does: "Close the GLOBAL settings", when: () => GP.open, run: () => closeGlobal() });
