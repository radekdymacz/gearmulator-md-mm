#!/bin/bash
#
# Check an installed Machinedrum Editor / Monomachine Editor package.
# Run it after installing one or both packages:
#
#   sudo installer -pkg Machinedrum-Editor-macOS.pkg -target /
#   sudo installer -pkg Monomachine-Editor-macOS.pkg -target /
#   scripts/macos/verify_mdmm_pkg_install.sh [md] [mm]
#
# With no arguments it checks both. It needs no sudo. For each machine it
# checks the Installer receipts, that the three bundles are where expected
# with valid signatures, and that the installed Audio Unit passes auval.
#
# auval loads the AU in this process. Set GEARMULATOR_DATA_ROOT to an empty
# folder to run it without touching your own Gearmulator settings (CI does).

set -euo pipefail

machines=("$@")
if [[ ${#machines[@]} -eq 0 ]]; then
  machines=(md mm)
fi

failed=0
fail() {
  echo "FAIL: $*" >&2
  failed=1
}

for machine in "${machines[@]}"; do
  case "${machine}" in
    md) stem="Gearmulator MD"; app="Machinedrum Editor"; subtype="Tmdr" ;;
    mm) stem="Gearmulator MM"; app="Monomachine Editor"; subtype="Tmno" ;;
    *) echo "unknown machine: ${machine} (use md or mm)" >&2; exit 2 ;;
  esac
  id="com.nativekloud.mdmm.${machine}"
  echo "== ${app}"

  for part in app vst3 au; do
    if pkgutil --pkg-info "${id}.${part}" >/dev/null 2>&1; then
      echo "receipt ${id}.${part}: $(pkgutil --pkg-info "${id}.${part}" | awk '/^version:/{print $2}')"
    else
      fail "no Installer receipt for ${id}.${part}"
    fi
  done

  for bundle in \
      "/Applications/${app}.app" \
      "/Library/Audio/Plug-Ins/VST3/${stem}.vst3" \
      "/Library/Audio/Plug-Ins/Components/${stem}.component"; do
    executable="${bundle}/Contents/MacOS/${stem}"
    if [[ ! -x "${executable}" ]]; then
      fail "missing ${executable}"
      continue
    fi
    if codesign --verify --deep --strict "${bundle}" 2>/dev/null; then
      echo "ok  ${bundle} ($(lipo -archs "${executable}"))"
    else
      fail "signature does not verify: ${bundle}"
    fi
  done

  for copy in \
      "${HOME}/Library/Audio/Plug-Ins/VST3/${stem}.vst3" \
      "${HOME}/Library/Audio/Plug-Ins/Components/${stem}.component"; do
    if [[ -e "${copy}" ]]; then
      echo "note: a per-user copy also exists and may shadow the installed one: ${copy}"
    fi
  done

  auval_log="$(mktemp "${TMPDIR:-/tmp}/mdmm-auval.XXXXXX")"
  if auval -v aumu "${subtype}" GmPv >"${auval_log}" 2>&1 \
      && grep -q "AU VALIDATION SUCCEEDED" "${auval_log}"; then
    echo "ok  auval aumu ${subtype} GmPv: AU VALIDATION SUCCEEDED"
  else
    tail -40 "${auval_log}" >&2
    fail "auval aumu ${subtype} GmPv did not succeed (log: ${auval_log})"
    continue
  fi
  rm -f "${auval_log}"
done

if [[ ${failed} -ne 0 ]]; then
  echo "Package install verification FAILED" >&2
  exit 1
fi
echo "Package install verification passed"
