# Mobile web build

This is the port's normal wasm32/Asyncify target. It builds the current
working tree directly, including the mobile UI, with no source copy or
private patch. `web-dist` packages the engine, a newly generated `ce.dat`,
launcher, local fonts/licenses, and `build-info.json` with one version and
asset sizes/SHA-256 hashes. Original Fallout files are never packaged.

The player installs their own game from a ZIP in the file manager
(`files.html`); resources, saves and settings stay in the browser's local
storage ([storage.md](storage.md), [save-storage.md](save-storage.md)).
Once opened online, the app also starts without a network; new builds are
applied without interrupting a game (see Offline start and updates).
The development game adapter reads a local folder through the
local development server; changes and saves in this session exist only in WASM
memory and disappear on reload. It does not read or alter the folder's saves.

Current device work prioritizes iPhone/Safari and Home Screen use. Android web
compatibility is secondary: Redmi audio crackle and the Yandex Keyboard's
Enter/Done action remain known, deferred issues. Google Keyboard completion was
confirmed on a phone; desktop browser checks do not establish a fix for either
Android issue. These issues do not block implementing local installation, saves
and offline startup for iPhone.

## Build

On macOS/Linux: Python 3.10+, CMake, Ninja and Emscripten **4.0.4**. The
compiler version is pinned in `emscripten-version.txt` and checked by the
build command. A native compiler builds the existing `ce-dat-tool` first.

With the SDK installed:

```sh
python3 os/web/build.py --emsdk /absolute/path/to/emsdk
```

Or activate the SDK in your shell and run `python3 os/web/build.py`.
Output: `out/web/dist/`. This is the complete static app directory.
Release uses `-O2` for compilation and linking, including optimization of the
Asyncify-instrumented module; `-O1` is only suitable for faster iteration.
The command runs the native tool build, web configure/build and package
verification. A failed packaging operation leaves the previous bundle intact.
The build has its own Emscripten cache (`<build dir>/emscripten-cache`) and
unpacks Emscripten's ports (SDL2, SDL2_mixer, zlib) in
`/tmp/f2touch-emscripten-ports`: their `__FILE__` strings are that path, so the
app's files carry no local path and the same sources give the same files on
any machine (`sw.js` then keeps the unchanged ones on an update).

Install the SDK from its official source if needed:

```sh
git clone --depth 1 https://github.com/emscripten-core/emsdk.git out/web/emsdk
python3 out/web/emsdk/emsdk.py install 4.0.4
python3 out/web/emsdk/emsdk.py activate 4.0.4
```

Use Python 3.10+ for these commands too. macOS's `/usr/bin/python3` can be
older; select an already installed newer interpreter explicitly.

Options:

- `--dat-tool /path/to/ce-dat-tool`: reuse a native tool instead of building it.
- `--app-version VERSION`: set the same version in About and build-info;
  the default is `git describe --tags --always --dirty`, or `development`
  for a source archive without Git metadata.
- `--jobs 4`, `--build-dir PATH`, `--output PATH`.
- `--diagnostics`: enable the engine's touch/action diagnostics.

The supported build uses Emscripten's official CMake toolchain via `emcmake`.
`OPTION_WASM_64` and `OPTION_WASM_JSPI` are rejected: the current file-read
bridge requires wasm32 and Asyncify. Resource cache: 256 KiB blocks, 64 MiB
LRU, inherited as initial measurement values from the prototype.

## Run locally

Serve the app alone:

```sh
python3 os/web/serve.py
```

For a playable development session, provide your own existing game folder,
optionally with RPU already installed:

```sh
python3 os/web/serve.py --game /absolute/path/to/your/Fallout2
```

Open the printed URL. `?dev=1` explicitly enables the local development
adapter; `&mouse=1` lets a mouse act as a finger for desktop debugging.
Real mobile touch is the default input path. A phone-like viewport can be
selected in browser developer tools. By default the server binds only to 127.0.0.1.

The game folder is never copied into `dist` or modified by the server.
Reads use HTTP byte ranges on demand; the whole DAT is not loaded into RAM.
The folder's `ce.dat` and SAVEGAME are excluded; the app uses its own
freshly packed `ce.dat`. This development path needs the local server and
does not demonstrate offline play. Installed games use the same range-reader
contract with local browser storage.

For a phone on the same Wi-Fi, use `--lan`. On a Mac this chooses the Wi-Fi
interface's private IPv4 address. Each LAN session gets a new random URL
path; share that complete URL only with the tester. The server refuses
public and wildcard bind addresses and rejects unrelated Host headers.
LAN mode defaults to real touch; append `&mouse=1` only for a desktop check.
Start in landscape on a phone. The launcher blocks starting in portrait.

```sh
/usr/bin/python3 os/web/serve.py --lan --game /absolute/path/to/your/Fallout2 --port 18765
```

The server alone supports Python 3.9+ (including the system Python on this
Mac); the build still needs Python 3.10+. `--listen 192.168.1.10` explicitly
selects a private address instead of auto-detection. The Mac must stay awake.
Stop with Ctrl+C. `--verbose` logs each resource request; by default only
errors are logged. No router port forwarding, public tunnel or device
certificate is needed. This HTTP test checks the engine and touch input;
HTTPS, Home Screen behavior and offline startup are separate checks.
See [the iPhone test instructions](iphone-test.md).

`--port 8091` selects another port. `--base-path /preview/` exercises hosting
under a project subdirectory; all app asset paths are relative. Runtime
inspection on the explicit dev path: `window.falloutDev.stats()` and
`window.falloutDev.module.FS`. No original game path is written into the bundle.

The development build also exposes `falloutDev.audio()` (context state and
latency) and `falloutDev.geometry()` (CSS/backing-store and SDL dimensions).
Play unlocks one AudioContext synchronously and shares it with SDL. The web
audio buffer is 2048 frames to tolerate more main-thread jitter; sustained
mobile audio performance still needs device testing.
For a local phone audio test, add `&stats=1` to the dev URL. An optional
non-interactive overlay shows context state, sample rate, buffer size,
callback gap, callback work time and late callbacks over the last five seconds,
plus lifetime maxima. A late callback is an interval over 1.5 buffer periods. It observes the existing
SDL callback without changing PCM or routing; it is off on the normal URL.
The stats session also enables the existing per-second engine frame monitor
in its temporary MEMFS config. The overlay shows FPS and UI/present times;
`falloutDev.frameTiming()` exposes the frame measurements and `perf.log` stays
in the browser. Timing is available as `falloutDev.audioTiming()` when enabled. Callback gaps
are diagnostic observations, not hardware underrun counters.
These observations stay in the browser; the read-only server does not collect
phone audio telemetry. The launcher displays a short hash covering all packaged
assets, so frontend-only changes also distinguish phone test bundles.
The web mixer feeds SDL small PCM blocks rather than individual frames; the
native mixer is unchanged. This reduces conversion overhead but cannot prevent
all main-thread stalls in the current ScriptProcessor backend.

Character names and mobile save descriptions use a native HTML text field
over the game's field. Selection, paste, deletion and composition remain
browser-native; the engine still validates the game's charset and length.
The single-line field belongs to an HTML form: a native IME Done/Enter submit
accepts the final DOM value, dismisses the keyboard and finishes the game field.
Hardware Enter and beforeinput line-break actions use the same bridge. IME
candidate confirmation and merely dismissing the keyboard do not accept text.
If automatic keyboard focus is refused on iOS, tap the visible field itself.
The viewport requests keyboard overlay. On coarse-pointer devices, a fallback
keeps the pre-keyboard canvas dimensions until the keyboard closes; desktop
resizing and device rotation remain available.
Legacy game/mod dialogs are not covered by this MUI field integration yet.
After startup, canvas keeps the game's aspect ratio and fits the available
viewport uniformly. Browser chrome/orientation changes can add black bars;
they must not stretch squares or shift the touch targets. A fresh launch in
landscape/fullscreen chooses its logical resolution for that viewport.

## Visit counter

`visits.js` counts page openings of the launcher, file manager and privacy
page on `play.f2touch.com` only, with GoatCounter (`imbaner.goatcounter.com`,
statistics private): page path, outside referrer, link labels (`?ref=`,
`utm_*`) and screen width, no cookies or game data. Named events, once per
page load: `archive-chosen`, `files-installed`, `saves-imported`,
`import-error-<code>`, `start-app` / `start-browser`, `start-failed`,
`visits-off` (counted just before the opt-out takes effect) and `visits-on`.
Other hosts, automated browsers, Do Not Track, Global Privacy Control and the
opt-out on `privacy.html` send nothing.

## Checks and CI

```sh
python3 -m unittest discover -s os/web -p 'test_*.py'
node --test os/web/test_*.mjs
python3 os/web/package.py --verify out/web/dist
```

The HTTP checks use tiny synthetic resources and cover ranges, invalid
ranges, HEAD/MIME, subdirectory hosting and isolation of saves/symlinks/paths.
LAN checks cover bind/Host restrictions, session paths, unsupported writes
and keeping the browser adapter opt-in on private hosts.
An isolated C++ check verifies that slow browser frames still yield for
painting/input while the native frame limiter keeps its existing behavior.
A host SDL2 check compares the production web mixer with independent whole-file
SDL conversion: PCM/resampling, loops, short effects, seeking, pause, volume and
bounded block feeding. It needs a C++ compiler and `sdl2-config` (CI installs
`libsdl2-dev`; otherwise this check is reported as skipped).
A narrow WASM/Asyncify check compiles the production web exit helper with a
deliberately held runtime and verifies atexit/onExit after asynchronous main.
It needs the installed SDK, Node and Python 3.10+; CI runs it after the build
(`python os/web/test_lifecycle.py`). It does not simulate phone performance.
The package verifier checks required assets, hashes, sizes, the WASM header
and absence of extra files/original DATs.

The existing web workflow now builds and uploads the whole static app as
an artifact. It does not publish or deploy it. CI contains no original
game files or real user saves.

The launcher uses the Android import screen's colors and panel structure
with **Fallout Classic Dialog** as the default font and PT Mono as a glyph
fallback. Full import states and matching the native Android import font
are handled with the later import implementation.

## Local storage and HTTPS check

`python3 os/web/https-test.py` starts a verified static-only loopback server
and an installed Cloudflare Quick Tunnel. It exposes no `--game` endpoints.
Open the printed address on the phone; Home Screen installation is optional.
Original files never cross the tunnel: the player selects their own ZIP on
the device.

The file manager streams Store/Deflate ZIP/ZIP64 in a dedicated module worker
(`probe-worker.js`) using vendored zip.js 2.24.0 (BSD-3-Clause). Resources live
in their own IndexedDB; a completed manifest becomes active only after
CRC-checked extraction. Cancel, quota failure and interruption do not activate
partial data. Local range reads verify each block's SHA-256. One exclusive Web
Lock owns a game or storage operation. A full game ZIP or a prepared complete
mod set over the installed base is reviewed before replacement. The launcher
mounts completed local resources through the same range-reader contract as
the LAN adapter. Engine-owned ce.dat still comes from the matching app bundle;
the writable game view cannot modify the stored resource chunks. See
[storage.md](storage.md).

Saves and settings use a separate block store with a lazy writable view,
including paged directory metadata. A native checkpoint publishes the complete
slot, records and settings atomically before returning save success. Normal
engine fsync does not persist unfinished catalog state; SDK and FS.syncfs are
not patched. All slots are exchanged with Android and PCs as one ZIP/ZIP64 from
the game's Settings, over the paused session. User databases in the IDBFS
layout of earlier test builds are refused and left unchanged, not migrated.
See [save-storage.md](save-storage.md). No automatic telemetry is installed.

## Offline start and updates

`sw.js` is the app's service worker. `package.py` fills it with every other
file of the build and its SHA-256, so each build is a new worker version.
Installing it takes each file from an earlier build's cache when its size and
hash match, otherwise downloads it past the HTTP cache and checks them, and
stores the whole set in one cache per build (`fallout2-app:<scope>:<build>`).
An update downloads only the files that changed: the engine has no version or
commit compiled in (the page gives them, `src/app_version.h`), so its WASM and
JS stay the same while its code does.
Any failure discards that cache and the previous build stays in use. The worker
then answers requests for the build's own files from that cache, whatever the
query string, so the launcher, file manager, engine, WASM and workers always
come from one build and start without a network. Requests for anything else
(the development `__dev__` endpoints, byte ranges, other origins) go to the
network. Game resources and saves stay in IndexedDB; the worker never touches them.

The worker never claims an open page: the first visit runs from the network and
the next start is offline-capable. A new build installs in the background and
waits; meanwhile the launcher shows "Downloading an update…" and Play still
starts the current build. `app-cache.js` lets it take over only while the
launcher is idle (no game starting or running) and the app has a single window;
the launcher then shows "Updating the app…" and reloads. Otherwise the new build
waits until every window is closed, as browsers do by default. Activation removes
the older caches of the same scope only, so several copies of the app may share
an origin. When an idle app comes back to the foreground, the launcher applies a
build installed meanwhile (during a game) and looks for a newer one. The
launcher's footer shows the version and the build id.

After the game's main returns, the web wrapper explicitly finishes the runtime
with `emscripten_force_exit`, so pending asynchronous callbacks cannot defer
atexit/onExit. The frontend restores the existing launcher and stops text input,
audio and diagnostics without a page reload or a network request. A new Play
creates a fresh engine instance and restores committed user files. Normal exit
removes the old user-store listener; page shutdown aborts uncommitted writes.

Storage tests need `npm ci --prefix os/web --ignore-scripts` first (fake-indexeddb
is a pinned development dependency only) and Node 24+ with Web Locks. They exercise the actual importer
with ZIP64/Store/Deflate, corruption, cancellation, quota errors, incomplete
recovery and invalid paths. They do not establish iPhone performance or OS
storage persistence.

The ordinary launcher is now a compact Fallout2/Play screen. Resource inspection
runs before showing Play; missing resources route to the importer. Play retains
synchronous audio unlock/fullscreen requests before asynchronous engine setup.
File management remains in the game's Settings; resource errors provide a
recovery link.
PWA installation is not required: ordinary browser tabs use the same launch path.
Stage 7 reuses native touch/MUI behavior and adds browser interruption handling;
implementation and acceptance scope: [mobile-behavior.md](mobile-behavior.md).
The manifest now starts at index.html with unchanged id/scope/origin. Existing
installed icons may keep the earlier debug URL until the browser refreshes their
manifest; no site-data reset/reinstallation is required. The constrained flex
layout keeps Play visible in short landscape viewports. Layout validation uses
tiny synthetic resources in a separate localhost origin without running a game.
