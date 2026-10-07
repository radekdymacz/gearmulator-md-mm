@EDITOR@ @VERSION@ for Windows x64 - NOT TESTED
=================================================================

NOT TESTED: this build comes from CI. It compiles and passes the unit
tests and a VST3 load test, but no person has used it on Windows yet, and
the editor window (a web page) has not been seen on Windows. Reports are
welcome: https://github.com/radekdymacz/gearmulator-md-mm/issues

What is in here
---------------
  @PRODUCT@.exe    the standalone app (run it from anywhere)
  @PRODUCT@.vst3\  the VST3 plug-in (a folder, keep it whole)
  LICENSE.md       GPL-3.0

Install
-------
  VST3:        copy the whole folder "@PRODUCT@.vst3" into
               C:\Program Files\Common Files\VST3\
               then rescan plug-ins in your DAW.
  Standalone:  run @PRODUCT@.exe; no installation needed.

The files are not signed: Windows SmartScreen may warn on first start
("More info", then "Run anyway").

Bring your own ROM
------------------
No firmware is included. On first start the editor asks for the ROM of
your own machine; follow the card in the editor window.
