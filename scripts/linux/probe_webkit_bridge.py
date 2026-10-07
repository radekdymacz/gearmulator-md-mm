#!/usr/bin/env python3
# Probe (doc/release/LINUX.md): the page bridge's channels under webkit2gtk-4.0, JUCE 7's Linux backend.
#
#   xvfb-run -a python3 scripts/linux/probe_webkit_bridge.py [basic|pending|answered] [rounds]
#
# basic:    gmbridge:// iframe navigations reach decide-policy, javascript: URLs given to load_uri run (also a
#           900 KB one), a script file next to the page loads, a fragment navigation fires hashchange.
# pending:  as JUCE does on Linux, the policy decision of a gmbridge:// navigation is answered later, and a
#           javascript: URL is loaded while it is still pending (the plug-in answers a page message from inside
#           pageAboutToLoad, before JUCE sends the decision).
# answered: the same, but the decision is answered first and the javascript: URL loaded after it.
# file:     as pending, but the answer is a numbered script file beside the page that the page's poll loads.
import json
import os
import sys

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("WebKit2", "4.0")
from gi.repository import GLib, Gtk, WebKit2

mode = sys.argv[1] if len(sys.argv) > 1 else "basic"
rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 200
d = "/tmp/wkprobe"
os.makedirs(d, exist_ok=True)
page = os.path.join(d, "page.html")
open(page, "w").write("""<!doctype html><html><body><script>
function nav(u){const f=document.createElement('iframe');f.style.display='none';f.src=u;document.documentElement.appendChild(f);setTimeout(()=>f.remove(),1000);}
let n=0;
window.gm={recv(x,seq){n++;nav('gmbridge://c/got-'+encodeURIComponent(String(x).slice(0,40))+'-len'+String(x).length+'-n'+n);}};
window.addEventListener('hashchange',()=>nav('gmbridge://c/hash-'+location.hash.slice(1,30)));
function poll(k){const s=document.createElement('script');s.src='recv-'+k+'.js?'+Date.now();
 s.onload=()=>{s.remove();poll(k+1);};s.onerror=()=>{s.remove();setTimeout(()=>poll(k),20);};document.head.appendChild(s);}
window.addEventListener('load',()=>{nav('gmbridge://c/hello');nav('gmbridge://c/hello2');if(location.search.indexOf('poll')>=0)poll(1);});
</script></body></html>""")
for f in os.listdir(d):
    if f.startswith("recv-"):
        os.remove(os.path.join(d, f))

w = Gtk.Window()
v = WebKit2.WebView()
w.add(v)
w.show_all()
seen = []
crashed = []


def log(*a):
    print(*a, flush=True)


def answer(decision):
    decision.ignore()
    return False


def policy(view, decision, kind):
    if kind != WebKit2.PolicyDecisionType.NAVIGATION_ACTION:
        return False
    uri = decision.get_navigation_action().get_request().get_uri()
    if mode == "basic":
        log("NAV", repr(decision.get_frame_name()), uri[:80])
    if not uri.startswith("gmbridge://"):
        return False
    seen.append(uri)
    if mode == "basic":
        decision.ignore()
        return True
    # JUCE: the decision goes to the plug-in and comes back later; the plug-in's answer to the message
    # (javascript:) is sent before the decision (pending) or after it (answered).
    script = "javascript:window.gm&&gm.recv('" + "x" * 20000 + "'," + str(len(seen)) + ")"
    if len(seen) >= rounds:
        decision.ignore()
        return True

    def load():
        if mode == "file":
            # the plug-in writes the next numbered script beside the page; the page's poll loads it
            k = len(seen) - 1
            tmp = os.path.join(d, "t.js")
            open(tmp, "w").write(script[len("javascript:"):])
            os.rename(tmp, os.path.join(d, "recv-" + str(k) + ".js"))
        else:
            v.load_uri(script)
        return False

    def pending_then_answer():
        load()
        GLib.timeout_add(5, answer, decision)
        return False

    def answer_then_load():
        answer(decision)
        GLib.timeout_add(5, load)
        return False

    GLib.idle_add(pending_then_answer if mode in ("pending", "file") else answer_then_load)
    return True


def terminated(view, reason):
    crashed.append(int(reason))
    log("WEB PROCESS TERMINATED", reason)


v.connect("decide-policy", policy)
v.connect("web-process-terminated", terminated)
v.connect("load-failed", lambda view, ev, uri, err: log("LOAD-FAILED", uri[:80], err.message))
step = [0]


def tick():
    step[0] += 1
    s = step[0]
    if mode == "basic":
        if s == 10:
            log("STEP js small")
            v.load_uri("javascript:window.gm&&gm.recv('js-small',1)")
        if s == 15:
            log("STEP js big")
            v.load_uri("javascript:window.gm&&gm.recv('" + "x" * 900000 + "',2)")
        if s == 20:
            log("STEP file")
            tmp = os.path.join(d, "t.js")
            open(tmp, "w").write("gm.recv('file-1',3)")
            os.rename(tmp, os.path.join(d, "recv-1.js"))
        if s == 25:
            log("STEP hash")
            v.load_uri("file://" + page + "?poll#h1")
    if s == 150 or (mode != "basic" and (len(seen) >= rounds or crashed)):
        log("RESULT", mode, "messages", len(seen), "crashed", crashed, json.dumps([u[:60] for u in seen[:6]]))
        Gtk.main_quit()
        return False
    return True


v.load_uri("file://" + page + ("?poll" if mode in ("basic", "file") else ""))
GLib.timeout_add(200, tick)
Gtk.main()
sys.exit(1 if crashed else 0)
