#pragma once
// Black specks and a coloured fringe in the HAM level presentation pictures (lev11000/22000/33000,
// entry 2). They are in the original files: isolated pixels of colour 0 (black) among bright colours, and the
// first two columns of every row, where HAM starts from colour 0 and takes a few pixels to reach the right colour.
// At start the three pictures are decoded and, for every pixel to fix, the HAM value (6 bits) is computed that
// gives the colour nearest to the average of the pixels around, without changing the following pixels.
// Every frame, if the screen is in HAM mode and the planes show one of these pictures, those pixels are fixed
// in the plane memory (idempotent: the same values are written again).
#include "amiga/machine.h"
#include <string>
#include <vector>

namespace dm::game {

class HamFix {
public:
    void load();          // silent if the files are missing
    void apply(amiga::Machine& m);
    size_t patchCount() const { size_t n = 0; for (const auto& i : images_) n += i.patches.size(); return n; }

private:
    struct Patch { int x, y; uint8_t value; };
    struct Image {
        int height = 0;
        std::vector<uint8_t> plane0;                // plane 0, 40 bytes per row
        std::vector<Patch> patches;
    };
    std::vector<Image> images_;
};

} // namespace dm::game
