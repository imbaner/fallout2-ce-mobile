#include "mui.h"
#include "mui_icons.h"
#include "mui_notify.h"
#include "mui_screens.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include "platform/web/window.h"
#endif

#include "character_editor.h"
#include "color.h"
#include "critter.h"
#include "dbox.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_sound.h"
#include "hero_appearance.h"
#include "input.h"
#include "kb.h"
#include "object.h"
#include "perk.h"
#include "skill.h"
#include "stat.h"
#include "svga.h"
#include "trait.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x060A07);
    const MuiColor kListBackground = muiRgb(0x0B120D);
    const MuiColor kSelected = muiRgb(0x16301D);
    const MuiColor kWarning = muiRgb(0xFF6E5E);
    const MuiColor kNotice = muiRgb(0xE0C95F);

    // Texts in `game\ce.msg`.
    constexpr int kTextDone = 123;
    constexpr int kTextStats = 190;
    constexpr int kTextReset = 191;
    constexpr int kTextLater = 192;
    constexpr int kTextTake = 193;
    constexpr int kTextNewPerk = 194;
    constexpr int kTextNoConditions = 195;
    constexpr int kTextTag = 196;
    constexpr int kTextUntag = 197;
    constexpr int kTextTraits = 198;
    constexpr int kTextAppearance = 445;
    constexpr int kTextRace = 446;
    constexpr int kTextStyle = 447;
    constexpr int kTextGender = 448;

    // Texts in `game\editor.msg`.
    constexpr int kEditorName = 106;
    constexpr int kEditorMale = 107;
    // The genders' full words ("Male", "Female").
    constexpr int kEditorMaleWord = 645;
    constexpr int kEditorLevel = 113;
    constexpr int kEditorExperience = 114;
    constexpr int kEditorCharacterPoints = 120;
    constexpr int kEditorPerks = 124;
    constexpr int kEditorKarma = 125;
    constexpr int kEditorKills = 126;
    constexpr int kEditorSkillPoints = 130;
    constexpr int kEditorAtMaximum = 132;
    constexpr int kEditorAtMinimum = 134;
    constexpr int kEditorNoSkillPoints = 136;
    constexpr int kEditorTagsUsed = 140;
    constexpr int kEditorTagSkills = 144;
    constexpr int kEditorTraitsUsed = 148;
    constexpr int kEditorSkills = 150;
    constexpr int kEditorPickPerk = 152;
    constexpr int kEditorPickTrait = 153;
    constexpr int kEditorLoseTrait = 154;
    constexpr int kEditorPickTagSkill = 155;
    constexpr int kEditorNameUnchanged = 160;

    // Name length the game's window allows.
    constexpr size_t kNameMaxLength = 11;

    std::u32string gameText(const char* text)
    {
        return muiDecodeGameText(text);
    }

    std::u32string editorText(int id)
    {
        return muiDecodeGameText(characterEditorGetText(id));
    }

    std::u32string ceText(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    std::u32string number(int value)
    {
        return muiDecodeUtf8(std::to_string(value).c_str());
    }

    // Two lines of the game's messages as one.
    std::string messagePair(int id)
    {
        return std::string(characterEditorGetText(id)) + " " + characterEditorGetText(id + 1);
    }

    enum class Tab {
        Stats,
        Skills,
        // Perks, karma, kills in game; traits in creation.
        Perks,
        // Creation with sfall's Hero Appearance sets: gender, race, style.
        Appearance,
    };

    enum class Folder {
        Perks,
        Karma,
        Kills,
    };

    // What the card shows.
    enum class Item {
        None,
        PrimaryStat,
        DerivedStat,
        HitPoints,
        Condition,
        Level,
        SkillPoints,
        Skill,
        Trait,
        FolderEntry,
        Folder,
        // Gender (index 0), race (1), style (2).
        Appearance,
    };

    // Perk choice panel steps (see `CharacterEditorPerkStep`).
    enum class PerkStep {
        Closed,
        Perk,
        TagSkill,
        LoseTrait,
        NewTrait,
    };

    // Waits for the loop around the screen (dialog boxes can't run while the
    // screen is drawn).
    enum class Pending {
        None,
        Done,
    };

    // Character screen: stats, skills, perks (karma, kills) tabs, the card of
    // the chosen line at the right, tabs to the other game screens at the
    // edge. Level ups: skill points on the skills tab, owed perks in the perk
    // panel over it. Creation: SPECIAL points, tag skills, traits, name, age
    // and gender. Works over the character editor's state and rules
    // (character_editor.h).
    class CharacterScreen : public MuiScreen {
    public:
        int result = 0;
        Pending pending = Pending::None;

        CharacterScreen()
        {
            modal = true;
        }

        // Creating: the window is over the title menu's picture.
        bool coversScreen() override { return !muiUsesWindows() || creating; }
        bool isGameScreen() override { return true; }

        void reset();
        void build(MuiContext& ui) override;
        void back() override;
        void key(int keyCode) override;

        // Runs what `pending` asks for.
        void runPending();

    private:
        bool creating = false;
        // Creation's appearance tab (Hero Appearance's sets), the gender
        // there, not in the header.
        bool appearance = false;
        // Tablet windows: the roomy layout (`muiIsTallContent`).
        bool tall = false;
        // Its header's sizes (they fit the width).
        bool roomyHeader = false;
        Tab tab = Tab::Stats;
        Folder folder = Folder::Perks;

        Item item = Item::None;
        int itemIndex = 0;

        PerkStep perkStep = PerkStep::Closed;
        int perkChoice = 0;
        // Owed perks were offered when the screen opened.
        bool perksOffered = false;

        bool nameOpen = false;
        std::u32string name;

        unsigned int now = 0;
        std::string cardKey;
        std::string perkCardKey;

        void select(Item newItem, int index);
        void showMistake(const std::string& text);
        void finish(int rc);

        CharacterEditorCard currentCard();

        void buildHeader(MuiContext& ui, const MuiRect& rect);
        void buildTabs(MuiContext& ui, const MuiRect& column);
        void buildMain(MuiContext& ui, const MuiRect& rect);
        void buildStats(MuiContext& ui, const MuiRect& rect);
        void buildStatsTall(MuiContext& ui, const MuiRect& rect);
        float headerWidth(MuiContext& ui, float height, bool roomy);
        void buildSkills(MuiContext& ui, const MuiRect& rect);
        void buildFolder(MuiContext& ui, const MuiRect& rect);
        void buildTraits(MuiContext& ui, const MuiRect& rect);
        void buildAppearance(MuiContext& ui, const MuiRect& rect);
        void buildCard(MuiContext& ui, const MuiRect& rect, const CharacterEditorCard& card, const std::string& id, std::string* key, bool hero = false);
        void buildPerkPanel(MuiContext& ui);
        void buildNameEditor(MuiContext& ui);

        bool row(MuiContext& ui, const std::string& id, const MuiRect& rect, bool selected, bool* pressed, float touchRight = 0.0f);
        bool smallButton(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiIcon icon, bool enabled);
        bool textButton(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool primary, bool enabled);
        float chip(MuiContext& ui, const std::string& id, float x, const MuiRect& bar, const std::u32string& text, MuiColor color, MuiIcon icon, bool hasIcon, bool* tapped);

        void openPerkPanel();
        void choosePerk();
        void handlePerkStep(CharacterEditorPerkStep step);
        void closeName(bool apply);
    };

    CharacterScreen gCharacterScreen;

    // Skill points to spend, or spent here to take back (otherwise the
    // skills' - and + and the points counter would only show mistakes).
    bool skillPointsInPlay()
    {
        if (pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS) > 0) {
            return true;
        }

        for (int skill = 0; skill < SKILL_COUNT; skill++) {
            if (characterEditorCanLowerSkill(static_cast<Skill>(skill))) {
                return true;
            }
        }
        return false;
    }

    // Owed perks put off with "Later": the panel doesn't open by itself again
    // (switching screens with the tabs) until more are owed.
    int gPerksPutOff = 0;

    void CharacterScreen::reset()
    {
        finished = false;
        result = 0;
        pending = Pending::None;
        creating = characterEditorIsCreating();
        appearance = creating && heroAppearanceHasChoice();
        tab = Tab::Stats;
        folder = Folder::Perks;
        perkStep = PerkStep::Closed;
        perkChoice = 0;
        perksOffered = false;
        nameOpen = false;
        cardKey.clear();
        perkCardKey.clear();
        select(Item::PrimaryStat, STAT_STRENGTH);
    }

    void CharacterScreen::select(Item newItem, int index)
    {
        if (newItem != item || index != itemIndex) {
            // The game's card sound.
            if (item != Item::None) {
                soundPlayFile("isdxxxx1");
            }
            item = newItem;
            itemIndex = index;
        }
    }

    // The game's small mistakes (it shows dialog boxes for them) as a
    // notification, with its error sound.
    void CharacterScreen::showMistake(const std::string& text)
    {
        soundPlayFile("iisxxxx1");
        muiNotify(text.c_str(), MuiNoticeKind::Warning);
    }

    void CharacterScreen::finish(int rc)
    {
        result = rc;
        finished = true;
    }

    CharacterEditorCard CharacterScreen::currentCard()
    {
        switch (item) {
        case Item::PrimaryStat: {
            CharacterEditorCard card = characterEditorGetPrimaryStatCard(static_cast<Stat>(itemIndex));
            card.subtitle = std::to_string(characterEditorGetPrimaryStat(static_cast<Stat>(itemIndex))) + " - " + characterEditorGetPrimaryStatDescription(static_cast<Stat>(itemIndex));
            return card;
        }
        case Item::DerivedStat:
            return characterEditorGetDerivedStatCard(itemIndex);
        case Item::HitPoints:
            return characterEditorGetHitPointsCard();
        case Item::Condition:
            return characterEditorGetConditionCard(itemIndex);
        case Item::Level:
            return characterEditorGetLevelCard(itemIndex);
        case Item::SkillPoints:
            return characterEditorGetSkillPointsCard();
        case Item::Skill:
            return characterEditorGetSkillCard(static_cast<Skill>(itemIndex));
        case Item::Trait:
            return characterEditorGetTraitCard(static_cast<Trait>(itemIndex));
        case Item::FolderEntry: {
            std::vector<CharacterEditorFolderEntry> entries = characterEditorGetFolder(static_cast<CharacterEditorFolder>(folder));
            if (itemIndex >= 0 && itemIndex < static_cast<int>(entries.size()) && entries[itemIndex].hasCard) {
                return entries[itemIndex].card;
            }
            return characterEditorGetFolderCard(static_cast<CharacterEditorFolder>(folder));
        }
        case Item::Folder:
            if (creating) {
                return characterEditorGetTraitsCard();
            }
            return characterEditorGetFolderCard(static_cast<CharacterEditorFolder>(folder));
        case Item::Appearance: {
            CharacterEditorCard card;
            int gender = critterGetStat(gDude, STAT_GENDER);
            if (itemIndex == 0) {
                card.title = characterEditorGetText(kEditorMaleWord + gender);
            } else if (itemIndex == 1) {
                heroAppearanceGetRaceText(gender, heroAppearanceRace(), &card.title, &card.description);
            } else {
                heroAppearanceGetStyleText(gender, heroAppearanceRace(), heroAppearanceStyle(), &card.title, &card.description);
            }
            return card;
        }
        case Item::None:
            break;
        }
        return CharacterEditorCard();
    }

    void CharacterScreen::build(MuiContext& ui)
    {
        now = ui.now;

        // Owed perks are offered first thing (the game's window does the
        // same), "later" leaves them for the perks tab.
        if (!perksOffered) {
            perksOffered = true;
            int owed = creating ? 0 : characterEditorGetPerkOwed();
            if (owed < gPerksPutOff) {
                gPerksPutOff = owed;
            }
            if (owed > gPerksPutOff && !characterEditorGetAvailablePerks().empty()) {
                openPerkPanel();
            }
        }

        // Opaque over the map, or a window over it (the title menu's picture
        // while creating).
        MuiScreenFrame frame = muiScreenFrame(ui, kBackground, creating);

        // Under the perk panel and the name field nothing takes touches (they
        // are parts of this screen, the first widget under the finger would
        // get them).
        bool interactive = ui.isInteractive;
        ui.isInteractive = interactive && perkStep == PerkStep::Closed && !nameOpen;

        const MuiRect& content = frame.content;
        const MuiRect& tabs = frame.tabs;

        // A tap outside is Back in game (Done), not under the perk panel or the
        // name field; creating it would be Cancel.
        if (frame.tappedOutside && !creating && perkStep == PerkStep::Closed && !nameOpen) {
            back();
        }

        tall = muiIsTallContent(ui, content);
        float gap = ui.dp(tall ? 10.0f : 8.0f);
        float headerHeight = ui.dp(tall ? 44.0f : 32.0f);
        buildHeader(ui, { content.x, content.y, content.w, headerHeight });

        MuiRect body = { content.x, content.y + headerHeight + gap, content.w, content.h - headerHeight - gap };
        float cardWidth = tall ? std::clamp(body.w * 0.364f, ui.dp(260.0f), ui.dp(340.0f)) : std::clamp(body.w * 0.35f, ui.dp(220.0f), ui.dp(300.0f));
        MuiRect main = { body.x, body.y, body.w - cardWidth - gap, body.h };
        MuiRect cardRect = { main.right() + gap, body.y, cardWidth, body.h };

        buildMain(ui, main);

        CharacterEditorCard card = currentCard();
        buildCard(ui, cardRect, card, "character.card", &cardKey, item == Item::Appearance);

        buildTabs(ui, tabs);

        ui.isInteractive = interactive;

        if (perkStep != PerkStep::Closed) {
            buildPerkPanel(ui);
        }

        if (nameOpen) {
            buildNameEditor(ui);
        }
    }

    void CharacterScreen::back()
    {
        if (nameOpen) {
            closeName(false);
            return;
        }

        if (perkStep != PerkStep::Closed) {
            if (perkStep == PerkStep::Perk) {
                gPerksPutOff = characterEditorGetPerkOwed();
                perkStep = PerkStep::Closed;
            } else {
                // Extra steps go back to the perks (the perk is taken back).
                characterEditorCancelPerk();
                perkStep = PerkStep::Perk;
                perkChoice = 0;
            }
            return;
        }

        // In game leaving keeps the changes (the game's Done), creation goes
        // back to the characters (Cancel).
        finish(creating ? 1 : 0);
    }

    void CharacterScreen::key(int keyCode)
    {
        if (!nameOpen) {
            return;
        }

        if (keyCode == KEY_BACKSPACE || keyCode == KEY_DELETE) {
            if (!name.empty()) {
                name.pop_back();
            }
        } else if (keyCode == KEY_RETURN) {
            closeName(true);
        }
    }

    void CharacterScreen::runPending()
    {
        Pending what = pending;
        pending = Pending::None;

        if (what == Pending::Done) {
            // The game's checks before starting the game.
            int problem = characterEditorCheckReady();
            if (problem != 0) {
                soundPlayFile("iisxxxx1");
                const char* body[] = { characterEditorGetText(problem + 1) };
                showDialogBox(characterEditorGetText(problem), body, 1, 192, 126, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
                return;
            }

            if (characterEditorNameIsDefault()) {
                soundPlayFile("iisxxxx1");
                const char* body[] = { characterEditorGetText(kEditorNameUnchanged + 1) };
                if (showDialogBox(characterEditorGetText(kEditorNameUnchanged), body, 1, 192, 126, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) == 0) {
                    return;
                }
            }

            soundPlayFile("ib1p1xx1");
            finish(0);
        }
    }

    // MARK: Parts

    // Row highlight over [rect]; touches on [rect] up to [touchRight] (the
    // row's buttons at the right take their own).
    bool CharacterScreen::row(MuiContext& ui, const std::string& id, const MuiRect& rect, bool selected, bool* pressed, float touchRight)
    {
        MuiRect touchRect = rect;
        if (touchRight > 0.0f) {
            touchRect.w = touchRight - rect.x;
        }
        bool tapped = ui.touchable(id, touchRect, pressed);
        if (*pressed || selected) {
            muiFillRoundRect(rect, ui.dp(5.0f), *pressed ? muiTheme().buttonPressed : kSelected);
        }
        return tapped;
    }

    bool CharacterScreen::smallButton(MuiContext& ui, const std::string& id, const MuiRect& rect, MuiIcon icon, bool enabled)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed;
        bool tapped = ui.touchable(id, rect, &pressed);
        muiFillRoundRect(rect, ui.dp(6.0f), pressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);
        MuiColor color = enabled ? theme.accent : theme.accent.withAlpha(80);
        muiDrawIcon(icon, rect.centerX(), rect.centerY(), rect.h * 0.46f, color);
        return tapped;
    }

    bool CharacterScreen::textButton(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool primary, bool enabled)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed;
        bool tapped = ui.touchable(id, rect, &pressed);
        MuiColor fill = pressed ? theme.buttonPressed : (primary ? theme.buttonPrimary : theme.button);
        MuiColor textColor = primary && !pressed ? theme.buttonPrimaryText : theme.buttonText;
        if (!enabled) {
            textColor = textColor.withAlpha(90);
        }
        muiFillRoundRect(rect, ui.dp(6.0f), fill);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);
        float size = muiFitTextSize(label, rect.w - ui.dp(12.0f), ui.dp(tall ? 16.0f : 14.0f), ui.dp(10.0f));
        muiDrawTextAligned(label, rect, size, textColor, MuiAlign::Center, MuiAlign::Center);
        return tapped && enabled;
    }

    // Chip of the header at [x]; returns its width.
    float CharacterScreen::chip(MuiContext& ui, const std::string& id, float x, const MuiRect& bar, const std::u32string& text, MuiColor color, MuiIcon icon, bool hasIcon, bool* tapped)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(roomyHeader ? 16.0f : 14.0f);
        float padding = ui.dp(roomyHeader ? 14.0f : 9.0f);
        float iconSize = ui.dp(roomyHeader ? 18.0f : 15.0f);
        float width = padding * 2.0f + muiTextWidth(text, size) + (hasIcon ? iconSize + ui.dp(5.0f) : 0.0f);
        MuiRect rect = { x, bar.y, width, bar.h };

        bool pressed = false;
        bool wasTapped = ui.touchable(id, rect, &pressed);
        if (tapped != nullptr) {
            *tapped = wasTapped;
        }

        muiFillRoundRect(rect, ui.dp(6.0f), pressed ? theme.buttonPressed : kListBackground);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), pressed ? theme.accent : theme.panelBorder);

        float cursor = rect.x + padding;
        if (hasIcon) {
            muiDrawIcon(icon, cursor + iconSize / 2.0f, rect.centerY(), iconSize, color);
            cursor += iconSize + ui.dp(5.0f);
        }
        muiDrawTextAligned(text, { cursor, rect.y, rect.right() - cursor, rect.h }, size, color, MuiAlign::Start, MuiAlign::Center);
        return width;
    }

    // The header's chips and buttons at the roomy or the compact sizes, as
    // `buildHeader` lays them out.
    float CharacterScreen::headerWidth(MuiContext& ui, float height, bool roomy)
    {
        float size = ui.dp(roomy ? 16.0f : 14.0f);
        float padding = ui.dp(roomy ? 14.0f : 9.0f);
        float iconSize = ui.dp(roomy ? 18.0f : 15.0f);
        float gap = ui.dp(roomy ? 8.0f : 6.0f);
        float buttonWidth = ui.dp(roomy ? 130.0f : 110.0f);
        auto chipWidth = [&](const std::u32string& text, bool hasIcon) {
            return padding * 2.0f + muiTextWidth(text, size) + (hasIcon ? iconSize + ui.dp(5.0f) : 0.0f) + gap;
        };

        if (creating) {
            std::u32string nameText = gameText(characterEditorNameIsDefault() ? characterEditorGetText(kEditorName) : critterGetName(gDude));
            std::u32string age = gameText(statGetName(STAT_AGE)) + U" " + number(critterGetStat(gDude, STAT_AGE));
            int gender = critterGetStat(gDude, STAT_GENDER);
            return chipWidth(nameText, true) + height * 2.0f + ui.dp(8.0f) + chipWidth(age, false)
                + (appearance ? 0.0f : chipWidth(editorText(kEditorMale + gender), false))
                + chipWidth(editorText(kEditorCharacterPoints) + U" " + number(characterEditorGetCharacterPoints()), false)
                + buttonWidth;
        }

        int hp;
        int maxHp;
        characterEditorGetHitPoints(&hp, &maxHp);
        std::u32string experience = editorText(kEditorExperience) + U" " + number(pcGetStat(PC_STAT_EXPERIENCE));
        int next = pcGetExperienceForNextLevel();
        if (next != -1) {
            experience += U" / " + number(next);
        }
        float width = chipWidth(gameText(critterGetName(gDude)), false)
            + chipWidth(editorText(kEditorLevel) + U" " + number(pcGetStat(PC_STAT_LEVEL)), false)
            + chipWidth(number(hp) + U"/" + number(maxHp), true)
            + chipWidth(experience, false);
        if (skillPointsInPlay()) {
            width += chipWidth(editorText(kEditorSkillPoints) + U" " + number(pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS)), false);
        }
        if (characterEditorHasChanges()) {
            width += buttonWidth;
        }
        return width;
    }

    void CharacterScreen::buildHeader(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        // Tall: the mockup's sizes while everything fits the width.
        roomyHeader = tall && headerWidth(ui, rect.h, true) <= rect.w;
        float gap = ui.dp(roomyHeader ? 8.0f : 6.0f);
        float x = rect.x;
        bool tapped;

        if (creating) {
            // Name (tap edits), age, gender, character points; done at the
            // right.
            std::u32string nameText = gameText(characterEditorNameIsDefault() ? characterEditorGetText(kEditorName) : critterGetName(gDude));
            x += chip(ui, "character.name", x, rect, nameText, theme.accent, MuiIcon::Edit, true, &tapped) + gap;
            if (tapped) {
                _gsound_red_butt_press(-1, 0);
                nameOpen = true;
                name = characterEditorNameIsDefault() ? std::u32string() : gameText(critterGetName(gDude));
                ui.takeTextInput();
                beginTextInput();
            }

            // Age stepper.
            float stepper = rect.h;
            if (smallButton(ui, "character.age.minus", { x, rect.y, stepper, rect.h }, MuiIcon::Minus, true)) {
                if (!characterEditorAdjustAge(-1)) {
                    soundPlayFile("iisxxxx1");
                }
            }
            x += stepper + ui.dp(4.0f);
            std::u32string age = gameText(statGetName(STAT_AGE)) + U" " + number(critterGetStat(gDude, STAT_AGE));
            x += chip(ui, "character.age", x, rect, age, theme.text, MuiIcon::Edit, false, nullptr) + ui.dp(4.0f);
            if (smallButton(ui, "character.age.plus", { x, rect.y, stepper, rect.h }, MuiIcon::Plus, true)) {
                if (!characterEditorAdjustAge(1)) {
                    soundPlayFile("iisxxxx1");
                }
            }
            x += stepper + gap;

            // Gender toggles (on the appearance tab when there is one).
            if (!appearance) {
                int gender = critterGetStat(gDude, STAT_GENDER);
                x += chip(ui, "character.gender", x, rect, editorText(kEditorMale + gender), theme.text, MuiIcon::Edit, false, &tapped) + gap;
                if (tapped) {
                    _gsound_red_butt_press(-1, 0);
                    characterEditorSetGender(gender == 0 ? 1 : 0);
                }
            }

            int points = characterEditorGetCharacterPoints();
            chip(ui, "character.points", x, rect, editorText(kEditorCharacterPoints) + U" " + number(points), points != 0 ? kNotice : theme.textDim, MuiIcon::Edit, false, &tapped);
            if (tapped) {
                select(Item::Level, 0);
            }

            float doneWidth = ui.dp(roomyHeader ? 130.0f : 110.0f);
            MuiRect done = { rect.right() - doneWidth, rect.y, doneWidth, rect.h };
            if (textButton(ui, "character.done", done, ceText(kTextDone, "DONE"), true, true)) {
                pending = Pending::Done;
            }
            return;
        }

        x += chip(ui, "character.name", x, rect, gameText(critterGetName(gDude)), theme.accent, MuiIcon::Edit, false, nullptr) + gap;

        x += chip(ui, "character.level", x, rect, editorText(kEditorLevel) + U" " + number(pcGetStat(PC_STAT_LEVEL)), theme.text, MuiIcon::Edit, false, &tapped) + gap;
        if (tapped) {
            select(Item::Level, 0);
        }

        int hp;
        int maxHp;
        characterEditorGetHitPoints(&hp, &maxHp);
        x += chip(ui, "character.hp", x, rect, number(hp) + U"/" + number(maxHp), theme.text, MuiIcon::HitPoints, true, &tapped) + gap;
        if (tapped) {
            select(Item::HitPoints, 0);
        }

        std::u32string experience = editorText(kEditorExperience) + U" " + number(pcGetStat(PC_STAT_EXPERIENCE));
        int next = pcGetExperienceForNextLevel();
        if (next != -1) {
            experience += U" / " + number(next);
        }
        x += chip(ui, "character.experience", x, rect, experience, theme.text, MuiIcon::Edit, false, &tapped) + gap;
        if (tapped) {
            select(Item::Level, 1);
        }

        if (skillPointsInPlay()) {
            int skillPoints = pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS);
            chip(ui, "character.skillpoints", x, rect, editorText(kEditorSkillPoints) + U" " + number(skillPoints), skillPoints != 0 ? kNotice : theme.textDim, MuiIcon::Edit, false, &tapped);
            if (tapped) {
                select(Item::SkillPoints, 0);
            }
        }

        // Puts back what was changed here (the game's Cancel).
        if (characterEditorHasChanges()) {
            float resetWidth = ui.dp(roomyHeader ? 130.0f : 110.0f);
            MuiRect reset = { rect.right() - resetWidth, rect.y, resetWidth, rect.h };
            if (textButton(ui, "character.reset", reset, ceText(kTextReset, "Reset"), false, true)) {
                _gsound_red_butt_press(-1, 0);
                characterEditorRevert();
                perkStep = PerkStep::Closed;
            }
        }
    }

    // Tabs at the right edge: back and the other game screens (in game).
    void CharacterScreen::buildTabs(MuiContext& ui, const MuiRect& column)
    {
        MuiGameScreenTab tab = muiGameScreenTabs(ui, column, "character.nav", MuiGameScreenTab::Character, !creating);
        if (tab == MuiGameScreenTab::Back) {
            back();
        } else if (tab != MuiGameScreenTab::None) {
            // The game opens it once this screen is closed.
            if (muiSwitchGameScreen(ui, tab)) {
                finish(0);
            }
        }
    }

    void CharacterScreen::buildMain(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        // Tall: the tabs over the panel (the window's mockup), else in it.
        MuiRect inner;
        MuiRect panelRect;
        float tabHeight;
        float gap;
        if (tall) {
            tabHeight = ui.dp(44.0f);
            gap = ui.dp(8.0f);
            inner = rect;
            panelRect = { rect.x, rect.y + tabHeight + ui.dp(8.0f), rect.w, rect.h - tabHeight - ui.dp(8.0f) };
            ui.panel(panelRect);
        } else {
            ui.panel(rect);
            inner = rect.inset(ui.dp(6.0f));
            tabHeight = ui.dp(34.0f);
            gap = ui.dp(4.0f);
        }

        struct TabInfo {
            Tab tab;
            const char* id;
            std::u32string label;
        };

        std::u32string skillsLabel = editorText(kEditorSkills);
        if (creating && characterEditorGetTagSkillsLeft() > 0) {
            skillsLabel += U" " + number(characterEditorGetTagSkillsLeft());
        }

        std::u32string perksLabel = creating
            ? ceText(kTextTraits, "Traits") + U" " + number(2 - characterEditorGetTraitsLeft()) + U"/2"
            : editorText(kEditorPerks);
        // Owed perk: dot on the perks tab (as the notice there, only with a
        // perk to choose).
        bool perkOwed = !creating && characterEditorGetPerkOwed() != 0 && !characterEditorGetAvailablePerks().empty();

        std::vector<TabInfo> tabs = {
            { Tab::Stats, "character.tab.stats", ceText(kTextStats, "Stats") },
            { Tab::Skills, "character.tab.skills", skillsLabel },
            { Tab::Perks, "character.tab.perks", perksLabel },
        };
        if (appearance) {
            tabs.push_back({ Tab::Appearance, "character.tab.appearance", ceText(kTextAppearance, "Appearance") });
        }

        // The cells by the widest each label gets: creation's counters at
        // their widest digits.
        float textSize = ui.dp(tall ? 16.0f : 14.0f);
        auto widest = [textSize](const std::u32string& text, const std::vector<std::u32string>& counters) {
            std::u32string result;
            for (const std::u32string& counter : counters) {
                std::u32string candidate = text + U" " + counter;
                if (muiTextWidth(candidate, textSize) > muiTextWidth(result, textSize)) {
                    result = candidate;
                }
            }
            return result;
        };
        std::vector<std::u32string> widestLabels;
        for (const TabInfo& info : tabs) {
            widestLabels.push_back(info.label);
        }
        if (creating) {
            widestLabels[1] = widest(editorText(kEditorSkills), { U"1", U"2", U"3" });
            widestLabels[2] = widest(ceText(kTextTraits, "Traits"), { U"0/2", U"1/2", U"2/2" });
        }
        MuiTabRow tabRow = muiLayoutTabRow({ inner.x, inner.y, inner.w, tabHeight }, widestLabels, gap, ui.dp(10.0f), textSize, ui.dp(10.0f));

        for (size_t index = 0; index < tabs.size(); index++) {
            const TabInfo& info = tabs[index];
            const MuiRect& tabRect = tabRow.cells[index];
            bool active = info.tab == tab;
            bool pressed;
            bool tapped = ui.touchable(info.id, tabRect, &pressed);
            muiFillRoundRect(tabRect, ui.dp(6.0f), pressed ? theme.buttonPressed : (active ? theme.buttonPrimary : theme.button));
            muiStrokeRoundRect(tabRect, ui.dp(6.0f), ui.dp(1.0f), active || pressed ? theme.accent : theme.buttonBorder);
            float size = muiFitTextSize(info.label, tabRect.w - ui.dp(10.0f), tabRow.textSize, ui.dp(10.0f));
            muiDrawTextAligned(info.label, tabRect, size, active && !pressed ? theme.buttonPrimaryText : theme.buttonText, MuiAlign::Center, MuiAlign::Center);
            if (info.tab == Tab::Perks && perkOwed) {
                float dotInset = ui.dp(tall ? 12.0f : 9.0f);
                muiFillCircle(tabRect.right() - dotInset, tabRect.y + dotInset, ui.dp(tall ? 4.5f : 4.0f), kNotice);
            }

            if (tapped && !active) {
                _gsound_red_butt_press(-1, 0);
                tab = info.tab;
                switch (tab) {
                case Tab::Stats:
                    select(Item::PrimaryStat, STAT_STRENGTH);
                    break;
                case Tab::Skills:
                    // Points to spend (tag skills): their card, else the first skill.
                    if (creating || skillPointsInPlay()) {
                        select(Item::SkillPoints, 0);
                    } else {
                        select(Item::Skill, SKILL_SMALL_GUNS);
                    }
                    break;
                case Tab::Perks:
                    select(Item::Folder, 0);
                    break;
                case Tab::Appearance:
                    select(Item::Appearance, 1);
                    break;
                }
            }
        }

        MuiRect area = tall ? panelRect.inset(ui.dp(8.0f)) : MuiRect { inner.x, inner.y + tabHeight + ui.dp(6.0f), inner.w, inner.bottom() - inner.y - tabHeight - ui.dp(6.0f) };
        switch (tab) {
        case Tab::Stats:
            if (tall) {
                buildStatsTall(ui, area);
            } else {
                buildStats(ui, area);
            }
            break;
        case Tab::Skills:
            // Tall: the list scrolls under the panel's edges.
            buildSkills(ui, tall ? panelRect.inset(ui.dp(8.0f), ui.dp(2.0f)) : area);
            break;
        case Tab::Perks:
            if (creating) {
                buildTraits(ui, tall ? panelRect.inset(ui.dp(8.0f), ui.dp(2.0f)) : area);
            } else if (tall) {
                buildFolder(ui, area);
            } else {
                // Own frame: its sides and bottom are the panel's.
                MuiRect frame = { rect.x, area.y, rect.w, rect.bottom() - area.y };
                muiStrokeRoundRect(frame, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.panelBorder);
                buildFolder(ui, frame.inset(ui.dp(6.0f)));
            }
            break;
        case Tab::Appearance:
            buildAppearance(ui, area);
            break;
        }
    }

    // SPECIAL with conditions under it, derived stats next to them.
    void CharacterScreen::buildStats(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(8.0f);
        MuiRect left = { rect.x, rect.y, (rect.w - gap) * 0.54f, rect.h };
        MuiRect right = { left.right() + gap, rect.y, rect.right() - left.right() - gap, rect.h };

        float rowHeight = std::clamp(rect.h / kCharacterEditorDerivedStatCount, ui.dp(16.0f), ui.dp(30.0f));
        float size = ui.dp(14.0f);
        float padding = ui.dp(6.0f);

        // Creation: SPECIAL rows share the column alone (bigger steppers).
        float statHeight = creating ? std::min(left.h / PRIMARY_STAT_COUNT, ui.dp(40.0f)) : rowHeight;

        for (int stat = 0; stat < PRIMARY_STAT_COUNT; stat++) {
            MuiRect rowRect = { left.x, left.y + stat * statHeight, left.w, statHeight };
            std::string id = "character.special." + std::to_string(stat);
            float stepperWidth = (statHeight - ui.dp(4.0f)) * 2.0f + ui.dp(30.0f);
            bool pressed;
            if (row(ui, id, rowRect, item == Item::PrimaryStat && itemIndex == stat, &pressed, creating ? rowRect.right() - stepperWidth : 0.0f)) {
                select(Item::PrimaryStat, stat);
            }

            std::u32string label = gameText(statGetName(static_cast<Stat>(stat)));
            int value = characterEditorGetPrimaryStat(static_cast<Stat>(stat));

            if (creating) {
                // - value +
                float button = statHeight - ui.dp(4.0f);
                MuiRect plus = { rowRect.right() - button, rowRect.y + ui.dp(2.0f), button, button };
                MuiRect valueRect = { plus.x - ui.dp(30.0f), rowRect.y, ui.dp(30.0f), rowRect.h };
                MuiRect minus = { valueRect.x - button, plus.y, button, button };

                float labelSize = muiFitTextSize(label, minus.x - rowRect.x - padding * 2.0f, size, ui.dp(10.0f));
                muiDrawTextAligned(label, { rowRect.x + padding, rowRect.y, minus.x - rowRect.x, rowRect.h }, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
                muiDrawTextAligned(number(value), valueRect, size, value > 10 ? kWarning : theme.accent, MuiAlign::Center, MuiAlign::Center);

                if (smallButton(ui, id + ".minus", minus, MuiIcon::Minus, value > 1)) {
                    select(Item::PrimaryStat, stat);
                    if (characterEditorAdjustPrimaryStat(static_cast<Stat>(stat), -1)) {
                        _gsound_red_butt_press(-1, 0);
                    } else {
                        soundPlayFile("iisxxxx1");
                    }
                }
                if (smallButton(ui, id + ".plus", plus, MuiIcon::Plus, characterEditorGetCharacterPoints() > 0 && value < 10)) {
                    select(Item::PrimaryStat, stat);
                    if (characterEditorAdjustPrimaryStat(static_cast<Stat>(stat), 1)) {
                        _gsound_red_butt_press(-1, 0);
                    } else {
                        soundPlayFile("iisxxxx1");
                    }
                }
                continue;
            }

            // Name, value, its description.
            std::u32string description = gameText(characterEditorGetPrimaryStatDescription(static_cast<Stat>(stat)));
            float descriptionSize = ui.dp(11.5f);
            float descriptionWidth = muiTextWidth(description, descriptionSize);
            MuiRect descriptionRect = { rowRect.right() - padding - descriptionWidth, rowRect.y, descriptionWidth, rowRect.h };
            MuiRect valueRect = { descriptionRect.x - ui.dp(28.0f), rowRect.y, ui.dp(22.0f), rowRect.h };

            float labelSize = muiFitTextSize(label, valueRect.x - rowRect.x - padding * 2.0f, size, ui.dp(10.0f));
            muiDrawTextAligned(label, { rowRect.x + padding, rowRect.y, valueRect.x - rowRect.x, rowRect.h }, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(number(value), valueRect, size, theme.accent, MuiAlign::End, MuiAlign::Center);
            muiDrawTextAligned(description, descriptionRect, descriptionSize, theme.textDim, MuiAlign::End, MuiAlign::Center);
        }

        // Conditions: only those the character has (in game).
        if (!creating) {
            float chipSize = ui.dp(12.0f);
            float chipHeight = ui.dp(24.0f);
            float x = left.x + ui.dp(4.0f);
            float y = left.y + PRIMARY_STAT_COUNT * rowHeight + ui.dp(8.0f);
            bool any = false;
            for (int condition = 0; condition < kCharacterEditorConditionCount; condition++) {
                if (!characterEditorConditionIsActive(condition)) {
                    continue;
                }

                any = true;
                std::u32string text = gameText(characterEditorGetConditionName(condition));
                float width = muiTextWidth(text, chipSize) + ui.dp(14.0f);
                if (x + width > left.right() && x > left.x + ui.dp(4.0f)) {
                    x = left.x + ui.dp(4.0f);
                    y += chipHeight + ui.dp(4.0f);
                }

                MuiRect chipRect = { x, y, std::min(width, left.w - ui.dp(8.0f)), chipHeight };
                bool selected = item == Item::Condition && itemIndex == condition;
                bool pressed;
                if (ui.touchable("character.condition." + std::to_string(condition), chipRect, &pressed)) {
                    select(Item::Condition, condition);
                }
                muiFillRoundRect(chipRect, ui.dp(5.0f), pressed || selected ? kSelected : kListBackground);
                muiStrokeRoundRect(chipRect, ui.dp(5.0f), ui.dp(1.0f), kWarning);
                muiDrawTextAligned(text, chipRect, chipSize, kWarning, MuiAlign::Center, MuiAlign::Center);
                x += chipRect.w + ui.dp(4.0f);
            }

            if (!any) {
                std::u32string text = ceText(kTextNoConditions, "No conditions");
                MuiRect textRect = { left.x + padding, y, left.w - padding, chipHeight };
                muiDrawTextAligned(text, textRect, chipSize, theme.textDim, MuiAlign::Start, MuiAlign::Center);
            }
        }

        for (int index = 0; index < kCharacterEditorDerivedStatCount; index++) {
            MuiRect rowRect = { right.x, right.y + index * rowHeight, right.w, rowHeight };
            bool pressed;
            if (row(ui, "character.derived." + std::to_string(index), rowRect, item == Item::DerivedStat && itemIndex == index, &pressed)) {
                select(Item::DerivedStat, index);
            }

            bool warning;
            std::u32string value = gameText(characterEditorFormatDerivedStat(index, &warning).c_str());
            float valueWidth = muiTextWidth(value, size);
            std::u32string label = gameText(characterEditorGetDerivedStatName(index));
            float labelSize = muiFitTextSize(label, rowRect.w - valueWidth - padding * 3.0f, size, ui.dp(10.0f));
            muiDrawTextAligned(label, { rowRect.x + padding, rowRect.y, rowRect.w, rowRect.h }, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(value, { rowRect.x, rowRect.y, rowRect.w - padding, rowRect.h }, size, warning ? kWarning : theme.accent, MuiAlign::End, MuiAlign::Center);
        }
    }

    // Tall layout (the window's mockups): SPECIAL in rows over the whole
    // width (creation: bigger, with the rank and - +; in game: conditions
    // under them), derived stats in two columns under a line. Rows shrink
    // when they don't fit (7" tablets, every condition at once).
    void CharacterScreen::buildStatsTall(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(16.0f);
        float rankSize = ui.dp(14.0f);
        float padding = ui.dp(12.0f);
        float lineGap = ui.dp(10.0f);

        // Conditions laid out first: their height.
        struct ConditionChip {
            int condition;
            std::u32string text;
            MuiRect rect;
        };
        std::vector<ConditionChip> chips;
        float chipSize = ui.dp(13.0f);
        float chipHeight = ui.dp(26.0f);
        float chipGap = ui.dp(6.0f);
        float conditionsHeight = 0.0f;
        if (!creating) {
            float x = 0.0f;
            float y = 0.0f;
            float room = rect.w - ui.dp(8.0f);
            for (int condition = 0; condition < kCharacterEditorConditionCount; condition++) {
                if (!characterEditorConditionIsActive(condition)) {
                    continue;
                }
                std::u32string text = gameText(characterEditorGetConditionName(condition));
                float width = std::min(muiTextWidth(text, chipSize) + ui.dp(20.0f), room);
                if (x + width > room && x > 0.0f) {
                    x = 0.0f;
                    y += chipHeight + chipGap;
                }
                chips.push_back({ condition, text, { x, y, width, chipHeight } });
                x += width + chipGap;
            }
            conditionsHeight = ui.dp(8.0f) + (chips.empty() ? chipHeight : y + chipHeight);
        }

        int derivedRows = (kCharacterEditorDerivedStatCount + 1) / 2;
        float statNominal = ui.dp(creating ? 50.0f : 34.0f);
        float derivedNominal = ui.dp(30.0f);
        float natural = statNominal * PRIMARY_STAT_COUNT + conditionsHeight + lineGap * 2.0f + derivedNominal * derivedRows;
        float scale = std::min(1.0f, (rect.h - conditionsHeight - lineGap * 2.0f) / (natural - conditionsHeight - lineGap * 2.0f));
        float statHeight = std::floor(statNominal * scale);
        float derivedHeight = std::floor(derivedNominal * scale);

        float y = rect.y;
        for (int stat = 0; stat < PRIMARY_STAT_COUNT; stat++) {
            MuiRect rowRect = { rect.x, y, rect.w, statHeight };
            y += statHeight;
            std::string id = "character.special." + std::to_string(stat);
            std::u32string label = gameText(statGetName(static_cast<Stat>(stat)));
            int value = characterEditorGetPrimaryStat(static_cast<Stat>(stat));
            std::u32string rank = gameText(characterEditorGetPrimaryStatDescription(static_cast<Stat>(stat)));

            if (creating) {
                // Name, rank, - value +.
                float buttonHeight = std::min(ui.dp(40.0f), statHeight - ui.dp(8.0f));
                float buttonWidth = ui.dp(44.0f);
                MuiRect plus = { rowRect.right() - ui.dp(6.0f) - buttonWidth, rowRect.centerY() - buttonHeight / 2.0f, buttonWidth, buttonHeight };
                MuiRect valueRect = { plus.x - ui.dp(36.0f), rowRect.y, ui.dp(36.0f), rowRect.h };
                MuiRect minus = { valueRect.x - buttonWidth, plus.y, buttonWidth, buttonHeight };
                MuiRect rankRect = { minus.x - ui.dp(8.0f) - ui.dp(96.0f), rowRect.y, ui.dp(96.0f), rowRect.h };

                bool pressed;
                if (row(ui, id, rowRect, item == Item::PrimaryStat && itemIndex == stat, &pressed, minus.x)) {
                    select(Item::PrimaryStat, stat);
                }
                float labelSize = muiFitTextSize(label, rankRect.x - rowRect.x - padding * 2.0f, size, ui.dp(11.0f));
                muiDrawTextAligned(label, { rowRect.x + padding, rowRect.y, rankRect.x - rowRect.x, rowRect.h }, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
                muiDrawTextAligned(rank, rankRect, muiFitTextSize(rank, rankRect.w, rankSize, ui.dp(10.0f)), theme.textDim, MuiAlign::End, MuiAlign::Center);
                muiDrawTextAligned(number(value), valueRect, size, value > 10 ? kWarning : theme.accent, MuiAlign::Center, MuiAlign::Center);

                if (smallButton(ui, id + ".minus", minus, MuiIcon::Minus, value > 1)) {
                    select(Item::PrimaryStat, stat);
                    if (characterEditorAdjustPrimaryStat(static_cast<Stat>(stat), -1)) {
                        _gsound_red_butt_press(-1, 0);
                    } else {
                        soundPlayFile("iisxxxx1");
                    }
                }
                if (smallButton(ui, id + ".plus", plus, MuiIcon::Plus, characterEditorGetCharacterPoints() > 0 && value < 10)) {
                    select(Item::PrimaryStat, stat);
                    if (characterEditorAdjustPrimaryStat(static_cast<Stat>(stat), 1)) {
                        _gsound_red_butt_press(-1, 0);
                    } else {
                        soundPlayFile("iisxxxx1");
                    }
                }
                continue;
            }

            // Name; value and rank at the right.
            bool pressed;
            if (row(ui, id, rowRect, item == Item::PrimaryStat && itemIndex == stat, &pressed)) {
                select(Item::PrimaryStat, stat);
            }
            float rankWidth = muiTextWidth(rank, rankSize);
            MuiRect rankRect = { rowRect.right() - padding - rankWidth, rowRect.y, rankWidth, rowRect.h };
            MuiRect valueRect = { rankRect.x - ui.dp(8.0f) - ui.dp(30.0f), rowRect.y, ui.dp(30.0f), rowRect.h };
            float labelSize = muiFitTextSize(label, valueRect.x - rowRect.x - padding * 2.0f, size, ui.dp(11.0f));
            muiDrawTextAligned(label, { rowRect.x + padding, rowRect.y, valueRect.x - rowRect.x, rowRect.h }, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(number(value), valueRect, size, theme.accent, MuiAlign::End, MuiAlign::Center);
            muiDrawTextAligned(rank, rankRect, rankSize, theme.textDim, MuiAlign::End, MuiAlign::Center);
        }

        // Conditions the character has, or that there are none.
        if (!creating) {
            float top = y + ui.dp(8.0f);
            float left = rect.x + ui.dp(4.0f);
            if (chips.empty()) {
                std::u32string text = ceText(kTextNoConditions, "No conditions");
                muiDrawTextAligned(text, { rect.x + padding, top, rect.w - padding, chipHeight }, ui.dp(14.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            }
            for (const ConditionChip& chip : chips) {
                MuiRect chipRect = { left + chip.rect.x, top + chip.rect.y, chip.rect.w, chip.rect.h };
                bool selected = item == Item::Condition && itemIndex == chip.condition;
                bool pressed;
                if (ui.touchable("character.condition." + std::to_string(chip.condition), chipRect, &pressed)) {
                    select(Item::Condition, chip.condition);
                }
                muiFillRoundRect(chipRect, ui.dp(5.0f), pressed || selected ? kSelected : kListBackground);
                muiStrokeRoundRect(chipRect, ui.dp(5.0f), ui.dp(1.0f), kWarning);
                muiDrawTextAligned(chip.text, chipRect, chipSize, kWarning, MuiAlign::Center, MuiAlign::Center);
            }
            y += conditionsHeight;
        }

        y += lineGap;
        muiFillRect({ rect.x + ui.dp(4.0f), y, rect.w - ui.dp(8.0f), ui.dp(1.0f) }, theme.panelBorder);
        y += lineGap;

        // Two columns: armor class, action points, carry weight, melee
        // damage, healing rate; resistances, sequence, critical chance.
        static const int kColumns[2][5] = {
            { 0, 1, 2, 3, 8 },
            { 4, 5, 6, 7, 9 },
        };
        float columnGap = ui.dp(10.0f);
        float columnWidth = (rect.w - columnGap) / 2.0f;
        for (int column = 0; column < 2; column++) {
            for (int line = 0; line < derivedRows; line++) {
                int index = kColumns[column][line];
                MuiRect rowRect = { rect.x + column * (columnWidth + columnGap), y + line * derivedHeight, columnWidth, derivedHeight };
                bool pressed;
                if (row(ui, "character.derived." + std::to_string(index), rowRect, item == Item::DerivedStat && itemIndex == index, &pressed)) {
                    select(Item::DerivedStat, index);
                }

                bool warning;
                std::u32string value = gameText(characterEditorFormatDerivedStat(index, &warning).c_str());
                float valueWidth = muiTextWidth(value, size);
                std::u32string label = gameText(characterEditorGetDerivedStatName(index));
                float labelSize = muiFitTextSize(label, rowRect.w - valueWidth - padding * 3.0f, size, ui.dp(11.0f));
                muiDrawTextAligned(label, { rowRect.x + padding, rowRect.y, rowRect.w, rowRect.h }, labelSize, theme.text, MuiAlign::Start, MuiAlign::Center);
                muiDrawTextAligned(value, { rowRect.x, rowRect.y, rowRect.w - padding, rowRect.h }, size, warning ? kWarning : theme.accent, MuiAlign::End, MuiAlign::Center);
            }
        }
    }

    // Skills: tagged marked, - and + for skill points (tag in creation).
    void CharacterScreen::buildSkills(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        // Tall: creation's rows bigger (their own buttons, the mockup).
        float rowHeight = ui.dp(tall && creating ? 52.0f : 42.0f);
        float size = ui.dp(tall ? 16.0f : 15.0f);
        float padding = ui.dp(6.0f);
        float button = ui.dp(36.0f);

        float offset = ui.scroll("character.skills", rect, SKILL_COUNT * rowHeight);
        bool scrolling = ui.isScrolling("character.skills");
        bool steppers = !creating && skillPointsInPlay();

        muiPushClip(rect);
        for (int skill = 0; skill < SKILL_COUNT; skill++) {
            MuiRect rowRect = { rect.x, rect.y - offset + skill * rowHeight, rect.w, rowHeight };
            if (rowRect.bottom() < rect.y || rowRect.y > rect.bottom()) {
                continue;
            }

            std::string id = "character.skills." + std::to_string(skill);
            // Buttons: - and + (tag in creation) at the right.
            float buttonsLeft = rowRect.right();
            if (creating) {
                buttonsLeft = rowRect.right() - padding - ui.dp(92.0f);
            } else if (steppers) {
                buttonsLeft = rowRect.right() - padding - button * 2.0f - ui.dp(62.0f) - ui.dp(4.0f);
            }
            bool pressed;
            if (row(ui, id, rowRect, item == Item::Skill && itemIndex == skill, &pressed, buttonsLeft - ui.dp(4.0f)) && !scrolling) {
                select(Item::Skill, skill);
            }

            bool tagged = characterEditorIsSkillTagged(static_cast<Skill>(skill));
            float dot = ui.dp(5.0f);
            float dotX = rowRect.x + padding + dot;
            if (tagged) {
                muiFillCircle(dotX, rowRect.centerY(), dot, theme.accent);
            } else {
                muiStrokeCircle(dotX, rowRect.centerY(), dot, ui.dp(1.2f), theme.textDim);
            }

            float right = rowRect.right() - padding;
            MuiRect valueRect;
            if (creating) {
                float tagWidth = ui.dp(92.0f);
                MuiRect tagRect = { right - tagWidth, rowRect.y + (rowHeight - button) / 2.0f, tagWidth, button };
                valueRect = { tagRect.x - ui.dp(56.0f), rowRect.y, ui.dp(50.0f), rowRect.h };
                std::u32string label = tagged ? ceText(kTextUntag, "Untag") : ceText(kTextTag, "Tag");
                if (textButton(ui, id + ".tag", tagRect, label, false, true) && !scrolling) {
                    select(Item::Skill, skill);
                    if (characterEditorToggleTagSkill(static_cast<Skill>(skill))) {
                        _gsound_red_butt_press(-1, 0);
                    } else {
                        showMistake(messagePair(kEditorTagsUsed));
                    }
                }
            } else if (!steppers) {
                valueRect = { right - ui.dp(52.0f), rowRect.y, ui.dp(52.0f), rowRect.h };
            } else {
                MuiRect plus = { right - button, rowRect.y + (rowHeight - button) / 2.0f, button, button };
                valueRect = { plus.x - ui.dp(58.0f), rowRect.y, ui.dp(52.0f), rowRect.h };
                MuiRect minus = { valueRect.x - button - ui.dp(4.0f), plus.y, button, button };

                if (smallButton(ui, id + ".minus", minus, MuiIcon::Minus, characterEditorCanLowerSkill(static_cast<Skill>(skill))) && !scrolling) {
                    select(Item::Skill, skill);
                    if (characterEditorAdjustSkill(static_cast<Skill>(skill), -1) == CharacterEditorSkillChange::Changed) {
                        _gsound_red_butt_press(-1, 0);
                    } else {
                        showMistake(characterEditorGetText(kEditorAtMinimum));
                    }
                }

                if (smallButton(ui, id + ".plus", plus, MuiIcon::Plus, pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS) > 0) && !scrolling) {
                    select(Item::Skill, skill);
                    switch (characterEditorAdjustSkill(static_cast<Skill>(skill), 1)) {
                    case CharacterEditorSkillChange::Changed:
                        _gsound_red_butt_press(-1, 0);
                        break;
                    case CharacterEditorSkillChange::NoPoints:
                        showMistake(characterEditorGetText(kEditorNoSkillPoints));
                        break;
                    default:
                        showMistake(characterEditorGetText(kEditorAtMaximum));
                        break;
                    }
                }
            }

            std::u32string label = gameText(skillGetName(static_cast<Skill>(skill)));
            float labelX = dotX + dot + ui.dp(10.0f);
            float labelWidth = valueRect.x - labelX - (steppers ? button + ui.dp(8.0f) : 0.0f);
            float labelSize = muiFitTextSize(label, labelWidth, size, ui.dp(10.0f));
            muiDrawTextAligned(label, { labelX, rowRect.y, labelWidth, rowRect.h }, labelSize, tagged ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
            muiDrawTextAligned(number(skillGetValue(gDude, static_cast<Skill>(skill))) + U"%", valueRect, size, theme.accent, MuiAlign::End, MuiAlign::Center);
        }
        muiPopClip();
    }

    // Perks, karma, kills (tabs of their own), owed perk notice.
    void CharacterScreen::buildFolder(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float tabHeight = ui.dp(tall ? 40.0f : 32.0f);
        float gap = ui.dp(tall ? 6.0f : 4.0f);

        const struct {
            Folder folder;
            const char* id;
            int label;
        } tabs[] = {
            { Folder::Perks, "character.folder.perks", kEditorPerks },
            { Folder::Karma, "character.folder.karma", kEditorKarma },
            { Folder::Kills, "character.folder.kills", kEditorKills },
        };

        std::vector<std::u32string> labels;
        for (const auto& info : tabs) {
            labels.push_back(editorText(info.label));
        }
        MuiTabRow tabRow = muiLayoutTabRow({ rect.x, rect.y, rect.w, tabHeight }, labels, gap, ui.dp(8.0f), ui.dp(tall ? 15.0f : 13.0f), ui.dp(10.0f));

        for (int index = 0; index < 3; index++) {
            const MuiRect& tabRect = tabRow.cells[index];
            bool active = tabs[index].folder == folder;
            bool pressed;
            bool tapped = ui.touchable(tabs[index].id, tabRect, &pressed);
            muiFillRoundRect(tabRect, ui.dp(6.0f), pressed ? theme.buttonPressed : (active ? kSelected : theme.button));
            muiStrokeRoundRect(tabRect, ui.dp(6.0f), ui.dp(1.0f), active || pressed ? theme.accent : theme.buttonBorder);
            const std::u32string& label = labels[index];
            float size = muiFitTextSize(label, tabRect.w - ui.dp(8.0f), tabRow.textSize, ui.dp(10.0f));
            muiDrawTextAligned(label, tabRect, size, active ? theme.accent : theme.buttonText, MuiAlign::Center, MuiAlign::Center);
            if (tapped && !active) {
                _gsound_red_butt_press(-1, 0);
                folder = tabs[index].folder;
                select(Item::Folder, 0);
                ui.setScroll("character.folder", 0.0f);
            }
        }

        float listGap = ui.dp(tall ? 8.0f : 6.0f);
        MuiRect list = { rect.x, rect.y + tabHeight + listGap, rect.w, rect.bottom() - rect.y - tabHeight - listGap };

        // Owed perk: notice opening the perk panel.
        bool owed = folder == Folder::Perks && characterEditorGetPerkOwed() != 0 && !characterEditorGetAvailablePerks().empty();
        if (owed) {
            MuiRect notice = { list.x, list.y, list.w, ui.dp(tall ? 40.0f : 36.0f) };
            bool pressed;
            if (ui.touchable("character.perks.owed", notice, &pressed)) {
                _gsound_red_butt_press(-1, 0);
                openPerkPanel();
            }
            muiFillRoundRect(notice, ui.dp(6.0f), pressed ? theme.buttonPressed : kListBackground);
            muiStrokeRoundRect(notice, ui.dp(6.0f), ui.dp(1.2f), kNotice);
            std::u32string text = ceText(kTextNewPerk, "New perk available");
            float size = muiFitTextSize(text, notice.w - ui.dp(16.0f), ui.dp(tall ? 16.0f : 14.0f), ui.dp(10.0f));
            muiDrawTextAligned(text, notice.inset(ui.dp(tall ? 12.0f : 8.0f), 0.0f), size, kNotice, MuiAlign::Start, MuiAlign::Center);
            list.y += notice.h + listGap;
            list.h -= notice.h + listGap;
        }

        std::vector<CharacterEditorFolderEntry> entries = characterEditorGetFolder(static_cast<CharacterEditorFolder>(folder));

        float rowHeight = ui.dp(tall ? 48.0f : 34.0f);
        float headingHeight = ui.dp(tall ? 34.0f : 28.0f);
        float height = 0.0f;
        for (const CharacterEditorFolderEntry& entry : entries) {
            height += entry.heading ? headingHeight : rowHeight;
        }

        float offset = ui.scroll("character.folder", list, height);
        bool scrolling = ui.isScrolling("character.folder");
        float size = ui.dp(tall ? 16.0f : 14.0f);
        float padding = ui.dp(tall ? 14.0f : 8.0f);

        muiPushClip(list);
        float y = list.y - offset;
        for (int index = 0; index < static_cast<int>(entries.size()); index++) {
            const CharacterEditorFolderEntry& entry = entries[index];
            float entryHeight = entry.heading ? headingHeight : rowHeight;
            MuiRect rowRect = { list.x, y, list.w, entryHeight };
            y += entryHeight;
            if (rowRect.bottom() < list.y || rowRect.y > list.bottom()) {
                continue;
            }

            std::u32string text = gameText(entry.text.c_str());
            if (entry.heading) {
                bool pressed = false;
                if (entry.hasCard && row(ui, "character.folder." + std::to_string(index), rowRect, item == Item::FolderEntry && itemIndex == index, &pressed) && !scrolling) {
                    select(Item::FolderEntry, index);
                }
                muiDrawTextAligned(text, { rowRect.x + padding, rowRect.y, rowRect.w, rowRect.h }, ui.dp(12.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
                continue;
            }

            bool pressed;
            if (row(ui, "character.folder." + std::to_string(index), rowRect, item == Item::FolderEntry && itemIndex == index, &pressed) && !scrolling) {
                select(Item::FolderEntry, index);
            }

            // Rank of perks with several, kills count.
            std::u32string detail;
            if (entry.count != -1) {
                detail = number(entry.count);
            } else if (entry.maxRank > 1) {
                detail = number(entry.rank) + U"/" + number(entry.maxRank);
            }

            float detailWidth = detail.empty() ? 0.0f : muiTextWidth(detail, size) + padding;
            float textSize = muiFitTextSize(text, rowRect.w - padding * 2.0f - detailWidth, size, ui.dp(10.0f));
            muiDrawTextAligned(text, { rowRect.x + padding, rowRect.y, rowRect.w, rowRect.h }, textSize, theme.text, MuiAlign::Start, MuiAlign::Center);
            if (!detail.empty()) {
                muiDrawTextAligned(detail, { rowRect.x, rowRect.y, rowRect.w - padding, rowRect.h }, size, theme.accent, MuiAlign::End, MuiAlign::Center);
            }
        }
        muiPopClip();
    }

    // Creation: traits to take (up to two).
    void CharacterScreen::buildTraits(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float rowHeight = ui.dp(tall ? 52.0f : 42.0f);
        float size = ui.dp(tall ? 16.0f : 15.0f);
        float padding = ui.dp(6.0f);
        float button = ui.dp(36.0f);

        float offset = ui.scroll("character.traits", rect, TRAIT_COUNT * rowHeight);
        bool scrolling = ui.isScrolling("character.traits");

        muiPushClip(rect);
        for (int trait = 0; trait < TRAIT_COUNT; trait++) {
            MuiRect rowRect = { rect.x, rect.y - offset + trait * rowHeight, rect.w, rowHeight };
            if (rowRect.bottom() < rect.y || rowRect.y > rect.bottom()) {
                continue;
            }

            std::string id = "character.traits." + std::to_string(trait);
            bool pressed;
            if (row(ui, id, rowRect, item == Item::Trait && itemIndex == trait, &pressed, rowRect.right() - padding - ui.dp(96.0f)) && !scrolling) {
                select(Item::Trait, trait);
            }

            bool selected = characterEditorIsTraitSelected(static_cast<Trait>(trait));
            float dot = ui.dp(5.0f);
            float dotX = rowRect.x + padding + dot;
            if (selected) {
                muiFillCircle(dotX, rowRect.centerY(), dot, theme.accent);
            } else {
                muiStrokeCircle(dotX, rowRect.centerY(), dot, ui.dp(1.2f), theme.textDim);
            }

            float buttonWidth = ui.dp(92.0f);
            MuiRect buttonRect = { rowRect.right() - padding - buttonWidth, rowRect.y + (rowHeight - button) / 2.0f, buttonWidth, button };
            std::u32string label = selected ? ceText(kTextUntag, "Untag") : ceText(kTextTake, "Take");
            if (textButton(ui, id + ".toggle", buttonRect, label, false, true) && !scrolling) {
                select(Item::Trait, trait);
                if (characterEditorToggleTrait(static_cast<Trait>(trait))) {
                    _gsound_red_butt_press(-1, 0);
                } else {
                    showMistake(messagePair(kEditorTraitsUsed));
                }
            }

            std::u32string text = gameText(traitGetName(static_cast<Trait>(trait)));
            float labelX = dotX + dot + ui.dp(10.0f);
            float labelWidth = buttonRect.x - labelX - padding;
            muiDrawTextAligned(text, { labelX, rowRect.y, labelWidth, rowRect.h }, muiFitTextSize(text, labelWidth, size, ui.dp(10.0f)), selected ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
        }
        muiPopClip();
    }

    // Creation's appearance (sfall's Hero Appearance): gender, race and the
    // race's styles in columns, rows as the traits'. A row chosen shows its
    // card, the hero turning in it.
    void CharacterScreen::buildAppearance(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        float rowHeight = ui.dp(tall ? 52.0f : 42.0f);
        float headingHeight = ui.dp(tall ? 28.0f : 22.0f);
        float size = ui.dp(tall ? 16.0f : 15.0f);
        float headingSize = ui.dp(tall ? 13.0f : 12.0f);
        float padding = ui.dp(6.0f);
        float gap = ui.dp(tall ? 12.0f : 8.0f);

        int gender = critterGetStat(gDude, STAT_GENDER);
        int race = heroAppearanceRace();
        int style = heroAppearanceStyle();

        // A heading (kind -1) or a gender (0), race (1), style (2) row.
        struct Line {
            std::u32string text;
            int kind;
            int value;
        };

        std::vector<Line> lists[3];
        lists[0].push_back({ ceText(kTextGender, "Gender"), -1, 0 });
        for (int value = 0; value < 2; value++) {
            lists[0].push_back({ editorText(kEditorMaleWord + value), 0, value });
        }

        lists[1].push_back({ ceText(kTextRace, "Race"), -1, 0 });
        int raceCount = heroAppearanceRaceCount(gender);
        for (int value = 0; value < raceCount; value++) {
            std::string title;
            std::string description;
            heroAppearanceGetRaceText(gender, value, &title, &description);
            lists[1].push_back({ muiHeroAppearanceRowName(title, value), 1, value });
        }

        lists[2].push_back({ ceText(kTextStyle, "Style"), -1, 0 });
        int styleCount = heroAppearanceStyleCount(gender, race);
        for (int value = 0; value < styleCount; value++) {
            std::string title;
            std::string description;
            heroAppearanceGetStyleText(gender, race, value, &title, &description);
            lists[2].push_back({ muiHeroAppearanceRowName(title, value), 2, value });
        }

        float columnWidth = (rect.w - gap * 2.0f) / 3.0f;
        const char* kinds[] = { "gender", "race", "style" };

        for (int column = 0; column < 3; column++) {
            const MuiRect area = { rect.x + column * (columnWidth + gap), rect.y, columnWidth, rect.h };
            const std::vector<Line>& lines = lists[column];

            float contentHeight = 0.0f;
            for (const Line& line : lines) {
                contentHeight += line.kind == -1 ? headingHeight : rowHeight;
            }
            std::string scrollId = "character.appearance." + std::to_string(column);
            float offset = ui.scroll(scrollId, area, contentHeight);
            bool scrolling = ui.isScrolling(scrollId);

            muiPushClip(area);
            float y = area.y - offset;
            for (const Line& line : lines) {
                MuiRect rowRect = { area.x, y, area.w, line.kind == -1 ? headingHeight : rowHeight };
                y += rowRect.h;
                if (rowRect.bottom() < area.y || rowRect.y > area.bottom()) {
                    continue;
                }

                if (line.kind == -1) {
                    muiDrawTextAligned(line.text, { rowRect.x + padding, rowRect.y, rowRect.w - padding * 2.0f, rowRect.h }, headingSize, theme.textDim, MuiAlign::Start, MuiAlign::End);
                    continue;
                }

                bool chosen = line.value == (line.kind == 0 ? gender : (line.kind == 1 ? race : style));
                std::string id = std::string("character.appearance.") + kinds[line.kind] + "." + std::to_string(line.value);
                bool pressed;
                if (row(ui, id, rowRect, item == Item::Appearance && itemIndex == line.kind && chosen, &pressed) && !scrolling) {
                    if (!chosen) {
                        _gsound_red_butt_press(-1, 0);
                        if (line.kind == 0) {
                            characterEditorSetGender(line.value);
                        } else if (line.kind == 1) {
                            heroAppearanceChoose(line.value, 0);
                        } else {
                            heroAppearanceChoose(race, line.value);
                        }
                    }
                    select(Item::Appearance, line.kind);
                }

                float dot = ui.dp(5.0f);
                float dotX = rowRect.x + padding + dot;
                if (chosen) {
                    muiFillCircle(dotX, rowRect.centerY(), dot, theme.accent);
                } else {
                    muiStrokeCircle(dotX, rowRect.centerY(), dot, ui.dp(1.2f), theme.textDim);
                }

                float labelX = dotX + dot + ui.dp(10.0f);
                float labelWidth = rowRect.right() - labelX - padding;
                muiDrawTextAligned(line.text, { labelX, rowRect.y, labelWidth, rowRect.h }, muiFitTextSize(line.text, labelWidth, size, ui.dp(10.0f)), chosen ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
            }
            muiPopClip();
        }
    }

    // Card: the game's picture drawn in the interface's color (the hero
    // turning for his appearance), title, subtitle, description (scrolls
    // when long).
    void CharacterScreen::buildCard(MuiContext& ui, const MuiRect& rect, const CharacterEditorCard& card, const std::string& id, std::string* key, bool hero)
    {
        const MuiTheme& theme = muiTheme();
        ui.panel(rect);

        MuiRect content = tall ? rect.inset(ui.dp(18.0f), ui.dp(16.0f)) : rect.inset(ui.dp(12.0f), ui.dp(10.0f));

        std::string newKey = std::to_string(card.frmId.fid()) + card.title;
        if (newKey != *key) {
            *key = newKey;
            ui.setScroll(id, 0.0f);
        }

        float y = content.y;
        int width;
        int height;
        SDL_Texture* picture = card.frmId.fid() != -1 ? muiLineArtTexture(card.frmId, &width, &height) : nullptr;
        if (hero) {
            float pictureHeight = content.h * 0.42f;
            muiDrawDudeTurning({ content.x, y, content.w, pictureHeight }, now);
            y += pictureHeight + ui.dp(8.0f);
        } else if (picture != nullptr) {
            float pictureHeight = std::min(content.w * height / width, content.h * 0.42f);
            MuiRect pictureRect = muiFitRect({ content.x, y, content.w, pictureHeight }, static_cast<float>(width), static_cast<float>(height));
            muiDrawTexture(picture, pictureRect, theme.accent);
            y += pictureHeight + ui.dp(8.0f);
        }

        float titleSize = ui.dp(tall ? 22.0f : 17.0f);
        std::u32string title = gameText(card.title.c_str());
        titleSize = muiFitTextSize(title, content.w, titleSize, ui.dp(12.0f));
        muiDrawTextAligned(title, { content.x, y, content.w, muiLineHeight(titleSize) }, titleSize, theme.accent, MuiAlign::Start, MuiAlign::Start);
        y += muiLineHeight(titleSize) + ui.dp(3.0f);

        float textSize = ui.dp(tall ? 16.0f : 13.5f);
        float lineHeight = muiLineHeight(textSize);
        std::vector<std::u32string> lines;
        size_t subtitleLines = 0;
        if (!card.subtitle.empty()) {
            lines = muiWrapText(gameText(card.subtitle.c_str()), content.w, textSize);
            subtitleLines = lines.size();
        }

        muiDrawLine(content.x, y, content.right(), y, ui.dp(1.0f), theme.panelBorder);
        y += ui.dp(6.0f);

        for (const std::u32string& line : muiWrapText(gameText(card.description.c_str()), content.w, textSize)) {
            lines.push_back(line);
        }

        MuiRect textRect = { content.x, y, content.w, content.bottom() - y };
        float gapAfterSubtitle = subtitleLines != 0 ? ui.dp(6.0f) : 0.0f;
        float offset = ui.scroll(id, textRect, lines.size() * lineHeight + gapAfterSubtitle);

        muiPushClip(textRect);
        float lineY = textRect.y - offset;
        for (size_t index = 0; index < lines.size(); index++) {
            if (index == subtitleLines) {
                lineY += gapAfterSubtitle;
            }
            if (lineY + lineHeight >= textRect.y && lineY <= textRect.bottom()) {
                muiDrawText(lines[index], textRect.x, lineY, textSize, index < subtitleLines ? theme.textDim : theme.text);
            }
            lineY += lineHeight;
        }
        muiPopClip();
    }

    // MARK: Perk panel

    void CharacterScreen::openPerkPanel()
    {
        perkStep = PerkStep::Perk;
        perkChoice = 0;
        perkCardKey.clear();
    }

    void CharacterScreen::handlePerkStep(CharacterEditorPerkStep step)
    {
        perkChoice = 0;
        perkCardKey.clear();

        switch (step) {
        case CharacterEditorPerkStep::TagSkill:
            perkStep = PerkStep::TagSkill;
            break;
        case CharacterEditorPerkStep::LoseTrait:
            perkStep = PerkStep::LoseTrait;
            break;
        case CharacterEditorPerkStep::NewTrait:
            perkStep = PerkStep::NewTrait;
            break;
        case CharacterEditorPerkStep::Done:
            soundPlayFile("ib1p1xx1");
            // More owed perks: choose the next one.
            if (characterEditorGetPerkOwed() != 0 && !characterEditorGetAvailablePerks().empty()) {
                perkStep = PerkStep::Perk;
            } else {
                perkStep = PerkStep::Closed;
                tab = Tab::Perks;
                folder = Folder::Perks;
                select(Item::Folder, 0);
            }
            break;
        case CharacterEditorPerkStep::Failed:
            soundPlayFile("iisxxxx1");
            perkStep = PerkStep::Closed;
            break;
        }
    }

    void CharacterScreen::choosePerk()
    {
        switch (perkStep) {
        case PerkStep::Perk: {
            std::vector<Perk> perks = characterEditorGetAvailablePerks();
            if (perkChoice < static_cast<int>(perks.size())) {
                handlePerkStep(characterEditorChoosePerk(perks[perkChoice]));
            }
            break;
        }
        case PerkStep::TagSkill: {
            std::vector<Skill> skills = characterEditorGetNewTagSkillOptions();
            if (perkChoice < static_cast<int>(skills.size())) {
                handlePerkStep(characterEditorChooseTagSkill(skills[perkChoice]));
            }
            break;
        }
        case PerkStep::LoseTrait: {
            std::vector<Trait> traits = characterEditorGetLoseTraitOptions();
            if (perkChoice < static_cast<int>(traits.size())) {
                handlePerkStep(characterEditorChooseLoseTrait(traits[perkChoice]));
            }
            break;
        }
        case PerkStep::NewTrait: {
            std::vector<Trait> traits = characterEditorGetNewTraitOptions();
            if (perkChoice < static_cast<int>(traits.size())) {
                handlePerkStep(characterEditorChooseNewTrait(traits[perkChoice]));
            }
            break;
        }
        case PerkStep::Closed:
            break;
        }
    }

    // Owed perk choice over the screen: list and card, like the game's perk
    // window (Tag! and Mutate! ask for more in it).
    void CharacterScreen::buildPerkPanel(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        ui.dim();

        MuiRect safe = ui.safeRect().inset(ui.dp(10.0f));
        float width = std::min(safe.w * 0.9f, ui.dp(760.0f));
        float height = std::min(safe.h * 0.9f, ui.dp(330.0f));
        MuiRect panel = { safe.centerX() - width / 2.0f, safe.centerY() - height / 2.0f, width, height };
        muiFillRoundRect(panel, ui.dp(theme.radius), kListBackground);
        muiStrokeRoundRect(panel, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.accent);
        ui.region(panel);

        MuiRect inner = panel.inset(ui.dp(10.0f));
        float gap = ui.dp(10.0f);
        float cardWidth = std::clamp(inner.w * 0.38f, ui.dp(220.0f), ui.dp(300.0f));
        MuiRect left = { inner.x, inner.y, inner.w - cardWidth - gap, inner.h };
        MuiRect cardRect = { left.right() + gap, inner.y, cardWidth, inner.h };

        // Options of this step with their cards.
        int title = kEditorPickPerk;
        std::vector<std::u32string> names;
        std::vector<std::u32string> details;
        std::vector<CharacterEditorCard> cards;
        switch (perkStep) {
        case PerkStep::Perk:
            for (Perk perk : characterEditorGetAvailablePerks()) {
                names.push_back(gameText(perkGetName(perk)));
                int rank = perkGetRank(gDude, perk);
                int maxRank = perkGetMaxRank(perk);
                details.push_back(maxRank > 1 ? number(rank) + U"/" + number(maxRank) : U"");
                cards.push_back(characterEditorGetPerkCard(perk));
            }
            break;
        case PerkStep::TagSkill:
            title = kEditorPickTagSkill;
            for (Skill skill : characterEditorGetNewTagSkillOptions()) {
                names.push_back(gameText(skillGetName(skill)));
                details.push_back(number(skillGetValue(gDude, skill)) + U"%");
                cards.push_back(characterEditorGetSkillCard(skill));
            }
            break;
        case PerkStep::LoseTrait:
            title = kEditorLoseTrait;
            for (Trait trait : characterEditorGetLoseTraitOptions()) {
                names.push_back(gameText(traitGetName(trait)));
                details.push_back(U"");
                cards.push_back(characterEditorGetTraitCard(trait));
            }
            break;
        case PerkStep::NewTrait:
            title = kEditorPickTrait;
            for (Trait trait : characterEditorGetNewTraitOptions()) {
                names.push_back(gameText(traitGetName(trait)));
                details.push_back(U"");
                cards.push_back(characterEditorGetTraitCard(trait));
            }
            break;
        case PerkStep::Closed:
            return;
        }

        if (names.empty()) {
            perkStep = PerkStep::Closed;
            return;
        }
        perkChoice = std::min(perkChoice, static_cast<int>(names.size()) - 1);

        float titleSize = ui.dp(15.0f);
        float titleHeight = muiLineHeight(titleSize) + ui.dp(6.0f);
        muiDrawTextAligned(editorText(title), { left.x + ui.dp(4.0f), left.y, left.w, titleHeight }, titleSize, theme.accent, MuiAlign::Start, MuiAlign::Center);

        float buttonHeight = ui.dp(40.0f);
        MuiRect list = { left.x, left.y + titleHeight + ui.dp(4.0f), left.w, left.h - titleHeight - buttonHeight - ui.dp(14.0f) };
        float rowHeight = ui.dp(40.0f);
        float size = ui.dp(15.0f);
        float padding = ui.dp(8.0f);

        float offset = ui.scroll("character.perkpanel.list", list, names.size() * rowHeight);
        bool scrolling = ui.isScrolling("character.perkpanel.list");

        muiPushClip(list);
        for (int index = 0; index < static_cast<int>(names.size()); index++) {
            MuiRect rowRect = { list.x, list.y - offset + index * rowHeight, list.w, rowHeight };
            if (rowRect.bottom() < list.y || rowRect.y > list.bottom()) {
                continue;
            }

            bool pressed;
            if (row(ui, "character.perkpanel.list." + std::to_string(index), rowRect, index == perkChoice, &pressed) && !scrolling) {
                if (index != perkChoice) {
                    soundPlayFile("isdxxxx1");
                }
                perkChoice = index;
            }

            float detailWidth = details[index].empty() ? 0.0f : muiTextWidth(details[index], size) + padding;
            float textSize = muiFitTextSize(names[index], rowRect.w - padding * 2.0f - detailWidth, size, ui.dp(10.0f));
            muiDrawTextAligned(names[index], { rowRect.x + padding, rowRect.y, rowRect.w, rowRect.h }, textSize, index == perkChoice ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
            if (!details[index].empty()) {
                muiDrawTextAligned(details[index], { rowRect.x, rowRect.y, rowRect.w - padding, rowRect.h }, size, theme.textDim, MuiAlign::End, MuiAlign::Center);
            }
        }
        muiPopClip();

        // Later (perks) or back to the perks (extra steps), take.
        float buttonWidth = (left.w - gap) / 2.0f;
        MuiRect secondary = { left.x, left.bottom() - buttonHeight, buttonWidth, buttonHeight };
        MuiRect primary = { secondary.right() + gap, secondary.y, buttonWidth, buttonHeight };
        if (perkStep == PerkStep::Perk) {
            if (textButton(ui, "character.perkpanel.later", secondary, ceText(kTextLater, "Later"), false, true)) {
                _gsound_red_butt_press(-1, 0);
                back();
            }
        } else if (smallButton(ui, "character.perkpanel.back", secondary, MuiIcon::Back, true)) {
            _gsound_red_butt_press(-1, 0);
            back();
        }

        if (textButton(ui, "character.perkpanel.take", primary, ceText(kTextTake, "Take"), true, true)) {
            choosePerk();
        }

        if (perkStep != PerkStep::Closed && perkChoice < static_cast<int>(cards.size())) {
            buildCard(ui, cardRect, cards[perkChoice], "character.perkpanel.card", &perkCardKey);
        }
    }

    // MARK: Name

    void CharacterScreen::closeName(bool apply)
    {
        if (apply) {
            std::string encoded = muiEncodeGameText(name);
            // Leading and trailing spaces aren't part of a name.
            size_t start = encoded.find_first_not_of(' ');
            size_t end = encoded.find_last_not_of(' ');
            if (start != std::string::npos) {
                characterEditorSetName(encoded.substr(start, end - start + 1).c_str());
            }
            soundPlayFile("ib1p1xx1");
        }

        nameOpen = false;
        endTextInput();
    }

    // Name field at the top (the keyboard takes the bottom).
    void CharacterScreen::buildNameEditor(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        ui.dim();

        std::u32string typed = ui.takeTextInput();
#ifdef __EMSCRIPTEN__
        if (webTakeTextInput(typed)) name.clear();
#endif
        for (char32_t codepoint : typed) {
            // One line of the game's charset.
            if (codepoint < 0x20 || muiEncodeGameText(std::u32string(1, codepoint)).empty()) {
                continue;
            }
            if (muiEncodeGameText(name).size() < kNameMaxLength) {
                name.push_back(codepoint);
            }
        }

        MuiRect safe = ui.safeRect().inset(ui.dp(10.0f));
        float width = std::min(safe.w * 0.7f, ui.dp(520.0f));
        MuiRect panel = { safe.centerX() - width / 2.0f, safe.y, width, ui.dp(112.0f) };
        muiFillRoundRect(panel, ui.dp(theme.radius), kListBackground);
        muiStrokeRoundRect(panel, ui.dp(theme.radius), ui.dp(theme.borderWidth), theme.accent);
        ui.region(panel);

        MuiRect inner = panel.inset(ui.dp(12.0f), ui.dp(10.0f));
        float titleSize = ui.dp(14.0f);
        muiDrawTextAligned(editorText(kEditorName), { inner.x, inner.y, inner.w, muiLineHeight(titleSize) }, titleSize, theme.textDim, MuiAlign::Start, MuiAlign::Start);

        float fieldHeight = ui.dp(44.0f);
        float buttonWidth = ui.dp(120.0f);
        MuiRect field = { inner.x, inner.bottom() - fieldHeight, inner.w - buttonWidth - ui.dp(10.0f), fieldHeight };
        MuiRect done = { field.right() + ui.dp(10.0f), field.y, buttonWidth, fieldHeight };

        muiFillRoundRect(field, ui.dp(6.0f), kBackground);
        muiStrokeRoundRect(field, ui.dp(6.0f), ui.dp(1.2f), theme.buttonBorder);

        float size = ui.dp(18.0f);
#ifdef __EMSCRIPTEN__
        webTextInputField(name, field.x, field.y, field.w, field.h, size, kNameMaxLength);
        if (webTextInputDone()) {
            closeName(true);
            return;
        }
#endif
        float textWidth = muiTextWidth(name, size);
        MuiRect textRect = field.inset(ui.dp(10.0f), 0.0f);
        muiDrawTextAligned(name, textRect, size, theme.accent, MuiAlign::Start, MuiAlign::Center);

        // Blinking cursor.
        if ((now / 500) % 2 == 0) {
            float cursorX = textRect.x + textWidth + ui.dp(2.0f);
            float lineHeight = muiLineHeight(size);
            muiFillRect({ cursorX, field.centerY() - lineHeight / 2.0f, ui.dp(2.0f), lineHeight }, theme.accent);
        }

        // Tap on the field brings the keyboard back.
        if (ui.touchable("character.name.field", field)) {
            beginTextInput();
        }

        if (textButton(ui, "character.name.done", done, ceText(kTextDone, "DONE"), true, true)) {
            closeName(true);
        }
    }

} // namespace

int muiCharacterScreenRun()
{
    CharacterScreen& screen = gCharacterScreen;
    screen.reset();
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        } else if (keyCode != -1) {
            screen.key(keyCode);
        }

        // The game's quit request closes the editor like its cancel.
        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.result = 1;
            screen.finished = true;
        }

        screen.runPending();

        devAutotestTick();

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
    endTextInput();

    return screen.result;
}

} // namespace fallout
