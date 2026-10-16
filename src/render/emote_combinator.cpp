#include "render/emote_combinator.h"
#include "pack/psb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace artc {
namespace {
bool Fail(std::string &error, const std::string &message) {
    error = "meshCombinator: " + message;
    return false;
}

bool FiniteNumber(const PsbValue &value) {
    return value.type == PsbValue::Number && std::isfinite(value.number);
}

bool AddBudget(size_t count, size_t &total, std::string &error) {
    constexpr size_t bytes_per_mesh = 128;
    if (count > kEmoteCombinatorMaxEntryBytes / bytes_per_mesh)
        return Fail(error, "entry decoded byte budget exceeded");
    const size_t bytes = count * bytes_per_mesh;
    if (bytes > kEmoteCombinatorMaxDecodedBytes - total)
        return Fail(error, "cumulative decoded byte budget exceeded");
    total += bytes;
    return true;
}

bool PreflightEntry(const PsbDocument &document, const PsbValue &item,
                    size_t &total, std::string &error) {
    const auto &type = item.At("meshType");
    const auto &neutral = item.At("neutralIndex");
    const auto &variable = item.At("variable");
    const auto &key = variable.At("key");
    const auto &count = variable.At("meshCount");
    const auto &begin = variable.At("rangeBegin");
    const auto &end = variable.At("rangeEnd");
    if (item.type != PsbValue::Object || !FiniteNumber(type) || type.number != 1)
        return Fail(error, "unsupported meshType (expected 1)");
    const auto &entry_type = item.At("entryType");
    if (entry_type.type != PsbValue::Null &&
        (!FiniteNumber(entry_type) || entry_type.number != 0))
        return Fail(error, "unsupported entryType (only absent/default 0)");
    if (variable.type != PsbValue::Object || key.type != PsbValue::String ||
        !FiniteNumber(count) || count.number < 1 || std::floor(count.number) != count.number ||
        count.number > std::numeric_limits<int>::max())
        return Fail(error, "invalid variable key or meshCount");
    if (!FiniteNumber(neutral) || std::floor(neutral.number) != neutral.number ||
        neutral.number < -1 || neutral.number >= count.number)
        return Fail(error, "neutralIndex outside [-1, meshCount-1]");
    if (!FiniteNumber(begin) || !FiniteNumber(end) || begin.number == end.number)
        return Fail(error, "nonfinite or degenerate variable range");
    const size_t mesh_count = static_cast<size_t>(count.number);
    if (!AddBudget(mesh_count, total, error)) return false;
    const auto &ref = item.At("rawMeshList");
    const auto &bank = ref.extra ? document.extra_resources : document.resources;
    if (ref.type != PsbValue::Resource || ref.resource >= bank.size())
        return Fail(error, "invalid rawMeshList resource");
    const auto range = bank[ref.resource];
    // Read-only preflight of the public table uses the same overflow-safe
    // bounds check as ReadResource. Actual bytes still only come from that
    // accessor, which revalidates the range before copying.
    if (range.first > document.bytes.size() || range.second > document.bytes.size() - range.first)
        return Fail(error, "invalid rawMeshList resource bounds");
    if (range.second != mesh_count * 128)
        return Fail(error, "rawMeshList length does not match meshCount * 128");
    return true;
}

bool FiniteGrid(const EmoteCombinatorGrid &grid) {
    return std::all_of(grid.begin(), grid.end(), [](float v) { return std::isfinite(v); });
}

bool ValidEntry(const EmoteCombinatorEntry &entry, std::string &error) {
    if (entry.mesh_type != 1) return Fail(error, "unsupported meshType (expected 1)");
    if (entry.meshes.empty() || entry.meshes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return Fail(error, "invalid meshCount");
    if (entry.neutral_index < -1 || entry.neutral_index >= static_cast<int>(entry.meshes.size()))
        return Fail(error, "neutralIndex outside [-1, meshCount-1]");
    if (!std::isfinite(entry.range_begin) || !std::isfinite(entry.range_end) ||
        entry.range_begin == entry.range_end)
        return Fail(error, "nonfinite or degenerate variable range");
    for (const auto &mesh : entry.meshes)
        if (!FiniteGrid(mesh)) return Fail(error, "nonfinite control point");
    return true;
}

bool StoreGrid(const std::array<double, 32> &value, EmoteCombinatorGrid &out,
               std::string &error) {
    EmoteCombinatorGrid next;
    for (size_t i = 0; i < next.size(); ++i) {
        if (!std::isfinite(value[i]) || std::fabs(value[i]) > std::numeric_limits<float>::max())
            return Fail(error, "nonfinite or overflowing result");
        next[i] = static_cast<float>(value[i]);
    }
    out = next;
    error.clear();
    return true;
}
} // namespace

bool MeasureEmoteCombinator(const PsbDocument &document, const PsbValue &field,
                           size_t &decoded_bytes, size_t &metadata_bytes,
                           std::string &error) try {
    const auto &entry_type = field.At("entryType");
    if (entry_type.type != PsbValue::Null &&
        (!FiniteNumber(entry_type) || entry_type.number != 0))
        return Fail(error, "unsupported entryType (only absent/default 0)");
    const auto &list = field.At("combinatorList");
    if (field.type != PsbValue::Object || list.type != PsbValue::Array || list.array.empty())
        return Fail(error, "expected nonempty combinatorList");
    if (list.array.size() > kEmoteCombinatorMaxEntries)
        return Fail(error, "entry count budget exceeded");
    size_t decoded = 0, metadata = list.array.size() * sizeof(EmoteCombinatorEntry);
    for (const auto &item : list.array) {
        if (!PreflightEntry(document, item, decoded, error)) return false;
        const size_t length = item.At("variable").At("key").string.size();
        if (length >= kEmoteCombinatorMaxDecodedBytes ||
            metadata > kEmoteCombinatorMaxDecodedBytes - length - 1)
            return Fail(error, "metadata byte budget exceeded");
        metadata += length + 1;
    }
    decoded_bytes = decoded; metadata_bytes = metadata; error.clear(); return true;
} catch (const std::exception &e) {
    error = e.what(); return false;
}

bool ParseEmoteCombinator(const PsbDocument &document, const PsbValue &field,
                         EmoteMeshCombinator &out, std::string &error) try {
    const auto &list = field.At("combinatorList");
    if (field.type != PsbValue::Object || list.type != PsbValue::Array || list.array.empty())
        return Fail(error, "expected nonempty combinatorList");
    if (list.array.size() > kEmoteCombinatorMaxEntries)
        return Fail(error, "entry count budget exceeded");
    size_t total = 0, metadata = 0;
    if (!MeasureEmoteCombinator(document, field, total, metadata, error)) return false;
    EmoteMeshCombinator next;
    next.entries.reserve(list.array.size());
    for (const auto &item : list.array) {
        const auto &neutral = item.At("neutralIndex");
        const auto &variable = item.At("variable");
        const auto &key = variable.At("key");
        const auto &count = variable.At("meshCount");
        const auto &begin = variable.At("rangeBegin");
        const auto &end = variable.At("rangeEnd");
        std::vector<uint8_t> raw;
        if (!document.ReadResource(item.At("rawMeshList"), raw))
            return Fail(error, "invalid rawMeshList resource");
        if (raw.size() % 128 != 0 || static_cast<double>(raw.size() / 128) != count.number)
            return Fail(error, "rawMeshList length does not match meshCount * 128");
        EmoteCombinatorEntry entry;
        entry.neutral_index = static_cast<int>(neutral.number);
        entry.variable_key = key.string;
        entry.range_begin = begin.number;
        entry.range_end = end.number;
        entry.meshes.resize(raw.size() / 128);
        size_t offset = 0;
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "meshCombinator requires IEEE float32");
        for (auto &mesh : entry.meshes) {
            for (float &value : mesh) {
                const uint32_t bits = uint32_t(raw[offset]) | (uint32_t(raw[offset + 1]) << 8) |
                    (uint32_t(raw[offset + 2]) << 16) | (uint32_t(raw[offset + 3]) << 24);
                std::memcpy(&value, &bits, sizeof(value));
                offset += 4;
            }
        }
        if (!ValidEntry(entry, error)) return false;
        next.entries.push_back(std::move(entry));
    }
    out = std::move(next);
    error.clear();
    return true;
} catch (const std::bad_alloc &) {
    error = "bad_alloc";
    return false;
} catch (const std::length_error &) {
    error = "length_error";
    return false;
}

bool EvaluateEmoteCombinator(const EmoteMeshCombinator &combinator,
                             const std::map<std::string, double> &variables,
                             EmoteCombinatorGrid &out, std::string &error) {
    bool all_neutral=false;
    return EvaluateEmoteCombinator(combinator, variables, out, error, all_neutral);
}

bool EvaluateEmoteCombinator(const EmoteMeshCombinator &combinator,
                             const std::map<std::string, double> &variables,
                             EmoteCombinatorGrid &out, std::string &error,
                             bool &all_neutral) try {
    if (combinator.entries.empty()) return Fail(error, "expected nonempty combinatorList");
    if (combinator.entries.size() > kEmoteCombinatorMaxEntries)
        return Fail(error, "entry count budget exceeded");
    size_t total = 0;
    for (const auto &entry : combinator.entries)
        if (!AddBudget(entry.meshes.size(), total, error)) return false;
    std::array<double, 32> sum{};
    bool neutral = true;
    for (const auto &entry : combinator.entries) {
        if (!ValidEntry(entry, error)) return false;
        const auto found = variables.find(entry.variable_key);
        const double value = found == variables.end() ? 0 : found->second;
        if (!std::isfinite(value)) return Fail(error, "nonfinite variable value");
        double t = 0;
        if ((entry.range_end > entry.range_begin && value <= entry.range_begin) ||
            (entry.range_end < entry.range_begin && value >= entry.range_begin)) {
            t = 0;
        } else if ((entry.range_end > entry.range_begin && value >= entry.range_end) ||
                   (entry.range_end < entry.range_begin && value <= entry.range_end)) {
            t = 1;
        } else {
            const double width = entry.range_end - entry.range_begin;
            if (std::isfinite(width)) {
                t = (value - entry.range_begin) / width;
            } else {
                // An interior value in a range crossing the finite double
                // extremes: scale before subtraction to avoid overflow.
                const double scale = std::max(std::fabs(entry.range_begin), std::fabs(entry.range_end));
                t = (value / scale - entry.range_begin / scale) /
                    (entry.range_end / scale - entry.range_begin / scale);
            }
            t = std::clamp(t, 0.0, 1.0);
        }
        if (!std::isfinite(t)) return Fail(error, "unrepresentable variable range mapping");
        const double q = t * static_cast<double>(entry.meshes.size() - 1);
        const size_t lo = static_cast<size_t>(std::floor(q));
        const size_t hi = std::min(lo + 1, entry.meshes.size() - 1);
        const double fraction = q - static_cast<double>(lo);
        neutral = neutral && entry.neutral_index >= 0 &&
            lo == static_cast<size_t>(entry.neutral_index) && fraction < 0x1p-23;
        for (size_t i = 0; i < sum.size(); ++i)
            sum[i] += (1 - fraction) * entry.meshes[lo][i] + fraction * entry.meshes[hi][i];
    }
    if (!StoreGrid(sum, out, error)) return false;
    all_neutral = neutral;
    return true;
} catch (const std::bad_alloc &) {
    error = "bad_alloc";
    return false;
} catch (const std::length_error &) {
    error = "length_error";
    return false;
}

bool EvaluateEmoteCombinator(const PsbDocument &document, const PsbValue &field,
                            const std::map<std::string, double> &variables,
                            EmoteCombinatorGrid &out, std::string &error) try {
    EmoteMeshCombinator parsed;
    return ParseEmoteCombinator(document, field, parsed, error) &&
           EvaluateEmoteCombinator(parsed, variables, out, error);
} catch (const std::bad_alloc &) {
    error = "bad_alloc";
    return false;
} catch (const std::length_error &) {
    error = "length_error";
    return false;
}

bool CombineSameDomainEmoteGrids(const EmoteCombinatorGrid &child,
                                const EmoteCombinatorGrid &parent,
                                EmoteCombinatorGrid &out, std::string &error) try {
    if (!FiniteGrid(child) || !FiniteGrid(parent)) return Fail(error, "nonfinite control point");
    std::array<double, 32> sum;
    for (size_t i = 0; i < sum.size(); ++i) {
        const double identity = static_cast<double>((i / 2) % 4) / 3;
        const double identity_y = static_cast<double>((i / 2) / 4) / 3;
        sum[i] = static_cast<double>(child[i]) + parent[i] - (i % 2 ? identity_y : identity);
    }
    return StoreGrid(sum, out, error);
} catch (const std::bad_alloc &) {
    error = "bad_alloc";
    return false;
} catch (const std::length_error &) {
    error = "length_error";
    return false;
}
} // namespace artc
