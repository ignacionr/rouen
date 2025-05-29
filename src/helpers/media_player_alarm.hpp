#pragma once
#include "media_player_item.hpp"
#include "platform_utils.hpp"
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
    static media_player_item& alarm_item_instance() {
        static media_player_item alarm_item;
        return alarm_item;
    }
    
    static void play_sound_loop(std::string_view file_path) {
        auto& alarm_item = alarm_item_instance();
        auto resource_path = rouen::platform::get_resource_path(std::string(file_path), "");
        alarm_item.url = resource_path.string();
        alarm_item.stopMedia();
        std::string socket_path = alarm_item.mpv_socket.create_socket_path();
        std::string mpv_path;
        bool mpv_found = rouen::platform::check_mpv_availability(mpv_path);
        if (!mpv_found) {
            try { "notify"_sfn("Cannot play alarm: MPV not found. Please install MPV using 'brew install mpv'."); } catch (...) {}
            return;
        }
        
#ifdef _WIN32
        // Windows implementation using CreateProcess
        static std::string socket_arg = "--input-ipc-server=" + socket_path;
        std::vector<std::string> args = {
            mpv_path,
            "--no-video",
            "--loop=inf", 
            "--really-quiet",
            "--keep-open=always",
            "--idle=yes",
            "--input-ipc-timeout=1000",
            socket_arg,
            alarm_item.url
        };
        
        std::string cmdline;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) cmdline += " ";
            cmdline += "\"" + args[i] + "\"";
        }
        
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        if (CreateProcessA(nullptr, const_cast<char*>(cmdline.c_str()), nullptr, nullptr, 
                          FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
            alarm_item.player_pid = pi.dwProcessId;
            alarm_item.is_playing = true;
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (alarm_item.mpv_socket.init_socket(socket_path)) {
                alarm_item.startPositionTracking();
            }
        } else {
            alarm_item.player_pid = 0;
            alarm_item.is_playing = false;
        }
#else
        // Unix implementation using fork/exec
        pid_t pid = fork();
        if (pid == -1) return;
        else if (pid == 0) {
            int fd = open("/tmp/mpv_alarm_output.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd != -1) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
            setsid();
            static std::string socket_arg;
            socket_arg = "--input-ipc-server=" + socket_path;
            std::vector<const char*> args;
            args.push_back(mpv_path.c_str());
            args.push_back("--no-video");
            args.push_back("--loop=inf");
            args.push_back("--really-quiet");
            args.push_back("--keep-open=always");
            args.push_back("--idle=yes");
            args.push_back("--input-ipc-timeout=1000");
            args.push_back(socket_arg.c_str());
            args.push_back(alarm_item.url.c_str());
            args.push_back(nullptr);
            execv(mpv_path.c_str(), const_cast<char* const*>(args.data()));
            exit(1);
        }
        if (pid > 0) {
            alarm_item.player_pid = pid;
            alarm_item.is_playing = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (alarm_item.mpv_socket.init_socket(socket_path)) {
                alarm_item.startPositionTracking();
            }
        } else {
            alarm_item.player_pid = 0;
            alarm_item.is_playing = false;
        }
#endif
    }
    
    static void stop_sound_loop() {
        auto& alarm_item = alarm_item_instance();
        alarm_item.stopMedia();
    }
}
