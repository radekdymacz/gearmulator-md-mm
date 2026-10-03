"use strict";
/* The AUDIO / MIDI panel (P6): the standalone's audio and MIDI devices in the editor's own design,
   opened from the engine menu (AUDIO/MIDI…), the menu bar (Audio › Audio/MIDI Settings…) or with ",".
   One file for both editors and both mockups (skins/shared/); its self-test is deskAudioSelfTest.js.
   It only renders a gm-audio/devices document: audioDoc() gives it, audioSend(command) changes one
   thing, audioMeter(on) asks for the input level (audioLevel(v) brings it). The host is the page's:
   a simulated device list in the mockups, the plug-in's AudioDeviceManager in the editors. */
var AP={open:false,level:0};
const AMTIP={
 output:"Where the sound goes. The engine keeps running while the device changes",
 test:"A short test tone on the active outputs",
 input:"The device the external inputs come from. NONE: no input",
 mute:"The input starts muted so a microphone and speakers cannot feed back. LIVE passes the input to the machine",
 chans:"The outputs the editor plays on. At least one stays on",
 rate:"The device's sample rate. The machine's own rate is fixed; the plug-in resamples",
 buffer:"Smaller: less latency, more CPU. Larger: safer on a busy computer",
 midiIn:"MIDI inputs that play and control the machine",
 midiOut:"Where the machine's MIDI goes (clock, notes, SysEx)",
 bt:"Pair a Bluetooth MIDI device (the system's dialog)" };
const amEsc=t=>String(t??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"})[c]);
function drawAudio(){const pop=$("#audiopop");if(!pop)return;if(!AP.open){pop.hidden=true;return}const D=audioDoc();
 const head=note=>`<div class="libhead"><span class="cap">Audio / MIDI</span>${note}<button class="libx" data-am="close" title="Close (Esc)">Esc</button></div>`;
 if(!D){pop.innerHTML=head(`<span class="note">Reading the devices…</span>`);pop.hidden=false;placeAudio();return}
 if(!D.standalone){pop.innerHTML=head(`<span class="note">In a plug-in the host owns audio and MIDI: set them up in the host.</span>`);pop.hidden=false;placeAudio();return}
 const sel=(id,cur,list,tip,none)=>`<select id="am-${id}" data-am="${id}" title="${tip}">${none?`<option value=""${cur?"":" selected"}>${none}</option>`:""}${list.map(o=>{const v=typeof o==="object"?o.id:String(o),l=typeof o==="object"?o.name:String(o);return`<option value="${amEsc(v)}"${v===String(cur)?" selected":""}>${amEsc(l)}</option>`}).join("")}${cur&&!list.some(o=>(typeof o==="object"?o.id:String(o))===String(cur))?`<option value="${amEsc(cur)}" selected>${amEsc(cur)} (gone)</option>`:""}</select>`;
 const rate=D.sampleRate||{value:0,list:[]},buf=D.bufferSize||{value:0,list:[]};
 const chip=`${rate.value?Math.round(rate.value/100)/10+" kHz":"--"} · ${buf.value||"--"} · ${D.latencyMs?D.latencyMs+" ms":"--"}`;
 const outs=D.outputChannels||[],pairs=[];for(let i=0;i<outs.length;i++)pairs.push(i);
 pop.innerHTML=head(`<span class="lcdchip" title="Sample rate · buffer · output latency">${chip}</span><span class="note">${D.running?"The engine is running.":"No audio device is running."}${D.error?` <b class="amerr">${amEsc(D.error)}</b>`:""}</span>`)+`
 <div class="amgrid">
  <section class="card"><header><h3>Output</h3><span>${amEsc(D.driver?.id||"")}</span></header>
   ${(D.driver?.list||[]).length>1?`<div class="grow2"><span class="ilab">Driver</span>${sel("driver",D.driver.id,D.driver.list,"The system's audio driver")}</div>`:""}
   <div class="grow2"><span class="ilab">Device</span>${sel("output",D.output?.id,D.output?.list||[],AMTIP.output)}<button class="amkey" data-am="test" title="${AMTIP.test}">TEST</button></div>
   <div class="grow2 amtop"><span class="ilab">Channels</span><span class="amchans" title="${AMTIP.chans}">${outs.map((c,i)=>`<button class="amled" data-am="chan" data-i="${i}" aria-pressed="${!!c.on}" title="${amEsc(c.name)}"><i class="led${c.on?" on":""}"></i>${i+1}</button>`).join("")||`<span class="note">none</span>`}</span></div></section>
  <section class="card"><header><h3>Input</h3><span>external inputs</span></header>
   <div class="grow2"><span class="ilab">Device</span>${sel("input",D.input?.id,D.input?.list||[],AMTIP.input,"NONE")}</div>
   <div class="grow2"><span class="ilab">Level</span><span class="ammeter" title="The input level before the mute"><i id="amlevel" style="width:${Math.round(Math.min(1,Math.sqrt(Math.max(0,AP.level)))*100)}%"></i></span></div>
   <div class="grow2"><span class="ilab">Input</span><span class="seg" title="${AMTIP.mute}"><button data-am="mute" data-v="1" aria-pressed="${!!D.input?.muted}">MUTED</button><button data-am="mute" data-v="0" aria-pressed="${!D.input?.muted}">LIVE</button></span></div>
   <div class="amnote">${D.input?.muted?"Input muted (prevents feedback)":"Input live: keep speakers away from the microphone"}</div></section>
  <section class="card"><header><h3>Clock</h3><span>rate and buffer</span></header>
   <div class="grow2"><span class="ilab">Sample rate</span>${sel("rate",rate.value,rate.list.map(r=>({id:String(r),name:(r/1000)+" kHz"})),AMTIP.rate)}</div>
   <div class="grow2"><span class="ilab">Buffer</span>${sel("buffer",buf.value,buf.list.map(b=>({id:String(b),name:b+" samples"+(rate.value?" · "+(Math.round(b/rate.value*10000)/10)+" ms":"")})),AMTIP.buffer)}</div>
   <div class="grow2"><span class="ilab">Latency</span><b class="mono">${D.latencyMs?D.latencyMs+" ms":"--"}</b></div></section>
  <section class="card amwide"><header><h3>MIDI</h3><span>inputs, output, Bluetooth</span></header>
   <div class="grow2 amtop"><span class="ilab">Inputs</span><span class="amins" title="${AMTIP.midiIn}">${(D.midiInputs||[]).map(m=>`<button class="amled" data-am="midiIn" data-id="${amEsc(m.id)}" aria-pressed="${!!m.on}"><i class="led${m.on?" on":""}"></i>${amEsc(m.name)}</button>`).join("")||`<span class="note">No MIDI inputs.</span>`}</span></div>
   <div class="grow2"><span class="ilab">Output</span>${sel("midiOut",D.midiOutput?.id||"",D.midiOutput?.list||[],AMTIP.midiOut,"NONE")}${D.bluetooth?`<button class="amkey" data-am="bt" title="${AMTIP.bt}">BLUETOOTH MIDI…</button>`:""}</div></section>
 </div>
 <div class="libfoot"><span>, or the engine menu opens it · Esc closes · a change applies at once and is kept</span><span class="fw">${amEsc(D.output?.id||"no output")}</span></div>`;
 if(typeof enhanceSelects==="function")enhanceSelects(pop);pop.hidden=false;placeAudio()}
function placeAudio(){const pop=$("#audiopop"),r=$(".lcdpanel").getBoundingClientRect(),top=Math.max(16,r.bottom+8);pop.style.top=(top+scrollY)+"px";pop.style.maxHeight=Math.max(240,innerHeight-top-12)+"px";pop.style.left=Math.max(16,(document.documentElement.clientWidth-pop.offsetWidth)/2+scrollX)+"px"}
function openAudio(){if(typeof closeGlobal==="function"&&typeof GP!=="undefined"&&GP.open)closeGlobal();if(typeof closeLib==="function")closeLib(false);AP.open=true;audioMeter(true);drawAudio()}
function closeAudio(){if(!AP.open)return;AP.open=false;audioMeter(false);drawAudio()}
function audioLevel(v){AP.level=v;const l=document.getElementById("amlevel");if(l)l.style.width=Math.round(Math.min(1,Math.sqrt(Math.max(0,v)))*100)+"%"}
function audioClick(a){const f=a.dataset.am;
 if(f==="close"){closeAudio();return}
 if(f==="test"){audioSend({do:"test"});return}
 if(f==="bt"){audioSend({do:"bluetooth"});return}
 if(f==="mute"){audioSend({set:"mute",on:a.dataset.v==="1"});return}
 if(f==="chan"){audioSend({set:"outputChannel",index:+a.dataset.i,on:a.getAttribute("aria-pressed")!=="true"});return}
 if(f==="midiIn"){audioSend({set:"midiInput",device:a.dataset.id,on:a.getAttribute("aria-pressed")!=="true"});return}}
document.addEventListener("change",e=>{const s=e.target.closest?.("#audiopop select[data-am]");if(!s)return;const f=s.dataset.am,v=s.value;
 const cmd={driver:{set:"driver",device:v},output:{set:"output",device:v},input:{set:"input",device:v},rate:{set:"sampleRate",value:+v},buffer:{set:"bufferSize",value:+v},midiOut:{set:"midiOutput",device:v}}[f];if(cmd)audioSend(cmd)});
document.addEventListener("click",e=>{if(!AP.open)return;const pop=$("#audiopop");if(pop.contains(e.target)){const a=e.target.closest("[data-am]");if(a&&a.tagName==="BUTTON")audioClick(a);return}if(!e.target.closest?.("#dlg,.kpop,#kpop,.lcdeng"))closeAudio()},true);
document.addEventListener("keydown",e=>{if(e.target.closest?.("input,select,textarea")||e.metaKey||e.ctrlKey||e.altKey)return;
 if(AP.open&&e.key==="Escape"){e.preventDefault();e.stopImmediatePropagation();closeAudio();return}
 if(e.key===","&&(!$("#dlg")||$("#dlg").hidden)){e.preventDefault();e.stopImmediatePropagation();AP.open?closeAudio():openAudio()}},true);
addEventListener("resize",()=>{if(AP.open)placeAudio()});
