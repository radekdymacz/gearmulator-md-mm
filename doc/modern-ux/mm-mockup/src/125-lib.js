
/* ===== Kit library + Pattern chooser (from the MD Editor v50), adapted to the Monomachine =====
   MM: 128 kits (plus the UNDO KIT), 128 patterns in 8 banks A-H of 16, kit names 11 characters (SysEx 0x55).
   Every MM pattern recalls its kit (1-18); there is no CLASSIC mode. A kit or pattern DUMP is only taken on
   GLOBAL > FILE > SYSEX RECV, so every write that is a dump goes through the RECV / SEND flow (structEdited). */
var LIB={open:null,sel:0,renaming:null,drag:null,sig:""};
const clone=o=>JSON.parse(JSON.stringify(o));
const nn=k=>String(k+1).padStart(2,"0");
const escH=s=>String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
const KNAME=11;
/* Name characters: SysEx 0x55 takes 11 ASCII bytes (7-bit). Which of them the MM font draws is not checked, so the mockup keeps to a safe set. */
const KBAD=/[^A-Z0-9 \-.\/+&!?'#*():]/g;
const KITN={0:"MONOMACHINE",1:"ACID BATH",2:"VOCODED",3:"FM BELLS",4:"DIGI CHOIR",5:"SID LEADS",6:"NOISE TOYS",7:"DUB CHORDS"};
/* kit content (1-18): machines + all DATA pages, LFOs, MIDI page, routing, trig and multi-trig settings, multi env, name */
function captureKit(){return clone({tracks:S.tracks.map(t=>({m:t.m,name:t.name,v:t.v,lev:t.lev,out:t.out,inp:t.inp,trigpos:t.trigpos,port:t.port,leg:t.leg,assign:t.assign})),midi:S.midi.map(t=>({v:t.v,cc:t.cc,ch:t.ch,name:t.name})),multi:S.multi,menv:S.menv})}
const FACTORY=captureKit();
const FKM=[["SWAVE-PULS","FM+STAT","SID-6581","DPRO-WAVE","GND-NOIS","FX-CHORUS"],["VO-6","SWAVE-ENS","FM+DYN","DPRO-BBOX","SID-6581","FX-REVERB"]];
function setMach(d,i,m){const t=d.tracks[i];t.m=m;t.v.SYN=synDefaults(m);t.name=machName(m);t.inp=isFx(m)?(i?"NEIBOR":"INP AB"):t.inp}
function factoryKit(k){const d=clone(FACTORY);if(!k)return d;const set=FKM[k%2];d.tracks.forEach((t,i)=>{if((k+i)%3===0)setMach(d,i,set[i]);Object.values(t.v).forEach(a=>a.forEach((x,j)=>a[j]=clamp(x+(k*37+i*11+j*5)%21-10)))});return d}
function clearedKit(){const d=clone(FACTORY);d.tracks.forEach((t,i)=>{t.m="GND-SIN";t.name="Sine";t.v={SYN:synDefaults("GND-SIN"),AMP:[...DEFV.AMP],FLT:[...DEFV.FLT],EFX:[...DEFV.EFX],LF1:[...DEFV.LFO],LF2:[...DEFV.LFO],LF3:[...DEFV.LFO]};t.lev=100;t.out={AB:true,CD:false,EF:false};t.inp="NEIBOR";t.trigpos=null});return d}
S.kits=Array.from({length:128},(_,k)=>({name:KITN[k]||"",empty:!KITN[k],data:null}));S.workName=S.kits[0].name;
function kitData(k){const s=S.kits[k];return s.data?clone(s.data):s.empty?clearedKit():factoryKit(k)}
function pruneLocks(){for(const k of [...S.locks.keys()]){const[t,p]=k.split("|");if(!pname(+t,p))S.locks.delete(k)}if(!pname(S.sel,S.lane)){S.lane=isMidiT(S.sel)?"MID.1":"FLT.1";S.lanePage=S.lane.split(".")[0]}}
function applyKit(d){d=clone(d);S.tracks.forEach((t,i)=>Object.assign(t,d.tracks[i]));S.midi.forEach((t,i)=>Object.assign(t,d.midi[i]));S.multi=d.multi;S.menv=d.menv;pruneLocks()}
/* pattern content (1-53): notes, locks, kit link, MIDI notes, arp / transpose / swing / slide per track; plus scale setup */
const packSeq=t=>({steps:t.steps,slide:[...t.slide],swing:[...t.swing],arp:t.arp,tr:t.tr});
function capturePat(){return clone({len:S.len,mult:S.mult,swingAmt:S.swingAmt,patTrn:S.patTrn,tr:[...S.tracks,...S.midi].map(packSeq),locks:[...S.locks].map(([k,m])=>[k,[...m]])})}
function emptyPat(len){return{len,mult:"1X",swingAmt:50,patTrn:64,tr:[...S.tracks,...S.midi].map(()=>({steps:Array(64).fill(null),slide:[],swing:[1,3,5,7,9,11,13,15].flatMap(x=>[x,x+16,x+32,x+48]),arp:newArp(),tr:{TRACK:64,SCALE:0,KEY:0}})),locks:[]}}
function applyPat(d){d=clone(d);S.len=d.len;S.mult=d.mult;S.swingAmt=d.swingAmt;S.patTrn=d.patTrn;[...S.tracks,...S.midi].forEach((t,i)=>{const x=d.tr[i];t.steps=x.steps;t.slide=new Set(x.slide);t.swing=new Set(x.swing);t.arp=x.arp;t.tr=x.tr});S.locks=new Map(d.locks.map(([k,m])=>[k,new Map(m)]));pruneLocks()}
S.patInfo=Array.from({length:128},(_,p)=>({has:p<8,len:[32,16,32,64,16,48,32,16][p%8]}));S.patData={};
(()=>{const base=capturePat();for(let p=1;p<8;p++){const d=clone(base),r=(p*2)%d.len,tn=[0,5,7,-2,3,-5,10,12][p];d.len=S.patInfo[p].len;
 d.tr.forEach((x,i)=>{const s=x.steps.slice(0,32);x.steps=Array.from({length:64},(_,k)=>{const st=s[(k+r)%32];if(!st||!st.n||i===4)return st&&clone(st);return{...clone(st),n:st.n.map(n=>clamp(n+tn))}})});d.locks=[];S.patData[p]=d}})();
const curHas=()=>[...S.tracks,...S.midi].some(t=>t.steps.slice(0,S.len).some(Boolean));
const linked=k=>Array.from({length:128},(_,p)=>p).filter(p=>S.patKit[p]===k&&hasPat(p));
const kDisp=k=>k===S.kit?S.workName:S.kits[k].name;

/* firmware honesty: what each action is on the real machine */
const KTIP={
 load:"LOAD KIT (SysEx 0x58). Unsaved edits are discarded; the machine keeps them in its UNDO KIT (the first entry of its kit list). A kit loaded from the KIT menu becomes the current pattern's kit (1-12); whether a SysEx LOAD KIT relinks the pattern too is not tested",
 save:"SAVE KIT to the current slot (SysEx 0x59). The overwritten slot goes to the machine's UNDO KIT (1-20)",
 saveas:"SAVE KIT n to the selected slot (SysEx 0x59). On the MD, SAVE KIT n also makes n the current kit (P1); the editor assumes the same on the MM (not measured). The old slot keeps what it had",
 copy:"Reads the slot with a kit dump request (0x53). A request returns the stored slot, so the current kit's unsaved edits come from the editor's own copy",
 paste:"Kit dump (0x52) into the slot. The MM only takes a dump on GLOBAL › FILE › SYSEX RECV, so it goes through RECV (emulator) or SEND n (hardware). The emulator crosses two receive passes per kit dump. Pasting into the current kit then needs LOAD KIT (0x58) to be heard",
 clear:"There is no SysEx CLEAR KIT. The editor sends a kit dump with the empty kit: six GND-SIN machines and default pages, as the machine's own empty kit (1-19). Goes through SYSEX RECV",
 rename:"Current kit: SysEx 0x55 sets the working kit's name (11 ASCII bytes); SAVE stores it. Other slots: dump request, new name, dump back through SYSEX RECV. Which characters the MM font draws is not checked",
 reload:"LOAD KIT on the current slot (0x58): the edits are discarded. The machine keeps them in its UNDO KIT",
 drag:"Drag onto another slot to copy it: dump request, then a dump into the target (through SYSEX RECV)"};
const PTIP={
 go:"LOAD PATTERN (SysEx 0x57). While playing, the machine switches at the pattern end (1-45); the editor keeps the queue itself. The pattern also loads its kit (1-18)",
 now:"Switch now (Shift+click). Stopped: the same as a click. Playing: the machine only switches at the pattern end, so the editor would send STOP, LOAD PATTERN, PLAY (not tested)",
 copy:"Pattern dump request (0x68). Copies notes, locks, MIDI notes, arp / transpose / swing / slide and the kit link, like the machine's COPY PATTERN (1-53)",
 paste:"Pattern dump (0x67) into the slot. The MM only takes it on SYSEX RECV, so it goes through RECV (emulator) or SEND n (hardware). Unlike the MD, a pattern dump is not live",
 clear:"There is no SysEx CLEAR PATTERN. The editor sends an empty pattern dump through SYSEX RECV. The machine's CLEAR PATTERN keeps the kit link (1-54); so does this"};

function kitSlot(k){const cur=k===S.kit,st=S.kits[k],empty=st.empty&&!cur,name=kDisp(k),lp=linked(k),ed=cur&&S.kitState==="edited";
 const links=lp.length?lp.slice(0,3).map(patName).join(" ")+(lp.length>3?" +"+(lp.length-3):""):empty?"":"no pattern";
 const tip=`K${nn(k)} ${name||"(empty)"}${cur?" · current kit, "+(ed?"edited: not saved on the machine":"saved"):""} · ${lp.length?"linked to "+lp.map(patName).join(" "):"no pattern links to it (the machine marks it with a star)"}. ${cur?"":"Click loads it ("+KTIP.load+"); Alt+click only selects it. "}${KTIP.drag}`;
 const nm=LIB.renaming===k?`<input class="lsin" id="lsin" maxlength="${KNAME}" value="${escH(name)}" aria-label="Kit name, up to ${KNAME} characters" spellcheck="false" autocomplete="off">`:empty&&!name?"EMPTY":escH(name||"EMPTY")+(!lp.length&&!empty?"*":"");
 return`<button class="ls ks${empty?" empty":""}${cur?" cur":""}${ed?" edited":""}" data-ks="${k}" draggable="${LIB.renaming===k?"false":"true"}" aria-selected="${k===LIB.sel}" title="${escH(tip)}"><span class="lsh"><b>K${nn(k)}</b>${cur?`<em><i class="led${ed?" on":""}"></i>${ed?"ed":"ok"}</em>`:""}</span><span class="lsn">${nm}</span><span class="lsl">${links}</span></button>`}
function drawKitLib(){const k=LIB.sel,cur=S.kit,ed=S.kitState==="edited",empty=S.kits[k].empty&&k!==cur,dis=c=>c?" disabled":"";
 return`<div class="libhead"><span class="cap">Kit library</span><span class="lcdchip">K${nn(cur)} ${escH(S.workName||"EMPTY")} · ${ed?"edited":"saved"}</span><span class="note">128 slots. Every pattern recalls its kit. Writes go through SYSEX RECV.</span>${Syx.keys()}<button class="libx" data-la="close" title="Close (Esc)">Esc</button></div>
 <div class="libacts"><div class="grp"><span class="ilab">Current K${nn(cur)}</span><button data-la="save" title="${KTIP.save}">Save</button><button class="danger" data-la="reload"${dis(!ed)} title="${KTIP.reload}">Reload</button></div>
  <div class="grp"><span class="ilab">Slot K${nn(k)}</span><button data-la="load"${dis(k===cur&&!ed)} title="${k===cur?"Already the current kit. Enter reloads it when it is edited. ":""}${KTIP.load} (Enter)">Load</button><button data-la="saveas"${dis(k===cur)} title="${k===cur?"This is the current slot: use Save. ":""}${KTIP.saveas}">Save as K${nn(k)}</button>
  <button data-la="copy" title="${KTIP.copy} (Cmd+C)">Copy</button><button data-la="paste"${dis(LCLIP?.type!=="kit")} title="${LCLIP?.type==="kit"?"Paste K"+nn(LCLIP.from)+" "+escH(LCLIP.name)+". ":"Copy a kit first. "}${KTIP.paste} (Cmd+V)">Paste</button>
  <button class="danger" data-la="clear"${dis(empty)} title="${KTIP.clear} (Delete)">Clear</button><button data-la="rename" title="${KTIP.rename} (F2 or double-click)">Rename</button></div></div>
 <div class="libgrid kits" aria-label="128 kit slots">${Array.from({length:128},(_,i)=>kitSlot(i)).join("")}</div>
 <div class="libfoot"><span>Click loads · arrows move, Enter loads · Alt+click selects · F2 or double-click renames · Delete clears · drag a slot onto another to copy · Cmd+Z undoes · Esc closes</span><span class="fw" title="The machine keeps one UNDO KIT as the first entry of its kit list: the kit lost to the last load or overwrite (1-20). The editor's own undo works on top of it.">The machine also keeps an UNDO KIT</span></div>`}
function patSlot(p){const cur=p===S.pat,q=p===S.queued&&!cur,has=hasPat(p);
 const tip=`${patName(p)} · ${has?patLen(p)+" steps · kit "+kitName(S.patKit[p]):"empty"}${cur?" · current":""}${q?" · queued: starts at the pattern end":""}. Click queues, Shift+click switches now. Drag onto another slot to copy.`;
 return`<button class="ls ps${has?"":" empty"}${cur?" cur":""}${q?" q":""}" data-ps="${p}" draggable="${has}" aria-selected="${p===LIB.sel}" title="${escH(tip)}"><b>${patName(p)}</b><span>${has?patLen(p)+" · K"+nn(S.patKit[p]):"EMPTY"}</span></button>`}
function drawPatLib(){const p=LIB.sel,cur=S.pat,dis=c=>c?" disabled":"";
 const rows=[..."ABCDEFGH"].map((b,i)=>`<button class="bank lbank${i===cur>>4?" on":""}" data-lb="${i}" title="Bank ${b} (key ${b})"><i class="led"></i>${b}</button>`+Array.from({length:16},(_,j)=>patSlot(i*16+j)).join("")).join("");
 return`<div class="libhead"><span class="cap">Patterns</span><span class="lcdchip">${patName(cur)} · ${S.len} steps · K${nn(S.patKit[cur])}${S.queued!=null&&S.queued!==cur?" → "+patName(S.queued):""}</span><span class="note">8 banks × 16. ${S.playing?"Playing: a new pattern starts at the pattern end.":"Stopped: a click switches at once."} Factory presets sit in A-D, E-H start empty.</span>${Syx.keys()}<button class="libx" data-la="close" title="Close (Esc)">Esc</button></div>
 <div class="libacts"><div class="grp"><span class="ilab">Slot ${patName(p)}</span><button data-la="go"${dis(p===cur&&S.queued==null)} title="${PTIP.go} (Enter)">${S.playing?"Queue":"Go"}</button><button data-la="now"${dis(p===cur&&S.queued==null)} title="${PTIP.now}">Now</button>
  <button data-la="copy" title="${PTIP.copy} (Cmd+C)">Copy</button><button data-la="paste"${dis(LCLIP?.type!=="pat")} title="${LCLIP?.type==="pat"?"Paste "+patName(LCLIP.from)+". ":"Copy a pattern first. "}${PTIP.paste} (Cmd+V)">Paste</button><button class="danger" data-la="clear"${dis(!hasPat(p))} title="${PTIP.clear} (Delete)">Clear</button></div></div>
 <div class="libgrid pats" aria-label="128 patterns">${rows}</div>
 <div class="libfoot"><span>Arrows move · A–H jump to a bank · Enter queues · Shift+click or Now switches now · Delete clears · drag a slot onto another to copy · Esc closes</span><span class="fw" title="${PTIP.go}">Queued = blinking</span></div>`}
let LCLIP=null;
function libSig(){return[LIB.open,LIB.sel,LIB.renaming,S.pat,S.queued,S.kit,S.kitState,S.workName,S.playing,S.len,LCLIP?.type,LCLIP?.from,hasPat(S.pat),S.patKit.join(),S.kits.map(k=>k.name+(k.empty?0:1)).join("|"),S.patInfo.map(x=>+x.has+"."+x.len).join()].join("~")}
function drawLib(focus){if(!LIB.open||LIB.drag)return;const sig=libSig(),pop=$("#libpop"),had=focus||pop.contains(document.activeElement);
 if(sig===LIB.sig&&pop.firstChild){if(focus&&!pop.contains(document.activeElement))libFocus();return}LIB.sig=sig;
 pop.innerHTML=LIB.open==="kit"?drawKitLib():drawPatLib();pop.dataset.kind=LIB.open;pop.setAttribute("aria-label",LIB.open==="kit"?"Kit library":"Pattern chooser");
 const inp=$("#lsin");if(inp){inp.focus({preventScroll:true});inp.select();return}if(had)libFocus()}
function libFocus(){const pop=$("#libpop"),el=pop.querySelector(".ls[aria-selected=true]");if(!el)return;el.focus({preventScroll:true});
 if(pop.scrollHeight>pop.clientHeight){const a=el.getBoundingClientRect(),b=pop.getBoundingClientRect();if(a.top<b.top)pop.scrollTop-=b.top-a.top+8;else if(a.bottom>b.bottom)pop.scrollTop+=a.bottom-b.bottom+8}}
function placeLib(){const pop=$("#libpop"),r=$(".lcdpanel").getBoundingClientRect(),top=Math.max(16,r.bottom+8);pop.style.top=(top+scrollY)+"px";pop.style.maxHeight=Math.max(240,innerHeight-top-12)+"px";pop.style.left=Math.max(16,(document.documentElement.clientWidth-pop.offsetWidth)/2+scrollX)+"px"}
function openLib(kind){if(!engReady()){toast("The engine is not ready yet.");return}closePicker();closeK();if(LIB.open)closeLib(false);LIB.open=kind;LIB.sel=kind==="kit"?S.kit:(S.queued??S.pat);LIB.renaming=null;$("#libpop").hidden=false;drawLib();placeLib();libFocus();$(kind==="kit"?"#kitf":"#pat").setAttribute("aria-expanded","true")}
function closeLib(back){if(!LIB.open)return;const k=LIB.open;LIB.open=null;LIB.sig="";LIB.renaming=null;LIB.drag=null;$("#libpop").hidden=true;const t=$(k==="kit"?"#kitf":"#pat");t.setAttribute("aria-expanded","false");if(back)t.focus()}
function toggleLib(kind){LIB.open===kind?closeLib(false):openLib(kind)}
addEventListener("resize",()=>{if(LIB.open)placeLib()});
function relinkCur(k){S.patKit[S.pat]=k}

/* ----- kit actions (all editor state, so all undoable) ----- */
function kitLoad(k){if(HOST.kit)return HOST.kit("load",k);if(k===S.kit){if(S.kitState==="edited")kitReload();else toast(kitName(k)+" is already the current kit.");return}
 const go=()=>{S.kit=k;applyKit(kitData(k));S.workName=S.kits[k].name;relinkCur(k);setKitState("clean");tx();render();drawLib(true);toast("Loaded "+kitName(k)+". "+patName(S.pat)+" now uses it.")};
 if(S.kitState==="edited"){ask(`Load <b>${kitName(k)}</b>? Your edits to <b>${kitName(S.kit)}</b> are not saved on the machine. Without saving, they go to its UNDO KIT.`,[["Save and load","cream",()=>{saveKit();go()}],["Load without saving","danger",go],["Cancel","",()=>drawLib(true)]]);return}go()}
function kitSave(){saveKit();drawLib(true)}
function kitSaveAs(k){if(HOST.kit)return HOST.kit("saveAs",k);if(k===S.kit){kitSave();return}
 const go=()=>{const from=S.kit;S.kits[k]={name:S.workName,empty:false,data:captureKit()};S.kit=k;relinkCur(k);setKitState("clean");tx();render();drawLib(true);toast("Saved as "+kitName(k)+". It is now the current kit; K"+nn(from)+" keeps its saved version.")};
 if(!S.kits[k].empty){ask(`Overwrite <b>${kitName(k)}</b> with the current kit <b>${kitName(S.kit)}</b>? The machine keeps the overwritten kit in its UNDO KIT.`,[["Overwrite","danger",go],["Cancel","",()=>drawLib(true)]]);return}go()}
function kitSrc(k){return k===S.kit?{from:k,name:S.workName,empty:false,data:captureKit()}:{from:k,name:S.kits[k].name,empty:S.kits[k].empty,data:S.kits[k].data?clone(S.kits[k].data):null}}
function kitCopy(k){LCLIP={type:"kit",...kitSrc(k)};drawLib(true);toast("Copied "+kitName(k)+(k===S.kit&&S.kitState==="edited"?" with its unsaved edits.":"."))}
function kitPut(k,src,verb){if(src.from===k){toast("That is the same slot.");return}
 if(READING.kit.has(k)){toast(READ_NOTE);return}
 const go=()=>{S.kits[k]={name:src.name,empty:src.empty,data:src.data?clone(src.data):src.empty?null:kitData(src.from)};if(k===S.kit){applyKit(kitData(k));S.workName=S.kits[k].name;setKitState("clean")}if(HOST.slotWritten)HOST.slotWritten("kit",k);structEdited();render();drawLib(true);toast(verb+" K"+nn(src.from)+" into "+kitName(k)+" (kit dump through SYSEX RECV).")};
 const busy=!S.kits[k].empty||k===S.kit;if(busy){ask(`${verb} <b>K${nn(src.from)} ${escH(src.name||"EMPTY")}</b> over <b>${kitName(k)}</b>?${k===S.kit?" It is the current kit"+(S.kitState==="edited"?": its unsaved edits are lost.":", so it is loaded too."):""}`,[["Overwrite","danger",go],["Cancel","",()=>drawLib(true)]]);return}go()}
function kitPaste(k){if(LCLIP?.type!=="kit"){toast("Copy a kit first.");return}kitPut(k,LCLIP,"Paste")}
function kitClear(k){if(S.kits[k].empty&&k!==S.kit){toast(kitName(k)+" is already empty.");return}
 if(READING.kit.has(k)){toast(READ_NOTE);return}
 ask(`Clear <b>${kitName(k)}</b>?${k===S.kit?" It is the current kit: all six tracks go back to GND-SIN"+(S.kitState==="edited"?" and the unsaved edits are lost.":"."):""}${linked(k).length?" "+linked(k).length+" pattern(s) link to it.":""}`,[["Clear kit","danger",()=>{S.kits[k]={name:"",empty:true,data:null};if(k===S.kit){applyKit(clearedKit());S.workName="";setKitState("clean")}if(HOST.slotWritten)HOST.slotWritten("kit",k);structEdited();render();drawLib(true);toast("Cleared K"+nn(k)+".")}],["Cancel","",()=>drawLib(true)]])}
function kitReload(){if(HOST.kit)return HOST.kit("reload",S.kit);if(S.kitState!=="edited"){toast(kitName(S.kit)+" matches its saved slot. Nothing to reload.");return}
 ask(`Reload <b>${kitName(S.kit)}</b> from the machine? Your edits go to its UNDO KIT.`,[["Reload (discard edits)","danger",()=>{applyKit(kitData(S.kit));S.workName=S.kits[S.kit].name;setKitState("clean");tx();render();drawLib(true);toast("Reloaded "+kitName(S.kit)+" from the machine.")}],["Cancel","",()=>drawLib(true)]])}
function startRename(k){if(LIB.open!=="kit")return;LIB.sel=k;LIB.renaming=k;drawLib(true)}
function finishRename(ok){const k=LIB.renaming;if(k==null)return;const v=($("#lsin")?.value||"").toUpperCase().replace(KBAD,"").slice(0,KNAME).trimEnd();LIB.renaming=null;
 if(ok&&v!==kDisp(k)){if(k===S.kit){S.workName=v;setKitState("edited");tx();toast("Renamed the working kit (SysEx 0x55). SAVE stores the name on the machine.")}
  else if(READING.kit.has(k)){toast(READ_NOTE)}
  else{const s=S.kits[k];if(s.empty&&!s.data)s.data=clearedKit();s.empty=false;s.name=v;if(HOST.slotWritten)HOST.slotWritten("kit",k);structEdited();toast("Renamed K"+nn(k)+" (kit dump through SYSEX RECV).")}renderTop()}
 drawLib(true)}
/* ----- pattern actions ----- */
function patSrc(p){return{from:p,has:hasPat(p),kit:S.patKit[p],data:p===S.pat?capturePat():clone(S.patData[p]||emptyPat(S.patInfo[p].len))}}
function patCopy(p){LCLIP={type:"pat",...patSrc(p)};drawLib(true);toast("Copied "+patName(p)+": notes, locks, arp, transpose, swing, slide and its kit link.")}
function patPut(p,src,verb){if(src.from===p){toast("That is the same slot.");return}
 if(READING.pattern.has(p)){toast(READ_NOTE);return}
 const go=()=>{const d=clone(src.data);S.patData[p]=d;S.patInfo[p]={has:src.has,len:d.len};S.patKit[p]=src.kit;if(p===S.pat)applyPat(d);if(HOST.slotWritten)HOST.slotWritten("pattern",p);structEdited();render();drawLib(true);toast(verb+" "+patName(src.from)+" into "+patName(p)+" (pattern dump through SYSEX RECV).")};
 if(hasPat(p)){ask(`${verb} <b>${patName(src.from)}</b> over <b>${patName(p)}</b>? Its notes and locks are replaced.`,[["Overwrite","danger",go],["Cancel","",()=>drawLib(true)]]);return}go()}
function patPaste(p){if(LCLIP?.type!=="pat"){toast("Copy a pattern first.");return}patPut(p,LCLIP,"Paste")}
function patClear(p){if(!hasPat(p)){toast(patName(p)+" is already empty.");return}
 if(READING.pattern.has(p)){toast(READ_NOTE);return}
 ask(`Clear <b>${patName(p)}</b>? Its notes and locks are removed. It keeps its kit link, as on the machine.`,[["Clear pattern","danger",()=>{const e=emptyPat(patLen(p));S.patData[p]=e;S.patInfo[p]={has:false,len:e.len};if(p===S.pat)applyPat(e);if(HOST.slotWritten)HOST.slotWritten("pattern",p);structEdited();render();drawLib(true);toast("Cleared "+patName(p)+".")}],["Cancel","",()=>drawLib(true)]])}
function patGo(p,now){LIB.sel=p;if(p===S.pat&&S.queued==null){drawLib(true);return}goPattern(p,now);drawLib(true)}
function libAct(a){const k=LIB.sel,kit=LIB.open==="kit";
 ({close:()=>closeLib(true),load:()=>kitLoad(k),save:kitSave,saveas:()=>kitSaveAs(k),reload:kitReload,rename:()=>startRename(k),
   copy:()=>kit?kitCopy(k):patCopy(k),paste:()=>kit?kitPaste(k):patPaste(k),clear:()=>kit?kitClear(k):patClear(k),go:()=>patGo(k,false),now:()=>patGo(k,true)})[a]?.()}

/* ----- mouse ----- */
document.addEventListener("click",e=>{if(!LIB.open)return;if($("#libpop").contains(e.target)||e.target.closest?.("#dlg,#kitf,#pat,#undo,#redo"))return;closeLib(false)},true);
document.addEventListener("click",e=>{if(!LIB.open||!$("#libpop").contains(e.target)||e.target.closest?.("#lsin"))return;
 const a=e.target.closest("[data-la]");if(a){if(!a.disabled)libAct(a.dataset.la);return}
 /* a click loads the slot at once (LOAD KIT; over unsaved edits the machine asks first: Save and load, Load
    without saving, Cancel); the current kit is only selected (Reload, or Enter, reloads it); Alt+click selects
    without loading (MM-PORT-PLAN f, as the Machinedrum Editor) */
 const ks=e.target.closest("[data-ks]");if(ks){const k=+ks.dataset.ks;if(LIB.renaming!=null)finishRename(true);LIB.sel=k;drawLib(true);
  if(k!==S.kit&&!e.altKey&&!e.metaKey&&!e.ctrlKey&&e.detail<2)kitLoad(k);return}
 const ps=e.target.closest("[data-ps]");if(ps){patGo(+ps.dataset.ps,e.shiftKey);return}
 const lb=e.target.closest("[data-lb]");if(lb){LIB.sel=+lb.dataset.lb*16+(LIB.sel&15);drawLib(true)}});
/* the first click has loaded the slot: double-click renames it (unless the machine is asking) */
document.addEventListener("dblclick",e=>{const ks=e.target.closest("#libpop [data-ks]");if(ks&&!e.target.closest("#lsin")&&$("#dlg").hidden)startRename(+ks.dataset.ks)});
document.addEventListener("input",e=>{if(e.target.id!=="lsin")return;const i=e.target,c=i.selectionStart,v=i.value.toUpperCase().replace(KBAD,"").slice(0,KNAME);if(v!==i.value){i.value=v;i.setSelectionRange(Math.min(c,v.length),Math.min(c,v.length))}});
document.addEventListener("focusout",e=>{if(e.target.id==="lsin"&&LIB.renaming!=null)setTimeout(()=>{if(document.activeElement?.id!=="lsin")finishRename(true)},0)});
document.addEventListener("dragstart",e=>{const s=e.target.closest?.("#libpop .ls[data-ks],#libpop .ls[data-ps]");if(!s)return;LIB.drag={kit:"ks" in s.dataset,v:+(s.dataset.ks??s.dataset.ps)};s.classList.add("dragging");e.dataTransfer.effectAllowed="copy";try{e.dataTransfer.setData("text/plain",String(LIB.drag.v))}catch(_){}});
const libTarget=el=>el.closest?.(LIB.drag?.kit?"#libpop [data-ks]":"#libpop [data-ps]");
document.addEventListener("dragover",e=>{if(!LIB.drag)return;const t=libTarget(e.target);$$("#libpop .ls.over").forEach(x=>x!==t&&x.classList.remove("over"));if(!t)return;e.preventDefault();e.dataTransfer.dropEffect="copy";t.classList.add("over")});
document.addEventListener("drop",e=>{if(!LIB.drag)return;const t=libTarget(e.target),d=LIB.drag;LIB.drag=null;if(!t)return;e.preventDefault();const to=+(t.dataset.ks??t.dataset.ps);LIB.sel=to;
 if(d.kit)kitPut(to,kitSrc(d.v),"Copy");else patPut(to,patSrc(d.v),"Copy");drawLib(true)});
document.addEventListener("dragend",()=>{if(!LIB.drag&&!$$("#libpop .dragging").length)return;LIB.drag=null;drawLib(true)});
/* ----- keyboard (capture, so the panel owns its keys while it is open) ----- */
[["Enter / Space on KIT or the pattern","","Open the kit library / pattern chooser"],["Arrows","","Move (kits: without loading)"],["Enter","","Kits: load (a click loads too; Alt+click only selects). Patterns: queue (⇧-click or Now: at once)"],
 ["A–H","","Patterns: jump to a bank"],["F2","","Kits: rename (or double-click)"],["Delete","","Clear the slot"],
 ["C / V","cmd","Copy / paste the slot"],["Z","cmd","Undo a paste, clear or rename"],["Escape","","Close"]]
 .forEach(([k,m,d])=>Keys.bind({keys:[k],mod:m,group:"Kit library, pattern chooser",does:d}));
document.addEventListener("keydown",e=>{
 if(!LIB.open){if((e.key==="Enter"||e.key===" ")&&(e.target.id==="kitf"||e.target.id==="pat")){e.preventDefault();e.stopImmediatePropagation();openLib(e.target.id==="kitf"?"kit":"pat")}return}
 if(!$("#dlg").hidden)return;
 if(e.target.id==="lsin"){if(e.key==="Enter"||e.key==="Escape"){e.preventDefault();finishRename(e.key==="Enter")}e.stopImmediatePropagation();return}
 const mod=e.metaKey||e.ctrlKey,kit=LIB.open==="kit",n=128,cols=16,key=e.key;let h=true;
 if(key==="Escape")closeLib(true);
 else if(!mod&&!e.altKey&&/^Arrow/.test(key)){LIB.sel=(LIB.sel+{ArrowLeft:-1,ArrowRight:1,ArrowUp:-cols,ArrowDown:cols}[key]+n)%n;drawLib(true)}
 else if(key==="Enter"){if(kit)kitLoad(LIB.sel);else patGo(LIB.sel,false)}
 else if(key==="F2"&&kit)startRename(LIB.sel);
 else if(key==="Delete"||key==="Backspace")(kit?kitClear:patClear)(LIB.sel);
 else if(mod&&(key==="c"||key==="C"))(kit?kitCopy:patCopy)(LIB.sel);
 else if(mod&&(key==="v"||key==="V"))(kit?kitPaste:patPaste)(LIB.sel);
 else if(mod&&(key==="z"||key==="Z")){e.shiftKey?redo():undo();drawLib(true)}
 else if(!kit&&!mod&&!e.altKey&&/^[a-h]$/i.test(key)){LIB.sel="abcdefgh".indexOf(key.toLowerCase())*16+(LIB.sel&15);drawLib(true)}
 else h=false;
 if(h){e.preventDefault();e.stopImmediatePropagation()}},true);
