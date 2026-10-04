#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>

#include "ytdlp_service.hpp"
#include "process_helper.hpp"
#include "config_service.hpp"
#include "platform_utils.hpp"
#include "glaze_include.hpp"

namespace rouen::helpers {

std::string ytdlp_service::find_executable() {
    return rouen::platform::find_executable("yt-dlp");
}

bool ytdlp_service::supports_remote_components() {
    std::string exe = find_executable();
    if (exe.empty()) return false;
    return ProcessHelper::ytdlp_supports_remote_components(exe);
}

std::string ytdlp_service::build_format_spec(std::string_view pref_quality) {
    std::string q_str(pref_quality);
    std::transform(q_str.begin(), q_str.end(), q_str.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    int target_max_h = 360;
    if (q_str == "4k" || q_str == "2160p") {
        target_max_h = 2160;
    } else if (q_str == "1440p") {
        target_max_h = 1440;
    } else if (q_str == "1080p") {
        target_max_h = 1080;
    } else if (q_str == "720p") {
        target_max_h = 720;
    } else {
        target_max_h = 360;
    }

    return std::format(
        "bestvideo[protocol^=http][height<={0}]+bestaudio[protocol^=http]/bestvideo[height<={0}]+bestaudio/bestvideo[width<={0}]+bestaudio/best[height<={0}][vcodec!=none]/best[width<={0}][vcodec!=none]/bestvideo+bestaudio/best[vcodec!=none]",
        target_max_h
    );
}

ytdlp_stream_result ytdlp_service::resolve_stream_urls(
    const std::string& norm_url,
    std::string_view pref_quality
) {
    std::string ytdl_exe = find_executable();
    if (ytdl_exe.empty()) {
        return {{}, "yt-dlp executable not found", false};
    }

    auto config = ConfigService::instance();
    std::string const initial_cookie_args = config ? config->get_ytdlp_cookie_args() : "";
    std::string const format_spec = build_format_spec(pref_quality);

    auto is_url_accessible = [](std::string_view stream_url) -> bool {
        if (stream_url.empty()) return false;
        std::string const probe_cmd = "curl -s -L --max-time 5 -r 0-100 -o /dev/null -w \"%{http_code}\" -H \"User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36\" \"" + std::string(stream_url) + "\" 2>&1";
        std::string const status = ProcessHelper::executeCommand(probe_cmd);
        if (status.find("403") != std::string::npos || status.find("401") != std::string::npos || status.find("429") != std::string::npos) {
            return false;
        }
        if (status.find("200") != std::string::npos || status.find("206") != std::string::npos) {
            return true;
        }
        return !status.empty() && status.find("40") == std::string::npos && status.find("50") == std::string::npos;
    };

    auto run_ytdlp_cmd = [&ytdl_exe, &format_spec, &norm_url](std::string_view cookie_args, std::string_view extra_extractor_args = "", std::string_view custom_format = "") -> std::pair<std::vector<std::string>, std::string> {
        std::string_view const target_fmt = custom_format.empty() ? std::string_view(format_spec) : custom_format;
        std::string cmd;
        std::string remote_flag = ProcessHelper::ytdlp_supports_remote_components(ytdl_exe) ? "--remote-components ejs:github " : "";
        std::string ext_flag = extra_extractor_args.empty() ? "" : (std::string(extra_extractor_args) + " ");
        std::string cook_flag = cookie_args.empty() ? "" : (std::string(cookie_args) + " ");
        std::string ua_flag;
        if constexpr (rouen::platform::is_apple) {
            if (cookie_args.find("safari") != std::string::npos) {
                ua_flag = "--user-agent \"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15\" ";
            } else if (!cookie_args.empty()) {
                ua_flag = "--user-agent \"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36\" ";
            }
        }
        cmd = std::format("\"{}\" --no-warnings {}{}{}{}-g -f \"{}\" \"{}\" 2>&1", ytdl_exe, remote_flag, ua_flag, ext_flag, cook_flag, target_fmt, norm_url);
        std::cerr << "[ytdlp_service Diagnostics] Executing command: " << cmd << '\n';
        std::string const output = ProcessHelper::executeCommand(cmd);
        std::stringstream ss(output);
        std::string line;
        std::vector<std::string> parsed_urls;
        while (std::getline(ss, line)) {
            line.erase(line.find_last_not_of(" \r\n\t") + 1);
            if (line.starts_with("http://") || line.starts_with("https://")) {
                parsed_urls.push_back(line);
            }
        }
        std::cerr << "[ytdlp_service Diagnostics] Command returned " << parsed_urls.size() << " URLs. Output snippet: " 
                  << (output.length() > 300 ? output.substr(0, 300) + "..." : output) << '\n';
        return {parsed_urls, output};
    };

    auto urls_are_valid_video = [&is_url_accessible](const std::vector<std::string>& test_urls) -> bool {
        if (test_urls.empty()) return false;

        bool has_audio_only = false;
        bool has_video = false;
        for (const auto& u : test_urls) {
            if (u.find("mime=video") != std::string::npos || u.find("mime%3Dvideo") != std::string::npos ||
                u.find(".mpd") != std::string::npos || u.find(".mp4") != std::string::npos) {
                has_video = true;
            }
            if (u.find("mime=audio") != std::string::npos || u.find("mime%3Daudio") != std::string::npos || u.find(".m4a") != std::string::npos) {
                has_audio_only = true;
            }
        }

        // An HLS playlist (.m3u8) paired with a separate WebM audio stream is a known YouTube unauthenticated
        // throttling artifact (DVR fMP4 chunks without complete moov/init boxes). Only accept .m3u8 if it's the sole URL.
        if (!has_video) {
            for (const auto& u : test_urls) {
                if (u.find(".m3u8") != std::string::npos) {
                    if (test_urls.size() == 1) {
                        has_video = true;
                    }
                    break;
                }
            }
        }

        // If there are multiple URLs (e.g. separate video + audio streams from yt-dlp bestvideo+bestaudio),
        // as long as at least one URL is not strictly audio or HLS fragment fallback, consider it a valid video stream set.
        if (!has_video && test_urls.size() >= 2) {
            for (const auto& u : test_urls) {
                if (u.find("mime=audio") == std::string::npos && u.find("mime%3Daudio") == std::string::npos && u.find(".m3u8") == std::string::npos) {
                    has_video = true;
                    break;
                }
            }
        }

        // Single URL that is audio-only is not a video stream
        if (test_urls.size() == 1 && has_audio_only && !has_video) {
            return false;
        }

        // Must contain at least one valid video stream
        if (!has_video) {
            return false;
        }

        for (const auto& u : test_urls) {
            if (!is_url_accessible(u)) {
                return false;
            }
        }
        return true;
    };

    std::cerr << "[ytdlp_service Diagnostics] Resolving URL: " << norm_url << " (initial_cookie_args: '" << initial_cookie_args << "')\n";
    std::vector<std::string> urls;
    std::string resolved;

    if (!initial_cookie_args.empty()) {
        std::cerr << "[ytdlp_service Diagnostics] Attempting resolution with configured cookie args...\n";
        std::tie(urls, resolved) = run_ytdlp_cmd(initial_cookie_args);
        if (!urls.empty() && !urls_are_valid_video(urls)) {
            std::cerr << "[ytdlp_service Diagnostics] Configured cookie attempt URLs failed validation. Clearing...\n";
            urls.clear();
        }
    }

    if (urls.empty()) {
        std::cerr << "[ytdlp_service Diagnostics] Attempting un-cookied resolution...\n";
        std::tie(urls, resolved) = run_ytdlp_cmd("");
        if (!urls.empty() && !urls_are_valid_video(urls)) {
            std::cerr << "[ytdlp_service Diagnostics] Un-cookied URLs failed validation. Clearing...\n";
            urls.clear();
        }
    }

    bool const is_auth_err = (resolved.find("Sign in to confirm") != std::string::npos ||
                              resolved.find("YouTube requires cookies") != std::string::npos ||
                              resolved.find("cookies are no longer valid") != std::string::npos ||
                              resolved.find("bot") != std::string::npos ||
                              resolved.find("HTTP Error 429") != std::string::npos);

    // Auto-healing Pass 1: Refresh cookies
    if (urls.empty()) {
        std::cerr << "[ytdlp_service Diagnostics] Pass 1: Initial attempt returned no valid URLs (auth_err=" << is_auth_err << "). Refreshing cookies...\n";
        if (config) {
            config->clear_youtube_cookies();
            if (config->refresh_youtube_cookies()) {
                std::string const fresh_cookie_args = config->get_ytdlp_cookie_args();
                std::cerr << "[ytdlp_service Diagnostics] Pass 1: Refreshed cookies args: '" << fresh_cookie_args << "'. Retrying...\n";
                auto [ref_urls, ref_output] = run_ytdlp_cmd(fresh_cookie_args);
                resolved = ref_output;
                if (!ref_urls.empty() && urls_are_valid_video(ref_urls)) {
                    urls = ref_urls;
                }
            }
        }
    }

    // Auto-healing Pass 2: Direct browser cookies extraction
    if (urls.empty()) {
        std::cerr << "[ytdlp_service Diagnostics] Pass 2: Trying direct extraction across installed browsers...\n";
        static const std::vector<std::string_view> candidate_browsers = {"safari", "chrome", "firefox", "brave", "edge", "vivaldi", "opera", "chromium"};
        const char* home = getenv("HOME");
        std::string const home_dir = home ? home : "";

        for (const auto& browser : candidate_browsers) {
            bool can_attempt = true;
            if constexpr (rouen::platform::is_apple) {
                if (browser == "safari") {
                    std::string const safari_cookies = home_dir + "/Library/Containers/com.apple.Safari/Data/Library/Cookies/Cookies.binarycookies";
                    FILE* f = fopen(safari_cookies.c_str(), "rb");
                    if (f) fclose(f);
                    else can_attempt = false;
                } else if (browser == "chrome") {
                    std::string const chrome_cookies = home_dir + "/Library/Application Support/Google/Chrome/Default/Cookies";
                    FILE* f = fopen(chrome_cookies.c_str(), "rb");
                    if (f) fclose(f);
                    else can_attempt = false;
                } else if (browser == "firefox") {
                    can_attempt = std::filesystem::exists(home_dir + "/Library/Application Support/Firefox/Profiles");
                } else if (browser == "brave") {
                    can_attempt = std::filesystem::exists(home_dir + "/Library/Application Support/BraveSoftware/Brave-Browser");
                } else if (browser == "edge") {
                    can_attempt = std::filesystem::exists(home_dir + "/Library/Application Support/Microsoft Edge");
                } else if (browser == "vivaldi") {
                    can_attempt = std::filesystem::exists(home_dir + "/Library/Application Support/Vivaldi");
                } else if (browser == "opera") {
                    can_attempt = std::filesystem::exists(home_dir + "/Library/Application Support/com.operasoftware.Opera");
                } else if (browser == "chromium") {
                    can_attempt = std::filesystem::exists(home_dir + "/Library/Application Support/Chromium");
                }
            }
            if (!can_attempt) continue;

            std::cerr << "[ytdlp_service Diagnostics] Pass 2: Trying browser: " << browser << '\n';
            std::string const fallback_args = std::format("--cookies-from-browser {}", browser);
            auto [fb_urls, fb_output] = run_ytdlp_cmd(fallback_args);
            if (!fb_urls.empty() && !urls_are_valid_video(fb_urls)) fb_urls.clear();

            if (fb_urls.empty()) {
                std::tie(fb_urls, fb_output) = run_ytdlp_cmd(fallback_args, "--extractor-args \"youtube:player_client=ios,android\"");
                if (!fb_urls.empty() && !urls_are_valid_video(fb_urls)) fb_urls.clear();
            }
            if (fb_urls.empty()) {
                std::tie(fb_urls, fb_output) = run_ytdlp_cmd(fallback_args, "--extractor-args \"youtube:player_client=ios,android\"", "bestvideo+bestaudio/best[vcodec!=none]");
                if (!fb_urls.empty() && !urls_are_valid_video(fb_urls)) fb_urls.clear();
            }
            resolved = fb_output;
            if (!fb_urls.empty()) {
                urls = fb_urls;
                if (config) {
                    config->set_env_value("ROUEN_COOKIES_BROWSER", std::string(browser), true);
                }
                if (!home_dir.empty()) {
                    std::string const save_cmd = std::format("\"{}\" --no-warnings --cookies-from-browser {} --cookies \"{}/.config/rouen/cookies.txt\" --skip-download --playlist-items 0 \"https://www.youtube.com\" 2>&1", ytdl_exe, browser, home_dir);
                    ProcessHelper::executeCommand(save_cmd);
                }
                std::cerr << "[ytdlp_service Diagnostics] Auto-healed: resolved YouTube URL using cookies from browser: " << browser << '\n';
                break;
            }
        }
    }

    // Auto-healing Pass 3: Multi-client fallback without cookies
    if (urls.empty()) {
        std::cerr << "[ytdlp_service Diagnostics] Pass 3: Trying client specs without cookies...\n";
        static const std::vector<std::string_view> client_specs = {
            "--extractor-args \"youtube:player_client=ios,android\"",
            "--extractor-args \"youtube:player_client=mweb,android\"",
            "--extractor-args \"youtube:player_client=tv_embedded,android\"",
            "--extractor-args \"youtube:player_client=android\"",
            "--extractor-args \"youtube:player_client=web\""
        };
        for (const auto& cspec : client_specs) {
            std::cerr << "[ytdlp_service Diagnostics] Pass 3: Trying cspec: " << cspec << '\n';
            auto [fb_urls, fb_output] = run_ytdlp_cmd("--no-cookies", cspec);
            if (!fb_urls.empty() && !urls_are_valid_video(fb_urls)) fb_urls.clear();

            if (fb_urls.empty()) {
                std::tie(fb_urls, fb_output) = run_ytdlp_cmd("--no-cookies", cspec, "bestvideo+bestaudio/best[vcodec!=none]");
                if (!fb_urls.empty() && !urls_are_valid_video(fb_urls)) fb_urls.clear();
            }
            if (fb_urls.empty()) {
                std::tie(fb_urls, fb_output) = run_ytdlp_cmd("--no-cookies", cspec, "best[vcodec!=none]");
                if (!fb_urls.empty() && !urls_are_valid_video(fb_urls)) fb_urls.clear();
            }
            resolved = fb_output;
            if (!fb_urls.empty()) {
                urls = fb_urls;
                std::cerr << "[ytdlp_service Diagnostics] Auto-healed: resolved YouTube URL using client spec: " << cspec << '\n';
                break;
            }
        }
    }

    return {urls, resolved, !urls.empty()};
}

std::vector<ytdlp_search_result> ytdlp_service::search(
    const std::string& query,
    int max_results
) {
    std::string ytdlp_path = find_executable();
    if (ytdlp_path.empty()) return {};

    std::string escaped_query;
    for (char c : query) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            escaped_query += '\\';
        }
        escaped_query += c;
    }

    auto config = ConfigService::instance();
    std::string cookie_args = config ? config->get_ytdlp_cookie_args() : "";
    std::string cflags = cookie_args.empty() ? "" : (" " + cookie_args);
    std::string remote_flag = supports_remote_components() ? "--remote-components ejs:github " : "";
    std::string cmd = std::format("\"{}\" --no-warnings --no-call-home {}--socket-timeout 10{} --flat-playlist --extractor-args \"youtubetab:approximate_date\" --dump-json \"ytsearch{}:{}\"", ytdlp_path, remote_flag, cflags, max_results, escaped_query);

    std::string output = ProcessHelper::executeCommand(cmd);
    std::stringstream ss(output);
    std::string line;
    std::vector<ytdlp_search_result> results;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        try {
            glz::json_t resp;
            auto ec = glz::read_json(resp, line);
            if (!ec) {
                ytdlp_search_result res;
                if (resp.contains("id") && resp["id"].is_string()) {
                    res.id = resp["id"].get<std::string>();
                }
                if (resp.contains("title") && resp["title"].is_string()) {
                    res.title = resp["title"].get<std::string>();
                }
                if (!res.id.empty() && res.id.length() == 11 && !res.id.starts_with("UC")) {
                    res.url = "https://www.youtube.com/watch?v=" + res.id;
                } else if (resp.contains("url") && resp["url"].is_string()) {
                    res.url = resp["url"].get<std::string>();
                } else if (!res.id.empty()) {
                    res.url = "https://www.youtube.com/watch?v=" + res.id;
                }
                if (resp.contains("duration") && resp["duration"].is_number()) {
                    res.duration = resp["duration"].get<double>();
                }
                if (resp.contains("duration_string") && resp["duration_string"].is_string()) {
                    res.duration_string = resp["duration_string"].get<std::string>();
                }
                if (resp.contains("channel") && resp["channel"].is_string()) {
                    res.channel = resp["channel"].get<std::string>();
                } else if (resp.contains("uploader") && resp["uploader"].is_string()) {
                    res.channel = resp["uploader"].get<std::string>();
                }
                if (resp.contains("channel_id") && resp["channel_id"].is_string()) {
                    res.channel_id = resp["channel_id"].get<std::string>();
                }
                if (resp.contains("upload_date") && resp["upload_date"].is_string()) {
                    res.upload_date = resp["upload_date"].get<std::string>();
                }
                if (resp.contains("view_count") && resp["view_count"].is_number()) {
                    res.view_count = static_cast<long long>(resp["view_count"].get<double>());
                }
                if (!res.url.empty()) {
                    results.push_back(res);
                }
            }
        } catch (...) {}
    }
    return results;
}

std::filesystem::path ytdlp_service::fetch_subtitles(
    const std::string& video_url,
    const std::string& out_prefix,
    std::string_view custom_cookie_args
) {
    std::string ytdlp_path = find_executable();
    if (ytdlp_path.empty()) return {};

    auto config = ConfigService::instance();

    auto fetch_sub_file = [&ytdlp_path, &out_prefix, &video_url](std::string_view cargs, std::string_view extra_ext_args = "") -> std::filesystem::path {
        std::string extra_flags = cargs.empty() ? "" : (" " + std::string(cargs));
        std::string ext_flags = extra_ext_args.empty() ? "" : (" " + std::string(extra_ext_args));
        std::string remote_flag = ProcessHelper::ytdlp_supports_remote_components(ytdlp_path) ? "--remote-components ejs:github" : "";
        std::string const cmd = std::format("\"{}\" -q --no-warnings {}{}{} --skip-download --write-sub --write-auto-sub "
                                      "--sub-lang \"en,es,en-US,en-GB,es-419,es-ES,.*\" --sub-format srt -o \"{}.%(ext)s\" \"{}\"",
                                      ytdlp_path, remote_flag, extra_flags, ext_flags, out_prefix, video_url);
        ProcessHelper::executeCommand(cmd);
        try {
            for (const auto& entry : std::filesystem::directory_iterator("/tmp")) {
                std::string const fname = entry.path().string();
                if (fname.starts_with(out_prefix)) {
                    return entry.path();
                }
            }
        } catch (...) {}
        return {};
    };

    std::string initial_cargs = custom_cookie_args.empty() ? (config ? config->get_ytdlp_cookie_args() : "") : std::string(custom_cookie_args);
    std::filesystem::path found_file = fetch_sub_file(initial_cargs);

    if (found_file.empty()) {
        std::string const fallback_args = config ? config->get_ytdlp_cookie_args() : "";
        if (!fallback_args.empty() && fallback_args != initial_cargs) {
            found_file = fetch_sub_file(fallback_args);
        }
    }

    if (found_file.empty()) {
        static const std::vector<std::string_view> candidate_browsers = {"safari", "chrome", "firefox", "brave", "edge", "vivaldi", "opera", "chromium"};
        for (const auto& browser : candidate_browsers) {
            std::string const fb_args = std::format("--cookies-from-browser {}", browser);
            found_file = fetch_sub_file(fb_args);
            if (found_file.empty()) {
                found_file = fetch_sub_file(fb_args, "--extractor-args \"youtube:player_client=android_vr,android,tv\"");
            }
            if (!found_file.empty()) {
                if (config) {
                    config->set_env_value("ROUEN_COOKIES_BROWSER", std::string(browser), true);
                }
                const char* home = getenv("HOME");
                if (home) {
                    std::string const save_cmd = std::format("\"{}\" -q --no-warnings --cookies-from-browser {} --cookies \"{}/.config/rouen/cookies.txt\" --skip-download --playlist-items 0 \"https://www.youtube.com\" 2>&1", ytdlp_path, browser, home);
                    ProcessHelper::executeCommand(save_cmd);
                }
                break;
            }
        }
    }

    if (found_file.empty()) {
        static const std::vector<std::string_view> client_specs = {
            "--extractor-args \"youtube:player_client=android_vr,android,tv\"",
            "--extractor-args \"youtube:player_client=android,tv\"",
            "--extractor-args \"youtube:player_client=tv_embedded,android\""
        };
        for (const auto& cspec : client_specs) {
            found_file = fetch_sub_file("--no-cookies", cspec);
            if (!found_file.empty()) break;
        }
    }

    return found_file;
}

} // namespace rouen::helpers
