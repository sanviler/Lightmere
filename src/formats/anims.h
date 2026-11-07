#pragma once
// Character definitions and animation sets (Darkmere executable, CODE hunk).
// Tables indexed by the entity type (low byte of the instance flags):
//   $1D7DC behaviour routine, $1D8EC definition (actions), $1D9FC statistics and base of the offsets.
// Definition: for every action at offset X: [X-2] frames, [X] and [X+2] signed offsets from $1D9FC
// of the body set and of the second part. A set: 4 pointers (directions) to descriptor sequences
// ended by $FFFE/$FFFF. Walking has one more frame (index 8 = standing).
#include "formats/common.h"
#include <array>
#include <optional>

namespace dm {

enum class Action : uint8_t { Walk = 0x04, A14 = 0x14, A1E = 0x1E, A28 = 0x28, A32 = 0x32, A3C = 0x3C };

struct AnimSet {                                   // a set: 4 directions
    std::array<std::vector<uint16_t>, 4> dirs;
};

struct ActionAnim {
    int frames = 0;
    std::optional<AnimSet> body, second;
};

struct CharacterDef {
    uint32_t definition = 0, behaviour = 0, stats = 0;
    std::array<std::optional<ActionAnim>, 6> actions;   // in the order of kActions
};

constexpr std::array<Action, 6> kActions = {Action::Walk, Action::A14, Action::A1E, Action::A28, Action::A32, Action::A3C};

class Animations {
public:
    explicit Animations(const Bytes& mainCode, int kinds = 64);
    const std::optional<CharacterDef>& character(int kind) const;
    // $10340: action chosen by the entity flags (+58, +59)
    static Action actionFor(uint8_t f58, uint8_t f59);
    // $1039E: drawn descriptor (part 0 = body, 1 = second part); $FFFF if none
    uint16_t descriptor(int kind, uint8_t f58, uint8_t f59, int direction, int frame, int part = 0) const;

    struct Pose { Action action; int direction, frame; };
    // first body pose using the descriptor (the first pose chosen by the authors is the type's descriptor)
    std::optional<Pose> findPose(int kind, uint16_t desc) const;
    // descriptor of a given pose; $FFFF if none
    uint16_t descriptorAt(int kind, const Pose& p, int part = 0) const;

private:
    Bytes code_;                                     // a copy: no dangling reference
    std::vector<std::optional<CharacterDef>> defs_;
    std::optional<AnimSet> readSet(uint32_t off, int frames) const;
};

} // namespace dm
