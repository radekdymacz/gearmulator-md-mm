
/* ===== Gates, arpeggiator, transpose, trig setup (the dock under the note lane) ===== */
const ARPM=["OFF","KEY","SID","ADD"],ARPP=["TRUE","UP","DOWN","CYCLE","RND"],SCALES=["---","FIX","MAJ","MIN"];
const ARPHELP={0:"<b>OFF</b> The track plays the base note of each trig. Chord notes wait for the arpeggiator or POLY.",
 1:"<b>KEY</b> Runs while notes are held and plays only held notes. Release all and it stops.",
 2:"<b>SID</b> Needs two or more notes. They stay in the cycle after release until a new chord.",
 3:"<b>ADD</b> Starts on the first note. Each extra key joins while one key is held."};
function midiLenSteps(t,s){const v=S.locks.get(lkKey(t,"MID.0"))?.get(s)??trk(t).v.MID[0];return v>=127?Infinity:Math.max(.25,v/8)}
/* a gate runs from a note trig to its NOTE OFF or the next trig (synth tracks), or for LEN (MIDI) */
function noteSpans(t){const tr=trk(t),out=[],L=S.len;
 for(let s=0;s<L;s++){const st=tr.steps[s];if(!st||st.off)continue;let e=s+1;
  if(isMidiT(t)){const d=midiLenSteps(t,s);if(d===Infinity){e=L;for(let k=s+1;k<L;k++)if(tr.steps[k]?.off){e=k;break}}else e=Math.min(L,s+d)}
  else{while(e<L&&!tr.steps[e])e++}
  out.push({s,e,n:st.n?st.n:[lastNote(t,s)],st,kind:stepKind(st),pitchless:!st.n,endOff:!!tr.steps[e]?.off})}return out}
/* honest gate: the amp envelope may die before the gate ends (HOLD, then DEC). Estimate, in steps. */
function ampSteps(t){if(isMidiT(t))return Infinity;const a=trk(t).v.AMP;if(a[2]>=127)return Infinity;return(a[0]+a[1]+a[2])/127*16}
function arpSeq(tr,chord){const a=tr.arp;let c=[...chord];if(a.PLAY===1)c.sort((x,y)=>x-y);if(a.PLAY===2)c.sort((x,y)=>y-x);if(a.PLAY===3){c.sort((x,y)=>x-y);c=[...c,...c.slice(1,-1).reverse()]}
 let seq=[];for(let o=0;o<=a.RNGE;o++)seq=seq.concat(c.map(n=>n+12*o));if(a.PLAY===4){let h=7;seq=seq.map(()=>seq[(h=(h*31+11)%97)%seq.length])}return seq}
function arpTicks(t,span){const tr=trk(t),a=tr.arp;if(!a.MODE||!a.SPD)return[];if(a.MODE===2&&span.n.length<2)return[];const tl=a.SPD/6,seq=arpSeq(tr,span.n),out=[];
 for(let k=0;k*tl<span.e-span.s&&k<256;k++){const r=k%a.len;if(!a.rhy[r])continue;out.push({x:span.s+k*tl,w:tl,n:seq[k%seq.length]+a.ofs[r]})}return out}
/* a lane-style key per arp step: the offset is a bipolar LED meter from the middle line (±24 = the half key) */
function arpCells(a){return Array.from({length:16},(_,k)=>{const o=a.ofs[k],on=a.rhy[k],na=k>=a.len,f=(Math.abs(o)/24).toFixed(4);
 return`<div class="ac ${na?"na":""} ${on?"":"mute"}" data-ac="${k}" title="Step ${k+1}: ${na?"past the end (click to extend)":on?"plays, offset "+(o>0?"+":"")+o:"muted"}. Click = mute, drag = offset.">${na?"":`<span class="z"></span><i class="${o>=0?"up":"dn"}" style="${o>=0?`bottom:50%;height:max(2px,calc((50% - 8px)*${f}))`:`top:50%;height:calc((50% - 8px)*${f})`}"></i><b>${o?(o>0?"+":"")+o:k+1}</b>`}</div>`}).join("")}
/* The dock's tabs share one layout, the lock lane's: the rail's LOCK PARAMETER keys area holds the panel's
   choices as keys (a group label over each row, the lock-parameter key style), the dock body holds one row of
   value boxes on eight fixed columns and the panel's picture under it, down to the lane's bottom line. The
   help is one muted line in the header (the long text in its tooltip). Nothing here sizes the dock. */
const dkk=(attrs,on,txt,title="")=>`<button class="pk dkk" ${attrs} aria-pressed="${!!on}"${title?` title="${title}"`:""}>${txt}</button>`;
const dkGroup=(label,keys,n,attrs="")=>`<div class="dkg"><span class="dkl">${label}</span><div class="dkrow ${n>4?"tight":""}" style="--n:${n}" ${attrs}>${keys}</div></div>`;
const dkAttr=s=>String(s).replace(/&/g,"&amp;").replace(/"/g,"&quot;").replace(/</g,"&lt;");
const dkPlain=h=>h.replace(/<b>([^<]*)<\/b>/g,"$1:").replace(/<[^>]+>/g,"");
function dockKeys(t,d){const tr=trk(t),midi=isMidiT(t);
 if(d==="arp"){const a=tr.arp;return dkGroup("Mode",ARPM.map((m,i)=>dkk(`data-v="${i}"`,a.MODE===i,m)).join(""),4,`data-set="arpmode"`)
  +dkGroup("Play",ARPP.map((m,i)=>dkk(`data-v="${i}"`,a.PLAY===i,m)).join(""),5,`data-set="arpplay"`)
  +dkGroup("Trig",midi?`<span class="dkna" title="MIDI arpeggiators have no envelope switches">none on MIDI</span>`:["amp","flt","lfo"].map(k=>dkk(`data-arptrig="${k}"`,a[k],k.toUpperCase(),`The arpeggiator's notes retrigger the ${k.toUpperCase()} envelope`)).join(""),3)}
 if(d==="trn"){const sc=tr.tr.SCALE;return dkGroup("Scale",SCALES.map((m,i)=>dkk(`data-v="${i}"`,sc===i,m)).join(""),4,`data-set="scale"`)
  +`<div class="dkg"><span class="dkl">Key</span><span class="dkna" title="${sc>1?"Click a key on the keyboard to set the root":"KEY only matters for MAJ and MIN"}">${sc>1?KEYS[tr.tr.KEY]+" · click the keyboard":"only for MAJ and MIN"}</span></div>`}
 if(d==="trig"){const tp=tr.trigpos??"";return dkGroup("Trig position",[dkk(`data-v=""`,tp==="","—","No forwarding")].concat([0,1,2,3,4,5].filter(k=>k!==t).map(k=>dkk(`data-v="${k}"`,tp===k,"T"+(k+1),"Also play "+tLabel(k)+" "+S.tracks[k].m))).join(""),6,`data-set="trigpos"`)
  +dkGroup("Port",["ALWAYS","LEGATO"].map((m,i)=>dkk(`data-v="${i}"`,tr.port===i,m,i?"ONLY LEGATO: glide only between overlapping notes":"Glide on every note")).join(""),2,`data-set="port"`)
  +dkGroup("Legato",["amp","flt","lfo"].map(k=>dkk(`data-leg="${k}"`,tr.leg[k],k.toUpperCase(),`On: the ${k.toUpperCase()} envelope still fires when notes overlap`)).join(""),3)}
 return dkGroup("Channel",Array.from({length:16},(_,k)=>dkk(`data-v="${k+1}"`,tr.ch===k+1,k+1,"MIDI channel "+(k+1))).join(""),8,`data-set="mch"`)
  +`<div class="dkg"><span class="dkl">LFOs</span><span class="dkna">shared with T${t-5} ${S.tracks[t-6].m}</span></div>`}
/* the header: a readout and one help line */
function dockRead(t,d){const tr=trk(t);
 if(d==="arp")return`<span id="arpcap">Rhythm + offset · ${tr.arp.len} steps</span>`;
 if(d==="trn"){const n=tr.tr.TRACK-64+(tr.tr.SCALE===1?0:S.patTrn-64);return`plays ${n>0?"+":""}${n} semitones${tr.tr.SCALE===1?" · FIX: pattern ignored":""}`}
 if(d==="trig"){const a=ampSteps(t);return a===Infinity?"the gate sustains":`a gate sounds ≈ ${a<10?a.toFixed(1):Math.round(a)} steps`}
 return"CH "+String(tr.ch).padStart(2,"0")}
const TRNHELP=["<b>---</b> no transpose scale: track, pattern, song and multi-trig transpose add up and any note can sound. Click a key: C plays it (up from C; Alt-click: down to it).","<b>FIX</b> only track transpose applies. Use it for drums and FX. Click a key: C plays it (up from C; Alt-click: down to it).","<b>MAJ</b> transposed notes stay in the major scale of KEY: click a key to set it. Out-of-scale keys and roll rows are hatched.","<b>MIN</b> transposed notes stay in the minor scale of KEY: click a key to set it. Out-of-scale keys and roll rows are hatched."];
function dockHelp(t,d){const tr=trk(t);
 if(d==="arp"){const h=dkPlain(ARPHELP[tr.arp.MODE]);return[h,h+" SPD 6 = a 16th. Chords come from the trig: shift-click in the roll. The light notes in the roll are what it plays. In the strip: click a step to mute it, drag it up or down for an offset, click past the end or the LEN row under it to set the length. FUNCTION + ARP: one per track, 12 per pattern."]}
 if(d==="trn"){const h=dkPlain(TRNHELP[tr.tr.SCALE]);return[h,h+" Transpose is live: the programmed notes do not change. Song rows and multi trig add their own. FUNCTION + TRANSPOSE."]}
 if(d==="trig")return["Drag the dots or the values · A F L: the envelopes an overlapping note restarts (LEGATO)","ATK, HOLD and DEC shape how long a gate really sounds, in steps after the trig (the light part of a bar in the roll); DEC 127 holds to the NOTE OFF. PORT is the glide to the next note: ALWAYS, or only when notes overlap. LEGATO picks the envelopes that still fire when notes overlap (filled A F L): all on = staccato bass, all off = a smooth lead. TRIG POSITION forwards this track's notes to another track. KIT › TRIG."];
 return["LEN 127 = until a NOTE OFF · PCHG only sends when locked","LEN 127 = until a NOTE OFF; drag a note's end in the roll to lock its LEN. PCHG only sends when it is locked. CL1-4 pick the CC numbers (or AFT) of the four CC values above them. Internal tracks can also send MIDI (GLOBAL › CONTROL OUT1), but without these values. GLOBAL › MIDI SEQ › MIDISEQ SET: the page is stored in the kit."]}
/* the body: value boxes on eight columns, then the picture */
const dkVals=(cells,cls="")=>`<div class="dkvals ${cls}">${cells.join("")}</div>`;
/* TRIG SETUP and TRANSPOSE are not per step, so their pictures are LCD plates (the Sound page's small plots),
   not lane bars: one gate's envelope on its own time axis, and a one-octave keyboard. */
const u16=v=>v/127*16;	/* AMP ATK, HOLD, DEC in steps (ampSteps' estimate: 127 = a bar) */
const dkSteps=a=>a<10?a.toFixed(1):String(Math.round(a));
/* one gate: ATK up, HOLD flat, DEC down (DEC 127 holds to the NOTE OFF), against steps after the trig. On the
   right two overlapping notes: the glide between them (PORT, ALWAYS or only LEGATO) and, where the second one
   starts, which envelopes fire again (LEGATO's A F L). The axis grows while a dot is dragged past its end. */
ED.dktrig={geo(W,H,c){const a=trk(S.sel).v.AMP,at=u16(a[0]),ho=u16(a[1]),inf=a[2]>=127,de=inf?Infinity:u16(a[2]),tot=at+ho+(inf?0:de);
  const need=clamp(Math.ceil(((inf?at+ho+2:tot)*1.12+.3)/4)*4,4,52),span=active&&active.c===c&&c._span?Math.max(c._span,need):need;c._span=span;
  const gw=clamp(Math.round(W*.26),170,300),x0=12,x1=W-gw-30,T=24,B=H-17;
  return{a,at,ho,de,inf,tot,span,x0,x1,T,B,X:s=>x0+s/span*(x1-x0),S:x=>(x-x0)/(x1-x0)*span,Y:v=>B-v*(B-T),g0:W-gw-8,g1:W-10}},
 draw(g,W,H,c){const G=this.geo(W,H,c),ink=cssv("--ink"),lcd=cssv("--lcd"),tr=trk(S.sel);g.font=SFONT;
  /* the time axis: 0 = the trig, a tick and its number after each step (every 2nd or 4th when long) */
  const ev=G.span>32?4:G.span>16?2:1;g.fillStyle=inkA(.25);g.fillRect(G.x0,G.B,G.x1-G.x0,1);
  for(let k=1;k<=G.span;k++){const xa=Math.round(G.X(k));g.fillStyle=inkA(k%4?.07:.14);g.fillRect(xa,G.T-6,1,G.B-G.T+6);if(k%ev)continue;g.fillStyle=inkA(.4);g.fillRect(xa,G.B,1,4);g.fillStyle=inkA(.75);const tx=String(k);g.fillText(tx,xa-tx.length*2.5,H-3)}
  g.fillStyle=inkA(.75);g.fillText("TRIG",G.x0-2,H-3);
  /* the envelope */
  const end=G.inf?G.x1:G.X(G.tot),pts=[[G.x0,G.Y(0)],[G.X(G.at),G.Y(1)],[G.X(G.at+G.ho),G.Y(1)],[end,G.inf?G.Y(1):G.Y(0)]];
  g.beginPath();pts.forEach(([x,y],i)=>i?g.lineTo(x,y):g.moveTo(x,y));g.lineTo(end,G.B);g.closePath();g.fillStyle=inkA(.13);g.fill();
  g.beginPath();pts.forEach(([x,y],i)=>i?g.lineTo(x,y):g.moveTo(x,y));g.strokeStyle=ink;g.lineWidth=2.2;g.stroke();
  if(G.inf){g.fillStyle=ink;g.beginPath();g.moveTo(G.x1+7,G.Y(1));g.lineTo(G.x1,G.Y(1)-4);g.lineTo(G.x1,G.Y(1)+4);g.fill()}
  else{g.setLineDash([3,3]);g.strokeStyle=inkA(.6);g.lineWidth=1;g.beginPath();g.moveTo(Math.round(end)+.5,G.T-6);g.lineTo(Math.round(end)+.5,G.B);g.stroke();g.setLineDash([]);
   g.fillStyle=ink;const t="≈"+dkSteps(G.tot);g.fillText(t,Math.min(G.x1-t.length*5,end+4),G.T)}
  /* segment names, inside the shape where they fit */
  [["ATK",0,G.at],["HOLD",G.at,G.at+G.ho],["DEC",G.at+G.ho,G.inf?G.span:G.tot]].forEach(([n,s0,s1])=>{const w=G.X(s1)-G.X(s0);if(w<n.length*6+4)return;g.fillStyle=ink;g.fillText(n,(G.X(s0)+G.X(Math.min(s1,G.span)))/2-n.length*3,G.B-5)});
  label(g,G.inf?"one gate · DEC 127 holds to the NOTE OFF":"one gate · steps after the trig");
  /* the glide: note 1 low, note 2 high, starting before note 1 ends (they overlap); the line is the pitch */
  const L=this.leg(W,H),on=["amp","flt","lfo"].filter(k=>tr.leg[k]);g.fillStyle=inkA(.2);g.fillRect(G.g0-14,6,1,H-12);
  g.fillStyle=inkA(.35);g.fillRect(L.n1,L.yl-5,L.e1-L.n1,10);g.fillRect(L.s2,L.yh-5,L.n2-L.s2,10);
  g.beginPath();g.moveTo(L.n1,L.yl);g.lineTo(L.s2,L.yl);g.lineTo(L.s2+L.pw,L.yh);g.lineTo(L.n2,L.yh);g.strokeStyle=ink;g.lineWidth=2.2;g.stroke();
  g.fillStyle=ink;g.fillText(tr.port?"PORT · ONLY WHEN NOTES OVERLAP":"PORT · GLIDES TO EVERY NOTE",G.g0,15);
  g.fillStyle=inkA(.75);g.fillText(L.pw<1?"NO GLIDE":"GLIDE",L.s2+L.pw/2+9,(L.yl+L.yh)/2+3);
  /* LEGATO, under where note 2 starts over note 1: the envelopes that fire again (filled) or run on (open) */
  g.setLineDash([2,3]);g.strokeStyle=inkA(.5);g.lineWidth=1;g.beginPath();g.moveTo(Math.round(L.s2)+.5,L.yh+6);g.lineTo(Math.round(L.s2)+.5,L.ym-2);g.stroke();g.setLineDash([]);
  ["amp","flt","lfo"].forEach((k,i)=>{const x=L.s2+i*15,y=L.ym,f=!!tr.leg[k];g.lineWidth=1.2;g.strokeStyle=ink;g.strokeRect(x+.5,y+.5,12,12);if(f){g.fillStyle=ink;g.fillRect(x+.5,y+.5,12,12)}g.fillStyle=f?lcd:ink;g.fillText(k[0].toUpperCase(),x+3.5,y+10)});
  g.fillStyle=inkA(.75);g.fillText(on.length?on.join(" ").toUpperCase()+" FIRE AGAIN":"NONE FIRE AGAIN: SMOOTH",L.s2+50,L.ym+10)},
 leg(W,H){const G=this.geo(W,H,{}),w=G.g1-G.g0;return{n1:G.g0,e1:G.g0+w*.5,s2:G.g0+w*.36,n2:G.g1,yl:H-44,yh:32,ym:H-17,pw:trk(S.sel).v.AMP[7]/127*w*.5}},
 handles(W,H,c){const G=this.geo(W,H,c),a=G.a,L=this.leg(W,H),st=x=>clamp(Math.round(u16inv(Math.max(0,G.S(x)))),0,127);
  return[{x:G.X(G.at),y:G.Y(1),k:"ATK",drag:x=>a[0]=st(x)},{x:G.X(G.at+G.ho),y:G.Y(1),k:"HOLD",drag:x=>a[1]=clamp(Math.round(u16inv(Math.max(0,G.S(x)-G.at))),0,127)},
   {x:G.inf?G.x1:G.X(G.tot),y:G.inf?G.Y(1):G.Y(0),k:"DEC",drag:x=>a[2]=x>=G.x1-1?127:clamp(Math.round(u16inv(Math.max(0,G.S(x)-G.at-G.ho))),0,126)},
   {x:L.s2+L.pw,y:L.yh,k:"PORT",drag:x=>a[7]=clamp(Math.round((x-L.s2)/((L.n2-L.n1)*.5)*127),0,127)}]}};
function u16inv(s){return s/16*127}
/* TRANSPOSE: C to C on an LCD plate. MAJ/MIN: the scale's keys lit, the others hatched (as the roll's rows), the
   root (KEY) marked; a click on a key sets KEY. --- and FIX: the shift the track plays, as an arrow from C (from
   the upper C when it goes down). */
const BLK=[1,3,6,8,10];
ED.dktrn={geo(W,H){const T=22,B=H-6,kh=B-T,ww=clamp(Math.round(kh*.5),30,64);return{T,B,kh,ww,x0:12,bw:ww*.62,bh:kh*.58}},
 keys(W,H){const G=this.geo(W,H),out=[];let wi=0;for(let n=0;n<=12;n++){if(BLK.includes(n%12))out.push({n,blk:1,x:G.x0+wi*G.ww-G.bw/2,y:G.T,w:G.bw,h:G.bh});else{out.push({n,blk:0,x:G.x0+wi*G.ww,y:G.T,w:G.ww,h:G.kh});wi++}}return out},
 keyAt(c,e){const r=c.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top,K=this.keys(r.width,r.height),hit=k=>x>=k.x&&x<k.x+k.w&&y>=k.y&&y<k.y+k.h;
  const k=K.find(k=>k.blk&&hit(k))||K.find(k=>!k.blk&&hit(k));return k?k.n:null},	/* 0-12: the upper C is 12 */
 shift(tr){return tr.tr.TRACK-64+(tr.tr.SCALE===1?0:S.patTrn-64)},
 draw(g,W,H){const G=this.geo(W,H),K=this.keys(W,H),tr=trk(S.sel),sc=tr.tr.SCALE,key=tr.tr.KEY,ink=cssv("--ink"),lcd=cssv("--lcd"),N=this.shift(tr),sv=sc>1;
  const src=N<0?12:0,dst=src+(N%12===0&&N?(N>0?12:-12):N%12),cx=n=>{const k=K.find(k=>k.n===n);return k.x+k.w/2},kOf=n=>K.find(k=>k.n===n);g.font=SFONT;
  const hatch=k=>{g.save();g.beginPath();g.rect(k.x+1,k.y+1,k.w-2,k.h-2);g.clip();g.strokeStyle=inkA(.22);g.lineWidth=1;for(let d=-k.h;d<k.w;d+=5){g.beginPath();g.moveTo(k.x+d,k.y+k.h);g.lineTo(k.x+d+k.h,k.y);g.stroke()}g.restore()};
  K.filter(k=>!k.blk).forEach(k=>{if(sv&&!inScale(tr,k.n))hatch(k);g.strokeStyle=ink;g.lineWidth=1.2;g.strokeRect(k.x+.5,k.y+.5,k.w-1,k.h-1)});
  K.filter(k=>k.blk).forEach(k=>{g.fillStyle=sv&&!inScale(tr,k.n)?inkA(.4):ink;g.fillRect(k.x,k.y,k.w,k.h)});
  const hk=this.hov!=null&&kOf(+this.hov.split("|")[0]);if(hk){g.fillStyle=hk.blk?lcd:inkA(.14);g.globalAlpha=hk.blk?.35:1;g.fillRect(hk.x+2,hk.y+2,hk.w-4,hk.h-4);g.globalAlpha=1}
  const dot=(n,fill,r=4.5)=>{const k=kOf(n),x=k.x+k.w/2,y=k.blk?k.y+k.h-10:k.y+k.h-24;g.beginPath();g.arc(x,y,r,0,7);if(fill){g.fillStyle=k.blk?lcd:ink;g.fill()}else{g.strokeStyle=k.blk?lcd:ink;g.lineWidth=1.6;g.stroke()}};
  K.filter(k=>!k.blk).forEach(k=>{const r=sv&&k.n%12===key;g.fillStyle=r?ink:inkA(.6);g.fillText(KEYS[k.n%12],k.x+k.w/2-3,k.y+k.h-6)});
  if(sv){for(let n=0;n<=12;n++)if(inScale(tr,n%12))dot(n,1,n%12===key?5.5:3.5);
   const k=kOf(key);g.strokeStyle=k.blk?lcd:ink;g.lineWidth=1.5;g.beginPath();g.arc(k.x+k.w/2,k.blk?k.y+k.h-10:k.y+k.h-24,9,0,7);g.stroke();if(key===0){g.beginPath();g.arc(cx(12),kOf(12).y+kOf(12).h-24,9,0,7);g.stroke()}}
  else{dot(src,0,5);if(N){dot(dst,1,5);const y=G.T+G.kh-24,xa=cx(src),xb=cx(dst),dir=Math.sign(xb-xa);g.strokeStyle=ink;g.lineWidth=2;g.beginPath();g.moveTo(xa+dir*7,y);g.lineTo(xb-dir*9,y);g.stroke();
    g.fillStyle=ink;g.beginPath();g.moveTo(xb-dir*3,y);g.lineTo(xb-dir*10,y-4.5);g.lineTo(xb-dir*10,y+4.5);g.fill()}}
  /* the words beside the keyboard */
  const tx=G.x0+8*G.ww+22,big="16px Silkscreen, monospace",xo=Math.floor((Math.abs(N)-1)/12),oct=N&&xo?`, ${N>0?"+":"−"}${xo} oct`:"";
  const lines=sv?[[`${KEYS[key]} ${sc===2?"major":"minor"}`,`lit keys are in the scale · ${N?"shifted "+(N>0?"+":"")+N+", then kept in it":"no shift"}`,"click a key to set the root (KEY)"]]
   :[[N?`plays ${N>0?"+":""}${N} semitone${Math.abs(N)>1?"s":""}`:"no shift",sc===1?`FIX: track ${tr.tr.TRACK-64>0?"+":""}${tr.tr.TRACK-64} only · pattern, song and multi trig ignored`:`track ${tr.tr.TRACK-64>0?"+":""}${tr.tr.TRACK-64} + pattern ${S.patTrn-64>0?"+":""}${S.patTrn-64} · no scale: every note can sound`,N?`C plays the ${noteName12(dst)} ${N>0?"above":"below"}${oct}`:"KEY is only for MAJ and MIN"]];
  const hv=this.hov&&this.hov.split("|"),[l1,l2,l3]=hv?[lines[0][0],lines[0][1],this.effect(tr,+hv[0],hv[1]==="1").tip]:lines[0];g.fillStyle=ink;g.font=big;g.fillText(l1.toUpperCase(),tx,G.T+18);g.font="10px Silkscreen, monospace";g.fillStyle=inkA(.8);g.fillText(l2.toUpperCase(),tx,G.T+40);g.fillText(l3.toUpperCase(),tx,G.T+58);
  label(g,"transpose · "+(["no scale","fix","maj","min"][sc]))},
 /* what a click on key n (0-12) does. MAJ/MIN: KEY = n. --- and FIX: the track transpose so that C plays n, up
    from C (0..+12), or with Alt down to it (−12..0); in --- the pattern's own shift is counted in. */
 effect(tr,n,alt){const sc=tr.tr.SCALE;if(sc>1){const key=n%12;return{key,note:key,tip:key===tr.tr.KEY?`${KEYS[key]} is the root (KEY)`:`click: root (KEY) ${KEYS[key]}`}}
  const want=alt?n-12:n,pat=sc===1?0:S.patTrn-64,TRACK=clamp(64+want-pat,0,127),N=TRACK-64+pat,sg=v=>(v>0?"+":"")+v;
  return{TRACK,note:N,tip:`click: C plays ${noteName12(N)} ${N>0?"above":N<0?"below":"(no shift)"} · track ${sg(TRACK-64)}${pat?` + pattern ${sg(pat)} = ${sg(N)}`:""}${alt?"":" · Alt-click: down"}`}},
 hov:null,
 cursor(c,e){const n=this.keyAt(c,e),k=n==null?null:n+"|"+(e.altKey?1:0);if(k!==this.hov){this.hov=k;c.title=n==null?"":this.effect(trk(S.sel),n,e.altKey).tip;drawEd(c)}return n!=null?"pointer":"default"}};
/* a press on the plate: set KEY or TRACK (one undo step) and play the note it makes on the selected synth track
   until the button comes up, as the home-row keys do */
let trnHeld=null;
function trnDown(c,e){const t=S.sel,tr=trk(t),x=ED.dktrn.keyAt(c,e);if(x==null)return;const n=ED.dktrn.effect(tr,x,e.altKey);
 if(n.key!=null?n.key!==tr.tr.KEY:n.TRACK!==tr.tr.TRACK){if(n.key!=null){tr.tr.KEY=n.key;edit("transpose",{t,key:n.key})}else{tr.tr.TRACK=n.TRACK;edit("transpose",{t,v:n.TRACK-64})}render()}
 if(isMidiT(t))return;const note=clamp(KEYS_BASE+12*KB.oct+n.note);trnHeld={t,note};keyNote(t,note,KB.vel)}
document.addEventListener("pointerout",e=>{const c=e.target.closest?.('canvas[data-ed="dktrn"]');if(c&&!c.contains(e.relatedTarget)&&ED.dktrn.hov){ED.dktrn.hov=null;c.title="";drawEd(c)}});
function trnUp(){if(!trnHeld)return;const h=trnHeld;trnHeld=null;keyNote(h.t,h.note,0)}
const noteName12=n=>KEYS[((n%12)+12)%12];
function dockVis(t,d){if(d==="trn")return`<canvas class="ed dkplot" data-ed="dktrn" aria-label="Transpose on a one-octave keyboard"></canvas>`;if(d==="trig")return`<canvas class="ed dkplot" data-ed="dktrig" aria-label="One gate's AMP envelope and the glide between two notes; drag the dots"></canvas>`;return""}
/* the lane's scale column, in the left gutter beside the picture */
const dkScale=(a,b,c)=>`<div class="dkscale" aria-hidden="true"><b>${a}</b><b>${b}</b><b>${c}</b></div>`;
/* a value box moved: the picture and the readout follow (called from syncControls). A canvas is redrawn in
   place, so a dot being dragged keeps its canvas. */
function syncDock(){const v=$("#dkvis[data-live]");if(!v||S.ws!=="seq")return;const t=S.sel,d=S.dock;if(v.querySelector("canvas.ed"))redraw();else v.innerHTML=dockVis(t,d);const r=$("#dkread");if(r)r.innerHTML=dockRead(t,d)}
function arpPanel(t){const a=trk(t).arp;
 return`${dkVals([pc("arp","SPD",{t}),pc("arp","RNGE",{t}),pc("arp","OJMP",{t})])}<div class="dkvis" id="dkvis">
  <div class="arptrack" id="arptrack" title="Rhythm + offset: click a step to mute it, drag it up or down for an offset (−24…+24), click past the end to extend">${arpCells(a)}</div>
  ${dkScale("+24","0","−24")}<div class="arplenrow">${Array.from({length:16},(_,k)=>`<button class="${k<a.len?"on":""}" data-arplen="${k+1}" aria-label="Length ${k+1}" title="Length ${k+1} (the LEVEL knob)"></button>`).join("")}</div></div>`}
function trnPanel(t){return`${dkVals([pc("trn","TRACK",{t,label:"TRACK"}),pc("ptrn","PAT",{t,label:"PAT · all"})])}<div class="dkvis" id="dkvis" data-live="1">${dockVis(t,"trn")}</div>`}
function trigPanel(t){return`${dkVals([pc("AMP",0,{t,label:"ATK"}),pc("AMP",1,{t,label:"HOLD"}),pc("AMP",2,{t,label:"DEC"}),pc("AMP",7,{t,label:"PORT"})])}<div class="dkvis" id="dkvis" data-live="1">${dockVis(t,"trig")}</div>`}
function midiPanel(t){const tr=trk(t);
 return`${dkVals([0,1,2,3,4,5,6,7].map(i=>pc("MID",i,{t,label:i<4?FIXED.MID[i]:"CC "+(tr.cc[i-4]===128?"AFT":tr.cc[i-4])})))}<div class="dkvis" id="dkvis">${dkVals([0,1,2,3].map(()=>`<span class="dkcell"></span>`).concat([0,1,2,3].map(i=>pc("cc",i,{t,label:"CL"+(i+1)}))),"dkvals2")}</div>`}
