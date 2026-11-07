#pragma once
// Data of a Darkmere level (lev<L>000.epf + gfx<L>NNN.epf), rebuilt from the original routines.
//
// lev_000 (routine $0D54A):
//   [u16 n1][n1 x 16 descriptors][u16 n2][n2 x 4 T2][u16 n3][n3 x 8 T3][u16 n4][n4 x 22]
//   [u16 n5][n5 x 14][u16 n6][n6 x 6][256][256][u16 n7][n7 x 4][256 x 4 room clipping]
// lev_001 (routine $0CD50): 256 Huffman-packed rooms, 296 bytes each.
// Descriptor i <-> global graphic entry i (archive gfx<L><i/100>, entry i%100).
// Graphics have 4 bitplanes in reverse order. Derived descriptors follow three tables
// per level in the executable (source -> destination pairs, routine $0B0B4):
//   $1A374 alias    (flag $2000): same graphic as the source
//   $195A8 mirror   (flag $8000): source flipped horizontally; dx -= round16(w)-w
//   $19EA8 xor      (flag $4000): file data XOR source graphic (blitter minterm $3C)
// Checked byte by byte on 2,754 graphics loaded in RAM in 10 save states.
#include "formats/palette.h"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace dm {

class EpfArchive;

struct Descriptor {
    uint16_t ref = 0;      // +0  reference/variant (in RAM: pointer to the graphics cache)
    uint16_t group = 0;    // +2  group
    int16_t dx = 0, dy = 0;// +4  hotspot (added to the position)
    uint16_t w = 0, h = 0; // +8  size in pixels
    uint8_t frames = 0;    // +14 number of frames
    uint8_t flags = 0;     // +15
};

struct Sprite {
    int width = 0, height = 0;           // width rounded up to 16
    std::vector<uint8_t> indices;        // 0 = transparent
};

// Object instance (lev_000, 14-byte record). File coordinates (+32 in RAM).
struct ObjectInstance {
    uint16_t type = 0, name = 0, flags = 0;
    uint16_t x = 0, z = 0, y = 0;
    uint16_t next = 0;                   // index of the next one in the room list, 0 = end
};
using ObjectType = std::array<uint8_t, 22>;  // word 0 = graphic descriptor; bytes 10 and 17 for the projection

constexpr int kRoomSize = 0x128;          // 296 byte
using RoomData = std::array<uint8_t, kRoomSize>;

class Level {
public:
    // level: 1..3 (files lev*.epf and gfx*.epf from the game files); mainCode: CODE hunk of MAIN.BIN
    Level(int level, const Bytes& mainCode);
    ~Level();

    int number() const { return level_; }
    const RoomData& room(int n);                       // decoding + cache
    const std::vector<Descriptor>& descriptors() const { return desc_; }
    uint16_t floorDescriptor(uint16_t id) const { return id < t2_.size() ? t2_[id] : 0xFFFF; }
    uint16_t wallDescriptor(uint16_t id) const { return id < t3_.size() ? t3_[id] : 0xFFFF; }
    bool wallsAnchoredAtBase() const { return t3flag_ == 0; }
    const std::array<uint8_t, 4>& roomClip(int n) const { return clip_[size_t(n & 0xFF)]; }
    const Sprite* sprite(uint16_t descriptor);         // nullptr if not available
    int drawDx(uint16_t descriptor) const;             // hotspot x with the corrections of $0D812
    uint16_t wallOverlay(uint16_t descriptor) const;   // $0EF62: overlaid element (door leaf), $FFFF if none
    uint16_t wallFlame(uint16_t descriptor) const;     // $0EE6A: flame of the wall torch, $FFFF if none
    uint16_t animFrame(uint16_t descriptor, int frame) const; // frame of an animated element (list $1AAEE)
    int animPhase(uint16_t descriptor) const;                 // position of a frame in its cycle, -1 if not animated
    const std::vector<ObjectType>& objectTypes() const { return types_; }
    const std::vector<ObjectInstance>& instances() const { return instances_; }
    std::vector<uint16_t> roomObjects(int n);          // room list (head = word +4 of the room)
    uint8_t spawnMax(int n) const { return spawnMax_[size_t(n & 0xFF)]; } // $1FBE8: maximum of visible characters
    bool isEntity(uint16_t inst) const { return entities_.count(inst) != 0; } // character: it moves, drawn separately

private:
    Bytes rawGfx(uint16_t descriptor);
    const Bytes* planes(uint16_t descriptor, int depth = 0);  // 4 raw planes, transformations applied
    std::map<uint16_t, uint16_t> alias_, mirror_, xor_;       // destination -> source
    std::map<uint16_t, int> dxFix_;                           // hotspot corrections (mirrors and their aliases)
    std::map<uint16_t, uint16_t> overlay_;                    // table $1AB24
    std::map<uint16_t, uint16_t> flame_;                      // table $1A9D8
    std::vector<uint16_t> animBases_;                         // list $1AAEE
    std::map<uint16_t, std::optional<Bytes>> planeCache_;
    int level_;
    Bytes rooms_;                                      // raw lev_001
    std::vector<Descriptor> desc_;
    std::vector<uint16_t> t2_, t3_;
    std::vector<ObjectType> types_;
    std::vector<ObjectInstance> instances_;
    std::set<uint16_t> entities_;
    uint8_t t3flag_ = 0;
    std::array<std::array<uint8_t, 4>, 256> clip_{};
    std::array<uint8_t, 256> spawnMax_{};
    std::map<int, RoomData> roomCache_;
    std::map<uint16_t, std::optional<Sprite>> spriteCache_;
    std::map<int, std::unique_ptr<EpfArchive>> gfx_;
};

// Decoding of a room, faithful to routine $0CD50.
RoomData decodeRoom(const Bytes& lev001, int n);

} // namespace dm
