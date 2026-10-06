#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <optional>
#include <algorithm>
#include <regex>
#include "glaze_include.hpp"
#include "process_helper.hpp"
#include "platform_utils.hpp"

namespace rouen::helpers {

struct VSInstallation {
    std::string instance_id;
    std::string display_name;      // e.g. "Visual Studio Community 2026"
    std::string version;           // e.g. "18.9.12120.119"
    std::string installation_path; // e.g. "C:\Program Files\Microsoft Visual Studio\18\Community"
    std::string msbuild_path;      // e.g. "<path>\MSBuild\Current\Bin\amd64\MSBuild.exe"
    std::string vcvarsall_path;    // e.g. "<path>\VC\Auxiliary\Build\vcvarsall.bat"
    std::string devenv_path;       // e.g. "<path>\Common7\IDE\devenv.exe"
    std::string cl_path;           // e.g. "<path>\VC\Tools\MSVC\<ver>\bin\Hostx64\x64\cl.exe"
    std::string toolset{"v145"};   // e.g. "v145", "v143"

    struct glaze {
        using T = VSInstallation;
        static constexpr auto value = glz::object(
            "instance_id", &T::instance_id,
            "display_name", &T::display_name,
            "version", &T::version,
            "installation_path", &T::installation_path,
            "msbuild_path", &T::msbuild_path,
            "vcvarsall_path", &T::vcvarsall_path,
            "devenv_path", &T::devenv_path,
            "cl_path", &T::cl_path,
            "toolset", &T::toolset
        );
    };
};

struct VCProjectInfo {
    std::string project_name;
    std::string root_namespace;
    std::string platform_toolset{"v145"};
    std::string language_standard{"stdcpplatest"};
    std::vector<std::string> configurations; // e.g. ["Debug", "Release"]
    std::vector<std::string> platforms;      // e.g. ["x64", "Win32", "ARM64"]
    std::vector<std::string> source_files;   // ClCompile items
    std::vector<std::string> header_files;   // ClInclude items
    std::string target_name;
    std::string target_path;
    bool is_valid{false};
};

class VCToolchainService {
public:
    static VCToolchainService& instance() {
        static VCToolchainService s_instance;
        return s_instance;
    }

    /**
     * Query all installed Visual Studio versions using vswhere.exe and filesystem fallbacks.
     */
    std::vector<VSInstallation> get_installations(bool force_refresh = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!cached_installations_.empty() && !force_refresh) {
            return cached_installations_;
        }

        std::vector<VSInstallation> results;

        // 1. Locate vswhere.exe
        std::string vswhere_path = find_vswhere();
        if (!vswhere_path.empty()) {
            std::string cmd = std::format("\"{}\" -all -format json -products *", vswhere_path);
            std::string json_output = ProcessHelper::executeCommand(cmd);
            if (!json_output.empty()) {
                results = parse_vswhere_json(json_output);
            }
        }

        // 2. Filesystem probe fallbacks if vswhere produced no entries
        if (results.empty()) {
            results = probe_filesystem_installations();
        }

        // 3. For each detected installation, resolve full paths for tools
        for (auto& vs : results) {
            resolve_tools(vs);
        }

        cached_installations_ = results;
        return results;
    }

    /**
     * Get the default/preferred Visual Studio installation.
     */
    std::optional<VSInstallation> get_default_installation() {
        auto insts = get_installations();
        if (insts.empty()) return std::nullopt;
        return insts.front();
    }

    /**
     * Parse .vcxproj XML content to extract basic project metadata, configurations, and sources.
     */
    static VCProjectInfo parse_project_file(const std::string& vcxproj_path) {
        VCProjectInfo info;
        std::ifstream file(vcxproj_path);
        if (!file.is_open()) {
            return info;
        }

        info.is_valid = true;
        std::filesystem::path p(vcxproj_path);
        info.project_name = p.stem().string();

        std::string line;
        static const std::regex root_ns_regex(R"(<RootNamespace>(.*?)</RootNamespace>)", std::regex::optimize);
        static const std::regex toolset_regex(R"(<PlatformToolset>(.*?)</PlatformToolset>)", std::regex::optimize);
        static const std::regex lang_std_regex(R"(<LanguageStandard>(.*?)</LanguageStandard>)", std::regex::optimize);
        static const std::regex proj_config_regex(R"(<ProjectConfiguration\s+Include=\"(.*?)\|(.*?)\">)", std::regex::optimize);
        static const std::regex cl_compile_regex(R"(<ClCompile\s+Include=\"(.*?)\")", std::regex::optimize);
        static const std::regex cl_include_regex(R"(<ClInclude\s+Include=\"(.*?)\")", std::regex::optimize);

        while (std::getline(file, line)) {
            std::smatch match;
            if (std::regex_search(line, match, root_ns_regex)) {
                info.root_namespace = match[1].str();
                if (!info.root_namespace.empty()) {
                    info.project_name = info.root_namespace;
                }
            } else if (std::regex_search(line, match, toolset_regex)) {
                info.platform_toolset = match[1].str();
            } else if (std::regex_search(line, match, lang_std_regex)) {
                info.language_standard = match[1].str();
            } else if (std::regex_search(line, match, proj_config_regex)) {
                std::string cfg = match[1].str();
                std::string plat = match[2].str();
                if (std::find(info.configurations.begin(), info.configurations.end(), cfg) == info.configurations.end()) {
                    info.configurations.push_back(cfg);
                }
                if (std::find(info.platforms.begin(), info.platforms.end(), plat) == info.platforms.end()) {
                    info.platforms.push_back(plat);
                }
            } else if (std::regex_search(line, match, cl_compile_regex)) {
                std::string src = match[1].str();
                if (std::find(info.source_files.begin(), info.source_files.end(), src) == info.source_files.end()) {
                    info.source_files.push_back(src);
                }
            } else if (std::regex_search(line, match, cl_include_regex)) {
                std::string hdr = match[1].str();
                if (std::find(info.header_files.begin(), info.header_files.end(), hdr) == info.header_files.end()) {
                    info.header_files.push_back(hdr);
                }
            }
        }

        // Set sensible defaults if not present
        if (info.configurations.empty()) {
            info.configurations = {"Release", "Debug"};
        }
        if (info.platforms.empty()) {
            info.platforms = {"x64", "ARM64", "Win32"};
        }

        return info;
    }

    /**
     * Query detailed MSBuild properties (TargetName, TargetPath) via MSBuild command line.
     */
    static void enrich_project_info_with_msbuild(VCProjectInfo& info, const std::string& vcxproj_path, 
                                                 const VSInstallation& vs, const std::string& config, const std::string& platform) {
        if (vs.msbuild_path.empty()) return;

        std::string win_proj = std::filesystem::path(vcxproj_path).make_preferred().string();
        std::string cmd = std::format("\"{}\" \"{}\" /getProperty:TargetName /getProperty:TargetPath /p:Configuration={} /p:Platform={} /nologo",
            vs.msbuild_path, win_proj, config, platform);

        std::string output = ProcessHelper::executeCommand(cmd);
        if (output.empty()) return;

        // Parse JSON output from /getProperty
        static const std::regex target_name_regex(R"(\"TargetName\":\s*\"(.*?)\")");
        static const std::regex target_path_regex(R"(\"TargetPath\":\s*\"(.*?)\")");
        std::smatch match;
        if (std::regex_search(output, match, target_name_regex)) {
            info.target_name = match[1].str();
        }
        if (std::regex_search(output, match, target_path_regex)) {
            std::string tp = match[1].str();
            // unescape double slashes
            std::string unescaped;
            for (size_t i = 0; i < tp.size(); ++i) {
                if (tp[i] == '\\' && i + 1 < tp.size() && tp[i + 1] == '\\') {
                    unescaped += '\\';
                    ++i;
                } else {
                    unescaped += tp[i];
                }
            }
            info.target_path = unescaped;
        }
    }

    /**
     * Build the MSBuild command line for a specified action.
     */
    static std::string build_command(const VSInstallation& vs, const std::string& vcxproj_path, 
                                     const std::string& action, const std::string& config, 
                                     const std::string& platform, const std::string& target_file = "") {
        std::string msbuild = vs.msbuild_path.empty() ? "MSBuild.exe" : vs.msbuild_path;
        std::string proj = std::filesystem::path(vcxproj_path).make_preferred().string();

        if (action == "build") {
            return std::format("\"{}\" \"{}\" /t:Build /p:Configuration={} /p:Platform={} /m:2",
                msbuild, proj, config, platform);
        } else if (action == "rebuild") {
            return std::format("\"{}\" \"{}\" /t:Rebuild /p:Configuration={} /p:Platform={} /m:2",
                msbuild, proj, config, platform);
        } else if (action == "clean") {
            return std::format("\"{}\" \"{}\" /t:Clean /p:Configuration={} /p:Platform={} /m:2",
                msbuild, proj, config, platform);
        } else if (action == "check_syntax") {
            if (!target_file.empty()) {
                std::string sel = std::filesystem::path(target_file).filename().string();
                return std::format("\"{}\" \"{}\" /t:ClCompile /p:SelectedFiles=\"{}\" /p:Configuration={} /p:Platform={} /m:2",
                    msbuild, proj, sel, config, platform);
            } else {
                return std::format("\"{}\" \"{}\" /t:ClCompile /p:Configuration={} /p:Platform={} /m:2",
                    msbuild, proj, config, platform);
            }
        }
        return "";
    }

private:
    VCToolchainService() = default;

    std::mutex mutex_;
    std::vector<VSInstallation> cached_installations_;

    static std::string find_vswhere() {
        std::vector<std::string> candidates;

        if (const char* pf86 = std::getenv("ProgramFiles(x86)")) {
            candidates.push_back((std::filesystem::path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe").string());
        }
        if (const char* pf = std::getenv("ProgramFiles")) {
            candidates.push_back((std::filesystem::path(pf) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe").string());
        }

        candidates.push_back("C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe");
        candidates.push_back("C:\\Program Files\\Microsoft Visual Studio\\Installer\\vswhere.exe");

        for (const auto& c : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(c, ec)) {
                return c;
            }
        }
        return "";
    }

    static std::vector<VSInstallation> parse_vswhere_json(const std::string& json_str) {
        std::vector<VSInstallation> list;

        // Use regex parser for resilience against diverse vswhere versions and extra properties
        static const std::regex item_regex(R"(\{\s*\"instanceId\":\s*\"([^\"]+)\"[^}]*\"installationPath\":\s*\"([^\"]+)\"[^}]*\"installationVersion\":\s*\"([^\"]+)\"[^}]*\"displayName\":\s*\"([^\"]+)\")");

        // Also fallback simpler regex scanning blocks
        std::string content = json_str;
        size_t pos = 0;
        while ((pos = content.find("\"instanceId\"", pos)) != std::string::npos) {
            size_t block_start = content.rfind('{', pos);
            size_t block_end = content.find('}', pos);
            if (block_start != std::string::npos && block_end != std::string::npos) {
                std::string block = content.substr(block_start, block_end - block_start + 1);

                VSInstallation inst;
                inst.instance_id = extract_json_value(block, "instanceId");
                inst.installation_path = extract_json_value(block, "installationPath");
                inst.version = extract_json_value(block, "installationVersion");
                inst.display_name = extract_json_value(block, "displayName");

                // Unescape backslashes in installation path
                std::string clean_path;
                for (size_t i = 0; i < inst.installation_path.size(); ++i) {
                    if (inst.installation_path[i] == '\\' && i + 1 < inst.installation_path.size() && inst.installation_path[i + 1] == '\\') {
                        clean_path += '\\';
                        ++i;
                    } else {
                        clean_path += inst.installation_path[i];
                    }
                }
                inst.installation_path = clean_path;

                if (!inst.installation_path.empty()) {
                    list.push_back(inst);
                }
                pos = block_end + 1;
            } else {
                break;
            }
        }

        return list;
    }

    static std::string extract_json_value(const std::string& block, const std::string& key) {
        std::string pattern = std::format("\"{}\":\\s*\"([^\"]*)\"", key);
        std::regex re(pattern);
        std::smatch m;
        if (std::regex_search(block, m, re)) {
            return m[1].str();
        }
        return "";
    }

    static std::vector<VSInstallation> probe_filesystem_installations() {
        std::vector<VSInstallation> list;
        std::vector<std::pair<std::string, std::string>> candidates = {
            {"C:\\Program Files\\Microsoft Visual Studio\\18\\Community", "Visual Studio Community 2026"},
            {"C:\\Program Files\\Microsoft Visual Studio\\18\\Professional", "Visual Studio Professional 2026"},
            {"C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise", "Visual Studio Enterprise 2026"},
            {"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community", "Visual Studio Community 2022"},
            {"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional", "Visual Studio Professional 2022"},
            {"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise", "Visual Studio Enterprise 2022"},
            {"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community", "Visual Studio Community 2019"}
        };

        for (const auto& [path, name] : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                VSInstallation inst;
                inst.installation_path = path;
                inst.display_name = name;
                inst.instance_id = std::filesystem::path(path).filename().string();
                list.push_back(inst);
            }
        }
        return list;
    }

    static void resolve_tools(VSInstallation& vs) {
        std::filesystem::path root(vs.installation_path);
        std::error_code ec;

        // 1. MSBuild
        std::vector<std::filesystem::path> msbuild_candidates = {
            root / "MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe",
            root / "MSBuild" / "Current" / "Bin" / "MSBuild.exe",
            root / "MSBuild" / "15.0" / "Bin" / "amd64" / "MSBuild.exe",
            root / "MSBuild" / "15.0" / "Bin" / "MSBuild.exe"
        };
        for (const auto& m : msbuild_candidates) {
            if (std::filesystem::exists(m, ec)) {
                vs.msbuild_path = m.string();
                break;
            }
        }

        // 2. vcvarsall.bat
        std::filesystem::path vcvars = root / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat";
        if (std::filesystem::exists(vcvars, ec)) {
            vs.vcvarsall_path = vcvars.string();
        }

        // 3. devenv.exe
        std::filesystem::path devenv = root / "Common7" / "IDE" / "devenv.exe";
        if (std::filesystem::exists(devenv, ec)) {
            vs.devenv_path = devenv.string();
        }

        // 4. cl.exe & toolset deduction
        std::filesystem::path msvc_root = root / "VC" / "Tools" / "MSVC";
        if (std::filesystem::exists(msvc_root, ec)) {
            std::vector<std::string> versions;
            for (const auto& entry : std::filesystem::directory_iterator(msvc_root, ec)) {
                if (entry.is_directory()) {
                    versions.push_back(entry.path().filename().string());
                }
            }
            std::sort(versions.rbegin(), versions.rend()); // latest first
            if (!versions.empty()) {
                std::string latest_ver = versions.front();
                std::filesystem::path cl_path = msvc_root / latest_ver / "bin" / "Hostx64" / "x64" / "cl.exe";
                if (std::filesystem::exists(cl_path, ec)) {
                    vs.cl_path = cl_path.string();
                }

                // Deduce toolset from version
                if (latest_ver.starts_with("14.5")) {
                    vs.toolset = "v145";
                } else if (latest_ver.starts_with("14.4") || latest_ver.starts_with("14.3")) {
                    vs.toolset = "v143";
                } else if (latest_ver.starts_with("14.2")) {
                    vs.toolset = "v142";
                }
            }
        }
    }
};

} // namespace rouen::helpers
