#pragma once
// The Lightmere view of the game ("porting" mode).
//
// Layout (1920 x 1080 example):
//  - at the top a band with the place name, centred;
//  - below it the room drawn by the C++ renderer from the game state (rows 8-183 of the original screen), at
//    an integer scale k (6 at 1080p, 8 at 1440p, 12 in 4K);
//  - in the bottom corners the two halves of the original panel, split where its artwork splits (x = 158), at
//    5/6 of the room scale: on the left the hero's portrait, energy and name ("Ebryn"); on the right the target
//    (portrait, energy, name aligned to the right). Rooms are diamonds: the bottom corners are empty.
// Only the panel pixels are taken from the corners of the original screen; the panel strings are rewritten
// with the game's own descriptor font, so the place name can move to the top band.
//
// Frames that do not show the room as the renderer sees it:
//  - room changes: the last C++ view of the old room stays still until the game shows the new one;
//    banners and other changes of the screen show the game screen inside the layout;
//  - fades (entering or leaving interiors): the game darkens the whole screen, panel included; the game
//    screen is shown inside the layout, and what Lightmere draws itself darkens with it;
//  - menus and dialogs: separated from the room (menu_layers.h) and shown over the layout, blurred
//    and darkened (modern background) or covered by the black checkerboard of the game (original background);
//  - anything else without the panel (game over): the whole original screen over the blurred layout.
#include "amiga/machine.h"
#include "app/panel_shape.h"
#include "app/picture_blit.h"
#include "engine/room_renderer.h"
#include "game/live_view.h"
#include "game/panel_text.h"
#include "render/enhance.h"
#include <SDL.h>
#include <unordered_map>
#include <vector>

namespace lm {

struct ViewOptions {
    bool dynamicLight = false;
    bool modernBackground = true;
    // CRT filter (crt-pixie style, at the integer scale of each part) on the room, the corners, the place name
    // and the game menus. One graphics filter at a time: with the CRT filter on, Dynamic Light is not used.
    bool crt = false;
};

class PortingView {
public:
    PortingView(SDL_Renderer* ren, dm::game::LiveView& live, dm::game::PanelText& text);
    ~PortingView();
    PortingView(const PortingView&) = delete;
    PortingView& operator=(const PortingView&) = delete;

    // analyses the frame the machine has just produced; log: one line per frame on stderr (tests)
    void update(amiga::Machine& m, bool log);
    // the layout is shown (in game); otherwise the original screen is shown
    bool active() const { return showNew_; }
    // the game panel has been on screen at least once (the game has started)
    bool everInGame() const { return everInGame_; }
    // the game panel is fully visible in this frame
    bool panelOnScreen() const { return panelOnScreen_; }
    // draws the layout into the whole render target (ww x wh)
    void draw(int ww, int wh, const ViewOptions& o);
    // the options changed: the room and the menu background are made again
    void invalidate() { roomDirty_ = true; menuBgValid_ = false; light_.resetHistory(); }

    // geometry of the original screen used by the layout
    static constexpr int kViewY0 = 8, kViewH = 176;                 // room rows
    static constexpr int kCornerY0 = 127, kCornerH = 73, kSplitX = 158;   // corners: rows 127-199, split at x = 158
    static constexpr int kNameY0 = 183, kNameH = 8;                 // row of the names in the panel
    static constexpr int kCanvasH = kViewH + kCornerH + kNameH;     // canvas: room, corners, place name

private:
    void detectPanel(amiga::Machine& m);
    void buildCanvas(amiga::Machine& m, bool screen, double fade);
    struct Geometry {
        SDL_Rect room, left, right, name;   // destination rectangles
        int k = 1;                          // room scale rounded (checkerboard, blur)
    };
    void drawLayout(const Geometry& g);

    SDL_Renderer* ren_;
    dm::game::LiveView& live_;
    dm::game::PanelText& text_;
    PanelShape shape_;
    dm::IndexedFrame liveFrame_, textFrame_;
    dm::gfx::DynamicLight light_;

    // panel and fades
    bool panelOnScreen_ = false, everInGame_ = false, fadeLike_ = false;
    double fade_ = 1.0;
    std::unordered_map<uint8_t, uint32_t> panelRef_;   // panel colours at full light (index -> RGB)
    long panelRefLum_ = 0;
    // view state
    bool showNew_ = false, overlay_ = false, wholeScreenOverlay_ = false, seenPanel_ = false;
    int lastLevel_ = -1, stableRoom_ = -1, freezeRun_ = 0, underRun_ = 0;
    bool roomFreeze_ = false, screenMode_ = false;
    std::vector<uint32_t> screen_, lastScreen_, lastRoom_;    // 320 x 200 RGB
    std::unordered_map<uint8_t, uint32_t> textColour_;      // colours of the panel font (index -> RGB)
    // images and textures
    std::vector<uint32_t> canvas_, menuLayer_, inactiveLayer_;
    int nameW_ = 0;
    bool roomDirty_ = true, menuBgValid_ = false, wasOverlay_ = false;
    bool lightOn_ = false, menuModern_ = true;      // options of the last draw
    SDL_Texture* canvasTex_ = nullptr;
    SDL_Texture* roomTex_ = nullptr;
    int roomTexW_ = 0, roomTexH_ = 0;
    SDL_Texture* menuTex_ = nullptr;
    SDL_Texture* inactiveTex_ = nullptr;
    SDL_Texture* menuBg_ = nullptr;
    int menuBgW_ = 0, menuBgH_ = 0;
    bool crtOn_ = false;                        // the regions go through PictureBlit (CRT)
    PictureBlit roomBlit_, leftBlit_, rightBlit_, nameBlit_, menuBlit_;
    SDL_Texture* checkerTex_ = nullptr;
    int checkerW_ = 0, checkerH_ = 0;
};

} // namespace lm
