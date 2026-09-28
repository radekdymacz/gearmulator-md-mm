/* AUDIO-MIDI PANEL BEGIN (P6): the standalone's audio and MIDI devices in the editor's own design,
   opened from the engine menu (AUDIO/MIDI…), the menu bar (Audio › Audio/MIDI Settings…) or with ",".
   The same block is in the MD mockup, the MM mockup and the MD skin (the sync scripts check it).
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
/* The panel's self-test (the editors' ?selftest=p6audio, or the mockup's console): open the panel, see
   the level arrive, change the buffer size and the output device and back, and check after each change
   that the machine keeps playing (its step moves). T gives log, play(on), step() and playing(). */
async function audioSelfTest(T){const sleep=ms=>new Promise(r=>setTimeout(r,ms)),res=[];
 const until=async(f,ms=8000)=>{const t0=Date.now();while(Date.now()-t0<ms){const D=audioDoc();if(D&&f(D))return D;await sleep(100)}const D=audioDoc();throw new Error("timeout: output "+D?.output?.id+", buffer "+D?.bufferSize?.value+", running "+D?.running+(D?.error?", error "+D.error:""))};
 const moving=async()=>{const s0=T.step();for(let i=0;i<30;i++){await sleep(100);if(T.playing()&&T.step()!==s0)return true}return false};
 const check=async(name,fn)=>{try{const n=await fn();res.push(true);T.log("ok   "+name+(n?": "+n:""))}catch(e){res.push(false);T.log("FAIL "+name+": "+e.message)}};
 const D0=await until(D=>D.standalone,15000),out0=D0.output.id,buf0=D0.bufferSize.value,muted0=!!D0.input.muted;
 T.log("devices: "+D0.output.list.length+" outputs ("+out0+"), "+D0.input.list.length+" inputs, "+D0.sampleRate.value+" Hz, buffer "+buf0+", "+(D0.midiInputs||[]).length+" MIDI inputs, running "+D0.running);
 let levels=0;const lv=audioLevel;window.audioLevel=v=>{levels++;lv(v)};
 await check("panel opens",async()=>{openAudio();await sleep(700);if(!AP.open||$("#audiopop").hidden)throw new Error("not shown");const n=$("#audiopop").querySelectorAll("select,button").length;return n+" controls, "+levels+" level updates"});
 window.audioLevel=lv;
 T.play(true);await sleep(1500);
 await check("playing before the changes",async()=>{if(!(await moving()))throw new Error("the step does not move");return "step "+T.step()});
 const buf1=D0.bufferSize.list.find(b=>b!==buf0&&b>=128&&b<=1024)??D0.bufferSize.list.find(b=>b!==buf0);
 await check("buffer size "+buf0+" -> "+buf1,async()=>{if(buf1==null)throw new Error("one buffer size only");audioSend({set:"bufferSize",value:buf1});const D=await until(D=>D.bufferSize.value===buf1&&D.running);if(!(await moving()))throw new Error("audio stopped");return D.latencyMs+" ms, still playing"});
 await check("buffer size back to "+buf0,async()=>{audioSend({set:"bufferSize",value:buf0});await until(D=>D.bufferSize.value===buf0&&D.running);if(!(await moving()))throw new Error("audio stopped");return "still playing"});
 const out1=D0.output.list.find(o=>o!==out0);
 await check("output "+out0+" -> "+(out1??"-"),async()=>{if(!out1)return "one output device only, skipped";audioSend({set:"output",device:out1});await until(D=>D.output.id===out1&&D.running);if(!(await moving()))throw new Error("audio stopped");return "still playing"});
 await check("output back to "+out0,async()=>{if(!out1)return "skipped";audioSend({set:"output",device:out0});await until(D=>D.output.id===out0&&D.running);if(!(await moving()))throw new Error("audio stopped");return "still playing"});
 await check("input mute toggles and is kept",async()=>{audioSend({set:"mute",on:!muted0});await until(D=>!!D.input.muted===!muted0);audioSend({set:"mute",on:muted0});await until(D=>!!D.input.muted===muted0);return muted0?"muted again":"live again"});
 T.play(false);closeAudio();await sleep(300);
 T.log((res.every(Boolean)?"PASS ":"FAIL ")+res.filter(Boolean).length+"/"+res.length)}
/* AUDIO-MIDI PANEL END */
/* The mockup's example devices (in the plug-in: its AudioDeviceManager, as a gm-audio/devices document) */
const AUD={schema:"gm-audio/devices",version:1,standalone:true,driver:{id:"CoreAudio",list:["CoreAudio"]},
 output:{id:"MacBook Pro Speakers",list:["MacBook Pro Speakers","External Headphones","Audient iD14"]},
 input:{id:"",list:["MacBook Pro Microphone","Audient iD14"],muted:true},
 outputChannels:[{name:"Output 1",on:true},{name:"Output 2",on:true}],
 sampleRate:{value:48000,list:[44100,48000,88200,96000]},bufferSize:{value:512,list:[64,128,256,512,1024,2048]},
 latencyMs:11.3,running:true,midiInputs:[{id:"iac",name:"IAC Driver Bus 1",on:false},{id:"tm1",name:"Elektron TM-1",on:true}],
 midiOutput:{id:"",list:[{id:"iac",name:"IAC Driver Bus 1"},{id:"tm1",name:"Elektron TM-1"}]},bluetooth:true,error:""};
let audMeterT=0;
function audioDoc(){return HOST.audioDoc?HOST.audioDoc():AUD}
function audioMeter(on){if(HOST.audioMeter)return HOST.audioMeter(on);clearInterval(audMeterT);if(on)audMeterT=setInterval(()=>audioLevel(!AUD.input.id?0:(AUD.input.muted?.01:.04)+Math.random()*.12),120)}
function audioSend(c){if(HOST.audioSend)return HOST.audioSend(c);const D=AUD,say=t=>typeof toast==="function"&&toast(t);
 if(c.do==="test"){say("Test tone on "+D.output.id);return}
 if(c.do==="bluetooth"){say("The system's Bluetooth MIDI dialog opens here");return}
 if(c.set==="output"){D.output.id=c.device;const n=c.device==="Audient iD14"?4:2;D.outputChannels=Array.from({length:n},(_,i)=>({name:"Output "+(i+1),on:i<2}))}
 else if(c.set==="input")D.input.id=c.device;
 else if(c.set==="mute")D.input.muted=c.on;
 else if(c.set==="outputChannel"){const on=D.outputChannels.filter(x=>x.on).length;if(!c.on&&on<=1){D.error="At least one output channel stays on"}else{D.outputChannels[c.index].on=c.on;D.error=""}}
 else if(c.set==="sampleRate")D.sampleRate.value=c.value;
 else if(c.set==="bufferSize")D.bufferSize.value=c.value;
 else if(c.set==="midiInput"){const m=D.midiInputs.find(x=>x.id===c.device);if(m)m.on=c.on}
 else if(c.set==="midiOutput")D.midiOutput.id=c.device;
 if(c.set!=="outputChannel")D.error="";
 D.latencyMs=Math.round((D.bufferSize.value+96)/D.sampleRate.value*10000)/10;drawAudio()}
