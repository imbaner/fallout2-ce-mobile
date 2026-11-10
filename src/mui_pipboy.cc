#include "mui.h"
#include "mui_icons.h"
#include "mui_map_view.h"
#include "mui_notify.h"
#include "mui_screens.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "automap.h"
#include "critter.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_movie.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "movie.h"
#include "object.h"
#include "pipboy.h"
#include "scripts.h"
#include "stat.h"
#include "svga.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x060A07);
    const MuiColor kListBackground = muiRgb(0x0B120D);
    const MuiColor kSelected = muiRgb(0x16301D);
    const MuiColor kWarning = muiRgb(0xFF6E5E);

    // Texts in `game\ce.msg`.
    constexpr int kTextQuests = 199;
    constexpr int kTextData = 200;
    constexpr int kTextMaps = 201;
    constexpr int kTextVideos = 202;
    constexpr int kTextRest = 203;
    constexpr int kTextEmpty = 204;
    constexpr int kTextRestFor = 205;
    constexpr int kTextWakeUpAt = 206;
    constexpr int kTextUntilHealed = 207;
    constexpr int kTextMinutes = 208;
    constexpr int kTextHours = 209;
    constexpr int kTextStop = 210;
    constexpr int kTextPlay = 211;

    // Texts in `game\pipboy.msg`.
    constexpr int kPipboyNoQuests = 203;
    constexpr int kPipboyHitPoints = 301;
    constexpr int kPipboyCannotRest = 215;

    // Minutes of the "rest for" options (`PipboyRestDuration`).
    const int kRestMinutes[] = { 10, 30, 60, 120, 180, 240, 300, 360 };

    std::u32string gameText(const char* text)
    {
        return muiDecodeGameText(text);
    }

    std::u32string ceText(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    std::u32string number(int value, int digits = 1)
    {
        std::string text = std::to_string(value);
        while (static_cast<int>(text.size()) < digits) {
            text.insert(text.begin(), '0');
        }
        return std::u32string(text.begin(), text.end());
    }

    // "HH:MM" of game time as the game keeps it (HHMM).
    std::u32string clockText(int hourMinutes)
    {
        return number(hourMinutes / 100, 2) + U":" + number(hourMinutes % 100, 2);
    }

    std::u32string dateText()
    {
        int month;
        int day;
        int year;
        gameTimeGetDate(&month, &day, &year);
        return number(day, 2) + U"." + number(month, 2) + U"." + number(year);
    }

    enum class Section {
        Quests,
        Data,
        Maps,
        Videos,
        Rest,
        Count,
    };

    // What the loop around the screen runs (not while it's drawn).
    enum class Pending {
        None,
        Rest,
        Video,
    };

    // Pip-Boy: quests, holodisks (data), saved automaps, seen movies, rest.
    // Sections at the top, a list at the left, its content at the right, the
    // game screens' tabs at the edge. Works over the Pip-Boy's state and
    // rules (pipboy.h), the game's window isn't created.
    class PipboyScreen : public MuiScreen {
    public:
        Pending pending = Pending::None;
        int pendingValue = 0;
        bool resting = false;
        // Tablet windows: the roomy sizes (`muiIsTallContent`).
        bool tall = false;

        PipboyScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return !muiUsesWindows(); }
        bool isGameScreen() override { return true; }

        // The movie plays over the whole screen.
        bool isActive() override { return !gameMovieIsPlaying(); }

        void reset(bool rest);
        void build(MuiContext& ui) override;
        void back() override;

        void runPending();

        // Stops the movie when it ended by itself.
        void updateMovie();
        void finishMovie();

    private:
        Section section = Section::Quests;
        int quest = 0;
        int holodisk = 0;
        // Holodisk whose narration was started (-1 none).
        int narratedHolodisk = -1;
        int mapLocation = 0;
        int mapEntry = 0;
        int video = 0;
        // Movie playing in the player (-1 none), shown over the whole screen.
        int playingMovie = -1;
        bool fullscreen = false;
        unsigned int controlsTime = 0;
        std::string detailKey;
        MuiMapView mapView;
        std::string mapKey;
        unsigned int now = 0;

        void buildHeader(MuiContext& ui, const MuiRect& rect);
        void buildSections(MuiContext& ui, const MuiRect& rect);
        void buildQuests(MuiContext& ui, const MuiRect& list, const MuiRect& detail);
        void buildData(MuiContext& ui, const MuiRect& list, const MuiRect& detail);
        void buildMaps(MuiContext& ui, const MuiRect& list, const MuiRect& detail);
        void buildVideos(MuiContext& ui, const MuiRect& list, const MuiRect& detail);
        void buildRest(MuiContext& ui, const MuiRect& left, const MuiRect& right);
        void buildFullscreenMovie(MuiContext& ui);
        bool playerButton(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiIcon icon);

        bool row(MuiContext& ui, const std::string& id, const MuiRect& rect, bool selected);
        void listRows(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<std::u32string>& names, const std::vector<std::u32string>& details, int* selected);
        void drawEmpty(MuiContext& ui, const MuiRect& rect, const std::u32string& text);
        void drawText(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& title, const std::vector<std::u32string>& paragraphs);
        bool restButton(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, const std::u32string& detail, bool enabled);
        void resetDetail(MuiContext& ui, const std::string& key);
    };

    PipboyScreen gPipboyScreen;

    void PipboyScreen::reset(bool rest)
    {
        finished = false;
        pending = Pending::None;
        resting = false;
        section = rest ? Section::Rest : Section::Quests;
        quest = 0;
        holodisk = 0;
        narratedHolodisk = -1;
        mapLocation = 0;
        mapEntry = 0;
        video = 0;
        playingMovie = -1;
        fullscreen = false;
        detailKey.clear();
        mapKey.clear();

        // Opened to rest where it isn't allowed: the game's message.
        if (rest && !pipboyCanRest()) {
            soundPlayFile("iisxxxx1");
            muiNotify(pipboyGetText(kPipboyCannotRest), MuiNoticeKind::Warning);
        }
    }

    void PipboyScreen::back()
    {
        // Resting: stops the game's rest (as Esc does).
        if (resting) {
            pipboyStopRest();
            return;
        }

        if (fullscreen) {
            fullscreen = false;
            return;
        }

        finished = true;
    }

    void PipboyScreen::updateMovie()
    {
        if (playingMovie != -1 && !movieIsMobile()) {
            finishMovie();
        }
    }

    void PipboyScreen::finishMovie()
    {
        if (playingMovie == -1) {
            return;
        }

        gameMovieFinishMobile();
        playingMovie = -1;
        fullscreen = false;
    }

    void PipboyScreen::runPending()
    {
        Pending what = pending;
        pending = Pending::None;

        switch (what) {
        case Pending::Rest:
            soundPlayFile("ib1p1xx1");
            resting = true;
            // The game's rest loop runs the clock; this screen shows it.
            if (pipboyRestFor(pendingValue)) {
                finished = true;
            }
            resting = false;
            soundPlayFile("ib2lu1x1");
            break;
        case Pending::Video:
            finishMovie();
            if (gameMovieStartMobile(pendingValue)) {
                playingMovie = pendingValue;
            }
            break;
        case Pending::None:
            break;
        }
    }

    void PipboyScreen::build(MuiContext& ui)
    {
        now = ui.now;

        if (fullscreen && playingMovie != -1) {
            buildFullscreenMovie(ui);
            return;
        }

        // Opaque over the map, or a window over it.
        MuiScreenFrame frame = muiScreenFrame(ui, kBackground);

        // Leaving the holodisks stops the narration.
        if (section != Section::Data && narratedHolodisk != -1) {
            pipboySoundStop();
            narratedHolodisk = -1;
        }

        MuiRect content = frame.content;
        MuiRect tabs = frame.tabs;
        tall = muiIsTallContent(ui);

        // While resting only the stop button takes touches.
        bool interactive = ui.isInteractive;
        ui.isInteractive = interactive && !resting;

        MuiGameScreenTab tab = muiGameScreenTabs(ui, tabs, "pipboy", MuiGameScreenTab::Pipboy, true);
        if (tab == MuiGameScreenTab::Back || (frame.tappedOutside && !resting)) {
            back();
        } else if (tab != MuiGameScreenTab::None && muiSwitchGameScreen(ui, tab)) {
            finished = true;
        }

        float gap = ui.dp(tall ? 10.0f : 6.0f);
        float headerHeight = ui.dp(tall ? 40.0f : 30.0f);
        float sectionsHeight = ui.dp(tall ? 44.0f : 34.0f);
        buildHeader(ui, { content.x, content.y, content.w, headerHeight });
        buildSections(ui, { content.x, content.y + headerHeight + gap, content.w, sectionsHeight });

        float bodyY = content.y + headerHeight + sectionsHeight + gap * 2.0f;
        MuiRect body = { content.x, bodyY, content.w, content.bottom() - bodyY };
        float listWidth = std::round(body.w * (tall ? 0.34f : 0.37f));
        float listGap = ui.dp(tall ? 10.0f : 8.0f);
        MuiRect list = { body.x, body.y, listWidth, body.h };
        MuiRect detail = { list.right() + listGap, body.y, body.right() - list.right() - listGap, body.h };

        switch (section) {
        case Section::Quests:
            buildQuests(ui, list, detail);
            break;
        case Section::Data:
            buildData(ui, list, detail);
            break;
        case Section::Maps:
            buildMaps(ui, list, detail);
            break;
        case Section::Videos:
            buildVideos(ui, list, detail);
            break;
        case Section::Rest:
            ui.isInteractive = interactive;
            buildRest(ui, list, detail);
            break;
        case Section::Count:
            break;
        }

        ui.isInteractive = interactive;
    }

    // Date, time (runs while resting), holiday greeting.
    void PipboyScreen::buildHeader(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(tall ? 16.0f : 14.0f);
        float padding = ui.dp(tall ? 14.0f : 9.0f);
        float x = rect.x;

        std::vector<std::pair<std::u32string, MuiColor>> chips = {
            { dateText(), theme.accent },
            { clockText(gameTimeGetHour()), theme.accent },
        };

        const char* holiday = pipboyGetHoliday();
        if (holiday != nullptr) {
            chips.push_back({ gameText(holiday), theme.textDim });
        }

        for (const auto& chip : chips) {
            float width = muiTextWidth(chip.first, size) + padding * 2.0f;
            MuiRect chipRect = { x, rect.y, width, rect.h };
            muiFillRoundRect(chipRect, ui.dp(6.0f), kListBackground);
            muiStrokeRoundRect(chipRect, ui.dp(6.0f), ui.dp(1.0f), theme.panelBorder);
            muiDrawTextAligned(chip.first, chipRect, size, chip.second, MuiAlign::Center, MuiAlign::Center);
            x += width + ui.dp(tall ? 8.0f : 6.0f);
        }
    }

    void PipboyScreen::buildSections(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        const struct {
            const char* id;
            int text;
            const char* fallback;
        } sections[] = {
            { "pipboy.section.quests", kTextQuests, "Quests" },
            { "pipboy.section.data", kTextData, "Data" },
            { "pipboy.section.maps", kTextMaps, "Maps" },
            { "pipboy.section.videos", kTextVideos, "Videos" },
            { "pipboy.section.rest", kTextRest, "Rest" },
        };

        int count = static_cast<int>(Section::Count);
        float gap = ui.dp(tall ? 8.0f : 4.0f);
        std::vector<std::u32string> labels;
        for (int index = 0; index < count; index++) {
            labels.push_back(ceText(sections[index].text, sections[index].fallback));
        }
        MuiTabRow tabRow = muiLayoutTabRow(rect, labels, gap, ui.dp(10.0f), ui.dp(tall ? 16.0f : 14.0f), ui.dp(10.0f));

        for (int index = 0; index < count; index++) {
            const MuiRect& tabRect = tabRow.cells[index];
            bool active = static_cast<int>(section) == index;
            bool pressed;
            bool tapped = ui.touchable(sections[index].id, tabRect, &pressed);
            muiFillRoundRect(tabRect, ui.dp(6.0f), pressed ? theme.buttonPressed : (active ? theme.buttonPrimary : theme.button));
            muiStrokeRoundRect(tabRect, ui.dp(6.0f), ui.dp(1.0f), active || pressed ? theme.accent : theme.buttonBorder);
            const std::u32string& label = labels[index];
            float size = muiFitTextSize(label, tabRect.w - ui.dp(10.0f), tabRow.textSize, ui.dp(10.0f));
            muiDrawTextAligned(label, tabRect, size, active && !pressed ? theme.buttonPrimaryText : theme.buttonText, MuiAlign::Center, MuiAlign::Center);

            if (tapped && !active) {
                _gsound_red_butt_press(-1, 0);
                section = static_cast<Section>(index);
                detailKey.clear();
                // The movie belongs to the videos section.
                finishMovie();
            }
        }
    }

    // MARK: Parts

    bool PipboyScreen::row(MuiContext& ui, const std::string& id, const MuiRect& rect, bool selected)
    {
        bool pressed;
        bool tapped = ui.touchable(id, rect, &pressed);
        if (pressed || selected) {
            muiFillRoundRect(rect, ui.dp(5.0f), pressed ? muiTheme().buttonPressed : kSelected);
        }
        return tapped;
    }

    // Scrolling list of rows with an optional detail at the right (counts).
    void PipboyScreen::listRows(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<std::u32string>& names, const std::vector<std::u32string>& details, int* selected)
    {
        const MuiTheme& theme = muiTheme();
        ui.panel(rect);

        MuiRect inner = rect.inset(ui.dp(tall ? 8.0f : 6.0f));
        float rowHeight = ui.dp(tall ? 48.0f : 38.0f);
        float size = ui.dp(tall ? 16.0f : 14.5f);
        float padding = ui.dp(tall ? 14.0f : 8.0f);

        float offset = ui.scroll(id, inner, names.size() * rowHeight);
        bool scrolling = ui.isScrolling(id);

        muiPushClip(inner);
        for (int index = 0; index < static_cast<int>(names.size()); index++) {
            MuiRect rowRect = { inner.x, inner.y - offset + index * rowHeight, inner.w, rowHeight };
            if (rowRect.bottom() < inner.y || rowRect.y > inner.bottom()) {
                continue;
            }

            if (row(ui, id + "." + std::to_string(index), rowRect, index == *selected) && !scrolling && index != *selected) {
                soundPlayFile("ib1p1xx1");
                *selected = index;
                detailKey.clear();
            }

            const std::u32string& detail = index < static_cast<int>(details.size()) ? details[index] : std::u32string();
            float detailWidth = detail.empty() ? 0.0f : muiTextWidth(detail, size) + padding;
            float textSize = muiFitTextSize(names[index], rowRect.w - padding * 2.0f - detailWidth, size, ui.dp(10.0f));
            muiDrawTextAligned(names[index], { rowRect.x + padding, rowRect.y, rowRect.w, rowRect.h }, textSize, index == *selected ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
            if (!detail.empty()) {
                muiDrawTextAligned(detail, { rowRect.x, rowRect.y, rowRect.w - padding, rowRect.h }, size, theme.textDim, MuiAlign::End, MuiAlign::Center);
            }
        }
        muiPopClip();
    }

    void PipboyScreen::drawEmpty(MuiContext& ui, const MuiRect& rect, const std::u32string& text)
    {
        ui.panel(rect);
        MuiRect inner = rect.inset(ui.dp(tall ? 20.0f : 14.0f));
        float size = ui.dp(tall ? 16.0f : 14.0f);
        float y = inner.y;
        for (const std::u32string& line : muiWrapText(text, inner.w, size)) {
            muiDrawText(line, inner.x, y, size, muiTheme().textDim);
            y += muiLineHeight(size);
        }
    }

    // Detail changed: its scroll starts at the top.
    void PipboyScreen::resetDetail(MuiContext& ui, const std::string& key)
    {
        if (key != detailKey) {
            detailKey = key;
            ui.setScroll("pipboy.detail", 0.0f);
        }
    }

    // Title and paragraphs, scrolling.
    void PipboyScreen::drawText(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& title, const std::vector<std::u32string>& paragraphs)
    {
        const MuiTheme& theme = muiTheme();
        ui.panel(rect);

        MuiRect inner = tall ? rect.inset(ui.dp(20.0f), ui.dp(16.0f)) : rect.inset(ui.dp(12.0f), ui.dp(10.0f));
        float titleSize = ui.dp(tall ? 20.0f : 16.0f);
        float size = ui.dp(tall ? 16.0f : 14.0f);
        float lineHeight = muiLineHeight(size);
        float paragraphGap = ui.dp(tall ? 10.0f : 8.0f);
        float titleGap = ui.dp(tall ? 10.0f : 8.0f);

        std::vector<std::u32string> titleLines = muiWrapText(title, inner.w, titleSize);
        std::vector<std::vector<std::u32string>> wrapped;
        float height = titleLines.size() * muiLineHeight(titleSize) + titleGap;
        for (const std::u32string& paragraph : paragraphs) {
            wrapped.push_back(muiWrapText(paragraph, inner.w, size));
            height += wrapped.back().size() * lineHeight + paragraphGap;
        }

        float offset = ui.scroll(id, inner, height);

        muiPushClip(inner);
        float y = inner.y - offset;
        for (const std::u32string& line : titleLines) {
            muiDrawText(line, inner.x, y, titleSize, theme.accent);
            y += muiLineHeight(titleSize);
        }
        y += titleGap;

        for (const std::vector<std::u32string>& lines : wrapped) {
            for (const std::u32string& line : lines) {
                if (y + lineHeight >= inner.y && y <= inner.bottom()) {
                    muiDrawText(line, inner.x, y, size, theme.text);
                }
                y += lineHeight;
            }
            y += paragraphGap;
        }
        muiPopClip();
    }

    // MARK: Sections

    // Locations with the count of quests to do; their quests, done ones at
    // the end crossed out (like the game).
    void PipboyScreen::buildQuests(MuiContext& ui, const MuiRect& list, const MuiRect& detail)
    {
        const MuiTheme& theme = muiTheme();
        std::vector<PipboyQuestLocation> locations = pipboyGetQuests();
        if (locations.empty()) {
            drawEmpty(ui, { list.x, list.y, detail.right() - list.x, list.h }, gameText(pipboyGetText(kPipboyNoQuests)));
            return;
        }

        quest = std::min(quest, static_cast<int>(locations.size()) - 1);

        std::vector<std::u32string> names;
        std::vector<std::u32string> counts;
        for (const PipboyQuestLocation& location : locations) {
            names.push_back(gameText(location.name));
            counts.push_back(location.activeCount != 0 ? number(location.activeCount) : U"");
        }
        listRows(ui, "pipboy.quests", list, names, counts, &quest);

        const PipboyQuestLocation& location = locations[quest];
        resetDetail(ui, std::string("quests.") + location.name);

        ui.panel(detail);
        MuiRect inner = tall ? detail.inset(ui.dp(20.0f), ui.dp(16.0f)) : detail.inset(ui.dp(12.0f), ui.dp(10.0f));
        float titleSize = ui.dp(tall ? 20.0f : 16.0f);
        float size = ui.dp(tall ? 16.0f : 14.0f);
        float lineHeight = muiLineHeight(size);
        float mark = ui.dp(tall ? 20.0f : 18.0f);
        float textWidth = inner.w - mark;

        std::vector<const PipboyQuest*> ordered;
        for (const PipboyQuest& entry : location.quests) {
            if (!entry.completed) {
                ordered.push_back(&entry);
            }
        }
        for (const PipboyQuest& entry : location.quests) {
            if (entry.completed) {
                ordered.push_back(&entry);
            }
        }

        std::vector<std::vector<std::u32string>> wrapped;
        float height = muiLineHeight(titleSize) + ui.dp(8.0f);
        for (const PipboyQuest* entry : ordered) {
            wrapped.push_back(muiWrapText(gameText(entry->text), textWidth, size));
            height += wrapped.back().size() * lineHeight + ui.dp(10.0f);
        }

        float offset = ui.scroll("pipboy.detail", inner, height);

        muiPushClip(inner);
        float y = inner.y - offset;
        muiDrawText(gameText(location.name), inner.x, y, titleSize, theme.accent);
        y += muiLineHeight(titleSize) + ui.dp(8.0f);

        for (size_t index = 0; index < ordered.size(); index++) {
            bool done = ordered[index]->completed;
            MuiColor color = done ? theme.textDim : theme.text;

            if (done) {
                // Check mark.
                float cx = inner.x + ui.dp(6.0f);
                float cy = y + lineHeight / 2.0f;
                muiDrawPolyline({ { cx - ui.dp(4.0f), cy }, { cx - ui.dp(1.0f), cy + ui.dp(3.0f) }, { cx + ui.dp(5.0f), cy - ui.dp(4.0f) } }, ui.dp(1.6f), theme.textDim);
            } else {
                muiFillCircle(inner.x + ui.dp(6.0f), y + lineHeight / 2.0f, ui.dp(3.0f), theme.accent);
            }

            for (const std::u32string& line : wrapped[index]) {
                muiDrawText(line, inner.x + mark, y, size, color);
                if (done) {
                    // Crossed out, as the game shows done quests.
                    float middle = y + lineHeight * 0.52f;
                    muiDrawLine(inner.x + mark, middle, inner.x + mark + muiTextWidth(line, size), middle, ui.dp(1.0f), theme.textDim);
                }
                y += lineHeight;
            }
            y += ui.dp(10.0f);
        }
        muiPopClip();
    }

    // Holodisks and their text (lines of the game joined into paragraphs).
    void PipboyScreen::buildData(MuiContext& ui, const MuiRect& list, const MuiRect& detail)
    {
        std::vector<PipboyHolodisk> holodisks = pipboyGetHolodisks();
        if (holodisks.empty()) {
            drawEmpty(ui, { list.x, list.y, detail.right() - list.x, list.h }, ceText(kTextEmpty, "Nothing here yet."));
            return;
        }

        holodisk = std::min(holodisk, static_cast<int>(holodisks.size()) - 1);

        std::vector<std::u32string> names;
        for (const PipboyHolodisk& entry : holodisks) {
            names.push_back(gameText(entry.name));
        }
        listRows(ui, "pipboy.data", list, names, {}, &holodisk);

        const PipboyHolodisk& entry = holodisks[holodisk];
        resetDetail(ui, "data." + std::to_string(entry.index));

        // As the game: a holodisk shown plays its narration.
        if (entry.index != narratedHolodisk) {
            pipboyPlayHolodiskNarration(entry.index);
            narratedHolodisk = entry.index;
        }

        std::vector<std::u32string> paragraphs;
        for (const std::string& paragraph : pipboyGetHolodiskText(entry.index)) {
            paragraphs.push_back(gameText(paragraph.c_str()));
        }
        drawText(ui, "pipboy.detail", detail, gameText(entry.name), paragraphs);
    }

    // Locations, a location's maps under it; the map of the chosen one.
    void PipboyScreen::buildMaps(MuiContext& ui, const MuiRect& list, const MuiRect& detail)
    {
        const MuiTheme& theme = muiTheme();
        std::vector<PipboyAutomapLocation> locations = pipboyGetAutomaps();
        if (locations.empty()) {
            drawEmpty(ui, { list.x, list.y, detail.right() - list.x, list.h }, ceText(kTextEmpty, "Nothing here yet."));
            return;
        }

        mapLocation = std::min(mapLocation, static_cast<int>(locations.size()) - 1);
        mapEntry = std::min(mapEntry, static_cast<int>(locations[mapLocation].maps.size()) - 1);

        // Accordion: the chosen location open.
        ui.panel(list);
        MuiRect inner = list.inset(ui.dp(tall ? 8.0f : 6.0f));
        float rowHeight = ui.dp(tall ? 48.0f : 38.0f);
        float subHeight = ui.dp(tall ? 42.0f : 34.0f);
        float size = ui.dp(tall ? 16.0f : 14.5f);
        float padding = ui.dp(tall ? 14.0f : 8.0f);

        float height = locations.size() * rowHeight + locations[mapLocation].maps.size() * subHeight;
        float offset = ui.scroll("pipboy.maps", inner, height);
        bool scrolling = ui.isScrolling("pipboy.maps");

        muiPushClip(inner);
        float y = inner.y - offset;
        for (int index = 0; index < static_cast<int>(locations.size()); index++) {
            bool open = index == mapLocation;
            MuiRect rowRect = { inner.x, y, inner.w, rowHeight };
            y += rowHeight;

            if (row(ui, "pipboy.maps." + std::to_string(index), rowRect, false) && !scrolling && !open) {
                soundPlayFile("ib1p1xx1");
                mapLocation = index;
                mapEntry = 0;
            }

            // Open / closed arrow.
            float ax = rowRect.x + padding + ui.dp(4.0f);
            float ay = rowRect.centerY();
            float a = ui.dp(4.0f);
            if (open) {
                muiDrawPolyline({ { ax - a, ay - a / 2.0f }, { ax, ay + a / 2.0f }, { ax + a, ay - a / 2.0f } }, ui.dp(1.6f), theme.accent);
            } else {
                muiDrawPolyline({ { ax - a / 2.0f, ay - a }, { ax + a / 2.0f, ay }, { ax - a / 2.0f, ay + a } }, ui.dp(1.6f), theme.textDim);
            }

            std::u32string name = gameText(locations[index].name);
            float textX = ax + a + ui.dp(8.0f);
            muiDrawTextAligned(name, { textX, rowRect.y, rowRect.right() - textX - padding, rowRect.h }, muiFitTextSize(name, rowRect.right() - textX - padding, size, ui.dp(10.0f)), open ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);

            if (!open) {
                continue;
            }

            for (int entry = 0; entry < static_cast<int>(locations[index].maps.size()); entry++) {
                MuiRect subRect = { inner.x + ui.dp(14.0f), y, inner.w - ui.dp(14.0f), subHeight };
                y += subHeight;
                if (row(ui, "pipboy.maps." + std::to_string(index) + "." + std::to_string(entry), subRect, entry == mapEntry) && !scrolling && entry != mapEntry) {
                    soundPlayFile("ib1p1xx1");
                    mapEntry = entry;
                }

                std::u32string mapName = gameText(locations[index].maps[entry].name);
                float mapSize = muiFitTextSize(mapName, subRect.w - padding * 2.0f, ui.dp(tall ? 15.0f : 13.5f), ui.dp(10.0f));
                muiDrawTextAligned(mapName, { subRect.x + padding, subRect.y, subRect.w, subRect.h }, mapSize, entry == mapEntry ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
            }
        }
        muiPopClip();

        // The map (saved tiles, the game shows no markers on them).
        const PipboyAutomap& automap = locations[mapLocation].maps[mapEntry];
        std::string key = std::to_string(automap.map) + "." + std::to_string(automap.elevation);
        if (key != mapKey) {
            mapKey = key;
            std::vector<unsigned char> tiles;
            if (!automapGetPipboyTiles(automap.map, automap.elevation, tiles)) {
                tiles.clear();
            }
            mapView.setTiles(key, tiles, HEX_GRID_WIDTH, tiles.empty() ? 0 : HEX_GRID_HEIGHT);
        }

        ui.panel(detail);
        mapView.build(ui, "pipboy.map", detail.inset(ui.dp(4.0f)), {});
    }

    // Seen movies; the game plays the chosen one.
    void PipboyScreen::buildVideos(MuiContext& ui, const MuiRect& list, const MuiRect& detail)
    {
        const MuiTheme& theme = muiTheme();
        std::vector<PipboyVideo> videos = pipboyGetVideos();
        if (videos.empty()) {
            drawEmpty(ui, { list.x, list.y, detail.right() - list.x, list.h }, ceText(kTextEmpty, "Nothing here yet."));
            return;
        }

        video = std::min(video, static_cast<int>(videos.size()) - 1);

        std::vector<std::u32string> names;
        for (const PipboyVideo& entry : videos) {
            names.push_back(gameText(entry.name));
        }
        listRows(ui, "pipboy.videos", list, names, {}, &video);

        // Another one chosen: the playing one stops.
        if (playingMovie != -1 && playingMovie != videos[video].movie) {
            finishMovie();
        }

        ui.panel(detail);
        MuiRect inner = detail.inset(ui.dp(tall ? 16.0f : 12.0f));
        float titleSize = ui.dp(tall ? 20.0f : 16.0f);
        std::u32string title = gameText(videos[video].name);
        float titleHeight = muiLineHeight(titleSize) + ui.dp(6.0f);
        muiDrawTextAligned(title, { inner.x, inner.y, inner.w, muiLineHeight(titleSize) }, muiFitTextSize(title, inner.w, titleSize, ui.dp(11.0f)), theme.accent, MuiAlign::Start, MuiAlign::Start);

        float controlsHeight = ui.dp(40.0f);
        MuiRect screen = { inner.x, inner.y + titleHeight, inner.w, inner.h - titleHeight - controlsHeight - ui.dp(8.0f) };

        if (playingMovie == -1) {
            // Not playing: black screen with the play button.
            muiFillRoundRect(screen, ui.dp(6.0f), muiRgb(0x000000));
            float buttonSize = ui.dp(64.0f);
            MuiRect play = { screen.centerX() - buttonSize / 2.0f, screen.centerY() - buttonSize / 2.0f, buttonSize, buttonSize };
            bool pressed;
            if (ui.touchable("pipboy.videos.play", play, &pressed)) {
                _gsound_red_butt_press(-1, 0);
                pending = Pending::Video;
                pendingValue = videos[video].movie;
            }
            muiFillCircle(play.centerX(), play.centerY(), buttonSize / 2.0f, pressed ? theme.buttonPressed : theme.buttonPrimary);
            muiDrawIcon(MuiIcon::Play, play.centerX() + ui.dp(2.0f), play.centerY(), buttonSize * 0.42f, theme.buttonPrimaryText);

            std::u32string label = ceText(kTextPlay, "Play");
            muiDrawTextAligned(label, { inner.x, screen.bottom() + ui.dp(8.0f), inner.w, controlsHeight }, ui.dp(13.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
            return;
        }

        muiDrawMovie(ui, screen);

        // Pause / go on, stop; full screen at the right.
        float button = controlsHeight;
        float y = screen.bottom() + ui.dp(8.0f);
        bool paused = movieIsPaused();
        if (playerButton(ui, "pipboy.videos.pause", { inner.x, y, button * 1.4f, button }, paused ? MuiIcon::Play : MuiIcon::Pause)) {
            moviePause(!paused);
        }
        if (playerButton(ui, "pipboy.videos.stop", { inner.x + button * 1.4f + ui.dp(6.0f), y, button * 1.4f, button }, MuiIcon::Stop)) {
            finishMovie();
        }
        if (playerButton(ui, "pipboy.videos.fullscreen", { inner.right() - button * 1.4f, y, button * 1.4f, button }, MuiIcon::Expand)) {
            fullscreen = true;
            controlsTime = now;
        }
    }

    bool PipboyScreen::playerButton(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiIcon icon)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed;
        bool tapped = ui.touchable(id, rect, &pressed);
        muiFillRoundRect(rect, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);
        muiDrawIcon(icon, rect.centerX(), rect.centerY(), rect.h * 0.46f, theme.accent);
        if (tapped) {
            _gsound_red_butt_press(-1, 0);
        }
        return tapped;
    }

    // The movie over the whole screen; a tap shows the controls for a while.
    void PipboyScreen::buildFullscreenMovie(MuiContext& ui)
    {
        constexpr unsigned int kControlsMs = 3000;

        muiDrawMovie(ui, ui.screenRect());

        bool paused = movieIsPaused();
        bool controls = paused || now - controlsTime < kControlsMs;
        if (controls) {
            MuiRect safe = ui.safeRect().inset(ui.dp(12.0f));
            float button = ui.dp(48.0f);
            float width = button * 1.4f;
            float gap = ui.dp(10.0f);
            float total = width * 3.0f + gap * 2.0f;
            float x = safe.centerX() - total / 2.0f;
            float y = safe.bottom() - button;
            muiFillRoundRect({ x - gap, y - gap, total + gap * 2.0f, button + gap * 2.0f }, ui.dp(10.0f), muiRgb(0x000000, 160));

            if (playerButton(ui, "pipboy.videos.pause", { x, y, width, button }, paused ? MuiIcon::Play : MuiIcon::Pause)) {
                moviePause(!paused);
                controlsTime = now;
            }
            if (playerButton(ui, "pipboy.videos.stop", { x + width + gap, y, width, button }, MuiIcon::Stop)) {
                finishMovie();
                return;
            }
            if (playerButton(ui, "pipboy.videos.fullscreen", { x + (width + gap) * 2.0f, y, width, button }, MuiIcon::Collapse)) {
                fullscreen = false;
                return;
            }
        }

        // Anywhere else: controls on / off.
        if (ui.touchable("pipboy.videos.screen", ui.screenRect())) {
            controlsTime = controls && !paused ? 0 : now;
        }
    }

    bool PipboyScreen::restButton(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, const std::u32string& detail, bool enabled)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed;
        bool tapped = ui.touchable(id, rect, &pressed);
        muiFillRoundRect(rect, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);

        MuiColor color = enabled ? theme.buttonText : theme.buttonText.withAlpha(90);
        float size = muiFitTextSize(label, rect.w - ui.dp(8.0f), ui.dp(tall ? 16.0f : 14.0f), ui.dp(10.0f));
        if (detail.empty()) {
            muiDrawTextAligned(label, rect, size, color, MuiAlign::Center, MuiAlign::Center);
        } else {
            float detailSize = ui.dp(tall ? 12.5f : 11.0f);
            float total = muiLineHeight(size) + muiLineHeight(detailSize);
            float y = rect.centerY() - total / 2.0f;
            muiDrawTextAligned(label, { rect.x, y, rect.w, muiLineHeight(size) }, size, color, MuiAlign::Center, MuiAlign::Start);
            muiDrawTextAligned(detail, { rect.x, y + muiLineHeight(size), rect.w, muiLineHeight(detailSize) }, detailSize, enabled ? theme.textDim : theme.textDim.withAlpha(90), MuiAlign::Center, MuiAlign::Start);
        }
        return tapped && enabled;
    }

    // Clock, date, hit points; rest options by kind: how long, until an
    // hour, until healed (the game's options, its wake hours).
    void PipboyScreen::buildRest(MuiContext& ui, const MuiRect& left, const MuiRect& right)
    {
        const MuiTheme& theme = muiTheme();
        bool interactive = ui.isInteractive;
        bool canRest = pipboyCanRest();
        int time = gameTimeGetHour();

        ui.panel(left);
        MuiRect inner = left.inset(ui.dp(12.0f));
        float clockSize = std::min(ui.dp(48.0f), inner.w / 4.0f);
        float y = inner.y + ui.dp(12.0f);
        muiDrawTextAligned(clockText(time), { inner.x, y, inner.w, muiLineHeight(clockSize) }, clockSize, theme.accent, MuiAlign::Center, MuiAlign::Start);
        y += muiLineHeight(clockSize);
        muiDrawTextAligned(dateText(), { inner.x, y, inner.w, ui.dp(tall ? 22.0f : 20.0f) }, ui.dp(tall ? 16.0f : 14.0f), theme.textDim, MuiAlign::Center, MuiAlign::Start);
        y += ui.dp(tall ? 34.0f : 30.0f);

        std::u32string hp = gameText(pipboyGetText(kPipboyHitPoints)) + U" " + number(critterGetHitPoints(gDude)) + U"/" + number(critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS));
        muiDrawTextAligned(hp, { inner.x, y, inner.w, ui.dp(tall ? 24.0f : 22.0f) }, ui.dp(tall ? 17.0f : 15.0f), theme.text, MuiAlign::Center, MuiAlign::Start);

        if (resting) {
            // The only touchable thing while resting: stops it like Esc.
            ui.isInteractive = interactive;
            MuiRect stop = { inner.x + ui.dp(10.0f), inner.bottom() - ui.dp(44.0f), inner.w - ui.dp(20.0f), ui.dp(44.0f) };
            bool pressed;
            if (ui.touchable("pipboy.rest.stop", stop, &pressed)) {
                _gsound_red_butt_press(-1, 0);
                pipboyStopRest();
            }
            muiFillRoundRect(stop, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.button);
            muiStrokeRoundRect(stop, ui.dp(6.0f), ui.dp(1.2f), kWarning);
            muiDrawTextAligned(ceText(kTextStop, "Stop"), stop, ui.dp(15.0f), kWarning, MuiAlign::Center, MuiAlign::Center);
            ui.isInteractive = false;
        } else if (!canRest) {
            std::u32string text = gameText(pipboyGetText(kPipboyCannotRest));
            float size = ui.dp(13.0f);
            float ty = inner.bottom() - ui.dp(40.0f);
            for (const std::u32string& line : muiWrapText(text, inner.w, size)) {
                muiDrawTextAligned(line, { inner.x, ty, inner.w, muiLineHeight(size) }, size, kWarning, MuiAlign::Center, MuiAlign::Start);
                ty += muiLineHeight(size);
            }
        }

        ui.panel(right);
        MuiRect options = right.inset(ui.dp(10.0f));
        float headingSize = ui.dp(tall ? 13.0f : 11.5f);
        float headingHeight = ui.dp(tall ? 26.0f : 20.0f);
        float gap = ui.dp(tall ? 8.0f : 6.0f);
        float buttonHeight = std::min(ui.dp(tall ? 56.0f : 46.0f), (options.h - headingHeight * 3.0f - gap * 6.0f) / 4.0f);
        float columnWidth = (options.w - gap * 3.0f) / 4.0f;
        bool enabled = canRest && !resting;
        std::u32string minutes = ceText(kTextMinutes, "min");
        std::u32string hours = ceText(kTextHours, "h");

        auto heading = [&](const std::u32string& text) {
            muiDrawTextAligned(text, { options.x + ui.dp(2.0f), y, options.w, headingHeight }, headingSize, theme.textDim, MuiAlign::Start, MuiAlign::Center);
            y += headingHeight;
        };

        auto choose = [&](int option) {
            pending = Pending::Rest;
            pendingValue = option;
        };

        // How long: when it ends.
        y = options.y;
        heading(ceText(kTextRestFor, "Rest for"));
        for (int option = PIPBOY_REST_DURATION_TEN_MINUTES; option <= PIPBOY_REST_DURATION_SIX_HOURS; option++) {
            int column = option % 4;
            int line = option / 4;
            MuiRect rect = { options.x + column * (columnWidth + gap), y + line * (buttonHeight + gap), columnWidth, buttonHeight };
            int restMinutes = kRestMinutes[option];
            std::u32string label = restMinutes < 60 ? number(restMinutes) + U" " + minutes : number(restMinutes / 60) + U" " + hours;
            int end = (time / 100 * 60 + time % 100 + restMinutes) % (24 * 60);
            if (restButton(ui, "pipboy.rest." + std::to_string(option), rect, label, clockText(end / 60 * 100 + end % 60), enabled)) {
                choose(option);
            }
        }
        y += (buttonHeight + gap) * 2.0f;

        // Until an hour (the game's wake hours, mods can change them).
        heading(ceText(kTextWakeUpAt, "Wake up at"));
        for (int option = PIPBOY_REST_DURATION_UNTIL_MORNING; option <= PIPBOY_REST_DURATION_UNTIL_MIDNIGHT; option++) {
            int column = option - PIPBOY_REST_DURATION_UNTIL_MORNING;
            MuiRect rect = { options.x + column * (columnWidth + gap), y, columnWidth, buttonHeight };
            int wakeHour = pipboyGetRestOptionWakeHour(option);
            if (restButton(ui, "pipboy.rest." + std::to_string(option), rect, clockText(wakeHour * 100), U"", enabled)) {
                choose(option);
            }
        }
        y += buttonHeight + gap;

        // Until healed (the party only with someone to heal).
        heading(ceText(kTextUntilHealed, "Until healed"));
        int healOptions = pipboyGetRestOptionCount() - PIPBOY_REST_DURATION_UNTIL_HEALED;
        float healWidth = (options.w - gap * (healOptions - 1)) / healOptions;
        for (int index = 0; index < healOptions; index++) {
            int option = PIPBOY_REST_DURATION_UNTIL_HEALED + index;
            MuiRect rect = { options.x + index * (healWidth + gap), y, healWidth, buttonHeight };
            if (restButton(ui, "pipboy.rest." + std::to_string(option), rect, gameText(pipboyGetRestOptionText(option)), U"", enabled)) {
                choose(option);
            }
        }
    }

} // namespace

void muiPipboyScreenRun(bool rest)
{
    PipboyScreen& screen = gPipboyScreen;
    screen.reset(rest);
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        } else if (keyCode == KEY_LOWERCASE_P || keyCode == KEY_UPPERCASE_P) {
            // The Pip-Boy's key closes it (as in the game).
            screen.finished = true;
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE || pipboyShouldClose()) {
            screen.finished = true;
        }

        screen.runPending();
        screen.updateMovie();

        devAutotestTick();

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    screen.finishMovie();
    muiRemove(&screen);
}

} // namespace fallout
