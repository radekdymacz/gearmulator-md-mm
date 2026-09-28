/* Shared page behaviour: theme toggle, hero plate switch, the step-key accent, version label.
   No network. The only storage is this visitor's theme choice. */
(function () {
  "use strict";
  var root = document.documentElement;
  var C = window.MDMM_CONFIG || {};

  /* theme: light by default; dark only when chosen here */
  function label() {
    var next = root.dataset.theme === "dark" ? "light" : "dark";
    document.querySelectorAll("[data-theme-toggle]").forEach(function (b) {
      b.setAttribute("aria-label", "Switch to " + next + " theme");
      b.title = "Switch to " + next + " theme";
    });
  }
  document.querySelectorAll("[data-theme-toggle]").forEach(function (b) {
    b.addEventListener("click", function () {
      var next = root.dataset.theme === "dark" ? "light" : "dark";
      root.dataset.theme = next;
      try { localStorage.setItem("mdmm.theme", next); } catch (e) {}
      label();
    });
  });
  label();

  /* version, from config (no API call) */
  if (C.version) document.querySelectorAll("[data-version]").forEach(function (el) { el.textContent = "v" + C.version; });

  /* hero: MKI / MKII plate */
  var hero = document.getElementById("hero-shot");
  if (hero) {
    var btns = hero.querySelectorAll("[data-plate]");
    btns.forEach(function (b) {
      b.addEventListener("click", function () {
        btns.forEach(function (x) { x.setAttribute("aria-pressed", String(x === b)); });
        hero.querySelectorAll("[data-plate-img]").forEach(function (el) { el.hidden = el.dataset.plateImg !== b.dataset.plate; });
      });
    });
  }

  /* the editor's step keys: one 16-step row, LED bars, an accent and locks,
     and the soft playhead column gliding at 120 BPM (16ths = 125 ms) */
  var box = document.querySelector("[data-keys]");
  if (box) {
    var row = ["on", "", "", "on lk", "", "", "on", "", "on acc", "", "", "on", "", "on lk", "on", ""];
    var cells = row.map(function (c, i) {
      var k = document.createElement("i");
      k.className = (i % 4 === 0 ? "q " : "") + c;
      box.appendChild(k);
      return k;
    });
    var reduce = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;
    if (!reduce) {
      var ph = document.createElement("span");
      ph.className = "ph jump";
      box.appendChild(ph);
      var step = 0;
      var place = function () {
        var c = cells[step];
        ph.style.width = c.offsetWidth + "px";
        ph.style.transform = "translateX(" + (c.offsetLeft - 6) + "px)";
      };
      setInterval(function () {
        if (document.hidden) return;
        step = (step + 1) % 16;
        ph.classList.toggle("jump", step === 0);
        place();
      }, 125);
      place();
    }
  }
})();
