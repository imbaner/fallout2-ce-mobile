import {developmentFiles, mountGame} from './filesystem.js';
import {gameConfigPath} from './working-files.js';
import {createTextInput, createGameAudio, createGameViewport, createAudioTiming, createGameWindow} from './browser.js';
import {localFiles} from './resource-store.js';
import {createFilesView} from './files-view.js';
import {createSaveSession} from './save-session.js';
import {createSaveDownloads} from './save-download.js';
import {startAppCache} from './app-cache.js';
import {installHint, installedApp} from './install-hint.js';
import {countVisit, countEvent} from './visits.js';
import {language, webText as t} from './strings.js';
import {abortDetail, abortKind, createHangWatch, hideProblem, showProblem} from './problem-view.js';

const $ = id => document.getElementById(id);
const viewport = createGameViewport($('canvas'));
const gameWindow = createGameWindow($('canvas'),{isBlocked:() => !$('rotate').hidden || !$('save-exchange').hidden || !$('problem').hidden});
const textInput = createTextInput($('text-input'), $('canvas'), viewport);
const audio = createGameAudio();
const exchangeView = createFilesView($('save-exchange'),{shadow:true});
const downloadSaves = createSaveDownloads();
$('save-exchange').setAttribute('aria-label',t('saves'));
// SDL's document listeners must not receive touches/keys used in the overlay.
for (const type of ['pointerdown','pointerup','pointermove','touchstart','touchmove','touchend','touchcancel','keydown','keyup','wheel']) {
  $('save-exchange').addEventListener(type,event => event.stopPropagation());
  $('problem').addEventListener(type,event => event.stopPropagation());
}
// The game in front: its last report texts (src/error_report.h), kept so a
// page that ends meanwhile (the system closed it) is told at the next start.
// Away from the front the system may close it, that's no problem.
const RUNNING_KEY = 'f2touch.running';
const runningMark = {
  get() { try { return localStorage.getItem(RUNNING_KEY); } catch { return null; } },
  set(texts) { try { localStorage.setItem(RUNNING_KEY, texts); } catch { /* Private mode: not kept. */ } },
  clear() { try { localStorage.removeItem(RUNNING_KEY); } catch { /* Nothing kept. */ } },
};
const hangWatch = createHangWatch();
let watchTimer = null;
let hangShown = false;
const engineTexts = module => { try { return JSON.parse(module.webErrorReport.texts()); } catch { return null; } };
let audioTiming = null;
let diagnosticsTimer = null;
$('text-input').setAttribute('aria-label', t('game_text'));
document.documentElement.lang = language;
$('repair').textContent = t('game_files');
$('message').textContent = t('checking');
$('play').textContent = t('play');
$('retry').textContent = t('try_again');
$('rotate').textContent = t('rotate');
$('notice').textContent = t('unofficial');
$('tagline').textContent = t('tagline');
$('privacy').textContent = t('privacy');
countVisit();
let source = null;
let starting = false;
let running = false;
let updating = false;
let downloading = false;
let filesMode = null;
const appCache = startAppCache({idle:() => !!source && !starting && !running,
  onApplying:value => { updating = value; rotatePrompt(); },
  onDownloading:value => { downloading = value; rotatePrompt(); }});

function rotatePrompt() {
  const portrait = innerHeight > innerWidth && matchMedia('(pointer:coarse)').matches;
  $('rotate').hidden = !(running && portrait);
  if (source && !starting && !running && !document.body.classList.contains('error')) {
    // A download doesn't hold Play: a game started meanwhile runs this build,
    // the new one comes after it.
    $('play').disabled = portrait || updating;
    $('message').textContent = updating ? t('updating') : portrait ? t('rotate') : downloading ? t('update_downloading') : '';
    $('message').hidden = !updating && !portrait && !downloading;
  }
}
// Every few seconds while playing: the hang watchdog (no new frames for 20 s,
// 60 s while loading) and the running mark.
function watchEngine(module) {
  hangWatch.reset(performance.now());
  clearInterval(watchTimer);
  watchTimer = setInterval(() => {
    const report = module.webErrorReport;
    if (!running || !report) return;
    if (document.hidden || !$('rotate').hidden || !$('save-exchange').hidden || (!hangShown && !$('problem').hidden)) {
      hangWatch.reset(performance.now());
      return;
    }
    runningMark.set(report.texts());
    const state = report.state();
    const stalled = hangWatch.stalled(performance.now(), state);
    if (hangShown) {
      if (!stalled) { hangShown = false; hideProblem($('problem')); }
    } else if (stalled) {
      const texts = engineTexts(module);
      if (!texts) return;
      hangShown = true;
      showProblem($('problem'), {texts, title:texts.hangTitle, message:texts.hang,
        report:texts.hangReport.replace('{state}', state.loading ? 'loading' : 'playing'), actions:[
          {label:texts.wait, run:() => { hangShown = false; hideProblem($('problem')); hangWatch.waited(performance.now()); }},
          {label:texts.close, run:() => { runningMark.clear(); location.reload(); }},
        ]});
    }
  }, 5000);
}

// The engine stopped while playing: why, with the report (an error) or what
// to do (the browser out of memory).
function crashed(module, reason) {
  const texts = module && engineTexts(module);
  error(t('start_failed'), reason);
  runningMark.clear();
  if (!texts) return;
  const reload = {label:t('try_again'), run:() => location.reload()};
  if (abortKind(reason) === 'memory') {
    showProblem($('problem'), {texts, title:texts.memoryTitle, message:texts.memory, actions:[reload]});
  } else {
    showProblem($('problem'), {texts, title:texts.crashTitle, message:texts.crash,
      report:texts.crashReport.replace('{reason}', abortDetail(reason)), actions:[reload]});
  }
}

function error(message, detail) {
  console.error(message, detail || '');
  clearInterval(watchTimer);
  if (detail?.name !== 'StorageBusyError') countEvent('start-failed');
  textInput.end();
  audio.stop();
  audioTiming?.stop();
  clearInterval(diagnosticsTimer);
  $('diagnostics').hidden = true;
  document.body.classList.remove('playing');
  document.body.classList.add('error');
  $('message').textContent = detail?.name === 'StorageBusyError' ? t('already_open') : message;
  $('message').hidden = false;
  $('progress').hidden = true;
  $('play').hidden = true;
  $('retry').hidden = false;
  $('repair').hidden = false;
  running = false;
  rotatePrompt();
}

async function exited(status) {
  // This runs outside the finished WASM stack. Restore the already-loaded
  // document, including when the network is off; a reload could strand a PWA
  // on a black canvas until its HTML/JS becomes reachable again.
  textInput.end();
  audioTiming?.stop();
  audioTiming = null;
  clearInterval(diagnosticsTimer);
  diagnosticsTimer = null;
  audio.stop();
  running = starting = false;
  clearInterval(watchTimer);
  runningMark.clear();
  delete window.falloutDev;
  $('diagnostics').hidden = true;
  document.body.classList.remove('playing');
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  if (status !== 0) { error(t('start_failed'), 'Engine exit status: ' + status); return; }
  document.body.classList.remove('error');
  $('progress').hidden = true;
  $('retry').hidden = true;
  $('repair').hidden = true;
  $('play').hidden = !source;
  rotatePrompt();
  if (filesMode !== null) {
    const mode = filesMode; filesMode = null;
    await source?.close();
    location.href = './files.html?mode=' + encodeURIComponent(mode);
    return;
  }
  appCache.check();
}

// The page ended while the game was in front the last time.
function previousRun() {
  const saved = runningMark.get();
  runningMark.clear();
  let texts = null;
  try { texts = saved && JSON.parse(saved); } catch { return; }
  if (!texts) return;
  showProblem($('problem'), {texts, title:texts.crashTitle, message:texts.crashBefore, report:texts.crashBeforeReport,
    actions:[{label:t('back'), run:() => hideProblem($('problem'))}]});
}

async function prepare() {
  previousRun();
  await exchangeView.ready;
  if (typeof WebAssembly !== 'object') throw new Error(t('unsupported'));
  const response = await fetch(new URL('./build-info.json', import.meta.url), {cache:'no-store'});
  if (!response.ok) throw new Error('Cannot load build-info.json.');
  const build = await response.json();
  document.body.dataset.version = build.version;
  document.body.dataset.webBuild = build.buildId || build.files['fallout2-ce.wasm'].sha256.slice(0, 8);
  $('build-tag').textContent = build.version + ' · ' + document.body.dataset.webBuild;
  source = await developmentFiles() || await localFiles();
  if (!source) { location.replace('./files.html'); return; }
  $('play').hidden = !source;
  $('dev-note').textContent = t('dev_note');
  $('dev-note').hidden = source.kind === 'local';
  const hint = source.kind === 'local' && installHint();
  $('install-hint').textContent = hint ? t(hint) : '';
  $('install-hint').hidden = !hint;
  rotatePrompt();
  appCache.check();
}

async function play() {
  if (starting || running || updating || !source) return;
  if (innerHeight > innerWidth && matchMedia('(pointer:coarse)').matches) return;
  starting = true;
  $('storage-warning').hidden = true;
  $('play').disabled = true;
  $('message').textContent = t('loading');
  $('message').hidden = false;
  $('progress').hidden = false;
  let engine;
  try {
    // Both permission-sensitive actions happen in the original click, before
    // any asynchronous setup. SDL will share this already unlocked context.
    const SDL2 = audio.start();
    let fullscreen;
    try {
      fullscreen = document.documentElement.requestFullscreen?.({navigationUI:'hide'}).catch(() => {});
    } catch { /* Fullscreen is optional, e.g. in an iOS Home Screen app. */ }
    const module = engine = await globalThis.createFallout2({
      canvas:$('canvas'), noInitialRun:true, webMouseAsTouch:source.mouseAsTouch,
      // The engine has no version of its own (app_version.h).
      webAppVersion:document.body.dataset.version, webAppBuild:document.body.dataset.webBuild,
      SDL2, webTextInput:textInput, webViewport:viewport,webWindow:gameWindow,
      webOpenFiles:mode => { filesMode = mode; },
      locateFile:name => new URL('./' + name, import.meta.url).href,
      print:message => console.log(message), printErr:message => console.warn(message),
      onAbort:reason => {
        engine?.webUserFiles?.close();
        if (running) crashed(engine, reason);
        else error(t('start_failed'), reason);
      },
      onExit:status => queueMicrotask(() => {
        // The manager acquires both writer leases. Await their release instead
        // of racing a new page against the engine's asynchronous shutdown.
        Promise.resolve(engine?.webUserFiles?.close()).then(() => exited(status))
          .catch(exception => error(t('start_failed'), exception));
      }),
      onUserWriteSuccess:() => { $('storage-warning').hidden = true; },
      onUserWriteFailure:failure => {
        $('storage-warning').hidden = false;
        $('storage-warning').textContent = failure.fatal
          ? t('write_disabled')
          : failure.kind === 'QuotaExceededError' ? t('write_no_space') : t('write_failed');
      },
    });
    await mountGame(module, source);
    module.webExchangeSaves = source.kind === 'local' ? createSaveSession({
      host:$('save-exchange'),root:exchangeView.root,canvas:$('canvas'),user:module.webUserFiles,
      source,audio,textInput,buildId:document.body.dataset.webBuild,download:downloadSaves,
    }) : async () => { throw new Error('Save exchange requires locally installed game files'); };
    // Explicit test URLs only; the ordinary installed launcher has no debug API.
    const parameters = new URLSearchParams(location.search);
    const inspect = source && ['dev','stats'].some(key => parameters.has(key));
    if (inspect) window.falloutDev = {module, stats:() => module.GameFS.stats(),
      audio:audio.status, geometry:() => ({viewport:[innerWidth, innerHeight],
        canvas:[$('canvas').width, $('canvas').height],
        css:[$('canvas').getBoundingClientRect().width, $('canvas').getBoundingClientRect().height],
        dpr:devicePixelRatio, renderer:module.webRendererInfo})};
    if (inspect && parameters.get('stats') === '1') {
      // Only this explicit dev session gets frame timing. Root configs live in
      // writable MEMFS; the Mac's source config is never changed.
      const config = '/game/' + gameConfigPath(module.FS,'/game');
      const previous = module.FS.analyzePath(config).exists ? module.FS.readFile(config) : new Uint8Array();
      const suffix = new TextEncoder().encode('\n[debug]\nperf_log=1\n');
      const measuredConfig = new Uint8Array(previous.length + suffix.length);
      measuredConfig.set(previous);
      measuredConfig.set(suffix, previous.length);
      module.FS.writeFile(config, measuredConfig);
      audioTiming = createAudioTiming({active:() => !document.hidden && audio.status().state === 'running'});
      SDL2.audioContext?.addEventListener('statechange', () => audioTiming?.resetGap());
      window.falloutDev.audioTiming = audioTiming.status;
      window.falloutDev.frameTiming = () => module.webFrameTiming;
      diagnosticsTimer = setInterval(() => {
        audioTiming.attach(module.SDL2?.output ?? module.SDL2?.audio?.scriptProcessorNode);
        const timing = audioTiming.status();
        const state = audio.status();
        const frame = module.webFrameTiming;
        $('diagnostics').hidden = false;
        $('diagnostics').textContent = `${t('test_build')} ${document.body.dataset.webBuild}\n`
          + `Audio: ${state.state}, ${state.sampleRate || '?'} Hz, ${timing.bufferSize || '?'} frames, `
          + (state.queue ? `queue ${state.queue.frames}, underruns ${state.queue.underruns}\n` : 'ScriptProcessor\n')
          + `5s gap: ${timing.recent.maxGapMs.toFixed(1)} ms; work: ${timing.recent.maxWorkMs.toFixed(1)} ms; late: ${timing.recent.late}\n`
          + `All gap: ${timing.maxGapMs.toFixed(1)} ms; work: ${timing.maxWorkMs.toFixed(1)} ms`
          + (frame ? `\nFrame: ${frame.fps.toFixed(0)} fps; UI/present: ${frame.uiMs.toFixed(1)}/${frame.presentMs.toFixed(1)} ms` : '');
      }, 1000);
    }
    await fullscreen;
    document.body.classList.add('playing');
    // Measure the final fullscreen canvas, after layout has applied its CSS.
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    $('canvas').focus();
    running = true;
    rotatePrompt();
    countEvent(installedApp() ? 'start-app' : 'start-browser');
    watchEngine(module);
    module.callMain([]);
  } catch (exception) {
    engine?.webUserFiles?.close();
    error(t('start_failed'), exception);
  } finally {
    starting = false;
  }
}

for (const type of ['touchstart', 'touchmove', 'touchend']) {
  $('canvas').addEventListener(type, event => event.preventDefault(), {passive:false});
}
$('canvas').addEventListener('contextmenu', event => event.preventDefault());
$('canvas').addEventListener('gesturestart', event => event.preventDefault(), {passive:false});
$('play').addEventListener('click', play);
$('retry').addEventListener('click', () => location.reload());
window.addEventListener('resize', rotatePrompt);
for (const type of ['pointerdown', 'touchend', 'keydown']) {
  document.addEventListener(type, audio.resume, {capture:true, passive:true});
}
document.addEventListener('visibilitychange', () => {
  if (document.hidden) runningMark.clear();
  audioTiming?.resetGap();
  if (!document.hidden) audio.resume();
});
window.addEventListener('pagehide', () => runningMark.clear());
window.addEventListener('pageshow', event => {
  if (event.persisted && source?.kind === 'local') { location.reload(); return; }
  audioTiming?.resetGap();
  audio.resume();
});
prepare().catch(exception => error(t('start_failed'), exception));
