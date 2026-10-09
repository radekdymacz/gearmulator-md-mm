#!/bin/bash
#
# The two macOS installers (.pkg) and disk images (.dmg) of the editors, from the universal zip that
# build_mdmm.sh made. One script for the release (mdmm-editors-release.yml, signed) and for every push
# (mdmm-editors-macos.yml, ad-hoc signed): what a release ships is made by the code every push tries.
#
#   package_mdmm_installers.sh ZIP OUTPUT_DIR [TAG]
#
# TAG (mdmm-v0.3.5, mdmm-v0.3.5-rc1 ...) gives the installers their numeric version; default 0.0.0.
#
# Signing follows the environment, as the scripts it calls do (doc/release/SIGNING.md): with MDMM_SIGN_IDENTITY
# and friends the bundles are signed with Developer ID, otherwise ad-hoc. MDMM_NOTARIZE=1 (the release, with the
# five secrets) also notarizes and staples the bundles, packages and images; it needs MDMM_NOTARY_KEY_PATH,
# MDMM_NOTARY_KEY_ID and MDMM_NOTARY_ISSUER_ID.
#
# OUTPUT_DIR receives Machinedrum-Editor-macOS.pkg/.dmg and Monomachine-Editor-macOS.pkg/.dmg.
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 ZIP OUTPUT_DIR [TAG]" >&2
  exit 2
fi
zip="$1"
out="$2"
tag="${3:-mdmm-v0.0.0}"
here="$(cd "$(dirname "$0")" && pwd)"

# mdmm-v0.2.0 -> 0.2.0, mdmm-v0.1.0-alpha -> 0.1.0 (Installer versions are numeric).
version="$(printf '%s' "${tag#mdmm-v}" | sed -E 's/^([0-9]+\.[0-9]+\.[0-9]+).*/\1/')"

work="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mdmm-bundles.XXXXXX")"
/usr/bin/ditto -x -k "${zip}" "${work}"
bundles="$(dirname "$(find "${work}" -maxdepth 2 -name 'macsetup_Gearmulator-Elektron.command' -print -quit)")"
mkdir -p "${out}"

"${here}/sign_mdmm.sh" "${bundles}"
if [[ "${MDMM_NOTARIZE:-0}" == 1 ]]; then
  "${here}/notarize_mdmm.sh" --bundles "${bundles}"
fi
"${here}/build_mdmm_pkg.sh" "${bundles}" "${out}" "${version}"
"${here}/build_mdmm_dmg.sh" "${bundles}" "${out}" "${version}"
if [[ "${MDMM_NOTARIZE:-0}" == 1 ]]; then
  "${here}/notarize_mdmm.sh" \
    "${out}/Machinedrum-Editor-macOS.pkg" "${out}/Monomachine-Editor-macOS.pkg" \
    "${out}/Machinedrum-Editor-macOS.dmg" "${out}/Monomachine-Editor-macOS.dmg"
fi
rm -rf "${work}"
ls -l "${out}"
