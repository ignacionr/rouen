module;

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <ratio>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "cards/development/cmake.hpp"
#include "cards/development/fs-directory.hpp"
#include "cards/development/git.hpp"
#include "cards/development/git_overlay.hpp"
#include "cards/development/github.hpp"

export module rouen.cards.development;

export namespace rouen::cards::development {
    using rouen::cards::cmake_card;
    using rouen::cards::fs_directory;
    using ::git;
}
