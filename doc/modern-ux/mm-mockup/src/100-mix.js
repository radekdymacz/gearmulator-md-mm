
/* ===== Mix: routing drawn as a signal graph ===== */
const BUSES=["AB","CD","EF"],ROUTES=["3xSTEREO","3xSTEREO+AB=MIX","6xMONO"],INPUTS=["NEIBOR","INP A","INP B","INP AB","BUS AB","BUS CD","BUS EF"];
const shortM=m=>m.replace("SWAVE-","SW-").replace("DPRO-","DP-");
function busFlow(b){const T=S.tracks;let items=[];const notes=[];
 T.forEach((tr,t)=>{const lab=(tr.inp==="NEIBOR"&&isFx(tr.m)&&t>0?`T${t}›`:"")+`T${t+1} ${shortM(tr.m)}`;
  if(isFx(tr.m)&&tr.inp==="BUS "+b){if(tr.out[b]){items=[`(${items.join(" + ")||"silence"}) › ${lab} INSERT`]}else notes.push(`${lab} takes a copy`);return}
  if(tr.out[b]&&!tr.mute)items.push(lab)});
 return{items,notes}}
function outOf(b){if(S.routing==="6xMONO")return"not sent out";if(b==="AB")return"OUT A/B";return S.routing==="3xSTEREO+AB=MIX"?`OUT ${b[0]}/${b[1]} + A/B`:`OUT ${b[0]}/${b[1]}`}
function routeSvg(){const T=S.tracks,W=1200,H=256,NW=150,nx=i=>25+200*i,NY=30,NH=56,by={AB:124,CD:152,EF:180},mono=S.routing==="6xMONO";let h="";
 const IX=10,IY=206;h+=`<g><rect x="${IX}" y="${IY}" width="96" height="44" rx="3" class="jack"/><text x="${IX+8}" y="${IY+18}" font-size="10">INPUT</text><text x="${IX+8}" y="${IY+36}" font-size="13">A  B</text></g>`;
 BUSES.forEach(b=>{h+=`<line x1="12" y1="${by[b]}" x2="1146" y2="${by[b]}" class="bus ${mono?"dim":""}"/><text x="4" y="${by[b]-5}" font-size="9">${b}</text>`});
 h+=`<text x="1146" y="${IY+18}" font-size="9" text-anchor="end">${mono?"6 X MONO":S.routing==="3xSTEREO"?"3 X STEREO":"3 X STEREO · AB = MIX"}</text>`;
 if(!mono)BUSES.forEach(b=>{h+=`<circle cx="1158" cy="${by[b]}" r="7" class="jack ${busFlow(b).items.length?"live":""}"/><text x="1170" y="${by[b]+4}" font-size="10">${b}</text>`;
  if(S.routing==="3xSTEREO+AB=MIX"&&b!=="AB")h+=`<path d="M1146 ${by[b]} C1126 ${by[b]-14},1126 ${by.AB+10},1146 ${by.AB}" class="cord dash"/>`});
 T.forEach((tr,i)=>{const x=nx(i),fx=isFx(tr.m),sel=i===S.sel;
  if(mono)h+=`<path d="M${x+NW/2} ${NY+NH} L${x+NW/2} 196" class="cord ${tr.mute?"dash":""}" opacity=".5"/><text x="${x+NW/2+4}" y="194" font-size="9">OUT ${"ABCDEF"[i]}</text>`;
  BUSES.forEach((b,k)=>{if(!tr.out[b])return;const dx=x+26+k*20;h+=`<line x1="${dx}" y1="${NY+NH}" x2="${dx}" y2="${by[b]}" class="cord ${mono?"dash":""}"/><circle cx="${dx}" cy="${by[b]}" r="4.5" class="tap"/>`});
  if(fx&&tr.inp.startsWith("BUS")){const b=tr.inp.slice(4),dx=x+NW-22;h+=`<path d="M${dx} ${by[b]} L${dx} ${NY+NH+8}" class="cord dash"/><path d="M${dx-5} ${NY+NH+10} L${dx} ${NY+NH+2} L${dx+5} ${NY+NH+10}" class="cord"/><circle cx="${dx}" cy="${by[b]}" r="4" class="jack"/><text x="${dx+8}" y="${NY+NH+14}" font-size="9">IN ${b}</text>`}
  if(fx&&tr.inp==="NEIBOR"&&i>0)h+=`<path d="M${nx(i-1)+NW} ${NY+NH/2} L${x-4} ${NY+NH/2}" class="cord"/><path d="M${x-10} ${NY+NH/2-5} L${x-3} ${NY+NH/2} L${x-10} ${NY+NH/2+5}" class="cord"/><text x="${x-25}" y="${NY+NH/2-8}" font-size="8" text-anchor="middle">NEIBOR</text>`;
  if(fx&&tr.inp.startsWith("INP"))h+=`<path d="M${IX+96} ${IY+22} C${x} ${IY+22},${x+NW/2} ${IY-20},${x+NW/2} ${NY+NH}" class="cord dash"/><text x="${x+NW/2+6}" y="${NY+NH+30}" font-size="9">${tr.inp}</text>`;
  if(tr.m==="SID-6581"&&i>0&&sv2(tr,"MOD")&&sv2(tr,"MSRC")===1)h+=`<path d="M${nx(i-1)+NW-10} ${NY} C${nx(i-1)+NW} ${NY-20},${x+10} ${NY-20},${x+18} ${NY}" class="cord dot"/><text x="${x-10}" y="${NY-14}" font-size="8">PRCH</text>`;
  if(tr.trigpos!=null){const j=tr.trigpos;h+=`<path d="M${x+NW/2} ${NY} C${x+NW/2} ${NY-26},${nx(j)+NW/2} ${NY-26},${nx(j)+NW/2} ${NY}" class="cord dot"/><text x="${(x+nx(j))/2+NW/2-18}" y="${NY-18}" font-size="8">TRIG ›T${j+1}</text>`}
  h+=`<g class="node ${fx?"fx":""} ${sel?"sel":""} ${audible(i)?"":"muted"}" data-node="${i}"><rect class="body" x="${x}" y="${NY}" width="${NW}" height="${NH}" rx="3"/>${sel?`<rect class="frame" x="${x-4}" y="${NY-4}" width="${NW+8}" height="${NH+8}" rx="5"/>`:""}
   <text x="${x+8}" y="${NY+16}" font-size="10">T${i+1}${fx?" · FX":""}</text><text x="${x+8}" y="${NY+34}" font-size="12">${shortM(tr.m)}</text><text x="${x+8}" y="${NY+49}" font-size="8" opacity=".8">${tr.name.toUpperCase().slice(0,18)}</text></g>`});
 return`<svg class="route" viewBox="0 0 ${W} ${H}" preserveAspectRatio="xMidYMid meet" role="img" aria-label="Routing: tracks, mix buses, outputs">${h}</svg>`}
function sv2(tr,n){const i=MACH[tr.m].p.indexOf(n);return i<0?0:tr.v.SYN[i]}
function renderMix(){const T=S.tracks;
 const flows=BUSES.map(b=>{const f=busFlow(b);return`<span>${b}: ${f.items.join(" + ")||"<i>empty</i>"} <i>›</i> ${outOf(b)}${f.notes.length?" <i>· "+f.notes.join(", ")+"</i>":""}</span>`}).join("");
 $("#main").innerHTML=`<div class="routewrap">
  <div class="strips6 mixrow">${T.map((tr,i)=>{const fx=isFx(tr.m);return`<div class="strip6 ${i===S.sel?"sel":""}" data-sel="${i}" style="${audible(i)?"":"opacity:.5"}">
   <div class="shead"><b>T${i+1} ${shortM(tr.m)}</b><span>${tr.name}</span></div>
   <div class="frow"><div class="fcol"><div class="fader" role="slider" tabindex="0" aria-label="T${i+1} level" data-g="lev" data-t="${i}" title="LEV: the track's master level. It cannot be locked or modulated."><div class="tr"><i></i></div><div class="cap2"></div></div><div class="v" data-show="${i}" title="LEV: the track's level"></div></div>
    <div class="pcs">${pc("AMP",5,{t:i,label:"VOL"})}${pc("AMP",6,{t:i,label:"PAN"})}${pc("AMP",4,{t:i,label:"DIST"})}${pc("EFX",4,{t:i,label:"DSND"})}</div></div>
   <div class="busrow"><span class="inlab">Out</span><div class="busk">${BUSES.map(b=>`<button data-bus="${b}" data-t="${i}" aria-pressed="${tr.out[b]}" title="${tr.out[b]?"Sends to":"Not sent to"} mix bus ${b}">${b}</button>`).join("")}</div></div>
   <div class="busrow"><span class="inlab">In</span>${fx?`<select id="inp${i}" data-inp="${i}" aria-label="T${i+1} input">${INPUTS.filter(x=>!(x==="NEIBOR"&&i===0)).map(x=>opt(x,x,tr.inp)).join("")}</select>`:`<button class="kselbtn" disabled title="Only FX machines take an input"><span>synth · none</span></button>`}</div>
   <div class="mrow"><button class="ms m ${ARMED.has(i)?"prep":""}" ${ARMED.has(i)?`data-prep="${ARMED.get(i)?"X":"+"}"`:""} data-mute="${i}" aria-pressed="${tr.mute}" aria-label="Mute T${i+1}">M</button><button class="ms s" data-solo="${i}" aria-pressed="${tr.solo}" aria-label="Solo T${i+1}">S</button></div></div>`}).join("")}</div>
  <div class="routebar"><span class="cap">Routing</span><span class="seg" data-set="routing">${ROUTES.map(r=>`<button data-v="${r}" aria-pressed="${S.routing===r}">${r.replace("+"," + ")}</button>`).join("")}</span>
   <span class="grow"></span><span class="hint">Tracks sum into a bus in track order, so an FX on a bus only hears the tracks before it. Click a node to pick its strip.</span></div>
  ${routeSvg()}<div class="flowl">${flows}</div></div>`;
 syncControls()}
