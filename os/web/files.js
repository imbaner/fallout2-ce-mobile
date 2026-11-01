import {nativeText as n} from './import-native.js';
import {installHint} from './install-hint.js';
import {countEvent} from './visits.js';
import {language, webText as t} from './strings.js';

export function createFilesManager({root = document, mode = null, resources = null, buildId = null, onReturn = () => { location.href = './index.html'; }, download = null} = {}) {
  const $ = id => root.getElementById(id);
  const surface = root.host || document.body;
  document.documentElement.lang = language;
  const size = value => (value / 1024 ** 2).toFixed(1) + t('mib');
  let worker, active = resources?.active, plan, file, pickMode = 'game', busy = true, downloaded = null, cancelling = false, pendingAction = null;
  let previousStep = null, returning = false, doneTimer = null, failedWorker = false;
  const initialMode = mode;
  const labels = {
    title:n('import_title'), saves:n('saves_title'), choose:n('import_choose_archive'),
    play:resources ? n('import_back_to_game') : t('play'), other:n('import_choose_other'), cancel:n('import_cancel'),
  };
  const errors = {
    cancelled:t('error_cancelled'),
    no_space:t('error_no_space'),
    invalid_zip:t('error_invalid_zip'),
    invalid_path:t('error_invalid_path'),
    unsupported_zip:t('error_unsupported_zip'),
    missing_game:t('error_missing_game'),
    base_required:t('error_base_required'),
    ambiguous_root:t('error_ambiguous_root'),
    limit:t('error_limit'),
    invalid_save:t('error_invalid_save'),
    no_saves:t('error_no_saves'),
    slot_limit:t('error_slot_limit'),
    busy:t('error_busy'),
    stale_plan:t('error_stale_plan'),
    recovery_needed:t('error_recovery_needed'),
  };
  function revoke() { if (downloaded) URL.revokeObjectURL(downloaded); downloaded = null; $('download').hidden = true; }
  function buttons(primary,secondary) {
    for (const [id,action] of [['primary',primary],['secondary',secondary]]) {
      $(id).hidden = !action; $(id).disabled = false;
      if (action) { $(id).textContent = action[0]; $(id).onclick = action[1]; }
    }
  }
  // The corner arrow has Android's meaning: leave the manager for the game.
  // A previous step (e.g. replace confirmation -> review) is separate, like
  // Android's system Back, and is also available through Escape here.
  function back(action) { previousStep = action; $('back').hidden = busy || !active; }
  function show(title,message,details = '',note = '') {
    clearTimeout(doneTimer); doneTimer = null;
    $('download').hidden = true;
    $('title').textContent = title; $('message').textContent = message; $('details').textContent = details; $('note').textContent = note;
    $('progress').hidden = $('progress-text').hidden = true; $('saves-actions').hidden = true; back(null);
    surface.classList.remove('error');
  }
  async function play() {
    busy = true;
    try { await onReturn(); }
    catch (error) {
      busy = false; returning = false;
      show(labels.saves,t('return_failed'),String(error.message || error));
      buttons([t('retry'),play]); back(null);
    }
  }
  function returnToGame() {
    if (busy || !active) return;
    returning = true;
    show(labels.title,t('returning'));
    if (failedWorker) { play(); return; }
    // Dispose staged imports and release their leases before the next engine
    // starts. Merely terminating the worker leaves cleanup to a future launch.
    send('discard');
  }
  function importWelcome() {
    pickMode = 'saves';
    show(labels.saves,t('saves_choose_zip'));
    buttons([labels.choose,() => choose('saves')]);
  }
  function welcome() {
    if (resources) {
      if (initialMode === 'saves_export') { show(labels.saves,n('saves_exporting')); send('export-saves'); }
      else importWelcome();
      return;
    }
    plan = null; revoke();
    show(labels.title,active ? t('game_installed_here')
      : t('game_choose_zip'),
      active ? `${active.files.length} ${t('files')} · ${size(active.measurements.bytes)}` : '',
      [t('zip_supported'),
        // Before the first installation: on iOS the Home Screen app does not see it.
        !active && installHint() && t(installHint())].filter(Boolean).join('\n\n'));
    buttons([active ? t('replace_game') : labels.choose,() => choose('game')],active ? [labels.play,play] : null);
    $('saves-actions').hidden = !active;
  }
  function choose(mode) { pickMode = mode; $('archive').click(); }
  function send(type,extra = {}) {
    if (busy) return;
    pendingAction = type;
    busy = true; cancelling = false; revoke(); $('saves-actions').hidden = true; back(null);
    $('progress').hidden = false; $('progress').removeAttribute('value'); $('progress-text').hidden = false;
    buttons(null,['inspect','inspect-saves','install','install-saves','export-saves','export-before-replace'].includes(type)
      ? [labels.cancel,() => { cancelling = true; $('secondary').disabled = true; $('message').textContent = t('cancelling'); worker.postMessage({type:'cancel'}); }] : null);
    if (type === 'install-saves') $('message').textContent = n('saves_copying');
    worker.postMessage({type,...extra,...(type === 'status' && resources ? {resources} : {})});
  }
  function inspect(mode,chosen,rootHint) {
    file = chosen;
    show(mode === 'saves' ? labels.saves : labels.title,t('checking_archive'));
    send(mode === 'saves' ? 'inspect-saves' : 'inspect',{file:chosen,rootHint});
  }
  function review() {
    const saves = plan.kind === 'saves';
    if (saves) {
      const compatibility = ['saves_count_same','saves_count_likely','saves_count_unlikely','saves_count_unknown'];
      const changeText = ([kind,path]) => n(['saves_change_game','saves_change_updated','saves_change_added','saves_change_removed'][Number(kind)] || 'saves_change_removed',path.split('/').at(-1));
      show(labels.saves,[n('saves_found',plan.found),
        ...plan.compatibility.map((count,i) => count ? n(compatibility[i],count) : ''),
        plan.skipped ? n('saves_count_duplicate',plan.skipped) : ''].filter(Boolean).join('\n'),
        plan.saves.map(s => `${[' ','?','×','·'][s.level]} ${s.title || t('untitled')}${s.date ? '  ' + s.date : ''}${s.duplicate ? '  (' + n('saves_already_there') + ')' : ''}` + s.changes.map(change => '\n    ' + changeText(change)).join('')).join('\n'),
        plan.addError ? errors.slot_limit : '');
      buttons(!plan.addError && plan.added ? [n('saves_add'),() => send('install-saves',{token:plan.token})] : null,
        !plan.replaceError ? [n('saves_replace_all'),confirmReplace] : null);
      back(() => send('discard'));
    } else if (plan.choices) {
      // Fallout 2 and a mod in its subfolder that uses its files (Android's
      // ImportActivity.showChoice): which one to install. More mods than one:
      // the archive with one of them.
      if (plan.choices.length !== 2) {
        show(labels.title,errors.ambiguous_root); buttons([t('try_again'),() => send('discard')],null); return;
      }
      const [base,mod] = plan.choices, name = mod.replace(/\/$/,'').split('/').at(-1);
      show(labels.title,t('import_choose_game',name));
      buttons([name,() => send('inspect',{file,choice:mod})],[t('fallout2'),() => send('inspect',{file,choice:base})]);
      back(() => send('discard'));
    } else {
      const game = plan.description, delta = plan.changes;
      show(labels.title,n('import_found'),
        `${t('language')}: ${game.language}\n${t('mods')}: ${game.mods.join(', ') || t('no_dat_mods')}`
        + (game.looseLogic ? '\n' + t('loose_mod_files') : '')
        + `\n${plan.files} ${t('files')} · ${size(plan.bytes)}`
        + (plan.replaces ? '\n' + t('install_delta', delta.added, delta.replaced, delta.sameSize, delta.removed) : '')
        + (plan.mode === 'mods' ? '\n' + t('mods_keep_base', plan.keptFiles) : '')
        + `\n${t('storage_reserve')}: ${size(plan.space.requiredUpper)}`
        + (plan.archiveSaves ? '\n' + t('archive_saves', plan.archiveSaves) : ''),
        t('rpu_keeps_saves'));
      buttons([plan.replaces ? n('import_replace') : t('install'),() => send('install',{token:plan.token})],[labels.other,() => send('discard')]);
    }
  }
  function confirmReplace() {
    if (!plan) return;
    if (!plan.current && !plan.replaceConverted) { send('install-saves',{token:plan.token,replaceAll:true}); return; }
    show(labels.saves,plan.current ? n('saves_replace_confirm',plan.current) : t('restore_saves_confirm'),
      plan.replaceConverted ? t('quick_slots_fewer', plan.replaceConverted) : '');
    buttons([n('saves_replace'),() => send('install-saves',{token:plan.token,replaceAll:true})],
      plan.current ? [n('saves_export_first'),() => send('export-before-replace',{token:plan.token})] : null);
    back(review);
  }
  function complete(action = returnToGame) {
    busy = true; back(null); buttons(null,null);
    doneTimer = setTimeout(() => { busy = false; action(); },1500);
  }
  function exported(result,after = null) {
    if (download) {
      function attempt() {
        try {
          download(result);
          show(labels.saves,t('zip_download_requested'));
          complete(after || returnToGame);
        } catch (error) {
          busy = false;
          show(labels.saves,t('zip_download_failed'),String(error.message || error));
          buttons([t('retry'),attempt]);
          back(null);
        }
      }
      attempt();
      return;
    }
    downloaded = URL.createObjectURL(result.blob);
    show(labels.saves,n('saves_exported',result.slots),
      t('zip_save_hint'));
    // WebKit's Web Share path uses ReadAsArrayBuffer for the entire file before
    // opening the sheet. The iPhone >4GiB probe confirmed that download works
    // while share fails reading. Keep one disk-backed download path at all sizes.
    $('download').href = downloaded; $('download').download = result.filename; $('download').textContent = t('save_zip'); $('download').hidden = false;
    // Browsers cannot report when the file has actually reached Files. Keep
    // its URL alive until the user leaves; never auto-return after a download.
    buttons(null,after ? [n('saves_replace_all'),after] : null);
  }
  async function start() {
    $('saves-import').textContent = t('import_saves'); $('saves-export').textContent = t('export_saves');
    $('back').setAttribute('aria-label',n('import_back_to_game'));
    $('progress').setAttribute('aria-label',t('processing_files'));
    if (!isSecureContext || !navigator.locks || !globalThis.indexedDB) throw new Error(t('https_required'));
    if (!buildId) {
      const response = await fetch('./build-info.json',{cache:'no-store'}); if (!response.ok) throw new Error('build-info');
      buildId = (await response.json()).buildId;
    }
    $('notice').textContent = t('unofficial');
    $('build').textContent = buildId;
    failedWorker = false;
    worker = new Worker(new URL('./probe-worker.js',import.meta.url),{type:'module'});
    worker.onerror = () => {
      worker.terminate(); failedWorker = true; busy = false;
      show(labels.title,t('interrupted'));
      buttons([t('retry'),() => start().catch(startFailure)]); back(null);
    };
    worker.onmessage = ({data}) => {
      if (data.type === 'progress') {
        const value = data.value;
        if (value.cancelAllowed === false) buttons(null,null);
        if (value.phase === 'commit' || value.phase === 'cleanup') { $('message').textContent = t('finishing_install'); return; }
        if (value.phase === 'complete') return;
        $('progress').max = value.total || 1; $('progress').value = value.bytes || 0;
        $('progress-text').textContent = `${size(value.bytes || 0)} / ${size(value.total || 0)} · ${value.file || ''}`;
        if (!cancelling) $('message').textContent = value.phase === 'export' ? t('preparing_save_zip') : t('preparing_files');
        return;
      }
      const oldPlan = plan; busy = false;
      if ('active' in data) active = data.active;
      surface.classList.toggle('has-game',!!active);
      if ('font' in data) (root.host || document.documentElement).dataset.font = data.font === 1 ? 'mono' : 'classic';
      if (data.type === 'error') {
        returning = false;
        if (resources && data.cancelled) { returnToGame(); return; }
        if (resources && data.code === 'no_saves' && pendingAction === 'export-saves') {
          show(labels.saves,n('saves_export_none')); complete(); return;
        }
        if (data.code !== 'cancelled') countEvent('import-error-' + (errors[data.code] ? data.code : 'other'));
        plan = null; show(resources ? labels.saves : labels.title,errors[data.code] || t('operation_failed'));
        $('details').textContent = data.message; surface.classList.add('error');
        buttons([t('try_again'),welcome],active ? [labels.play,play] : null);
        return;
      }
      plan = data.plan;
      if (data.action === 'install' || data.action === 'install-saves') countEvent(data.action === 'install' ? 'files-installed' : 'saves-imported');
      if (returning) { returning = false; play(); return; }
      if (['export-saves','export-before-replace'].includes(data.action)) { exported(data.exchange,data.action === 'export-before-replace' ? confirmReplace : null); return; }
      if (plan) { review(); return; }
      if (data.action === 'install' && oldPlan?.archiveSaves && file) {
        show(labels.title,t('installed_with_saves'),t('review_saves_hint'));
        buttons([t('review_saves'),() => inspect('saves',file,oldPlan.root)],[labels.play,play]); return;
      }
      if (data.action === 'install-saves') {
        show(labels.saves,n('saves_done',data.exchange.added),data.exchange.skipped ? n('saves_count_duplicate',data.exchange.skipped) : '');
        complete(); return;
      }
      if (data.action === 'discard' && oldPlan?.kind === 'saves') { importWelcome(); return; }
      welcome();
      if (data.action === 'status' && initialMode === 'saves_import') {
        importWelcome();
      } else if (data.action === 'status' && initialMode === 'saves_export') {
        show(labels.saves,n('saves_exporting')); send('export-saves');
      }
    };
    busy = false; send('status');
  }
  $('archive').onchange = async () => {
    const chosen = $('archive').files[0]; $('archive').value = '';
    if (!chosen || busy) return;
    // Lock the UI before any permission-sensitive promise can yield.
    busy = true; buttons(null,null);
    try { await navigator.storage?.persist?.(); } catch {}
    busy = false;
    if (pickMode === 'game') countEvent('archive-chosen');
    inspect(pickMode,chosen);
  };
  $('back').onclick = returnToGame;
  const keydown = event => {
    if (event.key !== 'Escape' || busy) return;
    event.preventDefault();
    if (previousStep) previousStep(); else returnToGame();
  };
  root.addEventListener('keydown',keydown);
  $('saves-import').onclick = importWelcome;
  $('saves-export').onclick = () => { show(labels.saves,n('saves_exporting')); send('export-saves'); };
  function destroy() {
    clearTimeout(doneTimer); revoke();
    if (worker) { worker.onmessage = worker.onerror = null; worker.terminate(); }
    root.removeEventListener('keydown',keydown); removeEventListener('pagehide',destroy);
  }
  addEventListener('pagehide',destroy,{once:true});
  function startFailure(error) {
    busy = false; failedWorker = true;
    show(labels.title,t('storage_open_failed'),String(error.message || error));
    buttons([t('retry'),() => { worker?.terminate(); start().catch(startFailure); }]); back(null);
  }
  show(labels.saves,t('opening_storage')); buttons(null,null);
  const ready = start().catch(startFailure);
  return {ready,destroy};
}

