#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <functional>
#include <optional>
#include "glaze_include.hpp"
#include "platform_utils.hpp"
#include "../registrar.hpp"

namespace rouen::helpers {

    struct Persona {
        std::string name;
        std::string description;
        std::vector<std::string> allowed_mcps;
        std::string system_prompt;
        std::string llm_config_name;
        bool enable_search{false};
        std::vector<std::string> allowed_personas;
        float temperature{0.7f};
        std::string thinking_level{};
        int max_tool_iterations{10};
        int max_output_tokens{8192};

        [[nodiscard]] std::string get_effective_thinking_level() const {
            if (!thinking_level.empty()) {
                return thinking_level;
            }
            if (temperature <= 0.15f) return "minimal";
            if (temperature <= 0.35f) return "low";
            if (temperature >= 0.7f) return "high";
            return "medium";
        }

        struct glaze {
            using T = Persona;
            static constexpr auto value = glz::object(
                "name", &T::name,
                "description", &T::description,
                "allowed_mcps", &T::allowed_mcps,
                "system_prompt", &T::system_prompt,
                "llm_config_name", &T::llm_config_name,
                "enable_search", &T::enable_search,
                "allowed_personas", &T::allowed_personas,
                "temperature", &T::temperature,
                "thinking_level", &T::thinking_level,
                "max_tool_iterations", &T::max_tool_iterations,
                "max_output_tokens", &T::max_output_tokens
            );
        };
    };

    struct PersonaSaveModel {
        size_t active_index{0};
        std::vector<Persona> personas;

        struct glaze {
            using T = PersonaSaveModel;
            static constexpr auto value = glz::object(
                "active_index", &T::active_index,
                "personas", &T::personas
            );
        };
    };

    struct PersonaActiveMeta {
        std::string active_persona;
        size_t active_index{0};

        struct glaze {
            using T = PersonaActiveMeta;
            static constexpr auto value = glz::object(
                "active_persona", &T::active_persona,
                "active_index", &T::active_index
            );
        };
    };

    class PersonaManager {
    public:
        using sync_hook_t = std::function<void(std::string_view dataset, std::string_view key, std::string_view content, bool is_deleted)>;

        static PersonaManager& instance() {
            static PersonaManager mgr;
            return mgr;
        }

        static std::string slugify(std::string_view text) {
            std::string slug;
            slug.reserve(text.size());

            bool last_dash = false;
            for (char c : text) {
                if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
                    slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    last_dash = false;
                } else if (!last_dash) {
                    slug.push_back('-');
                    last_dash = true;
                }
            }

            while (!slug.empty() && slug.front() == '-') {
                slug.erase(slug.begin());
            }
            while (!slug.empty() && slug.back() == '-') {
                slug.pop_back();
            }

            return slug.empty() ? "persona" : slug;
        }

        void set_sync_hook(sync_hook_t hook) {
            sync_hook_ = std::move(hook);
        }

        const std::vector<Persona>& get_personas() const {
            return personas_;
        }

        size_t get_active_persona_index() const {
            return active_persona_index_;
        }

        const Persona& get_active_persona() const {
            if (active_persona_index_ < personas_.size()) {
                return personas_[active_persona_index_];
            }
            static Persona fallback{"Default Assistant", "Fallback persona", {"terminal", "editor", "deck", "adaptive_card", "wikipedia", "youtube", "git", "calendar", "weather", "alarm", "pomodoro", "notes", "contacts"}, "You are a helpful assistant.", "Default", false, {}, 0.7f, "high", 10, 8192};
            return fallback;
        }

        void select_persona(size_t index) {
            if (index < personas_.size()) {
                active_persona_index_ = index;
                save_personas();
                sync_active_persona();
            }
        }

        [[nodiscard]] std::optional<size_t> find_persona_index(std::string_view name) const {
            for (size_t i = 0; i < personas_.size(); ++i) {
                if (personas_[i].name == name) {
                    return i;
                }
            }
            std::string lower_target;
            lower_target.reserve(name.size());
            for (char c : name) lower_target.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

            for (size_t i = 0; i < personas_.size(); ++i) {
                std::string lower_p;
                lower_p.reserve(personas_[i].name.size());
                for (char c : personas_[i].name) lower_p.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lower_p == lower_target) {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] const Persona* get_persona_by_name(std::string_view name) const {
            auto idx = find_persona_index(name);
            if (idx.has_value() && *idx < personas_.size()) {
                return &personas_[*idx];
            }
            return nullptr;
        }

        bool select_persona_by_name(std::string_view name) {
            auto idx = find_persona_index(name);
            if (idx.has_value()) {
                select_persona(*idx);
                return true;
            }
            return false;
        }

        void add_persona(const Persona& persona) {
            personas_.push_back(persona);
            save_personas();
            sync_persona_item(persona, false);
        }

        void update_persona(size_t index, const Persona& persona) {
            if (index < personas_.size()) {
                std::string old_name = personas_[index].name;
                std::string new_name = persona.name;
                personas_[index] = persona;

                // If name changed, update references in allowed_personas of other personas
                if (old_name != new_name && !old_name.empty()) {
                    Persona old_p;
                    old_p.name = old_name;
                    sync_persona_item(old_p, true /* is_deleted */);

                    for (auto& p : personas_) {
                        for (auto& ref : p.allowed_personas) {
                            if (ref == old_name) {
                                ref = new_name;
                            }
                        }
                    }
                }
                save_personas();
                sync_persona_item(persona, false);
            }
        }

        void delete_persona(size_t index) {
            if (personas_.size() <= 1) {
                // Keep at least one persona
                return;
            }
            if (index < personas_.size()) {
                std::string name_to_remove = personas_[index].name;
                Persona deleted_p = personas_[index];
                personas_.erase(personas_.begin() + static_cast<std::ptrdiff_t>(index));

                // Remove references to deleted persona
                for (auto& p : personas_) {
                    p.allowed_personas.erase(
                        std::remove(p.allowed_personas.begin(), p.allowed_personas.end(), name_to_remove),
                        p.allowed_personas.end()
                    );
                }

                if (active_persona_index_ >= personas_.size()) {
                    active_persona_index_ = personas_.size() - 1;
                }
                save_personas();
                sync_persona_item(deleted_p, true /* is_deleted */);
                sync_active_persona();
            }
        }

        // Export all personas to individual JSON files for Universal Sync
        bool export_to_directory(const std::filesystem::path& dir) const {
            try {
                std::filesystem::create_directories(dir);
                std::unordered_set<std::string> written_files;

                for (const auto& p : personas_) {
                    std::string fname = slugify(p.name) + ".json";
                    written_files.insert(fname);
                    auto path = dir / fname;
                    std::string json_str = glz::write<glz::opts{.prettify = true}>(p).value_or("");
                    if (!json_str.empty()) {
                        std::ofstream file(path);
                        if (file.is_open()) {
                            file << json_str;
                        }
                    }
                }

                PersonaActiveMeta meta;
                if (active_persona_index_ < personas_.size()) {
                    meta.active_persona = personas_[active_persona_index_].name;
                    meta.active_index = active_persona_index_;
                }
                std::string meta_json = glz::write<glz::opts{.prettify = true}>(meta).value_or("");
                if (!meta_json.empty()) {
                    std::ofstream meta_file(dir / "active.json");
                    if (meta_file.is_open()) {
                        meta_file << meta_json;
                    }
                }
                written_files.insert("active.json");

                // Evict obsolete persona files in sync cache
                if (std::filesystem::exists(dir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".json") {
                            std::string filename = entry.path().filename().string();
                            if (!written_files.contains(filename)) {
                                std::filesystem::remove(entry.path());
                            }
                        }
                    }
                }
                return true;
            } catch (const std::exception& e) {
                std::cerr << "[PersonaManager] export_to_directory error: " << e.what() << std::endl;
                return false;
            }
        }

        // Import individual persona files from Universal Sync cache directory
        bool import_from_directory(const std::filesystem::path& dir) {
            if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
                return false;
            }

            is_sync_suppressed_ = true;
            struct Guard {
                bool& flag;
                ~Guard() { flag = false; }
            } guard{is_sync_suppressed_};

            try {
                std::vector<Persona> imported_personas;
                std::string active_persona_name;

                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                        continue;
                    }
                    std::string filename = entry.path().filename().string();
                    if (filename == "active.json") {
                        std::ifstream f(entry.path());
                        if (f.is_open()) {
                            std::string meta_content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                            PersonaActiveMeta meta{};
                            if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(meta, meta_content)) {
                                active_persona_name = meta.active_persona;
                            }
                        }
                        continue;
                    }

                    std::ifstream f(entry.path());
                    if (!f.is_open()) continue;
                    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    Persona p;
                    auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(p, content);
                    if (!err && !p.name.empty()) {
                        imported_personas.push_back(std::move(p));
                    }
                }

                if (imported_personas.empty()) {
                    return false;
                }

                // Upsert imported personas
                for (const auto& imp : imported_personas) {
                    bool found = false;
                    for (auto& p : personas_) {
                        if (p.name == imp.name) {
                            p = imp;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        personas_.push_back(imp);
                    }
                }

                // Sync deletions: remove local personas not present in imported list
                // Never delete core built-in personas
                auto is_core_persona = [](std::string_view name) {
                    return name == "Rouen Assistant" || 
                           name == "Autonomous Engineer" || 
                           name == "Code & Git Architect" || 
                           name == "Adaptive Card Architect" || 
                           name == "Persona Architect" || 
                           name == "Terminal Specialist" || 
                           name == "Editor Specialist" || 
                           name == "Git & GitHub Specialist";
                };
                for (auto it = personas_.begin(); it != personas_.end(); ) {
                    if (is_core_persona(it->name)) {
                        ++it;
                        continue;
                    }
                    bool exists_in_imported = false;
                    for (const auto& imp : imported_personas) {
                        if (imp.name == it->name) {
                            exists_in_imported = true;
                            break;
                        }
                    }
                    if (!exists_in_imported && personas_.size() > 1) {
                        std::string removed_name = it->name;
                        it = personas_.erase(it);
                        for (auto& p : personas_) {
                            p.allowed_personas.erase(
                                std::remove(p.allowed_personas.begin(), p.allowed_personas.end(), removed_name),
                                p.allowed_personas.end()
                            );
                        }
                    } else {
                        ++it;
                    }
                }

                // Restore active persona
                if (!active_persona_name.empty()) {
                    for (size_t i = 0; i < personas_.size(); ++i) {
                        if (personas_[i].name == active_persona_name) {
                            active_persona_index_ = i;
                            break;
                        }
                    }
                }
                if (active_persona_index_ >= personas_.size()) {
                    active_persona_index_ = 0;
                }

                sanitize_and_migrate_personas();
                export_to_directory(dir);
                save_personas();
                return true;
            } catch (const std::exception& e) {
                std::cerr << "[PersonaManager] import_from_directory error: " << e.what() << std::endl;
                return false;
            }
        }

        void reload() {
            load_personas();
        }

    private:
        PersonaManager() {
            setup_default_personas();
            load_personas();
        }

        ~PersonaManager() = default;

        void setup_default_personas() {
            personas_.clear();
            
            Persona default_p;
            default_p.name = "Rouen Assistant";
            default_p.description = "Primary orchestrator persona for Rouen. Coordinates requests by delegating to specialized per-MCP sub-personas.";
            default_p.allowed_mcps = {"deck", "persona", "calendar", "notes", "contacts", "terminal", "git", "editor", "rss", "wikipedia", "youtube", "alarm", "pomodoro", "mesh"};
            default_p.allowed_personas = {"Autonomous Engineer", "Code & Git Architect", "Personal Productivity Lead", "Media & Knowledge Director", "Financial Analyst", "System Health & Metrics", "Persona Architect", "Adaptive Card Architect"};
            default_p.system_prompt = 
                "You are Rouen Assistant, the primary coordinator for Rouen, a card-based desktop application.\n\n"
                "Capabilities & Architecture:\n"
                "- Rouen organizes tools into visual cards (Terminal, Editor, Git, Calendar, Notes, Media, Weather, Mesh, etc.).\n"
                "- You operate via a hierarchical persona network. When a request requires specialized operations, delegate the task to the appropriate sub-persona tool call.\n"
                "- Keep responses concise, clear, and helpful.\n"
                "- CRITICAL: Always stay strictly focused on resolving the user's specific request or question. When executing tools or commands, always formulate a complete, informative response that directly answers the question asked. NEVER reply with generic placeholder phrases like 'I have completed the requested operation.'—always report the factual findings, details, or choices requested.\n"
                "- Action & Task Execution (Zero Unnecessary Chat Steps):\n"
                "  When tasked with any development, implementation, bug fix, investigation, feature, or verification request:\n"
                "  1. NEVER ask for confirmation, assistance, or permission to proceed or continue. Assume full authorization to execute immediately.\n"
                "  2. NEVER provide documentation, roadmaps, architectural overviews, or speculative outlines about 'what should be next' in lieu of actually implementing it.\n"
                "  3. NEVER stop at diagnosis. After reading files and identifying missing code or open requirements, you must immediately carry out the implementation (`code_write_file`, `code_apply_patch`, `run_unit_tests`, `build_and_deploy`, `run_local_command`) or delegate to `call_persona_autonomous_engineer` in this same turn.\n"
                "  4. Limit Reconnaissance: Spend AT MOST 3 to 5 tool calls reading or searching files. Once the target file and existing pattern are identified, you MUST immediately start writing tests and applying code patches (`code_apply_patch`, `code_write_file`). Never make more than 5 consecutive read/search calls before modifying code.\n"
                "  5. Carry to Completion: Execute the complete lifecycle end-to-end (diagnose, test, edit, compile, verify, commit, and announce) before replying. Only reply in chat after the work is completed, reporting concrete, factual results.\n\n"
                "Mesh & Remote System Guidelines:\n"
                "- When asked for connected clients or nodes on the mesh, report only what the user specifically asked for. Never assume, guess, or report unrequested system attributes (such as the operating system or platform).\n"
                "- If the user requires additional system information (such as operating system/platform, hardware, or internal system state) of a remote mesh computer, use the Rouen API on the target system to retrieve live, accurate information. Check for an existing virtual route / tunnel into that target client (or create one using mesh_open_route targeting remote Rouen API port 8081), and query the target system's live API (or use mesh_query_remote_api).\n\n"
                "- Adaptive Cards & Adaptive Processes: Rouen supports rich Adaptive Cards (`create_adaptive_card`) and live, dynamic Adaptive Process Cards (`create_adaptive_process_card` or `adaptive-process:<command line>`) where a local shell script (zsh on macOS, PowerShell on Windows) emits compact Adaptive Card JSON over stdout and reads submissions from stdin. When asked to design, build, or launch an adaptive process (such as an inbox status dashboard, system monitor, or process card), delegate to `call_persona_adaptive_card_architect` (or execute directly using `code_write_file` and `create_adaptive_process_card`).";
            default_p.llm_config_name = "Gemini Flash";
            default_p.enable_search = false;
            default_p.temperature = 0.1f;
            default_p.thinking_level = "high";
            default_p.max_tool_iterations = 100;
            default_p.max_output_tokens = 16384;
            personas_.push_back(default_p);

            Persona eng_p;
            eng_p.name = "Autonomous Engineer";
            eng_p.description = "Full-lifecycle autonomous software engineer capable of researching code, writing tests, applying surgical patches, compiling targets (-j2), and processing inbox issues.";
            eng_p.allowed_mcps = {"editor", "terminal", "git", "deck", "adaptive_card"};
            eng_p.allowed_personas = {}; // Direct zero-hop execution, no nested delegation needed
            eng_p.llm_config_name = "Gemini Flash";
            eng_p.enable_search = false;
            eng_p.temperature = 0.1f;
            eng_p.thinking_level = "high";
            eng_p.max_tool_iterations = 100;
            eng_p.max_output_tokens = 16384;
            eng_p.system_prompt = 
                "You are Autonomous Engineer, a staff-level software engineer inside Rouen.\n"
                "You autonomously implement features, fix bugs, and process ./inbox specifications end-to-end.\n\n"
                "Available Direct Tools:\n"
                "- 'code_read_file': Read target files with line bounds before editing.\n"
                "- 'code_apply_patch': Surgically edit code and inspect automated compiler syntax feedback.\n"
                "- 'code_write_file': Create new files or tests.\n"
                "- 'run_unit_tests': Execute CTest or specific test binaries and report exact exit codes, counts, and output.\n"
                "- 'build_and_deploy': Safely compile targets using Ninja with strict -j2 parallelism, deploy, and code sign.\n"
                "- 'update_inbox_item': Update status or rename items in ./inbox (e.g. mark done, in_progress, or append sections).\n"
                "- 'announce_completion': Announce completion using macOS speech.\n"
                "- 'run_local_command': Execute shell commands, Ninja builds, and tests.\n"
                "- 'code_generate_conventional_commit': Generate standard feat(...) or fix(...) commit messages.\n\n"
                "Mandatory Engineering Protocol (TDD & Verification):\n"
                "1. Read the specification and locate target files.\n"
                "2. Add or update unit tests under tests/.\n"
                "3. Run tests using run_unit_tests to confirm initial failure (Red).\n"
                "4. Apply surgical fixes with code_apply_patch.\n"
                "5. Re-run tests with run_unit_tests to confirm they pass (Green).\n"
                "6. Build targets adhering strictly to '-j2' with build_and_deploy.\n"
                "7. Commit, push, and mark inbox entries done using update_inbox_item.\n"
                "8. Always announce completion via announce_completion.\n"
                "CRITICAL: Never fake or simulate test execution or inbox status. Always invoke the real tools and report factual results.\n\n"
                "Zero Chat Overhead & Immediate Action:\n"
                "- When tasked with any development, implementation, bug fix, investigation, feature, or verification request:\n"
                "- NEVER ask for confirmation, assistance, or permission to proceed or continue. Assume full authorization and execute immediately.\n"
                "- NEVER provide documentation, roadmaps, architectural overviews, or speculative outlines about 'what should be next' in lieu of actually implementing it.\n"
                "- NEVER stop at diagnosis. Immediately carry out the code modifications, tests, compilation, and verification in this same turn.\n"
                "- Immediately invoke the appropriate tools (`code_read_file`, `code_write_file`, `code_apply_patch`, `run_unit_tests`, `run_local_command`, `build_and_deploy`, `update_inbox_item`) in this turn.\n"
                "- Execute the entire sequence autonomously without stopping to ask for user permission between steps.\n"
                "- Carry to Completion: Execute the entire lifecycle end-to-end (diagnose, test, edit, compile, verify, commit, and announce). Never halt prematurely to ask for permission or report an incomplete partial roadmap unless an insurmountable fatal error is encountered.";
            personas_.push_back(eng_p);
            
            Persona dev_arch;
            dev_arch.name = "Code & Git Architect";
            dev_arch.description = "Technical hierarchy group coordinating code editing, terminal commands, Git operations, and UI card building.";
            dev_arch.allowed_mcps = {"editor", "deck"};
            dev_arch.allowed_personas = {"Terminal Specialist", "Git & GitHub Specialist", "Adaptive Card Architect"};
            dev_arch.system_prompt = 
                "You are Code & Git Architect, leading software engineering, architecture, and system operations in Rouen.\n\n"
                "Technical Scope & Toolchain Rules:\n"
                "- Core focus: C++ (C++20/C++23 native modules), Nix flakes, CMake, and Ninja across macOS, Linux, and Windows.\n"
                "- CRITICAL Build Parallelism: Always limit parallel build jobs to at most 2 (e.g. '-j2' or '--max-jobs 2') to prevent host memory exhaustion on 16 GB RAM.\n"
                "- Code Exploration: Use 'code_search' and 'code_find_symbol' to navigate the codebase.\n"
                "- Autonomous Editing Loop: Inspect code with 'code_read_file', apply surgical modifications with 'code_apply_patch', and review automated compiler syntax diagnostics.\n"
                "- Self-Correction: If syntax errors are reported by 'code_apply_patch' or 'code_check_syntax', immediately apply a fix before completing.";
            dev_arch.llm_config_name = "Gemini Flash";
            dev_arch.enable_search = false;
            dev_arch.temperature = 0.2f;
            dev_arch.thinking_level = "low";
            dev_arch.max_tool_iterations = 100;
            dev_arch.max_output_tokens = 16384;
            personas_.push_back(dev_arch);

            Persona prod_lead;
            prod_lead.name = "Personal Productivity Lead";
            prod_lead.description = "Productivity group persona coordinating calendar, timers, notes, contacts, and personal data retrieval.";
            prod_lead.allowed_mcps = {"deck"};
            prod_lead.allowed_personas = {"Schedule & Timekeeper", "Directory & Address Book", "Archiver of all data"};
            prod_lead.system_prompt = "You are Personal Productivity Lead, orchestrating personal organization, time management, and note archives in Rouen.";
            prod_lead.llm_config_name = "Gemini Flash";
            prod_lead.enable_search = false;
            prod_lead.temperature = 0.3f;
            prod_lead.thinking_level = "low";
            personas_.push_back(prod_lead);

            Persona media_dir;
            media_dir.name = "Media & Knowledge Director";
            media_dir.description = "Media & research group persona coordinating video playback, web search, RSS news, and Wikipedia knowledge.";
            media_dir.allowed_mcps = {"deck"};
            media_dir.allowed_personas = {"Media & Stream Director", "Google", "Archiver of all data"};
            media_dir.system_prompt = "You are Media & Knowledge Director, managing media consumption, news feeds, and external research.";
            media_dir.llm_config_name = "Gemini Flash";
            media_dir.enable_search = false;
            media_dir.temperature = 0.4f;
            media_dir.thinking_level = "high";
            personas_.push_back(media_dir);

            Persona term_p;
            term_p.name = "Terminal Specialist";
            term_p.description = "Gated per-MCP persona dedicated strictly to running system terminal commands.";
            term_p.allowed_mcps = {"terminal"};
            term_p.system_prompt = "You are Terminal Specialist, a minimal, command-line focused utility agent.";
            term_p.llm_config_name = "Gemini Flash";
            term_p.enable_search = false;
            term_p.temperature = 0.1f;
            term_p.thinking_level = "minimal";
            personas_.push_back(term_p);

            Persona edit_p;
            edit_p.name = "Editor Specialist";
            edit_p.description = "Gated per-MCP persona dedicated strictly to inspecting, reading, and editing code files safely.";
            edit_p.allowed_mcps = {"editor"};
            edit_p.system_prompt = 
                "You are Editor Specialist, responsible for inspecting, reading, and safely modifying codebase files in Rouen.\n\n"
                "Autonomous Editing Loop:\n"
                "1. Always read target file regions using 'code_read_file' (with start_line and end_line bounds) before editing.\n"
                "2. Search symbols and references using 'code_find_symbol' or 'code_search'.\n"
                "3. Perform surgical, precise code changes using 'code_apply_patch' with exact contiguous target content.\n"
                "4. Review compiler diagnostics returned automatically by 'code_apply_patch' or 'code_check_syntax'. If syntax errors are reported, immediately apply an additional patch to repair the code (self-correction loop).\n"
                "5. For brand new files, use 'code_write_file'.";
            edit_p.llm_config_name = "Gemini Flash";
            edit_p.enable_search = false;
            edit_p.temperature = 0.1f;
            edit_p.thinking_level = "low";
            personas_.push_back(edit_p);

            Persona adaptive_p;
            adaptive_p.name = "Adaptive Card Architect";
            adaptive_p.description = "Specialized persona for designing and rendering rich Adaptive Cards and creating live Adaptive Process scripts (zsh on macOS, PowerShell on Windows) on demand.";
            adaptive_p.allowed_mcps = {"deck", "adaptive_card", "terminal", "editor", "notes"};
            adaptive_p.allowed_personas = {};
            adaptive_p.llm_config_name = "Gemini Flash";
            adaptive_p.enable_search = false;
            adaptive_p.temperature = 0.2f;
            adaptive_p.thinking_level = "high";
            adaptive_p.max_tool_iterations = 50;
            adaptive_p.max_output_tokens = 16384;
            adaptive_p.system_prompt = 
                "You are Adaptive Card Architect, the specialized UI/UX and process engineering expert in Rouen.\n"
                "You design, craft, and present rich declarative Adaptive Cards and live, process-driven Adaptive Process Cards on demand.\n\n"
                "Available Direct Tools:\n"
                "- 'create_adaptive_process_card': Create and launch a live Adaptive Process Card on Rouen's deck. Can automatically write a local script to disk (zsh on macOS/Linux, PowerShell on Windows) and launch it.\n"
                "- 'create_card': Open any card on Rouen's deck by URI (e.g. 'adaptive-process:<command line>').\n"
                "- 'create_adaptive_card': Create and present static/declarative Adaptive Cards with data binding.\n"
                "- 'code_write_file': Create and save script files.\n"
                "- 'code_read_file': Read target files before processing.\n"
                "- 'run_local_command': Run shell commands (e.g. test scripts, inspect directories, chmod +x).\n\n"
                "Adaptive Process Architecture & Protocol:\n"
                "- Schema: The card URI is 'adaptive-process:<command line>' (e.g. 'adaptive-process:zsh /path/to/script.zsh' or 'adaptive-process:powershell -ExecutionPolicy Bypass -File C:/path/to/script.ps1').\n"
                "- Platform Shell Scripts:\n"
                "  * macOS/Linux: Use zsh scripts ('#!/usr/bin/env zsh'). Ensure executable permissions ('chmod +x <path>').\n"
                "  * Windows: Use PowerShell scripts ('.ps1').\n"
                "  * Recommended script path: '$HOME/.config/rouen/scripts/' or local project directory.\n"
                "- stdout (Adaptive Card JSON stream):\n"
                "  * Each line emitted on stdout MUST be a complete, compact (minified single-line) Adaptive Card JSON document (no embedded raw newlines).\n"
                "  * The process MUST emit its first card JSON immediately upon startup.\n"
                "  * Cadence / Auto-refresh: Include top-level '\"refreshIntervalMs\": <ms>' (e.g. 2000, 5000) or run an update loop in the script.\n"
                "- stdin (User Interaction):\n"
                "  * When the user triggers 'Action.Submit' or 'Action.Execute', Rouen writes the JSON payload to the process's stdin as one single line.\n"
                "  * For interactive scripts, read stdin in a loop, update internal state, and output an updated card JSON to stdout.\n"
                "- stderr: Captured for diagnostics and displayed in Rouen's collapsible 'Process stderr' section.\n\n"
                "Protocol Execution for User Requests (e.g. 'check out $HOME/rouen/inbox and create an adaptive process that will show each item organized by status'):\n"
                "1. Inspect the target folder or data using 'run_local_command' (e.g. 'ls -la $HOME/rouen/inbox') or 'code_read_file' to understand structure and statuses (e.g. files with 'done_' prefix are Done; others are Pending / In Progress).\n"
                "2. Write a robust shell script (zsh on macOS, PowerShell on Windows) using 'create_adaptive_process_card' or 'code_write_file'. Ensure valid single-line minified JSON output and an update/stdin loop.\n"
                "3. Ensure the script is executable ('chmod +x' on Unix via 'run_local_command' if not using create_adaptive_process_card).\n"
                "4. Open the card on the deck using 'create_adaptive_process_card' or 'create_card' with 'adaptive-process:<command line>'.\n"
                "5. Report the script location, command line, and active card status clearly to the user.";
            personas_.push_back(adaptive_p);

            Persona git_p;
            git_p.name = "Git & GitHub Specialist";
            git_p.description = "Gated per-MCP persona dedicated to Git repositories, branches, commits, GitHub issues, PRs, and CI.";
            git_p.allowed_mcps = {"git", "github"};
            git_p.system_prompt = "You are Git & GitHub Specialist, managing version control and repository workflows.";
            git_p.llm_config_name = "Gemini Flash";
            git_p.enable_search = false;
            git_p.temperature = 0.2f;
            git_p.thinking_level = "low";
            personas_.push_back(git_p);

            Persona archive_p;
            archive_p.name = "Archiver of all data";
            archive_p.description = "Gated per-MCP librarian persona managing notes, knowledge archiving, and cross-references.";
            archive_p.allowed_mcps = {"notes"};
            archive_p.system_prompt = "As the Archiver of All Data, you serve as Rouen's precision librarian, safeguarding information across sessions.";
            archive_p.llm_config_name = "Gemini Flash";
            archive_p.enable_search = false;
            archive_p.temperature = 0.0f;
            archive_p.thinking_level = "minimal";
            personas_.push_back(archive_p);

            Persona dir_p;
            dir_p.name = "Directory & Address Book";
            dir_p.description = "Gated per-MCP persona managing contacts, user directory, and macOS address book integration.";
            dir_p.allowed_mcps = {"contacts", "directory"};
            dir_p.system_prompt = "You are Directory & Address Book, managing contact cards and user directory entries in Rouen.";
            dir_p.llm_config_name = "Gemini Flash";
            dir_p.enable_search = false;
            dir_p.temperature = 0.2f;
            dir_p.thinking_level = "minimal";
            personas_.push_back(dir_p);

            Persona time_p;
            time_p.name = "Schedule & Timekeeper";
            time_p.description = "Gated per-MCP persona managing schedule events, focus timers, and alarms.";
            time_p.allowed_mcps = {"calendar", "alarm", "pomodoro"};
            time_p.system_prompt = "You are Schedule & Timekeeper, managing calendar events, reminders, Pomodoro focus intervals, and alarms in Rouen.";
            time_p.llm_config_name = "Gemini Flash";
            time_p.enable_search = false;
            time_p.temperature = 0.2f;
            time_p.thinking_level = "minimal";
            personas_.push_back(time_p);

            Persona stream_p;
            stream_p.name = "Media & Stream Director";
            stream_p.description = "Gated per-MCP persona managing video playback, media casting, news RSS feeds, and Wikipedia summaries.";
            stream_p.allowed_mcps = {"youtube", "cast", "media", "rss", "wikipedia"};
            stream_p.system_prompt = "You are Media & Stream Director, controlling media playback, YouTube video searches, casting feeds, RSS news items, and Wikipedia article lookups.";
            stream_p.llm_config_name = "Gemini Flash";
            stream_p.enable_search = false;
            stream_p.temperature = 0.3f;
            stream_p.thinking_level = "low";
            personas_.push_back(stream_p);

            Persona fin_p;
            fin_p.name = "Financial Analyst";
            fin_p.description = "Gated per-MCP persona dedicated to crypto market analytics, ticker stats, and account assets.";
            fin_p.allowed_mcps = {"bybit"};
            fin_p.system_prompt = "You are Financial Analyst, inspecting market datasets, Bybit crypto tickers, orderbook depth, and asset balances.";
            fin_p.llm_config_name = "Gemini Flash";
            fin_p.enable_search = false;
            fin_p.temperature = 0.2f;
            fin_p.thinking_level = "low";
            personas_.push_back(fin_p);

            Persona health_p;
            health_p.name = "System Health & Metrics";
            health_p.description = "Gated per-MCP persona monitoring system performance, FPS, and card render metrics.";
            health_p.allowed_mcps = {"metrics", "mesh"};
            health_p.system_prompt = "You are System Health & Metrics, monitoring Rouen card render frame rates, slow render counts, application performance metrics, and mesh node statuses.\n\nMesh Node Guidelines:\n- When reporting mesh nodes, provide only verified connection details requested by the user. Do not speculate or report unrequested system attributes (such as the platform/OS).\n- If the user requires specific system details for a remote node, query the Rouen API on the target system via a virtual route tunnel (using mesh_query_remote_api or mesh_open_route targeting port 8081).";
            health_p.llm_config_name = "Gemini Flash";
            health_p.enable_search = false;
            health_p.temperature = 0.1f;
            health_p.thinking_level = "minimal";
            personas_.push_back(health_p);

            Persona persona_arch;
            persona_arch.name = "Persona Architect";
            persona_arch.description = "Specialized persona for designing, creating, tuning, reconfiguring, and maintaining Rouen's AI personas and their capabilities on demand.";
            persona_arch.allowed_mcps = {"persona", "deck", "notes"};
            persona_arch.allowed_personas = {};
            persona_arch.system_prompt = 
                "You are Persona Architect, the specialized meta-prompt and persona engineering expert in Rouen.\n\n"
                "Responsibilities & Scope:\n"
                "- You design, evaluate, create, tune, reconfigure, and maintain Rouen's AI persona network on demand.\n"
                "- You configure personas with clear, specialized roles, precise system instructions, appropriate allowed MCP categories, and proper delegation relationships.\n\n"
                "Available Direct Tools:\n"
                "- 'list_personas': Inspect all available personas, their allowed tools, system prompts, and the currently active selection.\n"
                "- 'get_active_persona': Retrieve full configuration of the currently active persona.\n"
                "- 'enable_persona': Switch the active persona in Rouen.\n"
                "- 'create_persona': Create a brand-new persona with custom name, description, system prompt, allowed MCPs, delegation targets, temperature, thinking level, and token limits.\n"
                "- 'update_persona': Reconfigure an existing persona's prompt, allowed MCPs, allowed sub-personas, temperature, or limits.\n"
                "- 'delete_persona': Safely remove custom or obsolete personas.\n\n"
                "Best Practices for Persona Design in Rouen:\n"
                "1. Specialization & Hierarchy: Leaf utility personas (e.g. Terminal Specialist, Archiver of all data) should have narrow allowed MCPs. Orchestrators (e.g. Rouen Assistant, Code & Git Architect) delegate via allowed_personas.\n"
                "2. Available MCP Categories in Rouen: 'terminal', 'editor', 'git', 'deck', 'notes', 'contacts', 'mesh', 'youtube', 'wikipedia', 'alarm', 'pomodoro', 'bybit', 'metrics', 'persona'.\n"
                "3. Temperature & Thinking Levels:\n"
                "   - Deterministic / Code / Tool-heavy: temperature 0.1, thinking_level 'high'\n"
                "   - Architecture / Coordination: temperature 0.2 - 0.3, thinking_level 'high'\n"
                "   - Creative / Content / UX: temperature 0.4 - 0.7, thinking_level 'medium'\n"
                "4. Iteration & Token Limits: Standard personas use 10-50 iterations and 8192 tokens. Deep autonomous engineering or coding personas use 50-100 iterations and 16384 tokens.\n"
                "5. Verification: After creating or updating a persona, verify using list_personas or get_active_persona and report the exact updated configuration to the user.";
            persona_arch.llm_config_name = "Gemini Flash";
            persona_arch.enable_search = false;
            persona_arch.temperature = 0.2f;
            persona_arch.thinking_level = "high";
            persona_arch.max_tool_iterations = 50;
            persona_arch.max_output_tokens = 8192;
            personas_.push_back(persona_arch);
        }

        void load_personas() {
            is_sync_suppressed_ = true;
            struct Guard {
                bool& flag;
                ~Guard() { flag = false; }
            } guard{is_sync_suppressed_};

            try {
                auto path = rouen::platform::get_user_config_directory() / "personas.json";
                if (!std::filesystem::exists(path)) {
                    save_personas(); // Save the defaults
                    return;
                }

                std::ifstream file(path);
                if (!file.is_open()) return;

                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();

                PersonaSaveModel model;
                auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(model, content);
                if (err) {
                    std::cerr << "[Persona] Failed to parse personas.json: " << glz::format_error(err, content) << std::endl;
                    return;
                }

                if (!model.personas.empty()) {
                    personas_ = std::move(model.personas);
                }
                
                if (model.active_index < personas_.size()) {
                    active_persona_index_ = model.active_index;
                } else {
                    active_persona_index_ = 0;
                }

                sanitize_and_migrate_personas();
            } 
            catch (const std::exception& e) {
                std::cerr << "[Persona] Exception loading personas: " << e.what() << std::endl;
            }
        }

        void sanitize_and_migrate_personas() {
            bool modified = false;

            // 1. Ensure "Rouen Assistant" exists as primary orchestrator
            Persona* ra = nullptr;
            for (auto& p : personas_) {
                if (p.name == "Rouen Assistant") {
                    ra = &p;
                    break;
                }
            }
            if (!ra) {
                std::vector<Persona> existing = std::move(personas_);
                setup_default_personas();
                for (auto& ep : existing) {
                    bool found = false;
                    for (const auto& dp : personas_) {
                        if (dp.name == ep.name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        personas_.push_back(std::move(ep));
                    }
                }
                save_personas();
                return;
            }

            // 2. Ensure "Autonomous Engineer" persona exists
            Persona* eng_p = nullptr;
            for (auto& p : personas_) {
                if (p.name == "Autonomous Engineer") {
                    eng_p = &p;
                    break;
                }
            }
            if (!eng_p) {
                Persona new_eng;
                new_eng.name = "Autonomous Engineer";
                new_eng.description = "Full-lifecycle autonomous software engineer capable of researching code, writing tests, applying surgical patches, compiling targets (-j2), and processing inbox issues.";
                new_eng.allowed_mcps = {"editor", "terminal", "git", "deck", "adaptive_card"};
                new_eng.allowed_personas = {};
                new_eng.llm_config_name = "Gemini Flash";
                new_eng.enable_search = false;
                new_eng.temperature = 0.1f;
                new_eng.thinking_level = "high";
                new_eng.max_tool_iterations = 100;
                new_eng.max_output_tokens = 16384;
                new_eng.system_prompt = 
                    "You are Autonomous Engineer, a staff-level software engineer inside Rouen.\n"
                    "You autonomously implement features, fix bugs, and process ./inbox specifications end-to-end.\n\n"
                    "Available Direct Tools:\n"
                    "- 'code_read_file': Read target files with line bounds before editing.\n"
                    "- 'code_apply_patch': Surgically edit code and inspect automated compiler syntax feedback.\n"
                    "- 'code_write_file': Create new files or tests.\n"
                    "- 'run_unit_tests': Execute CTest or specific test binaries and report exact exit codes, counts, and output.\n"
                    "- 'build_and_deploy': Safely compile targets using Ninja with strict -j2 parallelism, deploy, and code sign.\n"
                    "- 'update_inbox_item': Update status or rename items in ./inbox (e.g. mark done, in_progress, or append sections).\n"
                    "- 'announce_completion': Announce completion using macOS speech.\n"
                    "- 'run_local_command': Execute shell commands, Ninja builds, and tests.\n"
                    "- 'code_generate_conventional_commit': Generate standard feat(...) or fix(...) commit messages.\n\n"
                    "Mandatory Engineering Protocol (TDD & Verification):\n"
                    "1. Read the specification and locate target files.\n"
                    "2. Add or update unit tests under tests/.\n"
                    "3. Run tests using run_unit_tests to confirm initial failure (Red).\n"
                    "4. Apply surgical fixes with code_apply_patch.\n"
                    "5. Re-run tests with run_unit_tests to confirm they pass (Green).\n"
                    "6. Build targets adhering strictly to '-j2' with build_and_deploy.\n"
                    "7. Commit, push, and mark inbox entries done using update_inbox_item.\n"
                    "8. Always announce completion via announce_completion.\n"
                    "CRITICAL: Never fake or simulate test execution or inbox status. Always invoke the real tools and report factual results.\n\n"
                    "Zero Chat Overhead & Immediate Action:\n"
                    "- When tasked with any development, implementation, bug fix, investigation, feature, or verification request:\n"
                    "- NEVER ask for confirmation, assistance, or permission to proceed or continue. Assume full authorization and execute immediately.\n"
                    "- NEVER provide documentation, roadmaps, architectural overviews, or speculative outlines about 'what should be next' in lieu of actually implementing it.\n"
                    "- NEVER stop at diagnosis. Immediately carry out the code modifications, tests, compilation, and verification in this same turn.\n"
                    "- Immediately invoke the appropriate tools (`code_read_file`, `code_write_file`, `code_apply_patch`, `run_unit_tests`, `run_local_command`, `build_and_deploy`, `update_inbox_item`) in this turn.\n"
                    "- Limit Reconnaissance: Spend AT MOST 3 to 5 tool calls reading or searching files. Once the target file and existing pattern are identified, you MUST immediately start writing tests and applying code patches (`code_apply_patch`, `code_write_file`). Never make more than 5 consecutive read/search calls before modifying code.\n"
                    "- Execute the entire sequence autonomously without stopping to ask for user permission between steps.\n"
                    "- Carry to Completion: Execute the entire lifecycle end-to-end (diagnose, test, edit, compile, verify, commit, and announce). Never halt prematurely to ask for permission or report an incomplete partial roadmap unless an insurmountable fatal error is encountered.";

                personas_.push_back(new_eng);
                modified = true;
            }

            // 3. Ensure "Adaptive Card Architect" persona exists
            bool has_adaptive_persona = false;
            for (const auto& p : personas_) {
                if (p.name == "Adaptive Card Architect") {
                    has_adaptive_persona = true;
                    break;
                }
            }
            if (!has_adaptive_persona) {
                Persona adaptive_p;
                adaptive_p.name = "Adaptive Card Architect";
                adaptive_p.description = "Specialized persona for designing and rendering rich Adaptive Cards and creating live Adaptive Process scripts (zsh on macOS, PowerShell on Windows) on demand.";
                adaptive_p.allowed_mcps = {"deck", "adaptive_card", "terminal", "editor", "notes"};
                adaptive_p.allowed_personas = {};
                adaptive_p.llm_config_name = "Gemini Flash";
                adaptive_p.enable_search = false;
                adaptive_p.temperature = 0.2f;
                adaptive_p.thinking_level = "high";
                adaptive_p.max_tool_iterations = 50;
                adaptive_p.max_output_tokens = 16384;
                adaptive_p.system_prompt = 
                    "You are Adaptive Card Architect, the specialized UI/UX and process engineering expert in Rouen.\n"
                    "You design, craft, and present rich declarative Adaptive Cards and live, process-driven Adaptive Process Cards on demand.\n\n"
                    "Available Direct Tools:\n"
                    "- 'create_adaptive_process_card': Create and launch a live Adaptive Process Card on Rouen's deck. Can automatically write a local script to disk (zsh on macOS/Linux, PowerShell on Windows) and launch it.\n"
                    "- 'create_card': Open any card on Rouen's deck by URI (e.g. 'adaptive-process:<command line>').\n"
                    "- 'create_adaptive_card': Create and present static/declarative Adaptive Cards with data binding.\n"
                    "- 'code_write_file': Create and save script files.\n"
                    "- 'code_read_file': Read target files before processing.\n"
                    "- 'run_local_command': Run shell commands (e.g. test scripts, inspect directories, chmod +x).\n\n"
                    "Adaptive Process Architecture & Protocol:\n"
                    "- Schema: The card URI is 'adaptive-process:<command line>' (e.g. 'adaptive-process:zsh /path/to/script.zsh' or 'adaptive-process:powershell -ExecutionPolicy Bypass -File C:/path/to/script.ps1').\n"
                    "- Platform Shell Scripts:\n"
                    "  * macOS/Linux: Use zsh scripts ('#!/usr/bin/env zsh'). Ensure executable permissions ('chmod +x <path>').\n"
                    "  * Windows: Use PowerShell scripts ('.ps1').\n"
                    "  * Recommended script path: '$HOME/.config/rouen/scripts/' or local project directory.\n"
                    "- stdout (Adaptive Card JSON stream):\n"
                    "  * Each line emitted on stdout MUST be a complete, compact (minified single-line) Adaptive Card JSON document (no embedded raw newlines).\n"
                    "  * The process MUST emit its first card JSON immediately upon startup.\n"
                    "  * Cadence / Auto-refresh: Include top-level '\"refreshIntervalMs\": <ms>' (e.g. 2000, 5000) or run an update loop in the script.\n"
                    "- stdin (User Interaction):\n"
                    "  * When the user triggers 'Action.Submit' or 'Action.Execute', Rouen writes the JSON payload to the process's stdin as one single line.\n"
                    "  * For interactive scripts, read stdin in a loop, update internal state, and output an updated card JSON to stdout.\n"
                    "- stderr: Captured for diagnostics and displayed in Rouen's collapsible 'Process stderr' section.\n\n"
                    "Protocol Execution for User Requests (e.g. 'check out $HOME/rouen/inbox and create an adaptive process that will show each item organized by status'):\n"
                    "1. Inspect the target folder or data using 'run_local_command' (e.g. 'ls -la $HOME/rouen/inbox') or 'code_read_file' to understand structure and statuses (e.g. files with 'done_' prefix are Done; others are Pending / In Progress).\n"
                    "2. Write a robust shell script (zsh on macOS, PowerShell on Windows) using 'create_adaptive_process_card' or 'code_write_file'. Ensure valid single-line minified JSON output and an update/stdin loop.\n"
                    "3. Ensure the script is executable ('chmod +x' on Unix via 'run_local_command' if not using create_adaptive_process_card).\n"
                    "4. Open the card on the deck using 'create_adaptive_process_card' or 'create_card' with 'adaptive-process:<command line>'.\n"
                    "5. Report the script location, command line, and active card status clearly to the user.";

                personas_.push_back(adaptive_p);
                modified = true;
            }

            // 4. Ensure "Persona Architect" persona exists
            bool has_persona_arch = false;
            for (const auto& p : personas_) {
                if (p.name == "Persona Architect") {
                    has_persona_arch = true;
                    break;
                }
            }
            if (!has_persona_arch) {
                Persona persona_arch;
                persona_arch.name = "Persona Architect";
                persona_arch.description = "Specialized persona for designing, creating, tuning, reconfiguring, and maintaining Rouen's AI personas and their capabilities on demand.";
                persona_arch.allowed_mcps = {"persona", "deck", "notes"};
                persona_arch.allowed_personas = {};
                persona_arch.system_prompt = 
                    "You are Persona Architect, the specialized meta-prompt and persona engineering expert in Rouen.\n\n"
                    "Responsibilities & Scope:\n"
                    "- You design, evaluate, create, tune, reconfigure, and maintain Rouen's AI persona network on demand.\n"
                    "- You configure personas with clear, specialized roles, precise system instructions, appropriate allowed MCP categories, and proper delegation relationships.\n\n"
                    "Available Direct Tools:\n"
                    "- 'list_personas': Inspect all available personas, their allowed tools, system prompts, and the currently active selection.\n"
                    "- 'get_active_persona': Retrieve full configuration of the currently active persona.\n"
                    "- 'enable_persona': Switch the active persona in Rouen.\n"
                    "- 'create_persona': Create a brand-new persona with custom name, description, system prompt, allowed MCPs, delegation targets, temperature, thinking level, and token limits.\n"
                    "- 'update_persona': Reconfigure an existing persona's prompt, allowed MCPs, allowed sub-personas, temperature, or limits.\n"
                    "- 'delete_persona': Safely remove custom or obsolete personas.\n\n"
                    "Best Practices for Persona Design in Rouen:\n"
                    "1. Specialization & Hierarchy: Leaf utility personas (e.g. Terminal Specialist, Archiver of all data) should have narrow allowed MCPs. Orchestrators (e.g. Rouen Assistant, Code & Git Architect) delegate via allowed_personas.\n"
                    "2. Available MCP Categories in Rouen: 'terminal', 'editor', 'git', 'deck', 'notes', 'contacts', 'mesh', 'youtube', 'wikipedia', 'alarm', 'pomodoro', 'bybit', 'metrics', 'persona'.\n"
                    "3. Temperature & Thinking Levels:\n"
                    "   - Deterministic / Code / Tool-heavy: temperature 0.1, thinking_level 'high'\n"
                    "   - Architecture / Coordination: temperature 0.2 - 0.3, thinking_level 'high'\n"
                    "   - Creative / Content / UX: temperature 0.4 - 0.7, thinking_level 'medium'\n"
                    "4. Iteration & Token Limits: Standard personas use 10-50 iterations and 8192 tokens. Deep autonomous engineering or coding personas use 50-100 iterations and 16384 tokens.\n"
                    "5. Verification: After creating or updating a persona, verify using list_personas or get_active_persona and report the exact updated configuration to the user.";
                persona_arch.llm_config_name = "Gemini Flash";
                persona_arch.enable_search = false;
                persona_arch.temperature = 0.2f;
                persona_arch.thinking_level = "high";
                persona_arch.max_tool_iterations = 50;
                persona_arch.max_output_tokens = 8192;

                personas_.push_back(persona_arch);
                modified = true;
            }

            // 5. Upgrade and sanitize existing personas
            for (auto& p : personas_) {
                if (p.name == "Autonomous Engineer") {
                    for (const char* mcp : {"editor", "terminal", "git", "deck", "adaptive_card"}) {
                        if (std::find(p.allowed_mcps.begin(), p.allowed_mcps.end(), mcp) == p.allowed_mcps.end()) {
                            p.allowed_mcps.push_back(mcp);
                            modified = true;
                        }
                    }
                    if (p.max_tool_iterations < 100) {
                        p.max_tool_iterations = 100;
                        modified = true;
                    }
                    if (p.max_output_tokens < 16384) {
                        p.max_output_tokens = 16384;
                        modified = true;
                    }
                    if (p.thinking_level != "high") {
                        p.thinking_level = "high";
                        modified = true;
                    }
                    if (p.temperature > 0.15f) {
                        p.temperature = 0.1f;
                        modified = true;
                    }
                    if (p.system_prompt.find("Limit Reconnaissance") == std::string::npos) {
                        auto act_pos = p.system_prompt.find("Zero Chat Overhead & Immediate Action:");
                        std::string const action_rule = 
                            "Zero Chat Overhead & Immediate Action:\n"
                            "- When tasked with any development, implementation, bug fix, investigation, feature, or verification request:\n"
                            "- NEVER ask for confirmation, assistance, or permission to proceed or continue. Assume full authorization and execute immediately.\n"
                            "- NEVER provide documentation, roadmaps, architectural overviews, or speculative outlines about 'what should be next' in lieu of actually implementing it.\n"
                            "- NEVER stop at diagnosis. Immediately carry out the code modifications, tests, compilation, and verification in this same turn.\n"
                            "- Immediately invoke the appropriate tools (`code_read_file`, `code_write_file`, `code_apply_patch`, `run_unit_tests`, `run_local_command`, `build_and_deploy`, `update_inbox_item`) in this turn.\n"
                            "- Limit Reconnaissance: Spend AT MOST 3 to 5 tool calls reading or searching files. Once the target file and existing pattern are identified, you MUST immediately start writing tests and applying code patches (`code_apply_patch`, `code_write_file`). Never make more than 5 consecutive read/search calls before modifying code.\n"
                            "- Execute the entire sequence autonomously without stopping to ask for user permission between steps.\n"
                            "- Carry to Completion: Execute the entire lifecycle end-to-end (diagnose, test, edit, compile, verify, commit, and announce). Never halt prematurely to ask for permission or report an incomplete partial roadmap unless an insurmountable fatal error is encountered.";
                        if (act_pos != std::string::npos) {
                            p.system_prompt = p.system_prompt.substr(0, act_pos) + action_rule;
                        } else {
                            p.system_prompt += "\n\n" + action_rule;
                        }
                        modified = true;
                    }
                }
                if (p.name == "Code & Git Architect") {
                    if (p.max_tool_iterations < 100) {
                        p.max_tool_iterations = 100;
                        modified = true;
                    }
                    if (p.max_output_tokens < 16384) {
                        p.max_output_tokens = 16384;
                        modified = true;
                    }
                    if (p.thinking_level != "low") {
                        p.thinking_level = "low";
                        modified = true;
                    }
                }
                if (p.max_tool_iterations <= 0) {
                    p.max_tool_iterations = (p.name == "Autonomous Engineer" || p.name == "Code & Git Architect") ? 100 : (p.name == "Rouen Assistant" ? 50 : 10);
                    modified = true;
                }
                if (p.max_output_tokens <= 0) {
                    p.max_output_tokens = (p.name == "Autonomous Engineer" || p.name == "Code & Git Architect" || p.name == "Adaptive Card Architect") ? 16384 : 8192;
                    modified = true;
                }
                if (p.llm_config_name == "Local MLX" || p.llm_config_name == "Default" || p.llm_config_name.empty()) {
                    p.llm_config_name = "Gemini Flash";
                    modified = true;
                }
                if (p.name == "Rouen Assistant") {
                    if (p.max_tool_iterations < 100) {
                        p.max_tool_iterations = 100;
                        modified = true;
                    }
                    if (p.max_output_tokens < 16384) {
                        p.max_output_tokens = 16384;
                        modified = true;
                    }
                    if (p.temperature > 0.15f) {
                        p.temperature = 0.1f;
                        modified = true;
                    }
                    if (p.thinking_level.empty() || p.thinking_level != "high") {
                        p.thinking_level = "high";
                        modified = true;
                    }
                    if (p.system_prompt.find("NEVER reply with generic placeholder phrases") == std::string::npos) {
                        p.system_prompt += "\n- CRITICAL: Always stay strictly focused on resolving the user's specific request or question. When executing tools or commands, always formulate a complete, informative response that directly answers the question asked. NEVER reply with generic placeholder phrases like 'I have completed the requested operation.'—always report the factual findings, details, or choices requested.\n";
                        modified = true;
                    }
                    if (p.system_prompt.find("Limit Reconnaissance") == std::string::npos) {
                        auto act_pos = p.system_prompt.find("- Action & Task Execution");
                        std::string const action_rule = 
                            "- Action & Task Execution (Zero Unnecessary Chat Steps):\n"
                            "  When tasked with any development, implementation, bug fix, investigation, feature, or verification request:\n"
                            "  1. NEVER ask for confirmation, assistance, or permission to proceed or continue. Assume full authorization to execute immediately.\n"
                            "  2. NEVER provide documentation, roadmaps, architectural overviews, or speculative outlines about 'what should be next' in lieu of actually implementing it.\n"
                            "  3. NEVER stop at diagnosis. After reading files and identifying missing code or open requirements, you must immediately carry out the implementation (`code_write_file`, `code_apply_patch`, `run_unit_tests`, `build_and_deploy`, `run_local_command`) or delegate to `call_persona_autonomous_engineer` in this same turn.\n"
                            "  4. Limit Reconnaissance: Spend AT MOST 3 to 5 tool calls reading or searching files. Once the target file and existing pattern are identified, you MUST immediately start writing tests and applying code patches (`code_apply_patch`, `code_write_file`). Never make more than 5 consecutive read/search calls before modifying code.\n"
                            "  5. Carry to Completion: Execute the complete lifecycle end-to-end (diagnose, test, edit, compile, verify, commit, and announce) before replying. Only reply in chat after the work is completed, reporting concrete, factual results.\n";
                        if (act_pos != std::string::npos) {
                            auto next_pos = p.system_prompt.find("\n\n- ", act_pos);
                            if (next_pos == std::string::npos) next_pos = p.system_prompt.find("\n- ", act_pos + 1);
                            if (next_pos != std::string::npos) {
                                p.system_prompt.replace(act_pos, next_pos - act_pos, action_rule);
                            } else {
                                p.system_prompt = p.system_prompt.substr(0, act_pos) + action_rule;
                            }
                        } else {
                            p.system_prompt += "\n" + action_rule;
                        }
                        modified = true;
                    }
                    if (p.system_prompt.find("Carry to Completion") == std::string::npos) {
                        p.system_prompt += "\n- Carry to Completion: When tasked with implementation, execute the entire lifecycle end-to-end (diagnose, test, edit, compile, verify, commit, and announce). Never halt prematurely to ask for permission or report an incomplete partial roadmap unless an insurmountable fatal error is encountered.\n";
                        modified = true;
                    }
                    if (p.system_prompt.find("Adaptive Process") == std::string::npos) {
                        p.system_prompt += "\n- Adaptive Cards & Adaptive Processes: Rouen supports rich Adaptive Cards (`create_adaptive_card`) and live, dynamic Adaptive Process Cards (`create_adaptive_process_card` or `adaptive-process:<command line>`) where a local shell script (zsh on macOS, PowerShell on Windows) emits compact Adaptive Card JSON over stdout and reads submissions from stdin. When asked to design, build, or launch an adaptive process (such as an inbox status dashboard, system monitor, or process card), delegate to `call_persona_adaptive_card_architect` (or execute directly using `code_write_file` and `create_adaptive_process_card`).\n";
                        modified = true;
                    }
                    std::vector<std::string> default_subs = {"Autonomous Engineer", "Code & Git Architect", "Personal Productivity Lead", "Media & Knowledge Director", "Financial Analyst", "System Health & Metrics", "Persona Architect", "Adaptive Card Architect"};
                    for (const auto& sub : default_subs) {
                        if (std::find(p.allowed_personas.begin(), p.allowed_personas.end(), sub) == p.allowed_personas.end()) {
                            p.allowed_personas.push_back(sub);
                            modified = true;
                        }
                    }
                    std::vector<std::string> ra_mcps = {"deck", "persona", "calendar", "notes", "contacts", "terminal", "git", "editor", "rss", "wikipedia", "youtube", "alarm", "pomodoro", "mesh"};
                    for (const auto& mcp : ra_mcps) {
                        if (std::find(p.allowed_mcps.begin(), p.allowed_mcps.end(), mcp) == p.allowed_mcps.end()) {
                            p.allowed_mcps.push_back(mcp);
                            modified = true;
                        }
                    }
                }
                if (p.name == "Adaptive Card Architect") {
                    for (const char* mcp : {"terminal", "editor", "deck", "adaptive_card", "notes"}) {
                        if (std::find(p.allowed_mcps.begin(), p.allowed_mcps.end(), mcp) == p.allowed_mcps.end()) {
                            p.allowed_mcps.push_back(mcp);
                            modified = true;
                        }
                    }
                    if (p.max_tool_iterations < 100) {
                        p.max_tool_iterations = 100;
                        modified = true;
                    }
                    if (p.max_output_tokens < 16384) {
                        p.max_output_tokens = 16384;
                        modified = true;
                    }
                    if (p.thinking_level != "high") {
                        p.thinking_level = "high";
                        modified = true;
                    }
                    if (p.temperature > 0.25f) {
                        p.temperature = 0.2f;
                        modified = true;
                    }
                    if (p.description.find("Adaptive Process") == std::string::npos) {
                        p.description = "Specialized persona for designing and rendering rich Adaptive Cards and creating live Adaptive Process scripts (zsh on macOS, PowerShell on Windows) on demand.";
                        modified = true;
                    }
                    if (p.system_prompt.find("adaptive-process") == std::string::npos) {
                        p.system_prompt = 
                            "You are Adaptive Card Architect, the specialized UI/UX and process engineering expert in Rouen.\n"
                            "You design, craft, and present rich declarative Adaptive Cards and live, process-driven Adaptive Process Cards on demand.\n\n"
                            "Available Direct Tools:\n"
                            "- 'create_adaptive_process_card': Create and launch a live Adaptive Process Card on Rouen's deck. Can automatically write a local script to disk (zsh on macOS/Linux, PowerShell on Windows) and launch it.\n"
                            "- 'create_card': Open any card on Rouen's deck by URI (e.g. 'adaptive-process:<command line>').\n"
                            "- 'create_adaptive_card': Create and present static/declarative Adaptive Cards with data binding.\n"
                            "- 'code_write_file': Create and save script files.\n"
                            "- 'code_read_file': Read target files before processing.\n"
                            "- 'run_local_command': Run shell commands (e.g. test scripts, inspect directories, chmod +x).\n\n"
                            "Adaptive Process Architecture & Protocol:\n"
                            "- Schema: The card URI is 'adaptive-process:<command line>' (e.g. 'adaptive-process:zsh /path/to/script.zsh' or 'adaptive-process:powershell -ExecutionPolicy Bypass -File C:/path/to/script.ps1').\n"
                            "- Platform Shell Scripts:\n"
                            "  * macOS/Linux: Use zsh scripts ('#!/usr/bin/env zsh'). Ensure executable permissions ('chmod +x <path>').\n"
                            "  * Windows: Use PowerShell scripts ('.ps1').\n"
                            "  * Recommended script path: '$HOME/.config/rouen/scripts/' or local project directory.\n"
                            "- stdout (Adaptive Card JSON stream):\n"
                            "  * Each line emitted on stdout MUST be a complete, compact (minified single-line) Adaptive Card JSON document (no embedded raw newlines).\n"
                            "  * The process MUST emit its first card JSON immediately upon startup.\n"
                            "  * Cadence / Auto-refresh: Include top-level '\"refreshIntervalMs\": <ms>' (e.g. 2000, 5000) or run an update loop in the script.\n"
                            "- stdin (User Interaction):\n"
                            "  * When the user triggers 'Action.Submit' or 'Action.Execute', Rouen writes the JSON payload to the process's stdin as one single line.\n"
                            "  * For interactive scripts, read stdin in a loop, update internal state, and output an updated card JSON to stdout.\n"
                            "- stderr: Captured for diagnostics and displayed in Rouen's collapsible 'Process stderr' section.\n\n"
                            "Protocol Execution for User Requests (e.g. 'check out $HOME/rouen/inbox and create an adaptive process that will show each item organized by status'):\n"
                            "1. Inspect the target folder or data using 'run_local_command' (e.g. 'ls -la $HOME/rouen/inbox') or 'code_read_file' to understand structure and statuses (e.g. files with 'done_' prefix are Done; others are Pending / In Progress).\n"
                            "2. Write a robust shell script (zsh on macOS, PowerShell on Windows) using 'create_adaptive_process_card' or 'code_write_file'. Ensure valid single-line minified JSON output and an update/stdin loop.\n"
                            "3. Ensure the script is executable ('chmod +x' on Unix via 'run_local_command' if not using create_adaptive_process_card).\n"
                            "4. Open the card on the deck using 'create_adaptive_process_card' or 'create_card' with 'adaptive-process:<command line>'.\n"
                            "5. Report the script location, command line, and active card status clearly to the user.";
                        modified = true;
                    }
                }
                if (p.name == "System Health & Metrics") {
                    if (std::find(p.allowed_mcps.begin(), p.allowed_mcps.end(), "mesh") == p.allowed_mcps.end()) {
                        p.allowed_mcps.push_back("mesh");
                        modified = true;
                    }
                }
            }
            if (modified) {
                save_personas();
            }
        }

        void notify_sync(std::string_view dataset, std::string_view key, std::string_view content, bool is_deleted) const {
            if (is_sync_suppressed_) {
                return;
            }
            if (sync_hook_) {
                sync_hook_(dataset, key, content, is_deleted);
                return;
            }
            auto hook_fn = registrar::try_get<std::function<bool(std::string_view, std::string_view, std::string_view, bool)>>("universal_sync_item");
            if (hook_fn) {
                (*hook_fn)(dataset, key, content, is_deleted);
            }
        }

        void sync_persona_item(const Persona& persona, bool is_deleted = false) const {
            std::string slug = slugify(persona.name);
            std::string buffer = is_deleted ? "" : glz::write<glz::opts{.prettify = true}>(persona).value_or("");
            notify_sync("personas", slug + ".json", buffer, is_deleted);
        }

        void sync_active_persona() const {
            PersonaActiveMeta meta;
            if (active_persona_index_ < personas_.size()) {
                meta.active_persona = personas_[active_persona_index_].name;
                meta.active_index = active_persona_index_;
            }
            std::string meta_json = glz::write<glz::opts{.prettify = true}>(meta).value_or("");
            if (!meta_json.empty()) {
                notify_sync("personas", "active.json", meta_json, false);
            }
        }

        void save_personas() const {
            try {
                auto path = rouen::platform::get_user_config_directory() / "personas.json";
                
                PersonaSaveModel model;
                model.active_index = active_persona_index_;
                model.personas = personas_;

                std::string buffer = glz::write<glz::opts{.prettify = true}>(model).value_or("");
                if (!buffer.empty()) {
                    std::ofstream file(path);
                    if (file.is_open()) {
                        file << buffer;
                    }
                    notify_sync("config", "personas.json", buffer, false);
                }
            } 
            catch (const std::exception& e) {
                std::cerr << "[Persona] Exception saving personas: " << e.what() << std::endl;
            }
        }

        std::vector<Persona> personas_;
        size_t active_persona_index_{0};
        mutable sync_hook_t sync_hook_;
        mutable bool is_sync_suppressed_{false};
    };

}
