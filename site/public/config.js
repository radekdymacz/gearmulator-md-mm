/*
 * Site configuration: the only file to edit for a release or for payments.
 * Plain public data (no secrets), loaded before the page scripts.
 * A value containing "REPLACE_ME" counts as not configured.
 */
window.MDMM_CONFIG = {
  // Bumped by the release process. Shown on the page as-is; the page makes no API calls.
  version: "0.3.1",

  // Contact address. The pages carry it statically (entity-encoded mailto); keep this in sync.
  contact: { email: "radekdymacz@gmail.com" },

  source: "https://github.com/radekdymacz/gearmulator-md-mm",
  releasesPage: "https://github.com/radekdymacz/gearmulator-md-mm/releases",

  // Downloads per operating system, then per machine. GitHub's stable "latest release" redirect:
  // the release build must publish exactly these asset names. The download page picks the
  // visitor's system (or ?os=mac|win|linux) and offers the others. An OS without URLs shows
  // its note and the releases page.
  downloads: {
    mac: {
      label: "macOS",
      line: "macOS · the app, VST3 and AU in one installer",
      md: { name: "Machinedrum Editor", url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/latest/download/Machinedrum-Editor-macOS.pkg" },
      mm: { name: "Monomachine Editor", url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/latest/download/Monomachine-Editor-macOS.pkg" }
    },
    win: {
      label: "Windows",
      line: "Windows x64 · not tested yet · both editors, app and VST3, in one zip",
      md: { name: "Machinedrum Editor", url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/latest/download/MD-MM-Editors-Windows-x64-not-tested.zip" },
      mm: { name: "Monomachine Editor", url: "https://github.com/radekdymacz/gearmulator-md-mm/releases/latest/download/MD-MM-Editors-Windows-x64-not-tested.zip" }
    },
    linux: {
      label: "Linux",
      line: "Linux · coming soon · watch the releases page"
    }
  },

  // Pay what you want, through Lemon Squeezy (merchant of record) as a plain link-out.
  // No Lemon Squeezy script is loaded. provider: "auto" (Lemon Squeezy when checkoutUrl is set,
  // otherwise download only) | "lemonsqueezy" | "none".
  payments: {
    provider: "auto",
    lemonsqueezy: {
      // The product's share link (Products > the product > Share), e.g.
      // https://YOURSTORE.lemonsqueezy.com/buy/xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
      checkoutUrl: "https://nativekloud.lemonsqueezy.com/checkout/buy/7a9298ee-861d-4f01-b6f1-61030311c6ae"
    },
    currency: "EUR",
    symbol: "€",
    // What an amount buys: keep these true.
    amounts: [
      { value: 5,  label: "Say thanks" },
      { value: 10, label: "One feature evening" },
      { value: 25, label: "Towards test hardware" }
    ],
    preselect: 10
  },

  // Honest progress towards a named goal. Off by default: turn on only with real numbers,
  // updated by hand (raised = what actually came in, in whole euros).
  goal: { enabled: false, label: "A Monomachine for real-hardware testing", target: 0, raised: 0, updated: "" },

  // Supporters list: opt-in only. List a name only when the person asked to be listed.
  supporters: { enabled: false, names: [] }
};
