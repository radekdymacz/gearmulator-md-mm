
/* ===== Sequence (synth tracks) and MIDI SEQ (MIDI tracks) =====
   One object per step: a trig that carries its pitch (or chord), its NOTE OFF type,
   its envelope trig flags and its parameter locks. The overview shows every track;
   the note lane edits the selected track's trigs by pitch. Under it only the per-track
   SLIDE and SWING rows. The MIDI sequencer is its own workspace with the same editor. */
S.page=0;S.viewAll=true;S.follow=false;S.dock="locks";	/* P7: all steps by default; PAGE pages, ALL goes back */
const pages16=()=>Math.ceil(S.len/16);
function vis(){if(S.viewAll)return[0,S.len];S.page=Math.min(S.page,pages16()-1);return[S.page*16,Math.min(S.len,S.page*16+16)]}
function steps(){const[a,b]=vis();return Array.from({length:b-a},(_,k)=>a+k)}
/* ALL shows 64 steps in the width: columns may shrink below 18 px there (no horizontal scroll at 1280 px) */
function cols(){return`repeat(${steps().length},minmax(${S.viewAll?0:18}px,1fr)) 50px`}
const gapC=s=>s%16===0&&s!==vis()[0]?"gap":"";
/* MD Editor Sequence layout: rail = tracks + SYNTH/MIDI + lock parameter; main = page keys, ruler,
   ONE piano roll for the selected track (the height of the MD's 16-row grid), ENV/SLIDE/SWING rows, lock lane. */
const ROLL_H=16*28+15*3;
function th(t){const tr=trk(t),polyOff=S.mode==="poly"&&t!==S.sel&&t<6;return`<div class="th ${t===S.sel?"sel":""} ${audible(t)?"":"off"} ${polyOff?"poly-off":""}" data-sel="${t}">
 <div class="threw"><div class="sw"></div><div class="n ${t<6?"":"fill"}">${t<6?t+1:"M"+(t-5)}</div><div class="nm" title="${tr.name}"><b>${t<6?tr.m.replace("SWAVE-","SW-"):"CH"+String(tr.ch).padStart(2,"0")}</b></div>
 <button class="ms m ${ARMED.has(t)?"prep":""}" ${ARMED.has(t)?`data-prep="${ARMED.get(t)?"X":"+"}"`:""} data-mute="${t}" aria-pressed="${tr.mute}" aria-label="Mute ${tLabel(t)}">M</button><button class="ms s" data-solo="${t}" aria-pressed="${tr.solo}" aria-label="Solo ${tLabel(t)}">S</button></div></div>`}
function pageKeys(){return`<span class="pagectl rh"><button class="pgkey" id="pgkey" ${pages16()<2?"disabled":""} title="Next page. Shift-click = previous. Keys [ and ].">Page</button><span class="pleds" aria-hidden="true">${[0,1,2,3].map(k=>`<span class="pl ${k<pages16()?"":"na"} ${!S.viewAll&&k===S.page?"cur":""}" data-plp="${k}"><i class="led"></i></span>`).join("")}</span><button class="ptog ${S.viewAll?"on":""}" id="pgall" aria-pressed="${S.viewAll}" title="Show all steps"><i class="led"></i>All</button><button class="ptog ${S.follow?"on":""}" id="pgfollow" aria-pressed="${S.follow}" title="Page follows the play position"><i class="led"></i>Fol</button></span>`}
function lockPicker(t){const midi=isMidiT(t),list=pagesOf(t);if(!list.includes(S.lanePage))S.lanePage=list[0];
 return`<div class="railparams"><div class="rphead"><span class="cap">Lock parameter</span><button id="clearLane" class="iconkey" aria-label="Clear these locks" title="Clear ${pidLabel(t,S.lane)} locks"><svg viewBox="0 0 14 14" aria-hidden="true"><path d="M2 4h10M5.5 4V2.5h3V4M3.5 4l.7 8h5.6l.7-8M6 6.5v3.5M8 6.5v3.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg></button></div>
  <div class="lpspace"></div><div class="pagetabs" role="group" aria-label="DATA page">${midi?`<button data-lpage="MID" aria-pressed="true" style="grid-column:1/-1"><i class="led"></i>MIDI PAGE</button>`:PAGES.map(p=>{const n=trackLockPids(t).filter(x=>x.startsWith(p+".")).length;return`<button data-lpage="${p}" aria-pressed="${S.lanePage===p}" aria-label="${PAGEN[p]}${n?", "+n+" locked":""}"><i class="led ${n&&S.lanePage!==p?"g on":""}"></i>${p}</button>`}).join("")}</div>
  <div class="pkcol">${pnames(t,S.lanePage).map((nm,i)=>{if(!nm)return`<span class="pk empty"></span>`;const pid=S.lanePage+"."+i,n=S.locks.get(lkKey(t,pid))?.size||0;return`<button class="pk ${n?"has":""}" data-lane="${pid}" aria-pressed="${pid===S.lane}" title="${n?n+" locked step"+(n>1?"s":""):"No locks yet"}">${nm}${n?`<i>${n}</i>`:""}</button>`}).join("")}${Array.from({length:8-pnames(t,S.lanePage).length},()=>`<span class="pk empty"></span>`).join("")}</div></div>`}
function sideSw(){return`<div class="sidesw" role="group" aria-label="Sequencer side"><button data-side="int" aria-pressed="${S.side!=="midi"}" title="The six synth tracks"><i class="led"></i>Synth</button><button data-side="midi" aria-pressed="${S.side==="midi"}" title="The six MIDI sequencer tracks (FUNCTION + TRIG SELECT on the machine)"><i class="led"></i>MIDI</button></div>`}
function renderRail(){const r=$("#rail"),withSide=S.ws==="seq"||S.ws==="sound";
 r.innerHTML=`<div class="railhead">Track</div>`+side().map(th).join("")+(withSide?`<div style="margin-top:4px">${sideSw()}</div>`:"")+(S.ws==="seq"?lockPicker(S.sel):"")}
function rowStatus(t){const tr=trk(t),arp=tr.arp.MODE>0,trn=tr.tr.TRACK-64;return[arp?"ARP":"",t<6&&tr.tr.SCALE?["","FIX","MAJ","MIN"][tr.tr.SCALE]:(trn?(trn>0?"+":"")+trn:"")].filter(Boolean).join(" ")}
const ROLL_TIP=t=>`${tLabel(t)} notes. Click adds a note or moves its pitch · drag up or down for pitch · shift-click adds a chord note · drag a bar's end to move its ${isMidiT(t)?"LEN":"NOTE OFF"} · alt-click deletes a note, or sets a NOTE OFF on an empty step · scroll = pitch`;
function renderSeq(){const t=S.sel,tr=trk(t),midi=isMidiT(t);
 const rows=[["env",midi?"VEL":"Env"],["sld","Slide"],["swg","Swing"]];
 let h=`<div class="scroll" id="seqscroll"><div class="mstack ${S.viewAll?"all":""}" id="seq"><div class="mrowg ruler" style="grid-template-columns:${cols()}">${steps().map(s=>`<div class="rul ${gapC(s)}" data-s="${s}">${s%4===0?s+1:""}</div>`).join("")}<div class="rul"></div></div>
  <div class="nlane big"><canvas class="roll" data-ed="lane" data-t="${t}" data-big="1" title="${ROLL_TIP(t)}" aria-label="${ROLL_TIP(t)}"></canvas></div>
  <div class="tlanes" id="tlanes">${rows.map(([k,lab])=>{if(k==="env"&&midi)return`<div class="tlane env" style="grid-template-columns:${cols()}" title="Velocity per note (VEL, lockable)">${steps().map(s=>{const st=tr.steps[s];const v=st&&!st.off?velOf(t,s):null;return`<span class="tc envc ${gapC(s)} ${v==null?"na":""}">${v!=null?`<i class="velbar" style="--v:${v/127*100}%" title="VEL ${v}"></i>`:""}</span>`}).join("")}<span class="tlab">VEL</span></div>`;
   return`<div class="tlane ${k}" style="grid-template-columns:${cols()}" title="${{env:"Which envelopes this trig fires: AMP (red), FILTER (yellow), LFO (green), the manual's trig tracks. No dots = trigless.",sld:"Slide: a locked value glides to its next lock",swg:"Swing: these steps come late by the pattern's swing amount"}[k]}">${steps().map(s=>{const st=tr.steps[s];
    if(k==="env"){const ok=st&&!st.off;return`<span class="tc envc ${gapC(s)} ${ok?"":"na"}" data-s="${s}">${ok?["a","f","l"].map(b=>`<button class="d ${b} ${st[b]?"on":""}" data-env="${b}" data-s="${s}" aria-pressed="${!!st[b]}" aria-label="${{a:"AMP",f:"FILTER",l:"LFO"}[b]} trig step ${s+1}"></button>`).join(""):""}</span>`}
    const on=k==="sld"?tr.slide.has(s):tr.swing.has(s);return`<button class="tc ${on?"on":""} ${gapC(s)}" data-tl="${k}" data-s="${s}" aria-pressed="${on}" aria-label="${lab} step ${s+1}"></button>`}).join("")}<span class="tlab">${k==="env"?`<small class="envlab">A F L</small>`:lab}</span></div>`}).join("")}</div></div></div>
 <div class="seqfoot"><span></span><div class="legend"><span><i class="lg on"></i>Note</span>${midi?"":`<span><i class="lg on g"></i>Trigless</span>`}<span><i class="lg on y"></i>Note off</span><span><i class="lg on lk"></i>Has locks</span></div>${pageKeys()}</div>
 <div class="lanewrap"><div class="lanetop"><span class="cap">Lock lane · ${tLabel(t)} ${midi?"CH"+String(tr.ch).padStart(2,"0"):tr.m} · <b id="lanename">${pidLabel(t,S.lane)}</b> <span class="lanescale" id="lanescale"></span></span>
  <span class="lanehelp" title="Draw across the bars to lock this parameter per step. Alt-drag erases. Hatched steps have no trig, so they cannot hold a lock. Dashed line = kit value. A slide step glides to the next lock.">Draw to lock · alt-drag erases</span></div>
  <div class="scroll" id="lanescroll"><div class="lanebox"><div class="lane" id="lane" style="grid-template-columns:${cols()}"></div><svg class="lanesvg" id="lanesvg"></svg></div></div></div>`;
 $("#main").innerHTML=h;fitLane();renderLane();syncScroll();syncControls();alignLock()}
/* as in the MD Editor: the rail's LOCK PARAMETER block starts on the lock lane's title line and ends at its bottom edge */
function alignLock(){const rp=$("#rail .railparams"),lt=$(".lanetop"),ls=$("#lanescroll");if(!rp||!lt||!ls||S.ws!=="seq")return;
 rp.style.marginTop="0px";rp.style.height="auto";const dy=lt.getBoundingClientRect().top-rp.getBoundingClientRect().top;
 rp.style.marginTop=Math.max(0,dy)+"px";rp.style.height=Math.max(0,ls.getBoundingClientRect().bottom-lt.getBoundingClientRect().top)+"px";
 /* the lane's bars span the parameter keys: they start level with the page tabs and end with the last key */
 ls.style.marginTop="0px";const tb=rp.querySelector(".pagetabs"),ln=$("#lane");if(tb&&ln){const off=tb.getBoundingClientRect().top-ln.getBoundingClientRect().top;if(off>0)ls.style.marginTop=off+"px"}}
function dockInfo(t){return{locks:`<span class="cap" style="font-size:11px">${pidLabel(t,S.lane)}</span> <span class="lanescale" id="lanescale"></span> <span class="hint">draw to lock · alt-drag erases · slide trigs glide</span>`,arp:"FUNCTION + ARP · one per track, 12 per pattern",trn:"FUNCTION + TRANSPOSE · live, the notes stay",trig:"KIT › TRIG",midipage:"GLOBAL › MIDI SEQ › MIDISEQ SET · page stored in the kit"}[S.dock]}
function dockBody(t){if(S.dock==="locks")return`<div class="scroll" id="lanescroll"><div class="lanebox"><div class="lane" id="lane" style="grid-template-columns:${cols()}"></div><svg class="lanesvg" id="lanesvg"></svg></div></div>`;
 if(S.dock==="arp")return arpPanel(t);if(S.dock==="trn")return trnPanel(t);if(S.dock==="trig")return trigPanel(t);return midiPanel(t)}
/* the roll takes the stack width from CSS (.nlane width:0; min-width:100%), so the canvas bitmap size (dpr) never widens the grid */
function fitLane(){const g=$("#seq"),sc=$("#seqscroll");if(!g)return;$$("#seq canvas.roll").forEach(c=>c.style.width="");const d=$("#dock");if(d&&sc)d.style.width=sc.clientWidth+"px";redraw()}
/* MD v58/v59 lock lane: lockable steps are hardware keys with a vertical LED meter; no trig = a dim key.
   One barHTML for render and drag. Heights are fractions of the key's inner meter (key height minus 14 px). */
const IN="(100% - 14px)";
function laneMeta(){const t=S.sel,[pg,i]=S.lane.split(".");const m=meta(t,pg,+i);return{m,mx:maxOf(m),bip:!!m.signed}}
function barHTML(v){const{mx,bip}=laneMeta(),f=v/mx;if(bip)return f>=.5?`<i class="bp up" style="bottom:calc(6px + ${IN}*.5);height:calc(${IN}*${(f-.5).toFixed(4)})"></i>`:`<i class="bp dn" style="top:calc(8px + ${IN}*.5);height:calc(${IN}*${(.5-f).toFixed(4)})"></i>`;
 return`<i style="bottom:6px;height:calc(${IN}*${f.toFixed(4)})"></i>`}
function baseHTML(v,mx){return`<div class="base" style="bottom:calc(6px + ${IN}*${(v/mx).toFixed(4)})"></div>`}
function renderLane(){const lane=$("#lane");if(!lane)return;const t=S.sel,tr=trk(t),pid=S.lane,[pg,i]=pid.split("."),m=meta(t,pg,+i),mx=maxOf(m),lm=S.locks.get(lkKey(t,pid)),base=getP(t,pid);
 const sc=$("#lanescale");if(sc)sc.textContent=m.en?`${m.en[0]}…${m.en[mx]}`:m.signed?"−64…+63":"0–127";
 lane.innerHTML=steps().map(s=>{const st=tr.steps[s],on=st&&!st.off,v=lm?.get(s);
  return`<div class="lb ${on?"":"none"} ${on?"k-"+(isMidiT(t)?"full":stepKind(st)):""} ${gapC(s)} ${S.playing&&s===S.step?"ph":""}" data-s="${s}" title="${on?(v!=null?"Locked "+fmt(m,v):"Kit value "+fmt(m,base)):"No trig here: add one (a trigless trig with alt-click keeps the envelopes quiet)"}">${on?`${baseHTML(base,mx)}${m.signed?`<div class="mid"></div>`:""}${v!=null?barHTML(v):""}`:""}</div>`}).join("")+`<div class="lbscale"><b>${fmt(m,mx)}</b><span>${fmt(m,base)}<small>kit</small></span><b>${fmt(m,0)}</b></div>`;
 requestAnimationFrame(drawSlides)}
function drawSlides(){const svg=$("#lanesvg"),lane=$("#lane");if(!svg||!lane)return;const t=S.sel,tr=trk(t),lm=S.locks.get(lkKey(t,S.lane));svg.innerHTML="";if(!lm)return;
 const[pg,i]=S.lane.split("."),mx=maxOf(meta(t,pg,+i)),box=lane.getBoundingClientRect(),ink=cssv("--ink");let d="";const locked=[...lm.keys()].sort((a,b)=>a-b);
 locked.forEach(s=>{if(!tr.slide.has(s))return;const nx=locked.find(x=>x>s);if(nx==null)return;const a=lane.querySelector(`.lb[data-s="${s}"]`),b=lane.querySelector(`.lb[data-s="${nx}"]`);if(!a)return;
  const ra=a.getBoundingClientRect(),y=v=>ra.bottom-box.top-6-v/mx*(ra.height-14),xa=ra.left-box.left+ra.width/2;
  const xb=b?b.getBoundingClientRect().left-box.left+b.getBoundingClientRect().width/2:box.width-50;d+=`<path d="M${xa} ${y(lm.get(s))} L${xb} ${y(lm.get(nx))}" fill="none" stroke="${ink}" stroke-width="2.5" stroke-dasharray="6 3"/><circle cx="${xa}" cy="${y(lm.get(s))}" r="4" fill="${ink}"/>`});
 svg.setAttribute("width",box.width);svg.setAttribute("height",box.height);svg.innerHTML=d}
function syncScroll(){const a=$("#seqscroll"),b=$("#lanescroll");if(!a||!b)return;a.onscroll=()=>{b.scrollLeft=a.scrollLeft};b.onscroll=()=>{a.scrollLeft=b.scrollLeft}}
function refreshRow(t){redraw();return;const gate={},am=ampSteps(t);noteSpans(t).forEach(sp=>{for(let k=sp.s;k<sp.e;k++)gate[k]=(k===sp.s?(sp.e-sp.s>1?"gs":"g1"):k===sp.e-1?"ge":"gm")+(k-sp.s>=am?" gd":"")});$$(`.mst[data-t="${t}"]`).forEach(b=>{const s=+b.dataset.s,st=trk(t).steps[s];b.className=stepCls(t,s);b.setAttribute("aria-pressed",!!st);b.title=stepTitle(t,s,st);b.innerHTML=`<span class="nt">${stepText(t,s,st)}</span>${gate[s]?`<span class="gt ${gate[s]}"></span>`:""}`})}
function rerenderSeq(){const sl=$("#seqscroll")?.scrollLeft||0;renderRail();renderSeq();const sc=$("#seqscroll");if(sc){sc.scrollLeft=sl;const l=$("#lanescroll");if(l)l.scrollLeft=sl}enhanceSelects($("#main"));movePH(false)}
function clickStep(t,s,e){const tr=trk(t),st=tr.steps[s];
 if(e.shiftKey){tr.steps[s]=st?.off?null:{off:1};if(!st?.off)clearStepLocks(t,s)}
 else if(e.altKey&&!isMidiT(t)){if(!st||st.off)tr.steps[s]={n:[lastNote(t,s)],a:0,f:0,l:0};else{const k=stepKind(st);Object.assign(st,k==="full"?{a:0,f:0,l:0}:{a:1,f:1,l:1})}}
 else{if(st&&!st.off){tr.steps[s]=null;clearStepLocks(t,s)}else tr.steps[s]=note(lastNote(t,s))}
 structEdited();renderTop();if(t!==S.sel){S.sel=t;autoRange(t);render()}else rerenderSeq()}
function clickTl(k,s){const tr=trk(S.sel);if(k==="sld"){tr.slide.has(s)?tr.slide.delete(s):tr.slide.add(s)}else{tr.swing.has(s)?tr.swing.delete(s):tr.swing.add(s)}structEdited();rerenderSeq()}
function clickEnv(b,s){const t=S.sel,st=trk(t).steps[s];if(!st||st.off)return;st[b]=st[b]?0:1;if(!st.n&&!st.a&&!st.f&&!st.l){trk(t).steps[s]=null;clearStepLocks(t,s)}structEdited();rerenderSeq()}
let laneDraw=null;
function laneAt(e){const lane=$("#lane");if(!lane)return;const el=document.elementFromPoint(e.clientX,e.clientY)?.closest(".lb");if(!el||!lane.contains(el))return;
 const s=+el.dataset.s,t=S.sel,st=trk(t).steps[s];if(!st||st.off)return;const[pg,i]=S.lane.split("."),mx=maxOf(meta(t,pg,+i));const r=el.getBoundingClientRect();const v=clamp(Math.round((r.bottom-6-e.clientY)/(r.height-14)*mx),0,mx);
 if(laneDraw.erase){const m=S.locks.get(lkKey(t,S.lane));if(m){m.delete(s);if(!m.size)S.locks.delete(lkKey(t,S.lane))}}else if(!setLock(t,S.lane,s,v))return;
 el.querySelector("i")?.remove();if(!laneDraw.erase)el.insertAdjacentHTML("beforeend",barHTML(v));
 laneDraw.touched=true;renderTop();drawSlides()}
function endLaneDraw(){if(!laneDraw)return;const t=laneDraw.touched;laneDraw=null;if(t){structEdited();rerenderSeq()}}

/* ===== the note lane: the selected track's trigs by pitch, same columns as the overview ===== */
const ROWS=24;
S.rollLoT={};
function laneGeom(c){const t=+c.dataset.t,big=!!c.dataset.big,tr=trk(t),cr=c.getBoundingClientRect(),arp=big&&tr.arp.MODE>0,H=cr.height-(arp?30:0);const col={};
 document.querySelectorAll("#seq .ruler .rul[data-s]").forEach(b=>{const r=b.getBoundingClientRect();col[+b.dataset.s]={x0:r.left-cr.left,x1:r.right-cr.left}});const vs=steps();const lastX=vs.length&&col[vs[vs.length-1]]?col[vs[vs.length-1]].x1:0;
 let lo,rows;if(big){lo=S.rollLoT[t]??S.rollLo;rows=ROWS}else{const ns=tr.steps.slice(0,S.len).flatMap(x=>x?.n||[]);const mn=ns.length?Math.min(...ns):60,mx=ns.length?Math.max(...ns):60;rows=Math.max(6,Math.min(24,mx-mn+3));lo=Math.round((mn+mx)/2)-Math.floor(rows/2)}
 const top=big?4:2,rh=(H-top-(big?4:2))/rows;
 return{t,big,col,vs,a:vs[0],b:vs[vs.length-1]+1,top,rh,rows,lastX,arp,H,lo,hi:lo+rows-1,y:n=>top+(lo+rows-1-n)*rh}}
function inScale(tr,n){if(!tr.tr||tr.tr.SCALE<2)return true;const sc=tr.tr.SCALE===2?MAJ:MIN;return sc.includes(((n-tr.tr.KEY)%12+12)%12)}
function velOf(t,s){return velOf2(t,s)}
function velOf2(t,s){return S.locks.get(lkKey(t,"MID.1"))?.get(s)??trk(t).v.MID[1]}
ED.lane={draw(g,W,H,c){const G=laneGeom(c),t=G.t,tr=trk(t),ink=cssv("--ink");if(!G.vs.length)return;const midi=isMidiT(t);
  if(!audible(t))g.globalAlpha=.45;
  if(G.big)for(let n=G.lo;n<=G.hi;n++){const y=G.y(n),blk=[1,3,6,8,10].includes(n%12);if(blk){g.fillStyle=inkA(.05);g.fillRect(0,y,G.lastX,G.rh)}
   if(!inScale(tr,n)){g.fillStyle=inkA(.07);for(let x=0;x<G.lastX;x+=6)g.fillRect(x,y,3,G.rh)}
   if(n%12===0){g.fillStyle=inkA(.3);g.fillRect(0,y+G.rh-.5,G.lastX,1)}
   g.fillStyle=blk?ink:inkA(.12);g.fillRect(G.lastX+6,y+.5,blk?14:22,G.rh-1);if(n%12===0||n===G.hi){g.fillStyle=ink;g.font="9px Silkscreen, monospace";g.fillText(noteName(n),G.lastX+30,y+G.rh-2)}}
  else{g.fillStyle=ink;g.font="8px Silkscreen, monospace";const ns=tr.steps.slice(0,S.len).flatMap(x=>x?.n||[]);if(ns.length){const mn=Math.min(...ns),mx=Math.max(...ns);g.fillText(mn===mx?noteName(mn).replace("-",""):noteName(mn).replace("-","")+"-"+noteName(mx).replace("-",""),G.lastX+4,11)}const st=rowStatus(t);if(st)g.fillText(st,G.lastX+4,H-5)}
  G.vs.forEach(s=>{const x=G.col[s];if(!x)return;if(s%4===0){g.fillStyle=inkA(G.big?.1:.07);g.fillRect(x.x0,0,x.x1-x.x0,G.H)}if(S.playing&&s===S.step&&!G.big){g.fillStyle=inkA(.2);g.fillRect(x.x0,0,x.x1-x.x0,H)}});
  if(G.big&&S.ghost)side().forEach(o=>{if(o===t||(o<6&&(isFx(trk(o).m)||trk(o).m==="DPRO-BBOX")))return;noteSpans(o).forEach(sp=>{const r=spanX(G,sp);if(!r||sp.n[0]<G.lo||sp.n[0]>G.hi)return;g.fillStyle=inkA(.14);g.fillRect(r[0]+2,G.y(sp.n[0])+3,r[1]-r[0]-4,G.rh-6)})});
  noteSpans(t).forEach(sp=>{const r=spanX(G,sp);if(!r)return;const[x0,x1]=r,w=x1-x0,c0=G.col[sp.s];const va=midi?.4+.6*velOf(t,sp.s)/127:1;
   sp.n.forEach((n,k)=>{if(n<G.lo||n>G.hi)return;const y=G.y(n)+(G.big?1:.5),h=G.big?G.rh-2:Math.max(3,G.rh-1);
    if(k===0){if(sp.pitchless){g.setLineDash([4,3]);g.strokeStyle=ink;g.lineWidth=1.5;g.strokeRect(x0+.5,y+.5,w-1,h-1);g.setLineDash([])}
     else if(sp.kind==="trigless"){g.strokeStyle=ink;g.lineWidth=G.big?2:1.5;g.strokeRect(x0+1,y+.5,w-2,h-1)}
     else{const am=ampSteps(t),ea=sp.s+am,cA=ea<G.b?G.col[Math.max(G.a,Math.floor(ea))]:null;let xa=x1;if(ea<sp.e&&ea>=G.a&&cA)xa=cA.x0+(ea-Math.floor(ea))*(cA.x1-cA.x0);else if(ea<G.a)xa=x0;g.globalAlpha*=va;g.fillStyle=ink;g.fillRect(x0,y,Math.max(0,xa-x0),h);g.globalAlpha=audible(t)?1:.45;if(xa<x1){g.fillStyle=inkA(.3);g.fillRect(xa,y,x1-xa,h)}if(sp.kind==="part"&&G.big){g.fillStyle=cssv("--lcd");g.fillRect(x0+4,y+h/2-1,6,2)}}
     if(c0&&!sp.clipR&&G.big){g.fillStyle=cssv("--lcd");g.fillRect(x1-4,y+3,2,h-6)}}
    else if(midi){g.globalAlpha*=va;g.fillStyle=ink;g.fillRect(x0,y,w,h);g.globalAlpha=audible(t)?1:.45}else if(c0){const cw=c0.x1-c0.x0;g.strokeStyle=ink;g.lineWidth=G.big?1.5:1;g.strokeRect(c0.x0+.5,y+.5,cw-1,h-1);if(G.big){g.fillStyle=ink;g.beginPath();g.arc(c0.x0+cw/2,y+h/2,2.6,0,7);g.fill()}}});
   if(c0&&stepLocked(t,sp.s)){g.fillStyle=ink;g.fillRect(c0.x0+2,1,5,G.big?4:3)}
   if(G.big&&c0&&sp.n[0]>=G.lo&&sp.n[0]<=G.hi){const lab=tr.m==="DPRO-BBOX"?BBOX[sp.n[0]]:tr.m==="VO-6"?(()=>{const v=S.locks.get(lkKey(t,"SYN.4"))?.get(sp.s);return v!=null?CONS[v]:null})():null;if(lab){g.fillStyle=ink;g.font="8px Silkscreen, monospace";g.fillText(lab,c0.x0+2,Math.max(9,G.y(sp.n[0])-2))}}
   if(G.big&&midi&&c0&&S.locks.get(lkKey(t,"MID.1"))?.has(sp.s)){g.fillStyle=ink;g.font="8px Silkscreen, monospace";g.fillText("V"+velOf(t,sp.s),c0.x0+2,G.y(sp.n[0])-2)}
   if(G.arp){const tk=arpTicks(t,sp);if(tk.length){const ns=tk.map(q=>q.n),mn=Math.min(...ns),mx=Math.max(...ns),sy=H-6;tk.forEach(q=>{const s0=Math.floor(q.x),c1=G.col[s0];if(!c1)return;const cw=c1.x1-c1.x0,xx=c1.x0+(q.x-s0)*cw,yy=sy-(mx>mn?(q.n-mn)/(mx-mn):.5)*18;g.fillStyle=ink;g.fillRect(xx+1,yy-2,Math.max(2,q.w*cw-2),3)})}}});
  if(G.arp){g.fillStyle=inkA(.35);g.fillRect(0,G.H+2,G.lastX,1);g.fillStyle=ink;g.font="9px Silkscreen, monospace";g.fillText("ARP",G.lastX+10,H-10)}
  G.vs.forEach(s=>{if(!tr.steps[s]?.off||!G.col[s])return;const x=G.col[s].x0;g.fillStyle=ink;g.fillRect(x,0,2,H);if(G.big){g.font="8px Silkscreen, monospace";g.fillText("OFF",x+4,10)}});
  
  g.globalAlpha=1}};
function spanX(G,sp){const a=Math.max(sp.s,G.a),b=Math.min(sp.e,G.b);if(b<=a)return null;const c0=G.col[a],c1=G.col[b-1];if(!c0||!c1)return null;sp.clipR=sp.e>G.b;return[c0.x0,c1.x1]}
let rollDrag=null;
function laneHit(c,e){const G=laneGeom(c),r=c.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;let s=null;for(const k of G.vs){const q=G.col[k];if(q&&x>=q.x0-1.5&&x<=q.x1+1.5){s=k;break}}if(s==null)return null;
 const n=G.lo+G.rows-1-Math.floor((y-G.top)/G.rh),t=G.t;
 for(const sp of noteSpans(t)){const rr=spanX(G,sp);if(!rr)continue;for(let k=0;k<sp.n.length;k++){if(sp.n[k]!==n)continue;const c0=G.col[sp.s];const x1=k===0||isMidiT(t)?rr[1]:(c0?c0.x1:-1),x0=k===0||isMidiT(t)?rr[0]:(c0?c0.x0:-1);if(x>=x0&&x<=x1)return{s:sp.s,n,k,edge:k===0&&!sp.clipR&&x>x1-8,cell:s}}}
 return{s,n,k:-1,cell:s}}
function rollDown(c,e){const t=+c.dataset.t;if(t!==S.sel){select(t);return}const h=laneHit(c,e);if(!h)return;const tr=trk(t);
 if(h.k>=0&&e.altKey){const st=tr.steps[h.s];if(h.k>0||(st.n&&st.n.length>1))st.n.splice(h.k,1);else{tr.steps[h.s]=null;clearStepLocks(t,h.s)}structEdited();rerenderSeq();return}
 if(h.k<0&&e.altKey){const st=tr.steps[h.cell];if(!st||st.off){tr.steps[h.cell]=st?.off?null:{off:1};structEdited();rerenderSeq()}return}
 if(h.k>=0&&h.edge){rollDrag={mode:"len",s:h.s,c};return}
 if(h.k>=0){rollDrag={mode:"pitch",s:h.s,k:h.k,c};return}
 const st=tr.steps[h.cell];
 if(!st||st.off){tr.steps[h.cell]=note(h.n);rollDrag={mode:"pitch",s:h.cell,k:0,c,moved:true}}
 else if(e.shiftKey){if(!st.n)st.n=[h.n];else if(!st.n.includes(h.n))st.n.push(h.n);rollDrag={mode:"pitch",s:h.cell,k:st.n.length-1,c,moved:true}}
 else{if(!st.n)st.n=[h.n];else st.n[0]=h.n;rollDrag={mode:"pitch",s:h.cell,k:0,c,moved:true}}
 redraw()}
function rollMove(c,e){const t=+c.dataset.t;if(!rollDrag){if(t!==S.sel){c.style.cursor="pointer";return}const h=laneHit(c,e);c.style.cursor=h?.edge?"ew-resize":h?.k>=0?"ns-resize":"crosshair";return}
 c=rollDrag.c;const r=c.getBoundingClientRect(),G=laneGeom(c),tr=trk(S.sel),st=tr.steps[rollDrag.s];if(!st)return;
 if(rollDrag.mode==="pitch"){const n=clamp(G.lo+G.rows-1-Math.floor((e.clientY-r.top-G.top)/G.rh),0,127);if(st.n&&st.n[rollDrag.k]!==n){st.n[rollDrag.k]=n;rollDrag.moved=true;redraw()}}
 else{const x=e.clientX-r.left;let e2=rollDrag.s+1;for(const k of G.vs){if(k<=rollDrag.s)continue;const q=G.col[k];if(q&&x>(q.x0+q.x1)/2)e2=k+1}if(x>G.lastX)e2=G.b;e2=clamp(e2,rollDrag.s+1,S.len);if(e2===rollDrag.e)return;rollDrag.e=e2;rollDrag.moved=true;
  if(isMidiT(S.sel))setLock(S.sel,"MID.0",rollDrag.s,clamp((e2-rollDrag.s)*8,1,126));
  else{for(let k=rollDrag.s+1;k<S.len;k++){if(tr.steps[k]&&!tr.steps[k].off)break;if(tr.steps[k]?.off)tr.steps[k]=null}if(e2<S.len&&!tr.steps[e2])tr.steps[e2]={off:1}}
  redraw()}}
function rollUp(){const d=rollDrag;rollDrag=null;if(d?.moved){structEdited();rerenderSeq()}}
