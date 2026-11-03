#ifndef MOVIE_H
#define MOVIE_H

#include <SDL.h>

#include "geometry.h"

namespace fallout {

enum class MovieFlags : unsigned int {
    None = 0x00,
    Scale = 0x01,
    Direct = 0x02,
    DirectCentered = 0x04,
    Subtitles = 0x08,
};

constexpr inline MovieFlags operator&(MovieFlags lhs, MovieFlags rhs)
{
    return static_cast<MovieFlags>(static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs));
}

constexpr inline MovieFlags operator|(MovieFlags lhs, MovieFlags rhs)
{
    return static_cast<MovieFlags>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

typedef char* MovieBuildSubtitleFilePathProc(char* movieFilePath);
typedef void MovieSetPaletteEntriesProc(unsigned char* palette, int start, int end);
typedef void MovieSetPaletteProc(int frame);
typedef int(MovieBlitFunc)(int win, unsigned char* data, int width, int height, int pitch);

void movieInit();
void movieExit();
void _movieStop();
int movieSetFlags(MovieFlags flags);
void _movieSetPaletteFunc(MovieSetPaletteEntriesProc* proc);
void movieSetPaletteProc(MovieSetPaletteProc* proc);
int _movieRun(int win, char* filePath);
int _movieRunRect(int win, char* filePath, int x, int y, int w, int h);
void movieSetBuildSubtitleFilePathProc(MovieBuildSubtitleFilePathProc* proc);
void movieSetVolume(int volume);
void _movieUpdate();
int _moviePlaying();
void movieHandleRendererReset();
void movieHandleFocusGained();
void movieRenderDirectOverlay();

// CE: Mobile UI movie player: the movie plays without a window, its frames go
// to a texture the player draws, subtitles are given as text; it can be
// paused.
int movieStartMobile(char* filePath, bool subtitles);
bool movieIsMobile();
SDL_Texture* movieGetFrameTexture(int* width, int* height);
// Subtitle shown now (game charset), nullptr when none.
const char* movieGetSubtitle();
void moviePause(bool paused);
bool movieIsPaused();

} // namespace fallout

#endif /* MOVIE_H */
