import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,copyFile,mkdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';

// Exercise the real page controller, not a second implementation of its
// transitions. The worker protocol is stubbed; storage/ZIP has its own tests.
async function page(mode,check,options = {}) {
  const directory=await mkdtemp(join(tmpdir(),'fallout-files-ui-'));
  const previous=new Map(),events=new Map(),elements=new Map();
  function install(key,value) { previous.set(key,Object.getOwnPropertyDescriptor(globalThis,key));Object.defineProperty(globalThis,key,{value,writable:true,configurable:true}); }
  class Element {
    hidden=true;disabled=false;textContent='';files=[];
    classList={toggle(){},remove(){},add(){}};
    setAttribute(key,value){this[key]=value;} removeAttribute(key){delete this[key];}
    click(){return this.onclick?.();} focus(){}
  }
  const element=id=> {if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
  let worker;
  try {
    await mkdir(join(directory,'icons'));
    execFileSync('python3',['-c','import sys;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from import_ui import write_native_assets;write_native_assets(Path(sys.argv[2]))',fileURLToPath(new URL('.',import.meta.url)),directory]);
    await copyFile(new URL('./files.js',import.meta.url),join(directory,'files.mjs'));
    await copyFile(new URL('./files.js',import.meta.url),join(directory,'files.js'));
    await copyFile(new URL('./install-hint.js',import.meta.url),join(directory,'install-hint.js'));
    await copyFile(new URL('./visits.js',import.meta.url),join(directory,'visits.js'));
    await copyFile(new URL('./strings.js',import.meta.url),join(directory,'strings.js'));
    await copyFile(new URL('./save-session.js',import.meta.url),join(directory,'save-session.mjs'));
    await copyFile(new URL('./save-download.js',import.meta.url),join(directory,'save-download.mjs'));
    install('navigator',{language:'ru',locks:{},storage:{persist:async()=>true}});
    install('matchMedia',()=>({matches:false}));
    install('isSecureContext',true);install('indexedDB',{});
    install('document',{documentElement:{dataset:{}},body:new Element(),getElementById:element,addEventListener:(name,fn)=>events.set(name,fn),removeEventListener:(name)=>events.delete(name)});
    install('location',{search:mode?'?mode='+mode:'',href:'./files.html'});
    install('fetch',async()=>({ok:true,json:async()=>({buildId:'test'})}));
    install('Worker',class {sent=[];constructor(){worker=this;}postMessage(data){this.sent.push(data);}terminate(){} });
    install('addEventListener',(name,fn)=>events.set(name,fn));
    install('removeEventListener',name=>events.delete(name));
    const {createFilesManager} = await import(pathToFileURL(join(directory,'files.mjs')).href);
    createFilesManager({mode,...options});
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(worker.sent.at(-1).type,'status');
    const active={files:[],measurements:{bytes:1}};
    const emit=data=>worker.onmessage({data:{type:'result',active,...data}});
    await check({element,worker,emit,events,directory});
    events.get('pagehide')?.();
  } finally {
    for(const [key,descriptor]of previous) {if(descriptor)Object.defineProperty(globalThis,key,descriptor);else delete globalThis[key];}
    await rm(directory,{recursive:true,force:true});
  }
}

test('corner arrow releases a prepared import before returning to the game',async()=>page('saves_import',async({element,worker,emit})=>{
  emit({action:'status'});
  assert.equal(element('title').textContent,'Сохранения');
  assert.equal(element('primary').textContent,'Выбрать архив');
  assert.equal(element('back')['aria-label'],'Вернуться в игру');
  assert.equal(element('back').hidden,false);
  element('primary').click();
  element('archive').files=[new Blob()];await element('archive').onchange();
  assert.equal(worker.sent.at(-1).type,'inspect-saves');
  assert.equal(element('back').hidden,true);
  emit({action:'inspect-saves',plan:{kind:'saves',token:'p',found:1,compatibility:[0,0,0,1],skipped:0,added:1,current:2,saves:[{level:3,title:'Test',changes:[]}]}});
  element('secondary').click();
  assert.match(element('message').textContent,/Текущие сохранения \(2\)/);
  element('back').click();
  assert.equal(worker.sent.at(-1).type,'discard');
  assert.equal(location.href,'./files.html','wait for staging disposal and lease release');
  emit({action:'discard'});
  assert.equal(location.href,'./index.html');
}));

test('export uses one download action and the native corner arrow, with no text Back',async()=>page('saves_export',async({element,worker,emit})=>{
  emit({action:'status'});
  assert.equal(worker.sent.at(-1).type,'export-saves');
  assert.equal(element('back').hidden,true);
  emit({action:'export-saves',exchange:{blob:new Blob(['ZIP']),filename:'saves.zip',slots:1000}});
  assert.equal(element('download').hidden,false);
  assert.equal(element('download').download,'saves.zip');
  assert.equal(element('primary').hidden,true);assert.equal(element('secondary').hidden,true);
  assert.equal(element('back').hidden,false);
  element('back').click();emit({action:'discard'});
  assert.equal(location.href,'./index.html');
}));

test('cancellation stays on the operation and export before replacement retains confirmation',async()=>page('',async({element,worker,emit})=>{
  emit({action:'status'});element('saves-export').click();
  assert.equal(element('back').hidden,true);
  element('secondary').click();assert.equal(worker.sent.at(-1).type,'cancel');
  emit({type:'error',code:'cancelled',message:'cancelled'});
  assert.equal(element('back').hidden,false);
  const plan={kind:'saves',token:'p',found:1,compatibility:[0,0,0,1],skipped:0,added:1,current:2,saves:[{level:3,title:'Test',changes:[]}]};
  emit({action:'inspect-saves',plan});element('secondary').click();element('secondary').click();
  assert.equal(worker.sent.at(-1).type,'export-before-replace');
  emit({action:'export-before-replace',plan,exchange:{blob:new Blob(['ZIP']),filename:'backup.zip',slots:2}});
  assert.equal(element('download').hidden,false);
  assert.equal(element('secondary').textContent,'Заменить все');
  element('secondary').click();
  assert.match(element('message').textContent,/Текущие сохранения \(2\)/);
  assert.equal(element('primary').textContent,'Заменить');
  assert.equal(worker.sent.at(-1).type,'export-before-replace','confirmation does not apply replacement');
}));

test('live export requests a download once and automatically returns after worker disposal',async()=>{
  const downloads=[];let returns=0;
  await page('saves_export',async({element,worker,emit})=>{
    assert.equal(worker.sent[0].resources.active.run,'pinned');
    emit({action:'status'});
    emit({action:'export-saves',exchange:{blob:new Blob(['ZIP']),filename:'saves.zip',slots:1000}});
    assert.equal(downloads.length,1);assert.equal(element('download').hidden,true);
    assert.equal(element('primary').hidden,true);
    assert.match(element('message').textContent,/Скачивание запрошено/);
    assert.equal(returns,0);
    await new Promise(resolve=>setTimeout(resolve,1550));
    assert.equal(worker.sent.at(-1).type,'discard');assert.equal(returns,0);
    emit({action:'discard'});assert.equal(returns,1);
    assert.equal(location.href,'./files.html','live session never navigates');
  },{resources:{active:{run:'pinned'},configs:[]},buildId:'pinned-build',download:value=>downloads.push(value),onReturn:()=>{returns++;}});
});

test('detected download errors stay paused across repeated retries and keep the corner return arrow',async()=>{
  let attempts=0,returns=0;
  await page('saves_export',async({element,worker,emit})=>{
    emit({action:'status'});
    emit({action:'export-saves',exchange:{blob:new Blob(['ZIP']),filename:'saves.zip',slots:1}});
    assert.equal(element('primary').textContent,'Повторить');assert.equal(element('back').hidden,false);
    assert.match(element('message').textContent,/Не удалось начать скачивание/);
    assert.doesNotThrow(()=>element('primary').click());assert.equal(attempts,2);
    await new Promise(resolve=>setTimeout(resolve,1550));
    assert.equal(returns,0);assert.equal(worker.sent.at(-1).type,'export-saves');
    element('back').click();assert.equal(worker.sent.at(-1).type,'discard');emit({action:'discard'});
    assert.equal(returns,1);assert.equal(location.href,'./files.html');
  },{resources:{active:{run:'pinned'},configs:[]},download:()=>{attempts++;throw Error('request failed');},onReturn:()=>{returns++;}});
});

test('successful download retry uses the prepared ZIP and automatically resumes the session',async()=>{
  const result={blob:new Blob(['ZIP']),filename:'saves.zip',slots:1};let attempts=0,returns=0;
  await page('saves_export',async({element,worker,emit})=>{
    emit({action:'status'});emit({action:'export-saves',exchange:result});
    element('primary').click();assert.equal(attempts,2);
    assert.equal(element('primary').hidden,true);assert.equal(element('back').hidden,true);
    await new Promise(resolve=>setTimeout(resolve,1550));
    assert.equal(worker.sent.filter(message=>message.type==='export-saves').length,1,'do not build the ZIP again');
    assert.equal(worker.sent.at(-1).type,'discard');emit({action:'discard'});assert.equal(returns,1);
  },{resources:{active:{run:'pinned'},configs:[]},download:value=>{assert.equal(value,result);if(!attempts++)throw Error('request failed');},onReturn:()=>{returns++;}});
});

test('successful live import automatically disposes its worker and returns to the same document',async()=>{
  let returns=0;
  await page('saves_import',async({worker,emit,element})=>{
    emit({action:'status'});
    emit({action:'install-saves',exchange:{added:1000,skipped:0}});
    assert.match(element('message').textContent,/1000/);
    await new Promise(resolve=>setTimeout(resolve,1550));
    assert.equal(worker.sent.at(-1).type,'discard');emit({action:'discard'});
    assert.equal(returns,1);assert.equal(location.href,'./files.html');
  },{resources:{active:{run:'pinned'},configs:[]},onReturn:()=>{returns++;}});
});

test('a crashed exchange worker offers return without reloading the paused game',async()=>{
  let returns=0;
  await page('saves_export',async({worker,emit,element})=>{
    emit({action:'status'});worker.onerror();
    assert.equal(element('back').hidden,false);element('back').click();
    assert.equal(returns,1);assert.equal(location.href,'./files.html');
  },{resources:{active:{run:'pinned'},configs:[]},onReturn:()=>{returns++;}});
});

test('cancelled live exchange discards staging and returns automatically',async()=>{
  let returns=0;
  await page('saves_import',async({worker,emit})=>{
    emit({action:'status'});
    emit({type:'error',code:'cancelled',cancelled:true,message:'cancelled'});
    assert.equal(worker.sent.at(-1).type,'discard');emit({action:'discard'});assert.equal(returns,1);
  },{resources:{active:{run:'pinned'},configs:[]},onReturn:()=>{returns++;}});
});

test('invalid ZIP offers return to the retained session rather than a new launch',async()=>{
  let returns=0;
  await page('saves_import',async({emit,element})=>{
    emit({action:'status'});emit({type:'error',code:'invalid_zip',message:'bad CRC'});
    assert.equal(element('title').textContent,'Сохранения');
    assert.equal(element('secondary').textContent,'Вернуться в игру');
    await element('secondary').click();assert.equal(returns,1);assert.equal(location.href,'./files.html');
  },{resources:{active:{run:'pinned'},configs:[]},onReturn:()=>{returns++;}});
});

test('empty live export follows Android completion and returns without a download',async()=>{
  let returns=0;
  await page('saves_export',async({worker,emit,element})=>{
    emit({action:'status'});emit({type:'error',code:'no_saves',message:'none'});
    assert.match(element('message').textContent,/Нет сохранений|нет сохранений|Сохранений/);
    await new Promise(resolve=>setTimeout(resolve,1550));
    assert.equal(worker.sent.at(-1).type,'discard');emit({action:'discard'});assert.equal(returns,1);
  },{resources:{active:{run:'pinned'},configs:[]},download:()=>assert.fail('nothing to download'),onReturn:()=>{returns++;}});
});

test('live coordinator waits for writer reacquisition and allows retry without resuming a closed store',async()=>page('',async({element,directory})=>{
  const {createSaveSession}=await import(pathToFileURL(join(directory,'save-session.mjs')).href);
  const order=[];let callback,paused=false,finished=false,fail=true;
  const user={status:()=>({suspended:paused}),commit:async()=>order.push('commit'),suspend:async()=>{order.push('suspend');paused=true;},
    resume:async()=>{order.push('resume');if(fail){fail=false;throw Error('writer busy');}paused=false;}};
  const exchange=createSaveSession({host:element('host'),root:document,canvas:element('canvas'),user,
    source:{saveExchangeResources:async()=>{order.push('resources');return {active:{run:'pinned'},configs:[]};}},
    audio:{pause:async()=>order.push('pause-audio'),unpause:()=>order.push('resume-audio')},textInput:{end:()=>{}},
    managerFactory:options=>{callback=options.onReturn;return{ready:Promise.resolve(),destroy:()=>order.push('destroy')};}});
  const operation=exchange('saves_import').then(()=>{finished=true;});await new Promise(resolve=>setImmediate(resolve));
  assert.deepEqual(order,['pause-audio','commit','resources','suspend']);assert.equal(finished,false);
  await callback();assert.equal(finished,false);assert.equal(paused,true);assert.equal(element('host').hidden,false);
  assert.match(element('details').textContent,/writer busy/);
  await element('primary').click();await operation;
  assert.equal(finished,true);assert.equal(paused,false);assert.equal(element('host').hidden,true);
  assert.deepEqual(order.slice(-3),['resume','destroy','resume-audio']);
}));

test('download retains disk File URLs through resume and retries until pagehide, without persistent UI',async()=>page('',async({directory,events})=>{
  const {createSaveDownloads}=await import(pathToFileURL(join(directory,'save-download.mjs')).href);
  const requested=[],revoked=[];let serial=0,attached=0;
  const request=createSaveDownloads({
    document:{body:{append(){attached++;}},createElement:()=>({click(){requested.push(this.href);},remove(){attached--;}})},
    URL:{createObjectURL:()=>String(++serial),revokeObjectURL:url=>revoked.push(url)}});
  const first={blob:new Blob(['zip']),filename:'first.zip'};
  request(first);request(first);
  request({blob:new Blob(['zip2']),filename:'second.zip'});
  assert.deepEqual(requested,['1','1','2']);assert.deepEqual(revoked,[]);assert.equal(attached,0);
  events.get('pagehide')();assert.deepEqual(revoked,['1','2']);
}));

test('Fallout 2 with a mod in its subfolder asks which one to install, then inspects the chosen one',async()=>page('',async({element,worker,emit})=>{
  emit({action:'status'});
  element('primary').click();
  const archive=new Blob();element('archive').files=[archive];await element('archive').onchange();
  assert.equal(worker.sent.at(-1).type,'inspect');
  emit({action:'inspect',plan:{token:'c',choices:['Fallout 2/','Fallout 2/Resurrection/']}});
  assert.match(element('message').textContent,/Resurrection/);
  assert.equal(element('primary').textContent,'Resurrection');
  assert.equal(element('secondary').textContent,'Fallout 2');
  element('primary').click();
  assert.deepEqual([worker.sent.at(-1).type,worker.sent.at(-1).choice,worker.sent.at(-1).file],['inspect','Fallout 2/Resurrection/',archive]);
}));
