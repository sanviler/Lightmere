#include "platform/video.h"
#include <cstring>

namespace dm {

bool Video::init(const char* title, int lw, int lh, int scale) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest: sharp pixels
    win_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, lw * scale, lh * scale,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) return false;
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) return false;
    resizeLogical(lw, lh);
    return true;
}

void Video::resizeLogical(int w, int h) {
    if (w == w_ && h == h_ && tex_) return;
    w_ = w; h_ = h;
    frame_.assign(size_t(w) * h, Rgb{0, 0, 0});
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_RenderSetLogicalSize(ren_, w, h);
    SDL_RenderSetIntegerScale(ren_, SDL_TRUE);
}

void Video::shutdown() {
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    tex_ = nullptr; ren_ = nullptr; win_ = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Video::clear(Rgb c) { std::fill(frame_.begin(), frame_.end(), c); }

void Video::blitIndexed(int x0, int y0, int w, int h, const uint8_t* idx, const Palette& pal, int transparent) {
    for (int y = 0; y < h; ++y) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h_) continue;
        for (int x = 0; x < w; ++x) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w_) continue;
            uint8_t v = idx[size_t(y) * w + x];
            if (v == transparent) continue;
            frame_[size_t(sy) * w_ + sx] = pal.c[v];
        }
    }
}

void Video::blitRgb(int x0, int y0, int w, int h, const Rgb* px) {
    for (int y = 0; y < h; ++y) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h_) continue;
        for (int x = 0; x < w; ++x) {
            int sx = x0 + x;
            if (sx >= 0 && sx < w_) frame_[size_t(sy) * w_ + sx] = px[size_t(y) * w + x];
        }
    }
}

void Video::present() {
    void* pixels;
    int pitch;
    if (SDL_LockTexture(tex_, nullptr, &pixels, &pitch) == 0) {
        for (int y = 0; y < h_; ++y)
            std::memcpy(static_cast<uint8_t*>(pixels) + size_t(y) * pitch, &frame_[size_t(y) * w_], size_t(w_) * 3);
        SDL_UnlockTexture(tex_);
    }
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    SDL_RenderPresent(ren_);
}

void Video::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(win_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void Video::setTitle(const std::string& t) { SDL_SetWindowTitle(win_, t.c_str()); }

} // namespace dm
