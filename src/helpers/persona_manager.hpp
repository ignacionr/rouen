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
            static Persona fallback{"Default Assistant", "Fallback persona", {"terminal", "editor", "deck", "wikipedia", "youtube", "git", "calendar", "weather", "alarm", "pomodoro"}, "You are a helpful assistant.", "Default", false, {}, 0.7f};
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
            default_p.description = "The default helpful assistant for Rouen with standard system prompt instructions.";
            default_p.allowed_mcps = {"terminal", "editor", "deck", "wikipedia", "youtube", "git", "calendar", "weather", "alarm", "pomodoro", "notes"};
            default_p.system_prompt = 
                "You are a helpful AI assistant integrated into Rouen, a card-based desktop application. "
                "Rouen organizes its UI as cards - each feature (weather, git, terminal, etc.) is a visual card that can be opened, closed, and interacted with.\n"
                "You are knowledgeable, accurate, and provide helpful responses.";
            default_p.llm_config_name = "Default";
            default_p.enable_search = false;
            default_p.allowed_personas = {};
            default_p.temperature = 0.7f;
            
            personas_.push_back(default_p);
            
            // Add a second interesting example persona
            Persona developer_p;
            developer_p.name = "Terminal Hack";
            developer_p.description = "A command-line focused persona. Quiet, concise, and focused on executing commands.";
            developer_p.allowed_mcps = {"terminal", "editor", "git"};
            developer_p.system_prompt = 
                "You are a terminal-focused utility bot. You speak in a minimal, tech-focused tone. "
                "You have access to terminal commands, git, and editor tools to modify files and investigate the system.";
            developer_p.llm_config_name = "Default";
            developer_p.enable_search = false;
            developer_p.allowed_personas = {};
            developer_p.temperature = 0.2f;
            
            personas_.push_back(developer_p);
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
