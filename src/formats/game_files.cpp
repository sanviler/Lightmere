#include "formats/game_files.h"
#include "formats/game_manifest.h"
#include "platform/sha256.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace dm {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// files every complete installation contains (one per disk)
const char* const kRequired[] = {"main.bin", "lev11000.epf", "lev22000.epf", "lev33000.epf"};

// AmigaDOS disk image: 512-byte blocks, root block in the middle of the disk
class AdfReader {
public:
    explicit AdfReader(Bytes img) : img_(std::move(img)) {}
    bool valid() const { return img_.size() >= 512 * 1760 && img_[0] == 'D' && img_[1] == 'O' && img_[2] == 'S' && img_[3] <= 7; }
    bool ffs() const { return img_[3] & 1; }
    uint32_t root() const { return uint32_t(img_.size() / 512 / 2); }

    void list(uint32_t dirBlock, std::map<std::string, Bytes>& out, int depth = 0) const {
        if (depth > 8) return;
        for (int i = 6; i < 6 + 72; ++i) {
            uint32_t h = L(dirBlock, i);
            for (int guard = 0; h && guard < 4096; ++guard) {
                if (!inRange(h)) break;
                const int32_t type = int32_t(L(h, 127));
                if (type == 2) list(h, out, depth + 1);                  // directory
                else if (type == -3) out[name(h)] = file(h);              // file
                h = L(h, 124);                                            // next in hash chain
            }
        }
    }

private:
    Bytes img_;
    bool inRange(uint32_t b) const { return size_t(b + 1) * 512 <= img_.size(); }
    uint32_t L(uint32_t block, int index) const { return be32(&img_[size_t(block) * 512 + size_t(index) * 4]); }
    std::string name(uint32_t block) const {
        const uint8_t* p = &img_[size_t(block) * 512 + 432];
        return std::string(reinterpret_cast<const char*>(p + 1), std::min<size_t>(p[0], 30));
    }
    Bytes file(uint32_t header) const {
        const uint32_t size = L(header, 81);
        std::vector<uint32_t> blocks;
        for (uint32_t cur = header; cur && inRange(cur) && blocks.size() < 4096;) {
            const uint32_t n = std::min<uint32_t>(L(cur, 2), 72);
            for (uint32_t k = 0; k < n; ++k) blocks.push_back(L(cur, 77 - int(k)));   // table is filled backwards
            cur = L(cur, 126);                                                         // extension block
        }
        Bytes out;
        out.reserve(size);
        for (uint32_t b : blocks) {
            if (!inRange(b)) break;
            const uint8_t* p = &img_[size_t(b) * 512];
            if (ffs()) out.insert(out.end(), p, p + 512);
            else out.insert(out.end(), p + 24, p + 24 + std::min<uint32_t>(be32(p + 12), 488));   // OFS data block
        }
        if (out.size() > size) out.resize(size);
        return out;
    }
};
} // namespace

GameFiles& GameFiles::instance() {
    static GameFiles g;
    return g;
}

void GameFiles::clear() { files_.clear(); description_.clear(); }

bool GameFiles::mountAdf(const std::string& path, std::string& err) {
    Bytes img;
    try { img = readFile(path); } catch (const std::exception& e) { err = e.what(); return false; }
    AdfReader adf(std::move(img));
    if (!adf.valid()) { err = path + ": not an AmigaDOS disk image"; return false; }
    std::map<std::string, Bytes> found;
    adf.list(adf.root(), found);
    for (auto& [n, d] : found) {
        Entry e;
        e.path = path + ":" + n;
        e.data = std::move(d);
        e.inMemory = true;
        files_[lower(n)] = std::move(e);
    }
    return true;
}

bool GameFiles::mountDirectory(const std::string& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) files_[lower(e.path().filename().string())] = Entry{e.path().string(), {}, false};
    return true;
}

bool GameFiles::has(const std::string& name) const { return files_.count(lower(name)) > 0; }

Bytes GameFiles::read(const std::string& name) const {
    std::string n = name;
    if (const auto colon = n.find(':'); colon != std::string::npos) n = n.substr(colon + 1);   // "DF0:..."
    while (!n.empty() && (n[0] == '/' || n[0] == '\\')) n.erase(0, 1);
    auto it = files_.find(lower(n));
    if (it == files_.end()) throw FormatError("game file not found: " + name);
    return it->second.inMemory ? it->second.data : readFile(it->second.path);
}

std::vector<std::string> GameFiles::names() const {
    std::vector<std::string> v;
    for (const auto& [k, e] : files_) v.push_back(k);
    return v;
}

bool GameFiles::verify(std::string& problem) const {
    std::vector<std::string> missing, wrong;
    for (const ManifestEntry& m : kManifest) {
        if (!has(m.name)) { missing.emplace_back(m.name); continue; }
        Bytes d;
        try { d = read(m.name); } catch (const std::exception&) { missing.emplace_back(m.name); continue; }
        const std::string h = sha256Hex(d.data(), d.size());
        if (h != m.sha256a && !(m.sha256b && h == m.sha256b)) wrong.emplace_back(m.name);
    }
    if (missing.empty() && wrong.empty()) return true;
    auto list = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size() && i < 5; ++i) s += (i ? ", " : "") + upper(v[i]);
        if (v.size() > 5) s += ", ...";
        return s;
    };
    problem.clear();
    if (!wrong.empty()) problem += std::to_string(wrong.size()) + " file(s) differ from the known version (" + list(wrong) + ").\n";
    if (!missing.empty()) problem += std::to_string(missing.size()) + " file(s) are missing (" + list(missing) + ").\n";
    return false;
}

bool GameFiles::open(const std::string& folder, std::string& message) {
    auto complete = [&] { for (const char* r : kRequired) if (!has(r)) return false; return true; };
    std::string problem;
    auto check = [&](const std::string& what) {
        if (verify(problem)) { description_ = what; return true; }
        message = "Darkmere game files were found (" + what + "), but they are not the expected version of the game.\n\n" +
                  problem + "\nLightmere needs the four-disk version of Darkmere (1994), or its WHDLoad installation.";
        return false;
    };
    // 1. the four ADF images (any file name), identified by their contents
    {
        clear();
        std::error_code ec;
        std::vector<fs::path> adfs;
        for (const auto& e : fs::directory_iterator(folder, ec))
            if (e.is_regular_file(ec) && lower(e.path().extension().string()) == ".adf") adfs.push_back(e.path());
        std::sort(adfs.begin(), adfs.end());
        int mounted = 0;
        for (const auto& p : adfs) { std::string err; if (mountAdf(p.string(), err)) ++mounted; }
        if (mounted && complete()) return check(std::to_string(mounted) + " ADF images in " + folder);
        if (mounted) {
            clear();
            message = "Some Darkmere disk images were found in " + folder + ", but not all four.\n\n"
                      "Copy all four ADF disk images of Darkmere into that folder.";
            return false;
        }
    }
    // 2. WHDLoad installation: the "data" folder, the install folder itself, or the files copied loose
    for (const char* rel : {".", "data", "Darkmere/data"}) {
        clear();
        const fs::path d = fs::path(folder) / rel;
        if (mountDirectory(d.string()) && complete()) return check("WHDLoad files in " + d.lexically_normal().string());
    }
    clear();
    message = "The Darkmere game files were not found in " + folder + ".\n\n"
              "Copy the four ADF disk images of Darkmere (any file names, *.adf) into that folder, or choose the "
              "folder where they are. The \"data\" folder of the WHDLoad installation of Darkmere works too.\n\n"
              "Lightmere does not include any part of the original game: you must provide your own copy.";
    return false;
}

bool GameFiles::locate(const std::vector<std::string>& folders, std::string& message) {
    std::string first;
    for (const auto& f : folders) {
        if (open(f, message)) return true;
        if (first.empty()) first = message;
    }
    message = first;
    return false;
}

} // namespace dm
