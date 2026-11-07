#pragma once
// Menus and dialogs of the original game, separated from the room behind them, so that Lightmere can show
// them over its own layout.
// The game darkens the room with a checkerboard of black pixels and draws the menu on top. Comparing the screen
// with the last screen before the menu: in the dark squares of the checkerboard the background turns black, in
// the others it stays the same. Inactive windows (dialogs left behind the one in use) are covered by the game
// with its own checkerboard.
// All images are 320 x 200 lores pixels, 0x00RRGGBB.
#include <cstdint>
#include <vector>

namespace lm {

struct CheckerTest {
    bool checker = false;   // the game's menu checkerboard is on screen
    int parity = 0;         // parity (x + y) & 1 of its dark squares
};
CheckerTest detectMenuChecker(const std::vector<uint32_t>& screen, const std::vector<uint32_t>& before);

// menu: the pixels of the menu (ARGB, alpha 0 elsewhere).
// inactive (only with modernBackground): the inactive windows without the checkerboard, blurred and darkened like
// the background; they are then left out of "menu".
// wholeScreen: every pixel is menu (screens that are neither the game nor a menu, such as the game over).
void extractMenuLayers(const std::vector<uint32_t>& screen, const std::vector<uint32_t>& before, int parity,
                       bool wholeScreen, bool modernBackground, std::vector<uint32_t>& menu,
                       std::vector<uint32_t>& inactive);

// blur (three box passes, nearly Gaussian) and darken to 45%; used for the background of menus
void blurAndDarken(std::vector<uint32_t>& px, int w, int h, int radius);

} // namespace lm
