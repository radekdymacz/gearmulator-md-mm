/* ===== The demo host (DESIGN-UNIFY.md 4.6): this mockup on its own plays its edits as the plug-in does =====
   Without a host (no window.MMHost) every edit is still the machine's intent: the demo host keeps a view of the
   example's contract documents (demo-docs.json, through MmView.derive), folds each intent's writes into it (MmView.toFw,
   MmView.copied, MmView.writes: the functions the plug-in's page shows an edit with at once) and shows it (MMView.show),
   as if every command were answered at once. So what an intent's writes miss is lost on the screen here too. Its undo is
   a snapshot of that view, one per gesture (commit), with the example library's slots. The example engine's own pattern
   and kit switches and its library change the state shown outside it: rebase() takes that state as its view again (a
   switch is no undo step, a library edit is one). Loaded in the plug-in too, where it does nothing. */
window.MMDemoHost=window.MMHost?null:(()=>{
 let base=null,clip=null,rev=0;const U={undo:[],redo:[],last:null};
 const KINDS=[{kind:"pattern",at:"patterns"},{kind:"kit",at:"kits"},{kind:"song",at:"songs"},{kind:"global",at:"globals"},{kind:"workingKit",working:"workingKit"}];
 const lib=()=>JSON.stringify({kits:S.kits,patData:S.patData,patInfo:S.patInfo,patKit:S.patKit},(k,v)=>v===Infinity?"∞":v);
 const entry=()=>({base:structuredClone(base),lib:lib(),rev});
 /* the state shown, as a view (the members MMView.show writes) */
 function viewOf(){const F=(t,f)=>Object.fromEntries(f.map(n=>[n,copyV(t[n])]));
  return{ready:true,pat:S.pat,kit:S.kit,songSlot:S.songSlot??0,queued:S.queued,kitState:S.kitState,bpm:S.bpm,workName:S.workName,routing:S.routing,
   tracks:S.tracks.map(t=>F(t,[...KIT_F,...SEQ_F])),midi:S.midi.map(t=>F(t,[...MIDI_KIT_F,...SEQ_F])),locks:copyV(S.locks),len:S.len,mult:S.mult,swingAmt:S.swingAmt,
   patTrn:S.patTrn,multi:copyV(S.multi),menv:copyV(S.menv),song:copyV(S.song),mmap:copyV(S.mmap),plays:copyV(S.plays),glob:copyV(S.glob),
   patLens:Object.fromEntries(S.patInfo.map((x,p)=>[p,p===S.pat?S.len:x.len]))}}
 function put(e){base=structuredClone(e.base);rev=e.rev;const o=JSON.parse(e.lib,(k,v)=>v==="∞"?Infinity:v);Object.assign(S,o);show(base,true);drawLib()}
 const host={
  start(){const go=()=>{if(typeof MmView==="undefined"||typeof MmConvert==="undefined"){setTimeout(go,0);return}
   const d=window.MM_DEMO;
   if(d){MmConvert.useCatalogue(d.catalogue);const docs={patterns:{},kits:{},songs:{},globals:{},workingKit:null,sources:{},machine:d.machine},store=docStore(KINDS);
    for(const m of d.docs)storeDoc(docs,m,store);base=structuredClone(MmView.derive(docs));delete base.songs;show(base,true)}
   else base=viewOf();
   U.last=entry();startEngine("emu")};go()},
  /* an intent: its writes folded into the view, shown; a kit edit leaves the kit edited, a pattern, song or global
     edit is a dump the example's RECV takes */
  intent(op,args){if(!base)return;const kind=MmView.kindOf(op),c=MmView.toFw(Object.assign({op},structuredClone(args)),base);
   if(kind==="kit")c.k=base.kit;else if(kind==="song")c.s=base.songSlot;else if(kind==="pattern")c.p=base.pat;
   clip=MmView.copied(base,c)||clip;const w=MmView.writes(base,c,clip);if(!w.length)return;	/* a copy: the clipboard only */
   base=structuredClone(base);Overlay.add(-1,w);Overlay.over(base);Overlay.answered(-1);rev++;
   if(kind==="kit")base.kitState="edited";else exampleDump();
   show(base)},
  /* the gesture ended: one undo step when it changed something */
  commit(){if(!base)return;const l=lib();if(U.last&&U.last.rev===rev&&U.last.lib===l)return;if(U.last){U.undo.push(U.last);if(U.undo.length>200)U.undo.shift();U.redo=[]}U.last=entry();renderTop()},
  undo(){if(!U.undo.length){toast("Nothing to undo.");return}U.redo.push(entry());U.last=U.undo.pop();put(U.last);tx();toast("Undo")},
  redo(){if(!U.redo.length){toast("Nothing to redo.");return}U.undo.push(entry());U.last=U.redo.pop();put(U.last);tx();toast("Redo")},
  history(){return{undo:U.undo.length,redo:U.redo.length}},
  /* the example engine changed the state shown (a pattern or kit switch: step false; its library: true, an undo step) */
  rebase(step){if(!base)return;base=viewOf();SHOWN=base;rev++;if(!step)U.last=entry()}};
 return host})();
/* the example engine's own changes to the state shown (no intent): the demo host's view follows them */
function demoRebase(step){if(window.MMDemoHost)window.MMDemoHost.rebase(step)}
