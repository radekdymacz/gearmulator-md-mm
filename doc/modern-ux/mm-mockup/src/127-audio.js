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
