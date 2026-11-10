import {fileLifetime, mountUserFiles, connectUserFiles, gameConfigPath} from './working-files.js';
import {decodeConfig,encodeConfig,fixGameConfig} from './game-config.js';
// A range-reader contract independent of the persistent storage backend.
// The development adapter is opt-in and reads the server's game folder only.
export function isDevelopmentHost(hostname) {
  if (['localhost', '[::1]'].includes(hostname)) return true;
  const parts = hostname.split('.');
  if (parts.length !== 4 || parts.some(part => !/^\d{1,3}$/.test(part) || Number(part) > 255)) return false;
  const [a, b] = parts.map(Number);
  return a === 127 || a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
}

export async function developmentFiles() {
  const location = new URL(window.location.href);
  if (!location.searchParams.has('dev') || !isDevelopmentHost(location.hostname)) {
    return null;
  }
  const base = new URL('./', location);
  const response = await fetch(new URL('__dev__/game.json', base), {cache:'no-store'});
  if (response.status === 404) return null;
  if (!response.ok) throw new Error('Cannot read the development game manifest.');
  const manifest = await response.json();
  if (manifest.schema !== 1 || manifest.readOnly !== true || !Array.isArray(manifest.files)) {
    throw new Error('Invalid development game manifest.');
  }
  const seen = new Set();
  for (const file of manifest.files) {
    if (typeof file.path !== 'string' || file.path.includes('\\')
        || file.path.split('/').some(part => !part || part === '.' || part === '..')
        || !Number.isSafeInteger(file.size) || file.size < 0 || seen.has(file.path.toLowerCase())) {
      throw new Error('Invalid or duplicate game path.');
    }
    seen.add(file.path.toLowerCase());
  }
  // master.dat and critter.dat may be folders too (CE opens both).
  const has = name => seen.has(name) || [...seen].some(path => path.startsWith(name + '/'));
  if (!has('master.dat') || !has('critter.dat')) throw new Error('The game is incomplete.');
  return {
    entries:manifest.files,
    mouseAsTouch:location.searchParams.get('mouse') === '1',
    async read(file, offset, length) {
      if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) || offset < 0 || length < 0 || offset + length > file.size) {
        throw new Error('Invalid game read range.');
      }
      if (length === 0) return new Uint8Array();
      const path = file.path.split('/').map(encodeURIComponent).join('/');
      const response = await fetch(new URL('__dev__/game/' + path, base), {
        headers:{Range:`bytes=${offset}-${offset + length - 1}`}, cache:'no-store',
      });
      if (response.status !== 206) throw new Error('The development server did not return a byte range.');
      const data = new Uint8Array(await response.arrayBuffer());
      if (data.length !== length) throw new Error('The game file changed or was truncated.');
      return data;
    },
  };
}

export async function mountGame(module, source) {
  const FS = module.FS;
  // Development sessions get their own temporary saves, never the real local
  // user's store. Every permanent/local launch already holds the writer lease.
  const user = source.kind === 'local' ? await mountUserFiles(module,{prefix:source.prefix || ''}) : null;
  module.webUserFiles = user;
  module.webStorageMode = user ? 'persistent' : 'temporary';
  const root = '/game';
  FS.mkdirTree(root);
  let settingsBytes = 0;
  for (const entry of source.entries) {
    const lifetime = fileLifetime(entry.path);
    if (lifetime === 'save' || lifetime === 'temporary'
        || /(^|\/)maps\/.*\.(sav|db)$/i.test(entry.path)) continue;
    const path = root + '/' + entry.path;
    FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
    // Root configs are writable in this session. The source is always read-only.
    if (lifetime === 'settings' || (!entry.path.includes('/') && /\.txt$/i.test(entry.path))) {
      settingsBytes += entry.size;
      if (entry.size > 2 * 1024 * 1024 || settingsBytes > 8 * 1024 * 1024) throw new Error('Unexpectedly large game configuration.');
      FS.writeFile(path, await source.read(entry, 0, entry.size));
    } else {
      module.GameFS.createFile(path, entry.size, (offset, length) => source.read(entry, offset, length));
    }
  }
  // The config ddraw.ini names (a mod's own, as Resurrection's fores.cfg).
  const config = root + '/' + gameConfigPath(FS,root);
  const original = FS.analyzePath(config).exists ? decodeConfig(FS.readFile(config)) : '[system]\nmaster_dat=master.dat\ncritter_dat=critter.dat\nmaster_patches=data\ncritter_patches=data\nlanguage=english\n';
  FS.writeFile(config,encodeConfig(fixGameConfig(original)));
  // The engine's own assets always come from the same bundle as JS and WASM.
  const response = await fetch(new URL('./ce.dat', import.meta.url));
  if (!response.ok) throw new Error('Cannot load ce.dat.');
  FS.writeFile(root + '/ce.dat', new Uint8Array(await response.arrayBuffer()));
  module.webUserFiles = user ? await connectUserFiles(module,user,source) : null;
  FS.chdir(root);
}
