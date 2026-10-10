
/* ===== Note lengths in the piano roll (I-010, 0.5): a note is a trig and where it ends, as the machine plays it =====
   Pure: a track's steps (null, {off:1} or a trig {n, a, f, l}) and the pattern's length in, changes out ([step, value]
   pairs and, on a MIDI track, a LEN lock); the page applies and sends them (70-seq.js). Measured on the firmware
   (mmDeskFirmwareTest notelength, lenprobe):
   - A synth track has no length: a note sounds from its trig to the next NOTE OFF or trig of the track, past the
     pattern's end onto its start (a NOTE OFF on step 3 ends a note of step 29 of 32). A note of L steps is its trig
     and a NOTE OFF L steps later; when a trig is there, the trig ends it.
   - A MIDI track's note lasts LEN ticks (the MIDI page's LEN, lockable per step): 24 a quarter note, 6 a step at 1X
     (LEN 1-126, at every tempo); LEN 127 lasts until a NOTE OFF trig. The track's next trig ends a note before its
     LEN does, as on a synth track.
   So a note can only be drawn up to the track's next trig; a NOTE OFF the drawn length no longer needs is taken away,
   and a removed note leaves the note before it its length (a NOTE OFF where the removed one began). */
const MmRoll=(()=>{
 const TICKS=6,LEN_MAX=126,LEN_HOLD=127;
 const trig=st=>!!st&&!st.off;
 /* the steps after s, once round the pattern: k = s+1 .. s+len, each at index k % len */
 function after(len,s,f){for(let k=s+1;k<=s+len;k++)if(f(k,k%len))return k;return null}
 /* the next step with anything on it (a trig or a NOTE OFF), as an unwrapped step (s + len: none, the note plays until it comes round) */
 const nextEvent=(steps,len,s)=>after(len,s,(k,i)=>!!steps[i])??s+len;
 const nextTrig=(steps,len,s)=>after(len,s,(k,i)=>trig(steps[i]))??s+len;
 /* the trig whose gate runs into step s (nothing between: no NOTE OFF, no trig), or null */
 function before(steps,len,s){for(let k=1;k<len;k++){const i=((s-k)%len+len)%len,st=steps[i];if(st)return trig(st)?i:null}return null}
 /* where a note ends, unwrapped steps from its trig. midi: {len: LEN} for a MIDI track's note (null: a synth track) */
 function end(steps,len,s,midi){const e=nextEvent(steps,len,s);if(!midi||midi.len>=LEN_HOLD)return e;return Math.min(e,s+Math.max(1,midi.len)/TICKS)}
 /* the NOTE OFF that ends the note on s, as a step index, or -1 */
 function closer(steps,len,s){const e=nextEvent(steps,len,s);return e<s+len&&steps[e%len]?.off?e%len:-1}
 /* the most a note on s can be drawn, in steps: up to the next trig (it ends the note) */
 const room=(steps,len,s)=>nextTrig(steps,len,s)-s;
 /* a note on s that lasts L whole steps: the step changes (and, midi, the LEN lock to set: a number, or null to
    leave the kit's value when it is that already). NOTE OFFs between it and the next trig go; one goes where it ends
    when that is before the next trig. midi: {kitLen} for a MIDI track */
 function setLen(steps,len,s,L,midi){const r=room(steps,len,s);L=Math.max(1,Math.min(Math.round(L),r));const out=[];
  const offs=[];after(len,s,(k,i)=>{if(k>=s+r)return true;if(steps[i]?.off)offs.push(i);return false});
  let lock;if(midi){const v=L*TICKS<=LEN_MAX?L*TICKS:LEN_HOLD;lock=v===midi.kitLen?null:v}
  const off=(!midi||lock===LEN_HOLD||(lock==null&&midi.kitLen===LEN_HOLD))&&L<r?(s+L)%len:-1;
  for(const i of offs)if(i!==off)out.push([i,null]);if(off>=0&&!steps[off]?.off)out.push([off,{off:1}]);
  return midi?{steps:out,len:lock,L}:{steps:out,L}}
 /* a removed note: its step, and the NOTE OFF that ended it, go; the note before it keeps its length (a NOTE OFF where
    the removed one began, when that note ran into it; on a MIDI track only when its LEN went past it: lenOf(step)) */
 function remove(steps,len,s,lenOf){const out=[],c=closer(steps,len,s),p=before(steps,len,s);
  const ranInto=p!=null&&p!==s&&(!lenOf||lenOf(p)>=LEN_HOLD||lenOf(p)/TICKS>((s-p)%len+len)%len);
  out.push([s,ranInto?{off:1}:null]);if(c>=0&&c!==s)out.push([c,null]);return out}
 /* the steps a selection of the note on s takes: from its trig to its end, its closing NOTE OFF too (so a copy carries
    where it ends); not past the pattern's end */
 function span(steps,len,s,midi){const c=closer(steps,len,s);const e=c>s?c+1:Math.min(len,Math.ceil(end(steps,len,s,midi)));return{from:s,to:Math.max(s+1,e)}}
 /* the words of a length in steps, as a DAW says it at 1X (a step is a 1/16) */
 function say(L){const q={1:"1/16",2:"1/8",3:"3/16",4:"1/4",6:"3/8",8:"1/2",12:"3/4",16:"1 bar",32:"2 bars",48:"3 bars",64:"4 bars"}[L];return q||L+" steps"}
 /* the draw length key's steps, a click goes round them */
 const LENGTHS=[1,2,4,8,16];
 return{TICKS,LEN_MAX,LEN_HOLD,nextEvent,nextTrig,before,end,closer,room,setLen,remove,span,say,LENGTHS}})();
