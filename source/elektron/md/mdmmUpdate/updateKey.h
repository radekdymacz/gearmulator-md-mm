#pragma once

// The public half of the update signing key (doc/modern-ux/DESIGN-updates.md 3.2): 64 hex characters.
//
// PLACEHOLDER. While this is not 64 hex characters the editors check for updates and say so, but never
// install one (their button opens the download page instead). To turn installs on:
//   1. python3 scripts/release/update_keygen.py          (once, on your own computer)
//   2. paste the PUBLIC key it prints below, commit;
//   3. save the PRIVATE key as the GitHub secret MDMM_UPDATE_SIGNING_KEY.
// scripts/release/sign_update.py reads this line and refuses a signing key that does not belong to it.

namespace mdmmUpdate
{
	inline constexpr const char* g_updatePublicKeyHex = "PLACEHOLDER-run-scripts/release/update_keygen.py";
}
