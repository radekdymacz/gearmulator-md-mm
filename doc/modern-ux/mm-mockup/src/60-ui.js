
/* ===== Feedback: toast, TX lamp, dialogs ===== */
let toastT;function toast(m){const e=$("#toast");e.textContent=m;e.classList.add("on");clearTimeout(toastT);toastT=setTimeout(()=>e.classList.remove("on"),3000)}
let txT;function tx(){const l=$("#txled");if(!l)return;l.classList.add("on");clearTimeout(txT);txT=setTimeout(()=>l.classList.remove("on"),70)}
function ask(html,btns,cls=""){const d=$("#dlg");d.innerHTML=`<div class="dlgbox ${cls}" role="alertdialog" aria-modal="true">${html.startsWith("<")?html:`<p>${html}</p>`}<div class="btnrow">${btns.map(([t,c],i)=>`<button class="${c}" data-dlg="${i}">${t}</button>`).join("")}</div></div>`;d.hidden=false;d._btns=btns;d.querySelector("button")?.focus()}

/* ===== Honest machine state =====
   Sound edits go out as CCs at once (Appendix B) but stay unsaved in the kit.
   Pattern, kit and song DUMPS are different on the Monomachine: it only takes them on
   GLOBAL > FILE > SYSEX RECV while the screen says WAITING (P1-RESULT, GAP-REVIEW 3.1).
   EMU: the app drives that screen itself (the emulator can press the panel).
   HW: edits queue up until you open the screen and press Send. */
let pstT;
function structEdited(){tx();if(HOST.edited){HOST.edited("struct");return}if(S.engine==="emu"){S.patSent="recv";renderPst();clearTimeout(pstT);pstT=setTimeout(()=>{S.patSent="live";renderPst()},650)}else{S.pend++;S.patSent="pend";renderPst()}}
function soundEdited(){tx();setKitState("edited");if(HOST.edited)HOST.edited("sound")}
function renderPst(){if(HOST.renderPst)return HOST.renderPst();const p=$("#pst");if(!p)return;if(S.engine==="hw"&&S.pend){p.textContent="SEND "+S.pend;p.className="pst warn";p.title="Unsent pattern and song edits. Click to send them."}
 else if(S.patSent==="recv"){p.textContent="RECV";p.className="pst";p.title="The emulator is on SYSEX RECV and takes the dump."}else{p.textContent="";p.className="pst"}}
function sendDialog(){ask(`<div class="lcdbig recv">SYSEX RECV · WAITING…</div><p>The Monomachine only accepts a dump on its SysEx receive screen. <b>${S.pend}</b> edit${S.pend===1?"":"s"} to send.</p>
 <ol class="recvsteps"><li>On the Monomachine press <b>FUNCTION + KIT/SONG</b> (GLOBAL), then <b>FILE › SYSEX RECV</b>.</li><li>Set <b>MODE ORIG</b> and press <b>YES</b>. The screen shows <b>WAITING…</b></li><li>Press <b>Send</b> here. Then press <b>EXIT</b> on the machine.</li></ol>`,
 [["Send now","cream",()=>{S.pend=0;S.patSent="live";renderPst();tx();toast("Sent. The pattern and song slots now match the editor. Press EXIT on the Monomachine.")}],["Later","",()=>{}]],"first")}
function setKitState(st){S.kitState=st;const s=$("#save");if(!s)return;s.classList.toggle("dirty",st==="edited");s.lastElementChild.textContent=st==="edited"?"edited":"saved";s.title=st==="edited"?"Kit edits are not saved on the machine. They are kept in the DAW project.":"The kit matches its saved slot on the machine."}
function saveKit(){if(HOST.kit)return HOST.kit("save",S.kit);S.kits[S.kit]={name:S.workName,empty:false,data:captureKit()};setKitState("clean");tx();toast("Saved "+kitName(S.kit)+" on the machine (SAVE KIT). The overwritten kit went to the UNDO KIT slot.")}
function goPattern(p,now){p=(p+128)%128;if(p===S.pat&&S.queued==null)return;const kitChange=S.patKit[p]!==S.kit,go=now?()=>switchNow(p):()=>queuePattern(p);
 if(kitChange&&S.kitState==="edited"){ask(`<b>${patName(p)}</b> uses kit <b>${kitName(S.patKit[p])}</b>. Your edits to <b>${kitName(S.kit)}</b> are not saved. The Monomachine keeps them in its UNDO KIT slot, but only until the next unsaved switch.`,
  [["Save kit, then switch","cream",()=>{saveKit();go()}],["Switch (edits to UNDO KIT)","danger",go],["Cancel","",()=>{}]]);return}go()}
function switchNow(p){if(HOST.selectPattern)return HOST.selectPattern(p,true);const was=S.playing;S.queued=null;applyPattern(p);if(was){S.step=-1;toast("Switched now. On the machine this is STOP, LOAD PATTERN, PLAY (not tested).")}}
function queuePattern(p){if(HOST.selectPattern)return HOST.selectPattern(p,false);if(S.playing){S.queued=p;renderTop();tx();return}applyPattern(p)}
function applyPattern(p){const kc=S.patKit[p]!==S.kit;
 if(p!==S.pat){S.patData[S.pat]=capturePat();S.patInfo[S.pat]={has:curHas(),len:S.len};S.pat=p;applyPat(S.patData[p]||emptyPat(S.patInfo[p].len))}
 S.queued=null;if(kc){S.kit=S.patKit[p];applyKit(kitData(S.kit));S.workName=S.kits[S.kit].name;setKitState("clean");toast("Loaded "+kitName(S.kit)+" with "+patName(p)+".")}autoRange(S.sel);render();H.last=snap();
 const pf=$(".lcdpanel .patf");if(pf){pf.classList.remove("flash");void pf.offsetWidth;pf.classList.add("flash");setTimeout(()=>pf.classList.remove("flash"),500)}const ls=document.querySelector("#libpop .ps.cur");if(ls){ls.classList.remove("flash");void ls.offsetWidth;ls.classList.add("flash")}drawLib()}

/* ===== Top bar ===== */
function renderTop(){
 $$("#tabs button").forEach(b=>b.setAttribute("aria-selected",b.dataset.ws===S.ws));
 const lkk=$("#learnkey");lkk.setAttribute("aria-pressed",S.learn);lkk.classList.toggle("on",!!S.learn);
 $("#platekey span").textContent=S.plate==="mk1"?"MKI":"MKII";
 const n=S.locks.size,m=$("#meter");$("#lockn").textContent=String(n).padStart(2,"0")+"/62";m.className="f meter"+(n>=62?" full":n>=52?" warn":"");
 $("#bpm").textContent=S.bpm.toFixed(1);$("#pat").textContent=patName(S.queued??S.pat);$("#pat").parentElement.classList.toggle("queued",S.queued!=null);
 $("#kitname").textContent=kitName(S.kit);const hc=HOST.history?HOST.history():{undo:H.undo.length,redo:H.redo.length};$("#undo").disabled=!hc.undo;$("#redo").disabled=!hc.redo;$("#undon").textContent=hc.undo||"";$("#redon").textContent=hc.redo||"";
 $("#play").setAttribute("aria-pressed",S.playing);$("#playico").textContent=S.playing?"■":"▶";$("#play").setAttribute("aria-label",S.playing?"Stop":"Play");$("#rec").setAttribute("aria-pressed",!!S.rec);$("#recled").classList.toggle("on",!!S.rec);
 renderPst()}
/* line 2: fixed-width slots (as in the MD Editor v46), so values never push into COPY / CLR / PASTE */
function L2(k,lab,val,title,ed,w=8,id=""){return`<span class="l2 ${ed?"ed":""}" style="width:${w}ch" ${k?`data-l2="${k}"`:""} ${title?`title="${title}"`:""}><small>${lab}</small><b ${id?`id="${id}"`:""}>${val}</b></span>`}
function renderSub(){const t=S.sel,tr=trk(t);let h="";
 if(S.ws==="seq")h=L2("side","SEQ",S.side==="midi"?"MIDI":"SYNTH","SYNTH or MIDI: which six tracks the rail and the roll show. Click to switch.",1,10)+L2("len","LEN",S.len,"Pattern length 2-64 steps, shared by all tracks. Click +1 page, shift-click −1 page, scroll = one step.",1,6)+L2("mult","SPD",S.mult,"Tempo multiplier: 1X, 2X, 3/4X, 3/2X. Click to step.",1,8)+L2("swing","SWG",S.swingAmt+"%","Swing 50-80 %, one amount per pattern. Drag or scroll.",1,7);
 else if(S.ws==="sound")h=L2("","TRK",tLabel(t),"",0,6)+L2("","",isMidiT(t)?"MIDI CH"+String(tr.ch).padStart(2,"0"):tr.m,isMidiT(t)?"LFOs are shared with T"+(t-5):machName(tr.m),0,11);
 else if(S.ws==="mix")h=L2("route","ROUTE",S.routing.replace("3xSTEREO+AB=MIX","3xST+AB=MIX"),"Global routing: 3xSTEREO, 3xSTEREO+AB=MIX or 6xMONO. Click to step.",1,17)+L2("","IN","A B","",0,6);
 else if(S.ws==="perform")h=L2("pmode","MODE",{normal:"AUTO",multi:"MULTI",map:"MAP",poly:"POLY"}[S.mode],"Keyboard mode: auto track, multi trig, multi map or poly. Click to step.",1,10)+L2("","",S.mode==="multi"?["ALL TRK","SPLIT","SEQ STRT","SEQ TRNS"][S.multi.mode]:"","",0,9)+L2("","CH",{normal:"09",multi:"07",map:"08",poly:"09"}[S.mode],"MIDI channel of this keyboard mode","",5);
 else if(S.ws==="control")h=L2("","IN","CH1","Controller input channel",0,6)+L2("","CC OUT","0/s","CCs sent to the machine per second",0,11,"ccrate")+L2("","MAX","300/s","The editor thins CC output above this rate",0,10);
 else h=L2("","SONG","01","",0,7)+L2("","ROWS",S.song.length,"",0,7)+L2("","BARS",Math.round(songSteps()/16),"",0,7)+L2("","TIME",songTime(),"",0,8);
 $("#lcd2").innerHTML=h}

/* ===== Controls: one key-style value control for everything ===== */
function ref(el){const d=el.dataset,t=d.t!=null?+d.t:S.sel,tr=trk(t),g=d.g;
 if(PAGES.includes(g)||g==="MID")return[tr.v[g],+d.n,meta(t,g,+d.n),t,g];
 switch(g){case"lev":return[tr,"lev",{name:"LEV",max:127},t,g];case"menv":return[S.menv,d.n,{name:d.n,max:127},t,g];
  case"arp":return[tr.arp,d.n,{name:d.n,...{SPD:{max:127},RNGE:{en:["1 OCT","2 OCT","3 OCT","4 OCT"]},OJMP:{max:15}}[d.n]},t,g];
  case"trn":return[tr.tr,"TRACK",{name:"TRACK",max:127,signed:1},t,g];case"ptrn":return[S,"patTrn",{name:"PAT",max:127,signed:1},t,g];
  case"key":return[tr.tr,"KEY",{name:"KEY",en:KEYS},t,g];
  case"asg":{const r=tr.assign.tabs[S.asTab][+d.n];return[r,"add",{name:"ADD",max:127,signed:1},t,g]}
  case"link":return[S.ctl.links[+d.li],d.n,{name:d.n,max:127},t,g];case"src":return[srcById(d.src),d.n,{name:d.n,max:d.n==="DEPTH"?100:127},t,g];
  case"cc":return[tr.cc,+d.n,{name:"CC"+(+d.n+1),en:[...Array.from({length:128},(_,k)=>"CC"+k),"AFT"]},t,g];
  case"mmap":{const r=S.mmap[+d.i];return[r,d.n,{name:d.n,...{trn:{max:127,signed:1},ofs:{en:["---",...Array.from({length:64},(_,i)=>String(i).padStart(2,"0"))]},len:{max:64},tim:{en:["DIR","1","2","4","8","16","32"]}}[d.n]},t,g]}}}
const getV=el=>{const[o,n]=ref(el);return o[n]};
function setV(el,v){const[o,n,m,t,g]=ref(el);v=clamp(Math.round(v),0,maxOf(m));if(o[n]===v)return;o[n]=v;
 if(PAGES.includes(g)||g==="MID"||g==="lev"||g==="menv"||g==="asg"||g==="cc")soundEdited();else structEdited();
 if(g.startsWith("LF")&&(+el.dataset.n<2)){if(+el.dataset.n===0)o[1]=0;render();return}
 syncControls();redraw()}
function pc(g,n,{t,label,cls="",extra=""}={}){if(n==null)return`<div class="pc empty" aria-hidden="true"></div>`;
 return`<div class="pc ${cls}" role="slider" tabindex="0" data-g="${g}" data-n="${n}"${t!=null?` data-t="${t}"`:""} ${extra} aria-label="${label||n}"><span>${label||n}</span><b></b></div>`}
function page8(t,pg){const names=pnames(t,pg);return Array.from({length:8},(_,i)=>names[i]?pc(pg,i,{t,label:names[i]}):pc(0,null)).join("")}
function modBy(t,pg,i){const out=[];if(isMidiT(t))return out;const tr=trk(t);["LF1","LF2","LF3"].forEach((l,k)=>{const v=tr.v[l];if(v[7]>0&&LPAGES[v[0]]===pg&&v[1]===i)out.push(k+1)});return out}
function syncControls(){
 $$("#main [data-g]").forEach(el=>{if(!el.classList.contains("pc")&&!el.classList.contains("fader"))return;const r=ref(el);if(!r)return;const[o,n,m,t,g]=r,v=o[n];
  const mx=maxOf(m);el.style.setProperty("--f",(v==null?0:v/mx*100)+"%");el.setAttribute("aria-valuenow",v);el.setAttribute("aria-valuemax",mx);
  if(m.signed&&el.classList.contains("pc")){const q=v/mx*100;el.classList.add("bip");el.style.setProperty("--pl",Math.min(q,50.4)+"%");el.style.setProperty("--pw",Math.max(1.5,Math.abs(q-50.4))+"%")}
  const b=el.querySelector("b");if(b)b.textContent=fmt(m,v);el.setAttribute("aria-valuetext",fmt(m,v));
  if(PAGES.includes(g)||g==="MID"){const pid=g+"."+n;el.classList.toggle("lk",S.locks.has(lkKey(t,pid)));const mb=modBy(t,g,+n);let md=el.querySelector(".mod");
   if(mb.length){if(!md){md=document.createElement("i");md.className="mod";el.appendChild(md)}md.textContent="~"+mb.join("");el.title=`Modulated by LFO ${mb.join(" + ")}`}else if(md)md.remove();
   const mp=S.ctl.links.filter(l=>l.t===t&&l.pid===pid);el.classList.toggle("mapped",mp.length>0)}});
 $$("#main [data-show]").forEach(el=>{const t=+el.dataset.show;el.textContent=trk(t).lev})}

/* ===== Key-style dropdowns (from the MD Editor) ===== */
let kFor=null;
function enhanceSelects(root){root.querySelectorAll("select").forEach(sel=>{if(sel.dataset.k)return;sel.dataset.k="1";sel.hidden=true;
 const b=document.createElement("button");b.className="kselbtn";b.type="button";b.dataset.for=sel.id;b.setAttribute("aria-haspopup","listbox");b.setAttribute("aria-expanded","false");
 const lab=sel.closest("label");b.setAttribute("aria-label",(lab?lab.firstChild.textContent.trim()+": ":(sel.getAttribute("aria-label")||"")+" ")+sel.selectedOptions[0]?.text);
 if(sel.disabled)b.disabled=true;b.innerHTML=`<span>${sel.selectedOptions[0]?.text??""}</span><svg viewBox="0 0 10 6" aria-hidden="true"><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>`;sel.after(b)})}
function openK(btn){const sel=document.getElementById(btn.dataset.for);kFor=btn;const pop=$("#kpop");let n=0,h="";
 for(const node of sel.children){if(node.tagName==="OPTGROUP"){h+=`<div class="kgrp">${node.label}</div>`;for(const o of node.children){h+=kopt(o,sel);n++}}else{h+=kopt(node,sel);n++}}
 const cols=n>24?4:n>18?3:n>9?2:1;pop.innerHTML=`<div class="klist" style="grid-template-columns:repeat(${cols},minmax(0,1fr))">${h}</div>`;
 pop.hidden=false;pop.style.minWidth=Math.max(btn.offsetWidth,cols*110)+"px";const r=btn.getBoundingClientRect();
 let top=r.bottom+scrollY+4;if(r.bottom+pop.offsetHeight+8>innerHeight&&r.top>pop.offsetHeight+8)top=r.top+scrollY-pop.offsetHeight-4;
 pop.style.top=top+"px";pop.style.left=Math.max(16,Math.min(r.left+scrollX,innerWidth-pop.offsetWidth-16))+"px";btn.setAttribute("aria-expanded","true");
 (pop.querySelector(".kopt[aria-selected=true]")||pop.querySelector(".kopt"))?.focus()}
function kopt(o,sel){if(o.hidden)return"";return`<button class="kopt" role="option" data-v="${o.value}" aria-selected="${o.value===sel.value}" ${o.disabled?"disabled":""}>${o.text}</button>`}
function closeK(){const pop=$("#kpop");if(pop.hidden)return;pop.hidden=true;kFor?.setAttribute("aria-expanded","false")}
document.addEventListener("click",e=>{const b=e.target.closest(".kselbtn");if(b){const same=kFor===b&&!$("#kpop").hidden;closeK();if(!same)openK(b);return}
 const o=e.target.closest("#kpop .kopt");if(o&&kFor){const sel=document.getElementById(kFor.dataset.for);sel.value=o.dataset.v;kFor.querySelector("span").textContent=sel.selectedOptions[0].text;closeK();kFor.focus();sel.dispatchEvent(new Event("change",{bubbles:true}));return}
 if(!e.target.closest("#kpop"))closeK()},true);
document.addEventListener("keydown",e=>{if($("#kpop").hidden)return;if(e.key==="Escape"){closeK();kFor?.focus();return}
 const opts=[...document.querySelectorAll("#kpop .kopt")],i=opts.indexOf(document.activeElement),d={ArrowDown:1,ArrowRight:1,ArrowUp:-1,ArrowLeft:-1}[e.key];if(d&&opts.length){e.preventDefault();opts[(i+d+opts.length)%opts.length].focus()}});
const opt=(v,t,sel)=>`<option value="${v}" ${String(v)===String(sel)?"selected":""}>${t}</option>`;

/* ===== Canvas screens (from the MD Editor): draw + drag handles ===== */
const cssv=v=>getComputedStyle(document.documentElement).getPropertyValue(v).trim();
function inkA(a){const h=cssv("--ink").replace("#","");const n=parseInt(h.length===3?h.split("").map(c=>c+c).join(""):h,16);return`rgba(${n>>16&255},${n>>8&255},${n&255},${a})`}
function grid(g,W,H){g.strokeStyle=inkA(0.13);g.lineWidth=1;for(let i=1;i<4;i++){g.beginPath();g.moveTo(0,Math.round(H*i/4)+.5);g.lineTo(W,Math.round(H*i/4)+.5);g.stroke()}for(let i=1;i<8;i++){g.beginPath();g.moveTo(Math.round(W*i/8)+.5,0);g.lineTo(Math.round(W*i/8)+.5,H);g.stroke()}}
function line(g,W,fy,c,w,dash){g.strokeStyle=c;g.lineWidth=w;g.setLineDash(dash||[]);g.beginPath();for(let x=0;x<=W;x+=1){const y=fy(x);x?g.lineTo(x,y):g.moveTo(x,y)}g.stroke();g.setLineDash([])}
function label(g,t,x=8,y=15){g.fillStyle=cssv("--ink");g.font="10px Silkscreen, monospace";g.fillText(String(t).toUpperCase(),x,y)}
const ED={};let raf=0,active=null;
function redraw(){if(raf)return;raf=requestAnimationFrame(()=>{raf=0;$$("canvas.ed,canvas.roll").forEach(drawEd)})}
function drawEd(c){const ed=ED[c.dataset.ed];if(!ed)return;const dpr=devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;if(!W)return;
 if(c.width!==Math.round(W*dpr)||c.height!==Math.round(H*dpr)){c.width=Math.round(W*dpr);c.height=Math.round(H*dpr)}
 const g=c.getContext("2d");g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);ed.draw(g,W,H,c);
 (ed.handles?.(W,H,c)||[]).forEach(h=>{const on=active&&active.c===c&&active.k===h.k;g.beginPath();g.arc(h.x,clamp(h.y,6,H-6),on?7:5.5,0,7);g.fillStyle=cssv("--lcd");g.fill();g.lineWidth=2.5;g.strokeStyle=cssv("--ink");g.stroke();
  if(on){g.fillStyle=cssv("--ink");g.font="10px Silkscreen, monospace";g.fillText(h.k,Math.min(W-60,h.x+10),Math.max(14,h.y-10))}})}
function nearest(c,e){const ed=ED[c.dataset.ed];if(!ed.handles)return null;const r=c.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;let best=null,bd=16;ed.handles(r.width,r.height,c).forEach(h=>{const d=Math.hypot(h.x-x,clamp(h.y,6,r.height-6)-y);if(d<bd){bd=d;best=h}});return best}
const RND=[.35,-.7,.9,-.25,.55,-.9,.1,.7];
/* LFO shapes in firmware order: TRI ITRI SAW ISAW SQR ISQR EXP IEXP RMP IRMP RND */
function lshape(w,x){const b=w>=10?5:w>>1,inv=w<10&&w%2===1;const v=[1-4*Math.abs(x-.5),1-2*x,x<.5?1:-1,2*Math.exp(-4*x)-1,2*x-1,RND[Math.floor(x*8)%8]][b];return inv?-v:v}
function shapeIcon(w){const pts=Array.from({length:25},(_,k)=>{const x=k/24;return[(x*20+1).toFixed(1),(9-6*lshape(w,x)).toFixed(1)]});return`<svg viewBox="0 0 22 18" aria-hidden="true"><polyline fill="none" stroke="currentColor" stroke-width="1.6" points="${pts.map(p=>p.join(",")).join(" ")}"/></svg>`}
