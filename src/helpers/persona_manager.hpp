#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include "glaze_include.hpp"
#include "platform_utils.hpp"

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
                "temperature", &T::temperature
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

    class PersonaManager {
    public:
        static PersonaManager& instance() {
            static PersonaManager mgr;
            return mgr;
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
            static Persona fallback{"Default Assistant", "Fallback persona", {"terminal", "editor", "deck", "adaptive_card", "wikipedia", "youtube", "git", "calendar", "weather", "alarm", "pomodoro", "notes", "contacts"}, "You are a helpful assistant.", "Default", false, {}, 0.7f};
            return fallback;
        }

        void select_persona(size_t index) {
            if (index < personas_.size()) {
                active_persona_index_ = index;
                save_personas();
            }
        }

        void add_persona(const Persona& persona) {
            personas_.push_back(persona);
            save_personas();
        }

        void update_persona(size_t index, const Persona& persona) {
            if (index < personas_.size()) {
                std::string old_name = personas_[index].name;
                std::string new_name = persona.name;
                personas_[index] = persona;

                // If name changed, update references in allowed_personas of other personas
                if (old_name != new_name && !old_name.empty()) {
                    for (auto& p : personas_) {
                        for (auto& ref : p.allowed_personas) {
                            if (ref == old_name) {
                                ref = new_name;
                            }
                        }
                    }
                }
                save_personas();
            }
        }

        void delete_persona(size_t index) {
            if (personas_.size() <= 1) {
                // Keep at least one persona
                return;
            }
            if (index < personas_.size()) {
                std::string name_to_remove = personas_[index].name;
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
            default_p.allowed_mcps = {"deck", "persona"};
            default_p.allowed_personas = {"Code & Git Architect", "Personal Productivity Lead", "Media & Knowledge Director", "Financial Analyst", "System Health & Metrics"};
            default_p.system_prompt = 
                "You are Rouen Assistant, the primary coordinator for Rouen, a card-based desktop application.\n\n"
                "Capabilities & Architecture:\n"
                "- Rouen organizes tools into visual cards (Terminal, Editor, Git, Calendar, Notes, Media, Weather, etc.).\n"
                "- You operate via a hierarchical persona network. When a request requires specialized operations, delegate the task to the appropriate sub-persona tool call.\n"
                "- Keep responses concise, clear, and helpful.";
            default_p.llm_config_name = "Default";
            default_p.enable_search = false;
            default_p.temperature = 0.7f;
            personas_.push_back(default_p);
            
            Persona dev_arch;
            dev_arch.name = "Code & Git Architect";
            dev_arch.description = "Technical hierarchy group coordinating code editing, terminal commands, Git operations, and UI card building.";
            dev_arch.allowed_mcps = {"editor", "deck"};
            dev_arch.allowed_personas = {"Terminal Specialist", "Git & GitHub Specialist", "Adaptive Card Architect"};
            dev_arch.system_prompt = "You are Code & Git Architect, leading software development and system operations in Rouen.";
            dev_arch.llm_config_name = "Default";
            dev_arch.enable_search = false;
            dev_arch.temperature = 0.2f;
            personas_.push_back(dev_arch);

            Persona prod_lead;
            prod_lead.name = "Personal Productivity Lead";
            prod_lead.description = "Productivity group persona coordinating calendar, timers, notes, contacts, and personal data retrieval.";
            prod_lead.allowed_mcps = {"deck"};
            prod_lead.allowed_personas = {"Schedule & Timekeeper", "Directory & Address Book", "Archiver of all data"};
            prod_lead.system_prompt = "You are Personal Productivity Lead, orchestrating personal organization, time management, and note archives in Rouen.";
            prod_lead.llm_config_name = "Default";
            prod_lead.enable_search = false;
            prod_lead.temperature = 0.3f;
            personas_.push_back(prod_lead);

            Persona media_dir;
            media_dir.name = "Media & Knowledge Director";
            media_dir.description = "Media & research group persona coordinating video playback, web search, RSS news, and Wikipedia knowledge.";
            media_dir.allowed_mcps = {"deck"};
            media_dir.allowed_personas = {"Media & Stream Director", "Google", "Archiver of all data"};
            media_dir.system_prompt = "You are Media & Knowledge Director, managing media consumption, news feeds, and external research.";
            media_dir.llm_config_name = "Default";
            media_dir.enable_search = false;
            media_dir.temperature = 0.4f;
            personas_.push_back(media_dir);

            Persona term_p;
            term_p.name = "Terminal Specialist";
            term_p.description = "Gated per-MCP persona dedicated strictly to running system terminal commands.";
            term_p.allowed_mcps = {"terminal"};
            term_p.system_prompt = "You are Terminal Specialist, a minimal, command-line focused utility agent.";
            term_p.llm_config_name = "Default";
            term_p.enable_search = false;
            term_p.temperature = 0.1f;
            personas_.push_back(term_p);

            Persona edit_p;
            edit_p.name = "Editor Specialist";
            edit_p.description = "Gated per-MCP persona dedicated strictly to inspecting and editing files.";
            edit_p.allowed_mcps = {"editor"};
            edit_p.system_prompt = "You are Editor Specialist, responsible for reading, writing, and editing files safely.";
            edit_p.llm_config_name = "Default";
            edit_p.enable_search = false;
            edit_p.temperature = 0.1f;
            personas_.push_back(edit_p);

            Persona adaptive_p;
            adaptive_p.name = "Adaptive Card Architect";
            adaptive_p.description = "Gated per-MCP persona specialized in designing and rendering rich Adaptive Cards.";
            adaptive_p.allowed_mcps = {"deck", "adaptive_card"};
            adaptive_p.system_prompt = "You are Adaptive Card Architect, a specialized UI/UX design expert persona in Rouen.";
            adaptive_p.llm_config_name = "Default";
            adaptive_p.enable_search = false;
            adaptive_p.temperature = 0.3f;
            personas_.push_back(adaptive_p);

            Persona git_p;
            git_p.name = "Git & GitHub Specialist";
            git_p.description = "Gated per-MCP persona dedicated to Git repositories, branches, commits, GitHub issues, PRs, and CI.";
            git_p.allowed_mcps = {"git", "github"};
            git_p.system_prompt = "You are Git & GitHub Specialist, managing version control and repository workflows.";
            git_p.llm_config_name = "Default";
            git_p.enable_search = false;
            git_p.temperature = 0.2f;
            personas_.push_back(git_p);

            Persona archive_p;
            archive_p.name = "Archiver of all data";
            archive_p.description = "Gated per-MCP librarian persona managing notes, knowledge archiving, and cross-references.";
            archive_p.allowed_mcps = {"notes"};
            archive_p.system_prompt = "As the Archiver of All Data, you serve as Rouen's precision librarian, safeguarding information across sessions.";
            archive_p.llm_config_name = "Default";
            archive_p.enable_search = false;
            archive_p.temperature = 0.0f;
            personas_.push_back(archive_p);

            Persona dir_p;
            dir_p.name = "Directory & Address Book";
            dir_p.description = "Gated per-MCP persona managing contacts, user directory, and macOS address book integration.";
            dir_p.allowed_mcps = {"contacts", "directory"};
            dir_p.system_prompt = "You are Directory & Address Book, managing contact cards and user directory entries in Rouen.";
            dir_p.llm_config_name = "Default";
            dir_p.enable_search = false;
            dir_p.temperature = 0.2f;
            personas_.push_back(dir_p);

            Persona time_p;
            time_p.name = "Schedule & Timekeeper";
            time_p.description = "Gated per-MCP persona managing schedule events, focus timers, and alarms.";
            time_p.allowed_mcps = {"calendar", "alarm", "pomodoro"};
            time_p.system_prompt = "You are Schedule & Timekeeper, managing calendar events, reminders, Pomodoro focus intervals, and alarms in Rouen.";
            time_p.llm_config_name = "Default";
            time_p.enable_search = false;
            time_p.temperature = 0.2f;
            personas_.push_back(time_p);

            Persona stream_p;
            stream_p.name = "Media & Stream Director";
            stream_p.description = "Gated per-MCP persona managing video playback, media casting, news RSS feeds, and Wikipedia summaries.";
            stream_p.allowed_mcps = {"youtube", "cast", "media", "rss", "wikipedia"};
            stream_p.system_prompt = "You are Media & Stream Director, controlling media playback, YouTube video searches, casting feeds, RSS news items, and Wikipedia article lookups.";
            stream_p.llm_config_name = "Default";
            stream_p.enable_search = false;
            stream_p.temperature = 0.3f;
            personas_.push_back(stream_p);

            Persona fin_p;
            fin_p.name = "Financial Analyst";
            fin_p.description = "Gated per-MCP persona dedicated to crypto market analytics, ticker stats, and account assets.";
            fin_p.allowed_mcps = {"bybit"};
            fin_p.system_prompt = "You are Financial Analyst, inspecting market datasets, Bybit crypto tickers, orderbook depth, and asset balances.";
            fin_p.llm_config_name = "Default";
            fin_p.enable_search = false;
            fin_p.temperature = 0.2f;
            personas_.push_back(fin_p);

            Persona health_p;
            health_p.name = "System Health & Metrics";
            health_p.description = "Gated per-MCP persona monitoring system performance, FPS, and card render metrics.";
            health_p.allowed_mcps = {"metrics"};
            health_p.system_prompt = "You are System Health & Metrics, monitoring Rouen card render frame rates, slow render counts, and application performance metrics.";
            health_p.llm_config_name = "Default";
            health_p.enable_search = false;
            health_p.temperature = 0.1f;
            personas_.push_back(health_p);
        }

        void load_personas() {
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
                auto err = glz::read_json(model, content);
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

                // Ensure "Adaptive Card Architect" persona exists
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
                    adaptive_p.description = "A specialized UI/UX designer persona expert in designing, crafting, and presenting rich, interactive Adaptive Cards in Rouen.";
                    adaptive_p.allowed_mcps = {"deck", "adaptive_card", "terminal", "editor", "notes"};
                    adaptive_p.system_prompt = 
                        "You are Adaptive Card Architect, a specialized UI/UX design expert persona in Rouen.\n\n"
                        "Objective:\n"
                        "- Design, create, and present rich, interactive Adaptive Cards for any request (e.g. flight tickets, invoices, user profiles, status dashboards, weather summaries, forms, or polls).\n"
                        "- Use the `create_adaptive_card` tool to save and present Adaptive Cards in Rouen.\n"
                        "- Craft elegant JSON card templates supporting TextBlock (bold, italic, colored), Container, ColumnSet, FactSet, Image, Input fields, Action.Submit, and Action.OpenUrl.\n"
                        "- When given data or context, provide appropriate `${var}` placeholders in `card_json` and matching key-value pairs in `context_json`.\n"
                        "- Always make card structures clear, modern, and visually delightful.";
                    adaptive_p.llm_config_name = "Default";
                    adaptive_p.enable_search = false;
                    adaptive_p.allowed_personas = {};
                    adaptive_p.temperature = 0.3f;

                    personas_.push_back(adaptive_p);
                    save_personas();
                }

                // Ensure "Rouen Assistant" includes "contacts" if missing
                for (auto& p : personas_) {
                    if (p.name == "Rouen Assistant") {
                        if (std::find(p.allowed_mcps.begin(), p.allowed_mcps.end(), "contacts") == p.allowed_mcps.end()) {
                            p.allowed_mcps.push_back("contacts");
                            save_personas();
                        }
                    }
                }
            } 
            catch (const std::exception& e) {
                std::cerr << "[Persona] Exception loading personas: " << e.what() << std::endl;
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
                }
            } 
            catch (const std::exception& e) {
                std::cerr << "[Persona] Exception saving personas: " << e.what() << std::endl;
            }
        }

        std::vector<Persona> personas_;
        size_t active_persona_index_{0};
    };

}
