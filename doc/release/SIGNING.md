# Signing and notarizing the macOS release

Rule: **nothing ships or is announced unsigned.** Every macOS download (two
installer packages and two disk images) is signed with an Apple Developer ID,
notarized by Apple and stapled. The `mdmm-v*` tag workflow does all of it once
five GitHub secrets exist. Without them it still builds, but the release stays a
draft titled `UNSIGNED - do not publish`. It will not attach anything to a
published release.

## What CI does

The job is `macos-installers` in `.github/workflows/mdmm-editors-release.yml`.
The scripts live in `scripts/macos/`.

| Step | Script | Signed run | Unsigned run (no secrets) |
|---|---|---|---|
| Sign the 6 bundles | `sign_mdmm.sh` | Developer ID Application, `--options runtime --timestamp`, inside-out, no `--deep`; the apps get `entitlements/mdmm-standalone.entitlements` | Ad-hoc, no hardened runtime (as before) |
| Notarize the bundles | `notarize_mdmm.sh --bundles` | Submits one zip, then staples each `.app`, `.vst3` and `.component` | Skipped |
| Installer packages | `build_mdmm_pkg.sh` | `productbuild --sign "Developer ID Installer: …" --timestamp` | Unsigned |
| Disk images | `build_mdmm_dmg.sh` | `codesign` with Developer ID Application | Unsigned |
| Notarize pkg and dmg | `notarize_mdmm.sh FILE…` | `notarytool submit --wait`, then `stapler staple` and `stapler validate` | Skipped |
| Verify | `verify_mdmm_signed.sh` | Checks the authority, hardened runtime, timestamp, allow-jit, the stapled tickets and `spctl -a -vv` | Structural checks only, with an "UNSIGNED" log line |
| Install test | `verify_mdmm_pkg_install.sh` | Also requires Developer ID and spctl acceptance (`MDMM_EXPECT_SIGNED=1`) | As before, plus auval |
| Release | — | Draft, which you publish | Draft titled UNSIGNED; fails if the release is already published |

Any signing or notarization error fails the job. If only some of the five
secrets are set, the job also fails, rather than quietly falling back to
unsigned.

The asset names are fixed because the site links to
`releases/latest/download/<name>`:

- `Machinedrum-Editor-macOS.pkg`
- `Monomachine-Editor-macOS.pkg`
- `Machinedrum-Editor-macOS.dmg`
- `Monomachine-Editor-macOS.dmg`

## The five secrets

Set them under Settings › Secrets and variables › Actions on
`radekdymacz/mdmm`.

| Secret | Contents |
|---|---|
| `MDMM_SIGNING_P12_BASE64` | One `.p12` holding **both** identities (Developer ID Application and Developer ID Installer), each with its private key, base64-encoded |
| `MDMM_SIGNING_P12_PASSWORD` | The password you set when exporting that `.p12` |
| `MDMM_NOTARY_KEY_ID` | The App Store Connect API key ID (10 characters) |
| `MDMM_NOTARY_ISSUER_ID` | The issuer ID (a UUID) shown above the keys list |
| `MDMM_NOTARY_KEY_P8` | The full text of `AuthKey_<KEYID>.p8`, including the `-----BEGIN PRIVATE KEY-----` lines |

### 1. Create the two certificates

You need an Account Holder role for Developer ID certificates.

1. On the Mac, open Keychain Access › Certificate Assistant › Request a
   Certificate From a Certificate Authority. Enter your email, choose "Saved to
   disk", and save `CertificateSigningRequest.certSigningRequest`.
2. Go to developer.apple.com › Certificates, IDs & Profiles › Certificates › +.
3. Choose **Developer ID Application**, profile type **G2 Sub-CA**, upload the
   CSR and download `developerID_application.cer`.
4. Repeat for **Developer ID Installer** (one more CSR is fine) and download
   `developerID_installer.cer`.
5. Double-click both `.cer` files. They land in the login keychain next to
   their private keys.

### 2. Export both identities into one .p12

1. In Keychain Access › login › My Certificates, select both
   "Developer ID Application: … (TEAMID)" and
   "Developer ID Installer: … (TEAMID)".
2. Expand each one to check that a private key is underneath.
3. Right-click › Export 2 items… › Personal Information Exchange (.p12) and set
   a strong password.
4. Base64-encode it and put it on the clipboard:

   ```sh
   base64 -i mdmm-signing.p12 | pbcopy      # → MDMM_SIGNING_P12_BASE64
   ```

   Then store the password as `MDMM_SIGNING_P12_PASSWORD`.
5. Delete the `.p12` file afterwards, or keep it in a password manager. Never
   commit it.

### 3. Create the notarization API key

1. Go to appstoreconnect.apple.com › Users and Access › Integrations ›
   App Store Connect API › Team Keys › +.
2. Name it `mdmm-notary`, give it the **Developer** access role and click
   Generate.
3. Note the **Key ID** (`MDMM_NOTARY_KEY_ID`) and the **Issuer ID** shown above
   the table (`MDMM_NOTARY_ISSUER_ID`).
4. Download `AuthKey_<KEYID>.p8`. Apple only lets you download it once. Then
   copy its text:

   ```sh
   pbcopy < AuthKey_XXXXXXXXXX.p8           # → MDMM_NOTARY_KEY_P8
   ```

To check the key locally before relying on CI:

```sh
xcrun notarytool history --key AuthKey_XXXXXXXXXX.p8 --key-id XXXXXXXXXX --issuer <issuer-uuid>
```

### 4. Release

1. If the tag's workflow already ran without secrets, delete the
   UNSIGNED draft release, or at least its assets. An asset that already
   exists is never replaced.
2. Re-run the workflow for the tag, or push the tag.
3. Check that the log shows `Signed-artifact verification passed` and
   `Gatekeeper accepts`.
4. Publish the draft.

Only after that do the "not signed / Open Anyway" texts change, in the same
release. The branch `release/site-docs-0.3.0` also edits several of these
files, so merge it first.

- `scripts/macos/pkg-resources/readme.html`: drop the "This installer is not
  signed yet" section, with its Open Anyway steps and `xattr` command.
- `scripts/macos/INSTALL-macOS.txt`: drop the "ad-hoc signed and is not
  notarized" wording. This is the zip guide; the site-docs branch edits it.
- `scripts/macos/pkg-resources/welcome.html`: re-check after the site-docs
  merge.
- `README.md` lines 13–15: drop "not signed or notarised yet … Open Anyway".
- `site/public/index.html` about line 105: drop the "Not notarised by Apple …
  Privacy & Security" item. The site-docs branch edits this file.
- `site/public/get/download/index.html` about line 84: drop the "not notarised
  … Open Anyway" step, and add the `.dmg` links next to the `.pkg` ones. The
  site-docs branch edits this file.
- `site/public/guide/index.html` (site-docs branch only): add the install
  options (pkg or dmg).
- `doc/release/v<next>.md`: no "Unsigned and not notarised" bullet (as in
  v0.3.0.md lines 97–98); mention the disk images.
  Links in release notes are absolute (the file becomes the GitHub release page body, where
  relative links break): `https://github.com/radekdymacz/mdmm/blob/main/doc/release/vX.Y.Z.md`.
- `scripts/macos/macsetup_Gearmulator-Elektron.command` (zip only): becomes
  unnecessary for signed bundles. Keep it or retire it with the zip.

## Signing locally (Radek's Mac)

The same scripts work by hand. The identities come from the login keychain:

```sh
export MDMM_SIGN_IDENTITY="Developer ID Application: <Name> (<TEAMID>)"
export MDMM_DMG_SIGN_IDENTITY="${MDMM_SIGN_IDENTITY}"
export MDMM_INSTALLER_IDENTITY="Developer ID Installer: <Name> (<TEAMID>)"
export MDMM_NOTARY_KEY_PATH=~/keys/AuthKey_XXXXXXXXXX.p8 MDMM_NOTARY_KEY_ID=XXXXXXXXXX MDMM_NOTARY_ISSUER_ID=<uuid>
scripts/macos/sign_mdmm.sh BUNDLES
scripts/macos/notarize_mdmm.sh --bundles BUNDLES
scripts/macos/build_mdmm_pkg.sh BUNDLES OUT 0.3.0
scripts/macos/build_mdmm_dmg.sh BUNDLES OUT 0.3.0
scripts/macos/notarize_mdmm.sh OUT/*.pkg OUT/*.dmg
scripts/macos/verify_mdmm_signed.sh OUT
```

Here `BUNDLES` is the folder holding the six `Gearmulator MD|MM.app|vst3|component`
bundles, for example the extracted `Gearmulator-Elektron-macOS-*` folder.

Dry run without a certificate: `MDMM_SIGN_HARDENED=1 sign_mdmm.sh BUNDLES`
signs ad-hoc with the hardened runtime and the real entitlements. Then
`MDMM_EXPECT_SIGNED=0 verify_mdmm_signed.sh OUT` runs the structural checks.

## Identifiers

A Developer ID signature makes the bundle identifier sticky: macOS ties the
app's permissions (the microphone, TCC) and LaunchServices ties "the app with
this id" to it. Up to 0.3.0 the editors used upstream Gearmulator's
`local.gearmulator.preview.GearmulatorMD` / `…MM`, the same as an old upstream
build (`Gearmulator MD.app` 2.2.9), and LaunchServices opened whichever it
found. Since the first signed release they have their own:

| Bundles (app, VST3, AU alike) | Up to 0.3.0 | Now |
|---|---|---|
| Machinedrum Editor, `Gearmulator MD.*` | `local.gearmulator.preview.GearmulatorMD` | `com.nativekloud.machinedrum-editor` |
| Monomachine Editor, `Gearmulator MM.*` | `local.gearmulator.preview.GearmulatorMM` | `com.nativekloud.monomachine-editor` |

JUCE gives every format of a target the same identifier, so there is no
`.vst3`/`.component` suffix. They are set in
`source/elektron/md/mdJucePlugin/mdmmPlugins.cmake`
(`GEARMULATOR_PLUGIN_BUNDLE_ID_<target>`, read by `source/juce.cmake`); every
other synth keeps upstream's. `sign_mdmm.sh` refuses to sign, and
`build_mdmm_pkg.sh` to package, a bundle with any other identifier;
`verify_mdmm_signed.sh` and `verify_mdmm_pkg_install.sh` check it.

What does **not** change, so DAW projects and saved state still load:

- the VST3 class IDs, which JUCE derives from the manufacturer code `GmPv` and
  the plug-in code `Tmdr` / `Tmno`, never from the bundle identifier
  (`ABCDEF019182FAEB476D5076546D6472` is the MD processor);
- the AU type, subtype and manufacturer (`aumu Tmdr GmPv`, `aumu Tmno GmPv`);
- the plug-in state format, and the data folder
  `~/Documents/Gearmulator Preview/<Machinedrum|Monomachine>/` with its
  `roms/`.

**Settings.** The editors no longer share their settings files with an upstream
build of the same machine (upstream reset the skin to its panel, and could not
load ours):

| | Up to 0.3.0 (upstream's name) | Now |
|---|---|---|
| Editor config | `…/<Machine>/config/Gearmulator MD.xml` | `…/<Machine>/config/Machinedrum Editor.xml` |
| App settings (audio device, last state) | `~/Library/Application Support/Gearmulator MD.settings` | `~/Library/Application Support/Machinedrum Editor.settings` |

(MM alike.) On the first start without its own file the editor copies the old
one (`mdSettingsMigration.h`): a copy, never a move, through a temporary file;
skipped once its own file exists; the old file is left for upstream. If the
copied file was last written by upstream (skin `mdDefault`), the editor opens
its own page anyway and writes it back (`keepEditorPage`). ROMs, the patch
manager and MIDI learn presets stay in the shared data folder.

**Upgrading from 0.1.0 – 0.3.0.** The package identifiers
(`com.nativekloud.mdmm.md.app` / `.vst3` / `.au`, `…mm…`) are unchanged since
0.1.0, so a new package upgrades the old receipts in place; the bundles land at
the same paths (`BundleIsRelocatable=false`, `BundleHasStrictIdentifier=false`,
so Installer overwrites the bundle there even though its identifier changed).
macOS asks again for microphone access the first time the new app uses an
input (the old grant belonged to the ad-hoc signed app). Saved window state
under the old identifier is simply left behind. Not yet tried on a Mac with
0.3.0 installed: install the new package over it, then check
`pkgutil --pkg-info com.nativekloud.mdmm.md.app` shows the new version and
`verify_mdmm_pkg_install.sh` passes.

## Names

After 0.3.1 every bundle, executable and plug-in carries the product name, set
once in `scripts/mdmm-product.env` (read by `mdmmPlugins.cmake` and sourced by
every script here):

| | Up to 0.3.1 | Now |
|---|---|---|
| App | `Machinedrum Editor.app` (renamed by the installer), executable `Gearmulator MD`, `CFBundleName` `Gearmulator MD` | `Machinedrum Editor.app`, executable and `CFBundleName` `Machinedrum Editor` |
| VST3 | `Gearmulator MD.vst3`, vendor `Gearmulator Preview` | `Machinedrum Editor.vst3`, vendor `Future Native Audio` |
| AU | `Gearmulator MD.component`, `Gearmulator Preview: Gearmulator MD` | `Machinedrum Editor.component`, `Future Native Audio: Machinedrum Editor` |
| Windows | `Gearmulator MD.exe`, `Gearmulator MD.vst3` | `Machinedrum Editor.exe`, `Machinedrum Editor.vst3` |

(MM alike.) The plug-in codes above do not change, so a project saved with
`Gearmulator MD` opens with `Machinedrum Editor`. The data folder keeps its
`Gearmulator Preview` parent (`g_dataFolderVendor` in `mdPluginProcessor.cpp`),
and the settings files keep their names (`mdSettingsMigration.h`), whatever the
product is called. The LV2 URI is pinned to the old one in `mdmmPlugins.cmake`.

**Upgrading from 0.3.1 or earlier.** Installer never removes a file the new
package does not have, so each component's `preinstall`
(`pkg-resources/remove-old-bundles`, rendered per machine and component by
`render_remove_old_bundles.sh` from the names and identifiers in
`mdmm-product.env`) removes what is left under the old name before the new
bundle lands: `Gearmulator MD.vst3`, `Gearmulator MD.component` and a
`Gearmulator MD.app` (and `MM`), plus an app at `Machinedrum Editor.app` built
before the rename. It looks in the install location and in the home folder of
the person installing (`$HOME`, the person at the console, `$SUDO_USER`: a
root-run Installer has no home of its own, and the 0.3.0 and 0.3.1 zip and disk
image told people to copy the plug-ins into `~/Library/Audio/Plug-Ins` by
hand, so those copies have no receipt). Never another user's home folder.
Only ours, by exact path (no symbolic link) and identifier: the bundle must
carry the editor's (`com.nativekloud.machinedrum-editor`), or, for the VST3
and the AU only, upstream Gearmulator's (`local.gearmulator.preview.GearmulatorMD`,
0.3.0 and earlier: same plug-in codes, so a DAW would clash with it anyway),
or our receipt (`com.nativekloud.mdmm.md.vst3` ...) must list the path. An
old-named app with upstream's identifier is somebody's own copy and stays.
`scripts/release/test_release_scripts.py` runs the rendered script against a
temporary folder (names, identifiers, symbolic links, receipts, home folders).
`verify_mdmm_pkg_install.sh` fails if one of ours is still there. The disk
images cannot do this; their `Install.txt` says which old bundles to delete,
and where. Why it matters beyond a duplicate in the plug-in list: 0.1.0-alpha
and 0.2.0 carried the upstream project's version, 2.2.9, and from 0.2.1 the
editors' own (0.2.1, 0.3.x). macOS keeps one Audio Unit per type, subtype and
manufacturer and is said to prefer the higher version (not verified here), so a
left-over 2.2.9 AU may win over the new one.

## Hardened runtime and the JIT

The DSP56300 emulator recompiles DSP code with asmjit 1.10 (`JitRuntime`).

- On macOS asmjit maps code with `MAP_JIT`. On arm64 that is always. On x86_64
  it first tries plain RWX, which the hardened runtime refuses, then falls back
  to `MAP_JIT`.
- On arm64 it brackets every write with `pthread_jit_write_protect_np`
  (`ProtectJitReadWriteScope`).
- dsp56kEmu never patches emitted code. Blocks are chained through a table of
  function pointers held in ordinary data memory.
- There is no `mprotect` to make plain memory executable and no dual mapping.
- The 68k CPU (Musashi) is an interpreter.

So the apps need only **`com.apple.security.cs.allow-jit`**. They do not need
`allow-unsigned-executable-memory` or `disable-executable-page-protection`.
Checked on arm64 by signing `dsp56kTestRunner` ad-hoc with the hardened
runtime:

- without allow-jit it crashes (SIGSEGV) in its first JIT block;
- with allow-jit it runs the whole suite, with output identical to the
  unhardened binary.

The apps also carry **`com.apple.security.device.audio-input`**. They declare
`NSMicrophoneUsageDescription`, and the hardened runtime silently cuts audio
input without this entitlement.

**Plug-ins and DAWs.** A VST3 or AU runs inside the host's process with the
host's entitlements; the plug-in's own entitlements are ignored, which is why
the plug-ins are signed without any. The JIT therefore works only in hosts
whose signature allows `MAP_JIT`:

- hosts without the hardened runtime;
- hosts with `com.apple.security.cs.allow-jit`;
- hosts with `allow-unsigned-executable-memory`, which implies it.

Most DAWs that load third-party plug-ins carry one of these. A hardened host
without them will crash the plug-in at the first JIT block, signed or not.
Notarizing our plug-ins cannot fix that, and an unsigned build would fail there
just the same.

Checked on 2026-10-06 on Radek's Mac:

- Logic Pro is not hardened.
- Ableton Live 12 has the hardened runtime with
  `allow-unsigned-executable-memory`.
- REAPER has the hardened runtime with `allow-jit`.

All three are fine. On arm64, `allow-unsigned-executable-memory` on its own was
confirmed to let the test runner's JIT run.

Check a host with:

```sh
codesign -d --entitlements - "/Applications/<DAW>.app"
```

Logic Pro and GarageBand may run AUs out of process (AUHostingService). Test
the AU there before announcing support. If a host fails, the standalone app
remains the fallback.

## Update signatures and the site deploy (I-004, I-005)

Independent of Apple's signing: the editors install an update only when its
ed25519 signature checks out against the public key compiled into them
(doc/modern-ux/DESIGN-updates.md). Publishing a release runs
`.github/workflows/mdmm-site-release.yml`, which writes `site/public/latest.json`
(signed), the version in `site/public/config.js`, commits both to main and
deploys mdmm.dev. Each secret is optional: without it that step says so and is
skipped.

| GitHub secret | What | Without it |
|---|---|---|
| `MDMM_UPDATE_SIGNING_KEY` | the update key's private half, 64 hex characters | latest.json has no signatures: the apps say "Update available" and offer the download page, never install |
| `CLOUDFLARE_API_TOKEN` | a Cloudflare API token that can deploy the `mdmm-site` Worker | the files are committed, the site is not deployed |
| `CLOUDFLARE_ACCOUNT_ID` | `331224f43e4f448483cad2f1185ea965` | as above |

### The update key (once)

1. On your own computer: `python3 scripts/release/update_keygen.py`. It prints
   both halves and writes nothing.
2. Paste the public key into `source/elektron/md/mdmmUpdate/updateKey.h`
   (`g_updatePublicKeyHex`), commit, release. Builds before that commit carry the
   placeholder and never install updates themselves.
3. Save the private key as the repository secret `MDMM_UPDATE_SIGNING_KEY`
   (Settings > Secrets and variables > Actions > New repository secret), and in
   your password manager. Nowhere else.

The workflow refuses a key whose public half is not the one in `updateKey.h`.
To sign by hand: `MDMM_UPDATE_SIGNING_KEY=... python3 scripts/release/sign_update.py --version 0.3.3 FILE...`.

### The Cloudflare token (once)

Cloudflare dashboard > My Profile > API Tokens > Create Token > Create Custom Token:

- Permissions: **Account > Workers Scripts > Edit**. (If wrangler asks for more
  when the custom domain route is first attached, add **Zone > Workers Routes >
  Edit** on mdmm.dev; the route exists already, so a plain deploy needs only the
  first.)
- Account resources: Include > the NativeKloud account
  (`331224f43e4f448483cad2f1185ea965`) only.
- No client IP filtering (GitHub runners' addresses change); a TTL if you like.

Save the token as `CLOUDFLARE_API_TOKEN` and the account ID as
`CLOUDFLARE_ACCOUNT_ID` (repository secrets). Then, to backfill the current
release: Actions > "MD/MM site follows the release" > Run workflow > the tag.
