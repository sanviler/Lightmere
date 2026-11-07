#pragma once
// Dynamic Light: an optional filter of the Lightmere view, applied to the room image (320 x 176, the same image
// shown without the filter) before it is scaled up. The original graphics are not changed.
//  - rooms with fire are put in a cool half-light, with a warm light around the flames (torches, lanterns,
//    fireplaces), a glow on the flames and slightly darker edges;
//  - rooms without fire (woods, streets by day) stay almost as they are; entering and leaving a room the
//    half-light changes gradually.
// Flames are recognised by their colour (bright yellow-orange), so the light follows the flame animation and
// flickers with it. No SDL dependency: an ARGB image in, a scaled ARGB image out.
#include <cstdint>
#include <vector>

namespace dm::gfx {

struct LightParams {
    float ambient[3] = {0.52f, 0.55f, 0.67f};          // half-light of rooms with fire (multiplies the colours)
    float ambientNoFire[3] = {0.94f, 0.94f, 0.98f};    // rooms without fire
    float firePixelsDark = 40.0f;                      // flame pixels for full half-light (a torch has about 70)
    float warm[3] = {1.25f, 0.85f, 0.50f};             // colour of the fire light
    float farWeight = 0.75f, farGamma = 0.6f;          // wide halo
    float nearWeight = 0.55f;                          // halo close to the flames
    float farSigma = 55.0f, nearSigma = 10.0f;         // radii in pixels of the original room
    float refPixels = 150.0f;                          // flame pixels giving the full light (a fireplace)
    float glow[3] = {0.54f, 0.33f, 0.12f};             // glow on the flames (added)
    float glowSigma = 2.3f;
    float vignette = 0.28f;                            // darker edges
    float smoothing = 0.5f;                            // blend with the light of the previous frame (0 = none)
};

class DynamicLight {
public:
    LightParams params;
    // src: w x h ARGB pixels; the output is scaled by "scale" (1-4) with repeated pixels
    void process(const uint32_t* src, int w, int h, int scale);
    const std::vector<uint32_t>& image() const { return out_; }
    int width() const { return outW_; }
    int height() const { return outH_; }
    void resetHistory() { prevFar_.clear(); prevNear_.clear(); darkness_ = -1; }

private:
    std::vector<uint32_t> out_;
    int outW_ = 0, outH_ = 0;
    std::vector<float> emis_, far_, near_, glow_, prevFar_, prevNear_, scratch_;
    std::vector<int> mul_, add_;                       // per-pixel factors of the room (3 channels, fixed point)
    std::vector<int> rowM_, rowA_;                     // factors of one output row, before horizontal interpolation
    std::vector<std::vector<int>> rowBufs_;            // the same, one pair per thread
    float powLut_[1025] = {};
    float powKey_ = -1;
    float darkness_ = -1;                              // current half-light (0-1)
    float farRefCalc_ = 1, nearRefCalc_ = 1;           // full light, computed from refPixels and the radii
    float refKey_[3] = {-1, -1, -1};
};

} // namespace dm::gfx
