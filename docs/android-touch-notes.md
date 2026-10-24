# Android touch adaptation — working notes

## Updates

Android's `UpdateManager` checks the latest stable GitHub release of
`imbaner/fallout2-ce-mobile` in a worker on the first main-menu entry of a
new process, at most once per hour (manual checks always work). The last
request time is stored in SharedPreferences; there is no background timer.
Resuming the activity or returning to the menu does not check again.
`[touch] update_check=0` disables automatic requests.
The installed package must be the public release package; debug packages
are disabled unless `[debug] update_repo=owner/test-repo` explicitly selects
a separate repository of APKs signed with that debug package's key. Never
publish test APKs in the public release repository. Release assets must
keep the `fallout2-ce-mobile-<version>-arm64.apk` naming convention.

The main menu centers its visible actions and adds a quiet update row only
when a release is available. The
update dialog's single action downloads and opens Android's installer only
while that dialog stays open and the game is foregrounded at the main
menu. Closing it leaves the download running but revokes installation
intent. Ready files and release metadata live in `cache/update/`; on a new
process they are rechecked, never installed automatically. Interrupted
downloads can be retried from the start. Cancel removes the partial file;
Delete in About removes a complete download; a successful update clears old files
at the next start. Saves and game files are not touched.

The initial APK URL must point to this repository and tag under
`github.com/.../releases/download/`. HTTPS redirects are followed at most
five times and restricted to `api.github.com`, `github.com`,
`release-assets.githubusercontent.com` and `objects.githubusercontent.com`.
Size, GitHub's required SHA-256 digest, APK package, exact version name,
strictly newer version code and all current signing certificates are checked
after downloading, on cache restoration and before installation. A public
release APK must not be debuggable. A changed digest invalidates a cached APK
even when the tag, URL and size did not change. Key rotation is deliberately
not accepted automatically; it needs an explicit migration.
APK size is limited to 128 MiB, metadata to 1 MiB and notes to 32768 UTF-16
code units. Metadata and APK transfers have overall read budgets of 30
seconds and 10 minutes, in addition to 10-second socket timeouts. A conservative
space check reserves room for both the APK and the installer's staged copy.
Notes are displayed as plain text without remote images or executable links;
their wrapped layout is cached rather than recomputed every frame.
`PackageInstaller` sessions use a package-scoped mutable PendingIntent and
a signature-permission-protected receiver on every supported Android version,
with `RECEIVER_NOT_EXPORTED` on Android 13+. The per-process action and session
ID reject unrelated results; a terminal result cancels its PendingIntent and
invalidates the ID to reject replays. Mutability is necessary for the system's
status extras, and a session needs both pending-user-action and terminal results.
Unknown-source permission is requested through the system settings only
after the player asks to update. Install cancellation retains the APK.

About contains status/actions, the immediately saved auto-check switch,
source and an offline license dialog. Its orange dot reflects update
availability, independently of unsaved settings, and uses the same selected
color as other section markers. The update action has its own orange dot.
A ready APK can be deleted below that action using the shared danger button
style, without a divider inside the update group. Update and license
screens use the common Back rail (at the screen edge on phones, beside the
window in windowed mode). During gameplay About allows
checking only, with a note to install from the main menu. Release notes
are UTF-8, unlike the game's localized texts. Desktop builds have no updater
except the offline `update` / `updateingame` dev autotest scenarios.

**2026-09-29, GPT-6 Astra:** исправлен вылет перед новым главным меню
после изменения HR-фона в работе Claude Opus 5.5. Причина — именованный
FRM передавался в загрузчик только для числовых FID; исправлены загрузка
через `FrmImage` и ключ кэша. Автотест меню/выбора персонажа и обе сборки
прошли, APK установлен.


Notes for the touch-first control scheme (stage 2 and later). Stage 1 added
zoomable world view (`src/world_view.cc`), see comments there for the render
pipeline.

The game menu and the save/load screen were written by Codex / GPT-6 Sol,
the save catalog by Codex / GPT-6 Astra, reviewed and reworked by Claude
Code / Claude Opus 5.5 on 2026-09-29. The 2026-09-28 movie-input fix was
made by Codex / GPT-6 Sol (`gpt-6-sol`).

The subsequent save-history implementation was completed by **Codex / GPT-6
Astra (`gpt-6-astra`)**, continuing the earlier Opus/Sol work (attribution
clarified by the user on 2026-09-29). It separates physical placement from
chronological list ranks and session quickload, uses configurable quick/manual
ranges, and adds permanent copies of quick saves and complete-save transaction
recovery. The existing phone reservation remains 11–20; all 364 original
save files were verified unchanged. Gameplay touch tests ran on macOS, and the
storage tests additionally ran on the physical Android phone.

## How the cursor works today (stage 1)

- Mouse cursor position is in screen coordinates. Everything that touches
  the map converts it with `worldViewScreenToWorld` (game_mouse.cc,
  `_check_move` in animation.cc, sfall `tile_under_cursor` /
  `tile_by_position`, `mapEdgeIsOverClippedArea`).
- On Android the game runs in relative mouse mode: cursor position is owned
  by the engine, fingers move it like a trackpad. In `GAME_MOUSE_MODE_MOVE`
  touchscreen mode is on and a single finger puts the cursor under the
  finger (`touch.cc`, only for one finger).
- `gGameMouseHexCursor` is one map object with two roles:
  - move mode: hex outline on the tile under cursor — part of the map, zooms
    with it (it must match the tile);
  - other modes (arrow, crosshair, skills) and action menu: arrow/icon/menu
    at the cursor point — drawn by world view as unzoomed overlay
    (`worldViewSetOverlayObject`), anchored at the cursor's world point.
- `gGameMouseBouncingCursor` (destination marker in move mode) is a regular
  map object and zooms with the map.
- Two finger pan/pinch over the map keeps the cursor on the same map point
  (mouse.cc) when the cursor is over the map, clamped to the map area.

## Touch controls (stage 2, `src/touch_controls.cc`)

Enabled with `[touch] controls=1` (default on Android). Gestures over the map
are handled before mouse emulation (`mouse.cc`); everything over the UI still
goes through mouse emulation in touchscreen (absolute) mode.

- No cursor on the map: a touch picks the object / tile at the finger
  (`playerObjectAt`, `playerPrimaryTargetAt`) and calls player commands
  with explicit targets (`player_commands.h`: move, primary action, attack,
  item / skill on a target, look). The mouse code calls the same commands, so
  game rules (AP, reach, scripts) are the same for both. E.g. unarmed attack
  on a distant enemy fails with "target out of range" like with the mouse.
  See docs/touch-architecture.md.
- Radial menu is a transparent window with action menu button art; items come
  from `gameMouseBuildActionMenuItems` (cancel is dropped, tap outside
  cancels).
- Combat: first tap selects, second tap on the same enemy/tile confirms;
  selected enemy stays selected. Hints (`map_hints.cc`, mobile UI over the
  map, fixed size, anchored to objects/tiles): hit chance above the selected
  enemy (red cross when it can't be hit), the selected tile outlined with the
  move cost next to it, a use mark above an object selected for use, the
  tile dude walks to. The game mouse mode is still set (crosshair / arrow /
  move) as the game's targeting state - combat outlines of critters in sight
  and cursor mode for scripts depend on it - but nothing is drawn for it:
  `gameMouseRefresh` does nothing on the map with touch controls and the hex
  cursor objects are never shown.
- Edge scrolling is disabled with touch controls.
- Mouse emulation is left only for the game's own (not yet ported) windows:
  touchscreen (absolute) mode is always on, cursor sprite is drawn only while
  a button is held. Emulated mouse clicks never reach the map (`game.cc`),
  otherwise a finger lifted after a button closed a window would walk there.
- UI: one finger drag = mouse drag (button held, `gTouchDragActive` keeps it
  held between events). Inventory decides by gesture, not cursor mode
  (`inventoryUsesContextMenu`): drag moves items, tap shows description, long
  press opens context menu; in "use item on" tap uses the item. Inventory
  lists scroll only with nearly vertical swipes (pan mode). Inventory cursor
  mode is kept at hand (arrow mode hover handlers reset dragged item image).
- UI scrolling: nearly vertical one finger swipe = mouse wheel (natural
  direction, one line per 20 px), other directions drag; two fingers do
  nothing over UI. Inventory context menu is shown as the radial menu
  (`touchControlsChooseActionMenuItem`, modal).
- Tap magnets (`touchControlsSnapTap`, radius in dp so they scale with
  physical size, not with zoom): a ground tap within 24 dp of the tile dude
  walks to (or the move selected in combat) counts as that tile - the second
  tap on the same tile runs (`_dude_move`) / confirms. With HUD item
  highlight on, outlined items have priority over the ground: a tap on or
  within 24 dp of an item's sprite box uses that item directly
  (`actionPickUp`, no pixel hit needed - thin spears), also for long press;
  only items that show their outline (`objectHasVisibleOutline`), so an item
  out of sight isn't picked. Without highlight items are picked by pixels as
  in the game, so dude can walk next to them.
  Taps on objects are never changed. Autotest: `--dev-autotest-scenario=magnet`.
- Combat confirmation sticks to the selection (2026-10-09,
  `touchControlsStickToSelection`): a critter's sprite covers tiles behind
  it, so the confirming tap often landed on it and picked the critter. Now
  a tap within the selected tile's magnet confirms the move even under a
  sprite (a critter's own tile still picks the critter, to switch to it);
  a tap on the selected enemy's pixels confirms the attack even under
  another sprite (no margin: tiles next to it stay easy to pick). The
  selected enemy is the one with the hit chance above it (in the tactical
  view also the only outline cycling, `objectSetTargetOutline`, set by map
  hints). Autotest `behindcritter`.
- Combat's tactical view (2026-10-09/10, `tactical_view.cc`, a port
  setting `[enhancements] tactical_view` for the button): a HUD button over
  End turn / End combat, kept on across combats and starts (`[touch]
  tactical_view`) until switched again; shown on the player's turns.
  - Critters and items are drawn see-through, 65% opaque
    (`objectSetSeeThrough`: 256x256 tables of palette colors mixed, built
    from the game's palette once; drawing only, objects' flags - saved -
    untouched); every critter the game sees keeps its outline (target
    highlight aside; their sight is updated whenever anyone in the combat
    moves, a critter whose outline changed is drawn again). Outlines don't
    cycle (`objectStillOutlineColor`), only the selected enemy's does; where
    the figure is behind something its outline is mixed in (45%).
  - The tiles lie on the ground (`ground_lines.cc`): the mobile UI draws a
    faint grid where the dude can walk (a breadth-first walk around what
    blocks, as `_make_path`), the area's border (closed loops, mitered
    corners, soft edges, a dark edge under it, the UI's green dimmed) and
    everyone's tile in their outline's color, and every point of a line
    shows by what the game drew over the ground there
    (`objectSeeThroughCover`, marked by each standing object and roof as
    it's drawn, `objectSeeThroughMark`; the egg's see-through part doesn't
    cover): 40% through see-through figures and behind scenery, none under
    the dude and the outlines.
  - The move tile and the hit chance above the enemy as in normal combat.
    Taps and long presses pick tiles: a critter by the tile it stands on
    (`tacticalViewCritterAt`), items and corpses not at all. Autotest
    `tactical`.
- Combat outlines with touch controls (2026-10-09,
  `combatOutlinesFollowTurn`): the game outlines critters only while the
  cursor is a crosshair; touch controls have no cursor (a move selection
  is the move mode), so target highlight "On" now outlines them the whole
  player's turn, "Targeting only" while an enemy is selected.
  In combat a ground tap picks an enemy standing on that tile or on its body
  column (`touchControlsFindEnemyNear`: +-12 world px around its tile center,
  from the sprite top to the feet); sprite boxes of wide critters cover
  neighbour tiles, which must stay walkable.
- Taps on UI: cursor jumps to the finger, the press comes two frames later
  (`gTouchTapPending`, mouse.cc). Window manager spends a pass on leaving
  the previously hovered button and one on entering the new one, a press in
  the same frame was lost (quick taps on neighbour attack mode chips kept
  the previous mode). Autotest pushes such quick taps
  (`DEV_AUTOTEST_ACTION_HUD_QUICK_TAP_MODE`).
- Small buttons catch touches within 14 px (`windowFindNearestButton`),
  Android back button = Escape.
- The tile dude walks to is marked by map hints until dude stops
  (`mapHintsSetDestination`); a tap near it counts as that tile (runs).
- No hidden multi-finger gestures with touch controls: two finger
  tap/long press (right button, cycled cursor modes) and 3/4 finger shortcuts
  are ignored; only two finger pan/pinch over the map remains. Right click on
  UI buttons is a long press instead (`windowButtonAtPointHasRightClick`),
  e.g. weapon button cycles attack modes incl. aimed shot.
- World map: one finger drag over the map view scrolls it pixel by pixel
  (`touchControlsSetDragHandler`), tap travels, party marker tap radius is
  12 px. World map zoom is not implemented: its renderer draws straight into
  the window with fixed view size, zoom needs rewriting it.
- CE's 2-column inventory/loot backgrounds (`invbox2.png`, `loot2.png` in
  ce.dat) exist only in English; with other languages keep
  `inventory_columns=1` or accept English labels.
- Gestures handled by the map are drained from the queue all at once; mouse
  emulation still takes one gesture per frame (click semantics).

## Touch HUD (block 1, `src/touch_hud.cc`, `src/hud_layout.cc`)

Enabled with `[touch] hud=1` (default on Android). `hud_scale` (percent) is
the UI size setting, `hud_density` overrides screen density (0 - Android
`densityDpi / 160`; the Mac rig uses 2.667 on Retina to match the phone:
320 dpi, 1600x720 shown as 1066x480).

- Layout is data: `hud_layout.cc` has one table per orientation (landscape
  now). Groups (System, Screens, Tools, Log, Weapon, Combat) are attached to
  corners/edges, element sizes are in dp, `avoidOthers` groups slide along
  their edge to not overlap others (end turn between top right and weapon).
  Safe area: SDL lays the surface out under the display cutout
  (`SHORT_EDGES`), insets come from `MainActivity.getSafeInsets`
  (`DisplayCutout`) via JNI and are re-read on every HUD show (rotation to
  the other landscape side is handled then).
- Drawn by the mobile UI (`HudScreen`, non-modal, bottom of the mui stack):
  layout is placed in output pixels every frame, text is TTF. Taps become
  input events (`enqueueInputEvent`), so actions run in the game loop.
  Hidden while a game screen is open: any `GameMode` except combat/player
  turn, or a shown modal window (`windowIsModalShown`, e.g. elevator).
- Map/UI hit test uses `windowGetVisibleAtPoint`: hidden windows and
  transparent pixels pass touches to the map.
- The original bar is created but never shown; buttons send the same keys
  as bar buttons (I, C, P, Tab, S, B, Esc, F6, 1, space, enter, -20 for the
  item button), so game rules are unchanged. HUD only:
  - quick save / quick load are the game's F6 / F7 (load asks for
    confirmation first). sfall AutoQuickSave works as documented in
    files/fallout2.cfg: `[ui] auto_quick_save` pages starting at
    `auto_quick_save_page` are cycled without picking a slot (CE used to
    cycle the first N slots from slot 1 and had no page setting);
  - highlight: toggle, outlines ground items (`OUTLINE_TYPE_ITEM`, containers
    per `[Highlighting] Containers` of mods\sfall-mods.ini), new items are
    outlined once a second, off clears item outlines. Not the sfall script:
    it made the camera stutter and only worked while held;
  - log.
- Weapon group (status, modes, swap hands, weapon) sits on a translucent
  terminal panel (`HudGroupSpec::padding`, framed groups). Weapon button:
  item inventory art, action name (top right), AP cost (bottom left),
  crosshair when aimed (bottom right), ammo bar at the right edge; data from
  `interfaceGetItemButtonInfo`, the same description the bar's item button is
  drawn from. Attack modes strip: `interfaceGetAvailableItemActions` (same
  rules as the right click cycle), chips show mode names, aimed modes are a
  crosshair right after their base mode, reload is an icon; strip width is
  fixed, overflow shows arrow buttons.
- Party orders (Party Orders mod, `mods/party_orders.ini`): button on top of
  the left column, only with the mod installed and a living party member on
  the map. It opens the reusable action list (`muiShowActionList`,
  `mui_action_list.cc`: placed next to the button on the side with room, not
  over what it's for - skills will use it too); an order presses the mod's
  hotkey from its ini with `sfall_kb_press_hotkey` (reaches
  `HOOK_KEYPRESS`, the keys count as held for `key_pressed` until released,
  so "48+29" combinations work).
- Skills (`skilldexOpen(target)`, mobile path `skilldexChooseMobile`): the
  game's skilldex list (names from `skillGetName`, value in %) in the
  action list, blocking (`muiChooseAction`). From the HUD it opens next to
  the Skills button; from the radial "use skill" next to the critter
  without covering it (`muiPopupAnchor`/`muiPlacePopup`). No back button:
  tap outside closes. While a skill waits for its target the Skills button
  is lit and a hint (target icon + skill name) is shown at the top; tapping
  Skills again cancels targeting. Rows get lower to fit all 8 skills.
- The HUD closes only its own party orders list when the button goes away
  (it used to close any action list, so without a party member the
  blocking skills list vanished and the game waited for it until Esc).
- Mobile inventory and loot screens don't make the game's inventory window
  (`_setup_inventory`, `inventoryHasWindow()`): its drawing functions do
  nothing without it, the game logic (item moves, scripts, AP) is the same.
  Before, it was drawn every frame under the mobile screen (slower) and
  showed through while the game presented frames on exit. The barter
  screen doesn't make the game's trade window either.
- Talk and barter: the game's dialog windows (background, reply, options,
  talk and barter subwindows) are created hidden under the mobile UI
  (`gameDialogWindowFlags`): they only hold what the game draws (the talking
  head / map around the speaker the talk screen shows) and the button logic
  the screens reach by key codes. Party member control and customization
  have no mobile screens yet: the background is shown while they are open.
- Autotest frames log the game's windows visible besides the map ("visible
  game windows"); under the mobile screens there are none.
- Mobile screens covering the whole screen opaquely (inventory, loot,
  barter, talk: `MuiScreen::coversScreen`) stop the map and the game's
  screen buffer from being uploaded and drawn under them (`renderPresent`).
  Before, inventory and loot re-drew the zoomed map with the sharp filter
  every frame under the screen (~2.5 ms of 16 on a Mac, more on a phone GPU).
- Map rendering (`world_texture.cc`, from phone perf.log, 2026-09-26). The
  world buffer (8-bit, built for the smallest zoom, ~2x the screen each way)
  was one big streaming texture. On the phone every update of it cost 4-6 ms
  whatever its size (the previous frame still draws it: the driver copies or
  waits for the whole ~9.5 MB texture), a map scroll step (the game moves
  the buffer and draws the uncovered strips) re-uploaded everything, and
  palette cycling (entries 229-254, the alarm light alone 30 times a second)
  re-converted everything: ~30-47 ms of a 50-65 ms frame.
  Now the GPU copy is made of 256x256 tiles with ring addressing: a tile is
  updated at most once a frame (small copies for the driver), a scroll only
  moves the ring origin (`worldViewScrolled`, called by
  `mapScrollImmediate` before the strips are drawn), each tile remembers
  which cycling entries it uses and where, and only entries which really
  changed are re-uploaded. The visible part is composed from the tiles at
  an integer scale into one texture (no seams, reused while nothing
  changed) and scaled to the screen as before (nearest / linear / sharp
  bilinear). Frames are pixel-equal to the previous rendering (default,
  magnet, hud scenarios incl. pans, scroll steps, zoom, pinch, walking).
- Drawing only what's in view (2026-09-29, `world_view.cc`
  `worldViewClipRender`). The tile engine drew the whole world buffer
  (made for zoom 0.5: ~2260x1060, 2.4 Mpx) on every full redraw, though at
  zoom 1 a fifth of it is on screen; the phone's perf.log showed frames of
  40-50 ms game logic plus 40 ms uploading ~2.2 Mpx while the camera moved.
  Now `tileRefreshGame` draws only the part of a rect inside the view plus a
  scroll step (`worldViewNeededRect`); the world view keeps the drawn rect
  (`gDrawnRect`: moved with the contents on a scroll, grown by bands along
  its sides) and draws the bands that come into view after panning, zooming
  out or a scroll (`worldViewEnsureRendered`: end of `worldViewPanByWorld`,
  end of `mapScrollImmediate`). Reading the buffer beyond the view (save
  thumbnails) draws that part first (`worldViewRequire`). A cleared buffer
  forgets what was drawn.
  Two causes of whole redraws on camera moves are gone too:
  - Pinch zooming near the map's edges: every zoom step moved the view back
    inside the EDG limits (`mapEdgeHandleViewSizeChanged` -> `tileSetCenter`
    with a full redraw). While fingers pinch the view may go past the edges;
    it's moved back once when they're lifted (`worldViewBeginGesture` /
    `worldViewEndGesture` from the two-finger gesture in mouse.cc).
  - Roofs hiding / showing when dude walks in or out: the whole screen was
    redrawn (`_obj_move`), now only the changed roof tiles
    (`tile_fill_roof` reports their screen rect).
  Autotest `perfmap` (`TITLE=1 runtest.sh <out> --dev-load-game=10
  --dev-autotest-scenario=perfmap`, on a device through the launch intent,
  below) logs per part: frames, ms per frame, map pixels drawn by the CPU,
  uploads to the GPU. Mac, per frame, before -> after: pinch 766 -> 118 kpx
  uploaded; stepping under a roof and out 110 -> 4 kpx; drawn pixels at
  zoom 1 about a fifth. Frames compared with drawing everything: the same
  except animations' timing. What's left for 60 fps: floor and objects on
  the GPU, faster CPU loops, GPU fades.
- Stuck fingers (2026-09-29, rare on the phone: touch dead except long
  presses, or taps and walks forced to the left edge's middle - where
  Android's back gesture starts). Fingers go either to the mobile UI or to
  the map's touch (touch.cc); a finger down on the map whose end came while
  a mobile screen was open (the back gesture's Esc opens the game menu, a
  long press opens a screen) was eaten by the UI: touch.cc kept it pressed
  forever, every later touch was its second finger, and the mouse
  emulation kept "clicking" (`gTouchDragActive`) where it was. Now such an
  end makes the map forget the finger without a gesture and resets touch
  controls and the emulated mouse (`inputResetTouchGestures`); on every new
  finger both sides also forget fingers SDL doesn't have down any more
  (`touch_forget_missing`, `muiForgetMissingFingers`). Autotest
  `stuckfinger` (failed before: the tap afterwards didn't walk).
  A second case (in a dialog: taps dead, long presses working - the UI's
  finger stayed "down", taps fire on release): `inputEventQueueReset`
  (the start of the player's combat turn) drains every SDL event, finger
  ends too. `inputForgetLiftedFingers` now runs after every event loop and
  after that drain: fingers of ours SDL has had down neither now nor at the
  previous check are let go without a gesture (the previous check covers
  SDL dropping a finger right after queueing its end); a new finger with a
  number we still hold (Android reuses numbers) drops the stale one.
  Debug builds ignore the launch intent's `args` when opened from recent
  apps (the old autotest intent would run again).
- Touch audit (2026-09-29), besides stuck fingers:
  - Tap vs pan: touch.cc took 4 screen pixels of movement as a pan (made for
    640x480; under a millimeter on a phone, a finger rolling while tapping
    lost the tap). Now 8 dp (Android's touch slop), at least 4 pixels.
  - Map gestures are read only by `_mouse_info`, which returns while the
    cursor is hidden or the mouse disabled: gestures piled up and ran all at
    once later (taps long after they were made). Now they're dropped then
    and an emulated button let go.
  - Mobile UI without screens kept pointer events: a screen shown later
    took them as taps. They're dropped.
  - `tappedOutside`: a finger down before the screen opened (a long press
    opened it) doesn't close it when lifted outside.
  - Focus lost (app switched, notification shade): fingers, their gestures
    and held keys are let go (their ends may not come).
  - Stale touches: pointer events of the mobile UI are applied one down/up
    per frame (a quick tap between two frames is seen pressed, then
    released); after a stall taps made during it played out one by one
    later, often twice (people tap again when nothing happens). Events keep
    the finger event's time; a touch pressed and released over 400 ms ago
    that no screen has seen yet is dropped whole (`kStaleTouchMs`,
    `dropStaleTouches`); one already pressed on screen gets its end.
    Autotest `staletouch` (a HUD tap read after a 600 ms stall doesn't open
    the menu, one right after does).
  Not yet: the map's gestures (touch.cc -> `_mouse_info` -> touch
  controls) get the same expiry with step 4 of `touch-architecture.md`;
  legacy windows (preferences, main menu, elevator, world map...) still use
  mouse emulation until they're mobile screens.
- Touch recorder (2026-10-01, `touch_log.{h,cc}`, `[debug] touch_log`,
  always on in the touch-only build): after a report of touches going to
  the left edge by themselves (logcat's MIUIInput showed 20 ms taps coming
  from the touchscreen driver at the camera edge, re-sent to the game by
  MIUI's back-gesture strip - but no coordinates, so a finger or palm
  couldn't be told from a sensor glitch). The latest finger events (screen
  pixels, pressure, the event's time against when the game read it, who
  took it: ui / map / drained with the input queue), the map's gestures
  (done, dropped as stale, ignored), stale drops of the UI, fingers let go
  because their ends never came, back, focus, stalls and game modes are
  kept in a 2048 record ring in memory. touch.log (next to the game data,
  2 MB, then touch.log.old) gets the last 30 s only when: 4 taps shorter
  than 40 ms within 3 s, touches read over `kTouchStaleMs` late, finger ends
  lost, 3+ fingers, the game leaving the foreground or closing (so swiping
  home or closing the game right after a glitch saves it; `touchLogExit`
  from `gameExit` and SDL_QUIT). Pull with
  `adb pull /sdcard/Android/data/io.github.imbaner.fallout2ce.debug/files/touch.log`
  and compare with `adb logcat -d -v time | grep MIUIInput` (same wall
  clock). Off in autotests except `touchlog`. Until 2026-10-01 evening it
  never ran on the phone: the touch-only default was set before the config
  was read, but `gameConfigInit` first puts every setting's default into the
  config (`settingsWriteToConfig(true)`) and a saved fallout2.cfg had
  `touch_log=0`, so the read turned it off. Now set after the read (as
  `mobile_ui`), with the journal below.
- Game journal (2026-10-01, `action_log.{h,cc}`, `[debug] action_log`,
  always on in the touch-only build): actions.log next to the game data,
  one run per file (the previous in actions.log.old, 8 MB cap), lines
  written once a second and when the game goes to the background or
  closes. Wall clock, then: commands (HUD, screens' tabs), map taps and the
  radial menu with their targets, actions of the inventory, loot and barter
  screens, dialogs, screens/modes (`perfMonitorGameModeNames`), maps,
  saves, loads (with the dude's and the party's equipment and looks), and
  changes of what the dude and the party wear and hold, polled every frame
  (the party every 250 ms) so changes from any path, scripts too, show;
  fingers down/up (0..1 of the screen, who took them) and the camera
  (center tile, zoom, every 300 ms when it changed), so a session can be
  replayed from a save. Not while a game or a map loads.
  Pull with `adb pull /sdcard/Android/data/io.github.imbaner.fallout2ce.debug/files/actions.log`.
- The dude's look and his armor apart (2026-10-01, a player's saves, cause
  not found yet): one save with the leather armor's look and no armor worn
  (it had gone to Sulik), the next with a jacket worn and the jumpsuit's
  look. Happens rarely, the player thinks around fast quick saves and
  loads with small actions between (camera, armor, walking, attacks). Every
  ordinary way works (autotests `armorlook`, `armorwalk`, `armorsaveload`,
  "look ... ok" in the items' trace). Nothing papers over it (a look fixed
  from the equipment after loads was tried and taken out: it would hide the
  culprit); vanilla's own `_proto_dude_update_gender` on load still sets
  the no-armor look without a redraw. The journal now names the culprit
  next time: every change of the dude's look art is logged with its callers
  (`objectSetFrmId` -> `actionLogDudeFid`, `_Unwind_Backtrace` offsets in
  the library, "so+0x..."; symbolize with
  `llvm-symbolizer --obj=out/dev/so/libfallout2-ce-<BuildId>.so 0x...`, on
  the Mac `atos -o <exe> -l 0x100000000 0x1<offset>`), "MISMATCH" when the
  look isn't what he wears, "STALE" when the inventory's equipment globals
  (`gInventoryArmor`... that `critterGetArmor` answers with) are set
  outside its screens, and the state at every save ("saving to slot") and
  load ("loaded, as saved" before the gender update). The `GIVE_ITEM`
  autotest action left the item on the map as well (freed twice on a
  load), fixed as `add_obj_to_inven` does.
- Map load crash (2026-10-01, phone: Klamath's rat caves, back down the
  ladder with Sulik): the game reads a map's file reporting progress every
  32 KB (`fileRead` -> `gameMouseRefreshImmediately` -> `renderPresent`);
  our touch HUD built in that frame read protos the map load had dropped
  (`interfaceGetAvailableItemActions` -> `critterCanAim` -> `protoGetProto`
  -> `fileReadString`). Those nested reads added to the outer read's byte
  count, its next chunk came out below zero, and `xfileRead` read the rest
  of the map file past the squares' array (`_square_load`), overwriting
  memory after it - the party's list among it (the HUD then crashed on it:
  `std::length_error` on the phone, a null party member on the Mac). Fixed
  in db.cc: reads made while the progress handler runs don't report
  progress (`gFileReadProgressReporting`), a new handler counts from zero.
  Reproduced with the player's save (944 nested reads per run); autotest
  `mapreturn` (--dev-map=klatrap.map) walks that path. Found by tracing the
  party count through the load (ASan hangs on this macOS beta). Defense in
  depth: a read never asks for a chunk below one byte
  (`fileReadProgressNextChunk`), and the touch HUD isn't built while a map
  loads (`mapIsLoading`: the world is half gone; the screen is black then
  anyway). Audit of the other `renderPresent` callers: they are screens'
  own loops and the game's animations with the world whole - the file
  reading's progress was the only frame drawn from inside a low level
  operation.
- Save slot folders are made 0755 as the game's own (`save_storage.cc`
  made them 0700: adb couldn't read the player's saves).
- Automated tests on the phone: debug builds take the game's command line
  from the launch intent (`MainActivity.getArguments`):
  `adb shell am start -n io.github.imbaner.fallout2ce.debug/com.alexbatalov.fallout2ce.MainActivity
  --es args "--dev-autotest=<dir in files> --dev-load-game=<slot> --dev-autotest-scenario=perfmap"`.
- Palette fades are timed (`colorPaletteFadeBetween`, 700 ms /
  anim_speed): the original picked a number of steps from a reference fade
  measured at startup (a cheap screen), each step presenting a frame; on the
  map a step re-uploads the whole world and screen (every entry changes), so
  fades took 4-16 s on the phone (perf.log). Slow frames now skip steps.
- Steady 15 fps seconds with no uploads in perf.log are movies (they play at
  their own rate).
- SDL render batching is on (`SDL_HINT_RENDER_BATCHING`): naming the
  render driver ("opengl" on desktop) turned it off, every draw went to the
  driver alone. Android picks GLES2 without the name, so it batched already.
- Icons (`mui_icons.cc`) are drawn once per size and thickness into white
  textures (premultiplied alpha) and tinted when drawn: they were most of
  the HUD's time. HUD build on a Mac: 1 ms -> 0.19 ms (batching + icons).
  Menus (inventory, loot, dialog, barter, Pip-Boy, options...) pause the
  world as in the original (`isoDisable`), only drawing is skipped under
  covering mobile screens.
- Party member control tab (talk and barter screens, party members only,
  after talk / barter / review): the game's combat control and
  customization panels in one (`gameDialogGetPartyControlView`,
  `gameDialogQueuePartyControlAction` -> the talk loop,
  `GAME_DIALOG_PARTY_CONTROL_EVENT`). Stats under the head (hit points,
  best skill, carrying, melee damage, AP; level / AC / addict with sfall's
  PartyMemberExtraInfo), weapon and armor with "use best" (the panel's own
  code, `partyMemberUseBestWeapon/Armor`), five dispositions (0 custom ..
  4 berserk as the game's buttons: CE's `Disposition` names are shifted by
  one), six settings with their values (game\custom.msg). A disposition
  switches to its AI packet (the companion's preset from data\AI.txt);
  changing a setting switches to the custom disposition first, as the
  game's customization panel is only reached through it. Options a
  companion can't use (data\party.txt, RPU) are dimmed. Labels
  `game\ce.msg` 172-189. Autotest: `--dev-autotest-scenario=party`
  (test save slot 10); `pcparty` shows the game's own panels with
  `[touch] mobile_ui=0`.
- HUD "Highlight" button (`touch_hud.cc`) follows all options of sfall's
  highlighting mod (`[Highlighting]` in mods\sfall-mods.ini,
  gl_highlighting.ssl - that script works only while its key is held, the
  phone has none): items, Containers, Corpses (not "no steal"), Critters
  (combat colors, not in combat), CheckLOS, colors, MotionScanner (1 - a
  motion sensor carried, 2 - a charge per switching on; messages
  `game\ce.msg` 170-171). Own keys in the same section: IncludeNoHighlight=1
  (also objects with the game's "no highlight" flag - 118 of 124 container
  protos have it: all furniture, graves, bones, vents, wall safes; the mod
  lets only containers through), ExcludePids= (protos never outlined).
  Line of sight uses sight blocking (walls, opaque objects), not the mod's
  line of fire, where critters standing between hid things. Refreshed every
  250 ms; outlines of others (combat, interaction) are left alone. Phone:
  Containers=1, Corpses=1, IncludeNoHighlight=1, ExcludePids empty (the user
  wants everything: the button is an opt-in simplification).
- Interaction outline: an object dude is sent to (tap, skill / item on it,
  any radial action except look / rotate) or selected for use in combat is
  outlined green with the game's outline (`OUTLINE_TYPE_FRIENDLY`, set
  directly: "no highlight" objects like shelves and many containers are
  outlined too) until dude is done or another command is given; its own
  outline comes back then, the refresh covers the outline border
  (`mapHintsSetInteraction`). Move cost is drawn inside the selected tile.
- Frame timing log: `[debug] perf_log=1` in fallout2.cfg appends to
  `perf.log` next to the data once a second: game modes, fps, longest frame,
  ms per frame for the game's loop, screen upload + map, mobile UI, present,
  limiter sleep (`perf_monitor.cc`).
- The map can be moved and zoomed while others act in combat: the combat
  wait loops (`_combat_turn_run`, Goris animation) take system events
  (`_GNW95_process_message`), the game keeps scrolling allowed there and its
  own input disabled (HUD buttons follow `interfaceBarDisable`). Autotest:
  default scenario step `34b_pan_enemy_turn` (dude's screen position moves
  by the drag).
- Interface bar counters (AC, HP, AP) don't roll under the touch HUD
  (`interfaceRenderCounter`): the bar is hidden, the rolling only stalled
  the game (~0.5 s after changing armor, with frames presented).
- Use item on (radial "use item" -> `inventoryOpenUseItemOn`, mobile path
  `muiChooseItemToUse`, `mui_use_item.cc`): a compact vertical panel next
  to the target: its name, filters in one row, three columns of 50 dp
  cells; equipped items are left out. Tap uses the item on the target
  (`inventoryUseItemOnObject`, the game's use-on action, 2 AP in combat),
  long press shows the name, tap outside cancels. The HUD stays visible
  (`kUseOn` isn't a covering screen mode for it). The panel grows by rows
  up to the screen height, then scrolls. An overloaded dude walks instead
  of running (game rule), so the use takes longer.
  Autotest: hud scenario steps h13j-h13r (first aid, science, stimpak on
  Sulik, inventory logged before/after).
- Floating text over critters (float_msg) is placed within the visible part
  of the world view (`textObjectFindPlacement`; the world buffer is bigger
  than the screen, texts of critters at the view edge went off screen).
  Autotest `FLOAT_TEXT`. Size still follows the zoom (stage 2).
- Test save for the phone: `--dev-autotest-scenario=testsave
  --dev-map=arvillag.map` (Arroyo village, varied inventory, armed Sulik and
  Vic with their scripts) writes slot 10; `loadsave` loads it back and tries
  party orders. The phone's `data/SAVEGAME` was recreated from adb (the one
  made by the game isn't writable over adb), old copy in
  `data/SAVEGAME_backup`.
- Icons (`mui_icons.cc`, `muiDrawIcon`): HUD buttons, AP/HP, modes, dialog
  tabs, item filters and the radial action menu use drawn line icons in the
  theme colors, no game art or abbreviations. Long press on a HUD button
  shows its name instead of acting.
- Status: AP always like on the bar (empty out of combat, red on others'
  turn, number on dude's turn) and HP (colors as on the bar), with bolt and
  heart icons.
- Indicators (SNEAK, LEVEL, POISONED, ..., sfall custom tags) are a HUD
  group at top center instead of the indicator bar window.
- Log: button only; new messages pop up for 4 s next to it (max 4 lines),
  the button opens a translucent log panel (`MuiContext::scroll`, drag with
  inertia).
- Texts come from `game\ce.msg` (files/ce.dat, English and Russian): button
  names 100-113, quick load question 116, attack mode names 124-145 (the
  Russian ones as on RPU's item button art); English fallbacks in code.
- Autotest: `--dev-autotest-scenario=hud`.
- Next in block 1: hide HUD / adapt full screens (inventory overlaps HUD
  corners), dialogs, barter, Pip-Boy, character, options, save/load, UI size
  in options.

## Mobile UI (`src/mui*.cc`, block 1)

Touch-first screens replacing the original ones (decision 2026-09-25: rebuild
every screen on the new scalable UI, visual style/textures later via theme).
`[touch] mobile_ui=1` (default on Android).

- Drawn by the GPU at native output resolution in `renderPresent` over world,
  original 8-bit UI and movies (`SDL_RenderSetLogicalSize(0, 0)` while
  drawing). Sizes in dp: output px per dp = `hudGetScreenDensity()` *
  `hud_scale`; safe area from HUD metrics (cutout insets).
- Text: PT Mono (OFL, `files/ce.dat/fonts/`), stb_truetype glyph atlases per
  pixel size (`third_party/stb`); game texts are decoded by the code page
  of `game_charset.h`: the alphabet from the game's fonts (a Cyrillic or
  Greek font draws the Latin look-alikes as Latin letters), the variant
  from `[system] language`; the fonts win when they disagree (old Russian
  translations in `text/english`). `game\ce.msg` then comes in the device's
  language of that alphabet. The app reads saves' names by the code page
  the game wrote (`game-charset.txt`).
- Game art: `muiArtTexture` converts FRM frames with the current palette,
  nearest filtering (crisp pixels), rebuilt when the palette changes.
- Immediate mode screens (`MuiScreen::build` every frame) in a stack; the
  top one gets input. Modal screens take all touches, non-modal only
  touches on their widgets (`region`), the rest goes to touch.cc/game.
  Pointer events are applied one down/up per frame, so quick taps work.
  Widgets: `touchable`, `button`, `panel`, `dim`, `tappedOutside`; theme in
  `MuiTheme` (placeholder terminal-green).
- `muiRunModal` runs input+render for synchronous game flows; Esc/Android
  back -> `MuiScreen::back`.
- Done: dialog boxes (`showDialogBox` -> `muiShowDialogBox`, texts from
  DBOX.MSG), radial action menu drawing (54 dp circles, own line icons,
  centered on the finger, near edges buttons spread over the free arc;
  input stays in touch_controls, sliding selects only after the finger
  moved 12 dp from where the menu opened).
- Widgets also: `scroll` (inertial, child ids `<id>.`), `dragSource` /
  `dragging` / `dropped` (drag starts on a sideways move or a 350 ms hold),
  `slider`, long press in `touchable` (500 ms).
- Dialog tabs (`muiDialogTabs`, right edge, same place on every dialog
  screen): Talk / Barter (traders only) / History. Talk sends `t`, barter `b`,
  history opens the talk screen's review panel.
- Talk screen (`mui_game_dialog.cc`, `TalkScreen`, active while
  `gameDialogIsTalking`): talking head / map around the speaker copied every
  frame from the game's display buffer (388 x 200, keeps animation and
  lips), reply with the speaker name under it, options on the right (any
  option via `GAME_DIALOG_OPTION_EVENT_BASE + index`, not only the first
  nine). No exit button (the vanilla `0` exit is a bug CE disables with
  `no_exit_hotkey=1`). Autotest: `--dev-autotest-scenario=talk`.
- Barter screen (`mui_barter.cc`, `BarterScreen`, active while
  `barterGetView` != null): the game's `barterProcessUI` loop stays; the
  screen queues `BarterAction`s and posts `BARTER_ACTION_EVENT`, the loop
  performs them with the game's own moves (`itemMoveForce`, weight-checked
  `itemMove` for giving to party members) and redisplays. Columns 3 / 1 / 1 / 3
  (player, offer, request, trader) of square cells of one size, no head. The
  game's dialog panel slide (`_gdialog_scroll_subwin`) is instant with mobile
  UI, so the tabs switch at once; the one frame between the talk loop and the
  barter loop keeps the last shown frame (`MuiScreen::holdsFrame`,
  `muiHoldsFrame` in `renderPresent`, at most 500 ms). Tab column and content
  area of both screens come from `muiDialogLayout`. Tap moves an item between
  inventory and table, drag & drop too; the dragged item shows name, weight
  and cost. Offer (`m`) button, one-line message (`muiBarterMessage` from the
  game's barter/dialog messages). Autotest: `--dev-autotest-scenario=barter`.
- Inventory Filter mod features (the mod itself needs unsafe scripting, which
  CE lacks) in `mui_items.cc`, shared by barter and later loot/inventory:
  category filters (all, weapons, weapons+ammo, armor, drugs, ammo, misc;
  long press weapons = firearms only), money first, weight of the shown
  category, trader money. Party members as tabs (with CE
  `[qol] party_loot_and_barter=1`; party members on the map, like the mod's
  `party_member_list_critters`; also when bartering with a party member,
  unlike the mod - the others stay reachable): tap selects, dropping a player item on a tab gives it. Loot has "Give all"
  / "Take all", the inventory "Drop all" (below).
  Which party members (2026-10-07, the original game + sfall + RPU +
  Inventory Filter as the reference): only those whose items the original
  game lets the player at (bartering and stealing not forbidden, as the
  mod: no animals or robots, no Karl or Jonny, whose scripts turn them
  hostile when caught stealing - always succeeding on a party member is a
  loophole there); a party member on another elevation or over 30 tiles
  away (the mod's range) has a dimmed tab, a tap or a drop on it says
  "... is too far away" (`inventoryPartyMemberIsReachable`,
  `muiNotifyPartyMemberOutOfReach`). Tabs stay where the mod has none
  (bartering with a party member, corpses, stealing from a party member):
  the mod skips them because it swaps inventories through the dude, not
  for balance, and the original reaches the same by more clicks.
  Party members' items on the player's barter table go back to whoever
  put them when the barter ends without a deal (`gBarterTableShares`,
  `barterReturnPlayerTable`); what a party member can't carry anymore goes
  to the dude (as everything did in the game - CE gave all to the dude,
  overloading him), so overloading can't be shifted to party members.
  Taking someone else's item off the table is weight checked (the game
  moved it unchecked). Autotest `barterparty`.
- Inventory screen (`mui_inventory.cc`, `InventoryScreen`, active while
  `inventoryGetView` != null): the game's `inventoryOpen` loop stays, the
  screen queues `InventoryAction`s (`INVENTORY_ACTION_EVENT`) performed with
  the same functions as mouse dragging and the action menu
  (`inventoryPlaceItem`, `inventoryRunItemActionMenu`,
  `inventoryDropToGround`; items are found by pointer and turned into the
  window's key codes). Layout: filters + grid (4 columns) with "Drop all"
  (two taps), character figure (faces the player, a horizontal swipe turns it,
  two-way arrow under the feet, one scale for all directions, stays inside
  open containers) with armor slot,
  HP / AC / weight and hand slots, right panel with the game's summary
  (`inventoryGetSummary`) or the chosen item (`inventoryGetItemInfo`: what
  looking tells, weight, weapon damage/range/AP as if held, armor class and
  thresholds; no price). Back button in the tab column closes an open
  container first. No party tabs here (Inventory Filter has them only in
  barter and loot).
- Loot screen (`mui_loot.cc`, `LootScreen`, active while `lootGetView` !=
  null): the game's `inventoryOpenLooting` loop stays, `LootAction`s
  (`LOOT_ACTION_EVENT`) run inside it (stealing XP and "caught" as for mouse
  moves; moves go through `inventoryLootTransfer`, shared with dragging).
  Two sides like barter: player side (party tabs, `[qol]
  party_loot_and_barter`), looted side (name, arrows between dead critters at
  the tile), filters, grids, weights, "Give all" (Inventory Filter) and "Take
  all" (the game's, weight checked) of the shown items; none of them while
  stealing. No party equipment slots (CE's stealing-from-party-member
  addition). Game messages show as a toast (`muiLootMessage`). Autotest:
  `--dev-autotest-scenario=loot`.
- Gestures of item lists (inventory, barter, loot): tap - info (barter: move),
  holding still - the game's action menu as a radial around the finger
  (look, use, unload, drop), moving sideways - drag (any direction when the
  list fits and doesn't scroll). While dragging: hands, armor, the info panel
  (drop on the ground), items it can go onto (`inventoryCanDropOnto`: ammo
  into matching weapon, anything into a container), the open container's
  header (take out) light up. A drug/consumable from the player's root
  inventory can also be dragged onto the player's figure. Only eligible
  `ITEM_TYPE_DRUG` items light the figure and show the Use icon. The drop
  queues `InventoryActionType::UseOnSelf`; the inventory loop revalidates
  the item and calls the same `drugItemTakeDrug` path as the action menu,
  including its normal effects, scripts, removal, and UI refresh. Dragging
  a non-consumable onto the figure does nothing. The `inventory` autotest
  checks that a dragged stimpak is consumed and a knife is retained.
- What an action changed glows for a moment: target slot, loaded weapon,
  container, the figure after "Use" from the action menu
  (`InventoryView::lastMenuAction`), the weapon after "Unload".
- CE fix: in the game's inventory, dropping a list item onto another list item
  (ammo onto a weapon, item onto a bag) did nothing, only items from slots
  worked.
- Radial menu shows the name of the highlighted action (`game\ce.msg`
  146-155). Opened over a mobile UI screen it takes the mui finger
  (`RadialMenuScreen::pointerInput`).
- Ammo is shown in rounds everywhere, as the game's lists do
  (`_display_inventory_info`): a stack is clips, its object the one clip not
  full, the others full (`itemStackAmount`); cells show all of a stack, even
  one clip. Weight and cost of a stack or its part: `itemStackWeight`,
  `itemStackCost` (the game's inventory totals count with them too; a clip
  not full weighs as a full one, costs by its rounds).
- Under the finger (`muiDrawDraggedItem`, inventory, loot, barter): the
  stack's cell; name, "xN" - the least part that moves (one item, ammo's
  first clip: the one not full, it goes first), its weight and, in barter,
  its price in this deal (`barterGetItemPrice`, the same formula as the
  table totals). So a single clip shows what lands on the table.
- Quantity picker (`mui_quantity.cc`, `muiQuantitySelect`, used by
  `inventoryQuantitySelect`): the stack's cell, name, value, the chosen ones'
  weight and, in a deal with a trader, price (`barterGetMovedQuantity`
  passes it), -/slider/+, all/cancel/done; explosive timer mode (m:ss, step
  10). Ammo: the game moves clips, the picker shows rounds (10 -> 34 -> 58
  for clips of 10, 24, 24). Texts `game\ce.msg` 121-123.
- Weight is shown with the weight icon on every screen (inventory, loot,
  barter, the bubble, the picker), money with "$": symbols, no words.
- Autotest: `--dev-autotest-scenario=ui`, `DEV_AUTOTEST_ACTION_MUI_TAP` taps
  a widget by id (`muiGetWidgetCenter`), `MUI_DRAG` drags widget -> target.
- Character screen (`mui_character.cc`, `muiCharacterScreenRun`), in game
  and for character creation. `characterEditorShow` doesn't create the
  game's editor window under the mobile UI (`characterEditorShowMobile`): the
  editor's state (texts, karma, reputations, tag skills and traits being
  chosen) is set up by `characterEditorStateInit`, shared with the window
  path, and the screen works through the editor's API in
  `character_editor.h` (same rules: skill costs and minimums, tag/trait
  limits, owed perk levels, Tag!/Mutate! steps, Lifegiver/Educated, the
  checks before starting the game). Perks/karma/kills lists come from one
  builder (`characterEditorGetFolder`) used by the window too.
  - In game: header (name, level, HP, experience / next level, skill points,
    Reset = the game's Cancel while something changed), tabs Stats (SPECIAL
    with descriptions, active conditions only, derived stats), Skills (-/+),
    Perks (own frame: perks/karma/kills). Owed perks: the perk panel opens
    when the screen opens (like the game), "Later" leaves a notice on the
    perks tab (dot on the tab). Leaving (back, Android back, other screen
    tabs) keeps the changes like Done; skill points of new levels are the
    starting point of Reset.
  - Right edge: back, inventory, character, Pip-Boy, map
    (`muiGameScreenTabs`, shared with the inventory screen: HUD icons, the
    screen closes and the game gets the HUD key I / C / P / Tab, so the
    game's own rules apply, e.g. inventory AP in combat); creation: back
    (Cancel) only, Done in the header.
  - Switching screens with the tabs: only the game's sound (it plays its own
    when opening, the tab is silent), the last frame stays until the next
    mobile screen covers the screen (`muiHoldFrameForSwitch`, max 500 ms;
    not for Pip-Boy/map, the game's windows yet).
  - Combat (dude's turn): the inventory button of the HUD and the inventory
    tab show what opening it costs (`inventoryCheckOpen`: the game's check
    from `inventoryOpen`, Quick Pockets, config, sfall `set_inven_ap_cost`),
    red when AP are short. The same translucent unavailable overlay dims
    the HUD and tab icons when AP are short, and the Pip-Boy HUD button and
    tabs throughout combat; the AP badge remains on top. These controls
    still accept taps and show the game's existing feedback. A blocked tab
    tap keeps the screen (`muiSwitchGameScreen` returns false). The cost is
    paid on every opening, as in the game.
  - No Pip-Boy (2026-10-01): as the inventory's check, the rule is the
    game's and lives in its module (`pipboyUnavailableReason`: in combat,
    then not worn - before the vault suit movie, sfall's pipboy at start
    aside). The HUD button and the tabs are dimmed for either; the HUD's
    tap shows the game's box (`pipboyShowUnavailable`, from
    `gameCommandExecute` and `pipboyOpen`), a tab's tap keeps the screen
    with the game's text as a warning notice. Autotest `pipboynotworn`.
  - Skills' - and + and the skill points counter show only while points are
    to spend or spent here can be taken back (`skillPointsInPlay`); the
    game's window has them always, doing nothing then but its error boxes.
  - Owed perks put off with "Later" don't open the panel by themselves again
    (switching screens) until more are owed; dot on the tab and a notice.
  - Creation: name (text field, soft keyboard, `SDL_TEXTINPUT` ->
    `muiHandleTextInput`, game charset via `muiEncodeGameText`, 11 chars),
    age -/+, gender, character points, SPECIAL -/+, tag skills, traits tab.
    The game's option window (save/load/print a character file) isn't there.
  - Card: skilldex picture (`muiLineArtTexture`: the drawing's dark ink from
    the game's color table becomes the interface color, the paper
    transparent; all 174 pictures work), title, subtitle, description
    (scrolls when long).
  - Small mistakes (no skill points, at minimum/maximum, all tags used) are a
    toast with the game's texts and error sound; creation checks use the
    game's dialog boxes.
  - Overlays inside one screen (perk panel, name field) switch the rest of
    the screen off (`ui.isInteractive`): the first widget under the finger
    gets the touch, so rows with buttons limit their own touch area.
  - Texts `game\ce.msg` 190-198, the rest from `game\editor.msg`.
  - Autotests: `character` (test save, +7000 xp, perk, skills, tabs, reset,
    inventory tab), `creation` (SPECIAL, tag, trait, typed name, age,
    gender, done checks, cancel).
- Notifications (`mui_notify.cc`, `muiNotify`), one queue for everything the
  player should see right away (audit 2026-09-27):
  - Sources where the game shows them, its logic unchanged: the message log
    (`displayMonitorAddMessage`: ~110 places, scripts, mods), replies of the
    one talked to (`gameDialogRenderSupplementaryMessage`: barter results,
    item descriptions when looking in barter, party member control replies;
    with the speaker's name), mistakes on the mobile screens (character
    screen, `MuiNoticeKind::Warning`, the game's error sound, not logged -
    the game showed dialog boxes).
  - Where: on the map with the touch HUD, log lines next to the log button
    (`muiNotifyHudArea`, none while the log is open); over screens covering
    the HUD (inventory, loot, barter, talk, character, the game's full screen
    windows) plates at the top center (up to 3, up to 5 lines, time by
    length). Drawn by the kit over every screen (`muiRender`).
  - Not notifications, stay: dialog boxes with questions/answers
    (`showDialogBox` -> `muiShowDialogBox`), floating text over critters
    (world), status indicators (HUD).
  - Removed: loot's own message bubble, barter's reply strip (the offer
    button is under the two tables now), the HUD's own toasts, the character
    screen's toast.
- Pip-Boy (`mui_pipboy.cc`, `muiPipboyScreenRun`), stage 1 of 3.
  `pipboyOpen` doesn't create the game's window under the mobile UI
  (`pipboyOpenMobile`); state shared with the window (`pipboyStateInit` /
  `pipboyStateFree`: rest options, texts, quests, holodisks, the map update
  script on close), data API in `pipboy.h`. Sections: quests (locations with
  the count to do, done ones crossed out at the end), data (holodisk lines
  joined into paragraphs), maps (locations accordion with their maps; the
  saved automap tiles from `automapGetPipboyTiles`, explored part only),
  videos (seen movies, the game plays them full screen), rest (clock, date,
  HP; the game's 13/14 options grouped: rest for (with the wake time), wake
  up at (the game's wake hours, sfall can change them), until healed;
  "Stop" = the game's Esc while its rest loop runs, the screen draws the
  clock during it; `pipboyShouldClose` when events interrupt). The game's
  draw calls of the rest loop skip without a window. Opened to rest (a bed)
  where it isn't allowed: the game's message as a notification. Texts
  `game\ce.msg` 199-211. Autotests: `pipboy` (mobile), `pcpipboy` (the
  game's window, PC mode; `DEV_AUTOTEST_ACTION_EVENT` sends window button
  codes).
  - Stage 2 done: maps renderer `MuiMapView` (mui_map_view.cc): the game's
    automap tiles drawn as glowing lines between neighbouring walls and dim
    scenery dots into a render target texture (one geometry call per layer,
    texture pixels per tile in steps, drawn again after a pinch), markers on
    top; `MuiContext::panZoom` — one finger drags, two fingers pinch around
    their middle (mui keeps a second finger now). Pip-Boy maps use it; the
    map tab is a mobile screen too (`mui_automap.cc`, `automapShow` under
    the mobile UI): `automapGetView` (what the game's window shows: seen
    walls, scenery with details, dude, exit grids, critters with the motion
    sensor), details switch (`automapSetHighDetails`, remembered like the
    game's), motion sensor (`automapActivateScanner`: the game's rule and
    charge, its messages as notifications). Autotests `automap`, `pipboy`
    (pinch steps).
  - Stage 3 done: movie player of the mobile UI. `movieStartMobile`
    (movie.cc): no window, every MVE frame goes to the texture
    (`movieGetFrameTexture`), subtitles as text (`movieGetSubtitle`), the
    caller's loop presents frames (steps still run from the background
    processes in `inputGetInput`); `moviePause` holds stepping and the
    movie's sound (`MVE_rmHoldMovie`: sound buffer stopped, timing reset on
    release). `gameMovieStartMobile` / `gameMovieFinishMobile` (game_movie.cc):
    file, subtitles setting, music pause, movie effects; seen mark, palette
    and music back. `muiDrawMovie` draws frame + subtitle. Pip-Boy videos:
    player in the detail pane (play/pause, stop, full screen with controls
    for 3 s after a tap); leaving the section / another video stops it.
    No seeking (MVE).
  - All game movies go through it: `gameMoviePlay` under the mobile UI
    (`gameMoviePlayMobile`) plays into the movie screen (`muiMovieScreenRun`,
    over the whole screen, subtitles drawn by it); a tap or a key skips like
    the game's, no player controls (user's wish). The game's fades and music
    flags stay (fade to black before, palette black after, fade in with
    FADE_OUT). Autotest `movie` (`DEV_AUTOTEST_ACTION_PLAY_MOVIE`).
    A title-menu or character-selector tap left a synthetic mouse press in
    the old window system. The movie loop treated `inputGetInput() == -2`
    from that same press as a new skip and closed Intro or Elder immediately.
    The full-screen player now waits for the opening press to be released
    before accepting a later mouse click as skip; its native touch widget
    and keyboard skip still work. Regression scenarios `titleintro` and
    `newgameelder` tap the real legacy buttons, verify a decoded movie frame,
    and capture it. They failed before this change and passed afterward;
    the existing `movie` scenario also passed its new-touch skip check. This
    follow-up was completed by Codex on GPT-6 Sol (`gpt-6-sol`).
- Called shot (`mui_called_shot.cc`, `muiSelectCalledShot` from
  `calledShotSelectHitLocation`): a panel in the room the HUD leaves (HUD
  element rects), the target's name, its called shot picture between the
  game's two columns of hit locations (names and chances from
  `calledShotGetTargets`: the game's order, `_determine_to_hit`; no chance
  shows "--" like the game's window, the map hint too); a tap outside, the
  close button or back cancels. The game's window isn't created under the
  mobile UI. Autotest `calledshot` (`DEV_AUTOTEST_ACTION_MOVE_NEAR_CRITTER`;
  the attack itself fails in that test setup — teleported dude, plain shots
  too — not the panel).
- Game menu (`mui_options.cc`, `showOptions` under the mobile UI): a panel
  over the dimmed map, where and the game's date at the top, Continue / Save
  / Load / Settings (the game's preferences window for now) / Exit (the
  game's quit confirmation); the game's window keys; a tap outside or back
  continues. Returns 1 after a save or a load like the game's window.
- Save / load screen (`mui_loadsave.cc`, opened by `lsgSaveGame` /
  `lsgLoadGame` under the mobile UI, the game's window isn't created):
  - Every save newest first, numbered by that order (the game's slots aren't
    shown); quick saves marked. A save shows its description, otherwise
    where it was made, under it the place or the game's date. Broken and old
    saves use the game's list texts (LSGAME.MSG 112/113) and can only be
    deleted.
  - Right: picture, character, place, game date, when it was made; Delete
    left of the main action. Saving makes a new save only ("+ New save" row
    pinned at the top, description typed with the Android keyboard, empty
    by default); existing saves aren't overwritten. A quick save can be
    copied to a permanent one.
  - From the title screen: load only, no Save / Load switch.
  - Errors are the game's (LSGAME.MSG 132-135); a load that fails goes back
    to the main menu like the game's window. While a save loads a plate
    covers the screen (the game presents frames meanwhile). After loading
    from the title screen the game's palette is restored (the title fade
    leaves it black); the busy cursor is reset when the screen closes (it
    keeps map input off).
  - Pictures: saves made with the mobile UI have `PREVIEW.PNG` (the world as
    seen, widescreen); others show SAVE.DAT's thumbnail converted with the
    game's palette.
- Save catalog (`save_catalog.cc`, `save_storage.cc`; used by loadsave.cc
  for every save, PC mode too). The game's slots and files stay as CE/sfall
  have them; the catalog decides:
  - quick saves: `[ui] auto_quick_save` pages from `auto_quick_save_page`
    (phone: slots 11-20), a free one or the oldest quick save, no
    description; manual saves: the first free slot outside that range;
  - the number of quick saves is a settings row (0-100); changing it moves
    saves between slots so none is lost (`setQuickRange`), a quick save
    made permanent moves to a manual slot keeping its place (`move`, its
    record goes along; a record left behind by a stop is picked up by the
    next refresh);
  - order: `CE-META.TXT` next to SAVE.DAT (creation time and order, bound to
    SAVE.DAT's time, size and header), saves without it use SAVE.DAT's time;
  - quick load: the last save loaded or made in this session;
  - writes are transactions over the slot folder (`.ce-write-N`: the old
    folder moved aside, back when the write fails or the game stops midway;
    `.ce-cleanup-N`: removed folders), recovered by the next refresh;
  - a refresh lists SAVEGAME once and reads only the slots in it (phone,
    20 saves: 0.7 ms; checking all 1000 slots took 69 ms). The screen
    refreshes when it opens, a quick save to pick its slot; writes, removal
    and copies update only their slot.
  Test: `ctest` in the build folder (`save_catalog_test` target).
- Autotests (`runtest.sh`, `TITLE=1` for the title screen ones): `muiloadsave`
  (new save with a description, load), `muiloadsavedelete`, `muiloadfailed`
  (broken save: game's error, main menu), `savehistory` (quick save ring,
  session quick load, copy), `muiloadsavemain` / `muiloadsavemainempty` /
  `muiloadsavemainresume` (title screen: palette, map input after loading).
  Scenarios changing saves run in a SAVEGAME of their own; the test game's
  saves wait in `SAVEGAME.autotest` and come back at exit (or at the next
  run after a killed one).
- Settings screen (`src/mui_preferences.cc`, `preferences.cc`
  `doPreferences` under `muiIsEnabled()`; the game's window stays for
  `mobile_ui=0`):
  - sections at the left (`muiSectionList`), the section's rows at the right
    in a scroll area, Reset all / Apply under them, Back in the right rail
    (`muiGameScreenTabs` with no screens);
  - rows are data (`makeRows`): the game's settings through
    `PreferenceValues` (`preferencesGetValues` / `preferencesApply`, the
    same steps as the window's Done: `_SavePrefs`, `_JustUpdate_`, combat
    highlight), the port's (`[enhancements]`, `touch.hud_scale`,
    `world_view.filter`) and CE's (`[qol]`, `[ui]`) settings with badges
    "порт" / "CE"; value labels from OPTIONS.MSG, names from ce.msg
    245-289;
  - kit widgets: `toggle`, `segmented` (cells as wide as their labels, one
    text size), `slider` (finger drags it smoothly, reports `dragging`);
  - a change marks its row and section with a dot, Apply is highlighted
    while something changed; volumes and brightness are heard / seen at
    once (`preferencesPreview`, sample sounds when the finger lets go,
    brightness on a map thumbnail `worldViewCaptureView`, in game only),
    the rest waits for Apply;
  - Back with changes: "Применить изменения?" Да (apply) / Нет (volumes and
    brightness back); Reset all asks first, then sets every tab's defaults
    (the game's window's defaults, our settings' own defaults) without
    applying;
  - hidden on touch: mouse sensitivity; not shown yet: the quick save count
    (needs a safe move, see the roadmap) and CE options not checked in the
    new screens.
  Autotest `preferences` (discard, apply, reset + Yes); `runtest.sh` puts
  the test game's fallout2.cfg back after every run.
- Main menu (`src/mui_main_menu.cc`; `mainmenu.cc` creates no window under
  the mobile UI, shows / hides the screen where it shows / hides the window,
  its loop takes the tapped button as a `MainMenuOption`, so `main.cc` runs
  everything as before):
  - the game's high resolution menu picture (HR_MAINMENU.FRM of
    f2_res.dat, no button plates: those are HR_MENU_BG.FRM, not used) fit to
    the screen height (no distortion), pinned to the right edge, its left
    edge fading into black up to the buttons; without f2_res.dat the 640x480
    MAINMENU.FRM right of its baked plates. A wider picture would show more
    (see the roadmap's idea about extending it);
  - Continue (`[enhancements] main_menu_continue`, a settings row) loads the
    save made last (`SaveCatalog::newest`, `lsgContinueGame`) with its place
    under the label (MAP.MSG read by the screen: the game loads map names
    only for a game); New game, Load game, Settings (the game's "Menu"),
    then Intro, Credits, Exit smaller in a row; copyright and version below;
  - no screensaver timeout on it (a phone left on the title would start the
    intro); keys work as in the game.
- Premade character selector (`src/mui_character_selector.cc`,
  `characterSelectorOpen` under the mobile UI; `character_selector.h`
  exposes the premade list, portraits, biographies):
  - tabs with the characters' names and "+ Own"; the portrait (592x260
    panorama without the monitor's edge), a swipe over it goes to the next
    one; the biography (.bio lines joined again, its first line the title)
    scrolls;
  - stats in two columns: SPECIAL as short names (ce.msg) with the game's
    word for the value, hit points, armor class, action points, melee damage,
    tagged skills; traits under them;
  - buttons: Modify (the character screen with that character) and Play; on
    "+ Own" the dude as the game's Create makes it (all 5, free points in
    amber, "tag 3", "up to 2 traits") and Create character. The own card's
    picture is the game's loading screen SPLASH3.RIX (a lone figure, nobody
    in particular; `gameReadSplash`, the language's one or the English one),
    its band with the figure;
  - no palette fades (the screen's pictures are drawn in full colour).
  Autotests: `mainmenu` (Continue shown, settings and back, selector tabs,
  swipe, own card, Create and Modify with cancel, Continue loads the world),
  `newgameelder` and `titleintro` go through the new buttons.
- Small screens (2026-09-29):
  - elevator (`src/mui_elevator.cc`, `elevator.cc` `elevatorSelectLevel`
    under the mobile UI): the game's panel art (its level names are drawn on
    it, localized) in a card of the UI (rounded, the terminal's border, "Elevator"
    and the level the dude is at over it) over the dimmed map, the HUD hidden
    (`MuiScreen::hidesHud`), a finger target per level over
    its button and name, the pressed button art, the gauge's needle moving to
    the chosen level at the game's pace (`anim_speed`) with the game's lift
    sound, then the game's pause; Back in the rail or a tap outside the card
    stays (the game's Esc);
    level keys work. The start level is `elevatorStartLevel`, shared with the
    game's panel. Autotest `elevator` (`OPEN_ELEVATOR` action);
  - death screen (`main.cc` `showDeath`) and ending slides (`endgame.cc`):
    the scene screen (`src/mui_scenes.cc`): the game's picture (death) or the
    frame the game composed (endings: panning, fades, its fitting kept; the
    frame buffer is ours, not a window), the game's palette and fades, the
    subtitle in the UI's font over the bottom (death text without its "1:"
    timings); a tap skips as a key. Autotests `death` (`KILL_DUDE`, back to
    the main menu) and `endgame` (`SET_GVAR` of two endings,
    `REQUEST_ENDGAME`);
  - credits and quotes (`credits.cc` `creditsOpen`): the lines roll in the
    UI's font in the game's colors (title / name / group) at the game's pace;
    a tap or a key stops them;
  - F1 help: not shown under the mobile UI (a picture of the keyboard and
    mouse controls the touch UI replaced; a touch help is a later idea).
- Loading curtain: the game's black modal window over the screen while a
  game loads (new game, load from the main menu, Continue) is the mobile
  UI's black screen under it (`muiCurtainShow`, `main.cc`
  `mainCurtainShow`): no window of the game in these flows.
- Taps begun before a screen showed are not its taps (`touchable`: the
  finger's down time, from its event, before the screen's open time). The
  main menu counts its open time from the end of its fade in
  (`muiMainMenuReady`): a tap made in the dark (skipping the death screen,
  during the fades) no longer presses Continue or New game.
- World map (`src/mui_worldmap.cc`, `worldmap.cc` under the mobile UI):
  - the game's logic stays: `wmTravelUpdate` (steps, the car and its gas,
    healing, time, encounters with their "encounter?" dialog) and
    `wmEnterPartyLocation` (a visited town with its map: the town map;
    another area: its first map; the wasteland: `desert1`, as tapping the
    party in the game) are shared by the game's window loop and the mobile
    loop (`wmWorldMapMobileLoop`); the screen reads `wmMobileGetState` and
    asks for travel / entering (`MuiWorldmapAction`); `wmInterfaceInit`
    keeps the logic's art, the window part (`wmInterfaceWindowInit`: window,
    background, dial, tabs, buttons, mouse edge scrolling) is only for the
    game's window, drawing into it is skipped without it
    (`wmInterfaceHasWindow`);
  - the world as the game's world: terrain art over the whole screen, a
    finger moves it, two fingers zoom (never smaller than the screen); towns
    as rings (visited solid, known dashed, the destination amber) with their
    names, under the fog as in the game; the fog a texture of a pixel per
    subtile, smoothly scaled (unknown black, known dimmed); the party a dot,
    dots to the destination's cross; the encounter icon blinking red
    (special amber);
  - a tap on the terrain walks there, on a town goes to its center, on the
    party (not walking) enters; "Enter <town>" / "Enter the wasteland" at the
    bottom center when standing; the menu button and the date / time chip
    (the car's gas under it in the car) at the top left, "Towns" at the top
    right: a popup list (header, close, rows with "you're here" /
    "destination" / "visited", scrolls under its header), a tap beside it
    only closes it; "Enter" stays under the open list, which covers it where
    they meet (2026-10-01, user's wish);
  - the menu button opens the game menu itself (`muiWorldmapMenuRun` ->
    `muiGameMenuRun` of mui_options.cc, 2026-10-01, user's wish: one menu,
    not two): dimmed world map, the town the party is at and the date in
    its header, Continue, Settings, Exit (the game's quit dialog) - no save
    and load, the game has none on the world map; the game's options mode
    is on meanwhile (as `showOptions`) and hides the world map's controls
    as it hides the touch HUD on the map;
  - the camera follows the travelling party (`[enhancements]
    worldmap_follow_party`, a settings row), a finger on the map pauses it
    until the next destination;
  - town map (`muiTownMapRun`): the town's picture fit to the screen, its
    entrances (the game's rules for which show) as marks with names, the
    town's name framed at the top left, Back in the rail; number keys work;
  - autotest `worldmap` (`WORLDMAP_AREA`: Klamath visited, the party there).
- Script windows (`window.cc`, 2026-09-29): checked every script the game
  and the installed mods run (master.dat, rpu.dat, party_orders.dat,
  npc_armor.dat, sfall.dat, data/scripts; the bytecode walked from the
  procedures' code, as the interpreter reads it). No script creates a window,
  buttons or regions (`create_win`, `add_button`, sfall `create_win`,
  `draw_image`, `interface_art_draw`, ... - none). Only:
  - `barstow`, `dumar`, `surf` use the interpreter's own dialog (`say_*`):
    developers' test scripts, not in scripts.lst, never loaded;
  - Festus (`gcfestus`, Gecko) calls `display` with a reply's text as a file
    name in dialog node 30: a bug of the script, nothing is read or shown (in
    the game too);
  - sfall's party control (`gl_partycontrol`) adds an interface tag: the
    touch HUD shows the indicator bar's tags, sfall's custom ones included.
  Nothing to port. A mod made later that creates a script window would show
  it as the game's window; since the touch-only build (no mouse emulation,
  2026-09-30) its buttons can't be pressed on Android - such a mod needs a
  mobile renderer for script windows.
- Touch architecture steps 3-4 (2026-09-29): fingers reach the map with no
  mouse in between (touch-native input), the mouse emulation is only for the
  game's windows (`mobile_ui=0`), scripts reading the mouse get the last map
  touch; see touch-architecture.md. The Android build is touch-only
  (`FALLOUT_TOUCH_ONLY`): no mouse at all, the mobile UI can't be turned off
  (`[touch] mobile_ui`, `controls`, `hud`, `[world_view] enabled` are forced
  on).
- Legacy stage 0 (2026-09-30): the mode checks are constants on touch-only,
  the Android library drops what only the replaced windows used
  (`--gc-sections`, hidden visibility). Phone APK: `./gradlew assemblePhone
  -PabiFilters=arm64-v8a` (build type `phone`: Release -O3, same package
  `.debug` and key as the debug app, installs over it) ->
  `app/build/outputs/apk/phone/app-phone.apk`. Library code: Debug 10.3 MB,
  phone 4.9 MB; APK 6.9 -> 2.7 MB. `assembleDebug` (-O0, nothing dropped)
  stays for debugging.
- Legacy stage 1 (2026-09-30): commands instead of keys - the HUD and the
  mobile screens post typed commands and requests (`game_commands.h`),
  Android's back button is the mobile UI's Back, the touch-only build takes
  no keyboard (mods' own keys and a soft keyboard's text editing aside);
  see touch-architecture.md.
- Legacy stage 2 (2026-09-30): talk and barter make no game windows under the
  mobile UI; the talking head is drawn into an own canvas; the game's logic
  runs in the same order, the screens' loops wait for the player. Logic
  checked by state traces (`dialogtrace`, `headtrace`, `bartertrace`)
  against the build before and the game's windows (`out/dev/runpc.sh`).
- Legacy stage 3 (2026-09-30): inventory and loot/steal (and trade) make no
  windows, buttons or drawing under the mobile UI, the screens' loops wait
  for the player; `inventorytrace` / `loottrace` match the build before step
  by step, `pcinventory` checks the game's windows.
- Legacy stage 4 (2026-09-30): with the touch HUD the interface bar is a
  model without a window (no art, buttons, side panels, monitor art);
  combat doesn't wait for the invisible end buttons' animation. Traces
  `hudtrace`, `combattrace`, `calledtrace`, `pcbar` (`out/dev/runbar.sh`).
- Legacy stage 5 (2026-09-30): no pause window, no debug window (script
  errors to the log), map save errors in the mobile dialog, scripts' own
  windows unsupported (logged), the cursor is only its id (no art), the
  bar/inventory/monitor drawing bodies are out of the touch-only build.

## Port enhancements (`[enhancements]`, `EnhancementSettings`)

Improvements that Fallout 2 CE, sfall and RPU don't have: they keep the
game's rules and data (saves, scripts and mods work the same) and only make
playing better. Each is a setting of `[enhancements]` in fallout2.cfg
(default on, 0 turns it off), later a tab of the settings screen. They are
written into the engine where the game does the thing (no separate layer);
the setting only picks the behavior there. CE's own `[qol]` section stays
CE's.

- `combat_speed_all_animations` (2026-09-29). The game's combat speed
  preference (0-50, `animationComputeTicksPerFrame`, original 0x418794) adds
  to the frames per second of **walking** in combat only, the player's with
  "player speedup". Running always plays at its FRM speed, and who runs is
  the game's choice (AI: at least half its max AP and the critter's
  `critters.lst` run flag - geckos and people run, ants can't; the player:
  "always run" or the same tile again), so at full speed runners were slower
  than walkers. With the setting running gets the same addition, every other
  animation in combat (attacks, reloads, hits, knockdowns, deaths, getting
  up, weapon draw, explosions) up to 2x at full speed. The game times what
  happens during an animation in its frames (`delay` of animation
  descriptions counts frames of the sequence's animation: attack sound and
  hit at the action frame, projectiles, hit reactions), so it stays in step.
  Measured (autotest `combatspeed`, dude, 4 hexes / a punch, ms): speed 0 -
  walk 1418, run 422, punch 1131; speed 50 game's rule - 255, 421, 1131;
  with the setting - 256, 134, 575.
  Sounds of these animations (shots, bursts, hits, screams: the ones an
  animation sequence plays, `animationLoadSoundEffect`) play as fast, same
  pitch: `soundSetTempo` before loading, sound.cc shortens a sound loaded
  whole before it reaches the sound buffer (`sound_tempo.cc`). The method:
  attacks (a shot's start: a 5 ms window 4 times as loud as the 20 ms
  before) are put exactly at their new times, the parts between them are
  shortened by WSOLA (30 ms pieces, each placed within 10 ms where it
  continues the previous one best, crossfaded) and cut with a 4 ms fade
  where the next attack comes. Plain WSOLA (ffmpeg's atempo too) loses or
  doubles shots of a burst (6-9 of 10 at 2x in `sound_tempo_test`); this
  keeps all. Continuous fire ("brrr", no separate attacks) is one part.
  Speech, music and ambient sounds aren't touched. The longest burst (2 s,
  stereo) takes about 1 ms on the Mac, 7 ms on the phone, when the attack
  starts. Autotest `combatspeed` logs a burst's length: 2057 ms, with the
  setting at full speed 1065 ms.
- Not a setting (a fix of the port's timing): the game advanced an animation
  by one frame per pass of its loop, so it couldn't be faster than the frame
  rate - on the phone the loop drops to 12-35 fps while the map scrolls, so
  fast walking was several times slower than meant. `_object_animate` now
  takes the frames the loop was late for too (each frame with everything it
  triggers: steps, action points, spatial scripts, sequence delays), up to
  250 ms behind; a longer stall (a menu, loading) goes on from the next
  frame as before. With 50 ms added to every frame (autotest): walk 329,
  run 218, punch 654 ms instead of several times longer. Only in combat
  (out of it the extra frames cost the phone's loop more than they gave),
  and the frames' refreshes are drawn once at the end of `_object_animate`
  (`tileBeginDeferredRefresh`: touching rects merged, past 48 a new one
  joins the nearest).

## Cursor-era issues (resolved)

Noted 2026-09-25 for when the cursor goes away; all done since: targets and
the selected tile are kept in map coordinates (`player_commands.cc`,
`map_hints.cc`), there's no cursor sprite under the mobile UI, the action
menu is the radial menu, floating texts and combat hints are fixed-size
overlays (`mui_floating_text.cc`, `map_hints.cc`), scroll limits follow the
visible rect of the world view, movies are throttled (the mobile movie
screen).

## Attack modes and reload (resolved)

The original hides them in the weapon button's right click (`N` cycles
primary -> aimed -> secondary -> aimed -> reload). The touch HUD shows them
as an attack-mode strip next to the weapon button (`touch_hud.cc`: the
weapon's modes, reload as an icon, fixed width, scrolls sideways), plus
end turn / end combat, swap hands, sneak and quick save/load buttons.

## Testing

- macOS: `--dev-new-game --dev-autotest=<dir>` (optionally
  `--dev-map=<map>`, default start map is faster) runs a scripted
  zoom/pan/pinch/walk/radial/inventory/combat sequence (`src/dev_autotest.cc`) and saves
  frames + log. Use `mouse_lock=1` so the cursor is engine-owned like on
  Android.
- Scenarios: `--dev-autotest-scenario=<name>` (`devAutotestSetScenario`
  in `src/dev_autotest.cc` lists them); `out/dev/runtest.sh` (ignored local
  rig) runs one on a copy of the game, frames + log.txt; a FAIL or NOT FOUND
  line is a failure. Screens at other sizes: the game's `[screen]
  resolution_x` with 480 lines and `[touch] hud_density` (output pixels per
  dp), as Android picks them (`svga.cc`, `hud_layout.cc`).
- The owner's Xiaomi: `adb install` works (allowed in the developer
  options; a new package asks on the phone once), `adb shell input` needs a
  SIM card ("USB debugging (security settings)"), so touch testing on the
  device is manual.
