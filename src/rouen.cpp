// 1. Standard includes in alphabetic order
#include <array>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#ifndef _WIN32
#include <sys/signal.h>
#endif
#include <thread>

// 2. Libraries used in the project, in alphabetic order
#include "config_service.hpp"
#include "media_player.hpp"
#include "universal_sync_host.hpp"

// Platform-specific includes for process status handling
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>
// Windows doesn't have POSIX process status macros, so we define simple alternatives
#define WIFEXITED(status) (true)
#define WEXITSTATUS(status) (status)
#define WIFSIGNALED(status) (false)
#define WTERMSIG(status) (0)
#define popen _popen
#define pclose _pclose

// Windows debug console functionality
void setup_windows_debug_console() {
#ifdef _DEBUG
    // Allocate a console for this GUI application
    if (AllocConsole()) {
        // Redirect stdout, stdin, stderr to console
        freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
        freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
        
        // Set the console title
        SetConsoleTitle(L"Rouen Debug Console");
        
        // Make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog
        // point to console as well
        std::ios::sync_with_stdio(true);
        
        // Optional: Set console text attributes for better visibility
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole != INVALID_HANDLE_VALUE) {
            // Set console colors: white text on black background
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
        
        std::cout << "=== Rouen Debug Console Initialized ===" << '\n';
        std::cout << "Debug build - Console output enabled" << '\n';
        std::cout << "=========================================" << '\n';
    }
#endif // _DEBUG
}
#else
#include <sys/wait.h>
#endif

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "helpers/debug.hpp"
#include "helpers/notify_service.hpp"
#include "helpers/config_service_init.hpp" // For configuration service initialization
#include "hosts/video_feed_host.hpp"
#include "main_wnd.hpp"
#include "registrar.hpp"
#include <curl/curl.h>

int main(int argc, char* argv[]) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    
#ifdef _WIN32
    // Initialize Windows debug console for development builds
    setup_windows_debug_console();
#endif
    // Initialize CURL globally on the main thread before starting any threads
    curl_global_init(CURL_GLOBAL_ALL);
    for (int i = 1; i < argc; ++i) {
        std::string_view const arg(argv[i]);
        if (arg == "--no-initial-cards" || arg == "--no-cards") {
            deck::no_initial_cards = true;
        }
    }
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    try {
    // Debug: Print working directory at startup
    std::cout << "[DEBUG] Application starting from: " << std::filesystem::current_path() << '\n';
    std::cout << "[DEBUG] .env file should be at: " << std::filesystem::current_path() / ".env" << '\n';
    std::cout << "[DEBUG] .env file exists: " << (std::filesystem::exists(std::filesystem::current_path() / ".env") ? "YES" : "NO") << '\n';
    
    notify_service const notify; // Initialize the notify service
    
    // Initialize the configuration service
    rouen::helpers::ConfigServiceInitializer::initialize();
    
    // Force reload of .env file now that we have the correct working directory
    auto config_service = rouen::helpers::ConfigService::instance();
    config_service->load_env_file();
    std::cout << "[DEBUG] Forced reload of .env file completed" << '\n';
    
    // Register the run_command function - non-blocking with incremental output
    registrar::add<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>>(
        "run_command", 
        std::make_shared<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>>(
            [](std::string const& cmd, std::shared_ptr<std::function<void(std::string)>> const& callback) {
                // Launch the command in a background thread to avoid freezing the UI
                std::thread([cmd, callback]() noexcept { // NOLINT(bugprone-exception-escape)
                    try {
                        // Create a pipe to the command
                        FILE* pipe = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
                        if (!pipe) {
                            if (callback) {
                                (*callback)("Error: Failed to execute command");
                            }
                            return;
                        }
                        
                        // Buffer for reading output
                        std::array<char, 128> buffer;
                        std::string current_output;
                        bool has_output = false;
                        
                        // Read output incrementally
                        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                            has_output = true;
                            current_output += buffer.data();
                            
                            // Send the current output to the callback
                            if (callback) {
                                (*callback)(current_output);
                            }
                        }
                        
                        // Get the exit status of the command
                        int status = pclose(pipe);
                        
                        // If there was no output but the command completed, provide a default message
                        if (!has_output) {
                            if (WIFEXITED(status)) {
                                int exit_status = WEXITSTATUS(status);
                                if (exit_status == 0) {
                                    current_output = "Command completed successfully with no output.";
                                } else {
                                    current_output = std::format("Command failed with exit code: {}", exit_status);
                                }
                            } else {
                                current_output = "Command terminated abnormally.";
                            }
                            
                            // Send the final status message to the callback
                            if (callback) {
                                (*callback)(current_output);
                            }
                        } else {
                            // For commands with output, append the exit status
                            std::string status_message;
                            if (WIFEXITED(status)) {
                                int exit_status = WEXITSTATUS(status);
                                status_message = std::format("\n\nProcess exited with code: {}", exit_status);
                            } else if (WIFSIGNALED(status)) {
                                int term_signal = WTERMSIG(status);
                                status_message = std::format("\n\nProcess terminated by signal: {}", term_signal);
                            } else {
                                status_message = "\n\nProcess completed.";
                            }
                            
                            current_output += status_message;
                            
                            // Send the final output with status to the callback
                            if (callback) {
                                (*callback)(current_output);
                            }
                        }
                        
                        // Add a small marker to indicate process completion
                        if (callback) {
                            // Send a specially marked message that the card can detect to know the process is definitely complete
                            (*callback)(current_output + "\n<PROCESS_COMPLETED>");
                        }
                    } catch (std::exception const& e) {
                        std::cerr << "[ERROR] Exception in run_command thread: " << e.what() << '\n';
                    } catch (...) {
                        std::cerr << "[ERROR] Unknown exception in run_command thread\n";
                    }
                }).detach(); // Detach the thread so it runs independently
            }
        )
    );
    
    // Run startup synchronization if enabled
    if (config_service->get_env("ROUEN_SYNC_AUTO_ON_STARTUP") == "1") {
        std::cout << "[INFO] Auto-pull on startup is enabled. Running Sync In...\n";
        rouen::helpers::UniversalSyncService::instance().sync_in();
    }
    
    // Get the video feed host instance (do not auto-start)
    auto video_feed = rouen::hosts::VideoFeedHost::get_host();
    
    // Create and initialize the main window
    std::cout << "Creating main window..." << '\n';
    main_wnd window;
    std::cout << "Initializing main window..." << '\n';
    if (!window.initialize()) {
        std::cout << "Failed to initialize window!" << '\n';
        SYS_ERROR("Failed to initialize window");
        return -1;
    }
    std::cout << "Main window initialized successfully, starting main loop..." << '\n';
    
    // Run the main loop
    window.run();

    // Stop all media players and video feed host
    media_player::shutdown();
    video_feed->stop();

    // Run shutdown synchronization if enabled
    if (config_service->get_env("ROUEN_SYNC_AUTO_ON_SHUTDOWN") == "1") {
        std::cout << "[INFO] Auto-push on shutdown is enabled. Running Two-Way Sync...\n";
        rouen::helpers::UniversalSyncService::instance().sync_twoway("Auto-sync shutdown update", false);
    }

    } catch (const std::exception& e) {
        std::cerr << "Fatal exception: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal exception occurred\n";
        return 1;
    }

    curl_global_cleanup();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(__argc, __argv);
}
#endif
