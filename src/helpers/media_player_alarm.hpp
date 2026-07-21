#pragma once
#include "media_player_item.hpp"
#include "platform_utils.hpp"
#include "config_service.hpp"
#include <string>
#include <thread>
#include <chrono>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <process.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <signal.h>
#endif

#include <vector>
#include <filesystem>
#include <fstream>

namespace media_player_alarm_helper {
    inline media_player_item& alarm_item_instance() {
        static media_player_item alarm_item;
        return alarm_item;
    }
    
    inline void play_sound_loop(std::string_view file_path) {
        auto& alarm_item = alarm_item_instance();
        auto resource_path = rouen::platform::get_resource_path(std::string(file_path), "");
        alarm_item.url = resource_path.string();
        alarm_item.stopMedia();
        alarm_item.playMedia();
    }
    
    inline void stop_sound_loop() {
        auto& alarm_item = alarm_item_instance();
        alarm_item.stopMedia();
    }
}
