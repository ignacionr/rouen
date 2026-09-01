module;

#include "dynamic_library.hpp"

export module rouen.helpers.dynamic_library;

export namespace rouen::helpers::dynamic_library {
    using rouen::helpers::dynamic_library::load;
    using rouen::helpers::dynamic_library::get_symbol;
    using rouen::helpers::dynamic_library::last_error;
    using rouen::helpers::dynamic_library::platform_extension;
}
