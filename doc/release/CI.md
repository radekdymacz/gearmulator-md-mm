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
