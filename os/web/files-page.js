import {createFilesView} from './files-view.js';
import {createFilesManager} from './files.js';
import {startAppCache} from './app-cache.js';
import {webText as t} from './strings.js';
import {countVisit} from './visits.js';
document.title = 'F2 Touch — ' + t('game_files').toLowerCase();
createFilesView(document.body);
// The in-game overlay shares this view but must not navigate away from the game.
document.getElementById('privacy').textContent = t('privacy');
document.getElementById('privacy').hidden = false;
countVisit();
createFilesManager({mode:new URLSearchParams(location.search).get('mode')});
// Registers the offline cache on a first run that starts here; new builds
// are applied by the launcher.
startAppCache();
addEventListener('pageshow',event => { if (event.persisted) location.reload(); });
