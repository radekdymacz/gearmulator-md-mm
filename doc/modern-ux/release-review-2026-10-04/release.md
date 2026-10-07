# MD + MM Editors v0.2.0 release-readiness audit (HEAD 855902a8c, 2026-10-04)

Key fact: the v0.2.0 draft is stale. Tags mdmm-v0.2.0 and mdmm-v0.2.1 exist and both are DRAFT releases (each with both .pkg files). HEAD is 28 commits and 233 files past mdmm-v0.2.1. The only published release is 0.1.0-alpha, which is what /releases/latest/download/ serves.

## 1. CI
- PASS: tag workflow mdmm-editors-release.yml. It builds a universal macOS bundle, builds both .pkg files, test-installs them, runs auval, and attaches them to the existing draft or creates one. It never overwrites an existing asset. The 0.2.1 tag run was green (51m).
- PASS: push workflows. mdmm-core and mdmm-audio-io are green on the last 3 pushes. mdmm-shared-controller and mdmm-integration-policy not examined.
- FAIL (minor): "Elektron Windows artifacts" failed on 4 of the last 5 pushes before 10-03 18:24. The failing step was "Test and package MD/MM"; I did not get the cause from the logs. The last two pushes are green, so it looks flaky or was fixed, but the Windows job is not release-grade. The macOS artifact job failed on the same pushes and then went green.
  Fix: look at 37117073478 and 37113691784 before trusting Windows.
- NEEDS-RADEK: the tag workflow tests firmware-free (GEARMULATOR_REQUIRE_FIRMWARE_TESTS=0). Its own comment says release assets should come from a local, firmware-tested build, uploaded to the draft first. The workflow keeps existing assets.
- NOTE: the step name "Build, test, sign, and package" is misleading. Nothing is signed (see 3).

## 2. Versioning
- FAIL: the live site and the release line disagree.
  - Repo: MDMM_EDITOR_VERSION is 0.2.1 (source/elektron/md/mdJucePlugin/mdmmPlugins.cmake:10). build_mdmm_pkg.sh defaults to 0.2.1. doc/release has v0.2.1.md. The README says latest is 0.2.1.
  - site/public/config.js says 0.2.0. The deployed config.js says "0.1.0-alpha".
  - HEAD still reports 0.2.1 although 28 commits of new features are on top.
- FAIL: the drafts predate HEAD. The Mix strips, DAW automation restore, drag-mute, shared page layer, QWERTY and mnemonic keys, Sound page by function, sampler and rhythm generators are not in any release notes.
  Fix: pick a version (0.3.0 or 0.2.2). Bump mdmm.cmake, the build_mdmm_pkg.sh default and site config.js. Write doc/release/vX.md. Tag. Delete or supersede the two drafts. No in-app "about" string was checked; it presumably follows the CMake version.

## 3. Packaging
- PASS: the packages are unsigned, three components each (app, VST3, AU). They support "Install for me only", with relative payload paths and domains enable_currentUserHome. Firmware-like files make the script refuse to build. There is a licence preamble, welcome and readme, and an au-postinstall script.
- NEEDS-RADEK: unsigned and not notarised. On current macOS (15 and later, including Tahoe) a downloaded .pkg is blocked on double-click. The user has to go to System Settings > Privacy & Security > Open Anyway. The site says "right-click > Open", which is outdated for 15+ and Tahoe. Unnotarised plug-ins can also be refused by DAWs.
  Fix: the Apple Developer renewal, then Developer ID Installer plus notarytool, as the plan already says. Until then the site FAQ and release notes should describe the Open Anyway path. The scripts/macos/INSTALL-macOS.txt text is for the old ZIP, not the .pkg.
- NEEDS-RADEK: Windows. CI builds VST3 and Standalone, but the site, README and release notes say "macOS only" and no Windows installer is attached to the release. Decide if the release is macOS-only (current claim, accurate). installer/nsis is upstream's.

## 4. User docs
- FAIL: README is stale on ROM install. It says to drag your ROM onto the editor window. Commits 63d9be1c9 and 0b0762618 removed drag-and-drop: the ROM is chosen with the file chooser only.
- FAIL: README "Downloads" in the appended upstream section points to joelanders' releases. The top section is fine.
- FAIL: the site lists "MIDI learn, app LFOs" in Features, and the Control tour tile reads "MIDI learn, app LFOs and random sources". Commits b7b752cc0, 61a4e4094 and 715b23f80 hide MIDI mapping, the Control tab and the LEARN key until a controller feature exists. The v0.2.1 notes still describe Control All, which is a different feature and still present.
  Fix: remove the claim or restore the feature. Also check that the Control screenshot (shots/*control*) is not shown.
- NEEDS-RADEK: the site says "Six workspaces". Confirm that still holds after the Control tab was hidden (it is probably five).
- PASS: release notes v0.1.0-alpha, v0.2.0 and v0.2.1 exist and are accurate for their tags. doc/release/drafts.md is internal and carries launch drafts (Discord post, message to joelanders); it was not reviewed in full.
- No standalone user manual or quick-start was found. doc/manuals holds only the Elektron PDFs and md_manual.txt. In-app help: only a ROM start-up card, no help page found.

## 5. Legal
- PASS: LICENSE.md is GPL-3. The licence preamble credits The Usual Suspects, joelanders and NativeKloud. The README credits joelanders and dsp56300/gearmulator, and carries the Elektron trademark disclaimer. The site footer links the GPL-3 and the source.
- PASS (ROMs): no .rom or .bin firmware file is tracked. The only .syx files are generated test corpora, whose README says "No factory content, no ROM data". The largest blobs in history are .psd and skin PNGs from upstream (about 20 MB, not ROMs). The build script refuses to package firmware. A brief search, not an exhaustive scan.
- PASS: the mdStudio fonts (Barlow Condensed, IBM Plex Mono, Silkscreen) ship with OFL texts. Roboto is Apache.
- NEEDS-RADEK: the .pkg and the site do not ship the OFL texts, only the source tree does. The bundled fonts are OFL so shipping them in a binary is permitted. I did not verify the OFL notices are included in the installer resources.
- NEEDS-RADEK: the site loads no third-party fonts or analytics (no googleapis found).
- NEEDS-RADEK: the site contact address is a personal gmail (radekdymacz@gmail.com) on a public page. Confirm that is intended.
- NEEDS-RADEK: README and the site's "Free"/"name your price" text are consistent. Lemon Squeezy is not yet configured, so no payment link exists.

## 6. site/
- PASS: mdmm.dev, /get/, /privacy/, /terms/, /refunds/ and /contact/ all return 200. The download URL redirects and returns 200, serving the alpha .pkg files.
- FAIL: the deployed config.js says version "0.1.0-alpha" while the repo says 0.2.0. Redeploy is needed after the release (Radek's step).
- FAIL: download links serve the 0.1.0-alpha .pkg because 0.2.x are still drafts. Publishing the release (a draft publish) flips "latest". The 0.2.0 draft should be deleted so it is never published.
- FAIL: feature claim on MIDI learn (see 4).
- PASS: HW MIDI is labelled beta and "not tested on a real machine". ROM requirement is stated: Machinedrum OS 1.63, Monomachine OS 1.32B. The "About half a CPU core per instance" limit is stated.

## 7. AGENTS.md (untracked)
- It is a copy of CLAUDE.md with "Claude Code" replaced by "Codex" in the title and intro. It has no repo-specific content of its own (the upstream Gearmulator overview and Visual Studio build notes). It was probably generated by a Codex session or tool.
- Verdict: do NOT commit as is. It describes upstream Gearmulator (VS 2026 on Windows) and says nothing about the MD/MM fork or the mdmm-v* flow. Either add it to .git/info/exclude or delete it. If you want Codex support, write a real one that points to CLAUDE.md and doc/modern-ux/FOUNDATION.md.

## Release blockers before tagging (in order)
1. Decide the version. Bump CMake, build_mdmm_pkg.sh and site config.js. Write the notes covering the 28 commits.
2. Fix README (ROM chooser, downloads link) and the site (MIDI learn, workspace count, Gatekeeper wording).
3. Check Windows CI flakiness.
4. Local firmware-tested build, upload to the draft, publish. Deploy the site.
5. Clean up the AGENTS.md leftover. Delete the stale 0.2.0 draft.
