#pragma once
// Video layer of the viewer: shows an RGB frame buffer at the native resolution
// (320x200 PAL lowres), scaled by an integer factor (sharp pixels).
//
#include "formats/palette.h"
#include <SDL.h>
#include <string>
#include <vector>

namespace dm {

class Video {
public:
    bool init(const char* title, int logicalW, int logicalH, int scale = 3);
    void shutdown();
    void resizeLogical(int w, int h);

    int width() const { return w_; }
    int height() const { return h_; }
    std::vector<Rgb>& frame() { return frame_; }
    void clear(Rgb c = {0, 0, 0});
    void blitIndexed(int x, int y, int w, int h, const uint8_t* idx, const Palette& pal, int transparent = -1);
    void blitRgb(int x, int y, int w, int h, const Rgb* px);

    void present();
    void toggleFullscreen();
    void setTitle(const std::string& t);

private:
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
    std::vector<Rgb> frame_;
    bool fullscreen_ = false;
};

} // namespace dm
