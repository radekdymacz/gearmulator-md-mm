"use strict";
/* ===================================================================
   Monomachine Editor mockup. Example state follows the OS 1.32 manual.
   Values are firmware units (0-127, or small enums); display converts.
   =================================================================== */

/* ===== Data from the manual ===== */
const PAGES=["SYN","AMP","FLT","EFX","LF1","LF2","LF3"];
const PAGEN={SYN:"Synthesis",AMP:"Amplification",FLT:"Filter",EFX:"Effects",LF1:"LFO 1",LF2:"LFO 2",LF3:"LFO 3",MID:"MIDI page"};
const FIXED={AMP:["ATK","HOLD","DEC","REL","DIST","VOL","PAN","PORT"],FLT:["BASE","WDTH","HPQ","LPQ","ATK","DEC","BOFS","WOFS"],EFX:["EQF","EQG","SRR","DTIM","DSND","DFB","DBAS","DWID"],
 LF1:["PAGE","DEST","TRIG","WAVE","MULT","SPD","INTL","DPTH"],LF2:["PAGE","DEST","TRIG","WAVE","MULT","SPD","INTL","DPTH"],LF3:["PAGE","DEST","TRIG","WAVE","MULT","SPD","INTL","DPTH"],
 MID:["LEN","VEL","PB","PCHG","CC1","CC2","CC3","CC4"]};
/* CC map, Appendix B: per track channel (base + track) */
const CCBASE={SYN:48,AMP:56,FLT:72,EFX:80,LF1:88,LF2:104,LF3:112};
/* Machines, Appendix A. id = the SysEx 0x5B machine number (Appendix C). The 8 SYN slots follow the OS 1.32B screens ("" = unused knob), which differ from the manual text in places. */
const MACH={
 "GND-GND":{id:0,p:[],about:"Silence. A clean slot."},
 "GND-SIN":{id:1,p:["","","","","","","","TUNE"],about:"A plain sine. Clearing a machine gives this one."},
 "GND-NOIS":{id:2,p:["ST","RED","STON","","","","","TUNE"],about:"Noise from white (RED 0) through pink (64) to red (127)."},
 "SID-6581":{id:3,p:["PW","PWAD","PWRS","WAVE","MOD","MSRC","MFRQ","TUNE"],about:"MOS 6581 oscillator with ring mod and sync. MSRC = PRCH takes the second frequency from the track before."},
 "SWAVE-SAW":{id:4,p:["UNIL","UNIW","UNIX","","SUBX","SUB1","SUB2","TUNE"],about:"Eight saw oscillators: base, unison pair, extended pair, three subs."},
 "SWAVE-PULS":{id:5,p:["UNIL","UNIW","SUB1","SUB2","PW","PWAD","PWRS","TUNE"],about:"Five pulse oscillators, three with pulse width sweep."},
 "DPRO-WAVE":{id:6,p:["WAVE","WP","WPM","WPRS","SYNC","SFRQ","","TUNE"],about:"32 raw 12-bit waveforms. WP morphs into the next one."},
 "DPRO-BBOX":{id:7,p:["PTCH","STRT","","","RTRG","RTIM","",""],about:"24 drum sounds from C-3 upwards. The key picks the sound."},
 "FM+STAT":{id:8,p:["1FRQ","1FIN","1ENV","1FB","2FRQ","2VOL","TONE","TUNE"],about:"The versatile FM+: two listed-frequency modulators."},
 "FM+PAR":{id:9,p:["1FRQ","1ENV","2FRQ","2ENV","3FRQ","3ENV","TONE","TUNE"],about:"Three parallel copies of the FM+ block."},
 "FM+DYN":{id:10,p:["1FRQ","1FEN","1VOL","1VEN","2FRQ","2ENV","2FB","TUNE"],about:"Continuous modulator frequencies with their own envelopes."},
 "VO-6":{id:11,p:["VOC1","VOC2","V-SW","VOIC","CONS","CLEN","CVOL","TUNE"],about:"Formant voice. A trig is a consonant then a vowel: lock them per step to spell words."},
 "SWAVE-ENS":{id:14,p:["PCH2","PCH3","PCH4","WAVE","PW","CHRL","CHRW","TUNE"],about:"Up to four-note chords from one track. Lock PCH2-4 to change chord."},
 "DPRO-DDRW":{id:32,p:["WAV1","MIX","WAV2","TIME","BR1","WID","BR2","TUNE"],mk2:true,about:"Two of the 64 waveforms (factory or user) blended. MKII only."},
 "DPRO-DENS":{id:33,p:["PCH2","PCH3","PCH4","WAVE","","CHRL","CHRW","TUNE"],mk2:true,about:"Chords from one of the 64 waveforms. MKII only."},
 "FX-THRU":{id:12,p:["","","","","","","","INP"],fx:1,about:"Passes its input through the track effects. Needs one trig to open."},
 "FX-REVERB":{id:13,p:["DEC","DAMP","GATE","MIX","HP","LP","","INP"],fx:1,about:"The Machinedrum's gated reverb. GATE 127 turns the gate off."},
 "FX-CHORUS":{id:15,p:["DEL","DEP","SPD","MIX","FB","WID","LP","INP"],fx:1,about:"2 × 3 tap stereo chorus."},
 "FX-DYNAMIX":{id:16,p:["ATK","REL","THRS","MIX","RAT","GAIN","RMS","INP"],fx:1,about:"Compressor. RAT 127 = limiter."},
 "FX-RINGMOD":{id:17,p:["WAVE","EXT","","MIX","","","","INP"],fx:1,about:"Ring-modulates the input with a sine-to-triangle carrier."},
 "FX-PHASER":{id:18,p:["CNTR","DEP","SPD","MIX","FB","WID","","INP"],fx:1,about:"Sweeping phaser, also widens."},
 "FX-FLANGER":{id:19,p:["DEL","DEP","SPD","MIX","FB","WID","","INP"],fx:1,about:"Flanger: notches spread evenly."}};
const FAMS=[["SWAVE","SuperWave · analogue style"],["SID","SID · 6581 chip"],["DPRO","DigiPRO · digital waves"],["FM+","FM+ · frequency modulation"],["VO","VO · voice"],["GND","GND · basic"],["FX","FX · processes other audio"]];
const famKey=m=>m.startsWith("FM+")?"FM+":m.split("-")[0];
const machName=m=>({"SWAVE-SAW":"SuperWave saw","SWAVE-PULS":"SuperWave pulse","SWAVE-ENS":"SuperWave ensemble","SID-6581":"SID 6581","DPRO-WAVE":"DigiPRO wave","DPRO-BBOX":"DigiPRO beat box","DPRO-DDRW":"DigiPRO double draw","DPRO-DENS":"DigiPRO ensemble","FM+STAT":"FM+ static","FM+PAR":"FM+ parallel","FM+DYN":"FM+ dynamic","VO-6":"VO-6 voice","GND-GND":"Ground","GND-SIN":"Sine","GND-NOIS":"Noise","FX-THRU":"Through","FX-REVERB":"Gate reverb","FX-CHORUS":"Chorus","FX-DYNAMIX":"Dynamix","FX-RINGMOD":"Ring modulator","FX-PHASER":"Phaser","FX-FLANGER":"Flanger"})[m]||m;
const isFx=m=>!!MACH[m]?.fx;

/* Enumerated values. Everything else is 0-127. */
/* LFO / ASSIGN PAGE, WAVE, MULT and PTCH lists in firmware order (index = firmware value, read from the OS 1.32B screens) */
const LPAGES=["PTCH","SYN","AMP","FLT","EFX","LF1","LF2","LF3","MID"];
const PTCHD=["1/12","2/12","7/12","1OCT","2OCT","4OCT","8OCT","16OCT"];
const LTRIG=["FREE","TRIG","HOLD","ONE","HALF"];
const LWAVE=["TRI","ITRI","SAW","ISAW","SQR","ISQR","EXP","IEXP","RMP","IRMP","RND"];
const LMULT=["1X","2X","4X","8X","16X","32X","64X"];
const CONS=["-","B","D","F","G","H","J","K","L","M","N","P","R","RR","S","SJ","T","TH","TJ","V","Z"];
const EN={"SID-6581.WAVE":["TRI","SAW","PULS","MIX","NOIS"],"SID-6581.MOD":["OFF","RING","SYNC","R+S"],"SID-6581.MSRC":["MFRQ","PRCH"],
 PWRS:["OFF","ON"],WPRS:["OFF","ON"],STON:["OFF","ON"],"V-SW":["OFF","ON"],CONS};
const SIGNED=new Set(["PAN","EQG","DIST","DSND","TUNE","PB"]);
const DEFV={SYN:[0,0,0,0,0,0,0,64],AMP:[0,40,64,20,64,100,64,0],FLT:[0,127,0,0,0,40,0,0],EFX:[64,64,0,32,64,0,0,127],LFO:[2,1,1,0,1,32,0,0],MID:[64,100,64,0,0,0,0,0]};

/* Note names, the MM way: C-4 = MIDI 60 */
const NN=["C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"];
const noteName=n=>n==null?"---":n<12?"LOW":NN[((n%12)+12)%12]+(Math.floor(n/12)-1);
const KEYS=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
const MAJ=[0,2,4,5,7,9,11],MIN=[0,2,3,5,7,8,10];
const BBOX={48:"BD1",49:"SD1",50:"TOM1",51:"TOM2",52:"BONG",53:"CLAP",54:"RIM",55:"COW",56:"CH",57:"OH",58:"RIDE",59:"CRSH",60:"BD2",61:"SD2",62:"TIMB",63:"AGGO",64:"TIMP",65:"SNAP",66:"WOOD",67:"TRI",68:"SHKR",69:"MARA",70:"WHSL",71:"BLIP"};
