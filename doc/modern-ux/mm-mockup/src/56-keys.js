/* ===== Keys (from the Machinedrum Editor, P5 and its mnemonic map, DESIGN-generators.md §5) =====
   Every shortcut is an entry of Keys: the ? overlay is generated from it, so it lists what the keys really do.
   An entry with run() is dispatched by the one handler (in order, the first match wins); one without run() is
   handled next to its own code and only described. The dispatcher (Keys) is skins/shared/deskKeys.js, both
   editors' one file, included just before this one (build.sh). */

/* ===== The ? overlay =====
   The map's rules, as the MD Editor's: plain keys play (the home row, Z / X octave, C / V velocity, Space); a plain
   letter off the piano row acts on the selected track (M, T; ↑ / ↓ pick it); Alt means all (Alt+M, Alt+Delete,
   Alt-drag, Alt-click). Two Alts are not "all", as on the machine: Alt+←/→ rotate (FUNCTION + arrows) and Alt+Space
   record (RECORD + PLAY). No ⇧ or ⌘ letter commands but the standard ⌘Z ⌘⇧Z ⌘Y ⌘C ⌘V. */
const KEY_GROUPS=["Playing","Selected track","All","Transport","Workspaces","Sequence","Song","Values","Anywhere","Kit library, pattern chooser","Help"];
function drawKeys(){const pop=$("#keyspop");if(!pop)return;const groups=[];
 for(const b of Keys.list()){if(b.hidden||(b.mapping&&!S.mapping))continue;let g=groups.find(x=>x.name===b.group);if(!g)groups.push(g={name:b.group,rows:[]});g.rows.push(b)}
 const at=g=>{const i=KEY_GROUPS.indexOf(g.name);return i<0?KEY_GROUPS.length:i};groups.sort((x,y)=>at(x)-at(y));
 pop.innerHTML=`<div class="libhead"><span class="cap">Keys</span><span class="note">Every shortcut of the editor, from its key map. ? or Esc closes.</span><button class="libx" data-keysx="1">Esc</button></div>
 <div class="keysgrid">${groups.map(g=>`<section class="kgrp2"><h3>${g.name}</h3><div class="krows">${g.rows.map(b=>`<div class="krow"><kbd>${Keys.label(b)}</kbd><span>${typeof b.does==="function"?b.does():b.does}</span></div>`).join("")}</div></section>`).join("")}</div>`;
 pop.hidden=false;const r=$(".lcdpanel").getBoundingClientRect(),top=Math.max(16,r.bottom+8);
 pop.style.top=(top+scrollY)+"px";pop.style.maxHeight=Math.max(240,innerHeight-top-12)+"px";pop.style.left=Math.max(16,(document.documentElement.clientWidth-pop.offsetWidth)/2+scrollX)+"px"}
function toggleKeys(on){const pop=$("#keyspop");if(!pop)return;if(on??pop.hidden)drawKeys();else pop.hidden=true}
Keys.bind({keys:["?"],group:"Help",does:"This list of keys",modal:"keyspop",run:()=>toggleKeys()});
Keys.bind({keys:["Escape"],group:"Help",does:"Close the list of keys",when:()=>!$("#keyspop").hidden,run:()=>toggleKeys(false)});
document.addEventListener("click",e=>{const pop=$("#keyspop");if(!pop||pop.hidden)return;if(e.target.closest("[data-keysx]")||!pop.contains(e.target))pop.hidden=true},true);
