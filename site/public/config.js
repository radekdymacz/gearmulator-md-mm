/*
 * Site configuration: the only file Radek edits to go live.
 * Plain data, loaded before the page scripts. No secrets belong here: it is public.
 *
 * Anything still containing "REPLACE_ME" is treated as not configured:
 * the page then points people at the GitHub Releases page instead of a broken link.
 */
window.MDMM_CONFIG = {
  source: "https://github.com/radekdymacz/gearmulator-md-mm",
  releasesPage: "https://github.com/radekdymacz/gearmulator-md-mm/releases",
  version: "alpha",

  // Direct download links (GitHub Release assets). Fill in when the release is published.
  downloads: {
    "md-app":    { label: "Machinedrum Editor — standalone app (macOS)",       url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/download/REPLACE_ME/Machinedrum-Editor-app-macOS.zip" },
    "md-plugin": { label: "Machinedrum Editor — VST3 + AU plug-in (macOS)",    url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/download/REPLACE_ME/Machinedrum-Editor-plugin-macOS.zip" },
    "mm-app":    { label: "Monomachine Editor — standalone app (macOS)",       url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/download/REPLACE_ME/Monomachine-Editor-app-macOS.zip" },
    "mm-plugin": { label: "Monomachine Editor — VST3 + AU plug-in (macOS)",    url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/download/REPLACE_ME/Monomachine-Editor-plugin-macOS.zip" }
  },

  // Donations: a plain link-out to PayPal. No PayPal script is ever loaded.
  // mode:
  //   "donate"   -> url like https://www.paypal.com/donate/?business=YOUR_ID&no_recurring=0&currency_code=EUR
  //                 (the amount is added as &amount=10 and &currency_code=)
  //   "paypalme" -> url like https://www.paypal.me/YOURNAME  (the amount is added as /10EUR)
  //   "hosted"   -> url like https://www.paypal.com/donate/?hosted_button_id=XXXX
  //                 (PayPal does not take a preset amount here: the person types it on PayPal)
  paypal: {
    mode: "paypalme",
    url: "https://www.paypal.me/REPLACE_ME",
    currency: "EUR",
    symbol: "€",
    amounts: [5, 10, 20, 35],
    preselect: 10
  },

  // A second way to support, off until enabled. Set enabled: true and a real URL.
  alt: {
    enabled: false,
    label: "GitHub Sponsors",            // or "Ko-fi"
    url: "https://github.com/sponsors/REPLACE_ME"   // or "https://ko-fi.com/REPLACE_ME"
  }
};
