/* ===== Host (P6): the seam between this UI and what plays it =====
   Without a host (this mockup on its own) the mockup's example engine plays: a local clock,
   snapshot undo, a pretend boot, its own questions. In the plug-in, mmAdapter.js sets
   window.MMHost before this script runs, and the real machine plays: its transport, its library,
   its undo and its questions (in C++). A host implements every call below
   (sync-mmstudio-skin.py checks both ways); the mockup hands those actions to it.
     start()                          the page is up
     edited(what, kind)               a gesture edited the view: "struct", "sound" or "commit" (it ended);
                                      kind names the document it edited ("pattern", "kit", "song",
                                      "global") when the gesture knows, else the host takes what
                                      "struct" (pattern, song) or "sound" (kit) edit
     slotWritten(kind, slot)          a library gesture wrote another slot ("kit" | "pattern")
     undo(), redo(), history()        undo lives with the host; history() = {undo, redo} counts
     togglePlay(), ownsClock          the machine is the sequencer
     selectPattern(p, now)            LOAD PATTERN (now: STOP, LOAD PATTERN, PLAY)
     kit(op, k)                       "save" | "load" | "saveAs" | "reload"
     tempo(bpm), mutes()              the machine's tempo; the tracks' mutes changed (synth and MIDI)
     keyMode(mode)                    the keyboard mode changed ("normal" | "multi" | "map" | "poly"): POLY
                                      is the machine's audio mode
     record()                         the RECORD key: the host picks GRID or LIVE RECORDING, or off
     songSlot(slot), loadSong(slot)   the Song workspace edits another of the 24 songs; LOAD SONG makes it
                                      the machine's
     waiting(), sendNow()             HW MIDI: how many messages wait for the machine's SYSEX RECV, and the
                                      person says it is there (the pattern field's SEND n dialog)
     playKey(note), keyUp(), joy(xy)  the keyboard (a key pressed, the keys let go) and the joystick
     learning(on), learnTarget(target), learnBind(target, knob)
                                      LEARN on or off, the value clicked, the knob pressed for it
     modulators(setup)                the Control workspace's app sources or links changed: the
                                      host's engine runs them ({sources, links}, ctlSetup()); it
                                      shows their moving values with MMView.setModulation
     notes                            the host's words for the Control sources ({ccSource, appSource})
     engine(kind), firstRun(), bootScreen(on), renderPst(), engineLabels, menu()
     syxChoose(), syxExport(), syxStart(kinds), syxStop()   SysEx import and export (P7): the host's file
                                      dialogs; the preview and progress come back through MMView
     romManage()   LOAD ROM in the engine menu (a host shows which firmware runs, REPLACE and REMOVE; none: the start-up card)
     chooseRom(), revealRom(), recheck()   the start-up card's keys (P7): the native file chooser for the
                                      firmware, the ROM folder, look again (the page never reads the ROM)
     audioDoc(), audioSend(command), audioMeter(on)   the AUDIO / MIDI panel's devices
   The view's side, for a host: window.MMView (130-main.js): values to read, setters (the LCD
   picture, the held key, the pattern field's RECV state, the engine words, the machine's mutes,
   keyboard mode, RECORD state and songs), and
   disable(capability, reason) for what the host's engine cannot do (NA_SEL maps each capability
   to its controls, NA_INFO lists those with none). */
const HOST=window.MMHost||{};
