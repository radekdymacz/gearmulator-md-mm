/* Runs in <head> before paint. Light is always the default (the OS preference is not followed);
   dark only when this visitor chose it with the toggle. */
(function () {
  var d = document.documentElement;
  d.classList.add("js");
  var t = "light";
  try { if (localStorage.getItem("mdmm.theme") === "dark") t = "dark"; } catch (e) {}
  d.dataset.theme = t;
})();
