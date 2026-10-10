
/* ===== Sequence (synth tracks) and MIDI SEQ (MIDI tracks) =====
   One object per step: a trig that carries its pitch (or chord), its NOTE OFF type,
   its envelope trig flags and its parameter locks. The overview shows every track;
   the note lane edits the selected track's trigs by pitch. Under it only the per-track
   SLIDE and SWING rows. The MIDI sequencer is its own workspace with the same editor. */
S.page=0;S.viewAll=true;S.follow=false;S.dock="locks";	/* P7: all steps by default; PAGE pages, ALL goes back */
const pages16=()=>Math.ceil(S.len/16);
function vis(){if(S.viewAll)return[0,S.len];S.page=Math.min(S.page,pages16()-1);return[S.page*16,Math.min(S.len,S.page*16+16)]}
function steps(){const[a,b]=vis();return Array.from({length:b-a},(_,k)=>a+k)}
/* ALL shows 64 steps in the width: columns may shrink below 18 px there (no horizontal scroll at 1280 px).
   One 50 px gutter on the LEFT of every row, as a DAW's piano roll: the roll's keys, the A F L / SLIDE / SWING
   names, the lock lane's scale and the dock panels' scale all sit in it, so the step columns line up. */
function cols(){return`50px repeat(${steps().length},minmax(${S.viewAll?0:18}px,1fr))`}
const gapC=s=>s%16===0&&s!==vis()[0]?"gap":"";
/* MD Editor Sequence layout: rail = tracks + SYNTH/MIDI + lock parameter; main = page keys, ruler,
   ONE piano roll for the selected track (the height of the MD's 16-row grid), ENV/SLIDE/SWING rows, lock lane. */
const ROLL_H=16*28+15*3;
function th(t){const tr=trk(t),polyOff=S.mode==="poly"&&t!==S.sel&&t<6;return`<div class="th ${t===S.sel?"sel":""} ${S.ws==="seq"&&S.marks?.has(t)?"marked":""} ${trackSel(t)} ${audible(t)?"":"off"} ${polyOff?"poly-off":""}" data-sel="${t}">
 <div class="threw"><div class="sw"></div><div class="n ${t<6?"":"fill"}">${t<6?t+1:"M"+(t-5)}</div><div class="nm${S.ws==="seq"&&S.gen?" tagged":""}" title="${tr.name}"><b>${t<6?tr.m.replace("SWAVE-","SW-"):"CH"+String(tr.ch).padStart(2,"0")}</b>${S.ws==="seq"&&S.gen?`<i class="gtag" title="${genTip(t)}">${genTag(genSpec(t))}</i>`:""}</div>
 <button class="ms m ${ARMED.has(t)?"prep":""}" ${ARMED.has(t)?`data-prep="${ARMED.get(t)?"X":"+"}"`:""} data-mute="${t}" aria-pressed="${tr.mute}" aria-label="Mute ${tLabel(t)}">M</button><button class="ms s" data-solo="${t}" aria-pressed="${tr.solo}" aria-label="Solo ${tLabel(t)}">S</button></div></div>`}
function pageKeys(){return`<span class="pagectl rh"><button class="pgkey" id="pgkey" ${pages16()<2?"disabled":""} title="Next page. Shift-click = previous. Keys [ and ].">Page</button><span class="pleds" aria-hidden="true">${[0,1,2,3].map(k=>`<span class="pl ${k<pages16()?"":"na"} ${!S.viewAll&&k===S.page?"cur":""}" data-plp="${k}"><i class="led"></i></span>`).join("")}</span><button class="ptog ${S.viewAll?"on":""}" id="pgall" aria-pressed="${S.viewAll}" title="Show all steps"><i class="led"></i>All</button><button class="ptog ${S.follow?"on":""}" id="pgfollow" aria-pressed="${S.follow}" title="Page follows the play position"><i class="led"></i>Fol</button></span>`}
function lockPicker(t){const midi=isMidiT(t),list=pagesOf(t),d=dockOf(t);if(!list.includes(S.lanePage))S.lanePage=list[0];
 return`<div class="railparams ${d==="locks"?"":"dockrail"}"><div class="rphead"><span class="cap">${DOCKN[d]}</span><button id="clearLane" class="iconkey" aria-label="Clear these locks" title="Clear ${pidLabel(t,S.lane)} locks"><svg viewBox="0 0 14 14" aria-hidden="true"><path d="M2 4h10M5.5 4V2.5h3V4M3.5 4l.7 8h5.6l.7-8M6 6.5v3.5M8 6.5v3.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg></button></div>
  <div class="lpspace"></div><div class="pagetabs" role="group" aria-label="DATA page">${midi?`<button data-lpage="MID" aria-pressed="true" style="grid-column:1/-1"><i class="led"></i>MIDI PAGE</button>`:PAGES.map(p=>{const n=trackLockPids(t).filter(x=>x.startsWith(p+".")).length;return`<button data-lpage="${p}" aria-pressed="${S.lanePage===p}" aria-label="${PAGEN[p]}${n?", "+n+" locked":""}"><i class="led ${n&&S.lanePage!==p?"g on":""}"></i>${p}</button>`}).join("")}</div>
  <div class="pkcol">${pnames(t,S.lanePage).map((nm,i)=>{if(!nm)return`<span class="pk empty"></span>`;const pid=S.lanePage+"."+i,n=S.locks.get(lkKey(t,pid))?.size||0;return`<button class="pk ${n?"has":""}" data-lane="${pid}" aria-pressed="${pid===S.lane}" title="${n?n+" locked step"+(n>1?"s":""):"No locks yet"}">${nm}${n?`<i>${n}</i>`:""}</button>`}).join("")}${Array.from({length:8-pnames(t,S.lanePage).length},()=>`<span class="pk empty"></span>`).join("")}</div>${d==="locks"?"":`<div class="dockkeys" id="dockkeys">${dockKeys(t,d)}</div>`}</div>`}
function sideSw(){return`<div class="sidesw" role="group" aria-label="Sequencer side"><button data-side="int" aria-pressed="${S.side!=="midi"}" title="The six synth tracks"><i class="led"></i>Synth</button><button data-side="midi" aria-pressed="${S.side==="midi"}" title="The six MIDI sequencer tracks (FUNCTION + TRIG SELECT on the machine)"><i class="led"></i>MIDI</button></div>`}
function renderRail(){const r=$("#rail"),withSide=S.ws==="seq"||S.ws==="sound";
 const anyMs=[...S.tracks,...S.midi].some(x=>x.mute||x.solo);
 r.innerHTML=`<div class="railhead">Track<button class="iconkey allon" id="allon" ${anyMs?"":"disabled"} title="Unmute and unsolo every track (0)">M/S off</button></div>`+side().map(th).join("")+(withSide?`<div style="margin-top:4px">${sideSw()}</div>`:"")+(S.ws==="seq"?lockPicker(S.sel):"");altLabels()}
function rowStatus(t){const tr=trk(t),arp=tr.arp.MODE>0,trn=tr.tr.TRACK-64;return[arp?"ARP":"",t<6&&tr.tr.SCALE?["","FIX","MAJ","MIN"][tr.tr.SCALE]:(trn?(trn>0?"+":"")+trn:"")].filter(Boolean).join(" ")}
const ROLL_TIP=t=>`${tLabel(t)} notes. ${S.rollDraw?`Draw (B): click adds a note of ${MmRoll.say(S.rollLen)}, drag sideways paints · up or down: pitch · shift-click adds a chord note`:`Select (B: draw): drag a box round notes · click a note selects it · drag the selection moves it (⌘-drag: a copy) · double-click adds a note · shift-click a note extends the selection`} · drag a note's end: its length (new notes get it too; a ${isMidiT(t)?"MIDI note's LEN, 6 a step":"NOTE OFF where it ends"}) · alt-click deletes a note (the one before keeps its length), or sets a NOTE OFF on an empty step · ⌘-click or ⌘-drag selects steps · right-click: the step menu · scroll = pitch${isMidiT(t)?"":" · the keys on the left play the note"}${(()=>{const a=trk(t).arp;return a.MODE&&a.SPD?` · light notes: what the arpeggiator plays${a.PLAY===4?" (random order: outlined, the pitches vary)":""}`:""})()}`;
/* ===== Values while live recording (as the MD Editor's): the firmware locks the step that plays when a value arrives (CC; a
   step without a trig gets a trigless lock); the plug-in names it (machine.desk.recLock: track, page.value, step) and
   the trig row's cell and the lock lane's bar show it until the read-back brings the real lock. ===== */
let recLockKey="";
function markRecLock(){$$(".lkpend").forEach(c=>c.classList.remove("lkpend"));const l=S.recLock;if(!l||S.ws!=="seq"||l.t!==S.sel)return;
 const c=document.querySelector(`#tlanes .tlane.env [data-s="${l.s}"]`);if(c)c.classList.add("lkpend");
 if(S.lane===l.pid){const b=document.querySelector(`#lane .lb[data-s="${l.s}"]`);if(b)b.classList.add("lkpend")}}
function setRecLock(l){S.recLock=l||null;const key=l?l.t+":"+l.pid+":"+l.s:"";
 if(key&&key!==recLockKey)toast(`Locks ${tLabel(l.t)} ${pidLabel(l.t,l.pid)} on step ${l.s+1}: the step that plays when the value arrives.`);
 recLockKey=key;setTimeout(markRecLock,20)}
function renderSeq(){const t=S.sel,tr=trk(t),midi=isMidiT(t);dockOf(t);
 const rows=[["env",midi?"VEL":"Env"],["sld","Slide"],["swg","Swing"]];
 let h=`<div class="scroll" id="seqscroll"><div class="mstack ${S.viewAll?"all":""}" id="seq"><div class="mrowg ruler" style="grid-template-columns:${cols()}"><div class="rul"></div>${steps().map(s=>`<div class="rul ${gapC(s)} ${rulSel(s)}" data-s="${s}">${s%4===0?s+1:""}</div>`).join("")}</div>
  <div class="nlane big"><canvas class="roll" data-ed="lane" data-t="${t}" data-big="1" title="${ROLL_TIP(t)}" aria-label="${ROLL_TIP(t)}"></canvas></div>
  <div class="tlanes" id="tlanes">${rows.map(([k,lab])=>{if(k==="env"&&midi)return`<div class="tlane env" style="grid-template-columns:${cols()}" title="Velocity per note (VEL, lockable)"><span class="tlab">VEL</span>${steps().map(s=>{const st=tr.steps[s];const v=st&&!st.off?velOf(t,s):null;return`<span class="tc envc ${gapC(s)} ${v==null?"na":""}">${v!=null?`<i class="velbar" style="--v:${v/127*100}%" title="VEL ${v}"></i>`:""}</span>`}).join("")}</div>`;
   return`<div class="tlane ${k}" style="grid-template-columns:${cols()}" title="${{env:"Which envelopes this trig fires: AMP (red), FILTER (yellow), LFO (green), the manual's trig tracks. No dots = trigless.",sld:"Slide: a locked value glides to its next lock",swg:"Swing: these steps come late by the pattern's swing amount"}[k]}"><span class="tlab">${k==="env"?`<small class="envlab">A F L</small>`:lab}</span>${steps().map(s=>{const st=tr.steps[s];
    if(k==="env"){const ok=st&&!st.off;return`<span class="tc envc ${gapC(s)} ${ok?"":"na"} ${cellSel(s)}" data-s="${s}">${ok?["a","f","l"].map(b=>`<button class="d ${b} ${st[b]?"on":""}" data-env="${b}" data-s="${s}" aria-pressed="${!!st[b]}" aria-label="${{a:"AMP",f:"FILTER",l:"LFO"}[b]} trig step ${s+1}"></button>`).join(""):""}</span>`}
    const on=k==="sld"?tr.slide.has(s):tr.swing.has(s);return`<button class="tc ${on?"on":""} ${gapC(s)} ${cellSel(s)}" data-tl="${k}" data-s="${s}" aria-pressed="${on}" aria-label="${lab} step ${s+1}"></button>`}).join("")}</div>`}).join("")}</div></div></div>
 ${genBarHtml()}
 <div class="lanewrap"><div class="lanetop">${S.dock==="locks"?`<span class="cap">Lock lane · ${tLabel(t)} ${midi?"CH"+String(tr.ch).padStart(2,"0"):tr.m} · <b id="lanename">${pidLabel(t,S.lane)}</b> <span class="lanescale" id="lanescale"></span></span>
  <span class="lockbudget" id="lockbudget"></span><span class="lanehelp" title="Draw across the bars to lock this parameter per step. Alt-drag erases. Shift-drag draws a ramp, a straight line from where you press to where you let go. The wheel over a step with a trig moves its lock (Shift: fine). Hatched steps have no trig, so they cannot hold a lock. Dashed line = kit value. A slide step glides to the next lock.">Draw to lock · ⇧ ramp · alt erases</span>`:(()=>{const[h,hl]=dockHelp(t,S.dock);return`<span class="cap">${DOCKN[S.dock]} · ${tLabel(t)} ${midi?"CH"+String(tr.ch).padStart(2,"0"):tr.m}</span><span class="lockbudget dkread" id="dkread">${dockRead(t,S.dock)}</span><span class="lanehelp dkhelp" title="${dkAttr(hl)}">${h}</span>`})()}${dockTabs(t)}</div>
  ${S.dock==="locks"?dockBody(t):`<div class="dockbody" id="dock">${dockBody(t)}</div>`}</div>`;
 $("#main").innerHTML=h;document.body.classList.toggle("dockother",S.dock!=="locks");fitLane();renderLane();syncScroll();syncControls();alignLock();syncLockBudget();markRecLock()}
/* as in the MD Editor: the rail's LOCK PARAMETER block starts on the line above the lock lane (the lane's top border, under the GEN bar) and ends at its bottom edge */
function alignLock(){const rp=$("#rail .railparams"),lt=$(".lanetop"),ls=$("#lanescroll")||$("#main>.lanewrap>.dockbody");if(!rp||!lt||!ls||S.ws!=="seq")return;
 const roll=$("#seq canvas.roll[data-big]"),tb=rp.querySelector(".pagetabs");
 const size=()=>{rp.style.marginTop="0px";rp.style.height="auto";const sf=$("#main>.lanewrap"),dy=(sf?sf.getBoundingClientRect().top:lt.getBoundingClientRect().top)-rp.getBoundingClientRect().top;
  rp.style.marginTop=Math.max(0,dy)+"px";rp.style.height=Math.max(0,ls.getBoundingClientRect().bottom-rp.getBoundingClientRect().top)+"px";titles()};
 /* the lane's title sits on the LOCK PARAMETER title's line */
 const titles=()=>{const a=rp.querySelector(".rphead .cap"),b=lt.querySelector(".cap");if(!a||!b)return;const c=e=>{const q=e.getBoundingClientRect();return q.top+q.height/2};lt.style.marginTop="0px";const d=c(a)-c(b);if(Math.abs(d)>.5)lt.style.marginTop=d+"px"};
 const gap=()=>tb?tb.getBoundingClientRect().top-ls.getBoundingClientRect().top:0;
 /* the lock lane is as tall as the LOCK PARAMETER keys; the space above them goes to the piano roll, which pushes the lane down */
 ls.style.marginTop="0px";if(roll)roll.style.removeProperty("height");size();
 const g=Math.min(gap(),ls.getBoundingClientRect().height-140);if(roll&&g>1){roll.style.setProperty("height",(roll.getBoundingClientRect().height+g)+"px","important");size();
  /* the header's title alignment can move the body by a pixel or two: settle it on the keys' line */
  for(let i=0;i<3;i++){const e=gap();if(Math.abs(e)<.5||e>0&&ls.getBoundingClientRect().height-e<140)break;roll.style.setProperty("height",(roll.getBoundingClientRect().height+e)+"px","important");size()}redraw()}
 const r=gap();if(r>1)ls.style.marginTop=r+"px";
 /* every tab: the panel's keys cover the LOCK PARAMETER keys area exactly, the body's top and bottom lines */
 const dk=rp.querySelector(".dockkeys");if(dk&&tb){dk.style.top=(tb.getBoundingClientRect().top-rp.getBoundingClientRect().top-rp.clientTop)+"px";dk.style.bottom=(rp.getBoundingClientRect().bottom-ls.getBoundingClientRect().bottom)+"px"}}
/* the panel under the steps: the lock lane, or the track's arpeggiator, transpose and trig setup (MIDI tracks: the MIDI page) */
const DOCKN={locks:"Lock parameter",arp:"Arpeggiator",trn:"Transpose",trig:"Trig setup",midi:"MIDI page"};
/* the fourth tab is the kit's: TRIG SETUP on a synth track, the MIDI PAGE on a MIDI track (it stays the fourth when the track changes) */
function dockOf(t){const midi=isMidiT(t);if(S.dock==="midipage")S.dock="midi";if(!DOCKN[S.dock])S.dock="locks";if(S.dock==="midi"&&!midi)S.dock="trig";if(S.dock==="trig"&&midi)S.dock="midi";return S.dock}
function dockTabs(t){const midi=isMidiT(t),tabs=[["locks","Locks"],["arp","Arp"],["trn","Transpose"],midi?["midi","MIDI page"]:["trig","Trig setup"]];
 return`<span class="docktabs" role="tablist">${tabs.map(([k,n])=>`<button role="tab" class="lkey" data-dock="${k}" aria-selected="${S.dock===k}"><i class="led"></i>${n}</button>`).join("")}</span>`}
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
 lane.innerHTML=`<div class="lbscale"><b>${fmt(m,mx)}</b><span>${fmt(m,base)}<small>kit</small></span><b>${fmt(m,0)}</b></div>`+steps().map(s=>{const st=tr.steps[s],on=st&&!st.off,v=lm?.get(s);
  return`<div class="lb ${on?"":"none"} ${on?"k-"+(isMidiT(t)?"full":stepKind(st)):""} ${gapC(s)} " data-s="${s}" title="${on?(v!=null?"Locked "+fmt(m,v):"Kit value "+fmt(m,base)):"No trig here: add one (a trigless trig with alt-click keeps the envelopes quiet)"}">${on?`${baseHTML(base,mx)}${m.signed?`<div class="mid"></div>`:""}${v!=null?barHTML(v):""}`:""}</div>`}).join("");
 requestAnimationFrame(drawSlides)}
function drawSlides(){const svg=$("#lanesvg"),lane=$("#lane");if(!svg||!lane)return;const t=S.sel,tr=trk(t),lm=S.locks.get(lkKey(t,S.lane));svg.innerHTML="";if(!lm)return;
 const[pg,i]=S.lane.split("."),mx=maxOf(meta(t,pg,+i)),box=lane.getBoundingClientRect(),ink=cssv("--ink");let d="";const locked=[...lm.keys()].sort((a,b)=>a-b);
 locked.forEach(s=>{if(!tr.slide.has(s))return;const nx=locked.find(x=>x>s);if(nx==null)return;const a=lane.querySelector(`.lb[data-s="${s}"]`),b=lane.querySelector(`.lb[data-s="${nx}"]`);if(!a)return;
  const ra=a.getBoundingClientRect(),y=v=>ra.bottom-box.top-6-v/mx*(ra.height-14),xa=ra.left-box.left+ra.width/2;
  const xb=b?b.getBoundingClientRect().left-box.left+b.getBoundingClientRect().width/2:box.width;d+=`<path d="M${xa} ${y(lm.get(s))} L${xb} ${y(lm.get(nx))}" fill="none" stroke="${ink}" stroke-width="2.5" stroke-dasharray="6 3"/><circle cx="${xa}" cy="${y(lm.get(s))}" r="4" fill="${ink}"/>`});
 svg.setAttribute("width",box.width);svg.setAttribute("height",box.height);svg.innerHTML=d}
function syncScroll(){const a=$("#seqscroll"),b=$("#lanescroll");if(!a||!b)return;a.onscroll=()=>{b.scrollLeft=a.scrollLeft};b.onscroll=()=>{a.scrollLeft=b.scrollLeft}}
function refreshRow(t){redraw();return;const gate={},am=ampSteps(t);noteSpans(t).forEach(sp=>{for(let k=sp.s;k<sp.e;k++)gate[k]=(k===sp.s?(sp.e-sp.s>1?"gs":"g1"):k===sp.e-1?"ge":"gm")+(k-sp.s>=am?" gd":"")});$$(`.mst[data-t="${t}"]`).forEach(b=>{const s=+b.dataset.s,st=trk(t).steps[s];b.className=stepCls(t,s);b.setAttribute("aria-pressed",!!st);b.title=stepTitle(t,s,st);b.innerHTML=`<span class="nt">${stepText(t,s,st)}</span>${gate[s]?`<span class="gt ${gate[s]}"></span>`:""}`})}
function rerenderSeq(){const sl=$("#seqscroll")?.scrollLeft||0;renderRail();renderSeq();const sc=$("#seqscroll");if(sc){sc.scrollLeft=sl;const l=$("#lanescroll");if(l)l.scrollLeft=sl}enhanceSelects($("#main"));movePH(false)}
function clickStep(t,s,e){const tr=trk(t),st=tr.steps[s];
 if(e.shiftKey){tr.steps[s]=st?.off?null:{off:1};if(!st?.off)clearStepLocks(t,s)}
 else if(e.altKey&&!isMidiT(t)){if(!st||st.off)tr.steps[s]={n:[lastNote(t,s)],a:0,f:0,l:0};else{const k=stepKind(st);Object.assign(st,k==="full"?{a:0,f:0,l:0}:{a:1,f:1,l:1})}}
 else{if(st&&!st.off){tr.steps[s]=null;clearStepLocks(t,s)}else tr.steps[s]=note(lastNote(t,s))}
 editStep(t,s);renderTop();if(t!==S.sel){S.sel=t;autoRange(t);render()}else rerenderSeq()}
function clickTl(k,s){const tr=trk(S.sel),set=k==="sld"?tr.slide:tr.swing,on=!set.has(s);on?set.add(s):set.delete(s);edit(k==="sld"?"slide":"swingStep",{t:S.sel,s,on});rerenderSeq()}
function clickEnv(b,s){const t=S.sel,st=trk(t).steps[s];if(!st||st.off)return;st[b]=st[b]?0:1;if(!st.n&&!st.a&&!st.f&&!st.l){trk(t).steps[s]=null;clearStepLocks(t,s)}editStep(t,s);rerenderSeq()}
let laneDraw=null;
function laneAt(e){const lane=$("#lane");if(!lane)return;if(laneDraw.ramp){rampAt(e);return}const el=document.elementFromPoint(e.clientX,e.clientY)?.closest(".lb");if(!el||!lane.contains(el))return;
 const s=+el.dataset.s,t=S.sel,st=trk(t).steps[s];if(!st||st.off)return;const[pg,i]=S.lane.split("."),mx=maxOf(meta(t,pg,+i));const r=el.getBoundingClientRect();const v=clamp(Math.round((r.bottom-6-e.clientY)/(r.height-14)*mx),0,mx);
 if(laneDraw.erase){const m=S.locks.get(lkKey(t,S.lane));if(!m?.has(s))return;m.delete(s);if(!m.size)S.locks.delete(lkKey(t,S.lane));edit("lock",{t,...pidArgs(S.lane),s,v:null})}
 else{if(S.locks.get(lkKey(t,S.lane))?.get(s)===v)return;if(!setLock(t,S.lane,s,v))return;edit("lock",{t,...pidArgs(S.lane),s,v})}
 el.querySelector("i")?.remove();if(!laneDraw.erase)el.insertAdjacentHTML("beforeend",barHTML(v));
 laneDraw.touched=true;renderTop();drawSlides()}
function endLaneDraw(){if(!laneDraw)return;const d=laneDraw;if(d.ramp){rampSend();laneDraw=null;renderTop();rerenderSeq();return}laneDraw=null;if(d.touched)rerenderSeq()}

/* ===== the note lane: the selected track's trigs by pitch, same columns as the overview ===== */
const ROWS=24;
S.rollLoT={};
function laneGeom(c){const t=+c.dataset.t,big=!!c.dataset.big,tr=trk(t),cr=c.getBoundingClientRect(),arp=big&&tr.arp.MODE>0&&tr.arp.SPD>0,H=cr.height;const col={};
 document.querySelectorAll("#seq .ruler .rul[data-s]").forEach(b=>{const r=b.getBoundingClientRect();col[+b.dataset.s]={x0:r.left-cr.left,x1:r.right-cr.left}});const vs=steps();const lastX=vs.length&&col[vs[vs.length-1]]?col[vs[vs.length-1]].x1:0,firstX=vs.length&&col[vs[0]]?col[vs[0]].x0:0;
 let lo,rows;if(big){lo=S.rollLoT[t]??S.rollLo;rows=ROWS}else{const ns=tr.steps.slice(0,S.len).flatMap(x=>x?.n||[]);const mn=ns.length?Math.min(...ns):60,mx=ns.length?Math.max(...ns):60;rows=Math.max(6,Math.min(24,mx-mn+3));lo=Math.round((mn+mx)/2)-Math.floor(rows/2)}
 const top=big?4:2,rh=(H-top-(big?4:2))/rows;
 return{t,big,col,vs,a:vs[0],b:vs[vs.length-1]+1,top,rh,rows,firstX,lastX,kw:Math.max(0,firstX-3),arp,H,lo,hi:lo+rows-1,y:n=>top+(lo+rows-1-n)*rh}}
function inScale(tr,n){if(!tr.tr||tr.tr.SCALE<2)return true;const sc=tr.tr.SCALE===2?MAJ:MIN;return sc.includes(((n-tr.tr.KEY)%12+12)%12)}
function velOf(t,s){return velOf2(t,s)}
function velOf2(t,s){return S.locks.get(lkKey(t,"MID.1"))?.get(s)??trk(t).v.MID[1]}
ED.lane={draw(g,W,H,c){const G=laneGeom(c),t=G.t,tr=trk(t),ink=cssv("--ink");if(!G.vs.length)return;const midi=isMidiT(t);if(G.big){const tp=ROLL_TIP(t);if(c.title!==tp){c.title=tp;c.setAttribute("aria-label",tp)}}
  if(!audible(t))g.globalAlpha=.45;
  /* the keys sit in the rows' left gutter, as a DAW's piano roll: white keys the gutter's width, black keys from
     its left edge; C and the top row are named on the key's right end. The hovered row is lit, the key held lit more */
  if(G.big){const X0=G.firstX,KW=G.kw,BW=Math.round(KW*.55),led=cssv("--led"),hov=S.rollHov?.t===t?S.rollHov.n:null,held=rollDrag?.mode==="key"?rollDrag.n:null;
   for(let n=G.lo;n<=G.hi;n++){const y=G.y(n),blk=[1,3,6,8,10].includes(n%12);if(blk){g.fillStyle=inkA(.05);g.fillRect(X0,y,G.lastX-X0,G.rh)}
   if(!inScale(tr,n)){g.fillStyle=inkA(.07);for(let x=X0;x<G.lastX;x+=6)g.fillRect(x,y,3,G.rh)}
   if(n%12===0){g.fillStyle=inkA(.3);g.fillRect(X0,y+G.rh-.5,G.lastX-X0,1)}
   if(n===hov||n===held){g.fillStyle=inkA(n===held?.16:.09);g.fillRect(X0,y,G.lastX-X0,G.rh)}
   g.fillStyle=inkA(.12);g.fillRect(0,y+.5,KW,G.rh-1);if(blk){g.fillStyle=ink;g.fillRect(0,y+.5,BW,G.rh-1)}
   if(n===hov||n===held){g.save();g.globalAlpha=n===held?.85:.45;g.fillStyle=led;g.fillRect(0,y+.5,blk?BW:KW,G.rh-1);g.restore()}
   if(n%12===0||n===G.hi){g.fillStyle=ink;g.font="9px Silkscreen, monospace";g.textAlign="right";g.fillText(noteName(n),KW-3,y+G.rh/2+3.5);g.textAlign="left"}}}
  else{g.fillStyle=ink;g.font="8px Silkscreen, monospace";const ns=tr.steps.slice(0,S.len).flatMap(x=>x?.n||[]);if(ns.length){const mn=Math.min(...ns),mx=Math.max(...ns);g.fillText(mn===mx?noteName(mn).replace("-",""):noteName(mn).replace("-","")+"-"+noteName(mx).replace("-",""),G.lastX+4,11)}const st=rowStatus(t);if(st)g.fillText(st,G.lastX+4,H-5)}
  G.vs.forEach(s=>{const x=G.col[s];if(!x)return;if(s%4===0){g.fillStyle=inkA(G.big?.1:.07);g.fillRect(x.x0,0,x.x1-x.x0,G.H)}});
  if(G.big)drawSel(g,G,t);
  if(G.big&&S.ghost)side().forEach(o=>{if(o===t||(o<6&&(isFx(trk(o).m)||trk(o).m==="DPRO-BBOX")))return;noteSpans(o).forEach(sp=>{const r=spanX(G,sp);if(!r||sp.n[0]<G.lo||sp.n[0]>G.hi)return;g.fillStyle=inkA(.14);g.fillRect(r[0]+2,G.y(sp.n[0])+3,r[1]-r[0]-4,G.rh-6)})});
  /* the arpeggiator's output as ghost notes at their real pitch, under the trigs: lighter and thinner, never hit-tested. PLAY RND is not predictable: outlined, hatched */
  if(G.arp){const rnd=tr.arp.PLAY===4,gh=Math.max(3,Math.round(G.rh*.5));noteSpans(t).forEach(sp=>arpTicks(t,sp).forEach(q=>{if(q.n<G.lo||q.n>G.hi||q.x<G.a||q.x>=G.b)return;const s0=Math.floor(q.x),c1=G.col[s0];if(!c1)return;const cw=c1.x1-c1.x0,xx=c1.x0+(q.x-s0)*cw+1,ww=Math.max(2,q.w*cw-2),yy=G.y(q.n)+(G.rh-gh)/2;
   if(rnd){g.strokeStyle=inkA(.45);g.lineWidth=1;g.strokeRect(xx+.5,yy+.5,ww-1,gh-1);g.fillStyle=inkA(.22);for(let k=2;k<ww-1;k+=4)g.fillRect(xx+k,yy+1,1,gh-2)}else{g.fillStyle=inkA(.3);g.fillRect(xx,yy,ww,gh)}}))}
  noteSpans(t).forEach(sp=>{const r=spanX(G,sp);if(!r)return;const[x0,x1]=r,w=x1-x0,c0=G.col[sp.s];const va=midi?.4+.6*velOf(t,sp.s)/127:1;
   sp.n.forEach((n,k)=>{if(n<G.lo||n>G.hi)return;const y=G.y(n)+(G.big?1:.5),h=G.big?G.rh-2:Math.max(3,G.rh-1);
    if(k===0){if(sp.pitchless){g.setLineDash([4,3]);g.strokeStyle=ink;g.lineWidth=1.5;g.strokeRect(x0+.5,y+.5,w-1,h-1);g.setLineDash([])}
     else if(sp.kind==="trigless"){g.strokeStyle=ink;g.lineWidth=G.big?2:1.5;g.strokeRect(x0+1,y+.5,w-2,h-1)}
     else{const am=ampSteps(t),ea=sp.s+am,cA=ea<G.b?G.col[Math.max(G.a,Math.floor(ea))]:null;let xa=x1;if(ea<sp.e&&ea>=G.a&&cA)xa=cA.x0+(ea-Math.floor(ea))*(cA.x1-cA.x0);else if(ea<G.a)xa=x0;g.globalAlpha*=va;g.fillStyle=ink;g.fillRect(x0,y,Math.max(0,xa-x0),h);g.globalAlpha=audible(t)?1:.45;if(xa<x1){g.fillStyle=inkA(.3);g.fillRect(xa,y,x1-xa,h)}if(sp.kind==="part"&&G.big){g.fillStyle=cssv("--lcd");g.fillRect(x0+4,y+h/2-1,6,2)}}
     if(c0&&!sp.clipR&&G.big&&!sp.wrap){g.fillStyle=cssv("--lcd");g.fillRect(x1-4,y+3,2,h-6)}
     /* a gate past the pattern's end goes on at its start (I-010): the same bar, lighter, its end the handle */
     const wr=sp.wrap?spanX(G,{s:0,e:sp.wrap}):null;if(wr){g.globalAlpha*=va*.55;g.fillStyle=ink;g.fillRect(wr[0],y,wr[1]-wr[0],h);g.globalAlpha=audible(t)?1:.45;if(G.big){g.fillStyle=cssv("--lcd");g.fillRect(wr[1]-4,y+3,2,h-6)}}}
    else if(midi){g.globalAlpha*=va;g.fillStyle=ink;g.fillRect(x0,y,w,h);g.globalAlpha=audible(t)?1:.45}else if(c0){const cw=c0.x1-c0.x0;g.strokeStyle=ink;g.lineWidth=G.big?1.5:1;g.strokeRect(c0.x0+.5,y+.5,cw-1,h-1);if(G.big){g.fillStyle=ink;g.beginPath();g.arc(c0.x0+cw/2,y+h/2,2.6,0,7);g.fill()}}});
   if(c0&&stepLocked(t,sp.s)){g.fillStyle=ink;g.fillRect(c0.x0+2,1,5,G.big?4:3)}
   if(G.big&&c0&&sp.n[0]>=G.lo&&sp.n[0]<=G.hi){const lab=tr.m==="DPRO-BBOX"?BBOX[sp.n[0]]:tr.m==="VO-6"?(()=>{const v=S.locks.get(lkKey(t,"SYN.4"))?.get(sp.s);return v!=null?CONS[v]:null})():null;if(lab){g.fillStyle=ink;g.font="8px Silkscreen, monospace";g.fillText(lab,c0.x0+2,Math.max(9,G.y(sp.n[0])-2))}}
   if(G.big&&midi&&c0&&S.locks.get(lkKey(t,"MID.1"))?.has(sp.s)){g.fillStyle=ink;g.font="8px Silkscreen, monospace";g.fillText("V"+velOf(t,sp.s),c0.x0+2,G.y(sp.n[0])-2)}});
  /* NOTE OFFs: one that ends a note is where its bar ends (a faint line); one that ends nothing is marked */
  G.vs.forEach(s=>{if(!tr.steps[s]?.off||!G.col[s])return;const x=G.col[s].x0,ends=MmRoll.before(tr.steps,S.len,s)!=null;g.fillStyle=ends?inkA(.25):ink;g.fillRect(x,0,ends?1:2,H);if(G.big&&!ends){g.font="8px Silkscreen, monospace";g.fillText("OFF",x+4,10)}});
  if(G.big&&S.rollBox&&S.rollBox.t===t){const b=S.rollBox,a0=Math.max(b.s0,G.a),a1=Math.min(b.s1,G.b-1),c0=G.col[a0],c1=G.col[a1];if(c0&&c1&&a1>=a0){const y0=G.y(Math.min(G.hi,b.n1)),y1=G.y(Math.max(G.lo,b.n0))+G.rh;g.save();g.fillStyle=inkA(.08);g.fillRect(c0.x0,y0,c1.x1-c0.x0,y1-y0);g.strokeStyle=ink;g.lineWidth=1.5;g.setLineDash([4,3]);g.strokeRect(c0.x0+.5,y0+.5,c1.x1-c0.x0-1,y1-y0-1);g.restore()}}
  
  g.globalAlpha=1}};
/* a span's x range in the roll ([x0, x1]), or null when it is not in the steps shown; a MIDI note's end can fall inside a step (LEN ticks) */
function spanX(G,sp){const a=Math.max(sp.s,G.a),b=Math.min(sp.e,G.b);if(b<=a)return null;const bi=Math.ceil(b)-1,c0=G.col[a],c1=G.col[bi];if(!c0||!c1)return null;sp.clipR=sp.e>G.b||!!sp.wrap;return[c0.x0,c1.x0+(b-bi)*(c1.x1-c1.x0)]}
let rollDrag=null;
/* a bar's end that drags its length: 8 px, at most a third of the bar (a narrow 1/16 note keeps a body to press) */
const endZone=(x0,x1)=>Math.min(8,(x1-x0)/3);
function laneHit(c,e){const G=laneGeom(c),r=c.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;let s=null;for(const k of G.vs){const q=G.col[k];if(q&&x>=q.x0-1.5&&x<=q.x1+1.5){s=k;break}}if(s==null)return null;
 const n=G.lo+G.rows-1-Math.floor((y-G.top)/G.rh),t=G.t;
 for(const sp of noteSpans(t)){const rr=spanX(G,sp);if(rr)for(let k=0;k<sp.n.length;k++){if(sp.n[k]!==n)continue;const c0=G.col[sp.s];const x1=k===0||isMidiT(t)?rr[1]:(c0?c0.x1:-1),x0=k===0||isMidiT(t)?rr[0]:(c0?c0.x0:-1);if(x>=x0&&x<=x1)return{s:sp.s,n,k,edge:k===0&&!sp.clipR&&x>x1-endZone(x0,x1),cell:s}}
  /* the part of a gate past the pattern's end, at its start: its end sets the length */
  const wr=sp.wrap&&sp.n[0]===n?spanX(G,{s:0,e:sp.wrap}):null;if(wr&&x>=wr[0]&&x<=wr[1]&&x>wr[1]-endZone(wr[0],wr[1]))return{s:sp.s,n,k:0,edge:true,wrap:true,cell:s}}
 return{s,n,k:-1,cell:s}}
/* the row under the pointer (hover light) and a press on the keys: play that note on the track, as the home-row keys do */
S.rollHov=null;
const rollRow=(c,G,e)=>{const r=c.getBoundingClientRect(),n=G.lo+G.rows-1-Math.floor((e.clientY-r.top-G.top)/G.rh);return n>=G.lo&&n<=G.hi?n:null};
const onKeys=(c,G,e)=>e.clientX-c.getBoundingClientRect().left<G.firstX-1.5;
function rollKey(t,n){rollDrag.n=n;if(n!=null)keyNote(t,n,KB.vel)}
document.addEventListener("pointerout",e=>{const c=e.target.closest?.("canvas.roll");if(c&&!c.contains(e.relatedTarget)&&S.rollHov){S.rollHov=null;drawEd(c)}});
/* ===== Draw or select, and note lengths (I-007, I-010; 58-roll.js has what the machine does with a length) =====
   Draw (B, or the Draw key over the steps): a press on an empty step adds a note that lasts the draw length (the Len
   key; a NOTE OFF where it ends, or a MIDI note's LEN), a drag sideways paints. Off (select): a drag in an empty place
   boxes notes (the selection both editors share, StepSel: the steps from the first note's trig to the last one's end,
   its NOTE OFF too), a press on a note selects it, a drag of the selection moves it (⌘-drag: a copy, 77-select.js),
   a double-click adds a note. Both: a note's end drags its length (the draw length follows), up or down its pitch,
   alt-click deletes it (the note before it keeps its length). Prefs per viewer (localStorage); draw by default. */
S.rollDraw=true;S.rollLen=1;S.rollBox=null;
(()=>{let v=null;try{v=JSON.parse(localStorage.getItem("mmeditor.roll")||"null")}catch(_){}if(v&&typeof v==="object"){S.rollDraw=v.draw!==false;if(v.len>=1&&v.len<=64)S.rollLen=Math.round(v.len)}})();
function rollPrefs(){try{localStorage.setItem("mmeditor.roll",JSON.stringify({draw:S.rollDraw,len:S.rollLen}))}catch(_){}}
function rollKeysHtml(){return`<span class="rollkeys"><button class="ptog ${S.rollDraw?"on":""}" id="rolldraw" aria-pressed="${S.rollDraw}" title="Draw notes in the piano roll (B). Off: drag a box round notes to select them, drag them to move"><i class="led"></i>Draw</button><button class="pgkey rolllen" id="rolllen" title="The length a new note gets: click for the next, shift-click the one before (dragging a note's end sets it too)">${MmRoll.say(S.rollLen)}</button></span>`}
function syncRollKeys(){const d=$("#rolldraw"),l=$("#rolllen");if(d){d.classList.toggle("on",S.rollDraw);d.setAttribute("aria-pressed",S.rollDraw)}if(l)l.textContent=MmRoll.say(S.rollLen);const c=$("#seq canvas.roll[data-big]");if(c){const tp=ROLL_TIP(+c.dataset.t);c.title=tp;c.setAttribute("aria-label",tp)}}
function setRollDraw(on){S.rollDraw=!!on;S.rollBox=null;rollPrefs();syncRollKeys();redraw();toast(S.rollDraw?`Draw: a click adds a note of ${MmRoll.say(S.rollLen)} (B: select).`:"Select: drag a box round notes, drag them to move, double-click adds a note (B: draw).")}
function setRollLen(L,say){L=clamp(Math.round(L),1,64);if(L!==S.rollLen){S.rollLen=L;rollPrefs();syncRollKeys()}if(say)toast(`New notes: ${MmRoll.say(L)}.`)}
/* the Len key: the next (shift: the one before) of 1/16 1/8 1/4 1/2 1 bar, from where it is */
function stepRollLen(back){const Ls=MmRoll.LENGTHS,n=back?[...Ls].reverse().find(x=>x<S.rollLen)??Ls[Ls.length-1]:Ls.find(x=>x>S.rollLen)??Ls[0];setRollLen(n,true)}
Keys.bind({id:"roll-draw",short:"Draw",scope:"seq",keys:["B"],group:"Sequence",does:"Piano roll: draw notes on / off (off: drag a box round notes to select them, drag them to move, double-click adds one). ⇧B taps the tempo here, B on the other workspaces",when:()=>seqKeys()&&kbOn(),run:()=>setRollDraw(!S.rollDraw)});
Keys.bind({id:"roll-box",scope:"seq",area:"Roll",keys:["drag in an empty place"],group:"Sequence",does:"Select (Draw off): box notes; the selection takes them from the first trig to the last one's end. Drag the selection: move it"});
Keys.bind({id:"roll-length",scope:"seq",area:"Roll",keys:["drag a note's end"],group:"Sequence",does:"The note's length (a NOTE OFF where it ends; a MIDI note's LEN, 6 a step); new notes get it too. Up to the track's next trig, past the pattern's end onto its start"});
const midiOf=t=>isMidiT(t)?{kitLen:trk(t).v.MID[0]}:null;
/* changes ([step, value]) into the view; the steps changed */
function rollPut(t,changes){const tr=trk(t);for(const[k,v] of changes){tr.steps[k]=v;if(!v||v.off){tr.slide.delete(k);clearStepLocks(t,k)}}return changes.map(([k])=>k)}
/* the note on s lasts L steps (MmRoll.setLen): its NOTE OFFs and, on a MIDI track, its LEN lock. The steps changed,
   whether a lock did, the length it got (at most to the next trig) and the NOTE OFF it put down (-1: none) */
function rollLen(t,s,L){const r=MmRoll.setLen(trk(t).steps,S.len,s,L,midiOf(t)),off=(r.steps.find(([,v])=>v&&v.off)||[-1])[0],touched=rollPut(t,r.steps);let lock=false;
 if(isMidiT(t)){const key=lkKey(t,"MID.0"),m=S.locks.get(key),had=m?.get(s);if(r.len==null){if(had!=null){m.delete(s);if(!m.size)S.locks.delete(key);lock=true}}else if(had!==r.len&&setLock(t,"MID.0",s,r.len))lock=true}
 return{touched,lock,L:r.L,off}}
/* a note removed as a person means it: the note before it keeps its length (MmRoll.remove) */
function rollRemove(t,s){return rollPut(t,MmRoll.remove(trk(t).steps,S.len,s,isMidiT(t)?k=>midiLenOf(t,k):null))}
/* a new note on step s at pitch n that lasts the draw length */
function rollAdd(t,s,n){trk(t).steps[s]=note(n);const r=rollLen(t,s,S.rollLen);return{touched:new Set([s,...r.touched]),lock:r.lock,off:r.off}}
function rollDown(c,e){if(e.button!==0)return;const t=+c.dataset.t;if(t!==S.sel){select(t);return}
 {const G=laneGeom(c);if(e.button===0&&onKeys(c,G,e)){const n=rollRow(c,G,e);if(n==null)return;if(isMidiT(t)){kbTell("midi","The keys play the synth tracks: a MIDI track's notes go to the MIDI OUT only.");return}rollDrag={mode:"key",t,c};rollKey(t,n);drawEd(c);return}}
 const h=laneHit(c,e);if(!h)return;const tr=trk(t);
 if(e.metaKey||e.ctrlKey)return;	/* ⌘ selects (77-select.js), Ctrl on a Mac is the step menu's; the fill is in the step menu (K7) */
 if(h.k>=0&&e.altKey){const st=tr.steps[h.s];let touched;if(h.k>0||(st.n&&st.n.length>1)){st.n.splice(h.k,1);touched=[h.s]}else touched=rollRemove(t,h.s);rollDrag={mode:"erase",s:h.s,c,last:h.s,touched:new Set(touched)};redraw();return}
 if(h.k<0&&e.altKey){const st=tr.steps[h.cell];if(!st||st.off){tr.steps[h.cell]=st?.off?null:{off:1};editStep(t,h.cell);rerenderSeq()}return}
 if(h.k>=0&&h.edge){rollDrag={mode:"len",s:h.s,c,wrap:!!h.wrap,touched:new Set([h.s]),lock:false,L:null};return}
 if(!S.rollDraw){rollSelectDown(c,e,h);return}
 if(h.k>=0){rollDrag={mode:"pitch",s:h.s,k:h.k,c};return}
 const st=tr.steps[h.cell];
 if(!st||st.off){const a=rollAdd(t,h.cell,h.n);rollDrag={mode:"pitch",s:h.cell,k:0,c,moved:true,paint:true,touched:a.touched,lock:a.lock,paintOff:a.off}}
 else if(e.shiftKey){if(!st.n)st.n=[h.n];else if(!st.n.includes(h.n))st.n.push(h.n);rollDrag={mode:"pitch",s:h.cell,k:st.n.length-1,c,moved:true}}
 else{if(!st.n)st.n=[h.n];else st.n[0]=h.n;rollDrag={mode:"pitch",s:h.cell,k:0,c,moved:true}}
 redraw()}
/* select mode: a press in an empty place starts a box (a double-click adds a note); on a note it selects the note (shift:
   the selection grows to it), and what the drag does next decides: across the steps it moves the selection, up or
   down the note's pitch */
function rollSelectDown(c,e,h){const t=S.sel,tr=trk(t);
 if(h.k<0){rollDrag={mode:"box",c,t,a:{s:h.cell,n:h.n},moved:false};S.rollBox={t,s0:h.cell,s1:h.cell,n0:h.n,n1:h.n};redraw();return}
 const sp=MmRoll.span(tr.steps,S.len,h.s,isMidiT(t)?{len:midiLenOf(t,h.s)}:null),x=S.stepSel;
 if(e.shiftKey&&x&&x.n===1&&x.t===t){setSel({t,n:1,from:Math.min(x.from,sp.from),to:Math.max(x.to,sp.to)});return}
 const inside=inSel(t,h.s);if(!inside)setSel({t,n:1,from:sp.from,to:sp.to});
 rollDrag={mode:"note",c,s:h.s,k:h.k,cell0:h.cell,n0:h.n,moved:false}}
/* select mode: a double-click in an empty place adds a note (the draw length), as a DAW's piano roll does */
document.addEventListener("dblclick",e=>{const c=e.target.closest?.("#seq canvas.roll[data-big]");if(!c||S.rollDraw||S.rec||+c.dataset.t!==S.sel)return;const h=laneHit(c,e);if(!h||h.k>=0||onKeys(c,laneGeom(c),e))return;
 const t=S.sel,a=rollAdd(t,h.cell,h.n);editSpan(t,a.touched,a.lock);renderTop();rerenderSeq();toast(`Added ${noteName(h.n)} on step ${h.cell+1}, ${MmRoll.say(Math.min(S.rollLen,MmRoll.room(trk(t).steps,S.len,h.cell)))}.`)});
/* the box's notes: every note with a pitch in its rows and a gate in its steps (the part at the pattern's start too) */
function rollBoxed(t,b){const out=[];for(const sp of noteSpans(t)){if(!sp.n.some(n=>n>=b.n0&&n<=b.n1))continue;if(sp.s<=b.s1&&sp.e-1>=b.s0||sp.wrap&&b.s0<sp.wrap)out.push(sp.s)}return out}
function rollBoxEnd(d){const b=S.rollBox,t=S.sel;S.rollBox=null;if(!b){redraw();return}
 if(!d.moved){clearSel();redraw();return}
 const ss=rollBoxed(t,b),tr=trk(t);
 if(!ss.length){setSel({t,n:1,from:b.s0,to:b.s1+1});toast(Modifiers.say(`No note in the box: selected ${selSay(S.stepSel)}.`));return}
 const spans=ss.map(s=>MmRoll.span(tr.steps,S.len,s,isMidiT(t)?{len:midiLenOf(t,s)}:null));
 setSel({t,n:1,from:Math.min(...spans.map(x=>x.from)),to:Math.max(...spans.map(x=>x.to))});
 toast(Modifiers.say(`Selected ${ss.length} note${ss.length>1?"s":""}, ${selSay(S.stepSel)} · drag to move · ⌘C copy · ⌘X cut · ⌘D duplicate · Delete`))}
/* the length a drag of a note's end makes, in steps from its trig: the column the pointer is past the middle of; on
   the part of a gate at the pattern's start, a step there is a step past the end; past the last column shown, a
   column's width a step more */
function rollLenAt(G,d,x){let e2=null;for(const k of G.vs){const q=G.col[k];if(q&&x>(q.x0+q.x1)/2)e2=k+1}if(e2==null)e2=G.a;
 if(d.wrap)e2+=S.len;else if(x>G.lastX){const q=G.col[G.b-1],cw=q?Math.max(4,q.x1-q.x0):16;e2=G.b+Math.floor((x-G.lastX)/cw)}
 return Math.max(1,e2-d.s)}
function rollMove(c,e){const t=+c.dataset.t;if(!rollDrag){if(t!==S.sel){c.style.cursor="pointer";return}const G=laneGeom(c),n=rollRow(c,G,e),key=onKeys(c,G,e);
  if((S.rollHov?.n??null)!==n||S.rollHov?.t!==t){S.rollHov=n==null?null:{t,n};drawEd(c)}
  if(key){c.style.cursor=n==null?"default":"pointer";return}const h=laneHit(c,e);c.style.cursor=h?.edge?"ew-resize":h?.k>=0?(S.rollDraw?"ns-resize":"move"):(S.rollDraw?"crosshair":"default");return}
 if(rollDrag.mode==="key"){const d=rollDrag,n=rollRow(d.c,laneGeom(d.c),e);if(n!==d.n){if(d.n!=null)keyNote(d.t,d.n,0);rollKey(d.t,n);S.rollHov=n==null?null:{t:d.t,n};drawEd(d.c)}return}
 c=rollDrag.c;const d=rollDrag,G=laneGeom(c),r=c.getBoundingClientRect();
 if(d.mode==="box"){const h=laneHit(c,e),n=clamp(G.lo+G.rows-1-Math.floor((e.clientY-r.top-G.top)/G.rh),G.lo,G.hi),s=h?h.cell:e.clientX-r.left>G.lastX?G.b-1:G.a;
  const b={t:d.t,s0:Math.min(d.a.s,s),s1:Math.max(d.a.s,s),n0:Math.min(d.a.n,n),n1:Math.max(d.a.n,n)};if(JSON.stringify(b)!==JSON.stringify(S.rollBox)){S.rollBox=b;d.moved=true;redraw()}return}
 if(d.mode==="note"||d.mode==="move"){const h=laneHit(c,e),cell=h?h.cell:d.cell0,n=clamp(G.lo+G.rows-1-Math.floor((e.clientY-r.top-G.top)/G.rh),0,127);
  if(d.mode==="note"){if(cell!==d.cell0){d.mode="move"}else if(n!==d.n0&&d.k>=0){Object.assign(d,{mode:"pitch"});rollMove(c,e);return}else return}
  const x=S.stepSel;if(!x)return;const ghost=StepSel.dropAt(x,{from:{t:x.t,s:d.cell0},at:{t:x.t,s:cell}},S.len,SEL_TRACKS);d.ghost=ghost.from!==x.from?ghost:null;syncSel(d.ghost);return}
 if(rollPaintMove(c,e))return;const tr=trk(S.sel),st=tr.steps[d.s];if(!st)return;
 if(d.mode==="pitch"){const n=clamp(G.lo+G.rows-1-Math.floor((e.clientY-r.top-G.top)/G.rh),0,127);if(st.n&&st.n[d.k]!==n){st.n[d.k]=n;d.moved=true;redraw()}}
 else if(d.mode==="len"){const L=rollLenAt(G,d,e.clientX-r.left);if(L===d.want)return;d.want=L;const q=rollLen(S.sel,d.s,L);q.touched.forEach(k=>d.touched.add(k));d.lock=d.lock||q.lock;d.L=q.L;d.moved=true;redraw()}}
/* Paint (the Machinedrum Editor's drag across steps, MM-PORT-PLAN.md 2026-10-05): a new note dragged sideways out of its
   column paints a note at its pitch on every empty step the pointer crosses, the last one lasting the draw length
   (the NOTE OFF the drag put down moves with it); Alt-press on a note deletes it, and the drag goes on deleting every
   note it crosses (its slide and locks; the note before keeps its length). The steps: mmPaint (52-gen.js) and
   MmRoll. Nothing is sent before the release (an intent shows the view again, which would draw the roll anew under
   the pointer): then one steps intent (editSpan), one undo step. The columns between two moves count too. */
function rollPaintMove(c,e){const d=rollDrag;if(d.mode==="pitch"&&!d.paint)return false;if(d.mode!=="pitch"&&d.mode!=="paint"&&d.mode!=="erase")return false;
 const s=laneHit(c,e)?.cell;if(s==null||s===d.last||s>=S.len)return d.mode!=="pitch";
 const t=S.sel,tr=trk(t);if(d.mode==="pitch"){const st=tr.steps[d.s];if(!st?.n)return false;Object.assign(d,{mode:"paint",n:st.n[0],last:d.s,painted:new Set([d.s])})}
 if(d.mode==="erase"){const a=Math.min(d.last,s),b=Math.max(d.last,s);for(let k=a;k<=b;k++)if(tr.steps[k]&&!tr.steps[k].off)rollRemove(t,k).forEach(x=>d.touched.add(x))}
 else{if(d.paintOff>=0&&tr.steps[d.paintOff]?.off){tr.steps[d.paintOff]=null;d.touched.add(d.paintOff)}
  for(const[k,x] of mmPaint(tr.steps,d.last,s,"paint",d.n)){tr.steps[k]=x;d.touched.add(k);d.painted.add(k)}
  const q=rollLen(t,Math.max(...d.painted),S.rollLen);q.touched.forEach(k=>d.touched.add(k));d.lock=d.lock||q.lock;d.paintOff=q.off}
 d.last=s;redraw();return true}
/* the steps a drag changed, as one steps intent over their span (one per step would not do: each intent shows the
   view again from the documents and its own writes, so the later steps' changes were gone before they were sent);
   locks: the span's locks too (a MIDI note's LEN, the locks of the steps a note left) */
function editSpan(t,touched,locks){const ss=[...touched];if(!ss.length)return;const a=Math.min(...ss),b=Math.max(...ss)+1;edit("steps",{from:a,to:b,rows:[rangeRow(t,a,b,locks)]})}
/* the drag's edit, when it ends: the dragged step's notes, or its length (a synth track's NOTE OFFs, a MIDI track's
   LEN lock), or the steps a paint or an erase touched, or the box's selection, or the selection moved */
function rollUp(){const d=rollDrag;rollDrag=null;if(d?.mode==="key"){if(d.n!=null)keyNote(d.t,d.n,0);drawEd(d.c);return}const t=S.sel;
 if(d?.mode==="box"){rollBoxEnd(d);return}
 if(d?.mode==="note"){toast(Modifiers.say(`Selected ${selSay(S.stepSel)} · drag to move · ⌘C copy · ⌘X cut · ⌘D duplicate · Delete`));return}
 if(d?.mode==="move"){syncSel();if(d.ghost)selMoveTo(d.ghost.from);return}
 if(d?.mode==="paint"||d?.mode==="erase"){editSpan(t,d.touched,true);const n=d.mode==="paint"?d.painted.size:d.touched.size;if(n>1)toast(`${d.mode==="paint"?"Painted":"Erased"} ${n} ${d.mode==="paint"?"notes":"steps"} of ${tLabel(t)}${d.mode==="paint"?" with "+noteName(d.n):""} (one undo step).`);renderTop();rerenderSeq();return}
 if(!d?.moved)return;
 if(d.mode==="len"){editSpan(t,d.touched,d.lock);if(d.L){setRollLen(d.L);toast(`${tLabel(t)} step ${d.s+1}: ${MmRoll.say(d.L)}${d.L<d.want?" (the next trig ends it)":""}; new notes get it too.`)}rerenderSeq();return}
 if(d.touched)editSpan(t,d.touched,d.lock);else editStep(t,d.s);
 rerenderSeq()}
