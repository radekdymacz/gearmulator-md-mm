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
  mac:()=>Modifiers.mac,groups:KEY_GROUPS,note:()=>Modifiers.say("? or Esc closes. Click a key for what it does.")});
 pop.classList.add("kview");keyView.draw();pop.hidden=false;
 const r=$(".lcdpanel").getBoundingClientRect(),top=Math.max(16,r.bottom+8);
 pop.style.top=(top+scrollY)+"px";pop.style.maxHeight=Math.max(240,innerHeight-top-12)+"px";pop.style.left=Math.max(16,(document.documentElement.clientWidth-pop.offsetWidth)/2+scrollX)+"px"}
function toggleKeys(on){const pop=$("#keyspop");if(!pop)return;if(on??pop.hidden)drawKeys();else{pop.hidden=true;keyView?.reset()}}
Keys.bind({id:"keys-help",short:"This view",scope:"any",keys:["?"],group:"Help",does:"The keyboard view: what every key and gesture does (this)",modal:"keyspop",run:()=>toggleKeys()});
Keys.bind({id:"keys-help-close",short:"Close",scope:"any",keys:["Escape"],group:"Help",does:"Close the keyboard view",when:()=>!$("#keyspop").hidden,run:()=>toggleKeys(false)});
document.addEventListener("click",e=>{const pop=$("#keyspop");if(!pop||pop.hidden)return;if(e.target.closest("[data-keysx]")||!pop.contains(e.target)){pop.hidden=true;keyView?.reset()}},true);
/* the page zoom keys (skins/shared/deskZoom.js takes them before the map), described, as the MD Editor's */
Keys.bind({id:"zoom-out",short:"Zoom −",scope:"any",keys:["-"],mod:"cmd",group:"Anywhere",does:"Page zoom: smaller (also in the editor's menu)"});
Keys.bind({id:"zoom-in",short:"Zoom +",scope:"any",keys:["="],mod:"cmd",group:"Anywhere",does:"Page zoom: larger (⌘+ too)"});
Keys.bind({id:"zoom-reset",short:"Zoom 100 %",scope:"any",keys:["0"],mod:"cmd",group:"Anywhere",does:"Page zoom: back to 100 %"});
/* tips: the view's short list (a tip is an entry without keys); the MD Editor's ids where the tip is the same idea */
[
 ["tip-step-menu","Right-click a note in the piano roll or a step: everything a step can do, with its key (Ctrl-click on a Mac)."],
 ["tip-select","⌘-click or ⌘-drag steps to select them; the LCD's Copy, Clr and Paste then act on the selection."],
 ["tip-draw","B on Sequence turns the piano roll's Draw on and off (off: drag a box round notes to select them). ⇧B taps the tempo on every workspace."],
 ["tip-undo","A drag, a paint or a run of rotates is one undo step: ⌘Z takes it back whole."],
 ["tip-play","A to L play the selected track from any workspace, W E T Y U O P its black keys; Z / X move the octave, C / V the velocity."],
 ["tip-reset","Double-click a value: back to its default. ⇧ while dragging: fine."],
 ["tip-reclock","While LIVE RECORDING plays, a value you move is locked on the step that plays when it arrives; the step is marked until the machine's pattern shows it."]
].forEach(([id,does])=>Keys.bind({id,scope:"any",tip:true,keys:[],group:"Tips",does}));
/* the pointer's gestures that no key shares, described (K0), as the MD Editor's where the MM has them; each is handled next
   to its own code; here only so the keyboard view, the ? list and the guide list them */
[
 ["value-drag","sound mix perform control","Values",["value"],"","Values","Drag up / down or sideways: change it, a value a pixel"],
 ["value-fine","sound mix perform control","Values",["value"],"shift","Values","Drag: fine, a quarter as fast"],
 ["value-wheel","sound mix perform control","Values",["wheel on a value"],"","Values","One step a notch (⇧: 10)"],
 ["value-reset","sound mix perform control","Values",["double-click a value"],"","Values","Back to its default (64; a list's first entry)"],
 ["tempo-drag","any","Top bar",["tempo"],"","Transport","Drag up or down: the tempo, half a BPM a pixel (⇧: a tenth)"],
 ["lcd-value","any","LCD",["LCD value"],"","Anywhere","Click: its next value; ⇧-click the previous one; the wheel steps through them. SWING and PTRN: drag up or down"],
 ["header-menu","any","Anywhere",["right-click"],"","Anywhere","The editor's menu: zoom and window size, updates, the log folder, Developer"],
 ["gen-value","seq","GEN bar",["GEN value"],"","Sequence","Click: +1; ⇧-click: −1; drag up or down, or the wheel (⇧: 10)"],
 ["rkey","any","GEN bar",["R key"],"","Selected track","Click: randomise the selected track, as R (⌥-click: every track)"],
 ["ms-click","any","Tracks",["M / S key"],"","Selected track","Click: mute / solo that track"],
 ["song-steppers","song","Song",["row value − / +"],"shift","Song","Click: ten at a time"],
 ["song-drag","song","Song",["drag a pattern pad or a row"],"","Song","Onto the arrangement: a new row there, or the row moved"],
 ["lib-slot-click","library","Library",["kit slot"],"","Kit library, pattern chooser","Click: load it (⌥-click: only select it). Double-click: rename"],
 ["lib-pattern-now","library","Library",["pattern slot"],"shift","Kit library, pattern chooser","Click: go there at once, not at the end of the pattern"],
 ["lib-slot-drag","library","Library",["drag a slot onto another"],"","Kit library, pattern chooser","Copy it there"]
].forEach(([id,scope,area,keys,mod,group,does])=>Keys.bind({id,scope,area,keys,mod,group,does}));
