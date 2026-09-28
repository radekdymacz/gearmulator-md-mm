
/* ===== Host (P6): the seam between this UI and what plays it =====
   Here the mockup's own example engine plays: a local clock, snapshot undo, a pretend boot.
   In the plug-in, mmAdapter.js sets window.MMHost before this script runs, and the real
   machine plays: its transport, its library, its undo (in C++). The UI hands those actions to
   the host through these calls; a call the host does not have is done the mockup's way.
     start()                          the page is up
     edited(what)                     a gesture edited the view: "struct", "sound" or "commit" (it ended)
     slotWritten(kind, slot)          a library gesture wrote another slot ("kit" | "pattern")
     undo(), redo(), history()        undo lives with the host; history() = {undo, redo} counts
     togglePlay(), ownsClock          the machine is the sequencer
     selectPattern(p, now)            LOAD PATTERN (now: STOP, LOAD PATTERN, PLAY)
     kit(op, k)                       "save" | "load" | "saveAs" | "reload"
     tempo(bpm), mutes()              the machine's tempo; the synth tracks' mutes changed
     playKey(note), joy(xy), learnBind(target, knob)
     engine(kind), firstRun(), bootScreen(on), renderPst(), engineLabels
     audioDoc(), audioSend(command), audioMeter(on)   the AUDIO / MIDI panel's devices
   The view's side, for a host: window.MMView (130-main.js). */
const HOST=window.MMHost||{};
