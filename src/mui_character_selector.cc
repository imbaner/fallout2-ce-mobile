#include "mui.h"
#include "mui_screens.h"

#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "art.h"
#include "character_editor.h"
#include "character_selector.h"
#include "critter.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "message.h"
#include "object.h"
#include "palette.h"
#include "proto.h"
#include "skill.h"
#include "stat.h"
#include "svga.h"
#include "trait.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x060A07);
    const MuiColor kPanel = muiRgb(0x0B120D);
    const MuiColor kPoints = muiRgb(0xFFB000);

    // Texts in `game\ce.msg`.
    constexpr int kTextSpecialFirst = 293;
    constexpr int kTextModify = 300;
    constexpr int kTextPlay = 301;
    constexpr int kTextCreate = 302;
    constexpr int kTextCustomTab = 303;
    constexpr int kTextCustomTitle = 304;
    constexpr int kTextCustomBio = 305;
    constexpr int kTextPoints = 306;
    constexpr int kTextTagSkills = 307;
    constexpr int kTextChooseTraits = 308;
    constexpr int kTextSkills = 309;
    constexpr int kTextTraits = 310;

    // MISC.MSG, as the game's window.
    constexpr int kMessageHitPoints = 16;
    constexpr int kMessageActionPoints = 15;

    // The portraits are panoramas with the edge of the window's monitor
    // around them: that edge is left out.
    constexpr int kPortraitEdge = 16;

    // Loading screen picture of a lone figure (nobody in particular) for
    // the character the player makes; the band of it with the figure.
    constexpr int kCustomSplash = 3;
    constexpr int kCustomSplashTop = 85;

    // What a card shows: the dude as the game makes it for that choice.
    struct CardStats {
        std::u32string name;
        int special[7];
        int hitPoints;
        int armorClass;
        int actionPoints;
        int meleeDamage;
        std::vector<std::pair<std::u32string, int>> skills;
        std::vector<std::u32string> traits;
        int freePoints;
    };

    std::u32string number(int value)
    {
        char text[16];
        snprintf(text, sizeof(text), "%d", value);
        return muiDecodeUtf8(text);
    }

    std::u32string messageText(int id)
    {
        MessageListItem item;
        item.num = id;
        return messageListGetItem(&gMiscMessageList, &item) ? muiDecodeGameText(item.text) : std::u32string();
    }

    CardStats readDude()
    {
        CardStats stats;
        stats.name = muiDecodeGameText(objectGetName(gDude));
        for (int stat = STAT_STRENGTH; stat <= STAT_LUCK; stat++) {
            stats.special[stat] = critterGetStat(gDude, static_cast<Stat>(stat));
        }
        stats.hitPoints = critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS);
        stats.armorClass = critterGetStat(gDude, STAT_ARMOR_CLASS);
        stats.actionPoints = critterGetStat(gDude, STAT_MAXIMUM_ACTION_POINTS);
        stats.meleeDamage = critterGetStat(gDude, STAT_MELEE_DAMAGE);

        Skill skills[DEFAULT_TAGGED_SKILLS];
        skillsGetTagged(skills, DEFAULT_TAGGED_SKILLS);
        for (Skill skill : skills) {
            if (skill >= 0 && skill < SKILL_COUNT) {
                stats.skills.push_back({ muiDecodeGameText(skillGetName(skill)), skillGetValue(gDude, skill) });
            }
        }

        Trait traits[TRAITS_MAX_SELECTED_COUNT];
        traitsGetSelected(&(traits[0]), &(traits[1]));
        for (Trait trait : traits) {
            char* name = traitGetName(trait);
            if (name != nullptr) {
                stats.traits.push_back(muiDecodeGameText(name));
            }
        }

        stats.freePoints = gCharacterEditorRemainingCharacterPoints;
        return stats;
    }

    // Premade characters as tabs, the last tab - the player's own one: the
    // portrait (a swipe goes to the next one), biography, stats as the game's
    // window shows them, Modify and Play, or Create for the own one. Back at
    // the right edge.
    class CharacterSelectorScreen : public MuiScreen {
    public:
        enum class Action {
            None,
            Play,
            Modify,
            Create,
            Back,
        };

        CharacterSelectorScreen();

        bool coversScreen() override { return true; }
        bool isGameScreen() override { return true; }
        void build(MuiContext& ui) override;
        void back() override { pending = Action::Back; }
        void key(int keyCode) override;

        // Shows card [index] (the last one - the own character), making the
        // dude its character.
        void show(int index);
        int shown() const { return current; }
        bool isCustom() const { return current == premadeCharacterCount(); }

        Action pending = Action::None;

    private:
        std::vector<std::u32string> names;
        int current = 0;
        CardStats stats;
        std::u32string bioTitle;
        std::u32string bio;
        bool resetBioScroll = true;
        // Tablet windows: the roomy layout (`muiIsTallContent`).
        bool tall = false;

        std::vector<unsigned char> splash;
        int splashWidth = 0;
        int splashHeight = 0;

        bool swiping = false;
        float swipeStartX = 0.0f;
        float swipeX = 0.0f;

        void buildPicture(MuiContext& ui, const MuiRect& rect);
        void buildBio(MuiContext& ui, const MuiRect& rect);
        void buildStats(MuiContext& ui, const MuiRect& rect);
        void buildStatsTall(MuiContext& ui, const MuiRect& rect);
        void buildButtons(MuiContext& ui, const MuiRect& rect);
    };

    CharacterSelectorScreen::CharacterSelectorScreen()
    {
        modal = true;

        // Tab names are the characters' own.
        for (int index = 0; index < premadeCharacterCount(); index++) {
            names.push_back(premadeCharacterLoad(index) ? muiDecodeGameText(objectGetName(gDude)) : U"?");
        }
        names.push_back(muiDecodeGameText(muiText(kTextCustomTab, "+ Own")));

        unsigned char palette[768];
        std::vector<unsigned char> pixels;
        if (gameReadSplash(kCustomSplash, &pixels, palette, &splashWidth, &splashHeight)) {
            splash.resize(pixels.size() * 4);
            for (size_t index = 0; index < pixels.size(); index++) {
                // Palette is 6 bits per channel.
                const unsigned char* color = palette + pixels[index] * 3;
                splash[index * 4] = static_cast<unsigned char>(std::min(color[0] * 4, 255));
                splash[index * 4 + 1] = static_cast<unsigned char>(std::min(color[1] * 4, 255));
                splash[index * 4 + 2] = static_cast<unsigned char>(std::min(color[2] * 4, 255));
                splash[index * 4 + 3] = 255;
            }
        }

        show(premadeCharacterSelected());
    }

    void CharacterSelectorScreen::show(int index)
    {
        int count = premadeCharacterCount();
        current = (index % (count + 1) + count + 1) % (count + 1);
        resetBioScroll = true;
        bioTitle.clear();
        bio.clear();

        if (isCustom()) {
            // As the game's Create: a new character from scratch.
            _ResetPlayer();
            bioTitle = muiDecodeGameText(muiText(kTextCustomTitle, "Your own character"));
            bio = muiDecodeGameText(muiText(kTextCustomBio, "You decide who the Chosen One is: spend the stat points, tag the skills, choose traits and a name."));
        } else {
            premadeCharacterSelect(current);
            premadeCharacterLoad(current);

            // The file's lines are wrapped for the game's window: joined
            // again; its first text line is the title ("Narg's story:").
            std::vector<std::string> lines = premadeCharacterBio(current);
            for (size_t line = 0; line < lines.size(); line++) {
                std::u32string text = muiDecodeGameText(lines[line].c_str());
                while (!text.empty() && (text.back() == U' ' || text.back() == U'\t')) {
                    text.pop_back();
                }
                if (bio.empty() && bioTitle.empty() && !text.empty() && text.back() == U':') {
                    text.pop_back();
                    bioTitle = text;
                    continue;
                }
                if (text.empty()) {
                    if (!bio.empty() && bio.back() != U'\n') {
                        bio += U"\n";
                    }
                    continue;
                }
                if (!bio.empty() && bio.back() != U'\n') {
                    bio += U" ";
                }
                bio += text;
            }
        }

        stats = readDude();
    }

    void CharacterSelectorScreen::key(int keyCode)
    {
        switch (keyCode) {
        case KEY_ARROW_LEFT:
            soundPlayFile("ib2p1xx1");
            show(current - 1);
            break;
        case KEY_ARROW_RIGHT:
            soundPlayFile("ib2p1xx1");
            show(current + 1);
            break;
        case KEY_UPPERCASE_T:
        case KEY_LOWERCASE_T:
            if (!isCustom()) {
                pending = Action::Play;
            }
            break;
        case KEY_UPPERCASE_M:
        case KEY_LOWERCASE_M:
            if (!isCustom()) {
                pending = Action::Modify;
            }
            break;
        case KEY_UPPERCASE_C:
        case KEY_LOWERCASE_C:
            pending = Action::Create;
            break;
        case KEY_UPPERCASE_B:
        case KEY_LOWERCASE_B:
            pending = Action::Back;
            break;
        }
    }

    void CharacterSelectorScreen::build(MuiContext& ui)
    {
        // Opaque, or a window over the title menu's picture.
        MuiScreenFrame frame = muiScreenFrame(ui, kBackground, true);
        const MuiRect& content = frame.content;
        if (muiGameScreenTabs(ui, frame.tabs, "selector", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back || frame.tappedOutside) {
            back();
        }

        float gap = ui.dp(10.0f);
        float tabsHeight = ui.dp(36.0f);
        float minBio = ui.dp(80.0f);

        // The picture as wide as fits over the biography's least room.
        float aspect = static_cast<float>(592 - kPortraitEdge * 2) / (260 - kPortraitEdge * 2);

        // Tablet windows (the window's mockup): the characters' tabs over the
        // whole width, the picture and the story at the left, one column of
        // stats with the buttons under it at the right.
        tall = muiIsTallContent(ui);
        if (tall) {
            float tallGap = ui.dp(12.0f);
            MuiRect tabRect = { content.x, content.y, content.w, ui.dp(44.0f) };
            float bodyY = tabRect.bottom() + tallGap;
            float leftWidth = std::floor(content.w * 0.507f);
            float pictureHeight = std::min(leftWidth / aspect, (content.bottom() - bodyY) * 0.5f);
            MuiRect picture = { content.x, bodyY, leftWidth, pictureHeight };
            MuiRect bioRect = { content.x, picture.bottom() + tallGap, leftWidth, content.bottom() - picture.bottom() - tallGap };
            MuiRect right = { picture.right() + ui.dp(14.0f), bodyY, content.right() - picture.right() - ui.dp(14.0f), content.bottom() - bodyY };

            int selected = current;
            if (ui.segmented("selector.tabs", tabRect, names, &selected)) {
                _gsound_red_butt_press(-1, 0);
                show(selected);
            }

            buildPicture(ui, picture);
            buildBio(ui, bioRect);

            float buttonHeight = ui.dp(52.0f);
            MuiRect buttons = { right.x, right.bottom() - buttonHeight, right.w, buttonHeight };
            buildStatsTall(ui, { right.x, right.y, right.w, buttons.y - tallGap - right.y });
            buildButtons(ui, buttons);
            return;
        }
        float leftWidth = content.w * 0.56f;
        float pictureHeight = std::min(leftWidth / aspect, content.h - tabsHeight - minBio - gap * 2.0f);
        leftWidth = std::max(leftWidth, pictureHeight * aspect);

        MuiRect tabs = { content.x, content.y, leftWidth, tabsHeight };
        MuiRect picture = { content.x, tabs.bottom() + gap, leftWidth, pictureHeight };
        MuiRect bioRect = { content.x, picture.bottom() + gap, leftWidth, content.bottom() - picture.bottom() - gap };
        MuiRect right = { tabs.right() + gap * 1.5f, content.y, content.right() - tabs.right() - gap * 1.5f, content.h };

        int selected = current;
        if (ui.segmented("selector.tabs", tabs, names, &selected)) {
            _gsound_red_butt_press(-1, 0);
            show(selected);
        }

        buildPicture(ui, picture);
        buildBio(ui, bioRect);

        float buttonHeight = ui.dp(44.0f);
        MuiRect buttons = { right.x, right.bottom() - buttonHeight, right.w, buttonHeight };
        buildStats(ui, { right.x, right.y, right.w, buttons.y - gap - right.y });
        buildButtons(ui, buttons);
    }

    void CharacterSelectorScreen::buildPicture(MuiContext& ui, const MuiRect& rect)
    {
        muiFillRoundRect(rect, ui.dp(6.0f), kPanel);

        if (isCustom()) {
            if (!splash.empty()) {
                SDL_Texture* texture = muiRgbaTexture("selector.custom", splash.data(), splashWidth, splashHeight, 1);
                if (texture != nullptr) {
                    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
                    int height = std::min(splashHeight, static_cast<int>(splashWidth * rect.h / rect.w));
                    int top = std::clamp(kCustomSplashTop, 0, splashHeight - height);
                    muiDrawTexturePart(texture, { 0, top, splashWidth, height }, rect);
                }
            }
        } else {
            int width;
            int height;
            SDL_Texture* texture = muiPictureTexture(FrmId(premadeCharacterFace(current)), &width, &height);
            if (texture != nullptr && width > kPortraitEdge * 2 && height > kPortraitEdge * 2) {
                SDL_Rect source = { kPortraitEdge, kPortraitEdge, width - kPortraitEdge * 2, height - kPortraitEdge * 2 };
                muiDrawTexturePart(texture, source, muiFitRect(rect, static_cast<float>(source.w), static_cast<float>(source.h)));
            }
        }

        // A swipe over it goes to the next or previous one.
        bool pressed = false;
        ui.touchable("selector.picture", rect, &pressed);
        if (pressed && !swiping) {
            swiping = true;
            swipeStartX = ui.pointerX();
        }
        if (swiping) {
            if (ui.pointerDown()) {
                swipeX = ui.pointerX();
            } else {
                swiping = false;
                float distance = swipeX - swipeStartX;
                if (std::abs(distance) > ui.dp(48.0f)) {
                    soundPlayFile("ib2p1xx1");
                    show(distance < 0.0f ? current + 1 : current - 1);
                }
            }
        }
    }

    void CharacterSelectorScreen::buildBio(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(tall ? 16.0f : 12.0f);
        float lineHeight = tall ? std::max(muiLineHeight(size), ui.dp(24.0f)) : muiLineHeight(size);

        std::vector<std::u32string> lines;
        size_t start = 0;
        while (start <= bio.size()) {
            size_t end = bio.find(U'\n', start);
            std::u32string paragraph = bio.substr(start, end == std::u32string::npos ? std::u32string::npos : end - start);
            for (const std::u32string& line : muiWrapText(paragraph, rect.w, size)) {
                lines.push_back(line);
            }
            if (end == std::u32string::npos) {
                break;
            }
            start = end + 1;
        }

        float titleSize = tall ? ui.dp(13.0f) : size;
        float titleHeight = bioTitle.empty() ? 0.0f : lineHeight * 1.3f;
        float contentHeight = titleHeight + lines.size() * lineHeight;
        if (resetBioScroll) {
            ui.setScroll("selector.bio", 0.0f);
            resetBioScroll = false;
        }
        float offset = ui.scroll("selector.bio", rect, contentHeight);

        muiPushClip(rect);
        float y = rect.y - offset;
        if (!bioTitle.empty()) {
            muiDrawTextAligned(bioTitle, { rect.x, y, rect.w, lineHeight }, titleSize, tall ? theme.textDim : theme.text, MuiAlign::Start, MuiAlign::Center);
            y += titleHeight;
        }
        for (const std::u32string& line : lines) {
            muiDrawTextAligned(line, { rect.x, y, rect.w, lineHeight }, size, tall ? theme.text : theme.textDim, MuiAlign::Start, MuiAlign::Center);
            y += lineHeight;
        }
        muiPopClip();
    }

    void CharacterSelectorScreen::buildStats(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(12.0f);
        float rowHeight = ui.dp(19.0f);
        float columnGap = ui.dp(14.0f);
        float columnWidth = (rect.w - columnGap) / 2.0f;
        bool custom = isCustom();

        auto row = [&](float x, float y, const std::u32string& label, const std::u32string& value, MuiColor valueColor) {
            MuiRect line = { x, y, columnWidth, rowHeight };
            float labelSize = muiFitTextSize(label, columnWidth * 0.7f, size, ui.dp(9.0f));
            muiDrawTextAligned(label, line, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(value, line, size, valueColor, MuiAlign::End, MuiAlign::Center);
        };

        auto header = [&](float x, float y, const std::u32string& text) {
            muiDrawTextAligned(text, { x, y, columnWidth, rowHeight }, ui.dp(10.0f), theme.textDim, MuiAlign::Start, MuiAlign::End);
        };

        // SPECIAL: short names with the value, the game's word for it.
        static const char* const kSpecialFallbacks[] = { "ST", "PE", "EN", "CH", "IN", "AG", "LK" };
        float y = rect.y;
        for (int stat = 0; stat < 7; stat++) {
            std::u32string label = muiDecodeGameText(muiText(kTextSpecialFirst + stat, kSpecialFallbacks[stat])) + U" " + number(stats.special[stat]);
            row(rect.x, y, label, muiDecodeGameText(statGetValueDescription(stats.special[stat])), theme.textDim);
            y += rowHeight;
        }
        if (custom) {
            char points[16];
            snprintf(points, sizeof(points), "+%d", stats.freePoints);
            MuiRect line = { rect.x, y, columnWidth, rowHeight };
            muiDrawTextAligned(muiDecodeGameText(muiText(kTextPoints, "Points")), line, size, kPoints, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(muiDecodeUtf8(points), line, size, kPoints, MuiAlign::End, MuiAlign::Center);
        }

        // Derived stats, tagged skills.
        float x = rect.x + columnWidth + columnGap;
        y = rect.y;
        row(x, y, messageText(kMessageHitPoints), number(stats.hitPoints), theme.text);
        y += rowHeight;
        row(x, y, muiDecodeGameText(statGetName(STAT_ARMOR_CLASS)), number(stats.armorClass), theme.text);
        y += rowHeight;
        row(x, y, messageText(kMessageActionPoints), number(stats.actionPoints), theme.text);
        y += rowHeight;
        row(x, y, muiDecodeGameText(statGetName(STAT_MELEE_DAMAGE)), number(stats.meleeDamage), theme.text);
        y += rowHeight;

        header(x, y, muiDecodeGameText(muiText(kTextSkills, "Skills")));
        y += rowHeight;
        if (stats.skills.empty()) {
            char text[64];
            snprintf(text, sizeof(text), muiText(kTextTagSkills, "tag %d"), DEFAULT_TAGGED_SKILLS);
            muiDrawTextAligned(muiDecodeGameText(text), { x, y, columnWidth, rowHeight }, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
        }
        for (const auto& skill : stats.skills) {
            row(x, y, skill.first, number(skill.second) + U"%", theme.text);
            y += rowHeight;
        }

        // Traits under both columns.
        y = std::max(rect.y + rowHeight * 8.0f, y) + ui.dp(4.0f);
        MuiRect traitsHeader = { rect.x, y, rect.w, rowHeight };
        muiDrawTextAligned(muiDecodeGameText(muiText(kTextTraits, "Traits")), traitsHeader, ui.dp(10.0f), theme.textDim, MuiAlign::Start, MuiAlign::End);
        y += rowHeight;
        std::u32string traits;
        if (stats.traits.empty()) {
            char text[64];
            snprintf(text, sizeof(text), muiText(kTextChooseTraits, "up to %d, optional"), TRAITS_MAX_SELECTED_COUNT);
            traits = muiDecodeGameText(text);
        }
        for (const std::u32string& trait : stats.traits) {
            if (!traits.empty()) {
                traits += U" · ";
            }
            traits += trait;
        }
        MuiRect traitsRect = { rect.x, y, rect.w, rowHeight };
        muiDrawTextAligned(traits, traitsRect, muiFitTextSize(traits, rect.w, size, ui.dp(9.0f)), stats.traits.empty() ? theme.textDim : theme.text, MuiAlign::Start, MuiAlign::Center);
    }

    // Tall layout: SPECIAL with the full names, values and the game's words
    // for them; derived stats under a line, tagged skills, traits.
    void CharacterSelectorScreen::buildStatsTall(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(16.0f);
        float rankSize = ui.dp(14.0f);
        float captionSize = ui.dp(13.0f);
        bool custom = isCustom();

        // Rows over the height there is (30 dp at most).
        int lineCount = 7 + (custom ? 1 : 0) + 4 + 1 + std::max<int>(1, static_cast<int>(stats.skills.size())) + 2;
        float rowHeight = std::min(ui.dp(30.0f), (rect.h - ui.dp(17.0f)) / lineCount);
        float y = rect.y;

        auto row = [&](const std::u32string& label, const std::u32string& value, MuiColor valueColor, const std::u32string& rank) {
            MuiRect line = { rect.x, y, rect.w, rowHeight };
            float rankWidth = rank.empty() ? 0.0f : muiTextWidth(rank, rankSize) + ui.dp(8.0f);
            float valueWidth = muiTextWidth(value, size);
            float labelSize = muiFitTextSize(label, rect.w - rankWidth - valueWidth - ui.dp(12.0f), size, ui.dp(11.0f));
            muiDrawTextAligned(label, line, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
            if (!rank.empty()) {
                muiDrawTextAligned(rank, line, rankSize, theme.textDim, MuiAlign::End, MuiAlign::Center);
            }
            muiDrawTextAligned(value, { line.x, line.y, line.w - rankWidth, line.h }, size, valueColor, MuiAlign::End, MuiAlign::Center);
            y += rowHeight;
        };

        auto caption = [&](const std::u32string& text) {
            muiDrawTextAligned(text, { rect.x, y, rect.w, rowHeight }, captionSize, theme.textDim, MuiAlign::Start, MuiAlign::End);
            y += rowHeight;
        };

        for (int stat = 0; stat < 7; stat++) {
            row(muiDecodeGameText(statGetName(static_cast<Stat>(STAT_STRENGTH + stat))), number(stats.special[stat]), theme.accent, muiDecodeGameText(statGetValueDescription(stats.special[stat])));
        }
        if (custom) {
            char points[16];
            snprintf(points, sizeof(points), "+%d", stats.freePoints);
            MuiRect line = { rect.x, y, rect.w, rowHeight };
            muiDrawTextAligned(muiDecodeGameText(muiText(kTextPoints, "Points")), line, size, kPoints, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(muiDecodeUtf8(points), line, size, kPoints, MuiAlign::End, MuiAlign::Center);
            y += rowHeight;
        }

        y += ui.dp(8.0f);
        muiFillRect({ rect.x, y, rect.w, ui.dp(1.0f) }, theme.panelBorder);
        y += ui.dp(9.0f);

        row(messageText(kMessageHitPoints), number(stats.hitPoints), theme.accent, U"");
        row(muiDecodeGameText(statGetName(STAT_ARMOR_CLASS)), number(stats.armorClass), theme.accent, U"");
        row(messageText(kMessageActionPoints), number(stats.actionPoints), theme.accent, U"");
        row(muiDecodeGameText(statGetName(STAT_MELEE_DAMAGE)), number(stats.meleeDamage), theme.accent, U"");

        caption(muiDecodeGameText(muiText(kTextSkills, "Skills")));
        if (stats.skills.empty()) {
            char text[64];
            snprintf(text, sizeof(text), muiText(kTextTagSkills, "tag %d"), DEFAULT_TAGGED_SKILLS);
            muiDrawTextAligned(muiDecodeGameText(text), { rect.x, y, rect.w, rowHeight }, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
            y += rowHeight;
        }
        for (const auto& skill : stats.skills) {
            row(skill.first, number(skill.second) + U"%", theme.accent, U"");
        }

        caption(muiDecodeGameText(muiText(kTextTraits, "Traits")));
        std::u32string traits;
        if (stats.traits.empty()) {
            char text[64];
            snprintf(text, sizeof(text), muiText(kTextChooseTraits, "up to %d, optional"), TRAITS_MAX_SELECTED_COUNT);
            traits = muiDecodeGameText(text);
        }
        for (const std::u32string& trait : stats.traits) {
            if (!traits.empty()) {
                traits += U" · ";
            }
            traits += trait;
        }
        muiDrawTextAligned(traits, { rect.x, y, rect.w, rowHeight }, muiFitTextSize(traits, rect.w, size, ui.dp(10.0f)), stats.traits.empty() ? theme.textDim : theme.text, MuiAlign::Start, MuiAlign::Center);
    }

    void CharacterSelectorScreen::buildButtons(MuiContext& ui, const MuiRect& rect)
    {
        if (isCustom()) {
            if (ui.button("selector.create", rect, muiDecodeGameText(muiText(kTextCreate, "Create character")), MuiButtonStyle::Primary)) {
                pending = Action::Create;
            }
            return;
        }

        float gap = ui.dp(8.0f);
        MuiRect modify = { rect.x, rect.y, (rect.w - gap) * 0.42f, rect.h };
        MuiRect play = { modify.right() + gap, rect.y, rect.right() - modify.right() - gap, rect.h };
        if (ui.button("selector.modify", modify, muiDecodeGameText(muiText(kTextModify, "Modify")))) {
            pending = Action::Modify;
        }
        if (ui.button("selector.play", play, muiDecodeGameText(muiText(kTextPlay, "Play")), MuiButtonStyle::Primary)) {
            pending = Action::Play;
        }
    }

} // namespace

int muiCharacterSelectorRun()
{
    // The main menu faded to black; this screen is shown at once.
    colorPaletteLoad("color.pal");
    paletteSetEntries(_cmap);

    CharacterSelectorScreen screen;
    muiPush(&screen);

    int rc = 0;
    while (rc == 0) {
        sharedFpsLimiter.mark();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        } else if (keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        } else if (keyCode != -1) {
            screen.key(keyCode);
        }

        devAutotestTick();
        renderPresent();

        // Tapped actions run after the frame (the character screen is a
        // screen of its own), as the game's window runs them.
        CharacterSelectorScreen::Action action = screen.pending;
        screen.pending = CharacterSelectorScreen::Action::None;
        switch (action) {
        case CharacterSelectorScreen::Action::Play:
            rc = 2;
            break;
        case CharacterSelectorScreen::Action::Modify:
        case CharacterSelectorScreen::Action::Create:
            if (action == CharacterSelectorScreen::Action::Create) {
                _ResetPlayer();
            }
            if (characterEditorShow(1) == 0) {
                rc = 2;
            } else {
                // Cancelled: the card as it was.
                screen.show(screen.shown());
            }
            break;
        case CharacterSelectorScreen::Action::Back:
            rc = 3;
            break;
        case CharacterSelectorScreen::Action::None:
            break;
        }

        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);

    // As the game's window, which fades out.
    paletteSetEntries(gPaletteBlack);
    return rc;
}

} // namespace fallout
