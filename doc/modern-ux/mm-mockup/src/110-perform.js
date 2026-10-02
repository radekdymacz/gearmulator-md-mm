
/* ===== Perform: keyboard modes, multi trig, multi map, multi env, assign, mutes ===== */
const PMODES=[["normal","Auto track","Keys play the track in focus (AUTO TRACK channel 9)."],["multi","Multi trig","One source plays all six tracks as one big mono synth (channel 7)."],["map","Multi map","Key ranges start patterns, with offset, length and transpose (channel 8)."],["poly","Poly","All six engines play the track in focus with six voices. Other tracks go quiet."]];
const MTM=["ALL TRK","SPLIT KEY","SEQ START","SEQ TRNSP"],TIMS=["DIRECT","1/16","2/16","4/16","8/16","16/16","32/16"];
const isBlack=n=>[1,3,6,8,10].includes(n%12);
function kbRange(){const lo=12*(S.kbOct+1);return[lo,lo+47]}
function zoneOf(n){if(S.mode!=="multi"||S.multi.mode!==1)return null;return n>=S.multi.splitKey?"up":"lo"}
function mapRow(n){let lo=0;for(let i=0;i<S.mmap.length;i++){const r=S.mmap[i];if(n>=lo&&n<=r.hi)return i;lo=r.hi+1}return -1}
function renderKb(){const[lo,hi]=kbRange(),whites=[];for(let n=lo;n<=hi;n++)if(!isBlack(n))whites.push(n);const ww=100/whites.length;let h="",wi=0;
 for(let n=lo;n<=hi;n++){if(isBlack(n)){h+=`<div class="b" data-key="${n}" style="left:${wi*ww-ww*.3}%;width:${ww*.6}%" aria-label="${noteName(n)}"></div>`;continue}
  h+=`<div class="w" data-key="${n}" style="left:${wi*ww}%;width:${ww}%" aria-label="${noteName(n)}">${n%12===0?`<small>${noteName(n)}</small>`:""}</div>`;wi++}
 if(S.mode==="multi"&&S.multi.mode===1){const sk=S.multi.splitKey;let idx=whites.findIndex(w=>w>=sk);if(idx<0)idx=whites.length;const x=idx*ww;
  h+=`<div class="zone" style="left:0;width:${x}%;background:var(--ledg)"></div><div class="zone" style="left:${x}%;width:${100-x}%;background:var(--led)"></div><div class="splitm" id="splitm" style="left:${x}%" title="Drag the split point"><span>SPLIT ${noteName(sk)} · T${S.multi.splitTrack}-6 above</span></div>`}
 return h}
function mapBands(){const[lo,hi]=kbRange(),whites=[];for(let n=lo;n<=hi;n++)if(!isBlack(n))whites.push(n);const ww=100/whites.length,xOf=n=>{let i=whites.findIndex(w=>w>=n);if(i<0)i=whites.length;return i*ww};let from=0;
 return S.mmap.map((r,i)=>{const a=Math.max(from,lo),b=Math.min(r.hi,hi);from=r.hi+1;if(b<lo||a>hi)return"";const x0=xOf(a),x1=xOf(b+1);return`<div class="band ${i===S.mmapSel?"sel":""}" data-band="${i}" style="left:${x0}%;width:${Math.max(2,x1-x0)}%">${r.pat<0?"CUR":patName(r.pat)}${r.trn!==0&&r.trn!==64?"":""}</div>`}).join("")}
ED.menv={draw(g,W,H){const e=S.menv,ink=cssv("--ink"),T=18,B=H-10,y=v=>B-v*(B-T),a=e.ATK/127*W*.22,d=e.DEC/127*W*.24,s=e.SUS/127,r=e.REL>=127?W*.3:e.REL/127*W*.26,hold=W*.28;grid(g,W,H);
  g.strokeStyle=ink;g.lineWidth=2.2;g.beginPath();g.moveTo(0,y(0));g.lineTo(a,y(1));g.lineTo(a+d,y(s));g.lineTo(a+d+hold,y(s));g.lineTo(Math.min(W,a+d+hold+r),e.REL>=127?y(s):y(0));g.stroke();label(g,"ADSR over all tracks"+(e.REL>=127?" · REL ∞":""))},
 handles(W,H){const e=S.menv,T=18,B=H-10,y=v=>B-v*(B-T),a=e.ATK/127*W*.22,d=e.DEC/127*W*.24,hold=W*.28;return[{x:Math.max(5,a),y:y(1),k:"ATK",drag:x=>e.ATK=clamp(Math.round(x/(W*.22)*127))},{x:a+d,y:y(e.SUS/127),k:"DEC · SUS",drag:(x,yy)=>{e.DEC=clamp(Math.round((x-a)/(W*.24)*127));e.SUS=clamp(Math.round((B-yy)/(B-T)*127))}},
  {x:Math.min(W-6,a+d+hold+(e.REL/127*W*.26)),y:y(0)-4,k:"REL",drag:x=>e.REL=clamp(Math.round((x-a-d-hold)/(W*.26)*127))}]}};
function asgT(){return S.sel<6?S.sel:S.sel-6}
function renderPerform(){const pm=PMODES.find(p=>p[0]===S.mode),t=asgT(),tr=S.tracks[t],A=tr.assign,rows=A.tabs[S.asTab];
 const mtCtl=S.mode==="multi"?`<span class="seg" data-set="mtmode">${MTM.map((m,i)=>`<button data-v="${i}" aria-pressed="${S.multi.mode===i}">${m}</button>`).join("")}</span>
   ${S.multi.mode===1?`<span class="stepper"><span class="ilab">Upper from</span><button data-strk="-1">−</button><b class="mono">T${S.multi.splitTrack}</b><button data-strk="1">+</button></span>`:""}
   ${S.multi.mode>=2?`<span class="stepper"><span class="ilab">Timing</span><button data-tim="-1">−</button><b class="mono">${TIMS[S.multi.timing]}</b><button data-tim="1">+</button></span>`:""}`:"";
 const asRow=(r,k)=>`<div class="asrow"><b>${k+1}</b><select id="asp${k}" aria-label="Assign ${k+1} page">${[...PAGES,"PTCH"].map(p=>opt(LPAGES.indexOf(p),p,r.pg)).join("")}</select><select id="asd${k}" aria-label="Assign ${k+1} destination">${destNames(t,r.pg).map((d,i)=>opt(i,d,r.d)).join("")}</select>${pc("asg",k,{t,label:"ADD"})}</div>`;
 const joy=S.asTab.startsWith("JOY");
 $("#main").innerHTML=`<div class="perf ${S.mode==="map"?"map":""}">
  <div class="perf3">
   <section class="card menvcard"><header><h3>Multi envelope</h3><span>both DATA PAGE keys · multi trig only</span></header><canvas class="ed" data-ed="menv" aria-label="Multi envelope. Drag the dots."></canvas>
    <div class="ctl" style="grid-template-columns:repeat(5,minmax(0,1fr))">${["ATK","DEC","SUS","REL","PORT"].map(n=>pc("menv",n,{})).join("")}</div>
    <div class="hint">On top of every track's own envelope. For no effect: ATK 0, DEC, SUS and REL at 127.</div></section>
   <section class="card asgcard"><header><h3>Assign · T${t+1} ${shortM(tr.m)}</h3><span class="seg" data-set="astrk">${[0,1,2,3,4,5].map(k=>`<button data-v="${k}" aria-pressed="${k===t}">${k+1}</button>`).join("")}</span></header>
    <span class="seg" data-set="astab">${Object.keys(A.tabs).map(k=>`<button data-v="${k}" aria-pressed="${S.asTab===k}">${k}</button>`).join("")}</span>
    <div class="asgn">${joy?`<div class="joy" id="joy" title="Drag the stick. It springs back."><span class="cross"></span><span class="cross2"></span><small class="jl">L</small><small class="jr">R</small><small class="ju">U</small><small class="jd">D</small><span class="knobj" id="knobj" style="left:${50+S.joy.x*42}%;top:${50-S.joy.y*42}%"></span></div>`:""}
     <div style="display:grid;gap:6px">${rows.map(asRow).join("")}
      ${S.asTab==="JOY RL"?`<button class="lkey" data-mirr="1" aria-pressed="${A.mirr}"><i class="led"></i>MIRR (left = −right)</button>`:""}
      ${S.asTab==="KEY"?`<span class="keyrow"><button class="lkey" data-ktrk="hpf" aria-pressed="${A.hpf}"><i class="led"></i>HPF tracks keys</button><button class="lkey" data-ktrk="lpf" aria-pressed="${A.lpf}"><i class="led"></i>LPF tracks keys</button></span>`:""}
      <div class="hint">${{"JOY RL":"Tabletop: pitch bend. MIRR off = right only.","JOY U":"Tabletop: mod wheel (CC 1).","JOY D":"Tabletop: breath controller (CC 2).",VEL:"Velocity from the keys. The sequencer always plays velocity 100.",KEY:"Follows the last key: low for C-1, near max for C-8."}[S.asTab]} ADD −64…+63.</div></div></div></section>
   <section class="card"><header><h3>Mutes</h3><span>global · FUNCTION + BANK GROUP</span></header>
    <div class="mutelab">Synth tracks</div><div class="mutes12">${S.tracks.map((x,i)=>`<button class="${x.mute?"":"on"}" data-gmute="${i}" aria-pressed="${!x.mute}" title="T${i+1} ${x.m}"><i class="led"></i>T${i+1}</button>`).join("")}</div>
    <div class="mutelab">MIDI tracks</div><div class="mutes12">${S.midi.map((x,i)=>`<button class="${x.mute?"":"on"}" data-gmute="${i+6}" aria-pressed="${!x.mute}" title="M${i+1} CH${x.ch}"><i class="led"></i>M${i+1}</button>`).join("")}</div>
    <div class="hint">Mutes work on notes: a sound already playing rings out.</div>
</section></div>

  ${S.mode==="map"?`<section class="card maprow"><header><h3>Multi map</h3><span>GLOBAL › CONTROL › MULTIMAP EDIT · stored in the global slot</span></header>
   <table class="mmap"><thead><tr><th>Range</th><th>Pattern</th><th>Offset</th><th>Length</th><th>Transpose</th><th>Timing</th><th></th></tr></thead><tbody>${(()=>{let lo=0;return S.mmap.map((r,i)=>{const a=lo;lo=r.hi+1;return`<tr class="${i===S.mmapSel?"sel":""}" data-mrow="${i}"><td class="mono">${noteName(a)} – <span class="stepper"><button data-mhi="-1" data-i="${i}" ${i===S.mmap.length-1?"disabled":""}>−</button><b class="mono">${i===S.mmap.length-1?"G-9":noteName(r.hi)}</b><button data-mhi="1" data-i="${i}" ${i===S.mmap.length-1?"disabled":""}>+</button></span></td>
    <td><select id="mpat${i}" data-mpat="${i}" aria-label="Pattern">${opt(-1,"CUR",r.pat)}${Array.from({length:32},(_,p)=>opt(p,patName(p),r.pat)).join("")}</select></td><td style="width:90px">${pc("mmap","ofs",{extra:`data-i="${i}"`,label:"OFS"})}</td><td style="width:90px">${pc("mmap","len",{extra:`data-i="${i}"`,label:"LEN"})}</td><td style="width:90px">${pc("mmap","trn",{extra:`data-i="${i}"`,label:"TRN"})}</td><td style="width:90px">${pc("mmap","tim",{extra:`data-i="${i}"`,label:"TIM"})}</td>
    <td><button class="iconkey" data-mdel="${i}" aria-label="Delete range" title="Delete range (FUNCTION + UP)" ${S.mmap.length<2?"disabled":""}>×</button></td></tr>`}).join("")})()}</tbody></table>
   <div class="btnrow"><button data-madd="1">Split selected range</button><span class="hint">A range's lower key is the key after the one above. LEN needs an offset first. Timing waits for a bar line.</span></div></section>`:""}

  <div class="kbsec">
  <div class="perfbar kbtitle"><span class="cap">Keyboard</span><span class="keyrow">${PMODES.map(([k,n])=>`<button class="lkey" data-pmode="${k}" aria-pressed="${S.mode===k}"><i class="led"></i>${n}</button>`).join("")}</span>${mtCtl}
   <span class="stepper" style="margin-left:auto"><span class="ilab">Octave</span><button data-kboct="-1" aria-label="Octave down">−</button><b class="mono">${noteName(kbRange()[0])}</b><button data-kboct="1" aria-label="Octave up">+</button></span></div>
  <div class="hint">${pm[2]} ${S.mode==="multi"?{0:"Notes go to all six tracks; each keeps its own arpeggiator.",1:`Keys from ${noteName(S.multi.splitKey)} play T${S.multi.splitTrack}-T6, keys below play T1-T${S.multi.splitTrack-1}. The multi envelope is not used here.`,2:"Each note restarts the pattern, transposed from C-4.",3:"The first note starts the pattern; later notes only transpose it, so the loop never breaks."}[S.multi.mode]:""}</div>
  <div class="kbwrap">${S.mode==="map"?`<div class="mapbands">${mapBands()}</div>`:""}<div class="kb" id="kb">${renderKb()}</div>
   <div class="trklamps" id="trklamps">${S.tracks.map((x,i)=>`<span><i class="led" data-lamp="${i}"></i>T${i+1} ${shortM(x.m)}</span>`).join("")}<span class="hint" id="kbinfo" style="margin-left:auto">Click or drag across the keys to play.</span></div></div>
  </div></div>`;
 /* the cards keep their hints in view (tipify went with the Sound page's groups) */
 syncControls();redraw()}
let kbDown=null,lampT={};
function flashTracks(ts){ts.forEach(i=>{const l=$(`[data-lamp="${i}"]`);if(!l)return;l.classList.add("on");clearTimeout(lampT[i]);lampT[i]=setTimeout(()=>l.classList.remove("on"),160)})}
/* a key shown held (null: none) and the keyboard's info line (null: unchanged) */
function setKeyDown(n,info){$$(".kb .dn").forEach(k=>k.classList.remove("dn"));if(n!=null)$(`.kb [data-key="${n}"]`)?.classList.add("dn");const el=$("#kbinfo");if(el&&info!=null)el.textContent=info}
function playKey(n){if(HOST.playKey)return HOST.playKey(n);setKeyDown(n,null);let ts=[],msg="";
 if(S.mode==="normal"||S.mode==="poly"){const t=asgT();ts=[t];msg=`${S.mode==="poly"?"POLY ":""}T${t+1} ${noteName(n)}`}
 else if(S.mode==="multi"){const m=S.multi.mode;if(m===0){ts=[0,1,2,3,4,5];msg=`All tracks ${noteName(n)}`}else if(m===1){const up=n>=S.multi.splitKey,a=S.multi.splitTrack-1;ts=up?[0,1,2,3,4,5].filter(i=>i>=a):[0,1,2,3,4,5].filter(i=>i<a);msg=`${up?"Upper":"Lower"} zone ${noteName(n)}`}
  else{S.mtTrn=n-60;msg=`${m===2?"Restart":"Transpose"} pattern ${S.mtTrn>=0?"+":""}${S.mtTrn}${S.multi.timing?" at the next "+TIMS[S.multi.timing]:""}`;if(!S.playing)togglePlay();else if(m===2){S.step=-1}ts=[0,1,2,3,4,5]}}
 else{const i=mapRow(n);if(i>=0){const r=S.mmap[i];S.mmapSel=i;msg=`${noteName(n)} › ${r.pat<0?"current":patName(r.pat)} from step ${r.ofs+1}${r.len?", "+r.len+" steps":""}${r.trn!==64&&r.trn?"":""}`;$$(".band").forEach(b=>b.classList.toggle("sel",+b.dataset.band===i))}}
 flashTracks(ts);setKeyDown(n,msg)}
