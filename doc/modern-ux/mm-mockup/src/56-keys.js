/* ===== Keys (from the Machinedrum Editor, P5 and its mnemonic map, DESIGN-generators.md §5) =====
   Every shortcut is an entry of Keys: the ? overlay is generated from it, so it lists what the keys really do.
   An entry with run() is dispatched by the one handler (in order, the first match wins); one without run() is
   handled next to its own code and only described. The dispatcher (Keys) is skins/shared/deskKeys.js, both
   editors' one file, included just before this one (build.sh). */

/* ===== The ? keyboard view =====
   The map's rules, as the MD Editor's: plain keys play (the home row, Z / X octave, C / V velocity, Space); a plain
   letter off the piano row acts on the selected track (M, T; ↑ / ↓ pick it); Alt means all (Alt+M, Alt+Delete,
   Alt-drag, Alt-click). Two Alts are not "all", as on the machine: Alt+←/→ rotate (FUNCTION + arrows) and Alt+Space
   record (RECORD + PLAY). No ⇧ or ⌘ letter commands but the standard ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V. */
const KEY_GROUPS=["Playing","Selected track","All","Transport","Workspaces","Sequence","Song","Values","Anywhere","Kit library, pattern chooser","Help"];
/* B-018: ? opens the keyboard view both editors share (skins/shared/deskKeyView.js, DESIGN-keymap.md K-view): a drawn
   keyboard from this map with what each key does on the page shown (or every page), its layers, the mouse's tricks, the
   tips, and every entry by group (the list ? showed before). */
const WS_NAMES={seq:"Sequence",sound:"Sound",mix:"Mix",perform:"Perform",song:"Song",control:"Control"};
let keyView=null;
function drawKeys(){const pop=$("#keyspop");if(!pop)return;
 if(!keyView)keyView=KeyView.mount(pop,{entries:()=>Keys.list(),page:()=>S.ws,pageName:()=>WS_NAMES[S.ws]||"This page",mapping:()=>S.mapping,
  mac:()=>Modifiers.mac,fn:()=>Modifiers.fn!=="off",groups:KEY_GROUPS,note:()=>Modifiers.say("? or Esc closes. Click a key for what it does.")});
 pop.classList.add("kview");keyView.draw();pop.hidden=false;
 const r=$(".lcdpanel").getBoundingClientRect(),top=Math.max(16,r.bottom+8);
 pop.style.top=(top+scrollY)+"px";pop.style.maxHeight=Math.max(240,innerHeight-top-12)+"px";pop.style.left=Math.max(16,(document.documentElement.clientWidth-pop.offsetWidth)/2+scrollX)+"px"}
function toggleKeys(on){const pop=$("#keyspop");if(!pop)return;if(on??pop.hidden)drawKeys();else{pop.hidden=true;keyView?.reset()}}
Keys.bind({id:"keys-help",short:"This view",scope:"any",keys:["?"],group:"Help",does:"The keyboard view: what every key and gesture does (this)",modal:"keyspop",run:()=>toggleKeys()});
Keys.bind({id:"keys-help-close",short:"Close",scope:"any",keys:["Escape"],group:"Help",does:"Close the keyboard view",when:()=>!$("#keyspop").hidden,run:()=>toggleKeys(false)});
document.addEventListener("click",e=>{const pop=$("#keyspop");if(!pop||pop.hidden)return;if(e.target.closest("[data-keysx]")||!pop.contains(e.target)){pop.hidden=true;keyView?.reset()}},true);
