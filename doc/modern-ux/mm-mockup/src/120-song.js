
/* ===== Song (from the MD Editor, with the MM's per-row transpose, 6 + 6 mutes, 200 rows) ===== */
const T64=()=>Array(12).fill(64);
S.song=[{pat:0,rep:2},{pat:1,rep:2,trn:69},{pat:0,rep:2,mutes:[3]},{pat:2,rep:1,ofs:16,len:16},{pat:1,rep:2,ttr:[64,64,64,71,64,64,64,64,64,64,64,64]},{pat:3,rep:1,bpm:124,mutes:[4,7]},{type:"loop",to:1,count:2},{type:"end"}];
S.songSel=1;S.bank=0;
/* the row's rarer settings (per-track transpose, part, mutes) behind MORE, as the Machinedrum Editor's Song (2026-10) */
S.songMore=false;
function patLen(p){return p===S.pat?S.len:S.patInfo[p].len}
const hasPat=p=>p===S.pat?curHas():S.patInfo[p].has;
const rowLen=r=>r.len??(patLen(r.pat)-(r.ofs||0));
function songSteps(){let n=0;S.song.forEach(r=>{if(!r.type)n+=rowLen(r)*r.rep});S.song.forEach((r,i)=>{if(r.type==="loop"&&r.count!==Infinity){let seg=0;for(let k=r.to;k<i;k++){const q=S.song[k];if(!q.type)seg+=rowLen(q)*q.rep}n+=seg*(r.count-1)}});return n}
function songTime(){const sec=songSteps()*60/S.bpm/4;return`${Math.floor(sec/60)}:${String(Math.round(sec%60)).padStart(2,"0")}`}
function loopOf(i){return S.song.findIndex((r,k)=>r.type==="loop"&&k>i&&r.to<=i)}
const hasTtr=r=>r.ttr&&r.ttr.some(v=>v!==64);
/* the song to edit: any of the 24; LOAD SONG makes it the machine's (only while stopped) */
function songPick(){const g=S.songs;if(!g)return"";const nn=i=>String(i+1).padStart(2,"0");
 return`<span class="songpick"><select id="songsel" aria-label="Song to edit">${g.names.map((n,i)=>`<option value="${i}"${i===g.slot?" selected":""}>S${nn(i)} ${n}${i===g.current?" · on the machine":""}</option>`).join("")}</select>
  <button id="songload"${g.slot===g.current?" disabled":""} title="LOAD SONG: the machine plays this song in song mode. Only while stopped.">Load on the machine</button></span>`}
/* ===== Chain (MM-P8, as the Machinedrum Editor's Song): one palette, two ways to play its pads. ARRANGE adds
   to the song; CHAIN numbers the pads into the machine's own chain (hold BANK, press the TRIG keys; manual
   1-46: one bank, each pattern once, loops), sent at once. The header says what the machine plays (playsOf).
   The machine's chain comes from the host (MMView.setPlays: machine.desk.chain, song mode); on its own the
   example engine plays it at the pattern ends (chainWrap). */
S.songPick="arrange";S.chainDraft=[];S.chainTimer=0;S.chainSent=false;
S.plays={chain:null,songMode:false,song:0};
const nn2=i=>String(i+1).padStart(2,"0");
/* what the machine plays: in song mode the song (the Monomachine keeps a chain there but plays the song,
   measured); else its chain while one is active; else the current pattern. A long chain is shortened in the
   label (first two, last). */
function playsOf(){const c=S.plays.chain;
 if(S.plays.songMode)return{kind:"song",label:"SONG "+nn2(S.plays.song??0)};
 if(c&&c.active&&c.patterns.length){const p=c.patterns.map(patName),short=p.length>4?[p[0],p[1],"…",p[p.length-1]]:p;return{kind:"chain",label:"CHAIN "+short.join("»"),patterns:c.patterns.slice()}}
 return{kind:"pattern",label:"PATTERN "+patName(S.pat)}}
const canChain=()=>!NA.chains;
/* the example engine's chain (no host): the next entry at each pattern end */
function chainWrap(){const c=S.plays.chain;if(!c||!c.active||!c.patterns.length)return null;const p=c.patterns[c.next%c.patterns.length];c.next=(c.next+1)%c.patterns.length;return p}
function sendChain(d){if(HOST.chain)return HOST.chain(d);S.plays.chain={active:true,next:0,patterns:d.slice()};if(!S.playing){applyPattern(chainWrap())}toast(S.playing?"Chained: the machine plays them in this order from the pattern end, and loops.":"Chained: PLAY starts at "+patName(d[0])+", then loops.")}
function clearChain(){if(HOST.chainClear)return HOST.chainClear();S.plays.chain=null;render()}
/* every pad (and BACK) chains at once: the pads go out as the machine's chain 150 ms after the last click
   (the latest wins; the host also holds a chain back while the keys of the one before are on their way).
   Fewer than two pads: the chain the machine plays ends. */
function chainSoon(){clearTimeout(S.chainTimer);
 S.chainTimer=setTimeout(()=>{const d=S.chainDraft.slice(),c=S.plays.chain;if(!canChain())return;
  if(d.length>=2){if(!(c&&c.active&&c.patterns.length===d.length&&c.patterns.every((p,i)=>p===d[i]))){sendChain(d);S.chainSent=true}}
  else if((c&&c.active)||S.chainSent){clearChain();S.chainSent=false}},150)}
function chainFooter(){const c=S.plays.chain,bn="ABCDEFGH"[S.bank],d=S.chainDraft;
 const known=!!c||!window.MMHost,active=!!(c&&c.active&&c.patterns.length),list=active?c.patterns:[],at=list.indexOf(S.pat),next=active?list[(at+1)%list.length]:null;
 const live=!canChain()?`<span class="note">${NA.chains}</span>`:!known?`<span class="note">The chain is not readable on this engine.</span>`
  :S.plays.songMode?`<span class="note">Song mode: the machine plays song ${nn2(S.plays.song??0)}. A chain switches it to pattern mode.</span>`
  :active?list.map(p=>`<span class="lcdchip${p===S.pat?" now":""}${S.playing&&p===next&&at>=0?" nx":""}">${patName(p)}</span>`).join("<i>»</i>")+"<i>↺</i>"
  :`<span class="note">No chain. The machine plays ${patName(S.pat)} and stays on it.</span>`;
 return`<div class="chainfoot"><div class="irow"><span class="ilab">Plays</span><div class="chainrow">${live}</div></div>
  <div class="irow"><span class="ilab"></span><span class="chainacts"><button data-chain="undo"${d.length?"":" disabled"} title="Takes the last pad out and chains the rest at once">Back</button><button class="danger" data-chain="clear"${active||d.length?"":" disabled"} title="BANK + the TRIG key of the pattern that plays: the machine's way to end a chain. The pads start over">Clear</button></span>
  <span class="note">${d.length===1?`One more pad and the machine plays the chain (BANK ${bn} held, the TRIG keys in order; ${S.playing?"from the pattern end":"PLAY starts at the first"}).`:"Each pad chains at once: the machine plays them in order and loops."} One bank, each pattern once. Picking a pattern ends the chain; editing its patterns does not.</span></div></div>`}
function chainPad(p){const n=S.chainDraft.indexOf(p),d=S.chainDraft;if(n>=0)d.splice(n,1);else if(d.length<16)d.push(p);render();chainSoon()}
function chainAct(a){if(a==="undo"){S.chainDraft.pop();render();chainSoon();return}
 clearTimeout(S.chainTimer);S.chainDraft=[];render();if(S.plays.chain?.active||S.chainSent){clearChain();S.chainSent=false}}
function renderSong(){const sel=S.song[S.songSel]||S.song[0],chain=S.songPick==="chain",plays=playsOf();
 /* a chain is one bank's: another bank starts the draft over */
 S.chainDraft=S.chainDraft.filter(p=>p>>4===S.bank);
 const pad=p=>{const info=`${patName(p)}<small>${hasPat(p)?patLen(p):"empty"}</small>`;
  if(!chain)return`<button class="padd ${hasPat(p)?"has":""} ${!sel.type&&sel.pat===p?"cur":""}" data-addpat="${p}" draggable="true" title="Drag into the arrangement. Click adds after the selected row.">${info}</button>`;
  const n=S.chainDraft.indexOf(p);
  return`<button class="padd ${hasPat(p)?"has":""}${n>=0?" in":""}" data-chainpad="${p}" title="${patName(p)}${n>=0?": number "+(n+1)+" in the chain. Click takes it out, and the machine plays the rest.":". Click adds it: the machine plays the chain at once."}">${info}${n>=0?`<em>${n+1}</em>`:""}</button>`};
 const palette=`<div class="banks">${[..."ABCDEFGH"].map((b,k)=>`<button class="bank ${k===S.bank?"on":""}" data-bank="${k}"><i class="led"></i>${b}</button>`).join("")}</div>
  <div class="pgridp">${Array.from({length:16},(_,k)=>pad(S.bank*16+k)).join("")}</div>
  ${chain?chainFooter():`<p class="note pnote">Click adds after row ${String(S.songSel+1).padStart(3,"0")} · drag onto the grid</p>`}`;
 const playsTip={chain:"The machine plays its own chain (live, one bank, loops). Clear it in CHAIN, or pick a pattern.",song:"The machine is in song mode: it plays the stored song.",pattern:"The machine is in pattern mode: it plays this pattern and stays on it."}[plays.kind];
 const head=`<header class="phead"><h3>Patterns</h3><span class="seg" data-set="songpick" title="ARRANGE: a click adds the pattern to the song. CHAIN: a click numbers it into the machine's chain.">${[["arrange","Arrange"],["chain","Chain"]].map(([v,t])=>`<button data-v="${v}" aria-pressed="${S.songPick===v}">${t}</button>`).join("")}</span><span class="lcdchip playschip ${plays.kind}" id="songPlays" title="${playsTip}">${plays.label}</span></header>`;
 let insp="";
 if(!sel.type){const L=patLen(sel.pat),o=sel.ofs||0,ln=rowLen(sel),tt=sel.ttr||T64(),trn=(sel.trn??64)-64;
  insp=`<div class="irow"><span class="ilab">Row</span><span class="lcdchip">${String(S.songSel+1).padStart(3,"0")} · ${patName(sel.pat)}</span><span class="stepper"><button data-step="pat" data-d="-1" aria-label="Previous pattern">‹</button><button data-step="pat" data-d="1" aria-label="Next pattern">›</button></span></div>
   <div class="irow"><span class="ilab">Repeat</span><span class="stepper"><button data-step="rep" data-d="-1">−</button><b class="mono">${sel.rep}</b><button data-step="rep" data-d="1">+</button></span>
    <span class="ilab" style="margin-left:18px">Tempo</span><button class="ptog ${sel.bpm?"":"on"}" data-bpmkeep="1"><i class="led"></i>Keep</button>${sel.bpm?`<span class="stepper"><button data-step="bpm" data-d="-1">−</button><b class="mono">${sel.bpm}</b><button data-step="bpm" data-d="1">+</button></span>`:`<span class="note">uses the tempo before it</span>`}</div>
   <div class="irow"><span class="ilab">Transpose</span><span class="stepper"><button data-step="trn" data-d="-1">−</button><b class="mono">${trn>0?"+":""}${trn}</b><button data-step="trn" data-d="1">+</button></span><span class="note">pattern, all tracks · FIX tracks ignore it</span></div>
   <div class="irow"><span class="ilab"></span><button class="ptog morebtn ${S.songMore?"on":""}" data-rowmore="1" aria-expanded="${S.songMore}" title="Transpose single tracks, play part of the pattern, or mute tracks for this row"><i class="led"></i>More<small>${[hasTtr(sel)?"tracks transposed":"",sel.ofs||sel.len?`part ${o+1}–${o+ln}`:"",(sel.mutes||[]).length?`${sel.mutes.length} muted`:""].filter(Boolean).map(x=>" · "+x).join("")||" · per track, part, mutes"}</small></button></div>
   ${S.songMore?`<div class="irow"><span class="ilab">Per track</span><div style="display:grid;gap:3px;flex:1">${[0,6].map(off=>`<div class="ttr"><span class="ilab">${off?"MIDI":"INT"}</span>${[0,1,2,3,4,5].map(k=>{const v=tt[off+k]-64;return`<span class="tt ${v?"on":""}" title="${off?"M":"T"}${k+1}"><button data-ttr="${off+k}" data-d="-1" aria-label="${off?"M":"T"}${k+1} down">−</button><b>${v?(v>0?"+":"")+v:(off?"M":"T")+(k+1)}</b><button data-ttr="${off+k}" data-d="1" aria-label="${off?"M":"T"}${k+1} up">+</button></span>`}).join("")}</div>`).join("")}</div></div>
   <div class="irow"><span class="ilab">Part</span><div class="partbar" style="grid-template-columns:repeat(${L},1fr)">${Array.from({length:L},(_,k)=>`<i class="${k>=o&&k<o+ln?"on":""} ${k%16===0&&k?"pg":""}"></i>`).join("")}</div></div>
   <div class="irow"><span class="ilab"></span><span class="stepper"><span class="ilab">Offset</span><button data-step="ofs" data-d="-1">−</button><b class="mono">${o+1}</b><button data-step="ofs" data-d="1">+</button></span>
    <span class="stepper"><span class="ilab">Length</span><button data-step="len" data-d="-1">−</button><b class="mono">${ln}</b><button data-step="len" data-d="1">+</button></span><button class="ptog ${sel.ofs||sel.len?"":"on"}" data-fullpat="1"><i class="led"></i>Whole pattern</button></div>
   <div class="irow"><span class="ilab">Mutes</span><div style="display:grid;gap:3px;flex:1"><div class="mkeys m12">${[0,1,2,3,4,5].map(k=>`<button class="mkey ${(sel.mutes||[]).includes(k)?"off":""}" data-rowmute="${k}" title="T${k+1} ${S.tracks[k].m}">T${k+1}</button>`).join("")}</div><div class="mkeys m12">${[6,7,8,9,10,11].map(k=>`<button class="mkey ${(sel.mutes||[]).includes(k)?"off":""}" data-rowmute="${k}" title="MIDI ${k-5}">M${k-5}</button>`).join("")}</div></div></div>`:""}`}
 else if(sel.type==="end")insp=`<div class="irow"><span class="ilab">End</span><span class="note">The song stops here. Add patterns before it from the palette.</span></div>`;
 else insp=`<div class="irow"><span class="ilab">Command</span><span class="seg" data-set="loopkind">${["loop","jump","halt"].map(k=>`<button data-v="${k}" aria-pressed="${sel.type===k}">${k.toUpperCase()}</button>`).join("")}</span></div>
   ${sel.type!=="halt"?`<div class="irow"><span class="ilab">${sel.type==="loop"?"Back to":"Jump to"}</span><span class="stepper"><button data-step="to" data-d="-1">−</button><b class="mono">${String(sel.to+1).padStart(3,"0")}</b><button data-step="to" data-d="1">+</button></span></div>`:""}
   ${sel.type==="loop"?`<div class="irow"><span class="ilab">Times</span><span class="stepper"><button data-step="count" data-d="-1">−</button><b class="mono">${sel.count===Infinity?"∞":sel.count}</b><button data-step="count" data-d="1">+</button></span><button class="ptog ${sel.count===Infinity?"on":""}" data-inf="1"><i class="led"></i>Forever</button></div>`:""}
   <div class="irow"><span class="ilab"></span><span class="note">${sel.type==="loop"?"Loops nest. Loops also stretch a part past the 64-step pattern limit.":sel.type==="jump"?"Jumps the song pointer forward.":"Pauses until you pick a row to go on from."}</span></div>`;
 const lines=Math.ceil(200/16);
 $("#main").innerHTML=`<div class="songui lay2"><div class="songleft"><section class="card ${chain?"chainmode":""}">${head}${palette}</section>
   <section class="card"><header><h3>Selected row</h3><span class="rowacts"><button data-rowact="up" title="Move left">←</button><button data-rowact="down" title="Move right">→</button><button data-rowact="dup">Duplicate</button><button data-rowact="loop">Add loop</button><button data-rowact="del" class="danger">Delete</button></span></header><div class="insp">${insp}</div></section></div>
  <section class="card"><header><h3>Arrangement</h3><span class="note">${S.song.length} of 200 rows · T track transpose · M mutes · B tempo</span>${songPick()}</header>
   <div class="durbar" title="Song shape by time (length × repeats)">${S.song.map((r,i)=>r.type?`<i class="db dbm"></i>`:`<i class="db ${i===S.songSel?"sel":""}" data-row="${i}" style="flex:${rowLen(r)*r.rep} 1 0"></i>`).join("")}</div>
   <div class="slotgrid" id="tl">${Array.from({length:lines},(_,line)=>`<span class="sglab">${String(line*16+1).padStart(3,"0")}</span>${Array.from({length:16},(_,c)=>{const i=line*16+c,r=S.song[i];if(i>=200)return`<span></span>`;
     if(!r)return`<div class="scell empty" data-i="${i}"></div>`;
     const cls=`scell ${i===S.songSel?"sel":""} ${r.type?"cmd "+r.type:""} ${!r.type&&loopOf(i)>=0?"inloop":""}`;
     const txt=r.type==="end"?"END":r.type==="loop"?`↺${String(r.to+1).padStart(3,"0")}`:r.type==="jump"?`→${String(r.to+1).padStart(3,"0")}`:r.type==="halt"?"HALT":patName(r.pat);
     const sub=r.type==="loop"?(r.count===Infinity?"∞":"×"+r.count):!r.type?`${r.rep>1?"×"+r.rep:""}${(r.trn??64)!==64?" "+((r.trn-64)>0?"+":"")+(r.trn-64):""}${hasTtr(r)?" T":""}${r.mutes?.length?" M":""}${r.bpm?" B":""}${r.ofs||r.len?"~":""}`:"";
     return`<button class="${cls}" data-row="${i}" data-i="${i}" draggable="${r.type==="end"?"false":"true"}" title="Row ${String(i+1).padStart(3,"0")}${r.type?"":" · "+patName(r.pat)+" ×"+r.rep+" · "+rowLen(r)+" steps"}"><b>${txt}</b><small>${sub}</small></button>`}).join("")}`).join("")}</div></section></div>`}
function songAction(a){const i=S.songSel,r=S.song[i];
 if(a==="del"){if(r.type==="end")return;S.song.splice(i,1);S.songSel=Math.min(i,S.song.length-1)}
 if(a==="dup"&&!r.type){if(S.song.length>=200){toast("A song holds 200 rows.");return}S.song.splice(i+1,0,JSON.parse(JSON.stringify(r)));S.songSel=i+1}
 if(a==="up"&&i>0&&r.type!=="end"){[S.song[i-1],S.song[i]]=[S.song[i],S.song[i-1]];S.songSel=i-1}
 if(a==="down"&&i<S.song.length-2&&r.type!=="end"){[S.song[i+1],S.song[i]]=[S.song[i],S.song[i+1]];S.songSel=i+1}
 if(a==="loop"){const at=r.type==="end"?i:i+1;S.song.splice(at,0,{type:"loop",to:Math.max(0,i-1),count:2});S.songSel=at}
 structEdited();render()}
function songStep(k,d){const r=S.song[S.songSel];
 if(k==="pat")r.pat=(r.pat+d+128)%128;if(k==="rep")r.rep=Math.max(1,Math.min(64,r.rep+d));if(k==="bpm")r.bpm=Math.max(30,Math.min(300,(r.bpm||S.bpm)+d));
 if(k==="trn")r.trn=clamp((r.trn??64)+d,28,100);
 if(k==="ofs"){const L=patLen(r.pat);r.ofs=Math.max(0,Math.min(L-1,(r.ofs||0)+d));if(r.len&&r.ofs+r.len>L)r.len=L-r.ofs}
 if(k==="len"){const L=patLen(r.pat);r.len=Math.max(1,Math.min(L-(r.ofs||0),rowLen(r)+d))}
 if(k==="to")r.to=Math.max(0,Math.min(r.type==="jump"?S.song.length-1:S.songSel-(r.type==="loop"?1:0),r.to+d));
 if(k==="count")r.count=r.count===Infinity?(d<0?63:Infinity):Math.max(1,Math.min(63,r.count+d));
 structEdited();render()}
let drag2=null;
function dropTarget(el){const c=el?.closest?.(".scell");if(!c)return null;const i=+c.dataset.i,endI=S.song.length-1;return i<endI?{i,mode:"onto"}:{i:endI,mode:"append"}}
function showTarget(t){$$(".scell.over,.scell.appendto").forEach(x=>x.classList.remove("over","appendto"));if(!t)return;const c=document.querySelector(`.scell[data-i="${t.mode==="append"?S.song.length:t.i}"]`);c&&c.classList.add(t.mode==="append"?"appendto":"over")}
document.addEventListener("dragstart",e=>{const b=e.target.closest?.(".scell:not(.empty)"),pk=e.target.closest?.(".padd");
 if(b){drag2={kind:"row",v:+b.dataset.row};b.classList.add("dragging")}else if(pk){drag2={kind:"pat",v:+pk.dataset.addpat};pk.classList.add("dragging")}else return;
 e.dataTransfer.effectAllowed=drag2.kind==="row"?"move":"copy";try{e.dataTransfer.setData("text/plain",String(drag2.v))}catch(_){}});
document.addEventListener("dragover",e=>{if(!drag2)return;const t=dropTarget(e.target);if(!t)return;e.preventDefault();showTarget(t)});
document.addEventListener("drop",e=>{if(!drag2)return;const t=dropTarget(e.target);if(!t)return;e.preventDefault();
 if(drag2.kind==="pat"){if(S.song.length>=200)toast("A song holds 200 rows.");else if(t.mode==="onto"&&!S.song[t.i].type){S.song[t.i].pat=drag2.v;S.songSel=t.i}else{const at=t.mode==="onto"?t.i:S.song.length-1;S.song.splice(at,0,{pat:drag2.v,rep:1});S.songSel=at}}
 else{const from=drag2.v;let to=t.mode==="onto"?t.i:S.song.length-1;if(from!==to){const[r]=S.song.splice(from,1);if(from<to&&t.mode==="onto")to--;if(t.mode==="append")to=S.song.length-1;S.song.splice(to,0,r);S.songSel=to}}
 drag2=null;structEdited();render()});
document.addEventListener("dragend",()=>{drag2=null;showTarget(null);$$(".dragging").forEach(x=>x.classList.remove("dragging"))});
