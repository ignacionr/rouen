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

#include "cards/productivity/calculator.hpp"
#include "cards/productivity/converter.hpp"
#include "cards/productivity/invoice_card.hpp"
#include "cards/productivity/theme_card.hpp"
#include "cards/productivity/alarm.hpp"
#include "cards/productivity/editor.hpp"
#include "cards/productivity/pomodoro.hpp"

export module rouen.cards.productivity;

export namespace rouen::cards::productivity {
    using rouen::cards::calculator;
    using rouen::cards::converter;
    using rouen::cards::invoice_card;
}
