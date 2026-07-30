#pragma once

namespace rouen::platform {
#if defined(__APPLE__)
    void request_mac_microphone_permission();
#else
    inline void request_mac_microphone_permission() {}
#endif
}
