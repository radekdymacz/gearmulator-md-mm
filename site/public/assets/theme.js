/* Runs in <head> before paint: applies a theme the viewer picked earlier (light is the default). */
(function () {
  var d = document.documentElement;
  d.classList.add("js");
  try {
    var t = localStorage.getItem("mdmm.theme");
    if (t === "light" || t === "dark") d.dataset.theme = t;
  } catch (e) {}
})();
