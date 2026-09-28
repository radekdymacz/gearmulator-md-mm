/* Download flow: 1 machine -> 2 name your price (optional) -> 3 download.
   State lives only in the URL (?ed=md|mm&thanks=1). No storage, no cookies, no requests.
   Payment is a plain link-out to a Lemon Squeezy hosted checkout; no Lemon Squeezy script. */
(function () {
  "use strict";
  var C = window.MDMM_CONFIG || {};
  var step = document.body.dataset.step;
  var q = new URLSearchParams(location.search);
  var ed = q.get("ed") === "mm" ? "mm" : "md";
  var ready = function (u) { return typeof u === "string" && /^https:\/\//.test(u) && u.indexOf("REPLACE_ME") < 0; };

  if (step === "1") {
    var r = document.querySelector("input[name=ed][value=" + ed + "]");
    if (r && q.get("ed")) r.checked = true;
    return;
  }
  if (step !== "2") return;

  var D = (C.downloads || {})[ed] || {};
  var rel = ready(C.releasesPage) ? C.releasesPage : "https://github.com/radekdymacz/gearmulator-md-mm/releases";
  var fileUrl = ready(D.url) ? D.url : rel;
  var name = D.name || (ed === "mm" ? "Monomachine Editor" : "Machinedrum Editor");
  document.querySelector("[data-name]").textContent = name;
  document.title = "Download " + name + " — Future Native Audio";

  var pay = C.payments || {};
  var ls = (pay.lemonsqueezy || {}).checkoutUrl;
  var provider = pay.provider === "none" ? null : ready(ls) ? "lemonsqueezy" : null;
  var thanks = q.get("thanks") === "1";
  var support = document.getElementById("support");
  var plain = document.getElementById("plain");
  var started = document.getElementById("started");
  var nudge = document.getElementById("nudge");
  var progress = document.querySelectorAll("[data-prog]");

  function mark(n) {
    progress.forEach(function (li) {
      var k = Number(li.dataset.prog);
      li.classList.toggle("done", k < n);
      if (k === n) li.setAttribute("aria-current", "step"); else li.removeAttribute("aria-current");
    });
  }
  function download() {
    started.hidden = false;
    nudge.hidden = !(provider && !thanks);
    mark(3);
    location.href = fileUrl; /* a file download: this page stays */
  }
  document.querySelectorAll("[data-download]").forEach(function (el) {
    if (el.tagName === "A") el.href = fileUrl;
    el.addEventListener("click", function (e) { e.preventDefault(); download(); });
  });

  if (thanks) {
    document.getElementById("thanks").hidden = false;
    plain.hidden = true;
    mark(3);
    return;
  }
  if (!provider) { mark(3); return; } /* no payment configured: download only */

  /* pay what you want */
  plain.hidden = true;
  support.hidden = false;
  var sym = pay.symbol || "€", cur = pay.currency || "EUR";
  document.getElementById("cur").textContent = cur;
  var box = document.getElementById("amounts");
  (pay.amounts || []).forEach(function (a) {
    var l = document.createElement("label");
    l.className = "choice";
    var i = document.createElement("input");
    i.type = "radio"; i.name = "amount"; i.value = String(Number(a.value));
    if (Number(a.value) === Number(pay.preselect)) i.checked = true;
    var c = document.createElement("span");
    c.className = "card";
    var b = document.createElement("b"); b.textContent = sym + Number(a.value);
    var s = document.createElement("small"); s.textContent = a.label || "";
    c.appendChild(b); c.appendChild(s); l.appendChild(i); l.appendChild(c); box.appendChild(l);
  });
  var custom = document.getElementById("custom");
  var btn = document.getElementById("pay");
  var amount = function () {
    var v = parseInt(custom.value, 10);
    if (v > 0) return Math.min(v, 1000);
    var r = box.querySelector("input:checked");
    return r ? Number(r.value) : 0;
  };
  var sync = function () { var a = amount(); btn.textContent = a > 0 ? "Pay " + sym + a + " and download" : "Continue to checkout"; };
  box.addEventListener("change", function () { custom.value = ""; sync(); });
  custom.addEventListener("input", function () {
    if (parseInt(custom.value, 10) > 0) box.querySelectorAll("input").forEach(function (i) { i.checked = false; });
    sync();
  });
  sync();

  /* Lemon Squeezy share links take checkout[custom][...] as order metadata only; the price itself
     cannot be preset by URL, so the person types it at checkout (the note says so). */
  btn.addEventListener("click", function () {
    var u = new URL(ls);
    u.searchParams.set("checkout[custom][editor]", ed);
    var a = amount();
    if (a > 0) u.searchParams.set("checkout[custom][suggested]", String(a));
    window.open(u.toString(), "_blank", "noopener");
    download();
  });

  var g = C.goal || {};
  if (g.enabled && Number(g.target) > 0) {
    var pct = Math.max(0, Math.min(100, Math.round((Number(g.raised) || 0) / Number(g.target) * 100)));
    document.getElementById("goalfill").style.width = pct + "%";
    document.getElementById("goaltxt").textContent = (g.label || "Goal") + ": " + sym + (Number(g.raised) || 0) + " of " + sym + Number(g.target) + (g.updated ? " · updated " + g.updated : "");
    document.getElementById("goal").hidden = false;
  }
  var sp = C.supporters || {};
  if (sp.enabled && sp.names && sp.names.length) {
    var el = document.getElementById("supporters");
    el.textContent = "Thanks to " + sp.names.join(", ") + ".";
    el.hidden = false;
  }
  document.getElementById("nudgelink").addEventListener("click", function (e) {
    e.preventDefault(); support.scrollIntoView({ block: "start" }); btn.focus({ preventScroll: true });
  });
})();
