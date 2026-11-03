// The app's own files for offline starts: one complete, verified cache per
// build. package.py replaces the marker below with this build's files and
// their SHA-256 hashes, so every build is a new worker version. Game
// resources and saves live in IndexedDB and are never touched here.
const BUILD = __FALLOUT_BUILD__;
const SCOPE = new URL('./', self.location).href;
const SCOPE_PATH = new URL(SCOPE).pathname;
// Several copies of the app may share an origin (project pages): each one
// owns only the caches of its own scope.
const PREFIX = 'fallout2-app:' + SCOPE_PATH + ':';
const CACHE = PREFIX + BUILD.buildId;
const PATHS = new Set([...Object.keys(BUILD.files), 'build-info.json']);

async function sha256(bytes) {
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return Array.from(digest, value => value.toString(16).padStart(2, '0')).join('');
}

async function download(path) {
  // Revalidate past any HTTP cache; the hash decides what may be stored.
  const response = await fetch(new URL(path, SCOPE), {cache:'no-cache', credentials:'same-origin'});
  if (!response.ok) throw new Error('Cannot download ' + path + ': HTTP ' + response.status);
  return response;
}

async function verified(path, expected) {
  const response = await download(path), bytes = await response.arrayBuffer();
  if (bytes.byteLength !== expected.bytes || await sha256(bytes) !== expected.sha256) {
    throw new Error('Unexpected contents of ' + path);
  }
  return new Response(bytes, {headers:{'Content-Type':response.headers.get('Content-Type') || 'application/octet-stream'}});
}

// The same file of an earlier build of this scope, checked as a download is:
// an update downloads only the files that changed (the engine's stay the same
// while its code does). A cache that can't be read is downloaded past.
async function reused(path, expected, earlier) {
  for (const name of earlier) {
    try {
      const response = await (await caches.open(name)).match(new URL(path, SCOPE));
      if (!response) continue;
      const bytes = await response.arrayBuffer();
      if (bytes.byteLength === expected.bytes && await sha256(bytes) === expected.sha256) {
        return new Response(bytes, {headers:{'Content-Type':response.headers.get('Content-Type') || 'application/octet-stream'}});
      }
    } catch {
      // The download below.
    }
  }
  return null;
}

async function precache() {
  const earlier = (await caches.keys()).filter(name => name.startsWith(PREFIX) && name !== CACHE);
  const cache = await caches.open(CACHE);
  try {
    for (const [path, expected] of Object.entries(BUILD.files)) {
      await cache.put(new URL(path, SCOPE), await reused(path, expected, earlier) || await verified(path, expected));
    }
    // build-info.json lists this worker's own hash, so it is checked by content.
    const text = await (await download('build-info.json')).text(), info = JSON.parse(text);
    if (info.buildId !== BUILD.buildId
        || Object.entries(BUILD.files).some(([path, expected]) => info.files?.[path]?.sha256 !== expected.sha256)) {
      throw new Error('build-info.json belongs to another build');
    }
    await cache.put(new URL('build-info.json', SCOPE), new Response(text, {headers:{'Content-Type':'application/json'}}));
  } catch (error) {
    // An incomplete set is never used: the previous worker stays in charge.
    await caches.delete(CACHE);
    throw error;
  }
}

self.addEventListener('install', event => event.waitUntil(precache()));

// Activation means no window runs an older build any more: this worker never
// claims pages, and app-cache.js skips waiting only for a lone idle launcher.
self.addEventListener('activate', event => event.waitUntil((async () => {
  for (const name of await caches.keys()) {
    if (name.startsWith(PREFIX) && name !== CACHE) await caches.delete(name);
  }
})()));

self.addEventListener('fetch', event => {
  const request = event.request;
  if (request.method !== 'GET' || request.headers.has('Range') || !request.url.startsWith(SCOPE)) return;
  let path;
  try {
    path = decodeURIComponent(new URL(request.url).pathname.slice(SCOPE_PATH.length)) || 'index.html';
  } catch {
    return;
  }
  // Development endpoints and anything else outside this build use the network.
  if (!PATHS.has(path)) return;
  // Query strings (?mode=, ?dev=1) select behavior, not a different file.
  event.respondWith(caches.open(CACHE)
    .then(cache => cache.match(new URL(path, SCOPE)))
    .then(cached => cached || fetch(request)));
});

// app-cache.js asks a waiting build to take over. Only a single window may
// switch: another one could be in a game whose later imports must match it.
self.addEventListener('message', event => {
  if (event.data?.type !== 'apply-update') return;
  event.waitUntil((async () => {
    const windows = await self.clients.matchAll({type:'window', includeUncontrolled:true});
    const applied = windows.length === 1;
    if (applied) await self.skipWaiting();
    event.ports[0]?.postMessage({applied});
  })());
});
