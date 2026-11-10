#ifndef FALLOUT_GAME_CARD_H_
#define FALLOUT_GAME_CARD_H_

#include <string>

namespace fallout {

// The app's games (Android: GamesActivity, Games.java): every game in a
// folder of its own, picked in the app's list of them. What that list shows
// of a game and can't read from its files itself, the game tells it here.

// The game's card, written at its main menu: its picture (the menu's,
// card.png, once a run) and card.txt - its version line and where its newest save
// is ([place]). In the game's private folder (Android), next to the game in
// automated tests ("ce-card.*"), nowhere else.
void gameCardWrite(const std::u32string& place);

// The game got to its main menu or into play: it doesn't fail at its start
// (the app opens its list of games instead of a game that does).
void gameCardStarted();

// The game's name in the app's list, empty where there's no list.
const std::string& gameCardName();

// Opens the app's list of games over the game (it waits behind, paused).
void gameCardOpenGames();

} // namespace fallout

#endif /* FALLOUT_GAME_CARD_H_ */
