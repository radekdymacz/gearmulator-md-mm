/* ===== GLOBAL: the machine's own settings (B-051, ROADMAP F3) =====
   The Machinedrum Editor's GLOBAL panel, one implementation for both editors (skins/shared/deskGlobal.js: the slots,
   Reset to defaults, the footer, the controls); this is the Monomachine's host of it. S.glob is the active global
   as the page shows it (MmView's globPage: a channel 0-15, null OFF); a change is the globalMidi intent (the routing
   mode: routing), stored in the active slot and made active; a slot key is HOST.globalSlot (SET ACTIVE GLOBAL).
   Measured on OS 1.32B (mmDeskFirmwareTest spanprobe, globalprobe): track t listens on base + t while t < CHANNEL SPAN
   and base + t is channel 15 or lower (16 reaches no track); CONTROL OUT's clock, Start/Stop and program change are
   three bytes of the global; a program change is always taken in on the base channel. The MULTI MAP (the MD's map
   editor's counterpart) is on Perform. */
const GCH=c=>c==null?"OFF":String(c+1);
const gTrackCh=(g,t)=>g&&g.base!=null&&t<g.span&&g.base+t<=14?g.base+t:null;
const MM_GTIP={
 base:"BASE CHANNEL: track t listens on base + t, for its sound values, mute and notes, while t is inside the CHANNEL SPAN (channel 16 reaches no track). OFF: no track takes any over MIDI",
 span:"CHANNEL SPAN: how many tracks, from T1, have a MIDI channel of their own (6: all of them). 0: none, as in some old backups",
 auto:"AUTO TRACK: notes on this channel play the selected track",
 multiTrig:"MULTI TRIG: notes on this channel play the tracks as the kit's MULTI TRIG says (Perform)",
 multiMap:"MULTI MAP: notes on this channel start patterns as the MULTI MAP says (Perform)",
 pcIn:"Measured: the machine always takes a program change on its base channel (it selects a pattern); no setting turns it off",
 pcOut:"Picking a pattern sends a program change on the base channel",
 tempoIn:"INTERNAL: its own tempo. EXTERNAL: follows MIDI clock. In a DAW the plug-in sets EXT itself (it follows the host)",
 ctrlIn:"React to MIDI Start, Stop and Continue. In a DAW the plug-in turns it on itself",
 tempoOut:"Send MIDI clock",ctrlOut:"Send MIDI Start and Stop",
 routing:"How the six tracks reach the outputs (also on Mix)"};
/* "No track has …", "T3 has …", "T4, T5 and T6 have …" */
function gNoChannel(none){if(!none.length)return"";const tail=one=>`: mutes and notes cannot reach ${one?"it":"them"}, and the editor sends ${one?"its":"their"} sound values as kit dumps.`;
 if(none.length===6)return"No track has a MIDI channel of its own"+tail(false);
 const names=none.map(t=>"T"+(t+1));
 return none.length===1?names[0]+" has no MIDI channel of its own"+tail(true):names.slice(0,-1).join(", ")+" and "+names[names.length-1]+" have no MIDI channel of their own"+tail(false)}
function mmGlobalCards(G){const{tog,step,row}=GlobalUi;
 const none=[0,1,2,3,4,5].filter(t=>gTrackCh(G,t)==null),last=G.base==null?null:Math.min(G.base+G.span,15);
 const baseLabel=G.base==null?"OFF":G.span&&last>G.base?`${G.base+1}–${last}`:String(G.base+1);
 const warn=gNoChannel(none);
 return`
  <section class="card"><header><h3>Control</h3><span>MIDI</span></header>
   ${row("Base channel",step("base",baseLabel,MM_GTIP.base))}
   ${row("Channel span",step("span",String(G.span),MM_GTIP.span))}
   ${warn?`<p class="gwarn">${warn}</p>`:""}
   ${row("Auto track",step("auto",GCH(G.auto),MM_GTIP.auto))}
   ${row("Multi trig",step("multiTrig",GCH(G.multiTrig),MM_GTIP.multiTrig))}
   ${row("Multi map",step("multiMap",GCH(G.multiMap),MM_GTIP.multiMap))}
   ${row("Prg change in",`<b class="mono" title="${MM_GTIP.pcIn}">BASE</b>`)}
   ${row("Prg change out",tog("pcOut",G.programChangeOut,MM_GTIP.pcOut))}</section>
  <section class="card"><header><h3>Sync</h3><span>clock and transport</span></header>
   ${row("Tempo in",tog("tempoIn",G.clockIn,MM_GTIP.tempoIn,"EXT","INT"))}
   ${row("Ctrl in",tog("ctrlIn",G.transportIn,MM_GTIP.ctrlIn))}
   ${row("Tempo out",tog("tempoOut",G.clockOut,MM_GTIP.tempoOut))}
   ${row("Ctrl out",tog("ctrlOut",G.transportOut,MM_GTIP.ctrlOut))}</section>
  <section class="card"><header><h3>Routing</h3><span>the six tracks to the outputs</span></header>
   ${row("Mode",`<span class="seg" title="${MM_GTIP.routing}">${ROUTES.map(r=>`<button data-ga="routing" data-r="${r}" aria-pressed="${S.routing===r}">${r.replace("+"," + ")}</button>`).join("")}</span>`)}</section>`}
/* a channel stepper: OFF, 1 … 16 */
const gStep=(c,d)=>{const i=Math.max(0,Math.min(16,(c==null?0:c+1)+d));return i===0?null:i-1};
function mmGlobalClick(a,G){const f=a.dataset.ga,d=a.dataset.d!=null?+a.dataset.d:0,v=a.dataset.v!=null?+a.dataset.v:null;
 if(f==="routing"){if(S.routing!==a.dataset.r){S.routing=a.dataset.r;edit("routing",{v:S.routing});render();drawGlobal()}return}
 const toggles={pcOut:"programChangeOut",tempoIn:"clockIn",ctrlIn:"transportIn",tempoOut:"clockOut",ctrlOut:"transportOut"};
 let args=null;
 if(f==="span")args={span:Math.max(0,Math.min(16,G.span+d))};
 else if(["base","auto","multiTrig","multiMap"].includes(f))args={[f]:gStep(G[f],d)};
 else if(toggles[f])args={[toggles[f]]:v===1};
 if(!args||Object.entries(args).every(([k,x])=>G[k]===x))return;
 Object.assign(G,args);edit("globalMidi",args);drawGlobal()}
DeskGlobal.host={view:()=>S.glob||null,cards:mmGlobalCards,click:mmGlobalClick,menu:"FUNCTION + KIT/SONG",
 slot:n=>{if(HOST.globalSlot)return HOST.globalSlot(n);S.glob.slot=n;drawGlobal()},
 reset:()=>edit("globalReset",{}),
 ask:(html,yes)=>ask(html,[["Reset","danger",yes],["Cancel","",()=>{}]])};
