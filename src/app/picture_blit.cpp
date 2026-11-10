#include "app/picture_blit.h"
#include <algorithm>

namespace lm {

PictureBlit::~PictureBlit() { release(); }

void PictureBlit::release() {
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = nullptr; tw_ = th_ = 0; valid_ = false;
}

void PictureBlit::draw(SDL_Renderer* ren, const uint32_t* src, int stride, int sw, int sh, const SDL_Rect& dst,
                       bool crt, int xstep, bool useAlpha) {
    const int W = dst.w, H = dst.h;
    if (sw <= 0 || sh <= 0 || W <= 0 || H <= 0) return;
    cur_.resize(size_t(sw) * size_t(sh));
    bool transparent = false;
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            const uint32_t p = src[size_t(y) * size_t(stride) + size_t(x) * size_t(xstep)];
            cur_[size_t(y) * size_t(sw) + size_t(x)] = p;
            transparent |= useAlpha && (p >> 24) == 0;
        }
    const bool same = valid_ && tex_ && W == lastW_ && H == lastH_ && sw == lastSw_ && sh == lastSh_ && crt == lastCrt_ &&
                      cur_ == last_;
    if (!same) {
        rgb_.resize(cur_.size());
        for (size_t i = 0; i < cur_.size(); ++i)
            rgb_[i] = dm::Rgb{uint8_t(cur_[i] >> 16), uint8_t(cur_[i] >> 8), uint8_t(cur_[i])};
        out_.resize(size_t(W) * size_t(H) * 3);
        const int s = W / sw;
        if (s >= 1 && W == sw * s && H == sh * s) {
            if (crt && s >= 2) dm::picture::integerCrt(rgb_.data(), sw, sh, s, out_.data(), wk_);
            else dm::picture::integerSharp(rgb_.data(), sw, sh, s, out_.data());
        } else if (crt) {
            dm::picture::fillCrt(rgb_.data(), sw, sh, W, H, out_.data(), wk_);
        } else {
            dm::picture::fillSharp(rgb_.data(), sw, sh, W, H, out_.data());
        }
        argb_.resize(size_t(W) * size_t(H));
        for (int y = 0; y < H; ++y) {
            const int sy = std::min(sh - 1, int(int64_t(y) * sh / H));
            for (int x = 0; x < W; ++x) {
                const size_t o = size_t(y) * size_t(W) + size_t(x);
                uint32_t a = 0xFF000000u;
                if (transparent) {
                    const int sx = std::min(sw - 1, int(int64_t(x) * sw / W));
                    a = cur_[size_t(sy) * size_t(sw) + size_t(sx)] & 0xFF000000u;
                }
                argb_[o] = a ? (a | uint32_t(out_[o * 3]) << 16 | uint32_t(out_[o * 3 + 1]) << 8 | out_[o * 3 + 2]) : 0u;
            }
        }
        if (!tex_ || tw_ != W || th_ != H) {
            if (tex_) SDL_DestroyTexture(tex_);
            tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
            tw_ = W; th_ = H;
            if (tex_) SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
        }
        if (tex_) SDL_UpdateTexture(tex_, nullptr, argb_.data(), W * 4);
        last_.swap(cur_);
        lastW_ = W; lastH_ = H; lastSw_ = sw; lastSh_ = sh; lastCrt_ = crt; valid_ = tex_ != nullptr;
    }
    if (tex_) SDL_RenderCopy(ren, tex_, nullptr, &dst);       // 1:1
}

} // namespace lm
