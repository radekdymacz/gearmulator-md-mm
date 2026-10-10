/* ===== Host (P6): the seam between this UI and what plays it =====
   In the plug-in, mmAdapter.js sets window.MMHost before this script runs, and the real machine
   plays: its transport, its library, its undo and its questions (in C++). A host implements every
   call below (sync-mmstudio-skin.py checks both ways); the mockup hands those actions to it.
   Without one (this mockup on its own) the demo host plays the edits (54-demo.js, DESIGN-UNIFY.md
   4.6: the same intents, shown through their writes over the example's documents, and snapshot
   undo) and the mockup's example engine the rest: a local clock, a pretend boot, its library and
   its own questions.
     start()                          the page is up
     intent(op, args)                 a gesture's edit as the machine's intent (edit(), 60-ui.js; DESIGN-UNIFY.md
                                      4.1): the op and its arguments in the contract's vocabulary, knob and lock
                                      values and song rows in the page's units; the host sends it with the
                                      gesture's id and shows its effect at once
     commit()                         the gesture ended: the next intent is another undo step
     library(op, args)                the library's slot ops (kitCopy, kitPaste, kitCopyTo, kitClear, kitRename,
                                      patCopy, patPaste, patCopyTo, patClear): intents of the host's machine, which
                                      asks before one loses something; without it the example engine's library
     undo(), redo(), history()        undo lives with the host; history() = {undo, redo} counts
     togglePlay(), ownsClock          the machine is the sequencer
     selectPattern(p, now)            LOAD PATTERN (now: STOP, LOAD PATTERN, PLAY)
     kit(op, k)                       "save" | "load" | "saveAs" | "reload"
     tempo(bpm), mutes()              the machine's tempo; the tracks' mutes changed (synth and MIDI)
     keyMode(mode)                    the keyboard mode changed ("normal" | "multi" | "map" | "poly"): POLY
                                      is the machine's audio mode
     record(live)                     the RECORD key: the host picks GRID or LIVE RECORDING, or off; live
                                      (Alt+Space): LIVE RECORDING, also when stopped
     songSlot(slot), loadSong(slot)   the Song workspace edits another of the 24 songs; LOAD SONG makes it
                                      the machine's
     chain(patterns), chainClear()    MM-P8: the Song palette's CHAIN: the machine's own pattern chain (one
                                      bank, each pattern once, played in order and looped), and its end; what
                                      the machine then plays comes back through MMView.show
     waiting(), sendNow()             HW MIDI: how many messages wait for the machine's SYSEX RECV, and the
                                      person says it is there (the pattern field's SEND n dialog)
     playKey(note), keyUp(), joy(xy)  the keyboard (a key pressed, the keys let go) and the joystick
     noteOn(t, pitch, vel),           the home row, the piano roll's and the transpose keyboard's keys: the
     noteOff(t, pitch)                note intent, as on the MD (synth track t, pitch semitones from C3; the
                                      core plays it on the track's own MIDI channel)
     learning(on), learnTarget(target), learnBind(target, knob)
                                      LEARN on or off, the value clicked, the knob pressed for it
     modulators(setup)                the Control workspace's app sources or links changed: the
                                      host's engine runs them ({sources, links}, ctlSetup()); it
                                      shows their moving values with MMView.setModulation
     notes                            the host's words for the Control sources ({ccSource, appSource})
     engine(kind), firstRun(), bootScreen(on), renderPst(), engineLabels, menu(x, y) (the editor's menu at that point: the host draws it with DeskMenu, I-008)
     syxChoose(), syxExport(), syxStart(kinds, skip), syxStop()   SysEx import and export (P7, B-019): the host's file
                                      dialogs; the preview and progress come back through MMView
     removeRom(info)   REMOVE in the LOAD ROM card
     romManage()   LOAD ROM in the engine menu (a host shows which firmware runs, REPLACE and REMOVE; none: the start-up card)
     chooseRom(), revealRom(), recheck()   the start-up card's keys (P7): the native file chooser for the
                                      firmware, the ROM folder, look again (the page never reads the ROM)
     audioDoc(), audioSend(command), audioMeter(on)   the AUDIO / MIDI panel's devices
     globalSlot(n)                    GLOBAL's slot keys: make global slot n (0-7) the active one (B-051)
   The view's side, for a host: window.MMView (130-main.js): values to read; show(view), the one
   writer of the state's document members (the machine's documents as the host derives them,
   DESIGN-UNIFY.md phase 1: the current pattern and kit, song, global, tempo, mutes, POLY, what
   plays); setters for the rest (the library's other slots, the LCD picture, the held key, the
   pattern field's RECV state, the engine words, RECORD state); and disable(capability, reason)
   for what the host's engine cannot do (NA_SEL maps each capability to its controls, NA_INFO
   lists those with none). */
const HOST=window.MMHost||window.MMDemoHost||{};
/* the plug-in's host implements every call of the seam (53-seam.js) */
if(window.MMHost){const miss=MM_SEAM.host.filter(k=>!(k in window.MMHost));if(miss.length)console.warn("MMHost lacks "+miss.join(", ")+" (53-seam.js)")}
