// The app's own files come from sw.js: one verified cache per build, so the
// launcher, engine and workers always match and start without a network.
// A new build is applied only while the launcher is idle in the app's only
// window; otherwise it waits until every window of the app has closed.
// onDownloading tells while a new build is being installed.
export function startAppCache({idle = () => false, onApplying = () => {}, onDownloading = () => {}} = {}) {
  const container = globalThis.navigator?.serviceWorker;
  if (!container || !globalThis.isSecureContext) return {check() {}};
  let registration = null, applying = false, reloading = false, fallback = null;
  const reload = () => {
    clearTimeout(fallback);
    if (!reloading) { reloading = true; location.reload(); }
  };
  const ask = worker => new Promise(resolve => {
    const channel = new MessageChannel();
    const answer = applied => { clearTimeout(timer); channel.port1.close(); resolve(applied); };
    const timer = setTimeout(() => answer(false), 5000);
    channel.port1.onmessage = ({data}) => answer(data?.applied === true);
    worker.postMessage({type:'apply-update'}, [channel.port2]);
  });
  async function check() {
    // An uncontrolled page (the first visit) has nothing to replace: the
    // installed build controls the next start.
    const waiting = registration?.waiting;
    if (!waiting || !container.controller || applying || !idle()) return;
    applying = true; onApplying(true);
    if (await ask(waiting)) {
      // controllerchange reloads; a lost event must not leave Play disabled.
      if (!reloading) fallback = setTimeout(reload, 5000);
      return;
    }
    applying = false; onApplying(false);
  }
  container.addEventListener('controllerchange', () => { if (applying) reload(); });
  // A new build while the page has one: an uncontrolled first visit is
  // installing the app itself.
  const follow = worker => {
    if (!worker || !container.controller) return;
    onDownloading(true);
    worker.addEventListener('statechange', () => {
      if (worker.state === 'installing') return;
      onDownloading(false);
      if (worker.state === 'installed') check();
    });
  };
  container.register(new URL('./sw.js', import.meta.url)).then(value => {
    registration = value;
    value.addEventListener('updatefound', () => follow(value.installing));
    follow(value.installing);
    check();
  }).catch(error => console.warn('Offline app cache is unavailable', error));
  // An installed app may stay in memory for days: when it comes back to an
  // idle launcher, a build that arrived meanwhile (during a game) is applied,
  // and a newer one looked for.
  document.addEventListener('visibilitychange', () => {
    if (document.hidden || !idle()) return;
    check();
    registration?.update().catch(() => {});
  });
  return {check};
}
