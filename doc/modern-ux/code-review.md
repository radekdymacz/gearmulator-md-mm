# Feasibility: a screen-native UI for the MD/MM emulator (gearmulator-md-mm)

Repo: `gearmulator-md-mm` (shallow clone, HEAD `8cea052`; submodules not initialised; JUCE/dsp56300/mc68k/RmlUi sources therefore not inspected). All paths below are relative to the repo root. Read-only review, date 2026-09-27.

**Bottom line:** it is feasible. The firmware is the only thing that knows how to play a Machinedrum pattern correctly, so the recommended design keeps the **firmware as the sequencer**. Our UI owns a *decoded document model* of Elektron's own pattern, kit and song SysEx. Structural edits go back to the machine as SysEx dumps. Live knob moves use the existing CC parameter layer. A telemetry channel (LED transitions now, RAM reads later) drives the playhead.

The expensive, unknown part is not the UI. It is three things:
1. A byte-exact MD/MM pattern+kit codec. The repo has none today.
2. Proving that a pattern dump sent during playback takes effect without an audible glitch.
3. The Monomachine rule that it only accepts dumps on its "WAITING" receive screen.

A two-to-three-day spike can settle all three before any UI work starts.

---

## 1. Architecture

### 1.1 How the emulator runs real firmware

- **Hardware model:** one ColdFire MCU plus two DSP56303s. A single deterministic scheduler interleaves all three on the calling (audio) thread. See `source/elektron/md/mdLib/mdhardware.h:43-52` ("md::Hardware models the Elektron ColdFire MCU and two DSP56303s"). DSP2 produces the voices, DSP1 mixes and drives the codec, and the codec frame clock is the master (`mdhardware.h:46-50`).
- **Advancing time:** `advance(machineFrames)` at `mdhardware.h:181-186`. One codec frame is 2304 DSP cycles, or about 907 UC cycles at 40 MHz (`mdLib/mdtypes.h:47-48`; 44.1 kHz native rate).
- **CPU and DSP emulators:** the CPU is the `mc68k` submodule, a Musashi-derived core with ColdFire timings (`doc/monomachine_modulation_timing.md`, "core changes in mc68k PR #4"). The DSPs use the `dsp56300` JIT (CLAUDE.md "Emulation stack").
- **Firmware image:** a full 8 MiB flash image (`mdtypes.h:44`, `g_romSize = 0x800000`). It is accepted only when an FNV fingerprint matches exactly MD OS 1.63 (UW) or MM OS 1.32b (`mdtypes.h:55-56`, `mdLib/mdromloader.cpp:26-51`). This matters later: exactly one firmware build per product means RAM layout is deterministic.
- **MCU memory map** (`mdLib/mdmc.h:41-49`):
  - ROM
  - 1 MiB battery-backed **patch RAM** at `0x100000`, aliased at `0x700000`. This is the user data store: kits, patterns, songs.
  - 1 MiB **main RAM** at `0x200000`
  - SIM peripherals at `0x300000`
  - DSP HI08 windows at `0x500000` and `0x600000`

### 1.2 How the current UI talks to the machine

There are two parallel channels.

**Simulated front panel (primary channel).** This is how the current skin works.
- Buttons and encoders become `[row][mask]` packets on the panel UART2 (`mdhardware.h:258-262` `trySendPanelEvent`; `mddevice.h:174-177`).
- The LCD is rebuilt from the host-to-panel byte stream: a 128×64 KS0108 display plus 14 LED banks `0x20..0x2d`, all active-low (`mdLib/mdfrontpanel.h:14-31, 53-105`).
- A publisher gives tear-free snapshots plus a bounded stream of LED transitions stamped with emulation cycles (`mdfrontpanel.h:161-221`).
- The editor (`mdJucePlugin/mdEditor.cpp`, 2,093 lines) is almost entirely panel emulation: buttons, encoders, LCD hit-testing and LED painting (`mdEditor.h:117-185`).
- It already includes an "LCD interaction model" that recognises a small set of qualified LCD screens by their pixels (`mdJucePlugin/mdLcdInteractionModel.h:1-80`). That is screen-scraping, and it is a warning sign for anyone tempted to derive state from the LCD.

**Parameter / CC model.** This is real, but narrow.
- `parameterDescriptions_md.json` has 26 parameters per track × 16 tracks:
  - 8 machine parameters, 8 effects, 8 routing, level and mute
  - MD LFO exposure is only speed, amount and shape (`mdJucePlugin/parameterDescriptions_md.json:18-47`)
- `parameterDescriptions_mm.json` has 58 parameters per track × 6 tracks: synthesis, amp, filter, effects, **all three LFOs including destination, trigger, waveform, multiplier, speed, interlace and depth**, level and mute (`parameterDescriptions_mm.json:19-83`).
- Parameters map one-to-one to Elektron's public CC implementation (`mdLib/mdautomation.cpp:9-62`):
  - MD uses 4 channels × 4 lanes with CC bases 16, 40, 72 and 96.
  - MM uses one channel per track with page bases 48, 56, 72, 80, 88, 104 and 112.
- The controller keeps these parameters in sync by requesting the Global (`0x51`) and Kit (`0x53`) dumps and parsing the Kit dump (`mdLib/mdsysexautomation.h:55-70`; `mdJucePlugin/mdController.cpp:789, 879`). Delivery is lock-free and realtime (`mdController.h:88-131`). Validation is in `doc/mdmm_automation_validation.md`.
- **What is not modelled:** pattern data (trigs, locks, length, swing, accent, slide), song data, MD machine type, MD LFO routing (destination track and parameter), and MD kit-level settings. The existing abstraction is "kit sound parameters as CC", nothing more.

---

## 2. Data access without pressing virtual buttons

| Path | What it reaches | Live? | Evidence |
|---|---|---|---|
| **CC** (existing parameters) | 16×26 MD and 6×58 MM track sound parameters, including all MM LFO routing | Live, sample-scheduled | `mdhardware.h:204-210` `scheduleMidi`; `mdautomation.cpp:44-62` |
| **SysEx dump request / receive** | Global `0x50/51`, Kit `0x52/53`, **Pattern `0x67/68`**, Song `0x69/6a`, Status `0x70` / SET STATUS `0x71` (current pattern, kit, track) | Request: live. Receive: **MD any time; MM only on the WAITING screen** | `mdLib/mdsysexfile.h:140` (accepted commands); `mdLib/mdsysexautomation.cpp:10-16`; `mdLib/mdmidiprotocol.h:15-31`; MM request/response proven in `mdLibTest/mmSysexExportFirmwareTest.cpp:84-125, 235`; WAITING rule in `doc/elektron_md_mm_sysex.md:17-23` |
| **Direct emulated RAM** | Everything the firmware knows, including the playhead, edit buffer and mute state | Live, but offsets are unknown (needs reverse engineering) | `mddevice.h:200-203` `getHardware()`; `mdhardware.h:124-125` `getUC()` / `copyPatchRam()`; `mdmc.h:69-71` `read8`/`write8`, `:155` `replacePatchRam`; patch RAM behind a `shared_mutex` (`mdmc.cpp:141-151`); device access via `withDeviceLocked` (`synthLib/plugin.h:76-78`, used at `mdPluginProcessor.cpp:268-277`) |
| **MCP server** | Parameters, `send_midi`, `send_sysex`, `get_state`/`set_state` (opaque blobs), DOM and UI injection, screenshots | Live, HTTP+SSE on port 13710+; **disabled by default** | `doc/mcp_server.md:29-43, 127-300`; tools listed in `source/mcpServerLib/mcpPluginServer.cpp:85-91`; started in `jucePluginEditorLib/pluginProcessor.cpp:88-90, 243-259` |
| **Lua** | Skin-side `params.get/set/getText/getInfo` plus the DOM | Live, parameters only | `doc/lua_scripting.md:36-80` |

### Key observations

- **No pattern codec exists anywhere in the repo.** SysEx pattern dumps are only validated (framing, checksum, length at `mdsysexfile.h:119-147`) and passed through. A pattern and kit decoder/encoder for MD and MM is new work. The formats are documented in the Elektron manuals' SysEx appendices, and there is community prior art: MIDICtrl, MegaCommand and MCL for the MD. Check the licence of any code before reusing it.
- **MCP cannot read machine output.** It can send SysEx, but no tool returns MIDI-out or SysEx responses. A dump reply sent over MCP is lost. Any out-of-process UI needs new tools (for example `request_dump`, `subscribe_midi_out`, `get_led_transitions`, `read_ram`).
- **Round-trip latency (estimate; the spike must measure it):**
  - An MD pattern dump is a few kB of 7-bit data.
  - Real DIN MIDI at 31,250 baud moves about 3.1 kB/s, so a whole pattern takes roughly 1–2 s.
  - The emulated MIDI UART RX has **no baud pacing** ("UART1 retains legacy immediate delivery", `mdLib/mdsim.h:22-25`). The actual limit is how fast the firmware's receive ISR and parser drain bytes, plus RX overflow (`mdhardware.h:120-122` `midiRxOverflowCount`).
  - The file sender deliberately paces at TurboMIDI 8–10× (`doc/turbomidi.md:56-75`; `mdturbomidi.cpp:397-475`). That puts one pattern at roughly 100–300 ms of emulated time.
  - Dump *requests* return in emulated time, polled in 64-frame steps in the test at `mmSysexExportFirmwareTest.cpp:98-108`.
  - Conclusion: fine for "edit → push on mouse-up", too slow for a per-mouse-move stream.
- **Live vs reload:** CC edits are live. Pattern and kit dumps land in a **slot** and the firmware may need a reload, for example SET STATUS "current pattern" (`mdmidiprotocol.h:22-25`). Whether overwriting the *playing* slot is heard at once, at the next bar, or only after a reload is **unverified**. It is the first spike question.
- **Playhead / current step:**
  - Host transport becomes MIDI Start/Continue, Song Position Pointer and 24-PPQN clock into the firmware (`source/synthLib/midiClock.cpp:12-76`). The firmware sequencer therefore follows the DAW, and position can be *computed* from host PPQ plus pattern length and scale once we have decoded the pattern.
  - Direct *observation* is possible through cycle-stamped LED transitions (`mdfrontpanel.h:161-180`). Step LEDs are exposed for MD, and MM steps with colour (`mdfrontpanel.h:124-127`). This only shows what the panel shows (the current track and page).
  - The robust answer is to read the sequencer's step counter from RAM. The fingerprint lock makes this deterministic, but finding the offset is reverse-engineering work.
- **Precedent for firmware-level intervention:** the fork already patches DSP program memory, gated by the firmware fingerprint, to fix RAM-recording tails (`mdLib/mdrampacking.h:14-60`; `mdhardware.h:76-80`). An upstream "firmware-hooks" draft (PR #43) also exists (`doc/monomachine_modulation_timing.md:84`). Upstream is therefore not hostile to fingerprint-gated RAM/firmware knowledge.

---

## 3. The key question: firmware sequencer vs our sequencer

### (a) Edit firmware data and let the firmware play

**Pros**
- 100% playback fidelity. Every Elektron behaviour is the firmware's own:
  - p-lock semantics, MD's 64-lock-per-pattern limit, retrigs
  - accent, swing, slide, MM parameter slides and arpeggiator
  - LFO trigger modes, MD machine types, Ctrl-All, song mode, kit switching
- Patterns stay portable to real hardware (same SysEx). That fits the audience, which by definition owns the hardware (ROM from their own unit).
- DAW sync already exists (`midiClock.cpp`).

**Costs / breaks**
- A byte-exact MD+MM pattern/kit/song codec is required. Test it by round-tripping every firmware dump: `encode(decode(x)) == x`.
- Edit granularity is "resend the whole pattern". Each edit costs about 0.1–0.3 s of emulated MIDI time and may glitch or require a reload (unknown). Mitigations:
  - debounce edits and push on gesture end
  - send live tweaks as CC in the meantime
  - later, write RAM directly once offsets are known
- **MM receive gate:** the MM only accepts dumps on the GLOBAL > SYSEX RECV "WAITING" screen (`doc/elektron_md_mm_sysex.md:17-23`). A UI would have to drive the panel there and back, visibly interrupting the user. RAM writes are the likely real fix for MM.
- Two writers: someone editing on the virtual panel (or a real-hardware habit) makes our model stale. We must re-request the dump on change signals (status polling, LED/LCD change, patch-RAM diff).
- UI features are capped by the format:
  - p-locks are one 7-bit value per step per parameter, so drawn automation quantises to steps
  - MD's lock-row budget must be enforced in the UI
  - no more tracks or steps than the machine supports

### (b) Our own sequencer in the plugin, driving the machine with notes + CC

**Pros**
- Total freedom: unlimited lanes, microtiming, per-sample curves, longer patterns.
- Sample-accurate scheduling is already available (`scheduleMidi`, `mdhardware.h:204-210`).
- MM LFO routing is CC-addressable, so drag-and-drop routing works live on MM.

**What breaks / degrades**
- **P-lock fidelity:**
  - A CC changes the kit parameter *persistently*. Emulating a lock means CC-then-trig-then-restore: two CCs per lock, with ordering hazards against the trig.
  - Firmware-side LFO/lock interaction and MM parameter slides are lost.
  - MD sound-locks, machine changes, MD LFO destination and master FX are not CC-addressable, since MD exposes only 26 per-track CCs (`parameterDescriptions_md.json`).
- **Density:** 16 tracks × several locks per step is a burst of CCs into a firmware RX buffer that can overflow (`midiRxOverflowCount`). The soak test exists precisely because of this (`doc/mdmm_automation_validation.md`, `mdAutomationSoakTest`).
- **Unverified mappings:** accent and velocity handling on MD, and slide/legato on MM via MIDI, need checking against the firmware.
- **Two sequencers:** the machine's own sequencer still exists and must be kept stopped or empty. Patterns made this way are not portable to real hardware unless compiled to SysEx, which is (a) anyway.
- Effectively this turns the machines into MIDI sound modules and throws away the reason the MD/MM are loved.

### Verdict

Build **(a)** as the source of truth. Add the existing CC channel for live gestures, which (a) needs anyway for responsiveness. Keep (b) only as an optional later "extended lanes" mode (for example sub-step automation or >64 steps), clearly labelled as not portable.

---

## 4. UI technology and where a new UI plugs in

- **Current stack:** RmlUi (HTML/CSS-like) with Lua 5.4. Skins are compiled into binary data (`mdJucePlugin/CMakeLists.txt:58-65`; `source/skins.cmake`) and rendered through GL2/GL3/Metal back ends (`source/juceRmlUi/RmlUi_Renderer_*`).
- **Widgets available** in `source/juceRmlUi/`: canvas (`rmlElemCanvas`), drag source/target (`rmlDragSource`, `rmlDragTarget`), splitter, tree, list, knob, combo box. A grid, envelope editor and drag-to-route UI are all buildable in RmlUi, but custom drawing means C++ canvas work.
- **Plug-in point:** `PluginEditorState::createEditor(const Skin&)` always returns the panel editor (`mdJucePlugin/mdPluginEditorState.cpp:60-63`). Skins come from `productSkins()` (`mdProductSkins.h:10-23`) and a compatibility policy (`mdProductSkinPolicy.h:21-30`). A second editor is a small change:
  - add a skin entry such as `mdStudio`
  - branch in `createEditor` on the skin name to a new `jucePluginEditorLib::Editor` subclass (`jucePluginEditorLib/pluginEditor.h:83-169`)
  - the user switches editors through the existing skin selector
- **WebView option:** it is currently compiled out (`source/juce.cmake:80`, `JUCE_WEB_BROWSER=0`). It can be enabled with `NEEDS_WEB_BROWSER`. The JUCE fork's version is unknown because the submodule is not initialised; JUCE 8 provides WebView with native function bridging. A web UI embedded in the plug-in is viable and makes the grid, curves and drag-and-drop much cheaper to build. The cost is a second renderer next to RmlUi, plus WebView quirks in some DAWs (Windows WebView2 runtime, Linux).
- **Out-of-process UI:**
  - The MCP server (JSON-RPC over HTTP+SSE on localhost, with a discovery file, `doc/mcp_server.md:41-86`) proves the pattern works.
  - As a UI transport it has gaps: off by default, no push of MIDI-out, LED or playhead events, and it is request/response over HTTP.
  - A dedicated local WebSocket endpoint in the processor, carrying the same JSON contract, is viable. It suits the standalone app and development (hot-reload web UI in a browser).
  - For a DAW product, an external window is poor UX. Use it as the dev/test harness and also expose the same contract to an in-plugin WebView.
- **Threading constraints:**
  - The DOM must be modified on the message thread (CLAUDE.md, "RmlUi threading").
  - The device runs on the audio thread under the Plugin lock. RAM or dump reads must go through `withDeviceLocked`, as existing code does, or through a realtime-safe publisher like `FrontPanelPublisher`.
- **Remote device (a separate concern):** a network DSP bridge also exists (`source/bridge/`, `mdJucePlugin/serverPlugin.cpp:1-9`). Direct `getHardware()` access is "only valid for a local (non-bridged) device instance" (`mddevice.h:200-201`). A new UI that reads RAM will not work over the bridge unless new bridge messages are added.

---

## 5. Licence and legal

- **The whole repo is GPL-3.0** (`LICENSE.md`). Any plug-in linking this code, including a new in-process editor, is a derivative work and must be distributed under GPL-3 with full corresponding source.
  - Selling GPL software is allowed.
  - Restricting redistribution is not, and anyone can rebuild and share it.
  - The realistic commercial models are: paid convenience builds, support, or hosted extras.
- **A closed out-of-process UI** talking over a socket is the classic grey zone. The FSF position is that separate programs communicating at arm's length are separate works, but "intimate" exchange of internal data structures can make them one work. A protocol exposing decoded Elektron pattern data (a public format) is on the safer side. **Get legal advice before relying on this.** The simplest honest route is to release the UI as GPL too.
- **Third-party components:**
  - JUCE: JUCE 7 is GPLv3; JUCE 8 switched its open-source licence to AGPLv3. Confirm the fork's version.
  - RmlUi and Lua are MIT.
  - The VST3 SDK licence changed recently; verify it.
- **Firmware / ROM:**
  - Elektron's copyright. It cannot be bundled or distributed (README.md: "DO NOT ask us for the .bin files").
  - The loader accepts only an exact 8 MiB image of MD 1.63 or MM 1.32b (`mdromloader.cpp:26-51`), dumped from the user's own machine.
  - This limits the addressable market to owners of the hardware and adds onboarding friction: users must produce a full flash image.
- **Trademarks:** "Elektron", "Machinedrum" and "Monomachine" can be used descriptively ("for Machinedrum owners") but not as product branding.
- **Community norms:** no ROM or firmware discussion in the upstream Discord (README). The fork owner and upstream (TUS / joelanders) are not affiliated with Elektron.

---

## 6. Risks, unknowns, recommended architecture, phased plan

### Risks and unknowns (ranked)

1. **Live pattern push semantics.** Does a `0x67` dump to the playing slot take effect cleanly, need a reload, or glitch? Is timing affected? Blocks design (a) if bad; RAM writes are the fallback.
2. **MM WAITING gate.** SysEx push to MM needs a panel round trip to the receive screen. Likely forces RAM-level writes for MM live editing.
3. **Codec effort and exactness.** Two formats, MD and MM. Extended/64-step patterns, lock tables and MD UW specifics all need care. Byte-exact round-trip tests against real dumps are mandatory.
4. **RAM reverse engineering** for the playhead and edit buffer. Deterministic per fingerprint, but it is RE labour, and writing firmware RAM behind its back can corrupt derived state. Stick to read-only at first.
5. **State coherence** between the panel editor, our model and the DAW project state. Mutes are already session-only (`doc/mdmm_automation_validation.md`: "Kit dumps do not contain them").
6. **CPU headroom:** MM costs about 0.55–0.84 CPU-seconds per second of audio on Apple silicon in one fixture (`doc/monomachine_modulation_timing.md:90-93`). The UI must not add audio-thread work, which means no per-frame device-lock polling.
7. **Upstream drift:** a large, fast-moving fork (a 2,093-line editor and many PRs). Keep our code in new files behind one `createEditor` branch to limit merge pain.
8. **Legal:** GPL-3 means no closed-source moat, and the ROM requirement limits the market.

### Recommended architecture (Simple Made Easy: separate model, transport and view)

```
            ┌──────────── pure C++ lib (no JUCE) ────────────┐
            │ elektronData: MD/MM Pattern, Kit, Song values  │
            │ decode(sysex) -> value, encode(value) -> sysex │
            │ round-trip tested against firmware dumps       │
            └───────────────▲───────────────────┬────────────┘
                            │ dumps             │ edits (debounced)
  firmware ── MIDI out ──> MachineLink ── SysEx/CC in ──> firmware
  (plays)                   │  request/refresh on status change
                            │  CC for live gestures (existing Controller)
                            │  Telemetry: LED transitions, host PPQ,
                            │             later RAM step counter (read-only)
                            ▼
        JSON contract (document snapshot + patches + events)
          ├─ in-plugin view: new Editor (RmlUi or JUCE WebView)
          └─ localhost WebSocket (dev harness / standalone / tests)
```

- Store the document as values. Edits are pure functions `Pattern -> Pattern`. MachineLink is the only component with effects.
- The same JSON contract serves the in-process view and the socket, so the view technology can be chosen late.

### Phased plan (smallest proof first)

**P0: spike, 2–3 days, go/no-go.**
- Build the MD/MM targets with ROMs.
- From a tiny headless test (pattern after `mmSysexExportFirmwareTest`), do: request MD pattern → flip one trig byte (hand-decoded) → send while the sequencer runs from MIDI clock → capture audio.
- Measure: round-trip time, whether the change is heard with or without a reload, and glitches.
- Repeat for MM, including the WAITING-screen flow.
- Try LED-transition playhead tracking.
- Exit criterion: a documented answer to risks 1 and 2.

**P1: codec library, about 1–2 weeks.**
- MD pattern + kit, then MM pattern + kit, then song.
- Property tests: `encode(decode(dump)) == dump` for all 128 patterns, 64/128 kits and all songs from real backups. The repo's firmware harnesses can produce the backups (`mmSysexExportFirmwareTest ... full`).

**P2: read-only global grid.**
- New Editor behind a skin switch: all 16/6 tracks, trigs, lock markers, lengths, swing, playhead.
- Refresh through dump requests.
- No writes yet.

**P3: editing.**
- Trig, accent and slide toggles, and lengths, pushed as debounced pattern dumps.
- Kit parameter knobs through the existing CC parameters.
- Conflict handling: re-request on status or patch-RAM change.

**P4: graphical sound design.**
- MM amp and filter envelopes and filter curve, built from existing parameters.
- MM LFO drag-and-drop routing, live over CC (`Lfo*Destination/Page`).
- MD LFO routing through kit dumps.

**P5: automation lanes to p-locks.**
- Draw a curve, quantise to steps, write lock tables, enforce the MD lock budget.
- Contextual workspaces (sound design vs arrange/mix; song editor).

**P6 (optional): RAM telemetry and writes.**
- Fingerprint-gated, read-only first: step counter, then edit buffer.
- Writes only if P0 showed dumps are unusable live (likely for MM).
- Optional "extended lanes" host-side sequencer, marked non-portable.

**Decision owners before P1:**
- Radek: GPL-3 acceptance for the product.
- Radek: RmlUi vs WebView (can be deferred until P2 thanks to the JSON contract).

Housekeeping (outside this review's scope): this repo is not listed in the workspace `CLAUDE.md` audio sphere table.
