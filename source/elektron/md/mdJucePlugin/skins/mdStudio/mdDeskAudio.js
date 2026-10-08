"use strict";
/* The AUDIO / MIDI panel itself is skins/shared/deskAudio.js (both editors, loaded before this file).
   The plug-in's host (P6): the gm-audio/devices document of the standalone's AudioDeviceManager
   (mdAudioMidiLink.cpp). In a plug-in the document says standalone false: no engine-menu entry,
   and the panel only says that the host owns audio and MIDI. */
let audioDocument = null, audioError = "";
/* the panel shows the devices document and the last change's error: an audioSet failure is only in
   its result (errors[0]), view state until the next change */
function audioDoc() { return audioDocument && Object.assign({}, audioDocument, { error: audioError }); }
function audioSend(c) { Bridge.send(Object.assign({ op: "audioSet" }, c), { onResult: r => { audioError = r.ok ? "" : (r.errors || [])[0] || ""; if (AP.open) drawAudio(); } }); }
function audioMeter(on) { Bridge.send({ op: "audioMeter", on: !!on }); }
function showAudioEntry() {
	const o = document.querySelector('#engsel option[value="audio"]');
	if (o) o.hidden = o.disabled = !(audioDocument && audioDocument.standalone);
	Boot.midiRefresh();	/* the HW MIDI card's AUDIO / MIDI key follows this entry */
}
Bridge.onMessage(m => {
	if (m.type === "audio") { audioDocument = m.doc; showAudioEntry(); if (AP.open) drawAudio(); }
	else if (m.type === "audioLevel") audioLevel(m.in);
	else if (m.type === "openAudio") openAudio();
});
Keys.bind({ id: "audio-settings", scope: "any", keys: [","], group: "Anywhere", does: "AUDIO / MIDI settings (also in the engine menu)" });
showAudioEntry();
Bridge.send({ op: "audio" });
