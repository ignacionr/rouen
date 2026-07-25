#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "../../helpers/sqlite.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#if defined(__APPLE__)
#include "../../helpers/process_helper.hpp"
#endif
#include "contact.hpp"

namespace rouen::models::contacts {

class contacts_repository {
public:
    contacts_repository() 
        : contacts_repository(rouen::platform::get_user_data_path("contacts.db").string()) {}

    explicit contacts_repository(const std::string& db_path) : db_{db_path} {
        db_.ensure_table("contacts",
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "first_name TEXT, "
            "last_name TEXT, "
            "display_name TEXT, "
            "organization TEXT, "
            "job_title TEXT, "
            "email TEXT, "
            "phone TEXT, "
            "address TEXT, "
            "notes TEXT, "
            "picture_url TEXT, "
            "source TEXT, "
            "created TEXT, "
            "updated TEXT");
    }

    static std::string current_time_str() {
        auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    static std::string safe_column_text(sqlite3_stmt* stmt, int col) {
        const unsigned char* text = sqlite3_column_text(stmt, col);
        return text ? reinterpret_cast<const char*>(text) : "";
    }

    std::vector<contact> get_all_contacts() {
        std::vector<contact> list;
        std::string sql = "SELECT id, first_name, last_name, display_name, organization, job_title, "
                          "email, phone, address, notes, picture_url, source, created, updated "
                          "FROM contacts ORDER BY display_name ASC, first_name ASC, last_name ASC";
        
        db_.exec(sql, [&list](sqlite3_stmt* stmt) {
            contact c;
            c.id = sqlite3_column_int64(stmt, 0);
            c.first_name = safe_column_text(stmt, 1);
            c.last_name = safe_column_text(stmt, 2);
            c.display_name = safe_column_text(stmt, 3);
            c.organization = safe_column_text(stmt, 4);
            c.job_title = safe_column_text(stmt, 5);
            c.email = safe_column_text(stmt, 6);
            c.phone = safe_column_text(stmt, 7);
            c.address = safe_column_text(stmt, 8);
            c.notes = safe_column_text(stmt, 9);
            c.picture_url = safe_column_text(stmt, 10);
            c.source = safe_column_text(stmt, 11);
            c.created = safe_column_text(stmt, 12);
            c.updated = safe_column_text(stmt, 13);
            list.push_back(c);
        });
        return list;
    }

    std::optional<contact> get_contact_by_id(int64_t id) {
        std::optional<contact> result;
        std::string sql = "SELECT id, first_name, last_name, display_name, organization, job_title, "
                          "email, phone, address, notes, picture_url, source, created, updated "
                          "FROM contacts WHERE id = ?";
        
        db_.exec(sql, [&result](sqlite3_stmt* stmt) {
            contact c;
            c.id = sqlite3_column_int64(stmt, 0);
            c.first_name = safe_column_text(stmt, 1);
            c.last_name = safe_column_text(stmt, 2);
            c.display_name = safe_column_text(stmt, 3);
            c.organization = safe_column_text(stmt, 4);
            c.job_title = safe_column_text(stmt, 5);
            c.email = safe_column_text(stmt, 6);
            c.phone = safe_column_text(stmt, 7);
            c.address = safe_column_text(stmt, 8);
            c.notes = safe_column_text(stmt, 9);
            c.picture_url = safe_column_text(stmt, 10);
            c.source = safe_column_text(stmt, 11);
            c.created = safe_column_text(stmt, 12);
            c.updated = safe_column_text(stmt, 13);
            result = c;
        }, id);
        return result;
    }

    std::vector<contact> search_contacts(const std::string& query) {
        if (query.empty()) return get_all_contacts();
        
        std::vector<contact> list;
        std::string pattern = "%" + query + "%";
        std::string sql = "SELECT id, first_name, last_name, display_name, organization, job_title, "
                          "email, phone, address, notes, picture_url, source, created, updated "
                          "FROM contacts WHERE display_name LIKE ? OR first_name LIKE ? OR last_name LIKE ? "
                          "OR organization LIKE ? OR email LIKE ? OR phone LIKE ? OR notes LIKE ? "
                          "ORDER BY display_name ASC";
        
        db_.exec(sql, [&list](sqlite3_stmt* stmt) {
            contact c;
            c.id = sqlite3_column_int64(stmt, 0);
            c.first_name = safe_column_text(stmt, 1);
            c.last_name = safe_column_text(stmt, 2);
            c.display_name = safe_column_text(stmt, 3);
            c.organization = safe_column_text(stmt, 4);
            c.job_title = safe_column_text(stmt, 5);
            c.email = safe_column_text(stmt, 6);
            c.phone = safe_column_text(stmt, 7);
            c.address = safe_column_text(stmt, 8);
            c.notes = safe_column_text(stmt, 9);
            c.picture_url = safe_column_text(stmt, 10);
            c.source = safe_column_text(stmt, 11);
            c.created = safe_column_text(stmt, 12);
            c.updated = safe_column_text(stmt, 13);
            list.push_back(c);
        }, pattern, pattern, pattern, pattern, pattern, pattern, pattern);
        return list;
    }

    int64_t find_existing_contact(const contact& c) {
        if (c.id > 0) return c.id;

        int64_t found_id = -1;

        // 1. Match by email
        if (!c.email.empty()) {
            std::string sql = "SELECT id FROM contacts WHERE LOWER(email) = LOWER(?) LIMIT 1";
            db_.exec(sql, [&found_id](sqlite3_stmt* stmt) {
                found_id = sqlite3_column_int64(stmt, 0);
            }, c.email);
            if (found_id > 0) return found_id;
        }

        // 2. Match by phone
        if (!c.phone.empty()) {
            std::string sql = "SELECT id FROM contacts WHERE phone = ? LIMIT 1";
            db_.exec(sql, [&found_id](sqlite3_stmt* stmt) {
                found_id = sqlite3_column_int64(stmt, 0);
            }, c.phone);
            if (found_id > 0) return found_id;
        }

        // 3. Match by display name / full name
        std::string name_to_check = c.display_name.empty() ? c.get_full_name() : c.display_name;
        if (!name_to_check.empty() && name_to_check != "Unnamed Contact") {
            std::string sql = "SELECT id FROM contacts WHERE LOWER(display_name) = LOWER(?) LIMIT 1";
            db_.exec(sql, [&found_id](sqlite3_stmt* stmt) {
                found_id = sqlite3_column_int64(stmt, 0);
            }, name_to_check);
            if (found_id > 0) return found_id;
        }

        return -1;
    }

    int64_t upsert_contact(contact& c) {
        std::string now = current_time_str();
        if (c.display_name.empty()) {
            c.display_name = c.get_full_name();
        }
        if (c.source.empty()) c.source = "manual";

        if (c.id <= 0) {
            c.id = find_existing_contact(c);
        }

        if (c.id <= 0) {
            // New contact
            c.created = now;
            c.updated = now;
            std::string sql = "INSERT INTO contacts (first_name, last_name, display_name, organization, job_title, "
                              "email, phone, address, notes, picture_url, source, created, updated) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            db_.exec(sql, {}, c.first_name, c.last_name, c.display_name, c.organization, c.job_title,
                     c.email, c.phone, c.address, c.notes, c.picture_url, c.source, c.created, c.updated);
            c.id = db_.last_insert_rowid();
        } else {
            // Update existing contact - merge missing non-empty fields
            auto existing = get_contact_by_id(c.id);
            if (existing.has_value()) {
                if (c.first_name.empty()) c.first_name = existing->first_name;
                if (c.last_name.empty()) c.last_name = existing->last_name;
                if (c.display_name.empty()) c.display_name = existing->display_name;
                if (c.organization.empty()) c.organization = existing->organization;
                if (c.job_title.empty()) c.job_title = existing->job_title;
                if (c.email.empty()) c.email = existing->email;
                if (c.phone.empty()) c.phone = existing->phone;
                if (c.address.empty()) c.address = existing->address;
                if (c.notes.empty()) c.notes = existing->notes;
                if (c.picture_url.empty()) c.picture_url = existing->picture_url;
                c.created = existing->created.empty() ? now : existing->created;
            }

            c.updated = now;
            std::string sql = "UPDATE contacts SET first_name=?, last_name=?, display_name=?, organization=?, "
                              "job_title=?, email=?, phone=?, address=?, notes=?, picture_url=?, source=?, updated=? "
                              "WHERE id=?";
            db_.exec(sql, {}, c.first_name, c.last_name, c.display_name, c.organization, c.job_title,
                     c.email, c.phone, c.address, c.notes, c.picture_url, c.source, c.updated, c.id);
        }
        return c.id;
    }

    bool delete_contact(int64_t id) {
        std::string sql = "DELETE FROM contacts WHERE id = ?";
        db_.exec(sql, {}, id);
        return true;
    }

    // Export to directory with prettified JSON for Universal Sync
    bool export_to_directory(const std::filesystem::path& dir) {
        try {
            std::filesystem::create_directories(dir);
            auto contacts = get_all_contacts();
            std::vector<contact_dto> dtos;
            dtos.reserve(contacts.size());
            for (const auto& c : contacts) {
                dtos.push_back(to_dto(c));
            }

            std::string json_content = glz::write<glz::opts{.prettify = true}>(dtos).value_or("");
            if (!json_content.empty()) {
                std::ofstream file(dir / "contacts.json");
                if (file.is_open()) {
                    file << json_content;
                    return true;
                }
            }
        } catch (const std::exception&) {}
        return false;
    }

    // Import from directory for Universal Sync
    bool import_from_directory(const std::filesystem::path& dir) {
        auto contacts_file = dir / "contacts.json";
        if (!std::filesystem::exists(contacts_file)) return false;

        try {
            std::ifstream file(contacts_file);
            if (!file.is_open()) return false;

            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::vector<contact_dto> dtos;
            if (auto ec = glz::read_json(dtos, content); !ec) {
                for (const auto& dto : dtos) {
                    contact c = from_dto(dto);
                    upsert_contact(c);
                }
                return true;
            }
        } catch (const std::exception&) {}
        return false;
    }

    // macOS Contacts App Automation with progress callback
    int import_macos_contacts(std::function<void(float progress, const std::string& status)> progress_cb = nullptr) {
        int imported_count = 0;
#if defined(__APPLE__)
        try {
            if (progress_cb) progress_cb(0.05f, "Querying macOS Contacts application...");
            std::string script =
                "tell application \"Contacts\"\n"
                "    set contactList to {}\n"
                "    repeat with p in people\n"
                "        set pFirst to (first name of p)\n"
                "        if pFirst is missing value then set pFirst to \"\"\n"
                "        set pLast to (last name of p)\n"
                "        if pLast is missing value then set pLast to \"\"\n"
                "        set pName to (name of p)\n"
                "        if pName is missing value then set pName to \"\"\n"
                "        set pOrg to (organization of p)\n"
                "        if pOrg is missing value then set pOrg to \"\"\n"
                "        set pJob to (job title of p)\n"
                "        if pJob is missing value then set pJob to \"\"\n"
                "        set pNote to (note of p)\n"
                "        if pNote is missing value then set pNote to \"\"\n"
                "        set eStr to \"\"\n"
                "        repeat with e in emails of p\n"
                "            if eStr is not \"\" then set eStr to eStr & \", \"\n"
                "            set eStr to eStr & (value of e)\n"
                "        end repeat\n"
                "        set phStr to \"\"\n"
                "        repeat with ph in phones of p\n"
                "            if phStr is not \"\" then set phStr to phStr & \", \"\n"
                "            set phStr to phStr & (value of ph)\n"
                "        end repeat\n"
                "        set end of contactList to pFirst & \"||\" & pLast & \"||\" & pName & \"||\" & pOrg & \"||\" & pJob & \"||\" & eStr & \"||\" & phStr & \"||\" & pNote\n"
                "    end repeat\n"
                "    set AppleScript's text item delimiters to \"%%RECORD%%\"\n"
                "    return contactList as text\n"
                "end tell";

            std::string cmd = "osascript -e " + escape_shell_arg(script);
            std::string raw_res = ProcessHelper::executeCommand(cmd);

            if (!raw_res.empty()) {
                auto records = split_string(raw_res, "%%RECORD%%");
                size_t total = records.size();
                for (size_t i = 0; i < total; ++i) {
                    const auto& rec = records[i];
                    if (rec.empty()) continue;

                    float p = 0.1f + 0.9f * (static_cast<float>(i + 1) / static_cast<float>(total));
                    if (progress_cb) {
                        progress_cb(p, std::format("Importing macOS contact {}/{}...", i + 1, total));
                    }

                    auto fields = split_string(rec, "||");
                    contact c;
                    c.first_name = fields.size() > 0 ? trim_string(fields[0]) : "";
                    c.last_name = fields.size() > 1 ? trim_string(fields[1]) : "";
                    c.display_name = fields.size() > 2 ? trim_string(fields[2]) : "";
                    c.organization = fields.size() > 3 ? trim_string(fields[3]) : "";
                    c.job_title = fields.size() > 4 ? trim_string(fields[4]) : "";
                    c.email = fields.size() > 5 ? trim_string(fields[5]) : "";
                    c.phone = fields.size() > 6 ? trim_string(fields[6]) : "";
                    c.notes = fields.size() > 7 ? trim_string(fields[7]) : "";
                    c.source = "macos";

                    if (c.display_name.empty()) {
                        c.display_name = c.get_full_name();
                    }

                    if (!c.display_name.empty() || !c.email.empty() || !c.phone.empty()) {
                        upsert_contact(c);
                        imported_count++;
                    }
                }
            }
        } catch (const std::exception&) {}
#endif
        if (progress_cb) progress_cb(1.0f, "macOS Contacts import completed.");
        return imported_count;
    }

    // WhatsApp Contact Import (vCard, JSON, or text paste) with progress callback
    int import_whatsapp_contacts(const std::string& data, std::function<void(float progress, const std::string& status)> progress_cb = nullptr) {
        int count = 0;
        if (data.empty()) return count;

        std::string trimmed = trim_string(data);

        // 1. Try vCard parsing if data contains VCARD
        if (trimmed.find("VCARD") != std::string::npos) {
            std::stringstream ss(trimmed);
            std::string line;
            std::vector<contact> vcards;
            contact cur;
            bool in_vcard = false;

            while (std::getline(ss, line)) {
                line = trim_string(line);
                if (line.find("BEGIN:VCARD") != std::string::npos) {
                    in_vcard = true;
                    cur = contact{};
                    cur.source = "whatsapp";
                } else if (line.find("END:VCARD") != std::string::npos) {
                    if (in_vcard) {
                        if (cur.display_name.empty()) cur.display_name = cur.get_full_name();
                        if (!cur.display_name.empty() || !cur.phone.empty() || !cur.email.empty()) {
                            vcards.push_back(cur);
                        }
                    }
                    in_vcard = false;
                } else if (in_vcard) {
                    if (line.rfind("FN:", 0) == 0 || line.rfind("FN;", 0) == 0) {
                        auto pos = line.find(':');
                        if (pos != std::string::npos) cur.display_name = trim_string(line.substr(pos + 1));
                    } else if (line.rfind("N:", 0) == 0 || line.rfind("N;", 0) == 0) {
                        auto pos = line.find(':');
                        if (pos != std::string::npos) {
                            auto n_parts = split_string(line.substr(pos + 1), ";");
                            if (n_parts.size() > 0) cur.last_name = trim_string(n_parts[0]);
                            if (n_parts.size() > 1) cur.first_name = trim_string(n_parts[1]);
                        }
                    } else if (line.rfind("TEL", 0) == 0) {
                        auto pos = line.find(':');
                        if (pos != std::string::npos) {
                            if (!cur.phone.empty()) cur.phone += ", ";
                            cur.phone += trim_string(line.substr(pos + 1));
                        }
                    } else if (line.rfind("EMAIL", 0) == 0) {
                        auto pos = line.find(':');
                        if (pos != std::string::npos) {
                            if (!cur.email.empty()) cur.email += ", ";
                            cur.email += trim_string(line.substr(pos + 1));
                        }
                    } else if (line.rfind("ORG", 0) == 0) {
                        auto pos = line.find(':');
                        if (pos != std::string::npos) cur.organization = trim_string(line.substr(pos + 1));
                    } else if (line.rfind("TITLE", 0) == 0) {
                        auto pos = line.find(':');
                        if (pos != std::string::npos) cur.job_title = trim_string(line.substr(pos + 1));
                    } else if (line.rfind("NOTE", 0) == 0) {
                        auto pos = line.find(':');
                        if (pos != std::string::npos) cur.notes = trim_string(line.substr(pos + 1));
                    }
                }
            }

            size_t total = vcards.size();
            for (size_t i = 0; i < total; ++i) {
                float p = static_cast<float>(i + 1) / static_cast<float>(total);
                if (progress_cb) progress_cb(p, std::format("Importing WhatsApp contact {}/{}...", i + 1, total));
                upsert_contact(vcards[i]);
                count++;
            }
            if (progress_cb) progress_cb(1.0f, "WhatsApp vCard import complete.");
            return count;
        }

        // 2. Try JSON parsing
        if (trimmed.front() == '[' || trimmed.front() == '{') {
            try {
                std::vector<contact_dto> dtos;
                if (trimmed.front() == '{') {
                    contact_dto dto;
                    if (auto ec = glz::read_json(dto, trimmed); !ec) {
                        dtos.push_back(dto);
                    }
                } else {
                    (void)glz::read_json(dtos, trimmed);
                }
                size_t total = dtos.size();
                for (size_t i = 0; i < total; ++i) {
                    float p = static_cast<float>(i + 1) / static_cast<float>(total);
                    if (progress_cb) progress_cb(p, std::format("Importing WhatsApp contact {}/{}...", i + 1, total));
                    contact c = from_dto(dtos[i]);
                    c.source = "whatsapp";
                    upsert_contact(c);
                    count++;
                }
                if (progress_cb) progress_cb(1.0f, "WhatsApp JSON import complete.");
                if (count > 0) return count;
            } catch (const std::exception&) {}
        }

        // 3. Fallback: line-by-line parsing
        std::stringstream ss(trimmed);
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(ss, line)) {
            line = trim_string(line);
            if (!line.empty() && line.front() != '#') lines.push_back(line);
        }

        size_t total = lines.size();
        for (size_t i = 0; i < total; ++i) {
            float p = static_cast<float>(i + 1) / static_cast<float>(total);
            if (progress_cb) progress_cb(p, std::format("Importing contact line {}/{}...", i + 1, total));

            const auto& l = lines[i];
            contact c;
            c.source = "whatsapp";
            auto colon_pos = l.find(':');
            auto comma_pos = l.find(',');

            if (colon_pos != std::string::npos) {
                c.display_name = trim_string(l.substr(0, colon_pos));
                c.phone = trim_string(l.substr(colon_pos + 1));
            } else if (comma_pos != std::string::npos) {
                auto parts = split_string(l, ",");
                if (parts.size() > 0) c.display_name = trim_string(parts[0]);
                if (parts.size() > 1) c.phone = trim_string(parts[1]);
                if (parts.size() > 2) c.email = trim_string(parts[2]);
            } else {
                c.display_name = l;
            }

            if (!c.display_name.empty()) {
                upsert_contact(c);
                count++;
            }
        }
        if (progress_cb) progress_cb(1.0f, "WhatsApp import complete.");
        return count;
    }

private:
    hosting::db::sqlite db_;

    static std::string escape_shell_arg(const std::string& arg) {
        std::string res = "'";
        for (char ch : arg) {
            if (ch == '\'') res += "'\\''";
            else res += ch;
        }
        res += "'";
        return res;
    }

    static std::string trim_string(const std::string& s) {
        auto start = s.find_first_not_of(" \t\n\r\v\f");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\n\r\v\f");
        return s.substr(start, end - start + 1);
    }

    static std::vector<std::string> split_string(const std::string& s, const std::string& delim) {
        std::vector<std::string> result;
        size_t start = 0;
        size_t end = s.find(delim);
        while (end != std::string::npos) {
            result.push_back(s.substr(start, end - start));
            start = end + delim.length();
            end = s.find(delim, start);
        }
        result.push_back(s.substr(start));
        return result;
    }
};

} // namespace rouen::models::contacts
