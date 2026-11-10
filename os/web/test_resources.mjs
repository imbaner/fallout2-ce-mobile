import 'fake-indexeddb/auto';
import test from 'node:test';
import assert from 'node:assert/strict';
import {openResourceStore, DB_VERSION, BLOCK_SIZE, localFiles} from './resource-store.js';
import {activeKey} from './resource-store.js';
import {blockWriter} from './storage-common.js';
import {fileLifetime, patchDirectories} from './working-files.js';
import {importGameZip} from './zip-import.js';

const name = () => 'resources-' + crypto.randomUUID();
const readRequest = request => new Promise((resolve,reject) => {
  request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error);
});
async function raw(database, action) {
  const db = await readRequest(indexedDB.open(database));
  try { return await action(db); } finally { db.close(); }
}
async function change(db, stores, action) {
  const tx = db.transaction(stores,'readwrite');
  const done = new Promise((resolve,reject) => { tx.oncomplete = resolve; tx.onabort = () => reject(tx.error); });
  action(tx); await done;
}
const master = new Uint8Array(BLOCK_SIZE * 2 + 17).map((_,i) => i % 251);
const base = () => [['master.dat',master],['critter.dat',Uint8Array.of(4,8,15,16,23,42)]];
async function install(store, input = base()) {
  const run = await store.begin();
  for (const [path,data] of input) {
    const output = blockWriter(store,run,path,data.length), writer = output.stream.getWriter();
    await writer.write(data); await writer.close();
  }
  return store.activate(run,input.map(([path,data]) => ({path,size:data.length})),{test:true});
}

test('live save exchange pins installation and snapshots only bounded root configurations',async()=>{
  const database=name(),owner=await openResourceStore({name:database});
  const config=new TextEncoder().encode('[ui]\nauto_quick_save=1\n');
  const active=await install(owner,[...base(),['fallout2.cfg',config]]);await owner.close();
  const source=await localFiles({name:database});
  try {
    const snapshot=await source.saveExchangeResources();
    assert.equal(snapshot.active.run,active.run);assert.equal(snapshot.configs.length,1);
    assert.equal(snapshot.configs[0].path,'fallout2.cfg');assert.deepEqual(snapshot.configs[0].bytes,config);
    await assert.rejects(openResourceStore({name:database}),/other game|window/);
    assert.deepEqual(await source.read(source.entries.find(f=>f.path==='master.dat'),0,17),master.subarray(0,17));
  } finally {await source.close();}
});
async function count(database, store) {
  return raw(database,db => readRequest(db.transaction(store).objectStore(store).count()));
}

test('permanent ZIP extraction reopens with canonical range reads and exclusive ownership', async () => {
  const database = name();
  const {ZipWriter,Uint8ArrayWriter,Uint8ArrayReader} = globalThis.zip;
  const archive = new ZipWriter(new Uint8ArrayWriter(),{useWebWorkers:false});
  for (const [path,data] of base()) await archive.add(path,new Uint8ArrayReader(data),{level:0});
  const installed = await importGameZip(new Blob([await archive.close()]),{
    storeFactory:() => openResourceStore({name:database}), estimate:async () => ({})});
  const source = await localFiles({name:database});
  assert.equal(source.installation,installed.run);
  assert.deepEqual(await source.read(source.entries[0],BLOCK_SIZE - 11,31),master.slice(BLOCK_SIZE - 11,BLOCK_SIZE + 20));
  await assert.rejects(openResourceStore({name:database}),/other game/);
  await source.close();
  const reopened = await openResourceStore({name:database});
  assert.equal((await reopened.active()).run,installed.run);
  await reopened.close();
});

test('blocked upgrade reports another old client', async () => {
  const database = name(), oldClient = await readRequest(indexedDB.open(database,1));
  oldClient.onversionchange = () => {}; // Model an old window which refuses to close.
  await assert.rejects(openResourceStore({name:database}),/older game windows/);
  oldClient.close();
  const upgraded = await openResourceStore({name:database});
  assert.equal(await upgraded.active(),null);
  await upgraded.close();
});

test('quota error during replacement import propagates and leaves the active resources usable', async () => {
  const database = name(), store = await openResourceStore({name:database});
  const first = await install(store); await store.close();
  const {ZipWriter,Uint8ArrayWriter,Uint8ArrayReader} = globalThis.zip;
  const archive = new ZipWriter(new Uint8ArrayWriter(),{useWebWorkers:false});
  for (const [path,data] of base()) await archive.add(path,new Uint8ArrayReader(data),{level:0});
  await assert.rejects(importGameZip(new Blob([await archive.close()]),{
    estimate:async () => ({}), storeFactory:async () => {
      const writer = await openResourceStore({name:database});
      writer.writeBlocks = async () => { throw new DOMException('quota exhausted','QuotaExceededError'); };
      return writer;
    },
  }),error => error.name === 'QuotaExceededError');
  const reopened = await openResourceStore({name:database}), active = await reopened.active();
  assert.equal(active.run,first.run);
  assert.deepEqual(await reopened.read(active,active.files[0],0,8),master.slice(0,8));
  await reopened.close();
});

test('deduplication and collection preserve active/previous generations and shared DAT chunks', async () => {
  const database = name(), store = await openResourceStore({name:database});
  const first = await install(store);
  const chunks = await count(database,'chunks');
  const second = await install(store,[...base(),['mods/patch.dat',Uint8Array.of(1)]]);
  assert.equal(await count(database,'chunks'),chunks + 1);
  const third = await install(store,[...base(),['mods/patch.dat',Uint8Array.of(2)]]);
  await store.collect();
  assert.equal(await count(database,'chunks'),chunks + 2);
  assert.equal(await count(database,'generations'),2);
  assert.equal((await store.active()).run,third.run);
  assert.deepEqual(await store.read(second,second.files[0],0,8),master.slice(0,8));
  await assert.rejects(store.discard(third.run));
  await assert.rejects(store.discard(second.run));
  await assert.rejects(store.read(first,first.files[0],0,8),/Missing resource/);
  await store.close();
});

test('missing staging chunks, cancellation and failed activation preserve the previous installation', async () => {
  const database = name(), store = await openResourceStore({name:database});
  const first = await install(store), run = await store.begin();
  await store.block(run,'master.dat',0,Uint8Array.of(1));
  await assert.rejects(store.activate(run,[{path:'master.dat',size:1},{path:'critter.dat',size:1}]));
  assert.equal((await store.active()).run,first.run);
  const controller = new AbortController(), writer = blockWriter(store,run,'critter.dat',1,controller.signal).stream.getWriter();
  controller.abort(); await assert.rejects(writer.write(Uint8Array.of(2)),error => error.name === 'AbortError');
  await store.close();
  const reopened = await openResourceStore({name:database});
  assert.equal(await count(database,'generations'),1);
  assert.equal((await reopened.active()).run,first.run);
  // A real aborted activation transaction must roll back its pointer writes.
  const replacement = await reopened.begin();
  await reopened.block(replacement,'master.dat',0,Uint8Array.of(9));
  await reopened.block(replacement,'critter.dat',0,Uint8Array.of(8));
  await raw(database,db => change(db,['chunks','refs'],tx => {
    const request = tx.objectStore('refs').get([replacement,'master.dat',0]);
    request.onsuccess = () => tx.objectStore('chunks').delete(request.result.hash);
  }).catch(error => { throw error; }));
  await assert.rejects(reopened.activate(replacement,[{path:'master.dat',size:1},{path:'critter.dat',size:1}]));
  assert.equal((await reopened.active()).run,first.run);
  await reopened.close();
});

test('corrupt blocks/manifests and future formats fail closed without resetting storage', async () => {
  for (const damage of ['chunk','manifest','format','version']) {
    const database = name(), store = await openResourceStore({name:database}), active = await install(store);
    if (damage === 'chunk') {
      await raw(database,db => change(db,['chunks','refs'],tx => {
        const ref = tx.objectStore('refs').get([active.run,'master.dat',0]);
        ref.onsuccess = () => {
          const chunk = tx.objectStore('chunks').get(ref.result.hash);
          chunk.onsuccess = () => { const row = chunk.result; row.data[0] ^= 1; tx.objectStore('chunks').put(row); };
        };
      }));
      await assert.rejects(store.read(active,active.files[0],0,8),/Damaged resource/);
    } else if (damage === 'manifest') {
      await raw(database,db => change(db,['generations'],tx => {
        active.files[0].size++; tx.objectStore('generations').put(active);
      }));
    } else if (damage === 'format') {
      await raw(database,db => change(db,['state'],tx => tx.objectStore('state').put({format:9,minReader:9,minWriter:9},'format')));
    }
    await store.close();
    if (damage === 'version') (await readRequest(indexedDB.open(database,DB_VERSION + 1))).close();
    if (damage !== 'chunk') await assert.rejects(openResourceStore({name:database}));
    assert.ok(await count(database,'chunks') > 0);
  }
});

test('resource paths and lifetime policy', async () => {
  const database = name(), store = await openResourceStore({name:database});
  for (const path of ['../master.dat','/master.dat','a\\b','a:b','a//b']) {
    await assert.rejects(store.block('unknown',path,0,Uint8Array.of(1)));
  }
  const run = await store.begin();
  for (const paths of [['master.dat','MASTER.DAT'],['foo','foo/bar']]) {
    await assert.rejects(store.activate(run,[...base().map(([path,data]) => ({path,size:data.length})),...paths.map(path => ({path,size:0}))]));
  }
  assert.equal(fileLifetime('data/SAVEGAME/.stage/SAVE.DAT'),'save');
  assert.equal(fileLifetime('data/MAPS/AUTOMAP.DB'),'session');
  assert.equal(fileLifetime('data/proto/critters/00000001.pro'),'session');
  assert.equal(fileLifetime('data/config/game#patch.cfg'),'settings');
  assert.equal(fileLifetime('fallout2.cfg.tmp'),'temporary');
  assert.equal(fileLifetime('master.dat'),'resource');
  assert.deepEqual(patchDirectories('[system]\nmaster_patches=custom\\data\ncritter_patches=data'),['data','custom/data']);
  assert.throws(() => patchDirectories('[system]\nmaster_patches=../../outside'));
  await store.close();
});

test('several games: each its own installation, DAT chunks shared, the format 2 with a second game', async () => {
  const database = name();
  const first = await openResourceStore({name:database});
  const one = await install(first);
  assert.deepEqual(await first.games(),{current:'1',games:{'1':{name:'',added:0,played:0}}});
  await first.close();
  // Still a database of one game: format 1.
  assert.equal((await raw(database,db => readRequest(db.transaction('state').objectStore('state').get('format')))).format,1);

  const second = await openResourceStore({name:database,game:'2'});
  assert.equal(await second.active(),null);
  const two = await install(second,[...base(),['mods/rpu.dat',Uint8Array.of(1,2,3)]]);
  await second.saveGames({current:'2',games:{'1':{name:'',added:1,played:1},'2':{name:'RPU',added:2,played:2}}});
  // Both stay through a collection; the shared master.dat (3 blocks) and
  // critter.dat are stored once, the mod's block beside them.
  await second.collect();
  assert.equal(await count(database,'chunks'),5);
  assert.deepEqual((await second.liveGenerations()).sort(),[one.run,two.run].sort());
  await second.close();
  assert.equal((await raw(database,db => readRequest(db.transaction('state').objectStore('state').get('format')))).format,2);
  assert.equal(await raw(database,db => readRequest(db.transaction('state').objectStore('state').get(activeKey('2')))),two.run);

  const firstSource = await localFiles({name:database});
  assert.equal(firstSource.installation,one.run);
  await firstSource.close();
  const secondSource = await localFiles({name:database,game:'2'});
  assert.equal(secondSource.installation,two.run);
  assert.ok(secondSource.entries.some(file => file.path === 'mods/rpu.dat'));
  await secondSource.close();

  // Forgetting the second game drops its installation, not the first's.
  const owner = await openResourceStore({name:database});
  const list = await owner.forgetGame('2');
  assert.deepEqual(Object.keys(list.games),['1']);
  assert.equal(list.current,'1');
  assert.deepEqual(await owner.liveGenerations(),[one.run]);
  assert.equal(await count(database,'chunks'),4);
  assert.equal((await owner.active()).run,one.run);
  await owner.close();
  await assert.rejects(openResourceStore({name:database,game:'x'}),/Invalid game/);
});
