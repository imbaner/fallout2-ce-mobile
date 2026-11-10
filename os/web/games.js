// The list of games (as Android's Games.java/GamesActivity): each game an
// installation of its own in the resource database (resource-store.js
// `game`), its saves, settings and the port's records in its own part of the
// user's storage (`gamePrefix`). The registry - which games, their names,
// when added and played, the one played - is `games` in the resource state.
import {openResourceStore,FIRST_GAME,gameId,gamePrefix} from './resource-store.js';
import {userFileBytes,userFolderNames,userSettingText} from './user-report.js';
import {configName,configValue} from './game-config.js';
import {forgetGameFiles} from './save-streams.js';

const decoder = new TextDecoder();

// A new game's name: the game's folder in the archive, else the archive's.
export function gameName(plan) {
  const root = String(plan?.root || '').replace(/\/+$/,'');
  if (root) return root.slice(root.lastIndexOf('/') + 1);
  return String(plan?.filename || '').replace(/\.(zip|7z|rar|tar\.gz|tgz|tar)$/i,'').trim();
}

// [name], "[name] (2)" when another game of [list] has it.
export function uniqueGameName(list,id,name) {
  const taken = new Set(Object.entries(list.games).filter(([other]) => other !== id)
    .map(([,game]) => String(game.name || '').toLowerCase()));
  let candidate = name || '';
  for (let number = 2; candidate && taken.has(candidate.toLowerCase()); number++) candidate = name + ' (' + number + ')';
  return candidate;
}

// The games installed, the one played last first: {id, name, added,
// played, manifest} (`card` adds what the game wrote of itself).
export async function listGames() {
  const store = await openResourceStore();
  const games = [];
  let list;
  try {
    list = await store.games();
  } finally { await store.close(); }
  for (const [id,entry] of Object.entries(list.games)) {
    const source = await openResourceStore({game:id});
    try {
      const manifest = await source.active();
      if (manifest) games.push({id,name:entry.name || '',added:entry.added || manifest.completed || 0,played:entry.played || 0,manifest});
    } finally { await source.close(); }
  }
  games.sort((a,b) => (b.played - a.played) || (b.added - a.added));
  return {current:list.current,games};
}

// What the list shows of [game] beyond its installation: the card the game
// wrote at its menu (game_card.cc: card.txt, card.png), its saves, language
// and mods.
export async function gameCard(game) {
  const prefix = gamePrefix(game.id);
  const text = await userFileBytes(prefix + 'port/card.txt',{limit:64 * 1024}).catch(() => null);
  const values = {};
  for (const line of text ? decoder.decode(text).split('\n') : []) {
    const at = line.indexOf('=');
    if (at > 0) values[line.slice(0,at)] = line.slice(at + 1).trim();
  }
  const picture = await userFileBytes(prefix + 'port/card.png').catch(() => null);
  const slots = await userFolderNames(prefix + 'savegame').catch(() => []);
  const saves = slots.filter(name => /^slot\d+$/i.test(name)).length;
  // The player's saved config, else the installed one: its language.
  const ddraw = await userSettingText(prefix + 'settings/ddraw.ini').catch(() => '');
  const config = await userSettingText(prefix + 'settings/' + configName(ddraw)).catch(() => '');
  const mods = game.manifest.files.map(file => file.path).filter(path => /^mods\/[^/]+\.dat$/i.test(path))
    .map(path => path.slice(5)).sort((a,b) => a.localeCompare(b));
  return {version:values.version || '',place:values.place || '',
    picture:picture ? new Blob([picture],{type:'image/png'}) : null,saves,
    language:configValue(config,'system','language') || 'english',
    size:game.manifest.files.reduce((sum,file) => sum + file.size,0),mods};
}

// The name the list shows: the player's, else the game's version line,
// else "Game <id>" ([unnamed]).
export function displayName(game,card,unnamed) {
  return game.name || card?.version || unnamed.replace('%1$s',game.id);
}

async function change(action) {
  const store = await openResourceStore();
  try {
    const list = await store.games();
    const result = await action(list,store);
    await store.saveGames(list);
    return result;
  } finally { await store.close(); }
}

// A new game's id (nothing of it stored until its files are in).
export async function newGameId() {
  // One store at a time: the database's lease is exclusive.
  let list;
  const store = await openResourceStore();
  try { list = await store.games(); } finally { await store.close(); }
  let next = 1;
  for (const id of Object.keys(list.games)) next = Math.max(next,Number(id) + 1);
  // An installation of an import given up stays unlisted: not over it.
  for (;;) {
    const probe = await openResourceStore({game:String(next)});
    try { if (!(await probe.active())) return String(next); } finally { await probe.close(); }
    next++;
  }
}

// [id] played from now on.
export function setCurrentGame(id) {
  gameId(id);
  return change(list => {
    list.games[id] = {...(list.games[id] || {name:'',added:Date.now()}),played:Date.now()};
    list.current = id;
  });
}

export function renameGame(id,name) {
  gameId(id);
  return change(list => {
    if (!list.games[id]) return;
    list.games[id] = {...list.games[id],name:uniqueGameName(list,id,String(name || '').trim().slice(0,60))};
  });
}

// [id]'s installation and its part of the user's storage, gone.
export async function deleteGame(id) {
  gameId(id);
  const store = await openResourceStore();
  try { await store.forgetGame(id); } finally { await store.close(); }
  await forgetGameFiles({game:id});
}

export {FIRST_GAME,gamePrefix};
