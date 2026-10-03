/* ===== From the Machinedrum Editor (2026-10, MM-PORT-PLAN.md a-c): Alt as the global "all", the key map's
   track and transport keys, the home-row keyboard, and the small comforts. On the Monomachine every edit here
   is a change of the view's own state followed by its intent (edit(), the Machinedrum's ops: clearPattern,
   clearLocks, rotate, doublePattern, lock, pasteSteps, steps): one gesture is one undo step (a commit ends it). ===== */

/* ---- Alt, a global modifier: Alt + CLR (or Alt + Delete in Sequence) clears the whole pattern, Alt + the lock
   lane's clear key every lock of the selected track. While Alt is held the keys it changes say so. ---- */
S.alt=false;
function altLabels(){const c=$('[data-sec="clear"]');if(c){c.textContent=S.alt?"All":"Clr";c.title=S.alt?`Clear the whole pattern ${patName(S.pat)}: every track's notes, slides and locks (one undo step)`:"Clear (Delete). Alt: the whole pattern"}
 const cl=$("#clearLane");if(cl){const t=S.alt?`Clear every lock of ${tLabel(S.sel)}`:`Clear ${pidLabel(S.sel,S.lane)} locks. Alt: every lock of ${tLabel(S.sel)}`;cl.title=t;cl.setAttribute("aria-label",t)}}
function showAlt(on){if(S.alt===on)return;S.alt=on;document.body.classList.toggle("althold",on);altLabels();if(S.gen&&S.ws==="seq")genDraw();if(S.gen&&S.ws==="sound")renderMutStrip()}
addEventListener("keydown",e=>showAlt(e.altKey),true);addEventListener("keyup",e=>showAlt(e.altKey),true);addEventListener("blur",()=>showAlt(false));
document.addEventListener("pointermove",e=>showAlt(e.altKey),{passive:true,capture:true});
function clearPattern(){[...S.tracks,...S.midi].forEach(tr=>{tr.steps=tr.steps.map(()=>null);tr.slide=new Set()});S.locks.clear();edit("clearPattern",{});render();toast(`Cleared ${patName(S.pat)}: every track's notes, slides and locks.`)}
function clearTrackLocks(t){const n=trackLockPids(t).length;[...S.locks.keys()].forEach(k=>{if(+k.split("|")[0]===t)S.locks.delete(k)});if(!n){toast(tLabel(t)+" has no locks.");return}edit("clearLocks",{t});render();toast(`Cleared every lock of ${tLabel(t)} (${n} parameter${n>1?"s":""}).`)}

/* ---- the lock budget over the lock lane: the machine's 62 locked parameters in a pattern, pooled over all
   twelve tracks (manual 1-58), as the LCD's meter (dark from 52, blinking at 62) ---- */
function syncLockBudget(){const el=$("#lockbudget");if(!el)return;const n=S.locks.size;el.className="lockbudget"+(n>=62?" full":n>=52?" warn":"");el.innerHTML=`<b>${n}</b> / 62 locked parameters`;
 el.title=`The Monomachine holds 62 locked parameters in a pattern, over all twelve tracks (a track and a parameter with any lock counts once). ${n} in use, ${62-n} left.${n>=62?" Clear a lane before you lock a new parameter.":""}`}

/* ---- rotate: Alt + Left / Right (FUNCTION + arrows on the machine) moves the selected track's steps, slides
   and locks one step, wrapping at the length; swing stays on its steps (the groove, not the notes). The presses
   while Alt stays down are one undo step (rotHold holds the commit). ---- */
let rotHold=false;
addEventListener("keyup",e=>{if(e.key==="Alt")rotHold=false},true);addEventListener("blur",()=>{rotHold=false});
const seqKeys=()=>S.ws==="seq"&&!dialogOpen()&&$("#keyspop").hidden;
const rotStep=(s,by,len)=>((s+by)%len+len)%len;
function rotateTrack(by){const t=S.sel,tr=trk(t),len=S.len;if(len<2)return;
 const st=tr.steps.slice(0,len),sl=new Set();for(let s=0;s<len;s++){tr.steps[rotStep(s,by,len)]=st[s];if(tr.slide.has(s))sl.add(rotStep(s,by,len))}
 for(const s of tr.slide)if(s>=len)sl.add(s);tr.slide=sl;
 for(const[k,m] of S.locks){if(+k.split("|")[0]!==t)continue;const n=new Map();for(const[s,v] of m)n.set(s<len?rotStep(s,by,len):s,v);S.locks.set(k,n)}
 rotHold=true;edit("rotate",{t,by});rerenderSeq()}

/* ---- every-N fill: ⌘-click a step in the roll, every 2nd step from there to the end gets a note at that pitch
   (⌘⇧: every 4th); from a step with a note they go off, with their locks. A NOTE OFF is left alone. ---- */
function fillEvery(t,s,n,pitch){const tr=trk(t),end=S.len,on=!(tr.steps[s]&&!tr.steps[s].off);let ch=0;
 for(let k=s;k<end;k+=n){const st=tr.steps[k];if(st?.off)continue;if(on&&!st){tr.steps[k]=note(pitch);ch++}else if(!on&&st){tr.steps[k]=null;tr.slide.delete(k);clearStepLocks(t,k);ch++}}
 const nth=n===2?"2nd":"4th";if(!ch){toast(`${tLabel(t)} already has every ${nth} step ${on?"on":"off"} from step ${s+1}.`);return}
 edit("steps",{from:s,to:end,rows:[rangeRow(t,s,end)]});renderTop();rerenderSeq();toast(`${on?"Filled every":"Cleared every"} ${nth} step of ${tLabel(t)}, steps ${s+1}–${end}${on?" with "+noteName(pitch):""}.`)}

/* ---- the wheel over a lock-lane step with a trig moves its lock in the lane's parameter (from the kit value when
   it has none): 4 a notch (1 on a short list), ⇧ 1. A run of notches on one step is one undo step. The roll's own
   wheel stays the pitch scroll. ---- */
let wheelLock=null;
$("#main").addEventListener("wheel",e=>{const el=e.target.closest("#lane .lb");if(!el||S.ws!=="seq")return;const t=S.sel,s=+el.dataset.s,st=trk(t).steps[s],d=e.deltaY||(e.shiftKey?e.deltaX:0);
 if(!d||!st||st.off)return;e.preventDefault();const[pg,i]=S.lane.split("."),m=meta(t,pg,+i),mx=maxOf(m),now=performance.now(),key=t+"|"+S.lane+"|"+s;
 if(!wheelLock||wheelLock.key!==key||now-wheelLock.at>700){commit();wheelLock={key}}wheelLock.at=now;
 const had=S.locks.get(lkKey(t,S.lane))?.get(s),cur=had??getP(t,S.lane)??0,v=clamp(cur+(d<0?1:-1)*(e.shiftKey||mx<16?1:4),0,mx);if(had!=null&&v===had)return;
 if(!setLock(t,S.lane,s,v))return;edit("lock",{t,...pidArgs(S.lane),s,v});renderTop();renderLane();toast(`${pidLabel(t,S.lane)} ${fmt(m,v)} · ${tLabel(t)}, step ${s+1}`)},{passive:false});

/* ---- a ramp in the lock lane: ⇧-drag draws a straight line from the press to the pointer, shown as it moves; at
   the release every step with a trig under it is locked on the line, one gesture. ---- */
function lanePoint(e){const cells=$$("#lane .lb");if(!cells.length)return null;const el=cells.find(c=>e.clientX<c.getBoundingClientRect().right)||cells[cells.length-1],r=el.getBoundingClientRect(),{mx}=laneMeta();
 return{s:+el.dataset.s,v:clamp(Math.round((r.bottom-6-e.clientY)/(r.height-14)*mx),0,mx)}}
function rampPoints(){const d=laneDraw,tr=trk(S.sel);if(!d||!d.from)return[];const{mx}=laneMeta(),a=Math.min(d.from.s,d.to.s),b=Math.max(d.from.s,d.to.s),out=[];
 for(let s=a;s<=b;s++){const st=tr.steps[s];if(!st||st.off)continue;const v=d.to.s===d.from.s?d.to.v:d.from.v+(d.to.v-d.from.v)*(s-d.from.s)/(d.to.s-d.from.s);out.push([s,clamp(Math.round(v),0,mx)])}return out}
function rampAt(e){const p=lanePoint(e);if(!p)return;if(!laneDraw.from)laneDraw.from=p;laneDraw.to=p;renderLane();
 for(const[s,v] of rampPoints()){const el=document.querySelector(`#lane .lb[data-s="${s}"]`);if(!el)continue;el.querySelector("i")?.remove();el.insertAdjacentHTML("beforeend",barHTML(v));el.classList.add("ramp")}}
function rampSend(){let n=0;for(const[s,v] of rampPoints()){if(!setLock(S.sel,S.lane,s,v,n>0))break;edit("lock",{t:S.sel,...pidArgs(S.lane),s,v});n++}return n>0}

/* ---- double the pattern (LEN ×2 on LCD line 2): length × 2, the new half a copy of every track's steps, slides,
   swing and locks (64 steps is the longest) ---- */
function doublePattern(){const len=S.len;if(len*2>64){toast(`A pattern of ${len} steps cannot double: 64 steps is the longest.`);return}
 [...S.tracks,...S.midi].forEach(tr=>{for(let s=0;s<len;s++){tr.steps[s+len]=tr.steps[s]&&JSON.parse(JSON.stringify(tr.steps[s]));for(const set of[tr.slide,tr.swing])set.has(s)?set.add(s+len):set.delete(s+len)}});
 for(const m of S.locks.values()){for(let s=len;s<2*len;s++)m.delete(s);for(const[s,v] of[...m])if(s<len)m.set(s+len,v)}
 S.len=len*2;edit("doublePattern",{});render();toast(`Doubled ${patName(S.pat)}: ${len} to ${len*2} steps, the new half a copy.`)}

/* ---- paste to many: ⇧-click track headers to mark them, then ⌘V pastes the copied page into each one (from the
   page shown), one gesture. A plain click on a header, or Esc, unmarks them. (S.multi is MULTI TRIG here.) ---- */
S.marks=new Set();
function markToggle(t){S.marks.has(t)?S.marks.delete(t):S.marks.add(t);renderRail();const l=[...S.marks].sort((a,b)=>a-b).map(tLabel).join(" ");toast(S.marks.size?`⌘V pastes into ${l}`:"No tracks marked for paste")}
document.addEventListener("click",e=>{const th=e.target.closest("#rail .th[data-sel]");if(!th||e.target.closest("button,select,.pc,.fader")||S.ws!=="seq")return;
 if(e.shiftKey){e.preventDefault();e.stopImmediatePropagation();markToggle(+th.dataset.sel);return}if(S.marks.size)S.marks.clear()},true);
function pasteToMany(){const[a,b]=vis();if(CLIP?.type!=="page"){toast("Copy a track page first.");return}const ts=[...S.marks].sort((x,y)=>x-y),ok=ts.filter(t=>isMidiT(t)===CLIP.midi);
 ok.forEach(t=>{pastePage(t,a,b);edit("pasteSteps",{t,from:a,to:b})});if(ok.length)render()
 toast((ok.length?`Pasted steps ${a+1}–${b} into ${ok.map(tLabel).join(" ")} (one undo step).`:"")+(ok.length<ts.length?` ${ts.length-ok.length} marked track(s) skipped: pages paste between synth tracks or between MIDI tracks.`:""))}

/* ---- mutes from the keys: M the selected track, Alt+M every track (any audible: mute all; none: unmute all), 0
   unmutes and unsolos every track (also the rail's M/S OFF key). MIDI tracks only where the engine mutes them. ---- */
const muteOk=t=>t<6||!NA.midiMutes;
function mutesChanged(say){tx();if(HOST.mutes)HOST.mutes();render();if(say&&!["seq","sound","mix","perform"].includes(S.ws))toast(say)}
function muteSel(){const t=S.sel;if(!muteOk(t)){toast(NA.midiMutes);return}const tr=trk(t);tr.mute=!tr.mute;mutesChanged(`${tr.mute?"Muted":"Unmuted"} ${tLabel(t)}`)}
function muteAllToggle(){const ts=[...Array(12).keys()].filter(muteOk),on=ts.some(t=>!trk(t).mute);ts.forEach(t=>trk(t).mute=on);mutesChanged();toast(on?"Every track muted (Alt+M again: unmute all)":"Every track unmuted")}
function unmuteAll(){const ts=[...Array(12).keys()];if(!ts.some(t=>trk(t).solo||(trk(t).mute&&muteOk(t)))){toast("No track is muted or soloed.");return}ts.forEach(t=>{trk(t).solo=false;if(muteOk(t))trk(t).mute=false});mutesChanged();toast("Every track unmuted and unsoloed")}
document.addEventListener("click",e=>{if(e.target.closest("#allon"))unmuteAll()});

/* ---- tap tempo (manual p.36's TAP): the average of the last taps; live recording: RECORD + PLAY ---- */
const TAP=[];
function tapTempo(){const now=performance.now();if(TAP.length&&now-TAP[TAP.length-1]>2000)TAP.length=0;TAP.push(now);if(TAP.length>5)TAP.shift();
 if(TAP.length<2){toast("Tap tempo: keep tapping T");return}S.bpm=clamp(Math.round(60000/((TAP[TAP.length-1]-TAP[0])/(TAP.length-1))*10)/10,30,300);if(HOST.tempo)HOST.tempo(S.bpm);renderTop();if(S.playing)restartClock();toast("Tap tempo: "+S.bpm.toFixed(1)+" BPM")}
function liveRecord(){if(HOST.record)return HOST.record(true);S.rec=!S.rec;if(S.rec&&!S.playing)togglePlay();renderTop();if(S.rec)toast("LIVE RECORDING: the notes you play are recorded.")}

/* ---- the keyboard: the home row plays the selected synth track, from any workspace. A S D F G H J K L are white
   keys C D E F G A B C D from C-3 (Z / X: the octave, −3 to +3), real MIDI notes on the track's own channel
   (GLOBAL › MIDI › CHANNELS: base + track), at KB.vel (C / V: 20 40 60 80 100 127; the machine hears it through
   ASSIGN › VEL). Each key is its own note on and off, so legato and the machine's note priority work as on a
   keyboard; while LIVE RECORDING the machine records them. MIDI tracks are not played (their notes go to the
   MIDI OUT only). Key repeat is ignored. ---- */
const KEYS_WHITE="ASDFGHJKL",KEYS_SEMIS=[0,2,4,5,7,9,11,12,14],KEYS_OCT=[-3,3],KEYS_VELS=[20,40,60,80,100,127],KEYS_BASE=48;
const KB={oct:0,vel:100,held:new Map(),told:new Set()};
function keyVel(v,d){const up=KEYS_VELS.find(x=>x>v),down=[...KEYS_VELS].reverse().find(x=>x<v);return d>0?up??KEYS_VELS[KEYS_VELS.length-1]:down??KEYS_VELS[0]}
function kbOn(){return!dialogOpen()&&!LIB.open&&!AP.open&&$("#machpop").hidden&&$("#keyspop").hidden&&!document.activeElement?.closest?.("input,select,textarea,[contenteditable]")}
function kbTell(k,t){if(KB.told.has(k))return;KB.told.add(k);toast(t)}
function homeDown(e){if(e.repeat||KB.held.has(e.code))return;const t=S.sel,k=KEYS_WHITE.indexOf(e.code.replace(/^Key/,""));if(k<0)return;
 if(isMidiT(t)){kbTell("midi","The keys play the synth tracks: a MIDI track's notes go to the MIDI OUT only.");return}
 const n=clamp(KEYS_BASE+12*KB.oct+KEYS_SEMIS[k]);KB.held.set(e.code,{t,n});keyNote(t,n,KB.vel)}
function homeUp(code){const h=KB.held.get(code);if(!h)return;KB.held.delete(code);keyNote(h.t,h.n,0)}
/* a host plays the machine (the note intent: pitch in semitones from KEYS_BASE, C3); the example only shows the track's lamp */
function keyNote(t,n,vel){if(HOST.noteOn){const p=n-KEYS_BASE;return vel?HOST.noteOn(t,p,vel):HOST.noteOff(t,p)}if(vel){flashTracks([t]);kbTell("eg","Example: in the plug-in the keys play the machine.")}}
function kbVel(d){KB.vel=keyVel(KB.vel,d);toast(`Keyboard velocity ${KB.vel}`)}
function kbOct(d){KB.oct=clamp(KB.oct+d,KEYS_OCT[0],KEYS_OCT[1]);toast(`Keyboard octave ${KB.oct>0?"+":""}${KB.oct}: A plays ${noteName(KEYS_BASE+12*KB.oct)}`)}
document.addEventListener("keyup",e=>homeUp(e.code));addEventListener("blur",()=>[...KB.held.keys()].forEach(homeUp));
Keys.bind({keys:[...KEYS_WHITE],group:"Playing",hidden:true,field:true,when:kbOn,run:homeDown,does:""});
Keys.bind({keys:["Z"],group:"Playing",hidden:true,field:true,when:kbOn,run:()=>kbOct(-1),does:""});
Keys.bind({keys:["X"],group:"Playing",hidden:true,field:true,when:kbOn,run:()=>kbOct(1),does:""});
Keys.bind({keys:["C"],group:"Playing",hidden:true,field:true,when:kbOn,run:()=>kbVel(-1),does:""});
Keys.bind({keys:["V"],group:"Playing",hidden:true,field:true,when:kbOn,run:()=>kbVel(1),does:""});
Keys.bind({keys:["A S D F G H J K L"],group:"Playing",does:"Play the selected synth track: white keys C D E F G A B C D, real notes on its MIDI channel, from any workspace. While live recording the machine records them"});
Keys.bind({keys:["Z","X"],group:"Playing",does:()=>`Octave down / up, −3 to +3 (now ${KB.oct>0?"+":""}${KB.oct}: A is ${noteName(KEYS_BASE+12*KB.oct)})`});
Keys.bind({keys:["C","V"],group:"Playing",does:()=>`Velocity down / up: 20 40 60 80 100 127 (now ${KB.vel})`});

/* ---- the selected track's keys, the all keys, the step gestures ---- */
Keys.bind({keys:["M"],code:"KeyM",group:"Selected track",does:"Mute or unmute the selected track",when:kbOn,run:()=>muteSel()});
Keys.bind({keys:["M"],code:"KeyM",mod:"alt",group:"All",does:"Mute every track; when none is audible, unmute every track",when:kbOn,run:()=>muteAllToggle()});
Keys.bind({keys:["ArrowUp","ArrowDown"],group:"Selected track",does:"Select the previous / next track of the side shown (a focused value keeps ↑ / ↓ for itself)",
 when:()=>kbOn()&&$("#kpop").hidden&&S.ws!=="song",run:e=>{const sd=side(),i=sd.indexOf(S.sel);select(sd[((i<0?0:i)+(e.key==="ArrowDown"?1:5))%6])}});
Keys.bind({keys:["T"],group:"Transport",does:"Tap tempo (the average of the last taps)",when:kbOn,run:()=>tapTempo()});
Keys.bind({keys:["ArrowLeft","ArrowRight"],mod:"alt",group:"Selected track",does:"Sequence: rotate the selected track one step earlier / later: notes, slides and locks, wrapping at the length. Presses while ⌥ is down are one undo step. The one Alt that is not \"all\": FUNCTION + arrows on the machine",when:seqKeys,run:e=>rotateTrack(e.key==="ArrowRight"?1:-1)});
Keys.bind({keys:["0"],group:"All",does:"Unmute and unsolo every track",when:kbOn,run:()=>unmuteAll()});
Keys.bind({keys:["Escape"],group:"Sequence",does:"Unmark the tracks marked for paste",when:()=>seqKeys()&&S.marks.size>0,run:()=>{S.marks.clear();renderRail()}});
Keys.bind({keys:["roll"],mod:"cmd",group:"Sequence",does:"Click: every 2nd step from there to the end gets a note at that pitch (from a note: off), one undo step"});
Keys.bind({keys:["roll"],mod:"cmd+shift",group:"Sequence",does:"Click: every 4th step from there to the end"});
Keys.bind({keys:["wheel on a lock step"],group:"Sequence",does:"Move its lock in the lane's parameter, 4 a notch (⇧: 1)"});
Keys.bind({keys:["lock lane"],mod:"shift",group:"Sequence",does:"Drag: a ramp, a straight line from the press to the release (one undo step)"});
Keys.bind({keys:["track header"],mod:"shift",group:"Sequence",does:"Click: mark the track for paste; ⌘V then pastes into every marked track (one undo step)"});
