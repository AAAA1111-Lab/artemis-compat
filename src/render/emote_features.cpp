#include "render/emote_features.h"
#include "log/logger.h"

namespace artc {
void LogEmoteUnsupportedFeatures(const char* owner, const EmoteUnsupportedFeatures& features) {
    for (const auto& item : features.Counts())
        Log(kLogInfo, std::string("emote: unsupported feature ") + item.first + " x" +
                          std::to_string(item.second) + " (" + owner +
                          "; not applied, model still loads)");
}
} // namespace artc
