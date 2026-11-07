#include "formats/text.h"

namespace dm {

namespace {

// CP437 $80-$FF -> Unicode code point
const char16_t kCp437High[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0};

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

std::string fixWide(std::string s) {
    static const std::pair<const char*, const char*> kWide[] = {{"%&", "m"}, {"#$", "w"}, {"^_", "M"}, {"\\]", "W"}};
    for (auto [a, b] : kWide)
        for (size_t p; (p = s.find(a)) != std::string::npos;) s.replace(p, 2, b);
    return s;
}

} // namespace

std::string cp437ToUtf8(const std::string& s) {
    std::string out;
    for (unsigned char c : s) appendUtf8(out, c < 0x80 ? char32_t(c) : char32_t(kCp437High[c - 0x80]));
    return out;
}

std::vector<std::string> decodeMessages(const Bytes& stream, const Bytes& dict) {
    std::vector<std::string> words;
    {
        std::string cur;
        for (uint8_t c : dict) {
            if (c) cur += char(c);
            else { words.push_back(cp437ToUtf8(fixWide(cur))); cur.clear(); }
        }
        if (!cur.empty()) words.push_back(cp437ToUtf8(fixWide(cur)));
    }
    std::vector<std::string> msgs;
    std::string cur;
    for (size_t k = 2; k + 1 < stream.size(); k += 2) {
        uint16_t w = be16(&stream[k]);
        if (w & 0x8000) {
            uint8_t ch = uint8_t(w & 0xFF);
            if (ch) cur += cp437ToUtf8(std::string(1, char(ch)));
            if (w & 0x4000) { msgs.push_back(cur); cur.clear(); }
        } else {
            const uint16_t i = w & 0x3FFF;
            cur += i < words.size() ? words[i] : "<?>";
            if (w & 0x4000) { msgs.push_back(cur); cur.clear(); } // word + end of message
        }
    }
    if (!cur.empty()) msgs.push_back(cur);
    return msgs;
}

std::map<int, std::string> roomNames(const Bytes& stream, const Bytes& dict, const Bytes& index) {
    const auto msgs = decodeMessages(stream, dict);
    std::map<size_t, size_t> startToMsg;                 // position (in words) -> message index
    size_t m = 0;
    startToMsg[1] = m++;
    for (size_t k = 1; k + 1 < stream.size() / 2 + 1 && k * 2 + 1 < stream.size(); ++k)
        if (be16(&stream[k * 2]) & 0x4000) startToMsg[k + 1] = m++;
    std::map<int, std::string> best, cur;
    size_t p = 0;
    while (p + 4 <= index.size()) {
        if (index[p] == 0xFF) {                          // end of list
            if (cur.size() > best.size()) best = cur;
            cur.clear();
            ++p;
            continue;
        }
        const uint8_t type = index[p], id = index[p + 1];
        const uint16_t pos = be16(&index[p + 2]);
        size_t q = p + 4;
        while (q < index.size() && index[q] != 0xFF) ++q;
        if (type == 1) {
            auto it = startToMsg.find(pos);
            if (it != startToMsg.end() && it->second < msgs.size()) cur[id] = msgs[it->second];
        }
        p = q + 1;
    }
    if (cur.size() > best.size()) best = cur;
    return best;
}

} // namespace dm
