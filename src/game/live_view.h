#pragma once
// Live view: the room drawn by the C++ renderer from the state of the original game in memory.
//  - background: walls and floors of the current and neighbouring rooms, objects and flames of the neighbours
//    (RoomRenderer; the flames of every visible torch are animated)
//  - current room: the game's draw list ($20A06), that is objects, characters, flames and pieces of
//    wall in front of the characters, with the positions and frames computed by the game at this moment,
//    in list order and with the masks of $0E0D0 / $0B21E.
// The result is a 320 x (200 + extraTop) frame: the extra rows show the top of the rooms
// that the original game cuts off.
#include "amiga/machine.h"
#include "engine/room_renderer.h"
#include <map>
#include <memory>
#include <string>

namespace dm::game {

class LiveView {
public:
    LiveView(Bytes mainCode, int extraTop);
    // CODE hunk of MAIN.BIN (unpacked) from the game files
    static Bytes loadMainCode();
    // false if the game is not in a room (intro, start menu, level screens)
    bool render(const amiga::Machine& m, IndexedFrame& out, int animFrame);
    int extraTop() const { return extraTop_; }
    // writes a string with the panel's descriptor font ($011AE: level base $1A676 + code - 32;
    // codes from $80: tables $1A67E and $1A686), 8 pixels per character, into f (same coordinates as $0ACD2)
    void renderText(const amiga::Machine& m, const std::string& s, int x, int y, IndexedFrame& f);
    // one descriptor of the current level at (x, y), as $0ACD2 draws it (used to rebuild the panel)
    void renderDescriptor(const amiga::Machine& m, int x, int y, uint16_t d, IndexedFrame& f);
    // for the debug teleport: does the room exist in the level (1-3)? Free cell nearest the centre, in
    // RAM coordinates (file + 32)
    bool roomUsed(int level, int room);
    bool freeCell(int level, int room, int& x, int& y);
    int lastRoom() const { return room_; }

private:
    Bytes code_;
    RoomRenderer renderer_;
    std::map<int, std::unique_ptr<Level>> levels_;
    int extraTop_;
    int room_ = -1;
};

} // namespace dm::game
