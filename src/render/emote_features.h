// Read-only record of E-mote features a loaded model uses but this engine does
// not implement. Such features degrade (the channel/deformation is NOT applied,
// never approximated) and are reported instead of rejecting the whole model.
#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace artc {
class EmoteUnsupportedFeatures {
public:
    // Counts are per distinct document item (node/control), not per frame.
    void Add(const std::string& tag, size_t count = 1) { if (count) counts_[tag] += count; }
    void Merge(const EmoteUnsupportedFeatures& other) {
        for (const auto& item : other.counts_) counts_[item.first] += item.second;
    }
    bool Empty() const { return counts_.empty(); }
    bool Has(const std::string& tag) const { return counts_.count(tag) != 0; }
    size_t Count(const std::string& tag) const {
        const auto it = counts_.find(tag);
        return it == counts_.end() ? 0 : it->second;
    }
    const std::map<std::string, size_t>& Counts() const { return counts_; }
private:
    std::map<std::string, size_t> counts_;
};

// One INFO line per tag (call once per successful Load, never per frame).
void LogEmoteUnsupportedFeatures(const char* owner, const EmoteUnsupportedFeatures& features);
} // namespace artc
