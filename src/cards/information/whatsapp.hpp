#pragma once

// 1. Standard includes in alphabetic order
#include <atomic>
#include <chrono>
#include <ctime>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/imgui_include.hpp"
#include <sqlite3.h>

// 3. All other includes
#include "../../helpers/fetch.hpp"
#include "../../helpers/llm_config.hpp"
#include "../../registrar.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

class whatsapp : public card {
public:
    // Apple Core Data timestamps start at 2001-01-01; Unix epoch offset is 978307200 seconds.
    static constexpr std::int64_t kCoreDataEpochOffset = 978307200;

    struct chat_session {
        int64_t pk{};
        std::string jid;
        std::string partner_name;
        std::string last_message_text;
        int64_t unread_count{};
        double last_message_date{};
    };

    struct message {
        std::string text;
        bool from_me{};
        std::string push_name;
        double date{};
    };

    explicit whatsapp(std::string_view db_path = {}) {
        colors[0] = {0.07f, 0.58f, 0.38f, 1.0f}; // WhatsApp green
        colors[1] = {0.10f, 0.72f, 0.47f, 0.7f};  // lighter green
        get_color(2, ImVec4(0.85f, 0.95f, 0.85f, 1.0f)); // incoming bubble bg
        get_color(3, ImVec4(0.22f, 0.75f, 0.50f, 1.0f)); // outgoing bubble bg
        get_color(4, ImVec4(0.7f,  0.7f,  0.7f,  1.0f)); // secondary text
        get_color(5, ImVec4(1.0f,  1.0f,  1.0f,  1.0f)); // primary text

        name("WhatsApp");
        width  = 1200.0f;
        requested_fps = 2;

        db_path_ = db_path.empty() ? default_db_path() : std::string(db_path);
        refresh();
    }

    ~whatsapp() override {
        stop_refresh();
        // Bump both tokens so any in-flight detached AI threads discard their results.
        ++ai_->summary_token;
        ++ai_->reply_token;
    }

    bool render() override {
        // Periodic data refresh
        auto now = std::chrono::steady_clock::now();
        if (now - last_refresh_ > std::chrono::seconds(30)) {
            refresh();
        }

        // Auto-trigger AI summary when a new session's messages are ready.
        // Must be outside render_window (and outside data_mutex_) to avoid
        // any lock-order issue with ai_->mutex.
        maybe_auto_summarise();

        return render_window([this]() {
            render_content();
        });
    }

    std::string get_uri() const override { return "whatsapp"; }

private:
    // ── Data ─────────────────────────────────────────────────────────────────
    std::string db_path_;
    std::mutex  data_mutex_;

    std::vector<chat_session> sessions_;
    std::vector<message>      messages_;
    int                       selected_session_idx_{-1};
    int64_t                   loaded_session_pk_{-1};
    std::string               error_msg_;

    std::chrono::steady_clock::time_point last_refresh_{};
    std::jthread refresh_thread_;

    // ── AI shared state ───────────────────────────────────────────────────────
    // Lives in a shared_ptr so detached threads can safely outlive the card.
    struct ai_shared_state {
        std::mutex mutex; // guards all string/map/set fields below

        std::string summary;
        int64_t     summary_session_pk{-1};
        std::string reply;

        // Who the other party/parties are — keyed by session pk.
        std::map<int64_t, std::string> party_descriptions;
        std::set<int64_t>              party_desc_in_flight; // sessions being processed

        std::atomic<uint64_t> summary_token{0};
        std::atomic<uint64_t> reply_token{0};
        std::atomic<bool>     summary_loading{false};
        std::atomic<bool>     reply_loading{false};
    };
    std::shared_ptr<ai_shared_state> ai_{std::make_shared<ai_shared_state>()};

    static constexpr int kAiContextMessages = 50;

    // ── Helpers ──────────────────────────────────────────────────────────────

    // Returns true when the string looks like binary/protobuf data that WhatsApp
    // stores in ZLASTMESSAGETEXT for media messages (images, audio, stickers, etc.).
    // Criteria: contains a null byte OR >10% of bytes are non-printable control chars
    // OR the string is long with no whitespace (base64-ish blob).
    static bool is_binary_text(const std::string& s) {
        if (s.empty()) return false;
        int ctrl = 0;
        bool has_space = false;
        for (char raw : s) {
            auto c = static_cast<unsigned char>(raw);
            if (c == 0) return true;
            if (c < 32 && c != '\t' && c != '\n' && c != '\r') ++ctrl;
            if (c == ' ') has_space = true;
        }
        if (ctrl * 10 > static_cast<int>(s.size())) return true;
        // Long string with no spaces and using base64 alphabet → encoded blob
        if (s.size() > 40 && !has_space) {
            int b64 = 0;
            for (char raw : s) {
                auto c = static_cast<unsigned char>(raw);
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')
                    ++b64;
            }
            if (b64 * 100 >= static_cast<int>(s.size()) * 90) return true; // >=90% base64 chars
        }
        return false;
    }

    // Return human-readable text or a placeholder for media/binary content.
    static std::string sanitize_text(const std::string& s) {
        if (s.empty()) return s;
        if (is_binary_text(s)) return "(media)";
        return s;
    }

    // Is this JID just a phone number (not a real saved contact name)?
    static bool jid_is_phone_like(const std::string& jid) {
        // WhatsApp JIDs look like "+5491112345678@s.whatsapp.net" or bare phone numbers.
        for (char c : jid) {
            if (!std::isdigit(static_cast<unsigned char>(c)) &&
                c != '+' && c != '@' && c != '.' && c != '-' && c != '_')
                return false;
        }
        return !jid.empty();
    }

    static std::string default_db_path() {
        const char* home = std::getenv("HOME");
        std::string base = home ? home : "~";
        return base + "/Library/Group Containers/group.net.whatsapp.WhatsApp.shared/ChatStorage.sqlite";
    }

    static std::string format_timestamp(double core_data_ts) {
        if (core_data_ts <= 0.0) return "";
        std::time_t unix_ts = static_cast<std::time_t>(core_data_ts) + static_cast<std::time_t>(kCoreDataEpochOffset);
        const std::tm* tm_ptr = std::localtime(&unix_ts); // NOLINT(concurrency-mt-unsafe)
        if (!tm_ptr) return "";
        char buf[32];
        std::strftime(buf, sizeof(buf), "%d/%m %H:%M", tm_ptr);
        return buf;
    }

    // ── SQLite RAII wrapper ───────────────────────────────────────────────────
    struct db_handle {
        sqlite3* db{nullptr};
        ~db_handle() { if (db) sqlite3_close(db); }
        explicit operator bool() const { return db != nullptr; }
    };

    struct stmt_handle {
        sqlite3_stmt* stmt{nullptr};
        ~stmt_handle() { if (stmt) sqlite3_finalize(stmt); }
        explicit operator bool() const { return stmt != nullptr; }
    };

    // ── Background refresh ───────────────────────────────────────────────────
    void stop_refresh() {
        if (refresh_thread_.joinable()) refresh_thread_.request_stop();
    }

    void refresh() {
        stop_refresh();
        last_refresh_ = std::chrono::steady_clock::now();
        refresh_thread_ = std::jthread([this](std::stop_token st) {
            if (st.stop_requested()) return;
            load_sessions();
            if (!st.stop_requested() && loaded_session_pk_ >= 0) {
                load_messages(loaded_session_pk_);
            }
        });
    }

    void load_sessions() {
        db_handle db;
        int rc = sqlite3_open_v2(db_path_.c_str(), &db.db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            std::lock_guard lock(data_mutex_);
            error_msg_ = std::format("Cannot open DB: {}", sqlite3_errmsg(db.db));
            return;
        }

        const char* sql =
            "SELECT Z_PK, ZCONTACTJID, ZPARTNERNAME, ZLASTMESSAGETEXT, "
            "       ZUNREADCOUNT, ZLASTMESSAGEDATE "
            "FROM ZWACHATSESSION "
            "WHERE ZREMOVED = 0 "
            "  AND ZSESSIONTYPE IN (0, 1) "   // 0=individual, 1=group; exclude status(3) & channels(5)
            "ORDER BY ZLASTMESSAGEDATE DESC;";

        stmt_handle stmt;
        if (sqlite3_prepare_v2(db.db, sql, -1, &stmt.stmt, nullptr) != SQLITE_OK) {
            std::lock_guard lock(data_mutex_);
            error_msg_ = std::format("Prepare failed: {}", sqlite3_errmsg(db.db));
            return;
        }

        std::vector<chat_session> rows;
        while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
            chat_session s;
            s.pk                = sqlite3_column_int64(stmt.stmt, 0);
            auto* jid           = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 1));
            auto* pname         = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 2));
            auto* last_txt      = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 3));
            s.jid               = jid    ? jid    : "";
            s.partner_name      = pname  ? pname  : s.jid;
            s.last_message_text = sanitize_text(last_txt ? last_txt : "");
            s.unread_count      = sqlite3_column_int64(stmt.stmt, 4);
            s.last_message_date = sqlite3_column_double(stmt.stmt, 5);
            rows.push_back(std::move(s));
        }

        std::lock_guard lock(data_mutex_);
        sessions_ = std::move(rows);
        error_msg_.clear();
    }

    void load_messages(int64_t session_pk) {
        db_handle db;
        int rc = sqlite3_open_v2(db_path_.c_str(), &db.db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) return;

        const char* sql =
            "SELECT ZTEXT, ZISFROMME, ZPUSHNAME, ZMESSAGEDATE "
            "FROM ZWAMESSAGE "
            "WHERE ZCHATSESSION = ? "
            "ORDER BY ZSORT ASC;";

        stmt_handle stmt;
        if (sqlite3_prepare_v2(db.db, sql, -1, &stmt.stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(stmt.stmt, 1, session_pk);

        std::vector<message> rows;
        while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
            message m;
            auto* txt   = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 0));
            auto* pname = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 2));
            m.text      = sanitize_text(txt ? txt : "");
            m.from_me   = sqlite3_column_int(stmt.stmt, 1) != 0;
            m.push_name = pname ? pname : "";
            m.date      = sqlite3_column_double(stmt.stmt, 3);
            if (!m.text.empty()) rows.push_back(std::move(m));
        }

        std::lock_guard lock(data_mutex_);
        messages_        = std::move(rows);
        loaded_session_pk_ = session_pk;
    }

    // ── AI helpers ────────────────────────────────────────────────────────────

    std::string build_transcript(const std::vector<message>& msgs, int max_count) const {
        std::string out;
        int start = std::max(0, static_cast<int>(msgs.size()) - max_count);
        for (int i = start; i < static_cast<int>(msgs.size()); ++i) {
            const auto& m = msgs[static_cast<size_t>(i)];
            out += m.from_me ? "Me" : (m.push_name.empty() ? "Them" : m.push_name);
            out += ": ";
            out += m.text;
            out += '\n';
        }
        return out;
    }

    // Called from render() — no lock held — checks if we should auto-trigger a summary.
    void maybe_auto_summarise() {
        if (ai_->summary_loading.load()) return;

        int64_t     lpk{-1};
        int64_t     spk{-1};
        int         sel{-1};
        std::vector<message>      msgs_copy;
        std::string partner_name;
        {
            std::lock_guard lock(data_mutex_);
            lpk = loaded_session_pk_;
            sel = selected_session_idx_;
            {
                std::lock_guard alock(ai_->mutex);
                spk = ai_->summary_session_pk;
            }
            if (lpk < 0 || sel < 0 || sel >= static_cast<int>(sessions_.size())) return;
            if (lpk == spk) return; // already have a summary for this session
            if (messages_.empty()) return;
            msgs_copy    = messages_;
            partner_name = sessions_[static_cast<size_t>(sel)].partner_name;
            if (partner_name.empty()) partner_name = sessions_[static_cast<size_t>(sel)].jid;
        }
        trigger_ai_summary(lpk, std::move(msgs_copy), std::move(partner_name));
    }

    // Launches a detached thread for the summary.  Thread captures ai_ by value
    // (shared_ptr) so it is safe even if the card is destroyed mid-flight.
    void trigger_ai_summary(int64_t session_pk, std::vector<message> msgs,
                            std::string pname) {
        if (ai_->summary_loading.load()) return;
        if (msgs.empty()) return;

        uint64_t token = ++ai_->summary_token;
        ai_->summary_loading.store(true);

        std::string transcript_str = build_transcript(msgs, kAiContextMessages);
        auto ai = ai_;

        std::thread([ai, token, session_pk,
                     transcript   = std::move(transcript_str),
                     partner_name = std::move(pname)]() mutable {
            std::string result;
            try {
                if (!rouen::helpers::LLMConfig::is_configured()) {
                    result = "(AI not configured — set an LLM API key in Settings)";
                } else if (auto llm = rouen::helpers::LLMConfig::create_llm_instance()) {
                    auto settings = rouen::helpers::LLMConfig::get_current_config();
                    llm->add_instructions(
                        "You are a concise conversation summarizer. "
                        "Given a WhatsApp chat transcript, write a brief 3-5 sentence summary "
                        "of the key topics, decisions, and tone. Plain text only.");
                    std::string prompt = std::format(
                        "Summarize this WhatsApp conversation with {}:\n\n{}", partner_name, transcript);

                    if (ai->summary_token.load() == token) {
                        http::fetch fetcher{60};
                        auto resp = llm->sendMessage(prompt,
                            [&fetcher](const std::string& url, const std::string& data, auto hdr) {
                                return fetcher.post(url, data, hdr);
                            },
                            "user", settings.model_name);
                        result = resp.choices[0].message.content;
                    }
                } else {
                    result = "(Could not create LLM instance)";
                }
            } catch (const std::exception& e) {
                result = std::format("(AI error: {})", e.what());
            }

            if (ai->summary_token.load() == token) {
                std::lock_guard lock(ai->mutex);
                ai->summary             = std::move(result);
                ai->summary_session_pk  = session_pk;
            }
            ai->summary_loading.store(false);
        }).detach();
    }

    // Launches a detached thread for the candidate reply.
    void trigger_ai_reply(std::vector<message> msgs, std::string pname) {
        if (ai_->reply_loading.load()) return;
        if (msgs.empty()) return;

        uint64_t token = ++ai_->reply_token;
        ai_->reply_loading.store(true);

        std::string transcript_str = build_transcript(msgs, kAiContextMessages);
        auto ai = ai_;

        std::thread([ai, token,
                     transcript   = std::move(transcript_str),
                     partner_name = std::move(pname)]() mutable {
            std::string result;
            try {
                if (!rouen::helpers::LLMConfig::is_configured()) {
                    result = "(AI not configured)";
                } else if (auto llm = rouen::helpers::LLMConfig::create_llm_instance()) {
                    auto settings = rouen::helpers::LLMConfig::get_current_config();
                    llm->add_instructions(
                        "You are a helpful assistant drafting WhatsApp replies on behalf of the user (signed as 'Me'). "
                        "Write a single, natural, friendly reply that fits the conversation context. "
                        "Be concise — 1-3 sentences. Plain text only, no quotes, no 'Me:' prefix.");
                    std::string prompt = std::format(
                        "Conversation with {}:\n\n{}\n\nWrite a suitable reply:", partner_name, transcript);

                    if (ai->reply_token.load() == token) {
                        http::fetch fetcher{60};
                        auto resp = llm->sendMessage(prompt,
                            [&fetcher](const std::string& url, const std::string& data, auto hdr) {
                                return fetcher.post(url, data, hdr);
                            },
                            "user", settings.model_name);
                        result = resp.choices[0].message.content;
                    }
                } else {
                    result = "(Could not create LLM instance)";
                }
            } catch (const std::exception& e) {
                result = std::format("(AI error: {})", e.what());
            }

            if (ai->reply_token.load() == token) {
                std::lock_guard lock(ai->mutex);
                ai->reply = std::move(result);
            }
            ai->reply_loading.store(false);
        }).detach();
    }

    // Strip a leading "Me: " prefix (case-insensitive) that the LLM sometimes adds.
    static std::string strip_me_prefix(std::string_view text) {
        if (text.size() >= 4) {
            auto prefix = text.substr(0, 4);
            if (prefix == "Me: " || prefix == "me: ") return std::string(text.substr(4));
        }
        return std::string(text);
    }

    // Query ZWAGROUPMEMBER for a session and return a comma-separated list of names.
    static std::string load_group_members_str(const std::string& db_path, int64_t session_pk) {
        db_handle db;
        if (sqlite3_open_v2(db_path.c_str(), &db.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            return {};

        const char* sql =
            "SELECT ZCONTACTNAME, ZFIRSTNAME, ZMEMBERJID "
            "FROM ZWAGROUPMEMBER "
            "WHERE ZCHATSESSION = ? AND ZISACTIVE = 1 "
            "LIMIT 30;";
        stmt_handle stmt;
        if (sqlite3_prepare_v2(db.db, sql, -1, &stmt.stmt, nullptr) != SQLITE_OK) return {};
        sqlite3_bind_int64(stmt.stmt, 1, session_pk);

        std::string result;
        while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
            auto* cname = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 0));
            auto* fname = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 1));
            auto* jid   = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 2));
            std::string name = cname ? cname : (fname ? fname : (jid ? jid : "?"));
            if (!result.empty()) result += ", ";
            result += name;
        }
        return result;
    }

    // Fire a detached AI call to identify the other party/parties in a session.
    // Safe to call while holding data_mutex_ (uses only ai_->mutex for writes).
    void trigger_party_description(const chat_session& session) {
        auto pk       = session.pk;
        auto jid      = session.jid;
        auto pname    = session.partner_name;
        auto last_msg = session.last_message_text;
        bool is_group = jid.find("@g.us") != std::string::npos;

        {
            std::lock_guard al(ai_->mutex);
            if (ai_->party_desc_in_flight.count(pk)) return; // already running
            ai_->party_desc_in_flight.insert(pk);
        }

        auto ai           = ai_;
        std::string db_cp = db_path_; // local copy for capture

        std::thread([ai, pk, jid, pname, last_msg, is_group,
                     db_path = std::move(db_cp)]() mutable {
            std::string result;
            try {
                // Gather extra context
                std::string members_str;
                if (is_group) {
                    members_str = load_group_members_str(db_path, pk);
                }

                // For groups without any member data, skip the AI call — it will
                // only produce "group with unknown members" which is useless.
                // Build a sensible fallback instead.
                if (is_group && members_str.empty()) {
                    result = "Group chat";
                } else if (!rouen::helpers::LLMConfig::is_configured()) {
                    result = "(AI not configured)";
                } else if (auto llm = rouen::helpers::LLMConfig::create_llm_instance()) {
                    auto settings = rouen::helpers::LLMConfig::get_current_config();
                    llm->add_instructions(
                        "You are a contact-identifier assistant. Given details about a WhatsApp "
                        "conversation, write a single short phrase (max 12 words) describing "
                        "the CONTEXT or RELATIONSHIP — do NOT just restate the person's name. "
                        "Focus on who they are relative to the user (friend, colleague, family, "
                        "business, etc.) or what the conversation is about. "
                        "Plain text only, no quotes, no trailing period.");

                    std::string prompt;
                    if (is_group) {
                        // We have members_str here (non-empty branch above handles empty)
                        prompt = std::format(
                            "Group chat named '{}'. Members: {}. Last message snippet: '{}'.\n"
                            "Describe what this group is about or who these people are.",
                            pname, members_str, last_msg.substr(0, 120));
                    } else {
                        // Extract phone number from JID (format: +123456@s.whatsapp.net)
                        std::string phone = jid;
                        auto at = phone.find('@');
                        if (at != std::string::npos) phone = phone.substr(0, at);

                        prompt = std::format(
                            "WhatsApp contact. Saved name: '{}'. Phone: '{}'. "
                            "Last message snippet: '{}'.\n"
                            "Describe who this person is or your relationship — "
                            "do NOT just say their name.",
                            pname, phone, last_msg.substr(0, 120));
                    }

                    http::fetch fetcher{30};
                    auto resp = llm->sendMessage(prompt,
                        [&fetcher](const std::string& url, const std::string& data, auto hdr) {
                            return fetcher.post(url, data, hdr);
                        },
                        "user", settings.model_name);
                    result = resp.choices[0].message.content;
                    // Strip any trailing period the model may have added
                    if (!result.empty() && result.back() == '.') result.pop_back();
                } else {
                    result = "(LLM unavailable)";
                }
            } catch (const std::exception& e) {
                result = std::format("(error: {})", e.what());
            }

            std::lock_guard al(ai->mutex);
            ai->party_descriptions[pk] = std::move(result);
            ai->party_desc_in_flight.erase(pk);
        }).detach();
    }

    // ── Rendering ────────────────────────────────────────────────────────────
    void render_content() {
        std::lock_guard lock(data_mutex_);

        if (!error_msg_.empty()) {
            ImGui::TextColored(ImVec4(1,0,0,1), "Error: %s", error_msg_.c_str());
            return;
        }

        if (sessions_.empty()) {
            ImGui::TextColored(colors[4], "Loading chats...");
            return;
        }

        // ── Toolbar ───────────────────────────────────────────────────────────
        ImGui::TextColored(colors[0], "WhatsApp  ");
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            // queue refresh outside of the lock – but we hold it here, so just
            // flag next frame via resetting timer
            last_refresh_ = std::chrono::steady_clock::time_point{};
        }
        ImGui::Separator();

        // ── Two-column layout: chat list | message view ───────────────────────
        const float list_width = std::min(420.0f, ImGui::GetContentRegionAvail().x * 0.38f);
        ImGui::BeginChild("ChatList", ImVec2(list_width, 0), true);
        render_chat_list();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("MessageView", ImVec2(0, 0), true);
        render_message_view();
        ImGui::EndChild();
    }

    void render_chat_list() {
        // Snapshot party-description state (brief lock separate from data_mutex_).
        std::map<int64_t, std::string> party_descs;
        std::set<int64_t>              in_flight;
        {
            std::lock_guard al(ai_->mutex);
            party_descs = ai_->party_descriptions;
            in_flight   = ai_->party_desc_in_flight;
        }

        const float item_h    = ImGui::GetTextLineHeightWithSpacing() * 3.6f;
        const float avail_w   = ImGui::GetContentRegionAvail().x;
        const float btn_w     = ImGui::CalcTextSize("?").x + ImGui::GetStyle().FramePadding.x * 2.f;

        for (int i = 0; i < static_cast<int>(sessions_.size()); ++i) {
            const auto& s          = sessions_[static_cast<size_t>(i)];
            bool        is_selected = (i == selected_session_idx_);
            std::string display_name = s.partner_name.empty() ? s.jid : s.partner_name;

            ImGui::PushID(i);

            // ── Selectable row ──────────────────────────────────────────────
            if (ImGui::Selectable("##row", is_selected,
                                  ImGuiSelectableFlags_AllowOverlap, ImVec2(avail_w, item_h))) {
                if (selected_session_idx_ != i) {
                    selected_session_idx_ = i;
                    loaded_session_pk_    = -1;
                    messages_.clear();
                    ++ai_->reply_token;
                    { std::lock_guard al(ai_->mutex); ai_->reply.clear(); }
                    auto pk = s.pk;
                    stop_refresh();
                    refresh_thread_ = std::jthread([this, pk](std::stop_token) {
                        load_messages(pk);
                    });
                    // Trigger party description on first visit.
                    if (!party_descs.count(pk) && !in_flight.count(pk)) {
                        trigger_party_description(s);
                    }
                }
            }

            // Overlay content inside the selectable region
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - avail_w);

            ImGui::BeginGroup();

            // Row 1: unread badge + name + timestamp (right) + refresh button (right)
            if (s.unread_count > 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, colors[0]);
                ImGui::Text(" %lld ", static_cast<long long>(s.unread_count));
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }

            ImGui::TextColored(is_selected ? colors[0] : colors[5], "%s", display_name.c_str());

            // Timestamp — right-aligned
            std::string ts = format_timestamp(s.last_message_date);
            float ts_w = ImGui::CalcTextSize(ts.c_str()).x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(avail_w - ts_w - btn_w - ImGui::GetStyle().ItemSpacing.x * 2.f);
            ImGui::TextColored(colors[4], "%s", ts.c_str());

            // Refresh-description button — far right
            ImGui::SameLine();
            ImGui::SetCursorPosX(avail_w - btn_w);
            bool desc_loading = in_flight.count(s.pk) > 0;
            ImGui::BeginDisabled(desc_loading);
            if (ImGui::SmallButton(desc_loading ? "…" : "?")) {
                trigger_party_description(s);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Identify party with AI");
            ImGui::EndDisabled();

            // Row 2: last message preview
            if (!s.last_message_text.empty()) {
                std::string preview = s.last_message_text.size() > 52
                    ? s.last_message_text.substr(0, 49) + "..."
                    : s.last_message_text;
                ImGui::TextColored(colors[4], "  %s", preview.c_str());
            } else {
                ImGui::Spacing();
            }

            // Row 3: AI party description
            auto it = party_descs.find(s.pk);
            if (desc_loading) {
                ImGui::TextColored(colors[4], "  Identifying…");
            } else if (it != party_descs.end() && !it->second.empty()) {
                // Truncate to one line
                std::string desc = it->second.size() > 60
                    ? it->second.substr(0, 57) + "..." : it->second;
                ImGui::TextColored(colors[1], "  %s", desc.c_str());
            } else {
                ImGui::TextColored(colors[4], " "); // keep row height consistent
            }

            ImGui::EndGroup();
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    void render_message_view() {
        if (selected_session_idx_ < 0 ||
            selected_session_idx_ >= static_cast<int>(sessions_.size())) {
            ImGui::TextColored(colors[4], "Select a chat to read messages.");
            return;
        }

        const auto& session = sessions_[static_cast<size_t>(selected_session_idx_)];
        std::string title = session.partner_name.empty() ? session.jid : session.partner_name;

        // Snapshot AI state (brief lock, separate from data_mutex_).
        std::string ai_summary, ai_reply;
        int64_t     ai_summary_pk{-1};
        bool        summary_loading = ai_->summary_loading.load();
        bool        reply_loading   = ai_->reply_loading.load();
        {
            std::lock_guard al(ai_->mutex);
            ai_summary    = ai_->summary;
            ai_summary_pk = ai_->summary_session_pk;
            ai_reply      = ai_->reply;
        }

        // ── Header row ────────────────────────────────────────────────────────
        ImGui::TextColored(colors[0], "%s", title.c_str());
        ImGui::SameLine();

        ImGui::BeginDisabled(summary_loading || messages_.empty());
        if (ImGui::SmallButton(summary_loading ? "Summarising..." : "Summarise")) {
            trigger_ai_summary(session.pk, messages_, title);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(reply_loading || messages_.empty());
        if (ImGui::SmallButton(reply_loading ? "Generating..." : "Generate reply")) {
            ++ai_->reply_token;
            { std::lock_guard al(ai_->mutex); ai_->reply.clear(); }
            trigger_ai_reply(messages_, title);
        }
        ImGui::EndDisabled();

        // Copy button — strip any leading "Me: " the LLM may have added.
        if (!ai_reply.empty() && !reply_loading) {
            ImGui::SameLine();
            ImGui::TextColored(colors[4], "(reply ready)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##reply")) {
                ImGui::SetClipboardText(strip_me_prefix(ai_reply).c_str());
            }
        }

        ImGui::Separator();

        if (messages_.empty() && loaded_session_pk_ != session.pk) {
            ImGui::TextColored(colors[4], "Loading messages...");
            return;
        }
        if (messages_.empty()) {
            ImGui::TextColored(colors[4], "No text messages in this chat.");
            return;
        }

        // ── AI summary panel ─────────────────────────────────────────────────
        const float total_h   = ImGui::GetContentRegionAvail().y;
        const float summary_h = total_h * 0.35f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.20f, 0.15f, 0.85f));
        ImGui::BeginChild("AISummary", ImVec2(0, summary_h), true);
        ImGui::TextColored(colors[0], "AI Summary");
        ImGui::Separator();

        if (summary_loading) {
            ImGui::TextColored(colors[4], "Summarising last %d messages...", kAiContextMessages);
        } else if (ai_summary_pk == session.pk && !ai_summary.empty()) {
            ImGui::TextWrapped("%s", ai_summary.c_str());
        } else {
            ImGui::TextColored(colors[4], "Generating summary…");
        }

        if (!ai_reply.empty() && !reply_loading) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(colors[0], "Candidate reply:");
            ImGui::TextWrapped("%s", strip_me_prefix(ai_reply).c_str());
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // ── Message bubbles ───────────────────────────────────────────────────
        ImGui::BeginChild("Messages", ImVec2(0, 0), false,
                          ImGuiWindowFlags_NoScrollbar);

        const float avail      = ImGui::GetContentRegionAvail().x;
        const float bubble_max = avail * 0.72f;

        for (size_t idx = 0; idx < messages_.size(); ++idx) {
            const auto& msg = messages_[idx];
            std::string ts = format_timestamp(msg.date);

            if (msg.from_me) {
                float text_w = std::min(
                    ImGui::CalcTextSize(msg.text.c_str(), nullptr, false, bubble_max).x + 16.0f,
                    bubble_max);
                ImGui::SetCursorPosX(avail - text_w + ImGui::GetScrollX());
                ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[3]);
            } else {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[2]);
                if (!msg.push_name.empty()) {
                    ImGui::TextColored(colors[0], "%s", msg.push_name.c_str());
                }
            }

            std::string bubble_id = std::format("bubble_{}", idx);
            float text_h = ImGui::CalcTextSize(msg.text.c_str(), nullptr, false, bubble_max).y
                           + ImGui::GetTextLineHeight() * 0.5f + 8.0f;
            ImGui::BeginChild(bubble_id.c_str(), ImVec2(bubble_max, text_h + 4.0f),
                              false, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos({4, 2});
            ImGui::TextWrapped("%s", msg.text.c_str());
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
            ImGui::TextColored(colors[4], "  %s", ts.c_str());
            ImGui::EndChild();

            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
};

} // namespace rouen::cards
