#!/bin/bash
#
# Render the preinstall that removes the bundles an old version of the editor left behind (see
# pkg-resources/remove-old-bundles for what it removes and why).
#
#   render_remove_old_bundles.sh md|mm app|vst3|au OUTPUT_FILE
#
# build_mdmm_pkg.sh calls it for each component of each package; scripts/release/test_release_scripts.py calls it
# with stand-ins for the macOS tools and runs the result against a temporary folder. Everything comes from
# scripts/mdmm-product.env: the product and the old names, the two bundle identifiers.
#
# MDMM_RENDER_PLISTBUDDY, _PKGUTIL, _STAT and _DSCL replace the absolute paths of the macOS tools the script
# calls when it runs on a Mac (for the test only; the packaging never sets them).

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../mdmm-product.env
. "${script_dir}/../mdmm-product.env"

machine="${1:?usage: render_remove_old_bundles.sh md|mm app|vst3|au OUTPUT_FILE}"
kind="${2:?usage: render_remove_old_bundles.sh md|mm app|vst3|au OUTPUT_FILE}"
output="${3:?usage: render_remove_old_bundles.sh md|mm app|vst3|au OUTPUT_FILE}"
template="${script_dir}/pkg-resources/remove-old-bundles"

case "${machine}" in
  md) app_name="${MDMM_PRODUCT_NAME_MD}"; old_name="${MDMM_LEGACY_NAME_MD}"
      bundle_id="${MDMM_BUNDLE_ID_MD}"; old_bundle_id="${MDMM_LEGACY_BUNDLE_ID_MD}" ;;
  mm) app_name="${MDMM_PRODUCT_NAME_MM}"; old_name="${MDMM_LEGACY_NAME_MM}"
      bundle_id="${MDMM_BUNDLE_ID_MM}"; old_bundle_id="${MDMM_LEGACY_BUNDLE_ID_MM}" ;;
  *) echo "unknown machine: ${machine} (use md or mm)" >&2; exit 2 ;;
esac
# The package identifiers (the Installer receipts' names) are not the bundles': build_mdmm_pkg.sh.
identifier="com.nativekloud.mdmm.${machine}"

# old_id_counts: whether a bundle at the old name that carries upstream Gearmulator's identifier is ours. For the
# plug-ins it is (same plug-in codes: a DAW would clash anyway); for the app it is not (a copy of upstream's own).
case "${kind}" in
  app)  kind_text="standalone app"; extension="app"; folder="Applications"; old_id_counts=0 ;;
  vst3) kind_text="VST3"; extension="vst3"; folder="Library/Audio/Plug-Ins/VST3"; old_id_counts=1 ;;
  au)   kind_text="Audio Unit"; extension="component"; folder="Library/Audio/Plug-Ins/Components"; old_id_counts=1 ;;
  *) echo "unknown component: ${kind} (use app, vst3 or au)" >&2; exit 2 ;;
esac
old_path="${folder}/${old_name}.${extension}"
new_path="${folder}/${app_name}.${extension}"
receipt_id="${identifier}.${kind}"

# Only plain words go into the script: it runs as root on other people's Macs.
name_re='^[A-Za-z0-9][A-Za-z0-9 ._/+-]*$'
id_re='^[A-Za-z0-9][A-Za-z0-9._-]*$'
for value in "${app_name}" "${old_name}" "${old_path}" "${new_path}"; do
  if [[ ! "${value}" =~ ${name_re} ]]; then
    echo "refusing a name with other characters than letters, digits, space and ._/+-: ${value}" >&2
    exit 3
  fi
done
for value in "${bundle_id}" "${old_bundle_id}" "${receipt_id}"; do
  if [[ ! "${value}" =~ ${id_re} ]]; then
    echo "refusing an identifier with other characters than letters, digits and ._-: ${value}" >&2
    exit 3
  fi
done
if [[ "${old_name}" == "${app_name}" || "${old_bundle_id}" == "${bundle_id}" ]]; then
  echo "the old name or identifier equals the new one; nothing to remove" >&2
  exit 3
fi

plistbuddy="${MDMM_RENDER_PLISTBUDDY:-/usr/libexec/PlistBuddy}"
pkgutil="${MDMM_RENDER_PKGUTIL:-/usr/sbin/pkgutil}"
stat_tool="${MDMM_RENDER_STAT:-/usr/bin/stat}"
dscl_tool="${MDMM_RENDER_DSCL:-/usr/bin/dscl}"

# The values are checked above; the tool paths are the caller's (the packaging's own, or the test's).
sed -e "s|{{KIND}}|${kind_text}|g" \
    -e "s|{{APP_NAME}}|${app_name}|g" \
    -e "s|{{OLD_NAME}}|${old_name}|g" \
    -e "s|{{OLD_PATH}}|${old_path}|g" \
    -e "s|{{NEW_PATH}}|${new_path}|g" \
    -e "s|{{BUNDLE_ID}}|${bundle_id}|g" \
    -e "s|{{OLD_BUNDLE_ID}}|${old_bundle_id}|g" \
    -e "s|{{OLD_ID_COUNTS}}|${old_id_counts}|g" \
    -e "s|{{RECEIPT_ID}}|${receipt_id}|g" \
    -e "s|{{PLISTBUDDY}}|${plistbuddy}|g" \
    -e "s|{{PKGUTIL}}|${pkgutil}|g" \
    -e "s|{{STAT}}|${stat_tool}|g" \
    -e "s|{{DSCL}}|${dscl_tool}|g" \
    "${template}" > "${output}" || exit 4
if grep -q '{{[A-Z_]*}}' "${output}"; then
  echo "an unfilled {{...}} is left in ${output}" >&2
  exit 4
fi
chmod 755 "${output}"
/bin/sh -n "${output}"
