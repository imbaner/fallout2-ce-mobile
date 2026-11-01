// One view for first-run setup and the live game's exchange overlay.
const markup = `
  <main id="launcher">
    <section id="panel" aria-labelledby="title">
      <h1 id="title">Game files</h1>
      <div id="content">
        <p id="message" role="status" aria-live="polite">Opening storage…</p>
        <p id="details"></p>
        <div id="saves-actions" hidden>
          <button id="saves-import" type="button">Import saves</button>
          <button id="saves-export" type="button">Export saves</button>
        </div>
        <p id="note" class="dim"></p>
      </div>
      <progress id="progress" hidden aria-label="Processing files"></progress>
      <p id="progress-text" hidden></p>
      <div id="actions"><button id="secondary" type="button" hidden></button><button id="primary" type="button" hidden></button><a id="download" class="button" hidden>Save ZIP</a></div>
      <p id="footer"><span id="notice"></span> <span id="build"></span> <a id="privacy" href="./privacy.html" hidden>Privacy</a></p>
    </section>
  </main>
  <button id="back" type="button" aria-label="Return to game" hidden><img src="./icons/import-back.svg" width="28" height="28" alt=""></button>
  <input id="archive" type="file" accept=".zip,application/zip" hidden>
`;
export function createFilesView(container,{shadow = false} = {}) {
  const root = shadow ? container.attachShadow({mode:'open'}) : container;
  root.innerHTML = markup;
  const ready = shadow ? Promise.all(['app.css','import-native.css','files.css'].map(name => new Promise((resolve,reject) => {
    const link = document.createElement('link'); link.rel = 'stylesheet'; link.href = new URL(name,import.meta.url).href;
    link.onload = resolve; link.onerror = () => reject(new Error('Cannot load ' + name)); root.prepend(link);
  }))) : Promise.resolve();
  return {root:shadow ? root : document,ready};
}
