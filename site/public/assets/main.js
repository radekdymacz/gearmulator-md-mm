/* Shared page behaviour: theme switch, hero plate switch, screenshot tabs, the step motif.
   No network, no storage beyond the viewer's own theme choice. */
(function () {
  "use strict";
  var root = document.documentElement;

  /* ---- theme: light default, dark via OS or the button ---- */
  function current() {
    if (root.dataset.theme === "light" || root.dataset.theme === "dark") return root.dataset.theme;
    return window.matchMedia && matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  }
  function label() {
    var next = current() === "dark" ? "light" : "dark";
    document.querySelectorAll("[data-theme-toggle]").forEach(function (b) {
      b.setAttribute("aria-label", "Switch to " + next + " theme");
      b.title = "Switch to " + next + " theme";
    });
  }
  document.querySelectorAll("[data-theme-toggle]").forEach(function (b) {
    b.addEventListener("click", function () {
      var next = current() === "dark" ? "light" : "dark";
      root.dataset.theme = next;
      try { localStorage.setItem("mdmm.theme", next); } catch (e) {}
      label();
    });
  });
  label();

  /* ---- hero: MKI / MKII plate ---- */
  var hero = document.getElementById("hero-shot");
  if (hero) {
    var btns = hero.querySelectorAll("[data-plate]");
    btns.forEach(function (b) {
      b.addEventListener("click", function () {
        var v = b.dataset.plate;
        btns.forEach(function (x) { x.setAttribute("aria-pressed", String(x === b)); });
        hero.querySelectorAll("[data-plate-img]").forEach(function (el) { el.hidden = el.dataset.plateImg !== v; });
      });
    });
  }

  /* ---- screenshot tabs (WAI-ARIA tabs, roving tabindex) ---- */
  var list = document.querySelector("[role=tablist]");
  if (list) {
    var tabs = Array.prototype.slice.call(list.querySelectorAll("[role=tab]"));
    var select = function (t, focus) {
      tabs.forEach(function (x) {
        var on = x === t;
        x.setAttribute("aria-selected", String(on));
        x.tabIndex = on ? 0 : -1;
        var p = document.getElementById(x.getAttribute("aria-controls"));
        if (p) p.hidden = !on;
      });
      if (focus) t.focus();
    };
    tabs.forEach(function (t, i) {
      t.addEventListener("click", function () { select(t, false); });
      t.addEventListener("keydown", function (e) {
        var j = null;
        if (e.key === "ArrowRight" || e.key === "ArrowDown") j = (i + 1) % tabs.length;
        else if (e.key === "ArrowLeft" || e.key === "ArrowUp") j = (i - 1 + tabs.length) % tabs.length;
        else if (e.key === "Home") j = 0;
        else if (e.key === "End") j = tabs.length - 1;
        if (j !== null) { e.preventDefault(); select(tabs[j], true); }
      });
    });
  }

  /* ---- 16-step motif: a four-on-the-floor pattern and a walking playhead ---- */
  var box = document.querySelector("[data-steps]");
  if (box) {
    var pattern = [1,0,0,1, 0,0,1,0, 1,0,0,1, 0,1,1,0];
    var cells = pattern.map(function (on) {
      var i = document.createElement("i");
      if (on) i.className = "on";
      box.appendChild(i);
      return i;
    });
    var reduce = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;
    if (!reduce) {
      var step = 0, prev = null;
      setInterval(function () {
        if (document.hidden) return;
        if (prev) prev.classList.remove("ph");
        prev = cells[step];
        prev.classList.add("ph");
        step = (step + 1) % 16;
      }, 125); /* 16ths at 120 BPM */
    }
  }
})();
