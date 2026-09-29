#!/bin/bash
#
# Build the two macOS installer packages for the Machinedrum Editor and the
# Monomachine Editor from already-built bundles.
#
#   build_mdmm_pkg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]
#
# BUNDLE_DIR holds the six bundles as build_mdmm.sh stages them:
#   Gearmulator MD.app  Gearmulator MD.vst3  Gearmulator MD.component
#   Gearmulator MM.app  Gearmulator MM.vst3  Gearmulator MM.component
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
# domain the person picks decides the root. The packages are unsigned: there is no
# Apple Developer ID yet. Firmware is never packaged; the script refuses to
# build if a firmware-like file is found in any bundle.

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
source_dir="$(cd "${script_dir}/../.." && pwd)"
bundle_dir="$(cd "${1:?usage: build_mdmm_pkg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]}" && pwd)"
output_dir_input="${2:?usage: build_mdmm_pkg.sh BUNDLE_DIR OUTPUT_DIR [VERSION]}"
version="${3:-0.2.1}"
resources_src="${script_dir}/pkg-resources"

if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "VERSION must be MAJOR.MINOR.PATCH, got: ${version}" >&2
  exit 2
fi

mkdir -p "${output_dir_input}"
output_dir="$(cd "${output_dir_input}" && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mdmm-pkg.XXXXXX")"
trap 'rm -rf -- "${work_dir}"' EXIT

# One row per machine: bundle stem | app name in /Applications | id suffix |
# data folder | machine name | package file name
machines=(
  "Gearmulator MD|Machinedrum Editor|md|Machinedrum|Machinedrum|Machinedrum-Editor-macOS.pkg"
  "Gearmulator MM|Monomachine Editor|mm|Monomachine|Monomachine|Monomachine-Editor-macOS.pkg"
)

require_bundle() {
  if [[ ! -d "$1" ]]; then
    echo "Missing bundle: $1" >&2
    exit 3
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
  local bundle="$1" stem="$2"
  lipo -archs "${bundle}/Contents/MacOS/${stem}" | tr ' ' ','
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
  local template="$1" output="$2" machine="$3" app_name="$4" data_folder="$5" stem="$6"
  sed -e "s|{{MACHINE}}|${machine}|g" \
      -e "s|{{APP_NAME}}|${app_name}|g" \
      -e "s|{{DATA_FOLDER}}|${data_folder}|g" \
      -e "s|{{STEM}}|${stem}|g" \
      -e "s|{{VERSION}}|${version}|g" \
      "${template}" > "${output}"
}

# Never rewrite the permissions of directories that already exist on the
# target (/Applications, /Library/Audio/Plug-Ins/VST3 and so on).
package_info="${work_dir}/PackageInfo.template"
echo '<pkg-info overwrite-permissions="false" relocatable="false"/>' > "${package_info}"

for row in "${machines[@]}"; do
  IFS='|' read -r stem app_name id_suffix data_folder machine pkg_name <<< "${row}"
  identifier="com.nativekloud.mdmm.${id_suffix}"
  app="${bundle_dir}/${stem}.app"
  vst3="${bundle_dir}/${stem}.vst3"
  au="${bundle_dir}/${stem}.component"
  for bundle in "${app}" "${vst3}" "${au}"; do
    require_bundle "${bundle}"
    refuse_firmware "${bundle}"
  done

  archs="$(bundle_architectures "${app}" "${stem}")"
  machine_dir="${work_dir}/${id_suffix}"
  components="${machine_dir}/components"
  resources="${machine_dir}/resources"
  mkdir -p "${components}" "${resources}"

  # Component 1: the standalone app, renamed to the product name. Only the
  # folder name changes; the executable and Info.plist stay as built, so the
  # ad-hoc signature stays valid.
  stage_bundle "${app}" "${machine_dir}/root-app" "Applications" "${app_name}.app"
  write_component_plist "${machine_dir}/app.plist" "Applications/${app_name}.app"
  pkgbuild --root "${machine_dir}/root-app" \
    --component-plist "${machine_dir}/app.plist" \
    --identifier "${identifier}.app" --version "${version}" \
    --install-location / --ownership recommended --info "${package_info}" \
    "${components}/${id_suffix}-app.pkg"

  # Component 2: the VST3.
  stage_bundle "${vst3}" "${machine_dir}/root-vst3" "Library/Audio/Plug-Ins/VST3" "${stem}.vst3"
  write_component_plist "${machine_dir}/vst3.plist" "Library/Audio/Plug-Ins/VST3/${stem}.vst3"
  pkgbuild --root "${machine_dir}/root-vst3" \
    --component-plist "${machine_dir}/vst3.plist" \
    --identifier "${identifier}.vst3" --version "${version}" \
    --install-location / --ownership recommended --info "${package_info}" \
    "${components}/${id_suffix}-vst3.pkg"

  # Component 3: the AU, with a postinstall that makes hosts rescan.
  stage_bundle "${au}" "${machine_dir}/root-au" "Library/Audio/Plug-Ins/Components" "${stem}.component"
  write_component_plist "${machine_dir}/au.plist" "Library/Audio/Plug-Ins/Components/${stem}.component"
  mkdir -p "${machine_dir}/au-scripts"
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
    "${machine}" "${app_name}" "${data_folder}" "${stem}"
  render "${resources_src}/readme.html" "${resources}/readme.html" \
    "${machine}" "${app_name}" "${data_folder}" "${stem}"
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
      <os-version min="10.13"/>
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
          description="Installs ${stem}.vst3 into Library/Audio/Plug-Ins/VST3 (for all users, or in your home folder when installed for you only).">
    <pkg-ref id="${identifier}.vst3"/>
  </choice>
  <choice id="au" title="Audio Unit plug-in"
          description="Installs ${stem}.component into Library/Audio/Plug-Ins/Components (for all users, or in your home folder when installed for you only).">
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
    "${output_dir}/${pkg_name}"

  # Self-check: the product holds exactly the three payloads, at the right
  # paths, with no firmware.
  expanded="${machine_dir}/expanded"
  pkgutil --expand-full "${output_dir}/${pkg_name}" "${expanded}"
  for expected in \
      "${id_suffix}-app.pkg/Payload/Applications/${app_name}.app/Contents/MacOS/${stem}" \
      "${id_suffix}-vst3.pkg/Payload/Library/Audio/Plug-Ins/VST3/${stem}.vst3/Contents/MacOS/${stem}" \
      "${id_suffix}-au.pkg/Payload/Library/Audio/Plug-Ins/Components/${stem}.component/Contents/MacOS/${stem}"; do
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
