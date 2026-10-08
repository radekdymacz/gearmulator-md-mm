# Monocypher (vendored)

- What: Monocypher 4.0.2 by Loup Vaillant, Michael Savage and Fabio Scotoni, https://monocypher.org
- From: https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.2.tar.gz (`src/monocypher.c`, `src/monocypher.h`,
  `src/optional/monocypher-ed25519.c`, `src/optional/monocypher-ed25519.h`, `LICENCE.md`), unmodified.
- Licence: dual BSD-2-Clause or CC0-1.0 (`LICENCE.md`), at the user's choice. Both are compatible with the GPL-3 this
  program is under. We take it under CC0 (no conditions); `LICENCE.md` stays beside the files, with both texts and the
  copyright lines, and the editors' README lists it under third-party code.
- Used for: `crypto_ed25519_check` (RFC 8032 ed25519 with SHA-512) to verify update signatures
  (`../updateCore.cpp`, doc/modern-ux/DESIGN-updates.md). Nothing else is called.
- Update: replace the five files from a newer tag, run `mdmmUpdateTest`.
