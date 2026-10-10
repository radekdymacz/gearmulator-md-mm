
/* ===== State ===== */
const $=q=>document.querySelector(q),$$=q=>[...document.querySelectorAll(q)];
function clamp(v,a=0,b=127){return Math.max(a,Math.min(b,v))}
const newArp=()=>({MODE:0,PLAY:1,SPD:6,RNGE:1,OJMP:0,amp:1,flt:1,lfo:1,len:8,rhy:Array(16).fill(true),ofs:Array(16).fill(0)});
const newAssign=()=>({mirr:true,hpf:true,lpf:true,tabs:{"JOY RL":[{pg:3,d:1,add:88},{pg:2,d:5,add:64}],"JOY U":[{pg:5,d:7,add:94},{pg:3,d:3,add:64}],"JOY D":[{pg:3,d:0,add:40},{pg:2,d:5,add:64}],VEL:[{pg:2,d:5,add:84},{pg:3,d:7,add:64}],KEY:[{pg:3,d:0,add:64},{pg:3,d:1,add:64}]}});
function synDefaults(m){const p=MACH[m].p;return Array.from({length:8},(_,i)=>{const n=p[i];if(!n)return 0;if(n==="TUNE")return 64;if(n==="INP")return 100;if(n==="MIX")return 64;return({UNIL:40,UNIW:20,UNIX:0,SUB1:30,PW:64,VOC1:64,VOC2:64,"V-SW":1,CVOL:100,LP:127,GATE:127,DEC:64,"1ENV":80,TONE:64})[n]??0})}
function newTrack(m,name){return{m,name,v:{SYN:synDefaults(m),AMP:[...DEFV.AMP],FLT:[...DEFV.FLT],EFX:[...DEFV.EFX],LF1:[...DEFV.LFO],LF2:[...DEFV.LFO],LF3:[...DEFV.LFO]},
 lev:100,out:{AB:true,CD:false,EF:false},inp:"NEIBOR",mute:false,solo:false,steps:Array(64).fill(null),slide:new Set(),swing:new Set([1,3,5,7,9,11,13,15].flatMap(x=>[x,x+16,x+32,x+48])),
 arp:newArp(),tr:{TRACK:64,SCALE:0,KEY:0},trigpos:null,port:0,leg:{amp:1,flt:1,lfo:1},assign:newAssign()}}
function newMidi(ch){return{m:"MIDI",name:"MIDI "+ch,ch,v:{MID:[...DEFV.MID]},cc:[74,71,1,10],mute:false,solo:false,steps:Array(64).fill(null),slide:new Set(),swing:new Set([1,3,5,7,9,11,13,15].flatMap(x=>[x,x+16,x+32,x+48])),arp:newArp(),tr:{TRACK:64,SCALE:0,KEY:0}}}
const note=(n,bits=[1,1,1])=>({n:Array.isArray(n)?n:[n],a:bits[0],f:bits[1],l:bits[2]});

const S={ws:"seq",sel:0,side:"int",len:32,mult:"1X",swingAmt:58,playing:false,step:-1,bpm:120,pat:0,kit:0,kitState:"clean",queued:null,trigSel:"ALL",
 lane:"FLT.1",lanePage:"FLT",locks:new Map(),patTrn:64,routing:"3xSTEREO+AB=MIX",glob:{slot:0,base:0,span:6,auto:8,multiTrig:6,multiMap:7,clockIn:false,transportIn:false},plate:"mk2",engine:"emu",pend:0,patSent:"live",rec:false,
 mode:"normal",songs:{names:["DEMO SONG",...Array(23).fill("EMPTY")],slot:0,current:0},multi:{mode:0,splitKey:60,splitTrack:3,timing:4},menv:{ATK:0,DEC:127,SUS:127,REL:127,PORT:0},kbOct:3,
 mmap:[{hi:47,pat:0,ofs:0,len:0,trn:64,tim:4},{hi:59,pat:1,ofs:0,len:16,trn:64,tim:4},{hi:71,pat:0,ofs:0,len:0,trn:69,tim:4},{hi:127,pat:2,ofs:8,len:8,trn:64,tim:1}],mmapSel:1,
 gmutes:null,rollLo:36,ghost:true,asTab:"JOY RL",joy:{x:0,y:0},
 /* whether the machine takes input (machine.input); null while no host has said (the standalone
    mockup, which has no host), so engReady() falls back to the engine label (S.eng) */
 input:null};
S.tracks=[newTrack("SWAVE-SAW","Bass"),newTrack("FX-CHORUS","Bass chorus"),newTrack("SID-6581","Arp lead"),newTrack("VO-6","Monomachine"),newTrack("DPRO-BBOX","Drums"),newTrack("FX-REVERB","Room")];
S.midi=[1,2,3,4,5,6].map(k=>newMidi([10,3,4,5,6,7][k-1]));S.midi[0].name="Drum machine";S.midi[1].name="Poly pad";
S.kitNames={0:"MONOMACHINE",1:"ACID BATH",2:"VOCODED",5:"SID LEADS"};S.patKit=Array.from({length:128},(_,p)=>p===2?1:p===5?2:p>=16?p%128:0);

/* Example kit + pattern, built from the manual's own tips */
(()=>{const T=S.tracks;
 /* 1: SuperWave bass. The manual's "synth-bass bump": WDTH ~64, WOFS ~32, LPQ up. Outputs off: track 2 processes it. */
 T[0].v.SYN=[60,18,24,0,0,40,20,64];T[0].v.AMP=[0,4,16,24,72,104,64,0];T[0].v.FLT=[6,64,0,70,0,44,0,32];T[0].v.EFX=[64,64,0,48,64,0,0,127];T[0].out={AB:false,CD:false,EF:false};
 T[0].v.LF1=[3,1,1,0,1,32,0,20];
 const bass=[[0,36],[3,36],[6,48],[8,36],[10,39],[11,36],[14,43],[16,36],[19,36],[22,48],[24,34],[26,41],[27,34],[30,46]];
 bass.forEach(([s,n])=>T[0].steps[s]=note(n,[1,1,s%16===0?1:0]));T[0].steps[12]={off:1};T[0].steps[28]={off:1};T[0].slide=new Set([8,24]);
 /* 2: FX chorus on the bass (NEIBOR). One trig opens it; DEC and REL at 127 let the audio through. */
 T[1].inp="NEIBOR";T[1].v.SYN=[40,50,30,70,20,100,110,100];T[1].v.AMP=[0,127,127,127,64,110,64,0];T[1].steps[0]=note(60);
 /* 3: SID arp: the manual's "computer game" chords: low SPD, SID mode, envelope trigs off. */
 T[2].v.SYN=[80,30,1,2,0,0,0,64];T[2].v.AMP=[0,60,70,40,64,80,50,0];T[2].v.FLT=[20,90,0,30,0,50,0,0];T[2].v.EFX=[64,64,0,24,-40+64,50,10,100];
 T[2].v.LF1=[0,0,0,0,2,64,0,6];T[2].v.LF2=[5,7,1,4,0,16,0,40];
 [[0,[60,63,67]],[8,[56,60,63]],[16,[53,56,60]],[24,[55,59,62]]].forEach(([s,ch])=>T[2].steps[s]=note(ch));
 Object.assign(T[2].arp,{MODE:2,PLAY:1,SPD:3,RNGE:2,OJMP:0,amp:0,flt:0,lfo:0,len:8});T[2].arp.ofs=[0,0,12,0,0,7,0,-12,0,0,0,0,0,0,0,0];T[2].arp.rhy[3]=false;
 /* 4: VO-6 spells "monomachine" (the manual's tutorial): mo, no, ma, shi, n. */
 T[3].v.SYN=[35,5,1,0,9,5,100,64];T[3].v.AMP=[0,0,127,0,64,110,64,0];T[3].out={AB:false,CD:true,EF:false};
 [0,3,6,9,12].forEach(s=>T[3].steps[s]=note(41));T[3].steps[5]={off:1};
 [[16,19,22,25,28]].forEach(a=>a.forEach(s=>T[3].steps[s]=note(41)));T[3].steps[21]={off:1};
 /* 5: DigiPRO BeatBox. The key picks the drum. Transpose SCALE = FIX keeps it out of song and multi-trig transposes. */
 T[4].v.SYN=[64,0,0,0,0,0,0,0];T[4].v.AMP=[0,60,60,30,70,100,64,0];T[4].tr.SCALE=1;
 const dr={0:48,2:56,4:49,6:56,8:48,10:48,12:49,14:57,16:48,18:56,20:49,22:56,24:48,26:56,28:49,29:49,30:49,31:49};Object.entries(dr).forEach(([s,n])=>T[4].steps[+s]=note(n));
 /* 6: Gate reverb as an insert on mix bus AB: only tracks 1-5 on AB reach it. */
 T[5].inp="BUS AB";T[5].v.SYN=[60,50,70,40,20,110,0,100];T[5].v.AMP=[0,127,127,127,64,100,64,0];T[5].steps[0]=note(60);
 /* MIDI track 2: pad chords with real lengths */
 const M=S.midi[1];M.v.MID=[96,90,64,0,40,0,0,0];M.steps[0]=note([48,51,55,58]);M.steps[16]=note([44,48,51,55]);
 S.midi[0].steps[0]=note(36);S.midi[0].steps[8]=note(36);
 const L=(t,pid,pairs)=>pairs.forEach(([s,v])=>setLock(t,pid,s,v,true));
 L(0,"FLT.1",[[0,40],[8,96],[16,40],[24,110]]);
 L(3,"SYN.0",[[0,35],[3,35],[6,127],[9,40],[12,40],[16,35],[19,35],[22,127],[25,40],[28,40]]);
 L(3,"SYN.1",[[0,5],[3,5],[6,60],[9,110],[12,110],[16,5],[19,5],[22,60],[25,110],[28,110]]);
 L(3,"SYN.4",[[0,9],[3,10],[6,9],[9,15],[12,10],[16,9],[19,10],[22,9],[25,15],[28,10]]);
 L(3,"SYN.5",[[0,5],[3,5],[6,5],[9,0],[12,5],[16,5],[19,5],[22,5],[25,0],[28,5]]);
 L(3,"SYN.2",[[12,0],[28,0]]);
 L(4,"SYN.4",[[29,40],[30,80],[31,127]]);L(4,"SYN.5",[[29,20],[30,14],[31,8]]);
 L(2,"FLT.0",[[16,50],[24,70]]);
 L(7,"MID.1",[[16,70]]);
})();

/* ===== Model helpers ===== */
const trk=t=>t<6?S.tracks[t]:S.midi[t-6];
const isMidiT=t=>t>=6;
const tLabel=t=>t<6?"T"+(t+1):"M"+(t-5);
function side(){return(S.ws==="seq"||S.ws==="sound")&&S.side==="midi"?[6,7,8,9,10,11]:[0,1,2,3,4,5]}
function pagesOf(t){return isMidiT(t)?["MID"]:PAGES}
function pnames(t,pg){if(pg==="SYN")return MACH[trk(t).m].p;return FIXED[pg]}
function pname(t,pid){const[pg,i]=pid.split(".");return pnames(t,pg)[+i]||null}
function pidLabel(t,pid){const[pg]=pid.split(".");return pg+" "+pname(t,pid)}
function destNames(t,pidx){const pg=LPAGES[pidx];if(pg==="PTCH")return PTCHD;if(pg==="MID")return FIXED.MID;const a=[...pnames(t,pg)].map(n=>n||"—");while(a.length<8)a.push("—");return a}
function meta(t,pg,i){const tr=trk(t),name=pnames(t,pg)[i];
 if(pg.startsWith("LF")){if(i===0)return{name,en:LPAGES};if(i===1)return{name,en:destNames(t,tr.v[pg][0])};if(i===2)return{name,en:LTRIG};if(i===3)return{name,en:LWAVE};if(i===4)return{name,en:LMULT};return{name,max:127}}
 if(pg==="SYN"){const en=EN[tr.m+"."+name]||EN[name];if(en)return{name,en};if(tr.m==="DPRO-WAVE"&&name==="WAVE")return{name,max:31}}
 return{name,max:127,signed:SIGNED.has(name)||(pg==="SYN"&&/^PCH/.test(name))}}
/* LEN (P7): a click steps a page and goes round (16 32 48 64 16 ...), shift-click back; a scroll one step, 2-64.
   It used to stop at 64: a click on the factory patterns' LEN 64 did nothing. */
function lenStep(len,d,fine){if(fine)return clamp(len+d,2,64);const p=Math.ceil(len/16)+d;return(((p-1)%4+4)%4+1)*16}
const maxOf=m=>m.en?m.en.length-1:m.max;
function fmt(m,v){if(v==null)return"—";if(m.en)return m.en[clamp(v,0,m.en.length-1)];if(m.signed){const d=v-64;return(d>0?"+":"")+d}return String(v)}
function getP(t,pid){const[pg,i]=pid.split(".");return trk(t).v[pg]?.[+i]}
function lkKey(t,pid){return t+"|"+pid}
function setLock(t,pid,s,v,quiet){const k=lkKey(t,pid);if(!S.locks.has(k)){if(S.locks.size>=62){if(!quiet)toast("All 62 locked parameters are in use. Clear one before you lock a new parameter.");return false}S.locks.set(k,new Map())}S.locks.get(k).set(s,v);return true}
function stepLocked(t,s){for(const[k,m] of S.locks)if(+k.split("|")[0]===t&&m.has(s))return true;return false}
function trackLockPids(t){return[...S.locks.keys()].filter(k=>+k.split("|")[0]===t).map(k=>k.split("|")[1])}
function clearStepLocks(t,s){for(const[k,m] of [...S.locks])if(+k.split("|")[0]===t){m.delete(s);if(!m.size)S.locks.delete(k)}}
function audible(t){const all=[...S.tracks,...S.midi],any=all.some(x=>x.solo);return any?trk(t).solo:!trk(t).mute}
function stepKind(st){if(!st)return null;if(st.off)return"off";if(st.a&&st.f&&st.l)return"full";if(!st.a&&!st.f&&!st.l)return"trigless";return"part"}
/* LED colour the way the hardware shows it for the current TRIG SELECT */
function lampOf(t,st){if(!st)return"";if(st.off)return"y";if(isMidiT(t))return"r";const k=S.trigSel;if(k==="ALL")return stepKind(st)==="full"?"r":"g";const b={AMP:"a",FLT:"f",LFO:"l"}[k];return st[b]?"r":"g"}
function lastNote(t,s){const tr=trk(t);for(let k=s-1;k>=-S.len;k--){const st=tr.steps[(k+S.len)%S.len];if(st?.n)return st.n[0]}return isMidiT(t)?60:48}
const patName=p=>"ABCDEFGH"[p>>4]+String((p&15)+1).padStart(2,"0");
const kitName=k=>"K"+String(k+1).padStart(2,"0")+" "+((k===S.kit?S.workName:S.kits[k].name)||"EMPTY");
