
/* ===== GEN and MUTATE on the page (MM-PORT-PLAN.md d, the Machinedrum Editor's DESIGN-generators.md §5) =====
   The GEN bar, always under the piano roll on Sequence (it holds the PAGE control too), and the MUTATE bar, always
   over the Sound pages. Both are live: a change writes at once. A run of changes in one context (the workspace,
   the selected track, the pattern or the kit) is one gesture, one undo step: the commits after each click wait
   (genHeld, like rotate's rotHold) until the run ends, on another context, any other edit, undo or redo. Each change
   generates from the run's base, the pattern before it, so a value moved back gives its steps back. The results
   come from the pure functions (skins/shared/deskGen.js, 52-gen.js); here they become the view's own state, and
   GEN's are the steps intent of the tracks it wrote (their steps, slides and locks over the range, so a value moved
   back gives its steps back); MUTATE's are the params intent of the values it moved. Alt is the global "all": Alt + a GEN change
   every track of the side shown over the whole pattern, Alt+R every track (Sound: every synth track). ===== */
S.gen={specs:null,last:[],m:[],run:null};
S.mut={amount:20,seed:genSeed(),scope:new Set(["SYN"]),trial:null,note:""};
S.genOwn=false;
const genKey=()=>`${S.ws}:${S.sel}:${S.pat}`,mutKey=()=>`${S.ws}:${S.sel}:${S.kit}`;
function genInfo(t){const tr=trk(t);return{m:tr.m,notes:tr.steps.slice(0,S.len).flatMap(x=>x&&!x.off&&x.n||[]),key:tr.tr?.KEY|0,scale:tr.tr?.SCALE|0}}
/* every track's spec: from its role the first time, again when its machine changed and the spec was never edited */
function genSpecs(fill){const len=S.len||64;
 if(!S.gen.specs||fill){S.gen.specs=[];S.gen.last=[];S.gen.m=[]}
 for(let t=0;t<12;t++){const m=trk(t).m;if(!S.gen.specs[t]||(S.gen.m[t]!==m&&!S.gen.last[t]?.edited)){S.gen.specs[t]=mmGenDefault(genInfo(t),len,isMidiT(t));S.gen.m[t]=m;S.gen.last[t]={}}
  S.gen.specs[t]=genFit(S.gen.specs[t],len)}	/* STEPS never longer than the pattern, also when it got shorter */
 return S.gen.specs}
function genSpec(t=S.sel){return genSpecs()[t]}
const genEdited=(t=S.sel)=>{S.gen.last[t].edited=true};
/* the range a generator writes: the steps shown, Alt the whole pattern */
function genRange(all){const[a,b]=vis();return all?[0,S.len]:[a,Math.min(b,S.len)]}
/* a run or a trial with changes in the context shown holds the commits: one undo step */
function genHeld(){return !!(S.gen&&((S.gen.run?.applied&&S.gen.run.key===genKey())||(S.mut.trial?.applied&&S.mut.trial.key===mutKey())))}
/* the run or the trial ends: its gesture closes (the next edit is a new undo step) */
function genEnd(){if(!S.gen)return;const r=S.gen.run,m=S.mut.trial,live=(r&&r.applied)||(m&&m.applied);S.gen.run=null;S.mut.trial=null;S.mut.note="";if(!live)return;
 S.genEnding=true;try{if(HOST.commit)HOST.commit()}finally{S.genEnding=false}}
function genStale(){if(S.gen&&((S.gen.run&&S.gen.run.key!==genKey())||(S.mut.trial&&S.mut.trial.key!==mutKey())))genEnd()}
/* a run's base: every track's steps, slides and locks before the run */
function genBase(){return[...Array(12).keys()].map(t=>{const tr=trk(t);return{steps:tr.steps.map(x=>x&&JSON.parse(JSON.stringify(x))),slide:new Set(tr.slide),
 locks:[...S.locks].filter(([k])=>+k.split("|")[0]===t).map(([k,m])=>[k.split("|")[1],new Map(m)])}})}
function genStart(key){if(S.gen.run&&S.gen.run.key===key)return;genEnd();commit()}
/* one track over [from, to): back to the run's base there, then the generated steps; the trigs it turns off lose
   their locks and slides. False: the track is set to keep (and nothing of a run to give back). */
function genWrite(t,from,to,run){const b=run.base[t],tr=trk(t),r=mmGenSteps(genSpec(t),t,from,to,b.steps,S.len,isMidiT(t));
 if(!r&&!run.applied)return false;
 for(let s=from;s<to;s++){tr.steps[s]=b.steps[s]&&JSON.parse(JSON.stringify(b.steps[s]));b.slide.has(s)?tr.slide.add(s):tr.slide.delete(s);clearStepLocks(t,s);for(const[pid,m] of b.locks)if(m.has(s))setLock(t,pid,s,m.get(s),true)}
 if(!r)return true;
 for(const[s,st] of r.steps)tr.steps[s]=st;
 for(const s of r.gone){clearStepLocks(t,s);tr.slide.delete(s)}
 return true}
/* Live: every change of a GEN control writes at once (all: every track of the side shown, the whole pattern) */
function genLive(all=S.alt){
 if(S.rec){toast("The generators wait while the machine records live.");return false}
 genStale();genStart(genKey());
 const run=genRunFor(S.gen.run,genKey(),genBase,()=>0),[from,to]=genRange(all),wrote=[];
 for(const t of all?side():[S.sel])if(genWrite(t,from,to,run))wrote.push(t);
 if(!wrote.length){toast(all?"Every track is set to keep: nothing to generate.":`${tLabel(S.sel)} is set to keep.`);genDraw();return false}
 S.gen.run=run;run.applied++;
 S.genOwn=true;try{edit("steps",{from,to,rows:wrote.map(t=>rangeRow(t,from,to,true))})}finally{S.genOwn=false}
 autoRange(S.sel);if(S.ws==="seq")rerenderSeq();else renderTop();
 return true}
/* the result of a track's spec for the summary: its trigs over the range from the run's base (or now) */
function genPreview(t,from,to){const run=S.gen.run&&S.gen.run.key===genKey()?S.gen.run:null,base=run?run.base[t].steps:trk(t).steps;
 return{run,base,r:mmGenSteps(genSpec(t),t,from,to,base,S.len,isMidiT(t))}}
/* Defaults (↺): every spec from its machine; writes this track (Alt every track) */
function genDefaults(){genSpecs(true);toast("Every track's spec from its machine.");genLive()}
/* a new variation: a new seed (random, the notes), random hits and rotation in the cycle (euclid); keep stays */
function genVary(t){const sp=genSpec(t),r=n=>Math.floor(Math.random()*n);if(sp.kind==="keep")return false;
 sp.seed=genSeed();if(sp.kind==="euclid"){sp.k=1+r(sp.n);sp.rot=r(sp.n)}S.gen.last[t].edited=true;return true}
function genAgain(all=false){const ts=all?side():[S.sel],varied=ts.filter(genVary).length;
 if(!varied){toast(all?"Every track is set to keep: nothing to randomise.":`${tLabel(S.sel)} is set to keep: nothing to randomise.`);return}
 if(genLive(all)&&S.ws!=="seq")toast(all?"GEN: a new variation of every track.":`GEN: a new variation of ${tLabel(S.sel)}.`)}
/* the one randomise action (R; Alt+R or Alt-click on the R key: all), by workspace */
function randomise(all){if(S.ws==="sound")mutAgain(all);else genAgain(all)}
function genKind(kind){const t=S.sel,sp=genSpec(t),last=S.gen.last[t];if(sp.kind===kind)return;last[sp.kind]=sp;last.edited=true;
 const notes=sp.notes&&JSON.parse(JSON.stringify(sp.notes)),seed=sp.seed||genSeed();
 S.gen.specs[t]=genFit(last[kind],S.len||64)||(kind==="euclid"?{kind,k:4,n:Math.min(16,S.len||64),rot:0,seed,notes}:kind==="random"?{kind,density:25,seed,mode:"replace",notes}:{kind,notes});
 genLive()}
const genCycle=(list,cur,d)=>list[((list.indexOf(cur)+(d>0?1:-1))%list.length+list.length)%list.length];
/* a value of a bar moved by d (wheel, arrows, click, drag), in the spec's own ranges */
function genVal(k,d){const lim=(v,a,b)=>Math.max(a,Math.min(b,v));
 if(k==="amt"){S.mut.amount=lim(S.mut.amount+d,0,100);mutLive();renderMutStrip();return}
 const sp=genSpec(),n=sp.notes;genEdited();
 if(k==="k")sp.k=lim(sp.k+d,0,sp.n);
 if(k==="n"){sp.n=lim(sp.n+d,1,Math.min(64,S.len||64));sp.k=Math.min(sp.k,sp.n);sp.rot=Math.min(sp.rot,sp.n-1)}
 if(k==="rot")sp.rot=((sp.rot+d)%sp.n+sp.n)%sp.n;
 if(k==="dens")sp.density=lim(sp.density+d,0,100);
 if(k==="seed")sp.seed=((sp.seed-1+d)%99999+99999)%99999+1;
 if(n){if(k==="nmot")n.motion=genCycle(n.motion==="kit"||trk(S.sel).m==="DPRO-BBOX"?["off","kit"]:["off","step","leap"],n.motion,d);
  if(n.motion!=="kit"&&n.motion!=="off"&&n.root==null){const i=genInfo(S.sel),x=mmGenDefault({...i,m:"SWAVE-SAW"},S.len);Object.assign(n,x.notes)}
  if(k==="nroot")n.root=lim(n.root+d,0,127-12*n.range);
  if(k==="nscale")n.scale=genCycle(MM_SCALES,n.scale,d);
  if(k==="nrange"){n.range=lim(n.range+(d>0?1:-1),1,2);n.root=Math.min(n.root,127-12*n.range)}}
 genLive()}
/* The bars' pieces (the Machinedrum Editor's): a group (a title on a thin rule over its controls), a value (an LCD
   window, its label inside), a key (a small square cap with its keyboard key on it and a tiny label over it). */
const gbg=(label,body,cls="",tip="")=>`<div class="gbg ${cls}"${tip?` title="${tip}"`:""}><span class="gbl">${label}</span><div class="gbc">${body}</div></div>`;
const gv=(k,label,v,tip)=>`<span class="gv" data-gv="${k}" role="spinbutton" tabindex="0" aria-label="${label}" aria-valuenow="${parseInt(v)||0}" title="${tip}. Drag up or down, scroll, or click (⇧-click: down)."><small>${label}</small><b>${v}</b></span>`;
const gkc=(attr,act,cap,label,tip)=>`<button class="kc" ${attr}="${act}" title="${tip}" aria-label="${label}"><small>${label}</small><kbd>${cap}</kbd></button>`;
const randKey=tip=>`<button class="kc cream krand" data-rand="1" title="${tip}" aria-label="Randomise"><small>Random <em>⌥R all</em></small><kbd>R</kbd></button>`;
const gtitle=(name,target,all,tip)=>`<div class="gbt" title="${tip}"><b>${name}</b><span class="${all?"all":""}">${target}</span></div>`;
const gseg=(attr,cur,items,tips)=>`<span class="seg">${items.map(([k,n])=>`<button ${attr}="${k}" aria-pressed="${cur===k}" title="${tips[k]}">${n}</button>`).join("")}</span>`;
function genNotesHtml(sp,t){const n=sp.notes;if(!n||sp.kind==="keep"||isMidiT(t))return"";const kit=n.motion==="kit"||trk(t).m==="DPRO-BBOX";
 const mot=gv("nmot","Notes",n.motion==="off"?"off":n.motion,kit?"Off: the steps keep their drums (a new one takes the last). KIT: each hit picks BD1, SD1, CH or OH":"Off: the steps keep their pitch (a new one takes the track's last note). STEP: a walk of 1 or 2 scale degrees from the root. LEAP: any note of the range");
 const seed=sp.kind==="euclid"&&n.motion!=="off"?gv("seed","Seed",sp.seed,"The same seed gives the same notes"):"";
 if(n.motion==="off"||n.motion==="kit")return gbg("Notes",mot+seed,"gnotes","The rhythm decides when, the notes what: they go to the hits the rhythm writes");
 return gbg("Notes",mot+gv("nroot","Root",noteName(n.root),"The lowest note: the scale's root at its octave (the track's KEY when its SCALE is MAJ or MIN)")+gv("nscale","Scale",n.scale==="DORIAN"?"DOR":n.scale,"MAJ, MIN, PENT (minor pentatonic) or DORIAN")
  +gv("nrange","Range",n.range+" oct","One or two octaves from the root")+seed,"gnotes","The rhythm decides when, the notes what: they go to the hits the rhythm writes (every hit; in ADD the new ones, in THIN none)")}
function genStripHtml(){const t=S.sel,tr=trk(t),sp=genSpec(t),all=S.alt,[from,to]=genRange(all),{run,base,r}=genPreview(t,from,to);
 const mode=gbg("Mode",gseg("data-genkind",sp.kind,[["euclid","Euclid"],["random","Random"],["keep","Keep"]],{euclid:"k hits spread evenly over n steps, rotated",random:"each step on by chance, from a seed",keep:"leave this track as it is (in a run: as it was before the run)"}),"gmode");
 const params=sp.kind==="euclid"?gbg("Euclid",gv("k","Hits",sp.k,"How many hits in a cycle")+gv("n","Steps",sp.n,"The cycle's length in steps, at most the pattern's; it repeats over the pattern")+gv("rot","Rotate",sp.rot,"Moves the hits later"))
  :sp.kind==="random"?gbg("Random",gv("dens","Density",sp.density+"%","The chance of each step")+gv("seed","Seed",sp.seed,"The same seed gives the same steps"))
   +gbg("Write",gseg("data-genmode",sp.mode,[["replace","Replace"],["add","Add"],["thin","Thin"]],{replace:"the track becomes the result",add:"only steps that are off may turn on",thin:"only steps that are on may turn off"}),"gwrite")
  :gbg("Keep",`<span class="gsum">${tLabel(t)} is left as it is.</span>`);
 let sum="";
 if(r){const on=r.steps.filter(([,st])=>mmTrig(st)).length,b=[];for(let s=from;s<to;s++)if(mmTrig(base[s]))b.push(s);const now=new Set(r.steps.filter(([,st])=>mmTrig(st)).map(([s])=>s));
  const add=[...now].filter(s=>!mmTrig(base[s])).length,gone=b.filter(s=>!now.has(s)).length;
  sum=all?`every track · steps ${from+1}–${to}`:`${sp.kind==="euclid"?genSummary(sp,S.len)+" · ":""}${on} on${run?` <em>+${add} −${gone}</em>`:""} · steps ${from+1}–${to}`}
 else if(all)sum=`every track · steps ${from+1}–${to}`;
 const target=all?`all ${isMidiT(t)?"MIDI":"synth"} tracks`:`${tLabel(t)} · ${isMidiT(t)?"CH"+String(tr.ch).padStart(2,"0"):tr.m.replace("SWAVE-","SW-")}`;
 return`${gtitle("Gen",target,all,"Generators: every change writes to the pattern at once. A run of changes on this track is one undo step; Undo takes it back in one step. Alt: every track of the side shown, the whole pattern.")}
  ${mode}${params}${genNotesHtml(sp,t)}
  <div class="gsum" title="${run&&run.applied?"What the run changed, against the pattern before it":"What a change writes"}">${sum}</div>
  <div class="gkeys">${randKey(all?"Randomise every track of the side: a new variation of each spec, the whole pattern (Alt+R)":`Randomise ${tLabel(t)}: a new variation, ${sp.kind==="random"?"a new seed":sp.kind==="euclid"?"random hits and rotation in the cycle, a new seed for the notes":"nothing while it is set to Keep"} (R). Alt+R or Alt-click: every track`)}${gkc("data-gen","fill","↺","Defaults","Defaults: every track's spec from its machine: a bass 5/16 with a walk in its scale, a lead random 30 % over two octaves, a pad on 1 and 3, the drum box 8/16 on BD SD CH OH; FX machines kept; MIDI tracks 4/16, rhythm only. Writes this track (Alt: every track)")}</div>`}
/* the step gestures behind a small ? key (a click: the list of keys) */
function stepLegend(){const midi=isMidiT(S.sel),row=(cls,what,how)=>`<span>${cls!=null?`<i class="lg on ${cls}"></i>`:`<i class="lg none"></i>`}<b>${what}</b>${how}</span>`;
 return`<span class="steplegend"><button class="glegkey" id="steplegend" aria-label="Step gestures" aria-describedby="steplegpop">?</button><span class="legend glegpop" id="steplegpop" role="tooltip">${row("","Note","click in the roll; drag up or down for pitch, sideways to paint (alt: erase)")}${midi?"":row("g","Trigless","no envelope trigs (the ENV row)")}${row("y","Note off","alt-click an empty step")}${row("lk","Has locks","a lock on the step")}${row(null,"Fill","⌘-click: every 2nd step from there to the end gets the note (from a note: off); ⌘⇧-click: every 4th")}<small>? the list of keys</small></span></span>`}
function genBarHtml(){return`<div class="genbar"><div class="genband" id="genband">${genStripHtml()}</div><span class="gdiv" aria-hidden="true"></span>${stepLegend()}${pageKeys()}</div>`}
/* a rail tag's tooltip: the spec and its notes */
function genTip(t){const sp=genSpec(t),n=isMidiT(t)?"":mmNotesTag(sp,noteName);return`${genTag(sp)}${n?" · "+n:""}: ${tLabel(t)}'s generator (the GEN bar)`}
/* the bar again (and the rail's spec tags), without a full render */
function genDraw(){if(S.ws!=="seq")return;const host=$("#genband");if(host)host.innerHTML=genStripHtml();$$(".th[data-sel] .gtag").forEach(g=>{g.textContent=genTag(genSpec(+g.closest(".th").dataset.sel))})}

/* ---- MUTATE ---- */
const MUT_SCOPES=[["SYN","Syn","the machine's SYNTHESIS page (an FX machine's INP stays)"],["AMP","Amp","the AMP page (VOL stays)"],["FLT","Flt","the FILTER page"],["EFX","Efx","the EFFECTS page"],["LFO","Lfo","the three LFOs (their PAGE and DEST stay: where an LFO goes is routing)"]];
/* the groups in the scope (a group's title on the Sound page, "SYN:unison", "LF2:lfo"): their knobs on each
   track, by that track's machine (a machine without the group gives none) */
function mutGroupKnobs(tracks,base){const keys=[...S.mut.scope].filter(x=>x.includes(":")),out={};if(!keys.length)return out;
 for(const t of tracks){const g=mmSoundGroups(base[t].m);if(!g)continue;const list=[];
  for(const id of keys){const[pg,key]=id.split(":");if(/^LF\d$/.test(pg)){[2,3,4,5,6,7].forEach(i=>list.push([pg,i]));continue}const x=(g[pg]||[]).find(y=>y.key===key);if(x)x.idx.forEach(i=>list.push([pg,i]))}
  out[t]=list}
 return out}
function mutInfo(t,pg,i){const name=pnames(t,pg)?.[i];return name?{name,max:maxOf(meta(t,pg,i))}:null}
/* one apply of the trial: always from its base (R gives a fresh random sound, it never walks away), one gesture */
function mutApply(all){
 if(!all&&isMidiT(S.sel)){toast("MUTATE moves the synth tracks' sounds: a MIDI track's page is not a sound (Alt+R: every synth track).");return}
 if(!S.mut.scope.size){toast("Pick what to move: SYN, AMP, FLT, EFX or LFO, or a group's title.");return}
 genStale();let tr=S.mut.trial;
 if(!tr||tr.key!==mutKey()){genStart(mutKey());S.mut.seed=genSeed();tr=S.mut.trial={key:mutKey(),base:S.tracks.map(x=>({m:x.m,v:JSON.parse(JSON.stringify(x.v))})),touched:new Set(),all:false,applied:0}}
 tr.all=all;const tracks=all?[0,1,2,3,4,5]:[S.sel];
 const spec={tracks,pages:[...S.mut.scope].filter(x=>!x.includes(":")),extra:mutGroupKnobs(tracks,tr.base),amount:S.mut.amount,seed:S.mut.seed,protect:["VOL","TUNE"]};
 const values=mmMutate(spec,tr.base,mutInfo),now=new Set(values.map(([t,pg,i])=>t+":"+pg+":"+i));
 /* what an earlier apply moved and this one does not goes back to the base */
 for(const k of tr.touched)if(!now.has(k)){const[t,pg,i]=k.split(":");values.push([+t,pg,+i,tr.base[+t].v[pg][+i]])}
 if(!values.length){toast("Nothing to move here: no knobs in that scope.");return}
 for(const[t,pg,i,v] of values){S.tracks[t].v[pg][i]=v;const k=t+":"+pg+":"+i;v!==tr.base[t].v[pg][i]?tr.touched.add(k):tr.touched.delete(k)}
 S.genOwn=true;try{edit("params",{values:values.map(([t,pg,i,v])=>[t,PAGES.indexOf(pg),i,v])})}finally{S.genOwn=false}
 tr.applied++;
 const moved=new Set(values.map(([t])=>t)).size;S.mut.note=`seed ${S.mut.seed} · ${moved} track${moved===1?"":"s"}, ${values.length} values`;
 if(S.ws==="sound")render();else{renderTop();toast(`MUTATE: ${all?"every synth track":tLabel(S.sel)}, ${S.mut.note}.`)}}
/* amount or scope moved during a trial: heard at once, the same seed */
function mutLive(){if(S.mut.trial&&S.mut.trial.key===mutKey()&&S.mut.trial.applied)mutApply(S.mut.trial.all)}
/* R on Sound: a fresh random sound (a new seed) from the trial's base; all: every synth track */
function mutAgain(all=false){S.mut.seed=genSeed();mutApply(all)}
function mutStripHtml(){const t=S.sel,all=S.alt,tr=S.mut.trial&&S.mut.trial.key===mutKey()?S.mut.trial:null,midi=isMidiT(t);
 const chips=MUT_SCOPES.map(([g,n,tip])=>`<button data-mutg="${g}" aria-pressed="${S.mut.scope.has(g)}" title="Every knob of ${tip}">${n}</button>`).join("");
 return`${gtitle("Mutate",all?"every synth track":midi?`${tLabel(t)} · MIDI page`:`${tLabel(t)} · ${trk(t).m.replace("SWAVE-","SW-")}`,all,"Mutate: each knob in scope is pulled toward a random target by the amount, from a seed. A trial is one undo step; Undo takes it back in one step. VOL and TUNE stay. Alt: every synth track.")}
  ${gbg("Move",gv("amt","Amount",S.mut.amount+"%","How far each knob moves toward its random target"))}
  ${gbg("Scope",`<span class="seg">${chips}</span>${(n=>`<span class="ghint">${n?`+ ${n} group${n===1?"":"s"}`:"+ a group's title"}</span>`)([...S.mut.scope].filter(x=>x.includes(":")).length)}`,"gscope","The DATA pages Mutate moves; click a group's title below to add that group")}
  <div class="gsum" title="${S.mut.note}">${S.mut.note||(midi?"A MIDI track has no sound to move: Alt+R moves every synth track.":"")}</div>
  <div class="gkeys">${randKey(`${all?"Randomise every synth track":`Randomise ${tLabel(t)}`}: a fresh random sound, from the sound before the trial${tr?"":" (this sound)"}. One trial is one undo step (R; Alt+R or Alt-click: every synth track)`)}</div>`}
function mutBarHtml(){return`<div class="genband mutband" id="mutband">${mutStripHtml()}</div>`}
function renderMutStrip(){const host=$("#mutband");if(host)host.innerHTML=mutStripHtml();$$("[data-mutsg]").forEach(b=>b.setAttribute("aria-pressed",S.mut.scope.has(b.dataset.mutsg)))}

/* ---- the bars' clicks, values (drag, wheel, arrows) and keys ---- */
document.addEventListener("click",e=>{
 const g=e.target.closest("[data-gen]");if(g&&!g.disabled){if(g.dataset.gen==="fill")genDefaults();return}
 const rk=e.target.closest("[data-rand]");if(rk&&!rk.disabled){randomise(e.altKey||e.metaKey||e.ctrlKey);return}
 const k=e.target.closest("[data-genkind]");if(k){genKind(k.dataset.genkind);return}
 const md=e.target.closest("[data-genmode]");if(md){const sp=genSpec();if(sp.mode!==md.dataset.genmode){sp.mode=md.dataset.genmode;genEdited();genLive()}return}
 const v=e.target.closest(".gv[data-gv]");if(v&&!v.dataset.dragged){genVal(v.dataset.gv,e.shiftKey?-1:1);return}
 const c=e.target.closest("[data-mutg],[data-mutsg]");if(c){const id=c.dataset.mutg||c.dataset.mutsg;S.mut.scope.has(id)?S.mut.scope.delete(id):S.mut.scope.add(id);renderMutStrip();mutLive();e.stopPropagation();return}
 if(e.target.closest("#steplegend")){toggleKeys(true);e.stopPropagation()}},true);
let gvDrag=null;
document.addEventListener("pointerdown",e=>{const v=e.target.closest(".gv[data-gv]");if(!v||e.button!==0)return;gvDrag={v,y:e.clientY,k:v.dataset.gv,acc:0};delete v.dataset.dragged;try{v.setPointerCapture(e.pointerId)}catch(_){}});
document.addEventListener("pointermove",e=>{if(!gvDrag)return;const d=Math.trunc((gvDrag.y-e.clientY)/6)-gvDrag.acc;if(!d)return;
 gvDrag.acc+=d;gvDrag.v.dataset.dragged="1";genVal(gvDrag.k,d*(gvDrag.k==="dens"||gvDrag.k==="amt"?2:1));
 const n=document.querySelector(`.gv[data-gv="${gvDrag.k}"]`);if(n){n.dataset.dragged="1";gvDrag.v=n}});
document.addEventListener("pointerup",()=>{if(!gvDrag)return;const k=gvDrag.k;gvDrag=null;setTimeout(()=>{const n=document.querySelector(`.gv[data-gv="${k}"]`);if(n)delete n.dataset.dragged},0)});
document.addEventListener("wheel",e=>{const v=e.target.closest?.(".gv[data-gv]");if(!v)return;e.preventDefault();genVal(v.dataset.gv,((e.deltaY||e.deltaX)<0?1:-1)*(e.shiftKey?10:1))},{passive:false});
document.addEventListener("keydown",e=>{const v=e.target.closest?.(".gv[data-gv]");if(!v)return;const d={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1}[e.key];if(d==null)return;
 e.preventDefault();e.stopPropagation();const k=v.dataset.gv;genVal(k,d*(e.shiftKey?10:1));document.querySelector(`.gv[data-gv="${k}"]`)?.focus()},true);
Keys.bind({id:"randomise-track",scope:"any",keys:["R"],code:"KeyR",group:"Selected track",when:kbOn,run:()=>randomise(false),
 does:"Randomise the selected track: on Sound a fresh random sound (MUTATE, from the sound before the trial); everywhere else a new GEN variation: a new seed, or random hits and rotation"});
Keys.bind({id:"randomise-all",scope:"any",keys:["R"],code:"KeyR",mod:"alt",group:"All",when:kbOn,run:()=>randomise(true),
 does:"Randomise every track: on Sound every synth track's sound, everywhere else every GEN spec of the side shown over the whole pattern"});
Keys.bind({id:"gen-value-all",scope:"seq",area:"GEN bar",keys:["GEN value"],mod:"alt",group:"All",does:"Change a GEN value: every track of the side shown, the whole pattern (one undo step per run)"});
Keys.bind({id:"rkey-all",scope:"any",area:"GEN bar",keys:["R key"],mod:"alt",group:"All",does:"Click: randomise every track (Sound: every synth track; VOL and TUNE stay)"});
