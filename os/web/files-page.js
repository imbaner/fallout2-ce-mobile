import {createFilesView} from './files-view.js';
import {createFilesManager} from './files.js';
import {openResourceStore,FIRST_GAME,gameId} from './resource-store.js';
import {gameName,uniqueGameName} from './games.js';
import {forgetGamePicture} from './save-streams.js';
import {startAppCache} from './app-cache.js';
import {webText as t} from './strings.js';
import {countVisit} from './visits.js';
document.title = 'F2 Touch — ' + t('game_files').toLowerCase();
createFilesView(document.body);
// The in-game overlay shares this view but must not navigate away from the game.
document.getElementById('privacy').textContent = t('privacy');
document.getElementById('privacy').hidden = false;
countVisit();
// files.html?game=<id>[&add=1]: the list of games' game (a new one: add).
const parameters = new URLSearchParams(location.search);
let game = FIRST_GAME;
try { game = gameId(parameters.get('game') || FIRST_GAME); } catch { /* The first game. */ }
const adding = parameters.get('add') === '1';
createFilesManager({mode:parameters.get('mode'),game,adding,
  // Named after the archive (or the game's folder in it) unless the player
  // named it, played next.
  async onInstalled(plan) {
    const store = await openResourceStore({game});
    try {
      const list = await store.games(), now = Date.now();
      const known = list.games[game] || {name:'',added:now,played:0};
      if (!known.name) known.name = uniqueGameName(list,game,gameName(plan));
      list.games[game] = {...known,played:now};
      list.current = game;
      await store.saveGames(list);
    } finally { await store.close(); }
    // Other files: maybe another menu picture.
    if (!adding) await forgetGamePicture({game}).catch(error => console.warn('Card picture kept',error));
  }});
// Registers the offline cache on a first run that starts here; new builds
// are applied by the launcher.
startAppCache();
addEventListener('pageshow',event => { if (event.persisted) location.reload(); });
