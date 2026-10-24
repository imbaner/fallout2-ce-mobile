#include "mui_update.h"

#include <algorithm>
#include <sstream>

#include "db.h"
#include "game.h"
#include "mui.h"
#include "mui_screens.h"

namespace fallout {

namespace {

    // Full notices bundled with the app, available offline.
    const struct {
        const char* title;
        const char* path;
    } kLicenses[] = {
        { "Fallout 2 Community Edition - Sustainable Use License 1.0", "licenses\\fallout2-ce.txt" },
        { "SDL 2 - zlib License", "licenses\\sdl2.txt" },
        { "zlib - zlib License", "licenses\\zlib.txt" },
        { "libarchive - BSD License", "licenses\\libarchive.txt" },
        { "XZ Utils (liblzma) - BSD Zero Clause License", "licenses\\xz.txt" },
        { "LodePNG - zlib License", "licenses\\lodepng.txt" },
        { "stb_truetype, stb_vorbis - MIT / Public Domain", "licenses\\stb.txt" },
        { "fpattern", "licenses\\fpattern.txt" },
        { "PT Mono - SIL Open Font License 1.1", "fonts\\OFL.txt" },
    };

    class LicensesScreen : public MuiScreen {
    public:
        LicensesScreen()
        {
            for (const auto& license : kLicenses) {
                source.push_back({ muiDecodeUtf8(license.title), true });
                File* stream = fileOpen(license.path, "rb");
                if (stream != nullptr) {
                    std::string contents(std::max(fileGetSize(stream), 0), '\0');
                    if (!contents.empty()) contents.resize(fileRead(&contents[0], 1, contents.size(), stream));
                    fileClose(stream);
                    std::istringstream input(contents);
                    std::string line;
                    while (std::getline(input, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        source.push_back({ muiDecodeUtf8(line.c_str()), false });
                    }
                }
                source.push_back({ U"", false });
            }
        }

        void back() override { finished = true; }
        void build(MuiContext& ui) override
        {
            if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) back();
            const auto& theme = muiTheme();
            muiFillRect(ui.screenRect(), muiRgb(0x060A07));
            MuiRect inner;
            MuiRect rail;
            muiDialogLayout(ui, &inner, &rail);
            if (muiGameScreenTabs(ui, rail, "licenses", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back) back();
            float header = ui.dp(36.0f);
            muiDrawTextAligned(muiDecodeGameText(muiText(372, "Licenses")), { inner.x, inner.y, inner.w, header }, ui.dp(18.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
            MuiRect body = { inner.x, inner.y + header + ui.dp(8.0f), inner.w, inner.h - header - ui.dp(8.0f) };
            float size = ui.dp(12.0f);
            float lineHeight = muiLineHeight(size);
            if (wrappedWidth != body.w || wrappedSize != size) {
                wrappedWidth = body.w;
                wrappedSize = size;
                lines.clear();
                for (const Line& line : source) {
                    auto values = muiWrapText(line.text, body.w - ui.dp(8.0f), size);
                    if (values.empty()) values.push_back(U"");
                    for (const auto& value : values)
                        lines.push_back({ value, line.heading });
                }
            }
            float offset = ui.scroll("licenses.text", body, lines.size() * lineHeight);
            muiPushClip(body);
            float y = body.y - offset;
            for (const Line& line : lines) {
                if (y + lineHeight >= body.y && y <= body.bottom()) muiDrawTextAligned(line.text, { body.x, y, body.w, lineHeight }, size, line.heading ? theme.accent : theme.textDim, MuiAlign::Start, MuiAlign::Center);
                y += lineHeight;
            }
            muiPopClip();
        }

    private:
        struct Line {
            std::u32string text;
            bool heading;
        };
        std::vector<Line> source;
        std::vector<Line> lines;
        float wrappedWidth = -1.0f;
        float wrappedSize = -1.0f;
    };

} // namespace

void muiLicensesShow()
{
    LicensesScreen screen;
    muiRunModal(&screen);
}

} // namespace fallout
