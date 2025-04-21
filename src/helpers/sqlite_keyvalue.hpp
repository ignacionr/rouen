#pragma once

#include "sqlite.hpp"
#include "../../structural/key_value.hpp"

namespace hosting::db {
    struct sqlite_keyval {
        sqlite_keyval(const std::string &path) : db_{path} {
            // ensure the table exists
            db_.ensure_table("keyval", "key TEXT PRIMARY KEY, value TEXT");
        }

        void set(std::string const &key, std::optional<std::string> const &value) {
            if (value) {
                db_.exec(std::format("INSERT OR REPLACE INTO keyval (key, value) VALUES ('{}', '{}')", key, *value));
            }
            else {
                db_.exec(std::format("DELETE FROM keyval WHERE key = '{}'", key));
            }
        }

        void scan_level(std::string_view name_base, auto sink) {
            db_.exec(std::format("SELECT key FROM keyval WHERE key LIKE '{}%'", name_base), [&sink](int, char **data, char **columns) {
                sink(data[0]);
                return 0;
            });
        }
    private:
        sqlite db_;
    };
}
