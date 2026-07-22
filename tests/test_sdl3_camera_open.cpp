#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_camera.h>
#include <iostream>

TEST(SdlCameraOpenTest, TestOpeningAllCameras) {
    ASSERT_TRUE(SDL_Init(SDL_INIT_CAMERA)) << "Failed to init SDL_INIT_CAMERA: " << SDL_GetError();

    int count = 0;
    SDL_CameraID* devs = SDL_GetCameras(&count);
    std::cout << "Found " << count << " camera devices." << std::endl;

    if (devs) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetCameraName(devs[i]);
            std::cout << "Testing camera [" << i << "]: " << (name ? name : "Unknown") << " (ID: " << devs[i] << ")" << std::endl;

            int num_specs = 0;
            SDL_CameraSpec** specs = SDL_GetCameraSupportedFormats(devs[i], &num_specs);
            std::cout << "  Supported specs count: " << num_specs << std::endl;
            if (specs && num_specs > 0) {
                for (int s = 0; s < std::min(num_specs, 5); ++s) {
                    if (specs[s]) {
                        std::cout << "    Spec " << s << ": " << specs[s]->width << "x" << specs[s]->height 
                                  << " fmt=" << SDL_GetPixelFormatName(specs[s]->format) 
                                  << " fps=" << specs[s]->framerate_numerator << "/" << specs[s]->framerate_denominator << std::endl;
                    }
                }
                SDL_free(specs);
            }

            // Test 1: Opening with NULL spec (SDL chooses default)
            std::cout << "  Attempting SDL_OpenCamera(id, NULL)..." << std::endl;
            SDL_Camera* cam1 = SDL_OpenCamera(devs[i], nullptr);
            if (cam1) {
                std::cout << "    SUCCESSFULLY OPENED camera with NULL spec!" << std::endl;
                SDL_CloseCamera(cam1);
            } else {
                std::cout << "    FAILED to open camera with NULL spec: " << SDL_GetError() << std::endl;
            }

            // Test 2: Opening with zeroed spec
            SDL_CameraSpec custom_spec;
            SDL_zero(custom_spec);
            custom_spec.format = SDL_PIXELFORMAT_RGBA32;
            custom_spec.framerate_numerator = 30;
            custom_spec.framerate_denominator = 1;

            std::cout << "  Attempting SDL_OpenCamera(id, &custom_spec)..." << std::endl;
            SDL_Camera* cam2 = SDL_OpenCamera(devs[i], &custom_spec);
            if (cam2) {
                std::cout << "    SUCCESSFULLY OPENED camera with custom spec!" << std::endl;
                SDL_CloseCamera(cam2);
            } else {
                std::cout << "    FAILED to open camera with custom spec: " << SDL_GetError() << std::endl;
            }
        }
        SDL_free(devs);
    }
}
