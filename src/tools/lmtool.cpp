// lmtool: checks of the Darkmere file formats (developer tool, no external dependencies).
//   lmtool verify <data folder>               unpacks MAIN.BIN and every .epf entry, checks the CRCs
//   lmtool unpack <RNC in> <out>              unpacks an RNC1 file
//   lmtool epf    <archive.epf> <folder>      extracts every entry
//   lmtool text   <txtNNNNN.epf> <0..3>       prints the messages (0=EN 1=FR 2=DE 3=IT)
//   lmtool ilbm   <ILBM entry> <out.ppm>      converts an IFF picture (HAM6 too) to PPM
#include "formats/epf.h"
#include "formats/ilbm.h"
#include "formats/rnc1.h"
#include "formats/text.h"

#include <cstdio>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace dm;

static int verify(const fs::path& dir) {
    int files = 0, entries = 0, empty = 0, errors = 0;
    try {
        Bytes main = rnc::unpack(readFile((dir / "MAIN.BIN").string()));
        std::printf("MAIN.BIN: %zu bytes unpacked, CRC ok\n", main.size());
    } catch (const std::exception& e) {
        std::printf("MAIN.BIN: ERROR %s\n", e.what()); ++errors;
    }
    for (const auto& de : fs::directory_iterator(dir)) {
        if (de.path().extension() != ".epf") continue;
        ++files;
        try {
            EpfArchive a = EpfArchive::open(de.path().string());
            for (size_t i = 0; i < a.count(); ++i) {
                if (a.isEmpty(i)) { ++empty; continue; }
                a.entry(i);
                ++entries;
            }
        } catch (const std::exception& e) {
            std::printf("%s: ERROR %s\n", de.path().filename().string().c_str(), e.what());
            ++errors;
        }
    }
    std::printf("archives %d, entries %d, empty slots %d, errors %d\n", files, entries, empty, errors);
    return errors ? 1 : 0;
}

static void writePpm(const fs::path& out, int w, int h, const std::vector<Rgb>& px) {
    FILE* f = std::fopen(out.string().c_str(), "wb");
    if (!f) throw FormatError("cannot write " + out.string());
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(px.data(), 3, px.size(), f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::puts("usage: lmtool verify|unpack|epf|text|ilbm ...");
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "verify") return verify(argv[2]);
        if (cmd == "unpack" && argc >= 4) {
            writeFile(argv[3], rnc::unpack(readFile(argv[2])));
            return 0;
        }
        if (cmd == "epf" && argc >= 4) {
            EpfArchive a = EpfArchive::open(argv[2]);
            fs::create_directories(argv[3]);
            const std::string base = fs::path(argv[2]).stem().string();
            for (size_t i = 0; i < a.count(); ++i) {
                char name[64];
                std::snprintf(name, sizeof name, "%s_%03zu.bin", base.c_str(), i);
                writeFile((fs::path(argv[3]) / name).string(), a.entry(i));
            }
            std::printf("%zu entries extracted\n", a.count());
            return 0;
        }
        if (cmd == "text" && argc >= 4) {
            EpfArchive a = EpfArchive::open(argv[2]);
            int lang = std::atoi(argv[3]);
            auto msgs = decodeMessages(a.entry(size_t(lang) * 3), a.entry(size_t(lang) * 3 + 1));
            for (size_t i = 0; i < msgs.size(); ++i) std::printf("[%04zu] %s\n", i, msgs[i].c_str());
            return 0;
        }
        if (cmd == "ilbm" && argc >= 4) {
            IlbmImage img = decodeIlbm(readFile(argv[2]));
            writePpm(argv[3], img.width, img.height, img.toRgb());
            std::printf("%dx%d, %d planes%s\n", img.width, img.height, img.planes, img.ham ? ", HAM" : "");
            return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::puts("unknown command");
    return 2;
}
