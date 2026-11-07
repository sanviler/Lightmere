#pragma once
// The original game panel, observed while the game draws it.
//  - $01376 draws the panel: the background descriptor (L01624, chosen per level and character) and the display
//    list at $0160C, through the descriptor routine $0ACD2 (d0 = x, d1 = y, d2 = descriptor). The descriptors
//    drawn there are recorded, so the port can rebuild the panel shape from the user's game files.
//  - $0117C writes a string (a0 = string, d0 = x, d1 = y in pixels, 8 pixels per character, descriptor font):
//    $0114C writes the place name at (24, 183), $0107A the target name at (192, 183) after clearing the target
//    box ($0111A), and the target portrait at (272, 144). The current target is L1E660 (0 = none).
#include "amiga/machine.h"
#include <string>
#include <vector>

namespace dm::game {

struct PanelDraw {
    int x = 0, y = 0;
    uint16_t descriptor = 0;
};

class PanelText {
public:
    void install(amiga::Machine& m);
    bool log = false;                    // print every panel string on stderr (tests)
    // last strings written on the game panel (bytes as in the game)
    std::string roomName, targetName;
    // descriptors drawn by the last complete panel redraw ($01376), and a counter that changes with each redraw
    const std::vector<PanelDraw>& panelDraws() const { return panelDraws_; }
    int panelGeneration() const { return generation_; }
    // target portraits seen so far, drawn by $0107A at (272, 144): the target box of the panel changes there
    const std::vector<PanelDraw>& targetPortraits() const { return portraits_; }

private:
    std::vector<PanelDraw> panelDraws_, recording_, portraits_;
    bool inPanel_ = false;
    int generation_ = 0;
};

} // namespace dm::game
