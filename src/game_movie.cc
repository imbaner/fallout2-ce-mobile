#include "game_movie.h"

#include <array>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "color.h"
#include "content_config.h"
#include "cycle.h"
#include "debug.h"
#include "game.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "input.h"
#include "mouse.h"
#include "movie.h"
#include "movie_effect.h"
#include "mui.h"
#include "palette.h"
#include "platform_compat.h"
#include "settings.h"
#include "svga.h"
#include "text_font.h"
#include "touch.h"
#include "window.h"
#include "window_manager.h"

namespace fallout {

static char* gameMovieBuildSubtitlesFilePath(char* movieFilePath);
static bool gameMovieFindFilePath(char* movieFilePath, size_t movieFilePathSize, const char* movieFileName);
static bool gameMovieFindFilePathInDir(char* movieFilePath, size_t movieFilePathSize, const char* dir, const char* movieFileName);
static int gameMoviePlayMobile(GameMovie movie, GameMovieFlags flags);
static void gameMovieInitFileNames();
static bool gameMovieIsValidFileName(const char* fileName);
static void gameMovieLoadConfigFileNames();

// 0x50352A
static const float flt_50352A = 0.032258064f;

// 0x518DA0 movie_list
static const char* movieDefaultFileNames[MOVIE_COUNT] = {
    "iplogo.mve",
    "intro.mve",
    "elder.mve",
    "vsuit.mve",
    "afailed.mve",
    "adestroy.mve",
    "car.mve",
    "cartucci.mve",
    "timeout.mve",
    "tanker.mve",
    "enclave.mve",
    "derrick.mve",
    "artimer1.mve",
    "artimer2.mve",
    "artimer3.mve",
    "artimer4.mve",
    "credits.mve",
};

static std::array<std::string, GAME_MOVIE_MAX_COUNT> movieFileNames;

// 0x518DE4 subtitlePalList
static const char* gMoviePaletteFilePaths[MOVIE_COUNT] = {
    nullptr,
    "art\\cuts\\introsub.pal",
    "art\\cuts\\eldersub.pal",
    nullptr,
    "art\\cuts\\artmrsub.pal",
    nullptr,
    nullptr,
    nullptr,
    "art\\cuts\\artmrsub.pal",
    nullptr,
    nullptr,
    nullptr,
    "art\\cuts\\artmrsub.pal",
    "art\\cuts\\artmrsub.pal",
    "art\\cuts\\artmrsub.pal",
    "art\\cuts\\artmrsub.pal",
    "art\\cuts\\crdtssub.pal",
};

// 0x518E28 gmMovieIsPlaying
static bool gGameMovieIsPlaying = false;

// 0x518E2C gmPaletteWasFaded
static bool gGameMovieFaded = false;

// 0x596C78 gmovie_played_list
static unsigned char gGameMoviesSeen[MOVIE_COUNT];

// 0x596C89 full_path
static char gGameMovieSubtitlesFilePath[COMPAT_MAX_PATH];

// gmovie_init
// 0x44E5C0 gmovie_init
int gameMoviesInit()
{
    int volume = 0;
    if (backgroundSoundIsEnabled()) {
        volume = backgroundSoundGetVolume();
    }

    movieSetVolume(volume);

    movieSetBuildSubtitleFilePathProc(gameMovieBuildSubtitlesFilePath);

    gameMovieInitFileNames();
    gameMovieLoadConfigFileNames();

    memset(gGameMoviesSeen, 0, sizeof(gGameMoviesSeen));

    gGameMovieIsPlaying = false;
    gGameMovieFaded = false;

    return 0;
}

// 0x44E60C gmovie_reset
void gameMoviesReset()
{
    memset(gGameMoviesSeen, 0, sizeof(gGameMoviesSeen));

    gGameMovieIsPlaying = false;
    gGameMovieFaded = false;
}

// 0x44E638 gmovie_load
int gameMoviesLoad(File* stream)
{
    if (fileRead(gGameMoviesSeen, sizeof(*gGameMoviesSeen), MOVIE_COUNT, stream) != MOVIE_COUNT) {
        return -1;
    }

    return 0;
}

// 0x44E664 gmovie_save
int gameMoviesSave(File* stream)
{
    if (fileWrite(gGameMoviesSeen, sizeof(*gGameMoviesSeen), MOVIE_COUNT, stream) != MOVIE_COUNT) {
        return -1;
    }

    return 0;
}

// gmovie_play
// 0x44E690 gmovie_play
int gameMoviePlay(GameMovie movie, GameMovieFlags flags)
{
    if (!gameMovieExtendedIsValid(movie) || movieFileNames[movie].empty()) {
        debugPrint("\ngmovie_play() - Error: Invalid movie %d\n", movie);
        return -1;
    }

    if (muiIsEnabled()) {
        return gameMoviePlayMobile(movie, flags);
    }

    gGameMovieIsPlaying = true;

    const char* movieFileName = movieFileNames[movie].c_str();
    debugPrint("\nPlaying movie: %s\n", movieFileName);

    char movieFilePath[COMPAT_MAX_PATH];
    bool movieFound = false;

    movieFound = gameMovieFindFilePath(movieFilePath, sizeof(movieFilePath), movieFileName);

    if (!movieFound) {
        debugPrint("\ngmovie_play() - Error: Unable to open %s\n", movieFileName);
        gGameMovieIsPlaying = false;
        return -1;
    }

    if ((flags & GameMovieFlags::FadeIn) != GameMovieFlags::None) {
        paletteFadeTo(gPaletteBlack);
        gGameMovieFaded = true;
    }

    int win = windowCreate(0,
        0,
        screenGetWidth(),
        screenGetHeight(),
        COLOR_FIRST,
        WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (win == -1) {
        gGameMovieIsPlaying = false;
        return -1;
    }

    if ((flags & GameMovieFlags::StopMusic) != GameMovieFlags::None) {
        backgroundSoundDelete();
    } else if ((flags & GameMovieFlags::PauseMusic) != GameMovieFlags::None) {
        backgroundSoundPause();
    }

    windowRefresh(win);

    bool subtitlesEnabled = settings.preferences.subtitles;
    MovieFlags movieFlags = MovieFlags::DirectCentered;
    if (subtitlesEnabled) {
        char* subtitlesFilePath = gameMovieBuildSubtitlesFilePath(movieFilePath);

        int subtitlesFileSize;
        if (dbGetFileSize(subtitlesFilePath, &subtitlesFileSize) == 0) {
            movieFlags = MovieFlags::DirectCentered | MovieFlags::Subtitles;
        } else {
            subtitlesEnabled = false;
        }
    }

    movieSetFlags(movieFlags);

    Color oldTextColor;
    int oldFont;
    if (subtitlesEnabled) {
        const char* subtitlesPaletteFilePath;
        if (gameMovieIsValid(movie) && gMoviePaletteFilePaths[movie] != nullptr) {
            subtitlesPaletteFilePath = gMoviePaletteFilePaths[movie];
        } else {
            subtitlesPaletteFilePath = "art\\cuts\\subtitle.pal";
        }

        colorPaletteLoad(subtitlesPaletteFilePath);

        oldTextColor = scriptWindowGetTextColor();
        scriptWindowSetTextColor(1.0, 1.0, 1.0);

        oldFont = fontGetCurrent();
        windowSetFont(101);
    }

    bool cursorWasHidden = cursorIsHidden();
    if (cursorWasHidden) {
        gameMouseSetCursor(MOUSE_CURSOR_NONE);
        mouseShowCursor();
    }

    while (mouseGetEvent() != MouseButtonEvent::None) {
        _mouse_info();
    }

    mouseHideCursor();
    colorCycleDisable();

    movieEffectsLoad(movieFilePath);

    _zero_vid_mem();
    _movieRun(win, movieFilePath);

    MouseButtonEvent pressed = MouseButtonEvent::None;
    MouseButtonEvent buttons;
    do {
        // CE: Don't spin without pause - on Android busy loop starves touch
        // input delivery and triggers "app not responding".
        sharedFpsLimiter.mark();

        if (!_moviePlaying() || _game_user_wants_to_quit || inputGetInput() != -1) {
            break;
        }

        Gesture gesture;
        if (touch_get_gesture(&gesture) && gesture.state == kEnded) {
            break;
        }

        int x;
        int y;
        _mouse_get_raw_state(&x, &y, &buttons);

        pressed |= buttons;

        sharedFpsLimiter.throttle();
        // Exit on mouse only after a click cycle: observe left/right down at
        // least once, then wait until both are released.
    } while (((pressed & MouseButtonEvent::LeftButtonDown) == MouseButtonEvent::None && (pressed & MouseButtonEvent::RightButtonDown) == MouseButtonEvent::None) || (buttons & MouseButtonEvent::LeftButtonDown) != MouseButtonEvent::None || (buttons & MouseButtonEvent::RightButtonDown) != MouseButtonEvent::None);

    _movieStop();
    _moviefx_stop();
    _movieUpdate();
    paletteSetEntries(gPaletteBlack);

    gameMovieMarkSeen(movie);

    colorCycleEnable();

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    if (!cursorWasHidden) {
        mouseShowCursor();
    }

    if (subtitlesEnabled) {
        colorPaletteLoad("color.pal");

        windowSetFont(oldFont);

        float r = (float)((Color2RGB(oldTextColor) & 0x7C00) >> 10) * flt_50352A;
        float g = (float)((Color2RGB(oldTextColor) & 0x3E0) >> 5) * flt_50352A;
        float b = (float)(Color2RGB(oldTextColor) & 0x1F) * flt_50352A;
        scriptWindowSetTextColor(r, g, b);
    }

    windowDestroy(win);

    // CE: Destroying a window redraws only content it was covering (centered
    // 640x480). This leads to everything outside this rect to remain black.
    windowRefreshAll(&_scr_size);

    if ((flags & GameMovieFlags::PauseMusic) != GameMovieFlags::None) {
        backgroundSoundResume();
    }

    if ((flags & GameMovieFlags::FadeOut) != GameMovieFlags::None) {
        if (!subtitlesEnabled) {
            colorPaletteLoad("color.pal");
        }

        paletteFadeTo(_cmap);
        gGameMovieFaded = false;
    }

    gGameMovieIsPlaying = false;
    return 0;
}

// Movie playing in the mobile UI player.
static int gGameMovieMobile = -1;

// The movie starts playing into the player's texture: its file, subtitles
// when on and there are some (the player draws them, no subtitles palette),
// movie effects. Music is the callers' business.
static bool gameMovieBeginMobile(GameMovie movie)
{
    if (gGameMovieMobile != -1 || movie < 0 || movie >= GAME_MOVIE_MAX_COUNT || movieFileNames[movie].empty()) {
        return false;
    }

    char movieFilePath[COMPAT_MAX_PATH];
    if (!gameMovieFindFilePath(movieFilePath, sizeof(movieFilePath), movieFileNames[movie].c_str())) {
        debugPrint("\ngmovie_play() - Error: Unable to open %s\n", movieFileNames[movie].c_str());
        return false;
    }

    bool subtitlesEnabled = settings.preferences.subtitles;
    if (subtitlesEnabled) {
        int subtitlesFileSize;
        if (dbGetFileSize(gameMovieBuildSubtitlesFilePath(movieFilePath), &subtitlesFileSize) != 0) {
            subtitlesEnabled = false;
        }
    }

    colorCycleDisable();
    movieEffectsLoad(movieFilePath);

    if (movieStartMobile(movieFilePath, subtitlesEnabled) != 0) {
        _moviefx_stop();
        colorCycleEnable();
        return false;
    }

    gGameMovieMobile = movie;
    return true;
}

// Stops the movie and marks it seen; the palette stays the movie's.
static void gameMovieEndMobile()
{
    if (gGameMovieMobile == -1) {
        return;
    }

    _movieStop();
    _moviefx_stop();
    _movieUpdate();

    gameMovieMarkSeen(static_cast<GameMovie>(gGameMovieMobile));
    gGameMovieMobile = -1;

    colorCycleEnable();
}

bool gameMovieStartMobile(int movie)
{
    if (!gameMovieBeginMobile(static_cast<GameMovie>(movie))) {
        return false;
    }

    backgroundSoundPause();
    return true;
}

void gameMovieFinishMobile()
{
    if (gGameMovieMobile == -1) {
        return;
    }

    gameMovieEndMobile();

    // The movie set its colors.
    paletteSetEntries(_cmap);
    backgroundSoundResume();
}

// CE: `gameMoviePlay` under the mobile UI: the movie screen of the mobile UI
// plays it (a tap or a key skips it, like the game's), the game's fades and
// music flags as they were.
static int gameMoviePlayMobile(GameMovie movie, GameMovieFlags flags)
{
    // As `gameMoviePlay`: a movie that isn't there fails before the fades
    // and the music's pause (a mod without the game's movies, as Fallout:
    // Resurrection without the Arroyo dreams the timers ask for every game
    // event check: two whole fades over the world map each time).
    char movieFilePath[COMPAT_MAX_PATH];
    if (!gameMovieFindFilePath(movieFilePath, sizeof(movieFilePath), movieFileNames[movie].c_str())) {
        debugPrint("\ngmovie_play() - Error: Unable to open %s\n", movieFileNames[movie].c_str());
        return -1;
    }

    gGameMovieIsPlaying = true;

    if ((flags & GameMovieFlags::FadeIn) != GameMovieFlags::None) {
        paletteFadeTo(gPaletteBlack);
        gGameMovieFaded = true;
    }

    if ((flags & GameMovieFlags::StopMusic) != GameMovieFlags::None) {
        backgroundSoundDelete();
    } else if ((flags & GameMovieFlags::PauseMusic) != GameMovieFlags::None) {
        backgroundSoundPause();
    }

    if (gameMovieBeginMobile(movie)) {
        muiMovieScreenRun();
        gameMovieEndMobile();
    }

    paletteSetEntries(gPaletteBlack);

    // The game's windows under the movie screen are drawn again.
    windowRefreshAll(&_scr_size);

    if ((flags & GameMovieFlags::PauseMusic) != GameMovieFlags::None) {
        backgroundSoundResume();
    }

    if ((flags & GameMovieFlags::FadeOut) != GameMovieFlags::None) {
        paletteFadeTo(_cmap);
        gGameMovieFaded = false;
    }

    gGameMovieIsPlaying = false;
    return 0;
}

const char* gameMovieGetDefaultFileName(GameMovie movie)
{
    if (!gameMovieIsValid(movie)) {
        return nullptr;
    }

    return movieDefaultFileNames[movie];
}

bool gameMovieSetPath(GameMovie movie, const char* fileName)
{
    if (!gameMovieExtendedIsValid(movie) || !gameMovieIsValidFileName(fileName)) {
        return false;
    }

    movieFileNames[movie] = fileName;
    return true;
}

void gameMovieMarkSeen(GameMovie movie)
{
    if (gameMovieIsValid(movie)) {
        gGameMoviesSeen[movie] = 1;
    }
}

// 0x44EAE4 gmPaletteFinish
void gameMovieFadeOut()
{
    if (gGameMovieFaded) {
        paletteFadeTo(_cmap);
        gGameMovieFaded = false;
    }
}

// 0x44EB04 gmovie_has_been_played
bool gameMovieIsSeen(GameMovie movie)
{
    if (!gameMovieIsValid(movie)) {
        return false;
    }

    return gGameMoviesSeen[movie] == 1;
}

// 0x44EB14 gmovieIsPlaying
bool gameMovieIsPlaying()
{
    return gGameMovieIsPlaying;
}

// 0x44EB1C gmovie_subtitle_func
static char* gameMovieBuildSubtitlesFilePath(char* movieFilePath)
{
    char* path = movieFilePath;

    char* separator = strrchr(path, '\\');
    if (separator != nullptr) {
        path = separator + 1;
    }

    snprintf(gGameMovieSubtitlesFilePath, sizeof(gGameMovieSubtitlesFilePath), "text\\%s\\cuts\\%s", settings.system.language.c_str(), path);

    char* pch = strrchr(gGameMovieSubtitlesFilePath, '.');
    if (pch != nullptr) {
        *pch = '\0';
    }

    strcpy(gGameMovieSubtitlesFilePath + strlen(gGameMovieSubtitlesFilePath), ".SVE");

    return gGameMovieSubtitlesFilePath;
}

static bool gameMovieFindFilePath(char* movieFilePath, size_t movieFilePathSize, const char* movieFileName)
{
    assert(movieFilePath != nullptr);
    assert(movieFileName != nullptr);

    const char* language = settings.system.language.c_str();
    if (compat_stricmp(language, ENGLISH) != 0) {
        char localizedDir[COMPAT_MAX_PATH];
        snprintf(localizedDir, sizeof(localizedDir), "art\\%s\\cuts", language);
        if (gameMovieFindFilePathInDir(movieFilePath, movieFilePathSize, localizedDir, movieFileName)) {
            return true;
        }
    }

    return gameMovieFindFilePathInDir(movieFilePath, movieFilePathSize, "art\\cuts", movieFileName);
}

static bool gameMovieFindFilePathInDir(char* movieFilePath, size_t movieFilePathSize, const char* dir, const char* movieFileName)
{
    snprintf(movieFilePath, movieFilePathSize, "%s\\%s", dir, movieFileName);

    int movieFileSize;
    return dbGetFileSize(movieFilePath, &movieFileSize) == 0;
}

static void gameMovieInitFileNames()
{
    for (auto& fileName : movieFileNames) {
        fileName.clear();
    }

    for (int index = MOVIE_FIRST; index < MOVIE_COUNT; index++) {
        movieFileNames[index] = movieDefaultFileNames[index];
    }
}

static bool gameMovieIsValidFileName(const char* fileName)
{
    return fileName != nullptr
        && fileName[0] != '\0'
        && strpbrk(fileName, "\\/:") == nullptr;
}

static void gameMovieLoadConfigFileNames()
{
    char key[16];
    for (GameMovie index = MOVIE_FIRST; index < GAME_MOVIE_MAX_COUNT; index++) {
        snprintf(key, sizeof(key), "movie%d", index + 1);

        char* fileName;
        if (configGetString(&gContentConfig, CONTENT_CONFIG_MOVIES_SECTION, key, &fileName) && fileName[0] != '\0') {
            gameMovieSetPath(index, fileName);
        }
    }
}

} // namespace fallout
