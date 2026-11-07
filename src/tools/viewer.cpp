// lmviewer: viewer of the original game graphics and of the rooms drawn by the C++ renderer (developer tool,
// not shipped). Usage: lmviewer [folder with the ADF images or the WHDLoad data folder]
//
// Keys:
//   1 = IFF screens (intro, endings, HAM6 backgrounds)   2 = bob/tile graphics (gfx*.epf)
//   Left/Right = previous/next entry                     Up/Down = previous/next archive
//   3 = rooms (C++ renderer)                             Up/Down in mode 3 = previous/next level
//   P / O = next/previous palette (mode 2)               F11 = fullscreen   Esc = quit
#include "formats/epf.h"
#include "formats/game_files.h"
#include "formats/ilbm.h"
#include "formats/planar.h"
#include "formats/rnc1.h"
#include "engine/room_renderer.h"
#include "formats/level.h"
#include "formats/text.h"
#include "platform/video.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace dm;

namespace {

// Extracts the first CODE hunk of an AmigaDOS executable.
Bytes hunkCode(const Bytes& exe) {
    if (exe.size() < 32 || be32(exe.data()) != 0x3F3) throw FormatError("HUNK: header missing");
    size_t o = 24; // header with 1 hunk: 0x3F3, 0, n, first, last, size[0]
    o += 4 * (be32(&exe[16]) - be32(&exe[12])); // size table past the first
    while (o + 8 <= exe.size()) {
        uint32_t t = be32(&exe[o]) & 0x3FFFFFFF;
        if (t == 0x3E9) {
            uint32_t n = be32(&exe[o + 4]) * 4;
            return Bytes(exe.begin() + long(o + 8), exe.begin() + long(o + 8 + n));
        }
        o += 4;
    }
    throw FormatError("HUNK: CODE not found");
}

struct ScreenItem { std::string label; IlbmImage img; };

class Viewer {
public:
    Viewer() = default;

    bool load() {
        Bytes code = hunkCode(rnc::unpack(readGameFile("MAIN.BIN")));
        renderer_ = std::make_unique<RoomRenderer>(code);
        code_ = code;
        for (uint32_t o = kMainPaletteTable; o + 32 <= code.size() && palettes_.size() < 40; o += kMainPaletteStride)
            palettes_.push_back(Palette::fromOcs(&code[o], 16));
        for (const std::string& name : GameFiles::instance().names()) {
            if (name.size() < 4 || name.substr(name.size() - 4) != ".epf") continue;
            const std::string stem = name.substr(0, name.size() - 4);
            EpfArchive a(readGameFile(name));
            if (stem.rfind("gfx", 0) == 0) {
                gfx_.push_back({stem, std::make_unique<EpfArchive>(std::move(a))});
                continue;
            }
            for (size_t i = 0; i < a.count(); ++i) {
                Bytes e = a.entry(i);
                if (e.size() > 12 && !std::memcmp(e.data(), "FORM", 4) && !std::memcmp(e.data() + 8, "ILBM", 4))
                    screens_.push_back({stem + " #" + std::to_string(i), decodeIlbm(e)});
            }
        }
        std::sort(gfx_.begin(), gfx_.end(), [](auto& a, auto& b) { return a.name < b.name; });
        std::sort(screens_.begin(), screens_.end(), [](auto& a, auto& b) { return a.label < b.label; });
        std::printf("palettes %zu, screens %zu, graphics archives %zu\n", palettes_.size(), screens_.size(), gfx_.size());
        return !screens_.empty();
    }

    void key(SDL_Keycode k) {
        switch (k) {
            case SDLK_1: mode_ = 1; break;
            case SDLK_2: mode_ = 2; break;
            case SDLK_3: mode_ = 3; break;
            case SDLK_RIGHT: step(+1); break;
            case SDLK_LEFT: step(-1); break;
            case SDLK_DOWN:
                if (mode_ == 2) { arch_ = (arch_ + 1) % int(gfx_.size()); entry_ = 0; }
                if (mode_ == 3) level_ = level_ % 3 + 1;
                break;
            case SDLK_UP:
                if (mode_ == 2) { arch_ = (arch_ + int(gfx_.size()) - 1) % int(gfx_.size()); entry_ = 0; }
                if (mode_ == 3) level_ = (level_ + 1) % 3 + 1;
                break;
            case SDLK_c: chars_ = !chars_; break;          // characters on/off
            case SDLK_r: seed_ = seed_ * 1103515245u + 12345u; break; // new draw of the visible characters
            case SDLK_p: pal_ = (pal_ + 1) % int(palettes_.size()); break;
            case SDLK_o: pal_ = (pal_ + int(palettes_.size()) - 1) % int(palettes_.size()); break;
            default: break;
        }
    }

    void draw(Video& v) {
        if (mode_ == 1) {
            const auto& s = screens_[size_t(screen_)];
            v.resizeLogical(s.img.width, s.img.height);
            auto rgb = s.img.toRgb();
            v.blitRgb(0, 0, s.img.width, s.img.height, rgb.data());
            v.setTitle("lmviewer - screens: " + s.label + (s.img.ham ? " (HAM6)" : ""));
            return;
        }
        if (mode_ == 3) { drawRoom(v); return; }
        v.resizeLogical(320, 200);
        v.clear({40, 0, 40});
        auto& g = gfx_[size_t(arch_)];
        // shows the entries from entry_ in a grid
        int x = 4, y = 4, rowH = 0, shown = 0;
        for (size_t i = size_t(entry_); i < g.arc->count(); ++i) {
            Bytes e = g.arc->entry(i);
            if (e.empty()) continue;
            int ww = guessWidthWords(e);
            if (!ww) continue;
            PlanarImage img = decodePlanar(e, ww, int(e.size() / (size_t(ww) * 8)));
            if (x + img.width > 316) { x = 4; y += rowH + 4; rowH = 0; }
            if (y + img.height > 196 && shown) break;
            v.blitIndexed(x, y, img.width, img.height, img.indices.data(), palettes_[size_t(pal_)]);
            x += img.width + 4; rowH = std::max(rowH, img.height); ++shown;
        }
        char t[160];
        std::snprintf(t, sizeof t, "lmviewer - graphics: %s from entry %d (%d shown), palette %d", g.name.c_str(), entry_, shown, pal_);
        v.setTitle(t);
    }

    void setRoom(int level, int room) { mode_ = 3; level_ = level; room_ = room; }

private:
    void drawRoom(Video& v) {
        auto& lv = levels_[level_];
        if (!lv) lv = std::make_unique<Level>(level_, code_);
        IndexedFrame f;
        std::string err;
        renderer_->setAnimFrame(int(SDL_GetTicks() / 60)); // the game advances the flames every 3 ticks at 50 Hz
        renderer_->setDrawCharacters(chars_);
        renderer_->setSpawnSeed(seed_);
        try { renderer_->render(*lv, room_, f); } catch (const std::exception& e) { err = e.what(); }
        v.resizeLogical(320, 200);
        v.clear();
        v.blitIndexed(0, 0, f.width, f.height, f.px.data(), f.palette);
        auto& names = roomNames_[level_];
        if (names.empty()) {
            try {
                char fn[32];
                std::snprintf(fn, sizeof fn, "txt%d%d000.epf", level_, level_);
                EpfArchive t(readGameFile(fn));
                const size_t lang = 3; // Italian (0 = EN, 1 = FR, 2 = DE, 3 = IT)
                names = roomNames(t.entry(lang * 3), t.entry(lang * 3 + 1), t.entry(lang * 3 + 2));
            } catch (const std::exception&) {}
        }
        auto nm = names.find(room_);
        char t[200];
        std::snprintf(t, sizeof t, "lmviewer - level %d, room %d - %s %s", level_, room_,
                      nm != names.end() ? nm->second.c_str() : "(no name)", err.c_str());
        v.setTitle(t);
    }

    void step(int d) {
        if (mode_ == 3) { room_ = (room_ + 255 + d) % 255; return; }
        if (mode_ == 1) screen_ = (screen_ + int(screens_.size()) + d) % int(screens_.size());
        else entry_ = std::clamp(entry_ + d * 8, 0, int(gfx_[size_t(arch_)].arc->count()) - 1);
    }

    struct Gfx { std::string name; std::unique_ptr<EpfArchive> arc; };
    std::vector<Palette> palettes_;
    std::vector<ScreenItem> screens_;
    std::vector<Gfx> gfx_;
    std::unique_ptr<RoomRenderer> renderer_;
    Bytes code_;
    std::map<int, std::unique_ptr<Level>> levels_;
    std::map<int, std::map<int, std::string>> roomNames_;
    int mode_ = 1, screen_ = 0, arch_ = 0, entry_ = 0, pal_ = 1, level_ = 1, room_ = 120;
    bool chars_ = true;          // characters (spawn rule $10984)
    uint32_t seed_ = 0x12345678u;
};

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> folders;
    if (argc > 1 && argv[1][0] != '-') folders.emplace_back(argv[1]);
    folders.emplace_back(".");
    std::string msg;
    if (!GameFiles::instance().locate(folders, msg)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "lmviewer", msg.c_str(), nullptr);
        return 1;
    }
    Viewer viewer;
    try {
        if (!viewer.load()) return 1;
    } catch (const std::exception& e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "lmviewer", e.what(), nullptr);
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) return 1;
    Video video;
    if (!video.init("Darkmere", 320, 200, 3)) return 1;

    // Test mode: "--frames N" runs N frames and saves the last one to viewer.bmp (headless check).
    int maxFrames = -1;
    int startLevel = 1;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--frames") maxFrames = std::atoi(argv[i + 1]);
        if (std::string(argv[i]) == "--mode") viewer.key(std::atoi(argv[i + 1]) == 2 ? SDLK_2 : SDLK_1);
        if (std::string(argv[i]) == "--level") startLevel = std::clamp(std::atoi(argv[i + 1]), 1, 3);
        if (std::string(argv[i]) == "--room") viewer.setRoom(startLevel, std::atoi(argv[i + 1]));
    }

    bool running = true;
    for (int frame = 0; running; ++frame) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                else if (ev.key.keysym.sym == SDLK_F11) video.toggleFullscreen();
                else viewer.key(ev.key.keysym.sym);
            }
        }
        viewer.draw(video);
        video.present();
        if (maxFrames >= 0 && frame + 1 >= maxFrames) {
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(video.frame().data(), video.width(), video.height(), 24,
                                                                video.width() * 3, SDL_PIXELFORMAT_RGB24);
            SDL_SaveBMP(s, "viewer.bmp");
            SDL_FreeSurface(s);
            running = false;
        }
        SDL_Delay(16);
    }
    video.shutdown();
    SDL_Quit();
    return 0;
}
