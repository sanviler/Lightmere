#include "formats/level.h"
#include "formats/game_files.h"
#include <algorithm>
#include "formats/epf.h"
#include <cstdio>

namespace dm {

RoomData decodeRoom(const Bytes& b, int n) {
    if (b.size() < 0x404 || n < 0 || n > 255) throw FormatError("room: invalid data");
    const uint32_t start = be32(&b[size_t(n) * 4]);
    const size_t tree = 0x402, stream = tree + be16(&b[0x400]);
    RoomData out{};
    uint32_t bit = start;
    int node = 0;
    for (size_t produced = 0; produced < out.size();) {
        size_t wi = stream + (bit >> 4) * 2;
        uint16_t word = wi + 1 < b.size() ? be16(&b[wi]) : 0;
        if (uint16_t(word << (bit & 15)) & 0x8000) node += 2;
        ++bit;
        if (tree + size_t(node) + 1 >= b.size()) throw FormatError("room: invalid tree");
        int16_t v = int16_t(be16(&b[tree + size_t(node)]));
        if (v < 0) { out[produced++] = uint8_t(v & 0xFF); node = 0; }
        else node = v;
    }
    return out;
}

namespace {
std::vector<std::pair<uint16_t, uint16_t>> pairVector(const Bytes& code, uint32_t table, int level) {
    std::vector<std::pair<uint16_t, uint16_t>> out;   // (source, destination) in the original order
    const size_t at = table + size_t(level - 1) * 4;
    if (at + 4 > code.size()) return out;
    for (size_t p = be32(&code[at]); p + 4 <= code.size(); p += 4) {
        const uint16_t src = be16(&code[p]), dst = be16(&code[p + 2]);
        if (src == 0xFFFF) break;
        out.emplace_back(src, dst);
    }
    return out;
}
std::map<uint16_t, uint16_t> pairList(const Bytes& code, uint32_t table, int level) {
    std::map<uint16_t, uint16_t> out;
    for (auto [s, d] : pairVector(code, table, level)) out[d] = s;
    return out;
}
} // namespace

Level::Level(int level, const Bytes& code) : level_(level) {
    alias_ = pairList(code, 0x1A374, level);
    mirror_ = pairList(code, 0x195A8, level);
    xor_ = pairList(code, 0x19EA8, level);
    for (auto [s, d] : pairVector(code, 0x1AB24, level)) overlay_[s] = d; // wall -> overlaid element
    for (auto [s, d] : pairVector(code, 0x1A9D8, level)) flame_[s] = d;   // wall -> animated flame ($0EE6A)
    {   // $1AAEE: animated descriptors of the level; each one animates up to the next one in the list
        uint32_t p = be32(&code.at(0x1AAEE + size_t(level - 1) * 4));
        while (be16(&code.at(p)) != 0xFFFF) { animBases_.push_back(be16(&code.at(p))); p += 2; }
    }
    char name[64];
    std::snprintf(name, sizeof name, "lev%d%d000.epf", level, level);
    EpfArchive lev(readGameFile(name));
    Bytes d = lev.entry(0);
    rooms_ = lev.entry(1);

    size_t o = 0;
    auto u16 = [&](size_t at) { if (at + 2 > d.size()) throw FormatError("lev_000 truncated"); return be16(&d[at]); };
    const size_t n1 = u16(o); o += 2;
    desc_.resize(n1);
    for (size_t i = 0; i < n1; ++i, o += 16) {
        Descriptor& e = desc_[i];
        e.ref = u16(o); e.group = u16(o + 2);
        e.dx = int16_t(u16(o + 4)); e.dy = int16_t(u16(o + 6));
        e.w = u16(o + 8); e.h = u16(o + 10);
        e.frames = d[o + 14]; e.flags = d[o + 15];
    }
    const size_t n2 = u16(o); o += 2;
    for (size_t i = 0; i < n2; ++i) t2_.push_back(u16(o + i * 4));
    o += n2 * 4;
    const size_t n3 = u16(o); o += 2;
    t3flag_ = d[o + 1];
    for (size_t i = 0; i < n3; ++i) t3_.push_back(u16(o + i * 8 + 2));
    o += n3 * 8;
    {   // object types (22 bytes) and instances (14 bytes: type, name, flags, x, z, y, next)
        const size_t n4 = u16(o); o += 2;
        types_.resize(n4);
        for (size_t i = 0; i < n4; ++i) std::copy_n(&d.at(o + i * 22), 22, types_[i].begin());
        o += n4 * 22;
        const size_t n5 = u16(o); o += 2;
        instances_.resize(n5);
        for (size_t i = 0; i < n5; ++i) {
            ObjectInstance& e = instances_[i];
            e.type = u16(o + i * 14); e.name = u16(o + i * 14 + 2); e.flags = u16(o + i * 14 + 4);
            e.x = u16(o + i * 14 + 6); e.z = u16(o + i * 14 + 8); e.y = u16(o + i * 14 + 10);
            e.next = u16(o + i * 14 + 12);
        }
        o += n5 * 14;
    }
    o += 2 + size_t(u16(o)) * 6;
    for (size_t k = 0; k < 256; ++k) spawnMax_[k] = d[o + k];   // $1FBE8: visible characters per room
    o += 0x200;
    {   // entities (characters and containers with an inventory): [instance][inventory head]
        const size_t n7 = u16(o); o += 2;
        for (size_t i = 0; i < n7; ++i) entities_.insert(uint16_t(u16(o + i * 4) & 0x7FFF));
        o += n7 * 4;
    }
    if (o + 256 * 4 > d.size()) throw FormatError("lev_000: clipping table missing");
    for (size_t k = 0; k < 256; ++k)
        for (size_t j = 0; j < 4; ++j) clip_[k][j] = d[o + k * 4 + j];

    // Hotspot corrections as in $0D812: a mirror pair is skipped if its target is also
    // the target of an alias; the FIRST alias of the target gets the same correction.
    const auto aliasPairs = pairVector(code, 0x1A374, level);
    for (auto [s, dst] : pairVector(code, 0x195A8, level)) {
        if (alias_.count(dst) || s >= desc_.size()) continue;
        const int w = desc_[s].w, pad = ((w + 15) & ~15) - w;
        dxFix_[dst] -= pad;
        for (auto [as, ad] : aliasPairs)
            if (as == dst) { dxFix_[ad] -= pad; break; }
    }
}

Level::~Level() = default;

const RoomData& Level::room(int n) {
    auto it = roomCache_.find(n);
    if (it != roomCache_.end()) return it->second;
    return roomCache_.emplace(n, decodeRoom(rooms_, n)).first->second;
}

Bytes Level::rawGfx(uint16_t i) {
    if (i >= desc_.size()) return {};
    const int arch = i / 100, entry = i % 100;
    auto it = gfx_.find(arch);
    if (it == gfx_.end()) {
        char name[64];
        std::snprintf(name, sizeof name, "gfx%d%d%03d.epf", level_, level_, arch);
        std::unique_ptr<EpfArchive> a;
        try { a = std::make_unique<EpfArchive>(readGameFile(name)); } catch (const FormatError&) {}
        it = gfx_.emplace(arch, std::move(a)).first;
    }
    if (!it->second || size_t(entry) >= it->second->count()) return {};
    Bytes b = it->second->entry(size_t(entry));
    const Descriptor& e = desc_[i];
    const size_t need = size_t((e.w + 15) / 16) * 2 * e.h * 4;
    if (b.size() == need + 8) b.erase(b.begin(), b.begin() + 8);
    if (b.size() != need) return {};
    return b;
}

const Bytes* Level::planes(uint16_t i, int depth) {
    auto it = planeCache_.find(i);
    if (it != planeCache_.end()) return it->second ? &*it->second : nullptr;
    std::optional<Bytes> r;
    if (depth < 16 && i < desc_.size()) {
        if (auto a = alias_.find(i); a != alias_.end()) {
            if (const Bytes* s = planes(a->second, depth + 1)) r = *s;
        } else if (auto xr = xor_.find(i); xr != xor_.end()) {
            Bytes own = rawGfx(i);
            const Bytes* s = planes(xr->second, depth + 1);
            if (!own.empty() && s && s->size() == own.size())
                for (size_t k = 0; k < own.size(); ++k) own[k] ^= (*s)[k];
            if (!own.empty()) r = std::move(own);
        } else if (auto m = mirror_.find(i); m != mirror_.end()) {
            if (const Bytes* s = planes(m->second, depth + 1)) {
                const Descriptor& e = desc_[m->second];
                const size_t rb = size_t((e.w + 15) / 16) * 2, ps = rb * e.h;
                Bytes out(s->size());
                for (size_t p = 0; p < 4; ++p)
                    for (size_t y = 0; y < e.h; ++y)
                        for (size_t x = 0; x < rb * 8; ++x) {
                            const size_t so = p * ps + y * rb, dxp = rb * 8 - 1 - x;
                            if (so + rb > s->size()) continue;
                            if ((*s)[so + (x >> 3)] & (0x80 >> (x & 7))) out[so + (dxp >> 3)] |= uint8_t(0x80 >> (dxp & 7));
                        }
                r = std::move(out);
            }
        } else {
            Bytes own = rawGfx(i);
            if (!own.empty()) r = std::move(own);
        }
    }
    auto& slot = planeCache_[i] = std::move(r);
    return slot ? &*slot : nullptr;
}

int Level::drawDx(uint16_t i) const {
    if (i >= desc_.size()) return 0;
    auto f = dxFix_.find(i);
    return desc_[i].dx + (f != dxFix_.end() ? f->second : 0);
}

uint16_t Level::wallFlame(uint16_t i) const {
    auto it = flame_.find(i);
    return it != flame_.end() ? it->second : uint16_t(0xFFFF);
}

uint16_t Level::animFrame(uint16_t d, int frame) const {
    for (size_t k = 0; k < animBases_.size(); ++k) {
        if (animBases_[k] != d) continue;
        int n = 5;
        if (k + 1 < animBases_.size() && animBases_[k + 1] > d && animBases_[k + 1] - d <= 8) n = animBases_[k + 1] - d;
        return uint16_t(d + (frame % n));
    }
    return d;
}

int Level::animPhase(uint16_t d) const {
    for (size_t k = 0; k < animBases_.size(); ++k) {
        int n = 5;
        if (k + 1 < animBases_.size() && animBases_[k + 1] > animBases_[k] && animBases_[k + 1] - animBases_[k] <= 8) n = animBases_[k + 1] - animBases_[k];
        if (d >= animBases_[k] && d < animBases_[k] + n) return d - animBases_[k];
    }
    return -1;
}

uint16_t Level::wallOverlay(uint16_t i) const {
    auto it = overlay_.find(i);
    return it != overlay_.end() ? it->second : uint16_t(0xFFFF);
}

const Sprite* Level::sprite(uint16_t i) {
    auto it = spriteCache_.find(i);
    if (it != spriteCache_.end()) return it->second ? &*it->second : nullptr;
    std::optional<Sprite> result;
    if (i < desc_.size()) {
        const Descriptor& e = desc_[i];
        const Bytes* b = planes(i);
        const int ww = (e.w + 15) / 16, W = ww * 16, H = e.h;
        const size_t ps = size_t(ww) * 2 * H;
        if (b && b->size() >= ps * 4) {
            Sprite s;
            s.width = W; s.height = H;
            s.indices.assign(size_t(W) * H, 0);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    uint8_t v = 0;
                    for (int p = 0; p < 4; ++p)
                        if ((*b)[p * ps + size_t(y) * ww * 2 + (x >> 3)] & (0x80 >> (x & 7))) v |= uint8_t(8 >> p);
                    s.indices[size_t(y) * W + x] = v;
                }
            result = std::move(s);
        }
    }
    auto& slot = spriteCache_[i] = std::move(result);
    return slot ? &*slot : nullptr;
}

std::vector<uint16_t> Level::roomObjects(int n) {
    std::vector<uint16_t> out;
    uint16_t h = be16(&room(n)[4]);
    while (h && h < instances_.size() && std::find(out.begin(), out.end(), h) == out.end()) {
        out.push_back(h);
        h = instances_[h].next;
    }
    return out;
}

} // namespace dm
