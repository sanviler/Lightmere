#include "render/enhance.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <thread>

namespace dm::gfx {

namespace {

// three passes of a horizontal and vertical moving average (nearly Gaussian, sigma ~ r), zero outside
void blur(std::vector<float>& img, int w, int h, float sigma, std::vector<float>& tmp) {
    const int r = std::max(1, int(std::lround(sigma)));
    const float inv = 1.0f / float(2 * r + 1);
    tmp.resize(img.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            const float* s = &img[size_t(y) * size_t(w)];
            float* d = &tmp[size_t(y) * size_t(w)];
            float sum = 0;
            for (int x = 0; x <= r && x < w; ++x) sum += s[x];
            for (int x = 0; x < w; ++x) {
                d[x] = sum * inv;
                if (x + r + 1 < w) sum += s[x + r + 1];
                if (x - r >= 0) sum -= s[x - r];
            }
        }
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            for (int y = 0; y <= r && y < h; ++y) sum += tmp[size_t(y) * size_t(w) + size_t(x)];
            for (int y = 0; y < h; ++y) {
                img[size_t(y) * size_t(w) + size_t(x)] = sum * inv;
                if (y + r + 1 < h) sum += tmp[size_t(y + r + 1) * size_t(w) + size_t(x)];
                if (y - r >= 0) sum -= tmp[size_t(y - r) * size_t(w) + size_t(x)];
            }
        }
    }
}

// peak of the light made by a square patch of n flame pixels: the full light
float referencePeak(float sigma, float n) {
    const int side = std::max(1, int(std::lround(std::sqrt(n))));
    const int w = 4 * int(sigma) + side + 8;
    std::vector<float> img(size_t(w) * size_t(w), 0.0f), tmp;
    const int o = (w - side) / 2;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) img[size_t(o + y) * size_t(w) + size_t(o + x)] = 1.0f;
    blur(img, w, w, sigma, tmp);
    return *std::max_element(img.begin(), img.end());
}

} // namespace

void DynamicLight::process(const uint32_t* src, int w, int h, int scale) {
    if (w <= 0 || h <= 0) return;
    const size_t n = size_t(w) * size_t(h);
    const LightParams& p = params;

    // 1. flames: bright yellow-orange pixels
    emis_.assign(n, 0.0f);
    int flames = 0;
    for (size_t i = 0; i < n; ++i) {
        const int r = int((src[i] >> 16) & 255), g = int((src[i] >> 8) & 255), b = int(src[i] & 255);
        if (r > 191 && g > 115 && b < 140 && r - b > 89) { emis_[i] = 1.0f; ++flames; }
    }
    // half-light only where there is fire (a torch has about 70 flame pixels, a fireplace over 150); gradual
    // change, about half a second, entering and leaving rooms
    const float dark = std::min(1.0f, float(flames) / p.firePixelsDark);
    darkness_ = darkness_ < 0 ? dark : darkness_ * 0.92f + dark * 0.08f;
    float ambient[3];
    for (int c = 0; c < 3; ++c) ambient[c] = p.ambientNoFire[c] + (p.ambient[c] - p.ambientNoFire[c]) * darkness_;
    // 2. halos, wide and near, normalised on the light of a fireplace; glow on the flames
    far_ = emis_; near_ = emis_; glow_ = emis_;
    blur(far_, w, h, p.farSigma, scratch_);
    blur(near_, w, h, p.nearSigma, scratch_);
    blur(glow_, w, h, p.glowSigma, scratch_);
    if (refKey_[0] != p.refPixels || refKey_[1] != p.farSigma || refKey_[2] != p.nearSigma) {
        farRefCalc_ = referencePeak(p.farSigma, p.refPixels);
        nearRefCalc_ = referencePeak(p.nearSigma, p.refPixels);
        refKey_[0] = p.refPixels; refKey_[1] = p.farSigma; refKey_[2] = p.nearSigma;
    }
    const float fr = farRefCalc_, nr = nearRefCalc_;
    for (size_t i = 0; i < n; ++i) { far_[i] = std::min(1.0f, far_[i] / fr); near_[i] = std::min(1.0f, near_[i] / nr); }
    // softer flicker: half of the light of the previous frame
    if (p.smoothing > 0 && prevFar_.size() == n) {
        for (size_t i = 0; i < n; ++i) {
            far_[i] = far_[i] * (1 - p.smoothing) + prevFar_[i] * p.smoothing;
            near_[i] = near_[i] * (1 - p.smoothing) + prevNear_[i] * p.smoothing;
        }
    }
    prevFar_ = far_; prevNear_ = near_;
    // 3. per-pixel factors of the room, in fixed point: colour = (source x mul) / 256 + add
    if (powKey_ != p.farGamma) {
        for (int i = 0; i < 1025; ++i) powLut_[size_t(i)] = std::pow(float(i) / 1024.0f, p.farGamma);
        powKey_ = p.farGamma;
    }
    mul_.resize(n * 3); add_.resize(n * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            const float L = std::min(1.0f, p.farWeight * powLut_[size_t(far_[i] * 1024.0f)] + p.nearWeight * near_[i]);
            const float vx = (float(x) / float(w) - 0.5f) * 1.6f, vy = (float(y) / float(h) - 0.45f) * 1.8f;
            const float vig = std::clamp(1.0f - p.vignette * (0.4f + 0.6f * darkness_) * (vx * vx + vy * vy), 0.5f, 1.0f);
            const float gl = std::min(1.0f, glow_[i] * 1.6f);
            for (int c = 0; c < 3; ++c) {
                mul_[i * 3 + size_t(c)] = int((ambient[c] + p.warm[c] * L) * vig * 256.0f + 0.5f);
                add_[i * 3 + size_t(c)] = int(gl * p.glow[c] * 255.0f * 256.0f + 0.5f);   // 8 fractional bits
            }
        }
    // 4. output size (the source pixels are repeated, read on the fly)
    const int s = std::clamp(scale, 1, 4);
    outW_ = w * s; outH_ = h * s;
    out_.resize(size_t(outW_) * size_t(outH_));
    // 5. light: factors interpolated (bilinear, separable) over the scaled pixels
    std::vector<int> cx0(static_cast<size_t>(outW_)), cx1(static_cast<size_t>(outW_)), cwx(static_cast<size_t>(outW_));
    for (int X = 0; X < outW_; ++X) {
        const float fx = (float(X) + 0.5f) / float(s) - 0.5f;
        const int x0 = int(std::floor(fx));
        cwx[size_t(X)] = int((fx - float(x0)) * 256.0f + 0.5f);
        cx0[size_t(X)] = std::clamp(x0, 0, w - 1) * 3; cx1[size_t(X)] = std::clamp(x0 + 1, 0, w - 1) * 3;
    }
    // output rows split among threads (they are independent)
    auto rows = [&](int yFrom, int yTo, std::vector<int>& rowM, std::vector<int>& rowA) {
        rowM.resize(size_t(w) * 3); rowA.resize(size_t(w) * 3);
        for (int Y = yFrom; Y < yTo; ++Y) {
            const float fy = (float(Y) + 0.5f) / float(s) - 0.5f;
            const int y0 = int(std::floor(fy));
            const int wy = int((fy - float(y0)) * 256.0f + 0.5f);
            const int* m0 = &mul_[size_t(std::clamp(y0, 0, h - 1)) * size_t(w) * 3];
            const int* m1 = &mul_[size_t(std::clamp(y0 + 1, 0, h - 1)) * size_t(w) * 3];
            const int* a0 = &add_[size_t(std::clamp(y0, 0, h - 1)) * size_t(w) * 3];
            const int* a1 = &add_[size_t(std::clamp(y0 + 1, 0, h - 1)) * size_t(w) * 3];
            for (size_t k = 0; k < size_t(w) * 3; ++k) {
                rowM[k] = (m0[k] * (256 - wy) + m1[k] * wy) >> 8;
                rowA[k] = (a0[k] * (256 - wy) + a1[k] * wy) >> 8;
            }
            const uint32_t* srow = src + size_t(Y / s) * size_t(w);
            uint32_t* o = &out_[size_t(Y) * size_t(outW_)];
            for (int X = 0; X < outW_; ++X) {
                const int wx = cwx[size_t(X)], iw = 256 - wx;
                const int* ma = &rowM[size_t(cx0[size_t(X)])];
                const int* mb = &rowM[size_t(cx1[size_t(X)])];
                const int* aa = &rowA[size_t(cx0[size_t(X)])];
                const int* ab = &rowA[size_t(cx1[size_t(X)])];
                const uint32_t px = srow[X / s];
                int r = ((int((px >> 16) & 255) * ((ma[0] * iw + mb[0] * wx) >> 8)) + ((aa[0] * iw + ab[0] * wx) >> 8) + 128) >> 8;
                int g = ((int((px >> 8) & 255) * ((ma[1] * iw + mb[1] * wx) >> 8)) + ((aa[1] * iw + ab[1] * wx) >> 8) + 128) >> 8;
                int bl = ((int(px & 255) * ((ma[2] * iw + mb[2] * wx) >> 8)) + ((aa[2] * iw + ab[2] * wx) >> 8) + 128) >> 8;
                r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; bl = bl > 255 ? 255 : bl;
                o[X] = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(bl);
            }
        }
    };
    const int nt = std::clamp(int(std::thread::hardware_concurrency()) - 1, 1, 4);
    if (nt == 1) rows(0, outH_, rowM_, rowA_);
    else {
        rowBufs_.resize(size_t(nt) * 2);
        std::vector<std::thread> pool;
        for (int t = 1; t < nt; ++t)
            pool.emplace_back(rows, outH_ * t / nt, outH_ * (t + 1) / nt, std::ref(rowBufs_[size_t(t) * 2]), std::ref(rowBufs_[size_t(t) * 2 + 1]));
        rows(0, outH_ / nt, rowBufs_[0], rowBufs_[1]);
        for (auto& th : pool) th.join();
    }
}

} // namespace dm::gfx
