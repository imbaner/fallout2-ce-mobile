#ifndef FALLOUT_GAME_CHARSET_H_
#define FALLOUT_GAME_CHARSET_H_

namespace fallout {

// CE: Which characters the game's texts mean. The game has no encoding: a
// text is bytes, the game's fonts draw a picture for each, and translations
// replace the fonts (which is why one can sit in any language's folder).
// The mobile UI draws texts with its own font, so it needs the characters:
//
// - The alphabet comes from the game's fonts: a Cyrillic or Greek font draws
//   the letters that look Latin (А, В, Е...) as the Latin ones, a Latin
//   font draws accented letters there (À, Â, Å...).
// - Within an alphabet (Western, Central European... Windows code pages)
//   from `[system] language`: fonts can't tell them apart, translators draw
//   the accented letters their own way.
// - When the language says another alphabet than the fonts, the fonts are
//   right (a translation put in the English folder).
// - When words of the game's texts have bytes the language's code page reads
//   as no letters and another Latin one as letters, that one is right (Polish
//   in the English folder). Translations whose bytes are letters in both
//   (Czech, Hungarian in a Western language) can't be told this way.

// After the game's fonts are loaded.
void gameCharsetInit();

// Unicode of a game text's byte.
char32_t gameCharsetDecode(unsigned char ch);
// The game text's byte of [codepoint], 0 - it has none.
unsigned char gameCharsetEncode(char32_t codepoint);

// The code page's Windows name ("windows-1251").
const char* gameCharsetName();

// The game's texts are in a Cyrillic alphabet.
bool gameCharsetIsCyrillic();

// The language of the mobile UI's texts (`game\ce.msg`): the game's, when
// its texts are in it; otherwise the device's when its code page is the
// texts', or English.
const char* gameCharsetUiLanguage();

} // namespace fallout

#endif /* FALLOUT_GAME_CHARSET_H_ */
