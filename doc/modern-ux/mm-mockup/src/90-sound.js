
/* ===== Sound: the seven DATA pages as screens ===== */
const V=(pg)=>trk(S.sel).v[pg];
const synIdx=n=>MACH[trk(S.sel).m].p.indexOf(n);
const sv=n=>{const i=synIdx(n);return i<0?0:V("SYN")[i]};
const sset=(n,v)=>{const i=synIdx(n);if(i>=0)V("SYN")[i]=clamp(Math.round(v))};
ED.syn={draw(g,W,H){const tr=trk(S.sel),m=tr.m,f=famKey(m),ink=cssv("--ink");grid(g,W,H);
  if(m==="SWAVE-SAW"||m==="SWAVE-PULS"){const cx=W/2,sp=(W/2-20)/24,base=H-14,hh=H-40;const bar=(semi,h,lab,dash)=>{const x=cx+semi*sp;g.fillStyle=ink;g.globalAlpha=dash?.55:1;g.fillRect(x-3,base-h*hh,6,h*hh);g.globalAlpha=1;if(lab){g.font="8px Silkscreen, monospace";g.fillText(lab,x-8,base+11)}};
   const uw=sv("UNIW")/127*12,ul=sv("UNIL")/127,ux=Math.min(sv("UNIX"),sv("UNIL"))/127;bar(0,1,"");if(ul){bar(-uw,ul);bar(uw,ul)}g.font="8px Silkscreen, monospace";g.fillText("BASE",cx-10,base+11);if(ul)g.fillText("UNI ±"+Math.round(uw*10)/10,cx+uw*sp+6,base-ul*hh+8);if(m==="SWAVE-SAW"&&ux){bar(-2*uw,ux,"",1);bar(2*uw,ux,"X",1)}
   if(synIdx("SUBX")>=0&&sv("SUBX"))bar(-12.4,sv("SUBX")/127,"SQ");if(sv("SUB1"))bar(-12,sv("SUB1")/127,"-12");if(sv("SUB2"))bar(-24,sv("SUB2")/127,"-24");
   label(g,m==="SWAVE-SAW"?"oscillator stack · saw":"oscillator stack · pulse "+(sv("PW")-64))}
  else if(m==="SWAVE-ENS"||m==="DPRO-DENS"){const n0=0,offs=["PCH2","PCH3","PCH4"].map(p=>sv(p)).filter(v=>v).map(v=>v-64);const keys=25,kw=(W-20)/15,y0=26,wh=H-36;let wx=10;
   for(let k=0;k<keys;k++){const blk=[1,3,6,8,10].includes(k%12);if(blk)continue;g.strokeStyle=ink;g.lineWidth=1;g.strokeRect(wx+.5,y0+.5,kw-1,wh-1);const on=k===n0||offs.includes(k);if(on){g.fillStyle=ink;g.beginPath();g.arc(wx+kw/2,y0+wh-12,5,0,7);g.fill()}wx+=kw}
   wx=10;for(let k=0;k<keys;k++){const blk=[1,3,6,8,10].includes(k%12);if(!blk){wx+=kw;continue}g.fillStyle=ink;g.fillRect(wx-kw*.3,y0,kw*.6,wh*.58);if(offs.includes(k)){g.fillStyle=cssv("--lcd");g.beginPath();g.arc(wx,y0+wh*.45,4,0,7);g.fill()}}
   label(g,"chord from one track: C + "+(offs.length?offs.join(" "):"no extra notes"))}
  else if(m==="SID-6581"){const w=sv("WAVE"),pw=sv("PW")/127;line(g,W,x=>{const p=(x/W*3)%1;let v=[1-4*Math.abs(p-.5),2*p-1,p<pw?1:-1,((p*7)%1<.5?1:-1)*(p<.5?1:.4),RND[Math.floor(x/3)%8]][w];return H/2-v*(H/2-24)},ink,2);
   label(g,EN["SID-6581.WAVE"][w]+" · "+EN["SID-6581.MOD"][sv("MOD")]+(sv("MOD")?" from "+(sv("MSRC")?"PRCH (T"+(S.sel)+")":"MFRQ"):""))}
  else if(m==="VO-6"){const P=[["O",35,5],["A",127,60],["I",40,110],["E",80,100],["U",10,30]];g.font="10px Silkscreen, monospace";
   P.forEach(([l,a,b])=>{const x=10+a/127*(W-20),y=H-10-b/127*(H-34);g.fillStyle=inkA(.5);g.fillText(l,x-3,y+4)});label(g,"vowel · VOC1 → VOC2 ↑ · "+CONS[sv("CONS")]+" + vowel")}
  else if(m==="DPRO-WAVE"||m==="DPRO-DDRW"||m==="DPRO-BBOX"){const a=m==="DPRO-DDRW"?sv("WAV1"):sv("WAVE"),b=m==="DPRO-DDRW"?sv("WAV2"):(a+1)%32,mix=m==="DPRO-DDRW"?sv("MIX")/127:sv("WP")/127;
   const wf=(k,p)=>Math.sin(p*6.283)*(1-(k%5)/6)+Math.sin(p*6.283*((k%7)+2))*((k%5)/6)*.8+(k%3===2?(p<.5?.3:-.3):0);
   if(m==="DPRO-BBOX"){for(let x=0;x<W;x++){const p=x/W,v=Math.exp(-p*6)*Math.sin(p*90+Math.sin(p*400)*2);g.fillStyle=ink;g.fillRect(x,H/2-v*(H/2-24),1,1+Math.abs(v)*2)}label(g,"drum sounds · the key picks one")}
   else{line(g,W,x=>{const p=(x/W*2)%1;return H/2-((1-mix)*wf(a,p)+mix*wf(b,p))*(H/2-26)},ink,2);label(g,m==="DPRO-DDRW"?`wave ${a+1} ↔ ${b+1} · mix ${sv("MIX")}`:`wave ${a+1} → ${b+1} · WP ${sv("WP")}`)}}
  else if(f==="FM+"){const ps=MACH[m].p.filter(p=>/FRQ/.test(p));const bw=(W-40)/(ps.length+1);ps.forEach((p,k)=>{const v=sv(p)/127;g.fillStyle=ink;g.fillRect(20+k*bw,H-12-v*(H-40),bw*.6,v*(H-40));g.font="8px Silkscreen, monospace";g.fillText(p,20+k*bw,H-2)});
   const x=20+ps.length*bw;g.strokeStyle=ink;g.lineWidth=2;g.strokeRect(x,H-12-(H-40)*.5,bw*.6,(H-40)*.5);g.fillText("CAR",x,H-2);label(g,"modulators → carrier · tone "+sv("TONE"))}
  else if(isFx(m)){const inp=trk(S.sel).inp;g.strokeStyle=ink;g.lineWidth=2;g.strokeRect(W/2-50,H/2-18,100,36);g.font="10px Silkscreen, monospace";g.fillStyle=ink;g.fillText(m.slice(3),W/2-44,H/2+4);
   g.beginPath();g.moveTo(14,H/2);g.lineTo(W/2-52,H/2);g.stroke();g.fillText(inputLabel(S.sel),10,H/2-8);g.beginPath();g.moveTo(W/2+50,H/2);g.lineTo(W-14,H/2);g.stroke();g.fillText("TRACK FX",W-76,H/2-8);label(g,"needs a trig to open")}
  else{if(m==="GND-NOIS"){for(let x=0;x<W;x+=2){const v=hashN(x)*2-1;g.fillStyle=ink;g.fillRect(x,H/2,2,-v*(H/2-24)*(1-sv("RED")/200))}}else if(m==="GND-SIN")line(g,W,x=>H/2-Math.sin(x/W*6.283*2)*(H/2-24),ink,2);label(g,machName(m))}},
 handles(W,H){const m=trk(S.sel).m;
  if(m==="SWAVE-SAW"||m==="SWAVE-PULS"){const cx=W/2,sp=(W/2-20)/24,base=H-14,hh=H-40;return[{x:cx+sv("UNIW")/127*12*sp,y:base-sv("UNIL")/127*hh,k:"UNIW · UNIL",drag:(x,y)=>{sset("UNIW",(x-cx)/sp/12*127);sset("UNIL",(base-y)/hh*127)}}]}
  if(m==="VO-6")return[{x:10+sv("VOC1")/127*(W-20),y:H-10-sv("VOC2")/127*(H-34),k:"VOC1 · VOC2",drag:(x,y)=>{sset("VOC1",(x-10)/(W-20)*127);sset("VOC2",(H-10-y)/(H-34)*127)}}];
  if(m==="SID-6581")return[{x:sv("PW")/127*W/3,y:H/2,k:"PW",drag:x=>sset("PW",x/(W/3)*127)}];
  if(m==="DPRO-WAVE")return[{x:sv("WP")/127*W,y:20,k:"WP",drag:x=>sset("WP",x/W*127)}];
  if(m==="DPRO-DDRW")return[{x:sv("MIX")/127*W,y:20,k:"MIX",drag:x=>sset("MIX",x/W*127)}];
  return[]}};
function hashN(i){const x=Math.sin(i*127.1)*43758.5;return x-Math.floor(x)}
ED.amp={seg(W){const a=V("AMP");return{a:a[0]/127*W*.22,h:a[1]/127*W*.26,d:a[2]/127*W*.3,r:a[3]/127*W*.2}},
 draw(g,W,H){const s=this.seg(W),ink=cssv("--ink"),T=18,B=H-10,y=v=>B-v*(B-T);grid(g,W,H);
  const inf=V("AMP")[2]>=127,off=inf?W*.62:s.a+s.h*.6;g.strokeStyle=ink;g.lineWidth=2.2;g.beginPath();g.moveTo(0,y(0));g.lineTo(s.a,y(1));g.lineTo(s.a+s.h,y(1));if(inf){g.lineTo(off,y(1))}else{const k=Math.max(4,s.d);for(let x=0;x<=k;x+=2)g.lineTo(s.a+s.h+x,y(Math.exp(-3*x/k)));g.lineTo(s.a+s.h+k,y(0));g.lineTo(W,y(0))}g.stroke();
  g.setLineDash([4,3]);g.lineWidth=1.5;g.beginPath();g.moveTo(off,y(1));for(let x=0;x<=Math.max(3,s.r);x+=2)g.lineTo(off+x,y(Math.exp(-3*x/Math.max(3,s.r))));g.stroke();g.setLineDash([]);
  g.fillStyle=inkA(.5);g.fillRect(off,T,1,B-T);g.font="8px Silkscreen, monospace";g.fillStyle=ink;g.fillText("NOTE OFF",off+3,H-2);label(g,inf?"DEC 127: holds until note off":"AHDR · dashed = release")},
 handles(W,H){const s=this.seg(W),a=V("AMP"),T=18,B=H-10,y=v=>B-v*(B-T);return[{x:Math.max(5,s.a),y:y(1),k:"ATK",drag:x=>a[0]=clamp(Math.round(x/(W*.22)*127))},{x:s.a+s.h,y:y(1)+8,k:"HOLD",drag:x=>a[1]=clamp(Math.round((x-s.a)/(W*.26)*127))},
  {x:s.a+s.h+Math.max(4,s.d)*.35,y:y(Math.exp(-1.05)),k:"DEC",drag:x=>a[2]=clamp(Math.round((x-s.a-s.h)/.35/(W*.3)*127))},{x:s.a+s.h*.6+Math.max(3,s.r)*.35,y:y(Math.exp(-1.05)),k:"REL",drag:x=>a[3]=clamp(Math.round((x-s.a-s.h*.6)/.35/(W*.2)*127))}]}};
ED.flt={resp(u,env){const f=V("FLT"),b=Math.min(1,(f[0]+(env?f[6]:0))/127),w=(f[1]+(env?f[7]:0))/127,lp=Math.min(1,b+w);let d=0;if(u<b)d-=Math.pow((b-u)*7,2);if(u>lp)d-=Math.pow((u-lp)*7,2);d+=f[2]/127*1.6*Math.exp(-Math.pow((u-b)*26,2))+f[3]/127*1.6*Math.exp(-Math.pow((u-lp)*26,2));return d},
 draw(g,W,H){grid(g,W,H);const f=V("FLT");if(f[6]||f[7])line(g,W,x=>clamp(H/2+8-this.resp(x/W,1)*(H/4),6,H-6),inkA(.55),1.5,[5,4]);line(g,W,x=>clamp(H/2+8-this.resp(x/W)*(H/4),6,H-6),cssv("--ink"),2.2);label(g,"base + width"+(f[6]||f[7]?" · dashed = env peak":""))},
 handles(W,H){const f=V("FLT"),b=f[0]/127,lp=Math.min(1,b+f[1]/127),yy=u=>clamp(H/2+8-this.resp(u)*(H/4),6,H-6);
  return[{x:Math.max(6,b*W),y:yy(b),k:"BASE · HPQ",drag:(x,y)=>{f[0]=clamp(Math.round(x/W*127));f[2]=clamp(Math.round((H/2+8-y)/(H/2)*127))}},{x:Math.min(W-6,lp*W),y:yy(lp),k:"WDTH · LPQ",drag:(x,y)=>{f[1]=clamp(Math.round((x/W-f[0]/127)*127));f[3]=clamp(Math.round((H/2+8-y)/(H/2)*127))}}]}};
ED.efx={draw(g,W,H){const e=V("EFX"),ink=cssv("--ink"),mid=H*.42;grid(g,W,H);line(g,W,x=>mid-(e[1]-64)/64*(mid-20)*Math.exp(-Math.pow((x/W-e[0]/127)*8,2)),ink,2);
  const dt=Math.max(1,e[3])/256,fb=e[5]/64,snd=(e[4]-64)/64,base=H-8,hh=H*.36;let a=Math.abs(snd),x=dt,k=0;g.fillStyle=ink;
  while(x<=1&&k<40&&a>.02){const h=Math.min(1.2,a)*hh;const up=snd<0||k%2===0;g.fillRect(Math.round(x*W),up?base-h:base-h,4,h);if(snd>0){g.fillStyle=inkA(.4);g.fillRect(Math.round(x*W)+5,base-h*.3,2,h*.3);g.fillStyle=ink}a*=fb;x+=dt;k++}
  if(e[2]){g.fillStyle=inkA(.12);for(let x=0;x<W;x+=Math.max(3,e[2]/10))g.fillRect(x,6,1,mid)}label(g,"EQ · delay 1 bar"+(snd>0?" · ping-pong":snd<0?" · stereo kept":" · no send")+(fb>1?" · feedback grows!":""))},
 handles(W,H){const e=V("EFX"),mid=H*.42;return[{x:e[0]/127*W,y:mid-(e[1]-64)/64*(mid-20),k:"EQF · EQG",drag:(x,y)=>{e[0]=clamp(Math.round(x/W*127));e[1]=clamp(Math.round(64+(mid-y)/(mid-20)*64))}},
  {x:Math.max(1,e[3])/256*W+2,y:H-8-Math.min(1.2,Math.abs((e[4]-64)/64))*H*.36,k:"DTIM · DSND",drag:(x,y)=>{e[3]=clamp(Math.round(x/W*256));const m=clamp((H-8-y)/(H*.36),0,1);e[4]=clamp(Math.round(64+Math.sign(e[4]-64||-1)*m*63))}}]}};
function lfoCycle(v){const m=Math.pow(2,v[4]);return v[5]?2048/(v[5]*m):Infinity}
ED.lfo={draw(g,W,H,c){const l=c.dataset.l,v=V(l),tr=trk(S.sel),ink=cssv("--ink"),N=32,cyc=lfoCycle(v),dep=.15+.85*v[7]/127;grid(g,W,H);
  const trigs=[];for(let s=0;s<N;s++){const st=tr.steps[s%S.len];if(st&&!st.off&&st.l)trigs.push(s)}
  const lastTrig=p=>{let t=null;for(const s of trigs)if(s<=p)t=s;return t};const mode=v[2];
  line(g,W,x=>{const p=x/W*N;let ph;const lt=lastTrig(p);
   if(mode===0||lt==null)ph=p/cyc;else if(mode===1)ph=(p-lt)/cyc;else if(mode===2)ph=lt/cyc;else{const lim=mode===3?1:.5;ph=Math.min(lim-.0001,(p-lt)/cyc)}
   let o=lshape(v[3],((ph%1)+1)%1);if(v[6]){const il=Math.max(1,(128-v[6])/8);if(Math.floor(p/il)%2)o=0}return H/2+4-o*(H/2-16)*dep},ink,2);
  trigs.forEach(s=>{const x=s/N*W;g.fillStyle=ink;g.fillRect(x,H-7,2,6)});
  label(g,`${LTRIG[mode]} · ${cyc===Infinity?"stopped":"cycle "+(cyc>=1?Math.round(cyc*10)/10+" steps":Math.round(cyc*16)/16+" step")}`)}};
function inputLabel(t){const tr=trk(t);if(!isFx(tr.m))return"—";if(tr.inp==="NEIBOR")return t===0?"NEIBOR (none)":"T"+t+" "+S.tracks[t-1].m;return tr.inp}
function lfoCard(t,k){const l="LF"+k,v=trk(t).v[l],pg=LPAGES[v[0]],dn=destNames(t,v[0]);
 return`<section class="card"><header><div class="lfohead"><button class="lfohandle" data-cord="${l}" title="Drag onto any value to route LFO ${k} there" aria-label="Route LFO ${k}">~${k}</button><h3>LFO ${k}</h3></div><span>${v[7]?"":"depth 0 · off"}</span></header>
  <div class="lforoute"><select id="lp${k}" aria-label="LFO ${k} page">${LPAGES.map((p,i)=>opt(i,p==="MID"?"MIDI":p,v[0])).join("")}</select><span class="arrow">›</span><select id="ld${k}" aria-label="LFO ${k} destination">${dn.map((d,i)=>opt(i,d,v[1])).join("")}</select></div>
  <canvas class="ed" data-ed="lfo" data-l="${l}" aria-label="LFO ${k} over two bars, ticks = LFO trigs"></canvas>
  <div class="shapes w11">${LWAVE.map((n,i)=>`<button data-lwave="${l}" data-w="${i}" aria-pressed="${v[3]===i}" title="${n}" aria-label="${n}">${shapeIcon(i)}</button>`).join("")}</div>
  <div class="kv"><span class="seg" data-set="ltrig" data-l="${l}">${LTRIG.map((n,i)=>`<button data-v="${i}" aria-pressed="${v[2]===i}">${n}</button>`).join("")}</span></div>
  <div class="ctl four">${[4,5,6,7].map(i=>pc(l,i,{t,label:FIXED[l][i]})).join("")}</div></section>`}
function seqCards(t){const midi=isMidiT(t),tr=trk(t);return`<div class="seqcards"><section class="card arpc"><header><h3>Arpeggiator · ${tLabel(t)}</h3><span>FUNCTION + ARP · stored in the pattern</span></header>${arpPanel(t)}</section>
 <section class="card onec"><header><h3>Transpose</h3><span>FUNCTION + TRANSPOSE</span></header>${trnPanel(t)}</section>
 <section class="card onec"><header><h3>${midi?"MIDI page":"Trig setup"}</h3><span>${midi?"GLOBAL › MIDI SEQ":"KIT › TRIG"}</span></header>${midi?midiPanel(t):trigPanel(t)}</section></div>`}
/* one-line help captions become tooltips on their section, so Sound fits one screen */
function tipify(sel){$$(sel||"#main .soundnote,#main .seqcards .hint,#main .sound4 .hint,#main .arpc .statemach").forEach(n=>{const txt=n.textContent.trim().replace(/\s+/g," ");const host=n.closest(".card");if(host&&txt){const h=host.querySelector("header");const cur=h.getAttribute("title");h.setAttribute("title",(cur?cur+"\n":"")+txt);h.classList.add("tipped")}n.remove()})}
function renderSound(){const t=S.sel,tr=trk(t);
 if(isMidiT(t)){const int=S.tracks[t-6];$("#main").innerHTML=`<div class="sound4" style="grid-template-columns:minmax(0,2fr) minmax(0,1fr)">
  <section class="card"><header><h3>MIDI page · M${t-5}</h3><span>stored in the kit · CH ${String(tr.ch).padStart(2,"0")}</span></header>
   <div class="ctl four">${[0,1,2,3].map(i=>pc("MID",i,{t,label:FIXED.MID[i]})).join("")}${[4,5,6,7].map(i=>pc("MID",i,{t,label:"CC "+(tr.cc[i-4]===128?"AFT":tr.cc[i-4])})).join("")}</div>
   <div class="cap" style="margin-top:6px">CC numbers · GLOBAL › MIDI SEQ</div><div class="ctl four">${[0,1,2,3].map(i=>pc("cc",i,{t,label:"CL"+(i+1)})).join("")}</div>
   <div class="hint">PCHG only sends when it is locked on a step. Locks work on every value here, so a lock lane can draw an external synth's cutoff.</div></section>
  <section class="card"><header><h3>LFOs</h3><span>shared</span></header><div class="edblank">MIDI track ${t-5} shares its three LFOs with synth track ${t-5} (${int.m}). An LFO set to PAGE MIDI modulates this page. Edit them on T${t-5}.</div>
   <button class="cream" data-goto="${t-6}">Open T${t-5} LFOs</button></section></div>${seqCards(t)}`;tipify();syncControls();return}
 const m=tr.m,mk=MACH[m];
 $("#main").innerHTML=`<div class="sound4">
  <section class="card"><header><h3>Synthesis</h3><button class="machbtn" id="machbtn" aria-haspopup="dialog" aria-expanded="false" aria-label="Change machine"><span class="lcdtxt">${m}</span><span class="mfam">${famKey(m)}</span><svg viewBox="0 0 10 6" aria-hidden="true"><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5"/></svg></button></header>
   ${mk.p.some(Boolean)?`<canvas class="ed" data-ed="syn" aria-label="${machName(m)} screen"></canvas>`:`<div class="edblank">${mk.about}</div>`}<div class="soundnote">${mk.about}</div><div class="ctl four">${page8(t,"SYN")}</div></section>
  <section class="card"><header><h3>Amp</h3><span>AHDR envelope</span></header><canvas class="ed" data-ed="amp" aria-label="Amp envelope. Drag the dots."></canvas><div class="soundnote">HOLD replaces sustain: no NOTE OFF needed on the sequencer.</div><div class="ctl four">${page8(t,"AMP")}</div></section>
  <section class="card"><header><h3>Filter</h3><span>24 dB gap filter</span></header><canvas class="ed" data-ed="flt" aria-label="Filter response. Drag the dots."></canvas><div class="soundnote">BASE is the high-pass, WDTH the gap to the low-pass. Both track the note.</div><div class="ctl four">${page8(t,"FLT")}</div></section>
  <section class="card"><header><h3>Effects</h3><span>EQ · SRR · delay</span></header><canvas class="ed" data-ed="efx" aria-label="EQ and delay taps. Drag the dots."></canvas><div class="soundnote">DTIM is in 256th notes: 64 = one beat. DSND below 0 keeps stereo, above 0 ping-pongs.</div><div class="ctl four">${page8(t,"EFX")}</div></section></div>
 <div class="lfo3">${[1,2,3].map(k=>lfoCard(t,k)).join("")}</div>${seqCards(t)}`;
 tipify();syncControls();redraw()}

/* machine picker (MD Editor pattern) */
S.keepFx=true;
function machList(f){return Object.keys(MACH).filter(m=>famKey(m)===f)}
function openPicker(){const tr=trk(S.sel);S.pickFam=S.pickFam&&S.pickOpenFor===S.sel?S.pickFam:famKey(tr.m);S.pickOpenFor=S.sel;drawPicker();
 const pop=$("#machpop"),b=$("#machbtn").getBoundingClientRect();pop.hidden=false;pop.style.top=(b.bottom+scrollY+6)+"px";pop.style.left=Math.max(16,Math.min(b.left+scrollX,innerWidth-pop.offsetWidth-16))+"px";$("#machbtn").setAttribute("aria-expanded","true");pop.querySelector(".mk[aria-pressed=true],.mk:not(:disabled)")?.focus()}
function closePicker(){const pop=$("#machpop");if(pop.hidden)return;pop.hidden=true;$("#machbtn")?.setAttribute("aria-expanded","false")}
function prevText(m){const k=MACH[m];return`<b>${m}</b> ${k.about} · ${k.p.filter(Boolean).join(" ")||"no parameters"}`}
function drawPicker(){const tr=trk(S.sel),list=machList(S.pickFam);
 $("#machpop").innerHTML=`<div class="mp-fams">${FAMS.map(([f,n])=>`<button class="mf" data-fam="${f}" aria-pressed="${f===S.pickFam}"><i class="led"></i><b>${f}</b><span>${n.split(" · ")[1]}</span></button>`).join("")}</div>
  <div class="mp-right"><div class="mp-head"><span class="cap">${FAMS.find(x=>x[0]===S.pickFam)[1]}</span><span class="note">${list.length} machine${list.length>1?"s":""} · SysEx 0x5B</span></div>
  <div class="mp-grid">${list.map(m=>{const off=MACH[m].mk2&&S.plate==="mk1";return`<button class="mk" data-mach="${m}" aria-pressed="${m===tr.m}" ${off?"disabled title='MKII only'":""}><b>${m.replace(/^[A-Z]+\+?-?/,"")||m}</b><span>${off?"MKII only":machName(m)}</span></button>`}).join("")}</div>
  <div class="mp-foot"><div class="mp-prev" id="mpprev">${prevText(tr.m)}</div><button class="mp-keep" id="mpkeep" aria-pressed="${S.keepFx}"><i class="led ${S.keepFx?"on":""}"></i>Keep track effects + LFOs</button></div></div>`}
function setMachine(v){const t=S.sel,tr=trk(t);const old=tr.m;tr.m=v;tr.v.SYN=synDefaults(v);if(!S.keepFx){tr.v.AMP=[...DEFV.AMP];tr.v.FLT=[...DEFV.FLT];tr.v.EFX=[...DEFV.EFX];tr.v.LF1=[...DEFV.LFO];tr.v.LF2=[...DEFV.LFO];tr.v.LF3=[...DEFV.LFO]}
 for(const k of [...S.locks.keys()]){const[x,p]=k.split("|");if(+x===t&&p.startsWith("SYN."))S.locks.delete(k)}
 if(isFx(v)&&!isFx(old)){tr.inp=t===0?"INP AB":"NEIBOR";tr.v.AMP[2]=127;tr.v.AMP[3]=127;toast(`${v} needs audio in: input ${tr.inp}. AMP DEC and REL are at 127 so the sound passes.`)}
 if(!S.lane.startsWith("SYN.")||pname(t,S.lane))0;else S.lane="FLT.1";soundEdited();closePicker();render()}
document.addEventListener("click",e=>{
 if(e.target.closest("#machbtn")){$("#machpop").hidden?openPicker():closePicker();return}
 const pop=$("#machpop");if(pop.hidden)return;const f=e.target.closest(".mf");if(f){S.pickFam=f.dataset.fam;drawPicker();return}
 const m=e.target.closest(".mk");if(m&&!m.disabled){setMachine(m.dataset.mach);return}
 if(e.target.closest("#mpkeep")){S.keepFx=!S.keepFx;drawPicker();return}
 if(!e.target.closest("#machpop"))closePicker()},true);
document.addEventListener("mouseover",e=>{const m=e.target.closest("#machpop .mk");if(m)$("#mpprev").innerHTML=prevText(m.dataset.mach)});
document.addEventListener("keydown",e=>{if(e.key==="Escape"&&!$("#machpop").hidden){closePicker();$("#machbtn")?.focus()}});

/* visual LFO routing: drag the ~ handle onto a value */
let cord=null;
function cordStart(e,h){const r=h.getBoundingClientRect();cord={l:h.dataset.cord,x0:r.left+r.width/2,y0:r.top+r.height/2};document.body.classList.add("cording");
 $$("#main .pc[data-g]").forEach(p=>{const g=p.dataset.g;if(PAGES.includes(g)&&g!==cord.l)p.classList.add("droptarget")});h.setPointerCapture(e.pointerId);e.preventDefault()}
function cordMove(e){if(!cord)return;const ink=cssv("--led");$("#cordsvg").innerHTML=`<path d="M${cord.x0} ${cord.y0} C${cord.x0} ${cord.y0-60},${e.clientX} ${e.clientY-60},${e.clientX} ${e.clientY}" fill="none" stroke="${ink}" stroke-width="3" stroke-linecap="round"/><circle cx="${e.clientX}" cy="${e.clientY}" r="5" fill="${ink}"/>`;
 $$(".pc.dropok").forEach(p=>p.classList.remove("dropok"));const el=document.elementFromPoint(e.clientX,e.clientY)?.closest(".pc.droptarget");if(el)el.classList.add("dropok")}
function cordEnd(e){if(!cord)return;const el=document.elementFromPoint(e.clientX,e.clientY)?.closest(".pc.droptarget");$("#cordsvg").innerHTML="";$$(".droptarget,.dropok").forEach(p=>p.classList.remove("droptarget","dropok"));
 if(el){const v=V(cord.l);v[0]=LPAGES.indexOf(el.dataset.g);v[1]=+el.dataset.n;if(!v[7])v[7]=32;soundEdited();toast(`LFO ${cord.l[2]} → ${el.dataset.g} ${pname(S.sel,el.dataset.g+"."+el.dataset.n)} (PAGE and DEST set).`);cord=null;render();return}cord=null}
