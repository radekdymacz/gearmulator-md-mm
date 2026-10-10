/* ===== Sound by function (MM-PORT-PLAN e): every DATA page's knobs in small groups, data only =====
   The Sound workspace shows the track's sound as groups of a few knobs, each with a title on a rule, a small
   screen where its knobs draw a picture and its boxes (90-sound.js lays them out). The SYNTHESIS page's groups
   are per machine, from the manual's Appendix A (what its knobs do: SWAVE-SAW's unison pair and its subs,
   SID's pulse and its modulation, FM+ per modulator, VO-6's vowel and consonant, an FX machine's effect and
   its input); the fixed pages (AMP, FILTER, EFFECTS) are the same groups on every machine. A group:
     { key, title, pg: "SYN" | "AMP" | "FLT" | "EFX", knobs: [names], idx: [slot 0..7], ed: screen or null,
       note: what a group without a screen does, w: width in boxes (optional) }
   Every knob of a page is in exactly one group (mmGroupCheck; mmSoundTest.js runs it for every machine). A
   knob the table does not name goes to a group of its own page's name, so a table error shows, never hides. */
const SGM=(title,knobs,ed,note,w)=>({title,knobs:knobs?knobs.split(" "):[],ed:ed||null,note:note||"",w});
const TUNE_G=SGM("Pitch","TUNE",null,"TUNE: the machine's pitch, in semitones.");
const INP_G=k=>SGM("Input · mix",k,null,"INP: how loud its input comes in (INPUT on the Mix page). MIX: dry to wet.");
const MM_SYN_TAB={
 "GND-GND":[],
 "GND-SIN":[SGM("Pitch","TUNE",null,"A plain sine at the note. TUNE moves it in semitones.")],
 "GND-NOIS":[SGM("Noise","ST RED STON","noise"),TUNE_G],
 "SID-6581":[SGM("Oscillator","WAVE","sidwave"),SGM("Pulse","PW PWAD PWRS","pulse"),SGM("Modulation","MOD MSRC MFRQ",null,"RING and SYNC take a second frequency: MFRQ, or PRCH, the note of the track before."),TUNE_G],
 "SWAVE-SAW":[SGM("Unison","UNIL UNIW UNIX","uni"),SGM("Sub","SUBX SUB1 SUB2","sub"),TUNE_G],
 "SWAVE-PULS":[SGM("Unison","UNIL UNIW","uni"),SGM("Sub","SUB1 SUB2","sub"),SGM("Pulse","PW PWAD PWRS","pulse"),TUNE_G],
 "SWAVE-ENS":[SGM("Chord","PCH2 PCH3 PCH4","chord"),SGM("Wave","WAVE PW","enswave"),SGM("Chorus","CHRL CHRW",null,"CHRL: how much ensemble chorus. CHRW: how wide it spreads."),TUNE_G],
 "DPRO-DENS":[SGM("Chord","PCH2 PCH3 PCH4","chord"),SGM("Wave","WAVE","enswave"),SGM("Chorus","CHRL CHRW",null,"CHRL: how much ensemble chorus. CHRW: how wide it spreads."),TUNE_G],
 "DPRO-WAVE":[SGM("Wave","WAVE WP WPM WPRS","morph"),SGM("Sync","SYNC SFRQ",null,"SYNC: hard sync off, from SFRQ, or from PRCH, the note of the track before."),TUNE_G],
 "DPRO-BBOX":[SGM("Drum","PTCH STRT","drum"),SGM("Retrig","RTRG RTIM","rtrg")],
 "DPRO-DDRW":[SGM("Waves","WAV1 MIX WAV2 TIME","morph"),SGM("Bits","BR1 BR2",null,"BR1, BR2: the bit reduction of each wave."),SGM("Pitch","WID TUNE",null,"WID: how far apart the two waves are pitched. TUNE: the machine's pitch.")],
 "FM+STAT":[SGM("Modulator 1","1FRQ 1FIN 1ENV 1FB","fm"),SGM("Modulator 2","2FRQ 2VOL","fm"),SGM("Tone · pitch","TONE TUNE",null,"TONE: more high harmonics from every FM block. TUNE: the pitch.")],
 "FM+PAR":[SGM("Block 1","1FRQ 1ENV","fm"),SGM("Block 2","2FRQ 2ENV","fm"),SGM("Block 3","3FRQ 3ENV","fm"),SGM("Tone · pitch","TONE TUNE",null,"TONE: more high harmonics from every FM block. TUNE: the pitch.")],
 "FM+DYN":[SGM("Modulator 1","1FRQ 1FEN 1VOL 1VEN","fm"),SGM("Modulator 2","2FRQ 2ENV 2FB","fm"),TUNE_G],
 "VO-6":[SGM("Vowel","VOC1 VOC2 V-SW VOIC","vowel"),SGM("Consonant","CONS CLEN CVOL",null,"A trig is a consonant, then the vowel. CLEN: how long, CVOL: how loud (not every consonant has them)."),TUNE_G],
 "FX-THRU":[SGM("Input","INP","fxin")],
 "FX-REVERB":[SGM("Reverb","DEC DAMP GATE","verb"),SGM("Filter","HP LP","hplp"),INP_G("MIX INP")],
 "FX-CHORUS":[SGM("Chorus","DEL DEP SPD FB","sweep"),SGM("Tone · width","WID LP",null,"WID: the stereo width. LP: the low-pass on the chorus."),INP_G("MIX INP")],
 "FX-DYNAMIX":[SGM("Curve","THRS RAT GAIN","comp"),SGM("Timing","ATK REL RMS",null,"ATK 0.5-100 ms, REL 50 ms-5 s. RMS: how it hears the level."),INP_G("MIX INP")],
 "FX-RINGMOD":[SGM("Carrier","WAVE EXT","carrier"),INP_G("MIX INP")],
 "FX-PHASER":[SGM("Sweep","CNTR DEP SPD FB","sweep"),SGM("Width","WID",null,"WID: the stereo width."),INP_G("MIX INP")],
 "FX-FLANGER":[SGM("Sweep","DEL DEP SPD FB","sweep"),SGM("Width","WID",null,"WID: the stereo width."),INP_G("MIX INP")]};
/* the fixed pages: the same on every machine */
const MM_FIXED_TAB={
 AMP:[SGM("Envelope","ATK HOLD DEC REL","amp"),SGM("Drive","DIST","dist"),SGM("Level · pan","VOL PAN","pan"),SGM("Glide","PORT",null,"PORT: the slide from the note before (KIT › TRIG says when).")],
 FLT:[SGM("Filter","BASE WDTH HPQ LPQ","flt"),SGM("Filter env","ATK DEC BOFS WOFS","fenv")],
 EFX:[SGM("EQ","EQF EQG","eq"),SGM("Sample rate","SRR","srr",null,2),SGM("Delay","DTIM DSND DFB","delay"),SGM("Delay filter","DBAS DWID","dflt")]};
const MM_PG_WORD={SYN:"synth",AMP:"amp",FLT:"filter",EFX:"effects",MID:"midi"};
const sgKey=t=>t.toLowerCase().replace(/[^a-z0-9]+/g,"-");
/* one page's groups: the table's names resolved to slots, the knobs it leaves out in a group of their own */
function mmPageGroups(pg,names,table){const out=[],used=new Set();
 for(const d of table){const idx=[],knobs=[];for(const n of d.knobs){const i=names.indexOf(n);if(i<0||!n||used.has(i))continue;used.add(i);idx.push(i);knobs.push(n)}
  if(idx.length)out.push({key:sgKey(d.title),title:d.title,pg,knobs,idx,ed:d.ed,note:d.note,w:d.w})}
 const rest=names.map((n,i)=>n&&!used.has(i)?i:-1).filter(i=>i>=0);
 if(rest.length)out.push({key:pg.toLowerCase(),title:{SYN:"Synthesis",AMP:"Amp",FLT:"Filter",EFX:"Effects"}[pg],pg,knobs:rest.map(i=>names[i]),idx:rest,ed:null,note:""});
 return out}
/* a machine's groups, page by page: { SYN: [...], AMP: [...], FLT: [...], EFX: [...] } */
function mmSoundGroups(m){const mk=MACH[m];if(!mk)return null;
 return{SYN:mmPageGroups("SYN",mk.p,MM_SYN_TAB[m]||[]),AMP:mmPageGroups("AMP",FIXED.AMP,MM_FIXED_TAB.AMP),FLT:mmPageGroups("FLT",FIXED.FLT,MM_FIXED_TAB.FLT),EFX:mmPageGroups("EFX",FIXED.EFX,MM_FIXED_TAB.EFX)}}
/* the check: every named knob of every page in exactly one group, a group's knobs on its page, no group a
   catch-all, every table name a knob of that machine. -> [] or the problems, in words */
function mmGroupCheck(m){const g=mmSoundGroups(m),errs=[];if(!g)return["no machine "+m];
 for(const pg of ["SYN","AMP","FLT","EFX"]){const names=pg==="SYN"?MACH[m].p:FIXED[pg],seen=new Map();
  for(const x of g[pg])x.idx.forEach((i,k)=>{if(names[i]!==x.knobs[k])errs.push(`${m} ${pg}: ${x.title} names ${x.knobs[k]} at slot ${i}`);seen.set(i,(seen.get(i)||0)+1)});
  names.forEach((n,i)=>{if(!n)return;const c=seen.get(i)||0;if(c!==1)errs.push(`${m} ${pg}.${n} is in ${c} groups`)});
  const catchAll=g[pg].find(x=>x.key===pg.toLowerCase()&&!(pg==="SYN"?MM_SYN_TAB[m]||[]:MM_FIXED_TAB[pg]).some(d=>sgKey(d.title)===x.key));
  if(catchAll)errs.push(`${m} ${pg}: ${catchAll.knobs.join(" ")} not in the table`)}
 for(const d of MM_SYN_TAB[m]||[])for(const n of d.knobs)if(!MACH[m].p.includes(n))errs.push(`${m}: the table names ${n}, the machine has none`);
 return errs}
