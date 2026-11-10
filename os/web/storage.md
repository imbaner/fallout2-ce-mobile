# Permanent local resource storage

Resources, user data and the future application cache have separate owners.
The app's own files are cached by its service worker ([README](README.md#offline-start-and-updates)).
Saves and settings live in the separate user store described in
[save-storage.md](save-storage.md).

## Resource format

`resource-store.js` owns IndexedDB `fallout2-device-check-v1`, **database version 2**.
The database and its lock keep the historical names of the first test builds,
so installations on existing origins stay readable. Logical format 1 declares
minReader/minWriter 1. An unsupported database or logical format fails without
resetting the user's data.

| Store | Contents |
| --- | --- |
| state | Format declaration, active/previous generation IDs, collection marker |
| generations | Staging or completed manifests: paths/sizes, manifest SHA-256, measurements |
| refs | `[generation, canonical path, block index]` -> SHA-256 and exact block size |
| chunks | SHA-256 -> immutable bytes, at most 256 KiB |

Databases created by earlier test builds may also contain empty `meta`/`blocks`
stores; nothing reads them.

Paths are NFC, relative, slash-separated and case insensitive. Reject traversal,
drive/absolute/control paths, file/directory collisions and duplicate aliases.
Required nonempty master.dat/critter.dat remain part of every full manifest.

Writes require a staging generation. Activation checks the complete reference
layout and chunk existence, seals the file manifest and switches the active ID
in one transaction. Activation reads chunk keys, **not all payloads into RAM**.
Read/write transactions request strict durability; quota or abort errors reject.

Unchanged chunks are shared across generations, including large DATs. Collection
keeps active and one previous completed generation, removes abandoned staging,
then marks referenced hashes and deletes only unreferenced chunks. A persistent
dirty marker avoids scanning all references on every cold open; interruption
leaves collection retryable. It never touches the user filesystem.

## Several games

Each game is an installation of its own: `resource-store.js` opened with
`{game}` reads and writes that game's active/previous generation IDs. The first
game keeps the keys of the builds of one game (`active`, `previous`); others use
`active:<id>`, `previous:<id>`. Chunks are shared by all games, so a second copy
of the same files costs almost nothing. Collection keeps every game's active
and previous generations. The list itself (`games.js`) is `games` in `state`:
`{current, games:{<id>:{name, added, played}}}`. A database of one game has no
list and reads as game 1, so origins installed by older builds need no
migration. The logical format becomes 2 (minReader/minWriter 2) only when a
second game is added: older builds then refuse the database instead of
misreading it.

A game's part of the user's storage is the root for game 1 (as before) and
`games/<id>/` for the others: `savegame`, `settings`, `port`, `exports` under it.
The engine gets that root as `Module.webGameUserRoot`
(`src/platform/web/storage.cc`) and its name for the menu as
`Module.webGameName`. Deleting a game removes its generations and its part of
the user's storage (the first game's root folders are emptied, other games'
folders stay).

The launcher always shows the list (`games-view.js`, laid out as Android's
GamesActivity): Play is the user gesture that starts sound and full screen.
It has no footer: the unofficial notice, the version and the privacy link are
on the files page and in the game's Settings > About.
`files.html?game=<id>&add=1` installs a new game, `files.html?game=<id>`
replaces one's files; a new game is named after the archive's game folder (or
the archive), renameable in the list. The game writes its card at the menu
(`port/card.txt`, `port/card.png`, `src/game_card.cc`): the list's picture,
version and last save.

## Safe ZIP replacement

`zip-import.js` uses the pinned, vendored zip.js 2.24.0 inside a dedicated
worker. Inspecting a ZIP produces a review plan without replacing the active
generation. The plan pins the installation ID; it expires if another client
changes that installation. A worker-held Blob is disposed on rejection, cancel,
another archive selection or page shutdown. No resource lease is held while
the user reviews a plan; installation reacquires exclusive ownership.

The parser accepts Store/Deflate and ZIP64, strict UTF-8 or CP866 names.
It rejects encryption, links/special files, invalid paths, NFC/case aliases,
file/directory collisions and malformed local/central metadata. A unique game
at the shallowest root wins, matching Android: master.dat and critter.dat as
files or folders, the config ddraw.ini's ConfigFile names, and a mod in its
subfolder using Fallout 2's files offered as a choice (docs/android-touch-notes.md
"Game layouts"). Engine ce.dat, SAVEGAME, session map state and known
platform/temporary files are excluded.

Limits: 12 GiB archive, 10 GiB declared unpacked data, 50,000 entries,
16 MiB directory metadata/read allocation, 240 UTF-8 bytes/32 components per
selected game path. Actual output lengths and CRC are checked while extracting.
All selected entries must finish successfully before activation. Directory
inspection alone does not verify their CRC or prove valid Fallout DAT contents.

Extraction groups 256 KiB blocks into at most 1 MiB/64-block batches, including
blocks from small files. Each batch is hashed before one strict transaction;
only missing content hashes are inserted. These are application buffer limits,
not a measurement of peak browser memory or browser-internal disk overhead.
The conservative storage estimate reserves 110% of selected bytes + 16 MiB,
even when deduplication might need much less. It is not an exact quota guarantee;
real transaction quota errors also abort and preserve the old active generation.

Cancellation is allowed through extraction and final batch flush. An explicit
commit barrier then ends cancellation before atomic activation. Cleanup after
that point is retryable and cannot turn a committed installation into a reported
failure. Failed cleanup on reopen does not block reading a validated active
manifest. Abandoned staging remains inactive and is collected on a later open.
Neither replacement nor cancellation writes the separate user database.

The installed manifest keeps import measurements: transaction counts,
hashing/write time, new/reused bytes, small/large entry timings and bounded
progress samples. Small-file timing includes any shared batch flush it
triggers; it is not isolated disk profiling.

## Mod composition

`zip-import.js` accepts either a full installation or a prepared complete mod
set over an existing installation. It preserves master.dat/critter.dat,
patch000.dat and original music paths for mods-only replacement, plus the old
fallout2.cfg when none is provided. Everything else absent from the selected
mod set is removed in the new composition. `resource-store.inherit` copies
reference metadata under the same lease/strict transaction; DAT payloads are
not copied or decompressed. The source must still be the active generation.
Activation validates the combined inherited/new reference layout.

Preflight reports the configured language, DAT mod names, loose logic presence,
file changes, retained base count and embedded saves. Mod versions and game
compatibility are not inferred from ZIP filenames. Embedded saves are offered
as a separate explicit operation after resource activation, scoped to the
selected game root. Failing that operation does not overwrite existing saves.

The mounted view repairs absolute/traversing PC system/music paths in
fallout2.cfg. New system/language values take priority; other saved player
settings are merged. `port/settings-sources.json` records hashes of original
config templates at a checkpoint. A saved mod config is restored only if its
source template is unchanged; removed/changed config templates use new mod
defaults. Installations without this provenance retain their settings on an
ordinary launch; the first explicit replacement uses new mod defaults.
The resource bytes themselves are not rewritten by this mount operation.

Replacing the game from the game's Settings exits the engine in order and only
then opens the file manager; save exchange runs over the paused session
([save-storage.md](save-storage.md)). No manager writes alongside a live engine.

## Ownership

Every store handle holds an exclusive Web Lock. A second game, importer or
cleaner receives a close-other-window error. Release happens on explicit
close/pagehide, and the browser releases locks after process termination.
BFCache restoration reloads the local launcher before accessing a closed handle.
A schema upgrade blocked by an older open window asks the user to close it.
A new app cannot automatically downgrade unknown future formats. Original files
remain available in the user's ZIP.

## Reading and the writable view

GameFS reads ranges through Asyncify and checks each loaded block's length/hash.
Its LRU limit is 64 MiB. A large fd_read copies successive blocks into the WASM
destination before eviction; it does not preload the entire request into JS.
Missing/corrupt storage returns EIO (or a valid partial read), never fake zeros.

`/game` is a writable session view. Initial config copies are bounded (2 MiB
per file, 8 MiB total); other resources are lazy nodes. Truncating/replacing or
unlinking a node changes only this view, never the resource DB. Unsupported
in-place writes into an unmaterialized resource fail. Actual engine working
automap and config files are ordinary writable nodes. Installed loose prototype
files may be overwritten/deleted in the view and are restored from resources
on the next engine launch.

| Kind | Engine examples | Lifetime / destination |
| --- | --- | --- |
| Installed resources | DAT, scripts, art, loose PRO, original configs | Immutable resource DB; source for a fresh view |
| Save slots/catalog | SAVEGAME slots, previews, ce.sav, sfallgv.sav, slotdat.ini, staging/backup/metadata | User store/savegame; all aliases use one filesystem for rename |
| Settings | fallout2.cfg, ddraw.ini, data/config/game.cfg, game#patch.cfg | Writable view; captured in the same checkpoint; restored mirror takes priority over installed defaults |
| Working state | MAPS/*.SAV, MAPS/AUTOMAP.DB, working PROTO, generated mod order | Current engine session; saved copies inside a slot are user data |
| Temporary/debug | config .tmp, .bak, logs, debug worldmap output | Session only; never import as settings/save state |
| Exports | character .gcd/.bio, screenshots and character text reports | Session output; explicit export UX still pending |

SAVEGAME is linked to the user mount at the root and configured master/critter
patch directories. Existing saved settings are restored before resolving those
aliases. Root config files are copied, not symlinked: atomic temp-file/rename
updates must not replace a symlink and silently break persistence. C++ checkpoints
follow full slot/catalog/records/compatibility operations or explicit settings
Apply. User-root port records store the original text metadata format.
An error restores the durable user view and catalog; a later settings checkpoint
cannot persist a failed slot. No per-file autosave or unload save is used.

Normal development `?dev=1` sessions keep temporary files and never mount the
real user store or write the original Mac game folder.

## Verification

Run Node 24+ tests after npm ci. Python discovery also runs tiny real WASM
fixtures using the production GameFS, user store and mountGame; the SDK is
required. They use synthetic data, not a Mac gameplay/phone-performance
simulation. Resource retention, ZIP replacement and save loading on real
phones are checked by hand ([mobile-recheck.md](mobile-recheck.md)).
