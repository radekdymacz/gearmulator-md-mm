"use strict";
/* The CONTROL workspace's devices and the TR-06's view (deskController.js, DESIGN-tr06.md), in node: it
   renders the plug-in's "controller" document (fixtures checked against both contracts) as device tiles, the
   TR-06's view with its MIDI monitor and the mapping matrix behind any other device, and every change becomes
   the host call the pages turn into a ctl* command with only the arguments the contract declares. No browser,
   a small stand-in for the document; simulated documents only.
     node deskControllerPageTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
const { execFileSync } = require("child_process");

let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

/* the page's document, as far as the panel touches it */
const element = () => ({ className: "", id: "", hidden: false, innerHTML: "", textContent: "", style: { setProperty() {} }, offsetWidth: 800,
	setAttribute() {}, appendChild() {}, addEventListener() {}, querySelector: () => null, querySelectorAll: () => [], getBoundingClientRect: () => ({ bottom: 100 }) });
const document = { createElement: element, body: element(), head: element(), documentElement: { clientWidth: 1400 }, querySelector: () => null,
	querySelectorAll: () => [], getElementById: () => null, addEventListener() {} };
const ctx = vm.createContext({ console, document, scrollX: 0, scrollY: 0, innerHeight: 900 });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskController.js"), "utf8") + "\nthis.Ctl = Ctl;", ctx);
const { Ctl } = ctx;

const root = path.join(__dirname, "..", "..", "..", "..", "..", "..");
const schemaOf = m => path.join(root, "doc", "modern-ux", m + "-data-contract.schema.json");
const validate = (m, def, instance) => {
	try {
		execFileSync("python3", [path.join(root, "doc", "modern-ux", "page_contract_check.py"), schemaOf(m), def], { input: JSON.stringify(instance) });
		return [];
	} catch (e) {
		return (e.stdout || "").toString().trim().split("\n").filter(Boolean);
	}
};

/* fixtures: deskController::pageDocument's shape (deskControllerTest checks the C++ side against the same schema) */
const VOICES = [["BD", "BASS DRUM", [36, 35]], ["SD", "SNARE DRUM", [38, 40]], ["LT", "LOW TOM", [47, 45]], ["HT", "HIGH TOM", [50, 48]], ["CY", "CYMBAL", [49]], ["OH", "OPEN HIHAT", [46]], ["CH", "CLOSED HIHAT", [42, 44]]];
const doc = m => {
	const mm = m === "mm";
	return {
		schema: "desk/controller", version: 1, machine: m, profile: "tr06", profiles: [{ id: "off", label: "Off" }, { id: "tr06", label: "Roland TR-06" }],
		channel: 10, knobMode: "relative", about: "shipped mapping", tracks: mm ? 6 : 16, selected: 2, machineName: mm ? "SWAVE-SAW" : "TRX-BD", known: true, warning: "",
		voices: VOICES.map(([voice, label, notes], v) => Object.assign({ voice, label, notes, t: mm ? Math.min(v, 5) : v, out: { ch: 1 + (mm ? Math.min(v, 5) : 0), note: mm ? 60 : 36 + v } }, mm ? { note: 60 } : {})),
		knobs: [{ cc: 24, label: "BD LEVEL", i: 0, name: mm ? "SYN A" : "SYN 1", own: mm ? "UNIL" : "PTCH" }, { cc: 71, label: "ACC LEVEL", pg: 8, i: 0, name: "NOTE", own: mm ? "" : "PTCH" },
			{ cc: 20, label: "BD TUNE", i: -1, name: "", own: "" }].map(k => mm && k.pg == null ? Object.assign({ pg: k.i < 0 ? -1 : 0 }, k) : k),
		targets: mm ? [{ pg: 0, i: 0, name: "SYN A", own: "UNIL" }, { pg: 2, i: 0, name: "FLTR BASE", own: "" }, { pg: 7, i: 0, name: "LEVEL", own: "" }, { pg: 8, i: 0, name: "NOTE", own: "" }]
			: [{ i: 0, name: "SYN 1", own: "PTCH" }, { i: 16, name: "DIST", own: "" }, { i: 24, name: "LEVEL", own: "" }, { pg: 8, i: 0, name: "NOTE", own: "PTCH" }],
		last: null, named: true, inputs: [{ name: "TR-06", on: true, tr06: true }, { name: "IAC Driver Bus 1", on: false, tr06: false }], device: "TR-06", activity: null
	};
};
/* what is seen: the activity (deskController::Activity's shape): CCs on the TR-06's channel and another, notes */
const act = () => ({ seq: 3, last: { kind: "cc", n: 24, v: 87 }, elsewhere: null,
	ccs: [{ ch: 10, cc: 24, v: 87 }, { ch: 10, cc: 71, v: 64 }, { ch: 10, cc: 99, v: 5 }, { ch: 3, cc: 24, v: 40 }], notes: [{ ch: 10, n: 36, v: 100 }, { ch: 3, n: 60, v: 90 }] });
const seen = m => Object.assign(doc(m), { activity: act() });
const daw = m => Object.assign(doc(m), { named: false, inputs: null, device: "", profile: "off" });
for (const m of ["md", "mm"]) {
	const problems = [doc(m), seen(m), daw(m), Object.assign(seen(m), { activity: Object.assign(act(), { elsewhere: 3 }) })].flatMap(d => validate(m, "message", { type: "controller", doc: d }));
	check(problems.length === 0, m + ": the fixtures are controller messages of the contract" + (problems.length ? ": " + problems.join("; ") : ""));
}

/* the commands a host call becomes (mdDeskApp.js's cmd, mmAdapter.js's send: the same members) */
const commands = m => {
	const s = JSON.parse(fs.readFileSync(schemaOf(m), "utf8"));
	const out = {};
	for (const c of s.$defs.command.oneOf) out[c.properties.op.const] = new Set(Object.keys(c.properties).filter(k => k !== "op"));
	return out;
};
const HOST_OP = { set: "ctlSet", voice: "ctlVoice", knob: "ctlKnob", reset: "ctlReset", watch: "ctlWatch", clear: "ctlClear" };
for (const m of ["md", "mm"]) {
	const table = commands(m), d = doc(m);
	const sends = [
		Ctl.intent(d, "profile", "off", {}), Ctl.intent(d, "channel", "3", {}),
		Ctl.intent(d, "voice", "4", { voice: "SD" }), Ctl.intent(d, "knob", m === "mm" ? "2:0" : "-1:16", { cc: "24" }), Ctl.intent(d, "knob", "-1:-1", { cc: "24" })];
	sends.push(Ctl.intent(d, "use", "", {}), Ctl.intent(d, "setch", "", { ch: "3" }), ["watch", { on: true }], Ctl.intent(d, "knobmode", "absolute", {}), Ctl.intent(d, "clear", "", {}),
		Ctl.intent(d, "knob", "8:0", { cc: "20" }));
	if (m === "mm") sends.push(Ctl.intent(d, "note", "200", { voice: "OH", t: "5" }));
	const off = sends.filter(([f, a]) => !table[HOST_OP[f]] || Object.keys(a).some(k => !table[HOST_OP[f]].has(k)));
	check(off.length === 0, m + ": every change is a ctl command with declared arguments" + (off.length ? " (" + JSON.stringify(off) + ")" : ""));
	check(table.ctlTrack && table.ctlTrack.has("t") && table.ctlReset && table.ctlClear && table.ctlSet.has("knobMode"), m + ": ctlTrack, ctlReset, ctlClear and ctlSet knobMode are in the contract");
	const [, set] = sends[1], [, voice] = sends[2], [, knob] = sends[3], [, none] = sends[4];
	check(set.channel === 3 && sends[0][1].profile === "off", m + ": profile and channel");
	check(voice.voice === "SD" && voice.t === 4 && (m === "md" ? !("note" in voice) : voice.note === 60), m + ": a voice's track (MM: its note kept)");
	check(m === "md" ? knob.cc === 24 && knob.i === 16 && !("pg" in knob) : knob.pg === 2 && knob.i === 0, m + ": a knob's target");
	check(none.i === null, m + ": a knob's target cleared (i null)");
	check(sends[5][1].profile === "tr06" && sends[6][1].channel === 3, m + ": Use TR-06 turns the profile on, SET CH the channel");
	check(sends[8][1].knobMode === "absolute" && sends[9][0] === "clear", m + ": the knob mode, CLEAR");
	check(sends[10][1].pg === 8 && sends[10][1].i === 0 && sends[10][1].cc === 20, m + ": NOTE on another knob (pg 8, i 0)");
	if (m === "mm") check(sends[11][1].note === 127 && sends[11][1].t === 5, "mm: a note is held at 0..127");
}

/* a change reaches the host the page set after this script loaded (mdDeskApp.js, mmAdapter.js assign Ctl.host) */
{
	const calls = [];
	Ctl.host = { set: a => calls.push(["ctlSet", a]), voice: a => calls.push(["ctlVoice", a]), knob: a => calls.push(["ctlKnob", a]), reset: () => calls.push(["ctlReset"]), clear: () => calls.push(["ctlClear"]) };
	Ctl.onDoc(doc("md"));
	Ctl.change("knob", "-1:21", { cc: "20" });
	Ctl.change("channel", "5", {});
	Ctl.click({ dataset: { ctl: "clear" } });
	check(calls.length === 3 && calls[0][0] === "ctlKnob" && calls[0][1].i === 21 && calls[1][1].channel === 5 && calls[2][0] === "ctlClear", "changes go to the host the page assigned; CLEAR is ctlClear");
}

/* the devices: a tile per MIDI input; a TR-06 tile with the profile's LED; a DAW's */
{
	let renders = 0;
	Ctl.rerender = () => renders++;
	Ctl.onDoc(doc("md"));
	Ctl.show("");
	const html = Ctl.controlHtml("<i id=LEARN></i>");
	check(html.includes('id="ctlroot"') && html.includes('data-view="devices"'), "the CONTROL workspace opens on the devices");
	check((html.match(/<button class="ctltile/g) || []).length === 2 && html.includes('data-kind="tr06"') && html.includes("IAC Driver Bus 1") && html.includes("NOT ENABLED"), "one tile per MIDI input: the TR-06 and IAC (not enabled)");
	check(html.includes("<svg") && html.includes('class="led on"') && html.includes("PROFILE ON"), "inline icons; the TR-06 tile's LED lit, the profile on");
	check(!html.includes("LEARN") && !html.includes("TR-06 MAP"), "the mapping matrix and the old TR-06 MAP… key are not on the devices view");
	Ctl.onDoc(Object.assign(doc("md"), { profile: "off" }));
	check(Ctl.controlHtml("").includes('class="led dim"') && Ctl.controlHtml("").includes("PROFILE OFF"), "profile off with a TR-06 found: the LED dim, steady (no blinking anywhere)");
	Ctl.onDoc(daw("md"));
	const t = Ctl.tiles();
	check(t.length === 2 && t[0].name === "Host MIDI in" && t[1].kind === "tr06", "a DAW: Host MIDI in and the TR-06 tile");
	Ctl.onDoc(Object.assign(doc("md"), { inputs: [{ name: "TR-06", on: true, tr06: true }] }));
	check(Ctl.tiles().some(x => x.id === "dev:all"), "only a TR-06: a MIDI Learn tile keeps the mapping matrix reachable");
	/* a tile opens its view; DEVICES goes back */
	Ctl.onDoc(doc("md"));
	Ctl.click({ dataset: { ctl: "tile", view: "dev:IAC Driver Bus 1" } });
	const dev = Ctl.controlHtml("<i id=LEARN></i>");
	check(renders > 0 && Ctl.view() === "dev:IAC Driver Bus 1" && dev.includes("<i id=LEARN></i>") && dev.includes('id="ctl-back"') && dev.includes("IAC Driver Bus 1"), "a generic device: the page's mapping matrix (MIDI Learn) behind it, a DEVICES key");
	Ctl.click({ dataset: { ctl: "back" } });
	check(Ctl.view() === "", "DEVICES: back to the tiles");
}

/* the TR-06's view: settings, the monitor with names and what each CC moves, voices, knobs; every select has an id */
{
	Ctl.show("tr06");
	Ctl.onDoc(seen("mm"));
	let html = Ctl.controlHtml("<i id=LEARN></i>");
	check(html.includes('id="ctl-profile"') && html.includes('id="ctl-channel"') && html.includes('id="ctl-knobmode"') && html.includes('id="ctl-clear"') && !html.includes("LEARN"), "the TR-06: profile, channel, knob mode, CLEAR (no mapping matrix)");
	check(VOICES.every(([v]) => html.includes(`data-voice="${v}"`)) && html.includes('data-knob="24"') && html.includes('data-ctl="note"'), "every voice and knob has its row; the MM's notes");
	const mon = Ctl.monitorHtml();
	check(/CC 24<\/td><td class="mono v">87<\/td><td>BD LEVEL<\/td><td>→ T3 SYN A · UNIL/.test(mon), "the monitor: CC 24 · 87 · BD LEVEL → T3 SYN A · UNIL (the machine's own name)");
	check(/CC 99<\/td><td class="mono v">5<\/td><td><\/td><td>not a TR-06 knob/.test(mon), "a CC the TR-06 does not send: not a TR-06 knob");
	check(/CH 3<\/td><td class="mono">CC 24<\/td><td class="mono v">40<\/td><td>BD LEVEL<\/td><td>not the TR-06&#39;s channel|CH 3<\/td><td class="mono">CC 24<\/td><td class="mono v">40<\/td><td>BD LEVEL<\/td><td>not the TR-06's channel/.test(mon), "another channel's CC, labelled with its channel");
	check(mon.includes("NOTE 36 · 100 (BD)") && mon.includes("CH 3 NOTE 60 · 90"), "the last notes, with velocity; another channel's labelled");
	check(mon.indexOf("CC 24") < mon.indexOf("CC 71") && mon.indexOf("CC 71") < mon.indexOf("CC 99"), "sorted by CC number");
	/* NOTE on the MM: the voices on the selected track, from and to */
	const sel0 = Object.assign(seen("mm"), { selected: 0 });
	Ctl.onDoc(sel0);
	const up = JSON.parse(JSON.stringify(sel0)); up.voices[0].note = 62;
	Ctl.onDoc(up);
	check(Ctl.monitorHtml().includes("→ NOTE T1 60→62"), "MM: CC 71 → NOTE T1 60→62");
	/* NOTE on the MD: the machine's pitch parameter */
	Ctl.onDoc(seen("md"));
	check(Ctl.monitorHtml().includes("→ T3 NOTE · PTCH") && Ctl.monitorHtml().includes("→ T3 SYN 1 · PTCH"), "MD: CC 71 → T3 NOTE · PTCH, CC 24 → T3 SYN 1 · PTCH");
	Ctl.onDoc(Object.assign(seen("md"), { knobs: doc("md").knobs.map(k => k.cc === 71 ? Object.assign({}, k, { own: "no pitch on this machine" }) : k) }));
	check(Ctl.monitorHtml().includes("NOTE · no pitch on this machine"), "MD: a machine without pitch says so");
	html = Ctl.controlHtml("");
	const selects = [...html.matchAll(/<select([^>]*)>/g)].map(x => x[1]);
	check(selects.length > 4 && selects.every(a => / id="ctl-[\w-]+"/.test(a)), "every select of the TR-06's view has an id (the page's dropdowns find it)");
	check(html.includes("SYN 1 · PTCH") && html.includes("NOTE · PTCH"), "the knobs' target lists name the machine's own parameters");
	/* the view watches what arrives, the devices do not */
	const calls = [];
	Ctl.host = { watch: a => calls.push(a.on) };
	const real = document.getElementById;
	document.getElementById = id => id === "ctlroot" ? {} : null;
	Ctl.rendered();
	Ctl.show("");
	document.getElementById = real;
	check(calls.join() === "true,false", "the TR-06's view watches (ctlWatch on), the devices stop it");
}

/* nothing blinks; no TR-06 MAP… key, no controller bar, no CONTROLLER… in the engine menu (both pages, the mockups) */
{
	const root2 = path.join(__dirname, "..");
	const src = fs.readFileSync(path.join(__dirname, "deskController.js"), "utf8");
	check(!/\.blink|blink\(|animation|pgblink/.test(src), "deskController.js: no blinking, no animation");
	const files = [path.join(__dirname, "mdStudio.html"), path.join(root2, "mmStudio", "mmStudio.html"), path.join(root2, "mmStudio", "mmMockup.js"), path.join(__dirname, "mdDeskApp.js"), path.join(root2, "mmStudio", "mmAdapter.js")];
	check(files.every(f => !/CONTROLLER…|value="ctl"|"ctl"\]|"ctl",|TR-06 MAP|ctlbar|barHtml|ctlpop/.test(fs.readFileSync(f, "utf8"))), "no CONTROLLER… in the engine menu, no bar, no TR-06 MAP… panel");
}

console.log("deskControllerPageTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
process.exit(failures ? 1 : 0);
