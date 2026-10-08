# Design: the site follows releases, the app updates itself (I-004, I-005)

- 2026-10-08, branch `feat/updates`. Ideas: [IDEAS.md](../release/IDEAS.md) I-004 and I-005.
- Read first: [SIGNING.md](../release/SIGNING.md) (Apple signing, independent of this), `.github/workflows/mdmm-editors-release.yml`,
  [FOUNDATION.md](FOUNDATION.md) (the notice route: `notice` / `noticeAnswer`).

## 1. Problem

A user learns about a new version only by visiting mdmm.dev or GitHub. The site itself is edited by hand at each release
(`config.js`'s version). Two asks:

1. **I-004.** Publishing a GitHub release updates the site: the version in `site/public/config.js`, a machine-readable
   `site/public/latest.json`, and a deploy.
2. **I-005.** The editor notices a newer version (once a day at most, can be turned off, no identifiers sent), and the
   standalone app installs it after checking it was made by us: a SHA-256 of the file and an ed25519 signature with a
   key compiled into the app, independent of Apple's and Microsoft's signing.

## 2. Candidates

### Where the signature is made

| | Where | For | Against |
|---|---|---|---|
| A | In `mdmm-editors-release.yml`, at upload: a `<asset>.sig` next to each asset | Signs as soon as the bytes exist. | This repo's rule is that a hand-uploaded asset (from a local, firmware-tested build) wins over CI's: those would carry no signature, or a CI one for other bytes. Two places that must agree (the `.sig` assets and `latest.json`). |
| B | In the new release-**published** workflow, over the bytes GitHub serves | Signs exactly what users download, however it got onto the release. One place makes `sha256`, `sig` and `latest.json` together. | The key sits in the workflow that also commits to main and deploys (both already trusted steps: whoever can publish a release can ship anything). |

**Choice: B.** The signer is `scripts/release/make_latest_json.py` in `mdmm-site-release.yml`; it no-ops (no `sig`) without
`MDMM_UPDATE_SIGNING_KEY`. The release workflow (`mdmm-editors-release.yml`) is unchanged: publishing is the gate.

### What is signed

| | Message | Against |
|---|---|---|
| A | The file's bytes | An old, validly signed file could be offered as a newer version (a replay) or as the other machine's. |
| B | A statement naming the file: format, asset name, version, SHA-256 | — |

**Choice: B** (the same strength as signing the bytes, since the statement carries the file's SHA-256; it also binds the
name and version, so a replayed or swapped asset fails).

### How the app shows "Update available"

| | Way | For | Against |
|---|---|---|---|
| A | The existing notice route as is (`notice` → the page's modal `Dlg`) | No page change. | Modal: it interrupts playing, and a closed modal answers "Later". |
| B | A new message type and a new banner on both pages | Non-modal. | A second route for the same thing: a question with buttons and an answer. |
| C | The notice route with `"modal": false`: the page draws it as a banner (no overlay, no focus taken); the answer is the same `noticeAnswer` | Non-modal, one route, the plug-in code unchanged for every other notice. | Both pages learn one flag. |

**Choice: C.** A banner never queues behind or blocks the question dialog; a newer banner replaces the older one (the
plug-in sends one banner at a time and forgets the older answer).

### How the app fetches

`juce::URL` on Linux is plain sockets here (`JUCE_USE_CURL=0` in `juce.cmake`, no HTTPS), and on macOS it uses the
shared cookie store. So the app runs the system's **curl** (`/usr/bin/curl` on macOS, `curl` on Linux,
`%SystemRoot%\System32\curl.exe` on Windows 10 1803 and later): no cookie jar, a fixed `User-Agent: mdmm-editor`, HTTPS
only (also for redirects), size and time limits. Without curl the check says nothing and the menu says "could not
check". One code path on three systems, on a background thread, never the audio thread.

## 3. Data contracts

### 3.1 `https://mdmm.dev/latest.json` (written by `scripts/release/make_latest_json.py`)

```json
{
  "schema": 1,
  "version": "0.3.3",
  "tag": "mdmm-v0.3.3",
  "date": "2026-10-08",
  "notes": "https://github.com/radekdymacz/gearmulator-md-mm/releases/tag/mdmm-v0.3.3",
  "page": "https://mdmm.dev/get/",
  "assets": {
    "mac":   { "md": ASSET, "mm": ASSET },
    "win":   { "md": ASSET, "mm": ASSET },
    "linux": { "md": ASSET, "mm": ASSET }
  }
}
```

`ASSET` = `{"name", "url", "size", "sha256", "sig"}`:

- `name`: the fixed release asset name (the site's download links use the same): `Machinedrum-Editor-macOS.pkg`,
  `Monomachine-Editor-macOS.pkg`, `Machinedrum-Editor-Windows-x64-not-tested.zip`, `Monomachine-Editor-Windows-x64-not-tested.zip`,
  `Machinedrum-Editor-Linux-x64-not-tested.tar.gz`, `Monomachine-Editor-Linux-x64-not-tested.tar.gz`.
- `url`: exactly `https://github.com/radekdymacz/gearmulator-md-mm/releases/download/<tag>/<name>` (the app refuses any
  other URL).
- `size`: bytes (integer). `sha256`: 64 lowercase hex. `sig`: base64 (88 characters) of the 64-byte ed25519 signature, or
  absent when the release was not signed.

Rules for readers (the app, `mdmmUpdate::parseManifest`): `schema` must be 1; `version` is `MAJOR.MINOR.PATCH`;
an OS or a machine may be missing (no build for it: the app offers the site instead); unknown members are ignored. Any
other deviation (not JSON, wrong types, a bad hash, a foreign URL, a name that is not the fixed one) is "malformed" and
the app says nothing to the user.

Pre-releases (`mdmm-v*-*`) and drafts never reach `latest.json`.

### 3.2 The signature

- Key: ed25519 (RFC 8032). Private key = the 32-byte seed as 64 hex characters, in the GitHub secret
  `MDMM_UPDATE_SIGNING_KEY`. Public key = 32 bytes as 64 hex characters in
  `source/elektron/md/mdmmUpdate/updateKey.h` (`g_updatePublicKeyHex`).
- Message (UTF-8, `\n` line ends, four lines, each ending in `\n`):

  ```
  mdmm-update-v1
  <name>
  <version>
  <sha256 lowercase hex>
  ```

- The app verifies with Monocypher's `crypto_ed25519_check` (vendored, BSD-2-Clause or CC0; compatible with GPL-3).
- While `updateKey.h` holds the placeholder (not 64 hex characters), the app checks and notifies but never installs:
  its button is "Download" (the site) instead of "Update".
- `scripts/release/update_keygen.py` makes a pair (prints both; nothing is written). `scripts/release/sign_update.py`
  signs files. Both use `scripts/release/mdmm_ed25519.py` (pure Python RFC 8032, no packages). `make_latest_json.py`
  refuses to sign with a key whose public half is not the one compiled into the app (unless the app still has the
  placeholder: then it signs and warns).

### 3.3 The editor's config (the editor's `PropertiesFile`, shared by app and plug-in)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `updateCheck` | bool | true | Check mdmm.dev once a day. "Don't check" and the menu's tick set it. |
| `updateLastCheck` | int (Unix seconds) | 0 | When the last check started (success or not). |

A check is due when `updateCheck` is on and `now >= last + 86400`, or `last` is more than a day in the future (a clock
that went back). Windows ask the shared checker every minute while open, the first time 20 s after the page is ready
(nothing competes with the window's start-up; the background thread is made at the first check); "Check for Updates Now" ignores the day (not the
setting's wording: it is a direct ask). Nothing is sent but a plain `GET /latest.json` (no query, no cookies).

### 3.4 Files

```
.github/workflows/mdmm-site-release.yml          release: published (mdmm-v*), or by hand with a tag
scripts/release/mdmm_ed25519.py                  pure RFC 8032 (sign, public key), RFC 8032 test vectors in its self-test
scripts/release/update_keygen.py                 one-time key pair
scripts/release/sign_update.py                   sign files (prints name, sha256, sig)
scripts/release/make_latest_json.py              latest.json from the downloaded assets (+ signs)
scripts/release/set_site_version.py              the version line in site/public/config.js
source/elektron/md/mdmmUpdate/                   pure C++ (no JUCE): version, manifest, schedule, SHA-256, verify
source/elektron/md/mdmmUpdate/monocypher/        vendored Monocypher 4.0.2 (+ LICENCE.md)
source/elektron/md/mdmmUpdate/updateKey.h        the public key (placeholder until the owner pastes one)
source/elektron/md/mdmmUpdate/mdmmUpdateTest.cpp unit tests
source/elektron/md/mdJucePlugin/mdUpdater.*      the JUCE side: curl, the background thread, install, swap helper
```

## 4. What the user sees (states)

| State | Standalone | Plug-in |
|---|---|---|
| Newer version found | Banner: "Update available: 0.3.3" with **Update** / **Later** / **Don't check** (with the placeholder key or no signature: **Download** instead of Update) | Banner with **Download** (opens mdmm.dev/get/) / **Later** / **Don't check** |
| Downloading | Banner "Downloading 0.3.3… 42 %" with **Cancel** | — |
| Verification failed (size, SHA-256, signature, archive layout) | Banner "The download did not verify (…): nothing was installed." **Download** / **OK**. The file is deleted. | — |
| Ready, macOS | The system Installer opens the .pkg (the user clicks Install; works for "all users" and "me only"). Banner: "Quit the editor, then click Install. Close your DAW first if it uses the plug-ins." **Quit** / **OK** | — |
| Ready, Windows / Linux, folder writable | Banner: "0.3.3 is ready: it replaces the app when you quit." **Restart now** / **When I quit**. The VST3 is not touched: "close your DAW, then copy the new VST3 from <folder>". | — |
| Ready, folder not writable | The unpacked folder opens in Explorer / the file manager; the banner says what to copy where. | — |
| Up to date (asked from the menu) | Banner "You have the latest version (0.3.2)." **OK** | same |
| Could not check (asked from the menu) | Banner "Could not reach mdmm.dev." **OK** | same |

"Later" hides the banner until the next day's check. A daily check that fails says nothing.

The menu (the page's right-click menu, the standalone's menu bar): **Updates ▸ Check for Updates Now**, **Check Daily**
(ticked = `updateCheck`).

### Swap after exit (Windows, Linux)

The archive is unpacked into a staging folder in the temp folder and its layout checked (the standalone's file is
there). On quit (or **Restart now**) the app writes a helper script beside the staged files and starts it detached: it
waits for the app's process to end, replaces the standalone's files (Windows: the `.exe` and the text files beside it;
Linux: the executable and its two web view shims, each copied then renamed into place) and, after **Restart now**,
starts the app again. The plug-ins are never replaced: a DAW may have them loaded.

## 5. Not in this design

- No silent install anywhere; no elevation prompts by the app (macOS: the Installer asks if it needs to).
- No delta updates, no channels (pre-releases are never offered).
- No server other than mdmm.dev (check) and github.com (the download the user asked for).
