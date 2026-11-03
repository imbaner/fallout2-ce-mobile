// Emscripten 4.0.4 adapter. Retain the SDK's syscall implementations and errno,
// varargs and i53 handling; only add suspension for lazy user VFS cache misses.
// Loaded after gamefs.js, so resource fd_read remains the ordinary fallback.
addToLibrary({
  $UserIO__deps: ['$FS','$SYSCALLS'],
  $UserIO__postset: 'Module["UserIO"] = UserIO;',
  $UserIO: {
    backend:null,
    retry(action,sign) {
      if (Asyncify.state !== Asyncify.State.Normal) return Asyncify.handleSleep(() => {});
      try { const result = action(); UserIO.backend?.prune(); return result; }
      catch (error) {
        if (!error?.userIO) throw error;
        return Asyncify.handleAsync(async () => {
          for (;;) {
            try { await error.promise; const result = action(); UserIO.backend?.prune(); return result; }
            catch (next) {
              if (next?.userIO) { error = next; continue; }
              console.error('User filesystem I/O failed',next);
              return sign * (next?.errno || (next?.name === 'QuotaExceededError' ? {{{ cDefs.ENOSPC }}} : {{{ cDefs.EIO }}}));
            }
          }
        });
      }
    },
    io(fd,iov,count,pnum,writing) {
      if (Asyncify.state !== Asyncify.State.Normal) return Asyncify.handleSleep(() => {});
      const stream = SYSCALLS.getStreamFromFD(fd);
      return Asyncify.handleAsync(async () => {
        let total = 0;
        try {
          for (let i = 0; i < count; i++) {
            const ptr = {{{ makeGetValue('iov', `i * ${C_STRUCTS.iovec.__size__} + ${C_STRUCTS.iovec.iov_base}`, '*') }}};
            const length = {{{ makeGetValue('iov', `i * ${C_STRUCTS.iovec.__size__} + ${C_STRUCTS.iovec.iov_len}`, '*') }}};
            let done = 0;
            while (done < length) {
              const wanted = Math.min(length - done,256 * 1024 - stream.position % (256 * 1024));
              const n = await UserIO.backend.run(() => writing ? FS.write(stream,HEAPU8,ptr + done,wanted) : FS.read(stream,HEAPU8,ptr + done,wanted));
              total += n; done += n; if (!n || n < wanted) break;
            }
          }
          {{{ makeSetValue('pnum', 0, 'total', SIZE_TYPE) }}};
          return 0;
        } catch (error) {
          if (total) { {{{ makeSetValue('pnum', 0, 'total', SIZE_TYPE) }}}; return 0; }
          console.error('User file read/write failed',error);
          return error?.errno || (error?.name === 'QuotaExceededError' ? {{{ cDefs.ENOSPC }}} : {{{ cDefs.EIO }}});
        } finally { UserIO.backend.prune(); }
      });
    },
    dents(fd,dirp,count) {
      if (Asyncify.state !== Asyncify.State.Normal) return Asyncify.handleSleep(() => {});
      const stream = SYSCALLS.getStreamFromFD(fd);
      return Asyncify.handleAsync(async () => {
        try {
          const size = {{{ C_STRUCTS.dirent.__size__ }}}, offset = Math.floor(stream.position / size);
          if (!stream.userDirectory || stream.userDirectory.index !== offset) {
            const iterator = (async function* () {
              yield {name:'.',mode:0o040000}; yield {name:'..',mode:0o040000};
              yield* UserIO.backend.childEntries(stream.node);
            })();
            stream.userDirectory = {iterator,index:0};
            while (stream.userDirectory.index < offset) {
              if ((await iterator.next()).done) break;
              stream.userDirectory.index++;
            }
          }
          const directory = stream.userDirectory; let pos = 0;
          while (pos + size <= count) {
            const next = await directory.iterator.next(); if (next.done) break;
            const item = next.value, type = FS.isDir(item.mode) ? 4 : 8;
            const ino = stream.node.id, nextOffset = (++directory.index) * size;
            {{{ makeSetValue('dirp + pos', C_STRUCTS.dirent.d_ino, 'ino', 'i64') }}};
            {{{ makeSetValue('dirp + pos', C_STRUCTS.dirent.d_off, 'nextOffset', 'i64') }}};
            {{{ makeSetValue('dirp + pos', C_STRUCTS.dirent.d_reclen, C_STRUCTS.dirent.__size__, 'i16') }}};
            {{{ makeSetValue('dirp + pos', C_STRUCTS.dirent.d_type, 'type', 'i8') }}};
            stringToUTF8(item.name,dirp + pos + {{{ C_STRUCTS.dirent.d_name }}},256); pos += size;
          }
          stream.position = directory.index * size; return pos;
        } catch (error) { console.error('User directory read failed',error); return -(error?.errno || {{{ cDefs.EIO }}}); }
      });
    },
  },
});

// Reuse the already wrapped SDK bodies (wrapSyscallFunction has run). These
// aliases are local JS helpers: i64 conversion stays on the public syscall.
// Every path syscall that can reach the user VFS must be here: an unwrapped
// one lets a cache miss escape Asyncify and the engine never resumes (libc
// access() is faccessat).
for (const symbol of ['__syscall_openat','__syscall_mkdirat','__syscall_mknodat',
  '__syscall_newfstatat','__syscall_stat64','__syscall_lstat64','__syscall_fstat64',
  '__syscall_getdents64','__syscall_unlinkat','__syscall_renameat','__syscall_truncate64',
  '__syscall_ftruncate64','__syscall_readlinkat','__syscall_utimensat','__syscall_chmod','__syscall_fchmod',
  '__syscall_fchdir','__syscall_chdir','__syscall_rmdir','__syscall_faccessat']) {
  const original = LibraryManager.library[symbol];
  // A name the pinned SDK doesn't have is a mistake here, not an optional one.
  if (!original) throw new Error('userfs.js: no SDK syscall ' + symbol);
  const alias = 'UserNative' + symbol, additions = {};
  additions['$' + alias] = original;
  additions['$' + alias + '__deps'] = LibraryManager.library[symbol + '__deps'] || [];
  additions[symbol] = new Function('a','b','c','d','e','f','return UserIO.retry(() => ' + alias + '(a,b,c,d,e,f),-1)');
  additions[symbol + '__deps'] = ['$UserIO','$' + alias]; additions[symbol + '__async'] = true;
  addToLibrary(additions);
}
// libc directory enumeration streams metadata without creating FS nodes or a
// complete array of names. Ordinary MEMFS/resource directories use the SDK.
addToLibrary({
  __syscall_getdents64__deps:['$UserIO','$UserNative__syscall_getdents64','$stringToUTF8'],
  __syscall_getdents64__async:true,
  __syscall_getdents64:(fd,dirp,count) => {
    if (Asyncify.state !== Asyncify.State.Normal) return Asyncify.handleSleep(() => {});
    if (FS.getStream(fd)?.node.user) return UserIO.dents(fd,dirp,count);
    return UserNative__syscall_getdents64(fd,dirp,count);
  },
});
for (const symbol of ['fd_read','fd_write']) {
  const alias = 'UserNative_' + symbol, additions = {};
  additions['$' + alias] = LibraryManager.library[symbol];
  additions['$' + alias + '__deps'] = LibraryManager.library[symbol + '__deps'] || [];
  additions[symbol] = new Function('fd','iov','count','pnum',
    'if (Asyncify.state !== Asyncify.State.Normal) return Asyncify.handleSleep(() => {});' +
    'if (FS.getStream(fd)?.node.user) return UserIO.io(fd,iov,count,pnum,' + (symbol === 'fd_write') + ');' +
    'return ' + alias + '(fd,iov,count,pnum);');
  additions[symbol + '__deps'] = ['$UserIO','$' + alias]; additions[symbol + '__async'] = true;
  addToLibrary(additions);
}
