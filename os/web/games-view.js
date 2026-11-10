// The launcher's list of games (as Android's GamesActivity): the games at
// the left, the one played last first, "Add game" under them; the chosen
// one's card at the right (its menu picture, newest save, saves, size,
// language, mods) with Delete, Details and Play. Details: all of it, Rename
// and Replace files. A narrow picture (the game's own 640x480 menu picture
// right of its button plates) is as tall as the card and fades into it at
// its left, as in the game's menu; a wide one fills it.
import {webText as t, webCount, language} from './strings.js';
import {gameCard,displayName,renameGame,deleteGame,newGameId} from './games.js';

const el = (tag, attributes = {}, children = []) => {
  const node = document.createElement(tag);
  for (const [key,value] of Object.entries(attributes)) {
    if (key === 'text') node.textContent = value;
    else if (key === 'class') node.className = value;
    else if (key.startsWith('on')) node.addEventListener(key.slice(2),value);
    else node.setAttribute(key,value);
  }
  for (const child of children) if (child) node.append(child);
  return node;
};
const size = bytes => bytes >= 1024 ** 3 ? (bytes / 1024 ** 3).toFixed(2) + ' GB' : Math.round(bytes / 1024 ** 2) + ' MB';
const backIcon = () => {
  const svg = document.createElementNS('http://www.w3.org/2000/svg','svg');
  svg.setAttribute('viewBox','0 0 24 24'); svg.setAttribute('aria-hidden','true');
  const path = document.createElementNS('http://www.w3.org/2000/svg','path');
  path.setAttribute('d','M20 12H4.5M10.5 5.5L4 12l6.5 6.5');
  svg.append(path); return svg;
};

function languageName(value) {
  const codes = {english:'en',russian:'ru',german:'de',french:'fr',italian:'it',spanish:'es',polish:'pl',portuguese:'pt'};
  const code = codes[String(value).toLowerCase()];
  if (!code) return value;
  try { return new Intl.DisplayNames([language],{type:'language'}).of(code); } catch { return value; }
}

function whenPlayed(played) {
  if (!played) return t('games_never');
  const date = new Date(played), now = new Date();
  const time = date.toLocaleTimeString(language,{hour:'2-digit',minute:'2-digit'});
  const day = value => new Date(value.getFullYear(),value.getMonth(),value.getDate()).getTime();
  const days = Math.round((day(now) - day(date)) / 86400000);
  if (days === 0) return t('games_today',time);
  if (days === 1) return t('games_yesterday',time);
  return date.toLocaleDateString(language,{day:'numeric',month:'long'});
}

// [host]: the element it fills; [listed]: listGames(); [onPlay(id)]: Play
// (in the click: the page starts sound and full screen there); [onAdd(id)],
// [onReplace(id)]: the files page for a new game or this one's files.
export function createGamesView({host,listed,onPlay,onAdd,onReplace,onChanged}) {
  let selected = listed.games.some(game => game.id === listed.current) ? listed.current : listed.games[0]?.id;
  const cards = new Map(), urls = new Map();
  let details = false, busy = false;

  const list = el('ul',{class:'games-list',role:'listbox','aria-label':t('games_add')});
  const add = el('button',{type:'button',class:'games-add',onclick:() => showAdd()},[el('span',{class:'plus','aria-hidden':'true',text:'+'}),el('span',{text:t('games_add')})]);
  const picture = el('img',{class:'games-picture',alt:''});
  const noPicture = el('p',{class:'games-no-picture',text:t('games_picture_later')});
  const name = el('h2',{class:'games-name'});
  const last = el('p',{class:'games-last'}), stats = el('p',{class:'games-stats'}), mods = el('p',{class:'games-mods'});
  const cover = el('div',{class:'games-cover'},[picture,noPicture,el('div',{class:'games-shade'}),name,el('div',{class:'games-info'},[last,stats,mods])]);
  const play = el('button',{type:'button',class:'primary',text:t('play'),onclick:() => { if (!busy && selected) onPlay(selected); }});
  const actions = el('div',{class:'games-actions'},[
    el('button',{type:'button',class:'danger',text:t('games_delete'),onclick:() => confirmDelete()}),
    el('button',{type:'button',text:t('games_details'),onclick:() => showDetails(true)}),
    play]);
  const page = el('div',{class:'games-page'},[
    el('div',{class:'games-left'},[el('div',{class:'games-panel games-scroll'},[list]),add]),
    el('div',{class:'games-panel games-card'},[cover,actions])]);

  const detailsPicture = el('img',{class:'games-details-picture',alt:''});
  const detailsName = el('h2',{class:'games-details-name'}), detailsVersion = el('p',{class:'games-details-version'});
  const rows = el('div',{class:'games-rows'});
  const detailsPage = el('div',{class:'games-page games-details',hidden:''},[
    el('div',{class:'games-panel games-details-left'},[el('div',{class:'games-details-frame'},[detailsPicture]),detailsName,detailsVersion]),
    el('div',{class:'games-panel games-details-right'},[el('div',{class:'games-scroll'},[rows]),
      el('div',{class:'games-actions'},[
        el('button',{type:'button',text:t('games_rename'),onclick:() => showRename()}),
        el('button',{type:'button',text:t('games_replace'),onclick:() => { if (selected) onReplace(selected); }})])])]);
  const back = el('button',{type:'button',class:'games-back','aria-label':t('games_back'),hidden:'',onclick:() => showDetails(false)},[backIcon()]);
  const dialog = el('div',{class:'games-dialog',hidden:'',role:'dialog','aria-modal':'true'});
  host.replaceChildren(page,detailsPage,back,dialog);

  // A picture much narrower than the card (the game's menu art right of
  // its button plates) as tall as the card, else filling it; again on every
  // size of the card (rotation, window), as Android's CoverImageView.
  function fit() {
    const ready = picture.complete && picture.naturalWidth && cover.clientWidth && cover.clientHeight;
    cover.classList.toggle('narrow',!!ready && picture.naturalWidth / picture.naturalHeight < (cover.clientWidth / cover.clientHeight) * 0.75);
  }
  picture.addEventListener('load',fit);
  const resized = typeof ResizeObserver === 'function' ? new ResizeObserver(fit) : null;
  resized?.observe(cover);
  const game = id => listed.games.find(item => item.id === id);
  function cover_(image,card) {
    if (!card?.picture) { image.removeAttribute('src'); return; }
    if (!urls.has(card)) urls.set(card,URL.createObjectURL(card.picture));
    image.src = urls.get(card);
  }

  function render() {
    list.replaceChildren(...listed.games.map(item => {
      const card = cards.get(item.id);
      const thumb = el('span',{class:'games-thumb'},[]);
      if (card?.picture) { const image = el('img',{alt:''}); cover_(image,card); thumb.append(image); }
      const row = el('li',{role:'option','aria-selected':String(item.id === selected),tabindex:'0',class:item.id === selected ? 'selected' : '',
        onclick:() => { selected = item.id; render(); },
        onkeydown:event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); selected = item.id; render(); } }},
        [thumb,el('span',{class:'games-row-text'},[el('span',{class:'games-row-name',text:displayName(item,card,t('games_unnamed','%1$s'))}),
          el('span',{class:'games-row-when',text:whenPlayed(item.played)})])]);
      return row;
    }));
    const item = game(selected);
    if (!item) return;
    const card = cards.get(item.id);
    name.textContent = displayName(item,card,t('games_unnamed','%1$s'));
    cover_(picture,card);
    picture.hidden = !card?.picture; noPicture.hidden = !card || !!card.picture;
    fit();
    last.textContent = !card ? '' : !card.saves ? t('games_no_saves') : card.place ? t('games_last_save',card.place) : '';
    stats.textContent = !card ? '' : [card.saves ? webCount('games_saves',card.saves) : null,size(card.size),languageName(card.language)].filter(Boolean).join(' · ');
    mods.textContent = !card ? '' : !card.mods.length ? t('games_mods_none') : card.mods.length === 1 ? t('games_mods_one',card.mods[0])
      : t('games_mods_more',card.mods[0],card.mods.length - 1);
    if (details) renderDetails(item,card);
  }

  function renderDetails(item,card) {
    cover_(detailsPicture,card); detailsPicture.hidden = !card?.picture;
    detailsName.textContent = displayName(item,card,t('games_unnamed','%1$s'));
    // Not twice: an unnamed game is called by its version.
    detailsVersion.textContent = card?.version && card.version !== detailsName.textContent ? card.version : '';
    const row = (label,value) => el('div',{class:'games-row'},[el('span',{text:t(label)}),el('span',{text:value})]);
    rows.replaceChildren(...[
      card && row('games_label_saves',String(card.saves)),
      card && row('games_label_size',size(card.size)),
      card && row('games_label_language',languageName(card.language)),
      item.added && row('games_label_added',new Date(item.added).toLocaleDateString(language,{day:'numeric',month:'long',year:'numeric'})),
      row('games_label_played',whenPlayed(item.played)),
      card?.mods.length && el('h3',{text:t('games_mods_title',card.mods.length)}),
      ...(card?.mods || []).map(mod => el('p',{class:'games-mod',text:mod})),
    ].filter(Boolean));
  }

  function showDetails(show) {
    details = show; page.hidden = show; detailsPage.hidden = !show; back.hidden = !show; render();
  }

  // A window over the list: [withBack] its back button beside it, [input]
  // a field with that text (its last action gets the text).
  function showDialog(title,message,note,withBack,input,actionsList) {
    const field = input !== null ? el('input',{type:'text',maxlength:'60',value:input,'aria-label':title}) : null;
    const close = () => { dialog.hidden = true; dialog.replaceChildren(); };
    const panel = el('div',{class:'games-dialog-panel'},[
      el('h2',{text:title}),message ? el('p',{text:message}) : null,note ? el('p',{class:'note',text:note}) : null,field,
      el('div',{class:'games-dialog-buttons'},actionsList.map(([label,style,run],index) => el('button',{type:'button',class:style,text:label,
        onclick:() => { close(); if (run) run(field && index === actionsList.length - 1 ? field.value : undefined); }})))]);
    dialog.replaceChildren(el('div',{class:'games-dialog-row'},[panel,withBack ? el('button',{type:'button',class:'games-back','aria-label':t('games_back'),onclick:close},[backIcon()]) : null]));
    dialog.onclick = event => { if (event.target === dialog) close(); };
    dialog.hidden = false;
    (field || panel.querySelector('button.primary') || panel.querySelector('button'))?.focus();
  }
  const notice = text => showDialog(text,null,null,false,null,[['OK','primary',null]]);

  function showAdd() {
    if (busy) return;
    showDialog(t('games_add'),t('games_add_text'),t('games_add_note'),true,null,
      [[t('games_choose_zip'),'primary',async () => {
        busy = true;
        try { onAdd(await newGameId()); } catch (error) { busy = false; console.warn(error); notice(t('games_failed')); }
      }]]);
  }

  function confirmDelete() {
    const item = game(selected); if (!item || busy) return;
    const card = cards.get(item.id);
    showDialog(t('games_delete_title',displayName(item,card,t('games_unnamed','%1$s'))),
      t('games_delete_text',size(card?.size || 0),webCount('games_saves',card?.saves || 0)),
      card?.saves ? t('games_delete_note') : null,false,null,
      [[t('games_cancel'),'',null],[t('games_delete'),'danger',async () => {
        busy = true;
        try { await deleteGame(item.id); await onChanged(); }
        catch (error) { console.warn(error); notice(t('games_failed')); }
        finally { busy = false; }
      }]]);
  }

  function showRename() {
    const item = game(selected); if (!item) return;
    showDialog(t('games_rename_title'),null,null,false,displayName(item,cards.get(item.id),t('games_unnamed','%1$s')),
      [[t('games_cancel'),'',null],[t('games_save'),'primary',async value => {
        try { await renameGame(item.id,value); await onChanged(); } catch (error) { console.warn(error); notice(t('games_failed')); }
      }]]);
  }

  render();
  (async () => {
    for (const item of listed.games) {
      try { cards.set(item.id,await gameCard(item)); } catch (error) { console.warn('Game card not read',error); cards.set(item.id,null); }
      render();
    }
  })();

  return {
    get selected() { return selected; },
    get busy() { return busy; },
    set busy(value) { busy = value; play.disabled = value; },
    destroy() { resized?.disconnect(); for (const url of urls.values()) URL.revokeObjectURL(url); },
  };
}
