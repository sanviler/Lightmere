#pragma once
// Lightmere's own small windows, drawn with the game's plain 8x8 font (fnt4_000.epf, entry 0, 256 ASCII
// characters at 1 bit per pixel):
//  - the options menu (Escape), in the language chosen at the start of the game;
//  - the debug teleport panel (F10, only with --debug).
#include "formats/common.h"
#include <cstdint>
#include <string>
#include <vector>

namespace lm {

// an ARGB pixel buffer with a border and lines of text
class TextPanel {
public:
    TextPanel(int w, int h) : w_(w), h_(h), px_(size_t(w) * size_t(h), 0) {}
    void setFont(dm::Bytes font) { font_ = std::move(font); }
    bool hasFont() const { return font_.size() >= 2048; }
    void clear(uint32_t background, uint32_t border);
    void text(int x, int y, const std::string& s, uint32_t colour);
    void textCentred(int y, const std::string& s, uint32_t colour) { text((w_ - 8 * int(s.size())) / 2, y, s, colour); }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint32_t>& pixels() const { return px_; }

private:
    int w_, h_;
    std::vector<uint32_t> px_;
    dm::Bytes font_;
};

// game languages, in the order of the keys 1-4 on the language screen (L1FBCD)
enum class Language { English = 0, French = 1, German = 2, Italian = 3 };

struct OptionsMenu {
    enum Item { Fullscreen, DynamicLight, ModernBackground, Port1, Port2, Quit, Back, ItemCount };
    bool open = false;
    int focus = 0;
    // what is plugged into a control port, as the menu shows it
    struct PortText {
        int kind = 0;               // 0 nothing, 1 keyboard, 2 mouse, 3 joystick
        int number = 0;             // joystick number, 0 = not connected
        std::string name;           // joystick name (shown at the bottom when the item has the focus)
    };
    // draws the menu with the current values of the three switches and the two ports
    void draw(TextPanel& p, Language lang, bool fullscreen, bool dynamicLight, bool modernBackground,
              const PortText& port1, const PortText& port2) const;
    static constexpr int kWidth = 288, kHeight = 148;
};

struct TeleportPanel {
    bool open = false;
    std::string field[2] = {"1", ""};   // level, room
    int focus = 1;
    std::string message;
    void draw(TextPanel& p, bool waitingForLevel) const;
    static constexpr int kWidth = 200, kHeight = 72;
};

} // namespace lm
