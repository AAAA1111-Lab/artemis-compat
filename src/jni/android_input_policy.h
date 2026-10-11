#pragma once

#include <android/input.h>

namespace artc {

// Physical system-volume buttons belong to Android, even when the script
// registers the legacy HUP/HDW ids. JNI EmulateKeyEvent bypasses this policy.
// Only down/up edges for an engine-recognized key are consumed; unknown keys
// and ACTION_MULTIPLE remain available to the platform's default handler.
inline bool ShouldDispatchPhysicalAndroidKey(int key_code, int action, int engine_key) {
    if (key_code == AKEYCODE_VOLUME_UP || key_code == AKEYCODE_VOLUME_DOWN ||
        key_code == AKEYCODE_VOLUME_MUTE) return false;
    return engine_key > 0 &&
           (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_UP);
}

} // namespace artc
