#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "../src/cards/productivity/alarm.hpp"
#include "../src/cards/information/contact_card.hpp"
#include "../src/cards/productivity/theme_card.hpp"
#include "../src/cards/productivity/objectives_card.hpp"
#include "../src/cards/productivity/invoice_card.hpp"
#include "../src/cards/productivity/footprints_card.hpp"
#include "../src/cards/information/bybit_assets.hpp"
#include "../src/cards/information/weather.hpp"
#include "../src/cards/information/movies.hpp"
#include "../src/cards/information/calendar/calendar.hpp"
#include "../src/cards/information/rss.hpp"
#include "../src/cards/information/rss_feed.hpp"
#include "../src/cards/information/rss_item.hpp"
#include "../src/cards/information/ai_chat.hpp"

TEST(CardAdaptiveInterface, Rank2ProductivityAndInfoCards) {
    auto dummy_quitting = std::make_shared<std::function<bool()>>([]() { return false; });
    auto dummy_notify = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<bool()>>("quitting", dummy_quitting);
    registrar::add<std::function<void(const std::string&)>>("notify", dummy_notify);
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    // 1. Alarm
    rouen::cards::alarm alarm_card{};
    std::string alarm_json = alarm_card.get_adaptive_card_json();
    EXPECT_NE(alarm_json.find("AdaptiveCard"), std::string::npos);
    EXPECT_NE(alarm_json.find("Alarm"), std::string::npos);
    alarm_card.handle_action(R"({"verb":"toggle_alarm"})");

    // 2. Contact Card
    rouen::cards::contact_card contact{"new"};
    std::string contact_json = contact.get_adaptive_card_json();
    EXPECT_NE(contact_json.find("AdaptiveCard"), std::string::npos);
    contact.handle_action(R"({"verb":"toggle_edit"})");

    // 3. Theme Card
    rouen::cards::theme_card theme{};
    std::string theme_json = theme.get_adaptive_card_json();
    EXPECT_NE(theme_json.find("Theme Settings"), std::string::npos);
    theme.handle_action(R"({"verb":"next_theme"})");

    // 4. Objectives Card
    rouen::cards::objectives_card obj{};
    std::string obj_json = obj.get_adaptive_card_json();
    EXPECT_NE(obj_json.find("Objectives & Goals"), std::string::npos);
    obj.handle_action(R"({"verb":"refresh"})");

    // 5. Invoice Card
    rouen::cards::invoice_card inv{};
    std::string inv_json = inv.get_adaptive_card_json();
    EXPECT_NE(inv_json.find("Invoice"), std::string::npos);
    inv.handle_action(R"({"verb":"apply_retainer"})");

    // 6. Footprints Card
    rouen::cards::footprints_card fp{};
    std::string fp_json = fp.get_adaptive_card_json();
    EXPECT_NE(fp_json.find("FootPrints"), std::string::npos);
    fp.handle_action(R"({"verb":"logout"})");
}

TEST(CardAdaptiveInterface, BybitAssets) {
    rouen::cards::bybit_assets bybit{};
    std::string bybit_json = bybit.get_adaptive_card_json();
    EXPECT_NE(bybit_json.find("Bybit Assets"), std::string::npos);
    bybit.handle_action(R"({"verb":"refresh"})");
}

TEST(CardAdaptiveInterface, WeatherCard) {
    rouen::cards::weather weather_card{"Buenos Aires"};
    std::string weather_json = weather_card.get_adaptive_card_json();
    EXPECT_NE(weather_json.find("Weather"), std::string::npos);
    weather_card.handle_action(R"({"verb":"refresh"})");
}

TEST(CardAdaptiveInterface, MoviesCard) {
    rouen::cards::movies movies_card{};
    std::string movies_json = movies_card.get_adaptive_card_json();
    EXPECT_NE(movies_json.find("My Movies & Watchlists"), std::string::npos);
    movies_card.handle_action(R"({"verb":"refresh"})");
}

TEST(CardAdaptiveInterface, CalendarCard) {
    rouen::cards::calendar cal{};
    std::string cal_json = cal.get_adaptive_card_json();
    EXPECT_NE(cal_json.find("Calendar"), std::string::npos);
    cal.handle_action(R"({"verb":"refresh"})");
}

TEST(CardAdaptiveInterface, RSSCard) {
    rouen::cards::rss rss_card{};
    std::string rss_json = rss_card.get_adaptive_card_json();
    EXPECT_NE(rss_json.find("RSS Reader"), std::string::npos);
    rss_card.handle_action(R"({"verb":"refresh"})");
}

TEST(CardAdaptiveInterface, RSSFeedCard) {
    rouen::cards::rss_feed feed_card{"1"};
    std::string feed_json = feed_card.get_adaptive_card_json();
    EXPECT_NE(feed_json.find("AdaptiveCard"), std::string::npos);
    feed_card.handle_action(R"({"verb":"refresh"})");
}

TEST(CardAdaptiveInterface, RSSItemCard) {
    rouen::cards::rss_item item_card{"1"};
    std::string item_json = item_card.get_adaptive_card_json();
    EXPECT_NE(item_json.find("AdaptiveCard"), std::string::npos);
}

TEST(CardAdaptiveInterface, AIChatCard) {
    try {
        auto dummy_mcp = std::make_shared<rouen::hosts::mcp_host>();
        registrar::add<rouen::hosts::mcp_host>("mcp_service", dummy_mcp);
    } catch (...) {}
    rouen::cards::ai_chat chat{};
    std::string chat_json = chat.get_adaptive_card_json();
    EXPECT_NE(chat_json.find("AI Assistant Chat"), std::string::npos);
    chat.handle_action(R"({"verb":"send_message"})");
}
