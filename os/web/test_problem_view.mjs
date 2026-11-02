import test from 'node:test';
import assert from 'node:assert/strict';
import {abortDetail, abortKind, createHangWatch, fittedText, HANG_MS, LOADING_HANG_MS, reportUrls, URL_LIMIT} from './problem-view.js';

test('the hang watchdog: 20 s without new frames, 60 s while loading, not before the first frame', () => {
  const watch = createHangWatch();
  watch.reset(0);
  assert.equal(watch.stalled(0, {frames:0, loading:false}), false);
  assert.equal(watch.stalled(HANG_MS + 1, {frames:0, loading:false}), false, 'no frame yet: the game is starting');
  assert.equal(watch.stalled(HANG_MS + 2, {frames:5, loading:false}), false);
  assert.equal(watch.stalled(2 * HANG_MS, {frames:5, loading:false}), false);
  assert.equal(watch.stalled(2 * HANG_MS + 2, {frames:5, loading:false}), true);
  assert.equal(watch.stalled(2 * HANG_MS + 3, {frames:6, loading:true}), false, 'frames again');
  assert.equal(watch.stalled(2 * HANG_MS + 3 + HANG_MS, {frames:6, loading:true}), false, 'loading: longer');
  assert.equal(watch.stalled(2 * HANG_MS + 3 + LOADING_HANG_MS, {frames:6, loading:true}), true);
  watch.waited(200000);
  assert.equal(watch.stalled(200000 + HANG_MS - 1, {frames:6, loading:false}), false, 'the player waits');
});

test('an abort: memory or an error, its reason without addresses or URLs', () => {
  assert.equal(abortKind('Aborted(OOM)'), 'memory');
  assert.equal(abortKind('RuntimeError: memory access out of bounds'), 'crash');
  assert.equal(abortDetail('RuntimeError: unreachable at 0x1a2b https://play.f2touch.com/fallout2-ce.wasm\n  at foo'), 'RuntimeError: unreachable at …');
  assert.equal(abortDetail(undefined), 'abort');
});

test('the channels get the report as it is shown; no form until its link is set', () => {
  const texts = {form:'', github:'https://github.com/imbaner/fallout2-ce-mobile/issues/new', telegram:'https://t.me/imbaner?direct&text='};
  const urls = reportUrls(texts, 'Игра не отвечает', 'line 1\nline 2\n');
  assert.equal(urls.form, '');
  assert.equal(urls.github, 'https://github.com/imbaner/fallout2-ce-mobile/issues/new?title=%D0%98%D0%B3%D1%80%D0%B0%20%D0%BD%D0%B5%20%D0%BE%D1%82%D0%B2%D0%B5%D1%87%D0%B0%D0%B5%D1%82&body=%60%60%60%0Aline%201%0Aline%202%0A%60%60%60%0A');
  assert.equal(urls.telegram, 'https://t.me/imbaner?direct&text=line%201%0Aline%202%0A');
  assert.equal(reportUrls({...texts, form:'https://docs.google.com/forms/d/e/x/viewform?entry.1='}, 't', 'a b').form, 'https://docs.google.com/forms/d/e/x/viewform?entry.1=a%20b');
});

test('a long report fits the links: the oldest events go first, then the last lines', () => {
  assert.equal(fittedText('code\nLast events:\n- one\n- two\n', 'code%0ALast%20events%3A%0A-%20two%0A'.length), 'code\nLast events:\n- two\n');
  assert.equal(fittedText('one\ntwo\n', 'one%0A'.length), 'one\n');
  const report = 'Код: ошибка\n' + 'Скрипт: длинная строка про ошибку\n'.repeat(20) + 'Last events:\n' + '- карта и что-то ещё\n'.repeat(30);
  const urls = reportUrls({form:'https://docs.google.com/forms/d/e/x/viewform?usp=pp_url&entry.1=', github:'https://github.com/a/b/issues/new', telegram:''}, 't', report);
  assert.ok(urls.form.length <= URL_LIMIT && urls.github.length <= URL_LIMIT);
  assert.ok(decodeURIComponent(urls.form).includes('Код: ошибка\n'));
});
