#!/bin/bash
#
# Notarize with Apple and staple the ticket.
#
#   notarize_mdmm.sh FILE.pkg|FILE.dmg ...   submit each file, staple it
#   notarize_mdmm.sh --bundles BUNDLE_DIR    submit the six signed bundles in
#                                            one zip, then staple each bundle
#
# Stapling the bundles too means a copy dragged out of the disk image, or a
# plug-in a DAW loads offline, still carries its own ticket.
#
# Environment (an App Store Connect API key, doc/release/SIGNING.md):
#   MDMM_NOTARY_KEY_PATH   path to the AuthKey_XXXXXXXXXX.p8 file
#   MDMM_NOTARY_KEY_ID     the key ID
#   MDMM_NOTARY_ISSUER_ID  the issuer ID (a UUID)
#
# Fails on anything but status "Accepted", printing Apple's log.

set -euo pipefail

# shellcheck source=../mdmm-product.env
. "$(cd "$(dirname "$0")" && pwd)/../mdmm-product.env"
key_path="${MDMM_NOTARY_KEY_PATH:?MDMM_NOTARY_KEY_PATH is required}"
key_id="${MDMM_NOTARY_KEY_ID:?MDMM_NOTARY_KEY_ID is required}"
issuer_id="${MDMM_NOTARY_ISSUER_ID:?MDMM_NOTARY_ISSUER_ID is required}"
if [[ ! -f "${key_path}" ]]; then
  echo "Notary API key file not found: ${key_path}" >&2
  exit 2
fi
auth=(--key "${key_path}" --key-id "${key_id}" --issuer "${issuer_id}")

json_field() {
  python3 -c 'import json, sys; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$1"
}

# Submit one file and wait. Prints nothing on success, fails otherwise.
submit() {
  local file="$1" result id status rc=0
  echo "Notarizing $(basename "${file}")"
  result="$(xcrun notarytool submit "${file}" "${auth[@]}" --wait --timeout 2h \
    --output-format json)" || rc=$?
  id="$(printf '%s' "${result}" | json_field id 2>/dev/null || true)"
  status="$(printf '%s' "${result}" | json_field status 2>/dev/null || true)"
  if [[ ${rc} -ne 0 && -z "${status}" ]]; then
    echo "notarytool submit failed (exit ${rc}) for ${file}: ${result:-no output}" >&2
    return 1
  fi
  echo "  submission ${id}: ${status}"
  if [[ "${status}" != "Accepted" ]]; then
    if [[ -n "${id}" ]]; then
      xcrun notarytool log "${id}" "${auth[@]}" >&2 || true
    fi
    echo "Notarization did not succeed for ${file} (status: ${status:-unknown})" >&2
    return 1
  fi
  # Even an accepted submission can carry warnings; keep them in the CI log.
  xcrun notarytool log "${id}" "${auth[@]}" || true
}

staple() {
  xcrun stapler staple "$1"
  xcrun stapler validate "$1"
}

if [[ "${1:-}" == "--bundles" ]]; then
  bundle_dir="$(cd "${2:?usage: notarize_mdmm.sh --bundles BUNDLE_DIR}" && pwd)"
  bundles=()
  for stem in "${MDMM_PRODUCT_NAME_MD}" "${MDMM_PRODUCT_NAME_MM}"; do
    for ext in app vst3 component; do
      bundles+=("${bundle_dir}/${stem}.${ext}")
    done
  done
  work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mdmm-notarize.XXXXXX")"
  trap 'rm -rf -- "${work_dir}"' EXIT
  mkdir -p "${work_dir}/MD MM Editors"
  for bundle in "${bundles[@]}"; do
    /usr/bin/ditto "${bundle}" "${work_dir}/MD MM Editors/$(basename "${bundle}")"
  done
  /usr/bin/ditto -c -k --sequesterRsrc --keepParent "${work_dir}/MD MM Editors" \
    "${work_dir}/mdmm-bundles.zip"
  submit "${work_dir}/mdmm-bundles.zip"
  for bundle in "${bundles[@]}"; do
    staple "${bundle}"
  done
  exit 0
fi

if [[ $# -eq 0 ]]; then
  echo "usage: notarize_mdmm.sh FILE.pkg|FILE.dmg ... | --bundles BUNDLE_DIR" >&2
  exit 2
fi
for file in "$@"; do
  case "${file}" in
    *.pkg|*.dmg) ;;
    *) echo "Only .pkg and .dmg files are submitted directly: ${file}" >&2; exit 2 ;;
  esac
  submit "${file}"
  staple "${file}"
done
