#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include "../registrar.hpp"

struct notify_service {
    notify_service() {
        registrar::add<std::function<void(std::string const&)>>("notify", 
            std::make_shared<std::function<void(std::string const&)>>(
                [](std::string const &message) {
                    std::cerr << message << std::endl;
                }
            )
        );
    }
};