
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
function arpCells(a){return Array.from({length:16},(_,k)=>{const o=a.ofs[k],on=a.rhy[k],na=k>=a.len;const hh=Math.abs(o)/24*40;
 return`<div class="ac ${na?"na":""} ${on?"":"mute"}" data-ac="${k}" title="Step ${k+1}: ${na?"past the end (click to extend)":on?"plays, offset "+(o>0?"+":"")+o:"muted"}. Click = mute, drag = offset.">${na?"":`<span class="z"></span><i style="${o>=0?`bottom:50%;height:${Math.max(2,hh)}px`:`top:50%;height:${hh}px`}"></i><b>${o?(o>0?"+":"")+o:""}</b>`}</div>`}).join("")}
function arpPanel(t){const tr=trk(t),a=tr.arp,midi=isMidiT(t);
 return`<div class="dock3"><div style="display:grid;gap:7px;align-content:start">
   <div class="kv"><span class="mono" style="width:40px">MODE</span><span class="seg" data-set="arpmode">${ARPM.map((m,i)=>`<button data-v="${i}" aria-pressed="${a.MODE===i}">${m}</button>`).join("")}</span></div>
   <div class="kv"><span class="mono" style="width:40px">PLAY</span><span class="seg" data-set="arpplay">${ARPP.map((m,i)=>`<button data-v="${i}" aria-pressed="${a.PLAY===i}">${m}</button>`).join("")}</span></div>
   <div class="kv"><span class="mono" style="width:40px">TRIG</span>${midi?`<span class="hint">MIDI arpeggiators have no envelope switches.</span>`:["amp","flt","lfo"].map(k=>`<button class="lkey" data-arptrig="${k}" aria-pressed="${!!a[k]}"><i class="led"></i>${k.toUpperCase()}</button>`).join("")}</div>
   <div class="ctl" style="grid-template-columns:repeat(3,minmax(0,1fr))">${pc("arp","SPD",{t})}${pc("arp","RNGE",{t})}${pc("arp","OJMP",{t})}</div></div>
  <div style="display:grid;gap:3px;align-content:start"><span class="cap" id="arpcap" style="font-size:11px">Rhythm + offset · ${a.len} steps</span>
   <div class="arptrack" id="arptrack">${arpCells(a)}</div>
   <div class="arplenrow">${Array.from({length:16},(_,k)=>`<button class="${k<a.len?"on":""}" data-arplen="${k+1}" aria-label="Length ${k+1}" title="Length ${k+1} (the LEVEL knob)"></button>`).join("")}</div></div>
  <div class="statemach">${ARPHELP[a.MODE]}<br><br>SPD 6 = a 16th. Chords come from the trig: shift-click in the Sequence roll. The ARP strip at the bottom of the roll shows what it plays.</div></div>`}
function trnPanel(t){const tr=trk(t);
 return`<div class="dock3"><div style="display:grid;gap:8px;align-content:start"><div class="ctl" style="grid-template-columns:repeat(2,minmax(0,1fr))">${pc("trn","TRACK",{t,label:"TRACK"})}${pc("ptrn","PAT",{t,label:"PAT · all"})}</div>
  <div class="kv"><span class="mono" style="width:44px">SCALE</span><span class="seg" data-set="scale">${SCALES.map((m,i)=>`<button data-v="${i}" aria-pressed="${tr.tr.SCALE===i}">${m}</button>`).join("")}</span></div>
  <div class="kv"><span class="mono" style="width:44px">KEY</span>${tr.tr.SCALE>1?`<div style="width:120px">${pc("key","KEY",{t})}</div>`:`<span class="hint">KEY is used by MAJ and MIN.</span>`}</div></div>
  <div class="hint">${["<b>---</b> adds track, pattern, song and multi-trig transpose.","<b>FIX</b> only track transpose applies. Use it for drums and FX.","<b>MAJ</b> transposed notes stay in the major scale of KEY. Out-of-scale rows in the lane are hatched.","<b>MIN</b> transposed notes stay in the minor scale of KEY. Out-of-scale rows in the lane are hatched."][tr.tr.SCALE]}<br><br>Transpose is live: the programmed notes do not change. Song rows and multi trig add their own.</div>
  <div class="statemach">PAT ${fmt({signed:1},S.patTrn)} + TRACK ${fmt({signed:1},tr.tr.TRACK)}${tr.tr.SCALE===1?" (FIX: pattern ignored)":""}<br>= plays ${(()=>{const d=tr.tr.TRACK-64+(tr.tr.SCALE===1?0:S.patTrn-64);return(d>0?"+":"")+d})()} semitones</div></div>`}
function trigPanel(t){const tr=trk(t);
 return`<div class="dock3"><div style="display:grid;gap:8px;align-content:start"><div class="two" style="grid-template-columns:1fr"><label>Trig position · forward this track's notes<select id="trigpos">${opt("","none",tr.trigpos??"")}${[0,1,2,3,4,5].filter(k=>k!==t).map(k=>opt(k,"also play "+tLabel(k)+" "+S.tracks[k].m,tr.trigpos??"")).join("")}</select></label></div>
  <div class="kv"><span class="mono" style="width:50px">PORT</span><span class="seg" data-set="port">${["ALWAYS","ONLY LEGATO"].map((m,i)=>`<button data-v="${i}" aria-pressed="${tr.port===i}">${m}</button>`).join("")}</span></div></div>
  <div style="display:grid;gap:8px;align-content:start"><div class="kv"><span class="mono" style="width:50px">LEGATO</span>${["amp","flt","lfo"].map(k=>`<button class="lkey" data-leg="${k}" aria-pressed="${!!tr.leg[k]}"><i class="led"></i>${k.toUpperCase()}</button>`).join("")}</div>
  <div class="ctl" style="grid-template-columns:repeat(3,minmax(0,1fr))">${pc("AMP",7,{t,label:"PORT"})}${pc("AMP",1,{t,label:"HOLD"})}${pc("AMP",2,{t,label:"DEC"})}</div></div>
  <div class="hint">LEGATO picks the envelopes that still fire when notes overlap: all on = staccato bass, all off = a smooth lead. HOLD and DEC shape how long a gate really sounds (the light part of a bar in the roll).</div></div>`}
function midiPanel(t){const tr=trk(t);
 return`<div class="dock3"><div style="display:grid;gap:8px;align-content:start"><div class="two" style="grid-template-columns:1fr 1fr"><label>Channel<select id="mch">${Array.from({length:16},(_,k)=>opt(k+1,"CH "+String(k+1).padStart(2,"0"),tr.ch)).join("")}</select></label><label>LFOs<span class="hint" style="text-transform:none;letter-spacing:0">shared with T${t-5} ${S.tracks[t-6].m}</span></label></div>
  <div class="ctl four">${[0,1,2,3].map(i=>pc("MID",i,{t,label:FIXED.MID[i]})).join("")}</div></div>
  <div style="display:grid;gap:8px;align-content:start"><div class="ctl four">${[4,5,6,7].map(i=>pc("MID",i,{t,label:"CC "+(tr.cc[i-4]===128?"AFT":tr.cc[i-4])})).join("")}</div><div class="ctl four">${[0,1,2,3].map(i=>pc("cc",i,{t,label:"CL"+(i+1)})).join("")}</div></div>
  <div class="hint">LEN 127 = until a NOTE OFF; drag a note's end in the lane to lock its LEN. PCHG only sends when it is locked. CL1-4 pick the CC numbers (or AFT). Internal tracks can also send MIDI (GLOBAL › CONTROL OUT1), but without these values.</div></div>`}
