import test from 'node:test';
import assert from 'node:assert/strict';
import {visitUrl, visitsOptedOut, setVisitsOptedOut} from './visits.js';
import {webText} from './strings.js';

const storage = (items = {}) => ({getItem:key => items[key] ?? null,
  setItem:(key, value) => { items[key] = String(value); }, removeItem:key => { delete items[key]; }});
const page = ({hostname = 'play.f2touch.com', pathname = '/files.html', search = '', referrer = 'https://www.reddit.com/',
  doNotTrack = null, globalPrivacyControl = undefined, webdriver = false, localStorage = storage()} = {}) => ({
  location:{hostname, pathname, search}, navigator:{doNotTrack, globalPrivacyControl, webdriver},
  document:{title:'F2 Touch', referrer}, screen:{width:915, height:412}, localStorage,
});

test('the public site counts the page path and outside referrer only', () => {
  const url = new URL(visitUrl(page()));
  assert.equal(url.origin + url.pathname, 'https://imbaner.goatcounter.com/count');
  assert.equal(url.searchParams.get('p'), '/files.html');
  assert.equal(url.searchParams.get('r'), 'https://www.reddit.com/');
  assert.equal(url.searchParams.get('s'), '915');
  assert.equal(url.searchParams.get('e'), null);
  assert.equal(url.searchParams.get('q'), null);
  assert.equal(new URL(visitUrl(page({referrer:'https://play.f2touch.com/'}))).searchParams.get('r'), '');
});

test('only link labels pass from the query string; events carry just their name', () => {
  const campaign = new URL(visitUrl(page({search:'?ref=telegram&mode=saves&utm_campaign=mobile8'})));
  assert.equal(campaign.searchParams.get('q'), '?ref=telegram&utm_campaign=mobile8');
  const event = new URL(visitUrl({...page({search:'?ref=telegram'}), event:'start-app'}));
  assert.equal(event.searchParams.get('p'), 'start-app');
  assert.equal(event.searchParams.get('e'), 'true');
  assert.equal(event.searchParams.get('r'), '');
  assert.equal(event.searchParams.get('q'), null);
  assert.equal(event.searchParams.get('t'), 'start-app');
});

test('local and test hosts, automation, browser privacy signals and the opt-out send nothing', () => {
  assert.equal(visitUrl(page({hostname:'localhost'})), null);
  assert.equal(visitUrl(page({hostname:'beta.f2touch.com'})), null);
  assert.equal(visitUrl(page({webdriver:true})), null);
  assert.equal(visitUrl(page({doNotTrack:'1'})), null);
  assert.equal(visitUrl(page({globalPrivacyControl:true})), null);
  const localStorage = storage();
  setVisitsOptedOut(true, {localStorage});
  assert.equal(visitsOptedOut({localStorage}), true);
  assert.equal(visitUrl(page({localStorage})), null);
  setVisitsOptedOut(false, {localStorage});
  assert.notEqual(visitUrl(page({localStorage})), null);
});

test('unavailable storage counts as not opted out and never throws', () => {
  const broken = {getItem() { throw new Error('denied'); }, setItem() { throw new Error('denied'); }};
  assert.equal(visitsOptedOut({localStorage:broken}), false);
  setVisitsOptedOut(true, {localStorage:broken});
  assert.match(webText('privacy_visits'), /GoatCounter/);
});
