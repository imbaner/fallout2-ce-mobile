#include "mui.h"
#include "mui_screens.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include "platform/web/window.h"
#endif

#include "color.h"
#include "dbox.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "loadsave.h"
#include "map.h"
#include "save_compatibility.h"
#include "svga.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x060A07);
    const MuiColor kPanel = muiRgb(0x0B120D);
    const MuiColor kSelected = muiRgb(0x16301D);
    const MuiColor kDanger = muiRgb(0xFF8E72);
    const MuiColor kDangerFill = muiRgb(0x1B100E);
    const MuiColor kDangerPressed = muiRgb(0x47211D);

    // Texts in `game\ce.msg`.
    constexpr int kTextSaveGame = 215;
    constexpr int kTextLoadGame = 216;
    constexpr int kTextSaveTab = 219;
    constexpr int kTextLoadTab = 220;
    constexpr int kTextNewSave = 221;
    constexpr int kTextDescription = 222;
    constexpr int kTextDone = 223;
    constexpr int kTextQuick = 226;
    constexpr int kTextCurrentGame = 227;
    constexpr int kTextDelete = 234;
    constexpr int kTextDeleteWarning = 236;
    constexpr int kTextDeleteFailedTitle = 237;
    constexpr int kTextDeleteFailed = 238;
    constexpr int kTextNoSaves = 239;
    constexpr int kTextLoading = 240;
    constexpr int kTextPermanent = 241;
    constexpr int kTextNoRoom = 242;
    constexpr int kTextPermanentFailed = 243;
    constexpr int kTextDeleteTitle = 244;
    constexpr int kTextOtherGame = 329;
    constexpr int kTextModUpdated = 330;
    constexpr int kTextModAdded = 331;
    constexpr int kTextModMissing = 332;
    constexpr int kTextCompatibilityUnknown = 333;
    constexpr int kTextLoadAnywayTitle = 334;
    constexpr int kTextLoadAnyway = 335;

    // Texts in LSGAME.MSG.
    constexpr int kMessageCorrupt = 112;
    constexpr int kMessageOldVersion = 113;
    constexpr int kMessageSaveError = 132;

    // Description in SAVE.DAT, bytes without the terminator.
    constexpr size_t kDescriptionLength = 29;

    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    // SAVE.DAT thumbnails are palette indices: the game's palette (the
    // screen's one is black while the title screen fades), every pixel
    // opaque (index 0 too).
    void convertThumbnail(const std::vector<unsigned char>& indexed, std::vector<unsigned char>* rgba)
    {
        rgba->resize(indexed.size() * 4);
        for (size_t index = 0; index < indexed.size(); index++) {
            int color = indexed[index] * 3;
            (*rgba)[index * 4] = _cmap[color] << 2;
            (*rgba)[index * 4 + 1] = _cmap[color + 1] << 2;
            (*rgba)[index * 4 + 2] = _cmap[color + 2] << 2;
            (*rgba)[index * 4 + 3] = 255;
        }
    }

    std::u32string gameDateText(int day, int month, int year, unsigned int gameTime)
    {
        // Game time is in tenths of a second.
        unsigned int minutes = gameTime / 600;
        char date[48];
        snprintf(date, sizeof(date), "%02d.%02d.%04d  %02u:%02u", day, month, year, minutes / 60 % 24, minutes % 60);
        return muiDecodeUtf8(date);
    }

    struct SlotRow {
        int slot;
        MobileSaveSlotInfo info;
        // Against the game's files now (save_compatibility.h).
        SaveCompatibility compatibility = SaveCompatibility::kSame;
        std::vector<SaveCompatibilityChange> changes;
        // The tall layout's thumbnail (SAVE.DAT's), read when first shown.
        std::vector<unsigned char> thumbnail;
        bool thumbnailRead = false;
    };

    // "mods/rpu.dat" -> "rpu.dat".
    std::string archiveName(const std::string& path)
    {
        size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // What differs, as the details show it (game text).
    std::string changeText(const SaveCompatibilityChange& change)
    {
        const char* format;
        switch (change.kind) {
        case SaveCompatibilityChange::Kind::kGameChanged:
            return muiText(kTextOtherGame, "Other main game files");
        case SaveCompatibilityChange::Kind::kModUpdated:
            format = muiText(kTextModUpdated, "Mod updated: %s");
            break;
        case SaveCompatibilityChange::Kind::kModAdded:
            format = muiText(kTextModAdded, "Mod added: %s");
            break;
        default:
            format = muiText(kTextModMissing, "Mod missing: %s");
            break;
        }
        char line[160];
        snprintf(line, sizeof(line), format, archiveName(change.name).c_str());
        return line;
    }

    // Save / load screen: the saves newest first (the game's slots aren't
    // shown, quick saves are marked), the selected one's picture and details
    // at the right, its actions under them. Saving makes a new save (the
    // "New save" row at the top, with a description), existing ones aren't
    // overwritten; quick saves can be copied to keep them. From the main menu
    // it only loads.
    class LoadSaveScreen : public MuiScreen {
    public:
        LoadSaveScreen(bool save, bool mainMenu);

        // From the title menu the window is over its picture.
        bool coversScreen() override { return !muiUsesWindows() || fromMainMenu; }
        bool isGameScreen() override { return true; }
        void build(MuiContext& ui) override;
        void back() override;
        void key(int keyCode) override;

        // Game menu's or the title screen's preview of the current game.
        void captureCurrentGame();
        void refresh();
        // Runs what was tapped (dialog boxes, saving, loading) after the
        // frame.
        void runPending();

        int result = 0;
        bool saving;

    private:
        bool fromMainMenu;

        std::vector<SlotRow> rows;
        // Changes when the rows are read again (their thumbnails' textures).
        unsigned int rowsVersion = 0;
        // Tablet windows: the roomy layout (`muiIsTallContent`).
        bool tall = false;
        int selected = -1;
        // Slot of a new save (the "New save" row), -1 - none left.
        int newSlot = -1;
        bool resetScroll = true;

        std::u32string description;
        std::u32string descriptionBeforeEdit;
        bool editing = false;
        bool loading = false;

        int pendingAction = -1;
        int pendingDelete = -1;
        int pendingPermanent = -1;

        // Selected save's picture: the wide one of saves made with the
        // mobile UI, otherwise the thumbnail; the current game's for a new
        // save.
        struct Picture {
            std::vector<unsigned char> rgba;
            int width = 0;
            int height = 0;
            bool valid = false;
        };
        Picture picture;
        Picture currentGame;
        unsigned int pictureVersion = 0;

        const MobileSaveSlotInfo* infoFor(int slot) const;
        const SlotRow* rowFor(int slot) const;
        void select(int slot);
        std::u32string placeOf(const MobileSaveSlotInfo& info) const;
        std::u32string titleOf(const MobileSaveSlotInfo& info) const;

        void buildTabs(MuiContext& ui, const MuiRect& list);
        void buildList(MuiContext& ui, const MuiRect& list);
        void buildTallRow(MuiContext& ui, SlotRow& row, const MuiRect& rect, bool isSelected);
        void buildDetails(MuiContext& ui, const MuiRect& details);
        void buildDescriptionEditor(MuiContext& ui);
        void buildLoadingPlate(MuiContext& ui);
        void closeEditor(bool keep);

        void saveGame(int slot);
        void loadGame(int slot);
        void deleteSave(int slot);
        void makePermanent(int slot);
    };

    LoadSaveScreen::LoadSaveScreen(bool save, bool mainMenu)
        : saving(save)
        , fromMainMenu(mainMenu)
    {
        modal = true;
    }

    void LoadSaveScreen::back()
    {
        if (loading) {
            return;
        }

        if (editing) {
            closeEditor(false);
        } else {
            finished = true;
        }
    }

    void LoadSaveScreen::key(int keyCode)
    {
        if (loading) {
            return;
        }

        if (editing) {
            if (keyCode == KEY_BACKSPACE || keyCode == KEY_DELETE) {
                if (!description.empty()) {
                    description.pop_back();
                }
            } else if (keyCode == KEY_RETURN) {
                closeEditor(true);
            }
        } else if (keyCode == KEY_RETURN && selected != -1) {
            pendingAction = selected;
        }
    }

    void LoadSaveScreen::captureCurrentGame()
    {
        std::vector<unsigned char> thumbnail(kMobileSavePreviewWidth * kMobileSavePreviewHeight);
        if (lsgMobileCaptureWidePreview(&currentGame.rgba, &currentGame.width, &currentGame.height)) {
            currentGame.valid = true;
        } else if (lsgMobileCapturePreview(thumbnail.data(), thumbnail.size())) {
            convertThumbnail(thumbnail, &currentGame.rgba);
            currentGame.width = kMobileSavePreviewWidth;
            currentGame.height = kMobileSavePreviewHeight;
            currentGame.valid = true;
        }
    }

    void LoadSaveScreen::refresh()
    {
        lsgMobileRefreshSlots();
        newSlot = lsgFindFreeManualSlot();

        rows.clear();
        rowsVersion++;
        for (int slot = 0; slot < lsgGetTotalSlotCount(); slot++) {
            MobileSaveSlotInfo info;
            if (lsgMobileGetSlotInfo(slot, &info) && info.state != MobileSaveSlotState::Empty) {
                SlotRow row { slot, info };
                if (info.state == MobileSaveSlotState::Occupied) {
                    row.compatibility = saveCompatibilityCheck(slot, &row.changes);
                    devAutotestNote("  save slot %d: compatibility %d, %d changes%s%s\n", slot + 1, static_cast<int>(row.compatibility), static_cast<int>(row.changes.size()),
                        row.changes.empty() ? "" : ", first: ", row.changes.empty() ? "" : changeText(row.changes.front()).c_str());
                }
                rows.push_back(row);
            }
        }

        // Newest first; saves of the same time keep the slots' order.
        std::stable_sort(rows.begin(), rows.end(), [](const SlotRow& a, const SlotRow& b) {
            return a.info.order > b.info.order;
        });

        resetScroll = true;
        if (selected == -1 || (selected != newSlot && infoFor(selected) == nullptr)) {
            if (saving && newSlot != -1) {
                selected = newSlot;
            } else {
                selected = rows.empty() ? -1 : rows.front().slot;
            }
        }
        select(selected);
    }

    const MobileSaveSlotInfo* LoadSaveScreen::infoFor(int slot) const
    {
        const SlotRow* row = rowFor(slot);
        return row != nullptr ? &row->info : nullptr;
    }

    const SlotRow* LoadSaveScreen::rowFor(int slot) const
    {
        for (const SlotRow& row : rows) {
            if (row.slot == slot) {
                return &row;
            }
        }
        return nullptr;
    }

    void LoadSaveScreen::select(int slot)
    {
        selected = slot;
        picture.valid = false;
        pictureVersion++;

        if (slot != -1 && slot == newSlot) {
            // A new save: no description, the list shows where it's made.
            description.clear();
            picture = currentGame;
            return;
        }

        const MobileSaveSlotInfo* info = infoFor(slot);
        if (info == nullptr) {
            return;
        }

        description = muiDecodeGameText(info->description);
        if (info->state != MobileSaveSlotState::Occupied) {
            return;
        }

        if (lsgMobileReadWidePreview(slot, &picture.rgba, &picture.width, &picture.height)) {
            picture.valid = true;
            return;
        }

        std::vector<unsigned char> thumbnail(kMobileSavePreviewWidth * kMobileSavePreviewHeight);
        if (lsgMobileReadPreview(slot, thumbnail.data(), thumbnail.size())) {
            convertThumbnail(thumbnail, &picture.rgba);
            picture.width = kMobileSavePreviewWidth;
            picture.height = kMobileSavePreviewHeight;
            picture.valid = true;
        }
    }

    std::u32string LoadSaveScreen::placeOf(const MobileSaveSlotInfo& info) const
    {
        return muiPlaceText(info.map, info.elevation);
    }

    // Description, otherwise where it was made; broken saves as the game's
    // list shows them.
    std::u32string LoadSaveScreen::titleOf(const MobileSaveSlotInfo& info) const
    {
        switch (info.state) {
        case MobileSaveSlotState::Occupied:
            return info.description[0] != '\0' ? muiDecodeGameText(info.description) : placeOf(info);
        case MobileSaveSlotState::OldVersion:
            return muiDecodeGameText(lsgGetMessage(kMessageOldVersion));
        default:
            return muiDecodeGameText(lsgGetMessage(kMessageCorrupt));
        }
    }

    void LoadSaveScreen::closeEditor(bool keep)
    {
        if (!keep) {
            description = descriptionBeforeEdit;
        }
        editing = false;
        endTextInput();
    }

    void LoadSaveScreen::build(MuiContext& ui)
    {
        // The keyboard sheet and the loading plate keep the screen visible
        // under them, not touchable.
        bool interactive = ui.isInteractive;
        if (editing || loading) {
            ui.isInteractive = false;
        }

        // Opaque over the map, or a window over it (the title menu's
        // picture).
        MuiScreenFrame frame = muiScreenFrame(ui, kBackground, fromMainMenu);
        const MuiRect& content = frame.content;

        if (muiGameScreenTabs(ui, frame.tabs, "loadsave", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back || frame.tappedOutside) {
            back();
        }

        // List at the left (under the save / load switch in game), details
        // up to the top at the right.
        tall = muiIsTallContent(ui);
        float gap = ui.dp(tall ? 12.0f : 8.0f);
        float switchHeight = ui.dp(tall ? 48.0f : 38.0f);
        float listWidth = content.w * 0.44f - gap / 2.0f;
        float tabsHeight = fromMainMenu ? 0.0f : switchHeight + ui.dp(tall ? 10.0f : 8.0f);
        MuiRect list = { content.x, content.y + tabsHeight, listWidth, content.h - tabsHeight };
        MuiRect details = { list.right() + gap, content.y, content.right() - list.right() - gap, content.h };

        if (!fromMainMenu) {
            buildTabs(ui, { list.x, content.y, list.w, switchHeight });
        }

        buildList(ui, list);
        buildDetails(ui, details);

        ui.isInteractive = interactive;

        if (editing) {
            buildDescriptionEditor(ui);
        }

        if (loading) {
            buildLoadingPlate(ui);
        }
    }

    void LoadSaveScreen::buildTabs(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        std::u32string saveLabel = text(kTextSaveTab, "Save");
        std::u32string loadLabel = text(kTextLoadTab, "Load");
        // The buttons' own text size and padding.
        MuiTabRow tabRow = muiLayoutTabRow(rect, { saveLabel, loadLabel }, ui.dp(4.0f), ui.dp(theme.padding), ui.dp(theme.buttonTextSize), ui.dp(10.0f));
        const MuiRect& saveTab = tabRow.cells[0];
        const MuiRect& loadTab = tabRow.cells[1];

        if (ui.button("loadsave.save_tab", saveTab, saveLabel, saving ? MuiButtonStyle::Primary : MuiButtonStyle::Normal)) {
            saving = true;
            select(newSlot != -1 ? newSlot : selected);
        }

        if (ui.button("loadsave.load_tab", loadTab, loadLabel, saving ? MuiButtonStyle::Normal : MuiButtonStyle::Primary)) {
            saving = false;
            if (selected == newSlot) {
                select(rows.empty() ? -1 : rows.front().slot);
            }
        }
    }

    void LoadSaveScreen::buildList(MuiContext& ui, const MuiRect& list)
    {
        const MuiTheme& theme = muiTheme();
        muiFillRoundRect(list, ui.dp(7.0f), kPanel);
        muiStrokeRoundRect(list, ui.dp(7.0f), ui.dp(1.0f), theme.panelBorder);

        MuiRect inner = list.inset(ui.dp(tall ? 8.0f : 6.0f));
        // Tall: a thumbnail and three lines.
        float rowHeight = ui.dp(tall ? 84.0f : 50.0f);
        float newRowHeight = tall ? ui.dp(56.0f) : rowHeight;
        bool hasNewRow = saving && newSlot != -1;

        if (!hasNewRow && rows.empty()) {
            muiDrawTextAligned(text(kTextNoSaves, "No saved games yet."), inner, ui.dp(14.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
            return;
        }

        // "New save" stays at the top, the saves scroll under it.
        MuiRect scrollArea = inner;
        if (hasNewRow) {
            scrollArea.y += newRowHeight;
            scrollArea.h -= newRowHeight;
        }

        if (resetScroll) {
            ui.setScroll("loadsave.slots", 0.0f);
            resetScroll = false;
        }
        float offset = ui.scroll("loadsave.slots", scrollArea, rows.size() * rowHeight);

        float numberWidth = ui.dp(31.0f);
        float badgeWidth = ui.dp(43.0f);
        int count = static_cast<int>(rows.size()) + (hasNewRow ? 1 : 0);
        for (int index = 0; index < count; index++) {
            bool isNew = hasNewRow && index == 0;
            int rowIndex = hasNewRow ? index - 1 : index;
            int slot = isNew ? newSlot : rows[rowIndex].slot;

            MuiRect clip = isNew ? inner : scrollArea;
            MuiRect rect = isNew
                ? MuiRect { inner.x, inner.y, inner.w, newRowHeight - ui.dp(3.0f) }
                : MuiRect { inner.x, scrollArea.y + rowIndex * rowHeight - offset, inner.w, rowHeight - ui.dp(3.0f) };
            if (rect.bottom() < clip.y || rect.y > clip.bottom()) {
                continue;
            }

            muiPushClip(clip);

            if (selected == slot) {
                muiFillRoundRect(rect, ui.dp(5.0f), kSelected);
            }

            MuiRect touchRect = rect;
            touchRect.y = std::max(rect.y, clip.y);
            touchRect.h = std::min(rect.bottom(), clip.bottom()) - touchRect.y;
            if (ui.touchable("loadsave.slots." + std::to_string(slot + 1), touchRect)) {
                select(slot);
            }

            float textX = rect.x + numberWidth + ui.dp(13.0f);
            float textWidth = rect.right() - textX - ui.dp(5.0f);

            if (tall && !isNew) {
                buildTallRow(ui, rows[rowIndex], rect, slot == selected);
                muiPopClip();
                continue;
            }

            if (isNew) {
                muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(1.0f), theme.accent);
                std::u32string title = U"+ " + text(kTextNewSave, "New save");
                muiDrawTextAligned(title, { textX, rect.y, textWidth, rect.h }, muiFitTextSize(title, textWidth, ui.dp(15.0f), ui.dp(11.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);
                muiPopClip();
                continue;
            }

            const MobileSaveSlotInfo& info = rows[rowIndex].info;

            // Position in the list, newest first.
            std::string number = std::to_string(rowIndex + 1);
            muiDrawTextAligned(muiDecodeUtf8(number.c_str()), { rect.x + ui.dp(4.0f), rect.y, numberWidth, rect.h }, ui.dp(12.0f), theme.textDim, MuiAlign::End, MuiAlign::Center);

            float badgeRight = rect.right() - ui.dp(2.0f);
            if (lsgMobileIsQuickSlot(slot)) {
                textWidth -= badgeWidth;
                muiDrawTextAligned(text(kTextQuick, "Quick"), { badgeRight - badgeWidth, rect.y, badgeWidth, rect.h }, ui.dp(10.0f), theme.accent, MuiAlign::Center, MuiAlign::Center);
                badgeRight -= badgeWidth;
            }

            // Made with other game files: likely works (?) or not (x).
            SaveCompatibility compatibility = rows[rowIndex].compatibility;
            if (compatibility == SaveCompatibility::kLikely || compatibility == SaveCompatibility::kUnlikely) {
                float markWidth = ui.dp(20.0f);
                textWidth -= markWidth;
                bool unlikely = compatibility == SaveCompatibility::kUnlikely;
                muiDrawTextAligned(unlikely ? U"\u00D7" : U"?", { badgeRight - markWidth, rect.y, markWidth, rect.h }, ui.dp(unlikely ? 18.0f : 15.0f), unlikely ? kDanger : theme.accent, MuiAlign::Center, MuiAlign::Center);
            }

            bool occupied = info.state == MobileSaveSlotState::Occupied;
            std::u32string title = titleOf(info);
            MuiRect titleRect = { textX, rect.y + ui.dp(2.0f), textWidth, ui.dp(23.0f) };
            muiDrawTextAligned(title, titleRect, muiFitTextSize(title, textWidth, ui.dp(15.0f), ui.dp(11.0f)), occupied ? theme.text : kDanger, MuiAlign::Start, MuiAlign::Center);

            // Under the description where it was made; under the place
            // (no description) the game's date.
            if (occupied) {
                std::u32string subtitle = info.description[0] != '\0'
                    ? placeOf(info)
                    : gameDateText(info.gameDay, info.gameMonth, info.gameYear, info.gameTime);
                muiDrawTextAligned(subtitle, { textX, rect.y + ui.dp(25.0f), textWidth, ui.dp(19.0f) }, ui.dp(12.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            }

            muiPopClip();
        }
    }

    // Tall layout's row: the save's thumbnail, its title, where, when in
    // the game; quick saves and saves of other game files marked at the
    // right.
    void LoadSaveScreen::buildTallRow(MuiContext& ui, SlotRow& row, const MuiRect& rect, bool isSelected)
    {
        const MuiTheme& theme = muiTheme();
        const MobileSaveSlotInfo& info = row.info;
        bool occupied = info.state == MobileSaveSlotState::Occupied;

        MuiRect thumbnail = { rect.x + ui.dp(8.0f), rect.centerY() - ui.dp(32.5f), ui.dp(104.0f), ui.dp(65.0f) };
        muiFillRoundRect(thumbnail, ui.dp(4.0f), kBackground);
        if (occupied && !row.thumbnailRead) {
            row.thumbnailRead = true;
            std::vector<unsigned char> indexed(kMobileSavePreviewWidth * kMobileSavePreviewHeight);
            if (lsgMobileReadPreview(row.slot, indexed.data(), indexed.size())) {
                convertThumbnail(indexed, &row.thumbnail);
            }
        }
        if (!row.thumbnail.empty()) {
            SDL_Texture* texture = muiRgbaTexture("loadsave.thumbnail." + std::to_string(row.slot), row.thumbnail.data(), kMobileSavePreviewWidth, kMobileSavePreviewHeight, rowsVersion);
            muiDrawTextureCover(texture, thumbnail.inset(ui.dp(1.0f)));
        }
        muiStrokeRoundRect(thumbnail, ui.dp(4.0f), ui.dp(1.0f), theme.panelBorder);

        float textX = thumbnail.right() + ui.dp(12.0f);
        float right = rect.right() - ui.dp(8.0f);
        if (lsgMobileIsQuickSlot(row.slot)) {
            float badgeWidth = ui.dp(64.0f);
            muiDrawTextAligned(text(kTextQuick, "Quick"), { right - badgeWidth, rect.y, badgeWidth, rect.h }, ui.dp(12.0f), theme.accent, MuiAlign::End, MuiAlign::Center);
            right -= badgeWidth + ui.dp(4.0f);
        }

        // Made with other game files: likely works (?) or not (x).
        if (row.compatibility == SaveCompatibility::kLikely || row.compatibility == SaveCompatibility::kUnlikely) {
            float markWidth = ui.dp(24.0f);
            bool unlikely = row.compatibility == SaveCompatibility::kUnlikely;
            muiDrawTextAligned(unlikely ? U"\u00D7" : U"?", { right - markWidth, rect.y, markWidth, rect.h }, ui.dp(unlikely ? 20.0f : 17.0f), unlikely ? kDanger : theme.accent, MuiAlign::Center, MuiAlign::Center);
            right -= markWidth;
        }

        // Title (description or place), where if it has a description, the
        // game's date.
        float width = right - textX;
        float lineHeight = ui.dp(22.0f);
        std::vector<std::pair<std::u32string, float>> lines;
        std::u32string title = titleOf(info);
        lines.push_back({ title, ui.dp(16.0f) });
        if (occupied) {
            if (info.description[0] != '\0') {
                lines.push_back({ placeOf(info), ui.dp(14.0f) });
            }
            lines.push_back({ gameDateText(info.gameDay, info.gameMonth, info.gameYear, info.gameTime), ui.dp(14.0f) });
        }

        float y = rect.centerY() - lines.size() * lineHeight / 2.0f;
        for (size_t index = 0; index < lines.size(); index++) {
            MuiColor color = index != 0 ? theme.textDim : (!occupied ? kDanger : (isSelected ? theme.accent : theme.text));
            float size = muiFitTextSize(lines[index].first, width, lines[index].second, ui.dp(11.0f));
            muiDrawTextAligned(lines[index].first, { textX, y, width, lineHeight }, size, color, MuiAlign::Start, MuiAlign::Center);
            y += lineHeight;
        }
    }

    void LoadSaveScreen::buildDetails(MuiContext& ui, const MuiRect& details)
    {
        const MuiTheme& theme = muiTheme();
        muiFillRoundRect(details, ui.dp(7.0f), kPanel);
        muiStrokeRoundRect(details, ui.dp(7.0f), ui.dp(1.0f), theme.panelBorder);

        MuiRect inner = details.inset(ui.dp(tall ? 14.0f : 8.0f));

        // Picture: cropped to fill, a small margin inside the frame.
        float lineHeight = ui.dp(tall ? 26.0f : 17.0f);
        const SlotRow* selectedRow = rowFor(selected);
        float pictureHeight = tall ? std::min(inner.h * 0.55f, inner.w * 10.0f / 16.0f) : std::min(inner.h * 0.5f, inner.w * 9.0f / 16.0f);
        MuiRect frame = { inner.x, inner.y, inner.w, pictureHeight };
        muiFillRoundRect(frame, ui.dp(5.0f), kBackground);
        if (picture.valid) {
            SDL_Texture* texture = muiRgbaTexture("loadsave.picture", picture.rgba.data(), picture.width, picture.height, pictureVersion);
            if (texture != nullptr) {
                MuiRect image = frame.inset(ui.dp(2.0f));
                muiPushClip(image);
                muiDrawTextureCover(texture, image);
                muiPopClip();
            }
        }
        muiStrokeRoundRect(frame, ui.dp(5.0f), ui.dp(1.0f), theme.panelBorder);

        // Details: who, where, game time, when it was made.
        const MobileSaveSlotInfo* info = infoFor(selected);
        bool isNew = selected != -1 && selected == newSlot;
        float y = frame.bottom() + ui.dp(tall ? 10.0f : 4.0f);
        auto line = [&](const std::u32string& value, float size, MuiColor color) {
            muiDrawTextAligned(value, { inner.x, y, inner.w, lineHeight }, ui.dp(size), color, MuiAlign::Start, MuiAlign::Center);
            y += lineHeight;
        };

        if (info != nullptr && info->state == MobileSaveSlotState::Occupied) {
            // Who, and in a frame beside if it was made with other game
            // files: the first difference (and how many more), or that it
            // isn't known. Nothing for a save made with this game.
            std::u32string name = muiDecodeGameText(info->characterName);
            float nameSize = ui.dp(tall ? 18.0f : 12.0f);
            muiDrawTextAligned(name, { inner.x, y, inner.w, lineHeight }, nameSize, theme.accent, MuiAlign::Start, MuiAlign::Center);
            std::u32string badge;
            MuiColor badgeColor = theme.textDim;
            if (selectedRow->compatibility == SaveCompatibility::kUnknown) {
                badge = U"\u00B7 " + text(kTextCompatibilityUnknown, "Compatibility unknown");
            } else if (!selectedRow->changes.empty()) {
                bool unlikely = selectedRow->compatibility == SaveCompatibility::kUnlikely;
                badge = (unlikely ? U"\u00D7 " : U"? ") + muiDecodeGameText(changeText(selectedRow->changes.front()).c_str());
                if (selectedRow->changes.size() > 1) {
                    badge += muiDecodeUtf8((" +" + std::to_string(selectedRow->changes.size() - 1)).c_str());
                }
                badgeColor = unlikely ? kDanger : theme.accent;
            }
            if (!badge.empty()) {
                float x = inner.x + muiTextWidth(name, nameSize) + ui.dp(8.0f);
                float room = inner.right() - x;
                float size = muiFitTextSize(badge, room - ui.dp(10.0f), ui.dp(tall ? 13.0f : 10.0f), ui.dp(8.0f));
                float width = std::min(room, muiTextWidth(badge, size) + ui.dp(10.0f));
                MuiRect badgeRect = { x, y + ui.dp(1.0f), width, lineHeight - ui.dp(2.0f) };
                muiStrokeRoundRect(badgeRect, ui.dp(4.0f), ui.dp(1.0f), badgeColor);
                muiPushClip(badgeRect);
                muiDrawTextAligned(badge, badgeRect, size, badgeColor, MuiAlign::Center, MuiAlign::Center);
                muiPopClip();
            }
            y += lineHeight;

            line(placeOf(*info), tall ? 16.0f : 11.0f, theme.text);
            line(gameDateText(info->gameDay, info->gameMonth, info->gameYear, info->gameTime), tall ? 14.0f : 10.0f, theme.textDim);

            std::time_t created = static_cast<std::time_t>(info->created);
            const std::tm* local = created > 0 ? std::localtime(&created) : nullptr;
            if (local != nullptr) {
                char date[48];
                std::strftime(date, sizeof(date), "%d.%m.%Y  %H:%M:%S", local);
                line(muiDecodeUtf8(date), tall ? 14.0f : 10.0f, theme.textDim);
            }
        } else if (isNew && saving) {
            line(text(kTextCurrentGame, "Current game"), tall ? 18.0f : 12.0f, theme.accent);
        } else if (info != nullptr) {
            line(titleOf(*info), tall ? 18.0f : 12.0f, kDanger);
        }

        // Above the buttons: the description of a new save, making a quick
        // save permanent, or why there's nothing to save to.
        float actionHeight = ui.dp(tall ? 52.0f : 39.0f);
        float smallSize = ui.dp(tall ? 15.0f : 12.0f);
        MuiRect action = { inner.x, inner.bottom() - actionHeight, inner.w, actionHeight };
        MuiRect above = tall ? MuiRect { inner.x, action.y - ui.dp(54.0f), inner.w, ui.dp(44.0f) } : MuiRect { inner.x, action.y - ui.dp(41.0f), inner.w, ui.dp(35.0f) };

        if (saving && isNew) {
            muiFillRoundRect(above, ui.dp(5.0f), kBackground);
            muiStrokeRoundRect(above, ui.dp(5.0f), ui.dp(1.0f), theme.buttonBorder);

            // Empty: the place the list will show instead.
            bool empty = description.empty();
            std::u32string value = empty ? muiPlaceText(mapGetCurrentMap(), gElevation) : description;
            muiDrawTextAligned(value, above.inset(ui.dp(8.0f), 0.0f), smallSize, empty ? theme.textDim : theme.accent, MuiAlign::Start, MuiAlign::Center);

            if (ui.touchable("loadsave.description", above)) {
                descriptionBeforeEdit = description;
                editing = true;
                beginTextInput();
            }
        } else if (info != nullptr && info->state == MobileSaveSlotState::Occupied && lsgMobileIsQuickSlot(selected)) {
            if (newSlot != -1) {
                if (ui.button("loadsave.permanent", above, text(kTextPermanent, "Make permanent"))) {
                    pendingPermanent = selected;
                }
            } else {
                muiDrawTextAligned(text(kTextNoRoom, "No room for a new save"), above, smallSize, theme.textDim, MuiAlign::Center, MuiAlign::Center);
            }
        } else if (saving && newSlot == -1) {
            muiDrawTextAligned(text(kTextNoRoom, "No room for a new save"), above, smallSize, theme.textDim, MuiAlign::Center, MuiAlign::Center);
        }

        // Delete at the left of the main action (alone for broken saves).
        bool canAct = saving ? newSlot != -1 : info != nullptr && info->state == MobileSaveSlotState::Occupied;
        if (info != nullptr) {
            float deleteWidth = canAct ? std::min(ui.dp(tall ? 170.0f : 155.0f), action.w * 0.38f) : action.w;
            MuiRect deleteButton = { action.x, action.y, deleteWidth, action.h };
            bool pressed = false;
            if (ui.touchable("loadsave.delete", deleteButton, &pressed)) {
                pendingDelete = selected;
            }
            muiFillRoundRect(deleteButton, ui.dp(6.0f), pressed ? kDangerPressed : kDangerFill);
            muiStrokeRoundRect(deleteButton, ui.dp(6.0f), ui.dp(1.0f), kDanger);
            muiDrawTextAligned(text(kTextDelete, "Delete"), deleteButton, ui.dp(tall ? 16.0f : 13.0f), kDanger, MuiAlign::Center, MuiAlign::Center);

            action.x = deleteButton.right() + ui.dp(tall ? 10.0f : 6.0f);
            action.w = inner.right() - action.x;
        }

        if (canAct) {
            // Saving over an existing save makes a new one.
            std::u32string label;
            if (!saving) {
                label = text(kTextLoadGame, "Load game");
            } else if (isNew) {
                label = text(kTextSaveGame, "Save game");
            } else {
                label = text(kTextNewSave, "New save");
            }

            if (ui.button("loadsave.action", action, label, MuiButtonStyle::Primary)) {
                pendingAction = selected;
            }
        }
    }

    void LoadSaveScreen::buildDescriptionEditor(MuiContext& ui)
    {
        std::u32string typed = ui.takeTextInput();
#ifdef __EMSCRIPTEN__
        if (webTakeTextInput(typed)) description.clear();
#endif
        for (char32_t ch : typed) {
            // Only what the game's text can hold, as long as SAVE.DAT's field.
            if (ch >= 0x20
                && !muiEncodeGameText(std::u32string(1, ch)).empty()
                && muiEncodeGameText(description).size() < kDescriptionLength) {
                description.push_back(ch);
            }
        }

        const MuiTheme& theme = muiTheme();
        ui.dim();

        // At the top, over the keyboard.
        MuiRect safe = ui.safeRect().inset(ui.dp(9.0f));
        float width = std::min(safe.w, ui.dp(540.0f));
        MuiRect card = { safe.centerX() - width / 2.0f, safe.y, width, ui.dp(104.0f) };
        ui.panel(card);

        MuiRect inner = card.inset(ui.dp(10.0f));
        muiDrawTextAligned(text(kTextDescription, "Description"), { inner.x, inner.y, inner.w, ui.dp(22.0f) }, ui.dp(13.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);

        float doneWidth = ui.dp(102.0f);
        MuiRect field = { inner.x, inner.y + ui.dp(29.0f), inner.w - doneWidth - ui.dp(6.0f), ui.dp(44.0f) };
        muiFillRoundRect(field, ui.dp(6.0f), kBackground);
        muiStrokeRoundRect(field, ui.dp(6.0f), ui.dp(1.0f), theme.accent);
        muiDrawTextAligned(description, field.inset(ui.dp(8.0f), 0.0f), ui.dp(17.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
#ifdef __EMSCRIPTEN__
        webTextInputField(description, field.x, field.y, field.w, field.h, ui.dp(17.0f), kDescriptionLength);
        if (webTextInputDone()) {
            closeEditor(true);
            return;
        }
#endif

        // Tapping the field shows the keyboard again.
        if (ui.touchable("loadsave.description.field", field)) {
            beginTextInput();
        }

        if (ui.button("loadsave.description.done", { field.right() + ui.dp(6.0f), field.y, doneWidth, field.h }, text(kTextDone, "Done"), MuiButtonStyle::Primary)) {
            closeEditor(true);
        }
    }

    void LoadSaveScreen::buildLoadingPlate(MuiContext& ui)
    {
        // The game presents frames while it loads: a plate over the screen
        // instead of the game menu under it.
        const MuiTheme& theme = muiTheme();
        ui.dim();

        MuiRect safe = ui.safeRect().inset(ui.dp(12.0f));
        float width = std::min(safe.w, ui.dp(340.0f));
        MuiRect card = { safe.centerX() - width / 2.0f, safe.centerY() - ui.dp(35.0f), width, ui.dp(70.0f) };
        muiFillRoundRect(card, ui.dp(theme.radius), kPanel);
        ui.panel(card);

        MuiRect bar = { card.x + ui.dp(14.0f), card.y + ui.dp(17.0f), ui.dp(3.0f), card.h - ui.dp(34.0f) };
        muiFillRoundRect(bar, ui.dp(1.5f), theme.accent);
        muiDrawTextAligned(text(kTextLoading, "Loading save..."), { bar.right() + ui.dp(12.0f), card.y, card.right() - bar.right() - ui.dp(24.0f), card.h }, ui.dp(15.0f), theme.text, MuiAlign::Start, MuiAlign::Center);
    }

    void LoadSaveScreen::runPending()
    {
        int permanentSlot = pendingPermanent;
        int deleteSlot = pendingDelete;
        int actionSlot = pendingAction;
        pendingPermanent = -1;
        pendingDelete = -1;
        pendingAction = -1;

        if (editing) {
            return;
        }

        if (permanentSlot != -1) {
            makePermanent(permanentSlot);
        } else if (deleteSlot != -1) {
            deleteSave(deleteSlot);
        } else if (actionSlot != -1) {
            if (saving) {
                saveGame(actionSlot);
            } else {
                loadGame(actionSlot);
            }
        }
    }

    void LoadSaveScreen::saveGame(int slot)
    {
        // An existing save selected: the new save row.
        if (slot != newSlot) {
            if (newSlot != -1) {
                select(newSlot);
            }
            return;
        }

        std::string encoded = muiEncodeGameText(description);
        encoded.resize(std::min(encoded.size(), kDescriptionLength));

        // The game's error shows on failure.
        if (lsgMobileSaveGame(slot, encoded.c_str()) == 0) {
            result = 1;
            finished = true;
        } else {
            refresh();
        }
    }

    void LoadSaveScreen::loadGame(int slot)
    {
        const MobileSaveSlotInfo* info = infoFor(slot);
        if (info == nullptr || info->state != MobileSaveSlotState::Occupied) {
            return;
        }

        // Likely not to load right: asked first.
        if (rowFor(slot)->compatibility == SaveCompatibility::kUnlikely) {
            std::string title = muiText(kTextLoadAnywayTitle, "Load this save?");
            const char* body[] = { muiText(kTextLoadAnyway, "It was made with other game files and will likely not load right.") };
            if (showDialogBox(title.c_str(), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) == 0) {
                return;
            }
        }

        loading = true;
        devAutotestCaptureNextFrame("loadsave_loading");
        renderPresent();

        // On failure the game's error shows and the game goes back to the
        // main menu.
        result = lsgMobileLoadGame(slot) == 0 ? 1 : -1;
        loading = false;
        finished = true;
    }

    void LoadSaveScreen::deleteSave(int slot)
    {
        const MobileSaveSlotInfo* info = infoFor(slot);
        if (info == nullptr) {
            return;
        }

        // The warning, then the description (the game's text) if it has one.
        std::string title = muiText(kTextDeleteTitle, "Delete save?");
        const char* body[] = {
            muiText(kTextDeleteWarning, "This save will be deleted permanently."),
            info->description,
        };
        int bodyLength = info->description[0] != '\0' ? 2 : 1;
        if (showDialogBox(title.c_str(), body, bodyLength, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) == 0) {
            return;
        }

        if (!lsgMobileDeleteSlot(slot)) {
            soundPlayFile("iisxxxx1");
            const char* failure[] = { muiText(kTextDeleteFailed, "Could not delete this save.") };
            showDialogBox(muiText(kTextDeleteFailedTitle, "Deletion failed"), failure, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
        }

        selected = -1;
        refresh();
    }

    // The quick save stays where it is in the list, no longer quick (never
    // replaced by a new one).
    void LoadSaveScreen::makePermanent(int slot)
    {
        int permanent = lsgMakeQuickSavePermanent(slot);
        if (permanent != -1) {
            selected = permanent;
        } else {
            soundPlayFile("iisxxxx1");
            std::string title = lsgGetMessage(kMessageSaveError);
            const char* body[] = { muiText(kTextPermanentFailed, "Could not make the save permanent.") };
            showDialogBox(title.c_str(), body, 1, 0, 0, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
        }

        refresh();
    }

} // namespace

int muiLoadSaveScreenRun(bool saving, bool fromMainMenu)
{
    LoadSaveScreen screen(saving, fromMainMenu);
    if (!fromMainMenu) {
        screen.captureCurrentGame();
    }
    screen.refresh();
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        } else if (keyCode != -1) {
            screen.key(keyCode);
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();
        renderPresent();

        screen.runPending();

        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
    endTextInput();

    // Saving leaves the game's busy cursor, which keeps map input (touch
    // commands too) off; the game's window resets it when it closes (loading
    // does it itself, `lsgMobileLoadGame`).
    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    return screen.result;
}

} // namespace fallout
