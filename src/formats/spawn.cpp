#include "formats/spawn.h"
#include <algorithm>

namespace dm {

uint8_t initialEntityFlags59(const Bytes& code, int kind) {
    const uint32_t st = be32(&code.at(0x1D9FC + size_t(kind) * 4));
    return st > 0 && st + 3 <= code.size() ? code[st + 2] : 0;
}

std::vector<int> spawnVisible(const std::vector<int>& kinds, const std::vector<uint8_t>& f59, int maxVisible, SpawnRng& rng) {
    const int d1 = int(kinds.size());
    std::vector<bool> vis(kinds.size());
    int d2 = 0, d7 = maxVisible;
    for (size_t i = 0; i < kinds.size(); ++i) if (f59[i] & 4) { vis[i] = true; ++d2; }
    auto result = [&] { std::vector<int> r; for (int i = 0; i < d1; ++i) if (vis[size_t(i)]) r.push_back(i); return r; };
    if (d2) {
        for (size_t i = 0; i < kinds.size(); ++i) if (kinds[i] == 0x1B || kinds[i] == 0x25) { vis[i] = true; ++d2; }
        if (d2 >= d7) return result();
        d7 = d1;
    } else {
        if (d7 == 0 || d1 == 0) return {};
        d7 = std::min(d7, d1);
    }
    for (int guard = 0; d2 != d7 && guard < 10000; ++guard) {
        if (std::all_of(vis.begin(), vis.end(), [](bool v) { return v; })) break;
        const int r = int((rng.next() & 0xFFFF) % uint32_t(d1));
        if (!vis[size_t(r)]) { vis[size_t(r)] = true; ++d2; }
    }
    return result();
}

int initialDirection(const RoomData& room, int x, int y) {
    const uint8_t* m = &room[0xE8];
    const int cx = x >> 5, cy = y >> 5;
    int d4 = cx;
    while (d4 >= 0 && (m[cy * 8 + d4] & 7) == 0) --d4;
    const int px = ((cx - d4) << 5) + (x & 31);
    int d5 = cy;
    while (d5 >= 0 && (m[d5 * 8 + cx] & 7) == 0) --d5;
    const int py = ((cy - d5) << 5) + (y & 31);
    return py <= px ? 2 : 1;
}

} // namespace dm
