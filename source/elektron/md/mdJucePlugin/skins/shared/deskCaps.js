"use strict";
/* What the engine cannot do, both editors (skins/shared/; DESIGN-REVIEW-2026-10-02 finding 16). One routine marks
   each capability's controls from the page's table ({capability: selector}: CAP_CONTROLS on the MD page, NA_SEL on
   the MM page) and the reasons the page has; one guard keeps a marked control from taking a gesture (a click says
   the reason). A capability whose reason is empty is allowed. Each editor's look is an option of the one routine:
     attr       the data attribute that marks a control ("capna" on the MD page; "na" on the MM page, whose
                stylesheet dims [data-na] and which also marks library slots still being read with it)
     keepTitle  the control's own tooltip comes back when it is allowed again (the MD page's)
     disable    a form control is disabled too, and enabled again when allowed (the MD page's)
     cards      {capability: selector}: a card that also says the reason in words, under its header (the MM
                page's MULTI MAP)
   Only what this routine marked is released here, so another use of the attribute stays. */
const DeskCaps = (() => {
	const marked = new Map();	// attr -> Map(element -> {title, disabled}: what marking it changed)

	function mark({ controls, reason, attr = "na", keepTitle = false, disable = false, cards = {},
		all = s => [...document.querySelectorAll(s)], one = s => document.querySelector(s) }) {
		if (!marked.has(attr)) marked.set(attr, new Map());
		const mine = marked.get(attr), off = new Map();
		/* the first capability (in the table's order) that has a reason gives a control its reason */
		for (const [cap, sel] of Object.entries(controls)) {
			const why = reason(cap);
			if (!why) continue;
			for (const el of all(sel)) if (!off.has(el)) off.set(el, why);
		}
		for (const [el, was] of mine) {
			if (off.has(el)) continue;
			mine.delete(el);
			delete el.dataset[attr];
			el.removeAttribute("aria-disabled");
			if (keepTitle) el.title = was.title;
			if (was.disabled) el.disabled = false;
		}
		for (const [el, why] of off) {
			if (!mine.has(el)) {
				const was = { title: el.title || "", disabled: false };
				if (disable && "disabled" in el && !el.disabled) { el.disabled = true; was.disabled = true; }
				mine.set(el, was);
			}
			el.dataset[attr] = "1";
			el.title = why;
			el.setAttribute("aria-disabled", "true");
		}
		for (const [cap, sel] of Object.entries(cards)) {
			const card = one(sel), why = reason(cap);
			if (card && why && !card.querySelector(".statusline")) card.querySelector("header").insertAdjacentHTML("afterend", `<p class="statusline">${why}</p>`);
		}
	}

	/* A marked control (any element with the attribute) takes none of _events; a click shows its title. */
	function guard({ attr = "na", events, say }) {
		const sel = "[data-" + attr + "]";
		for (const ev of events)
			document.addEventListener(ev, e => {
				const el = e.target.closest?.(sel);
				if (!el) return;
				e.preventDefault(); e.stopImmediatePropagation();
				if (e.type === "click") say(el.title);
			}, { capture: true, passive: false });
	}

	return { mark, guard };
})();
