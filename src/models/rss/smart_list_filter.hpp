#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "../../helpers/glaze_include.hpp"

namespace media::rss {

    struct filter_condition {
        std::string field; // "title", "description", "pub_date", "media_duration_seconds", "feed_tag"
        std::string op;    // ">", "<", ">=", "<=", "==", "!=", "CONTAINS", "EXCLUDES", "MATCHES", "IN", "NOT IN"
        std::string value;

        struct glaze {
            using T = filter_condition;
            static constexpr auto value = glz::object(
                "field", &T::field,
                "op", &T::op,
                "value", &T::value
            );
        };
    };

    struct filter_group {
        std::string op = "AND"; // "AND" or "OR"
        std::vector<filter_condition> conditions;

        struct glaze {
            using T = filter_group;
            static constexpr auto value = glz::object(
                "op", &T::op,
                "conditions", &T::conditions
            );
        };
    };

    struct smart_list_dto {
        std::string title;
        filter_group filter;

        struct glaze {
            using T = smart_list_dto;
            static constexpr auto value = glz::object(
                "title", &T::title,
                "filter", &T::filter
            );
        };
    };

    struct rss_smart_lists_sync_dto {
        std::vector<smart_list_dto> smart_lists;

        struct glaze {
            using T = rss_smart_lists_sync_dto;
            static constexpr auto value = glz::object(
                "smart_lists", &T::smart_lists
            );
        };
    };

} // namespace media::rss
