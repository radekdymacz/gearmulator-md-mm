/* Checkout flow: 1 choose -> 2 pay what you want -> 3 download.
   State lives only in the URL query (?ed=md|mm|both&fmt=app|plugin|both&thanks=1): no storage,
   no cookies, no requests. PayPal is a plain link-out built from /config.js. */
(function () {
  "use strict";
  var C = window.MDMM_CONFIG || {};
  var step = document.body.dataset.step;
  var q = new URLSearchParams(location.search);
  var ED = { md: 1, mm: 1, both: 1 }, FMT = { app: 1, plugin: 1, both: 1 };
  var ed = ED[q.get("ed")] ? q.get("ed") : null;
  var fmt = FMT[q.get("fmt")] ? q.get("fmt") : null;

  function ready(url) { return typeof url === "string" && /^https:\/\//.test(url) && url.indexOf("REPLACE_ME") < 0; }
  function carry(extra) {
    var p = new URLSearchParams();
    if (ed) p.set("ed", ed);
    if (fmt) p.set("fmt", fmt);
    if (extra) Object.keys(extra).forEach(function (k) { p.set(k, extra[k]); });
    var s = p.toString();
    return s ? "?" + s : "";
  }

  /* carry the choice through every step link */
  document.querySelectorAll("a[data-carry]").forEach(function (a) { a.href = a.getAttribute("href").split("?")[0] + carry(); });
  document.querySelectorAll("[data-carry-field]").forEach(function (i) {
    var v = i.name === "ed" ? ed : i.name === "fmt" ? fmt : null;
    if (v) i.value = v; else i.remove();
  });

  /* ---- step 1: restore a previous choice when coming back ---- */
  if (step === "1") {
    if (ed) { var e = document.querySelector("input[name=ed][value=" + ed + "]"); if (e) e.checked = true; }
    if (fmt) { var f = document.querySelector("input[name=fmt][value=" + fmt + "]"); if (f) f.checked = true; }
  }

  /* ---- step 2: amounts and the PayPal link-out ---- */
  if (step === "2") {
    var pp = C.paypal || {};
    var cur = pp.currency || "EUR", sym = pp.symbol || "";
    var box = document.getElementById("amounts");
    var custom = document.getElementById("custom");
    var pay = document.getElementById("pay");
    var note = document.getElementById("paynote");
    document.getElementById("currency").textContent = cur;
    (pp.amounts || [5, 10, 20]).forEach(function (a) {
      var l = document.createElement("label");
      l.className = "choice";
      l.innerHTML = '<input type="radio" name="amount" value="' + Number(a) + '"><span class="card">' + sym + Number(a) + "</span>";
      box.appendChild(l);
    });
    var pre = box.querySelector('input[value="' + Number(pp.preselect) + '"]') || box.querySelector("input");
    if (pre) pre.checked = true;

    var amount = function () {
      var c = parseFloat(custom.value);
      if (c > 0) return Math.min(Math.round(c * 100) / 100, 1000);
      var r = box.querySelector("input:checked");
      return r ? Number(r.value) : 0;
    };
    var fmtAmount = function (a) { return sym ? sym + a : a + " " + cur; };
    var paypalUrl = function (a) {
      var base = pp.url;
      if (pp.mode === "paypalme") return base.replace(/\/+$/, "") + "/" + a + cur;
      if (pp.mode === "donate") {
        var u = new URL(base);
        u.searchParams.set("amount", String(a));
        u.searchParams.set("currency_code", cur);
        return u.toString();
      }
      return base; /* hosted button: PayPal asks for the amount itself */
    };
    var sync = function () {
      var a = amount();
      pay.textContent = a > 0 ? "Donate " + fmtAmount(a) + " with PayPal" : "Donate with PayPal";
    };
    box.addEventListener("change", function () { custom.value = ""; sync(); });
    custom.addEventListener("input", function () {
      if (parseFloat(custom.value) > 0) box.querySelectorAll("input").forEach(function (i) { i.checked = false; });
      sync();
    });
    sync();

    var configured = ready(pp.url);
    if (!configured) {
      pay.disabled = true;
      pay.setAttribute("aria-disabled", "true");
      note.textContent = "Donations are not switched on yet (the PayPal link is not configured). You can still download for free.";
    } else if (pp.mode === "hosted") {
      note.textContent = "PayPal opens in a new tab and asks for the amount there. Your download page opens here at the same time.";
    }

    document.getElementById("donate").addEventListener("submit", function (ev) {
      ev.preventDefault();
      if (!configured) return;
      var a = amount();
      if (!(a > 0)) { custom.focus(); return; }
      window.open(paypalUrl(a), "_blank", "noopener");
      location.href = "/get/download/" + carry({ thanks: "1" });
    });

    var alt = C.alt || {};
    var altA = document.getElementById("alt");
    if (alt.enabled && ready(alt.url)) {
      altA.href = alt.url;
      altA.textContent = alt.label || "Other ways to support";
      altA.hidden = false;
    }
  }

  /* ---- step 3: the downloads for the choice ---- */
  if (step === "3") {
    if (q.get("thanks") === "1") document.getElementById("thanks").hidden = false;
    var D = C.downloads || {};
    var eds = ed === "md" ? ["md"] : ed === "mm" ? ["mm"] : ["md", "mm"];
    var fmts = fmt === "app" ? ["app"] : fmt === "plugin" ? ["plugin"] : ["app", "plugin"];
    var ul = document.getElementById("dl");
    var rel = ready(C.releasesPage) ? C.releasesPage : "https://github.com/radekdymacz/gearmulator-md-mm/releases";
    var items = [];
    eds.forEach(function (e) { fmts.forEach(function (f) { var d = D[e + "-" + f]; if (d) items.push(d); }); });
    if (items.length) {
      ul.innerHTML = "";
      var pending = false;
      items.forEach(function (d) {
        var li = document.createElement("li");
        var s = document.createElement("span");
        s.textContent = d.label;
        var a = document.createElement("a");
        a.className = "btn btn-primary btn-sm";
        if (ready(d.url)) { a.href = d.url; a.textContent = "Download"; }
        else { a.href = rel; a.textContent = "Releases page"; pending = true; }
        li.appendChild(s); li.appendChild(a); ul.appendChild(li);
      });
      if (pending) {
        var n = document.createElement("li");
        n.innerHTML = '<span class="small">The first release is being prepared. Until it is published, the Releases page is where the files will appear.</span>';
        ul.appendChild(n);
      }
      var more = document.createElement("li");
      more.innerHTML = '<span class="small">Want the other editor or format? <a href="/get/">Change your choice</a>.</span>';
      ul.appendChild(more);
    }
  }
})();
