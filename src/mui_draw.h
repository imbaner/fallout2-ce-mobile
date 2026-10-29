#ifndef FALLOUT_MUI_DRAW_H_
#define FALLOUT_MUI_DRAW_H_

#include <string>
#include <vector>

#include <SDL.h>

namespace fallout {

class FrmId;

// Drawing primitives of the mobile UI (see mui.h). Everything is in renderer
// output pixels (physical screen pixels), not in game logical pixels.

struct MuiRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    MuiRect inset(float d) const { return { x + d, y + d, w - d * 2.0f, h - d * 2.0f }; }
    MuiRect inset(float dx, float dy) const { return { x + dx, y + dy, w - dx * 2.0f, h - dy * 2.0f }; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float centerX() const { return x + w / 2.0f; }
    float centerY() const { return y + h / 2.0f; }
};

struct MuiColor {
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 255;

    MuiColor withAlpha(Uint8 alpha) const { return { r, g, b, alpha }; }
};

constexpr MuiColor muiRgb(unsigned int rgb, Uint8 alpha = 255)
{
    return { static_cast<Uint8>((rgb >> 16) & 0xFF), static_cast<Uint8>((rgb >> 8) & 0xFF), static_cast<Uint8>(rgb & 0xFF), alpha };
}

enum class MuiAlign {
    Start,
    Center,
    End,
};

void muiDrawBegin(SDL_Renderer* renderer);
void muiDrawEnd();
// Frees textures (renderer is going away).
void muiDrawReset();

// Changes when textures are dropped (renderer reset): textures made
// outside of the caches here must be made again.
unsigned int muiDrawGeneration();

// Blend mode for textures drawn into with alpha over transparent black
// (premultiplied colors).
SDL_BlendMode muiPremultipliedBlendMode();

// Renderer of the frame being drawn (between `muiDrawBegin` and
// `muiDrawEnd`), nullptr otherwise.
SDL_Renderer* muiDrawGetRenderer();

void muiFillRect(const MuiRect& rect, MuiColor color);
void muiFillRoundRect(const MuiRect& rect, float radius, MuiColor color);
void muiStrokeRoundRect(const MuiRect& rect, float radius, float thickness, MuiColor color);
// [rect] shading from [left] at its left edge to [right] at its right one.
void muiFillHorizontalGradient(const MuiRect& rect, MuiColor left, MuiColor right);
// [rect] shading between the colors of its corners.
void muiFillGradient(const MuiRect& rect, MuiColor topLeft, MuiColor topRight, MuiColor bottomRight, MuiColor bottomLeft);
void muiFillCircle(float centerX, float centerY, float radius, MuiColor color);
void muiStrokeCircle(float centerX, float centerY, float radius, float thickness, MuiColor color);
void muiDrawLine(float x1, float y1, float x2, float y2, float thickness, MuiColor color);
void muiFillConvex(const std::vector<SDL_FPoint>& points, MuiColor color);
// Connected segments with round joins and ends.
void muiDrawPolyline(const std::vector<SDL_FPoint>& points, float thickness, MuiColor color, bool closed = false);

// Antialiased coverage (0-255) of [contours] in a [width] x [height] bitmap,
// pixel coordinates: each contour is closed; the ones going round the same
// way add up where they overlap, opposite ones cut holes.
std::vector<unsigned char> muiRasterizeContours(const std::vector<std::vector<SDL_FPoint>>& contours, int width, int height);

void muiPushClip(const MuiRect& rect);
void muiPopClip();

// Text. Font size is in output pixels.
std::u32string muiDecodeGameText(const char* text);
std::u32string muiDecodeUtf8(const char* text);
// Text in the game's charset (characters it hasn't are left out).
std::string muiEncodeGameText(const std::u32string& text);
float muiTextWidth(const std::u32string& text, float size);
float muiLineHeight(float size);
void muiDrawText(const std::u32string& text, float x, float y, float size, MuiColor color);
void muiDrawTextAligned(const std::u32string& text, const MuiRect& rect, float size, MuiColor color, MuiAlign horizontal, MuiAlign vertical);
std::vector<std::u32string> muiWrapText(const std::u32string& text, float width, float size);
// Largest size from [size] down to [minSize] the text fits [width] with.
float muiFitTextSize(const std::u32string& text, float width, float size, float minSize);

// Game art frame as texture in the current game palette (color 0 is
// transparent), scaled with nearest filtering to keep pixel art crisp.
SDL_Texture* muiArtTexture(const FrmId& frmId, int frame, int* width, int* height, int rotation = 0);
// Painted picture (portraits, menu art) as `muiArtTexture` (first frame),
// smooth scaling: it's shown much larger than drawn.
SDL_Texture* muiPictureTexture(const FrmId& frmId, int* width, int* height);
// Line drawing (skilldex pictures: dark ink on paper) as a white texture of
// the ink only, drawn tinted with `muiDrawTexture`; smooth scaling.
SDL_Texture* muiLineArtTexture(const FrmId& frmId, int* width, int* height);
void muiDrawTexture(SDL_Texture* texture, const MuiRect& rect, MuiColor tint = { 255, 255, 255, 255 });
// Draws the part [source] (texture pixels) of the texture over [rect].
void muiDrawTexturePart(SDL_Texture* texture, const SDL_Rect& source, const MuiRect& rect, MuiColor tint = { 255, 255, 255, 255 });
// Fills [rect] without distortion, cropping equally from the source edges.
void muiDrawTextureCover(SDL_Texture* texture, const MuiRect& rect, MuiColor tint = { 255, 255, 255, 255 });
// 8-bit image in the current game palette (e.g. composed by game code),
// cached under [key], re-uploaded when [version] or the palette changes.
SDL_Texture* muiIndexedTexture(const std::string& key, const unsigned char* data, int width, int height, unsigned int version);
// RGBA image (bytes R, G, B, A) cached under [key], re-uploaded when
// [version] changes; crisp pixels.
SDL_Texture* muiRgbaTexture(const std::string& key, const unsigned char* data, int width, int height, unsigned int version);
// Fits texture of [width] x [height] into [rect] keeping aspect ratio.
MuiRect muiFitRect(const MuiRect& rect, float width, float height);

} // namespace fallout

#endif /* FALLOUT_MUI_DRAW_H_ */
