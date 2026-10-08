#!/usr/bin/env python3
"""Sign update files for the in-app updater (doc/modern-ux/DESIGN-updates.md 3.2).

  MDMM_UPDATE_SIGNING_KEY=<64 hex> python3 scripts/release/sign_update.py --version 0.3.3 FILE...

Prints one JSON line per file: {"name", "size", "sha256", "sig"}. The signature covers the statement
"mdmm-update-v1\\n<name>\\n<version>\\n<sha256>\\n" (mdmm_ed25519.update_message). Without the key it prints
the same lines without "sig" and says so on stderr (exit 0): a release without the secret is not signed.
A key whose public half is not the one in updateKey.h is refused (while updateKey.h has the placeholder,
it signs and warns).
"""

import argparse
import base64
import hashlib
import json
import os
import re
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the repository
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mdmm_ed25519 as ed  # noqa: E402

KEY_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                          "source", "elektron", "md", "mdmmUpdate", "updateKey.h")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
HEX64_RE = re.compile(r"[0-9a-fA-F]{64}")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def compiled_public_key(header=KEY_HEADER):
    """The public key the app carries, or None while updateKey.h has the placeholder."""
    with open(header, encoding="utf-8") as f:
        m = re.search(r'g_updatePublicKeyHex\s*=\s*"([^"]*)"', f.read())
    if not m:
        raise SystemExit("updateKey.h has no g_updatePublicKeyHex")
    value = m.group(1)
    return bytes.fromhex(value) if HEX64_RE.fullmatch(value) else None


def signing_key():
    """The 32-byte seed from MDMM_UPDATE_SIGNING_KEY, or None when it is not set."""
    raw = os.environ.get("MDMM_UPDATE_SIGNING_KEY", "").strip()
    if not raw:
        return None
    if not HEX64_RE.fullmatch(raw):
        raise SystemExit("MDMM_UPDATE_SIGNING_KEY is not 64 hex characters (scripts/release/update_keygen.py)")
    return bytes.fromhex(raw)


def check_key(seed, header=KEY_HEADER):
    """Refuse a key the app would reject; warn while the app has the placeholder."""
    compiled = compiled_public_key(header)
    pub = ed.public_key(seed)
    if compiled is None:
        print("warning: updateKey.h still has the placeholder: the apps will not install these updates "
              "(paste " + pub.hex() + " there)", file=sys.stderr)
    elif compiled != pub:
        raise SystemExit("MDMM_UPDATE_SIGNING_KEY does not belong to the public key in updateKey.h "
                         "(" + compiled.hex() + "); refusing to sign")


def entry(path, version, seed):
    """{name, size, sha256[, sig]} for one file."""
    name = os.path.basename(path)
    digest = sha256_file(path)
    out = {"name": name, "size": os.path.getsize(path), "sha256": digest}
    if seed is not None:
        message = ed.update_message(name, version, digest)
        sig = ed.sign(seed, message)
        if not ed.verify(ed.public_key(seed), message, sig):
            raise SystemExit("a signature did not verify: " + name)
        out["sig"] = base64.b64encode(sig).decode("ascii")
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--version", required=True)
    p.add_argument("files", nargs="+")
    a = p.parse_args()
    if not VERSION_RE.match(a.version):
        raise SystemExit("version must be MAJOR.MINOR.PATCH: " + a.version)
    seed = signing_key()
    if seed is None:
        print("MDMM_UPDATE_SIGNING_KEY not set: not signing", file=sys.stderr)
    else:
        check_key(seed)
    for f in a.files:
        print(json.dumps(entry(f, a.version, seed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
