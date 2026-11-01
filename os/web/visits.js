// Anonymous counts for the public site (GoatCounter, no cookies): page
// openings with the page path, the site or link label (?ref=, utm_*) the
// visitor came from and the screen width, plus a few named events. Nothing
// from the game, its files or saves. Local and test hosts, automated
// browsers, Do Not Track, Global Privacy Control and the opt-out on
// privacy.html send nothing. The request is cross-origin, so sw.js never
// caches or holds it; offline it simply fails.
const HOST = 'play.f2touch.com';
const ENDPOINT = 'https://imbaner.goatcounter.com/count';
const OPT_OUT = 'f2touch-no-visits';
const CAMPAIGN = ['ref', 'utm_source', 'utm_medium', 'utm_campaign'];

export function visitsBlocked({navigator:device = globalThis.navigator} = {}) {
  return device.doNotTrack === '1' || device.globalPrivacyControl === true;
}

export function visitsOptedOut({localStorage:storage = globalThis.localStorage} = {}) {
  try { return storage.getItem(OPT_OUT) === '1'; } catch { return false; }
}

export function setVisitsOptedOut(value, {localStorage:storage = globalThis.localStorage} = {}) {
  try { value ? storage.setItem(OPT_OUT, '1') : storage.removeItem(OPT_OUT); } catch { /* Private mode. */ }
}

// The count request's URL, or null when nothing is counted. An event is
// counted under its name instead of the page.
export function visitUrl({event = null, location:place = globalThis.location, navigator:device = globalThis.navigator,
  document:page = globalThis.document, screen:display = globalThis.screen,
  localStorage:storage = globalThis.localStorage} = {}) {
  if (place?.hostname !== HOST || device.webdriver || visitsBlocked({navigator:device})
      || visitsOptedOut({localStorage:storage})) return null;
  const data = {p:event ?? place.pathname, t:event ?? page.title, r:'', s:display.width, rnd:Math.random().toString(36).slice(2, 7)};
  if (event) {
    data.e = true;
  } else {
    try { if (page.referrer && new URL(page.referrer).hostname !== HOST) data.r = page.referrer; } catch { /* Not a URL. */ }
    const search = new URLSearchParams(place.search), campaign = new URLSearchParams();
    for (const key of CAMPAIGN) if (search.has(key)) campaign.set(key, search.get(key));
    if (campaign.size) data.q = '?' + campaign;
  }
  return ENDPOINT + '?' + new URLSearchParams(data);
}

function send(url) {
  if (!url) return;
  try { if (navigator.sendBeacon?.(url)) return; } catch { /* Fall back to an image. */ }
  new Image().src = url;
}

export const countVisit = () => send(visitUrl());

const counted = new Set();
// Once per page load, e.g. a second start after quitting to the launcher
// counts once.
export function countEvent(name) {
  if (counted.has(name)) return;
  counted.add(name);
  send(visitUrl({event:name}));
}
