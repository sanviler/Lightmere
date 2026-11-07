#include "formats/epf.h"
#include "formats/rnc1.h"

namespace dm {

EpfArchive::EpfArchive(Bytes data) : data_(std::move(data)) {
    if (data_.size() < 8 || data_[0] != 'E' || data_[1] != 'P' || data_[2] != 'F')
        throw FormatError("EPF: signature missing");
    const size_t n = data_[3];
    if (be32(&data_[4]) != data_.size()) throw FormatError("EPF: inconsistent size");
    if (8 + n * 8 > data_.size()) throw FormatError("EPF: truncated table");
    for (size_t i = 0; i < n; ++i) {
        Entry e{be32(&data_[8 + i * 8]), be32(&data_[12 + i * 8])};
        if (size_t(e.off) + e.len > data_.size()) throw FormatError("EPF: entry out of bounds");
        entries_.push_back(e);
    }
}

bool EpfArchive::isEmpty(size_t i) const {
    const auto& e = entries_.at(i);
    const uint8_t* p = data_.data() + e.off;
    return e.len <= 8 && e.len >= 4 && p[0] == 'R' && p[1] == 'N' && p[2] == 'C' && p[3] == 0;
}

Bytes EpfArchive::entry(size_t i) const {
    const auto& e = entries_.at(i);
    const uint8_t* p = data_.data() + e.off;
    if (isEmpty(i)) return {};
    if (rnc::isPacked(p, e.len)) return rnc::unpack(p, e.len);
    return Bytes(p, p + e.len);
}

} // namespace dm
