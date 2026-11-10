import {resourcePath} from './resource-store.js';
import {mountLazyUserFiles} from './user-runtime.js';
import {USER_DATABASE} from './user-store.js';
import {digest} from './storage-common.js';
import {decodeConfig,encodeConfig,mergePlayerSettings,configName} from './game-config.js';

export const USER_ROOT = USER_DATABASE;

// Resource bytes are immutable. These categories describe the lifetime of the
// separate writable view; a mod's loose .pro is still an installed resource.
// The game's config under [root] (`configName` of its ddraw.ini), in the case
// the game has it: configs and ddraw.ini are files of the session's MEMFS.
export function gameConfigPath(FS,root) {
  const names = FS.analyzePath(root).exists ? FS.readdir(root) : [];
  const ddraw = names.find(name => name.toLowerCase() === 'ddraw.ini');
  const name = configName(ddraw ? decodeConfig(FS.readFile(root + '/' + ddraw)) : '');
  return name.includes('/') ? name : names.find(item => item.toLowerCase() === name.toLowerCase()) || name;
}

// [path] under [root] with each folder [FS] has in its case there.
function inExistingCase(FS,root,path) {
  let current = root;
  for (const part of path.split('/')) {
    const names = FS.analyzePath(current).exists && FS.isDir(FS.stat(current).mode) ? FS.readdir(current) : [];
    current += '/' + (names.find(name => name.toLowerCase() === part.toLowerCase()) ?? part);
  }
  return current;
}

export function fileLifetime(path) {
  const key = resourcePath(path.replace(/^\/game\//,''));
  if (/(^|\/)savegame(\/|$)/.test(key)) return 'save';
  if (/\.(tmp|bak|log|old)$/.test(key)) return 'temporary';
  if (/\.(cfg|ini)$/.test(key)) return 'settings';
  if (/(^|\/)maps\/.*\.(sav|db|edg)$/.test(key)
      || /(^|\/)proto\/(items|critters)\/.*\.pro$/.test(key)
      || /(^|\/)mods_order\.txt$/.test(key)) return 'session';
  if (/\.(gcd|bio|png|bmp)$/.test(key) && !key.includes('/')) return 'export';
  return 'resource';
}

export function patchDirectories(config) {
  let system = false;
  const roots = new Set(['data']);
  for (const raw of config.split(/\r?\n/)) {
    const line = raw.trim();
    if (line.startsWith('[')) { system = /^\[system\]$/i.test(line); continue; }
    if (!system) continue;
    const match = /^(master_patches|critter_patches)\s*=\s*(.*?)\s*$/i.exec(line);
    if (!match) continue;
    const path = match[2].replaceAll('\\','/').replace(/\/$/,'');
    if (!path || path === '.') { roots.add(''); continue; }
    roots.add(resourcePath(path));
  }
  return [...roots];
}

async function settingsFiles(FS, root, user) {
  const files = new Map();
  let total = 0;
  const run = action => user ? user.run(action) : action();
  async function walk(directory, relative = '') {
    for (const name of await run(() => FS.readdir(directory))) {
      if (name === '.' || name === '..') continue;
      const path = directory + '/' + name, key = relative + name, stat = await run(() => FS.lstat(path));
      if (FS.isDir(stat.mode)) { await walk(path,key + '/'); continue; }
      if (!FS.isFile(stat.mode) || fileLifetime(key) !== 'settings') continue;
      total += stat.size;
      if (stat.size > 2 * 1024 ** 2 || total > 8 * 1024 ** 2) throw new Error('Unexpectedly large settings');
      files.set(key,user?.readFile ? await user.readFile(path) : FS.readFile(path));
    }
  }
  await walk(root); return files;
}
async function writeSettings(FS, root, files, user) {
  const run = action => user ? user.run(action) : action();
  const existing = await settingsFiles(FS,root,user);
  for (const key of existing.keys()) if (!files.has(key)) await run(() => FS.unlink(root + '/' + key));
  for (const [key,data] of files) {
    const old = existing.get(key);
    if (old?.length === data.length && old.every((value,index) => value === data[index])) continue;
    const path = root + '/' + key;
    await run(() => FS.mkdirTree(path.slice(0,path.lastIndexOf('/'))));
    if (user?.exists ? await user.exists(path) : await run(() => FS.analyzePath(path).exists)) await run(() => FS.unlink(path));
    if (user?.writeFile) await user.writeFile(path,data); else FS.writeFile(path,data);
  }
}

// The game's config (`configName` of its ddraw.ini) among [templates] and
// the saved one whose player's settings it keeps (named by the saved
// ddraw.ini: another game's, as Resurrection's fores.cfg before Fallout 2's
// fallout2.cfg). Maps of paths to bytes; the paths found or undefined.
export function configsToMerge(templates,saved) {
  const nameIn = files => {
    const ddraw = [...files.keys()].find(path => path.toLowerCase() === 'ddraw.ini');
    return configName(ddraw ? decodeConfig(files.get(ddraw)) : '').toLowerCase();
  };
  const name = nameIn(templates), oldName = nameIn(saved);
  return {config:[...templates.keys()].find(path => path.toLowerCase() === name),
    old:[...saved.keys()].find(path => path.toLowerCase() === oldName)};
}

export async function mountUserFiles(module,{prefix = ''} = {}) {
  if (!module.UserIO) throw new Error('The engine and user filesystem versions do not match; reload the application');
  return mountLazyUserFiles(module,{prefix});
}

export async function connectUserFiles(module, user, source = {}) {
  const FS = module.FS;
  const templates = await settingsFiles(FS,'/game');
  const hashes = Object.fromEntries(await Promise.all([...templates].map(async ([path,data]) => [path.toLowerCase(),await digest(data)])));
  // The game's own part of the storage (its root for the first game).
  const gameRoot = user.gameRoot || user.root;
  const provenance = gameRoot + '/port/settings-sources.json';
  user.captureSettings = async () => {
    await writeSettings(FS,gameRoot + '/settings',await settingsFiles(FS,'/game'),user);
    if (user.writeFile) await user.writeFile(provenance,new TextEncoder().encode(JSON.stringify(hashes)));
    else FS.writeFile(provenance,JSON.stringify(hashes));
    if (source.installation) await user.writeFile(gameRoot + '/port/resource-installation.txt',new TextEncoder().encode(source.installation));
  };
  user.restoreSettings = async () => {
    const saved = await settingsFiles(FS,gameRoot + '/settings',user);
    const combined = new Map(templates);
    const templateKeys = new Map([...templates.keys()].map(path => [path.toLowerCase(),path]));
    const hasProvenance = user.exists ? await user.exists(provenance) : await user.run(() => FS.analyzePath(provenance).exists);
    if (hasProvenance && (await user.run(() => FS.stat(provenance))).size > 1024 ** 2) throw new Error('Invalid settings source metadata');
    const previousHashes = hasProvenance ? JSON.parse(user.readFile ? new TextDecoder().decode(await user.readFile(provenance)) : FS.readFile(provenance,{encoding:'utf8'})) : {};
    // Retired mod configs must not reappear after a composition replacement.
    for (const [path,data] of saved) {
      const key = path.toLowerCase(), target = templateKeys.get(key);
      if (target && (previousHashes[key] === hashes[key] || (!hasProvenance && !source.importMode))) combined.set(target,data);
    }
    const {config,old} = configsToMerge(templates,saved);
    if (config && old) combined.set(config,encodeConfig(mergePlayerSettings(decodeConfig(templates.get(config)),decodeConfig(saved.get(old)))));
    await writeSettings(FS,'/game',combined);
  };
  await user.restoreSettings();
  const cfg = '/game/' + gameConfigPath(FS,'/game');
  const config = FS.analyzePath(cfg).exists ? FS.readFile(cfg,{encoding:'utf8'}) : '';
  // SAVEGAME and its staging/backup/catalog directories share one mounted FS:
  // native rename operations keep their normal atomic-directory semantics.
  for (const root of new Set(['',...patchDirectories(config)])) {
    // In the game's folders' case: CE sees one of "DATA/" and "data/".
    const path = inExistingCase(FS,'/game',(root ? root + '/' : '') + 'savegame');
    FS.mkdirTree(path.slice(0,path.lastIndexOf('/')));
    if (FS.analyzePath(path).exists) throw new Error('Game resources collide with user saves');
    FS.symlink(gameRoot + '/savegame',path);
  }
  return user;
}
