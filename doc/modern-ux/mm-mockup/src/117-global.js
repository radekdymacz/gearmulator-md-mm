/* ===== GLOBAL: the machine's MIDI settings (B-051, ROADMAP F3: the MIDI part) =====
   The Machinedrum Editor's GLOBAL panel (mdDeskGlobal.js) for the Monomachine's GLOBAL › MIDI (manual 1-89, 1-91):
   the MIDI CHANNELS screen and CONTROL IN, of the active global. S.glob is the view (MmView's globPage: a channel
   0-15, null OFF); every change is the globalMidi intent, a dump of the active global the machine stores and makes
   active. A track the global gives no channel of its own (CHANNEL SPAN, or a base near 16) takes no mute or note over
   MIDI, and its sound values go as kit dumps. Not here yet (F3): the 8 global slots, CONTROL OUT, master tune. */
var GP={open:false};
const GCH=c=>c==null?"OFF":String(c+1);
/* the machine's rule, as the core's (elektronData::mmTrackChannel): base + t inside the span, up to channel 15 */
const gTrackCh=(g,t)=>g&&g.base!=null&&t<g.span&&g.base+t<=14?g.base+t:null;
const GTIP={
 base:"BASE CHANNEL: track t listens on base + t, for its sound values, mute and notes. OFF: no track takes any over MIDI",
 span:"CHANNEL SPAN: how many tracks, from T1, have a channel of their own. 0: none (as in some old backups)",
 auto:"AUTO TRACK: notes on this channel play the selected track",
 multiTrig:"MULTI TRIG: notes on this channel play the tracks as the kit's MULTI TRIG says (Perform)",
 multiMap:"MULTI MAP: notes on this channel start patterns as the MULTI MAP says (Perform)",
 clockIn:"TEMPO SYNC: INT plays at its own tempo, EXT follows MIDI clock. In a DAW the plug-in sets EXT itself (it follows the host)",
 transportIn:"TRANSPORT: ACCEPT reacts to MIDI Start, Stop and Continue. In a DAW the plug-in sets ACCEPT itself",
 tracks:"Each track's own MIDI channel under these settings; — has none: its mute and notes cannot reach it, the editor sends its sound values as kit dumps (slower)"};
function drawGlobal(){const pop=$("#globpop");if(!pop)return;if(!GP.open){pop.hidden=true;return}const G=S.glob;
 if(!G){pop.innerHTML=`<div class="libhead"><span class="cap">Global</span><span class="note">Reading the global settings from the machine…</span><button class="libx" data-ga="close">Esc</button></div>`;pop.hidden=false;placeGlobal();return}
 const step=(f,label,tip)=>`<span class="stepper" title="${tip}"><button data-ga="${f}" data-d="-1" aria-label="Less">−</button><b class="mono" data-gv="${f}">${label}</b><button data-ga="${f}" data-d="1" aria-label="More">+</button></span>`;
 const tog=(f,on,tip,a,b)=>`<span class="seg" title="${tip}"><button data-ga="${f}" data-v="1" aria-pressed="${on}">${a}</button><button data-ga="${f}" data-v="0" aria-pressed="${!on}">${b}</button></span>`;
 const chs=[0,1,2,3,4,5].map(t=>gTrackCh(G,t)),none=chs.map((c,t)=>c==null?"T"+(t+1):null).filter(Boolean);
 pop.innerHTML=`<div class="libhead"><span class="cap">Global</span><span class="lcdchip">GLOBAL ${G.slot+1}</span><span class="note">GLOBAL › MIDI on the machine (FUNCTION + KIT/SONG). A change is stored in the active global and made active at once.</span><button class="libx" data-ga="close" title="Close (Esc)">Esc</button></div>
 <div class="globgrid">
  <section class="card"><header><h3>MIDI channels</h3><span>GLOBAL › MIDI › CHANNELS</span></header>
   <div class="grow2"><span class="ilab">Base channel</span>${step("base",GCH(G.base),GTIP.base)}</div>
   <div class="grow2"><span class="ilab">Channel span</span>${step("span",String(G.span),GTIP.span)}</div>
   <div class="gtracks" title="${GTIP.tracks}">${chs.map((c,t)=>`<span class="gtk${c==null?" none":""}" data-gt="${t}"><small>T${t+1}</small><b>${c==null?"—":c+1}</b></span>`).join("")}</div>
   <p class="gwarn"${none.length?"":" hidden"}>${none.length===6?"No track":none.join(" ")} ${none.length===1?"has":"have"} no MIDI channel of ${none.length===1?"its":"their"} own: mutes and notes cannot reach ${none.length===1?"it":"them"}, and the editor sends ${none.length===1?"its":"their"} sound values as kit dumps.</p>
   <div class="grow2"><span class="ilab">Auto track</span>${step("auto",GCH(G.auto),GTIP.auto)}</div>
   <div class="grow2"><span class="ilab">Multi trig</span>${step("multiTrig",GCH(G.multiTrig),GTIP.multiTrig)}</div>
   <div class="grow2"><span class="ilab">Multi map</span>${step("multiMap",GCH(G.multiMap),GTIP.multiMap)}</div></section>
  <section class="card"><header><h3>Sync in</h3><span>GLOBAL › MIDI › CONTROL IN</span></header>
   <div class="grow2"><span class="ilab">Tempo sync</span>${tog("clockIn",G.clockIn,GTIP.clockIn,"EXT","INT")}</div>
   <div class="grow2"><span class="ilab">Transport</span>${tog("transportIn",G.transportIn,GTIP.transportIn,"ACCEPT","IGNORE")}</div></section>
 </div>
 <div class="libfoot"><span>The GLOBAL key or the engine menu opens it · Esc closes · every change is stored on the machine and made active</span><span class="fw" title="Still on the machine only: the other global slots, CONTROL OUT, master tune">MIDI settings of GLOBAL ${G.slot+1}</span></div>`;
 pop.hidden=false;placeGlobal()}
function placeGlobal(){const pop=$("#globpop"),r=$(".lcdpanel").getBoundingClientRect(),top=Math.max(16,r.bottom+8);pop.style.top=(top+scrollY)+"px";pop.style.maxHeight=Math.max(240,innerHeight-top-12)+"px";pop.style.left=Math.max(16,(document.documentElement.clientWidth-pop.offsetWidth)/2+scrollX)+"px"}
function globKeyLit(){const k=$("#globkey");if(k){k.setAttribute("aria-pressed",String(GP.open));k.classList.toggle("on",GP.open);k.querySelector(".led")?.classList.toggle("on",GP.open)}}
function openGlobal(){if(typeof closeLib==="function")closeLib(false);GP.open=true;drawGlobal();globKeyLit()}
function closeGlobal(){GP.open=false;drawGlobal();globKeyLit()}
/* a channel stepper: OFF, 1 … 16 */
const gStep=(c,d)=>{const i=Math.max(0,Math.min(16,(c==null?0:c+1)+d));return i===0?null:i-1};
function globalClick(a){const G=S.glob,f=a.dataset.ga;if(f==="close"){closeGlobal();return}if(!G)return;
 const d=a.dataset.d!=null?+a.dataset.d:0,v=a.dataset.v!=null?+a.dataset.v:null;let args=null;
 if(f==="span")args={span:Math.max(0,Math.min(16,G.span+d))};
 else if(["base","auto","multiTrig","multiMap"].includes(f))args={[f]:gStep(G[f],d)};
 else if(f==="clockIn"||f==="transportIn")args={[f]:v===1};
 if(!args||Object.entries(args).every(([k,x])=>G[k]===x))return;
 Object.assign(G,args);edit("globalMidi",args);drawGlobal()}
document.addEventListener("click",e=>{if(e.target.closest?.("#globkey")){GP.open?closeGlobal():openGlobal();e.stopPropagation();return}if(!GP.open)return;const pop=$("#globpop");if(pop.contains(e.target)){const a=e.target.closest("[data-ga]");if(a)globalClick(a);return}if(!e.target.closest?.("#dlg,.kpop,#kpop,.lcdeng"))closeGlobal()},true);
addEventListener("resize",()=>{if(GP.open)placeGlobal()});
Keys.bind({id:"global-key",scope:"any",area:"Top bar",keys:["GLOBAL key"],group:"Anywhere",does:"The machine's MIDI settings, kept in its active global: the channels each track listens on, sync in (also the engine menu: GLOBAL…)"});
Keys.bind({id:"close-global",short:"Close",scope:"any",keys:["Escape"],group:"Anywhere",does:"Close the GLOBAL settings",when:()=>GP.open,run:()=>closeGlobal()});
