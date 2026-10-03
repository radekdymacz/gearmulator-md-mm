"use strict";
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
