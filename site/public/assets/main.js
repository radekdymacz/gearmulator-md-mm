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

  /* hero: one window; editor tabs (tablist, arrow keys) and the MKI / MKII plate, one image at a time */
  var hero = document.getElementById("hero-shot");
  if (hero) {
    var names = { md: "Machinedrum Editor", mm: "Monomachine Editor" };
    var state = { machine: "md", plate: "mk1" };
    var tabs = Array.prototype.slice.call(document.querySelectorAll(".wstabs [role=tab]"));
    var plates = hero.querySelectorAll("[data-plate]");
    var show = function () {
      var key = state.machine + "-" + state.plate;
      hero.querySelectorAll("[data-shot]").forEach(function (el) {
        var on = el.dataset.shot === key;
        el.classList.toggle("on", on);
        if (on) el.removeAttribute("aria-hidden"); else el.setAttribute("aria-hidden", "true");
        if (on) el.querySelectorAll("img").forEach(function (i) { i.loading = "eager"; });
      });
      hero.querySelector("[data-wintitle]").textContent = names[state.machine];
      tabs.forEach(function (t) {
        var on = t.dataset.machine === state.machine;
        t.setAttribute("aria-selected", String(on));
        t.tabIndex = on ? 0 : -1;
        if (on) hero.setAttribute("aria-labelledby", t.id);
      });
      plates.forEach(function (x) { x.setAttribute("aria-pressed", String(x.dataset.plate === state.plate)); });
    };
    tabs.forEach(function (t, i) {
      t.addEventListener("click", function () { state.machine = t.dataset.machine; show(); });
      t.addEventListener("keydown", function (e) {
        var j = null;
        if (e.key === "ArrowRight" || e.key === "ArrowDown") j = (i + 1) % tabs.length;
        else if (e.key === "ArrowLeft" || e.key === "ArrowUp") j = (i - 1 + tabs.length) % tabs.length;
        else if (e.key === "Home") j = 0;
        else if (e.key === "End") j = tabs.length - 1;
        if (j === null) return;
        e.preventDefault();
        state.machine = tabs[j].dataset.machine; show(); tabs[j].focus();
      });
    });
    plates.forEach(function (b) { b.addEventListener("click", function () { state.plate = b.dataset.plate; show(); }); });
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
