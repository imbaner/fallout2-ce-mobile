#include "mui_update.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "app_update.h"
#include "game.h"
#include "mui.h"
#include "mui_screens.h"

namespace fallout {

namespace {

    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    std::u32string bytes(long long value)
    {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.1f", std::max(value, 0LL) / (1024.0 * 1024.0));
        return muiDecodeUtf8(buffer) + U" " + muiDecodeGameText(muiText(436, "MB"));
    }

    std::u32string updateError()
    {
        switch (appUpdateError()) {
        case AppUpdateError::Service:
            return text(416, "GitHub is temporarily unavailable. Try again later.");
        case AppUpdateError::Download:
            return text(417, "Download interrupted. Try again.");
        case AppUpdateError::InvalidApk:
            return text(418, "The update file is damaged or signed with a different key.");
        case AppUpdateError::Install:
            return text(419, "Could not install the update. You can try again.");
        default:
            return text(415, "Could not check for updates. Try again.");
        }
    }

    // Release notes are UTF-8 from GitHub. Keep paragraphs and list items,
    // remove simple Markdown decoration (no browser or remote content).
    std::vector<std::u32string> paragraphs(const std::string& notes)
    {
        std::vector<std::u32string> result;
        std::istringstream input(notes);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t start = line.find_first_not_of(" #\t");
            line = start == std::string::npos ? "" : line.substr(start);
            bool bullet = line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0;
            if (bullet) line.erase(0, 2);
            for (size_t at = 0; (at = line.find("**", at)) != std::string::npos;)
                line.erase(at, 2);
            line.erase(std::remove(line.begin(), line.end(), '`'), line.end());
            result.push_back((bullet ? U"• " : U"") + muiDecodeUtf8(line.c_str()));
        }
        if (result.empty()) result.push_back(text(434, "No release notes."));
        return result;
    }

    class UpdateScreen : public MuiScreen {
    public:
        void build(MuiContext& ui) override;
        void back() override
        {
            automatic = false;
            resumeInstall = false;
            appUpdateSetActive(false);
            finished = true;
        }

    private:
        bool automatic = false;
        bool installRequested = false;
        bool resumeInstall = false;
        std::string tag;
        std::vector<std::u32string> body;
        std::vector<std::u32string> lines;
        float notesWidth = -1.0f;
        float notesSize = -1.0f;

        void install()
        {
            automatic = resumeInstall = false;
            installRequested = true;
            appUpdateInstall();
        }
    };

    void UpdateScreen::build(MuiContext& ui)
    {
        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE || gGameLoaded) back();
        AppUpdateState state = appUpdateState();
        if (!finished) {
            if (state == AppUpdateState::Permission && installRequested) resumeInstall = true;
            if (state == AppUpdateState::Installing) installRequested = false;
            if (state == AppUpdateState::Downloaded && (automatic || resumeInstall)) install();
        }

        std::string current = appUpdateTag();
        if (tag != current) {
            tag = current;
            body = paragraphs(appUpdateNotes());
            notesWidth = -1.0f;
            ui.setScroll("update.notes", 0.0f);
        }
        const MuiTheme& theme = muiTheme();
        ui.dim();
        MuiRect safe;
        MuiRect rail;
        muiDialogLayout(ui, &safe, &rail);
        float width = std::min(safe.w, ui.dp(520.0f));
        float height = std::min(safe.h, ui.dp(360.0f));
        MuiRect card = { safe.centerX() - width / 2.0f, safe.centerY() - height / 2.0f, width, height };
        if (muiGameScreenTabs(ui, rail, "update", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back || ui.tappedOutside(card)) back();
        ui.panel(card);
        MuiRect inner = card.inset(ui.dp(16.0f));
        std::string version = tag;
        if (!version.empty() && version.front() == 'v') version.erase(0, 1);
        std::u32string title = text(430, "Update") + U" " + muiDecodeUtf8(version.c_str());
        MuiRect heading = { inner.x, inner.y, inner.w, ui.dp(36.0f) };
        muiDrawTextAligned(title, heading, muiFitTextSize(title, heading.w, ui.dp(18.0f), ui.dp(12.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);

        float line = muiLineHeight(ui.dp(12.0f));
        float buttonHeight = ui.dp(42.0f);
        MuiRect action = { inner.x, inner.bottom() - buttonHeight, inner.w, buttonHeight };
        bool ready = appUpdateReady();
        bool installNote = state == AppUpdateState::Available || state == AppUpdateState::Downloaded;
        float statusHeight = state == AppUpdateState::Error || state == AppUpdateState::Permission || installNote ? line * 3.0f : line * 2.0f;
        MuiRect status = { inner.x, action.y - statusHeight - ui.dp(8.0f), inner.w, statusHeight };
        MuiRect notes = { inner.x, heading.bottom() + ui.dp(8.0f), inner.w, std::max(0.0f, status.y - heading.bottom() - ui.dp(16.0f)) };

        float size = ui.dp(14.0f);
        float row = muiLineHeight(size);
        if (notesWidth != notes.w || notesSize != size) {
            notesWidth = notes.w;
            notesSize = size;
            lines.clear();
            for (const std::u32string& paragraph : body) {
                auto wrapped = muiWrapText(paragraph, notes.w - ui.dp(8.0f), size);
                if (wrapped.empty()) wrapped.push_back(U"");
                lines.insert(lines.end(), wrapped.begin(), wrapped.end());
            }
        }
        float offset = ui.scroll("update.notes", notes, lines.size() * row);
        muiPushClip(notes);
        float y = notes.y - offset;
        for (const auto& value : lines) {
            if (y + row >= notes.y && y <= notes.bottom()) muiDrawTextAligned(value, { notes.x, y, notes.w - ui.dp(8.0f), row }, size, theme.text, MuiAlign::Start, MuiAlign::Center);
            y += row;
        }
        muiPopClip();

        long long done;
        long long total;
        appUpdateProgress(&done, &total);
        std::u32string message;
        if (state == AppUpdateState::Error)
            message = updateError();
        else if (state == AppUpdateState::Permission)
            message = text(420, "Allow updates to be installed from this app.");
        else if (state == AppUpdateState::Downloading)
            message = bytes(done) + U" / " + bytes(total);
        else if (state == AppUpdateState::Installing)
            message = text(410, "Preparing installation…");
        else {
            message = text(422, "Size:") + U" " + bytes(total);
        }
        float messageY = status.y;
        for (const auto& value : muiWrapText(message, status.w, ui.dp(12.0f))) {
            muiDrawTextAligned(value, { status.x, messageY, status.w, line }, ui.dp(12.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            messageY += line;
        }
        if (installNote) {
            float noteLine = muiLineHeight(ui.dp(10.0f));
            for (const auto& value : muiWrapText(text(423, "The game will close to install. Saves and settings stay."), status.w, ui.dp(10.0f))) {
                muiDrawTextAligned(value, { status.x, messageY, status.w, noteLine }, ui.dp(10.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
                messageY += noteLine;
            }
        }
        if (state == AppUpdateState::Downloading) {
            MuiRect track = { status.x, status.bottom() - ui.dp(4.0f), status.w, ui.dp(3.0f) };
            muiFillRoundRect(track, ui.dp(1.5f), theme.button);
            float progress = total > 0 ? std::clamp(static_cast<float>(done) / total, 0.0f, 1.0f) : 0.0f;
            muiFillRoundRect({ track.x, track.y, track.w * progress, track.h }, ui.dp(1.5f), theme.accent);
            ui.mark("update.progress", track);
        }

        if (state == AppUpdateState::Downloading) {
            if (ui.button("update.cancel", action, text(429, "Cancel"))) {
                automatic = false;
                appUpdateCancel();
            }
        } else if (state != AppUpdateState::Installing && state != AppUpdateState::Checking && !finished) {
            const char* id = ready ? "update.install" : "update.download";
            std::u32string label = state == AppUpdateState::Permission ? text(421, "Open settings")
                : state == AppUpdateState::Error                       ? text(427, "Retry")
                : ready                                                ? text(408, "Install")
                                                                       : text(407, "Update");
            if (ui.button(id, action, label, MuiButtonStyle::Primary)) {
                if (state == AppUpdateState::Error && (appUpdateError() == AppUpdateError::Check || appUpdateError() == AppUpdateError::Service)) {
                    automatic = false;
                    appUpdateCheck(true);
                } else if (ready)
                    install();
                else if (!tag.empty()) {
                    automatic = true;
                    appUpdateDownload();
                } else
                    appUpdateCheck(true);
            }
        }
    }

} // namespace

std::u32string muiUpdateStatus()
{
    switch (appUpdateState()) {
    case AppUpdateState::Checking:
        return text(412, "Checking…");
    case AppUpdateState::UpToDate:
        return text(413, "The latest version is installed");
    case AppUpdateState::NoAsset:
        return text(439, "No update is available for this device");
    case AppUpdateState::Downloading:
        return text(409, "Downloading update…");
    case AppUpdateState::Installing:
        return text(410, "Preparing installation…");
    case AppUpdateState::Error:
        return updateError();
    default:
        if (appUpdateReady()) return text(406, "Update ready");
        if (appUpdateHasRelease()) return text(405, "Update available");
        return text(414, "Check for a newer version");
    }
}

void muiUpdateShow()
{
    if (gGameLoaded || !appUpdateHasRelease()) return;
    UpdateScreen screen;
    appUpdateSetActive(true);
    muiRunModal(&screen);
    appUpdateSetActive(false);
}

} // namespace fallout
