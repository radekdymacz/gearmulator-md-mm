# Rich Hickey review: the MD and MM Editors (2026-10-02)

Scope: the fork's own code, `git diff release/md-mm-alpha...main` (483 files, about 95k lines), at 5636b716e.
Three read-only reviewers: simplicity, data and state, architecture. Generated files skipped. Paths are
relative to `source/elektron/md/` unless they start with `doc/`.

## Scorecard

| Dimension | Rating |
|---|---|
| Simplicity | MOSTLY SIMPLE at the C++ core, COMPLECTED at the page edge |
| Data orientation | MOSTLY DATA (the MM page is place-oriented) |
| Architecture | MODERATE |

## The root cause

The Monomachine Editor was ported by copying the mockup-centred style, not by growing the Machinedrum
Editor's document-and-intent core. Findings 1 to 3 below come from that one choice; settling it removes
most of the copy checks and the per-machine duplicates.

## Findings, most impactful first

1. **Two edit models for the same features (high, all three reviewers).** The MD page sends fine-grained
   pure edits (about 50 rows in `mdDesk/mdDeskModel.cpp`, applied in `mdDeskEdit.cpp`). The MM page edits the
   mockup's own state and sends whole documents (`mmAdapter.js` `{op:"set", kind, doc}`). Undo granularity,
   validation and ask-before-loss differ per machine, and every feature is built twice.
   Fix: one intent vocabulary for both, written into FOUNDATION.md.
2. **The MM page keeps a second mutable copy of the documents (high).** The mockup's `S` is the live truth;
   `DOCS` is reconciled by hand (`synced`, `wantApply`, `applyPending`, `busy()`, `captureKit/Pat`,
   `mmConvert` both ways). Fix: derive the MM view from documents plus an overlay, as `deriveView` does on
   the MD; keep `S` for UI-only state.
3. **Shared page code is shared by copying text (high).** MODAL, BOOT, SYX, KEYS and AUDIO-MIDI blocks are
   copied into the MD mockup, the MM mockup and the MD skin and policed by `modal_check.py` /
   `audio_panel_check.py`; the GEN block is cut out of `mdDeskGen.js` by `sed` (`mm-mockup/build.sh:11`) and by
   regex (`sync-mmstudio-skin.py:52`). Fix: `skins/shared/*.js|css` files that both builds concatenate; delete
   the checks; make `mdDeskGen.js` a whole pure module.
4. **The MM page decides echoes by wall-clock windows (high).** `mmAdapter.js:251,257`
   (`now() - last.muteMs > 1200`, `polyMs > 1500`). Fix: overlays keyed to command ids, as on the MD page, or
   pending/observed per field from the core.
5. **The keyboard's momentary PTCH is a hidden kit edit (medium-high).** `MdMachine::cmdKeyNote`
   (`mdDeskMachine.cpp:871-923`) writes a kit parameter with no `Expectation`, restores it by guessing
   (`now != sent ? now : before`), and a memory image taken while a key is held could be taken as truth
   (inferred, not measured). The page has to know the PTCH index. The MM keyboard sends raw MIDI instead: a
   second way to play notes. Fix: a `noteOn {t, vel, pitch}` intent with the pitch mapping in the adapter,
   and held overrides as a named transient layer the working copy masks.
6. **`mdDeskApp.js` is 2,852 lines in one scope (medium-high).** About 25 concerns (7 workspaces, canvas
   editors, sound-group tables, sampler, GEN/MUTATE, gestures, telemetry) and about 20 module-level gesture
   `let`s. Fix: one file per workspace (as Live, Library, Global already are), the tables as shared data with
   the MM's `85-sound-groups.js`, one gesture value `{kind, ...}` in one slot.
7. **Chain latest-wins written twice (medium).** `mdDeskMachine.cpp:925-1000` and
   `mmDeskMachine.cpp:831-930` (`m_chainQueued`, `keysOnTheirWay`); validation factored on the MD, inline on
   the MM. Fix: `deskCore::Latest<T>` / `KeyRunSlot<T>` beside `PushSlot`, and a shared `validateChain`; CLEAR as
   an explicit variant, not an empty list.
8. **`review()` hands data to `submit()` through a member (medium).** `m_intent` in
   `mdDeskMachine.cpp:352-360, 517`. Fix: `review` returns the intent and the core passes it to `submit`.
9. **The adapters are large mutable objects (medium).** `MdMachine` about 32 to 40 members, `MmMachine` about
   42, each feature with its own `pump*`, `-1e9` sentinels. The MM adapter is one 1,386-line file while the MD
   is split. Fix: group fields into state values with pure `step(state, event, now) -> {state, effects}`, as
   `WorkingCopy/fromImage` already do; split the MM along the MD's seams; lift the push pump into a deskCore
   `Pushes<Ref, T>` (and make the MM's SYSEX RECV a policy mode, not `{1e18, quietMs}`).
10. **The SysEx port is rebound during a sample send (medium).** `mdDeskMachine.cpp:139-196` swaps
    `m_port.sendSysex` for a buffering lambda. Fix: one explicit outbound arbiter (sample, held, normal).
11. **`cmd()` knows about features (medium).** `mdDeskApp.js:54-67` ends GEN runs and MUTATE trials and reads
    an ambient `gesture` global that callers save and restore. Fix: pass the gesture explicitly; features
    subscribe to "edit sent".
12. **CSS lives inside the Python sync scripts (medium).** `sync-mdstudio-skin.py:75-390` carries hundreds of
    lines of skin CSS and unasserted `replace` patches. Fix: `skins/mdStudio/overrides.css`, concatenated, every
    replacement asserted.
13. **The page bridge travels in URLs (medium).** `javascript:gm.recv(...)` one way, `gmbridge://c/<json>` the
    other; no size guard or acknowledgement. Fix: keep the `Bridge` API, isolate the transport (ready for
    JUCE 8's native bridge), chunk large batches.
14. **Run and trial state mutated in place, also in getters (medium-low).** `genSpecs()` rewrites
    `S.gen.specs` while reading; trial Sets mutated. Fix: replace values per apply; fit on read.
15. **Contract says open, schema is closed (medium-low).** `data-contract.md:38` "readers ignore unknown
    members" vs 119 `additionalProperties: false` (MM 62). Fix: keep commands closed; open document shapes; add
    a contract version to `ready`.
16. **Smaller:** machine checks by name prefix instead of a per-machine record (`controlAllReaches`,
    `isSampler`); capability tables twice (`CAP_CONTROLS` / `NA_SEL`); the MM host seam checked by regex scrape;
    `KNOB_CCS` constant mutated from a document; `Docs` updated in place.

## Recommended order

1. Decide the one edit model and the one view model (findings 1, 2, 4). A hammock session first: it
   decides the shape of everything after it.
2. Shared JS/CSS modules for both editors; delete the copy checks and the sed extraction (3, 12).
3. The keyboard as a note intent with a named transient layer (5).
4. deskCore mailboxes and pumps shared by both adapters (7, 9, 10, 8).
5. Split `mdDeskApp.js` by workspace and one gesture slot (6, 11, 14).
6. Contract versioning and open document shapes (15), then the small items (16).

## Status (2026-10-03)

What the follow-up work (DESIGN-UNIFY.md and its phases, then the remaining findings) did with each finding:

1. **Fixed.** One intent vocabulary for both editors (DESIGN-UNIFY.md 4.1): the MM page sends the MD's ops and its own,
   applied as pure edits in `mmDeskEdit.cpp`, the shared ones in `deskCore/deskEdits.h`; `intent-cases.json` pins both.
2. **Mostly fixed.** The MM view is derived from the documents plus an overlay keyed to command ids (`mmView.js`); the
   reconcile code is gone. S's document members remain, written only by `MMView.show` (checked); the renderers reading
   the view directly is DESIGN-UNIFY.md phase 8.
3. **Fixed.** `skins/shared/` files both builds load; the copy checks and the sed/regex GEN extraction are gone.
4. **Fixed.** No wall-clock windows: echoes by command id; mutes, POLY and tempo as core expectations.
5. **Fixed.** The note intent (`noteOn`/`noteOff`, `deskNotes.h`), the pitch mapping in the adapters, held PTCH as the
   working copy's `held` layer; the MM keyboard uses the same intent.
6. **Fixed.** `mdDeskApp.js` is about 300 lines; one file per workspace and concern; one gesture slot (`Held`).
7. **Fixed.** `deskCore::Latest<T>`, `ChainRequest` (CLEAR said, not an empty list) and `validateChain`, used by both.
8. **Fixed.** `review` returns `Reviewed<Intent>`; no member carries the intent.
9. **Fixed (state values partly).** `deskCore::Pushes<Ref, T>` with `PushPolicy` `Paced`/`Held` (SYSEX RECV a mode). The
   MM adapter is split along the MD's seams (delivery, load, commands, chain, notes, record) and its small states are
   values with pure steps (`mmDeskWatch.h`: playing from the step byte, RECORD read-backs, the transport message, the
   sounding notes). Not regrouped: the current slots and the expectations stay members (no natural value fell out).
10. **Fixed.** One outbound arbiter, `SysexOut` (`mdDeskSds.h`): a sample's packets, then what was held, then the rest.
11. **Fixed.** `cmd` knows no feature: the gesture is explicit (`args.g`), GEN and MUTATE end themselves from `onEditSent`.
12. **Fixed.** `skins/mdStudio/mdOverrides.css` (and the MM's `mmOverrides.css`), concatenated; every replacement asserted.
13. **Fixed.** The `Bridge` API stays; the transport is one module on each side (`BridgeTransport` in `deskBridge.js`,
    `mdPageBridge.h`): long page batches go in pieces joined before decoding, the outbox goes as `gm.recv` calls of at
    most 1 MB split at message boundaries. Today's batches fit, so nothing else changed. Tested both sides.
14. **Fixed.** GEN specs and the MUTATE trial are values replaced per change, fitted on read (`genSpecs`, `nextTrial`).
15. **Fixed.** The machine document carries `contract` (2 on both, `Model::contractVersion`, written into the schemas by
    `--write-schema`); what the plug-in writes is open (`additionalProperties: true`), commands closed; the tests hold
    our writer to the declared members (`json::Schema::closedForWriter`). The pages do not act on `contract` yet.
16. **Partly.** Machine facts are a record (`machineFacts`, no name prefixes); `KNOB_CCS` is frozen; one capability
    routine for both pages (`skins/shared/deskCaps.js`; each page's table and look are its own, as data and options);
    the MM host seam and view members are data (`src/53-seam.js`) read by the page, the sync check and `mmViewTest.js`.
    Still so: `storeDoc` updates the page's `Docs` in place (one writer, the store).

Also found and fixed on the way: LOAD KIT of a never-written MM kit slot plays it as "NEW KIT" (measured), so the kit
that plays read as edited right after the load; `mmKitAsLoaded` is now what the stored slot is compared with (and what
seeds the working kit from a dump). A MIDI track mute taken back at once (the MM self-test p4 does it) stayed muted:
`muteMidi` compared with memory alone while the first keys were still on their way; it now compares with what the
machine will have (the expectation). The MD page re-enables a control it had disabled once the capability is back.

## Done well

- **Tables drive the system.** Command rows `{op, owner, gate, kind, args}` and `KindSpec` generate the
  schema, the router's argument checks and tests that fail in both directions.
- **The core's epochal model.** `DocState{observed, pending}`, whole-document values, history of before/after
  values (no command objects), results published after the documents they changed.
- **Effects at the edges.** Engines queue device input and hand it over in `step()`; `deskWire` is pure and
  header-only; capabilities derive from which `DevicePort` functions exist, never from the engine id.
- **Pure, clock-free transitions where it matters** (`WorkingCopy/fromImage`, `PushSlot`, `PushPolicy`).
- **Asks are values**, resent by the page with `force`; the MD page view is a pure `deriveView` with overlays
  keyed to command ids; unknown firmware bytes ride through untouched.

*"Simplicity is a prerequisite for reliability." — Rich Hickey*
