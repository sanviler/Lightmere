#include "game/ham_fix.h"
#include "formats/game_files.h"
#include "formats/epf.h"
#include "formats/ilbm.h"
#include <array>
#include <cstdlib>
#include <filesystem>

namespace dm::game {

namespace {
struct C3 { int r, g, b; };
int dist(C3 a, C3 b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }
int lum(C3 a) { return a.r + a.g + a.b; }

// colour of a HAM6 pixel given the previous colour
C3 hamStep(const Palette& pal, C3 prev, uint8_t v) {
    const int d = (v & 15) * 17;
    switch (v >> 4) {
        case 0: { const Rgb& c = pal.c[v & 15]; return {c.r, c.g, c.b}; }
        case 1: return {prev.r, prev.g, d};
        case 2: return {d, prev.g, prev.b};
        default: return {prev.r, d, prev.b};
    }
}
} // namespace

void HamFix::load() {
    for (int level = 1; level <= 3; ++level) {
        try {
            const std::string name = "lev" + std::to_string(level) + std::to_string(level) + "000.epf";
            const IlbmImage img = decodeIlbm(EpfArchive(readGameFile(name)).entry(2));
            if (!img.ham || img.planes != 6 || img.width != 320) continue;
            const int w = img.width, h = img.height;
            auto idx = [&](int x, int y) { return img.indices[size_t(y) * w + x]; };
            // original colours
            std::vector<C3> col(size_t(w) * h);
            for (int y = 0; y < h; ++y) {
                C3 c{img.palette.c[0].r, img.palette.c[0].g, img.palette.c[0].b};
                for (int x = 0; x < w; ++x) col[size_t(y) * w + x] = c = hamStep(img.palette, c, idx(x, y));
            }
            auto at = [&](int x, int y) { return col[size_t(y) * w + x]; };
            Image im;
            im.height = h;
            im.plane0.resize(size_t(h) * 40);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (idx(x, y) & 1) im.plane0[size_t(y) * 40 + size_t(x / 8)] |= uint8_t(0x80 >> (x & 7));
            // error on the pixels after x (up to the first pixel that sets the colour) if the colour at x becomes c
            auto tailError = [&](int x0, int y, C3 c) {
                int e = 0;
                for (int x = x0 + 1; x < w && (idx(x, y) >> 4) != 0; ++x) { c = hamStep(img.palette, c, idx(x, y)); e += dist(c, at(x, y)); }
                return e;
            };
            // 1) isolated black pixels: at least 2 bright neighbours and at most one black one, or bright on both sides
            for (int y = 1; y + 1 < h; ++y)
                for (int x = 1; x + 1 < w; ++x) {
                    if (lum(at(x, y)) != 0) continue;
                    const C3 nb[4] = {at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1)};
                    int bright = 0, black = 0; C3 t{0, 0, 0};
                    for (const C3& n : nb) {
                        if (lum(n) > 90) { ++bright; t.r += n.r; t.g += n.g; t.b += n.b; }
                        else if (lum(n) == 0) ++black;
                    }
                    const bool sides = lum(nb[0]) > 90 && lum(nb[1]) > 90;   // vertical runs of black too
                    if (!sides && (bright < 2 || black > 1 || bright + black < 3)) continue;
                    t = {t.r / bright, t.g / bright, t.b / bright};
                    int best = 1 << 30; uint8_t bv = idx(x, y);
                    for (int v = 0; v < 64; ++v) {
                        const C3 c = hamStep(img.palette, at(x - 1, y), uint8_t(v));
                        const int e = dist(c, t) * 4 + tailError(x, y, c);
                        if (e < best) { best = e; bv = uint8_t(v); }
                    }
                    im.patches.push_back({x, y, bv});
                }
            // 2) left fringe: columns 0 and 1 towards the colour of column 2
            for (int y = 0; y < h; ++y) {
                const C3 t = at(2, y);
                const C3 c0 = {img.palette.c[0].r, img.palette.c[0].g, img.palette.c[0].b};
                const int before = dist(at(0, y), t) + dist(at(1, y), t);
                int best = 1 << 30; uint8_t b0 = idx(0, y), b1 = idx(1, y);
                for (int v0 = 0; v0 < 64; ++v0) {
                    const C3 a = hamStep(img.palette, c0, uint8_t(v0));
                    for (int v1 = 0; v1 < 64; ++v1) {
                        const C3 b = hamStep(img.palette, a, uint8_t(v1));
                        const int e = dist(a, t) + dist(b, t) + tailError(1, y, b);
                        if (e < best) { best = e; b0 = uint8_t(v0); b1 = uint8_t(v1); }
                    }
                }
                if (best < before) {
                    if (b0 != idx(0, y)) im.patches.push_back({0, y, b0});
                    if (b1 != idx(1, y)) im.patches.push_back({1, y, b1});
                }
            }
            images_.push_back(std::move(im));
        } catch (const std::exception&) {}
    }
}

void HamFix::apply(amiga::Machine& m) {
    if (images_.empty() || !(m.firstBplCon0 & 0x0800) || ((m.firstBplCon0 >> 12) & 7) != 6) return;
    const uint32_t p0 = m.firstBplPt[0];
    std::array<uint8_t, 80> rows{};
    for (size_t i = 0; i < rows.size(); ++i) rows[i] = m.peek8(p0 + uint32_t(i));
    for (const Image& im : images_) {
        // which row of the picture is the first shown (two equal rows in a row)
        int r = -1;
        for (int y = 0; y + 1 < im.height && r < 0; ++y)
            if (std::equal(rows.begin(), rows.end(), im.plane0.begin() + y * 40)) r = y;
        if (r < 0) continue;
        for (const Patch& pt : im.patches) {
            const int dy = pt.y - r;
            if (pt.y >= 200) continue;                 // there are at most 200 rows per plane in memory
            for (int pl = 0; pl < 6; ++pl) {
                const uint32_t a = uint32_t(int32_t(m.firstBplPt[pl]) + dy * 40) + uint32_t(pt.x / 8);
                const uint8_t bit = uint8_t(0x80 >> (pt.x & 7));
                const uint8_t old = m.peek8(a);
                m.poke8(a, (pt.value >> pl) & 1 ? uint8_t(old | bit) : uint8_t(old & ~bit));
            }
        }
        return;
    }
}

} // namespace dm::game
