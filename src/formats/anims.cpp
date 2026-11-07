#include "formats/anims.h"

namespace dm {
namespace {
constexpr uint32_t kBehaviour = 0x1D7DC, kDefinition = 0x1D8EC, kBase = 0x1D9FC;
int s16(uint16_t v) { return int(int16_t(v)); }
} // namespace

std::optional<AnimSet> Animations::readSet(uint32_t off, int frames) const {
    AnimSet s;
    for (int k = 0; k < 4; ++k) {
        if (off + 4 * k + 4 > code_.size()) return std::nullopt;
        const uint32_t p = be32(&code_[off + 4 * k]);
        if (p == 0 || p >= code_.size()) return std::nullopt;
        for (int f = 0; f < frames && p + 2 * f + 2 <= code_.size(); ++f) {
            const uint16_t v = be16(&code_[p + 2 * f]);
            if (v >= 0xFFFE) break;                      // end of sequence
            s.dirs[k].push_back(v);
        }
    }
    return s;
}

Animations::Animations(const Bytes& code, int kinds) : code_(code), defs_(size_t(kinds)) {
    for (int k = 0; k < kinds; ++k) {
        const uint32_t dfn = be32(&code[kDefinition + 4 * k]);
        if (dfn == 0 || dfn >= code.size()) continue;
        CharacterDef c;
        c.definition = dfn;
        c.behaviour = be32(&code[kBehaviour + 4 * k]);
        c.stats = be32(&code[kBase + 4 * k]);
        for (size_t i = 0; i < kActions.size(); ++i) {
            const uint32_t off = uint32_t(kActions[i]);
            const int n = be16(&code[dfn + off - 2]);
            if (n == 0 || n > 40) continue;
            ActionAnim a;
            a.frames = n;
            const int count = kActions[i] == Action::Walk ? n + 1 : n;
            for (int part = 0; part < 2; ++part) {
                const int rel = s16(be16(&code[dfn + off + 2 * part]));
                if (rel == 0) continue;
                (part ? a.second : a.body) = readSet(uint32_t(int(kBase) + rel), count);
            }
            c.actions[i] = std::move(a);
        }
        defs_[size_t(k)] = std::move(c);
    }
}

const std::optional<CharacterDef>& Animations::character(int kind) const {
    static const std::optional<CharacterDef> none;
    return kind >= 0 && size_t(kind) < defs_.size() ? defs_[size_t(kind)] : none;
}

Action Animations::actionFor(uint8_t f58, uint8_t f59) {
    if (f58 & 0x08) return Action::A1E;
    if (f58 & 0x10) return Action::A28;
    if (f58 & 0x20) return Action::A32;
    if (f58 & 0x04) return Action::A14;
    if (f59 & 0x08) return Action::A3C;
    return Action::Walk;
}

uint16_t Animations::descriptor(int kind, uint8_t f58, uint8_t f59, int direction, int frame, int part) const {
    const auto& c = character(kind);
    if (!c) return 0xFFFF;
    const uint32_t off = uint32_t(actionFor(f58, f59)) + 2u * uint32_t(part);
    const int rel = s16(be16(&code_[c->definition + off]));
    if (rel == 0) return 0xFFFF;
    const uint32_t ps = uint32_t(int(kBase) + rel) + 4u * uint32_t(direction & 3);
    if (ps + 4 > code_.size()) return 0xFFFF;
    const uint32_t p = be32(&code_[ps]) + 2u * uint32_t(frame);
    return p + 2 <= code_.size() ? be16(&code_[p]) : 0xFFFF;
}

std::optional<Animations::Pose> Animations::findPose(int kind, uint16_t desc) const {
    const auto& c = character(kind);
    if (!c) return std::nullopt;
    for (size_t i = 0; i < kActions.size(); ++i) {
        const auto& a = c->actions[i];
        if (!a || !a->body) continue;
        for (int d = 0; d < 4; ++d) {
            const auto& seq = a->body->dirs[size_t(d)];
            for (size_t f = 0; f < seq.size(); ++f)
                if (seq[f] == desc) return Pose{kActions[i], d, int(f)};
        }
    }
    return std::nullopt;
}

uint16_t Animations::descriptorAt(int kind, const Pose& p, int part) const {
    const auto& c = character(kind);
    if (!c) return 0xFFFF;
    const int rel = s16(be16(&code_[c->definition + uint32_t(p.action) + 2u * uint32_t(part)]));
    if (rel == 0) return 0xFFFF;
    const uint32_t ps = uint32_t(int(kBase) + rel) + 4u * uint32_t(p.direction & 3);
    if (ps + 4 > code_.size()) return 0xFFFF;
    const uint32_t q = be32(&code_[ps]) + 2u * uint32_t(p.frame);
    if (q + 2 > code_.size()) return 0xFFFF;
    const uint16_t v = be16(&code_[q]);
    return v >= 0xFFFE ? uint16_t(0xFFFF) : v;
}

} // namespace dm
