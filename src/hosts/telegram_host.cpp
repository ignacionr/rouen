#include "telegram_host.hpp"

namespace rouen::hosts {

std::shared_ptr<telegram_host> telegram_host::get_host() {
    static std::mutex host_mutex;
    static std::shared_ptr<telegram_host> instance = nullptr;
    std::lock_guard<std::mutex> lock(host_mutex);
    if (!instance) {
        auto existing = registrar::try_get<telegram_host>("telegram_host");
        if (existing) {
            instance = existing;
        } else {
            instance = std::make_shared<telegram_host>();
            try {
                registrar::add("telegram_host", instance);
            } catch (...) {}
        }
    }
    return instance;
}

telegram_host::telegram_host() {
    load_state();
    start_polling();
}

telegram_host::~telegram_host() {
    stop_polling();
}

void telegram_host::load_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto path = rouen::platform::get_user_data_path("telegram.json", true);
        if (std::filesystem::exists(path)) {
            std::ifstream file(path);
            if (file.is_open()) {
                std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                telegram_host_state state;
                auto err = glz::read_json(state, json_str);
                if (!err) {
                    bot_token_ = state.bot_token;
                    last_update_id_ = state.last_update_id;
                    routes_ = state.routes;
                    sessions_map_.clear();
                    session_order_.clear();
                    for (const auto& s : state.sessions) {
                        sessions_map_[s.chat_id] = s;
                        session_order_.push_back(s.chat_id);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[TelegramHost] Failed to load state: " << e.what() << std::endl;
    }

    // Default route if empty
    if (routes_.empty()) {
        telegram_route def_route;
        def_route.id = "route_default";
        def_route.is_default = true;
        def_route.user_id = 0;
        def_route.target_type = 1; // AI Persona
        def_route.fixed_message = "Hello! I am your Telegram Bot.";
        def_route.persona_name = "Rouen Assistant";
        routes_.push_back(def_route);
    }
}

void telegram_host::save_state() {
    try {
        telegram_host_state state;
        state.bot_token = bot_token_;
        state.last_update_id = last_update_id_;
        state.routes = routes_;
        for (int64_t chat_id : session_order_) {
            auto it = sessions_map_.find(chat_id);
            if (it != sessions_map_.end()) {
                state.sessions.push_back(it->second);
            }
        }

        std::string json_str;
        (void)glz::write_json(state, json_str);
        if (!json_str.empty()) {
            auto path = rouen::platform::get_user_data_path("telegram.json", true);
            std::ofstream file(path);
            if (file.is_open()) {
                file << json_str;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[TelegramHost] Failed to save state: " << e.what() << std::endl;
    }
}

void telegram_host::set_bot_token(const std::string& token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bot_token_ == token) return;
        bot_token_ = token;
        bot_username_.clear();
        bot_first_name_.clear();
        save_state();
    }
    test_connection();
}

std::string telegram_host::get_bot_token() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bot_token_;
}

std::string telegram_host::get_status_message() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_message_;
}

std::string telegram_host::get_bot_username() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bot_username_;
}

std::vector<telegram_chat_session> telegram_host::get_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<telegram_chat_session> res;
    for (int64_t chat_id : session_order_) {
        auto it = sessions_map_.find(chat_id);
        if (it != sessions_map_.end()) {
            res.push_back(it->second);
        }
    }
    return res;
}

std::optional<telegram_chat_session> telegram_host::get_session(int64_t chat_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_map_.find(chat_id);
    if (it != sessions_map_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<telegram_route> telegram_host::get_routes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return routes_;
}

void telegram_host::set_routes(const std::vector<telegram_route>& routes) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_ = routes;
    save_state();
}

void telegram_host::add_route(const telegram_route& route) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_.push_back(route);
    save_state();
}

void telegram_host::update_route(size_t index, const telegram_route& route) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < routes_.size()) {
        routes_[index] = route;
        save_state();
    }
}

void telegram_host::delete_route(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < routes_.size()) {
        routes_.erase(routes_.begin() + static_cast<std::ptrdiff_t>(index));
        save_state();
    }
}

void telegram_host::start_polling() {
    stop_polling_ = false;
    poll_thread_ = std::thread([this]() { poll_loop(); });
}

void telegram_host::stop_polling() {
    stop_polling_ = true;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
}

void telegram_host::test_connection() {
    status_ = Status::Connecting;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_message_ = "Connecting to Telegram Bot API...";
    }
    std::thread([this]() {
        validate_token_and_fetch_bot_info();
    }).detach();
}

bool telegram_host::validate_token_and_fetch_bot_info() {
    std::string token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        token = bot_token_;
    }

    if (token.empty()) {
        status_ = Status::Disconnected;
        std::lock_guard<std::mutex> lock(mutex_);
        status_message_ = "No bot token configured.";
        return false;
    }

    try {
        std::string url = "https://api.telegram.org/bot" + token + "/getMe";
        http::fetch client(15);
        std::string res_str = client(url);
        if (res_str.empty()) {
            status_ = Status::Error;
            std::lock_guard<std::mutex> lock(mutex_);
            status_message_ = "Failed to connect to Telegram API (empty response).";
            return false;
        }

        glz::json_t json;
        auto err = glz::read_json(json, res_str);
        if (!err && json.contains("ok") && json["ok"].get<bool>()) {
            auto result = json["result"];
            std::lock_guard<std::mutex> lock(mutex_);
            if (result.contains("username")) {
                bot_username_ = result["username"].get<std::string>();
            }
            if (result.contains("first_name")) {
                bot_first_name_ = result["first_name"].get<std::string>();
            }
            status_ = Status::Active;
            status_message_ = "Connected as @" + bot_username_ + " (" + bot_first_name_ + ")";
            return true;
        } else {
            status_ = Status::Error;
            std::lock_guard<std::mutex> lock(mutex_);
            status_message_ = "Invalid Telegram bot token or API error.";
            return false;
        }
    } catch (const std::exception& ex) {
        status_ = Status::Error;
        std::lock_guard<std::mutex> lock(mutex_);
        status_message_ = std::string("Telegram API Error: ") + ex.what();
        return false;
    }
}

void telegram_host::poll_loop() {
    validate_token_and_fetch_bot_info();

    while (!stop_polling_) {
        std::string token;
        int64_t offset = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            token = bot_token_;
            offset = last_update_id_;
        }

        if (token.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        try {
            std::string url = "https://api.telegram.org/bot" + token + "/getUpdates";
            http::fetch client(35); // 35s timeout for 20s long poll

            glz::json_t req_body;
            req_body["offset"] = static_cast<double>(offset);
            req_body["timeout"] = 20.0;
            std::vector<glz::json_t> allowed = {"message"};
            req_body["allowed_updates"] = allowed;

            std::string req_json;
            glz::write_json(req_body, req_json);

            std::string res_str = client.post(url, req_json, {"Content-Type: application/json"});

            if (!res_str.empty()) {
                glz::json_t json;
                auto err = glz::read_json(json, res_str);
                if (!err && json.contains("ok") && json["ok"].get<bool>()) {
                    if (json.contains("result")) {
                        auto updates = json["result"].get<std::vector<glz::json_t>>();
                        for (const auto& update : updates) {
                            process_update(update);
                        }
                    }
                    if (status_ == Status::Error || status_ == Status::Disconnected) {
                        status_ = Status::Active;
                        std::lock_guard<std::mutex> lock(mutex_);
                        status_message_ = "Connected as @" + bot_username_;
                    }
                }
            }
        } catch (const std::exception& ex) {
            // Log & pause briefly before retrying poll
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void telegram_host::process_update(const glz::json_t& update) {
    if (!update.contains("update_id")) return;
    int64_t update_id = static_cast<int64_t>(update["update_id"].get<double>());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_update_id_ = std::max(last_update_id_, update_id + 1);
    }

    if (!update.contains("message")) return;
    auto msg_json = update["message"];
    if (!msg_json.contains("text") || !msg_json.contains("chat") || !msg_json.contains("from")) return;

    telegram_message msg;
    msg.message_id = msg_json.contains("message_id") ? static_cast<int64_t>(msg_json["message_id"].get<double>()) : 0;
    msg.timestamp = msg_json.contains("date") ? static_cast<int64_t>(msg_json["date"].get<double>()) : std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    msg.text = msg_json["text"].get<std::string>();
    msg.is_outgoing = false;

    auto from_obj = msg_json["from"];
    msg.from_id = from_obj.contains("id") ? static_cast<int64_t>(from_obj["id"].get<double>()) : 0;
    std::string first_name = from_obj.contains("first_name") ? from_obj["first_name"].get<std::string>() : "";
    std::string last_name = from_obj.contains("last_name") ? from_obj["last_name"].get<std::string>() : "";
    std::string username = from_obj.contains("username") ? from_obj["username"].get<std::string>() : "";

    if (!first_name.empty()) {
        msg.from_name = first_name + (last_name.empty() ? "" : " " + last_name);
    } else if (!username.empty()) {
        msg.from_name = "@" + username;
    } else {
        msg.from_name = std::to_string(msg.from_id);
    }

    auto chat_obj = msg_json["chat"];
    msg.chat_id = chat_obj.contains("id") ? static_cast<int64_t>(chat_obj["id"].get<double>()) : 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& session = sessions_map_[msg.chat_id];
        session.chat_id = msg.chat_id;
        session.user_id = msg.from_id;
        session.user_name = msg.from_name;
        session.last_message_text = msg.text;
        session.last_message_time = msg.timestamp;
        session.messages.push_back(msg);

        // Update session order
        auto it = std::find(session_order_.begin(), session_order_.end(), msg.chat_id);
        if (it != session_order_.end()) {
            session_order_.erase(it);
        }
        session_order_.insert(session_order_.begin(), msg.chat_id);

        save_state();
    }

    // Trigger routing engine
    route_incoming_message(msg);
}

namespace {

std::string sanitize_persona_name_for_tool(const std::string& name) {
    std::string sanitized = "call_persona_";
    for (char const c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            sanitized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            sanitized.push_back('_');
        }
    }
    std::string clean;
    for (char const c : sanitized) {
        if (c == '_' && !clean.empty() && clean.back() == '_') {
            continue;
        }
        clean.push_back(c);
    }
    return clean;
}

const rouen::helpers::Persona* find_persona_by_sanitized_name(const std::string& sanitized_name) {
    const auto& personas = rouen::helpers::PersonaManager::instance().get_personas();
    for (const auto& p : personas) {
        if (sanitize_persona_name_for_tool(p.name) == sanitized_name) {
            return &p;
        }
    }
    return nullptr;
}

std::string get_function_category(const rouen::hosts::mcp_host::function_definition& func) {
    if (func.name == "run_local_command") return "terminal";
    if (func.name == "edit_file") return "editor";
    if (func.name == "create_card" || func.name == "create_number_series_card" || func.name == "create_adaptive_card" || func.name == "list_adaptive_cards" || func.name == "get_adaptive_card") return "deck";
    if (func.name.starts_with("wikipedia_")) return "wikipedia";
    if (func.name.starts_with("youtube_")) return "youtube";
    if (func.name.starts_with("contacts_")) return "contacts";
    if (func.name == "create_alarm") return "alarm";
    return func.card_type;
}

std::string get_modular_mcp_instructions(const std::vector<std::string>& allowed_mcps) {
    std::string instr;
    auto has_mcp = [&](const std::string& name) {
        if (name == "adaptive_card" || name == "deck") {
            return std::find(allowed_mcps.begin(), allowed_mcps.end(), "deck") != allowed_mcps.end() ||
                   std::find(allowed_mcps.begin(), allowed_mcps.end(), "adaptive_card") != allowed_mcps.end();
        }
        if (name == "contacts" || name == "directory") {
            return std::find(allowed_mcps.begin(), allowed_mcps.end(), "contacts") != allowed_mcps.end() ||
                   std::find(allowed_mcps.begin(), allowed_mcps.end(), "directory") != allowed_mcps.end();
        }
        return std::find(allowed_mcps.begin(), allowed_mcps.end(), name) != allowed_mcps.end();
    };

    if (has_mcp("terminal")) {
        instr += "\nTERMINAL INSTRUCTIONS:\nYou have access to tools that can run local commands (e.g. bash commands). If the user asks you to check repository status, files, find the current date/time, or execute any shell command (including curl), use the provided `run_local_command` tool to execute them instead of giving them instructions on how to run it themselves.\n";
    }
    if (has_mcp("deck") || has_mcp("adaptive_card")) {
        instr += "\nDECK & ADAPTIVE CARD INSTRUCTIONS:\nWhen users ask you to 'open', 'show', 'create', 'design', or 'display' something, use the appropriate tool:\n- To create and present interactive, structured Adaptive Cards (such as flight passes, invoices, user profiles, status dashboards, forms, polls, or rich UI components), call `create_adaptive_card`. Provide a descriptive `title` and a valid Adaptive Card JSON structure for `card_json` (plus optional `context_json`).\n- To visualize numerical data or category comparisons, call `create_number_series_card`.\n- To open standard built-in cards, call `create_card` (e.g. 'pomodoro', 'terminal', 'git', 'calendar').\nCRITICAL INSTRUCTIONS ON DATA RETRIEVAL AND VISUALIZATION:\n1. If the user asks you to build, show, or create a card using data that can be retrieved via other tools (such as weather forecasts, git metrics, calendar events, or notes), you MUST follow a two-step process:\n- Step 1: Call the appropriate retrieval tool first to obtain real data.\n- Step 2: Call `create_adaptive_card` or `create_number_series_card` with the retrieved data.\n2. Never use placeholder data for visualization cards if there is a retrieval tool available to fetch actual data.\n";
    }
    if (has_mcp("wikipedia")) {
        instr += "\nWIKIPEDIA INSTRUCTIONS:\nCRITICAL INSTRUCTIONS ON WIKIPEDIA TOOL USAGE:\n1. If the user asks you to read, summarize, explain, or answer questions about a Wikipedia article or concept (for example: \"summarize 'The Garden of Forking Paths' by Borges\"), you MUST use retrieval tools: first search using wikipedia_search_concepts if needed to find the exact title, and then retrieve the full article using wikipedia_get_article_text. You must then summarize or answer directly in your chat response. DO NOT call wikipedia_create_card or create_card for this purpose!\n2. ONLY call wikipedia_create_card (or create_card) when the user explicitly requests to \"open\", \"show\", \"display\", or \"create\" a card/view on their screen (for example: \"open the wikipedia card for quantum computing\" or \"show the wikipedia card\").\n";
    }
    if (has_mcp("alarm")) {
        instr += "\nALARM INSTRUCTIONS:\nFor all other general alarms, timers, or reminders (e.g., 'set an alarm/timer for 20 minutes', 'alarm at 10 AM', 'remind me in 1 hour'), you MUST use the 'create_alarm' tool instead.\n";
    }
    if (has_mcp("pomodoro")) {
        instr += "\nPOMODORO INSTRUCTIONS:\nIMPORTANT: Differentiate clearly between Pomodoro and general alarms. Only use the Pomodoro tool (start_pomodoro) if the user explicitly mentions the word 'pomodoro'.\n";
    }
    if (has_mcp("calendar")) {
        instr += "\nCALENDAR INSTRUCTIONS:\nYou have access to calendar tools (`get_calendar_events`, `create_calendar_event`). Always call `get_calendar_events` when users ask about their schedule, events, meetings, appointments, or commitments for today or the coming week.\n";
    }
    if (has_mcp("notes")) {
        instr += "\nNOTES INSTRUCTIONS:\nYou have access to tools that can list, retrieve, create, update/append, or delete markdown notes. If the user wants to list/search notes, get a note's full text, write/save a new note, append information to a note, or delete a note, you MUST use the corresponding `notes_list`, `notes_get`, `notes_save`, `notes_append`, or `notes_delete` tool instead of guiding the user to do it manually.\n";
    }
    if (has_mcp("contacts") || has_mcp("directory")) {
        instr += "\nCONTACTS INSTRUCTIONS:\nYou have access to tools that can list, retrieve, create/update, delete, or import macOS contacts. If the user wants to search contacts, view contact details, save or import contacts, use the `contacts_list`, `contacts_get`, `contacts_save`, `contacts_delete`, or `contacts_import_macos` tools.\n";
    }
    return instr;
}

std::string run_ai_persona_completion(
    const rouen::helpers::Persona* target_persona,
    const std::string& incoming_text,
    const std::vector<std::pair<std::string, std::string>>& conversation_history,
    int depth
);

std::string execute_function_with_mcp_and_persona(const std::string& function_name, const std::string& args_json, int depth) {
    std::cout << "[TelegramHost] Executing tool/persona function: " << function_name << " args: " << args_json << " (depth " << depth << ")" << std::endl;
    if (depth > 5) {
        return "Error: Maximum inter-persona communication depth exceeded.";
    }

    if (function_name.starts_with("call_persona_")) {
        const auto* target_persona = find_persona_by_sanitized_name(function_name);
        if (!target_persona) {
            std::cerr << "[TelegramHost] Persona not found for function name: " << function_name << std::endl;
            return "Error: Target persona not found.";
        }

        std::string sub_msg;
        glz::json_t args_obj;
        auto err = glz::read_json(args_obj, args_json);
        if (!err && args_obj.is_object()) {
            if (args_obj.contains("message")) {
                sub_msg = args_obj["message"].get<std::string>();
            } else if (args_obj.contains("query")) {
                sub_msg = args_obj["query"].get<std::string>();
            } else if (args_obj.contains("prompt")) {
                sub_msg = args_obj["prompt"].get<std::string>();
            } else if (args_obj.contains("text")) {
                sub_msg = args_obj["text"].get<std::string>();
            } else if (args_obj.contains("instruction")) {
                sub_msg = args_obj["instruction"].get<std::string>();
            } else {
                sub_msg = args_json;
            }
        } else {
            sub_msg = args_json;
        }

        return run_ai_persona_completion(target_persona, sub_msg, {}, depth + 1);
    }

    auto mcp = registrar::try_get<rouen::helpers::mcp_service>("mcp_service");
    if (mcp) {
        try {
            auto res = mcp->execute_function(function_name, args_json);
            std::cout << "[TelegramHost] Tool result for " << function_name << ": " << (res.success ? res.result : ("Error: " + res.error_message)) << std::endl;
            return res.success ? res.result : "Error: " + res.error_message;
        } catch (const std::exception& e) {
            std::cerr << "[TelegramHost] Tool exception for " << function_name << ": " << e.what() << std::endl;
            return "Error executing function: " + std::string(e.what());
        }
    }
    return "Error: MCP service not available";
}

std::string run_ai_persona_completion(
    const rouen::helpers::Persona* target_persona,
    const std::string& incoming_text,
    const std::vector<std::pair<std::string, std::string>>& conversation_history,
    int depth
) {
    if (!target_persona) return "";

    std::cout << "[TelegramHost] Running AI persona completion for '" << target_persona->name << "' with LLM config '" << target_persona->llm_config_name << "' (depth " << depth << ")" << std::endl;

    std::string config_name = target_persona->llm_config_name;
    auto llm_config = rouen::hosts::LLMHost::get_current_config(config_name);
    
    // Check if preferred LLM config has an API key or is customized
    if (!llm_config.is_configured || (llm_config.api_key.empty() && llm_config.provider != rouen::hosts::LLMHost::Provider::CUSTOM)) {
        // Fallback to active configured API providers
        std::vector<std::string> fallbacks = {"Gemini Flash", "Grok Default", "OpenAI GPT-4"};
        for (const auto& f : fallbacks) {
            auto cfg = rouen::hosts::LLMHost::get_current_config(f);
            if (!cfg.api_key.empty()) {
                config_name = f;
                llm_config = cfg;
                break;
            }
        }
    }

    auto llm_opt = rouen::hosts::LLMHost::create_llm_instance(config_name);
    if (!llm_opt && !config_name.empty()) {
        std::vector<std::string> fallbacks = {"Gemini Flash", "Grok Default", "OpenAI GPT-4"};
        for (const auto& f : fallbacks) {
            auto cfg = rouen::hosts::LLMHost::get_current_config(f);
            if (!cfg.api_key.empty()) {
                config_name = f;
                llm_config = cfg;
                llm_opt = rouen::hosts::LLMHost::create_llm_instance(f);
                if (llm_opt) break;
            }
        }
    }

    if (!llm_opt) {
        std::cerr << "[TelegramHost] Failed to create LLM instance for " << target_persona->name << std::endl;
        return "Error: LLM instance could not be created. Please check LLM configuration in settings.";
    }

    std::time_t now_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm now_tm = *std::localtime(&now_time);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S (%A)", &now_tm);
    std::string time_instr = std::format("The current local date and time is: {}. Use this to understand relative dates like 'today', 'tomorrow', 'this week', etc.", time_buf);

    llm_opt->add_instructions(time_instr, "system");
    llm_opt->add_instructions(target_persona->system_prompt, "system");

    std::string modular_instr = get_modular_mcp_instructions(target_persona->allowed_mcps);
    if (!modular_instr.empty()) {
        llm_opt->add_instructions(modular_instr, "system");
    }

    if (!target_persona->allowed_personas.empty()) {
        std::string sub_persona_instr = "\nYou operate via a hierarchical persona network. When a request requires specialized operations, code development, or sub-domain assistance, delegate the task by invoking the appropriate persona tool call (e.g. call_persona_...).\n";
        llm_opt->add_instructions(sub_persona_instr, "system");
    }

    auto escape_json_str = [](const std::string& input) -> std::string {
        std::string out;
        for (char c : input) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += " ";
            else if (c == '\r') out += " ";
            else if (c == '\t') out += " ";
            else out.push_back(c);
        }
        return out;
    };

    std::vector<std::string> function_schemas;
    auto mcp = registrar::try_get<rouen::helpers::mcp_service>("mcp_service");
    if (mcp) {
        auto has_mcp = [&](const std::string& name) {
            if (name == "adaptive_card" || name == "deck") {
                return std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "deck") != target_persona->allowed_mcps.end() ||
                       std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "adaptive_card") != target_persona->allowed_mcps.end();
            }
            if (name == "contacts" || name == "directory") {
                return std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "contacts") != target_persona->allowed_mcps.end() ||
                       std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), "directory") != target_persona->allowed_mcps.end();
            }
            return std::find(target_persona->allowed_mcps.begin(), target_persona->allowed_mcps.end(), name) != target_persona->allowed_mcps.end();
        };

        auto functions = mcp->get_available_functions();
        for (const auto& func : functions) {
            std::string cat = get_function_category(func);
            if (!has_mcp(cat)) continue;

            std::string desc = escape_json_str(func.description.empty() ? "Operation" : func.description);
            std::string raw_schema = std::format(
                "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{}}}",
                func.name,
                desc,
                func.schema.empty() ? "{\"type\":\"object\",\"properties\":{}}" : func.schema
            );
            std::string schema;
            for (char c : raw_schema) {
                if (c != '\n' && c != '\r' && c != '\t') schema.push_back(c);
            }
            function_schemas.push_back(schema);
        }
    }

    for (const auto& allowed_sub_name : target_persona->allowed_personas) {
        const auto& personas = rouen::helpers::PersonaManager::instance().get_personas();
        const rouen::helpers::Persona* sub_p = nullptr;
        for (const auto& p : personas) {
            if (p.name == allowed_sub_name) {
                sub_p = &p;
                break;
            }
        }
        if (!sub_p) continue;

        std::string sub_func_name = sanitize_persona_name_for_tool(sub_p->name);
        std::string desc = escape_json_str("Delegates a task or asks a question to the Persona '" + sub_p->name + "'. Description: " + sub_p->description);
        std::string schema = std::format(
            "{{\"name\":\"{}\",\"description\":\"{}\",\"parameters\":{{\"type\":\"object\",\"properties\":{{\"message\":{{\"type\":\"string\",\"description\":\"The message or instruction to send to the persona.\"}}}},\"required\":[\"message\"]}}}}",
            sub_func_name, desc
        );
        function_schemas.push_back(schema);
    }

    bool is_local = (llm_config.provider == rouen::hosts::LLMHost::Provider::CUSTOM ||
                     llm_config.base_url.find("127.0.0.1") != std::string::npos ||
                     llm_config.base_url.find("localhost") != std::string::npos);

    http::fetch primary_fetcher{is_local ? 90 : 45};
    if (!is_local) primary_fetcher.set_max_retries(2);
    else primary_fetcher.set_max_retries(1);

    auto do_primary_post = [&primary_fetcher](const std::string& url, const std::string& data, auto hdr) {
        return primary_fetcher.post(url, data, hdr);
    };

    auto func_executor = [depth](const std::string& func_name, const std::string& func_args) -> std::string {
        return execute_function_with_mcp_and_persona(func_name, func_args, depth);
    };

    std::string search_mode;
    if ((llm_config.provider == rouen::hosts::LLMHost::Provider::GROK ||
         llm_config.provider == rouen::hosts::LLMHost::Provider::GEMINI) && target_persona->enable_search) {
        search_mode = "on";
    }

    std::cout << "[TelegramHost] Calling sendMessageWithFunctionCalling with " << function_schemas.size() << " schemas using model " << llm_config.model_name << std::endl;

    try {
        auto completion = llm_opt->sendMessageWithFunctionCalling(
            incoming_text,
            do_primary_post,
            func_executor,
            "user",
            llm_config.model_name,
            search_mode,
            target_persona->temperature,
            conversation_history.empty() ? nullptr : &conversation_history,
            function_schemas.empty() ? nullptr : &function_schemas
        );

        if (!completion.choices.empty() && !completion.choices[0].message.content.empty()) {
            return completion.choices[0].message.content;
        }
    } catch (const std::exception& ex) {
        std::cerr << "[TelegramHost] Primary persona completion error: " << ex.what() << ". Retrying with active API key provider..." << std::endl;
        
        http::fetch fallback_fetcher{45};
        fallback_fetcher.set_max_retries(2);
        auto do_fallback_post = [&fallback_fetcher](const std::string& url, const std::string& data, auto hdr) {
            return fallback_fetcher.post(url, data, hdr);
        };

        std::vector<std::string> fallbacks = {"Gemini Flash", "Grok Default", "OpenAI GPT-4"};
        for (const auto& f : fallbacks) {
            auto fallback_config = rouen::hosts::LLMHost::get_current_config(f);
            if (fallback_config.api_key.empty()) continue;

            auto fallback_llm_opt = rouen::hosts::LLMHost::create_llm_instance(f);
            if (fallback_llm_opt) {
                fallback_llm_opt->add_instructions(time_instr, "system");
                fallback_llm_opt->add_instructions(target_persona->system_prompt, "system");
                if (!modular_instr.empty()) fallback_llm_opt->add_instructions(modular_instr, "system");
                try {
                    auto completion = fallback_llm_opt->sendMessageWithFunctionCalling(
                        incoming_text,
                        do_fallback_post,
                        func_executor,
                        "user",
                        fallback_config.model_name,
                        "",
                        target_persona->temperature,
                        conversation_history.empty() ? nullptr : &conversation_history,
                        function_schemas.empty() ? nullptr : &function_schemas
                    );
                    if (!completion.choices.empty() && !completion.choices[0].message.content.empty()) {
                        return completion.choices[0].message.content;
                    }
                } catch (const std::exception& ex2) {
                    std::cerr << "[TelegramHost] Fallback " << f << " error: " << ex2.what() << std::endl;
                }
            }
        }
        return "Error executing request: " + std::string(ex.what());
    }

    return "";
}

} // namespace


void telegram_host::route_incoming_message(const telegram_message& msg) {
    std::string trimmed_text = msg.text;
    while (!trimmed_text.empty() && std::isspace(static_cast<unsigned char>(trimmed_text.front()))) trimmed_text.erase(0, 1);
    while (!trimmed_text.empty() && std::isspace(static_cast<unsigned char>(trimmed_text.back()))) trimmed_text.pop_back();

    if (trimmed_text == "/clear" || trimmed_text.starts_with("/clear ")) {
        clear_session_messages(msg.chat_id);
        send_telegram_message(msg.chat_id, "🧹 Chat context and conversation history have been cleared.");
        return;
    }

    telegram_route matched_route;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<telegram_route> sorted_routes = routes_;

        std::stable_sort(sorted_routes.begin(), sorted_routes.end(), [](const telegram_route& a, const telegram_route& b) {
            if (a.priority != b.priority) {
                return a.priority > b.priority;
            }
            if (a.is_default != b.is_default) {
                return !a.is_default;
            }
            return false;
        });

        for (const auto& r : sorted_routes) {
            if (!r.is_default && (r.user_id == msg.from_id || r.user_id == msg.chat_id)) {
                matched_route = r;
                found = true;
                break;
            } else if (r.is_default) {
                matched_route = r;
                found = true;
                break;
            }
        }
    }

    if (!found) return;

    if (matched_route.target_type == 0) { // Fixed message
        if (!matched_route.fixed_message.empty()) {
            send_telegram_message(msg.chat_id, matched_route.fixed_message);
        }
    } else if (matched_route.target_type == 1) { // AI Persona
        std::string persona_name = matched_route.persona_name;
        int64_t chat_id = msg.chat_id;
        std::string incoming_text = msg.text;

        std::thread([this, chat_id, incoming_text, persona_name]() {
            try {
                const auto& personas = rouen::helpers::PersonaManager::instance().get_personas();
                const rouen::helpers::Persona* target_persona = nullptr;
                for (const auto& p : personas) {
                    if (p.name == persona_name) {
                        target_persona = &p;
                        break;
                    }
                }
                if (!target_persona && !personas.empty()) {
                    target_persona = &personas[0];
                }

                if (!target_persona) return;

                std::vector<std::pair<std::string, std::string>> conversation_history;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = sessions_map_.find(chat_id);
                    if (it != sessions_map_.end()) {
                        size_t count = it->second.messages.size();
                        size_t start = (count > 20) ? (count - 20) : 0;
                        for (size_t i = start; i < count; ++i) {
                            if (i < count - 1) {
                                const auto& m = it->second.messages[i];
                                conversation_history.push_back({m.is_outgoing ? "assistant" : "user", m.text});
                            }
                        }
                    }
                }

                while (!conversation_history.empty() && conversation_history.front().first == "assistant") {
                    conversation_history.erase(conversation_history.begin());
                }

                std::string reply = run_ai_persona_completion(target_persona, incoming_text, conversation_history, 0);
                if (reply.empty()) {
                    reply = "I processed your request, but no text response was returned. Please try again.";
                }
                send_telegram_message(chat_id, reply);
            } catch (const std::exception& ex) {
                std::cerr << "[TelegramHost] Thread exception: " << ex.what() << std::endl;
                send_telegram_message(chat_id, "Error executing request: " + std::string(ex.what()));
            }
        }).detach();
    }
}

bool telegram_host::send_manual_message(int64_t chat_id, const std::string& text) {
    return send_telegram_message(chat_id, text);
}

bool telegram_host::inject_incoming_message(int64_t chat_id, int64_t from_id, const std::string& from_name, const std::string& text) {
    if (text.empty()) return false;

    telegram_message msg;
    msg.message_id = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    msg.from_id = (from_id != 0) ? from_id : chat_id;
    msg.from_name = from_name.empty() ? "User" : from_name;
    msg.chat_id = chat_id;
    msg.text = text;
    msg.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    msg.is_outgoing = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& session = sessions_map_[chat_id];
        session.chat_id = chat_id;
        session.user_id = msg.from_id;
        session.user_name = msg.from_name;
        session.last_message_text = text;
        session.last_message_time = msg.timestamp;
        session.messages.push_back(msg);

        auto it = std::find(session_order_.begin(), session_order_.end(), chat_id);
        if (it != session_order_.end()) {
            session_order_.erase(it);
        }
        session_order_.insert(session_order_.begin(), chat_id);

        save_state();
    }

    route_incoming_message(msg);
    return true;
}

bool telegram_host::clear_session_messages(int64_t chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_map_.find(chat_id);
    if (it != sessions_map_.end()) {
        it->second.messages.clear();
        it->second.last_message_text = "Context cleared";
        it->second.last_message_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        save_state();
        return true;
    }
    return false;
}

bool telegram_host::send_telegram_message(int64_t chat_id, const std::string& text, telegram_message* out_msg) {
    if (text.empty()) return false;

    telegram_message msg;
    msg.message_id = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    msg.chat_id = chat_id;
    msg.from_id = 0;
    msg.from_name = bot_username_.empty() ? "Bot" : ("@" + bot_username_);
    msg.text = text;
    msg.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    msg.is_outgoing = true;

    // Always store message in local session history
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& session = sessions_map_[chat_id];
        session.chat_id = chat_id;
        session.last_message_text = text;
        session.last_message_time = msg.timestamp;
        session.messages.push_back(msg);

        auto it = std::find(session_order_.begin(), session_order_.end(), chat_id);
        if (it != session_order_.end()) {
            session_order_.erase(it);
        }
        session_order_.insert(session_order_.begin(), chat_id);

        save_state();
    }

    if (out_msg) *out_msg = msg;

    std::string token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        token = bot_token_;
    }

    if (token.empty()) return true;

    try {
        std::string url = "https://api.telegram.org/bot" + token + "/sendMessage";
        http::fetch client(15);

        glz::json_t req_body;
        req_body["chat_id"] = chat_id;
        req_body["text"] = text;

        std::string req_json;
        glz::write_json(req_body, req_json);

        std::string res_str = client.post(url, req_json, {"Content-Type: application/json"});

        if (!res_str.empty()) {
            glz::json_t json;
            auto err = glz::read_json(json, res_str);
            if (!err && json.contains("ok") && json["ok"].get<bool>()) {
                if (json.contains("result") && json["result"].contains("message_id")) {
                    int64_t real_id = static_cast<int64_t>(json["result"]["message_id"].get<double>());
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto& session = sessions_map_[chat_id];
                    if (!session.messages.empty() && session.messages.back().text == text) {
                        session.messages.back().message_id = real_id;
                        save_state();
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[TelegramHost] send_telegram_message Telegram API warning: " << ex.what() << std::endl;
    }

    return true;
}

} // namespace rouen::hosts
