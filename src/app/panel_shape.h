#pragma once
// The shape of the original game panel (rows 126-199 of the screen), rebuilt from the user's game files.
// The game draws its panel with a few descriptors (recorded by PanelText while it does so); drawing the same
// descriptors with the C++ renderer gives, for every pixel, whether it belongs to the panel artwork and its
// palette index. Two kinds of panel pixels:
//  - fixed: the artwork (frames, ornaments, the hero's portrait);
//  - changing: the energy bars (holes enclosed in the artwork) and the target portrait (drawn at (272, 144)).
#include "amiga/machine.h"
#include "game/live_view.h"
#include "game/panel_text.h"
#include <vector>

namespace lm {

class PanelShape {
public:
    static constexpr int kTop = 126, kRows = 74;

    // rebuilds the shape when the game has redrawn its panel or a new target portrait appeared
    void update(const amiga::Machine& m, dm::game::LiveView& live, const dm::game::PanelText& text);
    void reset() { known_ = false; generation_ = -1; portraits_ = 0; }
    bool known() const { return known_; }
    // y is a screen row (kTop .. kTop + kRows - 1)
    bool fixed(int x, int y) const { return idx_[at(x, y)] != 0; }
    bool changing(int x, int y) const { return changing_[at(x, y)] != 0; }
    uint8_t index(int x, int y) const { return idx_[at(x, y)]; }

private:
    static size_t at(int x, int y) { return size_t(y - kTop) * 320 + size_t(x); }
    std::vector<uint8_t> idx_ = std::vector<uint8_t>(size_t(320) * kRows, 0);
    std::vector<uint8_t> changing_ = std::vector<uint8_t>(size_t(320) * kRows, 0);
    bool known_ = false;
    int generation_ = -1;
    size_t portraits_ = 0;
};

} // namespace lm
