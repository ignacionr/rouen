#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_camera.h>

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/texture_helper.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

struct camera_device_info {
    SDL_CameraID id{0};
    std::string name;
};

class camera_card : public card {
public:
    camera_card() {
        colors[0] = ImVec4{0.18f, 0.45f, 0.72f, 1.0f}; // Primary accent
        colors[1] = ImVec4{0.12f, 0.32f, 0.55f, 0.7f}; // Secondary
        width = 480.0f;
        name("Camera Feed");
        refresh_camera_list();
        register_api_services();
    }

    ~camera_card() override {
        unregister_api_services();
        close_camera();
        free_gpu_texture();
    }

    std::string get_uri() const override {
        return "camera";
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "camera" || uri.starts_with("camera:");
    }

    void handle_uri(std::string_view uri) override {
        if (uri.starts_with("camera:")) {
            std::string arg(uri.substr(7));
            try {
                int index = std::stoi(arg);
                select_camera(index);
            } catch (...) {}
        }
    }

    void register_api_services() {
        auto status_fn = std::make_shared<std::function<std::string()>>([this]() {
            return get_status_json();
        });
        registrar::add("camera_get_status", status_fn);

        auto snapshot_fn = std::make_shared<std::function<std::string(const std::string&)>>([this](const std::string& path) {
            return save_snapshot_json(path);
        });
        registrar::add("camera_save_snapshot", snapshot_fn);
    }

    void unregister_api_services() {
        registrar::remove<std::function<std::string()>>("camera_get_status");
        registrar::remove<std::function<std::string(const std::string&)>>("camera_save_snapshot");
    }

    std::string get_status_json() {
        bool active = (selected_index_ > 0 && camera_device_ != nullptr);
        std::string cam_name = (selected_index_ >= 0 && selected_index_ < static_cast<int>(cameras_.size()))
            ? cameras_[static_size_cast(selected_index_)].name : "-Off-";

        return std::format(
            R"({{"active":{},"camera_name":"{}","width":{},"height":{},"has_frame":{}}})",
            active ? "true" : "false",
            cam_name,
            frame_width_,
            frame_height_,
            has_frame_ ? "true" : "false"
        );
    }

    std::string save_snapshot_json(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!has_frame_ || frame_rgba_pixels_.empty() || frame_width_ <= 0 || frame_height_ <= 0) {
            return R"({"success":false,"error":"No camera frame available to capture"})";
        }

        FILE* f = fopen(filepath.c_str(), "wb");
        if (!f) {
            return std::format(R"({{"success":false,"error":"Failed to open output file {}"}})", filepath);
        }

        fprintf(f, "P6\n%d %d\n255\n", frame_width_, frame_height_);
        for (size_t i = 0; i < frame_rgba_pixels_.size(); i += 4) {
            fputc(frame_rgba_pixels_[i], f);     // R
            fputc(frame_rgba_pixels_[i+1], f);   // G
            fputc(frame_rgba_pixels_[i+2], f);   // B
        }
        fclose(f);

        return std::format(
            R"({{"success":true,"message":"Snapshot saved successfully","file":"{}","width":{},"height":{}}})",
            filepath, frame_width_, frame_height_
        );
    }

    void refresh_camera_list() {
        try {
            if (!SDL_WasInit(SDL_INIT_CAMERA)) {
                SDL_InitSubSystem(SDL_INIT_CAMERA);
            }
            cameras_.clear();
            cameras_.push_back({0, "-Off-"});

            int count = 0;
            SDL_CameraID* devs = SDL_GetCameras(&count);
            if (devs) {
                for (int i = 0; i < count; ++i) {
                    const char* dev_name = SDL_GetCameraName(devs[i]);
                    std::string name_str = dev_name ? std::string(dev_name) : std::format("Camera {}", i + 1);
                    cameras_.push_back({devs[i], name_str});
                }
                SDL_free(devs);
            }
            if (selected_index_ >= static_cast<int>(cameras_.size())) {
                selected_index_ = 0;
            }
        } catch (const std::exception& e) {
            error_message_ = std::format("Error enumerating cameras: {}", e.what());
        } catch (...) {
            error_message_ = "Unknown error enumerating cameras";
        }
    }

    void close_camera() {
        try {
            if (camera_device_) {
                SDL_CloseCamera(camera_device_);
                camera_device_ = nullptr;
            }
        } catch (...) {
            camera_device_ = nullptr;
        }
        has_frame_ = false;
        requested_fps = 1;
    }

    void free_gpu_texture() {
        if (rouen_gpu_texture_) {
            try {
                TextureHelper::destroyTexture(rouen_gpu_texture_);
            } catch (...) {}
            rouen_gpu_texture_ = nullptr;
        }
        gpu_texture_device_ = nullptr;
    }

    void select_camera(int index) {
        close_camera();
        register_api_services();

        if (index <= 0 || index >= static_cast<int>(cameras_.size())) {
            selected_index_ = 0;
            error_message_.clear();
            return;
        }

        selected_index_ = index;
        requested_fps = 30;
        permission_granted_time_ = std::chrono::steady_clock::time_point{};
        SDL_CameraID cam_id = cameras_[static_cast<size_t>(index)].id;

        try {
            camera_device_ = SDL_OpenCamera(cam_id, nullptr);
            if (!camera_device_) {
                error_message_ = std::format("Failed to open camera: {}", SDL_GetError());
                selected_index_ = 0;
                requested_fps = 1;
            } else {
                error_message_.clear();
            }
        } catch (const std::exception& e) {
            error_message_ = std::format("Exception opening camera: {}", e.what());
            selected_index_ = 0;
            requested_fps = 1;
        } catch (...) {
            error_message_ = "Unknown error opening camera";
            selected_index_ = 0;
            requested_fps = 1;
        }
    }

    void update_camera_frame(SDL_GPUDevice* device) {
        if (!camera_device_) return;

        int perm_state = SDL_GetCameraPermissionState(camera_device_);
        if (perm_state != 1) {
            permission_granted_time_ = std::chrono::steady_clock::time_point{};
            if (perm_state == -1) {
                error_message_ = "Camera access was denied in macOS System Settings.";
            }
            return;
        }

        auto now = std::chrono::steady_clock::now();
        if (permission_granted_time_ == std::chrono::steady_clock::time_point{}) {
            permission_granted_time_ = now;
            return;
        }

        if (now - permission_granted_time_ < std::chrono::milliseconds(350)) {
            return; // Warmup buffer: wait 350ms for macOS AVFoundation hardware pipeline initialization
        }

        try {
            Uint64 timestamp = 0;
            SDL_Surface* latest_surface = nullptr;

            // Drain all accumulated frames from the ring buffer, keeping only the LATEST real-time frame
            while (SDL_Surface* surface = SDL_AcquireCameraFrame(camera_device_, &timestamp)) {
                if (latest_surface) {
                    SDL_ReleaseCameraFrame(camera_device_, latest_surface);
                }
                latest_surface = surface;
            }

            if (latest_surface) {
                if (latest_surface->pixels && latest_surface->w > 0 && latest_surface->h > 0) {
                    SDL_Surface* rgba_surface = latest_surface;
                    bool converted = false;

                    if (latest_surface->format != SDL_PIXELFORMAT_RGBA32) {
                        rgba_surface = SDL_ConvertSurface(latest_surface, SDL_PIXELFORMAT_RGBA32);
                        converted = true;
                    }

                    if (rgba_surface && rgba_surface->pixels && rgba_surface->w > 0 && rgba_surface->h > 0) {
                        int w = rgba_surface->w;
                        int h = rgba_surface->h;
                        frame_width_ = w;
                        frame_height_ = h;

                        size_t buf_bytes = static_cast<size_t>(w * h * 4);
                        {
                            std::lock_guard<std::mutex> lock(frame_mutex_);
                            if (frame_rgba_pixels_.size() != buf_bytes) {
                                frame_rgba_pixels_.resize(buf_bytes);
                            }
                            std::memcpy(frame_rgba_pixels_.data(), rgba_surface->pixels, buf_bytes);
                            has_frame_ = true;
                        }

                        if (device) {
                            upload_gpu_texture_rgba(device, w, h, static_cast<const uint8_t*>(rgba_surface->pixels));
                        }
                    }

                    if (converted && rgba_surface) {
                        SDL_DestroySurface(rgba_surface);
                    }
                }
                SDL_ReleaseCameraFrame(camera_device_, latest_surface);
            }
        } catch (const std::exception& e) {
            error_message_ = std::format("Error acquiring frame: {}", e.what());
        } catch (...) {
            error_message_ = "Error acquiring camera frame";
        }
    }

    void upload_gpu_texture_rgba(SDL_GPUDevice* device, int w, int h, const uint8_t* rgba_data) {
        if (!device || !rgba_data || w <= 0 || h <= 0) return;

        Uint32 pitch = static_cast<Uint32>(w * 4);

        if (!rouen_gpu_texture_ || gpu_texture_width_ != w || gpu_texture_height_ != h || gpu_texture_device_ != device) {
            free_gpu_texture();

            SDL_GPUTextureCreateInfo texture_info = {};
            texture_info.type = SDL_GPU_TEXTURETYPE_2D;
            texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            texture_info.width = static_cast<Uint32>(w);
            texture_info.height = static_cast<Uint32>(h);
            texture_info.layer_count_or_depth = 1;
            texture_info.num_levels = 1;

            SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &texture_info);
            if (!tex) return;

            rouen_gpu_texture_ = new RouenGPUTexture();
            rouen_gpu_texture_->binding.texture = tex;
            rouen_gpu_texture_->binding.sampler = TextureHelper::getDefaultSampler(device);
            rouen_gpu_texture_->width = w;
            rouen_gpu_texture_->height = h;

            gpu_texture_width_ = w;
            gpu_texture_height_ = h;
            gpu_texture_device_ = device;
        }

        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = static_cast<Uint32>(h * pitch);

        SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (!transfer_buffer) return;

        void* map = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
        if (map) {
            std::memcpy(map, rgba_data, static_cast<size_t>(h * pitch));
            SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
        }

        SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(device);
        if (cmdbuf) {
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmdbuf);
            if (copy_pass) {
                SDL_GPUTextureTransferInfo transfer_src = {};
                transfer_src.transfer_buffer = transfer_buffer;
                transfer_src.offset = 0;
                transfer_src.pixels_per_row = static_cast<Uint32>(w);
                transfer_src.rows_per_layer = static_cast<Uint32>(h);

                SDL_GPUTextureRegion transfer_dst = {};
                transfer_dst.texture = rouen_gpu_texture_->binding.texture;
                transfer_dst.w = static_cast<Uint32>(w);
                transfer_dst.h = static_cast<Uint32>(h);
                transfer_dst.d = 1;

                SDL_UploadToGPUTexture(copy_pass, &transfer_src, &transfer_dst, false);
                SDL_EndGPUCopyPass(copy_pass);
            }
            SDL_SubmitGPUCommandBuffer(cmdbuf);
        }
        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
    }

    bool render(rouen::ui::ui_context& /*ui*/) override {
        if (selected_index_ > 0 && camera_device_) {
            SDL_GPUDevice* device = nullptr;
            try {
                auto dev_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
                if (dev_ptr) device = *dev_ptr;
            } catch (...) {}
            if (!device) device = TextureHelper::g_gpu_device;

            update_camera_frame(device);
        }

        return render_window([this]() {
            // Header / Controls Bar
            ImGui::AlignTextToFramePadding();
            ImGui::Text(ICON_MD_VIDEOCAM " Camera:");
            ImGui::SameLine();

            std::string combo_label = (selected_index_ >= 0 && selected_index_ < static_cast<int>(cameras_.size()))
                ? cameras_[static_size_cast(selected_index_)].name : "-Off-";

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 40.0f);
            if (ImGui::BeginCombo("##CameraCombo", combo_label.c_str())) {
                for (int i = 0; i < static_cast<int>(cameras_.size()); ++i) {
                    bool is_selected = (selected_index_ == i);
                    if (ImGui::Selectable(cameras_[static_size_cast(i)].name.c_str(), is_selected)) {
                        select_camera(i);
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_MD_REFRESH "##RefreshCameras")) {
                refresh_camera_list();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Scan for connected cameras");
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Error notice display
            if (!error_message_.empty()) {
                ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, ICON_MD_ERROR_OUTLINE " %s", error_message_.c_str());
                ImGui::Spacing();
            }

            // Display Feed / Placeholder
            if (selected_index_ == 0 || !camera_device_) {
                // Off state placeholder
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float view_h = std::max(240.0f, avail.y - 30.0f);
                
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.1f, 0.12f, 0.16f, 0.8f});
                if (ImGui::BeginChild("CameraOffView", ImVec2(avail.x, view_h), true)) {
                    float text_w = ImGui::CalcTextSize(ICON_MD_VIDEOCAM_OFF " Camera is Off").x;
                    ImGui::SetCursorPosX((avail.x - text_w) * 0.5f);
                    ImGui::SetCursorPosY(view_h * 0.4f);
                    ImGui::TextDisabled(ICON_MD_VIDEOCAM_OFF " Camera is Off");

                    float sub_w = ImGui::CalcTextSize("Select a camera from the dropdown above to start live feed.").x;
                    ImGui::SetCursorPosX(std::max(0.0f, (avail.x - sub_w) * 0.5f));
                    ImGui::TextDisabled("Select a camera from the dropdown above to start live feed.");
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            } else {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float view_h = std::max(240.0f, avail.y - 30.0f);

                if (rouen_gpu_texture_) {
                    float aspect = (frame_height_ > 0) ? static_cast<float>(frame_width_) / static_cast<float>(frame_height_) : 16.0f / 9.0f;
                    float display_w = avail.x;
                    float display_h = display_w / aspect;
                    if (display_h > view_h) {
                        display_h = view_h;
                        display_w = display_h * aspect;
                    }

                    float pad_x = (avail.x - display_w) * 0.5f;
                    if (pad_x > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);

                    ImTextureID tex_id = rouen::helpers::texture_id_cast(rouen_gpu_texture_);
                    ImGui::Image(tex_id, ImVec2(display_w, display_h));
                } else {
                    ImGui::TextDisabled(ICON_MD_HOURGLASS_EMPTY " Waiting for camera frame...");
                }

                // Telemetry Footer
                ImGui::Spacing();
                ImGui::TextColored(ImVec4{0.4f, 0.9f, 0.4f, 1.0f}, ICON_MD_FIBER_MANUAL_RECORD " LIVE");
                ImGui::SameLine();
                ImGui::TextDisabled("| Resolution: %dx%d @ 30 FPS", frame_width_, frame_height_);
            }

            return true;
        });
    }

    void render_video_ui() override {
        if (selected_index_ <= 0 || !has_frame_) return;

        ImVec2 display_size = ImGui::GetIO().DisplaySize;
        float aspect = (frame_height_ > 0) ? static_cast<float>(frame_width_) / static_cast<float>(frame_height_) : 16.0f / 9.0f;
        float display_w = display_size.x;
        float display_h = display_w / aspect;
        if (display_h > display_size.y) {
            display_h = display_size.y;
            display_w = display_h * aspect;
        }

        float pos_x = (display_size.x - display_w) * 0.5f;
        float pos_y = (display_size.y - display_h) * 0.5f;

        ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
        ImGui::SetNextWindowSize(ImVec2(display_w, display_h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##CameraCastWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground)) {
            if (rouen_gpu_texture_) {
                ImTextureID tex_id = rouen::helpers::texture_id_cast(rouen_gpu_texture_);
                ImGui::Image(tex_id, ImVec2(display_w, display_h));
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    static size_t static_size_cast(int val) {
        return static_cast<size_t>(std::max(0, val));
    }

    std::vector<camera_device_info> cameras_;
    int selected_index_{0};
    SDL_Camera* camera_device_{nullptr};
    std::string error_message_;
    std::chrono::steady_clock::time_point permission_granted_time_{};

    std::mutex frame_mutex_;
    std::vector<uint8_t> frame_rgba_pixels_;
    int frame_width_{0};
    int frame_height_{0};
    bool has_frame_{false};

    RouenGPUTexture* rouen_gpu_texture_{nullptr};
    SDL_GPUDevice* gpu_texture_device_{nullptr};
    int gpu_texture_width_{0};
    int gpu_texture_height_{0};
};

} // namespace rouen::cards
