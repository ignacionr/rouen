#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "tinyxml2.h"
#include "feed_item.hpp"

namespace media::rss {
    struct feed_xml_parser {
        static std::vector<feed_item> parse(const std::string& contents);
    };
}
