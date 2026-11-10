#include "app/ui.h"
#include <SDL.h>

namespace lm {

namespace {
constexpr uint32_t kWhite = 0xFFF0F0E0u, kGold = 0xFFE0B040u, kGrey = 0xFF909090u;
constexpr uint32_t kBackground = 0xE8101018u, kBorder = 0xFFC8A060u;

// menu texts; the font has no accented letters
struct MenuTexts {
    const char* title;
    const char* items[OptionsMenu::ItemCount];
    const char* on;
    const char* off;
    const char* filters[3];         // off, CRT, Dynamic Light
    const char* hint;
    const char* devices[4];         // nothing, keyboard, mouse, joystick
    const char* missing;            // joystick not connected
    const char* keyboardHint;
};
const MenuTexts kTexts[4] = {
    {"OPTIONS", {"FULLSCREEN", "GRAPHICS FILTER", "MODERN BACKGROUND", "PORT 1", "PORT 2", "QUIT GAME", "BACK TO GAME"},
     "ON", "OFF", {"OFF", "CRT", "DYNAMIC LIGHT"}, "ALT+F4 ALSO QUITS", {"NONE", "KEYBOARD", "MOUSE", "JOYSTICK"}, "NOT CONNECTED",
     "KEYPAD 8456, RIGHT CTRL FIRE"},
    {"OPTIONS", {"PLEIN ECRAN", "FILTRE GRAPHIQUE", "FOND MODERNE", "PORT 1", "PORT 2", "QUITTER LE JEU",
     "RETOUR AU JEU"}, "OUI", "NON", {"AUCUN", "CRT", "LUMIERE DYNAMIQUE"}, "ALT+F4 QUITTE AUSSI", {"AUCUN", "CLAVIER", "SOURIS", "JOYSTICK"},
     "NON CONNECTE", "PAVE 8456, CTRL DROIT TIR"},
    {"OPTIONEN", {"VOLLBILD", "GRAFIKFILTER", "MODERNER HINTERGRUND", "PORT 1", "PORT 2", "SPIEL BEENDEN",
     "ZURUECK ZUM SPIEL"}, "AN", "AUS", {"AUS", "CRT", "DYNAMISCHES LICHT"}, "ALT+F4 BEENDET AUCH", {"KEINS", "TASTATUR", "MAUS", "JOYSTICK"},
     "NICHT VERBUNDEN", "ZIFFERNBLOCK 8456, STRG RECHTS"},
    {"OPZIONI", {"SCHERMO INTERO", "FILTRO GRAFICO", "SFONDO MODERNO", "PORTA 1", "PORTA 2", "ESCI DAL GIOCO",
     "TORNA AL GIOCO"}, "SI", "NO", {"NESSUNO", "CRT", "LUCE DINAMICA"}, "ANCHE ALT+F4 ESCE", {"NESSUNO", "TASTIERA", "MOUSE", "JOYSTICK"},
     "NON COLLEGATO", "TASTIERINO 8456, CTRL DX FUOCO"},
};
} // namespace

void TextPanel::clear(uint32_t background, uint32_t border) {
    std::fill(px_.begin(), px_.end(), background);
    for (int x = 0; x < w_; ++x) { px_[size_t(x)] = border; px_[size_t(h_ - 1) * size_t(w_) + size_t(x)] = border; }
    for (int y = 0; y < h_; ++y) { px_[size_t(y) * size_t(w_)] = border; px_[size_t(y) * size_t(w_) + size_t(w_ - 1)] = border; }
}

void TextPanel::text(int x, int y, const std::string& s, uint32_t colour) {
    if (!hasFont()) return;
    for (size_t i = 0; i < s.size(); ++i)
        for (int r = 0; r < 8; ++r) {
            const uint8_t bits = font_[size_t(uint8_t(s[i])) * 8 + size_t(r)];
            for (int c = 0; c < 8; ++c) {
                if (!((bits >> (7 - c)) & 1)) continue;
                const int px = x + int(i) * 8 + c, py = y + r;
                if (px >= 0 && px < w_ && py >= 0 && py < h_) px_[size_t(py) * size_t(w_) + size_t(px)] = colour;
            }
        }
}

void OptionsMenu::render(dm::OptionsPanel& p, Language lang, bool fullscreen, GraphicsFilter filter,
                         bool modernBackground, const PortText& port1, const PortText& port2) const {
    const MenuTexts& t = kTexts[int(lang) & 3];
    auto portValue = [&](const PortText& pt) {
        std::string v = t.devices[pt.kind & 3];
        if (pt.kind == 3 && pt.number > 0) v += " " + std::to_string(pt.number);
        return v;
    };
    std::vector<dm::MenuItem> items = {
        {t.items[Fullscreen], fullscreen ? t.on : t.off},
        {t.items[Filter], t.filters[int(filter) % 3]},
        {t.items[ModernBackground], modernBackground ? t.on : t.off},
        {t.items[Port1], portValue(port1)},
        {t.items[Port2], portValue(port2)},
        {t.items[Quit], ""},
        {t.items[Back], ""}};
    // bottom line: details of the focused port, otherwise the quit hint
    std::string hint = t.hint;
    if (focus == Port1 || focus == Port2) {
        const PortText& pt = focus == Port1 ? port1 : port2;
        if (pt.kind == 1) hint = t.keyboardHint;
        else if (pt.kind == 3) hint = pt.number == 0 ? std::string(t.missing) : pt.name;
        else hint.clear();
    }
    for (char& ch : hint) if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);   // the font has capitals only
    p.title = t.title;
    p.footer = hint;
    p.focus = focus;
    p.render(items);
}

void TeleportPanel::draw(TextPanel& p, bool waitingForLevel) const {
    p.clear(kBackground, kBorder);
    p.text(8, 6, "TELEPORT (DEBUG)", kGold);
    if (waitingForLevel) { p.text(8, 30, "CHANGING LEVEL...", kWhite); return; }
    const bool blink = (SDL_GetTicks() / 400) % 2;
    p.text(8, 22, "LEVEL: " + field[0] + (focus == 0 && blink ? "_" : ""), focus == 0 ? kWhite : kGrey);
    p.text(8, 34, "ROOM:  " + field[1] + (focus == 1 && blink ? "_" : ""), focus == 1 ? kWhite : kGrey);
    p.text(8, 50, message.empty() ? "ENTER=GO TAB=FIELD" : message, message.empty() ? kGrey : kGold);
    p.text(8, 60, "ESC/F10=CLOSE", kGrey);
}

} // namespace lm
