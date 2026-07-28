#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <memory>
#include <vector>

#include "../src/helpers/media_player_item.hpp"
#include "../src/registrar.hpp"

class AudioTimeProgressionTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef _WIN32
        _putenv_s("SDL_AUDIO_DRIVER", "dummy");
#else
        setenv("SDL_AUDIO_DRIVER", "dummy", 1);
#endif
        SDL_Init(SDL_INIT_AUDIO);

        auto notify_mock = std::make_shared<std::function<void(std::string const&)>>([](std::string const& msg) {
            std::cout << "[MOCK NOTIFY] " << msg << std::endl;
        });
        registrar::add("notify", notify_mock);
    }

    void TearDown() override {
        SDL_Quit();
    }
};

TEST_F(AudioTimeProgressionTest, ResumeTimeProgressionForAudioOnlyContent) {
    media_player_item item;
    std::string test_url = "https://venganzasdelpasado.com.ar/2026/lavenganza_2026-07-22.mp3";
    item.url = test_url;

    // Simulate "Resume 23:03" (1383.0 seconds)
    double resume_offset = 1383.0;
    item.start_offset = resume_offset;

    std::cout << "[TEST] Starting playback for: " << test_url << " at offset " << resume_offset << "s (" << item.formatTime(resume_offset) << ")" << std::endl;
    ASSERT_TRUE(item.playMedia());

    // Wait up to 5 seconds for FFmpeg worker thread to connect and start decoding
    bool started_playing = false;
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (item.ffmpeg_running.load() && item.is_playing) {
            started_playing = true;
            break;
        }
    }
    ASSERT_TRUE(started_playing);

    // Collect position samples over 4 seconds
    std::vector<double> positions;
    std::cout << "[TEST] Sampling position over 4 seconds..." << std::endl;
    for (int i = 0; i < 8; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        double current_pos = item.get_current_position();
        positions.push_back(current_pos);
        std::cout << "[TEST] Sample " << i + 1 << ": " << current_pos << "s (" << item.formatTime(current_pos) << ")" << std::endl;
    }

    item.stopMedia();

    // Verification 1: Must be near the resume offset (not 0.0 or reset)
    EXPECT_GE(positions.front(), resume_offset - 2.0);

    // Verification 2: Position MUST strictly progress over the 4-second period
    double total_delta = positions.back() - positions.front();
    std::cout << "[TEST] Total position delta over 4 seconds: " << total_delta << "s" << std::endl;

    EXPECT_GT(total_delta, 1.0) << "Error: Media time position indication is frozen or not progressing!";
}
