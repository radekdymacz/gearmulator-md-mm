"use strict";
/* The version the editors show (deskAbout.js, 0.3.4): the page reads ?version= (the plug-in sets it to
   MDMM_EDITOR_VERSION, mdWebPageHost.cpp) and shows "v<version>" beside the Setup group's label and in the keyboard
   view's head; without it nothing. Both pages load the file.
     node deskAboutTest.js <MDMM_EDITOR_VERSION>   (CMake passes the version the build compiles in) */
const fs = require("fs"), path = require("path");
let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok   " : "  FAIL ") + what); if (!ok) failures++; };
const want = process.argv[2] || "0.0.0";
const HERE = __dirname;

/* a page opened with the version: the label goes beside the Setup label, once */
const made = [];
const label = { children: [], querySelector: sel => (sel === ".aboutver" ? label.children[0] || null : null), appendChild: c => label.children.push(c) };
global.location = { search: "?selftest=x&version=" + encodeURIComponent(want) + "&keyprobe=1" };
global.document = {
	readyState: "complete",
	querySelector: sel => (sel === ".setgroup .grplabel" ? label : null),
	createElement: tag => { const e = { tag, style: {} }; made.push(e); return e; },
	addEventListener: () => {}
};
const About = require("./deskAbout.js");
check(About.version === want, `the page's version is the build's, ${want} (got ${About.version})`);
check(About.label === "v" + want, `its label is v${want}`);
check(label.children.length === 1 && label.children[0].textContent === "v" + want && label.children[0].className === "aboutver",
	"shown beside the Setup group's label");
About.paint();
check(label.children.length === 1, "painted again: still once");

/* a page opened without it (the mockups): nothing */
delete require.cache[require.resolve("./deskAbout.js")];
global.location = { search: "" };
const none = require("./deskAbout.js");
check(none.version === "" && none.label === "", "no version in the page's address: no label");

/* the plug-in opens the page with the build's version; both pages load the file; the keyboard view shows it */
const host = fs.readFileSync(path.join(HERE, "../../mdWebPageHost.cpp"), "utf8");
check(/withParameter\("version",\s*mdmm::editorVersion\(\)\)/.test(host), "mdWebPageHost.cpp opens the page with ?version=mdmm::editorVersion()");
for (const page of ["../mdStudio/mdStudio.html", "../mmStudio/mmStudio.html"])
	check(fs.readFileSync(path.join(HERE, page), "utf8").includes('<script src="deskAbout.js"></script>'), `${path.basename(page)} loads deskAbout.js`);
check(/About\.label/.test(fs.readFileSync(path.join(HERE, "deskKeyView.js"), "utf8")), "the keyboard view's head shows About.label");

console.log(failures ? `deskAboutTest: FAIL (${failures})` : "deskAboutTest: PASS");
process.exit(failures ? 1 : 0);
