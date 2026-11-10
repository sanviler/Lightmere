#include "app/porting_view.h"
#include "app/menu_layers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace lm {

namespace {
constexpr int SW = 320, SH = 200;                     // original screen in lores pixels
// lores pixel (x, y) of the 320 x 200 game screen in the Amiga frame buffer
inline uint32_t amigaPixel(const amiga::Machine& m, int x, int y) {
    return m.framebuffer()[size_t(18 + y) * amiga::Machine::kFbWidth + size_t(34 + 2 * x)] & 0xFFFFFF;
}
inline bool inNameRow(int x, int y) {
    return y >= PortingView::kNameY0 && y < PortingView::kNameY0 + PortingView::kNameH && x >= 24 && x < 294;
}
} // namespace

PortingView::PortingView(SDL_Renderer* ren, dm::game::LiveView& live, dm::game::PanelText& text)
    : ren_(ren), live_(live), text_(text) {
    screen_.assign(size_t(SW) * SH, 0);
    lastScreen_.assign(size_t(SW) * SH, 0);
    lastRoom_.assign(size_t(SW) * SH, 0);
    canvas_.assign(size_t(SW) * kCanvasH, 0);
    canvasTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, kCanvasH);
    SDL_SetTextureBlendMode(canvasTex_, SDL_BLENDMODE_BLEND);
    menuTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH);
    SDL_SetTextureBlendMode(menuTex_, SDL_BLENDMODE_BLEND);
    inactiveTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH);
    SDL_SetTextureBlendMode(inactiveTex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(inactiveTex_, SDL_ScaleModeLinear);
}

PortingView::~PortingView() {
    for (SDL_Texture* t : {canvasTex_, roomTex_, menuTex_, inactiveTex_, menuBg_, checkerTex_})
        if (t) SDL_DestroyTexture(t);
}

// The game panel is on screen when (almost) all the pixels of its artwork show the artwork, and the screen is
// not in HAM mode (the level presentations are HAM pictures that can light the same pixels). The colours of
// the panel at full light are learnt from the screen (palette index -> RGB); a panel that is a darker copy of
// them is a fade.
void PortingView::detectPanel(amiga::Machine& m) {
    panelOnScreen_ = false; fadeLike_ = false; fade_ = 1.0;
    if (!shape_.known()) return;
    const bool ham = m.firstBplCon0 & 0x0800;
    std::map<std::pair<uint8_t, uint32_t>, int> pairs;            // (index, colour) -> pixels
    long lum = 0;
    int tot = 0;
    for (int y = PanelShape::kTop; y < PanelShape::kTop + PanelShape::kRows; ++y)
        for (int x = 0; x < SW; ++x) {
            if (!shape_.fixed(x, y) || shape_.changing(x, y) || inNameRow(x, y)) continue;
            const uint32_t c = screen_[size_t(y) * SW + size_t(x)];
            ++pairs[{shape_.index(x, y), c}];
            lum += long((c >> 16) & 255) + long((c >> 8) & 255) + long(c & 255);
            ++tot;
        }
    if (!tot) return;
    // the most frequent colour of every index, and how many pixels agree with it
    std::unordered_map<uint8_t, std::pair<uint32_t, int>> major;
    for (const auto& [k, n] : pairs) {
        auto& best = major[k.first];
        if (n > best.second) best = {k.second, n};
    }
    int agree = 0, colours = 0;
    for (const auto& [ix, best] : major) { agree += best.second; if (best.first) ++colours; }
    const bool consistent = agree * 100 >= tot * 98;
    if (panelRefLum_ == 0) {
        panelOnScreen_ = !ham && consistent && colours >= 4;
    } else {
        int lit = 0, litTot = 0, darker = 0;
        for (int y = PanelShape::kTop; y < PanelShape::kTop + PanelShape::kRows; ++y)
            for (int x = 0; x < SW; ++x) {
                if (!shape_.fixed(x, y) || shape_.changing(x, y) || inNameRow(x, y)) continue;
                const uint32_t c = screen_[size_t(y) * SW + size_t(x)];
                auto it = panelRef_.find(shape_.index(x, y));
                const uint32_t r = it != panelRef_.end() ? it->second : 0;
                if (r) { ++litTot; if (c) ++lit; }
                if (((c >> 16) & 255) <= ((r >> 16) & 255) + 16 && ((c >> 8) & 255) <= ((r >> 8) & 255) + 16 && (c & 255) <= (r & 255) + 16) ++darker;
            }
        panelOnScreen_ = !ham && litTot && lit * 100 >= litTot * 95;
        fadeLike_ = !ham && darker * 100 >= tot * 97;
        fade_ = std::clamp(double(lum) / double(panelRefLum_), 0.0, 1.0);
    }
    if (panelOnScreen_ && consistent && lum >= panelRefLum_) {
        panelRef_.clear();
        for (const auto& [ix, best] : major) panelRef_[ix] = best.first;
        panelRefLum_ = lum;
    }
    if (panelOnScreen_) everInGame_ = true;
}

void PortingView::update(amiga::Machine& m, bool log) {
    for (int y = 0; y < SH; ++y)
        for (int x = 0; x < SW; ++x) screen_[size_t(y) * SW + size_t(x)] = amigaPixel(m, x, y);
    // a level change: presentation and loading are outside the game, the panel is drawn again
    if (const uint32_t pb = m.gameBase()) {
        const int lv = m.peek8(pb + 0x1E688);
        if (lv != lastLevel_) { seenPanel_ = false; lastLevel_ = lv; panelRefLum_ = 0; panelRef_.clear(); shape_.reset(); }
    }
    shape_.update(m, live_, text_);
    detectPanel(m);
    if (m.firstBplCon0 & 0x0800) seenPanel_ = false;           // HAM screen: presentation, outside the game
    const double fadeF = (fadeLike_ && fade_ < 0.97) ? fade_ : 1.0;

    showNew_ = false;
    bool forceOverlay = false;
    if (panelOnScreen_ || seenPanel_) {
        showNew_ = live_.render(m, liveFrame_, int(m.frameCount() / 6));
        if (!showNew_) seenPanel_ = false;
        else if (panelOnScreen_) seenPanel_ = true;
        else forceOverlay = true;
    }
    // The panel is gone: a menu only if the checkerboard of the game is there; a fade shows the game screen
    // inside the layout; anything else (game over) the whole original screen over the blurred layout.
    wholeScreenOverlay_ = false;
    int parity = 0;
    if (forceOverlay) {
        const CheckerTest t = detectMenuChecker(screen_, lastScreen_);
        parity = t.parity;
        if (!t.checker) {
            if (fadeLike_ || fadeF < 1.0) forceOverlay = false;
            else wholeScreenOverlay_ = true;
        }
    }
    overlay_ = false;
    if (!showNew_) return;

    const auto& pal = liveFrame_.palette.c;
    auto liveRgb = [&](int x, int y) {
        const auto c = pal[liveFrame_.px[size_t(y) * SW + size_t(x)]];
        return (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
    };
    // same moment: the C++ view must match the game screen above the panel. Threshold 10%: a moving character
    // (the screen can be one frame behind the draw list) makes far less; room changes, banners and menus far more.
    // Over the threshold the game screen is shown at once; back to the C++ view after 2 frames under it.
    int diff = 0;
    if (!forceOverlay)
        for (int y = 8; y < 128; y += 2)
            for (int x = 0; x < SW; x += 2) if (liveRgb(x, y) != screen_[size_t(y) * SW + size_t(x)]) ++diff;
    constexpr int kThreshold = 60 * 160 * 10 / 100;
    const bool over = diff > kThreshold;
    // Room change: as soon as the room number changes, memory holds the new room while the screen still
    // shows the old one, still, until the game has finished loading; meanwhile the C++ view can be half-way. The
    // last C++ view of the old room stays while the screen matches it; if the screen shows something else
    // (banners) the screen is shown; the new room comes after 2 frames that match it better than the old one
    // (nearby rooms of the same street look alike: the threshold alone is not enough).
    const int curRoom = live_.lastRoom();
    if (stableRoom_ < 0) stableRoom_ = curRoom;
    if (curRoom != stableRoom_ && !roomFreeze_) { roomFreeze_ = true; freezeRun_ = 0; }
    bool frozen = false;
    if (roomFreeze_ && !forceOverlay) {
        int diffOld = 0;
        for (int y = 8; y < 128; y += 2)
            for (int x = 0; x < SW; x += 2) if (lastRoom_[size_t(y) * SW + size_t(x)] != screen_[size_t(y) * SW + size_t(x)]) ++diffOld;
        if (!over && diff < diffOld) ++freezeRun_; else freezeRun_ = 0;
        if (freezeRun_ >= 2) { roomFreeze_ = false; stableRoom_ = curRoom; screenMode_ = false; }
        else { frozen = diffOld <= kThreshold; screenMode_ = !frozen; }
    } else {
        if (over) { screenMode_ = true; underRun_ = 0; }
        else if (screenMode_ && ++underRun_ >= 2) screenMode_ = false;
        if (!roomFreeze_) stableRoom_ = curRoom;
    }
    overlay_ = forceOverlay;
    if (fadeF < 1.0 && !overlay_) { screenMode_ = true; underRun_ = 0; }   // fade: the game screen
    const bool screen = !overlay_ && screenMode_;
    if (log)
        std::fprintf(stderr, "frame %ld: %d of 9600 different%s\n", long(m.frameCount()), diff,
                     overlay_ ? " -> menu" : frozen ? " -> frozen room" : screen ? " -> game screen" : "");
    if (overlay_) {
        extractMenuLayers(screen_, lastScreen_, parity, wholeScreenOverlay_, menuModern_, menuLayer_, inactiveLayer_);
        SDL_UpdateTexture(menuTex_, nullptr, menuLayer_.data(), SW * 4);
        SDL_UpdateTexture(inactiveTex_, nullptr, inactiveLayer_.data(), SW * 4);
        return;
    }
    lastScreen_ = screen_;
    if (!screen && !frozen && !roomFreeze_)
        for (int y = 0; y < SH; ++y)
            for (int x = 0; x < SW; ++x) lastRoom_[size_t(y) * SW + size_t(x)] = liveRgb(x, y);
    buildCanvas(m, screen, fadeF);
}

void PortingView::buildCanvas(amiga::Machine& m, bool screen, double fade) {
    auto room = [&](int x, int y) { return lastRoom_[size_t(y) * SW + size_t(x)]; };   // the room under the panel
    auto px = [&](int x, int y) { return screen_[size_t(y) * SW + size_t(x)]; };
    // colours that do not come from the game screen (room under the panel, strings rewritten in C++) darken
    // with it during a fade
    auto dim = [&](uint32_t c) -> uint32_t {
        if (fade >= 1.0) return c;
        const auto ch = [&](int sh) { return uint32_t(double((c >> sh) & 255) * fade + 0.5) << sh; };
        return ch(16) | ch(8) | ch(0);
    };
    // room: the C++ view; during transitions the game screen where there is no panel
    for (int y = 0; y < kViewH; ++y)
        for (int x = 0; x < SW; ++x) {
            const int oy = kViewY0 + y;
            uint32_t c = dim(room(x, oy));
            if (screen && oy < 150 && !(oy >= PanelShape::kTop && shape_.fixed(x, oy))) c = px(x, oy);
            canvas_[size_t(y) * SW + size_t(x)] = 0xFF000000u | c;
        }
    // Panel strings: the game writes them with $0117C, the place name at (24, 183), the target name at (192, 183);
    // every character is an 8 x 8 tile of graphics (descriptor font, black background included). The same strings
    // are written again in C++ with the same font: the pixels of the tiles are exactly those of the strings. The
    // colours (index -> RGB) are read on the screen, under the tiles of the place name, only when the tiles
    // match the screen (every index a single colour; during room changes the name on screen and the one just
    // written by the game can differ, S29).
    textFrame_.width = SW; textFrame_.height = SH;
    textFrame_.px.assign(size_t(SW) * SH, 0);
    live_.renderText(m, text_.roomName, 24, kNameY0, textFrame_);
    if (!screen) {
        std::unordered_map<uint8_t, uint32_t> seen;
        bool ok = true;
        // only the place name box (up to the target box at x = 192): a longer name, from corrected texts, goes
        // on into the target box on the game screen, where the screen holds the target instead
        for (size_t i = 0; i < textFrame_.px.size() && ok; ++i)
            if (const uint8_t ix = textFrame_.px[i]; ix && int(i % size_t(SW)) < 192) {
                const uint32_t c = screen_[i];
                auto [it, fresh] = seen.emplace(ix, c);
                if (!fresh && it->second != c) ok = false;
            }
        if (ok && seen.size() >= 3) for (const auto& [ix, c] : seen) textColour_[ix] = c;
    }
    // writes a string with the panel font: canvas row, x of the font, columns, black transparent?
    auto writeText = [&](const std::string& str, int x, int canvasRow0, int x0, int x1, bool blackClear) {
        if (str.empty()) return;
        textFrame_.px.assign(size_t(SW) * SH, 0);
        live_.renderText(m, str, x, kNameY0, textFrame_);
        for (int y = 0; y < kNameH; ++y)
            for (int xx = x0; xx < x1; ++xx) {
                const uint8_t i = textFrame_.px[size_t(kNameY0 + y) * SW + size_t(xx)];
                if (!i) continue;
                auto it = textColour_.find(i);
                if (it == textColour_.end()) continue;
                canvas_[size_t(canvasRow0 + y) * SW + size_t(xx)] = (blackClear && it->second == 0) ? 0u : 0xFF000000u | dim(it->second);
            }
    };
    // first and last non-black pixel of a string written from x (the tiles have a black background)
    auto litRange = [&](const std::string& str, int x, int& lo, int& hi) {
        lo = SW; hi = -1;
        if (str.empty()) return;
        textFrame_.px.assign(size_t(SW) * SH, 0);
        live_.renderText(m, str, x, kNameY0, textFrame_);
        for (int y = 0; y < kNameH; ++y)
            for (int xx = 0; xx < SW; ++xx) {
                const uint8_t i = textFrame_.px[size_t(kNameY0 + y) * SW + size_t(xx)];
                auto it = i ? textColour_.find(i) : textColour_.end();
                if (it != textColour_.end() && it->second != 0) { lo = std::min(lo, xx); hi = std::max(hi, xx); }
            }
    };
    // corners: only the panel pixels. The row of the names, between the two medallions, holds only the name
    // tiles in the original: there the artwork is drawn with its own colours and the names written again, so
    // no pieces of old or new names remain during room changes. Elsewhere: the artwork from the screen;
    // the changing parts (bars, target portrait) where they differ from the room.
    auto panelPixel = [&](int x, int oy, uint32_t& out) -> bool {
        if (inNameRow(x, oy)) {
            if (!shape_.fixed(x, oy)) return false;
            auto it = panelRef_.find(shape_.index(x, oy));
            if (it == panelRef_.end() || !it->second) return false;
            out = dim(it->second);
            return true;
        }
        const uint32_t a = px(x, oy);
        if (shape_.fixed(x, oy)) { out = a; return true; }
        if (!shape_.changing(x, oy)) return false;
        if (screen ? (a != 0 && a != room(x, oy)) : a != room(x, oy)) { out = a; return true; }
        return false;
    };
    for (int y = 0; y < kCornerH; ++y)
        for (int x = 0; x < SW; ++x) {
            uint32_t c = 0;
            canvas_[size_t(kViewH + y) * SW + size_t(x)] = panelPixel(x, kCornerY0 + y, c) ? 0xFF000000u | c : 0u;
        }
    const int nameRow = kViewH + kNameY0 - kCornerY0;
    // the hero's name in place of the place name, where the left medallion ends
    writeText("Ebryn", 24, nameRow, 0, kSplitX, false);
    // the target name aligned to the right, mirroring "Ebryn": the same distance from the right medallion
    // (starting at x = 294) that "Ebryn" has from the left one (ending at x = 23), measured on lit pixels
    {
        int eLo, eHi, tLo, tHi;
        litRange("Ebryn", 24, eLo, eHi);
        const int gap = eLo <= eHi ? eLo - 24 : 1;
        // without a target (L1E660 = 0) no name, even if the game has not cleared the box yet
        const std::string target = m.peek32(m.gameBase() + 0x1E660) ? text_.targetName : std::string();
        int tx = 294 - 8 * int(target.size());
        litRange(target, tx, tLo, tHi);
        if (tLo <= tHi) tx += (293 - gap) - tHi;
        writeText(target, tx, nameRow, kSplitX, 294, false);
    }
    // the place name for the top band, written from x = 0 (black is transparent)
    for (int y = 0; y < kNameH; ++y)
        for (int x = 0; x < SW; ++x) canvas_[size_t(kViewH + kCornerH + y) * SW + size_t(x)] = 0u;
    writeText(text_.roomName, 0, kViewH + kCornerH, 0, SW, true);
    nameW_ = std::min(SW, 8 * int(text_.roomName.size()));
    SDL_UpdateTexture(canvasTex_, nullptr, canvas_.data(), SW * 4);
    roomDirty_ = true;
}

void PortingView::drawLayout(const Geometry& g) {
    if (crtOn_) {
        roomBlit_.draw(ren_, canvas_.data(), SW, SW, kViewH, g.room, crtOn_, 1, false);
        const uint32_t* corners = canvas_.data() + size_t(kViewH) * SW;
        leftBlit_.draw(ren_, corners, SW, kSplitX, kCornerH, g.left, crtOn_);
        rightBlit_.draw(ren_, corners + kSplitX, SW, SW - kSplitX, kCornerH, g.right, crtOn_);
        if (nameW_ > 0)
            nameBlit_.draw(ren_, canvas_.data() + size_t(kViewH + kCornerH) * SW, SW, nameW_, kNameH, g.name, crtOn_);
        return;
    }
    const SDL_Rect vs{0, 0, SW, kViewH};
    if (lightOn_ && roomTex_) SDL_RenderCopy(ren_, roomTex_, nullptr, &g.room);
    else SDL_RenderCopy(ren_, canvasTex_, &vs, &g.room);
    const SDL_Rect ls{0, kViewH, kSplitX, kCornerH};
    const SDL_Rect rs{kSplitX, kViewH, SW - kSplitX, kCornerH};
    SDL_RenderCopy(ren_, canvasTex_, &ls, &g.left);
    SDL_RenderCopy(ren_, canvasTex_, &rs, &g.right);
    if (nameW_ > 0) {
        const SDL_Rect ns{0, kViewH + kCornerH, nameW_, kNameH};
        SDL_RenderCopy(ren_, canvasTex_, &ns, &g.name);
    }
}

void PortingView::draw(int ww, int wh, const ViewOptions& o) {
    // room scale: the largest integer for which 320 columns and 176 rows fit; corners at 5/6 of it (5 at 1080p:
    // checked on 644 rooms, they never cover the parts of the rooms); the place name at the scale that fits in
    // the band above the room. Always integer scales (a non-integer scale would only stretch the corners and
    // blur the room).
    Geometry g;
    const int k = std::max(1, std::min(ww / SW, wh / kViewH));
    {
        const int c = std::max(1, k * 5 / 6);
        const int dw = SW * k, vx = (ww - dw) / 2;
        const int band = wh - kViewH * k;
        const int nsc = std::max(1, std::min(c, band / kNameH));
        const int vy = std::max(0, std::min(band, kNameH * nsc));
        const int ch = kCornerH * c;
        g.room = {vx, vy, dw, kViewH * k};
        g.left = {vx, wh - ch, kSplitX * c, ch};
        g.right = {vx + dw - (SW - kSplitX) * c, wh - ch, (SW - kSplitX) * c, ch};
        g.name = {vx + (dw - nameW_ * nsc) / 2, std::max(0, (vy - kNameH * nsc) / 2), nameW_ * nsc, kNameH * nsc};
    }
    g.k = k;
    crtOn_ = o.crt;
    const int vx = g.room.x, vy = g.room.y;
    // Dynamic Light (not with the CRT filter): the room is filtered at a scale that divides k (sharp pixels)
    lightOn_ = o.dynamicLight && !o.crt;
    if (lightOn_ && roomDirty_) {
        const int s = k % 3 == 0 ? 3 : k % 2 == 0 ? 2 : 1;
        light_.process(canvas_.data(), SW, kViewH, s);
        if (!roomTex_ || roomTexW_ != light_.width() || roomTexH_ != light_.height()) {
            if (roomTex_) SDL_DestroyTexture(roomTex_);
            roomTexW_ = light_.width(); roomTexH_ = light_.height();
            roomTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, roomTexW_, roomTexH_);
        }
        if (roomTex_) {
            SDL_SetTextureScaleMode(roomTex_, SDL_ScaleModeNearest);
            SDL_UpdateTexture(roomTex_, nullptr, light_.image().data(), roomTexW_ * 4);
        }
    }
    roomDirty_ = false;
    menuModern_ = o.modernBackground;
    // menus: at the largest scale for which their 200 rows fit, centred
    const int k2 = std::max(1, std::min(k, wh / SH));
    const SDL_Rect menuDst{(ww - SW * k2) / 2, (wh - SH * k2) / 2, SW * k2, SH * k2};
    auto drawMenu = [&](SDL_Texture* tex, const std::vector<uint32_t>& layer, PictureBlit& b) {
        if (crtOn_ && layer.size() >= size_t(SW) * SH) b.draw(ren_, layer.data(), SW, SW, SH, menuDst, crtOn_);
        else SDL_RenderCopy(ren_, tex, nullptr, &menuDst);
    };
    if (!overlay_) {
        drawLayout(g);
        wasOverlay_ = false;
        return;
    }
    if (!o.modernBackground && !wholeScreenOverlay_) {
        // original background: the layout as it is, covered by a checkerboard of black pixels as large as the
        // room pixels, as the game does; on top the menu, inactive windows with their checkerboard
        drawLayout(g);
        const int cw = ww / k + 4, chh = wh / k + 4;
        if (!checkerTex_ || checkerW_ != cw || checkerH_ != chh) {
            if (checkerTex_) SDL_DestroyTexture(checkerTex_);
            checkerW_ = cw; checkerH_ = chh;
            checkerTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, cw, chh);
            std::vector<uint32_t> cpx(size_t(cw) * size_t(chh));
            for (int y = 0; y < chh; ++y)
                for (int x = 0; x < cw; ++x) cpx[size_t(y) * size_t(cw) + size_t(x)] = ((x + y) & 1) ? 0xFF000000u : 0u;
            if (checkerTex_) { SDL_UpdateTexture(checkerTex_, nullptr, cpx.data(), cw * 4); SDL_SetTextureBlendMode(checkerTex_, SDL_BLENDMODE_BLEND); }
        }
        if (checkerTex_) {
            const SDL_Rect cd{vx % k - 2 * k, vy % k - 2 * k, cw * k, chh * k};   // aligned to the room pixels
            SDL_RenderCopy(ren_, checkerTex_, nullptr, &cd);
        }
        drawMenu(menuTex_, menuLayer_, menuBlit_);
        wasOverlay_ = true;
        return;
    }
    // modern background: the layout blurred and darkened, made once when the menu opens (the game is paused)
    // or when the window changes size
    if (!menuBg_ || menuBgW_ != ww || menuBgH_ != wh || !wasOverlay_ || !menuBgValid_) {
        if (menuBg_ && (menuBgW_ != ww || menuBgH_ != wh)) { SDL_DestroyTexture(menuBg_); menuBg_ = nullptr; }
        if (!menuBg_) menuBg_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ww, wh);
        menuBgW_ = ww; menuBgH_ = wh;
        SDL_Texture* target = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, ww, wh);
        std::vector<uint32_t> px(size_t(ww) * size_t(wh), 0);
        if (target && SDL_SetRenderTarget(ren_, target) == 0) {
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
            SDL_RenderClear(ren_);
            drawLayout(g);
            SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), ww * 4);
            SDL_SetRenderTarget(ren_, nullptr);
        }
        if (target) SDL_DestroyTexture(target);
        blurAndDarken(px, ww, wh, std::max(2, k * 2));
        SDL_UpdateTexture(menuBg_, nullptr, px.data(), ww * 4);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
        SDL_RenderClear(ren_);
        menuBgValid_ = true;
    }
    SDL_RenderCopy(ren_, menuBg_, nullptr, nullptr);
    SDL_RenderCopy(ren_, inactiveTex_, nullptr, &menuDst);     // already soft (linear scaling)
    drawMenu(menuTex_, menuLayer_, menuBlit_);
    wasOverlay_ = true;
}

} // namespace lm
