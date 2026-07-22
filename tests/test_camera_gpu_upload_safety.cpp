#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_camera.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

TEST(CameraGpuUploadSafetyTest, AcquireAndConvertCameraFrames) {
    ASSERT_TRUE(SDL_Init(SDL_INIT_CAMERA)) << "Failed to init SDL_INIT_CAMERA: " << SDL_GetError();

    int count = 0;
    SDL_CameraID* devs = SDL_GetCameras(&count);
    ASSERT_GT(count, 0) << "No camera devices found on macOS!";

    std::cout << "[TEST] Found " << count << " camera devices. Opening first device (ID: " << devs[0] << ")..." << std::endl;
    const char* dev_name = SDL_GetCameraName(devs[0]);
    std::cout << "[TEST] Camera name: " << (dev_name ? dev_name : "Unknown") << std::endl;

    SDL_Camera* cam = SDL_OpenCamera(devs[0], nullptr);
    ASSERT_NE(cam, nullptr) << "Failed to open camera device: " << SDL_GetError();

    // Wait up to 3 seconds for camera frames to start flowing
    std::cout << "[TEST] Waiting for camera frames..." << std::endl;
    bool received_frame = false;
    int acquired_count = 0;

    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(4)) {
        Uint64 timestamp = 0;
        SDL_Surface* frame_surface = SDL_AcquireCameraFrame(cam, &timestamp);
        if (frame_surface) {
            received_frame = true;
            ++acquired_count;

            EXPECT_GT(frame_surface->w, 0);
            EXPECT_GT(frame_surface->h, 0);
            EXPECT_NE(frame_surface->pixels, nullptr);

            // Test RGBA32 conversion safety
            SDL_Surface* rgba_surface = frame_surface;
            bool converted = false;
            if (frame_surface->format != SDL_PIXELFORMAT_RGBA32) {
                rgba_surface = SDL_ConvertSurface(frame_surface, SDL_PIXELFORMAT_RGBA32);
                converted = true;
            }

            ASSERT_NE(rgba_surface, nullptr);
            ASSERT_NE(rgba_surface->pixels, nullptr);
            EXPECT_EQ(rgba_surface->format, SDL_PIXELFORMAT_RGBA32);

            if (converted) {
                SDL_DestroySurface(rgba_surface);
            }
            SDL_ReleaseCameraFrame(cam, frame_surface);

            if (acquired_count >= 5) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    SDL_CloseCamera(cam);
    SDL_free(devs);

    std::cout << "[TEST] Successfully acquired and converted " << acquired_count << " live camera frames!" << std::endl;
    EXPECT_TRUE(received_frame) << "Did not receive any camera frames within timeout!";
}
