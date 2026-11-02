#pragma once

#include <array>
#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../glaze_include.hpp"
#include "../../models/series/series_repository.hpp"

namespace rouen::helpers::adaptive_cards {

struct element;

struct series_point {
    std::string label;
    std::string value;

    [[nodiscard]] float numeric_value() const {
        return static_cast<float>(std::strtod(value.c_str(), nullptr));
    }
};

struct gauge_thresholds {
    double lowLow{0.0};
    double low{0.0};
    double high{0.0};
    double highHigh{0.0};
};

struct fact {
    std::string title;
    std::string value;
};

struct choice {
    std::string title;
    std::string value;
};

struct media_source {
    std::string mimeType;
    std::string url;
};

struct text_run {
    std::string type;
    std::string text;
    bool bold{false};
    bool italic{false};
    std::string color;
    std::string size;
    std::string url;
};

struct table_cell {
    std::string type;
    std::vector<element> items;
};

struct table_row {
    std::string type;
    std::vector<table_cell> cells;
};

struct show_card {
    std::string type;
    std::vector<element> body;
};

struct action {
    std::string type;
    std::string title;
    std::string url;
    std::string verb;
    std::string style;
    std::string requiredRole;
    bool requiresConfirmation{false};
    std::string confirmationPrompt;
    bool requireReasonNote{false};
    // The Adaptive Cards spec allows this to be any JSON value (almost
    // always an object) merged into the Action.Execute payload; glz::json_t
    // accepts whatever shape shows up instead of requiring a JSON string.
    glz::json_t data{};
    std::vector<std::string> targetElements;
    show_card card;
};

struct element {
    std::string type;
    std::string id;
    std::string title;
    std::string label;
    std::string text;
    std::string value;
    std::string placeholder;
    std::string data;
    std::string size;
    std::string color;
    std::string weight;
    std::string url;
    std::string style;
    std::string horizontalAlignment;
    std::string altText;
    std::string imageSize;
    std::string poster;
    std::string spacing;
    std::string width;
    bool separator{false};
    bool isMultiSelect{false};
    bool isMultiline{false};
    double min{0.0};
    double max{0.0};
    std::vector<element> items;
    std::vector<element> columns;
    std::vector<fact> facts;
    std::vector<choice> choices;
    std::vector<media_source> sources;
    std::vector<element> images;
    std::vector<text_run> inlines;
    std::vector<table_row> rows;
    action selectAction;
    std::vector<action> actions;
    // Industrial SCADA & series widgets (Rouen.SeriesPlot, Rouen.Gauge, Rouen.XYPlot, Rouen.StatusBadge)
    std::string seriesName;
    std::string unit;
    std::string xLabel;
    std::string yLabel;
    std::string severity;
    bool persistToRepository{false};
    bool isBarChart{false};
    int height{0};
    double lowLimit{0.0};
    double highLimit{0.0};
    double target{0.0};
    gauge_thresholds thresholds{};
    std::vector<series_point> points;
    std::vector<std::array<double, 2>> xyPoints;
};

struct alarm_declaration {
    std::string id;
    std::string severity{"high"};
    std::string title;
    std::string value;
    bool latched{true};
};

struct card_document {
    std::string type;
    std::string version;
    std::string schema;
    std::string minHeight;
    std::string padding;
    int refreshIntervalMs{0};
    std::vector<alarm_declaration> alarms;
    std::vector<element> body;
    std::vector<action> actions;
};

struct parser_interface {
    virtual ~parser_interface() = default;
    [[nodiscard]] virtual card_document parse(std::string_view json) const = 0;
};

class parser final : public parser_interface {
public:
    [[nodiscard]] card_document parse(std::string_view json) const override {
        card_document card{};
        std::string payload = normalize_keys(json);
        const auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(card, payload);
        if (err) {
            throw std::runtime_error(glz::format_error(err, payload));
        }

        if (card.type.empty()) {
            card.type = "AdaptiveCard";
        }

        validate_elements(card.body);
        validate_actions(card.actions);
        persist_series_elements(card.body);

        return card;
    }

private:
    static void persist_series_elements(const std::vector<element>& elements) {
        for (const auto& node : elements) {
            if ((node.type == "Rouen.SeriesPlot" || node.type == "Rouen.Sparkline") &&
                node.persistToRepository && !node.points.empty()) {
                models::series::series_repository repo{};
                models::series::series_record rec{};
                rec.name = !node.seriesName.empty()
                    ? node.seriesName
                    : models::series::series_repository::slugify(node.title.empty() ? "series" : node.title);
                rec.title = node.title.empty() ? rec.name : node.title;
                rec.unit = node.unit;
                rec.points.reserve(node.points.size());
                for (const auto& pt : node.points) {
                    rec.points.push_back(models::series::data_point{
                        .label = pt.label,
                        .value = pt.numeric_value()
                    });
                }
                rec.is_bar_chart = node.isBarChart;
                (void)repo.save_series(rec);
            }
            if (!node.items.empty()) {
                persist_series_elements(node.items);
            }
            for (const auto& col : node.columns) {
                if (!col.items.empty()) {
                    persist_series_elements(col.items);
                }
            }
            for (const auto& row : node.rows) {
                for (const auto& cell : row.cells) {
                    if (!cell.items.empty()) {
                        persist_series_elements(cell.items);
                    }
                }
            }
        }
    }
    [[nodiscard]] static std::string normalize_keys(std::string_view input) {
        std::string output(input);
        std::size_t pos = 0;
        while ((pos = output.find("\"$data\"", pos)) != std::string::npos) {
            output.replace(pos, 7, "\"data\"");
            pos += 6;
        }
        pos = 0;
        while ((pos = output.find("\"$schema\"", pos)) != std::string::npos) {
            output.replace(pos, 9, "\"schema\"");
            pos += 8;
        }

        // Normalize unquoted numeric "width": 1 -> "width": "1"
        pos = 0;
        while ((pos = output.find("\"width\"", pos)) != std::string::npos) {
            std::size_t colon = output.find(':', pos + 7);
            if (colon == std::string::npos) break;
            std::size_t val_start = colon + 1;
            while (val_start < output.size() && (output[val_start] == ' ' || output[val_start] == '\t' || output[val_start] == '\r' || output[val_start] == '\n')) {
                ++val_start;
            }
            if (val_start < output.size() && (std::isdigit(static_cast<unsigned char>(output[val_start])) || output[val_start] == '.' || output[val_start] == '-')) {
                std::size_t val_end = val_start;
                while (val_end < output.size() && (std::isdigit(static_cast<unsigned char>(output[val_end])) || output[val_end] == '.' || output[val_end] == '-')) {
                    ++val_end;
                }
                std::string num_str = output.substr(val_start, val_end - val_start);
                std::string quoted = "\"" + num_str + "\"";
                output.replace(val_start, val_end - val_start, quoted);
                pos = val_start + quoted.size();
            } else {
                pos = colon + 1;
            }
        }

        // Normalize unquoted numeric "value": 14500 -> "value": "14500"
        pos = 0;
        while ((pos = output.find("\"value\"", pos)) != std::string::npos) {
            std::size_t colon = output.find(':', pos + 7);
            if (colon == std::string::npos) break;
            std::size_t val_start = colon + 1;
            while (val_start < output.size() && (output[val_start] == ' ' || output[val_start] == '\t' || output[val_start] == '\r' || output[val_start] == '\n')) {
                ++val_start;
            }
            if (val_start < output.size() && (std::isdigit(static_cast<unsigned char>(output[val_start])) || output[val_start] == '.' || output[val_start] == '-')) {
                std::size_t val_end = val_start;
                while (val_end < output.size() && (std::isdigit(static_cast<unsigned char>(output[val_end])) || output[val_end] == '.' || output[val_end] == '-')) {
                    ++val_end;
                }
                std::string num_str = output.substr(val_start, val_end - val_start);
                std::string quoted = "\"" + num_str + "\"";
                output.replace(val_start, val_end - val_start, quoted);
                pos = val_start + quoted.size();
            } else {
                pos = colon + 1;
            }
        }

        return output;
    }

    [[nodiscard]] static bool is_supported_type(const std::string& type) {
        static constexpr std::array<std::string_view, 23> supported{
            "TextBlock", "Container", "ColumnSet", "Column", "FactSet", "Input.Text", "Input.Toggle",
            "Image", "Input.ChoiceSet", "Input.Number", "Input.Date", "Input.Time",
            "Media", "ImageSet", "RichTextBlock", "Table", "ActionSet", "TableColumnDefinition",
            "Rouen.SeriesPlot", "Rouen.Sparkline", "Rouen.Gauge", "Rouen.XYPlot", "Rouen.StatusBadge"
        };
        return std::ranges::find(supported, type) != supported.end();
    }

    static void validate_elements(const std::vector<element>& elements) {
        for (const auto& node : elements) {
            if (!node.type.empty() && !is_supported_type(node.type)) {
                throw std::runtime_error("Unsupported element type: " + node.type);
            }
            if (node.type == "Container" || node.type == "Column") {
                validate_elements(node.items);
            } else if (node.type == "ColumnSet") {
                for (const auto& column : node.columns) {
                    validate_elements(column.items);
                }
                validate_elements(node.columns);
            } else if (node.type == "ImageSet") {
                validate_elements(node.images);
            } else if (node.type == "Table") {
                validate_elements(node.columns);
                for (const auto& row : node.rows) {
                    for (const auto& cell : row.cells) {
                        validate_elements(cell.items);
                    }
                }
            } else if (node.type == "ActionSet") {
                validate_actions(node.actions);
            }
        }
    }

    [[nodiscard]] static bool is_supported_action_type(const std::string& type) {
        static constexpr std::array<std::string_view, 5> supported{
            "Action.OpenUrl", "Action.Submit", "Action.ShowCard", "Action.Execute", "Action.ToggleVisibility"
        };
        return std::ranges::find(supported, type) != supported.end();
    }

    static void validate_actions(const std::vector<action>& actions) {
        for (const auto& card_action : actions) {
            if (!card_action.type.empty() && !is_supported_action_type(card_action.type)) {
                throw std::runtime_error("Unsupported action type: " + card_action.type);
            }
            if (card_action.type == "Action.ShowCard") {
                if (!card_action.card.body.empty()) {
                    validate_elements(card_action.card.body);
                }
            }
        }
    }
};

} // namespace rouen::helpers::adaptive_cards

template <>
struct glz::meta<rouen::helpers::adaptive_cards::fact> {
    using T = rouen::helpers::adaptive_cards::fact;
    static constexpr auto values = glz::object(
        "title", &T::title,
        "value", &T::value
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::choice> {
    using T = rouen::helpers::adaptive_cards::choice;
    static constexpr auto values = glz::object(
        "title", &T::title,
        "value", &T::value
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::media_source> {
    using T = rouen::helpers::adaptive_cards::media_source;
    static constexpr auto values = glz::object(
        "mimeType", &T::mimeType,
        "url", &T::url
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::text_run> {
    using T = rouen::helpers::adaptive_cards::text_run;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "text", &T::text,
        "bold", &T::bold,
        "italic", &T::italic,
        "color", &T::color,
        "size", &T::size,
        "url", &T::url
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::table_cell> {
    using T = rouen::helpers::adaptive_cards::table_cell;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "items", &T::items
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::table_row> {
    using T = rouen::helpers::adaptive_cards::table_row;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "cells", &T::cells
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::action> {
    using T = rouen::helpers::adaptive_cards::action;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "title", &T::title,
        "url", &T::url,
        "verb", &T::verb,
        "style", &T::style,
        "requiredRole", &T::requiredRole,
        "requiresConfirmation", &T::requiresConfirmation,
        "confirmationPrompt", &T::confirmationPrompt,
        "requireReasonNote", &T::requireReasonNote,
        "data", &T::data,
        "targetElements", &T::targetElements,
        "card", &T::card
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::series_point> {
    using T = rouen::helpers::adaptive_cards::series_point;
    static constexpr auto values = glz::object(
        "label", &T::label,
        "value", &T::value
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::gauge_thresholds> {
    using T = rouen::helpers::adaptive_cards::gauge_thresholds;
    static constexpr auto values = glz::object(
        "lowLow", &T::lowLow,
        "low", &T::low,
        "high", &T::high,
        "highHigh", &T::highHigh
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::element> {
    using T = rouen::helpers::adaptive_cards::element;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "id", &T::id,
        "title", &T::title,
        "label", &T::label,
        "text", &T::text,
        "value", &T::value,
        "placeholder", &T::placeholder,
        "data", &T::data,
        "size", &T::size,
        "color", &T::color,
        "weight", &T::weight,
        "url", &T::url,
        "style", &T::style,
        "horizontalAlignment", &T::horizontalAlignment,
        "altText", &T::altText,
        "imageSize", &T::imageSize,
        "poster", &T::poster,
        "spacing", &T::spacing,
        "separator", &T::separator,
        "isMultiSelect", &T::isMultiSelect,
        "isMultiline", &T::isMultiline,
        "min", &T::min,
        "max", &T::max,
        "items", &T::items,
        "columns", &T::columns,
        "facts", &T::facts,
        "choices", &T::choices,
        "sources", &T::sources,
        "images", &T::images,
        "inlines", &T::inlines,
        "rows", &T::rows,
        "selectAction", &T::selectAction,
        "actions", &T::actions,
        "width", &T::width,
        "seriesName", &T::seriesName,
        "unit", &T::unit,
        "xLabel", &T::xLabel,
        "yLabel", &T::yLabel,
        "severity", &T::severity,
        "persistToRepository", &T::persistToRepository,
        "isBarChart", &T::isBarChart,
        "height", &T::height,
        "lowLimit", &T::lowLimit,
        "highLimit", &T::highLimit,
        "target", &T::target,
        "thresholds", &T::thresholds,
        "points", &T::points,
        "xyPoints", &T::xyPoints
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::show_card> {
    using T = rouen::helpers::adaptive_cards::show_card;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "body", &T::body
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::alarm_declaration> {
    using T = rouen::helpers::adaptive_cards::alarm_declaration;
    static constexpr auto values = glz::object(
        "id", &T::id,
        "severity", &T::severity,
        "title", &T::title,
        "value", &T::value,
        "latched", &T::latched
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_cards::card_document> {
    using T = rouen::helpers::adaptive_cards::card_document;
    static constexpr auto values = glz::object(
        "type", &T::type,
        "version", &T::version,
        "schema", &T::schema,
        "minHeight", &T::minHeight,
        "padding", &T::padding,
        "refreshIntervalMs", &T::refreshIntervalMs,
        "alarms", &T::alarms,
        "body", &T::body,
        "actions", &T::actions
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};
