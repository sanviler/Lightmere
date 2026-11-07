// lmtext: the game texts as readable files, for correcting a translation (developer tool).
//   lmtext export <game folder> <out folder> [lang]     writes one file per text archive (default lang 3 = IT,
//                                                       with the English texts as reference)
//   lmtext check  <game folder> <texts>                 reads edited files (a folder or a file), reports problems
//   lmtext diff   <game folder> <texts> <out file>      writes only the corrected messages, with checks of the
//                                                       originals: the corrections file shipped with Lightmere
//   lmtext selftest <game folder>                       encodes the original texts again: the bytes must match
// The game folder holds the four ADF images or the WHDLoad data, as for Lightmere.
#include "formats/epf.h"
#include "formats/game_files.h"
#include "formats/text_codec.h"
#include "formats/text_files.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace dm;

namespace {

bool openGame(const std::string& folder) {
    std::string message;
    if (!GameFiles::instance().open(folder, message)) {
        std::fprintf(stderr, "%s\n", message.c_str());
        return false;
    }
    return true;
}

bool save(const fs::path& path, const std::string& s) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << s;
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.string().c_str()); return false; }
    std::printf("%s\n", path.string().c_str());
    return true;
}

int report(const text::ApplyReport& r) {
    for (const std::string& w : r.warnings) std::printf("avviso: %s\n", w.c_str());
    for (const std::string& e : r.errors) std::printf("ERRORE: %s\n", e.c_str());
    std::printf("messaggi corretti: %d, avvisi: %zu, errori: %zu\n", r.changed, r.warnings.size(), r.errors.size());
    return r.errors.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "selftest") {
        if (!openGame(argv[2])) return 1;
        int bad = 0;
        for (const char* name : {"txt11000.epf", "txt22000.epf", "txt33000.epf", "txt44000.epf"}) {
            const EpfArchive a(GameFiles::instance().read(name));
            for (size_t l = 0; l < 4; ++l) {
                const Bytes stream = a.entry(l * 3), dict = a.entry(l * 3 + 1), index = a.entry(l * 3 + 2);
                const auto msgs = text::decode(stream, dict);
                Bytes s2, d2;
                text::encode(msgs, s2, d2);
                const Bytes i2 = text::remapIndex(index, stream, s2);
                // the same messages, one of them longer: every index entry must follow its message
                auto longer = msgs;
                longer[0] += " x";
                Bytes s3, d3;
                text::encode(longer, s3, d3);
                const Bytes i3 = text::remapIndex(index, stream, s3);
                const auto e1 = text::parseIndex(index, stream), e3 = text::parseIndex(i3, s3);
                bool moved = e1.size() == e3.size();
                for (size_t k = 0; moved && k < e1.size(); ++k) moved = e1[k].message == e3[k].message;
                const bool ok = s2 == stream && d2 == dict && i2 == index && moved && text::decode(s3, d3) == longer;
                std::printf("%s language %zu: %zu messages, %s\n", name, l, msgs.size(), ok ? "ok" : "DIFFERENT");
                bad += !ok;
            }
        }
        return bad ? 1 : 0;
    }
    if (argc < 4) {
        std::puts("usage: lmtext export <game folder> <out folder> [lang]\n"
                  "       lmtext check  <game folder> <texts>\n"
                  "       lmtext diff   <game folder> <texts> <out file>");
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        if (!openGame(argv[2])) return 1;
        GameFiles& gf = GameFiles::instance();
        if (cmd == "export") {
            const int lang = argc > 4 ? std::atoi(argv[4]) : 3;
            const int ref = lang == 0 ? -1 : 0;
            const fs::path out = argv[3];
            fs::create_directories(out);
            const char* lng[4] = {"en", "fr", "de", "it"};
            const std::string p = lng[lang & 3];
            struct { const char* archive; const char* file; } kFiles[] = {
                {"txt11000.epf", "1_livello1.txt"}, {"txt22000.epf", "2_livello2.txt"},
                {"txt33000.epf", "3_livello3.txt"}, {"txt44000.epf", "4_racconto_e_menu.txt"}};
            for (const auto& f : kFiles)
                if (!save(out / (p + "_" + f.file), text::exportArchive(gf, f.archive, lang, ref))) return 1;
            if (!text::programFields(lang).empty() && !save(out / (p + "_5_programma.txt"), text::exportProgram(gf, lang)))
                return 1;
            return 0;
        }
        std::vector<text::TextFile> files;
        std::string err;
        if (!text::loadTexts(argv[3], files, err)) { std::printf("ERRORE: %s\n", err.c_str()); return 1; }
        if (cmd == "check") {
            text::ApplyReport r = text::apply(files, gf);
            text::programPatches(files, gf, r);
            return report(r);
        }
        if (cmd == "diff" && argc >= 5) {
            text::ApplyReport check = text::apply(files, gf);
            text::programPatches(files, gf, check);
            if (!check.errors.empty()) return report(check);
            GameFiles::instance().clear();
            if (!openGame(argv[2])) return 1;               // the originals again
            text::ApplyReport r;
            const std::string d = text::differences(files, GameFiles::instance(), r);
            if (!save(argv[4], d)) return 1;
            r.warnings = check.warnings;
            return report(r);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 2;
}
