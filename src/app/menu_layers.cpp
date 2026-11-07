#include "app/menu_layers.h"
#include <algorithm>

namespace lm {

namespace {
constexpr int W = 320, H = 200;
inline size_t at(int x, int y) { return size_t(y) * W + size_t(x); }

// keeps only the groups of at least minSize pixels (8-connected)
std::vector<uint8_t> largeGroups(const std::vector<uint8_t>& in, size_t minSize) {
    std::vector<uint8_t> keep(in.size(), 0), seen(in.size(), 0);
    std::vector<size_t> stack, members;
    for (size_t s0 = 0; s0 < in.size(); ++s0) {
        if (!in[s0] || seen[s0]) continue;
        members.clear();
        stack.assign(1, s0); seen[s0] = 1;
        while (!stack.empty()) {
            const size_t i = stack.back(); stack.pop_back(); members.push_back(i);
            const int x = int(i % W), y = int(i / W);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                    const size_t j = at(nx, ny);
                    if (in[j] && !seen[j]) { seen[j] = 1; stack.push_back(j); }
                }
        }
        if (members.size() >= minSize) for (size_t i : members) keep[i] = 1;
    }
    return keep;
}

// fills the closed holes (4-connected regions that do not touch the screen border, up to maxSize pixels)
void fillHoles(std::vector<uint8_t>& m, size_t maxSize) {
    std::vector<uint8_t> seen(m.size(), 0);
    std::vector<size_t> stack, members;
    for (size_t s0 = 0; s0 < m.size(); ++s0) {
        if (m[s0] || seen[s0]) continue;
        members.clear();
        bool border = false;
        stack.assign(1, s0); seen[s0] = 1;
        while (!stack.empty()) {
            const size_t i = stack.back(); stack.pop_back(); members.push_back(i);
            const int x = int(i % W), y = int(i / W);
            if (x == 0 || x == W - 1 || y == 0 || y == H - 1) border = true;
            const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& d : nb) {
                const int nx = x + d[0], ny = y + d[1];
                if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                const size_t j = at(nx, ny);
                if (!m[j] && !seen[j]) { seen[j] = 1; stack.push_back(j); }
            }
        }
        if (!border && members.size() <= maxSize) for (size_t i : members) m[i] = 1;
    }
}
} // namespace

CheckerTest detectMenuChecker(const std::vector<uint32_t>& screen, const std::vector<uint32_t>& before) {
    long same[2] = {0, 0}, black[2] = {0, 0}, n[2] = {0, 0};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint32_t bg = before[at(x, y)];
            if (!bg) continue;
            const uint32_t sc = screen[at(x, y)];
            const int par = (x + y) & 1;
            ++n[par];
            if (sc == 0) ++black[par];
            else if (sc == bg) ++same[par];
        }
    CheckerTest t;
    // dark squares: the parity with more pixels turned black; in the other one the background stayed
    t.parity = black[1] > black[0] ? 1 : 0;
    const long b = black[t.parity], e = same[t.parity ^ 1], nd = n[t.parity];
    t.checker = nd > 0 && b * 100 >= nd * 3 && e * 2 >= b;
    return t;
}

void extractMenuLayers(const std::vector<uint32_t>& screen, const std::vector<uint32_t>& before, int parity,
                       bool wholeScreen, bool modernBackground, std::vector<uint32_t>& menu,
                       std::vector<uint32_t>& inactiveOut) {
    menu.assign(size_t(W) * H, 0);
    inactiveOut.assign(size_t(W) * H, 0);
    auto px = [&](int x, int y) { return screen[at(x, y)]; };
    std::vector<uint8_t> isMenu(size_t(W) * H, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint32_t sc = px(x, y), bg = before[at(x, y)];
            // dark squares: anything not black; other squares: anything that changed
            isMenu[at(x, y)] = uint8_t(wholeScreen || ((((x + y) & 1) == parity) ? (sc != 0) : (sc != bg)));
        }
    std::vector<uint8_t> inactive(isMenu.size(), 0);
    if (!wholeScreen) {
        // 1. only large groups: single pixels that differ from the background by chance (flames, characters)
        //    are not menu
        isMenu = largeGroups(isMenu, 60);
        // 2. closed holes inside the menu (text, black outlines, pixels equal to the background by chance)
        fillHoles(isMenu, 4000);
        // 3. border pixels equal to the background: menu if their neighbours left and right, or above and
        //    below, are menu
        std::vector<uint8_t> grown = isMenu;
        for (int y = 1; y < H - 1; ++y)
            for (int x = 1; x < W - 1; ++x) {
                const size_t i = at(x, y);
                if (!isMenu[i] && ((isMenu[i - 1] && isMenu[i + 1]) || (isMenu[i - W] && isMenu[i + W]))) grown[i] = 1;
            }
        isMenu.swap(grown);

        // Inactive windows: around the pixel (5 x 5, menu pixels only) the squares of one parity are almost all
        // black and those of the other one almost all coloured.
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                if (!isMenu[at(x, y)]) continue;
                int n[2] = {0, 0}, blk[2] = {0, 0};
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || xx >= W || yy < 0 || yy >= H || !isMenu[at(xx, yy)]) continue;
                        const int par = (xx + yy) & 1;
                        ++n[par]; if (!px(xx, yy)) ++blk[par];
                    }
                for (int p2 = 0; p2 < 2; ++p2)
                    if (n[p2] >= 8 && blk[p2] * 10 >= n[p2] * 8 && (n[p2 ^ 1] - blk[p2 ^ 1]) * 10 >= n[p2 ^ 1] * 6)
                        inactive[at(x, y)] = 1;
            }
        // borders of the inactive windows (the 5 x 5 square leaves the window there): menu pixels next to an
        // inactive window, black or with a black neighbour (the checkerboard), twice; never the outline of an
        // active window (next to a solid pixel, with four coloured neighbours, that is not inactive)
        auto solid = [&](int sx, int sy) {
            if (sx < 1 || sx > W - 2 || sy < 1 || sy > H - 2) return false;
            const size_t j = at(sx, sy);
            if (!isMenu[j] || inactive[j]) return false;
            return px(sx, sy) && px(sx - 1, sy) && px(sx + 1, sy) && px(sx, sy - 1) && px(sx, sy + 1);
        };
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<uint8_t> grow = inactive;
            for (int y = 1; y < H - 1; ++y)
                for (int x = 1; x < W - 1; ++x) {
                    const size_t i = at(x, y);
                    if (!isMenu[i] || inactive[i]) continue;
                    int nIn = 0;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) nIn += inactive[at(x + dx, y + dy)];
                    if (nIn < 2) continue;
                    if (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1)) continue;
                    const bool blackHere = !px(x, y);
                    const bool blackNb = !px(x - 1, y) || !px(x + 1, y) || !px(x, y - 1) || !px(x, y + 1);
                    if (blackHere || blackNb) grow[i] = 1;
                }
            inactive.swap(grow);
        }
    }
    if (modernBackground && !wholeScreen) {
        // colours of the inactive windows without the checkerboard (black squares = mean of the coloured
        // neighbours), a light blur made only with pixels of the windows, darkened like the background
        std::vector<float> rgb(size_t(W) * H * 3, 0.0f), w(size_t(W) * H, 0.0f);
        bool any = false;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = at(x, y);
                if (!inactive[i]) continue;
                any = true;
                const uint32_t c = px(x, y);
                float r = 0, g = 0, b = 0, cnt = 0;
                if (c) { r = float((c >> 16) & 255); g = float((c >> 8) & 255); b = float(c & 255); cnt = 1; }
                else {
                    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (const auto& d : nb) {
                        const int xx = x + d[0], yy = y + d[1];
                        if (xx < 0 || xx >= W || yy < 0 || yy >= H) continue;
                        const uint32_t cc = px(xx, yy);
                        if (!cc || !inactive[at(xx, yy)]) continue;
                        r += float((cc >> 16) & 255); g += float((cc >> 8) & 255); b += float(cc & 255); cnt += 1;
                    }
                }
                if (cnt > 0) { rgb[i * 3] = r / cnt; rgb[i * 3 + 1] = g / cnt; rgb[i * 3 + 2] = b / cnt; w[i] = 1; }
            }
        if (any) {
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<float> nrgb(rgb.size(), 0.0f), nw(w.size(), 0.0f);
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x) {
                        const size_t i = at(x, y);
                        if (!inactive[i]) continue;
                        float r = 0, g = 0, b = 0, sw = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int xx = x + dx, yy = y + dy;
                                if (xx < 0 || xx >= W || yy < 0 || yy >= H) continue;
                                const size_t j = at(xx, yy);
                                if (w[j] <= 0) continue;
                                r += rgb[j * 3] * w[j]; g += rgb[j * 3 + 1] * w[j]; b += rgb[j * 3 + 2] * w[j]; sw += w[j];
                            }
                        if (sw > 0) { nrgb[i * 3] = r / sw; nrgb[i * 3 + 1] = g / sw; nrgb[i * 3 + 2] = b / sw; nw[i] = 1; }
                    }
                rgb.swap(nrgb); w.swap(nw);
            }
            for (size_t i = 0; i < inactiveOut.size(); ++i)
                if (inactive[i] && w[i] > 0)
                    inactiveOut[i] = 0xFF000000u | (uint32_t(rgb[i * 3] * 0.45f) << 16) | (uint32_t(rgb[i * 3 + 1] * 0.45f) << 8) |
                                     uint32_t(rgb[i * 3 + 2] * 0.45f);
        }
    }
    for (size_t i = 0; i < menu.size(); ++i)
        if (isMenu[i] && !(modernBackground && inactive[i])) menu[i] = 0xFF000000u | screen[i];
}

void blurAndDarken(std::vector<uint32_t>& px, int w, int h, int r) {
    std::vector<int> ch(size_t(w) * size_t(h) * 3), tmp(ch.size());
    for (size_t i = 0; i < px.size(); ++i) { ch[i * 3] = int((px[i] >> 16) & 255); ch[i * 3 + 1] = int((px[i] >> 8) & 255); ch[i * 3 + 2] = int(px[i] & 255); }
    auto boxH = [&](std::vector<int>& src, std::vector<int>& dst) {
        for (int y = 0; y < h; ++y)
            for (int c = 0; c < 3; ++c) {
                long sum = 0;
                auto get = [&](int x) { return src[(size_t(y) * size_t(w) + size_t(std::clamp(x, 0, w - 1))) * 3 + size_t(c)]; };
                for (int x = -r; x <= r; ++x) sum += get(x);
                for (int x = 0; x < w; ++x) { dst[(size_t(y) * size_t(w) + size_t(x)) * 3 + size_t(c)] = int(sum / (2 * r + 1)); sum += get(x + r + 1) - get(x - r); }
            }
    };
    auto boxV = [&](std::vector<int>& src, std::vector<int>& dst) {
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                long sum = 0;
                auto get = [&](int y) { return src[(size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(x)) * 3 + size_t(c)]; };
                for (int y = -r; y <= r; ++y) sum += get(y);
                for (int y = 0; y < h; ++y) { dst[(size_t(y) * size_t(w) + size_t(x)) * 3 + size_t(c)] = int(sum / (2 * r + 1)); sum += get(y + r + 1) - get(y - r); }
            }
    };
    for (int pass = 0; pass < 3; ++pass) { boxH(ch, tmp); boxV(tmp, ch); }
    for (size_t i = 0; i < px.size(); ++i)
        px[i] = 0xFF000000u | (uint32_t(ch[i * 3] * 45 / 100) << 16) | (uint32_t(ch[i * 3 + 1] * 45 / 100) << 8) | uint32_t(ch[i * 3 + 2] * 45 / 100);
}

} // namespace lm
