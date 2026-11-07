#include "engine/room_renderer.h"
#include "formats/spawn.h"
#include <algorithm>
#include <set>
#include <vector>

namespace dm {

namespace {
constexpr uint32_t kOrderTable = 0x1A6E6;   // 11 x (r0, c0, count)
constexpr uint32_t kArrowTable = 0x1A68E;   // arrow descriptor per level
constexpr uint32_t kPaletteTable = 0x17F2E; // pointers to the palettes (relative to the hunk)
inline int s8(uint8_t v) { return v > 127 ? int(v) - 256 : int(v); }
}

RoomData RoomRenderer::roomData(Level& lv, int n) {
    RoomData r = lv.room(n);
    if (state_.patchRoom) state_.patchRoom(n, r);
    return r;
}

void RoomRenderer::render(Level& lv, int room, IndexedFrame& f) {
    std::fill(f.px.begin(), f.px.end(), uint8_t(0));
    const RoomData r = roomData(lv, mapRoom(room));
    const uint32_t pp = be32(&code_.at(kPaletteTable + size_t(r[0xE7]) * 4));
    f.palette = Palette::fromOcs(&code_.at(pp), 32); // 16-31: object colours
    ymin_ = originY_ ? 0 : 8; // in the original frames rows 0-7 are always black (checked on 6 save states)
    visit(lv, room, 0, 0, 0, be16(&r[0]), be16(&r[2]) & 0x7FFF, f);
}

void RoomRenderer::visit(Level& lv, int n, int depth, int X, int Y, int xmin, int xmax, IndexedFrame& f) {
    n = mapRoom(n);
    const RoomData r = roomData(lv, n); // a copy: the cache can grow during the recursion
    xmin = std::max(xmin, int(be16(&r[0])) + X * 16);
    xmax = std::min(xmax, int(be16(&r[2]) & 0x7FFF) + X * 16);
    if (depth <= 4) {
        struct Link { uint8_t room, ex, ey; int c0, c1; };
        const Link links[2] = {{r[0xDF], r[0xE3], r[0xE4], 2, 3}, {r[0xE0], r[0xE5], r[0xE6], 0, 1}};
        for (const Link& l : links) {
            if (l.ex == 0x7F || l.ey == 0x7F || l.room == 0xFF) continue;
            const auto& c = lv.roomClip(n);
            int nx0 = std::max(xmin, c[size_t(l.c0)] * 8 + X * 16);
            int nx1 = std::min(xmax, c[size_t(l.c1)] * 8 + X * 16);
            visit(lv, l.room, depth + 1, X + s8(l.ex), Y + s8(l.ey), nx0, nx1, f);
        }
    }
    if (n < 0xFF) {
        depth_ = depth;
        drawRoom(lv, n, depth, X, Y, xmin, xmax, f);
        if (drawObjects_ && !(currentFromGame_ && depth == 0)) drawRoomObjects(lv, n, X, Y, xmin, xmax, f);
    }
}

void RoomRenderer::drawRoom(Level& lv, int n, int depth, int X, int Y, int xmin, int xmax, IndexedFrame& f) {
    if (xmin >= xmax) return;
    const RoomData r = roomData(lv, n);
    const int ox = X * 16, oy = Y * 16;
    const uint16_t arrow = be16(&code_.at(kArrowTable + size_t(lv.number() - 1) * 2));
    for (int k = 0; k < 11; ++k) {
        const uint8_t* t = &code_.at(kOrderTable + size_t(k) * 3);
        int rr = t[0] + t[2] - 1, cc = t[1] - t[2] + 1;
        for (int i = 0; i < t[2]; ++i, --rr, ++cc) {
            const size_t cell = 6 + size_t(rr * 6 + cc) * 6;
            const uint16_t floor = be16(&r[cell]), wr = be16(&r[cell + 2]), wl = be16(&r[cell + 4]);
            if (floor == 0xFFFF) continue;
            const int x = 128 + 32 * (rr - cc) + ox, y = 16 * (rr + cc) + oy;
            const size_t d6 = 0xE8 + size_t(cc + 1) * 8 + 1 + size_t(rr);
            struct Wall { uint16_t id; int x; size_t neighbour; };
            const Wall walls[2] = {{wr, x + 32, d6 - 8}, {wl, x, d6 - 1}};
            for (const Wall& w : walls) {
                if (w.id == 0xFFFF) continue;
                const uint16_t d = lv.wallDescriptor(w.id);
                if (d >= lv.descriptors().size()) continue;
                int yy = y;
                if (lv.wallsAnchoredAtBase()) yy = yy - lv.descriptors()[d].h + 16;
                drawDescriptor(lv, w.x, yy - 10, d, xmin, xmax, f);
                // $0EF62: with a neighbouring cell of type 5 the door leaf is drawn over
                if ((r[w.neighbour] & 7) == 5) {
                    const uint16_t o = lv.wallOverlay(d);
                    if (o != 0xFFFF) drawDescriptor(lv, w.x, yy - 10, o, xmin, xmax, f);
                }
                const uint16_t fl = lv.wallFlame(d);         // $0EE6A: flame of the torch
                if (fl != 0xFFFF && !(currentFromGame_ && depth == 0))
                    drawDescriptor(lv, w.x, yy - 10, lv.animFrame(fl, animFrame_), xmin, xmax, f);
            }
            drawDescriptor(lv, x, y - 10, lv.floorDescriptor(floor), xmin, xmax, f);
            if (depth == 0) {
                const uint8_t fm = r[0xE8 + size_t(cc + 1) * 8 + 1 + rr];
                if (fm & 0x10) drawDescriptor(lv, x, y - 10, arrow, xmin, xmax, f);
                if (fm & 0x20) drawDescriptor(lv, x, y - 10, uint16_t(arrow + 1), xmin, xmax, f);
            }
        }
    }
}

void RoomRenderer::drawDescriptor(Level& lv, int x, int y, uint16_t d, int xmin, int xmax, IndexedFrame& f) {
    if (d >= lv.descriptors().size()) return;
    const Sprite* s = lv.sprite(d);
    if (!s) return;
    const uint8_t po = lv.descriptors()[d].flags ? 0 : 16; // flag 0: colours 16-31 of the palette
    x += lv.drawDx(d);
    y += lv.descriptors()[d].dy + originY_;
    for (int yy = 0; yy < s->height; ++yy) {
        const int sy = y + yy;
        if (sy < ymin_ || sy >= f.height) continue;
        for (int xx = 0; xx < s->width; ++xx) {
            const int sx = x + xx;
            if (sx < xmin || sx >= xmax || sx < 0 || sx >= f.width) continue;
            const uint8_t v = s->indices[size_t(yy) * s->width + xx];
            if (v && !(mask_ && (*mask_)[size_t(sy) * f.width + sx])) f.px[size_t(sy) * f.width + sx] = uint8_t(v + po);
        }
    }
}

// $0DEB4: screen position of the top left corner of an object's graphic.
// RAM coordinates minus 32 = file coordinates. Bytes 17 and 10 of the type adjust the anchor.
void RoomRenderer::objectPosition(const Level& lv, uint16_t i, int X, int Y, int& sx, int& sy) {
    objectPosition(lv, lv.instances()[i], X, Y, sx, sy);
}

void RoomRenderer::objectPosition(const Level& lv, const ObjectInstance& o, int X, int Y, int& sx, int& sy) {
    const ObjectType& t = lv.objectTypes()[o.type];
    const int h = o.x >> 1, v = o.y >> 1;
    int d5 = h + v, d6 = 2 * h - 2 * v;
    const int k = int(((uint32_t(t[17]) << 16 >> 3) / 42 >> 1) >> 8);
    d6 -= 2 * k;
    d5 += k - t[10];
    sx = X * 16 + 160 + d6;
    sy = Y * 16 + d5 - int(o.z) - 10;
}

// $0E0D0: objects are drawn in the order of the room list; each one (A) is masked by the
// silhouettes of the objects before it in the list and in front of it: zTop(B) > z(A), y1(B) > y0(A),
// x1(B) > x0(A) and overlapping projections ($0B21E). Later objects are simply drawn on top.
// The boxes are in RAM coordinates (file + 32), as in $0E006.
void RoomRenderer::drawRoomObjects(Level& lv, int n, int X, int Y, int xmin, int xmax, IndexedFrame& f) {
    struct Item { uint16_t desc; int x, y; int x0, x1, y0, y1, zBase, zTop, h16; bool skip; std::vector<uint8_t> mask; };
    std::vector<Item> items;
    std::vector<uint16_t> list;
    if (!state_.roomObjects || !state_.roomObjects(n, list)) list = lv.roomObjects(n);
    auto inst = [&](uint16_t i) {
        ObjectInstance o{};
        if (state_.instance && state_.instance(i, o)) return o;
        return i < lv.instances().size() ? lv.instances()[i] : ObjectInstance{};
    };
    // entities of the room (bit 9 and type != 0) in instance order: spawn rule $10984
    std::vector<uint16_t> ents;
    for (uint16_t i : list) {
        const uint16_t fl = inst(i).flags;
        if ((fl & 0x200) && (fl & 0xFF)) ents.push_back(i);
    }
    std::sort(ents.begin(), ents.end());
    std::set<uint16_t> shown;
    if (drawCharacters_) {
        if (visibleOverride_) shown = *visibleOverride_;
        else {
            std::vector<int> kinds; std::vector<uint8_t> f59;
            for (uint16_t i : ents) {
                kinds.push_back(inst(i).flags & 0xFF);
                f59.push_back(initialEntityFlags59(code_, kinds.back()));
            }
            SpawnRng rng(spawnSeed_ ^ uint32_t(n));
            for (int j : spawnVisible(kinds, f59, lv.spawnMax(n), rng)) shown.insert(ents[size_t(j)]);
        }
    }
    for (uint16_t i : list) {
        const ObjectInstance o = inst(i);
        if (o.type >= lv.objectTypes().size()) continue;
        const ObjectType& t = lv.objectTypes()[o.type];
        uint16_t d = be16(&t[0]), second = 0xFFFF;
        if (drawCharacters_ && std::binary_search(ents.begin(), ents.end(), i)) {
            if (!shown.count(i)) continue;                // hidden by the spawn rule
            const int kind = o.flags & 0xFF;
            // character: the first pose is the type's descriptor; the second part (fireplace flame,
            // arms...) is the one of the same action, direction and frame
            if (const auto pose = anims_.findPose(kind, d)) {
                const uint16_t v = anims_.descriptorAt(kind, *pose, 1);
                if (v < lv.descriptors().size()) second = v;
            }
        } else if (o.flags & 0x800) continue;             // creature (characters disabled)
        if (d >= lv.descriptors().size()) continue;
        Item it{};
        it.desc = d;
        objectPosition(lv, o, X, Y, it.x, it.y);
        const uint32_t lf = (uint32_t(o.name) << 16) | o.flags;  // long at +2 of the instance
        it.x0 = o.x + 32; it.x1 = it.x0 + be16(&t[18]);
        it.y0 = o.y + 32; it.y1 = it.y0 + be16(&t[20]);
        it.zBase = int(o.z);  // the instance z is used in the comparison
        it.zTop = ((lf & 0x200) && (lf & 0x10)) ? 0x7F : int(o.z) + t[16];
        it.h16 = t[16];
        it.skip = (lf & 0x8000) != 0;
        auto silhouette = [&](Item& item) {             // silhouette with the hotspot, as drawDescriptor
            item.mask.assign(size_t(f.width) * size_t(f.height), 0);
            const uint16_t dd = item.desc;
            if (const Sprite* s = lv.sprite(dd)) {
                const int bx = item.x + lv.drawDx(dd), by = item.y + lv.descriptors()[dd].dy + originY_;
                for (int yy = 0; yy < s->height; ++yy)
                    for (int xx = 0; xx < s->width; ++xx) {
                        const int sx = bx + xx, sy = by + yy;
                        if (sx >= 0 && sx < f.width && sy >= 0 && sy < f.height && s->indices[size_t(yy) * s->width + xx])
                            item.mask[size_t(sy) * f.width + sx] = 1;
                    }
            }
        };
        silhouette(it);
        Item two = it;
        items.push_back(std::move(it));
        if (second != 0xFFFF) { two.desc = second; silhouette(two); items.push_back(std::move(two)); }
    }
    std::vector<uint8_t> cover(size_t(f.width) * size_t(f.height));
    // $0B21E: the projections (x - z, y - z) of the two boxes must overlap
    auto overlap = [](const Item& a, const Item& b) {
        auto lo = [](const Item& o, int v) { return v - o.zBase - o.h16; };
        auto hi = [](const Item& o, int v, int size) { return v - o.zBase + size; };
        return !(lo(a, a.x0) > hi(b, b.x0, b.x1 - b.x0) || hi(a, a.x0, a.x1 - a.x0) < lo(b, b.x0) ||
                 lo(a, a.y0) > hi(b, b.y0, b.y1 - b.y0) || hi(a, a.y0, a.y1 - a.y0) < lo(b, b.y0));
    };
    for (size_t ia = 0; ia < items.size(); ++ia) {
        const Item& a = items[ia];
        std::fill(cover.begin(), cover.end(), uint8_t(0));
        for (size_t ib = 0; ib < ia; ++ib) {          // only the objects before a in the list
            const Item& b = items[ib];
            if (b.skip) continue;
            if (b.zTop > a.zBase && b.y1 > a.y0 && b.x1 > a.x0 && overlap(a, b))
                for (size_t k = 0; k < cover.size(); ++k) cover[k] |= b.mask[k];
        }
        mask_ = &cover;
        drawDescriptor(lv, a.x, a.y, a.desc, xmin, xmax, f);
        mask_ = nullptr;
    }
}

void RoomRenderer::drawSprite(Level& lv, int x, int y, uint16_t d, int xmin, int xmax, IndexedFrame& f, const std::vector<uint8_t>* mask) {
    mask_ = mask;
    drawDescriptor(lv, x, y, d, xmin, xmax, f);
    mask_ = nullptr;
}

void RoomRenderer::silhouette(Level& lv, int x, int y, uint16_t d, IndexedFrame& f, std::vector<uint8_t>& out) const {
    if (d >= lv.descriptors().size()) return;
    const Sprite* s = lv.sprite(d);
    if (!s) return;
    const int bx = x + lv.drawDx(d), by = y + lv.descriptors()[d].dy + originY_;
    for (int yy = 0; yy < s->height; ++yy)
        for (int xx = 0; xx < s->width; ++xx) {
            const int sx = bx + xx, sy = by + yy;
            if (sx >= 0 && sx < f.width && sy >= 0 && sy < f.height && s->indices[size_t(yy) * s->width + xx])
                out[size_t(sy) * f.width + sx] = 1;
        }
}

} // namespace dm
