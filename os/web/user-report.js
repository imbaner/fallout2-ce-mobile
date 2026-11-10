import {USER_DATABASE,USER_DB_VERSION} from './user-store.js';
import {BLOCK_SIZE,digest} from './storage-common.js';
const request = r => new Promise((resolve,reject) => { r.onsuccess = () => resolve(r.result); r.onerror = () => reject(r.error); });
async function open(name) {
  const r = indexedDB.open(name); let absent = false;
  r.onupgradeneeded = () => { absent = true; r.transaction.abort(); };
  try { return await request(r); } catch (error) { if (!absent) throw error; return null; }
}
const get = (db,store,key) => request(db.transaction(store).objectStore(store).get(key));
const lookup = (db,parent,name) => request(db.transaction('nodes').objectStore('nodes').index('parent').get([parent,name.toLowerCase()]));
async function resolve(db,path) {
  let node = await get(db,'nodes','root');
  for (const name of path.split('/').filter(Boolean)) { if (!node) return; node = await lookup(db,node.id,name); }
  return node;
}
// Read only: settings/font queries never create, upgrade, or reset the DB and
// may run while a reviewed import owns the writer lease.
export async function userSettingText(path,{name = USER_DATABASE} = {}) {
  const db = await open(name); if (!db) return '';
  try {
    if (db.version === USER_DB_VERSION && db.objectStoreNames.contains('nodes')) {
      const format = await get(db,'state','format'); if (format?.format !== 3) return '';
      const node = await resolve(db,path); if (!node) return '';
      if (node.size > 2 * 1024 ** 2) throw new Error('Unexpectedly large settings file');
      const bytes = new Uint8Array(node.size);
      for (let at = 0; at < bytes.length; at += BLOCK_SIZE) {
        const ref = await get(db,'refs',[node.content,at / BLOCK_SIZE]), chunk = ref && await get(db,'chunks',ref.hash);
        if (!chunk || chunk.length !== ref.size || await digest(chunk) !== ref.hash) throw new Error('Corrupt settings block');
        bytes.set(chunk,at);
      }
      return new TextDecoder('windows-1251').decode(bytes);
    }
    return '';
  } finally { db.close(); }
}

// Read only, as `userSettingText`: a file's bytes (the list of games: a
// game's card), null when there's none or the storage isn't there.
export async function userFileBytes(path,{name = USER_DATABASE,limit = 8 * 1024 ** 2} = {}) {
  const db = await open(name); if (!db) return null;
  try {
    if (db.version !== USER_DB_VERSION || !db.objectStoreNames.contains('nodes')) return null;
    const format = await get(db,'state','format'); if (format?.format !== 3) return null;
    const node = await resolve(db,path); if (!node || !node.content) return null;
    if (node.size > limit) throw new Error('Unexpectedly large user file');
    const bytes = new Uint8Array(node.size);
    for (let at = 0; at < bytes.length; at += BLOCK_SIZE) {
      const ref = await get(db,'refs',[node.content,at / BLOCK_SIZE]), chunk = ref && await get(db,'chunks',ref.hash);
      if (!chunk || chunk.length !== ref.size || await digest(chunk) !== ref.hash) throw new Error('Corrupt user file block');
      bytes.set(chunk,at);
    }
    return bytes;
  } finally { db.close(); }
}

// The names in a folder (the list of games: a game's save slots), [] when
// there's none.
export async function userFolderNames(path,{name = USER_DATABASE} = {}) {
  const db = await open(name); if (!db) return [];
  try {
    if (db.version !== USER_DB_VERSION || !db.objectStoreNames.contains('nodes')) return [];
    const format = await get(db,'state','format'); if (format?.format !== 3) return [];
    const node = await resolve(db,path); if (!node) return [];
    const range = IDBKeyRange.bound([node.id,''],[node.id,'￿']);
    const children = await request(db.transaction('nodes').objectStore('nodes').index('parent').getAll(range));
    return children.map(child => child.name);
  } finally { db.close(); }
}
