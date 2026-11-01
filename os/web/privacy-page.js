import {visitsBlocked, visitsOptedOut, setVisitsOptedOut, countVisit, countEvent} from './visits.js';
import {language, webText as t} from './strings.js';

const $ = id => document.getElementById(id);
document.documentElement.lang = language;
document.title = 'F2 Touch — ' + t('privacy').toLowerCase();
$('title').textContent = t('privacy');
$('back').setAttribute('aria-label', t('back'));
$('privacy-local').textContent = t('privacy_local');
$('privacy-why').textContent = t('privacy_why');
$('privacy-visits').textContent = t('privacy_visits');

function show() {
  const blocked = visitsBlocked(), off = visitsOptedOut();
  $('privacy-state').textContent = t(blocked ? 'visits_browser_off' : off ? 'visits_off' : 'visits_on');
  $('primary').textContent = t(off ? 'visits_count' : 'visits_stop');
  $('primary').hidden = blocked;
}
// The opt-out press is the last thing counted; turning counting back on is
// counted after the change.
$('primary').addEventListener('click', () => {
  if (visitsOptedOut()) { setVisitsOptedOut(false); countEvent('visits-on'); }
  else { countEvent('visits-off'); setVisitsOptedOut(true); }
  show();
});
show();
countVisit();
