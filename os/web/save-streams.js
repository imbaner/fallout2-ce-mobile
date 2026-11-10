import {openResourceStore,gamePrefix,FIRST_GAME} from './resource-store.js';
import {openUserStore,USER_DATABASE} from './user-store.js';
import {readSaveArchive} from './save-zip-reader.js';
import {ZipOutput} from './zip-output.js';
import {temporaryExport} from './opfs-export.js';
import {BLOCK_SIZE,checkCancelled} from './storage-common.js';
import {ImportError,pathInZip} from './zip-import.js';
import {slotName,slotNumber,saveIdentity,parseRecords,recordText,quickRange} from './save-format.js';
import {configValue,decodeConfig,configName} from './game-config.js';
const encode = text => new TextEncoder().encode(text), decode = bytes => new TextDecoder().decode(bytes);
const fail = (code,message) => { throw new ImportError(code,message); };
const input = bytes => ({size:bytes.length,read:async (at,n) => bytes.subarray(at,at + n)});
const notify = (fn,value) => { try { fn(value); } catch (e) { console.warn('Save progress observer failed',e); } };
async function textFile(store,path,{job,root = 'root',limit = 16 * 1024 ** 2} = {}) {
  const parts = path.split('/'), name = parts.pop(), parent = await store.resolve(parts.join('/'),{job,root});
  const node = parent && await store.lookup(parent.id,name,job); if (!node) return '';
  if (node.size > limit) fail('limit','Unexpectedly large save metadata');
  const bytes = new Uint8Array(node.size);
  for (let at = 0; at < bytes.length; at += BLOCK_SIZE) bytes.set(await store.read(node.content,at,Math.min(BLOCK_SIZE,bytes.length - at)),at);
  return decode(bytes);
}
// The installed game's config (`configName` of its ddraw.ini) and ddraw.ini:
// the player's saved copies, else the game's own.
async function gameConfigs(owner,active,store,prefix = '') {
  const read = async path => {
    const saved = await textFile(store,prefix + 'settings/' + path,{limit:1024 ** 2}); if (saved) return saved;
    const file = active?.files.find(f => f.path.toLowerCase() === path.toLowerCase());
    if (!file) return ''; if (file.size > 1024 ** 2) fail('limit','Unexpectedly large game configuration');
    return decodeConfig(await owner.read(active,file,0,file.size));
  };
  const ddraw = await read('ddraw.ini');
  return [await read(configName(ddraw)),ddraw];
}
// [prefix]: the game's part (`gamePrefix`); a game that never ran has no
// savegame yet (root null).
async function installed(store,prefix = '') {
  const root = await store.resolve(prefix + 'savegame') || null, slots = new Map(), occupied = new Set();
  if (root) for await (const folder of store.children(root.id)) {
    if (/^\.ce-(write|cleanup)-\d+$/.test(folder.name)) fail('recovery_needed','Start the game once to recover an interrupted legacy save before importing or exporting saves');
    const number = slotNumber(folder.name); if (!number) continue; occupied.add(number);
    const node = await store.lookup(folder.id,'save.dat');
    if (node) {
      // Match Android's export inventory: a folder containing SAVE.DAT is
      // backed up even when its header is damaged. Never silently drop it.
      const regular = (node.mode & 0o170000) === 0o100000;
      const prefix = regular && node.size ? await store.read(node.content,0,Math.min(node.size,4096)) : new Uint8Array();
      slots.set(number,{folder,node,id:prefix.length ? saveIdentity(prefix,node.size) : ''});
    }
  }
  return {root,slots,occupied,records:parseRecords(await textFile(store,prefix + 'port/saves.txt'))};
}
async function removeTree(store,job,node) {
  if ((node.mode & 0o170000) === 0o040000) for await (const child of store.children(node.id)) await removeTree(store,job,child);
  await store.stage(job,{...node,deleted:true});
}
async function copyTree(store,scan,apply,node,parent,name) {
  const {job,deleted,...value} = node;
  await store.stage(apply,{...value,parent,name});
  if ((node.mode & 0o170000) === 0o040000) for await (const child of store.children(node.id,{job:scan})) await copyTree(store,scan,apply,child,node.id,child.name);
}
export async function prepareSaveZip(file,{signal,storeFactory = openResourceStore,userName = USER_DATABASE,
  progress = () => {},rootHint,moduleFactory,archiveReader = readSaveArchive,game = FIRST_GAME} = {}) {
  const prefix = gamePrefix(game);
  let owner,store,scan,apply,disposed = false,used = false;
  const dispose = async () => {
    if (disposed) return; disposed = true;
    try { if (scan) await store?.discard(scan); if (apply) await store?.discard(apply); }
    finally { await store?.close(); await owner?.close(); }
  };
  try {
    owner = await storeFactory(); const active = await owner.active(); if (!active) fail('base_required','Install game files first');
    store = await openUserStore({name:userName,signal});
    scan = await store.begin('archive'); const scanRoot = 'archive-' + scan;
    await store.stage(scan,{id:scanRoot,parent:'',name:scanRoot,mode:0o40700,size:0,time:Date.now()});
    const roots = new Map(); let bytes = 0, headers = 0, extractedFiles = 0, comparer;
    const hint = rootHint ? pathInZip(rootHint).replace(/\/?$/,'/').toLowerCase() : '';
    for await (const entry of archiveReader(file,{signal,moduleFactory,catalog:store.archiveCatalog(scan),onComparer:value => { comparer = value; }})) {
      checkCancelled(signal);
      const path = entry.path, lower = path.toLowerCase();
      const match = /^(.*?)(slot\d+)\/(.+)$/i.exec(path), slot = match && slotNumber(match[2]);
      const relative = match?.[3];
      const save = slot && !entry.directory && lower.startsWith(hint) && !relative.split('/').some(p => p.startsWith('.')) && !/(^|\/)ce-meta\.txt$/i.test(relative);
      // Any .cfg: ddraw.ini may name the game's config (`configName`).
      const metadata = !entry.directory && /(^|\/)(fallout2-saves\.txt|[^/]+\.cfg|ddraw\.ini)$/.test(lower);
      if (!save && !metadata) continue; // Reader still drains and CRC-checks it.
      const result = await store.putStream(scan,path,entry.chunks(),{root:scanRoot,signal,limit:metadata ? 16 * 1024 ** 2 : Number.MAX_SAFE_INTEGER});
      bytes += result.node.size; extractedFiles++;
      if (save && relative.toLowerCase() === 'save.dat') {
        if (++headers > 2000) fail('limit','Too many candidate save slots');
        if (result.node.size < 131 || decode(result.prefix.subarray(0,17)) !== 'FALLOUT SAVE FILE') fail('invalid_save','Invalid SAVE.DAT header: ' + path);
        const root = match[1], key = root.toLowerCase();
        if (!roots.has(key)) roots.set(key,{root,depth:root.split('/').filter(Boolean).length,slots:new Map()});
        roots.get(key).slots.set(slot,{slot,node:result.node,prefix:result.prefix,id:saveIdentity(result.prefix,result.node.size)});
      }
      notify(progress,{phase:'extract',bytes,total:0,files:extractedFiles,file:path,cancelAllowed:true});
    }
    const shallow = [...roots.values()].filter(r => r.depth === Math.min(...[...roots.values()].map(r => r.depth)));
    if (!shallow.length) fail('no_saves','No SLOT folders with SAVE.DAT');
    if (shallow.length !== 1) fail('ambiguous_root','Several save folders at the same depth');
    const found = shallow[0], parent = found.root.replace(/[^/]+\/$/,''), current = await installed(store,prefix);
    const info = await textFile(store,found.root + 'fallout2-saves.txt',{job:scan,root:scanRoot}) || await textFile(store,parent + 'fallout2-saves.txt',{job:scan,root:scanRoot});
    const sourceRecords = parseRecords(info), [first,end] = quickRange(...await gameConfigs(owner,active,store,prefix));
    const sourceDdraw = await textFile(store,parent + 'ddraw.ini',{job:scan,root:scanRoot});
    const sourceQuick = quickRange(await textFile(store,parent + configName(sourceDdraw),{job:scan,root:scanRoot}),sourceDdraw);
    const ids = new Set([...current.slots.values()].map(s => s.id)), saves = [...found.slots.values()], unordered = [];
    const currentComposition = (await textFile(store,prefix + 'port/resource-installation.txt')).trim() === active.run
      ? (await textFile(store,prefix + 'port/game-composition.txt')).trim() : '';
    const comparisons = new Map();
    const [config] = await gameConfigs(owner,active,store,prefix); let charset;
    try { charset = new TextDecoder(configValue(config,'system','language') === 'russian' ? 'windows-1251' : 'windows-1252'); } catch { charset = new TextDecoder(); }
    for (const item of saves) {
      const known = sourceRecords.get(item.slot);
      item.record = known?.id === item.id ? {...known} : {id:item.id,created:'0',order:'0',composition:'',quick:item.slot >= sourceQuick[0] && item.slot < sourceQuick[1]};
      item.duplicate = ids.has(item.id);
      const made = item.record.composition;
      if (!comparisons.has(made)) comparisons.set(made,made && currentComposition && comparer ? comparer(made,currentComposition) : {level:3,changes:[]});
      item.compatibility = comparisons.get(made);
      const view = new DataView(item.prefix.buffer,item.prefix.byteOffset,item.prefix.byteLength);
      const day = view.getUint16(91), month = view.getUint16(93), year = view.getUint16(95);
      item.seconds = year >= 1990 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31 ? Math.floor(new Date(year,month - 1,day,12).getTime() / 1000) : 0;
      item.date = item.seconds ? `${String(day).padStart(2,'0')}.${String(month).padStart(2,'0')}.${String(year).padStart(4,'0')}` : '';
      item.gameTime = view.getUint32(107);
      item.title = charset.decode(item.prefix.subarray(61,91)).split('\0')[0].trim() || charset.decode(item.prefix.subarray(29,61)).split('\0')[0].trim();
      if (item.record.order === '0') unordered.push(item);
    }
    unordered.sort((a,b) => a.seconds - b.seconds || a.gameTime - b.gameTime);
    unordered.forEach((item,i) => { item.record.order = String(BigInt(Math.max(item.seconds,1)) * 1000000n + BigInt(i)); });
    saves.sort((a,b) => BigInt(a.record.order) > BigInt(b.record.order) ? -1 : BigInt(a.record.order) < BigInt(b.record.order) ? 1 : b.slot - a.slot);
    function planFor(replace) {
      const free = [], quick = [];
      for (let n = 1; n <= 1000; n++) if (replace || !current.occupied.has(n)) (n >= first && n < end ? quick : free).push(n);
      const assignments = []; let skipped = 0;
      for (const item of saves) {
        if (!replace && item.duplicate) { skipped++; continue; }
        const target = replace && item.record.quick && quick.length ? quick.shift() : free.shift();
        if (!target) return {error:'slot_limit',skipped,needed:saves.filter(s => replace || !s.duplicate).length,available:assignments.length};
        assignments.push({item,target});
      }
      return {assignments,skipped,converted:replace ? assignments.filter(({item,target}) => item.record.quick && !(target >= first && target < end)).length : 0};
    }
    const add = planFor(false), replace = planFor(true);
    const summary = {token:crypto.randomUUID(),kind:'saves',filename:String(file.name || 'saves.zip'),root:found.root,
      found:saves.length,added:add.assignments?.length || 0,skipped:add.skipped,unknown:saves.filter(s => !s.record.composition).length,bytes,
      current:current.slots.size,addError:add.error,replaceError:replace.error,
      replaceConverted:replace.converted || 0,
      compatibility:[0,1,2,3].map(level => saves.filter(s => !s.duplicate && s.compatibility.level === level).length),
      saves:saves.map(s => ({title:s.title,date:s.date,duplicate:s.duplicate,created:s.record.created,quick:s.record.quick,
        level:s.compatibility.level,changes:s.compatibility.changes,known:!!s.record.composition}))};
    return {summary,active,dispose,
      export:options => writeExport(owner,store,options),
      async install({replaceAll = false,signal:writeSignal = signal,progress:notice = () => {}} = {}) {
      if (used || disposed) fail('stale_plan','Choose the ZIP again'); used = true;
      const plan = replaceAll ? replace : add;
      try {
        if (plan.error) fail('slot_limit','Not enough matching free save slots');
        checkCancelled(writeSignal); apply = await store.begin('save-import');
        const records = replaceAll ? new Map() : new Map(current.records);
        const saveRoot = current.root || await store.resolve(prefix + 'savegame',{job:apply,create:true});
        if (replaceAll && current.root) for await (const folder of store.children(current.root.id)) if (slotNumber(folder.name)) await removeTree(store,apply,folder);
        let done = 0;
        for (const {item,target} of plan.assignments) {
          checkCancelled(writeSignal);
          const folder = await store.node(item.node.parent,scan);
          await copyTree(store,scan,apply,folder,saveRoot.id,slotName(target));
          records.set(target,{...item.record,quick:target >= first && target < end});
          notify(notice,{phase:'save-import',files:++done,totalFiles:plan.assignments.length,cancelAllowed:true});
        }
        await store.putFile(apply,prefix + 'port/saves.txt',input(encode(recordText(records,{exported:false}))),{signal:writeSignal});
        checkCancelled(writeSignal); notify(notice,{phase:'commit',cancelAllowed:false});
        await store.commit(apply); apply = null;
        // Cleanup cannot turn a confirmed commit into a reported failure.
        return {added:plan.assignments.length,skipped:plan.skipped,replaced:replaceAll};
      } finally { await dispose().catch(e => console.warn('Save import cleanup failed',e)); }
    }};
  } catch (error) { await dispose().catch(() => {}); throw error; }
}

export async function exportSaveZip({signal,storeFactory = openResourceStore,userName = USER_DATABASE,
  progress = () => {},temporary = temporaryExport,game = FIRST_GAME} = {}) {
  const owner = await storeFactory(); let store;
  try {
    store = await openUserStore({name:userName,signal});
    return await writeExport(owner,store,{signal,progress,temporary,prefix:gamePrefix(game)});
  } finally { await store?.close(); await owner.close(); }
}
async function writeExport(owner,store,{signal,progress = () => {},temporary = temporaryExport,prefix = ''} = {}) {
  let temp,complete = false;
  try {
    const active = await owner.active();
    const current = await installed(store,prefix); if (!current.slots.size) fail('no_saves','No saves to export');
    const [first,end] = quickRange(...await gameConfigs(owner,active,store,prefix));
    const records = new Map(); let total = 0, overhead = 0;
    // Holding both leases pins the committed library throughout export.
    for (const [number,slot] of current.slots) {
      for await (const {path,node} of store.walk(slot.folder.id)) {
        total += node.size;
        const length = encode('SAVEGAME/' + slotName(number) + '/' + path).length;
        // Local header + worst-case ZIP64 extra; the central entry exists in
        // both the temporary spool and the completed output at peak disk use.
        overhead += 30 + length + 20 + 2 * (46 + length + 28);
      }
      const known = current.records.get(number);
      records.set(number,{...(known?.id === slot.id ? known : {id:slot.id,created:'0',order:'0',composition:''}),quick:number >= first && number < end});
    }
    // OPFS capacity is checked again by each actual write; this estimate is
    // advisory. A second system copy in Files is outside our storage quota.
    const metadata = encode(recordText(records));
    if (metadata.length > 16 * 1024 ** 2) fail('limit','Save metadata exceeds its budget');
    const space = await globalThis.navigator?.storage?.estimate?.();
    if (space && space.quota - space.usage < total + overhead + metadata.length + 16 * 1024 ** 2) throw new DOMException('Not enough space for a temporary ZIP','QuotaExceededError');
    temp = await temporary();
    let bytes = 0;
    const writer = new ZipOutput(temp.output,temp.directory,{signal,progress:() => notify(progress,{phase:'export',bytes,total,cancelAllowed:true})});
    for (const [number,slot] of current.slots) for await (const {path,node} of store.walk(slot.folder.id)) {
      checkCancelled(signal); if (/(^|\/)ce-meta\.txt$/i.test(path)) continue;
      await writer.add('SAVEGAME/' + slotName(number) + '/' + path,{size:node.size,read:async (at,n) => store.read(node.content,at,n)},{crc:node.crc,date:node.time});
      bytes += node.size; notify(progress,{phase:'export',bytes,total,cancelAllowed:true});
    }
    await writer.add('fallout2-saves.txt',input(metadata));
    await writer.close(); const file = await temp.file(); complete = true;
    return {blob:file,slots:current.slots.size,filename:'Fallout2-saves-' + new Date().toISOString().slice(0,10) + '.zip',bytes:file.size};
  } finally { if (!complete) await temp?.discard().catch(() => {}); }
}
export async function launcherFont({name = USER_DATABASE,game = FIRST_GAME} = {}) {
  const {userSettingText} = await import('./user-report.js');
  const prefix = gamePrefix(game);
  const config = configName(await userSettingText(prefix + 'settings/ddraw.ini',{name}));
  return configValue(await userSettingText(prefix + 'settings/' + config,{name}),'touch','font') === '1' ? 1 : 0;
}

// [game]'s files replaced: its card picture (game_card.cc writes it once)
// made again at its next menu, as Android's Games.forgetPicture.
export async function forgetGamePicture({name = USER_DATABASE,game = FIRST_GAME,signal} = {}) {
  const store = await openUserStore({name,signal});
  let job;
  try {
    const folder = await store.resolve(gamePrefix(game) + 'port').catch(() => null);
    const picture = folder && await store.lookup(folder.id,'card.png');
    if (!picture) return;
    job = await store.begin('forget-picture');
    await store.stage(job,{...picture,deleted:true});
    await store.commit(job); job = null;
  } finally {
    if (job) await store.discard(job).catch(() => {});
    await store.close();
  }
}

// A deleted game's part of the user's storage: its saves, settings and the
// port's records (the first game's in the root, its folders kept empty).
export async function forgetGameFiles({name = USER_DATABASE,game,signal} = {}) {
  const store = await openUserStore({name,signal});
  let job;
  try {
    job = await store.begin('delete-game');
    if (game === FIRST_GAME) {
      for (const folder of ['savegame','settings','port','exports']) {
        const node = await store.resolve(folder,{job});
        if (node) for await (const child of store.children(node.id,{job})) await removeTree(store,job,child);
      }
    } else {
      const node = await store.resolve(gamePrefix(game),{job});
      if (node) await removeTree(store,job,node);
    }
    await store.commit(job); job = null;
  } finally {
    if (job) await store.discard(job).catch(() => {});
    await store.close();
  }
}
