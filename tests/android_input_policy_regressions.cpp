#include "jni/android_input_policy.h"
#include <cstdlib>
#include <iostream>

static void Check(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}

int main() {
    using artc::ShouldDispatchPhysicalAndroidKey;
    // Frameworks register HUP=136 as Backlog and HDW=137 as Click. Neither
    // registration may steal the physical volume buttons from Android.
    for (int action : {AKEY_EVENT_ACTION_DOWN, AKEY_EVENT_ACTION_UP,
                       AKEY_EVENT_ACTION_MULTIPLE}) {
        Check(!ShouldDispatchPhysicalAndroidKey(AKEYCODE_VOLUME_DOWN, action, 136),
              "volume down returns to system dispatch even with a game binding");
        Check(!ShouldDispatchPhysicalAndroidKey(AKEYCODE_VOLUME_UP, action, 137),
              "volume up returns to system dispatch even with a game binding");
        Check(!ShouldDispatchPhysicalAndroidKey(AKEYCODE_VOLUME_MUTE, action, 137),
              "volume mute returns to system dispatch");
    }
    for (int action : {AKEY_EVENT_ACTION_DOWN, AKEY_EVENT_ACTION_UP}) {
        Check(ShouldDispatchPhysicalAndroidKey(AKEYCODE_BACK, action, 8),
              "back edges still reach the game's menu handler");
        Check(ShouldDispatchPhysicalAndroidKey(AKEYCODE_ENTER, action, 13),
              "confirm edges still reach the game");
        Check(!ShouldDispatchPhysicalAndroidKey(AKEYCODE_MEDIA_PLAY_PAUSE, action, -1),
              "unknown media buttons retain platform handling");
    }
    Check(!ShouldDispatchPhysicalAndroidKey(AKEYCODE_ENTER, AKEY_EVENT_ACTION_MULTIPLE, 13),
          "a multiple-action event cannot synthesize a false key release");
    Check(!ShouldDispatchPhysicalAndroidKey(AKEYCODE_ENTER, 99, 13),
          "an unsupported action remains unconsumed");
    std::cout << "android input policy regressions passed\n";
}
