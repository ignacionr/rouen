#pragma once

#include <string>

namespace rouen::models::github {
    struct login_host;
}

namespace rouen::cards::github {

    struct login_screen {
        explicit login_screen(models::github::login_host& host);

        bool render();

    private:
        models::github::login_host& host_;
        std::string personal_token_;
        bool show_help_{false};
    };

} // namespace rouen::cards::github
