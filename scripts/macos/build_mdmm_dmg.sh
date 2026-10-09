#!/bin/bash
#
# Build the two drag-and-drop disk images, the alternative to the installer
# packages, from already-signed bundles.
#
#   build_mdmm_dmg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]
#
# BUNDLE_DIR holds the six bundles as build_mdmm.sh stages them (sign them
# first with sign_mdmm.sh, and staple them with notarize_mdmm.sh --bundles for
# a release). OUTPUT_DIR receives:
#   Machinedrum-Editor-macOS.dmg
#   Monomachine-Editor-macOS.dmg
#
# Each image holds the app, the VST3 and the AU (named for the product,
# scripts/mdmm-product.env), three Finder links to drop them on (/Applications,
# /Library/Audio/Plug-Ins/VST3, /Library/Audio/Plug-Ins/Components) and
# Install.txt and LICENSE.txt (the GPL). Plain hdiutil, no third-party tools, no Finder scripting (it
# needs a logged-in GUI session, which CI runners do not reliably have).
#
# MDMM_DMG_SIGN_IDENTITY (a "Developer ID Application: ..." identity, optional
# MDMM_SIGN_KEYCHAIN) signs the image itself; without it the image is unsigned.
# Firmware is never included.

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../mdmm-product.env
. "${script_dir}/../mdmm-product.env"
bundle_dir="$(cd "${1:?usage: build_mdmm_dmg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]}" && pwd)"
output_dir_input="${2:?usage: build_mdmm_dmg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]}"
version="${3:-0.3.5}"
identity="${MDMM_DMG_SIGN_IDENTITY:-}"
keychain="${MDMM_SIGN_KEYCHAIN:-}"
install_template="${script_dir}/pkg-resources/dmg-install.txt"
license_preamble="${script_dir}/pkg-resources/license-preamble.txt"
license_md="${script_dir}/../../LICENSE.md"

if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "VERSION must be MAJOR.MINOR.PATCH, got: ${version}" >&2
  exit 2
fi
mkdir -p "${output_dir_input}"
output_dir="$(cd "${output_dir_input}" && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mdmm-dmg.XXXXXX")"
mount_point=""
cleanup() {
  if [[ -n "${mount_point}" ]]; then
    hdiutil detach -quiet -force "${mount_point}" || true
  fi
  rm -rf -- "${work_dir}"
}
trap cleanup EXIT

# product name (the bundles' name) | the name up to 0.3.1 | machine | data folder |
# image file name (the site links to it)
machines=(
  "${MDMM_PRODUCT_NAME_MD}|${MDMM_LEGACY_NAME_MD}|Machinedrum|Machinedrum|Machinedrum-Editor-macOS.dmg"
  "${MDMM_PRODUCT_NAME_MM}|${MDMM_LEGACY_NAME_MM}|Monomachine|Monomachine|Monomachine-Editor-macOS.dmg"
)

refuse_firmware() {
  if find "$1" -type f \( -iname '*.bin' -o -iname '*.rom' -o -iname '*.nvram' \
      -o -iname '*.syx' -o -iname '*.mdpd' -o -iname '*.cache' \) -print -quit | grep -q .; then
    echo "Firmware-like file found in $1; refusing to package it" >&2
    exit 5
  fi
}

# The link names Finder shows; each points at the folder its bundle goes to.
link_apps="Applications"
link_vst3="VST3 Plug-Ins"
link_au="Audio Unit Plug-Ins"

for row in "${machines[@]}"; do
  IFS='|' read -r app_name old_name machine data_folder dmg_name <<< "${row}"
  staging="${work_dir}/${app_name}"
  mkdir -p "${staging}"
  for ext in app vst3 component; do
    if [[ ! -d "${bundle_dir}/${app_name}.${ext}" ]]; then
      echo "Missing bundle: ${bundle_dir}/${app_name}.${ext}" >&2
      exit 3
    fi
    refuse_firmware "${bundle_dir}/${app_name}.${ext}"
  done

  /usr/bin/ditto --noextattr --noqtn "${bundle_dir}/${app_name}.app" "${staging}/${app_name}.app"
  /usr/bin/ditto --noextattr --noqtn "${bundle_dir}/${app_name}.vst3" "${staging}/${app_name}.vst3"
  /usr/bin/ditto --noextattr --noqtn "${bundle_dir}/${app_name}.component" "${staging}/${app_name}.component"
  ln -s /Applications "${staging}/${link_apps}"
  ln -s /Library/Audio/Plug-Ins/VST3 "${staging}/${link_vst3}"
  ln -s /Library/Audio/Plug-Ins/Components "${staging}/${link_au}"
  sed -e "s|{{MACHINE}}|${machine}|g" \
      -e "s|{{APP_NAME}}|${app_name}|g" \
      -e "s|{{DATA_FOLDER}}|${data_folder}|g" \
      -e "s|{{OLD_NAME}}|${old_name}|g" \
      -e "s|{{VERSION}}|${version}|g" \
      -e "s|{{LINK_APPS}}|${link_apps}|g" \
      -e "s|{{LINK_VST3}}|${link_vst3}|g" \
      -e "s|{{LINK_AU}}|${link_au}|g" \
      "${install_template}" > "${staging}/Install.txt"
  # GPL-3: the licence travels with the binaries, preamble first, as in the package
  { cat "${license_preamble}"; echo; cat "${license_md}"; } > "${staging}/LICENSE.txt"
  for bundle in "${staging}/${app_name}.app" "${staging}/${app_name}.vst3" "${staging}/${app_name}.component"; do
    codesign --verify --strict --deep "${bundle}"
  done

  dmg="${output_dir}/${dmg_name}"
  rm -f "${dmg}"
  hdiutil create -quiet -volname "${app_name} ${version}" -srcfolder "${staging}" \
    -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "${dmg}"

  if [[ -n "${identity}" ]]; then
    sign_args=(--force --timestamp --sign "${identity}")
    if [[ -n "${keychain}" ]]; then
      sign_args+=(--keychain "${keychain}")
    fi
    codesign "${sign_args[@]}" "${dmg}"
    codesign --verify --strict "${dmg}"
  fi

  # Self-check: mount read-only and look at what a person will see.
  mount_point="${work_dir}/mount-${app_name// /-}"
  mkdir -p "${mount_point}"
  hdiutil attach -quiet -readonly -nobrowse -noautoopen -mountpoint "${mount_point}" "${dmg}"
  for expected in "${app_name}.app/Contents/MacOS/${app_name}" \
      "${app_name}.vst3/Contents/MacOS/${app_name}" \
      "${app_name}.component/Contents/MacOS/${app_name}" "Install.txt" "LICENSE.txt"; do
    if [[ ! -f "${mount_point}/${expected}" ]]; then
      echo "Disk image self-check failed, missing: ${expected}" >&2
      exit 6
    fi
  done
  for link in "${link_apps}|/Applications" "${link_vst3}|/Library/Audio/Plug-Ins/VST3" \
      "${link_au}|/Library/Audio/Plug-Ins/Components"; do
    if [[ "$(readlink "${mount_point}/${link%%|*}")" != "${link#*|}" ]]; then
      echo "Disk image self-check failed, bad link: ${link%%|*}" >&2
      exit 6
    fi
  done
  for bundle in "${mount_point}/${app_name}.app" "${mount_point}/${app_name}.vst3" \
      "${mount_point}/${app_name}.component"; do
    codesign --verify --strict --deep "${bundle}"
  done
  refuse_firmware "${mount_point}"
  hdiutil detach -quiet "${mount_point}"
  mount_point=""

  echo "MDMM_DMG=${dmg}"
done

(cd "${output_dir}" && env LC_ALL=C LANG=C /usr/bin/shasum -a 256 \
  Machinedrum-Editor-macOS.dmg Monomachine-Editor-macOS.dmg)
