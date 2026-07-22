#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_camera.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::vector<std::string> get_macos_command_cameras() {
    std::vector<std::string> cameras;

    // Method 1: macOS system_profiler SPCameraDataType
    FILE* pipe = popen("system_profiler SPCameraDataType 2>/dev/null", "r");
    if (pipe) {
        char buffer[512];
        std::string output;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            output += buffer;
        }
        pclose(pipe);

        std::istringstream iss(output);
        std::string line;
        bool in_camera_section = false;
        while (std::getline(iss, line)) {
            if (line.find("Camera:") != std::string::npos) {
                in_camera_section = true;
                continue;
            }
            if (in_camera_section) {
                // Device name headers under Camera: (indent level 4 spaces)
                if (line.rfind("    ", 0) == 0 && line.rfind("      ", 0) != 0) {
                    std::string cam_name = trim(line);
                    if (!cam_name.empty() && cam_name.back() == ':') {
                        cam_name.pop_back();
                    }
                    cam_name = trim(cam_name);
                    if (!cam_name.empty() && cam_name != "Camera") {
                        cameras.push_back(cam_name);
                    }
                }
            }
        }
    }

    // Method 2 fallback: ffmpeg -f avfoundation -list_devices
    if (cameras.empty()) {
        FILE* fpipe = popen("ffmpeg -f avfoundation -list_devices true -i \"\" 2>&1", "r");
        if (fpipe) {
            char buffer[512];
            std::string foutput;
            while (fgets(buffer, sizeof(buffer), fpipe)) {
                foutput += buffer;
            }
            pclose(fpipe);

            std::istringstream fiss(foutput);
            std::string line;
            bool in_video_devices = false;
            while (std::getline(fiss, line)) {
                if (line.find("AVFoundation video devices:") != std::string::npos) {
                    in_video_devices = true;
                    continue;
                }
                if (line.find("AVFoundation audio devices:") != std::string::npos) {
                    break;
                }
                if (in_video_devices) {
                    if (line.find("Capture screen") != std::string::npos) continue;

                    size_t bracket = line.rfind(']');
                    if (bracket != std::string::npos && bracket + 1 < line.length()) {
                        std::string cam_name = trim(line.substr(bracket + 1));
                        if (!cam_name.empty()) {
                            cameras.push_back(cam_name);
                        }
                    }
                }
            }
        }
    }

    return cameras;
}

std::vector<std::string> get_rouen_sdl3_cameras() {
    std::vector<std::string> cameras;
    SDL_Init(SDL_INIT_CAMERA);

    int count = 0;
    SDL_CameraID* devs = SDL_GetCameras(&count);
    if (devs) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetCameraName(devs[i]);
            if (name) {
                cameras.push_back(std::string(name));
            }
        }
        SDL_free(devs);
    }
    return cameras;
}

} // namespace

TEST(CameraDeviceListTest, MatchMacOsLocalCommands) {
    std::vector<std::string> local_cameras = get_macos_command_cameras();
    std::vector<std::string> rouen_cameras = get_rouen_sdl3_cameras();

    std::cout << "\n=== MACOS LOCAL COMMAND CAMERAS (" << local_cameras.size() << ") ===" << std::endl;
    for (const auto& cam : local_cameras) {
        std::cout << "  - " << cam << std::endl;
    }

    std::cout << "\n=== ROUEN SDL3 CAMERAS (" << rouen_cameras.size() << ") ===" << std::endl;
    for (const auto& cam : rouen_cameras) {
        std::cout << "  - " << cam << std::endl;
    }
    std::cout << "=========================================\n" << std::endl;

    if (!local_cameras.empty()) {
        EXPECT_FALSE(rouen_cameras.empty()) << "Local macOS system command detected cameras, but Rouen SDL3 camera list is empty!";
        
        for (const auto& sys_cam : local_cameras) {
            bool found = false;
            for (const auto& r_cam : rouen_cameras) {
                if (r_cam.find(sys_cam) != std::string::npos || sys_cam.find(r_cam) != std::string::npos) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found) << "Local macOS camera '" << sys_cam << "' was not found in Rouen's camera device list!";
        }
    } else {
        std::cout << "[INFO] No local camera hardware found via system command." << std::endl;
    }
}
