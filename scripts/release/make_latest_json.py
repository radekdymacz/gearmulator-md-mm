#!/usr/bin/env python3
"""Write site/public/latest.json for a published release (doc/modern-ux/DESIGN-updates.md 3.1).

  python3 scripts/release/make_latest_json.py --tag mdmm-v0.3.3 --assets DIR --out site/public/latest.json [--date 2026-10-08]

DIR holds the release's assets as downloaded from GitHub (the bytes users get). Each of the six fixed
names that is there becomes assets.<os>.<machine> with its size, SHA-256 and, when MDMM_UPDATE_SIGNING_KEY
is set, its update signature (sign_update.py). A missing name is left out (no build for that system: the
app offers the site). No asset at all is an error.
"""

import argparse
import datetime
import json
import os
import re
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the repository
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sign_update  # noqa: E402

REPO = "radekdymacz/gearmulator-md-mm"
SITE = "https://mdmm.dev"
TAG_RE = re.compile(r"^mdmm-v([0-9]+\.[0-9]+\.[0-9]+)$")

# The fixed release asset names (the site's download links, config.js, use the same): os -> machine -> name.
# The app has the same table (mdmmUpdate/updateCore.cpp, assetName).
ASSETS = {
    "mac": {"md": "Machinedrum-Editor-macOS.pkg", "mm": "Monomachine-Editor-macOS.pkg"},
    "win": {"md": "Machinedrum-Editor-Windows-x64-not-tested.zip", "mm": "Monomachine-Editor-Windows-x64-not-tested.zip"},
    "linux": {"md": "Machinedrum-Editor-Linux-x64-not-tested.tar.gz", "mm": "Monomachine-Editor-Linux-x64-not-tested.tar.gz"},
}


def manifest(tag, assets_dir, date, seed):
    m = TAG_RE.match(tag)
    if not m:
        raise SystemExit("not a release tag (mdmm-vMAJOR.MINOR.PATCH, no suffix): " + tag)
    version = m.group(1)
    out = {
        "schema": 1,
        "version": version,
        "tag": tag,
        "date": date,
        "notes": "https://github.com/%s/releases/tag/%s" % (REPO, tag),
        "page": SITE + "/get/",
        "assets": {},
    }
    for os_name, machines in ASSETS.items():
        for machine, name in machines.items():
            path = os.path.join(assets_dir, name)
            if not os.path.isfile(path):
                print("not on the release, left out: " + name, file=sys.stderr)
                continue
            e = sign_update.entry(path, version, seed)
            e["url"] = "https://github.com/%s/releases/download/%s/%s" % (REPO, tag, name)
            out["assets"].setdefault(os_name, {})[machine] = {k: e[k] for k in ("name", "url", "size", "sha256", "sig") if k in e}
    if not out["assets"]:
        raise SystemExit("none of the release assets is in " + assets_dir)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--tag", required=True)
    p.add_argument("--assets", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--date", default=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d"))
    a = p.parse_args()
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", a.date):
        raise SystemExit("date must be YYYY-MM-DD: " + a.date)
    seed = sign_update.signing_key()
    if seed is None:
        print("MDMM_UPDATE_SIGNING_KEY not set: latest.json without signatures (the apps notify, never install)",
              file=sys.stderr)
    else:
        sign_update.check_key(seed)
    doc = manifest(a.tag, a.assets, a.date, seed)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    signed = sum(1 for o in doc["assets"].values() for e in o.values() if "sig" in e)
    total = sum(len(o) for o in doc["assets"].values())
    print("latest.json: %s, %d assets, %d signed" % (doc["version"], total, signed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
