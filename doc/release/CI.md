# CI and the development loop

Why: on 2026-10-07 and 10-08 there were 182 CI runs, and 0.3.5's Editors job ran 8 times on its release branch. The
same commit was tested on the release branch, again on main, again on the tag, and again in the local gate. A small
fix paid the price of a release. This file is the rule since 0.4.0.

## Dev loop: minutes, no tests

1. Work on a branch.
2. `scripts/mdmm-dev.sh play md` (or `mm`): builds that one standalone and opens it.
3. Radek plays with it and accepts it.
4. Merge to main.

During development Claude runs no test suites unless Radek asks. A test that guards the change at hand may be run on
its own (one target, seconds).

## What CI runs

| Event | Runs |
|---|---|
| Push to `main` | One light job in the background: automation core, Linux, unit tests, no memory checker. Nobody waits for it. |
| Push to `release/0.*` | The full suite once: Editors on macOS, Windows (LTCG, what ships) and Linux, core with ASan/UBSan and the transport diagnostics, audio I/O, shared controller. A newer push cancels the older run. |
| Tag `mdmm-v*` on the tested release-branch commit | Finds that branch run and reuses its packages: makes the installers, tests them, attaches them. No second build. |
| Nightly, 02:00 UTC (04:00 Polish summer time) | The full suite on main, only if main changed in the last 24 hours (`mdmm-nightly.yml`). Nobody waits; a red run is there in the morning. |
| By hand | Every workflow has Run workflow; the release installers have a dry run. |

Off: upstream's Elektron workflows (disabled in Actions), the per-push installer dry run.

## Release: once per batch

1. Cut `release/0.x.y` from main. Bump `MDMM_EDITOR_VERSION`, write `doc/release/v0.x.y.md`, point the README at it.
   If the emulation changed since the PGO profile was trained (the build says "stale", below), retrain it on the
   branch first: `scripts/macos/train_mdmm_pgo.sh`, then commit the two files it writes.
2. Push the branch. The full CI runs once.
3. `scripts/mdmm-local-gate.sh` once on the Mac with the firmware.
4. A failure: fix it, push, and re-run only the failed stage (`MDMM_GATE_ONLY=<stage>`; in Actions, Re-run failed jobs).
5. Tag the branch head (`git tag -a mdmm-v0.x.y -m "Machinedrum Editor + Monomachine Editor 0.x.y"`) and push the tag.
   The tag must be on the commit CI tested, or it builds again.
6. Merge the branch into main and push main (the site workflow reads the notes from main).
7. Publish the release when the tag run has attached the files.
8. Deploy the site at once (`cd site && CLOUDFLARE_ACCOUNT_ID=331224f43e4f448483cad2f1185ea965 npx --yes wrangler@4 deploy`;
   Radek runs it until CI has the Cloudflare secrets). The Discord announcement waits up to an hour for the release
   page to be live, so its link preview has the image.

## PGO (profile-guided optimisation, macOS)

The macOS editors are built with a profile of the emulation at work (lever L6,
[RESEARCH-emulation-cpu.md](../modern-ux/RESEARCH-emulation-cpu.md) "L6 measured": 7.5 % less CPU on the Machinedrum, 5.5 % on the Monomachine, the same
audio bit for bit). CI never has the firmware, so the profile is trained on the Mac and committed:

- `scripts/macos/train_mdmm_pgo.sh` (needs the ROMs, about 3 minutes, silent) builds `mdmmPerfGateTest` instrumented,
  plays every scenario of both machines, and writes `source/elektron/md/pgo/mdmm-macos.proftext` (LLVM's text
  profile: function names, hashes and counts) and `mdmm-macos.json` (the commit, the git tree of each profiled folder,
  the compiler, the workload). It proves the profile holds no firmware before writing it
  (`scripts/macos/mdmm_pgo_profile.py check`: every line parses as profile data, and no run of 31 bytes of either
  ROM is in it); CI repeats the parse on every build.
- The build (`GEARMULATOR_MDMM_APPLE_PGO_MODE=committed`, the default of `scripts/macos/build_mdmm.sh`, CI and the
  local gate) converts the text with its own compiler's `llvm-profdata` and uses it for both slices of the universal
  build. The receipt records `pgo_mode: committed`, the profile's SHA-256 and the stale folders.
- **Stale:** when a profiled folder (mdLib, mc68k, dsp56kEmu, dsp56kBase) changed since training, the build warns
  (a CI annotation "PGO", the local gate's stage 1 line) and still uses the profile: clang ignores the counts of every
  function that changed. Retrain before a release. **Missing** (no profile, no `llvm-profdata`): the build warns and
  is the plain ThinLTO build.
- Windows and Linux are built without PGO: MSVC's `.pgd` and GCC's `.gcda` belong to the exact build that was
  trained, so neither can be trained on the Mac and reused in CI.
