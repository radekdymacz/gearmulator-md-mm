#!/usr/bin/env python3
"""Tests for the release scripts of the in-app updates (doc/modern-ux/DESIGN-updates.md); run by ctest.

The RFC 8032 vectors, the signing statement, latest.json with and without the key, the key check against
updateKey.h, and the site's version line. Uses a throw-away test key, never a release key.

Also the macOS installer's clean-up of the old Gearmulator MD / MM bundles (scripts/macos/pkg-resources/
remove-old-bundles, B-032): the name list in scripts/mdmm-product.env, and the rendered script run against
a temporary folder with stand-ins for the macOS tools. It never touches the real /Library or home folder.
"""

import base64
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True  # no __pycache__ in the repository
sys.path.insert(0, HERE)
import mdmm_ed25519 as ed  # noqa: E402
import make_latest_json  # noqa: E402
import sign_update  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failures += 1


def run(args, env=None):
    e = dict(os.environ)
    e.pop("MDMM_UPDATE_SIGNING_KEY", None)
    e.update(env or {})
    return subprocess.run([sys.executable] + args, capture_output=True, text=True, env=e)


print("test_release_scripts")
check(ed.self_test() == 0, "RFC 8032 vectors")

seed = hashlib.sha256(b"mdmm-update test key, never a release key").digest()
pub = ed.public_key(seed)
check(pub.hex() == "e6e477dbe6e37718980cab5abee3f905278c5dc76cfea0cb79da63588699a1bc", "the test key's public half (mdmmUpdateTest.cpp uses it)")
msg = ed.update_message("Machinedrum-Editor-macOS.pkg", "0.3.3", hashlib.sha256(b"hello update").hexdigest())
check(msg == b"mdmm-update-v1\nMachinedrum-Editor-macOS.pkg\n0.3.3\ned70ed98d448ff57ee2b137e52bfcc54729095de8dd049f8432c1bbc06581687\n",
      "the signed statement")
check(base64.b64encode(ed.sign(seed, msg)).decode() ==
      "OqUWLRFl4MVug1zsUDdct7X1U8A2yoFW1HqR55IISFMYIJqjEL0vwqeCNRW66x3Q2S4st+ScYSFCpRmKn7i6CQ==",
      "the statement's signature (mdmmUpdateTest.cpp checks it with Monocypher)")

with tempfile.TemporaryDirectory() as tmp:
    assets = os.path.join(tmp, "assets")
    os.makedirs(assets)
    for name in ("Machinedrum-Editor-macOS.pkg", "Monomachine-Editor-macOS.pkg", "Machinedrum-Editor-Linux-x64-not-tested.tar.gz"):
        with open(os.path.join(assets, name), "wb") as f:
            f.write(b"hello update" if name.endswith(".pkg") else b"linux")
    out = os.path.join(tmp, "latest.json")

    # without the key: hashes, no signatures
    r = run([os.path.join(HERE, "make_latest_json.py"), "--tag", "mdmm-v0.3.3", "--assets", assets, "--out", out, "--date", "2026-10-08"])
    check(r.returncode == 0, "latest.json without the key: written " + r.stderr.strip().replace("\n", " | "))
    doc = json.load(open(out))
    check(doc["schema"] == 1 and doc["version"] == "0.3.3" and doc["tag"] == "mdmm-v0.3.3" and doc["date"] == "2026-10-08", "its header")
    check(doc["notes"] == "https://github.com/radekdymacz/gearmulator-md-mm/releases/tag/mdmm-v0.3.3" and doc["page"] == "https://mdmm.dev/get/", "notes and page")
    md = doc["assets"]["mac"]["md"]
    check(md["url"] == "https://github.com/radekdymacz/gearmulator-md-mm/releases/download/mdmm-v0.3.3/Machinedrum-Editor-macOS.pkg", "the asset URL")
    check(md["size"] == 12 and md["sha256"] == hashlib.sha256(b"hello update").hexdigest() and "sig" not in md, "size, sha256, no sig")
    check("win" not in doc["assets"] and list(doc["assets"]["linux"]) == ["md"], "missing assets are left out")

    # a test header with the test key: signs; a header with another key: refuses; the placeholder: signs and warns
    header = os.path.join(tmp, "updateKey.h")
    with open(header, "w") as f:
        f.write('inline constexpr const char* g_updatePublicKeyHex = "%s";\n' % pub.hex())
    sign_update.check_key(seed, header)
    check(True, "the matching key is accepted")
    with open(header, "w") as f:
        f.write('inline constexpr const char* g_updatePublicKeyHex = "%s";\n' % ("00" * 32))
    try:
        sign_update.check_key(seed, header)
        check(False, "a key for another public key is refused")
    except SystemExit:
        check(True, "a key for another public key is refused")
    check(sign_update.compiled_public_key() is None or len(sign_update.compiled_public_key()) == 32, "updateKey.h reads (placeholder or a key)")

    signed = make_latest_json.manifest("mdmm-v0.3.3", assets, "2026-10-08", seed)
    sig = base64.b64decode(signed["assets"]["mac"]["md"]["sig"])
    check(ed.verify(pub, msg, sig), "latest.json's signature verifies against the statement")
    check(base64.b64encode(sig).decode() == "OqUWLRFl4MVug1zsUDdct7X1U8A2yoFW1HqR55IISFMYIJqjEL0vwqeCNRW66x3Q2S4st+ScYSFCpRmKn7i6CQ==",
          "the same signature as the C++ test's")

    r = run([os.path.join(HERE, "make_latest_json.py"), "--tag", "mdmm-v0.3.3-rc1", "--assets", assets, "--out", out])
    check(r.returncode != 0, "a pre-release tag is refused")
    r = run([os.path.join(HERE, "make_latest_json.py"), "--tag", "mdmm-v0.3.3", "--assets", tmp + "/none", "--out", out])
    check(r.returncode != 0, "no assets is an error")
    r = run([os.path.join(HERE, "sign_update.py"), "--version", "0.3.3", os.path.join(assets, "Machinedrum-Editor-macOS.pkg")],
            {"MDMM_UPDATE_SIGNING_KEY": "xyz"})
    check(r.returncode != 0, "a malformed key is refused")

    # the site's version line
    cfg = os.path.join(tmp, "config.js")
    with open(cfg, "w") as f:
        f.write('window.MDMM_CONFIG = {\n  // Bumped\n  version: "0.3.2",\n  other: { version: 3 }\n};\n')
    r = run([os.path.join(HERE, "set_site_version.py"), "0.3.3", cfg])
    text = open(cfg).read()
    check(r.returncode == 0 and 'version: "0.3.3",' in text and "version: 3" in text, "config.js: the version line, nothing else")
    r = run([os.path.join(HERE, "set_site_version.py"), "0.3.3", cfg])
    check(r.returncode == 0 and "unchanged" in r.stdout, "config.js: the same version is a no-op")
    r = run([os.path.join(HERE, "set_site_version.py"), "0.3", cfg])
    check(r.returncode != 0, "config.js: a bad version is refused")
    site_cfg = os.path.join(HERE, "..", "..", "site", "public", "config.js")
    check(len(re.findall(r'^\s*version:\s*"[0-9.]+",', open(site_cfg).read(), re.M)) == 1, "site/public/config.js has exactly one version line")


# ---------------------------------------------------------------------------------------------------------------
# The installer's clean-up of the bundles left under the old names (B-032): scripts/macos/pkg-resources/
# remove-old-bundles, rendered by scripts/macos/render_remove_old_bundles.sh. Run against a temporary folder as
# the install location and a temporary home; the macOS tools it calls (PlistBuddy, pkgutil, stat, dscl) are
# replaced by stand-ins in the rendered copy, so the real /Library and the real home folder are never looked at.
# ---------------------------------------------------------------------------------------------------------------
REPO = os.path.join(HERE, "..", "..")
MACOS = os.path.join(REPO, "scripts", "macos")


def read_env(path):
    values = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'^(MDMM_[A-Z_]+)="(.*)"$', line.rstrip("\n"))
            if m:
                values[m.group(1)] = m.group(2)
    return values


PLISTBUDDY_STUB = r"""#!/bin/sh
# Print :KEY of FILE (-c "Print :KEY" FILE) for an XML plist, like PlistBuddy does (the real one is used on a Mac).
key="${2#Print :}"
/usr/bin/sed -n "/<key>${key}<\/key>/{n;s/.*<string>\(.*\)<\/string>.*/\1/p;}" "$3"
"""
PKGUTIL_STUB = r"""#!/bin/sh
# pkgutil [--volume V] --files ID: the lines of $MDMM_TEST_RECEIPTS/ID (a receipt the test made), else an error.
for last; do :; done
[ -f "${MDMM_TEST_RECEIPTS}/${last}" ] && /bin/cat "${MDMM_TEST_RECEIPTS}/${last}" || exit 1
"""
STAT_STUB = r"""#!/bin/sh
# stat -f%Su /dev/console: the person at the console.
echo "${MDMM_TEST_CONSOLE_USER}"
"""
DSCL_STUB = r"""#!/bin/sh
# dscl . -read /Users/NAME NFSHomeDirectory
[ "$3" = "/Users/${MDMM_TEST_CONSOLE_USER}" ] && echo "NFSHomeDirectory: ${MDMM_TEST_CONSOLE_HOME}" || exit 1
"""

PLIST = ('<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict>\n'
         '<key>CFBundleIdentifier</key>\n<string>%s</string>\n<key>CFBundleExecutable</key>\n<string>%s</string>\n</dict></plist>\n')

# The expected names, written out here on purpose: the installer deletes by these, so a change to the product env
# file (or to the renderer) that moves them must be a visible change to this test.
EXPECT = {
    "md": {"new": "Machinedrum Editor", "old": "Gearmulator MD", "id": "com.nativekloud.machinedrum-editor",
           "old_id": "local.gearmulator.preview.GearmulatorMD", "other_old": "Gearmulator MM"},
    "mm": {"new": "Monomachine Editor", "old": "Gearmulator MM", "id": "com.nativekloud.monomachine-editor",
           "old_id": "local.gearmulator.preview.GearmulatorMM", "other_old": "Gearmulator MD"},
}
KINDS = {  # kind -> (folder below the install root, bundle extension, may the old upstream identifier remove it)
    "app": ("Applications", "app", False),
    "vst3": ("Library/Audio/Plug-Ins/VST3", "vst3", True),
    "au": ("Library/Audio/Plug-Ins/Components", "component", True),
}

env_file = read_env(os.path.join(REPO, "scripts", "mdmm-product.env"))
for machine, e in EXPECT.items():
    up = machine.upper()
    check(env_file.get("MDMM_PRODUCT_NAME_" + up) == e["new"] and env_file.get("MDMM_LEGACY_NAME_" + up) == e["old"],
          "%s: the product env file names %r (old %r)" % (machine, e["new"], e["old"]))
    check(env_file.get("MDMM_BUNDLE_ID_" + up) == e["id"] and env_file.get("MDMM_LEGACY_BUNDLE_ID_" + up) == e["old_id"],
          "%s: the product env file's bundle identifiers (%s, old %s)" % (machine, e["id"], e["old_id"]))
with open(os.path.join(REPO, "source", "elektron", "md", "mdJucePlugin", "mdmmPlugins.cmake")) as f:
    cmake_text = f.read()
for machine, e in EXPECT.items():
    found = re.search(r'GEARMULATOR_PLUGIN_BUNDLE_ID_%sJucePlugin "([^"]+)"' % machine, cmake_text)
    check(found is not None and found.group(1) == e["id"], "%s: the build's bundle identifier is the env file's" % machine)
with open(os.path.join(MACOS, "build_mdmm_pkg.sh")) as f:
    build_pkg = f.read()
check("render_remove_old_bundles.sh" in build_pkg and "write_preinstall" in build_pkg and build_pkg.count("write_preinstall \"") >= 3,
      "build_mdmm_pkg.sh renders the preinstall for the app, the VST3 and the AU")

if os.name == "posix" and os.path.exists("/bin/sh") and os.path.exists("/usr/bin/sed") and os.path.exists("/usr/bin/grep"):
    with tempfile.TemporaryDirectory() as tmp:
        tools = os.path.join(tmp, "tools")
        os.makedirs(tools)
        real_plistbuddy = "/usr/libexec/PlistBuddy"
        stubs = {"pkgutil": PKGUTIL_STUB, "stat": STAT_STUB, "dscl": DSCL_STUB}
        if not os.path.exists(real_plistbuddy):
            stubs["PlistBuddy"] = PLISTBUDDY_STUB
        for name, text in stubs.items():
            with open(os.path.join(tools, name), "w") as f:
                f.write(text)
            os.chmod(os.path.join(tools, name), 0o755)
        render_env = dict(os.environ,
                          MDMM_RENDER_PLISTBUDDY=real_plistbuddy if os.path.exists(real_plistbuddy) else os.path.join(tools, "PlistBuddy"),
                          MDMM_RENDER_PKGUTIL=os.path.join(tools, "pkgutil"),
                          MDMM_RENDER_STAT=os.path.join(tools, "stat"),
                          MDMM_RENDER_DSCL=os.path.join(tools, "dscl"))
        print("  (the property-list reader: %s)" % ("the real PlistBuddy" if os.path.exists(real_plistbuddy) else "a stand-in"))

        def make_bundle(root, folder, name, ext, identifier, executable, link_to=None):
            path = os.path.join(root, folder, "%s.%s" % (name, ext))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            if link_to:
                os.symlink(link_to, path)
                return path
            os.makedirs(os.path.join(path, "Contents", "MacOS"))
            with open(os.path.join(path, "Contents", "Info.plist"), "w") as f:
                f.write(PLIST % (identifier, executable))
            with open(os.path.join(path, "Contents", "MacOS", executable), "w") as f:
                f.write("x")
            return path

        for machine, e in EXPECT.items():
            for kind, (folder, ext, old_id_counts) in KINDS.items():
                label = "%s %s" % (machine, kind)
                script = os.path.join(tmp, "preinstall-%s-%s" % (machine, kind))
                r = subprocess.run([os.path.join(MACOS, "render_remove_old_bundles.sh"), machine, kind, script],
                                   capture_output=True, text=True, env=render_env)
                check(r.returncode == 0, "%s: the preinstall renders %s" % (label, r.stderr.strip()))
                if r.returncode != 0:
                    continue

                def run_script(root, home, receipts=None, console=None, hide_plist=False):
                    senv = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": home,
                            "MDMM_TEST_RECEIPTS": receipts or os.path.join(tmp, "no-receipts"),
                            "MDMM_TEST_CONSOLE_USER": console[0] if console else "nobody-at-the-console",
                            "MDMM_TEST_CONSOLE_HOME": console[1] if console else "/nonexistent"}
                    rendered = script
                    if hide_plist:  # the property-list reader is missing: the identifier is unknown
                        rendered = script + "-noplist"
                        with open(script) as f:
                            text = f.read()
                        with open(rendered, "w") as f:
                            f.write(re.sub(r'^plistbuddy=".*"$', 'plistbuddy="/nonexistent/PlistBuddy"', text, flags=re.M))
                    return subprocess.run(["/bin/sh", rendered, "/pkg.pkg", root, "/", "/"], capture_output=True, text=True, env=senv)

                # ---- the install root and the home folder (as an all-users install by a person whose home is known)
                case = os.path.join(tmp, "case-" + label.replace(" ", "-"))
                root, home = os.path.join(case, "root"), os.path.join(case, "home")
                os.makedirs(root)
                os.makedirs(home)
                os.makedirs(os.path.join(case, "elsewhere"))
                gone = [
                    make_bundle(root, folder, e["old"], ext, e["id"], e["old"]),                  # 0.3.1: old name, our identifier
                    make_bundle(home, folder, e["old"], ext, e["id"], e["old"]),                  # the same, by hand in the home folder
                ]
                kept = [
                    make_bundle(root, folder, e["new"], ext, e["id"], e["new"]),                  # the current bundle
                    make_bundle(home, folder, e["new"], ext, e["id"], e["new"]),
                    make_bundle(root, folder, e["other_old"], ext, e["old_id"], e["other_old"]),  # the other machine's old one
                    make_bundle(root, folder, "Osirus", ext, "local.gearmulator.preview.Osirus", "Osirus"),  # an unrelated bundle
                    make_bundle(root, folder, e["old"] + " 2", ext, e["id"], e["old"]),           # a name that only starts the same
                ]
                foreign_home = os.path.join(case, "foreign-home")
                os.makedirs(foreign_home)
                # another user's home folder is never looked at, whatever it holds
                kept.append(make_bundle(foreign_home, folder, e["old"], ext, e["id"], e["old"]))
                # the old name at another path (one folder deeper) is not the path the old version used
                kept.append(make_bundle(root, folder + "/sub", e["old"], ext, e["id"], e["old"]))
                r = run_script(root, home)
                check(r.returncode == 0, "%s: exit 0 (%s)" % (label, r.stderr.strip()))
                check(all(not os.path.exists(p) for p in gone), "%s: the old-named bundles with our identifier are removed" % label)
                check(all(os.path.exists(p) for p in kept), "%s: the current, the other machine's, unrelated, other-path and other users' bundles stay" % label)

                # ---- upstream's identifier (0.3.0 and earlier) at the exact old path
                case2 = os.path.join(tmp, "case2-" + label.replace(" ", "-"))
                root2, home2 = os.path.join(case2, "root"), os.path.join(case2, "home")
                os.makedirs(root2)
                os.makedirs(home2)
                a = make_bundle(root2, folder, e["old"], ext, e["old_id"], e["old"])
                b = make_bundle(home2, folder, e["old"], ext, e["old_id"], e["old"])
                r = run_script(root2, home2)
                if old_id_counts:
                    check(not os.path.exists(a) and not os.path.exists(b),
                          "%s: a copy with upstream's identifier is removed, in the root and in the home folder" % label)
                else:
                    check(os.path.exists(a) and os.path.exists(b),
                          "%s: a copy with upstream's identifier stays (an app of upstream's own)" % label)

                # ---- somebody else's bundle at the old name, a symbolic link, a plain file
                case3 = os.path.join(tmp, "case3-" + label.replace(" ", "-"))
                root3, home3 = os.path.join(case3, "root"), os.path.join(case3, "home")
                os.makedirs(root3)
                os.makedirs(home3)
                foreign = make_bundle(root3, folder, e["old"], ext, "com.example.somebody-else", e["old"])
                target = make_bundle(case3, "elsewhere", "Real " + e["old"], ext, e["id"], e["old"])
                link = make_bundle(home3, folder, e["old"], ext, None, None, link_to=target)
                r = run_script(root3, home3)
                check(os.path.exists(foreign), "%s: a bundle with a foreign identifier stays" % label)
                check(os.path.islink(link) and os.path.exists(target), "%s: a symbolic link at the old name, and what it points to, stay" % label)

                # ---- our receipt lists a copy with a foreign identifier: it is ours
                case4 = os.path.join(tmp, "case4-" + label.replace(" ", "-"))
                root4, home4 = os.path.join(case4, "root"), os.path.join(case4, "home")
                receipts = os.path.join(case4, "receipts")
                os.makedirs(root4)
                os.makedirs(home4)
                os.makedirs(receipts)
                listed = make_bundle(root4, folder, e["old"], ext, "com.example.unknown", e["old"])
                with open(os.path.join(receipts, "com.nativekloud.mdmm.%s.%s" % (machine, kind)), "w") as f:
                    f.write("%s/%s.%s\n%s/%s.%s/Contents\n" % (folder, e["old"], ext, folder, e["old"], ext))
                r = run_script(root4, home4, receipts=receipts)
                check(not os.path.exists(listed), "%s: a copy our package's receipt lists is removed" % label)

                # ---- a root-run installer: its home is unusable, the person at the console is found
                case5 = os.path.join(tmp, "case5-" + label.replace(" ", "-"))
                root5, console_home, stranger_home = (os.path.join(case5, n) for n in ("root", "console-home", "stranger-home"))
                for d in (root5, console_home, stranger_home):
                    os.makedirs(d)
                at_console = make_bundle(console_home, folder, e["old"], ext, e["id"], e["old"])
                stranger = make_bundle(stranger_home, folder, e["old"], ext, e["id"], e["old"])
                r = run_script(root5, "/var/root", console=("tester", console_home))
                check(not os.path.exists(at_console), "%s: the old copy in the console user's home folder is removed" % label)
                check(os.path.exists(stranger), "%s: another user's home folder is left alone" % label)

                # ---- built before the rename: the current name, an old executable
                case6 = os.path.join(tmp, "case6-" + label.replace(" ", "-"))
                root6, home6 = os.path.join(case6, "root"), os.path.join(case6, "home")
                os.makedirs(root6)
                os.makedirs(home6)
                stale = make_bundle(root6, folder, e["new"], ext, e["old_id"], e["old"])
                fresh = make_bundle(home6, folder, e["new"], ext, e["id"], e["new"])
                r = run_script(root6, home6)
                check(not os.path.exists(stale), "%s: a bundle at the new name with the old executable is removed" % label)
                check(os.path.exists(fresh), "%s: a current bundle is not touched" % label)

                # ---- no property-list reader: nothing is removed by identifier, the exit is still 0
                case7 = os.path.join(tmp, "case7-" + label.replace(" ", "-"))
                root7, home7 = os.path.join(case7, "root"), os.path.join(case7, "home")
                os.makedirs(root7)
                os.makedirs(home7)
                unreadable = make_bundle(root7, folder, e["old"], ext, e["id"], e["old"])
                r = run_script(root7, home7, hide_plist=True)
                check(r.returncode == 0 and os.path.exists(unreadable), "%s: without a property-list reader nothing is removed, and the exit is 0" % label)

                # ---- nothing there at all
                case8 = os.path.join(tmp, "case8-" + label.replace(" ", "-"))
                root8, home8 = os.path.join(case8, "root"), os.path.join(case8, "home")
                os.makedirs(root8)
                os.makedirs(home8)
                r = run_script(root8, home8)
                check(r.returncode == 0 and "Removing" not in r.stdout, "%s: with no old copy there is nothing to do, and the exit is 0" % label)
else:
    print("  (no /bin/sh here: the installer clean-up is not run)")


# The README's "latest" link follows the version: the notes of the version the plug-ins carry (the first thing a
# reader of the repository sees), and that file exists. Bump MDMM_EDITOR_VERSION and forget the README, and this fails.
root = os.path.join(HERE, "..", "..")
with open(os.path.join(root, "source", "elektron", "md", "mdJucePlugin", "mdmmPlugins.cmake")) as f:
    found = re.search(r"^set\(MDMM_EDITOR_VERSION ([0-9.]+)\)\s*$", f.read(), re.M)
check(found is not None, "mdmmPlugins.cmake has the MDMM_EDITOR_VERSION line")
if found:
    notes = "doc/release/v%s.md" % found.group(1)
    check(os.path.isfile(os.path.join(root, notes)), "%s exists" % notes)
    with open(os.path.join(root, "README.md")) as f:
        check(("(%s)" % notes) in f.read(), "README.md links %s (the current version's notes)" % notes)

print("test_release_scripts: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
sys.exit(1 if failures else 0)
