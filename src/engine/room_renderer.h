#pragma once
// Drawing of a room, rebuilt from the original routines:
//   $0D000  recursive visit of the adjacent rooms ($DF/$E0 + offsets $E3..$E6, depth 4 at most)
//   $0D24E  drawing of the room: 11 diagonals (table $1A6E6), per cell floor T2 + walls T3
//   $0ACD2  drawing of a descriptor with hotspot and clipping to the view
//   $0CF36  palette = table $17F2E[room[$E7]]
//   $0DEB4  position of the room objects (list from word +4 of the room)
//   $0E0D0  order: the room list, every object masked by those in front of it
//   $10984  characters shown on entry; $115B8 first direction; animations from $1D8EC
// Character movement and dynamic doors come from the game state (see live_view.h).
#include "formats/level.h"
#include "formats/anims.h"
#include <functional>
#include <optional>
#include <set>

namespace dm {

struct IndexedFrame {
    int width = 320, height = 200;
    std::vector<uint8_t> px = std::vector<uint8_t>(320 * 200, 0);
    Palette palette;
};

class RoomRenderer {
public:
    // mainCode: unpacked CODE hunk of MAIN.BIN (order tables, arrows, palettes)
    explicit RoomRenderer(Bytes mainCode) : code_(std::move(mainCode)), anims_(code_) {}
    void render(Level& level, int room, IndexedFrame& out);
    void setDrawObjects(bool on) { drawObjects_ = on; }
    void setAnimFrame(int frame) { animFrame_ = frame; }   // animation counter (flames)
    void setDrawCharacters(bool on) { drawCharacters_ = on; }        // spawn rule $10984 and first pose
    void setSpawnSeed(uint32_t seed) { spawnSeed_ = seed; }          // random seed for the spawn rule
    void setVisibleOverride(std::optional<std::set<uint16_t>> v) { visibleOverride_ = std::move(v); } // checks
    const Animations& animations() const { return anims_; }
    static void objectPosition(const Level& lv, uint16_t inst, int X, int Y, int& sx, int& sy);
    static void objectPosition(const Level& lv, const ObjectInstance& o, int X, int Y, int& sx, int& sy);

    // --- live view (src/game/live_view.cpp) ---
    // Vertical offset of the whole drawing: extra rows above the room, which in the original game
    // are cut by the edge of the screen. With origin > 0 rows 0-7 are no longer blacked out.
    void setOriginY(int dy) { originY_ = dy; }
    // In the live view the objects and flames of the current room come from the game's draw list:
    // the renderer draws only those of the neighbouring rooms.
    void setCurrentRoomFromGame(bool on) { currentFromGame_ = on; }
    // draws a descriptor with hotspot, horizontal clipping and mask (pixels = 1 are left alone)
    void drawSprite(Level& lv, int x, int y, uint16_t d, int xmin, int xmax, IndexedFrame& f, const std::vector<uint8_t>* mask);
    // silhouette of a descriptor (1 where the graphic is not transparent), in frame coordinates
    void silhouette(Level& lv, int x, int y, uint16_t d, IndexedFrame& f, std::vector<uint8_t>& out) const;
    // Game state of the rooms, as in $0CCB0: substituted room number (L1E6CE -> L1E6D0) and room data
    // changed after decoding (closed doors, $07B9E). Without it: the file data.
    struct RoomState {
        std::function<int(int)> mapRoom;
        std::function<void(int, RoomData&)> patchRoom;
        // room objects from the game memory (lists per room at $200D6 + room x 8 + 4, instances at
        // L1FBE0): objects picked up or moved are no longer in the room list of the file
        std::function<bool(int, std::vector<uint16_t>&)> roomObjects;
        std::function<bool(uint16_t, ObjectInstance&)> instance;
    };
    void setRoomState(RoomState s) { state_ = std::move(s); }

private:
    void visit(Level& lv, int n, int depth, int X, int Y, int xmin, int xmax, IndexedFrame& f);
    void drawRoom(Level& lv, int n, int depth, int X, int Y, int xmin, int xmax, IndexedFrame& f);
    void drawDescriptor(Level& lv, int x, int y, uint16_t d, int xmin, int xmax, IndexedFrame& f);
    void drawRoomObjects(Level& lv, int n, int X, int Y, int xmin, int xmax, IndexedFrame& f);
    int mapRoom(int n) const { return state_.mapRoom && n < 0xFF ? state_.mapRoom(n) : n; }
    RoomData roomData(Level& lv, int n);               // n already substituted
    RoomState state_;
    Bytes code_;
    Animations anims_;
    bool drawObjects_ = true, drawCharacters_ = true;
    uint32_t spawnSeed_ = 0x12345678u;
    std::optional<std::set<uint16_t>> visibleOverride_;
    int animFrame_ = 0;
    const std::vector<uint8_t>* mask_ = nullptr;   // pixels not to draw (objects in front)
    int ymin_ = 0;
    int originY_ = 0;
    bool currentFromGame_ = false;
    int depth_ = 0;                                  // depth of the room being drawn
};

} // namespace dm
