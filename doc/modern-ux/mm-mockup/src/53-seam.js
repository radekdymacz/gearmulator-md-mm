/* ===== The seam as data (P6; DESIGN-REVIEW-2026-10-02 finding 16) =====
   The one list of the calls a host implements (window.MMHost; 55-host.js says what each means) and of the members
   the view gives a host (window.MMView, 130-main.js). The page checks its host and its view against it when it
   starts; sync-mmstudio-skin.py reads the same JSON for its checks of mmAdapter.js and the mockup (the calls the
   mockup makes and the members the adapter uses are in the lists); mmViewTest.js checks the loaded page's MMHost
   and MMView against it, both ways. Add a call or a member here first. */
const MM_SEAM={
 "host":[
  "ownsClock","engineLabels","notes","start","commit","intent","library","undo","redo","history","togglePlay",
  "selectPattern","kit","tempo","mutes","keyMode","record","songSlot","chain","chainClear","seqMode","loadSong","waiting",
  "sendNow","playKey","keyUp","noteOn","noteOff","joy","learning","learnTarget","learnBind","modulators",
  "engine","chooseRom","removeRom","romManage","syxChoose","syxExport","syxStart","syxStop","revealRom",
  "recheck","firstRun","bootScreen","renderPst","menu","audioDoc","audioSend","audioMeter"],
 "view":[
  "audible","soloed","engReady","asgT","noteName","pname","machName","kitName","gated","busy","sel","mode",
  "playing","step","tempo","engineState","kitState","learnTarget","learning","ctlSetup","show","startEmpty",
  "setPatternSlot","setKitSlot","setReading","setTempo","setInput","setPlaying","setStep","setSongRow","setEng","dlgOpen",
  "setEngineLabel","setEngineTip","setEngines","setAudioEntry","clearLearnTarget","setMapping","setModulation",
  "setCtlSetup","disable","setRecord","setLcd","setKeyDown","setPst","closeFirmwareDialog","bootRom",
  "bootInstalled","syxPreview","syxProgress","render","renderTop","drawLib","toast","ask","redraw","movePH",
  "setPos","flashTracks","goWs","clickStep","autoRange","kitSave","redrawAudio","audioLevel","openAudio"]};
