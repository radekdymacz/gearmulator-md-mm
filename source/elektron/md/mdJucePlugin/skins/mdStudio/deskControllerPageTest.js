"use strict";
/* The controller profile's panel (deskController.js, DESIGN-tr06.md), in node: it renders the plug-in's
   "controller" document (fixtures checked against both contracts), and every change becomes the host
   call the pages turn into a ctl* command with only the arguments the contract declares. No browser, a
   small stand-in for the document; simulated documents only.
     node deskControllerPageTest.js */
const fs = require("fs"), path = require("path"), vm = require("vm");
const { execFileSync } = require("child_process");

let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };

/* the page's document, as far as the panel touches it */
const element = () => ({ className: "", id: "", hidden: false, innerHTML: "", textContent: "", style: { setProperty() {} }, offsetWidth: 800,
	setAttribute() {}, appendChild() {}, addEventListener() {}, querySelector: () => null, querySelectorAll: () => [], getBoundingClientRect: () => ({ bottom: 100 }) });
const document = { createElement: element, body: element(), head: element(), documentElement: { clientWidth: 1400 }, querySelector: () => null };
const ctx = vm.createContext({ console, document, scrollX: 0, scrollY: 0, innerHeight: 900 });
vm.runInContext(fs.readFileSync(path.join(__dirname, "deskController.js"), "utf8") + "\nthis.Ctl = Ctl; this.closeController = closeController;", ctx);
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
		channel: 10, about: "shipped mapping", tracks: mm ? 6 : 16, selected: 2, known: true, warning: "",
		voices: VOICES.map(([voice, label, notes], v) => Object.assign({ voice, label, notes, t: mm ? Math.min(v, 5) : v, out: { ch: 1 + (mm ? Math.min(v, 5) : 0), note: mm ? 60 : 36 + v } }, mm ? { note: 60 } : {})),
		knobs: [{ cc: 24, label: "BD LEVEL", i: 0, name: mm ? "SYN A" : "SYN 1" }, { cc: 20, label: "BD TUNE", i: -1, name: "" }].map(k => mm ? Object.assign({ pg: k.i < 0 ? -1 : 0 }, k) : k),
		targets: mm ? [{ pg: 0, i: 0, name: "SYN A" }, { pg: 2, i: 0, name: "FLTR BASE" }, { pg: 7, i: 0, name: "LEVEL" }] : [{ i: 0, name: "SYN 1" }, { i: 16, name: "DIST" }, { i: 24, name: "LEVEL" }],
		last: null
	};
};
for (const m of ["md", "mm"]) {
	const problems = validate(m, "message", { type: "controller", doc: doc(m) });
	check(problems.length === 0, m + ": the fixture is a controller message of the contract" + (problems.length ? ": " + problems.join("; ") : ""));
}

/* the commands a host call becomes (mdDeskApp.js's cmd, mmAdapter.js's send: the same members) */
const commands = m => {
	const s = JSON.parse(fs.readFileSync(schemaOf(m), "utf8"));
	const out = {};
	for (const c of s.$defs.command.oneOf) out[c.properties.op.const] = new Set(Object.keys(c.properties).filter(k => k !== "op"));
	return out;
};
const HOST_OP = { set: "ctlSet", voice: "ctlVoice", knob: "ctlKnob", reset: "ctlReset" };
for (const m of ["md", "mm"]) {
	const table = commands(m), d = doc(m);
	const sends = [
		Ctl.intent(d, "profile", "off", {}), Ctl.intent(d, "channel", "3", {}),
		Ctl.intent(d, "voice", "4", { voice: "SD" }), Ctl.intent(d, "knob", m === "mm" ? "2:0" : "-1:16", { cc: "24" }), Ctl.intent(d, "knob", "-1:-1", { cc: "24" })];
	if (m === "mm") sends.push(Ctl.intent(d, "note", "200", { voice: "OH", t: "5" }));
	const off = sends.filter(([f, a]) => !table[HOST_OP[f]] || Object.keys(a).some(k => !table[HOST_OP[f]].has(k)));
	check(off.length === 0, m + ": every change is a ctl command with declared arguments" + (off.length ? " (" + JSON.stringify(off) + ")" : ""));
	check(table.ctlTrack && table.ctlTrack.has("t") && table.ctlReset, m + ": ctlTrack and ctlReset are in the contract");
	const [, set] = sends[1], [, voice] = sends[2], [, knob] = sends[3], [, none] = sends[4];
	check(set.channel === 3 && sends[0][1].profile === "off", m + ": profile and channel");
	check(voice.voice === "SD" && voice.t === 4 && (m === "md" ? !("note" in voice) : voice.note === 60), m + ": a voice's track (MM: its note kept)");
	check(m === "md" ? knob.cc === 24 && knob.i === 16 && !("pg" in knob) : knob.pg === 2 && knob.i === 0, m + ": a knob's target");
	check(none.i === null, m + ": a knob's target cleared (i null)");
	if (m === "mm") check(sends[5][1].note === 127 && sends[5][1].t === 5, "mm: a note is held at 0..127");
}

/* a change reaches the host the page set after this script loaded (mdDeskApp.js, mmAdapter.js assign Ctl.host) */
{
	const calls = [];
	Ctl.host = { set: a => calls.push(["ctlSet", a]), voice: a => calls.push(["ctlVoice", a]), knob: a => calls.push(["ctlKnob", a]), reset: () => calls.push(["ctlReset"]) };
	Ctl.onDoc(doc("md"));
	Ctl.change("knob", "-1:21", { cc: "20" });
	Ctl.change("channel", "5", {});
	check(calls.length === 2 && calls[0][0] === "ctlKnob" && calls[0][1].i === 21 && calls[1][1].channel === 5, "changes go to the host the page assigned");
}

/* rendering: every voice and knob, the selected track, the warning */
{
	const d = doc("mm");
	d.warning = "Channel 10 is also the Monomachine's (MULTI TRIG).";
	Ctl.open(); Ctl.onDoc(d);
	const html = Ctl.html();
	check(VOICES.every(([v]) => html.includes(`data-voice="${v}"`)), "every voice has its row");
	check(html.includes('data-cc="24"') && html.includes('data-cc="20"'), "every knob has its row");
	check(html.includes("track 3") && html.includes("MULTI TRIG") && html.includes('data-ctl="note"'), "the selected track, the warning, the MM's notes");
	Ctl.onDoc(Object.assign({}, doc("md"), { profile: "off" }));
	check(Ctl.html().includes("Off: MIDI in reaches the machine as it always did") && !Ctl.html().includes('data-ctl="note"'), "off, and the MD has no note column");
	ctx.closeController();
	check(!Ctl.isOpen(), "closeController closes it (the MODAL layer's close function)");
}

console.log("deskControllerPageTest: " + (failures ? "FAIL" : "PASS") + " (" + failures + " failures)");
process.exit(failures ? 1 : 0);
