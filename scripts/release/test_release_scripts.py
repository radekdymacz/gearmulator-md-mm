#!/usr/bin/env python3
"""Tests for the release scripts of the in-app updates (doc/modern-ux/DESIGN-updates.md); run by ctest.

The RFC 8032 vectors, the signing statement, latest.json with and without the key, the key check against
updateKey.h, and the site's version line. Uses a throw-away test key, never a release key.
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
