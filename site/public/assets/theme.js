/* Runs in <head> before paint. Light is always the default (the OS preference is not followed);
   dark only when this visitor chose it with the toggle. window.mdmmTheme(t) also keeps the
   colour-scheme and theme-color meta tags in step, for the first paint and for the toggle. */
(function () {
  var d = document.documentElement;
  d.classList.add("js");
  window.mdmmTheme = function (t) {
    d.dataset.theme = t;
    var cs = document.querySelector('meta[name="color-scheme"]');
    var tc = document.querySelector('meta[name="theme-color"]');
    if (cs) cs.setAttribute("content", t);
    if (tc) tc.setAttribute("content", t === "dark" ? "#000000" : "#fbfbf7");
  };
  var t = "light";
  try { if (localStorage.getItem("mdmm.theme") === "dark") t = "dark"; } catch (e) {}
  window.mdmmTheme(t);
})();
