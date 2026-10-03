"use strict";
/* The Sound page's tables, data only (mdDeskSound.js lays them out): what each group of a machine's knobs is
   called, which knobs it holds and which screen draws them (SYN_TAB, from the manual's Appendix A), the
   effects and routing pages' groups (FXRT_*), each screen's help (SND_TIP) and a line about the machines
   without a picture (ABOUT). Lookups are by machine name and by machineFacts (mdDeskModel.js), never by a
   test of the name. The Monomachine Editor has the same kind of table for its machines (the MM mockup's
   src/85-sound-groups.js, MM_SYN_TAB with a page per group): the shape is close but not the same (the MM
   names its pages and slots), so the two stay apart until both editors read one group shape. */

/* a line for a machine whose knobs draw no picture */
const ABOUT = {
	INP: "Processes external input A or B. It needs one trig to become active.",
	MID: "No audio. Sends NOTE plus two chord notes (N2, N3), LEN, VEL, PB, MW, AT and 5 CCs on its own MIDI channel.",
	"CTR-AL": "No audio. Moves the same parameter on all 16 tracks at once, and it can be locked per step.",
	"CTR-8P": "No audio. Holds 8 shortcuts to parameters on any tracks. It can change them without playing a sound (a trigless trig).",
	CTRM: "No audio. The only way to lock and sequence this master effect.",
	"RAM-R": "Records on its trig. MLEV/MBAL = the machine's own main out (resample), ILEV/IBAL = inputs A/B. LEN up to 2 bars. RAM is lost at power-off.",
	"RAM-P": "Plays what the matching RAM recorder captured. Lock STRT per step to chop. END below STRT plays it reversed.",
	ROM: "Plays a sample kept in ROM. ROM-25 to ROM-48 are made for loops (STRT and END are linear).",
	"GND-EMPTY": "No machine on this track." };
function about(m, cat) {
	const x = machineFacts(m, cat);
	if (x.masterFx) return ABOUT.CTRM;
	return ABOUT[m] || (x.recorder ? ABOUT["RAM-R"] : x.player ? ABOUT["RAM-P"] : "") || ABOUT[x.key] || "";
}

/* Sound (round 5): the track's sound in small groups of a few knobs, in the page's section look (a
   title on a rule, the Machinedrum page its knobs are on at the right, no frame). Three rows, in the
   Machinedrum's order: the SYNTHESIS page's groups, then EFFECTS (amp mod, EQ, filter, sample rate)
   and ROUTING (drive, level and pan, sends), then the LFO. Each row is one grid of three lines: the
   titles, the screens, the boxes, so every title, screen and box row is level across the row. A group
   whose knobs draw a picture has a screen; two groups without one share a column (the upper one's
   boxes at the screens' top, the lower one's on the row's box line); a lone one says what its knobs
   do where the screen would be. A row without screens is compact. Every knob of the machine is in
   exactly one group: the synthesis page's groups are per machine (SYN_TAB, from the manual's
   Appendix A), a name the table does not know goes to SYNTHESIS. */
const SND_TAG = { syn: ["synth", "SYNTHESIS"], fx: ["fx", "EFFECTS"], rt: ["route", "ROUTING"], lfo: ["lfo", "LFO"], kit: ["kit", "kit (EDIT KIT → RELATE)"] };
/* a synthesis group: its title, its knobs (the page's names, unqualified), its screen (or null) and,
   for a group without one, what its knobs do */
const SG = (title, knobs, ed, note) => ({ title, knobs: knobs.split(" "), ed, note });
const AENV = k => SG("Amp env", k, "env");
/* E12: pitch and bend, the start and decay, the filter (or the machine's own tone knob), retrigs */
function e12Groups(m, cat) {
	const x = (cat.byName[m]?.params || [])[3];
	if (m === "E12-BD") return [SG("Pitch", "PTCH BEND", "pitch"), AENV("START DEC"), SG("Snap", "SNAP SPLEN", "atk"), SG("Retrig", "RTRG RTIM", "rtrg")];
	const flt = x === "HPQ" ? SG("Filter", "HP HPQ", "resp") : x === "STOP" ? SG("Filter", "HP", "resp") : SG("Tone", "HP " + x, "resp");
	return [SG("Pitch", "PTCH BEND", "pitch"), AENV(x === "STOP" ? "STRT DEC STOP" : "STRT DEC"), flt, SG("Retrig", "RTRG RTIM", "rtrg")];
}
const OUT_NOTE = { MONO: "MONO sums the echo to mono", LEV: "LEV is the effect's output level", GATE: "GATE gates the reverb's tail", GAIN: "GAIN is the EQ's output gain",
	HP: "HP high-passes the compressor's side chain", OUTG: "OUTG is the output gain", MIX: "MIX blends the dry signal back in" };
const outG = k => SG("Output", k, null, k.split(" ").map(n => OUT_NOTE[n]).join(". ") + ".");
const SMP_G = [SG("Pitch", "PTCH", null), SG("Sample", "STRT END", "sample"), AENV("DEC HOLD"), SG("Retrig", "RTRG RTIM", "rtrg"), SG("Bit rate", "BRR", null)];
const SYN_TAB = {
	"TRX-BD": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC"), SG("Attack", "STRT NOIS", "atk"), SG("Tone", "HARM CLIP", "wave")],
	"TRX-B2": [SG("Pitch", "PTCH RAMP", "pitch"), AENV("DEC HOLD"), SG("Attack", "TICK NOIS", "atk"), SG("Tone", "DIRT DIST", "wave")],
	"TRX-SD": [SG("Pitch", "PTCH BUMP BENV TUNE", "pitch"), AENV("DEC"), SG("Snap", "SNAP", "noise"), SG("Tone", "TONE CLIP", "wave")],
	"TRX-XT": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC DAMP"), SG("Distortion", "DIST DTYP", "wave")],
	"TRX-CP": [SG("Claps", "CLPY RATE HARD", "claps"), SG("Tone", "TONE RICH", "spec"), SG("Room", "ROOM RSIZ RTUN", "room")],
	"TRX-RS": [SG("Body", "PTCH DEC", "osc"), SG("Distortion", "DIST", "wave")],
	"TRX-CB": [SG("Pitch", "PTCH BUMP", "pitch"), AENV("DEC DAMP"), SG("Tone", "TONE ENH", "spec")],
	"TRX-CH": [SG("Metal", "GAP MTAL", "metal"), SG("Filter", "HPF LPF", "resp"), AENV("DEC")],
	"TRX-CY": [SG("Body", "RICH SIZE PEAK", "metal"), SG("Top", "TOP TTUN", "spec"), AENV("DEC")],
	"TRX-MA": [SG("Shake", "ATT SUS REV", "env"), SG("Rattle", "RATL DAMP RTYP", "grains"), SG("Tone", "TONE HARD", "spec")],
	"TRX-CL": [SG("Body", "PTCH DEC TUNE ENH", "osc"), SG("Attack", "CLIC DUAL", "atk")],
	"EFM-BD": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC"), SG("FM", "MOD MFRQ MDEC MFB", "fm")],
	"EFM-SD": [SG("Body", "PTCH DEC", "osc"), SG("Noise", "NOISE NDEC HPF", "noise"), SG("FM", "MOD MFRQ MDEC", "fm")],
	"EFM-XT": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC CLIC"), SG("FM", "MOD MFRQ MDEC", "fm")],
	"EFM-CP": [SG("Body", "PTCH DEC HPF", "osc"), SG("Claps", "CLPS CDEC", "claps"), SG("FM", "MOD MFRQ MDEC", "fm")],
	"EFM-RS": [SG("Rim", "PTCH DEC MOD HPF", "fm"), SG("Snare", "SNAR SPTC SDEC SMOD", "fm")],
	"EFM-CB": [SG("Body", "PTCH DEC SNAP", "osc"), SG("FM", "MOD MFRQ MDEC FB", "fm")],
	"EFM-HH": [SG("Body", "PTCH DEC", "osc"), SG("Tremolo", "TREM TFRQ", "trem"), SG("FM", "MOD MFRQ MDEC FB", "fm")],
	"EFM-CY": [SG("Body", "PTCH DEC HPF", "osc"), SG("FM", "MOD MFRQ MDEC FB", "fm")],
	"P-I-BD": [SG("Body", "PTCH DEC DAMP", "osc"), SG("Strike", "HARD HAMR TENS", "strike")],
	"P-I-SD": [SG("Body", "PTCH DEC RING", "osc"), SG("Strike", "HARD TENS", "strike"), SG("Snares", "RVOL RDEC", "noise")],
	"P-I-MT": [SG("Body", "PTCH DEC DAMP", "osc"), SG("Strike", "HARD HAMR POS", "strike"), SG("Shell", "TUNE SIZE", "metal")],
	"P-I-RS": [SG("Body", "PTCH DEC RING", "osc"), SG("Strike", "HARD", "strike"), SG("Snares", "RVOL RDEC", "noise")],
	"P-I-ML": [SG("Body", "PTCH DEC", "osc"), SG("Strike", "HARD TENS", "strike")],
	"P-I-MA": [AENV("DEC"), SG("Grains", "GRNS GLEN SIZE HARD", "grains")],
	"P-I-HH": [SG("Metal", "PTCH CLSN RING", "metal"), SG("Decay · close", "DEC CLOS", "env"), SG("EQ", "BR AU AG", "spec")],
	"P-I-RC": [SG("Metal", "PTCH HARD RING", "metal"), SG("Decay · grab", "DEC GRAB", "env"), SG("EQ", "BR AU AG", "spec")],
	"GND-SIN": [SG("Pitch", "PTCH RAMP RDEC", "pitch"), AENV("DEC")],
	"GND-NS": [AENV("DEC")],
	"GND-IM": [SG("Impulse", "UP UVAL DOWN DVAL", "imp")],
	"INP-GA": [SG("Input", "VOL GATE", "ingate"), SG("Gate env", "ATCK HLD DEC", "env")],
	"INP-FA": [SG("Input", "ALEV GATE", "ingate"), SG("Filter env", "FATK FHLD FDEC FDPH", "env"), SG("Filter", "FFRQ FQ", "resp")],
	"INP-EA": [SG("Amp env", "AVOL AHLD ADEC", "env"), SG("Filter env", "FDPH FHLD FDEC", "env"), SG("Filter", "FFRQ FQ", "resp")],
	MID: [SG("Notes", "NOTE N2 N3", "chord"), SG("Length · velocity", "LEN VEL", "note"), SG("Controllers", "PB MW AT", "bars")],
	CTR: [SG("Parameters", "P1 P2 P3 P4 P5 P6 P7 P8", null)],
	"CTR-RE": [SG("Delay", "TIME FB", "taps"), SG("Modulation", "MOD MFRQ", "trem"), SG("Filter", "FILTF FILTW", "resp"), outG("MONO LEV")],
	"CTR-GB": [SG("Reverb", "DVOL PRED DEC DAMP", "verb"), SG("Filter", "HP LP", "resp"), outG("GATE LEV")],
	"CTR-EQ": [SG("EQ", "LF LG PF PG PQ HF HG", "eq3"), outG("GAIN")],
	"CTR-DX": [SG("Curve", "TRHD RTIO KNEE", "comp"), SG("Attack · release", "ATCK REL", "env"), outG("HP OUTG MIX")],
	ROM: SMP_G, "RAM-P": SMP_G,
	"RAM-R": [SG("Main in", "MLEV MBAL", "lvbal"), SG("Input", "ILEV IBAL", "lvbal"), SG("Cue", "CUE1 CUE2", "bars"), SG("Record", "LEN RATE", "rec")] };
/* machines that share a synthesis page */
const SYN_SAME = { "TRX-XC": "TRX-XT", "TRX-OH": "TRX-CH", "P-I-CC": "P-I-RC", "INP-GB": "INP-GA", "INP-FB": "INP-FA", "INP-EB": "INP-EA" };
function synTable(m, cat) {
	const x = machineFacts(m, cat), k = SYN_SAME[m] || m;
	if (x.key === "E12") return e12Groups(m, cat);
	return SYN_TAB[k] || (x.recorder ? SYN_TAB["RAM-R"] : x.player ? SYN_TAB["RAM-P"] : null) || SYN_TAB[x.key] || [];
}
/* the effects and routing pages' groups: key → [title, screen] (MID's CC pairs and CTR-8P's targets by pattern) */
const FXRT_GRP = { am: ["Amp mod", "am"], eq: ["EQ", "peq"], flt: ["Filter", "flt"], srr: ["Sample rate", "srr"], drv: ["Drive", "dist"], mix: ["Level · pan", "pan"], snd: ["Sends", "sends"], prog: ["Program", null] };
const FXRT_BY = { AMD: "am", AMF: "am", EQF: "eq", EQG: "eq", FLTF: "flt", FLTW: "flt", FLTQ: "flt", SRR: "srr", DIST: "drv", VOL: "mix", PAN: "mix", DEL: "snd", REV: "snd", PCHG: "prog" };
function fxrtKey(n, g) {
	const cc = /^CC(\d)[DV]$/.exec(n), p = /^P(\d)(TR|PA)$/.exec(n);
	if (cc) return ["cc" + cc[1], "CC " + cc[1], null];
	if (p) return ["p" + p[1], "P" + p[1] + " target", null];
	const k = FXRT_BY[n]; return k ? [k, ...FXRT_GRP[k]] : [g + "page", g === "fx" ? "Effects page" : "Routing page", null];
}
/* LFOS LFOD LFOM are the LFO's SPD DEPTH SHMIX (the same kit parameters, lfoParams) */
const LFO_RT = ["LFOS", "LFOD", "LFOM"];
/* each screen's help: its tooltip */
const SND_TIP = {
	sample: "Drag STRT and END. Ticks = STRT locks per step (the chops). END left of STRT = reverse. The waveform is the slot's own, read from the machine.",
	rec: "Drag LEN to set the recording length (127 = 2 bars). The waveform is the last take, read from the machine.",
	env: "The level after a trig: the attack rises, HOLD keeps it, the decay lets it fall; STOP, CLOS or GRAB cut it, STRT skips the start (dashed). Drag the dots; a dot on a rail at the foot is a knob the curve shows.",
	pitch: "The pitch after a trig: the dashed line is PTCH; RAMP or BUMP start above it and fall at RDEC or BENV, BEND bends up or down into it. Drag the dots.",
	atk: "The first moments of the hit: the click (STRT, TICK, CLIC, SNAP) and its length, the noise burst (NOIS, dashed), a second attack (DUAL), over the body (faint). Drag the dots up.",
	wave: "Two cycles through the tone stage (dashed = clean): harmonics, drive, its hardness, fewer bits. Drag the dots.",
	claps: "The clap: how many hands (the last one), how far apart (the second), how hard (the first); the last one's tail. Drag the dots.",
	room: "The claps (faint), then the room: ROOM how loud, RSIZ how long, RTUN its tone. Drag the dots.",
	spec: "The tone, low to high: where its colour sits and how much (the dot), or the lows and highs. Drag the dots.",
	metal: "The partials, low to high: pitch and spread move them sideways, the amounts lift them. Drag the dots.",
	osc: "The body after a trig: how fast it swings (the first peak, sideways), how long (dashed, sideways); knobs on the rails at the foot shape it. Drag the dots.",
	fm: "The carrier after a trig and the modulation that moves its tone (dotted: how much, how long). The modulator's frequency and feedback are on the rails. Drag the dots.",
	noise: "The noise burst: how loud (up), how long (sideways); HPF thins it. Drag the dots.",
	strike: "The mallet's hit: how hard (up), how soft a mallet (sideways), the skin's tension (dashed). Drag the dots.",
	grains: "The grains: how many, how long the shake, how hard each. Drag the dots.",
	trem: "The level the tremolo leaves: the dot is its speed (sideways) and depth (down).",
	resp: "The filter's response, low to high. Drag the edges sideways; up for the peak where the filter has a Q.",
	imp: "The impulse: how long and how far it goes up, then down. Drag the corners.",
	ingate: "The input (faint) and what passes the gate: louder than GATE (dashed), at the input's volume. Drag the dots up.",
	chord: "The notes a trig sends: NOTE on the keyboard at the foot, N2 and N3 semitones above it. Drag the dots.",
	note: "One note: LEN how long, VEL how loud. Drag the corner.",
	bars: "One bar a knob. Drag the bars' tops.",
	lvbal: "The input in the stereo field: balance sideways, level up. Drag the dot.",
	taps: "The repeats: TIME apart, FB how much each keeps. Drag the second one.",
	verb: "The dry hit (DVOL), then PRED later the tail, DEC long, DAMP smoother. Drag the dots.",
	eq3: "The master EQ from this machine: the shelves and the peak, sideways for the frequency, up to boost. PQ is the peak's width.",
	comp: "The compressor, input to output (dashed = unchanged): TRHD where it starts, RTIO how much, KNEE how softly. Drag the dots.",
	rtrg: "Retrigs: RTRG is how many (drag the last hit sideways), RTIM the time between them, relative to the tempo (drag the second hit).",
	am: "Tremolo: the level the amplitude modulator leaves. Dot = AMF (sideways) and AMD (up).",
	peq: "One band: EQF moves it, EQG boosts (up) or cuts (down).",
	flt: "24 dB filter: the pass band from FLTF to FLTF + FLTW. Left dot = FLTF (drag up for FLTQ), right dot = FLTW.",
	srr: "Sample-rate reduction: more SRR, longer held steps. Drag the dot sideways.",
	dist: "Distortion: input to output (dashed = clean). Drag the dot.",
	pan: "The track in the stereo field: PAN sideways, VOL up. Drag the dot.",
	sends: "Sends to the master effects: DEL = Rhythm Echo, REV = Gate Box. Drag the bars' dots.",
	lshape: "One cycle of the LFO: solid = SHP1 and SHP2 (inverted) mixed by SHMIX; faint = the two shapes. Drag the dot sideways for SHMIX.",
	lmotion: "The LFO across one bar of this track: SPD cycles it faster, DEPTH scales it (dashed). UPDTE TRIG restarts it on every trig (the ticks), HOLD keeps the value a trig takes. Dot = SPD (sideways) and DEPTH (up). Speed in 1/128 notes; 16 LFOs per kit." };
