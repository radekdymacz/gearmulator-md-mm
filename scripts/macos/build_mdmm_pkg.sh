#!/bin/bash
#
# Build the two macOS installer packages for the Machinedrum Editor and the
# Monomachine Editor from already-built bundles.
#
#   build_mdmm_pkg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]
#
# BUNDLE_DIR holds the six bundles as build_mdmm.sh stages them:
#   Machinedrum Editor.app  Machinedrum Editor.vst3  Machinedrum Editor.component
#   Monomachine Editor.app  Monomachine Editor.vst3  Monomachine Editor.component
# (the names come from scripts/mdmm-product.env)
#
# OUTPUT_DIR receives exactly two files, the names the website links to:
#   Machinedrum-Editor-macOS.pkg
#   Monomachine-Editor-macOS.pkg
#
# Each package has three components (standalone app, VST3, AU). Installed for
# all users (needs an administrator) they land in /Applications,
# /Library/Audio/Plug-Ins/VST3 and /Library/Audio/Plug-Ins/Components; with
# "Install for me only" (no administrator needed, e.g. on a managed Mac) in
# ~/Applications, ~/Library/Audio/Plug-Ins/VST3 and
# ~/Library/Audio/Plug-Ins/Components. The payload paths are relative, so the
# domain the person picks decides the root. Firmware is never packaged; the
# script refuses to build if a firmware-like file is found in any bundle.
#
# Upgrades: each component's preinstall (pkg-resources/remove-old-bundles)
# removes the bundles installed under the old names (MDMM_LEGACY_NAME_*:
# Gearmulator MD.vst3 / .component up to 0.3.1), only when they are ours, so a
# DAW does not list the editor twice.
#
# Signing (doc/release/SIGNING.md): the bundles are packaged as they are, so
# sign them first with sign_mdmm.sh. MDMM_INSTALLER_IDENTITY (a "Developer ID
# Installer: ..." identity, optional MDMM_SIGN_KEYCHAIN) signs the product
# archive; without it the packages are unsigned and say so in the log.

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../mdmm-product.env
. "${script_dir}/../mdmm-product.env"
source_dir="$(cd "${script_dir}/../.." && pwd)"
bundle_dir="$(cd "${1:?usage: build_mdmm_pkg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]}" && pwd)"
output_dir_input="${2:?usage: build_mdmm_pkg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]}"
version="${3:-0.3.5}"
resources_src="${script_dir}/pkg-resources"
installer_identity="${MDMM_INSTALLER_IDENTITY:-}"
sign_args=()
if [[ -n "${installer_identity}" ]]; then
  sign_args=(--sign "${installer_identity}" --timestamp)
  if [[ -n "${MDMM_SIGN_KEYCHAIN:-}" ]]; then
    sign_args+=(--keychain "${MDMM_SIGN_KEYCHAIN}")
  fi
fi

if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "VERSION must be MAJOR.MINOR.PATCH, got: ${version}" >&2
  exit 2
fi

mkdir -p "${output_dir_input}"
output_dir="$(cd "${output_dir_input}" && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mdmm-pkg.XXXXXX")"
trap 'rm -rf -- "${work_dir}"' EXIT

# One row per machine: product name (the bundles' and the executables' name) |
# the bundles' name up to 0.3.1 | id suffix | data folder | machine name |
# package file name (the site links to it) | the bundles' identifier
#
# The package identifiers (com.nativekloud.mdmm.<md|mm>[.app|.vst3|.au]) name
# the Installer receipts. They have been the same since 0.1.0 and stay so: a
# new package upgrades the receipts of an earlier one in place. They are not
# the bundles' identifiers (SIGNING.md, "Identifiers").
machines=(
  "${MDMM_PRODUCT_NAME_MD}|${MDMM_LEGACY_NAME_MD}|md|Machinedrum|Machinedrum|Machinedrum-Editor-macOS.pkg|com.nativekloud.machinedrum-editor"
  "${MDMM_PRODUCT_NAME_MM}|${MDMM_LEGACY_NAME_MM}|mm|Monomachine|Monomachine|Monomachine-Editor-macOS.pkg|com.nativekloud.monomachine-editor"
)

require_bundle() {
  if [[ ! -d "$1" ]]; then
    echo "Missing bundle: $1" >&2
    exit 3
  fi
}

# Every bundle must carry the editor's own identifier, never upstream
# Gearmulator's local.gearmulator.preview.* one.
require_bundle_id() {
  local bundle="$1" expected="$2" actual
  actual="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${bundle}/Contents/Info.plist")"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "Wrong bundle identifier on ${bundle}: ${actual} (expected ${expected})" >&2
    exit 8
  fi
}

# Every bundle must be free of firmware and private runtime material.
refuse_firmware() {
  if find "$1" -type f \( -iname '*.bin' -o -iname '*.rom' -o -iname '*.nvram' \
      -o -iname '*.syx' -o -iname '*.mdpd' -o -iname '*.cache' \) -print -quit | grep -q .; then
    echo "Firmware-like file found in $1; refusing to package it" >&2
    exit 5
  fi
}

# The binary's architectures decide the package's hostArchitectures.
bundle_architectures() {
  local bundle="$1" name="$2"
  lipo -archs "${bundle}/Contents/MacOS/${name}" | tr ' ' ','
}

# A component plist that pins the bundle to its install location. Without
# BundleIsRelocatable=false, Installer would "upgrade" any bundle with the same
# identifier wherever it finds one (a build tree, a user's ~/Library copy).
write_component_plist() {
  local plist="$1" relative_path="$2"
  cat > "${plist}" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
  <dict>
    <key>RootRelativeBundlePath</key>
    <string>${relative_path}</string>
    <key>BundleIsRelocatable</key>
    <false/>
    <key>BundleIsVersionChecked</key>
    <false/>
    <key>BundleHasStrictIdentifier</key>
    <false/>
    <key>BundleOverwriteAction</key>
    <string>upgrade</string>
  </dict>
</array>
</plist>
PLIST
}

# Stage one bundle under ROOT/INSTALL_SUBDIR/NAME, without extended
# attributes (quarantine, provenance) from the build machine.
stage_bundle() {
  local source="$1" root="$2" subdir="$3" name="$4"
  mkdir -p "${root}/${subdir}"
  /usr/bin/ditto --noextattr --noqtn "${source}" "${root}/${subdir}/${name}"
  codesign --verify --deep --strict "${root}/${subdir}/${name}"
}

# Replace {{KEY}} placeholders in a resource template.
render() {
  local template="$1" output="$2" machine="$3" app_name="$4" data_folder="$5" old_name="$6"
  sed -e "s|{{MACHINE}}|${machine}|g" \
      -e "s|{{APP_NAME}}|${app_name}|g" \
      -e "s|{{DATA_FOLDER}}|${data_folder}|g" \
      -e "s|{{OLD_NAME}}|${old_name}|g" \
      -e "s|{{VERSION}}|${version}|g" \
      "${template}" > "${output}"
}

# A component's scripts folder with the preinstall that removes the bundle
# 0.3.1 and earlier installed at OLD_PATH (relative to the install root) before
# NEW_PATH is installed.
write_preinstall() {
  local scripts="$1" old_path="$2" new_path="$3"
  mkdir -p "${scripts}"
  sed -e "s|{{APP_NAME}}|${app_name}|g" \
      -e "s|{{OLD_NAME}}|${old_name}|g" \
      -e "s|{{OLD_PATH}}|${old_path}|g" \
      -e "s|{{NEW_PATH}}|${new_path}|g" \
      -e "s|{{BUNDLE_ID}}|${bundle_id}|g" \
      -e "s|{{RECEIPT_ID}}|${receipt_id}|g" \
      "${resources_src}/remove-old-bundles" > "${scripts}/preinstall"
  chmod 755 "${scripts}/preinstall"
  /bin/sh -n "${scripts}/preinstall"
}

# Never rewrite the permissions of directories that already exist on the
# target (/Applications, /Library/Audio/Plug-Ins/VST3 and so on).
package_info="${work_dir}/PackageInfo.template"
echo '<pkg-info overwrite-permissions="false" relocatable="false"/>' > "${package_info}"

for row in "${machines[@]}"; do
  IFS='|' read -r app_name old_name id_suffix data_folder machine pkg_name bundle_id <<< "${row}"
  identifier="com.nativekloud.mdmm.${id_suffix}"
  app="${bundle_dir}/${app_name}.app"
  vst3="${bundle_dir}/${app_name}.vst3"
  au="${bundle_dir}/${app_name}.component"
  for bundle in "${app}" "${vst3}" "${au}"; do
    require_bundle "${bundle}"
    require_bundle_id "${bundle}" "${bundle_id}"
    refuse_firmware "${bundle}"
  done

  archs="$(bundle_architectures "${app}" "${app_name}")"
  machine_dir="${work_dir}/${id_suffix}"
  components="${machine_dir}/components"
  resources="${machine_dir}/resources"
  mkdir -p "${components}" "${resources}"

  # Component 1: the standalone app. It was built under the old name and
  # only renamed here up to 0.3.1, so an installed one may hold the old
  # executable: its preinstall replaces it whole.
  stage_bundle "${app}" "${machine_dir}/root-app" "Applications" "${app_name}.app"
  write_component_plist "${machine_dir}/app.plist" "Applications/${app_name}.app"
  receipt_id="${identifier}.app"
  write_preinstall "${machine_dir}/app-scripts" "Applications/${app_name}.app" "Applications/${app_name}.app"
  pkgbuild --root "${machine_dir}/root-app" \
    --component-plist "${machine_dir}/app.plist" \
    --scripts "${machine_dir}/app-scripts" \
    --identifier "${identifier}.app" --version "${version}" \
    --install-location / --ownership recommended --info "${package_info}" \
    "${components}/${id_suffix}-app.pkg"

  # Component 2: the VST3, with a preinstall that removes the old-named one.
  stage_bundle "${vst3}" "${machine_dir}/root-vst3" "Library/Audio/Plug-Ins/VST3" "${app_name}.vst3"
  write_component_plist "${machine_dir}/vst3.plist" "Library/Audio/Plug-Ins/VST3/${app_name}.vst3"
  receipt_id="${identifier}.vst3"
  write_preinstall "${machine_dir}/vst3-scripts" "Library/Audio/Plug-Ins/VST3/${old_name}.vst3" \
    "Library/Audio/Plug-Ins/VST3/${app_name}.vst3"
  pkgbuild --root "${machine_dir}/root-vst3" \
    --component-plist "${machine_dir}/vst3.plist" \
    --scripts "${machine_dir}/vst3-scripts" \
    --identifier "${identifier}.vst3" --version "${version}" \
    --install-location / --ownership recommended --info "${package_info}" \
    "${components}/${id_suffix}-vst3.pkg"

  # Component 3: the AU, with a preinstall that removes the old-named one and
  # a postinstall that makes hosts rescan.
  stage_bundle "${au}" "${machine_dir}/root-au" "Library/Audio/Plug-Ins/Components" "${app_name}.component"
  write_component_plist "${machine_dir}/au.plist" "Library/Audio/Plug-Ins/Components/${app_name}.component"
  receipt_id="${identifier}.au"
  write_preinstall "${machine_dir}/au-scripts" "Library/Audio/Plug-Ins/Components/${old_name}.component" \
    "Library/Audio/Plug-Ins/Components/${app_name}.component"
  /usr/bin/ditto "${resources_src}/au-postinstall" "${machine_dir}/au-scripts/postinstall"
  chmod 755 "${machine_dir}/au-scripts/postinstall"
  pkgbuild --root "${machine_dir}/root-au" \
    --component-plist "${machine_dir}/au.plist" \
    --scripts "${machine_dir}/au-scripts" \
    --identifier "${identifier}.au" --version "${version}" \
    --install-location / --ownership recommended --info "${package_info}" \
    "${components}/${id_suffix}-au.pkg"

  # Installer pages: welcome (what, where the ROM goes), readme (Gatekeeper,
  # removal), licence (GPL-3 with the notice first).
  render "${resources_src}/welcome.html" "${resources}/welcome.html" \
    "${machine}" "${app_name}" "${data_folder}" "${old_name}"
  render "${resources_src}/readme.html" "${resources}/readme.html" \
    "${machine}" "${app_name}" "${data_folder}" "${old_name}"
  { cat "${resources_src}/license-preamble.txt"; echo; cat "${source_dir}/LICENSE.md"; } \
    > "${resources}/license.txt"

  cat > "${machine_dir}/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>${app_name} ${version}</title>
  <product id="${identifier}" version="${version}"/>
  <welcome file="welcome.html" mime-type="text/html"/>
  <readme file="readme.html" mime-type="text/html"/>
  <license file="license.txt" mime-type="text/plain"/>
  <options customize="allow" require-scripts="false" hostArchitectures="${archs}"/>
  <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="true"/>
  <volume-check>
    <allowed-os-versions>
      <os-version min="12.0"/>
    </allowed-os-versions>
  </volume-check>
  <choices-outline>
    <line choice="app"/>
    <line choice="vst3"/>
    <line choice="au"/>
  </choices-outline>
  <choice id="app" title="${app_name} (standalone app)"
          description="Installs ${app_name}.app into Applications (/Applications, or ~/Applications when installed for you only).">
    <pkg-ref id="${identifier}.app"/>
  </choice>
  <choice id="vst3" title="VST3 plug-in"
          description="Installs ${app_name}.vst3 into Library/Audio/Plug-Ins/VST3 (for all users, or in your home folder when installed for you only).">
    <pkg-ref id="${identifier}.vst3"/>
  </choice>
  <choice id="au" title="Audio Unit plug-in"
          description="Installs ${app_name}.component into Library/Audio/Plug-Ins/Components (for all users, or in your home folder when installed for you only).">
    <pkg-ref id="${identifier}.au"/>
  </choice>
  <pkg-ref id="${identifier}.app" version="${version}">${id_suffix}-app.pkg</pkg-ref>
  <pkg-ref id="${identifier}.vst3" version="${version}">${id_suffix}-vst3.pkg</pkg-ref>
  <pkg-ref id="${identifier}.au" version="${version}">${id_suffix}-au.pkg</pkg-ref>
</installer-gui-script>
XML

  rm -f "${output_dir}/${pkg_name}"
  productbuild --distribution "${machine_dir}/distribution.xml" \
    --resources "${resources}" \
    --package-path "${components}" \
    ${sign_args[@]+"${sign_args[@]}"} \
    "${output_dir}/${pkg_name}"
  if [[ -n "${installer_identity}" ]]; then
    signature="$(pkgutil --check-signature "${output_dir}/${pkg_name}")"
    echo "${signature}"
    if [[ "${signature}" != *"Developer ID Installer:"* ]]; then
      echo "Package is not signed with a Developer ID Installer certificate" >&2
      exit 7
    fi
  else
    echo "note: ${pkg_name} is UNSIGNED (no MDMM_INSTALLER_IDENTITY)"
  fi

  # Self-check: the product holds exactly the three payloads, at the right
  # paths, with no firmware.
  expanded="${machine_dir}/expanded"
  pkgutil --expand-full "${output_dir}/${pkg_name}" "${expanded}"
  for expected in \
      "${id_suffix}-app.pkg/Payload/Applications/${app_name}.app/Contents/MacOS/${app_name}" \
      "${id_suffix}-vst3.pkg/Payload/Library/Audio/Plug-Ins/VST3/${app_name}.vst3/Contents/MacOS/${app_name}" \
      "${id_suffix}-au.pkg/Payload/Library/Audio/Plug-Ins/Components/${app_name}.component/Contents/MacOS/${app_name}" \
      "${id_suffix}-app.pkg/Scripts/preinstall" \
      "${id_suffix}-vst3.pkg/Scripts/preinstall" \
      "${id_suffix}-au.pkg/Scripts/preinstall" \
      "${id_suffix}-au.pkg/Scripts/postinstall"; do
    if [[ ! -f "${expanded}/${expected}" ]]; then
      echo "Package self-check failed, missing: ${expected}" >&2
      exit 6
    fi
  done
  refuse_firmware "${expanded}"

  echo "MDMM_PKG=${output_dir}/${pkg_name}"
done

(cd "${output_dir}" && env LC_ALL=C LANG=C /usr/bin/shasum -a 256 \
  Machinedrum-Editor-macOS.pkg Monomachine-Editor-macOS.pkg)
