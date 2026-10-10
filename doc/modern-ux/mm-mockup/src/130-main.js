
/* ===== Undo / redo: the host's, one step per gesture (the plug-in's core; on its own the demo host's snapshots,
   54-demo.js). A gesture ends on the events below, unless a drag, a hold or a GEN run or MUTATE trial holds it. ===== */
function commit(){if(laneDraw||rollDrag||drag||active||arpDrag||paint||rotHold)return;genStale();if(genHeld())return;if(HOST.commit)HOST.commit()}
function undo(){genEnd();if(HOST.undo)HOST.undo()}
function redo(){genEnd();if(HOST.redo)HOST.redo()}
["pointerup","keyup","click","change"].forEach(ev=>document.addEventListener(ev,()=>setTimeout(commit,0)));

/* ===== COPY CLEAR PASTE, per workspace, as the manual scopes them =====
   The machine's clipboard is the core's (copySteps, copySound, copyRow); the page keeps what it copied too (CLIP), to
   say what a paste would do and to show it at once. */
let CLIP=null;
/* the copied page into track t's steps a..b (its notes, slides and locks); the locks of parameters its machine has not are skipped (their count) */
function pastePage(t,a,b){const tr=trk(t);for(let i=a;i<b;i++){tr.steps[i]=CLIP.steps[i-a]&&JSON.parse(JSON.stringify(CLIP.steps[i-a]));tr.slide.delete(i);clearStepLocks(t,i)}CLIP.slide.forEach(x=>{if(x+a<b)tr.slide.add(x+a)});
 let skip=0;CLIP.locks.forEach(([pid,st])=>{if(!pname(t,pid)){skip++;return}st.forEach(([s,v])=>{if(s+a<b)setLock(t,pid,s+a,v,true)})});return skip}
/* ASSIGN's rows of a track as intents: page, dest and add of each tab's two rows, and its switches */
function editAssign(t,A){Object.entries(A.tabs).forEach(([tab,rows])=>rows.forEach((r,row)=>edit("assign",{t,src:ASRC[tab],row,page:r.pg,dest:r.d,add:r.add-64})));
 edit("assign",{t,mirror:!!A.mirr,hpf:!!A.hpf,lpf:!!A.lpf})}
function secAction(kind){const t=S.sel,tr=trk(t);
 if(S.ws==="seq"){const[a,b]=vis(),where=`${tLabel(t)}, steps ${a+1}–${b}`;
  if(kind==="copy"){CLIP={type:"page",midi:isMidiT(t),steps:tr.steps.slice(a,b).map(x=>x&&JSON.parse(JSON.stringify(x))),slide:[...tr.slide].filter(x=>x>=a&&x<b).map(x=>x-a),
   locks:[...S.locks].filter(([k])=>+k.split("|")[0]===t).map(([k,m])=>[k.split("|")[1],[...m].filter(([s])=>s>=a&&s<b).map(([s,v])=>[s-a,v])])};edit("copySteps",{t,from:a,to:b});toast("COPY PAGE: "+where+".");return}
  if(kind==="clear"){for(let i=a;i<b;i++){tr.steps[i]=null;tr.slide.delete(i);clearStepLocks(t,i)}edit("clearSteps",{t,from:a,to:b});render();toast("CLEAR PAGE: "+where+".");return}
  if(kind==="paste"){if(S.marks.size){pasteToMany();return}if(CLIP?.type!=="page"){toast("Copy a track page first.");return}if(CLIP.midi!==isMidiT(t)){toast("Track pages paste between synth tracks or between MIDI tracks.");return}
   const skip=pastePage(t,a,b);edit("pasteSteps",{t,from:a,to:b});render();toast("PASTE PAGE into "+where+"."+(skip?" "+skip+" lock(s) skipped: this machine has no such parameter.":""));return}}
 if(S.ws==="sound"&&!isMidiT(t)){if(kind==="copy"){CLIP={type:"machine",m:tr.m,v:JSON.parse(JSON.stringify(tr.v))};edit("copySound",{t});toast("COPY MACHINE: "+tr.m+" and all seven DATA pages.");return}
  if(kind==="clear"){tr.m="GND-SIN";tr.name=machName("GND-SIN");tr.v={SYN:synDefaults("GND-SIN"),AMP:[...DEFV.AMP],FLT:[...DEFV.FLT],EFX:[...DEFV.EFX],LF1:[...DEFV.LFO],LF2:[...DEFV.LFO],LF3:[...DEFV.LFO]};edit("clearSound",{t});render();toast("CLEAR MACHINE: "+tLabel(t)+" is GND-SIN again.");return}
  if(kind==="paste"){if(CLIP?.type!=="machine"){toast("Copy a machine first.");return}tr.m=CLIP.m;tr.name=machName(CLIP.m);tr.v=JSON.parse(JSON.stringify(CLIP.v));edit("pasteSound",{t});render();toast("PASTE MACHINE: "+CLIP.m+" onto "+tLabel(t)+".");return}}
 if(S.ws==="perform"){const A=S.tracks[asgT()];if(kind==="copy"){CLIP={type:"assign",a:JSON.parse(JSON.stringify(A.assign))};toast("Copied the assign tabs of T"+(asgT()+1)+".");return}
  if(kind==="clear"){A.assign=newAssign();editAssign(asgT(),A.assign);render();return}if(kind==="paste"){if(CLIP?.type!=="assign"){toast("Copy an assign first.");return}A.assign=JSON.parse(JSON.stringify(CLIP.a));editAssign(asgT(),A.assign);render();toast("Pasted the assign tabs.");return}}
 if(S.ws==="song"){const i=S.songSel,r=S.song[i];if(kind==="copy"){if(r.type==="end"){toast("END cannot be copied.");return}CLIP={type:"row",row:JSON.parse(JSON.stringify(r))};edit("copyRow",{i});toast("Copied row "+String(i+1).padStart(3,"0")+".");return}
  if(kind==="clear"){songAction("del");return}if(kind==="paste"){if(CLIP?.type!=="row"){toast("Copy a song row first.");return}if(S.song.length>=200){toast("A song holds 200 rows.");return}const at=r.type==="end"?i:i+1;S.song.splice(at,0,JSON.parse(JSON.stringify(CLIP.row)));S.songSel=at;edit("pasteRow",{i:at});render();return}}
 toast("Copy, clear and paste work in Sequence (page), Sound (machine), Perform (assign) and Song (row).")}

/* ===== First run: firmware needed ===== */
function firstRun(){if(HOST.firstRun)return HOST.firstRun();ask(`<div class="lcdbig">MONOMACHINE FIRMWARE NEEDED</div>
 <p>Monomachine Editor runs the real Monomachine operating system. Elektron's firmware cannot ship with the app, so you add the one from your own machine.</p>
 <ol class="recvsteps"><li>Dump the <b>OS 1.32B</b> flash image from your Monomachine (8 MiB, <span class="mono">.bin</span>).</li><li>Choose it here. The editor checks its size and fingerprint.</li><li>It stays on this computer only.</li></ol>
 <label class="drop" id="drop" tabindex="0"><input type="file" id="romfile" accept=".bin" hidden><span id="droptxt">Click to choose the .bin</span></label>
 <p class="hint">SFX-6, SFX-60 MKI and MKII use the same OS. The MKII adds the user waveforms and the DigiPRO draw machines.</p>`,[["Close preview","cream",()=>{}]],"first",{key:"firstRun"})}
function checkRom(f){const t=$("#droptxt");if(!f)return;const ok=f.size===8388608;t.textContent=ok?`✓ ${f.name}: 8 MiB. In the real app: check the OS 1.32B fingerprint, then start.`:`✗ ${f.name}: ${(f.size/1048576).toFixed(2)} MiB. The OS 1.32B image is exactly 8 MiB.`;$("#drop").classList.toggle("ok",ok);$("#drop").classList.toggle("bad",!ok)}

/* ===== LCD line 2 ===== */
function l2step(k,d,fine){
 if(k==="len"){S.len=lenStep(S.len,d,fine);edit("length",{v:S.len})}
 if(k==="mult"){const o=["1X","2X","3/4X","3/2X"];S.mult=o[(o.indexOf(S.mult)+d+4)%4];edit("speed",{v:S.mult})}
 if(k==="swing"){S.swingAmt=clamp(S.swingAmt+d,50,80);edit("swing",{v:S.swingAmt})}
  if(k==="ptrn"){S.patTrn=clamp(S.patTrn+d,0,127);edit("transpose",{v:S.patTrn-64})}
 if(k==="route"){S.routing=ROUTES[(ROUTES.indexOf(S.routing)+d+3)%3];edit("routing",{v:S.routing})}
 if(k==="side"){setSide(S.side==="midi"?"int":"midi");return}
 if(k==="seqmode"){seqMode(!S.plays.songMode);return}
 if(k==="dbl"){if(!fine)doublePattern();return}
 if(k==="pmode"){const o=PMODES.map(p=>p[0]);S.mode=o[(o.indexOf(S.mode)+d+4)%4]}
 render()}
let l2drag=null;
document.addEventListener("pointerdown",e=>{const el=e.target.closest(".l2.ed");if(!el)return;const k=el.dataset.l2;if(k==="swing"||k==="ptrn"){l2drag={k,y:e.clientY,v:k==="swing"?S.swingAmt:S.patTrn,moved:false};el.setPointerCapture(e.pointerId);e.preventDefault()}});
document.addEventListener("pointermove",e=>{if(!l2drag)return;if(e.buttons===0&&e.pointerType==="mouse"){l2drag=null;return}const d=Math.round((l2drag.y-e.clientY)/4);if(d)l2drag.moved=true;if(l2drag.k==="swing")S.swingAmt=clamp(l2drag.v+d,50,80);else S.patTrn=clamp(l2drag.v+d,0,127);renderSub()});
document.addEventListener("pointerup",()=>{if(!l2drag)return;const k=l2drag;l2drag=null;if(!k.moved)l2step(k.k,1);else{if(k.k==="swing")edit("swing",{v:S.swingAmt});else edit("transpose",{v:S.patTrn-64});render()}});
document.addEventListener("click",e=>{const el=e.target.closest(".l2.ed");if(!el)return;const k=el.dataset.l2;if(k!=="swing"&&k!=="ptrn")l2step(k,e.shiftKey?-1:1)});
document.addEventListener("wheel",e=>{const el=e.target.closest(".l2.ed");if(!el)return;e.preventDefault();if(el.dataset.l2==="seqmode")return;l2step(el.dataset.l2,(e.deltaY||e.deltaX)<0?1:-1,true)},{passive:false});

/* P7, as the MD Editor (v54): Shift + M prepares a mute ("+" unmute, "X" mute, blinking); the prepared
   mutes apply together when Shift comes up. Leaving the window drops them. */
const ARMED=new Map();
function showArmed(){$$(".ms.m[data-mute]").forEach(b=>{const p=ARMED.get(+b.dataset.mute);b.classList.toggle("prep",p!=null);if(p!=null)b.dataset.prep=p?"X":"+";else delete b.dataset.prep})}
document.addEventListener("keyup",e=>{if(e.key!=="Shift"||!ARMED.size)return;ARMED.forEach((m,i)=>{trk(i).mute=m});ARMED.clear();tx();if(HOST.mutes)HOST.mutes();render()});
addEventListener("blur",()=>{ARMED.clear();showArmed()});
/* As the MD Editor: a drag across the M (or S) keys paints them (shared/deskTogglePaint.js), rail, Mix and the
   Perform page's mutes alike: the pressed key toggles where the pointer goes down and its new state is the paint;
   every other key of its kind on the same side (synth T1-T6, MIDI M1-M6) the pointer crosses becomes that, once.
   Every point between two pointer events is looked at (no key skipped on a fast drag); the page's mutes go out
   once per event through HOST.mutes, which sends only the tracks that differ from the machine's (FieldExpectation).
   A click is a one-key paint and the click after the press is the gesture's; the keyboard's click and Shift-click
   (prepare) stay the click's. msPaint: {group, value, seen, at} or null. */
let msPaint=null,msClickEaten=false;
function msKeyAt(el){const b=el?.closest?.("[data-mute],[data-solo],[data-gmute]");if(!b||b.disabled||b.closest("[data-na]"))return null;
 const kind=b.dataset.mute!=null?"mute":b.dataset.solo!=null?"solo":"gmute",i=+b.dataset[kind];if(!trk(i))return null;return{group:kind+(i<6?":synth":":midi"),kind,i}}
const msOn=k=>!!trk(k.i)[k.kind==="solo"?"solo":"mute"];
function msSet(k,on){trk(k.i)[k.kind==="solo"?"solo":"mute"]=on}
function msSend(){tx();if(HOST.mutes)HOST.mutes();render()}
addEventListener("pointerdown",e=>{msClickEaten=false;if(e.button!==0||e.shiftKey||e.altKey||e.metaKey||e.ctrlKey)return;const k=msKeyAt(e.target);if(!k)return;
 const r=TogglePaint.begin(k.group,k.i,msOn(k));msPaint=Object.assign(r.paint,{at:{x:e.clientX,y:e.clientY}});msClickEaten=true;
 try{document.body.setPointerCapture(e.pointerId)}catch(_){}e.preventDefault();msSet(k,r.change);msSend()},true);
document.addEventListener("pointermove",e=>{if(!msPaint)return;if(e.buttons===0&&e.pointerType==="mouse"){endMsPaint();return}let sent=false;
 const co=e.getCoalescedEvents?.()||[];for(const ev of co.length?co:[e]){const at={x:ev.clientX,y:ev.clientY};
  for(const pt of TogglePaint.points(msPaint.at,at)){const k=msKeyAt(document.elementFromPoint(pt.x,pt.y));if(!k)continue;const r=TogglePaint.visit(msPaint,k.group,k.i,msOn(k));
   if(r.paint!==msPaint)msPaint=Object.assign(r.paint,{at:msPaint.at});if(r.change!=null){msSet(k,r.change);sent=true}}
  msPaint=Object.assign({},msPaint,{at})}
 if(sent)msSend()});
function endMsPaint(){if(!msPaint)return;msPaint=null;setTimeout(()=>{msClickEaten=false},0)}
document.addEventListener("pointerup",endMsPaint);document.addEventListener("pointercancel",endMsPaint);addEventListener("blur",endMsPaint);
addEventListener("click",e=>{if(!msClickEaten)return;msClickEaten=false;e.stopImmediatePropagation();e.preventDefault()},true);
/* P7: a drag across the SLIDE, SWING or envelope steps paints them: the first step decides on or off,
   and the drag is one edit (one undo step). A click is a one-step paint. */
let paint=null;
function paintAt(el){if(!paint||!el)return;const s=+el.dataset.s,k=el.dataset.tl||el.dataset.env,key=k+":"+s;if(k!==paint.k||paint.done.has(key))return;paint.done.add(key);
 const tr=trk(S.sel);
 if(el.dataset.tl){const set=k==="sld"?tr.slide:tr.swing;if(set.has(s)===paint.on)return;paint.on?set.add(s):set.delete(s);edit(k==="sld"?"slide":"swingStep",{t:S.sel,s,on:paint.on})}
 else{const st=tr.steps[s];if(!st||st.off||!!st[k]===paint.on)return;st[k]=paint.on?1:0;if(!st.n&&!st.a&&!st.f&&!st.l){tr.steps[s]=null;clearStepLocks(S.sel,s)}editStep(S.sel,s)}
 el.classList.toggle("on",paint.on);el.setAttribute("aria-pressed",paint.on);paint.moved=true}
function endPaint(){if(!paint)return;const p=paint;paint=null;if(p.moved)rerenderSeq()}
document.addEventListener("pointerup",endPaint);document.addEventListener("pointercancel",endPaint);addEventListener("blur",endPaint);
/* ===== Pointer input in the workspace ===== */
let drag=null,joyDrag=null,splitDrag=null,arpDrag=null;
const main=$("#main");
main.addEventListener("pointerdown",e=>{
 const pc=e.button===0&&!e.shiftKey&&!e.altKey&&!S.learn&&e.target.closest(".tc[data-tl],.tlane [data-env]");
 if(pc){const s=+pc.dataset.s,tr=trk(S.sel),k=pc.dataset.tl||pc.dataset.env;const on=pc.dataset.tl?!(k==="sld"?tr.slide:tr.swing).has(s):!tr.steps[s]?.[k];paint={k,on,done:new Set()};try{main.setPointerCapture(e.pointerId)}catch(_){}e.preventDefault();paintAt(pc);return}
 if(S.learn){const el=e.target.closest(".pc[data-g]");if(el&&(PAGES.includes(el.dataset.g)||el.dataset.g==="MID")){e.preventDefault();e.stopPropagation();const t=el.dataset.t!=null?+el.dataset.t:S.sel;S.learnT={t,pid:el.dataset.g+"."+el.dataset.n};toast(`Target: ${tLabel(t)} ${pidLabel(t,S.learnT.pid)}. Now press 1-8 for a knob.`);if(HOST.learnTarget)HOST.learnTarget({...S.learnT});return}}
 const h=e.target.closest(".lfohandle");if(h){cordStart(e,h);return}
 const pk=e.button===0&&e.target.closest('canvas[data-ed="dktrn"]');if(pk){trnDown(pk,e);e.preventDefault();return}
 const roll=e.target.closest("canvas.roll");if(roll){roll.setPointerCapture(e.pointerId);rollDown(roll,e);e.preventDefault();return}
 const c=e.target.closest("canvas.ed");if(c){const hh=nearest(c,e);if(!hh)return;active={c,k:hh.k,all:e.altKey&&!isMidiT(S.sel)};if(active.all)allTip();c.setPointerCapture(e.pointerId);e.preventDefault();redraw();return}
 const el=e.target.closest(".pc[data-g],.fader[data-g]");if(el){const t=el.dataset.t!=null?+el.dataset.t:S.sel;drag={el,x:e.clientX,y:e.clientY,v:getV(el),vert:el.classList.contains("fader"),hz:el.classList.contains("hz")&&el.getBoundingClientRect().width,mx:maxOf(ref(el)[2]),all:e.altKey&&!isMidiT(t)&&PAGES.includes(el.dataset.g)};if(drag.all)allTip();el.setPointerCapture(e.pointerId);el.classList.add("act");e.preventDefault();return}
 const lb=e.target.closest(".lb");if(lb){laneDraw=e.shiftKey&&!e.altKey?{ramp:true,touched:false}:{erase:e.altKey,touched:false};$("#lane").setPointerCapture(e.pointerId);laneAt(e);e.preventDefault();return}
 const ac=e.target.closest(".ac");if(ac){const k=+ac.dataset.ac,a=trk(S.sel).arp;if(k>=a.len){a.len=k+1;edit("arp",{t:S.sel,field:"length",v:a.len});renderArp();return}arpDrag={k,y:e.clientY,moved:false};$("#arptrack").setPointerCapture(e.pointerId);e.preventDefault();return}
 if(e.target.closest("#joy")){joyDrag=true;$("#joy").setPointerCapture(e.pointerId);joyAt(e);e.preventDefault();return}
 if(e.target.closest("#splitm")){splitDrag=true;$("#splitm").setPointerCapture(e.pointerId);e.preventDefault();return}
 const key=e.target.closest(".kb [data-key]");if(key){kbDown=true;$("#kb").setPointerCapture(e.pointerId);playKey(+key.dataset.key);e.preventDefault()}});
main.addEventListener("pointermove",e=>{
 if(paint){if(e.buttons===0&&e.pointerType==="mouse"){endPaint();return}paintAt(document.elementFromPoint(e.clientX,e.clientY)?.closest(".tc[data-tl],.tlane [data-env]"));return}
 if(e.buttons===0&&e.pointerType==="mouse"&&dragging()){endDrag(e);return}
 if(cord){cordMove(e);return}
 const roll=e.target.closest?.("canvas.roll")||(rollDrag&&$("#roll"));if(roll&&(rollDrag||e.target===roll)){rollMove(roll,e);if(rollDrag)return}
 if(active){const r=active.c.getBoundingClientRect(),hh=ED[active.c.dataset.ed].handles(r.width,r.height,active.c).find(h=>h.k===active.k);if(hh){const before=active.all?pagesCopy(S.sel):null,was={},menvWas={...S.menv};if(!isMidiT(S.sel))for(let k=0;k<6;k++)was[k]=pagesCopy(k);hh.drag(clamp(e.clientX-r.left,0,r.width),clamp(e.clientY-r.top,0,r.height));if(before)controlAllFrom(S.sel,before);
  /* a screen's handle moves kit values: their param intents; MULTI ENV's its own */
  if(!editParams(was))editMenv(menvWas);syncControls();redraw()}return}
 if(drag){const fine=e.shiftKey?.25:1,scale=drag.mx<16?drag.mx/127*1.6:1;/* a horizontal bar (the Mix PAN): its width is the whole range */
  const d=drag.vert?(drag.y-e.clientY)*127/150:drag.hz?(e.clientX-drag.x)*drag.mx/drag.hz:((e.clientX-drag.x)+(drag.y-e.clientY))/2;setV(drag.el,drag.v+d*fine*scale);return}
 if(laneDraw){laneAt(e);return}
 if(arpDrag){const a=trk(S.sel).arp,dd=Math.round((arpDrag.y-e.clientY)/4);if(Math.abs(dd)>0||arpDrag.moved){arpDrag.moved=true;a.ofs[arpDrag.k]=clamp((arpDrag.v0??(arpDrag.v0=a.ofs[arpDrag.k]))+dd,-24,24);renderArp()}return}
 if(joyDrag){joyAt(e);return}
 if(splitDrag){const kb=$("#kb").getBoundingClientRect(),[lo,hi]=kbRange(),whites=[];for(let n=lo;n<=hi;n++)if(!isBlack(n))whites.push(n);const i=clamp(Math.round((e.clientX-kb.left)/kb.width*whites.length),1,whites.length-1);if(S.multi.splitKey!==whites[i]){S.multi.splitKey=whites[i];$("#kb").innerHTML=renderKb();$("#splitm").setPointerCapture?.(e.pointerId)}return}
 if(kbDown){const k=document.elementFromPoint(e.clientX,e.clientY)?.closest(".kb [data-key]");if(k&&!k.classList.contains("dn"))playKey(+k.dataset.key);return}
 const c=e.target.closest?.("canvas.ed");if(c)c.style.cursor=ED[c.dataset.ed]?.cursor?.(c,e)||(nearest(c,e)?"grab":"default")});
function allTip(){if(S.allTold)return;S.allTold=1;toast("Control All: this value moves on all six synth tracks by the same amount (MIDI tracks stay).")}
function endDrag(e){trnUp();if(cord){cordEnd(e);return}if(rollDrag)rollUp();if(active){active=null;redraw()}if(drag){drag.el.classList.remove("act");drag=null}endLaneDraw();
 if(kbDown&&HOST.keyUp)HOST.keyUp();
 if(arpDrag){const a=trk(S.sel).arp,k=arpDrag.k;if(!arpDrag.moved){a.rhy[k]=!a.rhy[k];renderArp()}edit("arp",{t:S.sel,field:"step",i:k,v:a.rhy[k]?clamp(64+a.ofs[k]):255});arpDrag=null}
 if(joyDrag){joyDrag=false;S.joy={x:0,y:0};const k=$("#knobj");if(k){k.style.left="50%";k.style.top="50%"}if(HOST.joy)HOST.joy(S.joy)}
 if(splitDrag){splitDrag=false;edit("multiTrig",{splitKey:S.multi.splitKey});render()}if(kbDown){kbDown=false;$$(".kb .dn").forEach(k=>k.classList.remove("dn"))}}
/* P7: a gesture ends wherever the button comes up. A render during a drag (a pattern load, the machine's
   documents) removes the element that held the pointer, and its pointerup then lands outside #main: the drag
   stayed on and every mouse move edited the value (the kit showed "edited" after each pattern load). So the end
   is heard on the whole document, a move with no button down ends it too, and so does leaving the window. */
document.addEventListener("pointerup",endDrag);document.addEventListener("pointercancel",endDrag);
window.addEventListener("blur",()=>{if(dragging())endDrag({clientX:-1,clientY:-1})});
const dragging=()=>!!(drag||active||laneDraw||arpDrag||joyDrag||splitDrag||rollDrag||kbDown||cord);
function joyAt(e){const r=$("#joy").getBoundingClientRect();S.joy={x:clamp((e.clientX-r.left)/r.width*2-1,-1,1),y:clamp(1-(e.clientY-r.top)/r.height*2,-1,1)};const k=$("#knobj");k.style.left=(50+S.joy.x*42)+"%";k.style.top=(50-S.joy.y*42)+"%";
 const tr=S.tracks[asgT()],A=tr.assign,rows=A.tabs[S.asTab];const amt=S.asTab==="JOY U"?Math.max(0,S.joy.y):S.asTab==="JOY D"?Math.max(0,-S.joy.y):A.mirr?S.joy.x:Math.max(0,S.joy.x);
 $("#kbinfo")&&($("#kbinfo").textContent=rows.map(r=>`${LPAGES[r.pg]} ${destNames(asgT(),r.pg)[r.d]} ${Math.round((r.add-64)*amt)>=0?"+":""}${Math.round((r.add-64)*amt)}`).join(" · "));tx();if(HOST.joy)HOST.joy(S.joy)}
function renderArp(){const el=$("#arptrack");if(!el)return;const a=trk(S.sel).arp;el.innerHTML=arpCells(a);$$(".arplenrow button").forEach((b,k)=>b.classList.toggle("on",k<a.len));const c2=$("#arpcap");if(c2)c2.textContent="Rhythm + offset · "+a.len+" steps";$$(".mrowg .rowst").length&&0;redraw()}
main.addEventListener("wheel",e=>{
 const el=e.target.closest(".pc[data-g],.fader[data-g]");if(el){e.preventDefault();const d=(e.deltaY||e.deltaX)<0?1:-1;setV(el,getV(el)+d*(e.shiftKey?10:1));return}
 const st=e.target.closest(".mst");if(st){const t=+st.dataset.t,s=+st.dataset.s,x=trk(t).steps[s];if(!x?.n)return;e.preventDefault();const d=((e.deltaY||e.deltaX)<0?1:-1)*(e.shiftKey?12:1);x.n=x.n.map(n=>clamp(n+d));editStep(t,s);refreshRow(t);return}
 const roll=e.target.closest("canvas.roll[data-big]");if(roll){e.preventDefault();const t=+roll.dataset.t;S.rollLoT[t]=clamp((S.rollLoT[t]??S.rollLo)+((e.deltaY)<0?1:-1)*(e.shiftKey?12:2),0,127-ROWS);redraw()}},{passive:false});
main.addEventListener("dblclick",e=>{const el=e.target.closest(".pc[data-g]");if(el){const m=ref(el)[2];setV(el,m.en?0:m.signed?64:64)}});
main.addEventListener("keydown",e=>{const el=e.target.closest("[data-g]");if(!el)return;const d={ArrowRight:1,ArrowUp:1,ArrowLeft:-1,ArrowDown:-1,PageUp:10,PageDown:-10}[e.key];if(d==null)return;e.preventDefault();setV(el,getV(el)+d*(e.shiftKey?10:1))});

/* ===== Clicks ===== */
function autoRange(t){const ns=trk(t).steps.flatMap(x=>x?.n||[]);if(!ns.length)return;const lo=Math.min(...ns),hi=Math.max(...ns),cur=S.rollLoT[t];if(cur==null||lo<cur||hi>cur+ROWS-1)S.rollLoT[t]=clamp(Math.round((lo+hi)/2)-Math.floor(ROWS/2),0,127-ROWS)}
/* MIDI mapping (the CONTROL workspace and LEARN) is hidden until the host says it is on (the learn document's
   "enabled", mmAdapter.js); the mockup on its own shows it. */
function setMapping(on){S.mapping=!!on;{const t=$("#tabs [data-ws=control]"),k=$("#learnkey");if(t)t.hidden=!on;if(k)k.hidden=!on}if(!on){S.learn=false;S.learnT=null;document.body.classList.remove("learn");if(S.ws==="control")S.ws="seq"}}
function goWs(ws){if(ws==="control"&&!S.mapping)return;S.ws=ws;const t=S.sel%6+((ws==="seq"||ws==="sound")&&S.side==="midi"?6:0);select(t)}
function setSide(sd){S.side=sd;select(S.sel%6+(sd==="midi"?6:0))}
function select(t){S.sel=t;autoRange(t);const pg=S.lane.split(".")[0];if(isMidiT(t)){if(pg!=="MID"){S.lane="MID.1";S.lanePage="MID"}}else if(pg==="MID"||!pname(t,S.lane)){S.lane="FLT.1";S.lanePage="FLT"}render()}
document.addEventListener("click",e=>{
 const mu=e.target.closest("[data-mute]"),so=e.target.closest("[data-solo]");
 if(mu||so){const i=+(mu||so).dataset[mu?"mute":"solo"],t=trk(i);if(mu&&e.shiftKey){ARMED.has(i)?ARMED.delete(i):ARMED.set(i,!t.mute);showArmed();return}if(mu)t.mute=!t.mute;else t.solo=!t.solo;tx();if(HOST.mutes)HOST.mutes();render();return}
 const gm=e.target.closest("[data-gmute]");if(gm){const t=trk(+gm.dataset.gmute);t.mute=!t.mute;tx();if(HOST.mutes)HOST.mutes();render();return}
 const ev=e.target.closest("[data-env]");if(ev){if(e.detail>0)return;clickEnv(ev.dataset.env,+ev.dataset.s);return}	/* a mouse click was the paint gesture's */
 const sd=e.target.closest("[data-side]");if(sd){setSide(sd.dataset.side);return}
 const dk=e.target.closest("[data-dock]");if(dk){S.dock=dk.dataset.dock;rerenderSeq();return}
 const sdk=e.target.closest("[data-snddock]");if(sdk){S.dock=sdk.dataset.snddock;goWs("seq");return}
 const tl=e.target.closest(".tc[data-tl]");if(tl){if(e.detail>0)return;clickTl(tl.dataset.tl,+tl.dataset.s);return}
 const nd=e.target.closest("[data-node]");if(nd){S.sel=+nd.dataset.node;render();return}
 const sel=e.target.closest("[data-sel]");if(sel&&!e.target.closest("button,select,.pc,.fader")){select(+sel.dataset.sel);return}
 const lp=e.target.closest("[data-lpage]");if(lp){S.lanePage=lp.dataset.lpage;const n=pnames(S.sel,S.lanePage);S.lane=S.lanePage+"."+Math.max(0,trackLockPids(S.sel).filter(x=>x.startsWith(S.lanePage+".")).map(x=>+x.split(".")[1])[0]??0);if(!n.length){S.lane="FLT.1";S.lanePage="FLT"}render();return}
 const ch=e.target.closest("[data-lane]");if(ch){S.lane=ch.dataset.lane;render();return}
 if(e.target.closest("#clearLane")){if(e.altKey){clearTrackLocks(S.sel);return}S.locks.delete(lkKey(S.sel,S.lane));edit("clearLane",{t:S.sel,...pidArgs(S.lane)});render();return}
 if(e.target.closest('canvas[data-ed="dktrn"]'))return;	/* the plate plays on pointerdown (trnDown) */
 const sg=e.target.closest(".seg[data-set] button,.dkrow[data-set] button");if(sg){const k=sg.parentElement.dataset.set,v=sg.dataset.v,tr=trk(S.sel);
  if(k==="arpmode"){tr.arp.MODE=+v;edit("arp",{t:S.sel,field:"mode",v:+v});render();return}if(k==="arpplay"){tr.arp.PLAY=+v;edit("arp",{t:S.sel,field:"play",v:+v});render();return}
  if(k==="scale"){tr.tr.SCALE=+v;edit("transpose",{t:S.sel,scale:+v});render();return}
  if(k==="trigpos"){tr.trigpos=v===""?null:+v;edit("trigPos",{t:S.sel,v:tr.trigpos});render();return}if(k==="mch"){tr.ch=+v;edit("midiTrack",{t:S.sel-6,ch:+v-1});render();return}if(k==="port"){tr.port=+v;edit("portamento",{t:S.sel,v:+v?"legato":"always"});render();return}
  if(k==="ltrig"){editTrack(S.sel,()=>{V(sg.parentElement.dataset.l)[2]=+v});render();return}
  if(k==="routing"){S.routing=v;edit("routing",{v});render();return}
  if(k==="mtmode"){S.multi.mode=+v;edit("multiTrig",{mode:+v});render();return}if(k==="astab"){S.asTab=v;render();return}if(k==="astrk"){S.sel=+v;render();return}
  if(k==="songpick"){S.songPick=v;render();return}
  if(k==="loopkind"){const r=S.song[S.songSel];r.type=v;if(v==="halt")r.to=S.songSel;if(v==="jump"&&r.to<=S.songSel)r.to=Math.min(S.song.length-1,S.songSel+1);if(v==="loop"){if(!r.count)r.count=2;if(r.to>=S.songSel)r.to=Math.max(0,S.songSel-1)}editRow();render();return}}
 const at=e.target.closest("[data-arptrig]");if(at){const a=trk(S.sel).arp,k=at.dataset.arptrig;a[k]=a[k]?0:1;edit("arp",{t:S.sel,field:"trigs",v:(a.amp?1:0)|(a.flt?2:0)|(a.lfo?4:0)});render();return}
 const lg=e.target.closest("[data-leg]");if(lg){const l=trk(S.sel).leg,k=lg.dataset.leg;l[k]=l[k]?0:1;edit("legato",{t:S.sel,env:{amp:"amp",flt:"filter",lfo:"lfo"}[k],on:!!l[k]});render();return}
 const al=e.target.closest("[data-arplen]");if(al){trk(S.sel).arp.len=+al.dataset.arplen;edit("arp",{t:S.sel,field:"length",v:+al.dataset.arplen});renderArp();return}
 const rl=e.target.closest("[data-roll]");if(rl){S.rollLo=clamp(S.rollLo+ +rl.dataset.roll,0,127-ROWS);render();return}
 if(e.target.closest("#ghost")){S.ghost=!S.ghost;render();return}
 const lw=e.target.closest("[data-lwave]");if(lw){editTrack(S.sel,()=>{V(lw.dataset.lwave)[3]=+lw.dataset.w});render();return}
 const gt=e.target.closest("[data-goto]");if(gt){S.ws="sound";S.side="int";select(+gt.dataset.goto);return}
 const bs=e.target.closest("[data-bus]");if(bs){const t=+bs.dataset.t,tr=S.tracks[t];tr.out[bs.dataset.bus]=!tr.out[bs.dataset.bus];edit("route",{t,out:(tr.out.AB?1:0)|(tr.out.CD?2:0)|(tr.out.EF?4:0)});render();return}
 const pmk=e.target.closest("[data-pmode]");if(pmk){S.mode=pmk.dataset.pmode;if(HOST.keyMode)HOST.keyMode(S.mode);if(S.mode==="poly")toast("POLY: T"+(asgT()+1)+" now plays six voices. The other five tracks are off until you leave POLY.");render();return}
 const so2=e.target.closest("[data-strk]");if(so2){S.multi.splitTrack=clamp(S.multi.splitTrack+ +so2.dataset.strk,2,6);edit("multiTrig",{splitTrack:S.multi.splitTrack-1});render();return}
 const tm=e.target.closest("[data-tim]");if(tm){S.multi.timing=clamp(S.multi.timing+ +tm.dataset.tim,0,6);edit("multiTrig",{timing:S.multi.timing});render();return}
 const ko=e.target.closest("[data-kboct]");if(ko){S.kbOct=clamp(S.kbOct+ +ko.dataset.kboct,0,6);render();return}
 const bd=e.target.closest("[data-band]");if(bd){S.mmapSel=+bd.dataset.band;render();return}
 const mr=e.target.closest("tr[data-mrow]");if(mr&&!e.target.closest("button,.pc,.kselbtn")){S.mmapSel=+mr.dataset.mrow;render();return}
 const mh=e.target.closest("[data-mhi]");if(mh){const i=+mh.dataset.i,r=S.mmap[i],lo=i?S.mmap[i-1].hi+1:0,nx=S.mmap[i+1];r.hi=clamp(r.hi+ +mh.dataset.mhi,lo,nx?nx.hi-1:127);edit("multiMap",{i,hi:r.hi});render();return}
 const md=e.target.closest("[data-mdel]");if(md){const i=+md.dataset.mdel;if(S.mmap.length<2)return;S.mmap.splice(i,1);if(i===S.mmap.length)S.mmap[i-1].hi=127;S.mmapSel=Math.max(0,i-1);edit("multiMapDelete",{i});render();return}
 if(e.target.closest("[data-madd]")){const i=S.mmapSel,r=S.mmap[i],lo=i?S.mmap[i-1].hi+1:0;if(r.hi-lo<1){toast("A one-key range cannot be split.");return}const mid=Math.floor((lo+r.hi)/2);S.mmap.splice(i,0,{...r,hi:mid});edit("multiMapSplit",{i});render();return}
 const mi=e.target.closest("[data-mirr]");if(mi){const A=S.tracks[asgT()].assign;A.mirr=!A.mirr;edit("assign",{t:asgT(),mirror:A.mirr});render();return}
 const kt=e.target.closest("[data-ktrk]");if(kt){const A=S.tracks[asgT()].assign,k=kt.dataset.ktrk;A[k]=!A[k];edit("assign",{t:asgT(),[k]:A[k]});render();return}
 const pl=e.target.closest(".pl");if(pl&&S.ws==="seq"){const k=+pl.dataset.plp;if(k<pages16()){S.page=k;S.viewAll=false;render()}return}
 const pgk=e.target.closest("#pgkey");if(pgk&&!pgk.disabled){const n=pages16();S.viewAll=false;S.page=(S.page+(e.shiftKey?-1:1)+n)%n;render();return}
 if(e.target.closest("#pgall")){S.viewAll=!S.viewAll;render();return}
 if(e.target.closest("#pgfollow")){S.follow=!S.follow;render();return}
 if(S.ws==="song"){
  const bk=e.target.closest("[data-bank]");if(bk){S.bank=+bk.dataset.bank;render();return}
  const cp=e.target.closest("[data-chainpad]");if(cp){chainPad(+cp.dataset.chainpad);return}
  const ca=e.target.closest("[data-chain]");if(ca){if(!ca.disabled)chainAct(ca.dataset.chain);return}
  const ap=e.target.closest("[data-addpat]");if(ap){if(S.song.length>=200){toast("A song holds 200 rows.");return}let at2=S.songSel+1;if(S.song[S.songSel]?.type==="end")at2=S.songSel;S.song.splice(at2,0,{pat:+ap.dataset.addpat,rep:1});S.songSel=at2;edit("rowInsert",{i:at2,row:S.song[at2]});render();return}
  const rw=e.target.closest(".scell:not(.empty),.db[data-row]");if(rw){S.songSel=+rw.dataset.row;render();return}
  const ra=e.target.closest("[data-rowact]");if(ra){songAction(ra.dataset.rowact);return}
  const stp=e.target.closest("[data-step]");if(stp){songStep(stp.dataset.step,+stp.dataset.d*(e.shiftKey?10:1));return}
  const tt=e.target.closest("[data-ttr]");if(tt){const r=S.song[S.songSel];r.ttr=r.ttr||T64();const k=+tt.dataset.ttr;r.ttr[k]=clamp(r.ttr[k]+ +tt.dataset.d,28,100);editRow();render();return}
  const mk=e.target.closest("[data-rowmute]");if(mk){const r=S.song[S.songSel],k=+mk.dataset.rowmute;r.mutes=r.mutes||[];r.mutes=r.mutes.includes(k)?r.mutes.filter(x=>x!==k):[...r.mutes,k];editRow();render();return}
  if(e.target.closest("[data-bpmkeep]")){const r=S.song[S.songSel];r.bpm=r.bpm?undefined:Math.round(S.bpm);editRow();render();return}
  if(e.target.closest("[data-rowmore]")){S.songMore=!S.songMore;render();return}
  if(e.target.closest("[data-fullpat]")){const r=S.song[S.songSel];delete r.ofs;delete r.len;editRow();render();return}
  if(e.target.closest("[data-inf]")){const r=S.song[S.songSel];r.count=r.count===Infinity?2:Infinity;editRow();render();return}}
 const dl=e.target.closest("[data-dlg]");if(dl){const d=$("#dlg"),f=d._btns[+dl.dataset.dlg][2];d.hidden=true;f();return}
 if(e.target.id==="dlg"){$("#dlg").hidden=true;return}
 if(e.target.closest("#drop")&&!e.target.closest("input")){$("#romfile").click();return}
 if(e.target.closest("#undo")){undo();return}if(e.target.closest("#redo")){redo();return}
 const sc=e.target.closest("[data-sec]");if(sc){if(e.altKey&&sc.dataset.sec==="clear")clearPattern();else secAction(sc.dataset.sec);return}
 if(e.target.closest("#syncf.warn")){sendDialog();return}
 if(e.target.closest("#kitf")){toggleLib("kit");return}
 if(e.target.closest("#pat")){toggleLib("pat");return}
 if(e.target.closest("#learnkey")){S.learn=!S.learn;S.learnT=null;document.body.classList.toggle("learn",S.learn);renderTop();if(S.learn)toast("LEARN: click a value, then press 1-8 for a controller knob.");if(HOST.learning)HOST.learning(S.learn);return}
 const tb=e.target.closest("#tabs button");if(tb){goWs(tb.dataset.ws);return}
 if(e.target.closest("#platekey")){setPlate(S.plate==="mk1"?"mk2":"mk1");return}
 if(e.target.closest("#play")){togglePlay();return}
 if(e.target.closest("#rec")){if(HOST.record)return HOST.record();S.rec=!S.rec;renderTop();if(S.rec)toast("GRID RECORD on. On the machine: hold a trig to see its note, turn a knob to lock it. Here: just click and draw.");return}
 if(e.target.closest("#songload")){if(HOST.loadSong)return HOST.loadSong(S.songs.slot);S.songs.current=S.songs.slot;render();toast("LOAD SONG: the machine plays song "+String(S.songs.slot+1).padStart(2,"0")+" in song mode (only while stopped).");return}
 if(e.target.closest("#patPrev")){goPattern((S.queued??S.pat)-1);return}
 if(e.target.closest("#patNext")){goPattern((S.queued??S.pat)+1);return}});
document.addEventListener("change",e=>{const id=e.target.id,v=e.target.value,tr=trk(S.sel);
 if(id==="romfile"){checkRom(e.target.files[0]);return}
 if(id==="songsel"){if(HOST.songSlot)return HOST.songSlot(+v);S.songs.slot=+v;S.songSlot=+v;render();return}
 if(id==="engsel"){const btn=document.querySelector(".lcdeng");if(v==="global"){e.target.value=S.engine;btn.querySelector("span").textContent=e.target.selectedOptions[0].text;openGlobal();return}if(v==="audio"){e.target.value=S.engine;btn.querySelector("span").textContent=e.target.selectedOptions[0].text;openAudio();return}if(v==="rom"){e.target.value=S.engine;btn.querySelector("span").textContent=e.target.selectedOptions[0].text;if(HOST.romManage)HOST.romManage();else firstRun();return}
  S.engine=v;S.pend=0;renderPst();if(HOST.engine)HOST.engine(v);else startEngine(v);return}
 let m=id.match(/^lp(\d)$/);if(m){editTrack(S.sel,()=>{const l=V("LF"+m[1]);l[0]=+v;l[1]=0});render();return}
 m=id.match(/^ld(\d)$/);if(m){editTrack(S.sel,()=>{V("LF"+m[1])[1]=+v});render();return}
 m=id.match(/^inp(\d)$/);if(m){S.tracks[+m[1]].inp=v;edit("input",{t:+m[1],v:Math.max(0,INPUTS.indexOf(v))});render();return}
 m=id.match(/^asp(\d)$/);if(m){const r=S.tracks[asgT()].assign.tabs[S.asTab][+m[1]];r.pg=+v;r.d=0;edit("assign",{t:asgT(),src:ASRC[S.asTab],row:+m[1],page:r.pg,dest:0});render();return}
 m=id.match(/^asd(\d)$/);if(m){S.tracks[asgT()].assign.tabs[S.asTab][+m[1]].d=+v;edit("assign",{t:asgT(),src:ASRC[S.asTab],row:+m[1],dest:+v});return}
 m=id.match(/^mpat(\d+)$/);if(m){S.mmap[+m[1]].pat=+v;edit("multiMap",{i:+m[1],pat:mmapFw("pat",+v)});render();return}
 if(id==="trigpos"){tr.trigpos=v===""?null:+v;edit("trigPos",{t:S.sel,v:tr.trigpos});render();return}
 if(id==="mch"){tr.ch=+v;edit("midiTrack",{t:S.sel-6,ch:+v-1});render();return}});

/* ===== Keys: the Machinedrum Editor's mnemonic map (DESIGN-generators.md §5, MM-PORT-PLAN.md b), every key an
   entry of Keys (56-keys.js). LEARN's knob digits are its own (capture: while a value waits for its knob, 1-8 are
   knobs, not workspaces). ===== */
document.addEventListener("keydown",e=>{if(!(S.learn&&S.learnT&&/^[1-8]$/.test(e.key))||e.metaKey||e.ctrlKey||e.altKey||e.target.closest?.("input,select,textarea"))return;e.preventDefault();e.stopImmediatePropagation();learnBind(+e.key)},true);
function leaveLearn(){S.learn=false;document.body.classList.remove("learn");renderTop();if(HOST.learning)HOST.learning(false)}
Keys.bind({id:"close-dialog",scope:"any",keys:["Escape"],group:"Anywhere",does:"Close the dialog",when:()=>dialogOpen(),field:true,run:()=>{$("#dlg").hidden=true}});
Keys.bind({id:"undo",scope:"any",keys:["Z"],mod:"cmd",group:"Anywhere",does:"Undo",modal:"panel",run:()=>undo()});
Keys.bind({id:"redo",scope:"any",keys:["Z"],mod:"cmd+shift",group:"Anywhere",does:"Redo",modal:"panel",run:()=>redo()});
Keys.bind({id:"redo-y",scope:"any",keys:["Y"],mod:"cmd",group:"Anywhere",does:"Redo",modal:"panel",run:()=>redo()});
Keys.bind({id:"copy",scope:"any",keys:["C"],mod:"cmd",group:"Anywhere",does:"Copy (Sequence: the page shown of the track; Sound: the machine; Perform: the assign; Song: the row)",run:()=>secAction("copy")});
Keys.bind({id:"paste",scope:"any",keys:["V"],mod:"cmd",group:"Anywhere",does:"Paste (Sequence: into every track marked for paste too)",run:()=>secAction("paste")});
Keys.bind({id:"leave-learn",scope:"control",keys:["Escape"],group:"Anywhere",does:"Leave LEARN",mapping:true,when:()=>S.mapping&&S.learn,run:()=>leaveLearn()});
Keys.bind({id:"learn-knob",scope:"control",keys:["1 – 8"],group:"Anywhere",does:"LEARN: the controller knob for the value clicked",mapping:true});
Keys.bind({id:"audio-settings",scope:"any",keys:[","],group:"Anywhere",does:"AUDIO / MIDI settings (also in the engine menu)"});
Keys.bind({id:"play-stop",scope:"any",keys:["Space"],group:"Transport",does:"Play / stop",run:()=>togglePlay()});
Keys.bind({id:"record",scope:"any",keys:["Space"],code:"Space",mod:"alt",group:"Transport",does:"Live recording (RECORD + PLAY): Alt + play, the other Alt that is not \"all\". Again: recording off",run:()=>liveRecord()});
["seq","sound","mix","perform","song","control"].forEach((ws,i)=>Keys.bind({id:"workspace-"+(i+1),scope:"any",keys:[String(i+1)],group:"Workspaces",does:["Sequence","Sound","Mix","Perform","Song","Control"][i],mapping:ws==="control",when:ws==="control"?()=>S.mapping:null,run:()=>goWs(ws)}));
Keys.bind({id:"page-prev-next",scope:"seq",keys:["[","]"],group:"Sequence",does:"Previous / next page",when:()=>S.ws==="seq"&&pages16()>1,run:e=>{const n=pages16();S.viewAll=false;S.page=(S.page+(e.key==="]"?1:-1)+n)%n;render()}});
Keys.bind({id:"delete",scope:"seq song",keys:["Delete","Backspace"],group:"Sequence",does:"Clear the page shown of the selected track (Song: delete the row)",when:()=>S.ws==="song"||S.ws==="seq",run:()=>S.ws==="song"?songAction("del"):secAction("clear")});
Keys.bind({id:"clear-pattern",scope:"seq",keys:["Delete","Backspace"],mod:"alt",group:"All",does:"Sequence: clear the whole pattern: every track's notes, slides and locks (one undo step)",when:()=>S.ws==="seq",run:()=>clearPattern()});
Keys.bind({id:"clr-key-all",scope:"any",area:"Top bar",keys:["CLR"],mod:"alt",group:"All",does:"Click: clear the whole pattern, every track's notes, slides and locks (one undo step)"});
Keys.bind({id:"song-row",scope:"song",keys:["ArrowLeft","ArrowRight"],group:"Song",does:"Previous / next row",when:()=>S.ws==="song",run:e=>{S.songSel=clamp(S.songSel+(e.key==="ArrowRight"?1:-1),0,S.song.length-1);render()}});
Keys.bind({id:"value-up-down",scope:"any",keys:["ArrowUp","ArrowDown"],group:"Values",does:"A focused value, tempo or bar: one step (⇧: ×10, tempo: fine)"});
Keys.bind({id:"value-left-right",scope:"any",keys:["ArrowLeft","ArrowRight"],group:"Values",does:"A focused value: one step"});
Keys.bind({id:"control-all",scope:"sound mix",area:"Values",keys:["drag a value"],mod:"alt",group:"All",does:"Control All: move that value on every synth track (an editor feature; the Monomachine has no such key)"});
Keys.bind({id:"mkey-prepare",scope:"any",area:"Tracks",keys:["M key"],mod:"shift",group:"Anywhere",does:"Click: prepare that track's mute (+ / X); applied when ⇧ is let go"});
Keys.bind({id:"ms-paint",scope:"any",area:"Tracks",keys:["drag M / S keys"],group:"Anywhere",does:"Mute (solo) or unmute every track the drag crosses, as the first key became"});
Keys.bind({id:"roll-chord",scope:"seq",area:"Roll",keys:["roll"],mod:"shift",group:"Sequence",does:"Click: a chord note on the step"});
Keys.bind({id:"roll-paint",scope:"seq",area:"Roll",keys:["roll"],group:"Sequence",does:"Click an empty step: a note there; drag it up or down for its pitch, sideways to paint that note on every empty step crossed (one undo step)"});
Keys.bind({id:"roll-erase",scope:"seq",area:"Roll",keys:["roll"],mod:"alt",group:"Sequence",does:"Click: delete a note (drag on: every step crossed loses its notes, one undo step), or a NOTE OFF on an empty step"});
Keys.bind({id:"lane-erase",scope:"seq",area:"Lock lane",keys:["lock lane"],mod:"alt",group:"Sequence",does:"Drag: erase locks"});
Keys.bind({id:"lane-clear-all",scope:"seq",area:"Lock lane",keys:["lock lane clear"],mod:"alt",group:"Sequence",does:"Click: clear every lock of the track (all its parameters)"});

/* a control surface: nothing selects on a drag but the text fields (the stylesheet has user-select none on the body,
   text on the fields); selectstart is refused outside them too (WebKit, the plug-in's engine) */
const textField=el=>!!el?.closest?.("input,textarea,select,[contenteditable]:not([contenteditable=false]),.selectable");
document.addEventListener("selectstart",e=>{if(!textField(e.target))e.preventDefault()},true);
document.addEventListener("dragstart",e=>{if(e.target.closest?.("img,svg,canvas")&&!e.target.closest?.("[draggable=true]"))e.preventDefault()},true);

/* BPM: drag or arrows */
(()=>{const b=$("#bpm");let d=null;b.addEventListener("pointerdown",e=>{if(tempoLocked())return;d={y:e.clientY,v:S.bpm};b.setPointerCapture(e.pointerId)});
 b.addEventListener("pointermove",e=>{if(!d)return;if(e.buttons===0&&e.pointerType==="mouse"){d=null;return}S.bpm=clamp(Math.round((d.v+(d.y-e.clientY)*(e.shiftKey?.1:.5))*10)/10,30,300);if(HOST.tempo)HOST.tempo(S.bpm);renderTop();if(S.playing)restartClock()});
 b.addEventListener("pointerup",()=>d=null);b.addEventListener("keydown",e=>{const k={ArrowUp:1,ArrowDown:-1}[e.key];if(!k)return;e.preventDefault();if(tempoLocked())return;S.bpm=clamp(S.bpm+k*(e.shiftKey?.1:1),30,300);if(HOST.tempo)HOST.tempo(S.bpm);renderTop();if(S.playing)restartClock()})})();


/* ===== Engine status (from the MD Editor v48): the LCD says what the engine is doing; editing waits until it is ready ===== */
const ENG={...{norom:["NO ROM","off"],loading:["LOADING ROM","blink"],boot:["BOOTING OS","blink"],ready:["EMU OS 1.32B","on"],hwwait:["HW CONNECT","blink"],hwready:["HW MIDI","on"],hwnone:["HW NO MIDI","off"],error:["ROM ERROR","off"]},...HOST.engineLabels};
let engT=[];
/* Whether the machine takes input: machine.input when a host has said (S.input, set through
   setInput), as the MD page gates (V.input) -- not the engine label (S.eng), whose lifecycle map
   has no "takes input in hwLost" case and so used to disagree with the core. The standalone
   mockup has no host to call setInput, so S.input stays null and this falls back to the label. */
function engReady(){return S.input!=null?S.input:(S.eng==="ready"||S.eng==="hwready")}
function setInput(on){const v=!!on;if(S.input===v)return;S.input=v;refreshEngGate()}
/* the REC/PLAY keys and the "engwait" dimming follow engReady(), on any change that could move it */
function refreshEngGate(){const ready=engReady();document.querySelector(".lcdpanel").classList.toggle("engwait",!ready);document.body.classList.toggle("engwait",!ready);
 ["rec","play"].forEach(id=>{const k=document.getElementById(id);if(k)k.disabled=!ready})}
/* a host's words for what an engine state means: the label's tooltip */
function setEngineTip(st,tip){const e=ENG[st];if(!e||e[2]===tip)return;ENG[st]=[e[0],e[1],tip];if(S.eng===st)setEng(st)}
function setEng(st){S.eng=st;const[txt,led]=ENG[st],b=document.querySelector(".lcdeng"),l=document.getElementById("engled");if(!b)return;
 b.querySelector("span").textContent=txt;l.className="led "+(led==="on"?"on":led==="blink"?"on blink":"");
 refreshEngGate();
 /* P7: the start-up card over the whole window until the machine takes input; NO ROM and ROM ERROR are its first-run states */
 Boot.update({state:{norom:"missing",unsupported:"unsupported",loading:"loading",boot:"booting"}[st]||"ready",machine:"Monomachine"});
 /* HW MIDI with no machine answering: a card over the dimmed workspace says what to do (deskBoot.js) */
 Boot.midi({state:{hwwait:"connecting",hwnone:"lost"}[st]||null,machine:"Monomachine",text:ENG[st][2]});
 const kn=$("#kitname"),sv=$("#save");if(kn)kn.textContent=st==="hwwait"?"—":kitName(S.kit);if(sv)sv.style.visibility=st==="hwwait"?"hidden":"";
 bootScreen(st==="boot");
 b.title=ENG[st][2]||(engReady()?"Engine: running. Click to switch emulator or hardware, or load another ROM.":"Engine: "+txt.toLowerCase()+". Editing starts when it is ready.")}
/* while BOOTING OS the LCD shows a firmware-style start-up screen (the text is the editor's, not a copy of the ROM's) */
let bootT=[];function bootScreen(on){if(HOST.bootScreen)return HOST.bootScreen(on);const el=$("#bootscr");if(!el)return;
 /* P7: the example boot's screen is drawn into the start-up card: the editor's own text, not the ROM's */
 if(on){let f=0;const tk=()=>{if(S.eng!=="boot")return;f++;const b=new Uint8Array(1024);const px=(x,y)=>{b[y*16+(x>>3)]|=0x80>>(x&7)};for(let x=0;x<128;x++){px(x,0);px(x,63)}for(let y=0;y<64;y++){px(0,y);px(127,y)}for(let x=10;x<Math.min(118,10+f*6);x++)for(let y=28;y<36;y++)px(x,y);Boot.lcd(b);bootT.push(setTimeout(tk,70))};tk()}
 if(!HOST.bootScreen)return;bootT.forEach(clearTimeout);bootT=[];el.classList.toggle("on",on);if(!on)return;$("#bsmk").textContent=S.plate==="mk1"?"SFX-60":"SFX-60 MKII";
 const steps=[["OS 1.32B · TESTING MEMORY",15],["BATTERY RAM · 128 KITS",40],["128 PATTERNS · 24 SONGS",65],[S.plate==="mk1"?"DSP · FACTORY WAVES":"DSP · 64 DIGIPRO WAVES",85],["STARTING SEQUENCER",100]];
 steps.forEach(([t,f],i)=>bootT.push(setTimeout(()=>{$("#bstxt").textContent=t;$("#bsbar").style.width=f+"%"},i*210)))}
function startEngine(kind){engT.forEach(clearTimeout);engT=[];if(S.playing)togglePlay();
 if(kind==="hw"){setEng("hwwait");engT.push(setTimeout(()=>{setEng("hwready");toast("Hardware mode: sound edits go out as CCs; pattern and song edits wait for SYSEX RECV")},900));return}
 setEng("loading");engT.push(setTimeout(()=>setEng("boot"),700));engT.push(setTimeout(()=>{setEng("ready");toast("Emulator ready: the real OS 1.32B is running")},1800))}
/* ===== Transport ===== */
let clock=null;
function tick(){const prev=S.step;S.step=(S.step+1)%S.len;ctlTick();if(S.step===0){const q=S.queued??chainWrap();if(q!=null){applyPattern(q);return}}stepShown(prev)}
/* the playhead, POSITION, the page LEDs and the lamps at S.step (the step before it was prev). B-036, as the Machinedrum
   Editor's B-014: per step only the soft playhead moves (#phcol, from cached geometry) and POSITION, the page LEDs and
   the tempo LED change; no step cell is marked (their glows repainted a column of cells every step), no canvas is
   redrawn (they draw no playhead), and following the playhead to another page rebuilds the sequencer only */
let stepPage=-1;
function stepShown(prev){if(S.step===prev)return;const pp=Math.floor(S.step/16);
 if(pp!==stepPage){stepPage=pp;$$(".pl").forEach(b=>b.classList.toggle("play",+b.dataset.plp===pp&&S.playing))}
 if(S.follow&&S.ws==="seq"&&!S.viewAll&&pp!==S.page&&!laneDraw&&!menuOpen()){S.page=pp;followPage()}
 $("#tempoled").classList.toggle("on",S.step%4===0);setPos();queueMicrotask(movePH);
 if(S.ws==="perform"){const act=[0,1,2,3,4,5].filter(i=>{const st=S.tracks[i].steps[S.step];return st&&!st.off&&st.a&&audible(i)&&(S.mode!=="poly"||i===asgT())});flashTracks(act)}}
/* the playhead's page while following: the sequencer only (renderPage's work for it), not the whole page */
function followPage(){const sl=$("#seqscroll")?.scrollLeft||0;renderSeq();const sc=$("#seqscroll");if(sc){sc.scrollLeft=sl;const l=$("#lanescroll");if(l)l.scrollLeft=sl}enhanceSelects($("#main"));movePH(false)}
function stepMs(){const m={"1X":1,"2X":2,"3/4X":.75,"3/2X":1.5}[S.mult];return 60000/S.bpm/4/m}
function restartClock(){if(HOST.ownsClock)return;clearInterval(clock);clock=setInterval(tick,stepMs())}
/* Soft playhead (as the MD Editor, v45): one ink-tinted column over the roll, the ENV/SLIDE/SWING
   rows and the lock lane that glides from step to step. It jumps without animation on a wrap, a
   page flip or a scroll, and fades out on stop. It lives on <body> in viewport coordinates, so the
   roll's and the lane's scrollers both carry it (movePH(false) on scroll and resize). */
let phX=null,phGeo=null;
/* the playhead's geometry, read once per layout (a render, a scroll, a resize: movePH(false)), not every step (B-036:
   no forced layout read per step) */
function phGeom(){const seq=$("#seq"),sc=$("#seqscroll");if(!seq||!sc)return null;if(phGeo&&phGeo.seq===seq)return phGeo;
 const v=sc.getBoundingClientRect(),top=(seq.querySelector(".nlane.big")||seq).getBoundingClientRect().top,
  lane=$("#lane"),bot=(lane&&lane.getClientRects().length?lane:$("#tlanes")||seq).getBoundingClientRect().bottom,cols={};
 seq.querySelectorAll(".ruler .rul[data-s]").forEach(c=>{const r=c.getBoundingClientRect();cols[c.dataset.s]={left:r.left,right:r.right,width:r.width}});
 return phGeo={seq,v,top,bot,cols}}
function movePH(glide=true){let ph=document.getElementById("phcol");if(!glide){phGeo=null;stepPage=-1}
 const g=S.playing&&S.step>=0&&S.ws==="seq"?phGeom():null,r=g?g.cols[S.step]:null;
 if(!r){if(ph)ph.style.opacity="0";phX=null;return}
 if(!ph){ph=document.createElement("div");ph.id="phcol";ph.setAttribute("aria-hidden","true");document.body.appendChild(ph);phX=null}
 const v=g.v,top=g.top,bot=g.bot;
 const jump=!glide||phX==null||r.left<phX;
 ph.style.transition=jump?"opacity .15s":`transform ${Math.round(Math.min(stepMs()*.85,140))}ms cubic-bezier(.2,.7,.3,1),opacity .15s`;
 ph.style.width=r.width+"px";ph.style.top=(top-3)+"px";ph.style.height=(bot-top+6)+"px";ph.style.transform=`translateX(${r.left}px)`;
 ph.style.opacity=r.right>v.left+1&&r.left<v.right-1?"1":"0";phX=r.left}
addEventListener("scroll",()=>{if(S.playing)movePH(false)},true);addEventListener("resize",()=>{if(S.playing)movePH(false)});
function setPos(){$("#pos").textContent=S.playing&&S.step>=0?String(Math.floor(S.step/16)+1).padStart(2,"0")+"."+String(S.step%16+1).padStart(2,"0"):"--.--"}
/* A host's transport (P6): the machine's step and whether it plays, shown. */
function setStep(step){if(step===S.step)return;const prev=S.step;S.step=step;stepShown(prev)}
function setPlaying(on){if(S.playing===on)return;S.playing=on;if(!on){$$(".pl").forEach(b=>b.classList.remove("play"));$("#tempoled").classList.remove("on");$$(".ph").forEach(c=>c.classList.remove("ph"));S.step=-1}setPos();renderTop();redraw();movePH(false);markSongRow()}
function togglePlay(){if(HOST.togglePlay)return HOST.togglePlay();if(!S.playing&&S.eng&&!engReady())return;S.playing=!S.playing;clearInterval(clock);$$(".pl").forEach(b=>b.classList.remove("play"));$("#tempoled").classList.remove("on");$$(".ph").forEach(c=>c.classList.remove("ph"));if(S.playing){S.step=-1;tick();restartClock()}else S.queued=null;setPos();renderTop();redraw();movePH(false)}

/* ===== Render ===== */
/* The focused value keeps the focus across a render (its element is a new one then): its keys stay its own (↑ ↓ step
   it, the key map's promise), also when the machine's read-back of the step it just sent redraws the workspace. */
const FOCUS_KEYS=["g","n","t","i","l","gv"];
function focusedValue(){const el=document.activeElement,d=el&&el.dataset;if(!d||(d.g==null&&d.gv==null)||!$("#main")?.contains(el))return null;return FOCUS_KEYS.filter(k=>d[k]!=null).map(k=>`[data-${k}="${d[k]}"]`).join("")}
function render(){const f=focusedValue();renderPage();if(f){const el=document.querySelector("#main "+f);if(el&&document.activeElement!==el)el.focus({preventScroll:true})}}
function renderPage(){closePicker();closeK();const sl=$("#seqscroll")?.scrollLeft||0;renderTop();{const m=(S.ws==="seq"||S.ws==="sound")&&S.side==="midi";if(!m&&S.sel>5)S.sel-=6;if(m&&S.sel<6)S.sel+=6}const full=["mix","perform","song","control"].includes(S.ws);
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
/* ===== The view's side for a host (P6) =====
   A host reads the view through values (never its state) and sets it through named setters; what
   its engine cannot do it says with disable(capability, reason). */
const NA_SEL={
 midiMutes:[6,7,8,9,10,11].flatMap(t=>[`[data-mute="${t}"]`,`[data-solo="${t}"]`,`[data-gmute="${t}"]`]).join(","),
 transport:"#play",poly:'[data-pmode="poly"]',multiTrig:'[data-set="mtmode"] button,[data-strk],[data-tim],#splitm',
 multiMap:'.maprow [data-mhi],.maprow select,.maprow .kselbtn,.maprow .pc,[data-mdel],[data-madd],[data-band]',
 portamento:'[data-set="port"] button',gridRecord:"#rec",chains:'[data-chainpad],[data-chain]'};
const NA_CARD={multiMap:".maprow"};	// a card that also says the reason in words
/* capabilities with no control of their own here: they say how the machine is read */
const NA_INFO=["panelKeys","recvSession","lcd","workingKitMemory","telemetry"];
const NA={},READING={pattern:new Set(),kit:new Set()},READ_NOTE="Still reading this slot from the machine.";
function markNa(){
 /* one routine for both editors (shared/deskCaps.js): this page's look is [data-na], and a card that says why */
 DeskCaps.mark({controls:NA_SEL,reason:cap=>NA[cap]||"",attr:"na",cards:NA_CARD,all:$$,one:$});
 for(const[kind,attr] of [["pattern","ps"],["kit","ks"]])for(const el of $$(`#libpop .${attr}[data-${attr}]`)){const miss=READING[kind].has(+el.dataset[attr]);
  if(miss!==(el.dataset.na==="1")){if(miss){el.dataset.na="1";el.title=READ_NOTE}else delete el.dataset.na}}}
let naQueued=false;
new MutationObserver(()=>{if(!naQueued){naQueued=true;queueMicrotask(()=>{naQueued=false;markNa()})}}).observe(document.body,{childList:true,subtree:true});
DeskCaps.guard({attr:"na",events:["pointerdown","click","change","wheel","keydown","dragstart"],say:t=>toast(t)});
function disable(cap,why){NA[cap]=why||"";markNa()}
function setReading(kind,slots){READING[kind]=new Set(slots);markNa()}
/* start with nothing: no example kit, pattern, song or mappings. machineOnly (an engine reset): only the machine's
   state goes (kit, pattern, song, slots, transport); the Control workspace (CC sources, links, MIDI track targets)
   is the page's and the plug-in's, and stays */
function startEmpty(machineOnly){applyKit({...clearedKit(),multi:S.multi});S.tracks.forEach(t=>t.name=machName(t.m));applyPat(emptyPat(16));S.song=[{type:"end"}];S.songSel=0;
 S.kits=Array.from({length:128},()=>({name:"",empty:true,data:null}));S.patInfo=Array.from({length:128},()=>({has:false,len:16}));S.patKit=Array(128).fill(0);S.patData={};S.workName="";
 S.kit=0;S.pat=0;S.queued=null;S.playing=false;S.step=-1;if(machineOnly)return;S.ctl.links=[];S.ctl.sources=S.ctl.sources.filter(x=>x.kind==="cc")}
function setPatternSlot(p,{data,kit,has,len}){S.patKit[p]=kit;S.patInfo[p]={has,len};if(p===S.pat)applyPat(data);else S.patData[p]=data}
function setKitSlot(k,{name,empty,data}){S.kits[k]={name,empty,data}}
/* ===== The machine's documents, shown (DESIGN-UNIFY.md 4.3-4.5, phase 1) =====
   A host's view of the documents (mmView.js: derived from the documents it holds, with the documents and the
   values the page sent over them until they are answered) reaches S here, and nowhere else: show(v) is the
   one writer of S's document members. A member the view does not have yet (undefined) is left as it is; one
   equal to what was shown last is not written again, so a gesture under way keeps its objects and its value
   (what it sent is what the view shows anyway). What is written is a copy: S is the gestures' to change, the
   view never is. all: write every member again (a reset, an edit that was not taken). The drawing waits while
   a gesture or a menu is open (a render would close a menu under the person) and comes with the next show
   once they are closed; the state itself is always current. The mutes are the machine's unless a solo holds
   (the solo drives them). */
let SHOWN={},showLater=false;
const KIT_F=["m","name","v","lev","out","inp","trigpos","port","leg","assign"],MIDI_KIT_F=["v","cc","ch","name"],SEQ_F=["steps","slide","swing","arp","tr"];
const isSet=x=>x instanceof Set,isMap=x=>x instanceof Map;
function sameV(a,b){if(a===b)return true;if(a==null||b==null||typeof a!=="object"||typeof b!=="object")return false;
 if(isSet(a)||isSet(b))return isSet(a)&&isSet(b)&&a.size===b.size&&[...a].every(x=>b.has(x));
 if(isMap(a)||isMap(b))return isMap(a)&&isMap(b)&&a.size===b.size&&[...a].every(([k,x])=>b.has(k)&&sameV(x,b.get(k)));
 if(Array.isArray(a)!==Array.isArray(b))return false;const ka=Object.keys(a),kb=Object.keys(b);return ka.length===kb.length&&ka.every(k=>sameV(a[k],b[k]))}
function copyV(x){if(isSet(x))return new Set(x);if(isMap(x))return new Map([...x].map(([k,y])=>[k,copyV(y)]));if(Array.isArray(x))return x.map(copyV);
 if(x&&typeof x==="object")return Object.fromEntries(Object.entries(x).map(([k,y])=>[k,copyV(y)]));return x}
function show(v,all){
 if(all)SHOWN={};
 let doc=false,top=false,seq=false;
 if(v){const was=SHOWN;SHOWN=v;
  /* o[k] = a copy of x, unless the view has none or showed the same last time */
  const put=(o,k,x,y)=>{if(x===undefined||sameV(x,y))return false;o[k]=copyV(x);return true};
  for(const[k,f] of [["tracks",[...KIT_F,...SEQ_F]],["midi",[...MIDI_KIT_F,...SEQ_F]]])if(v[k])v[k].forEach((x,t)=>{const y=was[k]?.[t]||{};for(const n of f)if(put(S[k][t],n,x[n],y[n])){doc=true;if(SEQ_F.includes(n))seq=true}});
  for(const k of["len","mult","swingAmt","patTrn","locks"])if(put(S,k,v[k],was[k])){doc=true;seq=true}
  for(const k of["multi","menv","routing","plays"])if(put(S,k,v[k],was[k]))doc=true;
  if(put(S,"glob",v.glob,was.glob)&&GP.open)drawGlobal();
  if(put(S,"workName",v.workName,was.workName))top=true;
  for(const k of["pat","kit","queued","bpm","hostTempo"])if(put(S,k,v[k],was[k]))top=true;
  if(v.kitState!==undefined&&v.kitState!==was.kitState){setKitState(v.kitState);top=true}
  if(v.patSlot&&!sameV(v.patSlot,was.patSlot)){S.patKit[v.patSlot.p]=v.patSlot.kit;S.patInfo[v.patSlot.p]={has:v.patSlot.has,len:v.patSlot.len}}
  if(put(S,"song",v.song,was.song)||v.song!==undefined&&v.songSlot!==was.songSlot){S.songSlot=v.songSlot;S.songSel=Math.min(S.songSel||0,S.song.length-1);doc=true}
  if(put(S,"songs",v.songs,was.songs))doc=true;
  if(put(S,"mmap",v.mmap,was.mmap)){S.mmapSel=Math.min(S.mmapSel||0,S.mmap.length-1);doc=true}
  /* the machine's own mutes (T1-T6, M1-M6; null: not known) */
  const solo=[...S.tracks,...S.midi].some(x=>x.solo);
  if(v.mutes&&!solo)v.mutes.forEach((m,i)=>{if(m!=null&&m!==was.mutes?.[i]&&!!trk(i).mute!==m){trk(i).mute=m;doc=true}});
  /* POLY is the machine's audio mode; the keyboard's other modes are the page's */
  if(v.poly!=null&&v.poly!==was.poly){const m=v.poly?"poly":S.mode==="poly"?"normal":S.mode;if(m!==S.mode){S.mode=m;doc=true}}
  if(doc)pruneLocks();
  if(seq)autoRange(S.sel)}
 if(top)renderTop();
 if(!doc&&!showLater)return;
 if(busyNow()){showLater=true;return}
 showLater=false;render();drawLib()}
/* busy: a gesture, or a menu open (a render would close it under the person: the drawing waits for it, as for a drag) */
const busyNow=()=>{try{return !!(S.genEnding||drag||laneDraw||rollDrag||active||arpDrag||l2drag||joyDrag||splitDrag||cord||kbDown||paint)||menuOpen()}catch(_){return false}};
/* RECORD as the machine is in it: "off" | "grid" | "live" */
function setRecord(mode){const on=mode==="grid"||mode==="live";S.recMode=mode;if(!!S.rec!==on){S.rec=on;renderTop()}const b=$("#rec");if(b)b.title=mode==="live"?"LIVE RECORDING: notes you play are recorded. Click to stop recording.":mode==="grid"?"GRID RECORDING: the machine's TRIG keys write steps. Click to leave.":"RECORD: stopped = GRID RECORDING, playing = LIVE RECORDING (the keyboard's notes are recorded)."}
/* an engine state's LCD label: [text, led "on" | "blink" | "off", tooltip] */
function setEngineLabel(st,label){ENG[st]=label;if(S.eng===st)setEng(st)}
/* a host's engine map ([{id, label, available, reason}]) in the engine menu, before the menu's own entries */
function setEngines(list,current){const sel=$("#engsel");if(!sel)return;const own=["audio","global","rom"];
 for(const o of [...sel.options])if(!own.includes(o.value)&&!list.some(e=>e.id===o.value))o.remove();
 const first=[...sel.options].find(o=>own.includes(o.value))||null;
 for(const e of list){let o=sel.querySelector(`option[value="${e.id}"]`);if(!o){o=document.createElement("option");o.value=e.id}sel.insertBefore(o,first);o.textContent=e.label;o.disabled=!e.available;o.title=e.available?"":e.reason||""}
 if(current){sel.value=current;S.engine=current}}
/* the AUDIO / MIDI entry of the engine menu: only where the host has the devices (a standalone) */
function setAudioEntry(on){const o=document.querySelector('#engsel option[value="audio"]');if(o)o.hidden=o.disabled=!on;Boot.midiRefresh()}
/* the app sources and their links as the host's engine took them (ctlSetup() shape); the knob rows and their links stay */
function setCtlSetup({sources,links}){const cc=S.ctl.sources.filter(x=>x.kind==="cc"),ccIds=new Set(cc.map(x=>x.id)),old=S.ctl.sources;
 S.ctl.sources=[...cc,...sources.map(x=>({...x,val:old.find(o=>o.id===x.id)?.val??64}))];
 S.ctl.links=[...S.ctl.links.filter(l=>ccIds.has(l.src)),...links];
 if(!srcById(S.ctl.sel))S.ctl.sel=S.ctl.sources[0]?.id;if(S.ws==="control")render()}
/* the app sources as the host's engine runs them: their values ({id: 0-127}) and the CC rate */
function setModulation({values,ccPerSecond}){for(const sr of S.ctl.sources)if(values[sr.id]!=null)sr.val=values[sr.id];S.ctl.rate=ccPerSecond;if(S.ws==="control")ctlRefresh()}
const copy=o=>JSON.parse(JSON.stringify(o));
/* a host's own LCD picture: the machine's screen, 128 x 64, one bit a pixel in rows of 16 bytes
   (a Uint8Array); null: back to the editor's fields, cross-faded */
let lcdFadeT=0;
function setLcd(bits){const el=$("#bootscr");if(!el)return;clearTimeout(lcdFadeT);
 /* P7: the firmware's screen goes to the start-up card; the header's LCD stays itself */
 if(bits&&bits.length>=1024){Boot.lcd(bits);return}
 if(bits&&bits.length>=1024){let c=el.querySelector("canvas.fwlcd");if(!c){c=document.createElement("canvas");c.className="fwlcd";c.width=128;c.height=64;el.appendChild(c)}
  const g=c.getContext("2d"),cs=getComputedStyle(el);g.fillStyle=cs.getPropertyValue("--lcd").trim()||"#b7c79a";g.fillRect(0,0,128,64);g.fillStyle=cs.getPropertyValue("--ink").trim()||"#1d2a1a";
  for(let y=0;y<64;y++)for(let xb=0;xb<16;xb++){const b=bits[y*16+xb];if(b)for(let k=0;k<8;k++)if(b&(0x80>>k))g.fillRect(xb*8+k,y,1,1)}
  el.classList.remove("fading");el.classList.add("on","fw");return}
 if(!el.classList.contains("fw"))return;el.classList.add("fading");lcdFadeT=setTimeout(()=>el.classList.remove("on","fw","fading"),460)}
const dialogOpen=()=>!$("#dlg").hidden;
/* the firmware screen (an ask with class "first" and the firmware text) closes when the host has a firmware again */
/* the firmware runs: the no-ROM screen goes, shown or still waiting behind another dialog (only it: the SYSEX RECV
   steps share its look, so the dialog is named by its key, Dlg in deskModal.js) */
function closeFirmwareDialog(){Dlg.drop("firstRun")}
/* SysEx import and export: the host's file dialogs and document writes; the example shows a pretend file */
Syx.host={choose:()=>{if(HOST.syxChoose)return HOST.syxChoose();Syx.preview({ok:true,file:"example.syx",model:"Monomachine",fullBackup:false,problemCount:0,problems:[],items:{kit:[{slot:0,name:"SUPERWAVES",overwrites:true}],pattern:[{slot:0,name:"A01",kit:0,overwrites:true}],song:[],global:[]}})},
 exportAll:()=>{if(HOST.syxExport)return HOST.syxExport();toast("In the plug-in: a save dialog, then every document as one .syx.")},
 start:(k,s)=>{if(HOST.syxStart)return HOST.syxStart(k,s);Syx.progress({phase:"done",done:2,total:2,running:false,text:"2 imported (example).",report:{taken:2,items:[]}})},
 stop:()=>{if(HOST.syxStop)return HOST.syxStop()}};
/* the start-up card's keys: the host's native file chooser and ROM folder (the ROM stays on this computer); the
   example pretends an install */
Boot.host={chooseRom:()=>{if(HOST.chooseRom)return HOST.chooseRom();Boot.rom({ok:true,text:"\u2713 Monomachine OS 1.32B found (example)"});setTimeout(()=>startEngine("emu"),900)},
 revealRom:()=>{if(HOST.revealRom)return HOST.revealRom();toast("In the plug-in: the ROM folder opens in Finder.")},
 recheck:()=>{if(HOST.recheck)return HOST.recheck();startEngine("emu")},
 removeRom:i=>{if(HOST.removeRom)return HOST.removeRom(i)},say:t=>toast(t)};
/* the editor's menu (a host's): right-click anywhere the page has no menu of its own (DeskMenu.wantsEditor, I-008) */
document.addEventListener("contextmenu",e=>{if(!HOST.menu||!DeskMenu.wantsEditor(e))return;e.preventDefault();HOST.menu(e.clientX,e.clientY)});
window.MMView={
 /* values */
 audible,soloed:()=>[...S.tracks,...S.midi].some(x=>x.solo),engReady,asgT,noteName,pname,machName,kitName,
 gated:()=>Object.keys(NA_SEL),
 busy:busyNow,
 sel:()=>S.sel,mode:()=>S.mode,playing:()=>S.playing,step:()=>S.step,tempo:()=>S.bpm,engineState:()=>S.eng,kitState:()=>S.kitState,
 learnTarget:()=>S.learn&&S.learnT?{...S.learnT}:null,learning:()=>!!S.learn,ctlSetup,
 /* setters */
 /* the machine's documents: show(view) is the one writer of S's document members (DESIGN-UNIFY.md phase 1); the
    library's other slots keep their own cheap setters; setTempo is the BPM gesture's own write (the self-test's) */
 show,startEmpty,setPatternSlot,setKitSlot,setReading,setTempo:bpm=>{S.bpm=bpm},
 setInput,setPlaying,setStep,setSongRow:r=>{S.songRow=r;markSongRow()},
 setEng,dlgOpen:()=>!$("#dlg").hidden,setEngineLabel,setEngineTip,setEngines,setAudioEntry,clearLearnTarget:()=>{S.learnT=null},setMapping,setModulation,setCtlSetup,disable,
 setRecord,
 setLcd,setKeyDown,setPst,closeFirmwareDialog,bootRom:r=>Boot.rom(r),bootInstalled:o=>Boot.showInstalled(o),syxPreview:m=>Syx.preview(m),syxProgress:m=>Syx.progress(m),
 /* calls */
 render,renderTop,drawLib,toast,ask,redraw,movePH,setPos,flashTracks,goWs,clickStep,autoRange,kitSave,
 redrawAudio:()=>{if(AP.open)drawAudio()},audioLevel,openAudio};
/* the view gives exactly the seam's members (53-seam.js) */
{const have=Object.keys(window.MMView),odd=[...MM_SEAM.view.filter(k=>!have.includes(k)),...have.filter(k=>!MM_SEAM.view.includes(k))];if(odd.length)console.warn("MMView and 53-seam.js differ: "+odd.join(", "))}
/* The self-tests (a diagnostics build's bundle sets window.MMDiagnostics before this script) play the
   user through the view's state and run the panel's own test; a release page exports neither. */
if(window.MMDiagnostics)Object.assign(window.MMDiagnostics,{S,audioSelfTest});
setMapping(!window.MMHost);render();if(HOST.start)HOST.start();else startEngine("emu");{const lb=new URLSearchParams(location.hash.slice(1)).get("lib");if(lb)setTimeout(()=>openLib(lb),2000)}
