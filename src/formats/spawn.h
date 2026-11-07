#pragma once
// Characters shown on entering a room (routine $10984) and their first pose ($115B8).
//  - Every entity of the room is hidden; those with bit 2 of +59 stay (initial value = byte 2
//    of the type statistics, $1D9FC[type]+2): corpses and fixed characters.
//  - If there is at least one, types $1B and $25 appear too; if fewer than the room maximum, all appear.
//  - Otherwise min(maximum, number) of them appear, chosen at random ($0F4CA).
// The maximum per room is table $1FBE8 of the level file (Level::spawnMax).
#include "formats/common.h"
#include "formats/level.h"

namespace dm {

class SpawnRng {                                      // $0F4CA: b += a; a += b; b = swap(b); result a
public:
    explicit SpawnRng(uint32_t a = 0x12345678u, uint32_t b = 0x9ABCDEF0u) : a_(a), b_(b) {}
    uint32_t next() { b_ += a_; a_ += b_; b_ = (b_ << 16) | (b_ >> 16); return a_; }
private:
    uint32_t a_, b_;
};

uint8_t initialEntityFlags59(const Bytes& code, int kind);
// kinds/f59: entities of the room in instance order; returns the visible indices
std::vector<int> spawnVisible(const std::vector<int>& kinds, const std::vector<uint8_t>& f59, int maxVisible, SpawnRng& rng);
// first direction from RAM coordinates (file + 32): 2 if the wall towards y is nearer, otherwise 1
int initialDirection(const RoomData& room, int x, int y);

} // namespace dm
