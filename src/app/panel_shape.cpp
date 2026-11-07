#include "app/panel_shape.h"
#include <deque>

namespace lm {

void PanelShape::update(const amiga::Machine& m, dm::game::LiveView& live, const dm::game::PanelText& text) {
    if (text.panelDraws().empty()) return;
    if (known_ && generation_ == text.panelGeneration() && portraits_ == text.targetPortraits().size()) return;
    generation_ = text.panelGeneration();
    portraits_ = text.targetPortraits().size();

    dm::IndexedFrame f;
    f.width = 320; f.height = 200;
    f.px.assign(size_t(320) * 200, 0);
    for (const auto& d : text.panelDraws()) live.renderDescriptor(m, d.x, d.y, d.descriptor, f);
    for (int y = kTop; y < kTop + kRows; ++y)
        for (int x = 0; x < 320; ++x) idx_[at(x, y)] = f.px[size_t(y) * 320 + size_t(x)];

    // energy bars: holes of the artwork not connected to the outside (4-connected flood fill from the edges)
    std::vector<uint8_t> outside(idx_.size(), 0);
    std::deque<std::pair<int, int>> q;
    auto seed = [&](int x, int y) {
        if (!idx_[at(x, y)] && !outside[at(x, y)]) { outside[at(x, y)] = 1; q.emplace_back(x, y); }
    };
    for (int x = 0; x < 320; ++x) { seed(x, kTop); seed(x, kTop + kRows - 1); }
    for (int y = kTop; y < kTop + kRows; ++y) { seed(0, y); seed(319, y); }
    while (!q.empty()) {
        const auto [x, y] = q.front(); q.pop_front();
        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : nb) {
            const int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || nx >= 320 || ny < kTop || ny >= kTop + kRows) continue;
            seed(nx, ny);
        }
    }
    for (size_t i = 0; i < idx_.size(); ++i) changing_[i] = !idx_[i] && !outside[i];

    // target portraits
    f.px.assign(size_t(320) * 200, 0);
    for (const auto& d : text.targetPortraits()) live.renderDescriptor(m, d.x, d.y, d.descriptor, f);
    for (int y = kTop; y < kTop + kRows; ++y)
        for (int x = 0; x < 320; ++x)
            if (f.px[size_t(y) * 320 + size_t(x)]) changing_[at(x, y)] = 1;
    known_ = true;
}

} // namespace lm
