/* ===== Sound by function: the groups' screens (85-sound-groups.js names them, renderSound lays them out) =====
   One editor per kind of picture. A canvas knows its page (data-pg) and its group's knobs (data-k), so one
   editor serves every machine that has them; a dot drags the knobs it stands for, a dot on a rail at the foot
   is a knob the picture shows. */
const V=(pg)=>trk(S.sel).v[pg];
const synIdx=n=>MACH[trk(S.sel).m].p.indexOf(n);
const sv=n=>{const i=synIdx(n);return i<0?0:V("SYN")[i]};
const sset=(n,v)=>{const i=synIdx(n);if(i>=0)V("SYN")[i]=clamp(Math.round(v),0,maxOf(meta(S.sel,"SYN",i)))};
const gIdx=(pg,n)=>pg==="SYN"?synIdx(n):FIXED[pg].indexOf(n);
const kget=(pg,n)=>{const i=gIdx(pg,n);return i<0?0:V(pg)[i]};
const kset=(pg,n,v)=>{const i=gIdx(pg,n);if(i>=0)V(pg)[i]=clamp(Math.round(v),0,maxOf(meta(S.sel,pg,i)))};
const ck=c=>(c.dataset.k||"").split(" ").filter(Boolean);
const u7=v=>clamp(v,0,127)/127;
/* a dot on the foot rail for a knob: x is its value */
const railH=(W,H,pg,n,y=H-7)=>({x:6+u7(kget(pg,n))*(W-12),y,k:n,drag:x=>kset(pg,n,(x-6)/(W-12)*127)});
function railDraw(g,W,H,y=H-7){g.strokeStyle=inkA(.35);g.lineWidth=1;g.beginPath();g.moveTo(6,y+.5);g.lineTo(W-6,y+.5);g.stroke()}
const SFONT="8px Silkscreen, monospace";
function wfDP(k,p){return Math.sin(p*6.283)*(1-(k%5)/6)+Math.sin(p*6.283*((k%7)+2))*((k%5)/6)*.8+(k%3===2?(p<.5?.3:-.3):0)}
function hashN(i){const x=Math.sin(i*127.1)*43758.5;return x-Math.floor(x)}
/* SWAVE: the base saw with the unison pair at ±UNIW (UNIL), the extended pair at twice the width (UNIX) */
ED.uni={geo(W,H){return{cx:W/2,sp:(W/2-12)/26,base:H-14,hh:H-36}},
 draw(g,W,H,c){const G=this.geo(W,H),ink=cssv("--ink");grid(g,W,H);
  const bar=(semi,h,dash)=>{const x=G.cx+semi*G.sp;g.fillStyle=ink;g.globalAlpha=dash?.5:1;g.fillRect(x-3,G.base-h*G.hh,6,Math.max(1,h*G.hh));g.globalAlpha=1};
  const uw=sv("UNIW")/127*12,ul=sv("UNIL")/127;bar(0,1);if(ul){bar(-uw,ul);bar(uw,ul)}
  if(ck(c).includes("UNIX")){const ux=Math.min(sv("UNIX"),sv("UNIL"))/127;if(ux){bar(-2*uw,ux,1);bar(2*uw,ux,1)}}
  label(g,`unison ±${Math.round(uw*10)/10} st`)},
 handles(W,H){const G=this.geo(W,H);return[{x:G.cx+sv("UNIW")/127*12*G.sp,y:G.base-sv("UNIL")/127*G.hh,k:"UNIW · UNIL",drag:(x,y)=>{sset("UNIW",(x-G.cx)/G.sp/12*127);sset("UNIL",(G.base-y)/G.hh*127)}}]}};
/* the subs: a sine one (SUB1) and two (SUB2) octaves down, a square one octave down (SUBX); the base faint */
ED.sub={geo(W,H){return{x0:16,sp:(W-28)/27,base:H-16,hh:H-38}},
 bars(c){const k=ck(c);return k.map(n=>({n,semi:n==="SUB2"?-24:-12,dx:n==="SUBX"?-8:n==="SUB1"&&k.includes("SUBX")?8:0}))},
 draw(g,W,H,c){const G=this.geo(W,H),ink=cssv("--ink"),X=s=>G.x0+(s+25)*G.sp;grid(g,W,H);g.fillStyle=inkA(.3);g.fillRect(X(0)-3,G.base-G.hh,6,G.hh);
  for(const b of this.bars(c)){const v=sv(b.n)/127,x=X(b.semi)+b.dx;g.fillStyle=ink;g.fillRect(x-3,G.base-v*G.hh,6,Math.max(1,v*G.hh));g.font=SFONT;g.fillText(b.n==="SUBX"?"SQ":b.n==="SUB2"?"-24":"-12",x-7,H-4)}
  g.font=SFONT;g.fillStyle=inkA(.6);g.fillText("BASE",X(0)-12,H-4);label(g,"subs below the base")},
 handles(W,H,c){const G=this.geo(W,H),X=s=>G.x0+(s+25)*G.sp;return this.bars(c).map(b=>({x:X(b.semi)+b.dx,y:G.base-sv(b.n)/127*G.hh,k:b.n,drag:(x,y)=>sset(b.n,(G.base-y)/G.hh*127)}))}};
/* a pulse: SID's PW is the width; SWAVE's PW is signed, 0 = square, either end a spike. PWAD sweeps it (dashed) */
ED.pulse={duty(){const m=trk(S.sel).m,p=sv("PW");return m==="SID-6581"?clamp(p/127,.03,.97):clamp(.5-Math.abs(p-64)/64*.46,.03,.97)},
 draw(g,W,H){const ink=cssv("--ink"),d=this.duty(),ad=sv("PWAD")/127,T=22,B=H-22,yy=v=>v?T:B;grid(g,W,H);railDraw(g,W,H);
  if(ad)line(g,W,x=>{const p=(x/W*2)%1;return yy(p<clamp(d+ad*.4*(Math.floor(x/W*2)?1:.5),0.03,.97))},inkA(.5),1.5,[4,3]);
  line(g,W,x=>yy((x/W*2)%1<d),ink,2);label(g,`width ${Math.round(d*100)}%${ad?" · sweep "+sv("PWAD"):""}${sv("PWRS")?" · restarts":""}`)},
 handles(W,H){return[railH(W,H,"SYN","PW")]}};
/* SID: the waveform (TRI SAW PULS MIX NOIS) with its modulation in words */
ED.sidwave={draw(g,W,H){const w=sv("WAVE"),pw=sv("PW")/127,ink=cssv("--ink");grid(g,W,H);
  line(g,W,x=>{const p=(x/W*3)%1;const v=[1-4*Math.abs(p-.5),2*p-1,p<pw?1:-1,((p*7)%1<.5?1:-1)*(p<.5?1:.4),RND[Math.floor(x/3)%8]][w]??0;return H/2+4-v*(H/2-22)},ink,2);
  label(g,EN["SID-6581.WAVE"][w]+(sv("MOD")?" · "+EN["SID-6581.MOD"][sv("MOD")]+(sv("MSRC")?" from PRCH":" from MFRQ"):""))}};
/* SWAVE-ENS / DPRO-DENS: the chord from one track, C plus PCH2-4 */
ED.chord={draw(g,W,H){const ink=cssv("--ink"),offs=["PCH2","PCH3","PCH4"].map(p=>sv(p)).filter(v=>v).map(v=>v-64);const kw=(W-16)/15,y0=24,wh=H-30;let wx=8;grid(g,W,H);
  for(let k=0;k<25;k++){if([1,3,6,8,10].includes(k%12))continue;g.strokeStyle=ink;g.lineWidth=1;g.strokeRect(wx+.5,y0+.5,kw-1,wh-1);if(k===0||offs.includes(k)){g.fillStyle=ink;g.beginPath();g.arc(wx+kw/2,y0+wh-10,4.5,0,7);g.fill()}wx+=kw}
  wx=8;for(let k=0;k<25;k++){if(![1,3,6,8,10].includes(k%12)){wx+=kw;continue}g.fillStyle=ink;g.fillRect(wx-kw*.3,y0,kw*.6,wh*.58);if(offs.includes(k)){g.fillStyle=cssv("--lcd");g.beginPath();g.arc(wx,y0+wh*.45,3.5,0,7);g.fill()}}
  label(g,"C + "+(offs.length?offs.join(" "):"no other notes"))}};
/* the ensemble's wave: saw through square to spikes (WAVE), PW mutates it; DPRO-DENS: one of the 64 waves */
ED.enswave={draw(g,W,H){const ink=cssv("--ink"),m=trk(S.sel).m;grid(g,W,H);
  if(m==="DPRO-DENS"){const a=sv("WAVE");line(g,W,x=>H/2+4-wfDP(a,(x/W*2)%1)*(H/2-24),ink,2);label(g,"wave "+(a+1));return}
  const w=sv("WAVE")/127,pw=.5+(sv("PW")-64)/140;railDraw(g,W,H);
  line(g,W,x=>{const p=(x/W*2)%1,saw=1-2*p,sq=p<pw?1:-1,spk=p<.08?1:-.25;const v=w<.5?saw+(sq-saw)*w*2:sq+(spk-sq)*(w-.5)*2;return H/2+2-v*(H/2-24)},ink,2);label(g,w<.35?"saw":w<.65?"square":"spikes")},
 handles(W,H){return trk(S.sel).m==="DPRO-DENS"?[]:[railH(W,H,"SYN","WAVE")]}};
/* DPRO-WAVE: WAVE morphing into the next at WP; DPRO-DDRW: two waves mixed (MIX) */
ED.morph={draw(g,W,H){const m=trk(S.sel).m,dd=m==="DPRO-DDRW",a=dd?sv("WAV1"):sv("WAVE"),b=dd?sv("WAV2"):(a+1)%32,mix=(dd?sv("MIX"):sv("WP"))/127,ink=cssv("--ink");grid(g,W,H);
  line(g,W,x=>{const p=(x/W*2)%1;return H/2+4-((1-mix)*wfDP(a,p)+mix*wfDP(b,p))*(H/2-24)},ink,2);label(g,dd?`wave ${a+1} ↔ ${b+1} · mix ${sv("MIX")}`:`wave ${a+1} → ${b+1}${sv("WPM")?" · sweeps":""}`)},
 handles(W,H){const dd=trk(S.sel).m==="DPRO-DDRW";return[{x:u7(sv(dd?"MIX":"WP"))*W,y:20,k:dd?"MIX":"WP",drag:x=>sset(dd?"MIX":"WP",x/W*127)}]}};
/* DPRO-BBOX: one hit, from STRT on (the key picks the drum) */
ED.drum={draw(g,W,H){const ink=cssv("--ink"),s0=u7(sv("STRT"))*.5,f=.6+u7(sv("PTCH"))*1.2;grid(g,W,H);railDraw(g,W,H);
  for(let x=0;x<W;x++){const p=x/W+s0,v=Math.exp(-p*6)*Math.sin(p*90*f+Math.sin(p*400)*2);g.fillStyle=ink;g.fillRect(x,H/2-v*(H/2-24),1,1+Math.abs(v)*2)}label(g,"the key picks the drum")},
 handles(W,H){return[railH(W,H,"SYN","STRT")]}};
/* retrigs: how many (RTRG, 127 = on and on), how far apart (RTIM) */
ED.rtrg={geo(W){const n=sv("RTRG"),dt=(6+u7(sv("RTIM"))*(W*.3));return{n:n>=127?99:n,dt}},
 draw(g,W,H){const G=this.geo(W),ink=cssv("--ink");grid(g,W,H);railDraw(g,W,H);for(let k=0;k<=G.n;k++){const x=10+k*G.dt;if(x>W-4)break;const h=(H-38)*Math.pow(.86,k);g.fillStyle=ink;g.fillRect(x,H-14-h,3,h)}
  label(g,G.n?(sv("RTRG")>=127?"retrigs on and on":G.n+" retrig"+(G.n>1?"s":"")):"one hit")},
 handles(W,H){const G=this.geo(W);return[{x:10+G.dt,y:H-14-(H-38)*.86,k:"RTIM",drag:x=>sset("RTIM",(x-16)/(W*.3)*127)},railH(W,H,"SYN","RTRG")]}};
/* FM+: one block: the carrier moved by its modulator; dot = the modulator's frequency (sideways) and level (up) */
ED.fm={ks(c){const k=ck(c);return{f:k.find(n=>/FRQ$/.test(n)),a:k.find(n=>/(ENV|VOL)$/.test(n)&&!/VEN$/.test(n)),fb:k.find(n=>/FB$/.test(n))}},
 draw(g,W,H,c){const K=this.ks(c),ink=cssv("--ink"),r=.5+u7(sv(K.f))*6,I=(K.a?u7(sv(K.a)):.5)*4,fb=K.fb?u7(sv(K.fb)):0;grid(g,W,H);
  line(g,W,x=>{const p=x/W*2;return H/2+4-Math.sin(p*6.283*r)*(K.a?u7(sv(K.a)):.5)*(H/2-26)*.5},inkA(.4),1.2,[2,3]);
  line(g,W,x=>{const p=x/W*2,mod=Math.sin(p*6.283*r+fb*Math.sin(p*6.283*r));return H/2+4-Math.sin(p*6.283+I*mod)*(H/2-24)},ink,2);label(g,`ratio ${r.toFixed(1)}${fb?" · feedback":""}`)},
 handles(W,H,c){const K=this.ks(c);return K.f?[{x:u7(sv(K.f))*W,y:K.a?H-8-u7(sv(K.a))*(H-28):H/2,k:K.a?K.f+" · "+K.a:K.f,drag:(x,y)=>{sset(K.f,x/W*127);if(K.a)sset(K.a,(H-8-y)/(H-28)*127)}}]:[]}};
/* VO-6: the vowel map (VOC1 across, VOC2 up) with the tutorial's points */
ED.vowel={draw(g,W,H){const P=[["O",35,5],["A",127,60],["I",40,110],["E",80,100],["U",10,30]];grid(g,W,H);g.font="10px Silkscreen, monospace";
  P.forEach(([l,a,b])=>{g.fillStyle=inkA(.5);g.fillText(l,10+a/127*(W-20)-3,H-10-b/127*(H-34)+4)});label(g,(sv("V-SW")?"":"no vowel · ")+CONS[sv("CONS")]+" + vowel")},
 handles(W,H){return[{x:10+sv("VOC1")/127*(W-20),y:H-10-sv("VOC2")/127*(H-34),k:"VOC1 · VOC2",drag:(x,y)=>{sset("VOC1",(x-10)/(W-20)*127);sset("VOC2",(H-10-y)/(H-34)*127)}}]}};
/* GND-NOIS: white (RED 0) through pink to red */
ED.noise={draw(g,W,H){const ink=cssv("--ink"),red=u7(sv("RED"));grid(g,W,H);let y=0;for(let x=0;x<W;x+=2){const n=hashN(x)*2-1;y=y*red*.95+n*(1-red*.8);g.fillStyle=ink;g.fillRect(x,H/2,2,-clamp(y,-1,1)*(H/2-22))}
  label(g,red<.25?"white":red<.7?"pink":"red")}};
/* FX-THRU: the input through the machine into the track's effects */
ED.fxin={draw(g,W,H){const ink=cssv("--ink"),m=trk(S.sel).m,bw=Math.min(90,W*.4);grid(g,W,H);g.strokeStyle=ink;g.lineWidth=2;g.strokeRect(W/2-bw/2,H/2-14,bw,28);g.font=SFONT;g.fillStyle=ink;g.fillText(m.slice(3),W/2-bw/2+6,H/2+3);
  g.beginPath();g.moveTo(6,H/2);g.lineTo(W/2-bw/2-2,H/2);g.moveTo(W/2+bw/2,H/2);g.lineTo(W-6,H/2);g.stroke();g.fillText(inputLabel(S.sel),6,H/2-8);label(g,"needs a trig to open")}};
/* FX-REVERB: the tail (DEC long, DAMP darker), cut at GATE (127: no gate) */
ED.verb={draw(g,W,H){const ink=cssv("--ink"),dec=.08+u7(sv("DEC"))*.9,damp=u7(sv("DAMP")),gate=sv("GATE"),gx=gate>=127?W:12+u7(gate)*(W-24);grid(g,W,H);railDraw(g,W,H);
  g.fillStyle=ink;g.fillRect(8,20,3,H-40);for(let x=14;x<gx;x+=2){const p=(x-14)/W,a=Math.exp(-p/dec*(1+damp));const h=(H-40)*a*(.5+.5*hashN(x)*(1-damp*.6));g.fillRect(x,H/2-h/2,1.5,h)}
  if(gate<127){g.fillStyle=inkA(.5);g.fillRect(gx,16,1,H-30)}label(g,gate>=127?"no gate":"gated")},
 handles(W,H){return[{x:14+Math.min(W-20,(.08+u7(sv("DEC"))*.9)*W*.7),y:H/2,k:"DEC",drag:x=>sset("DEC",((x-14)/W/.7-.08)/.9*127)},railH(W,H,"SYN","GATE")]}};
/* a band: everything below HP and above LP is cut (FX-REVERB's filter, the delay's DBAS + DWID) */
function bandDraw(g,W,H,lo,hi,txt){const ink=cssv("--ink"),T=22,B=H-12;grid(g,W,H);line(g,W,x=>{const u=x/W;let d=0;if(u<lo)d=Math.pow((lo-u)*6,2);if(u>hi)d=Math.pow((u-hi)*6,2);return clamp(T+d*(B-T),T,B)},ink,2.2);label(g,txt)}
ED.hplp={draw(g,W,H){bandDraw(g,W,H,u7(sv("HP")),u7(sv("LP")),"the reverb's band")},
 handles(W,H){return[{x:u7(sv("HP"))*W,y:24,k:"HP",drag:x=>sset("HP",x/W*127)},{x:u7(sv("LP"))*W,y:24,k:"LP",drag:x=>sset("LP",x/W*127)}]}};
/* FX-DYNAMIX: in to out (dashed = unchanged): THRS where it starts, RAT how much (127 a limiter), GAIN after */
ED.comp={pt(W,H){const t=u7(sv("THRS")),r=1/(1+u7(sv("RAT"))*12),gn=u7(sv("GAIN"))*.4;return{t,r,gn,X:u=>8+u*(W-16),Y:v=>H-10-clamp(v,0,1)*(H-34)}},
 draw(g,W,H){const P=this.pt(W,H),ink=cssv("--ink");grid(g,W,H);g.setLineDash([4,3]);g.strokeStyle=inkA(.5);g.beginPath();g.moveTo(P.X(0),P.Y(0));g.lineTo(P.X(1),P.Y(1));g.stroke();g.setLineDash([]);
  g.strokeStyle=ink;g.lineWidth=2;g.beginPath();for(let k=0;k<=40;k++){const u=k/40,o=(u<P.t?u:P.t+(u-P.t)*P.r)+P.gn;k?g.lineTo(P.X(u),P.Y(o)):g.moveTo(P.X(u),P.Y(o))}g.stroke();label(g,sv("RAT")>=127?"limiter":"1 : "+(1+Math.round(u7(sv("RAT"))*254)))},
 handles(W,H){const P=this.pt(W,H);return[{x:P.X(P.t),y:P.Y(P.t+P.gn),k:"THRS · GAIN",drag:(x,y)=>{sset("THRS",(x-8)/(W-16)*127);sset("GAIN",((H-10-y)/(H-34)-P.t)/.4*127)}}]}};
/* chorus, phaser, flanger: the sweep over time, around DEL / CNTR by DEP at SPD; FB in words */
ED.sweep={ks(){return trk(S.sel).m==="FX-PHASER"?"CNTR":"DEL"},
 geo(W,H){const c=u7(sv(this.ks())),d=u7(sv("DEP"))*.5,s=.5+u7(sv("SPD"))*6,Y=v=>H-10-clamp(v,0,1)*(H-34);return{c,d,s,Y}},
 draw(g,W,H){const G=this.geo(W,H),ink=cssv("--ink");grid(g,W,H);railDraw(g,W,H);line(g,W,x=>G.Y(G.c+G.d*Math.sin(x/W*6.283*G.s)),ink,2);label(g,(sv("FB")?"feedback "+sv("FB"):"no feedback"))},
 handles(W,H){const G=this.geo(W,H),k=this.ks(),xp=W/(4*G.s);return[{x:4,y:G.Y(G.c),k,drag:(x,y)=>sset(k,(H-10-y)/(H-34)*127)},{x:xp,y:G.Y(G.c+G.d),k:"DEP",drag:(x,y)=>sset("DEP",((H-10-y)/(H-34)-G.c)/.5*127)},railH(W,H,"SYN","SPD")]}};
/* FX-RINGMOD: the carrier, sine to triangle (WAVE); EXT in words */
ED.carrier={draw(g,W,H){const ink=cssv("--ink"),w=u7(sv("WAVE"));grid(g,W,H);railDraw(g,W,H);line(g,W,x=>{const p=(x/W*3)%1,s=Math.sin(p*6.283),t=1-4*Math.abs(((p+.25)%1)-.5);return H/2-((1-w)*s+w*t)*(H/2-24)},ink,2);label(g,w<.5?"sine":"triangle")},
 handles(W,H){return[railH(W,H,"SYN","WAVE")]}};
/* AMP DIST: in to out (dashed = clean) */
ED.dist={k(){return(kget("AMP","DIST")-64)/63},
 draw(g,W,H){const k=this.k(),a=1+Math.abs(k)*9,ink=cssv("--ink"),X=u=>8+(u+1)/2*(W-16),Y=v=>H/2+6-v*(H/2-22);grid(g,W,H);
  g.setLineDash([4,3]);g.strokeStyle=inkA(.5);g.beginPath();g.moveTo(X(-1),Y(-1));g.lineTo(X(1),Y(1));g.stroke();g.setLineDash([]);
  g.strokeStyle=ink;g.lineWidth=2;g.beginPath();for(let i=0;i<=40;i++){const u=i/20-1,v=k>=0?Math.tanh(u*a)/Math.tanh(a):Math.sin(u*Math.PI/2*(1+Math.abs(k)*3))/(1+Math.abs(k));i?g.lineTo(X(u),Y(v)):g.moveTo(X(u),Y(v))}g.stroke();label(g,k?(k>0?"drive":"fold"):"clean")},
 handles(W,H){return[{x:8+.8*(W-16),y:H/2+6-.6*(H/2-22)-this.k()*(H/2-22)*.35,k:"DIST",drag:(x,y)=>kset("AMP","DIST",64+((H/2+6-.6*(H/2-22))-y)/((H/2-22)*.35)*63)}]}};
/* AMP VOL PAN: the track in the stereo field */
ED.pan={draw(g,W,H){const ink=cssv("--ink");grid(g,W,H);g.font=SFONT;g.fillStyle=ink;g.fillText("L",6,H-6);g.fillText("R",W-12,H-6);g.fillStyle=inkA(.5);g.fillRect(W/2,20,1,H-30);
  const p=kget("AMP","PAN")-64;label(g,p?(p<0?"left "+(-p):"right "+p):"centre")},
 handles(W,H){return[{x:10+u7(kget("AMP","PAN"))*(W-20),y:H-10-u7(kget("AMP","VOL"))*(H-34),k:"PAN · VOL",drag:(x,y)=>{kset("AMP","PAN",(x-10)/(W-20)*127);kset("AMP","VOL",(H-10-y)/(H-34)*127)}}]}};
/* FILTER ATK DEC: the filter's own envelope; BOFS lifts the base (solid), WOFS the width (dashed) */
ED.fenv={seg(W){const f=V("FLT");return{a:f[4]/127*W*.3,d:f[5]/127*W*.6}},
 draw(g,W,H){const f=V("FLT"),s=this.seg(W),T=22,B=H-10,ink=cssv("--ink");grid(g,W,H);
  const env=x=>x<s.a?x/Math.max(1,s.a):Math.exp(-3*(x-s.a)/Math.max(4,s.d));
  line(g,W,x=>B-env(x)*(B-T)*Math.max(.04,f[6]/127),ink,2.2);if(f[7])line(g,W,x=>B-env(x)*(B-T)*f[7]/127,inkA(.6),1.5,[5,4]);label(g,f[6]||f[7]?"solid = base · dashed = width":"no offset: the envelope does nothing")},
 handles(W,H){const s=this.seg(W),f=V("FLT"),T=22,B=H-10;return[{x:Math.max(5,s.a),y:B-(B-T)*Math.max(.04,f[6]/127),k:"ATK · BOFS",drag:(x,y)=>{f[4]=clamp(Math.round(x/(W*.3)*127));f[6]=clamp(Math.round((B-y)/(B-T)*127))}},
  {x:s.a+Math.max(4,s.d)*.35,y:B-(B-T)*Math.max(.04,f[6]/127)*Math.exp(-1.05),k:"DEC",drag:x=>f[5]=clamp(Math.round((x-s.a)/.35/(W*.6)*127))}]}};
/* EFFECTS EQF EQG: one band, boosted (up) or cut (down) */
ED.eq={draw(g,W,H){const e=V("EFX"),mid=H/2+4;grid(g,W,H);line(g,W,x=>mid-(e[1]-64)/64*(mid-22)*Math.exp(-Math.pow((x/W-e[0]/127)*8,2)),cssv("--ink"),2);label(g,e[1]===64?"flat":e[1]>64?"boost":"cut")},
 handles(W,H){const e=V("EFX"),mid=H/2+4;return[{x:e[0]/127*W,y:mid-(e[1]-64)/64*(mid-22),k:"EQF · EQG",drag:(x,y)=>{e[0]=clamp(Math.round(x/W*127));e[1]=clamp(Math.round(64+(mid-y)/(mid-22)*64))}}]}};
/* EFFECTS SRR: the held steps of a lower sample rate */
ED.srr={draw(g,W,H){const r=V("EFX")[2],st=1+r/6,ink=cssv("--ink");grid(g,W,H);railDraw(g,W,H);line(g,W,x=>H/2+2-Math.sin(x/W*6.283*1.5)*(H/2-24),inkA(.35),1,[3,3]);line(g,W,x=>H/2+2-Math.sin(Math.floor(x/st)*st/W*6.283*1.5)*(H/2-24),ink,2);label(g,r?"held "+Math.round(st)+" px":"full rate")},
 handles(W,H){return[railH(W,H,"EFX","SRR")]}};
/* EFFECTS DTIM DSND DFB: the repeats over one bar (DTIM in 256ths), DSND below 0 keeps stereo, above ping-pongs */
ED.delay={draw(g,W,H){const e=V("EFX"),ink=cssv("--ink"),dt=Math.max(1,e[3])/256,fb=e[5]/64,snd=(e[4]-64)/64,base=H-10,hh=H-36;grid(g,W,H);let a=Math.abs(snd),x=dt,k=0;g.fillStyle=ink;
  while(x<=1&&k<40&&a>.02){const h=Math.min(1.2,a)*hh;g.fillStyle=snd>0&&k%2?inkA(.55):ink;g.fillRect(Math.round(x*W),base-h,4,h);a*=fb;x+=dt;k++}
  label(g,(snd>0?"ping-pong":snd<0?"stereo kept":"no send")+(fb>1?" · grows!":""))},
 handles(W,H){const e=V("EFX");return[{x:Math.max(1,e[3])/256*W+2,y:H-10-Math.min(1.2,Math.abs((e[4]-64)/64))*(H-36),k:"DTIM · DSND",drag:(x,y)=>{e[3]=clamp(Math.round(x/W*256));const m=clamp((H-10-y)/(H-36),0,1);e[4]=clamp(Math.round(64+Math.sign(e[4]-64||-1)*m*63))}}]}};
/* EFFECTS DBAS DWID: the delay's own band */
ED.dflt={draw(g,W,H){const e=V("EFX");bandDraw(g,W,H,e[6]/127,Math.min(1,(e[6]+e[7])/127),"the repeats' band")},
 handles(W,H){const e=V("EFX");return[{x:e[6]/127*W,y:24,k:"DBAS",drag:x=>e[6]=clamp(Math.round(x/W*127))},{x:Math.min(1,(e[6]+e[7])/127)*W,y:24,k:"DWID",drag:x=>e[7]=clamp(Math.round(x/W*127-e[6]))}]}};
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
/* each screen's help: its tooltip */
const SND_TIP={uni:"The base saw (middle) and the unison pair either side: UNIW how far (sideways), UNIL how loud (up); dashed, the extended pair at twice the width (UNIX, never above UNIL). Drag the dot.",
 sub:"The subs below the base (faint): SUB1 a sine one octave down, SUB2 two, SUBX a square one octave down. Drag a bar's top.",
 pulse:"Two cycles of the pulse: PW its width (SWAVE: 0 = square, either end a spike), dashed the PWAD sweep. The dot on the rail is PW.",
 sidwave:"The SID's waveform at PW; the modulation (RING, SYNC) and where its second frequency comes from in the title.",
 chord:"The notes one trig plays: C, and PCH2-4 semitones above it. Lock PCH2-4 per step to change chord.",
 enswave:"The ensemble's wave: WAVE from saw through square to spikes (the rail), PW mutates it.",
 morph:"Two cycles of the wave: WP morphs it into the next one (DPRO-DDRW: MIX blends WAV1 and WAV2). Drag the dot sideways.",
 drum:"One hit from STRT on (the rail); the key picks which of the 24 drums plays.",
 rtrg:"The retrigs: RTIM the time between (drag the second hit), RTRG how many (the rail; 127 = on and on).",
 fm:"The carrier after its modulator (dotted): the dot is the modulator's frequency (sideways) and level (up). Feedback roughens it.",
 vowel:"The vowel map: VOC1 across, VOC2 up, with the vowels of the manual's tutorial. Drag the dot.",
 noise:"The noise: RED from white (0) through pink (64) to red (127).",
 fxin:"The input (INP AB, CD, EF or NEIBOR, the track before) through this machine into the track's own effects. An FX machine needs a trig to open.",
 verb:"The gated reverb: DEC how long (the dot), DAMP darker and shorter, GATE cuts the tail (the rail; 127 = no gate).",
 hplp:"The reverb's band: HP cuts the lows, LP the highs. Drag the edges.",
 comp:"The compressor, in to out (dashed = unchanged): THRS where it starts (sideways), GAIN after it (up), RAT how much (127 = a limiter).",
 sweep:"The sweep over time: around DEL (CNTR on the phaser) by DEP, SPD on the rail. Drag the dots.",
 carrier:"The ring modulator's carrier, sine to triangle (WAVE, the rail).",
 amp:"The level after a trig: ATK rises, HOLD keeps it (no NOTE OFF needed), DEC falls, REL after the note off (dashed). Drag the dots.",
 dist:"Distortion, in to out (dashed = clean). Drag the dot up or down.",
 pan:"The track in the stereo field: PAN sideways, VOL up. Drag the dot.",
 flt:"The 24 dB gap filter: BASE is the high-pass, WDTH the gap to the low-pass, the dots up for HPQ and LPQ. Dashed: where the envelope takes it. Both track the note.",
 fenv:"The filter's own envelope: ATK, DEC; BOFS how far it lifts the base (solid, up), WOFS the width (dashed). Drag the dots.",
 eq:"One EQ band: EQF moves it, EQG boosts (up) or cuts (down). Drag the dot.",
 srr:"Sample-rate reduction: more SRR, longer held steps (the rail).",
 delay:"The repeats over one bar: DTIM apart (in 256ths: 64 = a beat), DSND how loud (below 0 keeps stereo, above ping-pongs), DFB how much each keeps.",
 dflt:"The repeats' band: DBAS the low edge, DWID up to the high edge. Drag the edges.",
 lfo:"The LFO over two bars, as the trig mode makes it (the ticks are this track's LFO trigs). Drag the ~ key onto any value to route it there."};
const attrS=s=>String(s).replace(/&/g,"&amp;").replace(/"/g,"&quot;").replace(/</g,"&lt;");
const sndPlot=(ed,pg,knobs,extra="")=>`<div class="plot" title="${attrS(SND_TIP[ed]||"Drag the dots.")}"><canvas class="ed" data-ed="${ed}" data-pg="${pg}" data-k="${knobs.join(" ")}" ${extra} aria-label="${attrS(SND_TIP[ed]||"")}"></canvas></div>`;
/* a group's title: a MUTATE scope chip (its knobs join what R moves), or plain */
function sgTitle(x){if(x.titleHtml)return x.titleHtml;if(x.mut===false)return x.title;const id=x.pg+":"+x.key;
 return`<button class="mutg" data-mutsg="${id}" aria-pressed="${S.mut.scope.has(id)}" title="Add ${attrS(x.title)} to what Mutate moves">${x.title}</button>`}
const MM_PG_TIP={SYN:"the machine's SYNTHESIS page",AMP:"the AMPLIFICATION page",FLT:"the FILTER page",EFX:"the EFFECTS page",LF1:"LFO 1's page",LF2:"LFO 2's page",LF3:"LFO 3's page",MID:"the MIDI track's page"};
/* a group: its title on the rule (the page's word on the first group of that page in its row), its screen
   or what its knobs do, its boxes; each part is a cell of the row's grid (subgrid) */
function sgHtml(x,cols,tag,note){const t=S.sel;
 const body=x.body||`<div class="ctl" style="grid-template-columns:repeat(${cols},minmax(0,1fr))">${x.idx.map((i,k)=>pc(x.pg,i,{t,label:x.labels?.[k]||x.knobs[k]})).join("")}</div>`;
 return`<section class="sg" data-sg="${x.pg}:${x.key}"><header><h3>${sgTitle(x)}</h3>${tag?`<span title="Its knobs are on ${MM_PG_TIP[x.pg]||x.pg}">${tag}</span>`:""}</header>${x.plot||(note?`<p class="sgnote" title="${attrS(note)}">${note}</p>`:"")}${body}</section>`}
/* One row: columns over three lines (titles, screens, boxes) on the page's grid, at the page's grid lines
   (line0), so every row's screens are as tall. A group with a screen is a column of its own, as wide as its
   boxes (never narrower than about two); two without a screen share a column (the upper one's boxes at the
   screens' top, the lower one's title at their foot); a lone one says what its knobs do there. A row
   without screens is two lines. */
function sgRow(list,cls,line0){if(!list.length)return"";
 let prev="";const first=x=>{const w=x.tag??MM_PG_WORD[x.pg]??"";if(prev===x.pg)return"";prev=x.pg;return w},n=x=>x.n??x.idx.length;
 const total=list.reduce((a,x)=>a+n(x),0),narrow=total>12?1.8:2.4,w=x=>x.w??(x.plot?Math.max(n(x),narrow):n(x));
 const tracks=cols=>`grid-template-columns:${cols.map(c=>`minmax(0,${c}fr)`).join(" ")}`;
 if(!list.some(x=>x.plot))return`<div class="sgrow flat ${cls}" style="grid-row:${line0}/span 2;${tracks(list.map(x=>Math.max(1.4,w(x))))}">${list.map(x=>`<div class="sgcol">${sgHtml(x,n(x),first(x))}</div>`).join("")}</div>`;
 const cols=[],stacks=[];
 for(const x of list){if(x.plot){cols.push([x]);continue}const s=stacks[stacks.length-1];if(s&&s.length<2)s.push(x);else{const c=[x];stacks.push(c);cols.push(c)}}
 return`<div class="sgrow ${cls}" style="grid-row:${line0}/span 3;${tracks(cols.map(c=>Math.max(...c.map(w))))}">${cols.map(c=>{const cn=Math.max(...c.map(n)),kind=c[0].plot?"":c.length>1?" stack":" lone";
  return`<div class="sgcol${kind}">${c.map(x=>sgHtml(x,cn,first(x),kind===" lone"?x.note:"")).join("")}</div>`}).join("")}</div>`}
/* the rows of a page: [[groups], cls]... -> the page's grid template and the rows at their lines */
function sndRows(rows){let line=2,tpl=["auto"];const html=rows.map(([list,cls])=>{if(!list.length)return"";const flat=!list.some(x=>x.plot);
  if(line>2){tpl.push("var(--sndgap)");line++}const h=sgRow(list,cls,line);tpl.push(...(flat?["auto","auto"]:["auto","minmax(0,var(--plotmax))","auto"]));line+=flat?2:3;return h}).join("");
 return{tpl:tpl.join(" "),html}}
/* an LFO group: the ~ key and its title, its screen, then PAGE › DEST and TRIG, the eleven shapes, the four boxes */
function lfoGroup(t,k){const l="LF"+k,v=trk(t).v[l],dn=LPAGES[v[0]]?destNames(t,v[0]):["—"];
 return{key:"lfo",title:"LFO "+k,pg:l,idx:[4,5,6,7],knobs:[4,5,6,7].map(i=>FIXED[l][i]),n:4,w:4,tag:v[7]?"":"off",
  titleHtml:`<span class="lfohead"><button class="lfohandle" data-cord="${l}" title="Drag onto any value to route LFO ${k} there" aria-label="Route LFO ${k}">~${k}</button><button class="mutg" data-mutsg="${l}:lfo" aria-pressed="${S.mut.scope.has(l+":lfo")}" title="Add LFO ${k} to what Mutate moves (its PAGE and DEST stay)">LFO ${k}</button></span>`,
  plot:`<div class="plot" title="${attrS(SND_TIP.lfo)}"><canvas class="ed" data-ed="lfo" data-l="${l}" aria-label="LFO ${k} over two bars, ticks = LFO trigs"></canvas></div>`,
  body:`<div class="lfobody"><div class="sgline lforoute"><select id="lp${k}" aria-label="LFO ${k} page">${LPAGES.map((p,i)=>opt(i,p==="MID"?"MIDI":p,v[0])).join("")}</select><span class="arrow">›</span><select id="ld${k}" aria-label="LFO ${k} destination">${dn.map((d,i)=>opt(i,d,v[1])).join("")}</select>
   <span class="seg" data-set="ltrig" data-l="${l}" title="TRIG: FREE runs on, TRIG restarts it on a trig, HOLD keeps the value a trig takes, ONE and HALF play one cycle or half of one">${LTRIG.map((n,i)=>`<button data-v="${i}" aria-pressed="${v[2]===i}">${n}</button>`).join("")}</span></div>
   <div class="shapes w11">${LWAVE.map((n,i)=>`<button data-lwave="${l}" data-w="${i}" aria-pressed="${v[3]===i}" title="${n}" aria-label="${n}">${shapeIcon(i)}</button>`).join("")}</div>
   <div class="ctl" style="grid-template-columns:repeat(4,minmax(0,1fr))">${[4,5,6,7].map(i=>pc(l,i,{t,label:FIXED[l][i]})).join("")}</div></div>`}}
function machButton(m){return`<button class="machbtn" id="machbtn" aria-haspopup="dialog" aria-expanded="false" aria-label="Change machine"><span class="lcdtxt">${m}</span><span class="mfam">${famKey(m)}</span><svg viewBox="0 0 10 6" aria-hidden="true"><path d="M1 1l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5"/></svg></button>`}
/* the track's pattern settings live in the Sequence dock: one key each to go there */
function sndDockKeys(t){const midi=isMidiT(t);return`<span class="snddock" title="Stored in the pattern (arpeggiator, transpose) or the kit (${midi?"the MIDI page":"trig setup"}): edit them in the Sequence workspace's dock">${[["arp","Arp"],["trn","Transpose"],[midi?"midipage":"trig",midi?"MIDI set":"Trig setup"]].map(([d,n])=>`<button class="ptog" data-snddock="${d}">${n}</button>`).join("")}</span>`}
function renderSound(){const t=S.sel,tr=trk(t);let rows,head;
 if(isMidiT(t)){const int=S.tracks[t-6],G=(title,idx,labels,plain)=>({key:sgKey(title),title,pg:"MID",idx,knobs:idx.map(i=>FIXED.MID[i]),labels,mut:false,note:plain});
  const cc=[4,5,6,7].map(i=>"CC "+(tr.cc[i-4]===128?"AFT":tr.cc[i-4]));
  rows=[[[G("Note",[0,1]),G("Bend · program",[2,3]),G("Controllers",[4,5,6,7],cc),{key:"ccnum",title:"CC numbers",pg:"cc",idx:[0,1,2,3],knobs:["CL1","CL2","CL3","CL4"],mut:false,tag:"GLOBAL"},
   {key:"lfos",title:"LFOs",pg:"MID",idx:[],knobs:[],n:0,w:4,mut:false,tag:"",body:`<p class="sgabout">MIDI track ${t-5} shares its three LFOs with synth track ${t-5} (${int.m}): an LFO set to PAGE MIDI moves this page. <button class="cream" data-goto="${t-6}">Open T${t-5} LFOs</button></p>`}],"midirow"]];
  head=`<span class="lcdchip" title="The MIDI track's page is stored in the kit; PCHG only sends when it is locked on a step">M${t-5} · CH ${String(tr.ch).padStart(2,"0")}</span>`}
 else{const g=mmSoundGroups(tr.m);
  [...g.SYN,...g.AMP,...g.FLT,...g.EFX].forEach(x=>{x.plot=x.ed?sndPlot(x.ed,x.pg,x.knobs):""});
  const syn=g.SYN.length?g.SYN:[{key:"syn",title:"Synthesis",pg:"SYN",idx:[],knobs:[],n:0,w:3,mut:false,body:`<p class="sgabout">${MACH[tr.m].about}</p>`}];
  rows=[[[...syn,...g.AMP],"srcrow"],[[...g.FLT,...g.EFX],"tonerow"],[[1,2,3].map(k=>lfoGroup(t,k)),"lforow"]];
  head=machButton(tr.m)}
 const R=sndRows(rows);
 $("#main").innerHTML=`<div class="snd" style="grid-template-rows:${R.tpl}"><div class="sndhead">${head}${mutBarHtml()}${sndDockKeys(t)}</div>${R.html}</div>`;
 syncControls();redraw()}

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
/* MACHINE: the machine intent (its start values are the core's; keep: the effects and LFOs stay); the SYN locks of the
   track go with the old machine (clearLane each) */
function setMachine(v){const t=S.sel,tr=trk(t);const old=tr.m,synLocks=trackLockPids(t).filter(p=>p.startsWith("SYN."));tr.m=v;tr.name=machName(v);tr.v.SYN=synDefaults(v);if(!S.keepFx){tr.v.AMP=[...DEFV.AMP];tr.v.FLT=[...DEFV.FLT];tr.v.EFX=[...DEFV.EFX];tr.v.LF1=[...DEFV.LFO];tr.v.LF2=[...DEFV.LFO];tr.v.LF3=[...DEFV.LFO]}
 for(const k of [...S.locks.keys()]){const[x,p]=k.split("|");if(+x===t&&p.startsWith("SYN."))S.locks.delete(k)}
 if(isFx(v)&&!isFx(old)){tr.inp=t===0?"INP AB":"NEIBOR";tr.v.AMP[2]=127;tr.v.AMP[3]=127;toast(`${v} needs audio in: input ${tr.inp}. AMP DEC and REL are at 127 so the sound passes.`)}
 if(!S.lane.startsWith("SYN.")||pname(t,S.lane))0;else S.lane="FLT.1";
 edit("machine",{t,model:MACH[v].id,keepFx:!!S.keepFx});synLocks.forEach(pid=>edit("clearLane",{t,...pidArgs(pid)}));closePicker();render()}
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
 if(el){editTrack(S.sel,()=>{const v=V(cord.l);v[0]=LPAGES.indexOf(el.dataset.g);v[1]=+el.dataset.n;if(!v[7])v[7]=32});toast(`LFO ${cord.l[2]} → ${el.dataset.g} ${pname(S.sel,el.dataset.g+"."+el.dataset.n)} (PAGE and DEST set).`);cord=null;render();return}cord=null}
