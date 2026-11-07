#include "game/live_view.h"
#include "formats/rnc1.h"
#include "formats/game_files.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace dm::game {

namespace {
constexpr uint32_t kRoom = 0x1E6CC, kLevel = 0x1E688, kDrawList = 0x20A06;

struct Rec {
    uint16_t id; uint32_t inst, type;
    int sx, sy, desc; uint8_t flags;
    int x0, x1, y0, y1, zTop, z;
    int ix, iy, iz; uint32_t ilong;          // instance (current position)
    int tb16, tw18, tw20;                    // type
    bool noDraw;
};

inline int s16(uint16_t v) { return int(int16_t(v)); }
} // namespace

Bytes LiveView::loadMainCode() {
    const Bytes exe = rnc::unpack(readGameFile("MAIN.BIN"));
    if (exe.size() < 32 || be32(exe.data()) != 0x3F3) throw FormatError("MAIN.BIN: hunk header missing");
    size_t o = 24 + 4 * (be32(&exe[16]) - be32(&exe[12]));
    while (o + 8 <= exe.size()) {
        if ((be32(&exe[o]) & 0x3FFFFFFF) == 0x3E9) {
            const uint32_t n = be32(&exe[o + 4]) * 4;
            return Bytes(exe.begin() + long(o + 8), exe.begin() + long(o + 8 + n));
        }
        o += 4;
    }
    throw FormatError("MAIN.BIN: CODE hunk not found");
}

LiveView::LiveView(Bytes mainCode, int extraTop)
    : code_(mainCode), renderer_(std::move(mainCode)), extraTop_(extraTop) {}

bool LiveView::render(const amiga::Machine& m, IndexedFrame& f, int animFrame) {
    const uint32_t pb = m.gameBase();
    if (!pb) return false;
    const int level = m.peek8(pb + kLevel) + 1;
    const int room = m.peek16(pb + kRoom);
    if (level < 1 || level > 3 || room >= 0xFF) return false;
    const uint32_t list = m.peek32(pb + kDrawList);
    if (!list) return false;
    auto& lv = levels_[level];
    if (!lv) lv = std::make_unique<Level>(level, code_);
    room_ = room;

    // draw list of the game
    std::vector<Rec> recs;
    for (int k = 0; k < 160; ++k) {
        const uint32_t r = list + uint32_t(k) * 34;
        Rec c{};
        c.id = m.peek16(r);
        if (c.id == 0xFFFF) break;
        c.inst = m.peek32(r + 2); c.type = m.peek32(r + 8);
        c.sx = s16(m.peek16(r + 12)); c.sy = s16(m.peek16(r + 14)); c.desc = s16(m.peek16(r + 16));
        c.flags = m.peek8(r + 20);
        c.x0 = s16(m.peek16(r + 22)); c.x1 = s16(m.peek16(r + 24)); c.y0 = s16(m.peek16(r + 26)); c.y1 = s16(m.peek16(r + 28));
        c.zTop = s16(m.peek16(r + 30)); c.z = s16(m.peek16(r + 32));
        c.noDraw = c.id != 0xFFFD && c.id != 0xFFFE && m.peek16(c.type) == 0xFFFF;   // type without graphics: not drawn, but it masks ($0E23A)
        c.ilong = m.peek32(c.inst + 2);
        c.ix = s16(m.peek16(c.inst + 6)); c.iz = s16(m.peek16(c.inst + 8)); c.iy = s16(m.peek16(c.inst + 10));
        c.tb16 = m.peek8(c.type + 16); c.tw18 = s16(m.peek16(c.type + 18)); c.tw20 = s16(m.peek16(c.type + 20));
        recs.push_back(c);
    }
    // flames of the neighbouring rooms: same phase as the flames of the current room, animated by the game
    // (one frame every 3 game steps, that is every 6 video frames); without flames in the room the given
    // counter is used
    for (const Rec& c : recs)
        if (c.id != 0xFFFD && c.id != 0xFFFE && (c.flags & 0x40) && c.desc >= 0) {
            const int ph = lv->animPhase(uint16_t(c.desc));
            if (ph >= 0) { animFrame = ph; break; }
        }

    f.width = 320;
    f.height = 200 + extraTop_;
    f.px.assign(size_t(f.width) * size_t(f.height), 0);
    renderer_.setOriginY(extraTop_);
    renderer_.setCurrentRoomFromGame(true);
    renderer_.setDrawCharacters(false);
    renderer_.setAnimFrame(animFrame);
    // $0CCB0: room substituted by the script ($06582) and closed doors ($07B9E, table $1E768 of the level)
    const int subFrom = m.peek16(pb + 0x1E6CE), subTo = m.peek16(pb + 0x1E6D0);
    renderer_.setRoomState({
        [subFrom, subTo](int n) { return n == subFrom ? subTo : n; },
        [&m, pb](int n, RoomData& r) {
            uint32_t t = pb + 0x1E768;
            t += m.peek32(t + uint32_t(m.peek8(pb + kLevel)) * 4);
            for (int guard = 0; guard < 256 && m.peek8(t) != 0xFF; ++guard, t += 4) {
                if (m.peek8(t) != uint8_t(n)) continue;
                if (m.peek8(t + 3)) break;                       // door open
                const uint8_t kind = m.peek8(t + 2);
                for (int k = 0; k < 64; ++k) {
                    uint8_t& c = r[0xE8 + size_t(k)];
                    if ((c & 7) == kind) c = uint8_t((c & 0xF8) | 5);
                }
                break;
            }
        },
        [&m, pb](int n, std::vector<uint16_t>& out) {
            out.clear();
            if (n < 0 || n > 0xFE) return false;
            uint16_t h = m.peek16(pb + 0x200D6 + uint32_t(n) * 8 + 4);
            for (int guard = 0; h && guard < 512; ++guard) {
                if (std::find(out.begin(), out.end(), h) != out.end()) break;
                out.push_back(h);
                h = m.peek16(m.peek32(pb + 0x1FBE0) + uint32_t(h) * 14 + 12);
            }
            return true;
        },
        [&m, pb](uint16_t i, ObjectInstance& o) {
            const uint32_t a = m.peek32(pb + 0x1FBE0) + uint32_t(i) * 14;
            o.type = m.peek16(a); o.name = m.peek16(a + 2); o.flags = m.peek16(a + 4);
            o.x = uint16_t(m.peek16(a + 6) - 32); o.z = m.peek16(a + 8); o.y = uint16_t(m.peek16(a + 10) - 32);   // RAM = file + 32
            o.next = m.peek16(a + 12);
            return true;
        }});
    renderer_.render(*lv, room, f);
    f.px.resize(size_t(f.width) * size_t(f.height));   // render() uses the size of f

    const RoomData& rd = lv->room(room);
    const int xmin = be16(&rd[0]), xmax = be16(&rd[2]) & 0x7FFF;

    if (std::getenv("DM_LIVE_DEBUG"))
        for (const Rec& c : recs)
            std::fprintf(stderr, "id=%04X inst=%06X t=%06X sx=%d sy=%d d=%d fl=%02X x=%d..%d y=%d..%d zt=%d z=%d il=%08X i=%d,%d,%d\n",
                         c.id, c.inst, c.type, c.sx, c.sy, c.desc, c.flags, c.x0, c.x1, c.y0, c.y1, c.zTop, c.z, c.ilong, c.ix, c.iy, c.iz);
    // $0B21E: the projections of the two objects overlap
    auto overlap = [](const Rec& a, const Rec& b) {
        const int d0 = a.ix - a.iz - a.tb16, d1 = a.iy - a.iz - a.tb16, d2 = a.ix - a.iz + a.tw18, d3 = a.iy - a.iz + a.tw20;
        const int d4 = b.ix - b.iz - b.tb16, d5 = b.iy - b.iz - b.tb16, d6 = b.ix - b.iz + b.tw18, d7 = b.iy - b.iz + b.tw20;
        return !(d0 > d6 || d2 < d4 || d1 > d7 || d3 < d5);
    };
    const size_t npx = size_t(f.width) * size_t(f.height);
    std::vector<std::vector<uint8_t>> sil(recs.size());
    auto silOf = [&](size_t i) -> const std::vector<uint8_t>& {
        if (sil[i].empty()) {
            sil[i].assign(npx, 0);
            if (recs[i].desc >= 0) renderer_.silhouette(*lv, recs[i].sx, recs[i].sy, uint16_t(recs[i].desc), f, sil[i]);
        }
        return sil[i];
    };
    std::vector<uint8_t> mask(npx);
    // $0E204: first the fixed parts (masked only by fixed parts), then $0E28A: the moving parts (masked by everything)
    for (int pass = 0; pass < 2; ++pass)
        for (size_t ia = 0; ia < recs.size(); ++ia) {
            const Rec& a = recs[ia];
            if (a.id == 0xFFFD || a.id == 0xFFFE || a.desc < 0 || a.noDraw) continue;
            const bool dynamicA = a.flags & 6;
            if (dynamicA != (pass == 1)) continue;
            const int zA = (a.ilong & 0x4000) ? a.iz : a.z;
            std::fill(mask.begin(), mask.end(), uint8_t(0));
            bool any = false;
            for (size_t ib = 0; ib < ia; ++ib) {               // $0E0D0: only those before it in the list
                const Rec& b = recs[ib];
                if (b.id == 0xFFFD || b.id == 0xFFFE || b.desc < 0) continue;
                if (!dynamicA && (b.flags & 6)) continue;
                if (dynamicA && (a.flags & 0x10) && ib + 1 == ia && (b.flags & 8)) continue;   // the two parts of the same sprite
                if (b.ilong & 0x8000) continue;
                if (!(b.zTop > zA) || !(b.y1 > a.y0) || !(b.x1 > a.x0) || !overlap(a, b)) continue;
                const auto& sm = silOf(ib);
                for (size_t k = 0; k < npx; ++k) mask[k] |= sm[k];
                any = true;
            }
            renderer_.drawSprite(*lv, a.sx, a.sy, uint16_t(a.desc), xmin, xmax, f, any ? &mask : nullptr);
        }
    return true;
}

bool LiveView::roomUsed(int level, int room) {
    if (level < 1 || level > 3 || room < 0 || room > 254) return false;
    auto& lv = levels_[level];
    if (!lv) lv = std::make_unique<Level>(level, code_);
    const RoomData& r = lv->room(room);
    for (int i = 0xDF; i < 0xE3; ++i) if (r[size_t(i)] != 0xFF) return true;
    return false;
}

bool LiveView::freeCell(int level, int room, int& x, int& y) {
    if (!roomUsed(level, room)) return false;
    const RoomData& r = levels_[level]->room(room);
    double best = 1e9;
    for (int cc = 0; cc < 6; ++cc)
        for (int rr = 0; rr < 6; ++rr)
            if (r[0xE8 + size_t(cc + 1) * 8 + 1 + size_t(rr)] == 0) {
                const double d = std::abs(rr - 2.5) + std::abs(cc - 2.5);
                if (d < best) { best = d; x = rr * 32 + 48; y = cc * 32 + 48; }
            }
    return best < 1e9;
}

void LiveView::renderText(const amiga::Machine& m, const std::string& s, int x, int y, IndexedFrame& f) {
    const uint32_t pb = m.gameBase();
    if (!pb) return;
    const int li = m.peek8(pb + kLevel), level = li + 1;
    if (level < 1 || level > 3) return;
    auto& lv = levels_[level];
    if (!lv) lv = std::make_unique<Level>(level, code_);
    for (size_t i = 0; i < s.size(); ++i, x += 8) {
        const uint8_t c = uint8_t(s[i]);
        int d;
        if (c & 0x80) d = (c == 0xE1 ? 0 : c - 0x80) + m.peek16(pb + (c == 0xE1 ? 0x1A686 : 0x1A67E) + uint32_t(li) * 2);
        else if (c > 0x20) d = c - 0x20 + m.peek16(pb + 0x1A676 + uint32_t(li) * 2);
        else continue;
        renderer_.drawSprite(*lv, x, y, uint16_t(d), 0, f.width, f, nullptr);
    }
}

void LiveView::renderDescriptor(const amiga::Machine& m, int x, int y, uint16_t d, IndexedFrame& f) {
    const uint32_t pb = m.gameBase();
    if (!pb) return;
    const int level = m.peek8(pb + kLevel) + 1;
    if (level < 1 || level > 3) return;
    auto& lv = levels_[level];
    if (!lv) lv = std::make_unique<Level>(level, code_);
    renderer_.drawSprite(*lv, x, y, d, 0, f.width, f, nullptr);
}

} // namespace dm::game
