#!/usr/bin/env python3
"""One-time: make the ed25519 key pair for the in-app updates (doc/modern-ux/DESIGN-updates.md 3.2).

Prints both halves and writes nothing. Run it once, on your own computer:

  python3 scripts/release/update_keygen.py

  1. The PUBLIC key goes into source/elektron/md/mdmmUpdate/updateKey.h (g_updatePublicKeyHex), committed.
  2. The PRIVATE key goes into the GitHub secret MDMM_UPDATE_SIGNING_KEY (Settings > Secrets and variables >
     Actions > New repository secret), and into your password manager. Never into a file in the repository.

Losing the private key means the apps already out there can no longer install updates by themselves (they
still notify and offer the download page); a leaked key means someone could sign an update: in both cases
make a new pair, paste the new public key and release (users of older builds then update by hand once).
"""

import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the repository
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mdmm_ed25519 as ed  # noqa: E402

if len(sys.argv) > 1:
    print(__doc__)
    sys.exit(2)

seed = os.urandom(32)
pub = ed.public_key(seed)
probe = b"mdmm key check"
assert ed.verify(pub, probe, ed.sign(seed, probe))
print("PUBLIC key (paste into source/elektron/md/mdmmUpdate/updateKey.h):")
print("  " + pub.hex())
print()
print("PRIVATE key (GitHub secret MDMM_UPDATE_SIGNING_KEY; keep it nowhere else but your password manager):")
print("  " + seed.hex())
