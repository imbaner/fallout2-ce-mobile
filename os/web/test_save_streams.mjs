import 'fake-indexeddb/auto';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdtemp,rm,open} from 'node:fs/promises';
import {openAsBlob,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {execFileSync} from 'node:child_process';
import {prepareSaveZip,exportSaveZip,forgetGameFiles,forgetGamePicture} from './save-streams.js';
import {openUserStore} from './user-store.js';
import {slotName,saveIdentity,recordText} from './save-format.js';
import {ZipOutput,crc32} from './zip-output.js';
import {zeroCrc} from './fixtures/zip-crc.mjs';
import {readSaveArchive} from './save-zip-reader.js';
const encode = text => new TextEncoder().encode(text), input = data => ({size:data.length,read:async (at,n) => data.subarray(at,at + n)});
const available = existsSync(new URL('../../out/web/engine/fallout2-archive.wasm',import.meta.url));
const create = available ? (await import('../../out/web/engine/fallout2-archive.js')).default : null;
const binary = available ? await readFile(new URL('../../out/web/engine/fallout2-archive.wasm',import.meta.url)) : null;
const archiveTest = (name,fn) => test(name,{skip:!available},fn);
const moduleFactory = options => create({...options,wasmBinary:binary});
const name = () => 'stream-saves-' + crypto.randomUUID();
const config = encode('[system]\nlanguage=english\n[ui]\nauto_quick_save=1\nauto_quick_save_page=1\n');
const owner = () => ({active:async () => ({run:'resources',files:[{path:'fallout2.cfg',size:config.length}]}),read:async (_a,_b,at,n) => config.subarray(at,at + n),close:async () => {}});
async function sink(path,{sparseBlock} = {}) {
  const file = await open(path,'w+'); let position = 0, closed = false;
  return {get position() { return position; },async write(bytes) { if (bytes !== sparseBlock) { const {bytesWritten} = await file.write(bytes,0,bytes.length,position); assert.equal(bytesWritten,bytes.length); } position += bytes.length; },
    async read(at,n) { const bytes = new Uint8Array(n); assert.equal((await file.read(bytes,0,n,at)).bytesRead,n); return bytes; },
    async close() { if (!closed) { closed = true; await file.close(); } }};
}
async function temporary(directory) {
  const path = join(directory,crypto.randomUUID() + '.zip'), central = path + '.directory';
  const output = await sink(path), dir = await sink(central);
  return {output,directory:dir,async file() { await output.close(); await dir.close(); return openAsBlob(path); },async discard() { await output.close(); await dir.close(); await rm(path,{force:true}); await rm(central,{force:true}); }};
}
async function workspace(t) { const path = await mkdtemp(join(tmpdir(),'save-streams-')); t.after(() => rm(path,{recursive:true,force:true})); return path; }
function header(n) { const bytes = new Uint8Array(2000); bytes.set(encode('FALLOUT SAVE FILE')); bytes.set(encode('slot ' + n),61); return bytes; }
archiveTest('streaming decoder accepts UTF-8 and CP866 roots and reuses native composition comparisons',async () => {
  let compare;
  for (const cp866 of [false,true]) {
    const table = {'Т':0x92,'Е':0x85,'С':0x91};
    const options = cp866 ? {useUnicodeFileNames:false,encodeText:(text,type) => type === 'filename' ? Uint8Array.from(text,c => table[c] ?? c.charCodeAt(0)) : undefined} : {};
    const {ZipWriter,Uint8ArrayWriter,Uint8ArrayReader} = globalThis.zip;
    const writer = new ZipWriter(new Uint8ArrayWriter(),{useWebWorkers:false,...options});
    await writer.add('ТЕСТ/SAVEGAME/SLOT01/SAVE.DAT',new Uint8ArrayReader(header(1)),{level:6});
    const database = name(), plan = await prepareSaveZip(new Blob([await writer.close()]),{userName:database,storeFactory:owner,moduleFactory});
    assert.equal(plan.summary.root,'ТЕСТ/SAVEGAME/'); await plan.dispose();
    const store = await openUserStore({name:database}), job = await store.begin('codec');
    for await (const _ of readSaveArchive(new Blob([await readFile(new URL('./fixtures/android-exchange.zip',import.meta.url))]),{
      moduleFactory,catalog:store.archiveCatalog(job),onComparer:value => { compare = value; },
    })) {}
    await store.discard(job); await store.close();
  }
  assert.equal(compare('mods/rpu.dat=old:L','mods/rpu.dat=old:L').level,0);
  assert.deepEqual(compare('mods/rpu.dat=old:L','mods/rpu.dat=new:L'),{level:1,changes:[['1','mods/rpu.dat']]});
  assert.equal(compare('mods/rpu.dat=old:L','').level,2); assert.equal(compare('','mods/rpu.dat=new:L').level,3);
});
archiveTest('real Android ZIP streams into staging, imports, duplicate review and full replacement retain native identity',async t => {
  const database = name(), file = new Blob([await readFile(new URL('./fixtures/android-exchange.zip',import.meta.url))]);
  let plan = await prepareSaveZip(file,{userName:database,storeFactory:owner,moduleFactory});
  assert.equal(plan.summary.found,1); assert.equal(plan.summary.added,1); await plan.install();
  plan = await prepareSaveZip(file,{userName:database,storeFactory:owner,moduleFactory});
  assert.equal(plan.summary.skipped,1); assert.equal(plan.summary.added,0); await plan.install({replaceAll:true});
  const store = await openUserStore({name:database});
  const root = await store.resolve('savegame'), folder = await store.lookup(root.id,'SLOT01'), dat = await store.lookup(folder.id,'save.dat');
  const data = await store.read(dat.content,0,dat.size); assert.equal(saveIdentity(data),'f038a8a19aee2d74-2000');
  const port = await store.resolve('port'), records = await store.lookup(port.id,'saves.txt');
  assert.match(new TextDecoder().decode(await store.read(records.content,0,records.size)),/mods\/rpu.dat=0123:L/); await store.close();
  const dir = await workspace(t), result = await exportSaveZip({userName:database,storeFactory:owner,temporary:() => temporary(dir)});
  const exported = join(dir,'exported.zip'); await (await import('node:fs/promises')).writeFile(exported,new Uint8Array(await result.blob.arrayBuffer()));
  assert.match(execFileSync('python3',['-c','import sys,zipfile\nwith zipfile.ZipFile(sys.argv[1]) as z:\n assert z.testzip() is None; assert z.read("SAVEGAME/SLOT01/SAVE.DAT")[:17]==b"FALLOUT SAVE FILE"; print("native ZIP OK")',exported],{encoding:'utf8'}),/native ZIP OK/);
});
archiveTest('export backs up damaged SAVE.DAT slots too, matching native inventory without silently dropping bytes',async t => {
  const database = name(), store = await openUserStore({name:database}), job = await store.begin();
  await store.putFile(job,'savegame/SLOT01/SAVE.DAT',input(header(1)));
  await store.putFile(job,'savegame/SLOT02/SAVE.DAT',input(Uint8Array.of(1,2,3)));
  await store.commit(job); await store.close();
  const directory = await workspace(t), result = await exportSaveZip({userName:database,storeFactory:owner,temporary:() => temporary(directory)});
  assert.equal(result.slots,2);
  const archiveStore = await openUserStore({name:name()}), scan = await archiveStore.begin('archive'); let recovered = false;
  for await (const entry of readSaveArchive(result.blob,{moduleFactory,catalog:archiveStore.archiveCatalog(scan)})) {
    for await (const chunk of entry.chunks()) if (entry.path === 'SAVEGAME/SLOT02/SAVE.DAT') { assert.deepEqual(chunk,Uint8Array.of(1,2,3)); recovered = true; }
  }
  assert.ok(recovered); await archiveStore.discard(scan); await archiveStore.close();
});
archiveTest('1000 slots export as one ZIP and restore all manual/quick slots with a disk metadata catalog',async t => {
  const database = name(), store = await openUserStore({name:database}), job = await store.begin(), records = new Map();
  for (let n = 1; n <= 1000; n++) {
    const bytes = header(n); await store.putFile(job,'savegame/' + slotName(n) + '/SAVE.DAT',input(bytes));
    await store.putFile(job,'savegame/' + slotName(n) + '/maps/small.sav',input(Uint8Array.of(n % 251)));
    records.set(n,{id:saveIdentity(bytes),created:String(n),order:String(n),composition:'mods/rpu.dat=0123:L',quick:n >= 11 && n < 21});
  }
  await store.putFile(job,'port/saves.txt',input(encode(recordText(records,{exported:false})))); await store.commit(job); await store.close();
  const directory = await workspace(t), result = await exportSaveZip({userName:database,storeFactory:owner,temporary:() => temporary(directory)});
  assert.equal(result.slots,1000);
  const target = name(), plan = await prepareSaveZip(result.blob,{userName:target,storeFactory:owner,moduleFactory});
  assert.equal(plan.summary.found,1000); assert.equal(plan.summary.addError,'slot_limit'); assert.equal(plan.summary.replaceError,undefined);
  await plan.install({replaceAll:true}); const reopened = await openUserStore({name:target});
  const root = await reopened.resolve('savegame'); let count = 0;
  for await (const folder of reopened.children(root.id)) { const dat = await reopened.lookup(folder.id,'SAVE.DAT'); assert.equal(dat.size,2000); count++; }
  assert.equal(count,1000); await reopened.close();
});
archiveTest('CRC damage, truncated central directory and mismatched names never publish a save',async t => {
  const original = new Uint8Array(await readFile(new URL('./fixtures/android-exchange.zip',import.meta.url)));
  const damaged = original.slice(); damaged[100] ^= 0xff;
  const variants = [new Blob([damaged]),new Blob([original.subarray(0,original.length - 1)])];
  const mismatch = original.slice(); const view = new DataView(mismatch.buffer);
  for (let at = 0; at + 46 < mismatch.length; at++) if (view.getUint32(at,true) === 0x02014b50) { mismatch[at + 46] ^= 1; break; }
  variants.push(new Blob([mismatch]));
  for (const file of variants) {
    const database = name(); await assert.rejects(prepareSaveZip(file,{userName:database,storeFactory:owner,moduleFactory}));
    const store = await openUserStore({name:database}), root = await store.resolve('savegame'); let count = 0;
    for await (const _ of store.children(root.id)) count++; assert.equal(count,0); await store.close();
  }
});
archiveTest('cancelled extraction and rejected replacement activation preserve existing records and payload',async t => {
  const database = name(), file = new Blob([await readFile(new URL('./fixtures/android-exchange.zip',import.meta.url))]);
  let plan = await prepareSaveZip(file,{userName:database,storeFactory:owner,moduleFactory}); await plan.install();
  const c = new AbortController();
  await assert.rejects(prepareSaveZip(file,{userName:database,storeFactory:owner,moduleFactory,signal:c.signal,progress:() => c.abort()}),e => e.name === 'AbortError');
  plan = await prepareSaveZip(file,{userName:database,storeFactory:owner,moduleFactory});
  const original = IDBObjectStore.prototype.put;
  IDBObjectStore.prototype.put = function(value,key) { if (this.name === 'nodes' && value.name === 'SAVE.DAT') throw new DOMException('quota','QuotaExceededError'); return original.call(this,value,key); };
  try { await assert.rejects(plan.install({replaceAll:true}),e => e.name === 'QuotaExceededError'); } finally { IDBObjectStore.prototype.put = original; }
  plan = await prepareSaveZip(file,{userName:database,storeFactory:owner,moduleFactory}); assert.equal(plan.summary.skipped,1); await plan.dispose();
});
archiveTest('a 129 MiB incompressible save exports and imports without a whole-archive allocation',async t => {
  const database = name(), store = await openUserStore({name:database}), job = await store.begin();
  const block = new Uint8Array(256 * 1024);
  for (let at = 0; at < block.length; at += 65536) crypto.getRandomValues(block.subarray(at,at + 65536));
  block.set(encode('FALLOUT SAVE FILE'));
  await store.putFile(job,'savegame/SLOT01/SAVE.DAT',{size:129 * 1024 ** 2,read:async (_at,n) => block.subarray(0,n)});
  await store.commit(job); await store.close();
  const directory = await workspace(t), original = Blob.prototype.arrayBuffer;
  Blob.prototype.arrayBuffer = function() { assert.ok(this.size <= 256 * 1024,'an archive-sized arrayBuffer allocation was attempted'); return original.call(this); };
  try {
    const result = await exportSaveZip({userName:database,storeFactory:owner,temporary:() => temporary(directory)});
    assert.ok(result.blob.size > 128 * 1024 ** 2);
    const target = name(), plan = await prepareSaveZip(result.blob,{userName:target,storeFactory:owner,moduleFactory});
    assert.equal(plan.summary.found,1); await plan.install();
    const reopened = await openUserStore({name:target}), folder = await reopened.resolve('savegame/SLOT01'), dat = await reopened.lookup(folder.id,'save.dat');
    assert.equal(dat.size,129 * 1024 ** 2); assert.deepEqual(await reopened.read(dat.content,128 * 1024 ** 2,block.length),block); await reopened.close();
  } finally { Blob.prototype.arrayBuffer = original; }
});
archiveTest('70000 local and central records are decoded against a disk catalog without retaining a directory array',async t => {
  const directory = await workspace(t), tmp = await temporary(directory), writer = new ZipOutput(tmp.output,tmp.directory), empty = input(new Uint8Array());
  for (let n = 0; n < 70000; n++) await writer.add('many/entry-' + n,empty,{crc:0});
  await writer.close(); const file = await tmp.file(), store = await openUserStore({name:name()}), job = await store.begin('archive');
  let count = 0;
  for await (const entry of readSaveArchive(file,{moduleFactory,catalog:store.archiveCatalog(job)})) { for await (const _ of entry.chunks()) {} count++; }
  assert.equal(count,70000); assert.equal((await store.counts()).zipEntries,70000);
  await store.discard(job); assert.equal((await store.counts()).zipEntries,0); await store.close();
});
archiveTest('libarchive actually decodes a sparse Store ZIP64 across the 4 GiB offset and size boundary',async t => {
  const directory = await workspace(t), path = join(directory,'over4.zip'), zeros = new Uint8Array(256 * 1024);
  const out = await sink(path,{sparseBlock:zeros}), central = await sink(path + '.directory'), writer = new ZipOutput(out,central), size = 2 ** 32 + zeros.length;
  await writer.add('large.bin',{size,read:async () => zeros},{crc:zeroCrc(size)}); await writer.add('SAVEGAME/SLOT01/SAVE.DAT',input(header(1))); await writer.close();
  // Node 25.6.1 openAsBlob itself truncates >4GiB sizes (observed independently
  // with an 8-byte sparse fixture). Test the decoder on actual 64-bit file reads;
  // this adapter does NOT establish Safari's File/Blob handoff support.
  const disk = await open(path,'r'), length = (await disk.stat()).size;
  class DiskBlob extends Blob {
    get size() { return length; }
    slice(start = 0,end = length) {
      const at = Math.max(0,start < 0 ? length + start : start), until = Math.min(length,end < 0 ? length + end : end);
      return {async arrayBuffer() {
        const bytes = new Uint8Array(Math.max(0,until - at)); assert.ok(bytes.length <= 256 * 1024);
        assert.equal((await disk.read(bytes,0,bytes.length,at)).bytesRead,bytes.length); return bytes.buffer;
      }};
    }
  }
  const store = await openUserStore({name:name()}), job = await store.begin('archive'); let total = 0, count = 0;
  try {
    for await (const entry of readSaveArchive(new DiskBlob(),{moduleFactory,catalog:store.archiveCatalog(job)})) { for await (const chunk of entry.chunks()) total += chunk.length; count++; }
    assert.equal(count,2); assert.equal(total,size + 2000); await store.discard(job);
  } finally { await store.close(); await disk.close(); }
});

test('each game has its own saves: export and delete touch only that game',async t => {
  const directory = await workspace(t), database = name(), store = await openUserStore({name:database}), job = await store.begin('archive');
  await store.putFile(job,'savegame/SLOT01/SAVE.DAT',input(header(1)));
  await store.putFile(job,'settings/fallout2.cfg',input(config));
  await store.putFile(job,'games/2/savegame/SLOT03/SAVE.DAT',input(header(3)));
  await store.putFile(job,'games/2/savegame/SLOT04/SAVE.DAT',input(header(4)));
  await store.putFile(job,'games/2/port/card.txt',input(encode('version=Other\n')));
  await store.putFile(job,'games/2/port/card.png',input(Uint8Array.of(1)));
  await store.putFile(job,'port/card.png',input(Uint8Array.of(2)));
  await store.commit(job); await store.close();
  await forgetGamePicture({name:database,game:'2'});
  await forgetGamePicture({name:database,game:'5'});
  const pictures = await openUserStore({name:database});
  assert.ok(!(await pictures.lookup((await pictures.resolve('games/2/port')).id,'card.png')));
  assert.ok(await pictures.lookup((await pictures.resolve('games/2/port')).id,'card.txt'));
  assert.ok(await pictures.lookup((await pictures.resolve('port')).id,'card.png'));
  await pictures.close();
  const exported = await exportSaveZip({userName:database,storeFactory:async () => owner(),temporary:() => temporary(directory),game:'2'});
  assert.equal(exported.slots,2);
  const first = await exportSaveZip({userName:database,storeFactory:async () => owner(),temporary:() => temporary(directory)});
  assert.equal(first.slots,1);
  await forgetGameFiles({name:database,game:'2'});
  let reopened = await openUserStore({name:database});
  assert.ok(!(await reopened.resolve('games/2')));
  assert.ok(await reopened.lookup((await reopened.resolve('savegame/SLOT01')).id,'save.dat'));
  await reopened.close();
  // The first game's part is the root: emptied, the other games kept.
  const again = await openUserStore({name:database}), add = await again.begin('archive');
  await again.putFile(add,'games/3/savegame/SLOT01/SAVE.DAT',input(header(1))); await again.commit(add); await again.close();
  await forgetGameFiles({name:database,game:'1'});
  reopened = await openUserStore({name:database});
  assert.ok(!(await reopened.resolve('savegame/SLOT01')));
  assert.ok(!(await reopened.lookup((await reopened.resolve('settings')).id,'fallout2.cfg')));
  assert.ok(await reopened.resolve('savegame'));
  assert.ok(await reopened.lookup((await reopened.resolve('games/3/savegame/SLOT01')).id,'save.dat'));
  await reopened.close();
});
