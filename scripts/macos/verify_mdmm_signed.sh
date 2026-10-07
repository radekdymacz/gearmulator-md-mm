#!/bin/bash
#
# Check the release artifacts the way Gatekeeper will: the two installer
# packages and the two disk images, and every bundle inside them.
#
#   verify_mdmm_signed.sh ARTIFACT_DIR
#
# ARTIFACT_DIR holds Machinedrum-Editor-macOS.{pkg,dmg} and
# Monomachine-Editor-macOS.{pkg,dmg}.
#
# MDMM_EXPECT_SIGNED=1 (the default) requires Developer ID signatures, the
# hardened runtime, the allow-jit entitlement on the apps, stapled
# notarization tickets and spctl acceptance; any miss fails. MDMM_EXPECT_SIGNED=0
# runs only the structural checks (contents, ad-hoc signatures that verify) for
# an unsigned dry run, and says loudly that the artifacts are unsigned.

set -euo pipefail

# shellcheck source=../mdmm-product.env
. "$(cd "$(dirname "$0")" && pwd)/../mdmm-product.env"

artifact_dir="$(cd "${1:?usage: verify_mdmm_signed.sh ARTIFACT_DIR}" && pwd)"
expect_signed="${MDMM_EXPECT_SIGNED:-1}"
if [[ "${expect_signed}" != "0" && "${expect_signed}" != "1" ]]; then
  echo "MDMM_EXPECT_SIGNED must be 0 or 1" >&2
  exit 2
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mdmm-verify-signed.XXXXXX")"
mount_point=""
cleanup() {
  if [[ -n "${mount_point}" ]]; then
    hdiutil detach -quiet -force "${mount_point}" || true
  fi
  rm -rf -- "${work_dir}"
}
trap cleanup EXIT

failed=0
fail() {
  echo "FAIL: $*" >&2
  failed=1
}
ok() {
  echo "ok  $*"
}

# One signed bundle: valid signature, and with EXPECT_SIGNED the Developer ID
# authority, hardened runtime, timestamp, a stapled ticket and (apps) allow-jit.
check_bundle() {
  local bundle="$1" kind="$2" details entitlements actual_id
  actual_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${bundle}/Contents/Info.plist" 2>/dev/null || true)"
  [[ "${actual_id}" == "${bundle_id}" ]] \
    || fail "bundle identifier ${actual_id:-missing} is not ${bundle_id}: ${bundle}"
  if ! codesign --verify --strict --deep "${bundle}" 2>/dev/null; then
    fail "signature does not verify: ${bundle}"
    return
  fi
  [[ "${expect_signed}" == "1" ]] || { ok "verifies (ad-hoc allowed): ${bundle##*/}"; return; }
  details="$(codesign -dvvv "${bundle}" 2>&1)"
  [[ "${details}" == *"Authority=Developer ID Application:"* ]] \
    || fail "not Developer ID signed: ${bundle}"
  grep -Eq 'flags=0x[0-9a-f]+\([^)]*runtime' <<< "${details}" || fail "no hardened runtime: ${bundle}"
  [[ "${details}" == *"Timestamp="* ]] || fail "no secure timestamp: ${bundle}"
  xcrun stapler validate -q "${bundle}" || fail "no stapled ticket: ${bundle}"
  if [[ "${kind}" == "app" ]]; then
    entitlements="$(codesign -d --entitlements - --xml "${bundle}" 2>/dev/null)"
    [[ "${entitlements}" == *"com.apple.security.cs.allow-jit"* ]] \
      || fail "allow-jit entitlement missing: ${bundle}"
    # The check a quarantined app gets on first launch.
    spctl --assess --type execute -vv "${bundle}" || fail "spctl rejects ${bundle}"
  fi
  ok "Developer ID, hardened, notarized: ${bundle##*/}"
}

# The bundles' own identifiers (SIGNING.md, "Identifiers"), never upstream
# Gearmulator's local.gearmulator.preview.*.
# The bundles carry the product names (scripts/mdmm-product.env); the file names
# are the ones the website links to.
for row in "Machinedrum-Editor-macOS|${MDMM_PRODUCT_NAME_MD}|md|com.nativekloud.machinedrum-editor" \
           "Monomachine-Editor-macOS|${MDMM_PRODUCT_NAME_MM}|mm|com.nativekloud.monomachine-editor"; do
  IFS='|' read -r base app_name id_suffix bundle_id <<< "${row}"
  pkg="${artifact_dir}/${base}.pkg"
  dmg="${artifact_dir}/${base}.dmg"
  echo "== ${app_name}"

  # The installer package.
  if [[ ! -f "${pkg}" ]]; then
    fail "missing ${pkg}"
  else
    if [[ "${expect_signed}" == "1" ]]; then
      signature="$(pkgutil --check-signature "${pkg}" || true)"
      [[ "${signature}" == *"Developer ID Installer:"* ]] \
        || fail "package not signed with Developer ID Installer: ${pkg}"
      xcrun stapler validate -q "${pkg}" || fail "no stapled ticket: ${pkg}"
      spctl --assess --type install -vv "${pkg}" || fail "spctl rejects ${pkg}"
    else
      echo "UNSIGNED: ${pkg##*/} (structural checks only)"
    fi
    expanded="${work_dir}/${id_suffix}-pkg"
    pkgutil --expand-full "${pkg}" "${expanded}"
    check_bundle "${expanded}/${id_suffix}-app.pkg/Payload/Applications/${app_name}.app" app
    check_bundle "${expanded}/${id_suffix}-vst3.pkg/Payload/Library/Audio/Plug-Ins/VST3/${app_name}.vst3" plugin
    check_bundle "${expanded}/${id_suffix}-au.pkg/Payload/Library/Audio/Plug-Ins/Components/${app_name}.component" plugin
  fi

  # The disk image.
  if [[ ! -f "${dmg}" ]]; then
    fail "missing ${dmg}"
    continue
  fi
  hdiutil verify -quiet "${dmg}" || fail "hdiutil verify failed: ${dmg}"
  if [[ "${expect_signed}" == "1" ]]; then
    codesign --verify --strict "${dmg}" || fail "disk image signature does not verify: ${dmg}"
    codesign -dvvv "${dmg}" 2>&1 | grep -q "Authority=Developer ID Application:" \
      || fail "disk image not Developer ID signed: ${dmg}"
    xcrun stapler validate -q "${dmg}" || fail "no stapled ticket: ${dmg}"
    spctl --assess --type open --context context:primary-signature -vv "${dmg}" \
      || fail "spctl rejects ${dmg}"
  else
    echo "UNSIGNED: ${dmg##*/} (structural checks only)"
  fi
  mount_point="${work_dir}/mount-${id_suffix}"
  mkdir -p "${mount_point}"
  hdiutil attach -quiet -readonly -nobrowse -noautoopen -mountpoint "${mount_point}" "${dmg}"
  [[ -f "${mount_point}/Install.txt" ]] || fail "Install.txt missing from ${dmg}"
  grep -q "GNU GENERAL PUBLIC LICENSE" "${mount_point}/LICENSE.txt" 2>/dev/null \
    || fail "LICENSE.txt (GPL) missing from ${dmg}"
  [[ "$(readlink "${mount_point}/Applications")" == "/Applications" ]] \
    || fail "Applications link missing from ${dmg}"
  check_bundle "${mount_point}/${app_name}.app" app
  check_bundle "${mount_point}/${app_name}.vst3" plugin
  check_bundle "${mount_point}/${app_name}.component" plugin
  hdiutil detach -quiet "${mount_point}"
  mount_point=""
done

if [[ ${failed} -ne 0 ]]; then
  echo "Signed-artifact verification FAILED" >&2
  exit 1
fi
if [[ "${expect_signed}" == "1" ]]; then
  echo "Signed-artifact verification passed: Developer ID signed, notarized, stapled"
else
  echo "Structural verification passed. These artifacts are UNSIGNED and must not ship."
fi
