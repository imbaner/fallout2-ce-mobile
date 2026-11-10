import {lazyUserFilesystem} from './user-lazy-filesystem.js';
import {USER_DATABASE} from './user-store.js';
import {BLOCK_SIZE} from './storage-common.js';

// [options.prefix]: the game's part of the storage (`gamePrefix`): its
// savegame, settings, port and exports folders under `gameRoot`.
export async function mountLazyUserFiles(module,options = {}) {
  const FS = module.FS, backend = await lazyUserFilesystem(module,options), root = USER_DATABASE;
  FS.mkdirTree(root); FS.mount(backend.type,{},root);
  const gameRoot = options.prefix ? root + '/' + options.prefix.replace(/\/$/,'') : root;
  if (options.prefix) {
    // A new game's folders, kept by the next checkpoint (its first save or
    // settings).
    for (const folder of ['savegame','settings','port','exports']) await backend.run(() => FS.mkdirTree(gameRoot + '/' + folder));
  }
  let active = false, blocked = false, error = null, commits = 0;
  const notify = (name,value) => { try { module[name]?.(value); } catch (e) { console.error('Storage notice failed',e); } };
  const close = () => { globalThis.removeEventListener?.('pagehide',close); return backend.close(); };
  globalThis.addEventListener?.('pagehide',close,{once:true});
  async function operation(action) {
    if (active || blocked || backend.closed || backend.suspended) throw new Error('User storage is not available for a game write');
    active = true; try { return await action(); } finally { active = false; }
  }
  async function fileIO(path,data) {
    const stream = await backend.run(() => FS.open(path,data ? 'w' : 'r'));
    try {
      const size = data ? data.length : stream.node.user.size;
      // Only the explicit JS settings/metadata helpers allocate a whole small
      // file. Engine I/O and archive payloads never call this helper.
      if (!data && size > 8 * 1024 ** 2) throw new Error('User metadata exceeds its budget');
      const bytes = data || new Uint8Array(size);
      for (let at = 0; at < size;) {
        const count = Math.min(BLOCK_SIZE - at % BLOCK_SIZE,size - at);
        const done = await backend.run(() => data ? FS.write(stream,bytes,at,count) : FS.read(stream,bytes,at,count));
        if (done !== count) throw new Error('Incomplete user metadata I/O'); at += done;
      }
      return bytes;
    } finally { FS.close(stream); }
  }
  const user = {
    root,gameRoot,close,run:backend.run,readFile:path => fileIO(path),writeFile:(path,data) => fileIO(path,data),
    exists:path => backend.run(() => { try { FS.lookupPath(path); return true; } catch (e) { if (e.errno === 44) return false; throw e; } }),
    status:() => ({format:3,commits,blocked,error,suspended:backend.suspended,...backend.stats()}),
    suspend:() => operation(() => backend.suspend()),
    // Reacquire before the suspended WASM stack may execute another syscall.
    // A failure leaves it suspended and can be retried from the exchange UI.
    resume:() => backend.resume(),
    commit:() => operation(async () => {
      try {
        await backend.prepareSettings(); await user.captureSettings?.(); await backend.checkpoint(); commits++; error = null;
        notify('onUserWriteSuccess');
      } catch (failure) {
        error = String(failure.message || failure);
        try { if (backend.closed) throw failure; await backend.restore(); await user.restoreSettings?.(); }
        catch { blocked = true; }
        notify('onUserWriteFailure',{fatal:blocked,kind:failure.name,message:error}); throw failure;
      }
    }),
    rollback:() => operation(async () => {
      try { await backend.restore(); await user.restoreSettings?.(); }
      catch (failure) { blocked = true; notify('onUserWriteFailure',{fatal:true,kind:failure.name,message:String(failure)}); throw failure; }
    }),
  };
  user.sync = user.commit; return user;
}
