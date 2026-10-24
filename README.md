# Fallout 2 CE Mobile (unofficial)

[Русская версия](README.ru.md)

A fork of [Fallout 2: Community Engine](https://github.com/fallout2-ce/fallout2-ce) made for Android phones: every screen of the game rebuilt for a touch screen, in landscape. **Early beta.**

This is an **unofficial, fan-made, modified version** of Fallout 2 CE. It is not affiliated with or endorsed by Bethesda Softworks, ZeniMax Media or Interplay. Fallout is a trademark of its owners. The app contains **no game data**: you need your own copy of Fallout 2 ([GOG](https://www.gog.com/game/fallout_2), [Steam](https://store.steampowered.com/app/38410), [Epic Games](https://store.epicgames.com/p/fallout-2)).

## Screenshots

<p><img src="docs/screenshots/combat.jpg" alt="Combat: an enemy picked, its hit chance" width="49%"> <img src="docs/screenshots/dialogue.jpg" alt="Dialogue: the drill sergeant at Navarro" width="49%"></p>

<p><img src="docs/screenshots/inventory.jpg" alt="Inventory: an item's action menu" width="49%"> <img src="docs/screenshots/character.jpg" alt="Character" width="49%"></p>

## What is changed (modified software notice)

On top of CE (its engine, fixes and sfall compatibility are kept and updated from upstream):

- **Touch UI.** The game's screens are replaced with screens made for fingers and the phone's resolution: dialogue, barter, inventory, loot, character and level-up, character creation, Pip-Boy (quests, holodisks with their voice, maps, videos, rest), automap, world map and town maps, skills, game menu, settings, save/load, main menu, dialog boxes, called shots. No cursor: tap to act, long press for the action menu, drag to move items, two fingers to zoom the map.
- **HUD.** Buttons anchored to the screen's edges (sized in dp), the attack modes with reload, the message log, end turn/combat.
- **Tactical view in combat** (a button, can be turned off in the settings): figures turn see-through, the hexes you can reach and everyone's hex are drawn on the ground, so a tile behind an enemy can be picked and an enemy chosen by its hex.
- **Mod features built in** where the mods can't run on CE: Inventory Filter's filters and party tabs in loot and barter.
- **Saves.** A save list sorted by when saves were made, quick saves that never overwrite manual ones (their number is a setting), "Continue" in the main menu, saves interrupted midway are restored, optional marks for saves made with other game files or mods.
- **Android app.** The game's files are imported from a folder or any archive (.zip, .7z, .rar, .tar...) copied from a computer, with RPU and other mods and the saves; "Replace game" for new builds and mod updates; export and import of saves.
- **The port's own improvements** are settings (`[enhancements]` in `fallout2.cfg`).

The full list of changes is the git history over upstream.

## Install on Android

1. Download the APK from [Releases](https://github.com/imbaner/fallout2-ce-mobile/releases) and install it.
2. Copy your Fallout 2 folder from a computer to the phone: as is, or as one archive (cable, cloud drive, messenger). Mods installed there (for example [RPU](https://github.com/BGforgeNet/Fallout2_Restoration_Project)) and saves come along.
3. Start the app and choose that folder or archive.

Tested on a Redmi 15C (Android 16); players also run it on tablets (Alldocube). Android 7.0 or newer, arm64.

Plain Fallout 2 from GOG, Steam or Epic works too, with or without mods. The game's language is taken from its own files, older translations included; if your copy doesn't work, please open an [issue](https://github.com/imbaner/fallout2-ce-mobile/issues).

**Recommended setup: Fallout 2 + [Restoration Project (RPU)](https://github.com/BGforgeNet/Fallout2_Restoration_Project) 2.4.34.** The port is played and tested with it. RPU relies on sfall, which this engine reimplements: Fallout 2: Community Engine, the continuation of the original Fallout 2 Community Edition. Install RPU into the game folder on a computer, then copy the folder to the phone.

## App updates

Settings → About lets you check for updates and turn off automatic checks
(on a full launch, at most once an hour by default). A new release appears
quietly on the main menu.
Update downloads its APK and opens Android's installation confirmation;
installation is available only from the main menu. Closing the update
window never launches an installer later. A completed download survives
restarts and can be installed later or deleted from Settings → About.

The app contacts GitHub (`api.github.com`, `github.com` and download servers
under `*.githubusercontent.com`) to check for and download updates. Game
files, saves and player actions are not sent. With automatic checks off,
network requests happen only at your request. Before installation, the
APK's SHA-256 is checked against GitHub's release metadata; its package,
version and signing certificate are checked against the installed app.
Full licenses are available separately, offline, in About.

## Known limitations

- Landscape only; portrait is planned.
- Android only: no iOS or browser version yet.
- Laid out for phones: on tablets, 4:3 screens especially, the same layout is scaled up.
- Made for phones and tested on Android; the desktop builds keep CE's original windows (`[touch] mobile_ui=0`) and are not the focus of this fork.
- Mods with their own script windows are not covered by the touch UI (none of the popular ones use them).

## Building

Android: JDK 17, Android SDK with NDK 27.3.13750724.

```
cd os/android
./gradlew assembleRelease -PabiFilters=arm64-v8a
```

A release build is signed with your own key: point the Gradle property `fallout2ce.releaseKeystoreProperties` to a properties file with `storeFile`, `storePassword`, `keyAlias`, `keyPassword` (never commit it). Desktop builds are as upstream's (CMake).

## License

The engine is under the [Sustainable Use License](LICENSE.md), as upstream: free, non-commercial distribution only, with the license and notices kept. Third-party components: SDL 2, zlib, LodePNG (zlib license), libarchive (BSD), liblzma from XZ Utils (0BSD), stb (MIT / public domain), fpattern, PT Mono font (SIL OFL 1.1). The app shows all of them in Settings → About.

Credits: Alexander Batalov and the Fallout 2 CE contributors for the engine; the sfall, RPU and Inventory Filter authors, whose features are reimplemented here by their behavior (no code taken).

---

# Fallout 2: Community Engine

Fallout 2:CE is a fully working re-implementation of the classic Fallout engine, optimized for a hassle-free experience on multiple platforms, including Windows, Mac, iOS, Android, and Linux.  It provides high resolution support, quality-of-life improvements, and dozens upon dozens of bug fixes.

This is a fork of the original Fallout 2:CE project, which is no longer getting regular updates.

Popular total conversion mods are partially supported. Nevada and Sonora work. [Fallout 2 Restoration Project](https://github.com/BGforgeNet/Fallout2_Restoration_Project) is supported. [Fallout Et Tu](https://github.com/rotators/Fo1in2) and [Olympus 2207](https://olympus2207.com) are not yet supported. Other mods (particularly Resurrection and Yesterday) are not tested.

CE has broad (though not total) compatibility with [Sfall](https://github.com/sfall-team/sfall) scripting extensions.  Many traditional Fallout mods work out of the box, including NPC Armor, Party Orders, FO2Tweaks (we recommend v14.5+), Talking Heads, and many others.

## Installation — [Download Latest CE Release](https://github.com/fallout2-ce/fallout2-ce/releases)

You *must* own the game to play. Purchase your copy on [GOG](https://www.gog.com/game/fallout_2), [Epic Games](https://store.epicgames.com/p/fallout-2) or [Steam](https://store.steampowered.com/app/38410).

Download CE from the link above, then extract the archive into your game folder. Launch `fallout2-ce.exe` on Windows (or the platform-specific executable) to play. CE has a lot of enhancements that require configuration. See [Configuration](#configuration) for examples.

For detailed instructions on how to play on Mac, Linux, iOS, or Android, see the [Platform-specific instructions](#platform-specific-installation).

## Quality of life benefits over vanilla Fallout

* High resolution support
* Party members can loot and barter in place of PC
* Directly equip party members instead of convincing them to use the right equipment.
* Expanded 2-column inventory, loot screens
* Expanded 4-row barter screen
* Expanded AP bar
* Ctrl-click to quickly move items when bartering, looting, or stealing, and auto-balance caps
* Music continues playing between maps
* Auto open doors
* Integrated "HELP" menu
* 44.1 kHz stereo sound/music supported, in .ogg and .wav format as well as legacy .acm
* Last used save slot is remembered
* You can cancel elevator floor selection using ESC
* Spiffy ammo meter with burst-mode segmentation
* Item/Corpse/Container/Critter highlighting (configure using [mods/sfall-mods.ini](https://github.com/sfall-team/sfall/blob/master/artifacts/config_files/sfall-mods.ini))

<table>
  <tr>
    <td align="center">
    <img src="docs/images/two-column-inventory.jpg" alt="Expanded 2-column inventory" width="360"><br>
    <sub>Expanded 2-column inventory</sub>
    </td>
    <td align="center">
      <img src="docs/images/companion-equip.gif" alt="Companion equip screen" width="360"><br>
      <sub>Companion equip screen</sub>
    </td>

  </tr>
  <tr>
    <td align="center">
      <img src="docs/images/party-member-loot.gif" alt="Party member loot" width="360"><br>
      <sub>Party member loot</sub>
    </td>
    <td align="center">
      <img src="docs/images/help-menu.gif" alt="Integrated HELP menu" width="360"><br>
      <sub>Integrated HELP menu</sub>
    </td>
  </tr>
</table>

There are also dozens of small things that just work a little better than they did in the original.  Better pathfinding, fewer graphics glitches, less finicky weapon stacking, and much more.

## Configuration

The main configuration file is `fallout2.cfg`. This contains original game config as well as new CE settings.

Additional settings for screen resolution, UI customization, and map options are now integrated into the main `fallout2.cfg` file (previously part of `f2_res.ini` from Mash's HRP).

Here are some important settings in `fallout2.cfg` under the `[screen]` and `[ui]` sections.  See [the example config](https://github.com/fallout2-ce/fallout2-ce/tree/refs/heads/main/files/fallout2.cfg) for a full list of settings.

```ini
[screen]
resolution_x=1600 ; actual game window size (screen resolution), in pixels
resolution_y=1080
windowed=1 ; 0 = fullscreen
scale=2 ; 1 = original scale, 2 = 2x scale, etc. (e.g. at scale 2 and screen resolution 1920x1080, in-game resolution will be 960x540, thus every pixel is twice as wide and tall)

[ui]
; Maximum number of columns shown in the main inventory and loot/steal windows (valid range: 1..2)
inventory_columns=2

; Set to 1 to expand the barter/trade window vertically, adding a 4th item slot per side (requires ce.dat)
expand_barter_window=1

[qol]
; Allow opening party member inventory from the long-press context menu.
party_trade_from_menu=1

; Allow switching to companions' inventories in loot screens (and barter, in the future)
party_loot_and_barter=1
```

**Recommendations:**
- **Desktops**: Use any size you see fit.
- **Tablets**: Set these values to logical resolution of your device, for example iPad Pro 11 is 1668x2388 (pixels), but its logical resolution is 834x1194 (points).
- **Mobile phones**: Set height to 480, calculate width according to your device screen (aspect) ratio, for example Samsung S21 is 20:9 device, so the width should be 480 * 20 / 9 = 1067.

In time this stuff will receive an in-game interface, right now you have to do it manually. To see all currently working fallout2.cfg settings, just run the game once and quit. It will be automatically updated with defaults for every supported setting.
*Note*: Use of the IFACE_BAR settings requires the `f2_res.dat` file, which contains graphical assets. Various versions are available, but one compatible with the above settings can be found here: [f2_res.dat](https://github.com/fallout2-ce/fallout2-ce/raw/refs/heads/main/files/f2_res.dat)

Note: There are a few settings that are still configured in `ddraw.ini`.  They will be migrated to cfg over time.

## Platform-specific installation

### Windows

[Download](https://github.com/fallout2-ce/fallout2-ce/releases) and unzip into your `Fallout2` folder. Launch the game using `fallout2-ce.exe`.

### Linux

- Use Windows installation as a base - it contains data assets needed to play. Copy `Fallout2` folder somewhere, for example `/home/john/Desktop/Fallout2`.

- Alternatively you can extract the needed files from the GoG installer:

```console
$ sudo apt install innoextract
$ innoextract ~/Downloads/setup_fallout_2_1.02_gog_v1_\(77792\).exe -d Fallout2
```

- Download the Linux release archive, extract `fallout2-ce` and `ce.dat`, and copy them into this folder.

- Run `./fallout2-ce`.

### macOS

> **NOTE**: macOS 10.11 (El Capitan) or higher is required. Runs natively on Intel-based Macs and Apple Silicon.

- Use Windows installation as a base - it contains data assets needed to play. Copy `Fallout2` folder somewhere, for example `/Applications/Fallout2`.

- Alternatively you can use Fallout 2 from Macplay/The Omni Group as a base - you need to extract game assets from the original bundle. Mount CD/DMG, right click `Fallout 2` -> `Show Package Contents`, navigate to `Contents/Resources`. Copy `GameData` folder somewhere, for example `/Applications/Fallout2`.

- Or if you're a Terminal user and have Homebrew installed you can extract the needed files from the GoG installer.  **Note**: You must use "Offline backup game installer", not the main game installer.

```console
$ brew install innoextract
$ innoextract ~/Downloads/setup_fallout_2_1.02_gog_v1_\(77792\).exe -d fallout2
$ mv fallout2 /Applications/Fallout2
```

- [Download](https://github.com/fallout2-ce/fallout2-ce/releases) and copy `Fallout II Community Edition.app` to this folder.

- Run `Fallout II Community Edition.app`.

### Android

> **NOTE**: Fallout 2 was designed with mouse in mind. There are many controls that require precise cursor positioning, which is not possible with fingers. Current control scheme resembles trackpad usage:
- One finger moves mouse cursor around.
- Tap one finger for left mouse click.
- Tap two fingers for right mouse click (switches mouse cursor mode).
- Move two fingers to scroll current view (map view, worldmap view, inventory scrollers).

> **NOTE**: From Android standpoint release and debug builds are different apps. Both apps require their own copy of game assets and have their own savegames. This is intentional. As a gamer just stick with release version and check for updates.

- Download the Android [release](https://github.com/fallout2-ce/fallout2-ce/releases), which contains an .apk file and `ce.dat`.

- Use Windows installation as a base - it contains data assets needed to play. Copy `Fallout2` folder to your device, for example to `Downloads`. You need `master.dat`, `critter.dat`, `patch000.dat`, and `data` folder. Watch for file names - keep (or make) them lowercased (see [Configuration](#configuration)).  Copy `ce.dat` into this folder.

- Copy `fallout2-ce.apk` from the release to your device. Open it with file explorer, follow instructions (install from unknown source).

- When you run the game for the first time it will immediately present file picker. Select the folder from the first step. Wait until this data is copied. A loading dialog will appear, just wait for about 30 seconds. If you're installing total conversion mod or localized version with a large number of unpacked resources in `data` folder it can take up to 20 minutes. Once copied, the game will start automatically.

### iOS

> **NOTE**: See Android note on controls.

- Download `fallout2-ce.ipa`. Use sideloading applications ([AltStore](https://altstore.io/) or [Sideloadly](https://sideloadly.io/)) to install it to your device. Alternatively you can always build from source with your own signing certificate.

- Run the game once. You'll see error message saying "Couldn't find/load text fonts". This step is needed for iOS to expose the game via File Sharing feature.

- Use Finder (macOS Catalina and later) or iTunes (Windows and macOS Mojave or earlier) to copy `master.dat`, `critter.dat`, `patch000.dat`, `ce.dat`, and `data` folder to "Fallout 2" app ([how-to](https://support.apple.com/HT210598)). Watch for file names - keep (or make) them lowercased (see [Configuration](#configuration)).

-

**Controls on iPad:**

- One-finger tap → left click; hold → left button held.
- Two-finger tap → right click (cycles cursor mode between walk and attack).
- One-finger drag → scroll map / windows.
- Two-finger drag → mouse wheel.
- **Three-finger swipe down → ESC** (opens options / main menu).
- **Four-finger long press → F6** (quicksave). Hold four fingers on the screen for about half a second.
- **Three-finger long press → hold Shift** (highlights interactable objects — containers, doors, items — while held).
- Taps on the HUD (interface bar / end-turn / end-combat buttons) act as direct touches so small buttons are easy to hit without aiming.
- A Bluetooth keyboard works for everything else (numbered dialog choices, load game, etc.).

### Browser

> **NOTE**: WebAssembly build with emscripten
```
docker run --rm -v $(pwd):/src emscripten/emsdk:3.1.74 sh -c 'git config --global --add safe.directory "*" && mkdir -p build && cd build && emcmake cmake ../ -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain/Emscripten.cmake && emmake make'
```
- Demo available at https://github.com/ololoken/fallout2-ce-ems.git


## Contributing

For build instructions and contributor notes, see [CONTRIBUTING.md](CONTRIBUTING.md).

Integrating Sfall goodies is the top priority. Quality of life updates are OK too.  In any case open up an issue with your suggestion or to notify other people that something is being worked on.

For current sfall compatibility status and the remaining work needed to close gaps, see [SFALL_COMPATIBILITY.md](SFALL_COMPATIBILITY.md).

## For modders

* Robust (though not 100%) Sfall opcode and hook support
* .ogg/.wav support for map music
* .png support for static assets (must be 8bit indexed)
* .zip support for .dat archives
* Working BIS mapper

## License

The source code is this repository is available under the [Sustainable Use License](LICENSE.md).
