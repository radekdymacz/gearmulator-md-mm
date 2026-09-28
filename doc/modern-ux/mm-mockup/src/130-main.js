
/* ===== Undo / redo: plain snapshots of everything the user edits ===== */
const H={undo:[],redo:[],last:null};
const packT=t=>({...t,slide:[...t.slide],swing:[...t.swing]}),unpackT=t=>({...t,slide:new Set(t.slide),swing:new Set(t.swing)});
function snap(){return JSON.stringify({tracks:S.tracks.map(packT),midi:S.midi.map(packT),locks:[...S.locks].map(([k,m])=>[k,[...m]]),song:S.song,len:S.len,mult:S.mult,swingAmt:S.swingAmt,patTrn:S.patTrn,
 routing:S.routing,multi:S.multi,menv:S.menv,mmap:S.mmap,mode:S.mode,links:S.ctl.links,kitState:S.kitState,kits:S.kits,patData:S.patData,patInfo:S.patInfo,patKit:S.patKit,kit:S.kit,workName:S.workName},(k,v)=>v===Infinity?"∞":v)}
function restore(str){const o=JSON.parse(str,(k,v)=>v==="∞"?Infinity:v);S.tracks=o.tracks.map(unpackT);S.midi=o.midi.map(unpackT);S.locks=new Map(o.locks.map(([k,m])=>[k,new Map(m)]));
 Object.assign(S,{song:o.song,len:o.len,mult:o.mult,swingAmt:o.swingAmt,patTrn:o.patTrn,routing:o.routing,multi:o.multi,menv:o.menv,mmap:o.mmap,mode:o.mode});S.ctl.links=o.links||[];setKitState(o.kitState);if(o.kits){S.kits=o.kits;S.patData=o.patData;S.patInfo=o.patInfo;S.patKit=o.patKit;S.kit=o.kit;S.workName=o.workName;drawLib()}}
function commit(){if(laneDraw||rollDrag||drag||active||arpDrag)return;const cur=snap();if(H.last==null){H.last=cur;return}if(cur!==H.last){H.undo.push(H.last);if(H.undo.length>200)H.undo.shift();H.redo=[];H.last=cur;renderTop()}}
function undo(){if(!H.undo.length){toast("Nothing to undo.");return}H.redo.push(snap());const prev=H.undo.pop();restore(prev);H.last=prev;tx();render();toast("Undo")}
function redo(){if(!H.redo.length){toast("Nothing to redo.");return}H.undo.push(snap());const nx=H.redo.pop();restore(nx);H.last=nx;tx();render();toast("Redo")}
["pointerup","keyup","click","change"].forEach(ev=>document.addEventListener(ev,()=>setTimeout(commit,0)));

/* ===== COPY CLEAR PASTE, per workspace, as the manual scopes them ===== */
let CLIP=null;
function secAction(kind){const t=S.sel,tr=trk(t),e2melody=false;
 if(S.ws==="seq"){const[a,b]=vis(),where=`${tLabel(t)}, steps ${a+1}–${b}`;
  if(kind==="copy"){CLIP={type:"page",midi:isMidiT(t),steps:tr.steps.slice(a,b).map(x=>x&&JSON.parse(JSON.stringify(x))),slide:[...tr.slide].filter(x=>x>=a&&x<b).map(x=>x-a),
   locks:[...S.locks].filter(([k])=>+k.split("|")[0]===t).map(([k,m])=>[k.split("|")[1],[...m].filter(([s])=>s>=a&&s<b).map(([s,v])=>[s-a,v])])};toast("COPY PAGE: "+where+".");return}
  if(kind==="clear"){for(let i=a;i<b;i++){tr.steps[i]=null;tr.slide.delete(i);clearStepLocks(t,i)}structEdited();render();toast("CLEAR PAGE: "+where+".");return}
  if(kind==="paste"){if(CLIP?.type!=="page"){toast("Copy a track page first.");return}if(CLIP.midi!==isMidiT(t)){toast("Track pages paste between synth tracks or between MIDI tracks.");return}
   for(let i=a;i<b;i++){tr.steps[i]=CLIP.steps[i-a]&&JSON.parse(JSON.stringify(CLIP.steps[i-a]));tr.slide.delete(i);clearStepLocks(t,i)}CLIP.slide.forEach(x=>tr.slide.add(x+a));
   let skip=0;CLIP.locks.forEach(([pid,st])=>{if(!pname(t,pid)){skip++;return}st.forEach(([s,v])=>setLock(t,pid,s+a,v,true))});structEdited();render();toast("PASTE PAGE into "+where+"."+(skip?" "+skip+" lock(s) skipped: this machine has no such parameter.":""));return}}
 if(S.ws==="seq"&&e2melody){if(kind==="copy"){CLIP={type:"melody",steps:tr.steps.map(x=>x&&JSON.parse(JSON.stringify(x)))};toast("MELODY copy: all pitches of "+tLabel(t)+", no locks, no machine.");return}
  if(kind==="clear"){tr.steps=tr.steps.map(()=>null);[...S.locks.keys()].forEach(k=>{if(+k.split("|")[0]===t)S.locks.delete(k)});structEdited();render();toast("Cleared the notes of "+tLabel(t)+".");return}
  if(kind==="paste"){if(CLIP?.type!=="melody"){toast("Copy a melody first.");return}tr.steps=CLIP.steps.map(x=>x&&JSON.parse(JSON.stringify(x)));structEdited();render();toast("Pasted the melody into "+tLabel(t)+". Its locks and machine stay.");return}}
 if(S.ws==="sound"&&!isMidiT(t)){if(kind==="copy"){CLIP={type:"machine",m:tr.m,v:JSON.parse(JSON.stringify(tr.v))};toast("COPY MACHINE: "+tr.m+" and all seven DATA pages.");return}
  if(kind==="clear"){tr.m="GND-SIN";tr.v={SYN:synDefaults("GND-SIN"),AMP:[...DEFV.AMP],FLT:[...DEFV.FLT],EFX:[...DEFV.EFX],LF1:[...DEFV.LFO],LF2:[...DEFV.LFO],LF3:[...DEFV.LFO]};soundEdited();render();toast("CLEAR MACHINE: "+tLabel(t)+" is GND-SIN again.");return}
  if(kind==="paste"){if(CLIP?.type!=="machine"){toast("Copy a machine first.");return}tr.m=CLIP.m;tr.v=JSON.parse(JSON.stringify(CLIP.v));soundEdited();render();toast("PASTE MACHINE: "+CLIP.m+" onto "+tLabel(t)+".");return}}
 if(S.ws==="perform"){const A=S.tracks[asgT()];if(kind==="copy"){CLIP={type:"assign",a:JSON.parse(JSON.stringify(A.assign))};toast("Copied the assign tabs of T"+(asgT()+1)+".");return}
  if(kind==="clear"){A.assign=newAssign();soundEdited();render();return}if(kind==="paste"){if(CLIP?.type!=="assign"){toast("Copy an assign first.");return}A.assign=JSON.parse(JSON.stringify(CLIP.a));soundEdited();render();toast("Pasted the assign tabs.");return}}
 if(S.ws==="song"){const i=S.songSel,r=S.song[i];if(kind==="copy"){if(r.type==="end"){toast("END cannot be copied.");return}CLIP={type:"row",row:JSON.parse(JSON.stringify(r))};toast("Copied row "+String(i+1).padStart(3,"0")+".");return}
  if(kind==="clear"){songAction("del");return}if(kind==="paste"){if(CLIP?.type!=="row"){toast("Copy a song row first.");return}if(S.song.length>=200){toast("A song holds 200 rows.");return}const at=r.type==="end"?i:i+1;S.song.splice(at,0,JSON.parse(JSON.stringify(CLIP.row)));S.songSel=at;structEdited();render();return}}
 toast("Copy, clear and paste work in Sequence (page), Notes (melody), Sound (machine), Perform (assign) and Song (row).")}

/* ===== First run: firmware needed ===== */
function firstRun(){ask(`<div class="lcdbig">MONOMACHINE FIRMWARE NEEDED</div>
 <p>Monomachine Editor runs the real Monomachine operating system. Elektron's firmware cannot ship with the app, so you add the one from your own machine.</p>
 <ol class="recvsteps"><li>Dump the <b>OS 1.32B</b> flash image from your Monomachine (8 MiB, <span class="mono">.bin</span>).</li><li>Drop it here. The editor checks its size and fingerprint.</li><li>It stays on this computer only.</li></ol>
 <label class="drop" id="drop" tabindex="0"><input type="file" id="romfile" accept=".bin" hidden><span id="droptxt">Drop the .bin here, or click to choose it</span></label>
 <p class="hint">SFX-6, SFX-60 MKI and MKII use the same OS. The MKII adds the user waveforms and the DigiPRO draw machines.</p>`,[["Close preview","cream",()=>{}]],"first")}
function checkRom(f){const t=$("#droptxt");if(!f)return;const ok=f.size===8388608;t.textContent=ok?`✓ ${f.name}: 8 MiB. In the real app: check the OS 1.32B fingerprint, then start.`:`✗ ${f.name}: ${(f.size/1048576).toFixed(2)} MiB. The OS 1.32B image is exactly 8 MiB.`;$("#drop").classList.toggle("ok",ok);$("#drop").classList.toggle("bad",!ok)}

/* ===== LCD line 2 ===== */
function l2step(k,d,fine){
 if(k==="len"){S.len=fine?clamp(S.len+d,2,64):clamp((Math.ceil(S.len/16)+d)*16,16,64);structEdited()}
 if(k==="mult"){const o=["1X","2X","3/4X","3/2X"];S.mult=o[(o.indexOf(S.mult)+d+4)%4];structEdited()}
 if(k==="swing"){S.swingAmt=clamp(S.swingAmt+d,50,80);structEdited()}
  if(k==="ptrn"){S.patTrn=clamp(S.patTrn+d,0,127);structEdited()}
 if(k==="route"){S.routing=ROUTES[(ROUTES.indexOf(S.routing)+d+3)%3];soundEdited()}
 if(k==="side"){setSide(S.side==="midi"?"int":"midi");return}
 if(k==="pmode"){const o=PMODES.map(p=>p[0]);S.mode=o[(o.indexOf(S.mode)+d+4)%4]}
 render()}
let l2drag=null;
document.addEventListener("pointerdown",e=>{const el=e.target.closest(".l2.ed");if(!el)return;const k=el.dataset.l2;if(k==="swing"||k==="ptrn"){l2drag={k,y:e.clientY,v:k==="swing"?S.swingAmt:S.patTrn,moved:false};el.setPointerCapture(e.pointerId);e.preventDefault()}});
document.addEventListener("pointermove",e=>{if(!l2drag)return;const d=Math.round((l2drag.y-e.clientY)/4);if(d)l2drag.moved=true;if(l2drag.k==="swing")S.swingAmt=clamp(l2drag.v+d,50,80);else S.patTrn=clamp(l2drag.v+d,0,127);renderSub()});
document.addEventListener("pointerup",()=>{if(!l2drag)return;const k=l2drag;l2drag=null;if(!k.moved)l2step(k.k,1);else{structEdited();render()}});
document.addEventListener("click",e=>{const el=e.target.closest(".l2.ed");if(!el)return;const k=el.dataset.l2;if(k!=="swing"&&k!=="ptrn")l2step(k,e.shiftKey?-1:1)});
document.addEventListener("wheel",e=>{const el=e.target.closest(".l2.ed");if(!el)return;e.preventDefault();l2step(el.dataset.l2,(e.deltaY||e.deltaX)<0?1:-1,true)},{passive:false});

/* ===== Pointer input in the workspace ===== */
let drag=null,joyDrag=null,splitDrag=null,arpDrag=null;
const main=$("#main");
main.addEventListener("pointerdown",e=>{
 if(S.learn){const el=e.target.closest(".pc[data-g]");if(el&&(PAGES.includes(el.dataset.g)||el.dataset.g==="MID")){e.preventDefault();e.stopPropagation();const t=el.dataset.t!=null?+el.dataset.t:S.sel;S.learnT={t,pid:el.dataset.g+"."+el.dataset.n};toast(`Target: ${tLabel(t)} ${pidLabel(t,S.learnT.pid)}. Now press 1-8 for a knob.`);return}}
 const h=e.target.closest(".lfohandle");if(h){cordStart(e,h);return}
 const roll=e.target.closest("canvas.roll");if(roll){roll.setPointerCapture(e.pointerId);rollDown(roll,e);e.preventDefault();return}
 const c=e.target.closest("canvas.ed");if(c){const hh=nearest(c,e);if(!hh)return;active={c,k:hh.k};c.setPointerCapture(e.pointerId);e.preventDefault();redraw();return}
 const el=e.target.closest(".pc[data-g],.fader[data-g]");if(el){drag={el,x:e.clientX,y:e.clientY,v:getV(el),vert:el.classList.contains("fader"),mx:maxOf(ref(el)[2])};el.setPointerCapture(e.pointerId);el.classList.add("act");e.preventDefault();return}
 const lb=e.target.closest(".lb");if(lb){laneDraw={erase:e.altKey,touched:false};$("#lane").setPointerCapture(e.pointerId);laneAt(e);e.preventDefault();return}
 const ac=e.target.closest(".ac");if(ac){const k=+ac.dataset.ac,a=trk(S.sel).arp;if(k>=a.len){a.len=k+1;structEdited();renderArp();return}arpDrag={k,y:e.clientY,moved:false};$("#arptrack").setPointerCapture(e.pointerId);e.preventDefault();return}
 if(e.target.closest("#joy")){joyDrag=true;$("#joy").setPointerCapture(e.pointerId);joyAt(e);e.preventDefault();return}
 if(e.target.closest("#splitm")){splitDrag=true;$("#splitm").setPointerCapture(e.pointerId);e.preventDefault();return}
 const key=e.target.closest(".kb [data-key]");if(key){kbDown=true;$("#kb").setPointerCapture(e.pointerId);playKey(+key.dataset.key);e.preventDefault()}});
main.addEventListener("pointermove",e=>{
 if(cord){cordMove(e);return}
 const roll=e.target.closest?.("canvas.roll")||(rollDrag&&$("#roll"));if(roll&&(rollDrag||e.target===roll)){rollMove(roll,e);if(rollDrag)return}
 if(active){const r=active.c.getBoundingClientRect(),hh=ED[active.c.dataset.ed].handles(r.width,r.height,active.c).find(h=>h.k===active.k);if(hh){hh.drag(clamp(e.clientX-r.left,0,r.width),clamp(e.clientY-r.top,0,r.height));soundEdited();syncControls();redraw()}return}
 if(drag){const fine=e.shiftKey?.25:1,scale=drag.mx<16?drag.mx/127*1.6:1;const d=drag.vert?(drag.y-e.clientY)*127/150:((e.clientX-drag.x)+(drag.y-e.clientY))/2;setV(drag.el,drag.v+d*fine*scale);return}
 if(laneDraw){laneAt(e);return}
 if(arpDrag){const a=trk(S.sel).arp,dd=Math.round((arpDrag.y-e.clientY)/4);if(Math.abs(dd)>0||arpDrag.moved){arpDrag.moved=true;a.ofs[arpDrag.k]=clamp((arpDrag.v0??(arpDrag.v0=a.ofs[arpDrag.k]))+dd,-24,24);renderArp()}return}
 if(joyDrag){joyAt(e);return}
 if(splitDrag){const kb=$("#kb").getBoundingClientRect(),[lo,hi]=kbRange(),whites=[];for(let n=lo;n<=hi;n++)if(!isBlack(n))whites.push(n);const i=clamp(Math.round((e.clientX-kb.left)/kb.width*whites.length),1,whites.length-1);if(S.multi.splitKey!==whites[i]){S.multi.splitKey=whites[i];$("#kb").innerHTML=renderKb();$("#splitm").setPointerCapture?.(e.pointerId)}return}
 if(kbDown){const k=document.elementFromPoint(e.clientX,e.clientY)?.closest(".kb [data-key]");if(k&&!k.classList.contains("dn"))playKey(+k.dataset.key);return}
 const c=e.target.closest?.("canvas.ed");if(c)c.style.cursor=nearest(c,e)?"grab":"default"});
function endDrag(e){if(cord){cordEnd(e);return}if(rollDrag)rollUp();if(active){active=null;redraw()}if(drag){drag.el.classList.remove("act");drag=null}endLaneDraw();
 if(arpDrag){const a=trk(S.sel).arp;if(!arpDrag.moved){a.rhy[arpDrag.k]=!a.rhy[arpDrag.k];renderArp()}structEdited();arpDrag=null}
 if(joyDrag){joyDrag=false;S.joy={x:0,y:0};const k=$("#knobj");if(k){k.style.left="50%";k.style.top="50%"}}
 if(splitDrag){splitDrag=false;render()}if(kbDown){kbDown=false;$$(".kb .dn").forEach(k=>k.classList.remove("dn"))}}
main.addEventListener("pointerup",endDrag);main.addEventListener("pointercancel",endDrag);
function joyAt(e){const r=$("#joy").getBoundingClientRect();S.joy={x:clamp((e.clientX-r.left)/r.width*2-1,-1,1),y:clamp(1-(e.clientY-r.top)/r.height*2,-1,1)};const k=$("#knobj");k.style.left=(50+S.joy.x*42)+"%";k.style.top=(50-S.joy.y*42)+"%";
 const tr=S.tracks[asgT()],A=tr.assign,rows=A.tabs[S.asTab];const amt=S.asTab==="JOY U"?Math.max(0,S.joy.y):S.asTab==="JOY D"?Math.max(0,-S.joy.y):A.mirr?S.joy.x:Math.max(0,S.joy.x);
 $("#kbinfo")&&($("#kbinfo").textContent=rows.map(r=>`${LPAGES[r.pg]} ${destNames(asgT(),r.pg)[r.d]} ${Math.round((r.add-64)*amt)>=0?"+":""}${Math.round((r.add-64)*amt)}`).join(" · "));tx()}
function renderArp(){const el=$("#arptrack");if(!el)return;const a=trk(S.sel).arp;el.innerHTML=arpCells(a);$$(".arplenrow button").forEach((b,k)=>b.classList.toggle("on",k<a.len));const c2=$("#arpcap");if(c2)c2.textContent="Rhythm + offset · "+a.len+" steps";$$(".mrowg .rowst").length&&0;redraw()}
main.addEventListener("wheel",e=>{
 const el=e.target.closest(".pc[data-g],.fader[data-g]");if(el){e.preventDefault();const d=(e.deltaY||e.deltaX)<0?1:-1;setV(el,getV(el)+d*(e.shiftKey?10:1));return}
 const st=e.target.closest(".mst");if(st){const t=+st.dataset.t,s=+st.dataset.s,x=trk(t).steps[s];if(!x?.n)return;e.preventDefault();const d=((e.deltaY||e.deltaX)<0?1:-1)*(e.shiftKey?12:1);x.n=x.n.map(n=>clamp(n+d));structEdited();refreshRow(t);return}
 const roll=e.target.closest("canvas.roll[data-big]");if(roll){e.preventDefault();const t=+roll.dataset.t;S.rollLoT[t]=clamp((S.rollLoT[t]??S.rollLo)+((e.deltaY)<0?1:-1)*(e.shiftKey?12:2),0,127-ROWS);redraw()}},{passive:false});
main.addEventListener("dblclick",e=>{const el=e.target.closest(".pc[data-g]");if(el){const m=ref(el)[2];setV(el,m.en?0:m.signed?64:64)}});
main.addEventListener("keydown",e=>{const el=e.target.closest("[data-g]");if(!el)return;const d={ArrowRight:1,ArrowUp:1,ArrowLeft:-1,ArrowDown:-1,PageUp:10,PageDown:-10}[e.key];if(d==null)return;e.preventDefault();setV(el,getV(el)+d*(e.shiftKey?10:1))});

/* ===== Clicks ===== */
function autoRange(t){const ns=trk(t).steps.flatMap(x=>x?.n||[]);if(!ns.length)return;const lo=Math.min(...ns),hi=Math.max(...ns),cur=S.rollLoT[t];if(cur==null||lo<cur||hi>cur+ROWS-1)S.rollLoT[t]=clamp(Math.round((lo+hi)/2)-Math.floor(ROWS/2),0,127-ROWS)}
function goWs(ws){S.ws=ws;const t=S.sel%6+((ws==="seq"||ws==="sound")&&S.side==="midi"?6:0);select(t)}
function setSide(sd){S.side=sd;select(S.sel%6+(sd==="midi"?6:0))}
function select(t){S.sel=t;autoRange(t);const pg=S.lane.split(".")[0];if(isMidiT(t)){if(pg!=="MID"){S.lane="MID.1";S.lanePage="MID"}}else if(pg==="MID"||!pname(t,S.lane)){S.lane="FLT.1";S.lanePage="FLT"}render()}
document.addEventListener("click",e=>{
 const mu=e.target.closest("[data-mute]"),so=e.target.closest("[data-solo]");
 if(mu||so){const i=+(mu||so).dataset[mu?"mute":"solo"],t=trk(i);if(mu)t.mute=!t.mute;else t.solo=!t.solo;tx();render();return}
 const gm=e.target.closest("[data-gmute]");if(gm){const t=trk(+gm.dataset.gmute);t.mute=!t.mute;tx();render();return}
 const ev=e.target.closest("[data-env]");if(ev){clickEnv(ev.dataset.env,+ev.dataset.s);return}
 const sd=e.target.closest("[data-side]");if(sd){setSide(sd.dataset.side);return}
 const dk=e.target.closest("[data-dock]");if(dk){S.dock=dk.dataset.dock;rerenderSeq();return}
 const tl=e.target.closest(".tc[data-tl]");if(tl){clickTl(tl.dataset.tl,+tl.dataset.s);return}
 const nd=e.target.closest("[data-node]");if(nd){S.sel=+nd.dataset.node;render();return}
 const sel=e.target.closest("[data-sel]");if(sel&&!e.target.closest("button,select,.pc,.fader")){select(+sel.dataset.sel);return}
 const lp=e.target.closest("[data-lpage]");if(lp){S.lanePage=lp.dataset.lpage;const n=pnames(S.sel,S.lanePage);S.lane=S.lanePage+"."+Math.max(0,trackLockPids(S.sel).filter(x=>x.startsWith(S.lanePage+".")).map(x=>+x.split(".")[1])[0]??0);if(!n.length){S.lane="FLT.1";S.lanePage="FLT"}render();return}
 const ch=e.target.closest("[data-lane]");if(ch){S.lane=ch.dataset.lane;render();return}
 if(e.target.closest("#clearLane")){S.locks.delete(lkKey(S.sel,S.lane));structEdited();render();return}
 const sg=e.target.closest(".seg[data-set] button");if(sg){const k=sg.parentElement.dataset.set,v=sg.dataset.v,tr=trk(S.sel);
  if(k==="arpmode"){tr.arp.MODE=+v;structEdited();render();return}if(k==="arpplay"){tr.arp.PLAY=+v;structEdited();render();return}
  if(k==="scale"){tr.tr.SCALE=+v;structEdited();render();return}if(k==="port"){tr.port=+v;soundEdited();render();return}
  if(k==="ltrig"){V(sg.parentElement.dataset.l)[2]=+v;soundEdited();render();return}
  if(k==="routing"){S.routing=v;soundEdited();render();return}
  if(k==="mtmode"){S.multi.mode=+v;render();return}if(k==="astab"){S.asTab=v;render();return}if(k==="astrk"){S.sel=+v;render();return}
  if(k==="loopkind"){const r=S.song[S.songSel];r.type=v;if(v==="halt")r.to=S.songSel;if(v==="jump"&&r.to<=S.songSel)r.to=Math.min(S.song.length-1,S.songSel+1);if(v==="loop"){if(!r.count)r.count=2;if(r.to>=S.songSel)r.to=Math.max(0,S.songSel-1)}structEdited();render();return}}
 const at=e.target.closest("[data-arptrig]");if(at){const a=trk(S.sel).arp,k=at.dataset.arptrig;a[k]=a[k]?0:1;structEdited();render();return}
 const lg=e.target.closest("[data-leg]");if(lg){const l=trk(S.sel).leg,k=lg.dataset.leg;l[k]=l[k]?0:1;soundEdited();render();return}
 const al=e.target.closest("[data-arplen]");if(al){trk(S.sel).arp.len=+al.dataset.arplen;structEdited();renderArp();return}
 const rl=e.target.closest("[data-roll]");if(rl){S.rollLo=clamp(S.rollLo+ +rl.dataset.roll,0,127-ROWS);render();return}
 if(e.target.closest("#ghost")){S.ghost=!S.ghost;render();return}
 const lw=e.target.closest("[data-lwave]");if(lw){V(lw.dataset.lwave)[3]=+lw.dataset.w;soundEdited();render();return}
 const gt=e.target.closest("[data-goto]");if(gt){S.ws="sound";S.side="int";select(+gt.dataset.goto);return}
 const bs=e.target.closest("[data-bus]");if(bs){const tr=S.tracks[+bs.dataset.t];tr.out[bs.dataset.bus]=!tr.out[bs.dataset.bus];soundEdited();render();return}
 const pmk=e.target.closest("[data-pmode]");if(pmk){S.mode=pmk.dataset.pmode;if(S.mode==="poly")toast("POLY: T"+(asgT()+1)+" now plays six voices. The other five tracks are off until you leave POLY.");render();return}
 const so2=e.target.closest("[data-strk]");if(so2){S.multi.splitTrack=clamp(S.multi.splitTrack+ +so2.dataset.strk,2,6);render();return}
 const tm=e.target.closest("[data-tim]");if(tm){S.multi.timing=clamp(S.multi.timing+ +tm.dataset.tim,0,6);render();return}
 const ko=e.target.closest("[data-kboct]");if(ko){S.kbOct=clamp(S.kbOct+ +ko.dataset.kboct,0,6);render();return}
 const bd=e.target.closest("[data-band]");if(bd){S.mmapSel=+bd.dataset.band;render();return}
 const mr=e.target.closest("tr[data-mrow]");if(mr&&!e.target.closest("button,.pc,.kselbtn")){S.mmapSel=+mr.dataset.mrow;render();return}
 const mh=e.target.closest("[data-mhi]");if(mh){const i=+mh.dataset.i,r=S.mmap[i],lo=i?S.mmap[i-1].hi+1:0,nx=S.mmap[i+1];r.hi=clamp(r.hi+ +mh.dataset.mhi,lo,nx?nx.hi-1:127);render();return}
 const md=e.target.closest("[data-mdel]");if(md){const i=+md.dataset.mdel;if(S.mmap.length<2)return;const r=S.mmap.splice(i,1)[0];if(i===S.mmap.length)S.mmap[i-1].hi=127;S.mmapSel=Math.max(0,i-1);render();return}
 if(e.target.closest("[data-madd]")){const i=S.mmapSel,r=S.mmap[i],lo=i?S.mmap[i-1].hi+1:0;if(r.hi-lo<1){toast("A one-key range cannot be split.");return}const mid=Math.floor((lo+r.hi)/2);S.mmap.splice(i,0,{...r,hi:mid});render();return}
 const mi=e.target.closest("[data-mirr]");if(mi){const A=S.tracks[asgT()].assign;A.mirr=!A.mirr;soundEdited();render();return}
 const kt=e.target.closest("[data-ktrk]");if(kt){const A=S.tracks[asgT()].assign;A[kt.dataset.ktrk]=!A[kt.dataset.ktrk];soundEdited();render();return}
 const pl=e.target.closest(".pl");if(pl&&S.ws==="seq"){const k=+pl.dataset.plp;if(k<pages16()){S.page=k;S.viewAll=false;render()}return}
 const pgk=e.target.closest("#pgkey");if(pgk&&!pgk.disabled){const n=pages16();S.viewAll=false;S.page=(S.page+(e.shiftKey?-1:1)+n)%n;render();return}
 if(e.target.closest("#pgall")){S.viewAll=!S.viewAll;render();return}
 if(e.target.closest("#pgfollow")){S.follow=!S.follow;render();return}
 if(S.ws==="song"){
  const bk=e.target.closest("[data-bank]");if(bk){S.bank=+bk.dataset.bank;render();return}
  const ap=e.target.closest("[data-addpat]");if(ap){if(S.song.length>=200){toast("A song holds 200 rows.");return}let at2=S.songSel+1;if(S.song[S.songSel]?.type==="end")at2=S.songSel;S.song.splice(at2,0,{pat:+ap.dataset.addpat,rep:1});S.songSel=at2;structEdited();render();return}
  const rw=e.target.closest(".scell:not(.empty),.db[data-row]");if(rw){S.songSel=+rw.dataset.row;render();return}
  const ra=e.target.closest("[data-rowact]");if(ra){songAction(ra.dataset.rowact);return}
  const stp=e.target.closest("[data-step]");if(stp){songStep(stp.dataset.step,+stp.dataset.d*(e.shiftKey?10:1));return}
  const tt=e.target.closest("[data-ttr]");if(tt){const r=S.song[S.songSel];r.ttr=r.ttr||T64();const k=+tt.dataset.ttr;r.ttr[k]=clamp(r.ttr[k]+ +tt.dataset.d,28,100);structEdited();render();return}
  const mk=e.target.closest("[data-rowmute]");if(mk){const r=S.song[S.songSel],k=+mk.dataset.rowmute;r.mutes=r.mutes||[];r.mutes=r.mutes.includes(k)?r.mutes.filter(x=>x!==k):[...r.mutes,k];structEdited();render();return}
  if(e.target.closest("[data-bpmkeep]")){const r=S.song[S.songSel];r.bpm=r.bpm?undefined:Math.round(S.bpm);structEdited();render();return}
  if(e.target.closest("[data-fullpat]")){const r=S.song[S.songSel];delete r.ofs;delete r.len;structEdited();render();return}
  if(e.target.closest("[data-inf]")){const r=S.song[S.songSel];r.count=r.count===Infinity?2:Infinity;structEdited();render();return}}
 const dl=e.target.closest("[data-dlg]");if(dl){const d=$("#dlg"),f=d._btns[+dl.dataset.dlg][2];d.hidden=true;f();return}
 if(e.target.id==="dlg"){$("#dlg").hidden=true;return}
 if(e.target.closest("#drop")&&!e.target.closest("input")){$("#romfile").click();return}
 if(e.target.closest("#undo")){undo();return}if(e.target.closest("#redo")){redo();return}
 const sc=e.target.closest("[data-sec]");if(sc){secAction(sc.dataset.sec);return}
 if(e.target.closest("#pst.warn")){sendDialog();return}
 if(e.target.closest("#kitf")){toggleLib("kit");return}
 if(e.target.closest("#pat")){toggleLib("pat");return}
 if(e.target.closest("#learnkey")){S.learn=!S.learn;S.learnT=null;document.body.classList.toggle("learn",S.learn);renderTop();if(S.learn)toast("LEARN: click a value, then press 1-8 for a controller knob.");return}
 const tb=e.target.closest("#tabs button");if(tb){goWs(tb.dataset.ws);return}
 if(e.target.closest("#platekey")){setPlate(S.plate==="mk1"?"mk2":"mk1");return}
 if(e.target.closest("#play")){togglePlay();return}
 if(e.target.closest("#rec")){S.rec=!S.rec;renderTop();if(S.rec)toast("GRID RECORD on. On the machine: hold a trig to see its note, turn a knob to lock it. Here: just click and draw.");return}
 if(e.target.closest("#patPrev")){goPattern((S.queued??S.pat)-1);return}
 if(e.target.closest("#patNext")){goPattern((S.queued??S.pat)+1);return}});
document.addEventListener("change",e=>{const id=e.target.id,v=e.target.value,tr=trk(S.sel);
 if(id==="romfile"){checkRom(e.target.files[0]);return}
 if(id==="engsel"){const btn=document.querySelector(".lcdeng");if(v==="rom"){e.target.value=S.engine;btn.querySelector("span").textContent=e.target.selectedOptions[0].text;firstRun();return}
  S.engine=v;S.pend=0;renderPst();startEngine(v);return}
 let m=id.match(/^lp(\d)$/);if(m){const l=V("LF"+m[1]);l[0]=+v;l[1]=0;soundEdited();render();return}
 m=id.match(/^ld(\d)$/);if(m){V("LF"+m[1])[1]=+v;soundEdited();render();return}
 m=id.match(/^inp(\d)$/);if(m){S.tracks[+m[1]].inp=v;soundEdited();render();return}
 m=id.match(/^asp(\d)$/);if(m){const r=S.tracks[asgT()].assign.tabs[S.asTab][+m[1]];r.pg=+v;r.d=0;soundEdited();render();return}
 m=id.match(/^asd(\d)$/);if(m){S.tracks[asgT()].assign.tabs[S.asTab][+m[1]].d=+v;soundEdited();return}
 m=id.match(/^mpat(\d+)$/);if(m){S.mmap[+m[1]].pat=+v;render();return}
 if(id==="trigpos"){tr.trigpos=v===""?null:+v;soundEdited();render();return}
 if(id==="mch"){tr.ch=+v;soundEdited();render();return}});

/* ===== Keys ===== */
document.addEventListener("keydown",e=>{const mod=e.metaKey||e.ctrlKey,inField=e.target.closest?.("input,select,textarea");
 if(!$("#dlg").hidden&&e.key==="Escape"){$("#dlg").hidden=true;return}
 if(mod&&!inField&&(e.key==="z"||e.key==="Z")){e.preventDefault();e.shiftKey?redo():undo();return}
 if(mod&&!inField&&e.key==="y"){e.preventDefault();redo();return}
 if(mod&&!inField&&(e.key==="c"||e.key==="v")){e.preventDefault();secAction(e.key==="c"?"copy":"paste");return}
 if(S.learn&&S.learnT&&/^[1-8]$/.test(e.key)){e.preventDefault();learnBind(+e.key);return}
 if(S.learn&&e.key==="Escape"){S.learn=false;document.body.classList.remove("learn");renderTop();return}
 if(e.target.closest?.("input,select,textarea,[role=slider]")||mod||e.altKey)return;
 if(e.key==="l"||e.key==="L"){$("#learnkey").click();return}
 if(e.key==="r"||e.key==="R"){$("#rec").click();return}
 const ws=["seq","sound","mix","perform","song","control"][+e.key-1];if(ws){goWs(ws);return}
 if((S.ws==="song"||S.ws==="seq")&&(e.key==="Delete"||e.key==="Backspace")){e.preventDefault();S.ws==="song"?songAction("del"):secAction("clear");return}
 if(S.ws==="song"&&(e.key==="ArrowLeft"||e.key==="ArrowRight")){S.songSel=clamp(S.songSel+(e.key==="ArrowRight"?1:-1),0,S.song.length-1);render();return}
 if(e.key===" "){e.preventDefault();togglePlay()}
 if((e.key==="["||e.key==="]")&&S.ws==="seq"&&pages16()>1){const n=pages16();S.viewAll=false;S.page=(S.page+(e.key==="]"?1:-1)+n)%n;render()}});

/* BPM: drag or arrows */
(()=>{const b=$("#bpm");let d=null;b.addEventListener("pointerdown",e=>{d={y:e.clientY,v:S.bpm};b.setPointerCapture(e.pointerId)});
 b.addEventListener("pointermove",e=>{if(!d)return;S.bpm=clamp(Math.round((d.v+(d.y-e.clientY)*(e.shiftKey?.1:.5))*10)/10,30,300);renderTop();if(S.playing)restartClock()});
 b.addEventListener("pointerup",()=>d=null);b.addEventListener("keydown",e=>{const k={ArrowUp:1,ArrowDown:-1}[e.key];if(!k)return;e.preventDefault();S.bpm=clamp(S.bpm+k*(e.shiftKey?.1:1),30,300);renderTop();if(S.playing)restartClock()})})();


/* ===== Engine status (from the MD Editor v48): the LCD says what the engine is doing; editing waits until it is ready ===== */
const ENG={norom:["NO ROM","off"],loading:["LOADING ROM","blink"],boot:["BOOTING OS","blink"],ready:["EMU OS 1.32B","on"],hwwait:["HW CONNECT","blink"],hwready:["HW MIDI","on"],hwnone:["HW NO MIDI","off"],error:["ROM ERROR","off"]};
let engT=[];
function engReady(){return S.eng==="ready"||S.eng==="hwready"}
function setEng(st){S.eng=st;const[txt,led]=ENG[st],b=document.querySelector(".lcdeng"),l=document.getElementById("engled");if(!b)return;
 b.querySelector("span").textContent=txt;l.className="led "+(led==="on"?"on":led==="blink"?"on blink":"");
 const ready=engReady();document.querySelector(".lcdpanel").classList.toggle("engwait",!ready);document.body.classList.toggle("engwait",!ready);
 ["rec","play"].forEach(id=>{const k=document.getElementById(id);if(k)k.disabled=!ready});
 bootScreen(st==="boot");
 b.title=ready?"Engine: running. Click to switch emulator or hardware, or load another ROM.":"Engine: "+txt.toLowerCase()+". Editing starts when it is ready."}
/* while BOOTING OS the LCD shows a firmware-style start-up screen (the text is the editor's, not a copy of the ROM's) */
let bootT=[];function bootScreen(on){const el=$("#bootscr");if(!el)return;bootT.forEach(clearTimeout);bootT=[];el.classList.toggle("on",on);if(!on)return;$("#bsmk").textContent=S.plate==="mk1"?"SFX-60":"SFX-60 MKII";
 const steps=[["OS 1.32B · TESTING MEMORY",15],["BATTERY RAM · 128 KITS",40],["128 PATTERNS · 24 SONGS",65],[S.plate==="mk1"?"DSP · FACTORY WAVES":"DSP · 64 DIGIPRO WAVES",85],["STARTING SEQUENCER",100]];
 steps.forEach(([t,f],i)=>bootT.push(setTimeout(()=>{$("#bstxt").textContent=t;$("#bsbar").style.width=f+"%"},i*210)))}
function startEngine(kind){engT.forEach(clearTimeout);engT=[];if(S.playing)togglePlay();
 if(kind==="hw"){setEng("hwwait");engT.push(setTimeout(()=>{setEng("hwready");toast("Hardware mode: sound edits go out as CCs; pattern and song edits wait for SYSEX RECV")},900));return}
 setEng("loading");engT.push(setTimeout(()=>setEng("boot"),700));engT.push(setTimeout(()=>{setEng("ready");toast("Emulator ready: the real OS 1.32B is running")},1800))}
/* ===== Transport ===== */
let clock=null;
function tick(){const prev=S.step;S.step=(S.step+1)%S.len;ctlTick();if(S.step===0&&S.queued!=null){applyPattern(S.queued);return}
 const pp=Math.floor(S.step/16);$$(".pl").forEach(b=>b.classList.toggle("play",+b.dataset.plp===pp&&S.playing));
 if(S.follow&&S.ws==="seq"&&!S.viewAll&&pp!==S.page&&!laneDraw){S.page=pp;render()}
 $("#tempoled").classList.toggle("on",S.step%4===0);setPos();queueMicrotask(movePH);
 $$(`.mst[data-s="${prev}"],.lb[data-s="${prev}"],.tc[data-s="${prev}"]`).forEach(c=>c.classList.remove("ph"));$$(`.mst[data-s="${S.step}"],.lb[data-s="${S.step}"],.tc[data-s="${S.step}"]`).forEach(c=>c.classList.add("ph"));
 if(S.ws==="seq")redraw();
 if(S.ws==="perform"){const act=[0,1,2,3,4,5].filter(i=>{const st=S.tracks[i].steps[S.step];return st&&!st.off&&st.a&&audible(i)&&(S.mode!=="poly"||i===asgT())});flashTracks(act)}}
function stepMs(){const m={"1X":1,"2X":2,"3/4X":.75,"3/2X":1.5}[S.mult];return 60000/S.bpm/4/m}
function restartClock(){clearInterval(clock);clock=setInterval(tick,stepMs())}
/* Soft playhead (as the MD Editor, v45): one ink-tinted column over the roll, the ENV/SLIDE/SWING
   rows and the lock lane that glides from step to step. It jumps without animation on a wrap, a
   page flip or a scroll, and fades out on stop. It lives on <body> in viewport coordinates, so the
   roll's and the lane's scrollers both carry it (movePH(false) on scroll and resize). */
let phX=null;
function movePH(glide=true){let ph=document.getElementById("phcol");
 const seq=$("#seq"),sc=$("#seqscroll"),col=S.playing&&S.step>=0&&S.ws==="seq"&&seq&&sc?seq.querySelector(`.ruler .rul[data-s="${S.step}"]`):null;
 if(!col){if(ph)ph.style.opacity="0";phX=null;return}
 if(!ph){ph=document.createElement("div");ph.id="phcol";ph.setAttribute("aria-hidden","true");document.body.appendChild(ph);phX=null}
 const r=col.getBoundingClientRect(),v=sc.getBoundingClientRect(),top=(seq.querySelector(".nlane.big")||col).getBoundingClientRect().top,
  lane=$("#lane"),bot=(lane&&lane.getClientRects().length?lane:$("#tlanes")||seq).getBoundingClientRect().bottom;
 const jump=!glide||phX==null||r.left<phX;
 ph.style.transition=jump?"opacity .15s":`transform ${Math.round(Math.min(stepMs()*.85,140))}ms cubic-bezier(.2,.7,.3,1),opacity .15s`;
 ph.style.width=r.width+"px";ph.style.top=(top-3)+"px";ph.style.height=(bot-top+6)+"px";ph.style.transform=`translateX(${r.left}px)`;
 ph.style.opacity=r.right>v.left+1&&r.left<v.right-1?"1":"0";phX=r.left}
addEventListener("scroll",()=>{if(S.playing)movePH(false)},true);addEventListener("resize",()=>{if(S.playing)movePH(false)});
function setPos(){$("#pos").textContent=S.playing&&S.step>=0?String(Math.floor(S.step/16)+1).padStart(2,"0")+"."+String(S.step%16+1).padStart(2,"0"):"--.--"}
function togglePlay(){if(!S.playing&&S.eng&&!engReady())return;S.playing=!S.playing;clearInterval(clock);$$(".pl").forEach(b=>b.classList.remove("play"));$("#tempoled").classList.remove("on");$$(".ph").forEach(c=>c.classList.remove("ph"));if(S.playing){S.step=-1;tick();restartClock()}else S.queued=null;setPos();renderTop();redraw();movePH(false)}

/* ===== Render ===== */
function render(){closePicker();closeK();const sl=$("#seqscroll")?.scrollLeft||0;renderTop();{const m=(S.ws==="seq"||S.ws==="sound")&&S.side==="midi";if(!m&&S.sel>5)S.sel-=6;if(m&&S.sel<6)S.sel+=6}const full=["mix","perform","song","control"].includes(S.ws);
 $("#body").className="body "+(full?"full ":"")+"ws-"+S.ws;$("#rail").hidden=full;if(!full)renderRail();renderSub();
 ({control:renderControl,seq:renderSeq,sound:renderSound,mix:renderMix,perform:renderPerform,song:renderSong})[S.ws]();
 const sc=$("#seqscroll");if(sc){sc.scrollLeft=sl;const l=$("#lanescroll");if(l)l.scrollLeft=sl}enhanceSelects($("#main"));if(S.learn)document.body.classList.add("learn");movePH(false)}
function setPlate(v){S.plate=v;document.documentElement.dataset.plate=v;try{localStorage.setItem("mmeditor.plate",v)}catch(_){}
 if(v==="mk1"){const bad=S.tracks.filter(t=>MACH[t.m].mk2);if(bad.length)toast(`MKI: ${bad.map(t=>t.m).join(", ")} needs a MKII. It would load as nothing on a MKI.`);else toast("MKI: no user waveforms, no DPRO-DDRW or DPRO-DENS. The plate looks the same.")}
 if(v==="mk2")toast("MKII: the same silver plate, plus user waveforms and the DigiPRO draw machines.");
 render();redraw()}
(()=>{let v=null;try{v=localStorage.getItem("mmeditor.plate")}catch(_){}if(!["mk1","mk2"].includes(v))v="mk2";S.plate=v;document.documentElement.dataset.plate=v})();
document.fonts&&document.fonts.ready.then(()=>{if(S.ws==="seq"){fitLane();drawSlides()}redraw();alignLock()});
if(window.ResizeObserver)new ResizeObserver(()=>{if(S.ws==="seq"){fitLane();alignLock()}}).observe(document.getElementById("main"));
addEventListener("resize",()=>{if(S.ws==="seq"){fitLane();drawSlides();alignLock()}redraw()});

/* deep links: #ws=sound&t=3&dock=arp&plate=mk1&mode=multi */
(()=>{const q=new URLSearchParams(location.hash.slice(1));if(q.get("plate")){S.plate=q.get("plate");document.documentElement.dataset.plate=S.plate}if(q.get("ws"))S.ws=q.get("ws");if(q.get("side")==="midi")S.side="midi";{const t=(q.get("t")?+q.get("t")-1:0)+((S.ws==="seq"||S.ws==="sound")&&S.side==="midi"?6:0);S.sel=t;select(t)}if(q.get("dock"))S.dock=q.get("dock");if(q.get("mode"))S.mode=q.get("mode");if(q.get("mt"))S.multi.mode=+q.get("mt");if(q.get("all"))S.viewAll=true;autoRange(S.sel)})();
render();H.last=snap();startEngine("emu");{const lb=new URLSearchParams(location.hash.slice(1)).get("lib");if(lb)setTimeout(()=>openLib(lb),2000)}
