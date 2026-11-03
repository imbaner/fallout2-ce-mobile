#include "game_charset.h"

#include <string.h>

#include <string>

#include <SDL.h>

#include "debug.h"
#include "font_manager.h"
#include "message.h"
#include "platform_compat.h"
#include "settings.h"

namespace fallout {

namespace {

    // Windows-1250, bytes 0x80-0xFF.
    const char16_t kCp1250[128] = {
        0x20AC,
        0xFFFD,
        0x201A,
        0xFFFD,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0xFFFD,
        0x2030,
        0x0160,
        0x2039,
        0x015A,
        0x0164,
        0x017D,
        0x0179,
        0xFFFD,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0xFFFD,
        0x2122,
        0x0161,
        0x203A,
        0x015B,
        0x0165,
        0x017E,
        0x017A,
        0x00A0,
        0x02C7,
        0x02D8,
        0x0141,
        0x00A4,
        0x0104,
        0x00A6,
        0x00A7,
        0x00A8,
        0x00A9,
        0x015E,
        0x00AB,
        0x00AC,
        0x00AD,
        0x00AE,
        0x017B,
        0x00B0,
        0x00B1,
        0x02DB,
        0x0142,
        0x00B4,
        0x00B5,
        0x00B6,
        0x00B7,
        0x00B8,
        0x0105,
        0x015F,
        0x00BB,
        0x013D,
        0x02DD,
        0x013E,
        0x017C,
        0x0154,
        0x00C1,
        0x00C2,
        0x0102,
        0x00C4,
        0x0139,
        0x0106,
        0x00C7,
        0x010C,
        0x00C9,
        0x0118,
        0x00CB,
        0x011A,
        0x00CD,
        0x00CE,
        0x010E,
        0x0110,
        0x0143,
        0x0147,
        0x00D3,
        0x00D4,
        0x0150,
        0x00D6,
        0x00D7,
        0x0158,
        0x016E,
        0x00DA,
        0x0170,
        0x00DC,
        0x00DD,
        0x0162,
        0x00DF,
        0x0155,
        0x00E1,
        0x00E2,
        0x0103,
        0x00E4,
        0x013A,
        0x0107,
        0x00E7,
        0x010D,
        0x00E9,
        0x0119,
        0x00EB,
        0x011B,
        0x00ED,
        0x00EE,
        0x010F,
        0x0111,
        0x0144,
        0x0148,
        0x00F3,
        0x00F4,
        0x0151,
        0x00F6,
        0x00F7,
        0x0159,
        0x016F,
        0x00FA,
        0x0171,
        0x00FC,
        0x00FD,
        0x0163,
        0x02D9,
    };

    // Windows-1251, bytes 0x80-0xFF.
    const char16_t kCp1251[128] = {
        0x0402,
        0x0403,
        0x201A,
        0x0453,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0x20AC,
        0x2030,
        0x0409,
        0x2039,
        0x040A,
        0x040C,
        0x040B,
        0x040F,
        0x0452,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0xFFFD,
        0x2122,
        0x0459,
        0x203A,
        0x045A,
        0x045C,
        0x045B,
        0x045F,
        0x00A0,
        0x040E,
        0x045E,
        0x0408,
        0x00A4,
        0x0490,
        0x00A6,
        0x00A7,
        0x0401,
        0x00A9,
        0x0404,
        0x00AB,
        0x00AC,
        0x00AD,
        0x00AE,
        0x0407,
        0x00B0,
        0x00B1,
        0x0406,
        0x0456,
        0x0491,
        0x00B5,
        0x00B6,
        0x00B7,
        0x0451,
        0x2116,
        0x0454,
        0x00BB,
        0x0458,
        0x0405,
        0x0455,
        0x0457,
        0x0410,
        0x0411,
        0x0412,
        0x0413,
        0x0414,
        0x0415,
        0x0416,
        0x0417,
        0x0418,
        0x0419,
        0x041A,
        0x041B,
        0x041C,
        0x041D,
        0x041E,
        0x041F,
        0x0420,
        0x0421,
        0x0422,
        0x0423,
        0x0424,
        0x0425,
        0x0426,
        0x0427,
        0x0428,
        0x0429,
        0x042A,
        0x042B,
        0x042C,
        0x042D,
        0x042E,
        0x042F,
        0x0430,
        0x0431,
        0x0432,
        0x0433,
        0x0434,
        0x0435,
        0x0436,
        0x0437,
        0x0438,
        0x0439,
        0x043A,
        0x043B,
        0x043C,
        0x043D,
        0x043E,
        0x043F,
        0x0440,
        0x0441,
        0x0442,
        0x0443,
        0x0444,
        0x0445,
        0x0446,
        0x0447,
        0x0448,
        0x0449,
        0x044A,
        0x044B,
        0x044C,
        0x044D,
        0x044E,
        0x044F,
    };

    // Windows-1252, bytes 0x80-0xFF.
    const char16_t kCp1252[128] = {
        0x20AC,
        0xFFFD,
        0x201A,
        0x0192,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0x02C6,
        0x2030,
        0x0160,
        0x2039,
        0x0152,
        0xFFFD,
        0x017D,
        0xFFFD,
        0xFFFD,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0x02DC,
        0x2122,
        0x0161,
        0x203A,
        0x0153,
        0xFFFD,
        0x017E,
        0x0178,
        0x00A0,
        0x00A1,
        0x00A2,
        0x00A3,
        0x00A4,
        0x00A5,
        0x00A6,
        0x00A7,
        0x00A8,
        0x00A9,
        0x00AA,
        0x00AB,
        0x00AC,
        0x00AD,
        0x00AE,
        0x00AF,
        0x00B0,
        0x00B1,
        0x00B2,
        0x00B3,
        0x00B4,
        0x00B5,
        0x00B6,
        0x00B7,
        0x00B8,
        0x00B9,
        0x00BA,
        0x00BB,
        0x00BC,
        0x00BD,
        0x00BE,
        0x00BF,
        0x00C0,
        0x00C1,
        0x00C2,
        0x00C3,
        0x00C4,
        0x00C5,
        0x00C6,
        0x00C7,
        0x00C8,
        0x00C9,
        0x00CA,
        0x00CB,
        0x00CC,
        0x00CD,
        0x00CE,
        0x00CF,
        0x00D0,
        0x00D1,
        0x00D2,
        0x00D3,
        0x00D4,
        0x00D5,
        0x00D6,
        0x00D7,
        0x00D8,
        0x00D9,
        0x00DA,
        0x00DB,
        0x00DC,
        0x00DD,
        0x00DE,
        0x00DF,
        0x00E0,
        0x00E1,
        0x00E2,
        0x00E3,
        0x00E4,
        0x00E5,
        0x00E6,
        0x00E7,
        0x00E8,
        0x00E9,
        0x00EA,
        0x00EB,
        0x00EC,
        0x00ED,
        0x00EE,
        0x00EF,
        0x00F0,
        0x00F1,
        0x00F2,
        0x00F3,
        0x00F4,
        0x00F5,
        0x00F6,
        0x00F7,
        0x00F8,
        0x00F9,
        0x00FA,
        0x00FB,
        0x00FC,
        0x00FD,
        0x00FE,
        0x00FF,
    };

    // Windows-1253, bytes 0x80-0xFF.
    const char16_t kCp1253[128] = {
        0x20AC,
        0xFFFD,
        0x201A,
        0x0192,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0xFFFD,
        0x2030,
        0xFFFD,
        0x2039,
        0xFFFD,
        0xFFFD,
        0xFFFD,
        0xFFFD,
        0xFFFD,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0xFFFD,
        0x2122,
        0xFFFD,
        0x203A,
        0xFFFD,
        0xFFFD,
        0xFFFD,
        0xFFFD,
        0x00A0,
        0x0385,
        0x0386,
        0x00A3,
        0x00A4,
        0x00A5,
        0x00A6,
        0x00A7,
        0x00A8,
        0x00A9,
        0xFFFD,
        0x00AB,
        0x00AC,
        0x00AD,
        0x00AE,
        0x2015,
        0x00B0,
        0x00B1,
        0x00B2,
        0x00B3,
        0x0384,
        0x00B5,
        0x00B6,
        0x00B7,
        0x0388,
        0x0389,
        0x038A,
        0x00BB,
        0x038C,
        0x00BD,
        0x038E,
        0x038F,
        0x0390,
        0x0391,
        0x0392,
        0x0393,
        0x0394,
        0x0395,
        0x0396,
        0x0397,
        0x0398,
        0x0399,
        0x039A,
        0x039B,
        0x039C,
        0x039D,
        0x039E,
        0x039F,
        0x03A0,
        0x03A1,
        0xFFFD,
        0x03A3,
        0x03A4,
        0x03A5,
        0x03A6,
        0x03A7,
        0x03A8,
        0x03A9,
        0x03AA,
        0x03AB,
        0x03AC,
        0x03AD,
        0x03AE,
        0x03AF,
        0x03B0,
        0x03B1,
        0x03B2,
        0x03B3,
        0x03B4,
        0x03B5,
        0x03B6,
        0x03B7,
        0x03B8,
        0x03B9,
        0x03BA,
        0x03BB,
        0x03BC,
        0x03BD,
        0x03BE,
        0x03BF,
        0x03C0,
        0x03C1,
        0x03C2,
        0x03C3,
        0x03C4,
        0x03C5,
        0x03C6,
        0x03C7,
        0x03C8,
        0x03C9,
        0x03CA,
        0x03CB,
        0x03CC,
        0x03CD,
        0x03CE,
        0xFFFD,
    };

    // Windows-1254, bytes 0x80-0xFF.
    const char16_t kCp1254[128] = {
        0x20AC,
        0xFFFD,
        0x201A,
        0x0192,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0x02C6,
        0x2030,
        0x0160,
        0x2039,
        0x0152,
        0xFFFD,
        0xFFFD,
        0xFFFD,
        0xFFFD,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0x02DC,
        0x2122,
        0x0161,
        0x203A,
        0x0153,
        0xFFFD,
        0xFFFD,
        0x0178,
        0x00A0,
        0x00A1,
        0x00A2,
        0x00A3,
        0x00A4,
        0x00A5,
        0x00A6,
        0x00A7,
        0x00A8,
        0x00A9,
        0x00AA,
        0x00AB,
        0x00AC,
        0x00AD,
        0x00AE,
        0x00AF,
        0x00B0,
        0x00B1,
        0x00B2,
        0x00B3,
        0x00B4,
        0x00B5,
        0x00B6,
        0x00B7,
        0x00B8,
        0x00B9,
        0x00BA,
        0x00BB,
        0x00BC,
        0x00BD,
        0x00BE,
        0x00BF,
        0x00C0,
        0x00C1,
        0x00C2,
        0x00C3,
        0x00C4,
        0x00C5,
        0x00C6,
        0x00C7,
        0x00C8,
        0x00C9,
        0x00CA,
        0x00CB,
        0x00CC,
        0x00CD,
        0x00CE,
        0x00CF,
        0x011E,
        0x00D1,
        0x00D2,
        0x00D3,
        0x00D4,
        0x00D5,
        0x00D6,
        0x00D7,
        0x00D8,
        0x00D9,
        0x00DA,
        0x00DB,
        0x00DC,
        0x0130,
        0x015E,
        0x00DF,
        0x00E0,
        0x00E1,
        0x00E2,
        0x00E3,
        0x00E4,
        0x00E5,
        0x00E6,
        0x00E7,
        0x00E8,
        0x00E9,
        0x00EA,
        0x00EB,
        0x00EC,
        0x00ED,
        0x00EE,
        0x00EF,
        0x011F,
        0x00F1,
        0x00F2,
        0x00F3,
        0x00F4,
        0x00F5,
        0x00F6,
        0x00F7,
        0x00F8,
        0x00F9,
        0x00FA,
        0x00FB,
        0x00FC,
        0x0131,
        0x015F,
        0x00FF,
    };

    // Windows-1257, bytes 0x80-0xFF.
    const char16_t kCp1257[128] = {
        0x20AC,
        0xFFFD,
        0x201A,
        0xFFFD,
        0x201E,
        0x2026,
        0x2020,
        0x2021,
        0xFFFD,
        0x2030,
        0xFFFD,
        0x2039,
        0xFFFD,
        0x00A8,
        0x02C7,
        0x00B8,
        0xFFFD,
        0x2018,
        0x2019,
        0x201C,
        0x201D,
        0x2022,
        0x2013,
        0x2014,
        0xFFFD,
        0x2122,
        0xFFFD,
        0x203A,
        0xFFFD,
        0x00AF,
        0x02DB,
        0xFFFD,
        0x00A0,
        0xFFFD,
        0x00A2,
        0x00A3,
        0x00A4,
        0xFFFD,
        0x00A6,
        0x00A7,
        0x00D8,
        0x00A9,
        0x0156,
        0x00AB,
        0x00AC,
        0x00AD,
        0x00AE,
        0x00C6,
        0x00B0,
        0x00B1,
        0x00B2,
        0x00B3,
        0x00B4,
        0x00B5,
        0x00B6,
        0x00B7,
        0x00F8,
        0x00B9,
        0x0157,
        0x00BB,
        0x00BC,
        0x00BD,
        0x00BE,
        0x00E6,
        0x0104,
        0x012E,
        0x0100,
        0x0106,
        0x00C4,
        0x00C5,
        0x0118,
        0x0112,
        0x010C,
        0x00C9,
        0x0179,
        0x0116,
        0x0122,
        0x0136,
        0x012A,
        0x013B,
        0x0160,
        0x0143,
        0x0145,
        0x00D3,
        0x014C,
        0x00D5,
        0x00D6,
        0x00D7,
        0x0172,
        0x0141,
        0x015A,
        0x016A,
        0x00DC,
        0x017B,
        0x017D,
        0x00DF,
        0x0105,
        0x012F,
        0x0101,
        0x0107,
        0x00E4,
        0x00E5,
        0x0119,
        0x0113,
        0x010D,
        0x00E9,
        0x017A,
        0x0117,
        0x0123,
        0x0137,
        0x012B,
        0x013C,
        0x0161,
        0x0144,
        0x0146,
        0x00F3,
        0x014D,
        0x00F5,
        0x00F6,
        0x00F7,
        0x0173,
        0x0142,
        0x015B,
        0x016B,
        0x00FC,
        0x017C,
        0x017E,
        0x02D9,
    };

    enum class Alphabet {
        Latin,
        Cyrillic,
        Greek,
    };

    struct Charset {
        const char* name;
        const char16_t* table;
        Alphabet alphabet;
    };

    const Charset kCentralEuropean = { "windows-1250", kCp1250, Alphabet::Latin };
    const Charset kCyrillic = { "windows-1251", kCp1251, Alphabet::Cyrillic };
    const Charset kWestern = { "windows-1252", kCp1252, Alphabet::Latin };
    const Charset kGreek = { "windows-1253", kCp1253, Alphabet::Greek };
    const Charset kTurkish = { "windows-1254", kCp1254, Alphabet::Latin };
    const Charset kBaltic = { "windows-1257", kCp1257, Alphabet::Latin };

    // Languages by the game's name of them (`[system] language`, the folder
    // in `text`) and the device's (ISO 639-1); those not here are Western.
    struct Language {
        const char* name;
        const char* code;
        const Charset* charset;
    };

    const Language kLanguages[] = {
        { "english", "en", &kWestern },
        { "french", "fr", &kWestern },
        { "german", "de", &kWestern },
        { "italian", "it", &kWestern },
        { "spanish", "es", &kWestern },
        { "portuguese", "pt", &kWestern },
        { "polish", "pl", &kCentralEuropean },
        { "czech", "cs", &kCentralEuropean },
        { "slovak", "sk", &kCentralEuropean },
        { "hungarian", "hu", &kCentralEuropean },
        { "russian", "ru", &kCyrillic },
        { "ukrainian", "uk", &kCyrillic },
        { "belarusian", "be", &kCyrillic },
        { "bulgarian", "bg", &kCyrillic },
        { "greek", "el", &kGreek },
        { "turkish", "tr", &kTurkish },
        { "lithuanian", "lt", &kBaltic },
        { "latvian", "lv", &kBaltic },
        { "estonian", "et", &kBaltic },
    };

    // Letters of an alphabet drawn as Latin ones in its fonts (by the
    // alphabet's code page): Unicode, the Latin letter.
    struct LookAlike {
        char16_t letter;
        char latin;
    };

    const LookAlike kCyrillicLookAlikes[] = {
        { 0x0410, 'A' },
        { 0x0412, 'B' },
        { 0x0415, 'E' },
        { 0x041A, 'K' },
        { 0x041C, 'M' },
        { 0x041D, 'H' },
        { 0x041E, 'O' },
        { 0x0420, 'P' },
        { 0x0421, 'C' },
        { 0x0422, 'T' },
        { 0x0425, 'X' },
        { 0x0430, 'a' },
        { 0x0435, 'e' },
        { 0x043E, 'o' },
        { 0x0440, 'p' },
        { 0x0441, 'c' },
        { 0x0445, 'x' },
    };

    const LookAlike kGreekLookAlikes[] = {
        { 0x0391, 'A' },
        { 0x0392, 'B' },
        { 0x0395, 'E' },
        { 0x0396, 'Z' },
        { 0x0397, 'H' },
        { 0x0399, 'I' },
        { 0x039A, 'K' },
        { 0x039C, 'M' },
        { 0x039D, 'N' },
        { 0x039F, 'O' },
        { 0x03A1, 'P' },
        { 0x03A4, 'T' },
        { 0x03A5, 'Y' },
        { 0x03A7, 'X' },
        { 0x03BF, 'o' },
    };

    // The interface fonts' (font_manager.cc) look-alikes drawn as their
    // Latin letters must outnumber those drawn otherwise this many times. A
    // Russian translation's fonts: 77 to 8, the English ones: 0 to 54.
    constexpr int kLookAlikesMajority = 3;
    constexpr int kLookAlikesLeast = 6;

    const Charset* const kLatinCharsets[] = {
        &kWestern,
        &kCentralEuropean,
        &kTurkish,
        &kBaltic,
    };

    // Short ones, in every translation, with words in the language's letters.
    const char* const kSampleTexts[] = {
        "game\\stat.msg",
        "game\\skill.msg",
        "game\\perk.msg",
        "game\\trait.msg",
        "game\\misc.msg",
    };

    // Bytes inside the sample's words another code page reads as letters and
    // the language's doesn't, as against the other way round. Polish in
    // windows-1252: 534 to 0; Czech: 2 to 0, others none.
    constexpr int kWordBytesLeast = 20;
    constexpr int kWordBytesMajority = 10;

    const Charset* gCharset = nullptr;
    // The game's texts are in its language (`[system] language`).
    bool gTextsInLanguage = true;
    std::string gUiLanguage = "english";

    const Language* languageNamed(const char* name)
    {
        for (const Language& language : kLanguages) {
            if (compat_stricmp(language.name, name) == 0) {
                return &language;
            }
        }
        return nullptr;
    }

    const Charset* charsetOfLanguage(const char* name)
    {
        const Language* language = languageNamed(name);
        return language != nullptr ? language->charset : &kWestern;
    }

    const Charset* current()
    {
        // Before the fonts: by the language.
        return gCharset != nullptr ? gCharset : charsetOfLanguage(settings.system.language.c_str());
    }

    bool sameGlyph(int font, unsigned char ch1, unsigned char ch2, bool* known)
    {
        int width1;
        int height1;
        const unsigned char* pixels1;
        int width2;
        int height2;
        const unsigned char* pixels2;
        *known = interfaceFontGetGlyph(font, ch1, &width1, &height1, &pixels1)
            && interfaceFontGetGlyph(font, ch2, &width2, &height2, &pixels2);
        return *known
            && width1 == width2
            && height1 == height2
            && memcmp(pixels1, pixels2, width1 * height1) == 0;
    }

    // Whether the fonts draw [charset]'s look-alikes as the Latin letters.
    bool fontsDraw(const Charset& charset, const LookAlike* lookAlikes, int count)
    {
        int same = 0;
        int other = 0;
        for (int font = 0; font < interfaceFontCount(); font++) {
            for (int index = 0; index < count; index++) {
                for (int byte = 0x80; byte < 0x100; byte++) {
                    if (charset.table[byte - 0x80] != lookAlikes[index].letter) {
                        continue;
                    }

                    bool known;
                    if (sameGlyph(font, static_cast<unsigned char>(byte), static_cast<unsigned char>(lookAlikes[index].latin), &known)) {
                        same++;
                    } else if (known) {
                        other++;
                    }
                }
            }
        }

        return same >= kLookAlikesLeast && same >= other * kLookAlikesMajority;
    }

    // The alphabet the fonts draw; Latin when they don't show (Latin fonts'
    // accented letters are each translator's own).
    Alphabet fontsAlphabet()
    {
        if (fontsDraw(kCyrillic, kCyrillicLookAlikes, sizeof(kCyrillicLookAlikes) / sizeof(kCyrillicLookAlikes[0]))) {
            return Alphabet::Cyrillic;
        }
        if (fontsDraw(kGreek, kGreekLookAlikes, sizeof(kGreekLookAlikes) / sizeof(kGreekLookAlikes[0]))) {
            return Alphabet::Greek;
        }
        return Alphabet::Latin;
    }

    bool isLatinLetter(char16_t ch)
    {
        return ch >= 0x00C0 && ch <= 0x017F && ch != 0x00D7 && ch != 0x00F7;
    }

    bool isWordByte(unsigned char ch)
    {
        return ch >= 0x80 || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    }

    // Counts the bytes over 0x7F with a letter or another such byte on both
    // sides: a word's letters wherever the language has them, punctuation
    // stands at a word's ends.
    void countWordBytes(const char* text, int* counts)
    {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);
        for (size_t index = 1; bytes[index] != '\0' && bytes[index + 1] != '\0'; index++) {
            if (bytes[index] >= 0x80 && isWordByte(bytes[index - 1]) && isWordByte(bytes[index + 1])) {
                counts[bytes[index]]++;
            }
        }
    }

    // [named] unless the game's words read as letters only in another Latin
    // code page: a translation put in another language's folder (Sonora's
    // language packs). It shows only where [named] has no letters for the
    // bytes: Polish in a Western language, not Czech or Hungarian, whose
    // bytes are letters in windows-1252 too.
    const Charset* textsCharset(const Charset* named)
    {
        int counts[256] = {};
        for (const char* path : kSampleTexts) {
            MessageList messageList;
            if (messageListInit(&messageList) && messageListLoad(&messageList, path)) {
                for (int index = 0; index < messageList.entries_num; index++) {
                    if (messageList.entries[index].text[0] != '\0') {
                        countWordBytes(messageList.entries[index].text, counts);
                    }
                }
            }
            messageListFree(&messageList);
        }

        const Charset* found = named;
        int mostLetters = 0;
        for (const Charset* charset : kLatinCharsets) {
            int letters = 0;
            int lost = 0;
            for (int byte = 0x80; byte < 0x100; byte++) {
                bool letter = isLatinLetter(charset->table[byte - 0x80]);
                bool namedLetter = isLatinLetter(named->table[byte - 0x80]);
                if (letter && !namedLetter) {
                    letters += counts[byte];
                } else if (namedLetter && !letter) {
                    lost += counts[byte];
                }
            }

            if (letters >= kWordBytesLeast && letters >= lost * kWordBytesMajority && letters > mostLetters) {
                found = charset;
                mostLetters = letters;
            }
        }
        return found;
    }

    // The device's first language in [charset] (one of `kLanguages`).
    const Language* deviceLanguage(const Charset* charset)
    {
        const Language* found = nullptr;
        SDL_Locale* locales = SDL_GetPreferredLocales();
        if (locales == nullptr) {
            return nullptr;
        }
        for (SDL_Locale* locale = locales; locale->language != nullptr && found == nullptr; locale++) {
            for (const Language& language : kLanguages) {
                if (compat_stricmp(language.code, locale->language) == 0 && language.charset == charset) {
                    found = &language;
                    break;
                }
            }
        }
        SDL_free(locales);
        return found;
    }

} // namespace

void gameCharsetInit()
{
    const char* language = settings.system.language.c_str();
    const Charset* named = charsetOfLanguage(language);
    Alphabet drawn = fontsAlphabet();

    if (drawn == Alphabet::Cyrillic) {
        gCharset = &kCyrillic;
    } else if (drawn == Alphabet::Greek) {
        gCharset = &kGreek;
    } else {
        gCharset = named->alphabet == Alphabet::Latin ? textsCharset(named) : named;
    }

    gTextsInLanguage = gCharset == named;
    if (gTextsInLanguage) {
        gUiLanguage = language;
    } else {
        const Language* device = deviceLanguage(gCharset);
        gUiLanguage = device != nullptr ? device->name : "english";
    }

    debugPrint("Game texts: %s%s, mobile UI in %s\n",
        gCharset->name,
        gTextsInLanguage ? "" : " (not the language's)",
        gUiLanguage.c_str());
}

char32_t gameCharsetDecode(unsigned char ch)
{
    return ch < 0x80 ? ch : current()->table[ch - 0x80];
}

unsigned char gameCharsetEncode(char32_t codepoint)
{
    if (codepoint < 0x80) {
        return static_cast<unsigned char>(codepoint);
    }

    const char16_t* table = current()->table;
    for (int index = 0; index < 0x80; index++) {
        if (table[index] == codepoint && codepoint != 0xFFFD) {
            return static_cast<unsigned char>(0x80 + index);
        }
    }
    return 0;
}

const char* gameCharsetName()
{
    return current()->name;
}

bool gameCharsetIsCyrillic()
{
    return current()->alphabet == Alphabet::Cyrillic;
}

const char* gameCharsetUiLanguage()
{
    return gCharset != nullptr ? gUiLanguage.c_str() : settings.system.language.c_str();
}

} // namespace fallout
