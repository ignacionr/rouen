#pragma once

#include <string>
#include <vector>
#include <optional>
#include <format>

#include <glaze/glaze.hpp>

#include "../../helpers/sqlite.hpp"
#include "email_metadata.hpp"  // Include the EmailMetadata struct definition

namespace mail {
    class MetadataRepository {
    public:
        MetadataRepository(const std::string &db_path) : db_{db_path} {
            // Create necessary tables if they don't exist
            db_.ensure_table("email_metadata", 
                "id TEXT PRIMARY KEY, "
                "urgency INTEGER, "
                "category TEXT, "
                "summary TEXT, "
                "tags TEXT, "  // JSON array of tags stored as text
                "action_links TEXT, "  // JSON map of actions stored as text
                "created_at TEXT DEFAULT (datetime('now')), "
                "updated_at TEXT DEFAULT (datetime('now'))"
            );
        }

        // Store email metadata in the database (create or update)
        bool store(const EmailMetadata& metadata) {
            try {
                std::string tags_json;
                auto tags_res = glz::write_json(metadata.tags, tags_json);
                if (tags_res) {
                    return false;
                }
                
                std::string action_links_json;
                auto action_links_res = glz::write_json(metadata.action_links, action_links_json);
                if (action_links_res) {
                    return false;
                }

                // Check if the record already exists
                bool exists = false;
                std::string check_sql = "SELECT 1 FROM email_metadata WHERE id = ?";
                db_.exec(check_sql, [&exists](sqlite3_stmt* stmt) {
                    exists = true;
                }, metadata.id);

                std::string sql;
                if (exists) {
                    // Update existing record
                    sql = "UPDATE email_metadata SET "
                          "urgency = ?, category = ?, summary = ?, tags = ?, action_links = ?, updated_at = datetime('now') "
                          "WHERE id = ?";
                    db_.exec(sql, {}, 
                        metadata.urgency, 
                        metadata.category, 
                        metadata.summary, 
                        tags_json,
                        action_links_json,
                        metadata.id
                    );
                } else {
                    // Insert new record
                    sql = "INSERT INTO email_metadata "
                          "(id, urgency, category, summary, tags, action_links) "
                          "VALUES (?, ?, ?, ?, ?, ?)";
                    db_.exec(sql, {}, 
                        metadata.id, 
                        metadata.urgency, 
                        metadata.category, 
                        metadata.summary, 
                        tags_json,
                        action_links_json
                    );
                }
                
                return true;
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to store email metadata: {}", e.what()));
                return false;
            }
        }

        // Retrieve email metadata by ID
        std::optional<EmailMetadata> get(const std::string& email_id) {
            try {
                std::optional<EmailMetadata> result;
                
                std::string sql = "SELECT id, urgency, category, summary, tags, action_links "
                                  "FROM email_metadata WHERE id = ?";
                db_.exec(sql, [&result](sqlite3_stmt* stmt) {
                    EmailMetadata metadata;
                    metadata.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    metadata.urgency = sqlite3_column_int(stmt, 1);
                    metadata.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    metadata.summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    
                    // Parse the tags from JSON
                    const char* tags_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    if (tags_json && *tags_json) {
                        auto res = glz::read_json(metadata.tags, tags_json);
                        if (res) {
                            // If parsing failed, set a default tag
                            metadata.tags = {"parse_error"};
                        }
                    }
                    
                    // Parse the action_links from JSON
                    const char* action_links_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    if (action_links_json && *action_links_json) {
                        auto res = glz::read_json(metadata.action_links, action_links_json);
                        if (res) {
                            // If parsing failed, set an empty map
                            metadata.action_links = {};
                        }
                    }
                    
                    result = metadata;
                }, email_id);
                
                return result;
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to retrieve email metadata: {}", e.what()));
                return std::nullopt;
            }
        }

        // Get emails by category
        std::vector<EmailMetadata> get_by_category(const std::string& category) {
            try {
                std::vector<EmailMetadata> results;
                
                std::string sql = "SELECT id, urgency, category, summary, tags, action_links "
                                  "FROM email_metadata WHERE category = ? "
                                  "ORDER BY urgency DESC, updated_at DESC";
                db_.exec(sql, [&results](sqlite3_stmt* stmt) {
                    EmailMetadata metadata;
                    metadata.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    metadata.urgency = sqlite3_column_int(stmt, 1);
                    metadata.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    metadata.summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    
                    // Parse the tags from JSON
                    const char* tags_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    if (tags_json && *tags_json) {
                        auto res = glz::read_json(metadata.tags, tags_json);
                        if (res) {
                            // If parsing failed, set a default tag
                            metadata.tags = {"parse_error"};
                        }
                    }
                    
                    // Parse the action_links from JSON
                    const char* action_links_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    if (action_links_json && *action_links_json) {
                        auto res = glz::read_json(metadata.action_links, action_links_json);
                        if (res) {
                            // If parsing failed, set an empty map
                            metadata.action_links = {};
                        }
                    }
                    
                    results.push_back(metadata);
                }, category);
                
                return results;
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to retrieve email metadata by category: {}", e.what()));
                return {};
            }
        }

        // Get emails with a specific tag
        std::vector<EmailMetadata> get_by_tag(const std::string& tag) {
            try {
                std::vector<EmailMetadata> results;
                
                // This is a simplified implementation that loads all records and filters in memory
                // For larger datasets, a more efficient approach would be to use full-text search or a dedicated tags table
                std::string sql = "SELECT id, urgency, category, summary, tags, action_links FROM email_metadata";
                db_.exec(sql, [&results, &tag](sqlite3_stmt* stmt) {
                    // Parse the tags JSON
                    const char* tags_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    if (!tags_json || !*tags_json) {
                        return;
                    }
                    
                    std::vector<std::string> tags;
                    auto res = glz::read_json(tags, tags_json);
                    if (res) {
                        return;
                    }
                    
                    // Check if the tag is in the list
                    if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
                        EmailMetadata metadata;
                        metadata.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        metadata.urgency = sqlite3_column_int(stmt, 1);
                        metadata.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                        metadata.summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                        metadata.tags = tags;
                        
                        // Parse the action_links from JSON
                        const char* action_links_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                        if (action_links_json && *action_links_json) {
                            auto action_res = glz::read_json(metadata.action_links, action_links_json);
                            if (action_res) {
                                // If parsing failed, set an empty map
                                metadata.action_links = {};
                            }
                        }
                        
                        results.push_back(metadata);
                    }
                });
                
                return results;
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to retrieve email metadata by tag: {}", e.what()));
                return {};
            }
        }

        // Get most recent emails, optionally limited by count
        std::vector<EmailMetadata> get_recent(int limit = 20) {
            try {
                std::vector<EmailMetadata> results;
                
                std::string sql = std::format(
                    "SELECT id, urgency, category, summary, tags, action_links "
                    "FROM email_metadata "
                    "ORDER BY created_at DESC LIMIT {}",
                    limit
                );
                
                db_.exec(sql, [&results](sqlite3_stmt* stmt) {
                    EmailMetadata metadata;
                    metadata.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    metadata.urgency = sqlite3_column_int(stmt, 1);
                    metadata.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    metadata.summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    
                    // Parse the tags from JSON
                    const char* tags_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    if (tags_json && *tags_json) {
                        auto res = glz::read_json(metadata.tags, tags_json);
                        if (res) {
                            // If parsing failed, set a default tag
                            metadata.tags = {"parse_error"};
                        }
                    }
                    
                    // Parse the action_links from JSON
                    const char* action_links_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                    if (action_links_json && *action_links_json) {
                        auto res = glz::read_json(metadata.action_links, action_links_json);
                        if (res) {
                            // If parsing failed, set an empty map
                            metadata.action_links = {};
                        }
                    }
                    
                    results.push_back(metadata);
                });
                
                return results;
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to retrieve recent email metadata: {}", e.what()));
                return {};
            }
        }

        // Delete email metadata
        bool remove(const std::string& email_id) {
            try {
                std::string sql = "DELETE FROM email_metadata WHERE id = ?";
                db_.exec(sql, {}, email_id);
                return true;
            } catch (const std::exception& e) {
                "notify"_sfn(std::format("Failed to delete email metadata: {}", e.what()));
                return false;
            }
        }

    private:
        hosting::db::sqlite db_;
    };
}