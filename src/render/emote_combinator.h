// Finite meshType=1 meshCombinator core; no scene/ancestor policy or GL.
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace artc {
struct PsbDocument;
struct PsbValue;

// Row-major 4x4, interleaved xy. Coordinates are finite but not clamped.
using EmoteCombinatorGrid = std::array<float, 32>;

// Per-combinator safety limits, not official format limits. Repeated resource
// references each count in full toward the decoded budget. Entire lists are
// preflighted before any resource copy or decoded mesh allocation.
constexpr size_t kEmoteCombinatorMaxEntries = 4096;
constexpr size_t kEmoteCombinatorMaxEntryBytes = 8 * 1024 * 1024;
constexpr size_t kEmoteCombinatorMaxDecodedBytes = 64 * 1024 * 1024;

struct EmoteCombinatorEntry {
    int mesh_type = 1;
    int neutral_index = -1; // Never subtracted; used for controller-state neutral reporting.
    std::string variable_key;
    double range_begin = 0;
    double range_end = 1;
    std::vector<EmoteCombinatorGrid> meshes;
};
struct EmoteMeshCombinator {
    // First entry contains absolute grids; all subsequent entries contain deltas.
    std::vector<EmoteCombinatorEntry> entries;
};

// Allocation-free resource/list preflight, also used by scene Load to measure
// ALL unique nodes before decoding any of them. Metadata bytes include entry
// storage and variable-key bytes (decoded grid bytes are reported separately).
bool MeasureEmoteCombinator(const PsbDocument &document, const PsbValue &field,
                           size_t &decoded_bytes, size_t &metadata_bytes,
                           std::string &error);

// `field` is node.At("meshCombinator"), not the node. rawMeshList must be a
// normal/extra PSB resource containing meshCount * 128 float32LE bytes.
// Empty lists, invalid counts/types/neutral indices, nonfinite values and
// degenerate ranges are rejected by this core's safety policy, not claimed to
// reproduce official malformed-input behavior. Budgets above apply to both
// parsed and typed evaluation. bad_alloc/length_error become false + error.
// Output is unchanged on failure.
bool ParseEmoteCombinator(const PsbDocument &document, const PsbValue &field,
                         EmoteMeshCombinator &out, std::string &error);

// t=clamp((value-begin)/(end-begin),0,1), q=t*(count-1); interpolate adjacent
// meshes and SUM samples componentwise. Missing variables are zero. No identity
// addition, neutral subtraction, state accumulation or ancestor lookup.
bool EvaluateEmoteCombinator(const EmoteMeshCombinator &combinator,
                             const std::map<std::string, double> &variables,
                             EmoteCombinatorGrid &out, std::string &error);
// Same numeric grid result, with transactional controller-state neutral output.
// All entries must sample their neutral integer index (floor of the position)
// with fractional part strictly below 2^-23.
// Unknown (-1) neutral indices never count as neutral; grid==identity is NOT a
// substitute. Scene may revoke the effective patch while retaining ownership.
bool EvaluateEmoteCombinator(const EmoteMeshCombinator &combinator,
                             const std::map<std::string, double> &variables,
                             EmoteCombinatorGrid &out, std::string &error,
                             bool &all_neutral);
bool EvaluateEmoteCombinator(const PsbDocument &document, const PsbValue &field,
                            const std::map<std::string, double> &variables,
                            EmoteCombinatorGrid &out, std::string &error);

// Only for two grids already known by the caller to share the same domain:
// combined = child + parent - identity. Does not decide eligible ancestors,
// coordinate domains, sampling scope or scene meshCombine behavior.
bool CombineSameDomainEmoteGrids(const EmoteCombinatorGrid &child,
                                const EmoteCombinatorGrid &parent,
                                EmoteCombinatorGrid &out, std::string &error);
} // namespace artc
