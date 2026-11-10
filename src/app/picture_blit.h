#pragma once
// Draws a part of an ARGB picture into a rectangle of the render target with the picture filters of the video
// layer (platform/picture.h): integer scale (sharp, or CRT at exactly that scale) when the rectangle is an
// integer multiple of the part, otherwise fill (sharp bilinear or CRT for non-integer scales).
// Pixels with alpha 0 stay transparent: the mask is taken from the nearest source pixel (sharp edges), so the
// panel corners and the place name can still be drawn over the room.
// The filtered image is kept: when the source part and the rectangle do not change, nothing is computed again.
#include "platform/picture.h"
#include <SDL.h>
#include <cstdint>
#include <vector>

namespace lm {

class PictureBlit {
public:
    PictureBlit() = default;
    ~PictureBlit();
    PictureBlit(const PictureBlit&) = delete;
    PictureBlit& operator=(const PictureBlit&) = delete;

    // src: first pixel of the part, `stride` pixels per row; `xstep` source pixels per picture pixel (2 for the
    // hires frame buffer of the machine); useAlpha = false: the alpha
    // byte is ignored, every pixel is opaque
    void draw(SDL_Renderer* ren, const uint32_t* src, int stride, int sw, int sh, const SDL_Rect& dst, bool crt,
              int xstep = 1, bool useAlpha = true);
    // frees the texture (before the renderer is destroyed)
    void release();

private:
    SDL_Texture* tex_ = nullptr;
    int tw_ = 0, th_ = 0;
    std::vector<uint32_t> cur_, last_;
    std::vector<dm::Rgb> rgb_;
    std::vector<uint8_t> out_;
    std::vector<uint32_t> argb_;
    dm::picture::Work wk_;
    int lastW_ = 0, lastH_ = 0, lastSw_ = 0, lastSh_ = 0;
    bool lastCrt_ = false, valid_ = false;
};

} // namespace lm
