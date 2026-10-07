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
  WebView2-LICENSE.txt   the licence of Microsoft's WebView2 loader, which
                   is built into the editor

Install
-------
  VST3:        copy the whole folder "@PRODUCT@.vst3" into
               C:\Program Files\Common Files\VST3\
               then rescan plug-ins in your DAW.
  Standalone:  run @PRODUCT@.exe; no installation needed.

Microsoft Edge WebView2 Runtime
-------------------------------
The editor window is a web page shown by Microsoft Edge WebView2. Its
runtime comes with Windows 11 and with an up-to-date Windows 10, so
usually there is nothing to install. If the window says the runtime is
missing, install the "Evergreen" runtime from Microsoft:
  https://go.microsoft.com/fwlink/p/?LinkId=2124703
then open the editor again. Nothing else is needed: no installer, no DLL.
The editor keeps WebView2's cache in
  %LOCALAPPDATA%\Gearmulator\EditorWebView2
(safe to delete while the editor is closed).

The files are not signed: Windows SmartScreen may warn on first start
("More info", then "Run anyway").

Bring your own ROM
------------------
No firmware is included. On first start the editor asks for the ROM of
your own machine; follow the card in the editor window.
