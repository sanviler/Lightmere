#pragma once
// The original game files, read either from the four ADF disk images or from the "data" folder of the
// WHDLoad installation. Both contain the same files (verified byte by byte: only MAIN.BIN differs, by the
// copy-protection bypass of cracked disk sets, which the loader applies anyway).
// Lookups ignore case, as AmigaDOS does.
#include "formats/common.h"
#include <map>
#include <string>
#include <vector>

namespace dm {

class GameFiles {
public:
    static GameFiles& instance();

    void clear();
    // every regular file of an AmigaDOS (OFS or FFS) disk image; false if the image is not readable
    bool mountAdf(const std::string& path, std::string& err);
    // every file of a folder (not recursive)
    bool mountDirectory(const std::string& dir);

    bool has(const std::string& name) const;
    Bytes read(const std::string& name) const;            // throws FormatError if missing
    std::vector<std::string> names() const;
    const std::string& description() const { return description_; }

    // Opens the game in a folder: the four ADF images (any names) first, then a WHDLoad installation (the folder
    // itself, its "data" folder or "Darkmere/data"). The files are then checked against the SHA-256 fingerprints
    // of the known version (game_manifest.h). Returns false and a message for the user if they are missing or
    // not the right version.
    bool open(const std::string& folder, std::string& message);
    // the same, trying several folders in turn (the message is the one of the first folder)
    bool locate(const std::vector<std::string>& folders, std::string& message);
    // checks every file against the fingerprints; false and a description of the differences otherwise
    bool verify(std::string& problem) const;

private:
    struct Entry { std::string path; Bytes data; bool inMemory = false; };
    std::map<std::string, Entry> files_;                   // key: lower-case name
    std::string description_;
};

inline Bytes readGameFile(const std::string& name) { return GameFiles::instance().read(name); }

} // namespace dm
