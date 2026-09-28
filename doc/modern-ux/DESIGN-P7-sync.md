# P7 design: tempo, transport and playhead sync

The two editors, the Machinedrum Editor and the Monomachine Editor, each run one emulated machine. Users want both machines to play together:

- in a DAW, both plug-ins follow the host;
- as two standalone apps, the two apps follow each other.

This page covers what already works, what P7 fixed and what it recommends for the standalone apps.

## 1. The facts (measured)

| What | Where it was measured | Result |
|---|---|---|
| The host's transport and tempo reach the firmware | `synthLib::MidiClock`, since before P7 | Each block, the plug-in turns the host's play state, BPM and PPQ into MIDI Start (or SPP plus Continue), 24-PPQN clocks placed to the sample, and Stop. |
| MD as it boots, with the host clock at 100 BPM | `mdDeskFirmwareTest hostclock` | Starts and stops with the host, but plays its own tempo: 9.5 steps/s at both 100 and 150 BPM. GLOBAL sync flags are `00`. |
| MD with TEMPO IN external | the same test | Follows the clock: 6.75 steps/s at 100 BPM and 10.25 at 150 (ratio 1.52), and stops on Stop. |
| MM as it boots | `mmDeskFirmwareTest hostclock` | Ignores Start and the clock (0 steps). |
| MM GLOBAL bytes | `MM_SYNC_SEARCH=1 mmDeskFirmwareTest hostclock` | Each undecoded global byte was flipped and the machine measured again. Raw `0x06` = 1 takes Start/Stop at the machine's own tempo (TRANSPORT IN). Raw `0x05` = 1 as well follows the clock: 6.5 steps/s at 100 BPM and 10.25 at 150 (CLOCK IN). |
| MM global pushes | the same | A global is stored at once, but it is applied only when its slot is made active (0x56), and not while the panel is on SYSEX RECV. The MD behaves the same way (P5). |
| MM clock out | `MM_SYNC_OUT=1 mmDeskFirmwareTest hostclock` | Raw `0x07` and `0x08` are most likely CLOCK OUT and TRANSPORT OUT (the menu's order). The run was inconclusive because the restore between cases was not re-activated. Not used yet. |

## 2. In a DAW (done in P7)

The DAW is the clock, and the machine follows it the same way hardware does over MIDI: its GLOBAL settings accept the clock.

- **The model says what the global must be**, as pure functions:
  - `MdModel::hostFollowing(MdGlobal)` sets TEMPO IN external and CTRL IN on;
  - `MmModel::hostFollowing(MmGlobal)` sets CLOCK IN and TRANSPORT IN (`x05[0]` and `x05[1]`).

  Each returns nothing when the global already follows.
- **The adapter sets it** with a machine command, `followHost`, which is a row in both command tables (the schemas are generated from them).
  - It pushes the active global as a dump: the MD makes it active with 0x56 at once, the MM once the panel has left SYSEX RECV.
  - The push is the machine's own setting, not an edit, so it adds no undo step. The read-back is observed.
- **The session decides when.** It sends `followHost` every 2 s while the machine is ready, but only in a plug-in hosted by a DAW. The test is the processor's wrapper type: not Standalone and not Undefined, so the unit and session tests keep the machine's own clock.
- **The page says so.** PLAY on a machine that follows the host answers with a note: "The machine follows the host: it plays when the host's transport runs."
- **The playhead:** the page's playhead is the machine's own (RAM telemetry), so it follows whatever the machine plays. No second clock needs aligning.

Not covered yet:
- **A real DAW session has not been automated.** The chain is covered from both ends: the plug-in's `MidiClock` has its own tests, and the rigs feed the firmware the same bytes. Radek should check once in Live or Logic: play, change the tempo, loop, and stop.
- **Tempo resolution:** the firmware follows the clock itself, so there is no BPM rounding.
- **Loops and seeks:** `MidiClock` sends Stop, then SPP plus Continue. Both machines take SPP within a pattern (pattern mode). Song mode has not been checked.

### What the follow changes, and for how long

- **Where the setting lives.** The follow is a machine setting: the active global's sync bits, set by `followHost` while the plug-in is hosted. It lives in the machine's state, which the plug-in saves with the DAW project. So it stays inside that project.
- **The standalone app is not affected.** It keeps its own machine state (its settings file), and never runs `followHost`.
- **Removing the plug-in** from a project takes its machine with it; nothing outside the project changes.
- **Every host session re-applies it**, so undoing it is neither needed nor possible from the project.
- **The one leak.** If a user loads a DAW project's state into the standalone app ("Load a saved state…"), the machine still follows an external clock there. Its PLAY then waits for MIDI clock (the page says "The machine follows the host"), and the LCD's sync slot shows HOST. The fix is on the machine:
  - MD: GLOBAL › TEMPO IN internal (the GLOBAL panel);
  - MM: GLOBAL › MIDI SYNC CLOCK IN and TRANSPORT IN off.
- **Why the global is not restored automatically:** the machine has no separate "hosted" copy of its global to fall back to. Keeping it out of the stored state would mean saving a second global per project, which is not worth it for this edge. Documented instead.
- **On screen:** the LCD's sync slot shows HOST while the machine follows an external clock.

## 3. Two standalone apps (decision: Ableton Link, in v0.3)

**Radek's decision (2026-09-28): Link is the chosen design, built in v0.3; nothing is downloaded or coded for it in P7.** The comparison and the plan below stand. The MIDI-clock-over-IAC route is the alternative until then.


Three candidates:

| | Ableton Link | MIDI clock over the IAC bus | A local shared-memory or OSC link |
|---|---|---|---|
| What it shares | Tempo, beat phase and start/stop, with no leader | Tempo and transport from one leader to the followers | Whatever we define |
| With other software | Live, Bitwig, iOS apps and every Link app on the network | Any MIDI app | Only our own apps |
| New dependency | The Link SDK: header-only C++, GPLv2+ or a proprietary licence (GPLv2+ is compatible with our GPL-3), plus asio-standalone (Boost licence). **A download that needs Radek's approval.** | None: the standalone's MIDI in and out, and each machine's own sync settings | None, but a protocol and discovery we would have to own |
| Jitter and phase | Beat time from a shared clock: sample-accurate phase, independent of MIDI timing | MIDI clock jitter from the OS MIDI path (usually under 1 ms on IAC); the phase is relative (Start and SPP) | Ours to get right |
| User steps | Turn Link on in both apps | Choose the IAC ports in both apps' AUDIO / MIDI panels; one app leads (TEMPO/CLOCK OUT), the other follows (TEMPO/CLOCK IN) | None, but only two of our apps |
| Failure modes | Network permissions (local network on macOS) | A MIDI loop if both apps lead; ports are named per system | Stale segments, versioning |

**Chosen: Ableton Link** for the standalone apps (v0.3). It is the standard for peer tempo and phase sync on one machine or a local network. It has no leader to choose and no ports to wire, and it also syncs with Live and every other Link app, which is what users of these editors run next to them. It is one pinned SDK, under a licence we can ship.

How Link fits the P6 foundation:
- **The engine edge.** A new `LinkClock` in the standalone owns a `ableton::Link` instance and turns its session state into what the plug-in already consumes: BPM, beat position and playing. It feeds these into the same `synthLib::MidiClock` path the DAW uses. The machine then follows Link exactly as it follows a DAW, through `followHost`, now applied to "a host or Link".
- **Leading.** When the user changes the tempo in the editor, `LinkClock` commits it to the Link session. Start and stop sync both ways (`startStopSyncEnabled`).
- **The page.** A LINK key in the Setup group shows the peer count on LCD line 2's sync slot, for example `LINK 2`.
- **Pure parts, testable without JUCE:** the beat-to-PPQ mapping, and the quantum: 4 beats, one bar of a 16-step pattern at 1X.

**Meanwhile, without a dependency: MIDI clock over IAC.** The pieces work today, as long as the global settings are set:
- The MD's GLOBAL panel has TEMPO OUT and TEMPO IN; the leader MD sends the clock with TEMPO OUT and CTRL OUT.
- The MM needs CLOCK OUT and TRANSPORT OUT, most likely raw 0x07 and 0x08, still to be verified. A follower MM needs CLOCK IN and TRANSPORT IN, which `hostFollowing` sets.
- Next step: a "Sync" choice in the standalone's AUDIO / MIDI panel, one of Off, Lead over MIDI or Follow over MIDI. It would set the globals through a machine command like `followHost`, and list the IAC ports. It is not built in P7, because the MM's out bytes are not verified yet.

## 4. What P7 changed in the code

- `mdDesk/mdDeskModel.*`, `mmDesk/mmDeskModel.*`: `hostFollowing`, and the `followHost` command rows.
- `mdDesk/mdDeskMachine.cpp`, `mmDesk/mmDeskMachine.cpp`:
  - `cmdFollowHost`;
  - the PLAY note;
  - on the MM, making the active global active again once SYSEX RECV is left.
- `mdJucePlugin/mdDeskSession.*`: `followsHost()` (the wrapper type), and the periodic `followHost` in a DAW.
- `elektronData/mmGlobal.h`: raw 0x05 (CLOCK IN) and 0x06 (TRANSPORT IN) documented.
- Tests:
  - `mdDeskFirmwareTest <ROM> hostclock` (its own mode) and `mmDeskFirmwareTest` (in the default run, or `hostclock` alone);
  - the `mdP4ProbeFirmwareTest hostclock` probe.
