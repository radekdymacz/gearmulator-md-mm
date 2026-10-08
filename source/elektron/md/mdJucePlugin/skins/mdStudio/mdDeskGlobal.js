"use strict";
/* v61 (P5): GLOBAL settings (manual pp.57-62, 80-81): the machine's FUNCTION + PATTERN/SONG menu as one panel,
   opened from the engine menu (GLOBAL…). glob() gives the view, globSend(field, value) changes one
   setting; both are the host's (example state in the mockup, the desk's documents in the plug-in). What was
   measured on the firmware is plain; what could not be is marked "not verified". */
var GP={open:false};
const NOTE=n=>["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"][n%12]+(Math.floor(n/12)-1);
const GTIP={
 base:"The first of four MIDI channels the machine listens and sends on (channel n to n+3)",
 tempoIn:"INTERNAL: its own BPM. EXTERNAL: follows MIDI clock (MIDI Start waits for the clock)",
 ctrlIn:"React to MIDI Start, Stop and Continue",tempoOut:"Send MIDI clock",ctrlOut:"Send MIDI Start, Stop and Continue",
 pcIn:"Program change selects a pattern",pcOut:"Selecting a pattern sends a program change",
 pcCh:"BASE: in on the four base channels, out on the first",
 trig:"How a note mapped to a pattern starts it. GATE: plays while the key is held. START: at once. QUE: at the end of the playing pattern",
 local:"OFF: the TRIG keys and the sequencer do not play the internal sounds. Stored in the global; its effect could not be verified in the emulator",
 map:"The note that plays each track. A key maps one function: choosing a used key frees it elsewhere",
 inputs:"The external inputs as trig pads. Shown as stored; they need pads on the inputs, so they are not verified here" };
function drawGlobal(){const pop=$("#globpop");if(!GP.open){pop.hidden=true;return}const G=glob();if(!G){pop.innerHTML=`<div class="libhead"><span class="cap">Global</span><span class="note">Reading the global settings from the machine…</span><button class="libx" data-ga="close">Esc</button></div>`;pop.hidden=false;placeGlobal();return}
 const tog=(f,on,tip,a="ON",b="OFF")=>`<span class="seg" title="${tip}"><button data-ga="${f}" data-v="1" aria-pressed="${on}">${a}</button><button data-ga="${f}" data-v="0" aria-pressed="${!on}">${b}</button></span>`;
 const step=(f,label,tip)=>`<span class="stepper" title="${tip}"><button data-ga="${f}" data-d="-1" aria-label="Less">−</button><b class="mono">${label}</b><button data-ga="${f}" data-d="1" aria-label="More">+</button></span>`;
 const trackKey=t=>{const n=G.keymap.indexOf(t);return`<div class="gmk key${n<0?" none":""}" title="${GTIP.map}"><small>TRACK ${t+1}</small><span class="stepper"><button data-ga="map" data-t="${t}" data-d="-1" aria-label="Lower note">‹</button><b>${n<0?"--":NOTE(n)}</b><button data-ga="map" data-t="${t}" data-d="1" aria-label="Higher note">›</button></span></div>`};
 const pats=G.keymap.map((v,n)=>v!=null&&v>=16&&v<32?`${NOTE(n)}→P${v-15}`:null).filter(Boolean);
 const outs=["A","B","C","D","E","F","MAIN"];
 pop.innerHTML=`<div class="libhead"><span class="cap">Global</span><span class="lcdchip">GLOBAL ${G.slot+1}</span><span class="gslots" title="8 global setups; the lit one is active (SysEx 0x56)">${Array.from({length:8},(_,k)=>`<button data-ga="slot" data-v="${k}" aria-pressed="${k===G.slot}">${k+1}</button>`).join("")}</span><span class="note">FUNCTION + PATTERN/SONG on the machine. A change is stored and made active at once.</span><button class="libx" data-ga="close" title="Close (Esc)">Esc</button></div>
 <div class="globgrid">
  <section class="card"><header><h3>Control</h3><span>MIDI</span></header>
   <div class="grow2"><span class="ilab">Base channel</span>${step("base",(G.baseChannel+1)+"–"+(G.baseChannel+4),GTIP.base)}</div>
   <div class="grow2"><span class="ilab">Prg change in</span>${tog("pcIn",G.pcIn,GTIP.pcIn)}</div>
   <div class="grow2"><span class="ilab">Prg change out</span>${tog("pcOut",G.pcOut,GTIP.pcOut)}</div>
   <div class="grow2"><span class="ilab">Prg channel</span>${step("pcCh",G.pcChannel?G.pcChannel:"BASE",GTIP.pcCh)}</div>
   <div class="grow2"><span class="ilab">Local ctrl</span>${tog("local",G.local,GTIP.local)}<span class="gnv" title="${GTIP.local}">not verified</span></div></section>
  <section class="card"><header><h3>Sync</h3><span>clock and transport</span></header>
   <div class="grow2"><span class="ilab">Tempo in</span>${tog("tempoIn",G.tempoIn==="external",GTIP.tempoIn,"EXT","INT")}</div>
   <div class="grow2"><span class="ilab">Ctrl in</span>${tog("ctrlIn",G.ctrlIn,GTIP.ctrlIn)}</div>
   <div class="grow2"><span class="ilab">Tempo out</span>${tog("tempoOut",G.tempoOut,GTIP.tempoOut)}</div>
   <div class="grow2"><span class="ilab">Ctrl out</span>${tog("ctrlOut",G.ctrlOut,GTIP.ctrlOut)}</div></section>
  <section class="card"><header><h3>Trig in A / B</h3><span>external pads<span class="gnv" title="${GTIP.inputs}">not verified</span></span></header>
   <div class="ginputs" title="${GTIP.inputs}"><span></span><span>Gate</span><span>Sens</span><span>Vmin</span><span>Vmax</span><span>Dest</span>
    ${["A","B"].map((x,i)=>`<span>${x}</span>`+[0,2,4,6,8].map(k=>`<b>${G.inputs[k+i]}</b>`).join("")).join("")}</div></section>
  <section class="card wide"><header><h3>Map editor</h3><span>MIDI notes → tracks and patterns</span></header>
   <div class="grow2"><span class="ilab">Pattern trig</span><span class="seg" title="${GTIP.trig}">${["gate","start","que"].map((m,i)=>`<button data-ga="trig" data-v="${i}" aria-pressed="${G.trigMode===m}">${m.toUpperCase()}</button>`).join("")}</span><span class="note">${pats.length?pats.length+" notes play patterns: "+pats.slice(0,6).join(" ")+(pats.length>6?" …":""):"No notes play patterns."}</span></div>
   <div class="gmap">${Array.from({length:16},(_,t)=>trackKey(t)).join("")}</div></section>
  <section class="card"><header><h3>Routing</h3><span>track outputs · A–F skip the master effects</span></header>
   <div class="grout">${G.routing.map((o,t)=>`<button class="${o!=="MAIN"?"on":""}" data-ga="route" data-t="${t}" title="Track ${t+1}: ${o==="MAIN"?"main output, through the master effects":"output "+o+", skips the master effects"}. Click for the next output."><small>${t+1}</small><b>${o}</b></button>`).join("")}</div></section>
 </div>
 <div class="libfoot"><span>G or the engine menu opens it · Esc closes · every change is stored on the machine and made active</span><span class="fw" title="The machine keeps 8 global setups">GLOBAL ${G.slot+1} of 8</span></div>`;
 pop.hidden=false;placeGlobal()}
function placeGlobal(){const pop=$("#globpop"),r=$(".lcdpanel").getBoundingClientRect(),top=Math.max(16,r.bottom+8);pop.style.top=(top+scrollY)+"px";pop.style.maxHeight=Math.max(240,innerHeight-top-12)+"px";pop.style.left=Math.max(16,(document.documentElement.clientWidth-pop.offsetWidth)/2+scrollX)+"px"}
function openGlobal(){if(typeof closeLib==="function")closeLib(false);GP.open=true;drawGlobal()}
function closeGlobal(){GP.open=false;drawGlobal()}
function globalClick(a){const G=glob();if(!G)return;const f=a.dataset.ga,v=a.dataset.v!=null?+a.dataset.v:null,d=a.dataset.d!=null?+a.dataset.d:0;
 if(f==="close"){closeGlobal();return}
 if(f==="slot"){globSend("slot",v);return}
 if(f==="base"){globSend("baseChannel",Math.max(0,Math.min(12,G.baseChannel+d)));return}
 if(f==="pcCh"){globSend("programChangeChannel",Math.max(0,Math.min(16,(G.pcChannel||0)+d)));return}
 if(f==="trig"){globSend("trigMode",v);return}
 if(f==="route"){const outs=["MAIN","A","B","C","D","E","F"],t=+a.dataset.t;globSend("route",{t,out:outs[(outs.indexOf(G.routing[t])+1)%outs.length]});return}
 if(f==="map"){const t=+a.dataset.t;let n=G.keymap.indexOf(t);n=n<0?(d>0?0:127):n+d;n=Math.max(0,Math.min(127,n));globSend("keymap",{note:n,target:t});return}
 const map={pcIn:"programChangeIn",pcOut:"programChangeOut",local:"localControl",tempoIn:"tempoIn",ctrlIn:"ctrlIn",tempoOut:"tempoOut",ctrlOut:"ctrlOut"};
 if(map[f])globSend(map[f],v===1)}
document.addEventListener("click",e=>{if(!GP.open)return;const pop=$("#globpop");if(pop.contains(e.target)){const a=e.target.closest("[data-ga]");if(a)globalClick(a);return}if(!e.target.closest?.("#dlg,.kpop,#kpop,.lcdeng"))closeGlobal()},true);
addEventListener("resize",()=>{if(GP.open)placeGlobal()});

/* The plug-in's side: the view from md-desk/global (its derived "control" view) and the desk's commands. */
function glob() {
	const g = Docs.global; if (!g) return null; const c = g.control || {};
	return { slot: g.slot, baseChannel: g.baseChannel, tempoIn: c.tempoIn, ctrlIn: c.ctrlIn, tempoOut: c.tempoOut, ctrlOut: c.ctrlOut, pcIn: c.programChangeIn,
		pcOut: c.programChangeOut, pcChannel: c.programChangeChannel, trigMode: c.trigMode, local: c.localControl, keymap: g.keymap, routing: g.routing, inputs: g.settings.inputSettings };
}
function globSend(f, v) {
	if (f === "slot") cmd("globalSlot", { slot: v });
	else if (f === "route") cmd("route", v);
	else if (f === "keymap") cmd("globalSet", { field: "keymap", note: v.note, target: v.target });
	else if (typeof v === "boolean") cmd("globalSet", { field: f, on: v });
	else cmd("globalSet", { field: f, v });
}
Bridge.onMessage(m => { if (GP.open && (m.type === "doc" && m.kind === "global" || m.type === "result" && !m.ok)) setTimeout(drawGlobal, 20); });
Keys.bind({ id: "close-global", scope: "any", keys: ["Escape"], group: "Anywhere", does: "Close the GLOBAL settings", when: () => GP.open, run: () => closeGlobal() });
