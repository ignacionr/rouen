#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include "../registrar.hpp"
#include "debug.hpp"
#include "config_service.hpp"

struct notify_service {
    notify_service() {
        registrar::add<std::function<void(std::string const&)>>("notify", 
            std::make_shared<std::function<void(std::string const&)>>(
                [](std::string const &message) {
                    std::string say_path = CONFIG_SERVICE()->get_say_path();
                    [[maybe_unused]] int system_result = system(std::format("{} \"{}\"", say_path, message).c_str());
                    NOTIFY_INFO(message);
                }
            )
        );
    }
};
