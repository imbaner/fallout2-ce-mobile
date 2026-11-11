#ifndef FALLOUT_MUI_FLOATING_TEXT_H_
#define FALLOUT_MUI_FLOATING_TEXT_H_

namespace fallout {

// The game's floating texts drawn over the map at a fixed size (see
// mui_floating_text.cc), under the map's hints and the HUD.
void muiFloatingTextInit();

// The game's floating texts drawn over the map at the last frame.
int muiFloatingTextShownCount();

} // namespace fallout

#endif /* FALLOUT_MUI_FLOATING_TEXT_H_ */
