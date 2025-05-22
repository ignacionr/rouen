#pragma once
#include <string_view>
#include "tinyxml2.h"
#include "feed_item.hpp"
#include <vector>

namespace media::rss {
    struct feed_xml_parser {
        static std::vector<feed_item> parse(const std::string& contents);
    };
}
