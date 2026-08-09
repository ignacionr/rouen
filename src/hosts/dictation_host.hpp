#pragma once

#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#include <csignal>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <io.h>
#ifndef X_OK
#define X_OK 0
#endif
using pid_t = int;
#endif
#include <format>

#include "../helpers/debug.hpp"
#include "../helpers/fetch.hpp"
#include "../helpers/notify_service.hpp"
#include "../registrar.hpp"

namespace rouen::hosts {

/**
 * DictationHost Controller
 * 
 * Manages audio recording and communication with whisper-server for real-time dictation.
 */
class dictation_host {
public:
    enum class State {
        Idle,
        Starting,
        Recording,
        Transcribing,
        Error
    };

    dictation_host()
        : state_(State::Idle),
          server_url_("http://127.0.0.1:8080") {
        DB_INFO("dictation_host: Initialized dictation host");
    }

    ~dictation_host() {
        stop_recording_sync();
    }

    static std::shared_ptr<dictation_host> get_host() {
        static std::mutex host_mutex;
        static std::shared_ptr<dictation_host> instance = nullptr;
        std::lock_guard<std::mutex> lock(host_mutex);
        if (!instance) {
            instance = std::make_shared<dictation_host>();
            try {
                registrar::add("dictation_host", instance);
            } catch (...) {}
        }
        return instance;
    }

    State get_state() const {
        return state_.load();
    }

    std::string get_status_message() const {
        std::lock_guard<std::mutex> lock(status_mutex_);
        return status_message_;
    }

    void set_server_url(const std::string& url) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        server_url_ = url;
    }

    std::string get_server_url() const {
        std::lock_guard<std::mutex> lock(status_mutex_);
        return server_url_;
    }

    static std::string find_audio_recorder() {
        std::vector<std::string> candidates = {
            "/opt/homebrew/bin/rec",
            "/usr/local/bin/rec",
            "/usr/bin/rec",
            "/opt/homebrew/bin/ffmpeg",
            "/usr/local/bin/ffmpeg",
            "/usr/bin/ffmpeg"
        };
        for (const auto& path : candidates) {
            if (std::filesystem::exists(path) && ::access(path.c_str(), X_OK) == 0) {
                return path;
            }
        }
        
        // Search PATH with homebrew and nix paths included
        std::string which_rec = "PATH=$PATH:/opt/homebrew/bin:/usr/local/bin:/nix/var/nix/profiles/default/bin which rec >/dev/null 2>&1";
        if (std::system(which_rec.c_str()) == 0) {
            return "rec";
        }
        std::string which_ffmpeg = "PATH=$PATH:/opt/homebrew/bin:/usr/local/bin:/nix/var/nix/profiles/default/bin which ffmpeg >/dev/null 2>&1";
        if (std::system(which_ffmpeg.c_str()) == 0) {
            return "ffmpeg";
        }
        return "";
    }

    bool start_recording() {
        State current = state_.load();
        if (current != State::Idle && current != State::Error) {
            DB_WARN("dictation_host: Cannot start recording when not idle");
            return false;
        }

        set_status("Starting microphone recording...");
        state_.store(State::Starting);

        // Generate temp WAV path
        auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        temp_wav_path_ = (std::filesystem::temp_directory_path() / std::format("rouen_dictation_{}.wav", now_ns)).string();

        // Spawn recording process in background thread
        std::thread([this, wav_file = temp_wav_path_]() {
            std::string rec_bin = find_audio_recorder();
            if (rec_bin.empty()) {
                set_status("Error: neither 'rec' nor 'ffmpeg' found");
                state_.store(State::Error);
                try { "notify"_sfn("Dictation error: Recording tool (rec/ffmpeg) not found"); } catch (...) {}
                return;
            }

#ifndef _WIN32
            pid_t pid = fork();
            if (pid == 0) {
                // Set environment PATH to include Homebrew & Nix
                setenv("PATH", "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);

                // Child process
                if (rec_bin.find("rec") != std::string::npos) {
                    execl(rec_bin.c_str(), "rec", "-q", "-c", "1", wav_file.c_str(), "rate", "16000", static_cast<char*>(nullptr));
                } else {
                    execl(rec_bin.c_str(), "ffmpeg", "-y", "-loglevel", "quiet", "-f", "avfoundation", "-i", ":default", "-ar", "16000", "-ac", "1", wav_file.c_str(), static_cast<char*>(nullptr));
                }
                _exit(1);
            } else if (pid > 0) {
                rec_pid_.store(pid);
                
                // Wait briefly (100ms) to ensure process is confirmed running before transitioning to Recording state
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (kill(pid, 0) == 0 && state_.load() == State::Starting) {
                    state_.store(State::Recording);
                    set_status("Recording in progress...");
                    DB_INFO_FMT("dictation_host: Recording active PID {}", pid);
                    try { "notify"_sfn("Dictation recording started"); } catch (...) {}
                }
            } else {
                set_status("Error: failed to fork recording process");
                state_.store(State::Error);
            }
#else
            set_status("Recording not supported on Windows");
            state_.store(State::Error);
#endif
        }).detach();

        return true;
    }

    void stop_recording(std::function<void(std::string)> on_transcribed) {
        State current = state_.load();
        if (current != State::Recording && current != State::Starting) {
            DB_WARN("dictation_host: stop_recording called when not recording");
            return;
        }

        state_.store(State::Transcribing);
        set_status("Transcribing audio via Whisper...");

        pid_t pid = rec_pid_.exchange(0);
        std::string wav_file = temp_wav_path_;

        std::thread([this, pid, wav_file, on_transcribed]() {
            if (pid > 0) {
#ifndef _WIN32
                // Send SIGINT to gracefully close WAV header
                kill(pid, SIGINT);
                int status = 0;
                waitpid(pid, &status, 0);
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            std::string transcript = transcribe_file(wav_file);

            // Clean up temp file
            if (!wav_file.empty() && std::filesystem::exists(wav_file)) {
                std::error_code ec;
                std::filesystem::remove(wav_file, ec);
            }

            state_.store(State::Idle);
            set_status(transcript.empty() ? "Idle (No speech recognized)" : "Idle");

            if (!transcript.empty()) {
                try { "notify"_sfn(std::format("Dictation transcribed: {}", transcript)); } catch (...) {}
            } else {
                try { "notify"_sfn("Dictation complete (No speech recognized)"); } catch (...) {}
            }

            if (on_transcribed) {
                on_transcribed(transcript);
            }
        }).detach();
    }

private:
    void stop_recording_sync() {
#ifndef _WIN32
        pid_t pid = rec_pid_.exchange(0);
        if (pid > 0) {
            kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
        }
#else
        rec_pid_.exchange(0);
#endif
        if (!temp_wav_path_.empty() && std::filesystem::exists(temp_wav_path_)) {
            std::error_code ec;
            std::filesystem::remove(temp_wav_path_, ec);
        }
    }

    void set_status(const std::string& msg) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_message_ = msg;
    }

    void ensure_whisper_server_running() {
        std::string url;
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            url = server_url_;
        }

        try {
            http::fetch fetcher(1);
            std::string resp = fetcher(url + "/");
            if (!resp.empty()) {
                return;
            }
        } catch (...) {}

        DB_INFO("dictation_host: whisper-server not active, starting background server...");
        [[maybe_unused]] int res = std::system("./scripts/whisper_server.sh start >/dev/null 2>&1 &");
        (void)res;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    std::string transcribe_file(const std::string& wav_file) {
        if (!std::filesystem::exists(wav_file) || std::filesystem::file_size(wav_file) < 100) {
            DB_WARN("dictation_host: Audio WAV file missing or empty");
            return "";
        }

        ensure_whisper_server_running();

        std::string target_url;
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            target_url = server_url_ + "/inference";
        }

        std::string response_text;
        CURL* curl = curl_easy_init();
        if (curl) {
            curl_mime* mime = curl_mime_init(curl);
            curl_mimepart* part_file = curl_mime_addpart(mime);
            curl_mime_name(part_file, "file");
            curl_mime_filedata(part_file, wav_file.c_str());

            curl_mimepart* part_format = curl_mime_addpart(mime);
            curl_mime_name(part_format, "response_format");
            curl_mime_data(part_format, "text", CURL_ZERO_TERMINATED);

            curl_easy_setopt(curl, CURLOPT_URL, target_url.c_str());
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

            using WriteCallbackType = size_t(*)(char*, size_t, size_t, void*);
            WriteCallbackType cb = [](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                auto* str = static_cast<std::string*>(userdata);
                str->append(ptr, size * nmemb);
                return size * nmemb;
            };

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_text);

            CURLcode res = curl_easy_perform(curl);
            curl_mime_free(mime);
            curl_easy_cleanup(curl);

            if (res == CURLE_OK && !response_text.empty()) {
                // Trim leading/trailing whitespace
                size_t first = response_text.find_first_not_of(" \t\n\r");
                if (first != std::string::npos) {
                    size_t last = response_text.find_last_not_of(" \t\n\r");
                    response_text = response_text.substr(first, (last - first + 1));
                } else {
                    response_text.clear();
                }

                if (response_text == "[BLANK_AUDIO]" || response_text == "[blank_audio]") {
                    response_text.clear();
                }

                DB_INFO_FMT("dictation_host: Transcribed text: '{}'", response_text);
                return response_text;
            } else {
                DB_ERROR_FMT("dictation_host: libcurl error code {} from whisper-server", static_cast<int>(res));
            }
        }

        return "";
    }

    std::atomic<State> state_{State::Idle};
    std::atomic<pid_t> rec_pid_{0};
    std::string temp_wav_path_;
    mutable std::mutex status_mutex_;
    std::string status_message_;
    std::string server_url_;
};

} // namespace rouen::hosts
