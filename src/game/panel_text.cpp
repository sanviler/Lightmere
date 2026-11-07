#include "game/panel_text.h"
#include "amiga/cpu.h"
#include <cstdio>

namespace dm::game {

namespace {
std::string readString(const amiga::Machine& m, uint32_t a) {
    std::string s;
    for (int i = 0; i < 64; ++i) { const uint8_t c = m.peek8(a + uint32_t(i)); if (!c) break; s.push_back(char(c)); }
    return s;
}
} // namespace

void PanelText::install(amiga::Machine& m) {
    const uint32_t base = m.gameBase();
    // Every hook runs its observation and then the original instruction (cpu().execute()).

    // $01376: panel redraw starts; the drawing of the background and of the display list ends before $0114C is
    // called at $013F2 (place name)
    m.addHook(base + 0x01376, [this](amiga::Machine& mm) {
        inPanel_ = true;
        recording_.clear();
        mm.cpu().execute();
    });
    m.addHook(base + 0x013F2, [this](amiga::Machine& mm) {
        if (inPanel_ && !recording_.empty()) { panelDraws_ = recording_; ++generation_; }
        inPanel_ = false;
        mm.cpu().execute();
    });
    m.addHook(base + 0x0ACD2, [this](amiga::Machine& mm) {
        auto& c = mm.cpu();
        const PanelDraw d{int16_t(c.getD(0)), int16_t(c.getD(1)), uint16_t(c.getD(2))};
        if (d.descriptor != 0xFFFF) {
            if (inPanel_) recording_.push_back(d);
            else if (d.x == 272 && d.y == 144) {
                bool known = false;
                for (const auto& q : portraits_) known = known || q.descriptor == d.descriptor;
                if (!known) portraits_.push_back(d);
            }
        }
        c.execute();
    });
    // $0114C writes the place name, only if it is not empty, right after a panel redraw: the name is cleared
    // here and $0117C sets it again when it is written
    m.addHook(base + 0x0114C, [this](amiga::Machine& mm) {
        roomName.clear();
        mm.cpu().execute();
    });
    // $0111A clears the target box (name and portrait); $0107A then writes the name again only if there is a
    // target (L1E660): without a target the name is gone
    m.addHook(base + 0x0111A, [this](amiga::Machine& mm) {
        targetName.clear();
        if (log) std::fprintf(stderr, "panel: target box cleared\n");
        mm.cpu().execute();
    });
    m.addHook(base + 0x0117C, [this](amiga::Machine& mm) {
        auto& c = mm.cpu();
        const int x = int16_t(c.getD(0)), y = int16_t(c.getD(1));
        const std::string s = readString(mm, c.getA(0));
        if (y == 183 && x == 24) roomName = s;
        else if (y == 183 && x == 192) targetName = s;
        if (log) std::fprintf(stderr, "panel: (%d, %d) \"%s\"\n", x, y, s.c_str());
        c.execute();
    });
}

} // namespace dm::game
