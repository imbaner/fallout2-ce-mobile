// The problem window the engine can't draw itself (src/error_report.h): it
// stopped (abort), it shows no new frames (a hang), or the page ended while
// the game was in front. Its texts are the engine's own, in the game's
// language, prepared before (Module.webErrorReport.texts()). Nothing is sent
// by itself: the player picks a channel - the Google Form (no account),
// GitHub or Telegram (an account) - or copies the text.

export const HANG_MS = 20000;
export const LOADING_HANG_MS = 60000;

// The engine's frames stopped changing for longer than the limit (20 s, 60 s
// while a map or a save loads), not before the first one.
export function createHangWatch() {
  let frames = -1, changed = 0;
  return {
    reset(now) { frames = -1; changed = now; },
    waited(now) { changed = now; },
    stalled(now, state) {
      if (state.frames !== frames) { frames = state.frames; changed = now; return false; }
      return state.frames > 0 && now - changed >= (state.loading ? LOADING_HANG_MS : HANG_MS);
    },
  };
}

// The engine's abort: the browser out of memory (what to do, no report) or
// an error (its first line, without code addresses or the page's URLs).
export function abortKind(reason) {
  return /\bOOM\b|out of memory|Cannot enlarge memory/i.test(String(reason)) ? 'memory' : 'crash';
}
export function abortDetail(reason) {
  return String(reason ?? '').split('\n')[0].replace(/https?:\/\/\S+/g, '').replace(/0x[0-9a-f]+/gi, '…')
    .replace(/[^\x20-\x7e…]/g, '').trim().slice(0, 80) || 'abort';
}

export function reportUrls(texts, title, report) {
  const encode = encodeURIComponent;
  return {
    form: texts.form ? texts.form + encode(report) : '',
    github: `${texts.github}?title=${encode(title)}&body=${encode('```\n' + report + '```\n')}`,
    telegram: texts.telegram + encode(report),
  };
}

// The window over the page in [host]: [title], [message], [report] (null for
// the environment's problems), [actions] - {label, run} buttons at the end.
export function showProblem(host, {texts, title, message, report, actions = []}) {
  host.replaceChildren();
  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };
  const button = (label, run) => {
    const node = element('button', '', label);
    node.type = 'button';
    node.addEventListener('click', run);
    return node;
  };
  const panel = element('div', 'problem-panel');
  panel.append(element('h2', '', title), element('p', 'problem-message', message));
  if (report) {
    panel.append(element('pre', 'problem-report', report));
    const urls = reportUrls(texts, title, report);
    const channels = element('div', 'problem-channels');
    const channel = (label, caption, run) => {
      const cell = element('div', 'problem-channel');
      cell.append(button(label, run), element('small', '', caption));
      channels.append(cell);
    };
    const open = url => window.open(url, '_blank', 'noopener');
    if (urls.form) channel(texts.formLabel, texts.noAccount, () => open(urls.form));
    channel('GitHub', texts.account, () => open(urls.github));
    channel('Telegram', texts.account, () => {
      if (navigator.share) navigator.share({text:report}).catch(error => { if (error.name !== 'AbortError') open(urls.telegram); });
      else open(urls.telegram);
    });
    panel.append(channels);
    const copy = button(texts.copy, () => {
      navigator.clipboard?.writeText(report).then(() => { copy.textContent = texts.copied; }).catch(() => {});
    });
    actions = [{node:copy}, ...actions];
  }
  const row = element('div', 'problem-actions');
  for (const action of actions) row.append(action.node ?? button(action.label, action.run));
  panel.append(row);
  host.append(panel);
  host.hidden = false;
}

export function hideProblem(host) {
  host.hidden = true;
  host.replaceChildren();
}
