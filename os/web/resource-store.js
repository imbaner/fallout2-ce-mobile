// Permanent resource format. The database and lock keep their historical names
// so installations made by earlier test builds stay readable.
import {BLOCK_SIZE, MAX_READ, digest, WRITE_BATCH_BYTES, WRITE_BATCH_BLOCKS, checkCancelled, busyError} from './storage-common.js';
export {BLOCK_SIZE, MAX_READ};
export const RESOURCE_DB = 'fallout2-device-check-v1';
export const RESOURCE_LOCK = 'fallout2-device-check';
export const DB_VERSION = 2;
// Format 2: several games (`games` in state, `active:<id>`/`previous:<id>`
// beside the first game's `active`/`previous`). A database stays format 1
// until a second game is added: builds that know one game read it, and
// can't open one whose collection would drop the other games.
const FORMATS = [1,2];
// A generation's own record is the same in both.
const GENERATION_FORMAT = 1;
export const FIRST_GAME = '1';
// The first game keeps the keys of the builds of one game.
export const activeKey = game => game === FIRST_GAME ? 'active' : 'active:' + game;
export const previousKey = game => game === FIRST_GAME ? 'previous' : 'previous:' + game;
const generationKey = key => key === 'active' || key === 'previous' || key.startsWith('active:') || key.startsWith('previous:');
// The game's part of the user's storage: the first game's is its root (as
// builds of one game kept it), another's games/<id>/.
export const gamePrefix = game => game === FIRST_GAME ? '' : 'games/' + gameId(game) + '/';
export function gameId(value) {
  if (typeof value !== 'string' || !/^[1-9][0-9]{0,5}$/.test(value)) throw new Error('Invalid game');
  return value;
}
const request = value => new Promise((resolve, reject) => {
  value.onsuccess = () => resolve(value.result);
  value.onerror = () => reject(value.error);
});
const completed = tx => new Promise((resolve, reject) => {
  tx.oncomplete = resolve;
  tx.onabort = () => reject(tx.error || new Error('Resource transaction aborted'));
  tx.onerror = () => {};
});
export function resourcePath(path) {
  if (typeof path !== 'string' || path !== path.normalize('NFC') || path.length > 1024
      || /[\\\u0000-\u001f\u007f:]/.test(path)
      || path.split('/').some(part => !part || part === '.' || part === '..')) {
    throw new Error('Invalid resource path');
  }
  return path.toLowerCase();
}
function filesOf(files) {
  if (!Array.isArray(files) || files.length > 50000) throw new Error('Invalid resource manifest');
  const seen = new Set();
  let bytes = 0;
  const entries = files.map(file => {
    const key = resourcePath(file.path);
    if (seen.has(key) || !Number.isSafeInteger(file.size) || file.size < 0) throw new Error('Invalid resource manifest');
    seen.add(key); bytes += file.size;
    return {path:file.path, size:file.size};
  });
  for (const key of seen) {
    const parts = key.split('/');
    for (let i = 1; i < parts.length; i++) {
      if (seen.has(parts.slice(0,i).join('/'))) throw new Error('File/directory resource collision');
    }
  }
  // master.dat and critter.dat: files, or folders (Fallout: Nevada 1.00's master.dat).
  if (bytes > 10 * 1024 ** 3 || !['master.dat','critter.dat'].every(key => entries.some(file => {
    const path = resourcePath(file.path);
    return (path === key && file.size > 0) || path.startsWith(key + '/');
  }))) throw new Error('Incomplete resource manifest');
  return entries;
}
function checkFormat(value) {
  if (!FORMATS.includes(value?.format) || value?.minReader !== value?.format || value?.minWriter !== value?.format) {
    throw new Error('Unsupported resource format. Update the application; stored data has not been reset.');
  }
}
async function lease(locks, name) {
  if (!locks) throw new Error('Local storage requires Web Locks');
  let release, ready;
  const held = new Promise(resolve => { release = resolve; });
  const acquired = new Promise((resolve, reject) => { ready = {resolve,reject}; });
  const task = locks.request(name, {ifAvailable:true}, lock => {
    if (!lock) throw busyError('Close the other game or installation window and try again');
    ready.resolve(); return held;
  });
  task.catch(ready.reject);
  await acquired;
  return async () => { release(); await task; };
}
async function openDatabase(name) {
  const pending = indexedDB.open(name, DB_VERSION);
  let cancelled = false;
  pending.onupgradeneeded = () => {
    if (cancelled) { pending.transaction.abort(); return; }
    const db = pending.result;
    db.createObjectStore('state');
    db.createObjectStore('generations', {keyPath:'run'});
    db.createObjectStore('chunks', {keyPath:'hash'});
    db.createObjectStore('refs', {keyPath:['run','path','index']}).createIndex('run','run');
    pending.transaction.objectStore('state').put({format:1,minReader:1,minWriter:1},'format');
  };
  return new Promise((resolve, reject) => {
    pending.onblocked = () => {
      cancelled = true;
      reject(busyError('Close older game windows before upgrading local storage'));
    };
    pending.onerror = () => reject(pending.error);
    pending.onsuccess = () => {
      if (cancelled) pending.result.close(); else resolve(pending.result);
    };
  });
}

// [game]: whose installation `active`, `begin`...`activate` and `inherit`
// work on (the list of games: `games`, `saveGames`, `forgetGame`).
export async function openResourceStore({name = RESOURCE_DB, locks = globalThis.navigator?.locks, game = FIRST_GAME} = {}) {
  gameId(game);
  const release = await lease(locks, name === RESOURCE_DB ? RESOURCE_LOCK : name);
  let db, closed = false, closing = null;
  const fileIndexes = new WeakMap();
  const live = () => { if (closed) throw new Error('Resource store is closed'); };
  const transaction = (stores, mode = 'readonly') => {
    live(); const tx = db.transaction(stores, mode, mode === 'readwrite' ? {durability:'strict'} : undefined);
    return [tx,completed(tx)];
  };
  const get = async (store, key) => {
    const [tx,done] = transaction(store);
    const [value] = await Promise.all([request(tx.objectStore(store).get(key)),done]); return value;
  };
  const all = async store => {
    const [tx,done] = transaction(store);
    const [value] = await Promise.all([request(tx.objectStore(store).getAll()),done]); return value;
  };
  const store = {
    close() {
      if (!closing) { closed = true; db?.close(); closing = release(); }
      return closing;
    },
    game,
    async active() {
      const id = await get('state',activeKey(game));
      if (!id) return null;
      const value = await get('generations',id);
      if (!value || value.state !== 'complete') throw new Error('Damaged active resource manifest');
      checkFormat(value); filesOf(value.files);
      if (await digest(new TextEncoder().encode(JSON.stringify(value.files))) !== value.manifestHash) {
        throw new Error('Damaged resource manifest checksum');
      }
      return value;
    },
    async begin() {
      const run = crypto.randomUUID();
      const [tx,done] = transaction(['generations','state'],'readwrite');
      tx.objectStore('generations').add({run,state:'writing',format:GENERATION_FORMAT,minReader:GENERATION_FORMAT,minWriter:GENERATION_FORMAT,created:Date.now()});
      tx.objectStore('state').put(true,'needsCollection');
      await done; return run;
    },
    async block(run, path, index, data) {
      return store.writeBlocks(run,[{path,index,data}]);
    },
    async writeBlocks(run, input, {signal} = {}) {
      checkCancelled(signal);
      if (typeof run !== 'string' || !run) throw new Error('Invalid resource generation');
      if (!Array.isArray(input) || !input.length || input.length > WRITE_BATCH_BLOCKS) throw new Error('Invalid resource batch');
      let total = 0;
      const keys = new Set();
      const rows = input.map(({path,index,data}) => {
        const key = resourcePath(path), ref = key + ':' + index;
        if (!Number.isSafeInteger(index) || index < 0 || !(data instanceof Uint8Array)
            || !data.length || data.length > BLOCK_SIZE || keys.has(ref)) throw new Error('Invalid resource block');
        keys.add(ref); total += data.length;
        return {path:key,index,data:data.slice()};
      });
      if (total > WRITE_BATCH_BYTES) throw new Error('Resource batch exceeds memory budget');
      const hashStarted = performance.now();
      for (const row of rows) { checkCancelled(signal); row.hash = await digest(row.data); }
      const hashMs = performance.now() - hashStarted;
      checkCancelled(signal);
      const started = performance.now();
      let newBytes = 0, aborted = false, failure;
      const [tx,done] = transaction(['generations','chunks','refs'],'readwrite');
      const abort = () => { aborted = true; try { tx.abort(); } catch {} };
      const fail = error => { failure = error; try { tx.abort(); } catch {} };
      signal?.addEventListener('abort',abort,{once:true});
      const generation = tx.objectStore('generations').get(run);
      generation.onsuccess = () => {
        if (generation.result?.state !== 'writing') { fail(new Error('Resource generation is not writable')); return; }
        try {
          const unique = new Set();
          for (const {path,index,data,hash} of rows) {
            if (!unique.has(hash)) {
              unique.add(hash);
              const existing = tx.objectStore('chunks').getKey(hash);
              existing.onsuccess = () => {
                try {
                  if (existing.result === undefined) { tx.objectStore('chunks').put({hash,data}); newBytes += data.length; }
                } catch (error) { fail(error); }
              };
            }
            tx.objectStore('refs').put({run,path,index,hash,size:data.length});
          }
        } catch (error) { fail(error); }
      };
      try { await done; }
      catch (error) { if (aborted) throw new DOMException('Cancelled','AbortError'); throw failure || error; }
      finally { signal?.removeEventListener('abort',abort); }
      return {transactions:1,hashMs,writeMs:performance.now() - started,newBytes,reusedBytes:total - newBytes};
    },
    async inherit(run, source, files, {signal} = {}) {
      checkCancelled(signal);
      const wanted = new Map(files.map(file => [resourcePath(file.path),file.size]));
      const known = new Map(source.files.map(file => [resourcePath(file.path),file.size]));
      for (const [path,size] of wanted) if (known.get(path) !== size) throw new Error('Invalid inherited resource');
      const [tx,done] = transaction(['generations','refs','state'],'readwrite');
      let failure;
      const abort = () => { try { tx.abort(); } catch {} };
      const guarded = action => () => { try { action(); } catch (error) { failure = error; abort(); } };
      signal?.addEventListener('abort',abort,{once:true});
      const pending = tx.objectStore('generations').get(run);
      pending.onsuccess = guarded(() => {
        if (pending.result?.state !== 'writing') throw new Error('Resource generation is not writable');
        const active = tx.objectStore('state').get(activeKey(game));
        active.onsuccess = guarded(() => {
          if (active.result !== source.run) throw new Error('Inherited resource generation changed');
          const refs = tx.objectStore('refs').index('run').openCursor(source.run);
          refs.onsuccess = guarded(() => {
            const row = refs.result;
            if (!row) return;
            if (wanted.has(row.value.path)) tx.objectStore('refs').add({...row.value,run});
            row.continue();
          });
        });
      });
      try { await done; checkCancelled(signal); }
      catch (error) { checkCancelled(signal); throw failure || error; }
      finally { signal?.removeEventListener('abort',abort); }
    },
    async activate(run, input, measurements = {}, {completedAt = Date.now()} = {}) {
      if (!Number.isSafeInteger(completedAt) || completedAt < 0) throw new Error('Invalid installation timestamp');
      const files = filesOf(input);
      const manifestHash = await digest(new TextEncoder().encode(JSON.stringify(files)));
      // No asynchronous hashing inside this transaction: validation and the
      // active pointer switch either all commit or all roll back.
      const [tx,done] = transaction(['generations','refs','chunks','state'],'readwrite');
      const fail = () => tx.abort();
      const pending = tx.objectStore('generations').get(run);
      const value = {run,state:'complete',format:GENERATION_FORMAT,minReader:GENERATION_FORMAT,minWriter:GENERATION_FORMAT,
        files,measurements,manifestHash,completed:completedAt};
      pending.onsuccess = () => {
        if (pending.result?.state !== 'writing') { fail(); return; }
        const references = tx.objectStore('refs').index('run').getAll(run);
        references.onsuccess = () => {
          const expected = new Map();
          for (const file of files) for (let i = 0; i < Math.ceil(file.size / BLOCK_SIZE); i++) {
            expected.set(resourcePath(file.path) + ':' + i, Math.min(BLOCK_SIZE,file.size - i * BLOCK_SIZE));
          }
          if (references.result.length !== expected.size) { fail(); return; }
          for (const ref of references.result) {
            const key = ref.path + ':' + ref.index;
            if (expected.get(key) !== ref.size || !/^[0-9a-f]{64}$/.test(ref.hash)) { fail(); return; }
            expected.delete(key);
            const chunk = tx.objectStore('chunks').getKey(ref.hash);
            chunk.onsuccess = () => { if (chunk.result === undefined) fail(); };
          }
          const previous = tx.objectStore('state').get(activeKey(game));
          previous.onsuccess = () => {
            if (previous.result) tx.objectStore('state').put(previous.result,previousKey(game));
            tx.objectStore('generations').put(value);
            tx.objectStore('state').put(run,activeKey(game));
            tx.objectStore('state').put(true,'needsCollection');
          };
        };
      };
      await done; return value;
    },
    async discard(run) {
      const [tx,done] = transaction(['state','generations','refs'],'readwrite');
      // Any game's active or previous one stays.
      const kept = [];
      const keys = tx.objectStore('state').openCursor();
      const check = () => {
        if (kept.includes(run)) { tx.abort(); return; }
        tx.objectStore('state').put(true,'needsCollection');
        tx.objectStore('generations').delete(run);
        const cursor = tx.objectStore('refs').index('run').openKeyCursor(run);
        cursor.onsuccess = () => {
          const entry = cursor.result;
          if (entry) { tx.objectStore('refs').delete(entry.primaryKey); entry.continue(); }
        };
      };
      keys.onsuccess = () => {
        const row = keys.result;
        if (!row) { check(); return; }
        if (typeof row.key === 'string' && generationKey(row.key) && row.value) kept.push(row.value);
        row.continue();
      };
      await done;
    },
    async collect() {
      // Only an exclusive lease may collect. Preserve active and one previous
      // completed generation; no reader can be using a discarded generation.
      const keep = new Set(await store.liveGenerations());
      for (const item of await all('generations')) if (!keep.has(item.run)) await store.discard(item.run);
      const [tx,done] = transaction(['refs','chunks','state'],'readwrite'), used = new Set();
      const refs = tx.objectStore('refs').openCursor();
      refs.onsuccess = () => {
        const row = refs.result;
        if (row) { used.add(row.value.hash); row.continue(); return; }
        const chunks = tx.objectStore('chunks').openKeyCursor();
        chunks.onsuccess = () => {
          const row = chunks.result;
          if (row) { if (!used.has(row.key)) tx.objectStore('chunks').delete(row.key); row.continue(); }
          else tx.objectStore('state').put(false,'needsCollection');
        };
      };
      await done;
    },
    // Every game's active and previous generation.
    async liveGenerations() {
      const [tx,done] = transaction('state');
      const [keys,values] = await Promise.all([request(tx.objectStore('state').getAllKeys()),
        request(tx.objectStore('state').getAll()),done]);
      return keys.map((key,index) => typeof key === 'string' && generationKey(key) ? values[index] : null).filter(Boolean);
    },
    // The list of games: {current, games:{id:{name,added,played}}}. A
    // database of one game (format 1) has just the first, when installed.
    async games() {
      const value = await get('state','games');
      if (value && typeof value === 'object' && value.games && typeof value.games === 'object') return value;
      const installed = !!(await get('state','active'));
      return {current:FIRST_GAME,games:installed ? {[FIRST_GAME]:{name:'',added:0,played:0}} : {}};
    },
    // Writes [value] (from `games`); the format becomes 2 with a second game.
    async saveGames(value) {
      if (!value || typeof value !== 'object' || !value.games || typeof value.games !== 'object') throw new Error('Invalid list of games');
      for (const id of Object.keys(value.games)) gameId(id);
      gameId(value.current);
      const several = Object.keys(value.games).some(id => id !== FIRST_GAME);
      const [tx,done] = transaction('state','readwrite');
      tx.objectStore('state').put(JSON.parse(JSON.stringify(value)),'games');
      if (several) tx.objectStore('state').put({format:2,minReader:2,minWriter:2},'format');
      await done;
    },
    // A game deleted: its installation goes (collected), its entry too.
    async forgetGame(id) {
      gameId(id);
      const list = await store.games();
      delete list.games[id];
      if (list.current === id) {
        const next = Object.entries(list.games).sort(([,a],[,b]) => (b.played || b.added || 0) - (a.played || a.added || 0))[0];
        list.current = next ? next[0] : FIRST_GAME;
      }
      const [tx,done] = transaction('state','readwrite');
      tx.objectStore('state').delete(activeKey(id));
      tx.objectStore('state').delete(previousKey(id));
      tx.objectStore('state').put(true,'needsCollection');
      await done;
      await store.saveGames(list);
      await store.collect();
      return list;
    },
    async recover() {
      await store.active();
      if (await get('state','needsCollection')) {
        try { await store.collect(); store.cleanupError = null; }
        catch (error) {
          // Collection is retryable housekeeping. A failure must not prevent
          // reading the still-valid active installation after a failed import.
          store.cleanupError = String(error.message || error);
        }
      }
    },
    async read(manifest, file, offset, length) {
      live();
      if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) || offset < 0 || length < 0
          || length > MAX_READ || offset + length > file.size) throw new Error('Invalid local read range');
      let index = fileIndexes.get(manifest);
      if (!index) {
        index = new Map(manifest.files.map(item => [resourcePath(item.path),item]));
        fileIndexes.set(manifest,index);
      }
      const known = index.get(resourcePath(file.path));
      if (!known || known.size !== file.size || manifest.state !== 'complete') throw new Error('Unknown resource file');
      const output = new Uint8Array(length);
      if (!length) return output;
      const first = Math.floor(offset / BLOCK_SIZE), last = Math.floor((offset + length - 1) / BLOCK_SIZE);
      const [tx,done] = transaction(['refs','chunks']);
      const requests = [];
      for (let index = first; index <= last; index++) requests.push(new Promise((resolve,reject) => {
        const ref = tx.objectStore('refs').get([manifest.run,resourcePath(file.path),index]);
        ref.onerror = () => reject(ref.error);
        ref.onsuccess = () => {
          if (!ref.result) { reject(new Error('Missing resource block: ' + file.path)); return; }
          const chunk = tx.objectStore('chunks').get(ref.result.hash);
          chunk.onerror = () => reject(chunk.error);
          chunk.onsuccess = () => resolve({ref:ref.result,chunk:chunk.result,index});
        };
      }));
      const [parts] = await Promise.all([Promise.all(requests),done]);
      for (const {ref,chunk,index} of parts) {
        const expected = Math.min(BLOCK_SIZE,file.size - index * BLOCK_SIZE);
        if (ref.size !== expected || !chunk || chunk.data.length !== expected || await digest(chunk.data) !== ref.hash) {
          throw new Error('Damaged resource block: ' + file.path);
        }
        const start = Math.max(offset,index * BLOCK_SIZE), end = Math.min(offset + length,(index + 1) * BLOCK_SIZE);
        output.set(chunk.data.subarray(start - index * BLOCK_SIZE,end - index * BLOCK_SIZE),start - offset);
      }
      return output;
    },
  };
  try {
    db = await openDatabase(name);
    db.onversionchange = () => { store.close(); };
    checkFormat(await get('state','format'));
    await store.recover();
    return store;
  } catch (error) { await store.close(); throw error; }
}

export async function localFiles(options) {
  const store = await openResourceStore(options);
  try {
    const manifest = await store.active();
    if (!manifest) { await store.close(); return null; }
    const source = {kind:'local',entries:manifest.files,installation:manifest.run,importMode:manifest.measurements?.mode,mouseAsTouch:false,
      game:store.game,prefix:gamePrefix(store.game),
      read:(file,offset,length) => store.read(manifest,file,offset,length),close:() => store.close(),
      async saveExchangeResources() {
        // The engine keeps the resource lease throughout the exchange. Only
        // these two bounded configs are needed by the worker; DATs never move.
        const configs = [];
        for (const file of manifest.files.filter(f => /^(fallout2\.cfg|ddraw\.ini)$/i.test(f.path))) {
          if (file.size > 1024 ** 2) throw new Error('Unexpectedly large game configuration');
          configs.push({path:file.path,bytes:await store.read(manifest,file,0,file.size)});
        }
        return {active:manifest,configs,game:store.game};
      }};
    globalThis.addEventListener?.('pagehide',source.close,{once:true});
    return source;
  } catch (error) { await store.close(); throw error; }
}
