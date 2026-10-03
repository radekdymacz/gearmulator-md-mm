/* ===== GEN and MUTATE, the Monomachine's own (MM-PORT-PLAN.md d) =====
   Pure: values in, values out, nothing of the page. The generators without a machine in them (euclid, random,
   the seeds, genFit, genRunFor, mutPull, genNotes) are skins/shared/deskGen.js, both editors' one module,
   included just before this file (build.sh, sync-mmstudio-skin.py). Here the roles of the
   Monomachine's machines and their defaults, a track's steps from a spec (the MM's steps carry their notes),
   and the mutation of a track's DATA pages. skins/mmStudio/mmGenTest.js checks them in node. */

/* ---- roles, from the machine, first match wins; a lead machine that plays low notes is a bass ---- */
const MM_GEN_ROLES=[["keep",/^(FX-|GND-GND$)/],["drums",/^DPRO-BBOX$/],["pad",/^(SWAVE-ENS|DPRO-DENS)$/],["bass",/^SWAVE-(SAW|PULS)$/],
 ["voice",/^VO-6$/],["lead",/^(SID-6581|FM\+|DPRO-(WAVE|DDRW))/],["other",/./]];
/* DPRO-BBOX: the key picks the drum (from C-3 = 48). The notes of a drum box are BD1 SD1 CH OH */
const MM_DRUMS=[48,49,56,57];
const MM_SCALES=["MAJ","MIN","PENT","DORIAN"];
function mmGenRole(m,notes=[],midi=false){if(midi)return"midi";const r=(MM_GEN_ROLES.find(([,re])=>re.test(m||"GND-GND"))||["other"])[0];
 return r==="lead"&&notes.length&&Math.min(...notes)<48?"bass":r}
/* The defaults per role (the table of MM-PORT-PLAN.md d). n: the cycle, min(16, the pattern's length). notes:
   the NOTES group: motion "off" (the steps keep their pitch, a new one takes the track's last note), "step",
   "leap" or "kit" (a drum box: BD SD CH OH); the root is the lowest note at its octave (oct: the role's octave
   when the track has no notes yet). */
const MM_GEN_DEFAULTS={
 bass:n=>({kind:"euclid",k:5,n,rot:0,notes:{motion:"step",range:1,oct:36}}),
 lead:()=>({kind:"random",density:30,mode:"replace",notes:{motion:"step",range:2,oct:60}}),
 pad:n=>({kind:"euclid",k:2,n,rot:0,notes:{motion:"step",range:1,oct:48}}),
 voice:n=>({kind:"euclid",k:4,n,rot:0,notes:{motion:"off",range:1,oct:48}}),
 drums:n=>({kind:"euclid",k:8,n,rot:0,notes:{motion:"kit"}}),
 other:()=>({kind:"random",density:15,mode:"replace",notes:{motion:"off",range:1,oct:48}}),
 midi:n=>({kind:"euclid",k:4,n,rot:0}),
 keep:()=>({kind:"keep"})};
/* the root: the track's KEY when its transpose SCALE is MAJ or MIN (2, 3), else C, the one nearest the track's lowest
   note (or the role's octave; a tie goes down), so that the range stays inside MIDI 0..127 */
function mmGenRoot(key,lo,range=1){const r=lo-(((lo-key)%12)+12)%12;return Math.max(0,Math.min(127-12*range,lo-r>6?r+12:r))}
/* a track's default spec: info = { m, notes: [its notes now], key: 0..11, scale: 0..3 (OFF FIX MAJ MIN) }, len the
   pattern's length; midi: a MIDI track (rhythm only) */
function mmGenDefault(info,len=64,midi=false,seed=genSeed()){const role=mmGenRole(info.m,info.notes,midi),sp=MM_GEN_DEFAULTS[role](Math.min(16,len));
 if(sp.kind!=="keep")sp.seed=seed;
 if(sp.notes&&sp.notes.motion!=="kit"){const tonal=info.scale===2||info.scale===3,key=tonal?info.key|0:0,n=sp.notes;
  n.scale=info.scale===2?"MAJ":info.scale===3?"MIN":"PENT";n.root=mmGenRoot(key,info.notes.length?Math.min(...info.notes):n.oct,n.range);delete n.oct}
 return genFit(sp,len)}
/* the genNotes spec of a track's spec, or null: no notes (off, keep, a MIDI track) */
function mmNoteSpec(sp){const n=sp&&sp.notes;if(!n||n.motion==="off"||sp.kind==="keep")return null;
 return n.motion==="kit"?{pool:MM_DRUMS}:{root:n.root,scale:n.scale,range:n.range,motion:n.motion}}
/* the notes of the last trig before step s in a track's steps (wrapping at len), as the roll's own lastNote but the
   whole chord: what a new step plays when the notes do not write it (a pad's chord stays a chord) */
function mmLastNotes(steps,s,len,midi=false){for(let k=s-1;k>=s-len;k--){const st=steps[((k%len)+len)%len];if(st&&!st.off&&st.n&&st.n.length)return st.n.slice()}return[midi?60:48]}
const mmTrig=st=>!!(st&&!st.off);
/* A track's steps from a spec over [from, to), from base (its 64 steps before the run): every hit a trig, every
   other trig gone, a NOTE OFF where no hit lands kept. A trig the rhythm keeps keeps its step (pitch, envelope
   trigs; its locks stay) unless the notes write it; a new hit is {n: [its note], a, f, l} at its generated note or
   the notes of the track's last trig (a chord stays a chord). The notes write the hits the rhythm wrote: every hit in EUCLID and REPLACE, the new ones
   in ADD, none in THIN (the rhythm decides when, the notes what).
   -> { steps: [[s, step | null]...] for every step of the range, gone: [s...] the trigs turned off (their locks
   and slides go) } | null (keep: leave the track) */
function mmGenSteps(sp,t,from,to,base,len=to,midi=false){
 const cur=base.map(mmTrig),r=generate(sp,t,from,to,cur);if(!r)return null;
 const on=new Set(r.on),ns=midi?null:mmNoteSpec(sp),wrote=r.on.filter(s=>sp.kind!=="random"||sp.mode==="replace"||(sp.mode==="add"&&!cur[s]));
 const pitch=ns&&wrote.length?new Map(genNotes(ns,wrote,sp.seed>>>0,t)):null,steps=[],gone=[];
 for(let s=from;s<to;s++){const st=base[s];
  if(on.has(s)){if(cur[s]){const c=JSON.parse(JSON.stringify(st));if(pitch&&pitch.has(s))c.n=[pitch.get(s)];steps.push([s,c])}
   else steps.push([s,{n:pitch&&pitch.has(s)?[pitch.get(s)]:mmLastNotes(base,s,len,midi),a:1,f:1,l:1}])}
  else if(cur[s]){steps.push([s,null]);gone.push(s)}
  else steps.push([s,st&&st.off?{off:1}:null])}
 return{steps,gone}}
/* the NOTES group's words: "PENT C-2 ×1 STEP", "BD SD CH OH", "" (off) */
function mmNotesTag(sp,name=n=>String(n)){const n=sp&&sp.notes;if(!n||n.motion==="off"||sp.kind==="keep")return"";return n.motion==="kit"?"BD SD CH OH":`${n.scale} ${name(n.root)} ×${n.range} ${n.motion.toUpperCase()}`}

/* ---- MUTATE: each knob in scope pulled toward a random target (mutPull, the Machinedrum's formula), from the
   trial's base, keyed by (seed, track, page × 8 + knob) so a scope change never reshuffles the others.
     spec: { tracks: [t...] (synth tracks 0..5), pages: ["SYN" | "AMP" | "FLT" | "EFX" | "LFO"...], amount 0..100, seed,
             protect: names never moved (default VOL and TUNE: the level and the tuning the notes rely on),
             extra: { t: [[pg, i]...] } knobs of the groups in scope (a group's title on the Sound page), optional }
     base: the synth tracks before the trial: [{ m, v: { SYN: [8], AMP: [8], FLT, EFX, LF1, LF2, LF3 } }...]
     info(t, pg, i): { name, max } of that knob, or null where the machine has none
   -> [[t, pg, i, v]...] for every knob in scope (an unmoved one too, so a new seed from the base resets it).
   Never moved: an FX machine's INP and an LFO's PAGE and DEST (where an LFO goes is routing, not sound), the
   MIDI page (not a sound), GND-GND (nothing to move). ---- */
const MM_MUT_PAGES={SYN:["SYN"],AMP:["AMP"],FLT:["FLT"],EFX:["EFX"],LFO:["LF1","LF2","LF3"]};
const MM_MUT_KEEP=new Set(["INP","PAGE","DEST"]);
const MM_PG_INDEX={SYN:0,AMP:1,FLT:2,EFX:3,LF1:4,LF2:5,LF3:6};
function mmMutate(spec,base,info){const out=[],protect=new Set(spec.protect||["VOL","TUNE"]);
 for(const t of spec.tracks){const tr=base[t];if(!tr||!tr.v||tr.m==="GND-GND")continue;
  for(const sc of spec.pages)for(const pg of MM_MUT_PAGES[sc]||[]){const vals=tr.v[pg];if(!vals)continue;
   for(let i=0;i<8;i++){const k=info(t,pg,i);if(!k||!k.name||protect.has(k.name)||MM_MUT_KEEP.has(k.name))continue;
    out.push([t,pg,i,mutPull(vals[i],spec.amount,genU(spec.seed,t,MM_PG_INDEX[pg]*8+i),k.max)])}}
  /* the groups in scope (spec.extra[t]: [[pg, i]...]), each knob once, by the same rules */
  const have=new Set(out.filter(x=>x[0]===t).map(x=>x[1]+":"+x[2]));
  for(const[pg,i] of (spec.extra&&spec.extra[t])||[]){const vals=tr.v[pg],k=vals&&info(t,pg,i);if(!k||!k.name||protect.has(k.name)||MM_MUT_KEEP.has(k.name)||have.has(pg+":"+i))continue;
   have.add(pg+":"+i);out.push([t,pg,i,mutPull(vals[i],spec.amount,genU(spec.seed,t,MM_PG_INDEX[pg]*8+i),k.max)])}}
 return out}
