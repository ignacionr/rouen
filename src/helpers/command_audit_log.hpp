#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "auth_service.hpp"
#include "glaze_include.hpp"
#include "platform_utils.hpp"
#include "sqlite.hpp"

namespace rouen::helpers {

struct audit_entry {
    long long id{0};
    std::string timestamp_utc;
    std::string operator_sub;
    std::string operator_email;
    std::string operator_name;
    std::string operator_roles;
    std::string source_node_id;
    std::string target_card_uri;
    std::string verb;
    std::string payload_json;
    std::string operator_reason;
    std::string decision;
};

class command_audit_log {
public:
    static command_audit_log& instance() {
        static command_audit_log inst;
        return inst;
    }

    command_audit_log() : db_(resolve_db_path()) {
        ensure_schema();
    }

    explicit command_audit_log(const std::string& custom_db_path) : db_(custom_db_path) {
        ensure_schema();
    }

    static std::string current_utc_iso8601() {
        auto const now = std::chrono::system_clock::now();
        std::time_t const t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &t);
#else
        gmtime_r(&t, &tm_utc);
#endif
        char buf[32]{};
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return std::string(buf);
    }

    void record(
        const rouen::services::verified_operator_identity& op,
        const std::string& source_node_id,
        const std::string& target_card_uri,
        const std::string& verb,
        const std::string& payload_json,
        const std::string& operator_reason,
        const std::string& decision) {
        std::string roles_json = "[]";
        (void)glz::write_json(op.roles, roles_json);
        std::string const ts = current_utc_iso8601();

        db_.exec(
            "INSERT INTO command_audit_log ("
            "timestamp_utc, operator_sub, operator_email, operator_name, operator_roles, "
            "source_node_id, target_card_uri, verb, payload_json, operator_reason, decision"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            nullptr,
            ts,
            op.sub,
            op.email,
            op.name,
            roles_json,
            source_node_id,
            target_card_uri,
            verb,
            payload_json,
            operator_reason,
            decision);
    }

    std::vector<audit_entry> recent_entries(std::size_t limit = 100) {
        std::vector<audit_entry> out;
        auto const safe_limit = static_cast<long long>(limit == 0 ? 100 : limit);
        db_.exec(
            "SELECT id, timestamp_utc, operator_sub, operator_email, operator_name, "
            "operator_roles, source_node_id, target_card_uri, verb, payload_json, "
            "operator_reason, decision "
            "FROM command_audit_log ORDER BY id DESC LIMIT ?;",
            [&out](sqlite3_stmt* stmt) {
                auto col_str = [stmt](int idx) -> std::string {
                    const unsigned char* txt = sqlite3_column_text(stmt, idx);
                    return txt ? reinterpret_cast<const char*>(txt) : "";
                };
                audit_entry entry;
                entry.id = sqlite3_column_int64(stmt, 0);
                entry.timestamp_utc = col_str(1);
                entry.operator_sub = col_str(2);
                entry.operator_email = col_str(3);
                entry.operator_name = col_str(4);
                entry.operator_roles = col_str(5);
                entry.source_node_id = col_str(6);
                entry.target_card_uri = col_str(7);
                entry.verb = col_str(8);
                entry.payload_json = col_str(9);
                entry.operator_reason = col_str(10);
                entry.decision = col_str(11);
                out.push_back(std::move(entry));
            },
            safe_limit);
        return out;
    }

    std::string recent_entries_json(std::size_t limit = 100) {
        auto const entries = recent_entries(limit);
        std::string out = "[]";
        (void)glz::write_json(entries, out);
        return out;
    }

    void clear_all() {
        db_.exec("DELETE FROM command_audit_log;");
    }

private:
    static std::string resolve_db_path() {
        auto const path = platform::get_user_data_path("command_audit.db");
        std::filesystem::create_directories(path.parent_path());
        return path.string();
    }

    void ensure_schema() {
        db_.ensure_table(
            "command_audit_log",
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "timestamp_utc TEXT NOT NULL, "
            "operator_sub TEXT, "
            "operator_email TEXT, "
            "operator_name TEXT, "
            "operator_roles TEXT, "
            "source_node_id TEXT, "
            "target_card_uri TEXT NOT NULL, "
            "verb TEXT NOT NULL, "
            "payload_json TEXT NOT NULL, "
            "operator_reason TEXT, "
            "decision TEXT NOT NULL");
    }

    hosting::db::sqlite db_;
};

} // namespace rouen::helpers

template <>
struct glz::meta<rouen::helpers::audit_entry> {
    using T = rouen::helpers::audit_entry;
    static constexpr auto value = object(
        "id", &T::id,
        "timestamp_utc", &T::timestamp_utc,
        "operator_sub", &T::operator_sub,
        "operator_email", &T::operator_email,
        "operator_name", &T::operator_name,
        "operator_roles", &T::operator_roles,
        "source_node_id", &T::source_node_id,
        "target_card_uri", &T::target_card_uri,
        "verb", &T::verb,
        "payload_json", &T::payload_json,
        "operator_reason", &T::operator_reason,
        "decision", &T::decision
    );
};
