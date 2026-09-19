#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/signal.h>
#include <unistd.h>
#endif

// 2. Libraries used in the project, in alphabetic order
#include "config_service.hpp"
#include "hosts/rouen_mesh_host.hpp"
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
        SetConsoleTitleW(L"Rouen Debug Console");
        
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

void ensure_windows_console_attached() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
        std::ios::sync_with_stdio(true);
    }
}
#else
#include <sys/wait.h>
#endif

// 3. All other includes
#include "cards/interface/deck.hpp"
#include "helpers/debug.hpp"
#include "helpers/notify_service.hpp"
#include "helpers/presence_service.hpp"
#include "helpers/config_service_init.hpp" // For configuration service initialization
#include "helpers/fetch.hpp"
#include <glaze/glaze.hpp>
#include "hosts/plugin_host.hpp"
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
    bool cli_mode = false;
    std::string notify_message;
    std::string explicit_target;
    bool spoken = true;
    bool show_presence = false;
    bool show_help = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view const arg(argv[i]);
        if (arg == "--notify" || arg == "-n") {
            cli_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                notify_message = argv[++i];
            }
        } else if (arg == "--target" || arg == "-t") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                explicit_target = argv[++i];
            }
        } else if (arg == "--no-speak" || arg == "--silent") {
            spoken = false;
        } else if (arg == "--presence" || arg == "-p") {
            cli_mode = true;
            show_presence = true;
        } else if (arg == "--help" || arg == "-h") {
            cli_mode = true;
            show_help = true;
        } else if (arg == "--no-initial-cards" || arg == "--no-cards") {
            deck::no_initial_cards = true;
        }
    }

    if (cli_mode) {
#ifdef _WIN32
        ensure_windows_console_attached();
#endif
        if (show_help) {
            std::cout << "Rouen - Universal AI & Dashboard Mesh\n\n"
                      << "Usage:\n"
                      << "  rouen [options]\n\n"
                      << "Headless CLI Options:\n"
                      << "  -n, --notify <message>         Send a notification routed to user's active presence\n"
                      << "  -t, --target <client_id>       Specify explicit recipient client ID (optional)\n"
                      << "      --no-speak, --silent       Send notification silently without speech\n"
                      << "  -p, --presence                 Query and display current presence across mesh\n"
                      << "  -h, --help                     Display this help message\n\n"
                      << "GUI Options:\n"
                      << "      --no-cards                 Start with an empty deck\n";
            curl_global_cleanup();
            return 0;
        }

        if (notify_message.empty() && !show_presence) {
            std::cerr << "Error: --notify / -n requires a message argument.\n"
                      << "Usage: rouen --notify \"Your message here\"\n";
            curl_global_cleanup();
            return 1;
        }

        // 1. Fast path: check if local Rouen GUI instance is running on port 8081
        bool local_api_available = false;
        try {
            http::fetch api_check(1);
            std::string health = api_check("http://127.0.0.1:8081/api/health");
            if (api_check.last_http_code() == 200 && health.find("\"ok\"") != std::string::npos) {
                local_api_available = true;
            }
        } catch (...) {
            local_api_available = false;
        }

        if (local_api_available) {
            http::fetch api_client(3);
            if (show_presence) {
                try {
                    std::string pres_json = api_client("http://127.0.0.1:8081/api/presence");
                    glz::json_t doc;
                    if (glz::read_json(doc, pres_json) == glz::error_code::none) {
                        std::string rec_target;
                        if (doc.contains("recommended_target") && doc["recommended_target"].is_string()) {
                            rec_target = doc["recommended_target"].get<std::string>();
                        }
                        std::cout << "\n=== Rouen Mesh Presence ===\n";
                        std::cout << "Recommended Target: " << (rec_target.empty() ? "(none / local)" : rec_target) << "\n\n";
                        std::cout << std::format("{:<28} {:<12} {:<10} {:<16} {:<24}\n", "CLIENT ID", "USER", "PLATFORM", "STATUS", "LAST ACTIVE");
                        std::cout << std::string(90, '-') << "\n";
                        if (doc.contains("presences") && doc["presences"].is_array()) {
                            for (const auto& p : doc["presences"].get<std::vector<glz::json_t>>()) {
                                std::cout << std::format("{:<28} {:<12} {:<10} {:<16} {:<24}\n",
                                    p.contains("client_id") ? p["client_id"].get<std::string>() : "",
                                    p.contains("user") ? p["user"].get<std::string>() : "",
                                    p.contains("platform") ? p["platform"].get<std::string>() : "",
                                    p.contains("status") ? p["status"].get<std::string>() : "",
                                    p.contains("last_active_iso") ? p["last_active_iso"].get<std::string>() : "");
                            }
                        }
                        std::cout << "\n";
                        curl_global_cleanup();
                        return 0;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Rouen CLI] Local API presence query error: " << e.what() << "\n";
                }
            } else if (!notify_message.empty()) {
                try {
                    glz::json_t req;
                    req["message"] = notify_message;
                    if (!explicit_target.empty()) {
                        req["target"] = explicit_target;
                    }
                    req["speak"] = spoken;
                    std::string req_json;
                    (void)glz::write_json(req, req_json);

                    std::string res = api_client.post("http://127.0.0.1:8081/api/notify", req_json, {"Content-Type: application/json"});
                    if (api_client.last_http_code() == 200) {
                        std::cout << "[Rouen CLI] Notification successfully sent to running Rouen instance.\n";
                        curl_global_cleanup();
                        return 0;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Rouen CLI] Local API notify error: " << e.what() << ", attempting fallback...\n";
                }
            }
        }

        rouen::helpers::ConfigServiceInitializer::initialize();
        auto config_service = rouen::helpers::ConfigService::instance();
        config_service->load_env_file();

        auto& mesh = rouen::hosts::rouen_mesh_host::instance();
        mesh.initialize();
        auto mesh_cfg = mesh.get_config();
        rouen::services::presence_service::instance().set_headless(true);
        mg_log_set(MG_LL_NONE);
        mesh.start();

        std::cout << "[Rouen CLI] Connecting to Rouen Mesh..." << std::flush;
        auto start_wait = std::chrono::steady_clock::now();
        bool connected = false;
        while (std::chrono::steady_clock::now() - start_wait < std::chrono::milliseconds(5000)) {
            if (mesh.is_connected()) {
                connected = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (connected) {
            std::cout << " connected (" << mesh.get_status_message() << ").\n";
            mesh.refresh_registry();
            mesh.refresh_connected_clients();
            auto reg_wait = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - reg_wait < std::chrono::milliseconds(1500)) {
                auto entries = mesh.get_registry_entries("presence/");
                if (!entries.empty()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else {
            std::cout << " offline (mesh server unavailable).\n";
        }

        if (show_presence) {
            auto& ps = rouen::services::presence_service::instance();
            auto presences = ps.get_all_presences();
            std::string rec_target = ps.get_recommended_notification_target();
            std::cout << "\n=== Rouen Mesh Presence ===\n";
            std::cout << "Recommended Notification Target: " << (rec_target.empty() ? "(none / local)" : rec_target) << "\n\n";
            std::cout << std::format("{:<28} {:<12} {:<10} {:<16} {:<24}\n", "CLIENT ID", "USER", "PLATFORM", "STATUS", "LAST ACTIVE");
            std::cout << std::string(90, '-') << "\n";
            for (const auto& p : presences) {
                std::cout << std::format("{:<28} {:<12} {:<10} {:<16} {:<24}\n",
                                         p.client_id, p.user, p.platform, p.status, p.last_active_iso);
            }
            std::cout << "\n";
            mesh.stop();
            curl_global_cleanup();
            return 0;
        }

        // Route notification
        auto& ps = rouen::services::presence_service::instance();
        std::string target = explicit_target.empty() ? ps.get_recommended_notification_target() : explicit_target;
        bool is_remote_or_gui = connected && !target.empty() && target != mesh_cfg.client_id;

        if (is_remote_or_gui) {
            std::cout << std::format("[Rouen CLI] Routing notification to target '{}' (spoken: {})...\n", target, spoken ? "yes" : "no");
            bool sent = mesh.send_mesh_notification(target, notify_message, spoken);
            if (sent) {
                std::cout << std::format("[Rouen CLI] Successfully delivered notification to '{}'.\n", target);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            } else {
                std::cerr << "[Rouen CLI] Failed to deliver over mesh, falling back to local.\n";
                ps.route_notification(notify_message, ps.get_local_client_id(), spoken);
            }
        } else {
            std::cout << std::format("[Rouen CLI] Delivering notification locally (spoken: {})...\n", spoken ? "yes" : "no");
            ps.route_notification(notify_message, ps.get_local_client_id(), spoken);
        }

        mesh.stop();
        curl_global_cleanup();
        return 0;
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
    
    // Initialize the presence service and register in service registrar
    rouen::services::presence_service::instance().start();
    registrar::add<rouen::services::presence_service>("presence_service",
        std::shared_ptr<rouen::services::presence_service>(&rouen::services::presence_service::instance(), [](auto*){}));
    
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

    // Load plugins now that the ImGui context exists, but before the
    // deck (created at the top of window.run()) recreates any cards
    // persisted from a previous session - a persisted plugin card's
    // schema must already be registered by then.
    rouen::hosts::plugin_host::instance().load_all_plugins();

    // Run the main loop
    window.run();

    // Stop presence service
    rouen::services::presence_service::instance().stop();

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
