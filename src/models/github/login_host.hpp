#pragma once

#include <string>

namespace rouen::models::github {
    struct login_host {
        void set_personal_token(std::string_view token) {
            token_ = token;
        }
        
        std::string const &personal_token() const {
            return token_;
        }
        
        void save_to(auto saver) const {
            saver("token", token_);
        }
        
        void load_from(auto loader) {
            token_ = *loader("token");
        }
        
    private:
        std::string token_;
    };
}