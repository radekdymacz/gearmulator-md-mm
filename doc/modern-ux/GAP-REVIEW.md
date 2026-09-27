# MD Desk — gap review

Read-only review of the MD Desk mockup (`doc/modern-ux/mockup/index.html`, 1,620 lines, MD-only)
against (1) what the emulator/plugin already builds, (2) what the Machinedrum OS 1.63 manual
documents, and (3) the prior P0/feasibility work in this same folder. Date 2026-09-27.

Method: the mockup's state object `S` and its `render*`/event-handler functions were read in full
(lines 894–1620). Source citations are `file:line`. Manual citations use the printed page numbers
from the TOC/footers of `doc/manuals/machinedrum_manual_OS1.63.pdf` (extracted to text with
`pdftotext -layout`).

---

## 1. Emulator/plugin features the design ignores

### 1.1 Monomachine is entirely out of scope
The mockup is Machinedrum-only: `<title>Machinedrum Desk</title>` (`mockup/index.html:2`), a single
`KIT` array of MD machines (`mockup/index.html:952-953`), and the MKI/MKII plate toggle only
recolours the same MD layout (`mockup/index.html:21-25, 248-252`) — it never becomes a 6-track MM
kit with 3 trig tracks (AMP/FILTER/LFO) or an arpeggiator. The emulator supports MM as a first-class
target (`source/elektron/md/mdLibTest/mmSysexExportFirmwareTest.cpp`, per `code-review.md:60`), and
P0 got MD to GO status with MM explicitly blocked pending a ROM (`P0-RESULT.md:11,17`).
**Recommendation:** MD Desk needs either a second, MM-flavoured "Desk" (different track count,
different trig-track model, arpeggiator panel) or an explicit scope note that v1 is MD-only and MM
is a separate design pass — do not bolt MM onto the current per-track data model, which is
structurally MD-shaped (`syn`/`fx`/`rt` three 8-slot pages, one LFO per track).

### 1.2 Patch Manager (cross-synth preset browser/search/tagging)
`source/jucePluginEditorLib/patchmanager/patchmanager.h:39` — `class PatchManager : public
pluginLib::patchDB::DB, juce::Timer`, with favourites/MIDI-banks/local-storage/factory/data-source
groups (`patchmanager.h:44`) and a dirty-patch signal (`patchmanager.h:47-48`). This is the shared,
cross-emulator preset system (search, tags, favourites, program-change-driven load) that every other
synth in this repo exposes. MD Desk has no equivalent: its only "browse" surface is the per-track
machine picker (`mockup/index.html:1370-1388`) and a single hard-coded `S.tracks`/`S.mfx` kit with no
saved-kit list, no favourites, no search.
**Recommendation:** a "Kit" browser workspace (or a panel inside Sound) backed by the same
`PatchManager`/`patchDB` abstraction the rest of the plugin already uses, so MD kits get search,
favourites and program-change-triggered loading for free instead of a bespoke MD-only mechanism.

### 1.3 Program Change routing
`source/jucePluginLib/programChangeRouter.h:22-33` — `ProgramChangeRouter` with per-part bank/program
request queueing and a `NotifyFunc` callback, wired through `controller.cpp` and `processor.cpp`
(`grep` hits above). This is how a DAW's PC automation loads a kit/patch. The manual documents the
hardware side of this too: PRG CHANGE IN/OUT plus a receive channel is a Global setting
(`md_manual.txt:3171-3193`, page 1-60). MD Desk has no program-change UI at all — no indicator of
which program number the current kit maps to, no way to view/edit the PC-to-kit table.
**Recommendation:** surface program-change mapping either in the new Kit browser (1.2) or in a
Global/Settings workspace (see 1.6); at minimum, show the current PC number in the top bar next to
the pattern/kit LCD fields.

### 1.4 MIDI Learn (controller mapping + presets)
`source/jucePluginLib/midiLearnManager.h:13-30` — a full preset system for MIDI Learn mappings
(save/load/rename/delete named presets), plus `midiLearnMapping.*`, `midiLearnTranslator.*` and a
settings page `settingsMidiLearn.cpp`. The mockup's "Control" workspace and its own `LEARN` key
(`mockup/index.html:857`, `S.ctl.learn` at `mockup/index.html:1436-1451`) are a **parallel, app-only**
mapping system: it maps *app-simulated* CC sources (`cc21..cc28`, an "app LFO", an "app random"
generator — `mockup/index.html:1437-1438`) to track parameters, entirely inside the mockup's own
state, with no persistence and no relationship to the plugin's actual MIDI Learn feature or its saved
presets. A user opening the real plugin's MIDI Learn (hardware controller → parameter) and MD Desk's
"Control" workspace (app LFO/random → parameter) would find two unrelated, similarly-named systems.
**Recommendation:** either (a) rename the workspace to something that doesn't collide with MIDI
Learn (e.g. "Modulators" or "Macros") and clearly label it as app-only automation that isn't part of
the saved kit (the mockup already has one honest line about this — `mockup/index.html:1466` "It does
not play on a real Machinedrum" — but the workspace *label* "Control" plus the hardware-styled
`LEARN` key strongly implies otherwise), or (b) fold real hardware MIDI Learn into the same
workspace as a second tab so there is one "Control" surface, not two competing metaphors.

### 1.5 Settings pages (device, audio, MIDI routing, GUI/skin, DSP bridge)
`source/jucePluginEditorLib/`: `settingsCategories.*`, `settingsDeviceSpecific.*`,
`settingsDspAudio.*`, `settingsDspBridge.*`, `settingsGui.*`, `settingsMidi.*` (with a MIDI routing
matrix, `settingsMidiMatrix.*`, and panic buttons `settingsMidi.h:29-31`), `settingsPlugin.*`,
`settingsSkin.*`. None of this exists in the mockup: no audio/MIDI device picker, no DSP
bridge/remote-device settings, no skin switcher, no "All notes off"/panic. The mockup's only
persisted preference is the MKI/MKII plate colour in `localStorage`
(`mockup/index.html:1605-1606`).
**Recommendation:** MD Desk needs a Settings entry point (gear icon or a 7th workspace) that at
minimum reuses `SettingsMidi`'s panic actions and exposes skin/device selection — these are expected
in every other product skin in this repo and their absence will read as broken, not minimal.

### 1.6 Global hardware settings (base channel, Map Editor, local control, routing, sync)
The manual's GLOBAL EDIT menu (base channel `md_manual.txt:3063-3098`/page 1-58, MAP EDITOR
`:3105-3113`/page 1-59, LOCAL CONTROL `:3154-3166`/page 1-60, PROGRAM CHANGE `:3171-3193`/page 1-60,
TRIG IN A/B page 1-62, output routing) is a device-level configuration surface entirely separate from
kit/pattern/song editing. Nothing in MD Desk maps to it — the mockup's "Control" tab is the app-only
modulation-macro system from §1.4, not hardware Globals. The parameter/CC model that exists in the
plugin today (`code-review.md:42-51`) doesn't reach Global settings either (only Global 0x50/51 and
Kit 0x52/53 dumps are requested, per `code-review.md:60`).
**Recommendation:** add a "Global" or "Device" workspace (or a tab inside Settings) for base MIDI
channel, program-change in/out, local control, trig-in A/B, output routing and the Map Editor — this
is real hardware state the user can currently only reach via the physical panel emulation.

### 1.7 TurboMIDI / SysEx transfer status and pacing
`doc/turbomidi.md` and `mdturbomidi.cpp` implement a speed-negotiation protocol for bulk SysEx
transfer (cited in `code-review.md:73`, `P0-RESULT.md:108`). Every dump-based edit in the
recommended architecture (`code-review.md §6`) costs tens to hundreds of milliseconds and, per
P0, the emulator paces transfers at 8–10× real TurboMIDI speed while the *real* wall-clock cost on
hardware would be ~1.7 s/pattern (`P0-RESULT.md:100`). MD Desk's mockup shows every edit as
instantaneous (`setV`/`setLock` mutate state and re-render synchronously, `mockup/index.html:996,
980`) with no busy/pending affordance anywhere.
**Recommendation:** even though the emulator itself is fast, a real product must budget for (a) a
visible "sending…" state on any control that triggers a dump-sized push, and (b) graceful behaviour
if TurboMIDI negotiation fails and the transfer reverts to slow DIN pacing. See also §4 (unsaved/busy
states) and §3.1 (why this can't just be hidden).

### 1.8 MCP server / Lua scripting surfaces
`doc/mcp_server.md` documents an HTTP+SSE control server (parameters, `send_midi`, `send_sysex`,
opaque `get_state`/`set_state`, screenshots — `code-review.md:62`) and `doc/lua_scripting.md`
documents a skin-side scripting API (`params.get/set/getText/getInfo`, `code-review.md:63`). MD Desk
doesn't reference either. These are automation/tooling surfaces (useful for testing MD Desk itself,
per the P0's `GEARMULATOR_MDSTUDIO_SELFTEST` harness, `P0-RESULT.md:144`) rather than end-user UI, so
this is lower priority, but MD Desk's own eventual test/CI story should plan to drive it through
whatever the P2+ transport becomes (`code-review.md:196-208`), not through MCP, since MCP today
"cannot read machine output" (`code-review.md:68`).
**Recommendation:** no UI needed; note in the design doc that MD Desk's automated tests will need a
dedicated transport (per the P2 JSON contract), not MCP, because MCP has no push channel today.

### 1.9 Multi-output routing (per-track direct outs)
`mdPluginProcessor.cpp:516-534` (`isBusesLayoutSupported`) supports a main stereo bus plus additional
stereo output buses (the loop over `_layout.outputBuses.size()`, capped at 3 buses in the snippet
read). MD Desk's Mix workspace does model per-track "OUT" assignment (`OUTS=["MAIN","A"..."F"]`,
`mockup/index.html:1099-1107`), which is good — but it never reconciles the UI's 7-way `OUTS` list
against what the host/plugin's bus layout actually negotiated with the DAW (mismatched bus count is
silently possible: the code only asserts 3 buses in the sample read, so some `OUTS` entries may not
have a real destination bus in a given host).
**Recommendation:** query the live plugin bus layout and grey out (with an explanatory tooltip)
`OUT` choices the host hasn't provided a bus for, instead of always offering all seven.

---

## 2. Hardware workflows the design lacks (from the manual)

| Workflow | Manual reference | Where it belongs in MD Desk |
|---|---|---|
| **Kit-per-pattern auto-load in EXTENDED mode.** Selecting a new pattern automatically recalls its associated kit; two patterns sharing a kit won't reload it. | p.16 ("kits are connected to patterns… Selecting a new pattern will then automatically load the associated kit", `md_manual.txt:1039-1040`); repeated warning about losing unsaved kit edits on pattern change (`md_manual.txt:1929-1932`) | This is structurally missing: `S` holds exactly one kit (`S.tracks`, `S.mfx`) and one pattern's trigs/locks. `S.pat`/`patPrev`/`patNext` only change the LCD's displayed number (`mockup/index.html:1571-1572`) — no kit swap, no "kit changed under you" warning happens. Needs modelling patterns as a collection, each with a kit reference, and a loud "this pattern will load a different kit — unsaved changes to the current kit will be lost" confirmation, mirrored from the manual's own repeated warning. |
| **Pattern change while playing takes effect only at the end of the current pattern**, shown as a pending/cued state on the display. | p.37, "If pattern selection is done while the sequencer is playing, the selected pattern will not be activated until the current pattern has played to its end" (`md_manual.txt:2022-2024`) | Not modelled at all — see above; `patPrev`/`patNext` mutate `S.pat` immediately with no concept of "cued next pattern" vs "currently playing pattern". Needs a distinct LCD field for "next" vs "now", exactly like the hardware. |
| **Pattern chaining** (hold BANK + press TRIGs to queue a run of patterns from one bank; STOP/PLAY resumes at the cued pattern). | p.37, `md_manual.txt:2026-2035` | Missing entirely; this is different from Song mode. Best fit: a lightweight overlay on the pattern-select LCD area in the top bar, or a mode of the pattern grid in Song workspace distinct from full song rows. |
| **The MUTE window** — dedicated overlay for live muting, shrinks to a small HUD when a knob is touched. | p.44–45, `md_manual.txt:2378-2417` | manual-mapping.md already flags this (`manual-mapping.md:157-159`) as "worth replicating as a persistent corner widget, not just a modal" — MD Desk currently only has per-track M/S buttons in the rail/header (`mockup/index.html:1010-1012`), no dedicated always-reachable mute overlay. |
| **Tap tempo.** | p.36, `md_manual.txt:1963-1970` | The BPM field is drag/keyboard-only (`mockup/index.html:1588-1590`); no tap-tempo gesture (e.g. repeated click/keypress averaging interval) anywhere. |
| **Copy / Clear / Undo at track, track-page and pattern granularity**, and the same for kits and samples. | pp.41-45 (CLEAR TRACK / CLEAR TRACK PAGE / CLEAR PATTERN, `md_manual.txt:2255-2273`+), p.16 (UNDO KIT, `md_manual.txt:1124,1163-1174`), p.43 (paste + undo, `md_manual.txt:1316`), sample manager copy/clear/paste/undo (p.70, `md_manual.txt:3656`) | No copy/paste/clear/undo of any kind exists in the mockup at any granularity (see §4 for the cross-cutting version of this gap). This is the single largest missing category relative to the manual. |
| **+Drive Snapshot Manager** (128 snapshots, each bundling patterns/kits/songs/globals/samples). | p.77, `md_manual.txt:426-443` | manual-mapping.md flags this too (`manual-mapping.md:160-163`). No snapshot/session browser exists; the mockup has one implicit "session" only. |
| **Sample Manager** (UW): receive/rename/erase/RAM→ROM copy, with its own copy/clear/paste/undo. | p.70, `md_manual.txt:3611-3684` | The Sampler workspace mocks "Send"/"Rename"/"Copy RAM to ROM" as inert toasts (`mockup/index.html:1346,1356-1357,1530`) — real actions are entirely unimplemented, which is fine for a mockup but should be flagged as the largest remaining Sampler workspace gap. |
| **PARAMETER TWEAKING** (FUNCTION + knob moves one parameter across every track at once). | p.37, `md_manual.txt:2041-2052` | Not modelled; would be a natural "all tracks" modifier on the per-track knob drag gesture, with the manual's own caveat that it excludes MIDI/RAM-recorder tracks and doesn't restore symmetrically. |
| **Global settings**: base channel, Map Editor, local control, program change in/out/channel, trig-in A/B, output routing. | pp.58–62 | See §1.6 above — no workspace covers this. |

---

## 3. Design features the emulator/firmware cannot (yet) support, or that are risky

### 3.1 Everything is instant; the real machine is not
The mockup's model is a plain in-memory JS object mutated synchronously
(`setV`/`setLock`/`songAction`/etc., all direct `S` mutation followed by immediate re-render). But
per the code-review's own recommended architecture, the firmware is meant to be the source of truth,
and edits are **pattern/kit SysEx dumps**, not instant local writes (`code-review.md:187-208`). P0
measured this at ~17–25 ms in the *emulator* for a full pattern dump with no baud pacing
(`P0-RESULT.md:83-101`), which is fine, but:
- The MM path is not confirmed live-editable at all: MM only accepts dumps on the GLOBAL > FILE >
  SYSEX RECV "WAITING" screen (`code-review.md:60,103`; `P0-RESULT.md:103-114`), which is unresolved
  even for MD Desk's future MM support (§1.1). Any UI affordance implying "drag a knob, it's saved to
  the kit" for MM must not exist until this is solved (RAM writes or a panel-macro round trip).
- **Risk:** the mockup's instant-apply model, if built literally against real dumps, will either (a)
  silently coalesce/drop rapid edits (a knob drag firing dozens of full-pattern dumps a second would
  overflow MIDI RX — `code-review.md:122`, `midiRxOverflowCount`), or (b) require debouncing that the
  mockup gives no visual indication of ("is my last drag committed yet?").

### 3.2 P-lock resolution is quantised to firmware precision, not continuous
The mockup's lock lane and curve editors (`ED.synth`, `ED.fx`, `ED.route`, etc., lines 1210-1251) draw
continuous curves and let the user drag a step's lock to any of 128 values, which matches the real
p-lock format (one 7-bit value per step per parameter — `manual-mapping.md:22-24`, `code-review.md
§3(a) "p-locks are one 7-bit value per step per parameter, so drawn automation quantises to
steps"`). This part is fine. The **risk** is elsewhere: the mockup's `ED.lfo`/`ED.echo`/`ED.gate`
etc. curve editors invite the user to think of these as smoothly draggable continuous shapes, but
every value is still a discrete knob (0–127) under the hood — the manual-mapping doc's own warning
applies: "Curve editor must still resolve to the same 3 discrete knob values on save — don't invent
continuous curve shapes the hardware can't encode" (`manual-mapping.md:181`). Nothing in the mockup's
canvas code currently invents an unencodable shape, but there is no guard preventing a future contributor
from adding one (e.g. free-drawn multi-point envelopes) — worth a stated design rule and perhaps a
lint/test that every curve's handle set matches `pages(track.m)` 1:1.

### 3.3 The 64/62-lock pool is enforced, but only as a soft warning
`setLock()` (`mockup/index.html:980`) refuses new locks past 64 entries and shows a toast, which is
correct behaviour (manual: "A pattern can lock 64 parameters in total, shared by all tracks" —
`mockup/index.html:874`; MM's cap is 62, `manual-mapping.md:46`). This works today because MD Desk
only ever has one pattern loaded. Once multiple patterns exist (per §2's kit-per-pattern gap), the
64-lock ceiling is *per pattern*, and `S.locks` would need to become per-pattern too, with a
re-render on pattern switch — currently `S.locks` is a single flat `Map` for the whole app
(`mockup/index.html:966`), which will not scale to more than one loaded pattern without a rework.

### 3.4 Locks require a trig; deleting a trig silently drops its locks
The mockup does enforce this correctly (`clearStep` at `mockup/index.html:983`, called whenever a
trig is toggled off, e.g. `mockup/index.html:1516,1527`), matching the manual rule "A step with no
trig cannot hold a lock, and removing a trig removes its locks" (`mockup/index.html:875`). No gap
here — noted as a place the mockup already got right, to avoid a future regression.

### 3.5 CLASSIC vs EXTENDED lock persistence is shown but not testable end-to-end
The mockup shows a "CLASSIC: locks muted" warning line (`mockup/index.html:1027`) and keeps lock data
in `S.locks` regardless of mode, matching "In CLASSIC mode... locks persist silently... reactivate in
EXTENDED" (`manual-mapping.md:170-172`, confirmed by manual `md_manual.txt:1916-1918`). This is a case
where the mockup already reflects the hardware correctly — flagged here only so it isn't accidentally
"fixed" (i.e. locks must not be cleared on a CLASSIC/EXTENDED toggle).

---

## 4. Cross-cutting UX gaps

- **Undo/redo: entirely absent.** No undo stack, no `Ctrl+Z` handler, no per-action history exists
  anywhere in the mockup's ~1,600 lines. Every mutation (`markDirty()` call sites) is a one-way
  street. This is the single biggest gap versus the manual, which documents undo at kit (p.16),
  track/track-page/pattern (pp.41-45), song-paste (p.43) and sample-manager (p.70) granularity. A
  screen-native editor without at least one level of undo per workspace will feel like a regression
  from the hardware, which has undo everywhere.
- **Copy/paste: entirely absent.** No clipboard concept for a step, track, track-page, pattern, kit,
  song row or sample slot. The manual makes copy/paste a first-class citizen at nearly every
  granularity (§2 table). Recommend at minimum: copy/paste a track's full state (trigs+locks+kit
  params), copy/paste a pattern, copy/paste a song row (`songAction("dup")` exists for song rows only
  — `mockup/index.html:1165` — nothing else has an equivalent).
- **Keyboard shortcuts: minimal and partly workspace-blind.** What exists: `1`-`6` switch workspace
  (`mockup/index.html:1613`), `L` toggles Learn, `Space` toggles play, `[`/`]` page the sequencer,
  arrow keys nudge a focused control or a segbar, `Delete`/arrows work only in Song. There is no
  discoverable shortcut list/help overlay, no shortcut for mute/solo, no shortcut for save, no
  shortcut for switching selected track (beyond clicking).
- **Unsaved-state handling is a single global flag, and it conflates two different things.**
  `S.dirty`/`markDirty()` (`mockup/index.html:990`) sets a single boolean with no path back to
  `false` anywhere in the file (no save handler exists — the `.save` dirty dot can only ever turn on
  in this mockup) and no distinction between (a) "kit/pattern/song not yet sent to the machine as a
  SysEx dump" and (b) "project not yet saved in the DAW/standalone app". These are genuinely
  different operations with different failure modes (per §3.1, a dump can fail/overflow; a DAW
  project save cannot talk to MIDI at all) and the current single dot conflates them. Needs two
  indicators, or one indicator with a tooltip that says which of the two is pending.
- **Pattern change while playing:** see §2 — not modelled; `S.pat` changes render instantly with no
  "cued, takes effect at pattern end" state, unlike the hardware (`md_manual.txt:2022-2024`).
- **Kit change on pattern switch:** see §2 — not modelled; no warning for losing unsaved kit edits.
- **Error states: none exist.** No modelled state for "firmware/ROM not loaded", "device busy mid-dump",
  "SysEx transfer failed/timed out", "MM stuck on WAITING screen" (a real, documented risk —
  `P0-RESULT.md:103-114`), or "MIDI port disconnected". Every control in the mockup assumes the
  machine is present, booted and idle. A real product needs at least a global banner/blocking state
  for "no ROM loaded" and a per-action failure toast for a dump that didn't round-trip.
- **Onboarding / first run (ROM loading):** the plugin cannot ship or fetch firmware — "DO NOT ask us
  for the .bin files! They're under Elektron's copyright" (`README.md:7-8`) — and only accepts an
  exact fingerprint match for MD 1.63 / MM 1.32b (`mdromloader.cpp:26-51`, per `code-review.md:23`).
  MD Desk's mockup has zero onboarding UI: it assumes a fully-populated kit and pattern exist from
  first paint. A real first-run needs a "load your own ROM dump" flow with a clear explanation of why
  (legal), a fingerprint-mismatch error state, and a friendly empty-state before any ROM is loaded.
- **Accessibility:** ARIA roles/labels are generally good throughout (`role="slider"`,
  `aria-pressed`, `aria-label` on nearly every custom control). Gaps: (1) the canvas curve editors
  (`ED.synth`, `ED.fx`, `ED.lfo`, `ED.route`, `ED.echo`, `ED.gate`, `ED.eq`, `ED.dyn`) expose draggable
  "handles" only via pointer events (`main.addEventListener("pointerdown"...)`,
  `mockup/index.html:1494-1508`) with no keyboard path to move a handle — a keyboard user can reach
  the underlying `.pc` knob for the same parameter in some cases (e.g. FLTF/FLTQ have `.pc` sliders in
  the `.ctl` grid alongside the canvas) but not for parameters that are canvas-only-visualised drag
  targets sharing one handle for two values (e.g. `ED.route`'s combined PAN·VOL handle,
  `mockup/index.html:1249-1250` — VOL has no separate keyboard-operable control in that section other
  than the Mix workspace's fader). (2) Colour is used as the sole differentiator for accent/slide/lock
  markers on trig cells beyond a small icon — fine given the icons exist, but no explicit
  colour-contrast or colourblind-mode note exists anywhere in the design. (3) No reduced-motion
  consideration beyond a blanket `@media (prefers-reduced-motion: reduce){*{transition:none}}` in the
  CSS (seen in the raw file preamble) — the playhead's blink animation is separately guarded
  (`.meter.full b{animation:blink...}` + a matching reduced-motion override), which is good, but the
  step/lane playhead highlight relies only on class toggles with CSS transitions, not verified against
  reduced-motion.
- **No save/export path at all.** There's no button anywhere in the 1,620 lines that writes the kit,
  pattern or song back out (to the machine as SysEx, or to a project file). This is presumably because
  the mockup predates the transport layer (P0/P1 in `code-review.md`), but it means the "not saved"
  dirty indicator is decorative in the current mockup — it can turn on but nothing turns it off.

---

## 5. Prioritised list

### Must-have for v1
- **Model patterns as a collection with per-pattern kit association and per-pattern lock pool**, and
  make pattern-change-while-playing a cued/pending state, not instant — this is the single biggest
  structural gap (§2) and blocks everything else about song/pattern workflows being credible.
- **At least one level of undo, scoped per workspace action** — the manual has undo at nearly every
  granularity; shipping with none will feel broken to anyone who has used the hardware (§4).
- **Split the "dirty" indicator into dump-pending vs project-unsaved**, and actually wire a save/send
  path for at least one of them — right now dirty can only ever turn on (§4).
- **A visible pending/busy state for any edit that becomes a SysEx dump**, given P0's own finding that
  dumps take tens of milliseconds in the emulator and ~1.7 s on real hardware, and that MM can't even
  receive dumps outside a special screen (§1.7, §3.1).
- **An onboarding/empty state for "no ROM loaded" and a fingerprint-mismatch error state** — the
  mockup currently assumes a fully populated machine from first paint, but the plugin cannot ship
  firmware and only accepts an exact match (§4).
- **Copy/paste for at least track and pattern granularity** — copy/paste is used constantly on the
  real hardware and has zero mockup coverage today (§4).

### Should-have
- **Rename or merge the "Control" workspace so it stops colliding with the plugin's real MIDI Learn**
  feature — right now there are two same-named, unrelated mapping systems (§1.4).
- **A Kit browser backed by the shared `PatchManager`** instead of a single implicit in-memory kit,
  so favourites/search/program-change loading come for free (§1.2, §1.3).
- **A Global/Device settings workspace** for base channel, program change in/out, local control,
  trig-in A/B, output routing and the Map Editor (§1.6).
- **A persistent Mute-window-style overlay** distinct from the per-track M/S buttons, matching the
  manual's "shrinks to a HUD" behaviour (§2).
- **Pattern chaining** (BANK+TRIG cueing, distinct from full Song mode) (§2).
- **Keyboard access for canvas curve-editor handles that have no equivalent `.pc` control** (§4).

### Later
- **Monomachine support** — correctly deferred; needs its own design pass given the different track
  model (12 tracks, 3 trig sub-tracks, arpeggiator, POLY mode) and the unresolved MM WAITING-screen
  question (§1.1, §3.1).
- **+Drive Snapshot Manager equivalent** (session/snapshot browser bundling patterns/kits/songs) (§2).
- **Sample Manager real actions** (send/rename/RAM→ROM copy are currently inert toasts) (§2).
- **Tap tempo and PARAMETER TWEAKING (all-tracks knob move)** — real hardware features with no
  mockup equivalent, but lower priority than the structural gaps above (§2).
- **Multi-output bus-layout reconciliation** in the Mix workspace, so `OUT` choices reflect what the
  host actually negotiated (§1.9).
- **MCP/Lua-driven automated testing plan** for MD Desk itself, once the P2 JSON-contract transport
  exists (§1.8).
