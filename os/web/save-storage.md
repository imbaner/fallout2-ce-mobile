# Web user storage and save interchange

Format 3 replaced the whole-library IDBFS/MEMFS restore and the temporary
128 MiB exchange implementation of earlier test builds.

## Durable representation

The existing user database name stays `/fallout2-ce-mobile-userdata`; its schema
is 22, with reader/writer format guards `{format:3,minReader:3,minWriter:3}`.
The resource database is separate. All production writers retain the historical
resource lease and acquire the separate user writer lease.

* `nodes`: stable UUID inode, parent UUID, canonical name, type, size, time,
  immutable content ID and CRC. The unique parent/name index is the namespace.
  Directory rename changes an edge, not every descendant's path.
* `contents`, `refs`, `chunks`: immutable sealed files, flat block references,
  SHA-256 addressed blocks up to 256 KiB. Writes use copy-on-write staging;
  inheritance is flattened at sealing, without an accumulating parent chain.
* `jobs`, `staged`: disk-backed preparation, including streamed import metadata.
  Only sealed files can be published. One strict IndexedDB transaction removes
  changed old edges, installs the complete new namespace/records/settings, and
  advances the active revision. No payloads are copied in that transaction.
* `undo`: changed metadata for the preceding confirmed checkpoint; it pins
  referenced contents. `restorePrevious()` atomically reconstructs that view.
* `zipEntries`: temporary local ZIP record index, checked against the central
  directory one record at a time. It is discarded with its owning job.

Collection uses reference counts and the garbage index rather than scanning the
live tree. Startup discards abandoned jobs and reclaims their contents; ordinary
checkpoints do bounded cleanup. Active/previous contents and active jobs are
pinned. Collector and writer operations on a handle are serialized. Closing a
handle aborts in-flight publication transactions.

Each game of the list has its own part of this tree: the root for the first
game, `games/<id>/` for the others (`storage.md`, "Several games"). Save import
and export take the game's prefix.

## Engine adapter

`user-lazy-filesystem.js` keeps a 16 MiB block cache and a 2048-node metadata
cache. libc directory enumeration uses a paged iterator, without constructing
the entire directory tree. Changed nodes belong to the current native operation,
not the whole library. `userfs.js` reuses pinned SDK 4.0.4 syscall bodies and adds
Asyncify suspension at cache misses; no SDK file is modified. File fsync does
not publish an unfinished native catalog operation. The existing C++
SaveCatalog/SaveRecords/SaveStorage checkpoint remains the publication boundary.

The explicit JS configuration helpers are separately bounded to small settings
files; archive/engine payloads do not use their whole-file helper. Block/node
counters are budgets for our caches, not a measurement of total Safari RSS or
the game's other allocations. The engine remains wasm32.

## Earlier layouts and recovery

User databases in the IDBFS layout of earlier test builds (schema 21, formats
1–2) are not migrated: opening one fails with an unknown-layout error and leaves
its rows untouched. Old SDK clients cannot open schema 22 and cannot overwrite
it. Unknown/future formats fail closed.

Interrupted native catalog journals (`.ce-write-N`) must first be recovered by
the existing C++ catalog on game launch. Exchange refuses to bypass those journals.

## ZIP and UI contract

Export includes all slot folders containing SAVE.DAT in one ZIP (including
damaged headers, as on Android), `SAVEGAME/SLOT01..SLOT1000/` plus
`fallout2-saves.txt`, with native identities, creation/order/composition/quick
metadata. No mandatory slot selection or 128 MiB/32 MiB payload cap remains.
Import reviews found saves, adds to free manual slots while skipping existing
identities, or explicitly replaces all. Replacement maps newest quick saves
into the current quick range; a conversion to manual saves is stated before
confirmation. Insufficient matching slots rejects the plan without deleting data.
Composition comparisons reuse `src/save_composition.cc`. An installation stamp
prevents presenting a stale composition as current after replacing game files.

Payload decoding uses the pinned libarchive 3.8.1 already used by the project,
built separately for the worker. Its streamable ZIP reader does not build a
central-directory tree. A bounded validator checks central/local agreement,
CRC, counts, offsets, Unicode/CP866 names and ZIP64 against the disk catalog.
Store and Deflate are accepted; links/encryption/conflicting paths are rejected.
Save archive policy is independent of game archive limits: at most 2,000,000
records, 240 UTF-8 path bytes, depth 32, 16 MiB per interchange metadata file,
1000 physical slots, safe-integer sizes. Actual quota and storage are the payload
limit. This is not a guarantee that an arbitrary huge single file is a playable
wasm32 game save.

`ZipOutput` implements only ZIP Store/ZIP64 output per APPNOTE, with known CRCs
and no data descriptors. This matches Android's reader and avoids buffering a
file for Store+descriptor compatibility. Both payload output and the central
directory spool are OPFS files. Closing copies the spool in bounded blocks.
No all-entry ZIP writer Map or archive-sized Blob/Uint8Array is built. Disk
preflight includes both copies of central metadata; actual writes handle quota
errors. The system copy in Files/downloads needs additional space outside OPFS.

The final UI action downloads a disk-backed File. It reports preparation
and handoff, not observed completion in Files. Temporary files are not deleted
on pagehide; they are retained for seven days and old abandoned exports are
collected on a later export.

## Evidence and remaining acceptance

Automated checks cover 1000-slot export/import, quick/manual allocation, native
Android interchange, corruption/cancel/quota/abort, refused earlier layouts,
previous checkpoints, equal timestamps and real C++ catalog operations in WASM.
The native lazy-I/O fixture references 1000 slots with a **62.5 GiB logical**
library using shared immutable synthetic data, checks cache budgets, seek/COW
write and reopen. It does not store 62.5 GiB of distinct host data or test real
gameplay. A separate 129 MiB random-data save round trip prohibits whole-ZIP
`arrayBuffer()` calls. A real >4 GiB sparse ZIP64 and a 70,000-entry ZIP are
independently read by Python; libarchive also validates 70,000 entries against
the IndexedDB catalog.

WebKit desktop UI checked synthetic import, OPFS export, actual download,
duplicate review and backup-before-replace. That downloaded ZIP passed Android
GameSaves tests. It does not establish iPhone multi-GiB handoff or phone RSS.

**Pending:** a real Android↔web RPU save transfer on phones.

References: [ZIP APPNOTE](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT),
[WebKit OPFS](https://webkit.org/blog/12257/the-file-system-access-api-with-origin-private-file-system/),
[WebKit storage policy](https://webkit.org/blog/14403/updates-to-storage-policy/).


### Hardware gate update, 25 October 2026

The Safari iPhone probe created a 4,311,744,764-byte Store ZIP in 3723 ms. The user confirmed downloading it and reselecting that downloaded file from Files: matching size and valid first/end records. Full phone CRC was not measured. Web Share failed reading; WebKit ShareDataReader uses ReadAsArrayBuffer for each entire file before opening its sheet. Production uses direct download only, for every size. No arbitrary file-size threshold was introduced. On build 1d756577 the user then confirmed save migration, real-save loading, export and restore in the iPhone PWA.

Source: https://github.com/WebKit/WebKit/blob/main/Source/WebCore/page/ShareDataReader.cpp

### Live-session exchange, 26 October 2026 — 3c2e7137

Import/export from game settings now opens the same Android-derived manager in
an encapsulated overlay in the original document. `webExchangeSaves` suspends
the calling preferences stack with Asyncify, rather than requesting process
exit. The AudioContext stays suspended throughout the overlay, including its
user gestures and background/foreground events. Unapplied preference edits stay
in the existing screen, matching Android.

Before exchange, commit actual settings/user changes, require all user file
descriptors to be closed, then release only the user writer. The engine keeps
the resource lease; two root configurations (at most 1 MiB each) and the pinned
manifest authorize the worker's save-only resource view. DATs are not copied
or reopened. The worker still uses the existing streamed import/export code,
CRC validation, staged publication and explicit replacement confirmation.

Return first disposes staged work and releases worker leases. The mounted lazy
filesystem reacquires the user writer and drops its inode, negative-lookup,
directory and block caches before native execution continues. Native records
and the save catalog are refreshed. Failure to reacquire keeps the current
session paused with a retry; it never resumes against a closed database.
Abandoned worker jobs are handled by the existing store recovery on reopen.

Live export automatically requests the ordinary browser download and returns
after the native-style 1500 ms completion message. Browser confirmation remains
under Safari's control. All handed-off File URLs remain valid for the document
lifetime, with no persistent export notice. A detected download-request error keeps the
manager open with Retry and the native corner return arrow. Every retry catches
errors and reuses the prepared ZIP; success follows the normal automatic return.
This reports preparation/request, never an unobserved successful write in Files.
Import success and cancellation also return to the same session automatically;
errors preserve the paused session. Export-before-replace returns to the
explicit confirmation, never applies replacement by itself. First-run setup
and resource replacement retain their standalone manager.

The iPhone 17/iOS 27 Simulator Safari probe requested a disk-backed 67,108,988-byte
ZIP after 71,241 ms with userActivation=false. Safari presented its own Download
confirmation; after accepting, Downloads reported complete=true. This is an
isolated handoff probe, not full-game phone acceptance. Physical iPhone/iOS 26
live-session recheck remains pending. The earlier physical >4 GiB manual-download
acceptance still applies to that file-size test only.

### Export error UI update, 26 October 2026 — 48bbb522

Removed the permanent ZIP notice and retry link from the game document. Healthy
requests automatically return to the retained session. Only a detected request
error presents Retry plus the top-right return arrow in the exchange manager.
Repeated failures remain recoverable; a successful retry returns automatically
without rebuilding the ZIP. Handed-off URLs stay alive until pagehide. Silent
browser blocking and later system download errors have no ordinary page-level
callback and are not falsely reported as detected.
