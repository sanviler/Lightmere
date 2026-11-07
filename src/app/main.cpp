// Lightmere: a port of the Amiga game Darkmere (Core Design, 1994) to modern systems.
//
// The original 68000 code of the game runs unchanged on a small Amiga machine written in C++ (src/amiga); the
// room is drawn again by a C++ renderer from the state of the game (src/game, src/engine, src/app). No part of
// the original game is included: the program reads the user's own copy (the four ADF disk images, or the
// "data" folder of the WHDLoad installation) from the folder of the executable.
//
// Keys:
//   control ports             set in the options menu: nothing, keyboard, mouse or any joystick for each of the
//                             two Amiga ports (default: port 1 mouse, port 2 the first joystick, otherwise the
//                             keyboard joystick: numeric keypad 8/4/6/5 = directions, Right Ctrl = fire)
//                             all other keys go to the Amiga keyboard, arrows included
//   Escape                    options menu (fullscreen, dynamic light, modern background, quit)
//   F7                        background of menus and dialogs: modern (blurred) / original (black checkerboard)
//   F9                        Dynamic Light filter on / off
//   F11                       window / fullscreen
//   F12                       screenshot (PNG next to the executable)
//   with --debug only:  F8    Lightmere view / original screen      F10   teleport to a level and room
//   game controller           stick or d-pad = joystick, A = fire, B = second button, Start = options menu
//                             (the controller must be assigned to a port in the options menu)
//
// Options:
//   --debug              full window title (level, room, mode, filter, background) and the F8 / F10 tools
//   --fullscreen         start in fullscreen
//   --data <folder>      where the game files are (overrides game_path in lightmere.ini; default: the folder
//                        of the executable)
//   --saves <folder>     where the save disk is kept (default: "saves" next to the executable)
//   --original           only the original game code (no routines translated to C++)
//   --custom1 N          cheats for testing: 1 infinite help, 2 infinite magic, 4 infinite energy
//   --texts <path>       corrected game texts (a folder of text files made by lmtext, or one file): for
//                        translators, to see their corrections in the game
// Automated tests (developers): --test-frames N, --test-script file, --window-size WxH.
#include "amiga/machine.h"
#include "app/input.h"
#include "app/keymap.h"
#include "app/porting_view.h"
#include "app/ui.h"
#include "formats/epf.h"
#include "formats/game_files.h"
#include "formats/text_files.h"
#include "game/fixes.h"
#include "game/ham_fix.h"
#include "game/live_view.h"
#include "game/panel_text.h"
#include "game/routines.h"
#include "platform/folder_dialog.h"
#include "platform/png_write.h"
#include "platform/settings.h"
#include "version.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kTitle = "Lightmere ver. " LM_VERSION_STRING;

// paths: UTF-8 in lightmere.ini and from the system dialogs, native in std::filesystem
fs::path toPath(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string toUtf8(const fs::path& p) { const std::u8string u = p.generic_u8string(); return std::string(u.begin(), u.end()); }

void fail(const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Lightmere", msg.c_str(), nullptr);
}

// Game addresses (offsets from the first hunk of MAIN.BIN)
constexpr uint32_t kLevel = 0x1E688;          // byte: level - 1
constexpr uint32_t kRoom = 0x1E6CC;           // word: current room
constexpr uint32_t kLevelChange = 0x1E6D2;    // bit 1: change level (the game then adds 1 to kLevel)
constexpr uint32_t kHero = 0x22DB8;           // long: the hero's entity
constexpr uint32_t kLanguage = 0x1FBCD;       // byte: language chosen at the start (0-3, keys 1-4)

// moves the hero to (x, y) in room units (RAM coordinates) of the given room
void placeHero(amiga::Machine& m, int room, int x, int y) {
    const uint32_t pb = m.gameBase();
    const uint32_t e = m.peek32(pb + kHero);
    if (room >= 0) { m.poke16(e + 4, uint16_t(room)); m.poke16(pb + kRoom, uint16_t(room)); }
    m.poke32(e + 42, uint32_t(x * 0x16A) << 8);
    m.poke32(e + 46, uint32_t(y * 0x16A) << 8);
}

} // namespace

int main(int argc, char** argv) {
    std::string dataDir, savesDir, textsPath;
    bool debug = false, fullscreen = false, originalOnly = false;
    uint32_t custom1 = 0;
    long testFrames = -1;
    int winW = 0, winH = 0;
    std::string testScript;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--debug") debug = true;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--data") dataDir = next();
        else if (a == "--texts") textsPath = next();
        else if (a == "--saves") savesDir = next();
        else if (a == "--original") originalOnly = true;
        else if (a == "--custom1") custom1 = uint32_t(std::strtoul(next().c_str(), nullptr, 0));
        else if (a == "--test-frames") testFrames = std::atol(next().c_str());
        else if (a == "--test-script") testScript = next();
        else if (a == "--window-size") {
            const std::string v = next();
            winW = std::atoi(v.c_str());
            if (const auto x = v.find('x'); x != std::string::npos) winH = std::atoi(v.c_str() + x + 1);
        }
    }
    const bool testing = testFrames >= 0;
    if (testing) debug = true;                      // tests use the debug tools (teleport, full title)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    // audio and controllers are optional: without them the game still runs (silent, keyboard only)
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) std::fprintf(stderr, "no game controllers: %s\n", SDL_GetError());
    char* base = SDL_GetBasePath();
    const fs::path exeDir = base ? toPath(base) : fs::current_path();
    SDL_free(base);

    // settings kept between runs (lightmere.ini next to the executable; not in tests, which always start the
    // same way)
    const std::string settingsPath = (exeDir / "lightmere.ini").string();
    dm::Settings settings;
    const bool haveSettings = !testing && settings.load(settingsPath);

    // The user's copy of the game: the folder in game_path (lightmere.ini; relative to the executable or
    // absolute, default "." = next to the executable), or --data. The files are checked against the SHA-256
    // fingerprints of the known version. If they are not there, the user is asked to choose the folder, which
    // is then saved in game_path.
    auto resolve = [&](const std::string& p) {
        const fs::path q = toPath(p.empty() ? std::string(".") : p);
        fs::path r = (q.is_absolute() ? q : exeDir / q).lexically_normal();
        if (r.filename() == "." || r.filename().empty()) r = r.parent_path();   // "folder/." or "folder/"
        return r.string();
    };
    dm::GameFiles& gameFiles = dm::GameFiles::instance();
    const std::string gamePath = !dataDir.empty() ? dataDir : settings.get("game_path", ".");
    std::string msg;
    bool found = gameFiles.open(resolve(gamePath), msg);
    if (!found && dataDir.empty() && gamePath == ".") {
        std::string ignored;                         // started from a terminal in the game folder
        found = gameFiles.open(fs::current_path().string(), ignored);
    }
    while (!found) {
        if (testing || !dm::folderDialogAvailable()) { fail(msg); SDL_Quit(); return 1; }
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder..."},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
        const std::string text = msg + "\n\nChoose the folder that contains the Darkmere disk images?";
        const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, nullptr, "Lightmere",
                                     text.c_str(), 2, buttons, nullptr};
        int choice = 0;
        if (SDL_ShowMessageBox(&box, &choice) != 0 || choice != 1) { SDL_Quit(); return 1; }
        std::string chosen;
        if (!dm::chooseFolder("Choose the folder with the Darkmere ADF disk images", toUtf8(exeDir), chosen)) continue;
        found = gameFiles.open(toPath(chosen).string(), msg);
        if (found && !testing) {
            // saved relative to the executable when it is inside its folder (portable), otherwise absolute
            std::error_code ec;
            const fs::path rel = fs::relative(toPath(chosen), exeDir, ec);
            const bool inside = !ec && !rel.empty() && rel.begin()->string() != "..";
            settings.set("game_path", inside ? toUtf8(rel) : toUtf8(toPath(chosen)));
            settings.save(settingsPath);
        }
    }
    std::fprintf(stderr, "game files: %s (verified)\n", gameFiles.description().c_str());
    if (savesDir.empty()) savesDir = (exeDir / "saves").string();

    // corrected texts (--texts): put into the game files in memory; the texts of the program are written into
    // the program once it is loaded
    static std::vector<dm::text::ProgramPatch> programTexts;
    if (!textsPath.empty()) {
        std::vector<dm::text::TextFile> files;
        std::string terr;
        dm::text::ApplyReport r;
        if (!dm::text::loadTexts(toPath(textsPath).string(), files, terr)) r.errors.push_back(terr);
        else {
            r = dm::text::apply(files, gameFiles);
            programTexts = dm::text::programPatches(files, gameFiles, r);
        }
        std::fprintf(stderr, "texts: %d messages corrected, %zu warnings, %zu errors\n", r.changed, r.warnings.size(), r.errors.size());
        std::string list;
        int shown = 0;
        for (const auto* v : {&r.errors, &r.warnings})
            for (const std::string& s : *v) {
                std::fprintf(stderr, "texts: %s\n", s.c_str());
                if (shown++ < 12) list += "- " + s + "\n";
            }
        if (!list.empty()) {
            if (shown > 12) list += "- ... (" + std::to_string(shown - 12) + " more)\n";
            SDL_ShowSimpleMessageBox(r.errors.empty() ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_ERROR, kTitle,
                                     ("Corrected texts (" + textsPath + "): " + std::to_string(r.changed) +
                                      " messages applied.\n\n" + list).c_str(), nullptr);
        }
    }

    amiga::Machine m;
    m.log = [](const std::string& s) { std::fprintf(stderr, "[amiga] %s\n", s.c_str()); };
    static dm::game::Routines routineStats;
    const dm::game::HookMode rmode = originalOnly ? dm::game::HookMode::Off : dm::game::HookMode::Replace;
    static dm::game::HamFix hamFix;                 // black specks in the level presentation pictures
    hamFix.load();
    static dm::game::PanelText panelText;           // strings of the panel (place name, target name)
    m.onGamePatched = [rmode](amiga::Machine& mm) {
        for (const dm::text::ProgramPatch& p : programTexts) {   // corrected texts of the program
            const uint32_t a = mm.gameBase() + p.offset;
            bool same = true;
            for (size_t k = 0; k < p.original.size(); ++k) same = same && mm.peek8(a + uint32_t(k)) == uint8_t(p.original[k]);
            if (same) for (size_t k = 0; k < p.text.size(); ++k) mm.poke8(a + uint32_t(k), uint8_t(p.text[k]));
        }
        dm::game::applyFixes(mm);
        dm::game::installRoutines(mm, rmode, routineStats);
        panelText.install(mm);
    };
    m.setCustom1(custom1);
    std::string err;
    if (!m.boot(savesDir, err)) { fail("The game could not be started: " + err); SDL_Quit(); return 1; }

    lm::ViewOptions opt;
    bool portingMode = true;                        // F8 (debug): Lightmere view or original screen
    if (haveSettings) {
        opt.dynamicLight = settings.get("dynamic_light", "off") == "on";
        opt.modernBackground = settings.get("modern_background", "on") == "on";
        if (!fullscreen) fullscreen = settings.get("fullscreen", "off") == "on";
    }
    // the two Amiga control ports
    lm::InputPorts ports;
    if (haveSettings && !settings.get("port1").empty()) ports.load(settings.get("port1"), settings.get("port2"));
    else ports.setDefaults();

    constexpr int W = amiga::Machine::kFbWidth, H = amiga::Machine::kFbHeight;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* win = SDL_CreateWindow(kTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fail(SDL_GetError()); SDL_Quit(); return 1; }
    if (winW > 0 && winH > 0) SDL_SetWindowSize(win, winW, winH);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fail(SDL_GetError()); SDL_Quit(); return 1; }
    if (fullscreen) SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    auto isFullscreen = [&] { return (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0; };
    auto saveSettings = [&] {
        if (testing) return;
        settings.set("dynamic_light", opt.dynamicLight ? "on" : "off");
        settings.set("modern_background", opt.modernBackground ? "on" : "off");
        settings.set("fullscreen", isFullscreen() ? "on" : "off");
        settings.set("port1", ports.save(0));
        settings.set("port2", ports.save(1));
        if (settings.get("game_path").empty()) settings.set("game_path", ".");
        settings.save(settingsPath);
    };

    // the Lightmere view; without it (an unexpected game version) the original screen is shown
    std::unique_ptr<dm::game::LiveView> live;
    std::unique_ptr<lm::PortingView> view;
    try {
        live = std::make_unique<dm::game::LiveView>(dm::game::LiveView::loadMainCode(), 0);
        view = std::make_unique<lm::PortingView>(ren, *live, panelText);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Lightmere view not available: %s\n", e.what());
        portingMode = false;
    }
    SDL_Texture* screenTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    const SDL_Rect amigaSrc{34, 18, 640, 200};      // the 320 x 200 game screen in the Amiga frame buffer

    // Lightmere's own windows (options menu, teleport), drawn with the game's 8x8 font
    lm::OptionsMenu menu;
    lm::TeleportPanel tp;
    lm::TextPanel menuPanel(lm::OptionsMenu::kWidth, lm::OptionsMenu::kHeight);
    lm::TextPanel tpPanel(lm::TeleportPanel::kWidth, lm::TeleportPanel::kHeight);
    try {
        const dm::Bytes font = dm::EpfArchive(dm::readGameFile("fnt4_000.epf")).entry(0);
        menuPanel.setFont(font);
        tpPanel.setFont(font);
    } catch (const std::exception& e) { std::fprintf(stderr, "font: %s\n", e.what()); }
    SDL_Texture* menuTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, menuPanel.width(), menuPanel.height());
    SDL_SetTextureBlendMode(menuTex, SDL_BLENDMODE_BLEND);
    SDL_Texture* tpTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tpPanel.width(), tpPanel.height());
    SDL_SetTextureBlendMode(tpTex, SDL_BLENDMODE_BLEND);

    // Teleport (F10, debug): in the same level the hero is moved at once to the free cell nearest the centre of
    // the room; for another level the game's own level change is used ($0C5AA: bit 1 of kLevelChange, then
    // kLevel + 1, so kLevel is set to level - 2), the presentation is closed with fire and then the hero is moved.
    struct PendingTeleport {
        int level = 0, room = -1;
        bool needPresentation = false, sawPresentation = false;
        int afterPresentation = 0;
        long waitFrames = 0;
    } pending;
    int autoFire = 0;                               // frames of automatic fire (closing the presentation)
    auto submitTeleport = [&] {
        const int lvl = tp.field[0].empty() ? 0 : std::atoi(tp.field[0].c_str());
        const int rm = tp.field[1].empty() ? -1 : std::atoi(tp.field[1].c_str());
        int tx = 0, ty = 0;
        const uint32_t pb = m.gameBase();
        if (!pb || !view || !view->everInGame()) tp.message = "START PLAYING FIRST";
        else if (lvl < 1 || lvl > 3) tp.message = "LEVEL 1 TO 3";
        else if (!live->freeCell(lvl, rm, tx, ty)) tp.message = "NO SUCH ROOM";
        else {
            pending = {};
            pending.level = lvl; pending.room = rm;
            const int cur = m.peek8(pb + kLevel) + 1;
            pending.needPresentation = cur != lvl;
            if (cur != lvl) {
                m.poke8(pb + kLevel, uint8_t(lvl - 2));
                m.poke8(pb + kLevelChange, uint8_t(m.peek8(pb + kLevelChange) | 2));
            }
            tp.open = false;
        }
    };
    auto language = [&] {
        const uint32_t pb = m.gameBase();
        if (!pb || !view || !view->everInGame()) return lm::Language::English;   // not chosen yet
        return lm::Language(m.peek8(pb + kLanguage) & 3);
    };

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    const SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio) { m.setAudioRate(have.freq); SDL_PauseAudioDevice(audio, 0); }
    const uint32_t bytesPerFrame = uint32_t(have.freq ? have.freq : 44100) / 50 * 4;

    bool running = true, shotRequested = false;
    uint64_t nextTick = SDL_GetPerformanceCounter();
    const uint64_t perFrame = SDL_GetPerformanceFrequency() / 50;
    int shotN = 0;
    std::string lastTitle;
    uint32_t fpsT0 = SDL_GetTicks();
    long fpsCount = 0;
    int fps = -1;

    // automated tests: script lines "frame command args"
    std::vector<std::string> script;
    if (!testScript.empty()) {
        std::ifstream in(testScript);
        for (std::string l; std::getline(in, l);) if (!l.empty()) script.push_back(l);
    }
    int scriptJoy = 0;
    bool scriptShot = false, closeTpAfterShot = false;
    long lastScriptFrame = -1;

    // Amiga keys held down: released when a menu opens, so that none stays pressed in the game
    bool amigaKeyDown[0x80] = {};
    auto releaseKeys = [&] {
        for (int k = 0; k < 0x80; ++k)
            if (amigaKeyDown[k]) { amigaKeyDown[k] = false; m.keyEvent(uint8_t(k), false); }
    };
    auto menuActivate = [&](int item, int fromLeftRight) {   // 0 = Enter, -1 / +1 = left / right
        switch (item) {
            case lm::OptionsMenu::Fullscreen:
                SDL_SetWindowFullscreen(win, isFullscreen() ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); saveSettings(); break;
            case lm::OptionsMenu::DynamicLight:
                opt.dynamicLight = !opt.dynamicLight; if (view) view->invalidate(); saveSettings(); break;
            case lm::OptionsMenu::ModernBackground:
                opt.modernBackground = !opt.modernBackground; if (view) view->invalidate(); saveSettings(); break;
            case lm::OptionsMenu::Port1: case lm::OptionsMenu::Port2:
                ports.cycle(item - lm::OptionsMenu::Port1, fromLeftRight < 0 ? -1 : 1); saveSettings(); break;
            case lm::OptionsMenu::Quit: if (!fromLeftRight) running = false; break;
            case lm::OptionsMenu::Back: if (!fromLeftRight) menu.open = false; break;
            default: break;
        }
    };

    while (running && !m.aborted()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ports.handleEvent(ev);
            switch (ev.type) {
                case SDL_QUIT: running = false; break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) { menu.open = !menu.open; menu.focus = 0; releaseKeys(); }
                    else if (menu.open) {
                        const int b = ev.cbutton.button;
                        if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) menu.focus = (menu.focus + lm::OptionsMenu::ItemCount - 1) % lm::OptionsMenu::ItemCount;
                        else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) menu.focus = (menu.focus + 1) % lm::OptionsMenu::ItemCount;
                        else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) menuActivate(menu.focus, -1);
                        else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) menuActivate(menu.focus, 1);
                        else if (b == SDL_CONTROLLER_BUTTON_A) menuActivate(menu.focus, 0);
                        else if (b == SDL_CONTROLLER_BUTTON_B) menu.open = false;
                    }
                    break;
                case SDL_KEYDOWN: case SDL_KEYUP: {
                    const bool dn = ev.type == SDL_KEYDOWN;
                    const SDL_Scancode sc = ev.key.keysym.scancode;
                    // options menu (Escape): the game is paused while it is open
                    if (menu.open) {
                        if (!dn) break;
                        if (sc == SDL_SCANCODE_ESCAPE) menu.open = false;
                        else if (sc == SDL_SCANCODE_UP) menu.focus = (menu.focus + lm::OptionsMenu::ItemCount - 1) % lm::OptionsMenu::ItemCount;
                        else if (sc == SDL_SCANCODE_DOWN) menu.focus = (menu.focus + 1) % lm::OptionsMenu::ItemCount;
                        else if (sc == SDL_SCANCODE_LEFT) menuActivate(menu.focus, -1);
                        else if (sc == SDL_SCANCODE_RIGHT) menuActivate(menu.focus, 1);
                        else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE) menuActivate(menu.focus, 0);
                        else if (sc == SDL_SCANCODE_F11) menuActivate(lm::OptionsMenu::Fullscreen, 1);
                        break;
                    }
                    // teleport panel (F10, debug)
                    if (debug && dn && sc == SDL_SCANCODE_F10) {
                        tp.open = !tp.open;
                        if (tp.open) {
                            if (const uint32_t pb = m.gameBase()) {
                                tp.field[0] = std::to_string(m.peek8(pb + kLevel) + 1);
                                tp.field[1] = std::to_string(m.peek16(pb + kRoom));
                            }
                            tp.focus = 1; tp.message.clear();
                            releaseKeys();
                        }
                        break;
                    }
                    if (tp.open) {
                        if (!dn) break;
                        std::string& fld = tp.field[tp.focus];
                        int digit = -1;
                        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) digit = 1 + (sc - SDL_SCANCODE_1);
                        else if (sc == SDL_SCANCODE_0) digit = 0;
                        else if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) digit = 1 + (sc - SDL_SCANCODE_KP_1);
                        else if (sc == SDL_SCANCODE_KP_0) digit = 0;
                        if (digit >= 0 && fld.size() < (tp.focus == 0 ? 1u : 3u)) fld.push_back(char('0' + digit));
                        else if (sc == SDL_SCANCODE_BACKSPACE && !fld.empty()) fld.pop_back();
                        else if (sc == SDL_SCANCODE_TAB || sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN) tp.focus ^= 1;
                        else if (sc == SDL_SCANCODE_ESCAPE) tp.open = false;
                        else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) submitTeleport();
                        break;
                    }
                    if (dn && sc == SDL_SCANCODE_ESCAPE) {
                        menu.open = true; menu.focus = 0;
                        releaseKeys();
                        break;
                    }
                    if (dn && sc == SDL_SCANCODE_F7) {
                        if (portingMode && view) { opt.modernBackground = !opt.modernBackground; view->invalidate(); saveSettings(); }
                        break;
                    }
                    if (dn && sc == SDL_SCANCODE_F9) {
                        if (portingMode && view) { opt.dynamicLight = !opt.dynamicLight; view->invalidate(); saveSettings(); }
                        break;
                    }
                    if (dn && sc == SDL_SCANCODE_F8) {
                        if (debug && view) { portingMode = !portingMode; view->invalidate(); }
                        break;
                    }
                    if (dn && sc == SDL_SCANCODE_F11) {
                        SDL_SetWindowFullscreen(win, isFullscreen() ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        saveSettings();
                        break;
                    }
                    if (dn && sc == SDL_SCANCODE_F12) { shotRequested = true; break; }
                    if (sc == SDL_SCANCODE_F8 || sc == SDL_SCANCODE_F10) break;   // without --debug: no effect
                    if (ev.key.repeat) break;
                    if (ports.isJoystickKey(sc)) break;    // keypad and Right Ctrl: the keyboard joystick
                    auto it = lm::amigaKeymap().find(sc);
                    if (it != lm::amigaKeymap().end() && it->second < 0x80 && amigaKeyDown[it->second] != dn) {
                        amigaKeyDown[it->second] = dn;
                        m.keyEvent(it->second, dn);
                    }
                    break;
                }
                default: break;
            }
        }
        // automated tests: start (language 4, fire through the intro) and script
        bool testFire = false, testRight = false, testDown = false;
        if (testing) {
            const long fc = long(m.frameCount());
            for (long t : {1600L, 2000L, 2400L, 3000L}) if (fc >= t && fc < t + 10) testFire = true;
            testRight = fc >= 3100 && fc < 3200;
            testDown = fc >= 3200 && fc < 3260;
        }
        if (long(m.frameCount()) != lastScriptFrame) {
            lastScriptFrame = long(m.frameCount());
            for (const std::string& l : script) {
                std::istringstream ls(l);
                long fr; std::string cmd; ls >> fr >> cmd;
                if (fr != lastScriptFrame) continue;
                const uint32_t pb = m.gameBase();
                if (cmd == "room" && pb) { int n, x, y; ls >> n >> x >> y; placeHero(m, n, x, y); }
                else if (cmd == "pos" && pb) { int x, y; ls >> x >> y; placeHero(m, -1, x, y); }
                else if (cmd == "joy") ls >> scriptJoy;
                else if (cmd == "shot") scriptShot = true;
                else if (cmd == "f12") shotRequested = true;
                else if (cmd == "light") { int v = 0; ls >> v; opt.dynamicLight = v != 0; if (view) view->invalidate(); }
                else if (cmd == "menubg") { int v = 1; ls >> v; opt.modernBackground = v != 0; if (view) view->invalidate(); }
                else if (cmd == "view") { int v = 1; ls >> v; portingMode = v && view; if (view) view->invalidate(); }
                else if (cmd == "menu") { int v = 1; ls >> v; menu.open = v != 0; menu.focus = 0; }
                else if (cmd == "focus") { ls >> menu.focus; }
                else if (cmd == "cycle") { int port = 0, dir = 1; ls >> port >> dir; ports.cycle(port, dir); }
                else if (cmd == "f10") { tp.open = true; tp.field[0] = "2"; tp.field[1] = "4"; tp.focus = 1; shotRequested = true; closeTpAfterShot = true; }
                else if (cmd == "tp") {
                    ls >> tp.field[0] >> tp.field[1];
                    submitTeleport();
                    if (!tp.message.empty()) std::fprintf(stderr, "teleport: %s\n", tp.message.c_str());
                }
            }
        }
        if (autoFire > 0) testFire = true;
        const int extra = scriptJoy | (testDown ? 2 : 0) | (testRight ? 8 : 0) | (testFire ? 16 : 0);
        ports.apply(m, extra, menu.open || tp.open);

        // 50 Hz: with audio the queue of samples sets the pace, otherwise the timer
        const bool runNow = audio ? SDL_GetQueuedAudioSize(audio) < bytesPerFrame * 3 : SDL_GetPerformanceCounter() >= nextTick;
        if (!runNow) { SDL_Delay(1); continue; }
        nextTick += perFrame;
        const bool paused = menu.open || tp.open;
        if (paused) {
            if (audio && SDL_GetQueuedAudioSize(audio) < bytesPerFrame * 3) {
                std::vector<int16_t> silence(bytesPerFrame / 2, 0);
                SDL_QueueAudio(audio, silence.data(), bytesPerFrame);
            }
            if (testing) m.runFrame();              // tests keep counting frames (the script goes on)
        } else {
            m.runFrame();
            if (autoFire > 0) --autoFire;
            if (pending.room >= 0) {
                const uint32_t pb = m.gameBase();
                ++pending.waitFrames;
                const int cur = pb ? m.peek8(pb + kLevel) + 1 : 0;
                const bool ham = m.firstBplCon0 & 0x0800;
                if (ham) { pending.sawPresentation = true; pending.afterPresentation = 0; }
                else if (pending.sawPresentation) ++pending.afterPresentation;
                // same level: at once, in game; another level: 2 seconds after the presentation
                const bool ready = pending.needPresentation ? pending.afterPresentation >= 100 : (view && view->panelOnScreen());
                if (ready && cur == pending.level && (m.peek8(pb + kLevelChange) & 2) == 0) {
                    int tx = 0, ty = 0;
                    if (live->freeCell(pending.level, pending.room, tx, ty)) placeHero(m, pending.room, tx, ty);
                    pending.room = -1;
                } else if (ham && pending.waitFrames % 120 == 60) {
                    autoFire = 8;                   // level presentation: "press fire"
                } else if (pending.waitFrames > 6000) pending.room = -1;   // give up after 2 minutes
            }
            hamFix.apply(m);
            if (view) view->update(m, testing);
            if (audio) {
                auto& a = m.audioOut();
                if (!a.empty()) SDL_QueueAudio(audio, a.data(), uint32_t(a.size() * sizeof(int16_t)));
            }
        }
        if (testing) {
            const long fc = long(m.frameCount());
            if (fc == 100) m.keyEvent(0x04, true);
            if (fc == 103) m.keyEvent(0x04, false);
            if (fc >= testFrames) running = false;
        }

        // window title: "Lightmere ver. x.y.z - FPS"; with --debug also level, room, mode, filter, background
        ++fpsCount;
        if (const uint32_t now = SDL_GetTicks(); now - fpsT0 >= 1000) {
            fps = int((fpsCount * 1000 + (now - fpsT0) / 2) / (now - fpsT0));
            fpsCount = 0; fpsT0 = now;
        }
        std::string title = kTitle;
        if (debug) {
            if (const uint32_t pb = m.gameBase(); pb && view && view->everInGame()) {
                const int lvl = m.peek8(pb + kLevel) + 1, rm = m.peek16(pb + kRoom);
                if (lvl >= 1 && lvl <= 3 && rm < 0xFF) title += " - Level: " + std::to_string(lvl) + ", Room: " + std::to_string(rm);
            }
            title += !view ? " - Mode: Original (Lightmere view not available)" : portingMode ? " - Mode: Porting" : " - Mode: Original";
            title += (portingMode && opt.dynamicLight) ? " - Filter: Dynamic Light" : " - Filter: No Filter";
            if (portingMode) title += opt.modernBackground ? " - Background: Modern" : " - Background: Original";
        }
        if (fps >= 0 && !testing) title += " - " + std::to_string(fps) + " FPS";
        if (title != lastTitle) {
            SDL_SetWindowTitle(win, title.c_str());
            lastTitle = title;
            if (testing) std::fprintf(stderr, "title: %s\n", title.c_str());
        }

        // drawing
        int ww = 0, wh = 0;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (portingMode && view && view->active()) {
            view->draw(ww, wh, opt);
        } else {
            SDL_UpdateTexture(screenTex, nullptr, m.framebuffer().data(), W * 4);
            const int k = std::max(1, std::min(ww / 320, wh / 200));
            // outside the game (language, intro, presentations) centred; original mode (F8) at the top
            const SDL_Rect dst{(ww - 320 * k) / 2, portingMode ? (wh - 200 * k) / 2 : 0, 320 * k, 200 * k};
            SDL_RenderCopy(ren, screenTex, &amigaSrc, &dst);
        }
        auto drawPanel = [&](lm::TextPanel& p, SDL_Texture* t) {
            SDL_UpdateTexture(t, nullptr, p.pixels().data(), p.width() * 4);
            const int ks = std::max(1, std::min(ww / (p.width() * 2), wh / (p.height() * 3)));
            const SDL_Rect d{(ww - p.width() * ks) / 2, (wh - p.height() * ks) / 2, p.width() * ks, p.height() * ks};
            SDL_RenderCopy(ren, t, nullptr, &d);
        };
        if (menu.open) {
            lm::OptionsMenu::PortText pt[2];
            for (int i = 0; i < 2; ++i) {
                const lm::PortLabel l = ports.label(i);
                pt[i] = {int(l.kind), l.number, l.name};
            }
            menu.draw(menuPanel, language(), isFullscreen(), opt.dynamicLight, opt.modernBackground, pt[0], pt[1]);
            drawPanel(menuPanel, menuTex);
        } else if (tp.open || pending.room >= 0) {
            tp.draw(tpPanel, !tp.open);
            drawPanel(tpPanel, tpTex);
        }
        if (shotRequested) {                         // F12: what the window shows, lossless PNG
            shotRequested = false;
            std::vector<uint32_t> argb(size_t(ww) * size_t(wh));
            if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, argb.data(), ww * 4) == 0) {
                std::vector<uint8_t> rgb(argb.size() * 3);
                for (size_t i = 0; i < argb.size(); ++i) {
                    rgb[i * 3] = uint8_t(argb[i] >> 16); rgb[i * 3 + 1] = uint8_t(argb[i] >> 8); rgb[i * 3 + 2] = uint8_t(argb[i]);
                }
                fs::path name;
                do name = exeDir / ("lightmere_" LM_VERSION_STRING "_" + std::to_string(shotN++) + ".png"); while (fs::exists(name));
                dm::writePng(name.string(), ww, wh, rgb);
            }
            if (closeTpAfterShot) { tp.open = false; closeTpAfterShot = false; }
        }
        if (testing && (!running || scriptShot)) {   // tests: contents of the window
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ww, wh, 32, SDL_PIXELFORMAT_ARGB8888);
            if (s && SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
                SDL_SaveBMP(s, running ? ("lightmere_test_" + std::to_string(m.frameCount()) + ".bmp").c_str() : "lightmere_test_window.bmp");
            if (s) SDL_FreeSurface(s);
            scriptShot = false;
        }
        SDL_RenderPresent(ren);
    }
    saveSettings();
    if (audio) SDL_CloseAudioDevice(audio);
    view.reset();
    SDL_DestroyTexture(menuTex);
    SDL_DestroyTexture(tpTex);
    SDL_DestroyTexture(screenTex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
