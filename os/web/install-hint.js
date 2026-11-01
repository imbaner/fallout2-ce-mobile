// How to install the app from a phone's or tablet's browser tab, as a
// strings.js key; null in the installed app and on computers. iOS keeps a
// Home Screen app's storage apart from Safari's, so its hint says to install
// before choosing the game files.
export function installedApp({navigator:device = globalThis.navigator, matchMedia:media = globalThis.matchMedia} = {}) {
  return !!device.standalone || media('(display-mode: standalone)').matches || media('(display-mode: fullscreen)').matches;
}

export function installHint({navigator:device = globalThis.navigator, matchMedia:media = globalThis.matchMedia} = {}) {
  if (installedApp({navigator:device, matchMedia:media})) return null;
  if (/iP(hone|ad|od)/.test(device.userAgent) || (device.platform === 'MacIntel' && device.maxTouchPoints > 1)) return 'install_hint_ios';
  return media('(pointer: coarse)').matches ? 'install_hint_browser' : null;
}
