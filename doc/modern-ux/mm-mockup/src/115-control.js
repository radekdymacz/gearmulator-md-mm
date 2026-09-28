
/* ===== Control (from the MD Editor): CC mapping matrix, targets with curves, app-only LFO / Random =====
   Targets are any value on the 7 DATA pages of a synth track, or the MIDI page of a MIDI track.
   The editor sends each target's CC on the track channel (Appendix B: SYN 48-55, AMP 56-63, FLT 72-79,
   EFX 80-87, LFO1 88-95, LFO2 104-111, LFO3 112-119). Locks are not involved. */
S.ctl={sel:"cc21",selT:null,sent:0,addT:0,phase:0,
 sources:[...Array.from({length:8},(_,k)=>({id:"cc"+(21+k),kind:"cc",cc:21+k,label:"Knob "+(k+1),val:[64,40,0,0,0,0,0,0][k]})),
  {id:"lfoA",kind:"lfo",label:"LFO A",val:64,SHAPE:0,RATE:"1/2",DEPTH:100},{id:"rndA",kind:"rnd",label:"Random A",val:64,RATE:"1/16",SMOOTH:30,_t:64}],
 links:[{src:"cc21",t:0,pid:"FLT.1",min:20,max:110,curve:"lin",inv:false},{src:"cc21",t:2,pid:"FLT.0",min:0,max:80,curve:"exp",inv:false},
  {src:"cc22",t:2,pid:"EFX.4",min:64,max:127,curve:"lin",inv:false},{src:"cc22",t:3,pid:"SYN.0",min:10,max:127,curve:"log",inv:false},
  {src:"lfoA",t:2,pid:"AMP.6",min:30,max:98,curve:"lin",inv:false},{src:"rndA",t:7,pid:"MID.4",min:0,max:127,curve:"lin",inv:false}]};
const RATES={"1/16":1,"1/8":2,"1/4":4,"1/2":8,"1":16,"2":32,"4":64};
const srcById=id=>S.ctl.sources.find(x=>x.id===id);
function curveF(c,x){return c==="exp"?x*x:c==="log"?Math.sqrt(x):x}
function ccOf(t,pid){const[pg,i]=pid.split(".");if(pg==="MID")return"NRPN "+(0x38+ +i);return"CC "+(CCBASE[pg]+ +i)}
function applySrc(src){S.ctl.links.filter(l=>l.src===src.id).forEach(l=>{if(!pname(l.t,l.pid))return;let x=src.val/127;if(l.inv)x=1-x;x=curveF(l.curve,x);const[pg,i]=l.pid.split(".");const m=meta(l.t,pg,+i);trk(l.t).v[pg][+i]=clamp(Math.round((l.min+(l.max-l.min)*x)/127*maxOf(m)),0,maxOf(m));S.ctl.sent++})}
let ctlRaf=0;function ctlRefresh(){if(ctlRaf)return;ctlRaf=requestAnimationFrame(()=>{ctlRaf=0;syncControls();$$(".srch").forEach(h=>{const s2=srcById(h.dataset.src);h.style.setProperty("--f",s2.val/127*100+"%");h.querySelector(".sv").textContent=s2.val})})}
setInterval(()=>{const r=$("#ccrate");if(r){r.textContent=`${S.ctl.sent}/s`;r.classList.toggle("hot",S.ctl.sent>300)}S.ctl.sent=0},1000);
function ctlTick(){S.ctl.phase++;S.ctl.sources.forEach(sr=>{const n=RATES[sr.RATE]||8;
 if(sr.kind==="lfo"){const ph=(S.ctl.phase%n)/n;sr.val=clamp(Math.round(64+lshape(sr.SHAPE,ph)*63*sr.DEPTH/100));applySrc(sr)}
 if(sr.kind==="rnd"){if(S.ctl.phase%n===0)sr._t=Math.round(Math.random()*127);const a=1-sr.SMOOTH/140;sr.val=clamp(Math.round(sr.val+(sr._t-sr.val)*a));applySrc(sr)}});if(S.ws==="control")ctlRefresh()}
function learnBind(k){const lt=S.learnT;if(!lt)return;if(HOST.learnBind)HOST.learnBind(lt,k);const id="cc"+(20+k);if(S.ctl.links.some(l=>l.src===id&&l.t===lt.t&&l.pid===lt.pid)){toast("Already mapped.");return}
 S.ctl.links.push({src:id,t:lt.t,pid:lt.pid,min:0,max:127,curve:"lin",inv:false});S.learnT=null;S.ctl.sel=id;syncControls();toast(`Knob ${k} (CC ${20+k}) → ${tLabel(lt.t)} ${pidLabel(lt.t,lt.pid)}.`);if(S.ws==="control")render()}
const ALLT=[0,1,2,3,4,5,6,7,8,9,10,11];
function targetOpts(t){return pagesOf(t).flatMap(pg=>pnames(t,pg).map((n,i)=>n?opt(pg+"."+i,pg+" "+n,""):"")).join("")}
function renderControl(){const C=S.ctl,sel=srcById(C.sel)||C.sources[0];
 const mx=`<div class="mx"><span></span>${ALLT.map(i=>`<span class="mxh" title="${trk(i).name}"><b>${tLabel(i)}</b><small>${i<6?shortM(trk(i).m):"CH"+String(trk(i).ch).padStart(2,"0")}</small></span>`).join("")}
  ${C.sources.map(sr=>`<button class="srch ${sr.id===C.sel?"sel":""} k-${sr.kind}" data-src="${sr.id}" style="--f:${sr.val/127*100}%"><span class="sk">${sr.kind==="cc"?"CC "+sr.cc:"APP"}</span><b>${sr.label}</b><span class="sv mono">${sr.val}</span></button>
   ${ALLT.map(i=>{const ls=C.links.filter(l=>l.src===sr.id&&l.t===i);return`<button class="mxc ${ls.length?"on":""} ${sr.id===C.sel&&C.selT===i?"sel":""}" data-mxsrc="${sr.id}" data-mxt="${i}" title="${ls.map(l=>pidLabel(i,l.pid)).join(", ")||"no link"}">${ls.slice(0,2).map(l=>`<i>${pname(i,l.pid)}</i>`).join("")}${ls.length>2?`<i>+${ls.length-2}</i>`:""}</button>`}).join("")}`).join("")}</div>`;
 const links=C.links.map((l,li)=>({l,li})).filter(o=>o.l.src===sel.id&&(C.selT==null||o.l.t===C.selT));
 const appParams=sel.kind==="lfo"?`<div class="irow"><span class="ilab">Shape</span><div class="shapes">${[0,2,4,6,8,10].map(i=>`<button data-srcshape="${i}" aria-pressed="${sel.SHAPE===i}" title="${LWAVE[i]}">${shapeIcon(i)}</button>`).join("")}</div></div>
   <div class="irow"><span class="ilab">Rate</span><span class="seg" data-set="srcrate">${Object.keys(RATES).map(r=>`<button data-v="${r}" aria-pressed="${sel.RATE===r}">${r}</button>`).join("")}</span></div>
   <div class="irow"><span class="ilab">Depth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="DEPTH" data-g="src" data-n="DEPTH" data-src="${sel.id}"><span>DEPTH</span><b></b></div></div></div>`
  :sel.kind==="rnd"?`<div class="irow"><span class="ilab">Rate</span><span class="seg" data-set="srcrate">${Object.keys(RATES).map(r=>`<button data-v="${r}" aria-pressed="${sel.RATE===r}">${r}</button>`).join("")}</span></div>
   <div class="irow"><span class="ilab">Smooth</span><div style="width:140px"><div class="pc" role="slider" tabindex="0" aria-label="SMOOTH" data-g="src" data-n="SMOOTH" data-src="${sel.id}"><span>SMOOTH</span><b></b></div></div></div>`:"";
 const insp=`<section class="card"><header><h3>${sel.label}</h3><span>${sel.kind==="cc"?"CC "+sel.cc+" · from your controller":"<b class='apponly'>App only</b> · not saved in the kit"}</span></header>
  ${sel.kind!=="cc"?`<div class="note">Runs in the app and sends CCs to the Monomachine while it plays. A lock wins on its step.</div>`:""}
  ${appParams}
  <div class="lhead"><span class="cap">Targets</span>${C.selT!=null?`<button class="ptog on" data-selt="all"><i class="led"></i>${tLabel(C.selT)} only</button>`:`<span class="note">${links.length} target${links.length===1?"":"s"}</span>`}</div>
  <div class="lnks">${links.map(({l,li})=>`<div class="lnk"><span class="lcdchip" title="${ccOf(l.t,l.pid)} on the track channel">${tLabel(l.t)} ${pidLabel(l.t,l.pid)}</span>
   <div class="pc" role="slider" tabindex="0" aria-label="Min" data-g="link" data-n="min" data-li="${li}"><span>MIN</span><b></b></div><div class="pc" role="slider" tabindex="0" aria-label="Max" data-g="link" data-n="max" data-li="${li}"><span>MAX</span><b></b></div>
   <span class="seg" data-set="lcurve" data-li="${li}">${["lin","exp","log"].map(c=>`<button data-v="${c}" aria-pressed="${l.curve===c}">${c.toUpperCase()}</button>`).join("")}</span>
   <button class="ptog ${l.inv?"on":""}" data-linv="${li}"><i class="led"></i>Inv</button><button class="iconkey" data-ldel="${li}" aria-label="Remove target" title="Remove">×</button></div>`).join("")||`<div class="note">No targets yet. Add one below, or use LEARN.</div>`}</div>
  <div class="irow addrow"><span class="ilab">Add</span><select id="lt" aria-label="Target track">${ALLT.map(i=>opt(i,tLabel(i)+" · "+(i<6?trk(i).m:"MIDI CH"+trk(i).ch),C.addT)).join("")}</select>
   <select id="lp" aria-label="Target value">${targetOpts(C.addT)}</select><button class="cream" id="laddl">Add target</button></div>
  <div class="irow"><span class="ilab"></span><button data-addsrc="lfo">+ App LFO</button><button data-addsrc="rnd">+ Random</button></div></section>`;
 $("#main").innerHTML=`<div class="ctlui"><section class="card"><header><h3>Mapping matrix</h3><span>rows = sources · columns = tracks · drag a knob row to turn it</span></header>${mx}</section>${insp}</div>`;
 syncControls()}
let knobDrag=null;
document.addEventListener("pointerdown",e=>{const h=e.target.closest(".srch.k-cc");if(h){knobDrag={h,src:srcById(h.dataset.src),y:e.clientY,x:e.clientX,v:srcById(h.dataset.src).val,moved:false};h.setPointerCapture(e.pointerId)}},true);
document.addEventListener("pointermove",e=>{if(!knobDrag)return;const d=((e.clientX-knobDrag.x)+(knobDrag.y-e.clientY))/2;if(Math.abs(d)>2)knobDrag.moved=true;knobDrag.src.val=clamp(Math.round(knobDrag.v+d));applySrc(knobDrag.src);soundEdited();ctlRefresh()});
document.addEventListener("pointerup",()=>{if(knobDrag){const k=knobDrag;knobDrag=null;if(k.moved)k.h.dataset.moved="1"}},true);
document.addEventListener("click",e=>{if(S.ws!=="control")return;const C=S.ctl;
 const sh=e.target.closest(".srch");if(sh){if(sh.dataset.moved){sh.dataset.moved="";return}C.sel=sh.dataset.src;C.selT=null;render();return}
 const mc=e.target.closest(".mxc");if(mc){C.sel=mc.dataset.mxsrc;C.selT=+mc.dataset.mxt;C.addT=C.selT;render();return}
 if(e.target.closest("[data-selt]")){C.selT=null;render();return}
 const ss=e.target.closest("[data-srcshape]");if(ss){srcById(C.sel).SHAPE=+ss.dataset.srcshape;render();return}
 const sr=e.target.closest(".seg[data-set=srcrate] button");if(sr){srcById(C.sel).RATE=sr.dataset.v;render();return}
 const lc=e.target.closest(".seg[data-set=lcurve] button");if(lc){C.links[+lc.parentElement.dataset.li].curve=lc.dataset.v;render();return}
 const li=e.target.closest("[data-linv]");if(li){const l=C.links[+li.dataset.linv];l.inv=!l.inv;render();return}
 const ld=e.target.closest("[data-ldel]");if(ld){C.links.splice(+ld.dataset.ldel,1);render();return}
 if(e.target.closest("#laddl")){const pid=$("#lp").value,t=C.addT;if(C.links.some(l=>l.src===C.sel&&l.t===t&&l.pid===pid)){toast("Already a target.");return}C.links.push({src:C.sel,t,pid,min:0,max:127,curve:"lin",inv:false});render();return}
 const as=e.target.closest("[data-addsrc]");if(as){const k=as.dataset.addsrc,n=C.sources.filter(x=>x.kind===k).length;const id=k+"X"+Date.now();C.sources.push(k==="lfo"?{id,kind:"lfo",label:"LFO "+"ABCDEFGH"[n],val:64,SHAPE:2,RATE:"1",DEPTH:80}:{id,kind:"rnd",label:"Random "+"ABCDEFGH"[n],val:64,RATE:"1/8",SMOOTH:0,_t:64});C.sel=id;render();return}});
document.addEventListener("change",e=>{if(e.target.id==="lt"){S.ctl.addT=+e.target.value;render()}});
