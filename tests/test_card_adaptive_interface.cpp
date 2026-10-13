#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "../src/cards/productivity/alarm.hpp"
#include "../src/cards/productivity/kpi_card.hpp"
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
#include "../src/cards/information/rss_smart_list.hpp"
#include "../src/cards/information/ai_chat.hpp"
#include "../src/cards/system/about.hpp"
#include "../src/cards/system/sysinfo.hpp"
#include "../src/cards/system/cast_control.hpp"
#include "../src/cards/system/dbrepair.hpp"
#include "../src/cards/system/display_card.hpp"
#include "../src/cards/system/envvars.hpp"
#include "../src/cards/system/mesh_card.hpp"
#include "../src/cards/system/notifications.hpp"
#include "../src/cards/system/process_panel.hpp"
#include "../src/cards/system/settings.hpp"
#include "../src/cards/system/subnet_scanner.hpp"
#include "../src/cards/system/sync_card.hpp"
#include "../src/cards/productivity/pomodoro.hpp"
#include "../src/cards/production/adlib.hpp"
#include "../src/cards/development/vcproject_card.hpp"
#include "../src/cards/development/fs-directory.hpp"
#include "../src/cards/development/git.hpp"
#include "../src/cards/development/github/github_card.hpp"
#include "../src/cards/development/github/ci_card.hpp"
#include "../src/cards/productivity/calculator.hpp"
#include "../src/cards/productivity/converter.hpp"
#include "../src/cards/media/camera.hpp"
#include "../src/cards/media/chess_replay.hpp"
#include "../src/cards/media/media_card.hpp"
#include "../src/cards/media/media_companion.hpp"
#include "../src/cards/media/radio.hpp"
#include "../src/cards/media/radiocut.hpp"
#include "../src/cards/development/cmake.hpp"
#include "../src/cards/development/diff_card.hpp"
#include "../src/cards/information/directory_card.hpp"
#include "../src/cards/information/image_viewer.hpp"
#include "../src/cards/information/mail/mail.hpp"
#include "../src/cards/information/markdown_notes.hpp"
#include "../src/cards/information/markdown_viewer.hpp"
#include "../src/cards/information/number_series.hpp"
#include "../src/cards/information/pdf_viewer.hpp"
#include "../src/cards/information/solar_system.hpp"
#include "../src/cards/information/telegram.hpp"
#include "../src/helpers/adaptive_cards/parser.hpp"
#include "../src/helpers/markdown_renderer.hpp"
#include "../src/hosts/api_server_host.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-macro-identifier"
#endif
#include "../external/mongoose-7.15/mongoose.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

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
    EXPECT_NE(weather_json.find("Buenos Aires"), std::string::npos);
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
    if (!registrar::try_get<std::function<bool()>>("quitting")) {
        registrar::add<std::function<bool()>>("quitting", std::make_shared<std::function<bool()>>([]() { return false; }));
    }
    rouen::cards::rss rss_card{};
    std::string rss_json = rss_card.get_adaptive_card_json();
    EXPECT_NE(rss_json.find("RSS Reader"), std::string::npos);
    rss_card.handle_action(R"({"verb":"refresh"})");
}

TEST(CardAdaptiveInterface, RSSFreshnessCacheConcurrency) {
    if (!registrar::try_get<std::function<bool()>>("quitting")) {
        registrar::add<std::function<bool()>>("quitting", std::make_shared<std::function<bool()>>([]() { return false; }));
    }
    auto rss_card = std::make_shared<rouen::cards::rss>();
    std::atomic<bool> stop{false};
    std::vector<std::thread> workers;

    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                rss_card->invalidate_freshness_cache("https://example.com/feed.xml");
                rss_card->invalidate_freshness_cache();
            }
        });
    }

    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                try {
                    "invalidate_freshness_cache"_sfn("https://example.com/feed.xml");
                } catch (...) {}
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);

    for (auto& t : workers) {
        t.join();
    }

    rss_card.reset();
    EXPECT_NO_THROW({
        try {
            "invalidate_freshness_cache"_sfn("https://example.com/feed.xml");
        } catch (...) {}
    });
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
    
    // Initial card state verification
    std::string chat_json = chat.get_adaptive_card_json();
    EXPECT_NE(chat_json.find("AI Assistant Chat"), std::string::npos);
    EXPECT_NE(chat_json.find("message_input"), std::string::npos);
    EXPECT_NE(chat_json.find("send_message"), std::string::npos);
    EXPECT_NE(chat_json.find("clear_history"), std::string::npos);
    EXPECT_TRUE(chat_json.find("Provider:") != std::string::npos || chat_json.find("LLM not configured") != std::string::npos);

    // Test sending message with payload via action
    chat.handle_action(R"({"verb":"send_message","message_input":"Hello from unit test"})");
    chat_json = chat.get_adaptive_card_json();
    EXPECT_NE(chat_json.find("Hello from unit test"), std::string::npos);
    EXPECT_NE(chat_json.find("You"), std::string::npos);

    // Test clear history action
    chat.handle_action(R"({"verb":"clear_history"})");
    chat_json = chat.get_adaptive_card_json();
    EXPECT_EQ(chat_json.find("Hello from unit test"), std::string::npos);

    // Test internal turn count telemetry
    EXPECT_EQ(chat.get_internal_turn_count(), 0u);
    chat.set_internal_turn_count(3);
    EXPECT_EQ(chat.get_internal_turn_count(), 3u);
}

TEST(CardAdaptiveInterface, AboutCard) {
    rouen::cards::about_card about{nullptr};
    std::string about_json = about.get_adaptive_card_json();
    EXPECT_NE(about_json.find("Rouen Dashboard Application"), std::string::npos);
    EXPECT_NE(about_json.find("Version Information"), std::string::npos);
    EXPECT_NE(about_json.find("SemVer:"), std::string::npos);
}

TEST(CardAdaptiveInterface, SysinfoCard) {
    rouen::cards::sysinfo_card sysinfo{};
    std::string sysinfo_json = sysinfo.get_adaptive_card_json();
    EXPECT_NE(sysinfo_json.find("AdaptiveCard"), std::string::npos);
    EXPECT_NE(sysinfo_json.find("System Information"), std::string::npos);
    EXPECT_NE(sysinfo_json.find("Hardware Resources"), std::string::npos);
    EXPECT_NE(sysinfo_json.find("CPU Load:"), std::string::npos);
    EXPECT_NE(sysinfo_json.find("RAM Usage:"), std::string::npos);

    // Verify it is strictly valid JSON
    glz::json_t parsed;
    auto err = glz::read_json(parsed, sysinfo_json);
    EXPECT_FALSE(err);

    // Test actions
    sysinfo.handle_action(R"({"verb":"refresh"})");
    sysinfo.handle_action(R"({"verb":"run_benchmark"})");
}

TEST(CardAdaptiveInterface, PomodoroCard) {
    rouen::cards::pomodoro pom{};
    std::string pom_json = pom.get_adaptive_card_json();
    EXPECT_NE(pom_json.find("AdaptiveCard"), std::string::npos);
    EXPECT_NE(pom_json.find("Pomodoro Timer"), std::string::npos);
    EXPECT_NE(pom_json.find("Time Remaining:"), std::string::npos);

    // Verify it is strictly valid JSON
    glz::json_t parsed;
    auto err = glz::read_json(parsed, pom_json);
    EXPECT_FALSE(err);

    // Test action
    pom.handle_action(R"({"verb":"reset"})");
}

TEST(CardAdaptiveInterface, AlarmCardValidJsonNoEmbeddedNulls) {
    rouen::cards::alarm alarm_card{"alarm"};
    std::string alarm_json = alarm_card.get_adaptive_card_json();
    EXPECT_NE(alarm_json.find("AdaptiveCard"), std::string::npos);
    EXPECT_NE(alarm_json.find("Target:"), std::string::npos);

    // Critical check: ensure no embedded null bytes are present in string
    EXPECT_EQ(alarm_json.find('\0'), std::string::npos);

    // Verify it parses as valid JSON without errors
    glz::json_t parsed;
    auto err = glz::read_json(parsed, alarm_json);
    EXPECT_FALSE(err);

    // Verify that handle_cards_adaptive returns an adaptive_card JSON object, not a raw string
    auto cards_vector = std::vector<std::shared_ptr<card>>{ std::make_shared<rouen::cards::alarm>("alarm") };
    auto get_cards_fn = std::make_shared<std::function<std::vector<std::shared_ptr<card>>()>>(
        [cards_vector]() { return cards_vector; }
    );
    registrar::add<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards", get_cards_fn);

    struct mg_http_message hm_get {};
    std::string get_uri = "/api/cards/adaptive?index=0";
    std::string get_q = "index=0";
    hm_get.uri = mg_str_n(get_uri.data(), get_uri.size());
    hm_get.query = mg_str_n(get_q.data(), get_q.size());
    hm_get.method = mg_str("GET");

    std::string get_res = rouen::hosts::api_server_host::handle_cards_adaptive(nullptr, &hm_get);
    glz::json_t api_resp;
    auto api_err = glz::read_json(api_resp, get_res);
    EXPECT_FALSE(api_err);
    ASSERT_TRUE(api_resp.contains("adaptive_card"));
    EXPECT_TRUE(api_resp["adaptive_card"].is_object());
}


TEST(CardAdaptiveInterface, HttpApiAdaptiveAndActionEndpoints) {
    auto alarm_card = std::make_shared<rouen::cards::alarm>();
    auto cards_vector = std::vector<std::shared_ptr<card>>{ alarm_card };
    auto get_cards_fn = std::make_shared<std::function<std::vector<std::shared_ptr<card>>()>>(
        [cards_vector]() { return cards_vector; }
    );
    registrar::add<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards", get_cards_fn);

    // 1. GET /api/cards/adaptive
    struct mg_http_message hm_get {};
    std::string get_uri = "/api/cards/adaptive";
    hm_get.uri = mg_str_n(get_uri.data(), get_uri.size());
    hm_get.method = mg_str("GET");

    std::string get_res = rouen::hosts::api_server_host::handle_cards_adaptive(nullptr, &hm_get);
    EXPECT_NE(get_res.find("Alarm"), std::string::npos);
    EXPECT_NE(get_res.find("adaptive_card"), std::string::npos);

    // 2. GET /api/cards/adaptive?index=0
    struct mg_http_message hm_get_idx {};
    std::string get_idx_uri = "/api/cards/adaptive";
    std::string get_idx_q = "index=0";
    hm_get_idx.uri = mg_str_n(get_idx_uri.data(), get_idx_uri.size());
    hm_get_idx.query = mg_str_n(get_idx_q.data(), get_idx_q.size());
    hm_get_idx.method = mg_str("GET");

    std::string get_idx_res = rouen::hosts::api_server_host::handle_cards_adaptive(nullptr, &hm_get_idx);
    EXPECT_NE(get_idx_res.find("Alarm"), std::string::npos);
    EXPECT_NE(get_idx_res.find("adaptive_card"), std::string::npos);

    // 3. POST /api/cards/action
    struct mg_http_message hm_act {};
    std::string act_uri = "/api/cards/action";
    std::string act_body = R"({"index":0,"action":{"type":"Action.Execute","verb":"toggle_alarm"}})";
    hm_act.uri = mg_str_n(act_uri.data(), act_uri.size());
    hm_act.method = mg_str("POST");
    hm_act.body = mg_str_n(act_body.data(), act_body.size());

    std::string act_res = rouen::hosts::api_server_host::handle_cards_action(nullptr, &hm_act);
    EXPECT_NE(act_res.find("success"), std::string::npos);
    EXPECT_NE(act_res.find("dispatched"), std::string::npos);
}

TEST(ProcessApiTests, ProcessDefinitionsCRUD) {
    // 1. Create a new process definition via handle_process_definition_save
    struct mg_http_message hm_create {};
    std::string create_uri = "/api/process/definition";
    std::string create_body = R"({"name":"Unit Test Process","executable_path":"/bin/echo","arguments":"--test","working_directory":"/tmp"})";
    hm_create.uri = mg_str_n(create_uri.data(), create_uri.size());
    hm_create.method = mg_str("POST");
    hm_create.body = mg_str_n(create_body.data(), create_body.size());

    std::string create_res = rouen::hosts::api_server_host::handle_process_definition_save(nullptr, &hm_create);
    EXPECT_NE(create_res.find("\"success\":true"), std::string::npos);
    EXPECT_NE(create_res.find("\"name\":\"Unit Test Process\""), std::string::npos);

    // Extract generated id
    glz::json_t create_json;
    auto err = glz::read_json(create_json, create_res);
    ASSERT_FALSE(err);
    ASSERT_TRUE(create_json.contains("id"));
    int64_t def_id = static_cast<int64_t>(create_json["id"].get<double>());
    EXPECT_GT(def_id, 0);

    // 2. Fetch definition by id via handle_process_definition_get
    struct mg_http_message hm_get {};
    std::string get_uri = "/api/process/definition";
    std::string get_q = "id=" + std::to_string(def_id);
    hm_get.uri = mg_str_n(get_uri.data(), get_uri.size());
    hm_get.query = mg_str_n(get_q.data(), get_q.size());
    hm_get.method = mg_str("GET");

    std::string get_res = rouen::hosts::api_server_host::handle_process_definition_get(nullptr, &hm_get);
    EXPECT_NE(get_res.find("\"success\":true"), std::string::npos);
    EXPECT_NE(get_res.find("\"arguments\":\"--test\""), std::string::npos);

    // 3. Update existing definition via handle_process_definition_save
    struct mg_http_message hm_update {};
    std::string update_uri = "/api/process/definition";
    std::string update_body = "{\"id\":" + std::to_string(def_id) + ",\"name\":\"Updated Test Process\",\"arguments\":\"--updated\"}";
    hm_update.uri = mg_str_n(update_uri.data(), update_uri.size());
    hm_update.method = mg_str("POST");
    hm_update.body = mg_str_n(update_body.data(), update_body.size());

    std::string update_res = rouen::hosts::api_server_host::handle_process_definition_save(nullptr, &hm_update);
    EXPECT_NE(update_res.find("\"success\":true"), std::string::npos);
    EXPECT_NE(update_res.find("\"name\":\"Updated Test Process\""), std::string::npos);
    EXPECT_NE(update_res.find("\"arguments\":\"--updated\""), std::string::npos);

    // 4. Verify in processes list
    struct mg_http_message hm_list {};
    std::string list_uri = "/api/processes";
    hm_list.uri = mg_str_n(list_uri.data(), list_uri.size());
    hm_list.method = mg_str("GET");

    std::string list_res = rouen::hosts::api_server_host::handle_processes_list(nullptr, &hm_list);
    EXPECT_NE(list_res.find("Updated Test Process"), std::string::npos);

    // 5. Delete definition via handle_process_definition_delete
    struct mg_http_message hm_del {};
    std::string del_uri = "/api/process/definition";
    std::string del_q = "id=" + std::to_string(def_id);
    hm_del.uri = mg_str_n(del_uri.data(), del_uri.size());
    hm_del.query = mg_str_n(del_q.data(), del_q.size());
    hm_del.method = mg_str("DELETE");

    std::string del_res = rouen::hosts::api_server_host::handle_process_definition_delete(nullptr, &hm_del);
    EXPECT_NE(del_res.find("\"success\":true"), std::string::npos);
    EXPECT_NE(del_res.find("deleted successfully"), std::string::npos);

    // 6. Verify definition no longer exists
    std::string get_deleted_res = rouen::hosts::api_server_host::handle_process_definition_get(nullptr, &hm_get);
    EXPECT_NE(get_deleted_res.find("Process definition not found"), std::string::npos);
}

TEST(ProcessApiTests, OpenApiSpecIncludesAttachAndDefinitionEndpoints) {
    std::string spec = rouen::hosts::api_server_host::handle_openapi_spec(nullptr, nullptr);
    EXPECT_NE(spec.find("/api/process/attach"), std::string::npos);
    EXPECT_NE(spec.find("attachProcess"), std::string::npos);
    EXPECT_NE(spec.find("/api/process/definition"), std::string::npos);
    EXPECT_NE(spec.find("saveProcessDefinition"), std::string::npos);
    EXPECT_NE(spec.find("deleteProcessDefinition"), std::string::npos);
    EXPECT_NE(spec.find("/api/process/ui/screenshot"), std::string::npos);
    EXPECT_NE(spec.find("captureProcessWindowScreenshot"), std::string::npos);
}

TEST(ProcessApiTests, ProcessUIEndpointsValidation) {
    // Calling with non-existent / invalid process PID returns error response
    struct mg_http_message hm_set_val {};
    std::string uri = "/api/process/ui/set-value";
    std::string body = R"({"pid":0,"target":"Username","value":"Alice"})";
    hm_set_val.uri = mg_str_n(uri.data(), uri.size());
    hm_set_val.method = mg_str("POST");
    hm_set_val.body = mg_str_n(body.data(), body.size());

    std::string res = rouen::hosts::api_server_host::handle_process_ui_set_value(nullptr, &hm_set_val);
    EXPECT_NE(res.find("Valid running process identifier required"), std::string::npos);

    // Verify click and focus also properly require valid process
    std::string click_res = rouen::hosts::api_server_host::handle_process_ui_click(nullptr, &hm_set_val);
    EXPECT_NE(click_res.find("Valid running process identifier required"), std::string::npos);

    std::string focus_res = rouen::hosts::api_server_host::handle_process_ui_focus(nullptr, &hm_set_val);
    EXPECT_NE(focus_res.find("Valid running process identifier required"), std::string::npos);
}

TEST(ProcessApiTests, ProcessUIScreenshotEndpointValidation) {
    // 1. Missing / invalid process or window identifier returns error
    struct mg_http_message hm_empty {};
    std::string uri = "/api/process/ui/screenshot";
    std::string body_empty = R"({})";
    hm_empty.uri = mg_str_n(uri.data(), uri.size());
    hm_empty.method = mg_str("POST");
    hm_empty.body = mg_str_n(body_empty.data(), body_empty.size());

    std::string res_empty = rouen::hosts::api_server_host::handle_process_ui_screenshot(nullptr, &hm_empty);
    EXPECT_NE(res_empty.find("\"success\":false"), std::string::npos);
    EXPECT_NE(res_empty.find("required"), std::string::npos);

    // 2. Non-existent PID returns target window not found
    struct mg_http_message hm_pid {};
    std::string body_pid = R"({"pid":99999999,"filename":"/tmp/shot_nonexistent.png"})";
    hm_pid.uri = mg_str_n(uri.data(), uri.size());
    hm_pid.method = mg_str("POST");
    hm_pid.body = mg_str_n(body_pid.data(), body_pid.size());

    std::string res_pid = rouen::hosts::api_server_host::handle_process_ui_screenshot(nullptr, &hm_pid);
    EXPECT_NE(res_pid.find("\"success\":false"), std::string::npos);
    EXPECT_NE(res_pid.find("Target window not found"), std::string::npos);

    // 3. Hex string HWND format parsing
    struct mg_http_message hm_hex {};
    std::string body_hex = R"({"hwnd":"0xdeadbeef","filename":"/tmp/shot_hex.png"})";
    hm_hex.uri = mg_str_n(uri.data(), uri.size());
    hm_hex.method = mg_str("POST");
    hm_hex.body = mg_str_n(body_hex.data(), body_hex.size());

    std::string res_hex = rouen::hosts::api_server_host::handle_process_ui_screenshot(nullptr, &hm_hex);
    EXPECT_NE(res_hex.find("\"success\":false"), std::string::npos);

    // 4. GET request with query parameters
    struct mg_http_message hm_get {};
    std::string query = "pid=99999999&filename=/tmp/shot_get.png";
    hm_get.uri = mg_str_n(uri.data(), uri.size());
    hm_get.query = mg_str_n(query.data(), query.size());
    hm_get.method = mg_str("GET");

    std::string res_get = rouen::hosts::api_server_host::handle_process_ui_screenshot(nullptr, &hm_get);
    EXPECT_NE(res_get.find("\"success\":false"), std::string::npos);
    EXPECT_NE(res_get.find("Target window not found"), std::string::npos);
}

TEST(ProcessApiTests, ProcessUIWindowScopingAndWin32Actions) {
    // 1. Check OpenAPI specification contains new scoping fields and Win32 actions
    std::string spec = rouen::hosts::api_server_host::handle_openapi_spec(nullptr, nullptr);
    EXPECT_NE(spec.find("window_title"), std::string::npos);
    EXPECT_NE(spec.find("window_class"), std::string::npos);
    EXPECT_NE(spec.find("win32_click"), std::string::npos);
    EXPECT_NE(spec.find("win32_set_text"), std::string::npos);
    EXPECT_NE(spec.find("win32_command"), std::string::npos);

    // 2. Action endpoint with Win32 click action on macOS returns platform unsupported
    struct mg_http_message hm_action {};
    std::string uri = "/api/process/ui/action";
    std::string body = std::format(
        R"({{"pid":{},"target":"btn_ok","action":"win32_click","window_title":"MainDialog","window_class":"#32770","hwnd":"0x1234"}})",
        ::getpid()
    );
    hm_action.uri = mg_str_n(uri.data(), uri.size());
    hm_action.method = mg_str("POST");
    hm_action.body = mg_str_n(body.data(), body.size());

    std::string res_action = rouen::hosts::api_server_host::handle_process_ui_action(nullptr, &hm_action);
    EXPECT_NE(res_action.find("\"success\":false"), std::string::npos);
#if !defined(_WIN32)
    EXPECT_NE(res_action.find("only supported on Windows"), std::string::npos);
#endif

    // 3. Tree endpoint accepting scoped parameters (window_title, hwnd)
    struct mg_http_message hm_tree {};
    std::string uri_tree = "/api/process/ui/tree";
    std::string body_tree = std::format(
        R"({{"pid":{},"window_title":"NonExistentScopeWindow9999","max_depth":2}})",
        ::getpid()
    );
    hm_tree.uri = mg_str_n(uri_tree.data(), uri_tree.size());
    hm_tree.method = mg_str("POST");
    hm_tree.body = mg_str_n(body_tree.data(), body_tree.size());

    std::string res_tree = rouen::hosts::api_server_host::handle_process_ui_tree(nullptr, &hm_tree);
    EXPECT_NE(res_tree.find("total_node_count"), std::string::npos);

    // 4. Values endpoint GET with query parameters including window_title and window_class
    struct mg_http_message hm_values {};
    std::string uri_values = "/api/process/ui/values";
    std::string q_values = std::format("pid={}&window_title=NonExistentScopeWindow9999&window_class=DialogClass", ::getpid());
    hm_values.uri = mg_str_n(uri_values.data(), uri_values.size());
    hm_values.query = mg_str_n(q_values.data(), q_values.size());
    hm_values.method = mg_str("GET");

    std::string res_values = rouen::hosts::api_server_host::handle_process_ui_values(nullptr, &hm_values);
    EXPECT_NE(res_values.find("values"), std::string::npos);
}

TEST(CardAdaptiveInterface, VCProjectCard) {
    std::string proj_path = "examples/cpp23_win_task_runner/Cpp23TaskRunner.vcxproj";
    if (!std::filesystem::exists(proj_path)) {
        proj_path = "../examples/cpp23_win_task_runner/Cpp23TaskRunner.vcxproj";
    }

    rouen::cards::vcproject_card vc_card{proj_path};

    // 1. Verify Adaptive Card JSON schema
    std::string card_json = vc_card.get_adaptive_card_json();
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);
    EXPECT_NE(card_json.find("Visual Studio Project"), std::string::npos);
    EXPECT_NE(card_json.find("FactSet"), std::string::npos);
    EXPECT_NE(card_json.find("Action.Execute"), std::string::npos);
    EXPECT_NE(card_json.find("build"), std::string::npos);
    EXPECT_NE(card_json.find("rebuild"), std::string::npos);

    // Verify it parses as valid JSON via glaze
    glz::json_t doc;
    auto err = glz::read_json(doc, card_json);
    EXPECT_FALSE(err);
    EXPECT_EQ(doc["type"].get<std::string>(), "AdaptiveCard");
    EXPECT_EQ(doc["version"].get<std::string>(), "1.5");

    // 2. Test handle_action with Adaptive Card Execute payloads
    vc_card.handle_action(R"({"verb":"set_config","data":{"selected_config":"Debug"}})");
    std::string debug_card_json = vc_card.get_adaptive_card_json();
    EXPECT_NE(debug_card_json.find("Debug"), std::string::npos);

    vc_card.handle_action(R"({"verb":"set_platform","data":{"selected_platform":"x64"}})");
    vc_card.handle_action(R"({"verb":"select_stack","data":{"selected_stack":"0"}})");

    // 3. Test handle_action with direct string verbs
    vc_card.handle_action("check_syntax");
}

TEST(CardAdaptiveInterface, FsDirectoryFilterLookaheadAndRejection) {
    auto temp_dir = std::filesystem::temp_directory_path() / "rouen_test_fs_filter";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    // Create test files: alpha.txt, beta.cpp, document.pdf
    {
        std::ofstream(temp_dir / "alpha.txt") << "content";
        std::ofstream(temp_dir / "beta.cpp") << "content";
        std::ofstream(temp_dir / "document.pdf") << "content";
    }

    rouen::cards::fs_directory dir{temp_dir.string()};
    EXPECT_EQ(dir.get_filter(), "");
    EXPECT_EQ(dir.count_matches(""), 3u);

    // Typing 'a' matches alpha.txt -> accepted
    EXPECT_TRUE(dir.process_filter_char('a'));
    EXPECT_EQ(dir.get_filter(), "a");
    EXPECT_EQ(dir.count_matches(dir.get_filter()), 1u);

    // Typing 'l' matches alpha.txt -> accepted ("al")
    EXPECT_TRUE(dir.process_filter_char('l'));
    EXPECT_EQ(dir.get_filter(), "al");

    // Typing 'z' matches 0 files -> rejected! Filter remains "al"
    EXPECT_FALSE(dir.process_filter_char('z'));
    EXPECT_EQ(dir.get_filter(), "al");
    EXPECT_EQ(dir.get_last_rejected_char(), 'z');

    // Typing another invalid character 'x' -> rejected! Filter still "al"
    EXPECT_FALSE(dir.process_filter_char('x'));
    EXPECT_EQ(dir.get_filter(), "al");
    EXPECT_EQ(dir.get_last_rejected_char(), 'x');

    // Typing valid character 'p' -> accepted ("alp")
    EXPECT_TRUE(dir.process_filter_char('p'));
    EXPECT_EQ(dir.get_filter(), "alp");

    // Backspace pops one character
    EXPECT_TRUE(dir.process_filter_char('\b'));
    EXPECT_EQ(dir.get_filter(), "al");

    // Escape clears filter and resets rejection
    EXPECT_TRUE(dir.process_filter_char('\033'));
    EXPECT_EQ(dir.get_filter(), "");
    EXPECT_EQ(dir.get_last_rejected_char(), '\0');

    // Case-insensitivity: typing 'B' matches beta.cpp
    EXPECT_TRUE(dir.process_filter_char('B'));
    EXPECT_EQ(dir.get_filter(), "B");
    EXPECT_EQ(dir.count_matches(dir.get_filter()), 1u);

    std::filesystem::remove_all(temp_dir);
}

TEST(CardAdaptiveInterface, FsDirectoryAdaptiveCardJsonAndActions) {
    auto temp_dir = std::filesystem::temp_directory_path() / "rouen_test_fs_adaptive";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir / "subdir");

    {
        std::ofstream(temp_dir / "test.txt") << "hello world";
        std::ofstream(temp_dir / "subdir" / "nested.txt") << "nested content";
    }

    rouen::cards::fs_directory dir{temp_dir.string()};

    // URI matching
    EXPECT_TRUE(dir.matches_uri("dir"));
    EXPECT_TRUE(dir.matches_uri("dir:"));
    EXPECT_TRUE(dir.matches_uri(std::format("dir:{}", temp_dir.string())));

    // JSON generation
    std::string card_json = dir.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());

    glz::json_t parsed;
    auto err = glz::read_json(parsed, card_json);
    EXPECT_FALSE(err);
    EXPECT_EQ(parsed["type"].get<std::string>(), "AdaptiveCard");
    EXPECT_TRUE(card_json.find("subdir") != std::string::npos);
    EXPECT_TRUE(card_json.find("test.txt") != std::string::npos);

    // Action handling: navigate down into subdir
    std::string cd_action = std::format(R"({{"verb":"cd","data":{{"path":"{}"}}}})", (temp_dir / "subdir").string());
    dir.handle_action(cd_action);
    EXPECT_EQ(dir.get_uri(), std::format("dir:{}", (temp_dir / "subdir").string()));

    std::string sub_json = dir.get_adaptive_card_json();
    EXPECT_TRUE(sub_json.find("nested.txt") != std::string::npos);

    // Action handling: navigate up
    dir.handle_action(R"({"verb":"up"})");
    EXPECT_EQ(dir.get_uri(), std::format("dir:{}", temp_dir.string()));

    std::filesystem::remove_all(temp_dir);
}

TEST(GitCardTest, AISummaryStateAndMarkdownSeparation) {
    ::git git_card{};

    // Initial state expectations
    EXPECT_TRUE(git_card.last_ai_summary.empty());
    EXPECT_TRUE(git_card.last_ai_summary_repo.empty());
    EXPECT_TRUE(git_card.ai_summary_expanded);

    // Set sample AI markdown summary
    const std::string test_summary = 
        "### Working Tree Summary\n"
        "- feat(git): Rendered AI summaries in dedicated markdown box\n"
        "- fix(cli): Prevented raw status pollution in repo_status\n\n"
        "#### Modified Files\n"
        "- `src/cards/development/git.hpp`\n";

    git_card.last_ai_summary = test_summary;
    git_card.last_ai_summary_repo = "/path/to/my_repo";
    git_card.last_ai_summary_time = std::chrono::system_clock::now();
    git_card.selected_repo = "/path/to/my_repo";

    // Verify summary is stored independently and repo_status remains free of markdown pollution
    EXPECT_EQ(git_card.last_ai_summary, test_summary);
    EXPECT_EQ(git_card.repo_status.find("### Working Tree Summary"), std::string::npos);

    // Verify markdown primitives parse cleanly
    auto parsed_doc = rouen::helpers::parse_markdown_document(git_card.last_ai_summary);
    EXPECT_FALSE(parsed_doc.empty());
    size_t headings_count = 0;
    size_t list_items_count = 0;
    for (const auto& prim : parsed_doc.primitives) {
        if (std::holds_alternative<rouen::helpers::heading_primitive>(prim)) {
            headings_count++;
        } else if (std::holds_alternative<rouen::helpers::list_item_primitive>(prim)) {
            list_items_count++;
        }
    }
    EXPECT_GE(headings_count, 2u);
    EXPECT_GE(list_items_count, 3u);

    // Verify repository isolation: if selected_repo is different, summary repository does not match
    git_card.selected_repo = "/path/to/another_repo";
    EXPECT_NE(git_card.last_ai_summary_repo, git_card.selected_repo);
}

TEST(ApiServerHost, SchemasAndSchemesEndpointReturnsAllRegisteredSchemes) {
    // 1. Query registered schemas via api_server_host
    std::string json_schemas = rouen::hosts::api_server_host::handle_schemas_request(nullptr, nullptr);
    EXPECT_FALSE(json_schemas.empty());
    EXPECT_NE(json_schemas.find(R"("git")"), std::string::npos);
    EXPECT_NE(json_schemas.find(R"("ai_chat")"), std::string::npos);
    EXPECT_NE(json_schemas.find(R"("sysinfo")"), std::string::npos);
    EXPECT_NE(json_schemas.find(R"("weather")"), std::string::npos);
    EXPECT_NE(json_schemas.find(R"("alarm")"), std::string::npos);
    EXPECT_NE(json_schemas.find(R"("footprints")"), std::string::npos);
    EXPECT_NE(json_schemas.find(R"("terminal")"), std::string::npos);
    EXPECT_NE(json_schemas.find(R"("cmake")"), std::string::npos);

    // Verify parsed JSON array size is comprehensive (> 40 card schemes registered)
    glz::json_t parsed;
    auto err = glz::read_json(parsed, json_schemas);
    EXPECT_FALSE(err);
    ASSERT_TRUE(parsed.is_array());
    EXPECT_GT(parsed.get_array().size(), 40u);

    // 2. Verify OpenAPI spec documents both /api/schemas and /api/schemes
    std::string spec = rouen::hosts::api_server_host::handle_openapi_spec(nullptr, nullptr);
    EXPECT_NE(spec.find("/api/schemas"), std::string::npos);
    EXPECT_NE(spec.find("getSchemas"), std::string::npos);
    EXPECT_NE(spec.find("/api/schemes"), std::string::npos);
    EXPECT_NE(spec.find("getSchemes"), std::string::npos);
}

TEST(calculatorAdaptiveTest, JsonSerialization) {
    rouen::cards::calculator calc{};
    std::string card_json = calc.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify required FactSet fields and inputs exist
    bool found_memory_fact = false;
    bool found_mode_fact = false;
    bool found_last_eval_fact = false;
    bool found_direct_expr_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Text" && child.id == "direct_expr") {
                    found_direct_expr_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Memory (M):") found_memory_fact = true;
                if (fact.title == "Mode:") found_mode_fact = true;
                if (fact.title == "Last Eval:") found_last_eval_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_memory_fact);
    EXPECT_TRUE(found_mode_fact);
    EXPECT_TRUE(found_last_eval_fact);
    EXPECT_TRUE(found_direct_expr_input);
}

TEST(calculatorAdaptiveTest, ActionDispatch) {
    rouen::cards::calculator calc{};

    // 1. calc_input: append tokens
    calc.handle_action(R"({"verb":"calc_input","data":{"token":"12"}})");
    calc.handle_action(R"({"verb":"calc_input","data":{"token":" + "}})");
    calc.handle_action(R"({"verb":"calc_input","data":{"token":"8"}})");

    // 2. calc_eval: evaluate expression (12 + 8 = 20)
    calc.handle_action(R"({"verb":"calc_eval"})");
    std::string json_after_eval = calc.get_adaptive_card_json();
    EXPECT_NE(json_after_eval.find("20"), std::string::npos);
    EXPECT_NE(json_after_eval.find("12 + 8 = 20"), std::string::npos);

    // 3. calc_memory_store: store current 20 into memory
    calc.handle_action(R"({"verb":"calc_memory_store"})");
    std::string json_after_ms = calc.get_adaptive_card_json();
    EXPECT_NE(json_after_ms.find("20"), std::string::npos);

    // 4. calc_clear: clear display
    calc.handle_action(R"({"verb":"calc_clear"})");
    std::string json_after_clear = calc.get_adaptive_card_json();
    EXPECT_TRUE(json_after_clear.find(R"("text":"0")") != std::string::npos || json_after_clear.find(R"("text": "0")") != std::string::npos);

    // 5. calc_memory_recall: recall stored memory 20
    calc.handle_action(R"({"verb":"calc_memory_recall"})");
    std::string json_after_mr = calc.get_adaptive_card_json();
    EXPECT_NE(json_after_mr.find("20"), std::string::npos);

    // 6. Direct expression evaluation via Input.Text
    calc.handle_action(R"({"verb":"calc_eval","data":{"direct_expr":"50 * 2"}})");
    std::string json_direct = calc.get_adaptive_card_json();
    EXPECT_NE(json_direct.find("100"), std::string::npos);
}

TEST(calculatorAdaptiveTest, MalformedActionHandling) {
    rouen::cards::calculator calc{};
    EXPECT_NO_THROW(calc.handle_action(""));
    EXPECT_NO_THROW(calc.handle_action("not a json string"));
    EXPECT_NO_THROW(calc.handle_action("{"));
    EXPECT_NO_THROW(calc.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(calc.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(calc.handle_action("{\"verb\":\"calc_input\"}")); // Missing token
    EXPECT_NO_THROW(calc.handle_action("{\"verb\":\"calc_eval\",\"data\":null}"));
}

TEST(adlib_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::adlib_card adlib{};
    std::string card_json = adlib.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_pad1 = false;
    bool found_pad2 = false;
    bool found_pad3 = false;
    bool found_pad4 = false;
    bool found_bank_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && (child.id == "bank" || child.id == "soundbank")) {
                    found_bank_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Pad 1:" && fact.value.find("Applause") != std::string::npos) found_pad1 = true;
                if (fact.title == "Pad 2:" && fact.value.find("Drum Roll") != std::string::npos) found_pad2 = true;
                if (fact.title == "Pad 3:" && fact.value.find("Air Horn") != std::string::npos) found_pad3 = true;
                if (fact.title == "Pad 4:" && fact.value.find("Record Scratch") != std::string::npos) found_pad4 = true;
            }
        }
    }

    EXPECT_TRUE(found_pad1);
    EXPECT_TRUE(found_pad2);
    EXPECT_TRUE(found_pad3);
    EXPECT_TRUE(found_pad4);
    EXPECT_TRUE(found_bank_input);

    // Verify actions exist
    bool found_act_pad1 = false;
    bool found_act_pad2 = false;
    bool found_act_pad3 = false;
    bool found_act_stop = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "adlib_trigger") {
            if (act.title.find("Pad 1") != std::string::npos) found_act_pad1 = true;
            if (act.title.find("Pad 2") != std::string::npos) found_act_pad2 = true;
            if (act.title.find("Pad 3") != std::string::npos) found_act_pad3 = true;
        } else if (act.verb == "adlib_stop_all") {
            found_act_stop = true;
        }
    }
    EXPECT_TRUE(found_act_pad1);
    EXPECT_TRUE(found_act_pad2);
    EXPECT_TRUE(found_act_pad3);
    EXPECT_TRUE(found_act_stop);
}

TEST(adlib_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::adlib_card adlib{};

    // Initial state
    EXPECT_FALSE(adlib.is_any_playing());
    EXPECT_EQ(adlib.get_last_triggered_pad(), 0);

    // 1. adlib_trigger pad 1
    adlib.handle_action(R"({"type":"Action.Execute","verb":"adlib_trigger","data":{"pad":1}})");
    EXPECT_TRUE(adlib.is_any_playing());
    EXPECT_EQ(adlib.get_last_triggered_pad(), 1);
    ASSERT_GE(adlib.get_pads().size(), 1u);
    EXPECT_TRUE(adlib.get_pads()[0].is_playing);

    // 2. adlib_trigger pad 2 via pad_index
    adlib.handle_action(R"({"type":"Action.Execute","verb":"adlib_trigger","data":{"pad_index":2}})");
    EXPECT_TRUE(adlib.is_any_playing());
    EXPECT_EQ(adlib.get_last_triggered_pad(), 2);
    ASSERT_GE(adlib.get_pads().size(), 2u);
    EXPECT_TRUE(adlib.get_pads()[1].is_playing);

    // 3. adlib_load_bank: switch bank
    adlib.handle_action(R"({"type":"Action.Execute","verb":"adlib_load_bank","data":{"bank":"Audience Reactions"}})");
    EXPECT_EQ(adlib.get_soundbank_name(), "Audience Reactions");
    std::string updated_json = adlib.get_adaptive_card_json();
    EXPECT_NE(updated_json.find("Audience Reactions"), std::string::npos);

    // 4. adlib_stop_all: stop audio
    adlib.handle_action(R"({"type":"Action.Execute","verb":"adlib_stop_all"})");
    EXPECT_FALSE(adlib.is_any_playing());
    for (const auto& pad : adlib.get_pads()) {
        EXPECT_FALSE(pad.is_playing);
    }
}

TEST(adlib_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::adlib_card adlib{};
    EXPECT_NO_THROW(adlib.handle_action(""));
    EXPECT_NO_THROW(adlib.handle_action("not a json string"));
    EXPECT_NO_THROW(adlib.handle_action("{"));
    EXPECT_NO_THROW(adlib.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(adlib.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(adlib.handle_action("{\"verb\":\"adlib_trigger\"}")); // Missing pad
    EXPECT_NO_THROW(adlib.handle_action("{\"verb\":\"adlib_load_bank\",\"data\":null}"));
}

TEST(camera_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::camera_card camera{};
    std::string card_json = camera.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_device_choice = false;
    bool found_resolution = false;
    bool found_framerate = false;
    bool found_pixelformat = false;
    bool found_snapshot = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && (child.id == "device_choice" || child.id == "device")) {
                    found_device_choice = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Resolution:") found_resolution = true;
                if (fact.title == "Frame Rate:") found_framerate = true;
                if (fact.title == "Pixel Format:") found_pixelformat = true;
                if (fact.title == "Last Snapshot:") found_snapshot = true;
            }
        }
    }

    EXPECT_TRUE(found_device_choice);
    EXPECT_TRUE(found_resolution);
    EXPECT_TRUE(found_framerate);
    EXPECT_TRUE(found_pixelformat);
    EXPECT_TRUE(found_snapshot);

    // Verify actions exist
    bool found_capture_act = false;
    bool found_toggle_act = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "cam_capture") found_capture_act = true;
        if (act.verb == "cam_toggle_stream") found_toggle_act = true;
    }
    EXPECT_TRUE(found_capture_act);
    EXPECT_TRUE(found_toggle_act);
}

TEST(camera_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::camera_card camera{};

    // Initial state assertions
    EXPECT_FALSE(camera.is_capturing());
    EXPECT_EQ(camera.get_selected_device_index(), 0);
    EXPECT_TRUE(camera.get_last_snapshot_path().empty());

    // 1. cam_toggle_stream: activate stream
    camera.handle_action(R"({"type":"Action.Execute","verb":"cam_toggle_stream"})");
    EXPECT_TRUE(camera.is_capturing());

    // 2. cam_switch_device: select device
    camera.handle_action(R"({"type":"Action.Execute","verb":"cam_switch_device","data":{"device_choice":"dev_1"}})");
    EXPECT_EQ(camera.get_selected_device_index(), 1);

    // 3. cam_capture: take snapshot
    camera.handle_action(R"({"type":"Action.Execute","verb":"cam_capture"})");
    EXPECT_FALSE(camera.get_last_snapshot_path().empty());
    std::string updated_json = camera.get_adaptive_card_json();
    EXPECT_NE(updated_json.find(camera.get_last_snapshot_path()), std::string::npos);

    // 4. cam_toggle_stream: deactivate stream
    camera.handle_action(R"({"type":"Action.Execute","verb":"cam_toggle_stream"})");
    EXPECT_FALSE(camera.is_capturing());
}

TEST(camera_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::camera_card camera{};
    EXPECT_NO_THROW(camera.handle_action(""));
    EXPECT_NO_THROW(camera.handle_action("not a json string"));
    EXPECT_NO_THROW(camera.handle_action("{"));
    EXPECT_NO_THROW(camera.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(camera.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(camera.handle_action("{\"verb\":\"cam_switch_device\"}")); // Missing device
    EXPECT_NO_THROW(camera.handle_action("{\"verb\":\"cam_capture\",\"data\":null}"));
}

TEST(cast_controlAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::cast_control cast_card{};
    std::string card_json = cast_card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_endpoint_fact = false;
    bool found_viewers_fact = false;
    bool found_resolution_fact = false;
    bool found_framerate_fact = false;
    bool found_resolution_input = false;
    bool found_fps_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && (child.id == "resolution" || child.id == "res")) {
                    found_resolution_input = true;
                }
                if (child.type == "Input.Number" && (child.id == "fps" || child.id == "target_fps")) {
                    found_fps_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Stream Endpoint:") found_endpoint_fact = true;
                if (fact.title == "Active Viewers:") found_viewers_fact = true;
                if (fact.title == "Resolution:") found_resolution_fact = true;
                if (fact.title == "Framerate:" || fact.title == "Frame Rate:") found_framerate_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_endpoint_fact);
    EXPECT_TRUE(found_viewers_fact);
    EXPECT_TRUE(found_resolution_fact);
    EXPECT_TRUE(found_framerate_fact);
    EXPECT_TRUE(found_resolution_input);
    EXPECT_TRUE(found_fps_input);

    // Verify actions exist
    bool found_start_act = false;
    bool found_copy_act = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "cast_start") found_start_act = true;
        if (act.verb == "cast_copy_url") found_copy_act = true;
    }
    EXPECT_TRUE(found_start_act);
    EXPECT_TRUE(found_copy_act);
}

TEST(cast_controlAdaptiveTest, ActionDispatch) {
    rouen::cards::cast_control cast_card{};

    // Initial state assertions
    EXPECT_FALSE(cast_card.is_broadcasting());

    // 1. cast_start: start broadcast with fps and resolution
    cast_card.handle_action(R"raw({"type":"Action.Execute","verb":"cast_start","data":{"fps":60,"resolution":"1920 x 1080 (1080p)"}})raw");
    EXPECT_TRUE(cast_card.is_broadcasting());
    EXPECT_EQ(cast_card.get_target_fps(), 60);
    EXPECT_EQ(cast_card.get_resolution(), "1920 x 1080 (1080p)");

    std::string streaming_json = cast_card.get_adaptive_card_json();
    EXPECT_NE(streaming_json.find("STREAMING"), std::string::npos);
    EXPECT_NE(streaming_json.find("cast_stop"), std::string::npos);

    // 2. cast_copy_url: copy live stream url
    cast_card.handle_action(R"raw({"type":"Action.Execute","verb":"cast_copy_url"})raw");
    EXPECT_FALSE(cast_card.get_last_copied_url().empty());
    EXPECT_EQ(cast_card.get_last_copied_url(), cast_card.get_stream_endpoint());

    // 3. cast_stop: halt broadcast
    cast_card.handle_action(R"raw({"type":"Action.Execute","verb":"cast_stop"})raw");
    EXPECT_FALSE(cast_card.is_broadcasting());

    std::string stopped_json = cast_card.get_adaptive_card_json();
    EXPECT_NE(stopped_json.find("STOPPED"), std::string::npos);
    EXPECT_NE(stopped_json.find("cast_start"), std::string::npos);
}

TEST(cast_controlAdaptiveTest, MalformedActionHandling) {
    rouen::cards::cast_control cast_card{};
    EXPECT_NO_THROW(cast_card.handle_action(""));
    EXPECT_NO_THROW(cast_card.handle_action("not a json string"));
    EXPECT_NO_THROW(cast_card.handle_action("{"));
    EXPECT_NO_THROW(cast_card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(cast_card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(cast_card.handle_action("{\"verb\":\"cast_start\",\"data\":null}"));
    EXPECT_NO_THROW(cast_card.handle_action("{\"verb\":\"cast_stop\"}"));
}

TEST(chess_replayAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::chess_replay chess_card{};
    std::string card_json = chess_card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_white_player = false;
    bool found_black_player = false;
    bool found_opening = false;
    bool found_last_move = false;
    bool found_fen = false;
    bool found_user_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Text" && child.id == "chess_com_user") {
                    found_user_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "White Player:") found_white_player = true;
                if (fact.title == "Black Player:") found_black_player = true;
                if (fact.title == "Opening:") found_opening = true;
                if (fact.title == "Last Move:") found_last_move = true;
                if (fact.title == "FEN:") found_fen = true;
            }
        }
    }

    EXPECT_TRUE(found_white_player);
    EXPECT_TRUE(found_black_player);
    EXPECT_TRUE(found_opening);
    EXPECT_TRUE(found_last_move);
    EXPECT_TRUE(found_fen);
    EXPECT_TRUE(found_user_input);

    // Verify actions exist
    bool found_prev_move = false;
    bool found_next_move = false;
    bool found_flip_board = false;
    bool found_search_user = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "chess_prev_move") found_prev_move = true;
        if (act.verb == "chess_next_move") found_next_move = true;
        if (act.verb == "chess_flip_board") found_flip_board = true;
        if (act.verb == "chess_search_user") found_search_user = true;
    }

    EXPECT_TRUE(found_prev_move);
    EXPECT_TRUE(found_next_move);
    EXPECT_TRUE(found_flip_board);
    EXPECT_TRUE(found_search_user);
}

TEST(chess_replayAdaptiveTest, ActionDispatch) {
    rouen::cards::chess_replay chess_card{};

    // Initial state
    EXPECT_FALSE(chess_card.is_board_flipped());
    ASSERT_NE(chess_card.get_game(), nullptr);
    EXPECT_EQ(chess_card.get_game()->get_current_move_index(), 0u);

    // Load game content
    const std::string pgn =
        "[Event \"Kasparov vs Deep Blue\"]\n"
        "[Site \"Philadelphia, PA USA\"]\n"
        "[Date \"1996.02.10\"]\n"
        "[White \"Garry Kasparov\"]\n"
        "[Black \"Deep Blue\"]\n"
        "[Result \"1-0\"]\n"
        "1. e4 c5 2. c3 d5 3. exd5 Qxd5 4. d4 Nf6 5. Nf3 Bg4 6. Be2 e6 1-0\n";
    EXPECT_TRUE(chess_card.load_pgn_content(pgn));
    EXPECT_GT(chess_card.get_game()->get_move_count(), 0u);
    EXPECT_EQ(chess_card.get_game()->get_current_move_index(), 0u);

    // 1. chess_next_move
    chess_card.handle_action(R"({"type":"Action.Execute","verb":"chess_next_move"})");
    EXPECT_EQ(chess_card.get_game()->get_current_move_index(), 1u);

    chess_card.handle_action(R"({"type":"Action.Execute","verb":"chess_next_move"})");
    EXPECT_EQ(chess_card.get_game()->get_current_move_index(), 2u);

    // 2. chess_prev_move
    chess_card.handle_action(R"({"type":"Action.Execute","verb":"chess_prev_move"})");
    EXPECT_EQ(chess_card.get_game()->get_current_move_index(), 1u);

    // 3. chess_flip_board
    chess_card.handle_action(R"({"type":"Action.Execute","verb":"chess_flip_board"})");
    EXPECT_TRUE(chess_card.is_board_flipped());

    chess_card.handle_action(R"({"type":"Action.Execute","verb":"chess_flip_board"})");
    EXPECT_FALSE(chess_card.is_board_flipped());

    // 4. chess_search_user
    chess_card.handle_action(R"({"type":"Action.Execute","verb":"chess_search_user","data":{"chess_com_user":"magnuscarlsen"}})");
    EXPECT_EQ(chess_card.get_chess_com_username(), "magnuscarlsen");

    chess_card.handle_action(R"({"type":"Action.Execute","verb":"chess_search_user","data":{"username":"hikaru"}})");
    EXPECT_EQ(chess_card.get_chess_com_username(), "hikaru");
}

TEST(chess_replayAdaptiveTest, MalformedActionHandling) {
    rouen::cards::chess_replay chess_card{};
    EXPECT_NO_THROW(chess_card.handle_action(""));
    EXPECT_NO_THROW(chess_card.handle_action("not a json string"));
    EXPECT_NO_THROW(chess_card.handle_action("{"));
    EXPECT_NO_THROW(chess_card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(chess_card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(chess_card.handle_action("{\"verb\":\"chess_search_user\",\"data\":null}"));
}

TEST(cmake_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::cmake_card cmake_card{"/Users/ignaciorodriguez/src/rouen/CMakeLists.txt"};
    std::string card_json = cmake_card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_generator = false;
    bool found_build_type = false;
    bool found_max_jobs = false;
    bool found_target = false;
    bool found_build_type_input = false;
    bool found_target_name_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "build_type") {
                    found_build_type_input = true;
                }
                if (child.type == "Input.ChoiceSet" && child.id == "target_name") {
                    found_target_name_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Generator:") found_generator = true;
                if (fact.title == "Build Type:") found_build_type = true;
                if (fact.title == "Max Parallel Jobs:") found_max_jobs = true;
                if (fact.title == "Primary Target:") found_target = true;
            }
        }
    }

    EXPECT_TRUE(found_generator);
    EXPECT_TRUE(found_build_type);
    EXPECT_TRUE(found_max_jobs);
    EXPECT_TRUE(found_target);
    EXPECT_TRUE(found_build_type_input);
    EXPECT_TRUE(found_target_name_input);

    // Verify actions exist
    bool found_configure_act = false;
    bool found_build_act = false;
    bool found_clean_act = false;
    bool found_syntax_act = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "configure") found_configure_act = true;
        if (act.verb == "build") found_build_act = true;
        if (act.verb == "clean") found_clean_act = true;
        if (act.verb == "check_syntax") found_syntax_act = true;
    }
    EXPECT_TRUE(found_configure_act);
    EXPECT_TRUE(found_build_act);
    EXPECT_TRUE(found_clean_act);
    EXPECT_TRUE(found_syntax_act);
}

TEST(cmake_cardAdaptiveTest, ActionDispatch) {
    auto dummy_run_cmd = std::make_shared<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>>(
        [](std::string const&, std::shared_ptr<std::function<void(std::string)>>) {}
    );
    registrar::add<std::function<void(std::string const&, std::shared_ptr<std::function<void(std::string)>>)>>("run_command", dummy_run_cmd);

    rouen::cards::cmake_card cmake_card{"/Users/ignaciorodriguez/src/rouen/CMakeLists.txt"};

    // Initial state assertions
    EXPECT_EQ(cmake_card.get_build_type(), "Release");
    EXPECT_EQ(cmake_card.get_selected_target(), "rouen");
    EXPECT_FALSE(cmake_card.is_building());
    EXPECT_FALSE(cmake_card.is_adaptive_view_mode());

    // 1. configure with build_type
    cmake_card.handle_action(R"({"type":"Action.Execute","verb":"configure","data":{"build_type":"Debug"}})");
    EXPECT_EQ(cmake_card.get_build_type(), "Debug");
    EXPECT_TRUE(cmake_card.is_building());
    EXPECT_NE(cmake_card.get_last_command().find("Debug"), std::string::npos);
    EXPECT_NE(cmake_card.get_adaptive_card_json().find("Debug"), std::string::npos);

    // Cancel to reset cmd_running_
    cmake_card.cancel_running_action();
    EXPECT_FALSE(cmake_card.is_building());

    // 2. build with target_name
    cmake_card.handle_action(R"({"type":"Action.Execute","verb":"build","data":{"target_name":"unit_tests"}})");
    EXPECT_EQ(cmake_card.get_selected_target(), "unit_tests");
    EXPECT_TRUE(cmake_card.is_building());
    EXPECT_NE(cmake_card.get_last_command().find("unit_tests"), std::string::npos);
    EXPECT_NE(cmake_card.get_last_command().find("-j2"), std::string::npos);
    EXPECT_NE(cmake_card.get_adaptive_card_json().find("unit_tests"), std::string::npos);

    cmake_card.cancel_running_action();
    EXPECT_FALSE(cmake_card.is_building());

    // 3. clean
    cmake_card.handle_action(R"({"type":"Action.Execute","verb":"clean"})");
    EXPECT_TRUE(cmake_card.is_building());
    EXPECT_NE(cmake_card.get_last_command().find("clean"), std::string::npos);

    cmake_card.cancel_running_action();
    EXPECT_FALSE(cmake_card.is_building());

    // 4. check_syntax
    cmake_card.handle_action(R"({"type":"Action.Execute","verb":"check_syntax"})");
    EXPECT_TRUE(cmake_card.is_checking_syntax());

    // 5. toggle_adaptive_view
    cmake_card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(cmake_card.is_adaptive_view_mode());
    cmake_card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(cmake_card.is_adaptive_view_mode());
}

TEST(cmake_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::cmake_card cmake_card{"/Users/ignaciorodriguez/src/rouen/CMakeLists.txt"};
    EXPECT_NO_THROW(cmake_card.handle_action(""));
    EXPECT_NO_THROW(cmake_card.handle_action("not a json string"));
    EXPECT_NO_THROW(cmake_card.handle_action("{"));
    EXPECT_NO_THROW(cmake_card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(cmake_card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(cmake_card.handle_action("{\"verb\":\"configure\",\"data\":null}"));
    EXPECT_NO_THROW(cmake_card.handle_action("{\"verb\":\"build\",\"data\":null}"));
}

TEST(converterAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::converter conv{};
    std::string card_json = conv.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_category_input = false;
    bool found_value_input = false;
    bool found_from_unit_input = false;
    bool found_to_unit_input = false;
    bool found_category_fact = false;
    bool found_from_unit_fact = false;
    bool found_to_unit_fact = false;
    bool found_result_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "category") {
                    found_category_input = true;
                }
                if (child.type == "Input.Number" && child.id == "input_value") {
                    found_value_input = true;
                }
                if (child.type == "ColumnSet") {
                    for (const auto& col : child.columns) {
                        for (const auto& col_child : col.items) {
                            if (col_child.type == "Input.ChoiceSet" && col_child.id == "from_unit") {
                                found_from_unit_input = true;
                            }
                            if (col_child.type == "Input.ChoiceSet" && col_child.id == "to_unit") {
                                found_to_unit_input = true;
                            }
                        }
                    }
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Category:") found_category_fact = true;
                if (fact.title == "From Unit:") found_from_unit_fact = true;
                if (fact.title == "To Unit:") found_to_unit_fact = true;
                if (fact.title == "Result:") found_result_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_category_input);
    EXPECT_TRUE(found_value_input);
    EXPECT_TRUE(found_from_unit_input);
    EXPECT_TRUE(found_to_unit_input);
    EXPECT_TRUE(found_category_fact);
    EXPECT_TRUE(found_from_unit_fact);
    EXPECT_TRUE(found_to_unit_fact);
    EXPECT_TRUE(found_result_fact);

    // Verify actions
    bool found_convert_act = false;
    bool found_swap_act = false;
    bool found_copy_act = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "convert") found_convert_act = true;
        if (act.verb == "swap_units") found_swap_act = true;
        if (act.verb == "copy_result") found_copy_act = true;
    }
    EXPECT_TRUE(found_convert_act);
    EXPECT_TRUE(found_swap_act);
    EXPECT_TRUE(found_copy_act);
}

TEST(converterAdaptiveTest, ActionDispatch) {
    rouen::cards::converter conv{};

    // Initial state assertions
    EXPECT_EQ(conv.get_category(), "Length");
    EXPECT_FALSE(conv.is_adaptive_view_mode());

    // 1. Convert units: Length, Meters to Feet, input 100 -> 328.084 ft
    conv.handle_action(R"({"type":"Action.Execute","verb":"convert","data":{"category":"Length","from_unit":"Meters","to_unit":"Feet","input_value":100}})");
    EXPECT_EQ(conv.get_category(), "Length");
    EXPECT_EQ(conv.get_from_unit(), "Meters");
    EXPECT_EQ(conv.get_to_unit(), "Feet");
    EXPECT_DOUBLE_EQ(conv.get_input_value(), 100.0);
    EXPECT_NE(conv.get_conversion_result().find("328.084"), std::string::npos);
    EXPECT_NE(conv.get_adaptive_card_json().find("328.084"), std::string::npos);

    // 2. Swap units: Meters/Feet -> Feet/Meters
    conv.handle_action(R"({"type":"Action.Execute","verb":"swap_units"})");
    EXPECT_EQ(conv.get_from_unit(), "Feet");
    EXPECT_EQ(conv.get_to_unit(), "Meters");
    // Result recalculated for 100 Feet to Meters: 30.48 m
    EXPECT_NE(conv.get_conversion_result().find("30.48"), std::string::npos);

    // 3. Copy result
    conv.handle_action(R"({"type":"Action.Execute","verb":"copy_result"})");
    EXPECT_EQ(conv.get_last_copied(), conv.get_conversion_result());

    // 4. Convert units with category switch: Temperature, Celsius to Fahrenheit, 100 C -> 212 F
    conv.handle_action(R"({"type":"Action.Execute","verb":"convert","data":{"category":"Temperature","from_unit":"Celsius","to_unit":"Fahrenheit","input_value":100}})");
    EXPECT_EQ(conv.get_category(), "Temperature");
    EXPECT_EQ(conv.get_from_unit(), "Celsius");
    EXPECT_EQ(conv.get_to_unit(), "Fahrenheit");
    EXPECT_NE(conv.get_conversion_result().find("212"), std::string::npos);

    // 5. Toggle adaptive view
    conv.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(conv.is_adaptive_view_mode());
    conv.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(conv.is_adaptive_view_mode());
}

TEST(converterAdaptiveTest, MalformedActionHandling) {
    rouen::cards::converter conv{};
    EXPECT_NO_THROW(conv.handle_action(""));
    EXPECT_NO_THROW(conv.handle_action("not a json string"));
    EXPECT_NO_THROW(conv.handle_action("{"));
    EXPECT_NO_THROW(conv.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(conv.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(conv.handle_action("{\"verb\":\"convert\",\"data\":null}"));
    EXPECT_NO_THROW(conv.handle_action("{\"verb\":\"convert\",\"data\":{\"category\":\"NonExistent\",\"input_value\":\"not_a_number\"}}"));
}

TEST(dbrepair_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::dbrepair_card dbrepair{};
    std::string card_json = dbrepair.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_database_path = false;
    bool found_file_size = false;
    bool found_integrity_status = false;
    bool found_last_compacted = false;
    bool found_database_choice = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "database_choice") {
                    found_database_choice = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Database Path:") found_database_path = true;
                if (fact.title == "File Size:") found_file_size = true;
                if (fact.title == "Integrity Status:") found_integrity_status = true;
                if (fact.title == "Last Compacted:") found_last_compacted = true;
            }
        }
    }

    EXPECT_TRUE(found_database_path);
    EXPECT_TRUE(found_file_size);
    EXPECT_TRUE(found_integrity_status);
    EXPECT_TRUE(found_last_compacted);
    EXPECT_TRUE(found_database_choice);

    // Verify actions exist
    bool found_check_integrity = false;
    bool found_vacuum = false;
    bool found_backup = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "db_check_integrity") found_check_integrity = true;
        if (act.verb == "db_vacuum") found_vacuum = true;
        if (act.verb == "db_backup") found_backup = true;
    }
    EXPECT_TRUE(found_check_integrity);
    EXPECT_TRUE(found_vacuum);
    EXPECT_TRUE(found_backup);
}

TEST(dbrepair_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::dbrepair_card dbrepair{};

    // Initial state assertions
    EXPECT_FALSE(dbrepair.is_adaptive_view_mode());
    EXPECT_FALSE(dbrepair.get_selected_database().empty());

    // 1. db_check_integrity
    dbrepair.handle_action(R"({"type":"Action.Execute","verb":"db_check_integrity","data":{"database_choice":"adaptive_cards.db"}})");
    EXPECT_EQ(dbrepair.get_selected_database(), "adaptive_cards.db");
    EXPECT_NE(dbrepair.get_integrity_status().find("ok"), std::string::npos);

    // 2. db_vacuum
    dbrepair.handle_action(R"({"type":"Action.Execute","verb":"db_vacuum","data":{"db_target":"rouen.db"}})");
    EXPECT_EQ(dbrepair.get_selected_database(), "rouen.db");
    EXPECT_FALSE(dbrepair.get_last_compacted().empty());

    // 3. db_backup
    dbrepair.handle_action(R"({"type":"Action.Execute","verb":"db_backup","data":{"db_target":"rss_feeds.db"}})");
    EXPECT_EQ(dbrepair.get_selected_database(), "rss_feeds.db");
    EXPECT_FALSE(dbrepair.get_last_backup_path().empty());
    EXPECT_NE(dbrepair.get_last_backup_path().find("rss_feeds.db"), std::string::npos);

    // 4. toggle_adaptive_view
    dbrepair.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(dbrepair.is_adaptive_view_mode());
    dbrepair.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(dbrepair.is_adaptive_view_mode());
}

TEST(dbrepair_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::dbrepair_card dbrepair{};
    EXPECT_NO_THROW(dbrepair.handle_action(""));
    EXPECT_NO_THROW(dbrepair.handle_action("not a json string"));
    EXPECT_NO_THROW(dbrepair.handle_action("{"));
    EXPECT_NO_THROW(dbrepair.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(dbrepair.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(dbrepair.handle_action("{\"verb\":\"db_check_integrity\",\"data\":null}"));
    EXPECT_NO_THROW(dbrepair.handle_action("{\"verb\":\"db_vacuum\",\"data\":{\"database_choice\":\"nonexistent.db\"}}"));
    EXPECT_NO_THROW(dbrepair.handle_action("{\"verb\":\"db_backup\",\"data\":{\"db_target\":\"nonexistent.db\"}}"));
}

TEST(diff_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::diff_card card{};
    std::string old_content = "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\nline 11\nline 12\n";
    std::string new_content = "line 1\nline 2 mod\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\nline 11 mod\nline 12\n";
    card.load_arbitrary_diff(old_content, new_content, "src/cards/development/diff_card.cpp");

    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    // Verify all required FactSet fields and inputs exist
    bool found_target_file = false;
    bool found_view_mode = false;
    bool found_diff_source = false;
    bool found_line_stats = false;
    bool found_file_selector = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "file_selector") {
                    found_file_selector = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Target File:") found_target_file = true;
                if (fact.title == "View Mode:") found_view_mode = true;
                if (fact.title == "Diff Source:") found_diff_source = true;
                if (fact.title == "Line Stats:") found_line_stats = true;
            }
        }
    }

    EXPECT_TRUE(found_target_file);
    EXPECT_TRUE(found_view_mode);
    EXPECT_TRUE(found_diff_source);
    EXPECT_TRUE(found_line_stats);
    EXPECT_TRUE(found_file_selector);

    // Verify actions exist
    bool found_next_hunk = false;
    bool found_stage_hunk = false;
    bool found_discard_hunk = false;
    bool found_toggle_view_mode = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "next_hunk") found_next_hunk = true;
        if (act.verb == "stage_hunk") found_stage_hunk = true;
        if (act.verb == "discard_hunk") found_discard_hunk = true;
        if (act.verb == "toggle_view_mode") found_toggle_view_mode = true;
    }
    EXPECT_TRUE(found_next_hunk);
    EXPECT_TRUE(found_stage_hunk);
    EXPECT_TRUE(found_discard_hunk);
    EXPECT_TRUE(found_toggle_view_mode);
}

TEST(diff_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::diff_card card{};
    std::string old_content = "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\nline 11\nline 12\n";
    std::string new_content = "line 1\nline 2 mod\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\nline 11 mod\nline 12\n";
    card.load_arbitrary_diff(old_content, new_content, "src/cards/development/diff_card.cpp");

    ASSERT_GE(card.get_diff().chunks.size(), 2u);

    // Initial state assertions
    EXPECT_EQ(card.get_current_hunk_idx(), 0u);
    EXPECT_EQ(card.get_view_mode(), rouen::cards::DiffViewMode::SideBySide);
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. next_hunk
    card.handle_action(R"({"type":"Action.Execute","verb":"next_hunk"})");
    EXPECT_EQ(card.get_current_hunk_idx(), 1u);

    // 2. stage_hunk
    card.handle_action(R"({"type":"Action.Execute","verb":"stage_hunk","data":{"hunk_idx":0}})");
    EXPECT_EQ(card.get_diff().chunks[0].status, rouen::helpers::ChunkStatus::Accepted);

    // 3. discard_hunk
    card.handle_action(R"({"type":"Action.Execute","verb":"discard_hunk","data":{"hunk_idx":0}})");
    EXPECT_EQ(card.get_diff().chunks[0].status, rouen::helpers::ChunkStatus::Discarded);

    // 4. toggle_view_mode
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_view_mode"})");
    EXPECT_EQ(card.get_view_mode(), rouen::cards::DiffViewMode::Unified);
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_view_mode"})");
    EXPECT_EQ(card.get_view_mode(), rouen::cards::DiffViewMode::SideBySide);

    // 5. select_file
    card.handle_action(R"({"type":"Action.Execute","verb":"select_file","data":{"file_path":"new_file.cpp"}})");
    EXPECT_EQ(card.get_file_path(), "new_file.cpp");

    // 6. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(diff_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::diff_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"select_file\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"stage_hunk\",\"data\":{\"hunk_idx\":\"invalid\"}}"));
}

TEST(directory_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::directory_card dir{};
    dir.handle_action(R"({"type":"Action.Execute","verb":"add_contact","data":{"name":"Ignacio Rodriguez","email":"ignacio@example.com","phone":"+1 (555) 019-2831","organization":"Rouen Team"}})");

    std::string card_json = dir.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_search_input = false;
    bool found_tag_choice = false;
    bool found_contact_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Text" && child.id == "search_term") {
                    found_search_input = true;
                }
                if (child.type == "Input.ChoiceSet" && child.id == "tag_filter") {
                    found_tag_choice = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("Ignacio Rodriguez") != std::string::npos &&
                    fact.value.find("ignacio@example.com") != std::string::npos) {
                    found_contact_fact = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_search_input);
    EXPECT_TRUE(found_tag_choice);
    EXPECT_TRUE(found_contact_fact);

    bool found_search_act = false;
    bool found_open_act = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "search_contacts") found_search_act = true;
        if (act.verb == "open_contact") found_open_act = true;
    }
    EXPECT_TRUE(found_search_act);
    EXPECT_TRUE(found_open_act);
}

TEST(directory_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::directory_card dir{};

    // Initial state assertions
    EXPECT_FALSE(dir.is_adaptive_view_mode());

    // 1. add_contact
    dir.handle_action(R"({"type":"Action.Execute","verb":"add_contact","data":{"name":"Ada Lovelace","email":"ada@analytics.org","phone":"+1-555-0100","organization":"Engineering Team"}})");
    EXPECT_EQ(dir.get_last_action(), "add_contact");
    EXPECT_GT(dir.get_last_added_contact_id(), 0);
    EXPECT_GT(dir.get_total_count(), 0u);

    // 2. search_contacts
    dir.handle_action(R"({"type":"Action.Execute","verb":"search_contacts","data":{"search_term":"Ada","tag_filter":"Engineering"}})");
    EXPECT_EQ(dir.get_last_action(), "search_contacts");
    EXPECT_EQ(dir.get_search_query(), "Ada");
    EXPECT_EQ(dir.get_selected_tag(), "Engineering");
    EXPECT_FALSE(dir.get_contacts().empty());
    EXPECT_NE(dir.get_contacts().front().get_full_name().find("Ada"), std::string::npos);

    // 3. open_contact
    std::string opened_uri;
    auto dummy_create_card = std::make_shared<std::function<void(const std::string&)>>([&opened_uri](const std::string& uri) {
        opened_uri = uri;
    });
    registrar::add<std::function<void(const std::string&)>>("create_card", dummy_create_card);

    dir.handle_action(R"({"type":"Action.Execute","verb":"open_contact","data":{"contact_id":"42"}})");
    EXPECT_EQ(dir.get_last_action(), "open_contact");
    EXPECT_EQ(dir.get_last_opened_contact_id(), "42");
    EXPECT_EQ(opened_uri, "contact:42");

    // 4. toggle_adaptive_view
    dir.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(dir.is_adaptive_view_mode());
    dir.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(dir.is_adaptive_view_mode());
}

TEST(directory_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::directory_card dir{};
    EXPECT_NO_THROW(dir.handle_action(""));
    EXPECT_NO_THROW(dir.handle_action("not a json string"));
    EXPECT_NO_THROW(dir.handle_action("{"));
    EXPECT_NO_THROW(dir.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(dir.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(dir.handle_action("{\"verb\":\"search_contacts\",\"data\":null}"));
    EXPECT_NO_THROW(dir.handle_action("{\"verb\":\"open_contact\",\"data\":{\"contact_id\":null}}"));
    EXPECT_NO_THROW(dir.handle_action("{\"verb\":\"add_contact\",\"data\":{\"name\":null}}"));
}

TEST(display_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::display_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_target_fps_input = false;
    bool found_vsync_input = false;
    bool found_native_res_fact = false;
    bool found_renderer_fact = false;
    bool found_active_scale_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "target_fps") {
                    found_target_fps_input = true;
                }
                if (child.type == "Input.Toggle" && (child.id == "vsync_toggle" || child.id == "vsync")) {
                    found_vsync_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("Native Resolution:") != std::string::npos) {
                    found_native_res_fact = true;
                }
                if (fact.title.find("Renderer API:") != std::string::npos) {
                    found_renderer_fact = true;
                }
                if (fact.title.find("Active Scale:") != std::string::npos) {
                    found_active_scale_fact = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_target_fps_input);
    EXPECT_TRUE(found_vsync_input);
    EXPECT_TRUE(found_native_res_fact);
    EXPECT_TRUE(found_renderer_fact);
    EXPECT_TRUE(found_active_scale_fact);

    bool found_apply_act = false;
    bool found_reset_act = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "display_apply") found_apply_act = true;
        if (act.verb == "display_reset") found_reset_act = true;
    }
    EXPECT_TRUE(found_apply_act);
    EXPECT_TRUE(found_reset_act);
}

TEST(display_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::display_card card{};

    // Initial state assertions
    EXPECT_EQ(card.get_target_fps(), 120);
    EXPECT_FLOAT_EQ(card.get_ui_scale(), 2.0f);
    EXPECT_TRUE(card.is_vsync_enabled());
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. display_apply: mutate fps, scale, vsync
    card.handle_action(R"({"type":"Action.Execute","verb":"display_apply","data":{"target_fps":"60","ui_scale":1.5,"vsync_toggle":"false"}})");
    EXPECT_EQ(card.get_target_fps(), 60);
    EXPECT_FLOAT_EQ(card.get_ui_scale(), 1.5f);
    EXPECT_FALSE(card.is_vsync_enabled());

    // 2. display_apply with direct numbers and vsync boolean
    card.handle_action(R"({"type":"Action.Execute","verb":"display_apply","data":{"target_fps":30,"ui_scale":1.0,"vsync":true}})");
    EXPECT_EQ(card.get_target_fps(), 30);
    EXPECT_FLOAT_EQ(card.get_ui_scale(), 1.0f);
    EXPECT_TRUE(card.is_vsync_enabled());

    // 3. display_reset: restore defaults (native resolution & 60 FPS cap)
    card.handle_action(R"({"type":"Action.Execute","verb":"display_reset"})");
    EXPECT_EQ(card.get_target_fps(), 60);
    EXPECT_TRUE(card.is_vsync_enabled());

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(display_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::display_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"display_apply\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"display_apply\",\"data\":{\"target_fps\":\"invalid\",\"ui_scale\":\"not_a_num\"}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"display_reset\",\"data\":{}}"));
}

TEST(envvars_cardAdaptiveTest, JsonSerialization) {
    rouen::cards::envvars_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);
    EXPECT_NE(card_json.find("Environment Variables Inspector"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_env_key_input = false;
    bool found_env_val_input = false;
    bool found_fact_set = false;
    size_t fact_count = 0;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Text" && child.id == "env_key") {
                    found_env_key_input = true;
                }
                if (child.type == "Input.Text" && child.id == "env_val") {
                    found_env_val_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            found_fact_set = true;
            fact_count = elem.facts.size();
        }
    }

    EXPECT_TRUE(found_env_key_input);
    EXPECT_TRUE(found_env_val_input);
    EXPECT_TRUE(found_fact_set);
    EXPECT_GT(fact_count, 0u);

    bool found_set_action = false;
    bool found_export_action = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "env_set") found_set_action = true;
        if (act.verb == "env_export") found_export_action = true;
    }
    EXPECT_TRUE(found_set_action);
    EXPECT_TRUE(found_export_action);
}

TEST(envvars_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::envvars_card card{};

    // Initial state assertions
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. env_set: set new environment variable
    card.handle_action(R"({"type":"Action.Execute","verb":"env_set","data":{"env_key":"ROUEN_UNIT_TEST_VAR","env_val":"Alpha42"}})");
    EXPECT_EQ(card.get_last_action(), "env_set");
    EXPECT_EQ(card.get_last_set_key(), "ROUEN_UNIT_TEST_VAR");
    EXPECT_EQ(card.get_last_set_value(), "Alpha42");
    EXPECT_TRUE(card.has_env_var("ROUEN_UNIT_TEST_VAR"));
    EXPECT_EQ(card.get_env_var("ROUEN_UNIT_TEST_VAR"), "Alpha42");

    std::string updated_json = card.get_adaptive_card_json();
    EXPECT_NE(updated_json.find("ROUEN_UNIT_TEST_VAR"), std::string::npos);
    EXPECT_NE(updated_json.find("Alpha42"), std::string::npos);

    // 2. env_set with "key" and "value" field aliases
    card.handle_action(R"({"type":"Action.Execute","verb":"env_set","data":{"key":"ROUEN_UNIT_TEST_VAR2","value":"Beta99"}})");
    EXPECT_EQ(card.get_last_action(), "env_set");
    EXPECT_EQ(card.get_last_set_key(), "ROUEN_UNIT_TEST_VAR2");
    EXPECT_EQ(card.get_last_set_value(), "Beta99");
    EXPECT_TRUE(card.has_env_var("ROUEN_UNIT_TEST_VAR2"));
    EXPECT_EQ(card.get_env_var("ROUEN_UNIT_TEST_VAR2"), "Beta99");

    // 3. env_remove: unset variable
    card.handle_action(R"({"type":"Action.Execute","verb":"env_remove","data":{"env_key":"ROUEN_UNIT_TEST_VAR"}})");
    EXPECT_EQ(card.get_last_action(), "env_remove");
    EXPECT_EQ(card.get_last_removed_key(), "ROUEN_UNIT_TEST_VAR");
    EXPECT_FALSE(card.has_env_var("ROUEN_UNIT_TEST_VAR"));

    // 4. env_export: export snapshot
    card.handle_action(R"({"type":"Action.Execute","verb":"env_export"})");
    EXPECT_EQ(card.get_last_action(), "env_export");

    // Clean up second test variable
    card.handle_action(R"({"type":"Action.Execute","verb":"env_remove","data":{"key":"ROUEN_UNIT_TEST_VAR2"}})");
    EXPECT_FALSE(card.has_env_var("ROUEN_UNIT_TEST_VAR2"));

    // 5. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(envvars_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::envvars_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"env_set\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"env_set\",\"data\":{\"env_key\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"env_remove\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"env_export\",\"data\":{}}"));
}

TEST(gitAdaptiveTest, JsonSerialization) {
    ::git git_card{};
    std::string card_json = git_card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_branch_fact = false;
    bool found_working_tree_fact = false;
    bool found_upstream_sync_fact = false;
    bool found_latest_commit_fact = false;
    bool found_commit_msg_input = false;
    bool found_branch_choice_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Text" && child.id == "commit_message") {
                    found_commit_msg_input = true;
                }
                if (child.type == "Input.ChoiceSet" && child.id == "branch_name") {
                    found_branch_choice_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Branch:") found_branch_fact = true;
                if (fact.title == "Working Tree:") found_working_tree_fact = true;
                if (fact.title == "Upstream Sync:") found_upstream_sync_fact = true;
                if (fact.title == "Latest Commit:") found_latest_commit_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_branch_fact);
    EXPECT_TRUE(found_working_tree_fact);
    EXPECT_TRUE(found_upstream_sync_fact);
    EXPECT_TRUE(found_latest_commit_fact);
    EXPECT_TRUE(found_commit_msg_input);
    EXPECT_TRUE(found_branch_choice_input);

    bool found_act_commit = false;
    bool found_act_pull = false;
    bool found_act_push = false;
    bool found_act_ai_summary = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "git_commit") found_act_commit = true;
        if (act.verb == "git_pull") found_act_pull = true;
        if (act.verb == "git_push") found_act_push = true;
        if (act.verb == "git_ai_summary") found_act_ai_summary = true;
    }
    EXPECT_TRUE(found_act_commit);
    EXPECT_TRUE(found_act_pull);
    EXPECT_TRUE(found_act_push);
    EXPECT_TRUE(found_act_ai_summary);
}

TEST(gitAdaptiveTest, ActionDispatch) {
    ::git git_card{};

    // Initial state
    EXPECT_FALSE(git_card.is_adaptive_view_mode());

    // 1. git_commit
    git_card.handle_action(R"({"type":"Action.Execute","verb":"git_commit","data":{"commit_message":"feat(git): add adaptive card action handling"}})");
    EXPECT_EQ(git_card.get_last_action(), "git_commit");
    EXPECT_EQ(git_card.last_commit_message, "feat(git): add adaptive card action handling");

    // 2. git_pull
    git_card.handle_action(R"({"type":"Action.Execute","verb":"git_pull"})");
    EXPECT_EQ(git_card.get_last_action(), "git_pull");

    // 3. git_push
    git_card.handle_action(R"({"type":"Action.Execute","verb":"git_push"})");
    EXPECT_EQ(git_card.get_last_action(), "git_push");

    // 4. git_checkout
    git_card.handle_action(R"({"type":"Action.Execute","verb":"git_checkout","data":{"branch_name":"feature/adaptive-cards"}})");
    EXPECT_EQ(git_card.get_last_action(), "git_checkout");
    EXPECT_EQ(git_card.get_last_checked_out_branch(), "feature/adaptive-cards");
    EXPECT_EQ(git_card.current_branch, "feature/adaptive-cards");

    // 5. git_stage_file
    git_card.handle_action(R"({"type":"Action.Execute","verb":"git_stage_file","data":{"file_path":"src/cards/development/git.cpp"}})");
    EXPECT_EQ(git_card.get_last_action(), "git_stage_file");
    EXPECT_EQ(git_card.get_last_staged_file(), "src/cards/development/git.cpp");

    // 6. git_ai_summary
    git_card.handle_action(R"({"type":"Action.Execute","verb":"git_ai_summary"})");
    EXPECT_EQ(git_card.get_last_action(), "git_ai_summary");

    // 7. toggle_adaptive_view
    git_card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(git_card.is_adaptive_view_mode());
    git_card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(git_card.is_adaptive_view_mode());
}

TEST(gitAdaptiveTest, MalformedActionHandling) {
    ::git git_card{};
    EXPECT_NO_THROW(git_card.handle_action(""));
    EXPECT_NO_THROW(git_card.handle_action("not a json string"));
    EXPECT_NO_THROW(git_card.handle_action("{"));
    EXPECT_NO_THROW(git_card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(git_card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(git_card.handle_action("{\"verb\":\"git_commit\",\"data\":null}"));
    EXPECT_NO_THROW(git_card.handle_action("{\"verb\":\"git_checkout\",\"data\":{\"branch_name\":null}}"));
    EXPECT_NO_THROW(git_card.handle_action("{\"verb\":\"git_stage_file\",\"data\":{}}"));
}

TEST(github_cardAdaptiveTest, JsonSerialization) {
    rouen::cards::github_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_open_issues_fact = false;
    bool found_open_prs_fact = false;
    bool found_default_branch_fact = false;
    bool found_latest_release_fact = false;
    bool found_issue_title_input = false;
    bool found_issue_body_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Open Issues:") found_open_issues_fact = true;
                if (fact.title == "Open Pull Requests:") found_open_prs_fact = true;
                if (fact.title == "Default Branch:") found_default_branch_fact = true;
                if (fact.title == "Latest Release:") found_latest_release_fact = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Text" && child.id == "issue_title") {
                    found_issue_title_input = true;
                }
                if (child.type == "Input.Text" && child.id == "issue_body") {
                    found_issue_body_input = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_open_issues_fact);
    EXPECT_TRUE(found_open_prs_fact);
    EXPECT_TRUE(found_default_branch_fact);
    EXPECT_TRUE(found_latest_release_fact);
    EXPECT_TRUE(found_issue_title_input);
    EXPECT_TRUE(found_issue_body_input);

    bool found_create_issue_action = false;
    bool found_refresh_repo_action = false;
    bool found_open_url_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "create_issue") found_create_issue_action = true;
        if (act.verb == "refresh_repo") found_refresh_repo_action = true;
        if (act.type == "Action.OpenUrl" && act.url.find("github.com") != std::string::npos) {
            found_open_url_action = true;
        }
    }

    EXPECT_TRUE(found_create_issue_action);
    EXPECT_TRUE(found_refresh_repo_action);
    EXPECT_TRUE(found_open_url_action);
}

TEST(github_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::github_card card{};

    // Initial state
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. create_issue
    size_t const initial_issues = card.get_open_issues().size();
    card.handle_action(R"({"type":"Action.Execute","verb":"create_issue","data":{"issue_title":"Add adaptive cards to github card","issue_body":"Support adaptive card 1.5 JSON rendering"}})");
    EXPECT_EQ(card.get_last_action(), "create_issue");
    EXPECT_EQ(card.get_last_created_issue_title(), "Add adaptive cards to github card");
    EXPECT_EQ(card.get_last_created_issue_body(), "Support adaptive card 1.5 JSON rendering");
    EXPECT_EQ(card.get_open_issues().size(), initial_issues + 1);

    // 2. filter_issues
    card.handle_action(R"({"type":"Action.Execute","verb":"filter_issues","data":{"filter_type":"open"}})");
    EXPECT_EQ(card.get_last_action(), "filter_issues");
    EXPECT_EQ(card.get_filter_type(), "open");

    // 3. refresh_repo
    card.handle_action(R"({"type":"Action.Execute","verb":"refresh_repo"})");
    EXPECT_EQ(card.get_last_action(), "refresh_repo");
    EXPECT_TRUE(card.is_repo_refreshed());

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(github_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::github_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"create_issue\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"create_issue\",\"data\":{\"issue_title\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"filter_issues\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"refresh_repo\",\"data\":null}"));
}

TEST(github_ci_cardAdaptiveTest, JsonSerialization) {
    rouen::cards::github::github_ci_card ci_card{};
    std::string card_json = ci_card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_latest_build_fact = false;
    bool found_commit_sha_fact = false;
    bool found_duration_fact = false;
    bool found_status_fact = false;
    bool found_workflow_choice_input = false;
    bool found_target_branch_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Latest Build:") found_latest_build_fact = true;
                if (fact.title == "Commit SHA:") found_commit_sha_fact = true;
                if (fact.title == "Duration:") found_duration_fact = true;
                if (fact.title == "Status:") found_status_fact = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "workflow_choice") {
                    found_workflow_choice_input = true;
                }
                if (child.type == "Input.Text" && child.id == "target_branch") {
                    found_target_branch_input = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_latest_build_fact);
    EXPECT_TRUE(found_commit_sha_fact);
    EXPECT_TRUE(found_duration_fact);
    EXPECT_TRUE(found_status_fact);
    EXPECT_TRUE(found_workflow_choice_input);
    EXPECT_TRUE(found_target_branch_input);

    bool found_trigger_workflow_action = false;
    bool found_rerun_run_action = false;
    bool found_refresh_ci_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "trigger_workflow") found_trigger_workflow_action = true;
        if (act.verb == "rerun_run") found_rerun_run_action = true;
        if (act.verb == "refresh_ci") found_refresh_ci_action = true;
    }

    EXPECT_TRUE(found_trigger_workflow_action);
    EXPECT_TRUE(found_rerun_run_action);
    EXPECT_TRUE(found_refresh_ci_action);
}

TEST(github_ci_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::github::github_ci_card ci_card{};

    // Initial state
    EXPECT_FALSE(ci_card.is_adaptive_view_mode());

    // 1. trigger_workflow
    ci_card.handle_action(R"({"type":"Action.Execute","verb":"trigger_workflow","data":{"workflow_choice":"release.yml","target_branch":"develop"}})");
    EXPECT_EQ(ci_card.get_last_action(), "trigger_workflow");
    EXPECT_EQ(ci_card.get_last_triggered_workflow(), "release.yml");
    EXPECT_EQ(ci_card.get_last_target_branch(), "develop");

    // 2. rerun_run
    ci_card.handle_action(R"({"type":"Action.Execute","verb":"rerun_run","data":{"run_id":"142"}})");
    EXPECT_EQ(ci_card.get_last_action(), "rerun_run");
    EXPECT_EQ(ci_card.get_last_rerun_run_id(), "142");

    // 3. cancel_run
    ci_card.handle_action(R"({"type":"Action.Execute","verb":"cancel_run","data":{"run_id":"142"}})");
    EXPECT_EQ(ci_card.get_last_action(), "cancel_run");
    EXPECT_EQ(ci_card.get_last_cancelled_run_id(), "142");

    // 4. refresh_ci
    ci_card.handle_action(R"({"type":"Action.Execute","verb":"refresh_ci"})");
    EXPECT_EQ(ci_card.get_last_action(), "refresh_ci");
    EXPECT_TRUE(ci_card.is_ci_refreshed());

    // 5. toggle_adaptive_view
    ci_card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(ci_card.is_adaptive_view_mode());
    ci_card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(ci_card.is_adaptive_view_mode());
}

TEST(github_ci_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::github::github_ci_card ci_card{};
    EXPECT_NO_THROW(ci_card.handle_action(""));
    EXPECT_NO_THROW(ci_card.handle_action("not a json string"));
    EXPECT_NO_THROW(ci_card.handle_action("{"));
    EXPECT_NO_THROW(ci_card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(ci_card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(ci_card.handle_action("{\"verb\":\"trigger_workflow\",\"data\":null}"));
    EXPECT_NO_THROW(ci_card.handle_action("{\"verb\":\"trigger_workflow\",\"data\":{\"workflow_choice\":null}}"));
    EXPECT_NO_THROW(ci_card.handle_action("{\"verb\":\"rerun_run\",\"data\":null}"));
    EXPECT_NO_THROW(ci_card.handle_action("{\"verb\":\"cancel_run\",\"data\":null}"));
    EXPECT_NO_THROW(ci_card.handle_action("{\"verb\":\"refresh_ci\",\"data\":null}"));
}

TEST(image_viewerAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::image_viewer viewer{};
    std::string card_json = viewer.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_image_element = false;
    bool found_resolution_fact = false;
    bool found_aspect_ratio_fact = false;
    bool found_file_size_fact = false;
    bool found_color_depth_fact = false;
    bool found_zoom_factor_input = false;
    bool found_angle_deg_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Image") {
            found_image_element = true;
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Resolution:") found_resolution_fact = true;
                if (fact.title == "Aspect Ratio:") found_aspect_ratio_fact = true;
                if (fact.title == "File Size:") found_file_size_fact = true;
                if (fact.title == "Color Depth:") found_color_depth_fact = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Number" && child.id == "zoom_factor") {
                    found_zoom_factor_input = true;
                }
                if (child.type == "Input.ChoiceSet" && child.id == "angle_deg") {
                    found_angle_deg_input = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_image_element);
    EXPECT_TRUE(found_resolution_fact);
    EXPECT_TRUE(found_aspect_ratio_fact);
    EXPECT_TRUE(found_file_size_fact);
    EXPECT_TRUE(found_color_depth_fact);
    EXPECT_TRUE(found_zoom_factor_input);
    EXPECT_TRUE(found_angle_deg_input);

    bool found_zoom_action = false;
    bool found_fit_action = false;
    bool found_rotate_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "image_zoom") found_zoom_action = true;
        if (act.verb == "image_fit") found_fit_action = true;
        if (act.verb == "image_rotate") found_rotate_action = true;
    }

    EXPECT_TRUE(found_zoom_action);
    EXPECT_TRUE(found_fit_action);
    EXPECT_TRUE(found_rotate_action);
}

TEST(image_viewerAdaptiveTest, ActionDispatch) {
    rouen::cards::image_viewer viewer{};

    // Initial state
    EXPECT_FALSE(viewer.is_adaptive_view_mode());
    EXPECT_EQ(viewer.get_rotation_deg(), 0);
    EXPECT_TRUE(viewer.is_fit_to_window());

    // 1. image_zoom
    viewer.handle_action(R"({"type":"Action.Execute","verb":"image_zoom","data":{"zoom_factor":1.5}})");
    EXPECT_EQ(viewer.get_last_action(), "image_zoom");
    EXPECT_FLOAT_EQ(viewer.get_zoom_factor(), 1.5f);
    EXPECT_FALSE(viewer.is_fit_to_window());

    // 2. image_fit
    viewer.handle_action(R"({"type":"Action.Execute","verb":"image_fit"})");
    EXPECT_EQ(viewer.get_last_action(), "image_fit");
    EXPECT_TRUE(viewer.is_fit_to_window());

    // 3. image_rotate (button click without data increments by 90)
    viewer.handle_action(R"({"type":"Action.Execute","verb":"image_rotate"})");
    EXPECT_EQ(viewer.get_last_action(), "image_rotate");
    EXPECT_EQ(viewer.get_rotation_deg(), 90);

    // 4. image_rotate (choice data rotates to specified angle)
    viewer.handle_action(R"({"type":"Action.Execute","verb":"image_rotate","data":{"angle_deg":"180"}})");
    EXPECT_EQ(viewer.get_last_action(), "image_rotate");
    EXPECT_EQ(viewer.get_rotation_deg(), 180);

    // 5. toggle_adaptive_view
    viewer.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(viewer.is_adaptive_view_mode());
    viewer.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(viewer.is_adaptive_view_mode());
}

TEST(image_viewerAdaptiveTest, MalformedActionHandling) {
    rouen::cards::image_viewer viewer{};
    EXPECT_NO_THROW(viewer.handle_action(""));
    EXPECT_NO_THROW(viewer.handle_action("not a json string"));
    EXPECT_NO_THROW(viewer.handle_action("{"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"image_zoom\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"image_zoom\",\"data\":{\"zoom_factor\":null}}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"image_rotate\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"image_rotate\",\"data\":{\"angle_deg\":null}}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"image_fit\",\"data\":null}"));
}

TEST(kpi_cardAdaptiveTest, JsonSerialization) {
    rouen::cards::kpi_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_mrr_fact = false;
    bool found_uptime_fact = false;
    bool found_build_time_fact = false;
    bool found_backlog_fact = false;
    bool found_kpi_id_choice = false;
    bool found_new_value_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Monthly Recurring Revenue:") found_mrr_fact = true;
                if (fact.title == "Rouen Mesh Uptime:") found_uptime_fact = true;
                if (fact.title == "C++ Module Compilation Time:") found_build_time_fact = true;
                if (fact.title == "Open Issues Backlog:") found_backlog_fact = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "kpi_id") {
                    found_kpi_id_choice = true;
                }
                if (child.type == "Input.Number" && child.id == "new_value") {
                    found_new_value_input = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_mrr_fact);
    EXPECT_TRUE(found_uptime_fact);
    EXPECT_TRUE(found_build_time_fact);
    EXPECT_TRUE(found_backlog_fact);
    EXPECT_TRUE(found_kpi_id_choice);
    EXPECT_TRUE(found_new_value_input);

    bool found_update_action = false;
    bool found_refresh_action = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "update_kpi") found_update_action = true;
        if (act.verb == "refresh_kpis") found_refresh_action = true;
    }
    EXPECT_TRUE(found_update_action);
    EXPECT_TRUE(found_refresh_action);
}

TEST(kpi_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::kpi_card card{};

    // Initial state
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. update_kpi
    card.handle_action(R"({"type":"Action.Execute","verb":"update_kpi","data":{"kpi_id":"mrr","new_value":15000}})");
    EXPECT_EQ(card.get_last_action(), "update_kpi");
    EXPECT_EQ(card.get_selected_kpi_id(), "mrr");
    EXPECT_DOUBLE_EQ(card.get_kpi_value("mrr"), 15000.0);

    // 2. add_kpi
    card.handle_action(R"({"type":"Action.Execute","verb":"add_kpi","data":{"name":"User Retention","target_val":85.0,"unit":"%","current_val":80.0}})");
    EXPECT_EQ(card.get_last_action(), "add_kpi");
    EXPECT_EQ(card.get_selected_kpi_id(), "user_retention");
    EXPECT_DOUBLE_EQ(card.get_kpi_value("user_retention"), 80.0);
    EXPECT_EQ(card.get_kpi_metrics().size(), 5u);

    // 3. refresh_kpis
    card.handle_action(R"({"type":"Action.Execute","verb":"refresh_kpis"})");
    EXPECT_EQ(card.get_last_action(), "refresh_kpis");
    EXPECT_TRUE(card.is_kpis_refreshed());

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(kpi_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::kpi_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"update_kpi\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"update_kpi\",\"data\":{\"kpi_id\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"add_kpi\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"refresh_kpis\",\"data\":null}"));
}

TEST(mailAdaptiveTest, JsonSerialization) {
    rouen::cards::mail card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_folder_choice = false;
    bool found_reply_to_input = false;
    bool found_reply_subject_input = false;
    bool found_reply_body_input = false;
    bool found_email_facts = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            if (!elem.facts.empty()) {
                found_email_facts = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "mail_folder") {
                    found_folder_choice = true;
                }
                if (child.type == "Input.Text" && child.id == "reply_to") {
                    found_reply_to_input = true;
                }
                if (child.type == "Input.Text" && child.id == "reply_subject") {
                    found_reply_subject_input = true;
                }
                if (child.type == "Input.Text" && child.id == "reply_body") {
                    found_reply_body_input = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_folder_choice);
    EXPECT_TRUE(found_reply_to_input);
    EXPECT_TRUE(found_reply_subject_input);
    EXPECT_TRUE(found_reply_body_input);
    EXPECT_TRUE(found_email_facts);

    bool found_send_reply_action = false;
    bool found_fetch_action = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "mail_send_reply") found_send_reply_action = true;
        if (act.verb == "mail_fetch") found_fetch_action = true;
    }
    EXPECT_TRUE(found_send_reply_action);
    EXPECT_TRUE(found_fetch_action);
}

TEST(mailAdaptiveTest, ActionDispatch) {
    rouen::cards::mail card{};

    // Initial state
    EXPECT_FALSE(card.is_adaptive_view_mode());
    EXPECT_EQ(card.get_current_folder(), "INBOX");
    EXPECT_EQ(card.get_unread_count(), 3u);
    EXPECT_EQ(card.get_messages().size(), 3u);

    // 1. mail_send_reply
    card.handle_action(R"({"type":"Action.Execute","verb":"mail_send_reply","data":{"recipient":"linus@kernel.org","subject":"Re: Git 2.47","body":"Patch reviewed and approved."}})");
    EXPECT_EQ(card.get_last_action(), "mail_send_reply");
    EXPECT_EQ(card.get_last_reply_to(), "linus@kernel.org");
    EXPECT_EQ(card.get_last_reply_subject(), "Re: Git 2.47");
    EXPECT_EQ(card.get_last_reply_body(), "Patch reviewed and approved.");

    // 2. mail_fetch
    card.handle_action(R"({"type":"Action.Execute","verb":"mail_fetch","data":{"folder":"Archive"}})");
    EXPECT_EQ(card.get_last_action(), "mail_fetch");
    EXPECT_EQ(card.get_current_folder(), "Archive");
    EXPECT_TRUE(card.is_messages_refreshed());

    // 3. mail_mark_read
    card.handle_action(R"({"type":"Action.Execute","verb":"mail_mark_read","data":{"message_id":"msg-101"}})");
    EXPECT_EQ(card.get_last_action(), "mail_mark_read");
    EXPECT_EQ(card.get_last_marked_message_id(), "msg-101");
    EXPECT_EQ(card.get_unread_count(), 2u);

    // 4. mail_delete
    card.handle_action(R"({"type":"Action.Execute","verb":"mail_delete","data":{"message_id":"msg-102"}})");
    EXPECT_EQ(card.get_last_action(), "mail_delete");
    EXPECT_EQ(card.get_last_deleted_message_id(), "msg-102");
    EXPECT_EQ(card.get_messages().size(), 2u);
    EXPECT_EQ(card.get_unread_count(), 1u);

    // 5. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(mailAdaptiveTest, MalformedActionHandling) {
    rouen::cards::mail card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_fetch\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_fetch\",\"data\":{\"folder\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_send_reply\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_send_reply\",\"data\":{\"recipient\":null,\"subject\":null,\"body\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_mark_read\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_mark_read\",\"data\":{\"message_id\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_delete\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mail_delete\",\"data\":{\"message_id\":null}}"));
}

TEST(markdown_notesAdaptiveTest, JsonSerialization) {
    rouen::cards::markdown_notes notes{};
    std::string card_json = notes.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_note_id_fact = false;
    bool found_status_fact = false;
    bool found_title_input = false;
    bool found_tags_input = false;
    bool found_content_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Text" && child.id == "note_title") {
                    found_title_input = true;
                }
                if (child.type == "Input.Text" && child.id == "note_tags") {
                    found_tags_input = true;
                }
                if (child.type == "Input.Text" && child.id == "note_content") {
                    found_content_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Note ID:") found_note_id_fact = true;
                if (fact.title == "Status:") found_status_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_note_id_fact);
    EXPECT_TRUE(found_status_fact);
    EXPECT_TRUE(found_title_input);
    EXPECT_TRUE(found_tags_input);
    EXPECT_TRUE(found_content_input);

    bool found_save_action = false;
    bool found_new_action = false;
    bool found_delete_action = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "save_note") found_save_action = true;
        if (act.verb == "new_note") found_new_action = true;
        if (act.verb == "delete_note") found_delete_action = true;
    }
    EXPECT_TRUE(found_save_action);
    EXPECT_TRUE(found_new_action);
    EXPECT_TRUE(found_delete_action);
}

TEST(markdown_notesAdaptiveTest, ActionDispatch) {
    const auto temp_db = (std::filesystem::temp_directory_path() / "rouen_test_notes_action_dispatch.db").string();
    std::error_code ec;
    std::filesystem::remove(temp_db, ec);

    rouen::cards::markdown_notes notes{"", temp_db};

    // Initial state
    EXPECT_FALSE(notes.is_adaptive_view_mode());

    // 1. save_note
    notes.handle_action(R"({"type":"Action.Execute","verb":"save_note","data":{"title":"Test Note Alpha","tags":"unit-test, c++20","content":"# Test Note\nThis is content."}})");
    EXPECT_EQ(notes.get_last_action(), "save_note");
    EXPECT_GT(notes.get_current_note_id(), 0);
    EXPECT_EQ(notes.get_current_note_title(), "Test Note Alpha");
    EXPECT_EQ(notes.get_current_note_content(), "# Test Note\nThis is content.");
    EXPECT_EQ(notes.get_notes_count(), 1u);
    const int created_id = notes.get_current_note_id();

    // 2. search_notes
    notes.handle_action(R"({"type":"Action.Execute","verb":"search_notes","data":{"query":"Alpha"}})");
    EXPECT_EQ(notes.get_last_action(), "search_notes");
    EXPECT_EQ(notes.get_search_query(), "Alpha");
    EXPECT_EQ(notes.get_notes_count(), 1u);

    // 3. new_note
    notes.handle_action(R"({"type":"Action.Execute","verb":"new_note"})");
    EXPECT_EQ(notes.get_last_action(), "new_note");
    EXPECT_EQ(notes.get_current_note_id(), 0);
    EXPECT_TRUE(notes.get_current_note_title().empty());
    EXPECT_TRUE(notes.get_current_note_content().empty());

    // 4. delete_note
    notes.handle_action(std::format(R"({{"type":"Action.Execute","verb":"delete_note","data":{{"note_id":"{}"}}}})", created_id));
    EXPECT_EQ(notes.get_last_action(), "delete_note");
    EXPECT_EQ(notes.get_notes_count(), 0u);

    // 5. toggle_adaptive_view
    notes.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(notes.is_adaptive_view_mode());
    notes.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(notes.is_adaptive_view_mode());

    std::filesystem::remove(temp_db, ec);
}

TEST(markdown_notesAdaptiveTest, MalformedActionHandling) {
    rouen::cards::markdown_notes notes{};
    EXPECT_NO_THROW(notes.handle_action(""));
    EXPECT_NO_THROW(notes.handle_action("not a json string"));
    EXPECT_NO_THROW(notes.handle_action("{"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\":\"save_note\",\"data\":null}"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\":\"save_note\",\"data\":{\"title\":null,\"content\":null,\"tags\":null}}"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\":\"search_notes\",\"data\":null}"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\":\"search_notes\",\"data\":{\"query\":null}}"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\":\"delete_note\",\"data\":null}"));
    EXPECT_NO_THROW(notes.handle_action("{\"verb\":\"delete_note\",\"data\":{\"note_id\":null}}"));
}

TEST(markdown_viewerAdaptiveTest, JsonSerialization) {
    const auto temp_file = std::filesystem::temp_directory_path() / "rouen_test_markdown_viewer.md";
    {
        std::ofstream ofs(temp_file);
        ofs << "# README.md\n\nRouen is a next-generation desktop workspace.\n\n## Overview\nSection overview.\n\n### Details\nDeep details.\n";
    }

    rouen::cards::markdown_viewer viewer{temp_file.string()};
    std::string card_json = viewer.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_last_modified_fact = false;
    bool found_format_fact = false;
    bool found_headings_fact = false;
    bool found_heading_input = false;
    bool found_doc_path_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Last Modified:") found_last_modified_fact = true;
                if (fact.title == "Format:") found_format_fact = true;
                if (fact.title == "Headings:") found_headings_fact = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "heading") {
                    found_heading_input = true;
                }
                if (child.type == "Input.Text" && child.id == "doc_path") {
                    found_doc_path_input = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_last_modified_fact);
    EXPECT_TRUE(found_format_fact);
    EXPECT_TRUE(found_headings_fact);
    EXPECT_TRUE(found_heading_input);
    EXPECT_TRUE(found_doc_path_input);

    bool found_reload_action = false;
    bool found_copy_action = false;
    bool found_navigate_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "reload_doc") found_reload_action = true;
        if (act.verb == "copy_markdown") found_copy_action = true;
        if (act.verb == "navigate_section") found_navigate_action = true;
    }

    EXPECT_TRUE(found_reload_action);
    EXPECT_TRUE(found_copy_action);
    EXPECT_TRUE(found_navigate_action);

    std::error_code ec;
    std::filesystem::remove(temp_file, ec);
}

TEST(markdown_viewerAdaptiveTest, ActionDispatch) {
    const auto temp_file = std::filesystem::temp_directory_path() / "rouen_test_markdown_viewer_dispatch.md";
    {
        std::ofstream ofs(temp_file);
        ofs << "# Sample Document\n\nContent here.\n\n## Getting Started\nIntro.\n";
    }

    rouen::cards::markdown_viewer viewer{temp_file.string()};

    // Initial state
    EXPECT_FALSE(viewer.is_adaptive_view_mode());

    // 1. reload_doc
    viewer.handle_action(R"({"type":"Action.Execute","verb":"reload_doc"})");
    EXPECT_EQ(viewer.get_last_action(), "reload_doc");

    // 2. copy_markdown
    viewer.handle_action(R"({"type":"Action.Execute","verb":"copy_markdown"})");
    EXPECT_EQ(viewer.get_last_action(), "copy_markdown");

    // 3. navigate_section
    viewer.handle_action(R"({"type":"Action.Execute","verb":"navigate_section","data":{"heading":"Getting Started"}})");
    EXPECT_EQ(viewer.get_last_action(), "navigate_section");
    EXPECT_EQ(viewer.get_selected_heading(), "Getting Started");

    // 4. toggle_adaptive_view
    viewer.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(viewer.is_adaptive_view_mode());
    viewer.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(viewer.is_adaptive_view_mode());

    std::error_code ec;
    std::filesystem::remove(temp_file, ec);
}

TEST(markdown_viewerAdaptiveTest, MalformedActionHandling) {
    rouen::cards::markdown_viewer viewer{};
    EXPECT_NO_THROW(viewer.handle_action(""));
    EXPECT_NO_THROW(viewer.handle_action("not a json string"));
    EXPECT_NO_THROW(viewer.handle_action("{"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"reload_doc\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"copy_markdown\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"navigate_section\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"navigate_section\",\"data\":{\"heading\":null}}"));
}

TEST(media_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::media_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_stream_source = false;
    bool found_playback_time = false;
    bool found_volume_level = false;
    bool found_volume_slider = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Stream Source:") found_stream_source = true;
                if (fact.title == "Playback Time:") found_playback_time = true;
                if (fact.title == "Volume Level:") found_volume_level = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Number" && child.id == "volume_slider") {
                    found_volume_slider = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_stream_source);
    EXPECT_TRUE(found_playback_time);
    EXPECT_TRUE(found_volume_level);
    EXPECT_TRUE(found_volume_slider);

    bool found_play_action = false;
    bool found_pause_action = false;
    bool found_stop_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "media_play") found_play_action = true;
        if (act.verb == "media_pause") found_pause_action = true;
        if (act.verb == "media_stop") found_stop_action = true;
    }

    EXPECT_TRUE(found_play_action);
    EXPECT_TRUE(found_pause_action);
    EXPECT_TRUE(found_stop_action);
}

TEST(media_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::media_card card{};

    // Initial state assertions
    EXPECT_FALSE(card.is_playing());
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. media_play
    card.handle_action(R"({"type":"Action.Execute","verb":"media_play"})");
    EXPECT_TRUE(card.is_playing());
    EXPECT_EQ(card.get_last_action(), "media_play");
    std::string play_json = card.get_adaptive_card_json();
    EXPECT_NE(play_json.find("PLAYING"), std::string::npos);

    // 2. media_pause
    card.handle_action(R"({"type":"Action.Execute","verb":"media_pause"})");
    EXPECT_FALSE(card.is_playing());
    EXPECT_TRUE(card.is_paused());
    EXPECT_EQ(card.get_last_action(), "media_pause");
    std::string pause_json = card.get_adaptive_card_json();
    EXPECT_NE(pause_json.find("PAUSED"), std::string::npos);

    // 3. media_stop
    card.handle_action(R"({"type":"Action.Execute","verb":"media_stop"})");
    EXPECT_FALSE(card.is_playing());
    EXPECT_FALSE(card.is_paused());
    EXPECT_EQ(card.get_current_position_ms(), 0);
    EXPECT_EQ(card.get_last_action(), "media_stop");
    std::string stop_json = card.get_adaptive_card_json();
    EXPECT_NE(stop_json.find("STOPPED"), std::string::npos);

    // 4. media_set_volume
    card.handle_action(R"({"type":"Action.Execute","verb":"media_set_volume","data":{"volume":65}})");
    EXPECT_EQ(card.get_volume(), 65);
    EXPECT_EQ(card.get_last_action(), "media_set_volume");

    card.handle_action(R"({"type":"Action.Execute","verb":"media_set_volume","data":{"volume_slider":90}})");
    EXPECT_EQ(card.get_volume(), 90);

    // 5. media_seek
    card.handle_action(R"({"type":"Action.Execute","verb":"media_seek","data":{"seek_seconds":150}})");
    EXPECT_EQ(card.get_current_position_ms(), 150000);
    EXPECT_EQ(card.get_last_action(), "media_seek");

    // 6. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(media_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::media_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"media_play\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"media_set_volume\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"media_set_volume\",\"data\":{\"volume\":\"invalid\"}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"media_seek\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"media_seek\",\"data\":{\"seek_seconds\":\"invalid\"}}"));
}

TEST(media_companionAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::media_companion companion{};
    std::string card_json = companion.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_next_queue = false;
    bool found_track_progress = false;
    bool found_playlist = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Next in Queue:") found_next_queue = true;
                if (fact.title == "Track Progress:") found_track_progress = true;
                if (fact.title == "Playlist:") found_playlist = true;
            }
        }
    }

    EXPECT_TRUE(found_next_queue);
    EXPECT_TRUE(found_track_progress);
    EXPECT_TRUE(found_playlist);

    bool found_prev_action = false;
    bool found_toggle_play_action = false;
    bool found_next_action = false;
    bool found_toggle_shuffle_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "comp_prev") found_prev_action = true;
        if (act.verb == "comp_toggle_play") found_toggle_play_action = true;
        if (act.verb == "comp_next") found_next_action = true;
        if (act.verb == "comp_toggle_shuffle") found_toggle_shuffle_action = true;
    }

    EXPECT_TRUE(found_prev_action);
    EXPECT_TRUE(found_toggle_play_action);
    EXPECT_TRUE(found_next_action);
    EXPECT_TRUE(found_toggle_shuffle_action);
}

TEST(media_companionAdaptiveTest, ActionDispatch) {
    rouen::cards::media_companion companion{};

    // Initial state assertions
    EXPECT_FALSE(companion.is_playing());
    EXPECT_FALSE(companion.is_shuffle_enabled());
    EXPECT_FALSE(companion.is_adaptive_view_mode());
    EXPECT_EQ(companion.get_track_title(), "Clair de Lune");
    EXPECT_EQ(companion.get_artist_name(), "Claude Debussy");

    // 1. comp_toggle_play
    companion.handle_action(R"({"type":"Action.Execute","verb":"comp_toggle_play"})");
    EXPECT_TRUE(companion.is_playing());
    EXPECT_EQ(companion.get_last_action(), "comp_toggle_play");

    companion.handle_action(R"({"type":"Action.Execute","verb":"comp_toggle_play"})");
    EXPECT_FALSE(companion.is_playing());
    EXPECT_EQ(companion.get_last_action(), "comp_toggle_play");

    // 2. comp_toggle_shuffle
    companion.handle_action(R"({"type":"Action.Execute","verb":"comp_toggle_shuffle"})");
    EXPECT_TRUE(companion.is_shuffle_enabled());
    EXPECT_EQ(companion.get_last_action(), "comp_toggle_shuffle");

    companion.handle_action(R"({"type":"Action.Execute","verb":"comp_toggle_shuffle"})");
    EXPECT_FALSE(companion.is_shuffle_enabled());
    EXPECT_EQ(companion.get_last_action(), "comp_toggle_shuffle");

    // 3. comp_next
    companion.handle_action(R"({"type":"Action.Execute","verb":"comp_next"})");
    EXPECT_EQ(companion.get_last_action(), "comp_next");
    EXPECT_EQ(companion.get_track_title(), "Gymnopédie No. 1");

    // 4. comp_prev
    companion.handle_action(R"({"type":"Action.Execute","verb":"comp_prev"})");
    EXPECT_EQ(companion.get_last_action(), "comp_prev");
    EXPECT_EQ(companion.get_track_title(), "Clair de Lune");

    // 5. toggle_adaptive_view
    companion.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(companion.is_adaptive_view_mode());
    companion.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(companion.is_adaptive_view_mode());
}

TEST(media_companionAdaptiveTest, MalformedActionHandling) {
    rouen::cards::media_companion companion{};
    EXPECT_NO_THROW(companion.handle_action(""));
    EXPECT_NO_THROW(companion.handle_action("not a json string"));
    EXPECT_NO_THROW(companion.handle_action("{"));
    EXPECT_NO_THROW(companion.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(companion.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(companion.handle_action("{\"verb\":\"comp_next\",\"data\":null}"));
    EXPECT_NO_THROW(companion.handle_action("{\"verb\":\"comp_prev\",\"data\":null}"));
    EXPECT_NO_THROW(companion.handle_action("{\"verb\":\"comp_toggle_play\",\"data\":null}"));
    EXPECT_NO_THROW(companion.handle_action("{\"verb\":\"comp_toggle_shuffle\",\"data\":null}"));
}

TEST(mesh_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::mesh_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_relay_server = false;
    bool found_latency = false;
    bool found_peer_ws = false;
    bool found_peer_linux = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Relay Server:") found_relay_server = true;
                if (fact.title == "Roundtrip Latency:") found_latency = true;
                if (fact.title == "Peer ws-ir-01:") found_peer_ws = true;
                if (fact.title == "Peer linux-dev-02:") found_peer_linux = true;
            }
        }
    }

    EXPECT_TRUE(found_relay_server);
    EXPECT_TRUE(found_latency);
    EXPECT_TRUE(found_peer_ws);
    EXPECT_TRUE(found_peer_linux);

    bool found_reconnect_action = false;
    bool found_invite_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "mesh_reconnect") found_reconnect_action = true;
        if (act.verb == "mesh_invite") found_invite_action = true;
    }

    EXPECT_TRUE(found_reconnect_action);
    EXPECT_TRUE(found_invite_action);
}

TEST(mesh_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::mesh_card card{};

    // Initial state assertions
    EXPECT_TRUE(card.is_relay_connected());
    EXPECT_FALSE(card.is_adaptive_view_mode());
    EXPECT_EQ(card.get_local_client_id(), "macbook-pro-ir");
    EXPECT_EQ(card.get_relay_url(), "wss://mesh.rouen.live/relay");
    EXPECT_EQ(card.get_relay_latency_ms(), 24);

    // 1. mesh_reconnect
    card.handle_action(R"({"type":"Action.Execute","verb":"mesh_reconnect"})");
    EXPECT_EQ(card.get_last_action(), "mesh_reconnect");
    EXPECT_TRUE(card.is_relay_connected());

    // 2. mesh_ping_peer
    card.handle_action(R"({"type":"Action.Execute","verb":"mesh_ping_peer","data":{"peer_id":"ws-ir-01"}})");
    EXPECT_EQ(card.get_last_action(), "mesh_ping_peer");
    EXPECT_EQ(card.get_last_pinged_peer(), "ws-ir-01");

    // 3. mesh_invite
    card.handle_action(R"({"type":"Action.Execute","verb":"mesh_invite"})");
    EXPECT_EQ(card.get_last_action(), "mesh_invite");
    EXPECT_FALSE(card.get_pairing_invite().empty());

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(mesh_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::mesh_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mesh_reconnect\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mesh_ping_peer\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"mesh_invite\",\"data\":null}"));
}

TEST(notifications_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::notifications_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header_title = false;
    bool found_unread_badge = false;
    bool found_build_fact = false;
    bool found_memory_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& col_set : elem.items) {
                if (col_set.type == "ColumnSet") {
                    for (const auto& col : col_set.columns) {
                        for (const auto& item : col.items) {
                            if (item.type == "TextBlock") {
                                if (item.text.find("System Notifications History") != std::string::npos) {
                                    found_header_title = true;
                                }
                                if (item.text.find("NEW") != std::string::npos) {
                                    found_unread_badge = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("Build") != std::string::npos) found_build_fact = true;
                if (fact.title.find("Memory") != std::string::npos) found_memory_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header_title);
    EXPECT_TRUE(found_unread_badge);
    EXPECT_TRUE(found_build_fact);
    EXPECT_TRUE(found_memory_fact);

    bool found_clear_action = false;
    bool found_test_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "notif_clear_all") found_clear_action = true;
        if (act.verb == "notif_test") found_test_action = true;
    }

    EXPECT_TRUE(found_clear_action);
    EXPECT_TRUE(found_test_action);
}

TEST(notifications_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::notifications_card card{};

    // Initial state assertions
    EXPECT_FALSE(card.is_adaptive_view_mode());
    EXPECT_EQ(card.get_unread_count(), 2u);
    EXPECT_EQ(card.get_notifications().size(), 2u);

    // 1. notif_test
    card.handle_action(R"({"type":"Action.Execute","verb":"notif_test","data":{"severity":"ERROR","message":"Disk space running critically low","title":"Disk"}})");
    EXPECT_EQ(card.get_last_action(), "notif_test");
    EXPECT_EQ(card.get_last_test_severity(), "ERROR");
    EXPECT_EQ(card.get_last_test_message(), "Disk space running critically low");
    EXPECT_EQ(card.get_unread_count(), 3u);
    EXPECT_EQ(card.get_notifications().size(), 3u);

    // 2. notif_dismiss
    card.handle_action(R"({"type":"Action.Execute","verb":"notif_dismiss","data":{"id":"notif-1"}})");
    EXPECT_EQ(card.get_last_action(), "notif_dismiss");
    EXPECT_EQ(card.get_last_dismissed_id(), "notif-1");
    EXPECT_EQ(card.get_unread_count(), 2u);
    EXPECT_EQ(card.get_notifications().size(), 2u);

    // 3. notif_clear_all
    card.handle_action(R"({"type":"Action.Execute","verb":"notif_clear_all"})");
    EXPECT_EQ(card.get_last_action(), "notif_clear_all");
    EXPECT_TRUE(card.get_notifications().empty());
    EXPECT_EQ(card.get_unread_count(), 0u);

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(notifications_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::notifications_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"notif_test\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"notif_dismiss\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"notif_clear_all\",\"data\":null}"));
}

TEST(number_series_cardAdaptiveTest, JsonSerialization) {
    rouen::cards::number_series_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_current_reading = false;
    bool found_min_value = false;
    bool found_peak_value = false;
    bool found_sample_count = false;
    bool found_dataset_selector = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Current Reading:") found_current_reading = true;
                if (fact.title == "Minimum Value:") found_min_value = true;
                if (fact.title == "Peak Value:") found_peak_value = true;
                if (fact.title == "Sample Count:") found_sample_count = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.ChoiceSet" && child.id == "dataset_selector") {
                    found_dataset_selector = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_current_reading);
    EXPECT_TRUE(found_min_value);
    EXPECT_TRUE(found_peak_value);
    EXPECT_TRUE(found_sample_count);
    EXPECT_TRUE(found_dataset_selector);

    bool found_select_series = false;
    bool found_export_csv = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "select_series") found_select_series = true;
        if (act.verb == "export_csv") found_export_csv = true;
    }
    EXPECT_TRUE(found_select_series);
    EXPECT_TRUE(found_export_csv);
}

TEST(number_series_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::number_series_card card{};

    // Initial state assertions
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. select_series
    card.handle_action(R"({"type":"Action.Execute","verb":"select_series","data":{"dataset":"temps"}})");
    EXPECT_EQ(card.get_last_action(), "select_series");
    EXPECT_EQ(card.get_selected_series_name(), "temps");

    // Also test alias field name dataset_selector
    card.handle_action(R"({"type":"Action.Execute","verb":"select_series","data":{"dataset_selector":"cpu"}})");
    EXPECT_EQ(card.get_last_action(), "select_series");
    EXPECT_EQ(card.get_selected_series_name(), "cpu");

    // 2. filter_range
    card.handle_action(R"({"type":"Action.Execute","verb":"filter_range","data":{"range":"24h"}})");
    EXPECT_EQ(card.get_last_action(), "filter_range");
    EXPECT_EQ(card.get_selected_range(), "24h");

    card.handle_action(R"({"type":"Action.Execute","verb":"filter_range","data":{"range":"All"}})");
    EXPECT_EQ(card.get_last_action(), "filter_range");
    EXPECT_EQ(card.get_selected_range(), "All");

    // 3. export_csv
    card.handle_action(R"({"type":"Action.Execute","verb":"export_csv"})");
    EXPECT_EQ(card.get_last_action(), "export_csv");
    EXPECT_FALSE(card.get_last_exported_csv().empty());
    EXPECT_TRUE(std::filesystem::exists(card.get_last_exported_csv()));

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(number_series_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::number_series_card card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"select_series\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"select_series\",\"data\":{\"dataset\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"filter_range\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"filter_range\",\"data\":{\"range\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"export_csv\",\"data\":null}"));
}

TEST(pdf_viewerAdaptiveTest, JsonSerialization) {
    rouen::cards::pdf_viewer viewer{};
    std::string card_json = viewer.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_title_fact = false;
    bool found_author_fact = false;
    bool found_current_page_fact = false;
    bool found_zoom_level_fact = false;
    bool found_target_page_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Title:") found_title_fact = true;
                if (fact.title == "Author:") found_author_fact = true;
                if (fact.title == "Current Page:") found_current_page_fact = true;
                if (fact.title == "Zoom Level:") found_zoom_level_fact = true;
            }
        }
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "Input.Number" && child.id == "target_page") {
                    found_target_page_input = true;
                }
            }
        }
    }

    EXPECT_TRUE(found_title_fact);
    EXPECT_TRUE(found_author_fact);
    EXPECT_TRUE(found_current_page_fact);
    EXPECT_TRUE(found_zoom_level_fact);
    EXPECT_TRUE(found_target_page_input);

    bool found_prev_page = false;
    bool found_next_page = false;
    bool found_jump_page = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "pdf_prev_page") found_prev_page = true;
        if (act.verb == "pdf_next_page") found_next_page = true;
        if (act.verb == "pdf_jump_page") found_jump_page = true;
    }
    EXPECT_TRUE(found_prev_page);
    EXPECT_TRUE(found_next_page);
    EXPECT_TRUE(found_jump_page);
}

TEST(pdf_viewerAdaptiveTest, ActionDispatch) {
    rouen::cards::pdf_viewer viewer{};

    // Initial state
    EXPECT_FALSE(viewer.is_adaptive_view_mode());

    // 1. pdf_jump_page with page_number
    viewer.handle_action(R"({"type":"Action.Execute","verb":"pdf_jump_page","data":{"page_number":42}})");
    EXPECT_EQ(viewer.get_last_action(), "pdf_jump_page");
    EXPECT_EQ(viewer.get_current_page(), 42);

    // Also test alias field target_page
    viewer.handle_action(R"({"type":"Action.Execute","verb":"pdf_jump_page","data":{"target_page":100}})");
    EXPECT_EQ(viewer.get_last_action(), "pdf_jump_page");
    EXPECT_EQ(viewer.get_current_page(), 100);

    // 2. pdf_next_page
    viewer.handle_action(R"({"type":"Action.Execute","verb":"pdf_next_page"})");
    EXPECT_EQ(viewer.get_last_action(), "pdf_next_page");
    EXPECT_EQ(viewer.get_current_page(), 101);

    // 3. pdf_prev_page
    viewer.handle_action(R"({"type":"Action.Execute","verb":"pdf_prev_page"})");
    EXPECT_EQ(viewer.get_last_action(), "pdf_prev_page");
    EXPECT_EQ(viewer.get_current_page(), 100);

    // 4. pdf_zoom
    viewer.handle_action(R"({"type":"Action.Execute","verb":"pdf_zoom","data":{"zoom_factor":1.5}})");
    EXPECT_EQ(viewer.get_last_action(), "pdf_zoom");
    EXPECT_FLOAT_EQ(viewer.get_zoom_factor(), 1.5f);

    viewer.handle_action(R"({"type":"Action.Execute","verb":"pdf_zoom","data":{"zoom_factor":"125%"}})");
    EXPECT_EQ(viewer.get_last_action(), "pdf_zoom");
    EXPECT_FLOAT_EQ(viewer.get_zoom_factor(), 1.25f);

    // 5. toggle_adaptive_view
    viewer.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(viewer.is_adaptive_view_mode());
    viewer.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(viewer.is_adaptive_view_mode());
}

TEST(pdf_viewerAdaptiveTest, MalformedActionHandling) {
    rouen::cards::pdf_viewer viewer{};
    EXPECT_NO_THROW(viewer.handle_action(""));
    EXPECT_NO_THROW(viewer.handle_action("not a json string"));
    EXPECT_NO_THROW(viewer.handle_action("{"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"pdf_jump_page\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"pdf_jump_page\",\"data\":{\"page_number\":null}}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"pdf_next_page\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"pdf_prev_page\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"pdf_zoom\",\"data\":null}"));
    EXPECT_NO_THROW(viewer.handle_action("{\"verb\":\"pdf_zoom\",\"data\":{\"zoom_factor\":null}}"));
}

TEST(process_panelAdaptiveTest, JsonSerialization) {
    rouen::cards::process_panel panel{};
    std::string card_json = panel.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header = false;
    bool found_active_badge = false;
    bool found_process_facts = false;
    bool found_cmd_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "ColumnSet") {
                    for (const auto& col : child.columns) {
                        for (const auto& col_item : col.items) {
                            if (col_item.text.find("Process Orchestration Panel") != std::string::npos) {
                                found_header = true;
                            }
                            if (col_item.text.find("ACTIVE") != std::string::npos) {
                                found_active_badge = true;
                            }
                        }
                    }
                }
                if (child.type == "Input.Text" && child.id == "new_cmd") {
                    found_cmd_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            if (!elem.facts.empty()) {
                found_process_facts = true;
            }
        }
    }

    EXPECT_TRUE(found_header);
    EXPECT_TRUE(found_active_badge);
    EXPECT_TRUE(found_process_facts);
    EXPECT_TRUE(found_cmd_input);

    bool found_launch_action = false;
    bool found_kill_action = false;
    bool found_refresh_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "proc_launch") found_launch_action = true;
        if (act.verb == "proc_kill") found_kill_action = true;
        if (act.verb == "proc_refresh") found_refresh_action = true;
    }

    EXPECT_TRUE(found_launch_action);
    EXPECT_TRUE(found_kill_action);
    EXPECT_TRUE(found_refresh_action);
}

TEST(process_panelAdaptiveTest, ActionDispatch) {
    rouen::cards::process_panel panel{};

    // Initial state assertions
    EXPECT_FALSE(panel.is_adaptive_view_mode());
    EXPECT_EQ(panel.get_total_running_count(), 3u);
    EXPECT_EQ(panel.get_active_processes().size(), 3u);

    // 1. proc_launch
    panel.handle_action(R"({"type":"Action.Execute","verb":"proc_launch","data":{"command_line":"./scripts/daemon.sh"}})");
    EXPECT_EQ(panel.get_last_action(), "proc_launch");
    EXPECT_EQ(panel.get_last_launched_cmd(), "./scripts/daemon.sh");
    EXPECT_EQ(panel.get_total_running_count(), 4u);
    EXPECT_EQ(panel.get_active_processes().size(), 4u);

    // 2. proc_launch with new_cmd field
    panel.handle_action(R"({"type":"Action.Execute","verb":"proc_launch","data":{"new_cmd":"ninja -j2"}})");
    EXPECT_EQ(panel.get_last_action(), "proc_launch");
    EXPECT_EQ(panel.get_last_launched_cmd(), "ninja -j2");
    EXPECT_EQ(panel.get_total_running_count(), 5u);

    // 3. proc_kill
    panel.handle_action(R"({"type":"Action.Execute","verb":"proc_kill","data":{"pid":48914}})");
    EXPECT_EQ(panel.get_last_action(), "proc_kill");
    EXPECT_EQ(panel.get_last_killed_pid(), 48914);
    EXPECT_EQ(panel.get_total_running_count(), 4u);
    bool found_killed_pid = false;
    for (const auto& proc : panel.get_active_processes()) {
        if (proc.pid == 48914) found_killed_pid = true;
    }
    EXPECT_FALSE(found_killed_pid);

    // 4. proc_refresh
    panel.handle_action(R"({"type":"Action.Execute","verb":"proc_refresh"})");
    EXPECT_EQ(panel.get_last_action(), "proc_refresh");

    // 5. toggle_adaptive_view
    panel.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(panel.is_adaptive_view_mode());
    panel.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(panel.is_adaptive_view_mode());
}

TEST(process_panelAdaptiveTest, MalformedActionHandling) {
    rouen::cards::process_panel panel{};
    EXPECT_NO_THROW(panel.handle_action(""));
    EXPECT_NO_THROW(panel.handle_action("not a json string"));
    EXPECT_NO_THROW(panel.handle_action("{"));
    EXPECT_NO_THROW(panel.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(panel.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(panel.handle_action("{\"verb\":\"proc_launch\",\"data\":null}"));
    EXPECT_NO_THROW(panel.handle_action("{\"verb\":\"proc_launch\",\"data\":{\"command_line\":null}}"));
    EXPECT_NO_THROW(panel.handle_action("{\"verb\":\"proc_kill\",\"data\":null}"));
    EXPECT_NO_THROW(panel.handle_action("{\"verb\":\"proc_kill\",\"data\":{\"pid\":null}}"));
    EXPECT_NO_THROW(panel.handle_action("{\"verb\":\"proc_refresh\",\"data\":null}"));
}

TEST(radioAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::radio card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header = false;
    bool found_station_picker = false;
    bool found_broadcasting = false;
    bool found_codec = false;
    bool found_status = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "ColumnSet") {
                    for (const auto& col : child.columns) {
                        for (const auto& col_item : col.items) {
                            if (col_item.text.find("Global Internet Radio") != std::string::npos) {
                                found_header = true;
                            }
                        }
                    }
                }
                if (child.type == "Input.ChoiceSet" && child.id == "station_picker") {
                    found_station_picker = true;
                    EXPECT_GE(child.choices.size(), 4u);
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Currently Broadcasting:") found_broadcasting = true;
                if (fact.title == "Codec / Format:") found_codec = true;
                if (fact.title == "Station Status:") found_status = true;
            }
        }
    }

    EXPECT_TRUE(found_header);
    EXPECT_TRUE(found_station_picker);
    EXPECT_TRUE(found_broadcasting);
    EXPECT_TRUE(found_codec);
    EXPECT_TRUE(found_status);

    bool found_play_action = false;
    bool found_stop_action = false;
    bool found_fav_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "radio_play") found_play_action = true;
        if (act.verb == "radio_stop") found_stop_action = true;
        if (act.verb == "radio_toggle_fav") found_fav_action = true;
    }

    EXPECT_TRUE(found_play_action);
    EXPECT_TRUE(found_stop_action);
    EXPECT_TRUE(found_fav_action);
}

TEST(radioAdaptiveTest, ActionDispatch) {
    rouen::cards::radio card{};

    // Initial state assertions
    EXPECT_FALSE(card.is_adaptive_view_mode());
    EXPECT_FALSE(card.is_favorite());
    EXPECT_EQ(card.get_station_id(), "bbc4");
    EXPECT_EQ(card.get_station_name(), "BBC Radio 4");

    // 1. radio_play with station_picker choice
    card.handle_action(R"({"type":"Action.Execute","verb":"radio_play","data":{"station_picker":"kexp"}})");
    EXPECT_EQ(card.get_last_action(), "radio_play");
    EXPECT_EQ(card.get_station_id(), "kexp");
    EXPECT_EQ(card.get_station_name(), "KEXP 90.3 FM");
    EXPECT_TRUE(card.is_playing());

    // 2. radio_toggle_fav
    card.handle_action(R"({"type":"Action.Execute","verb":"radio_toggle_fav"})");
    EXPECT_EQ(card.get_last_action(), "radio_toggle_fav");
    EXPECT_TRUE(card.is_favorite());
    card.handle_action(R"({"type":"Action.Execute","verb":"radio_toggle_fav"})");
    EXPECT_FALSE(card.is_favorite());

    // 3. radio_stop
    card.handle_action(R"({"type":"Action.Execute","verb":"radio_stop"})");
    EXPECT_EQ(card.get_last_action(), "radio_stop");
    EXPECT_FALSE(card.is_playing());

    // 4. radio_play with station_id alternative key
    card.handle_action(R"({"type":"Action.Execute","verb":"radio_play","data":{"station_id":"mitre"}})");
    EXPECT_EQ(card.get_station_id(), "mitre");
    EXPECT_EQ(card.get_station_name(), "Radio Mitre");
    EXPECT_TRUE(card.is_playing());

    // 5. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(radioAdaptiveTest, MalformedActionHandling) {
    rouen::cards::radio card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radio_play\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radio_play\",\"data\":{\"station_picker\":null}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radio_stop\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radio_toggle_fav\",\"data\":null}"));
}

TEST(radiocutAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::radiocut card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header = false;
    bool found_date_input = false;
    bool found_hour_choice = false;
    bool found_segment_title = false;
    bool found_recorded_on = false;
    bool found_duration = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "ColumnSet") {
                    for (const auto& col : child.columns) {
                        for (const auto& col_item : col.items) {
                            if (col_item.text.find("RadioCut On-Demand Archive") != std::string::npos) {
                                found_header = true;
                            }
                        }
                    }
                }
                if (child.type == "Input.Date" && child.id == "cut_date") {
                    found_date_input = true;
                }
                if (child.type == "Input.ChoiceSet" && child.id == "cut_hour") {
                    found_hour_choice = true;
                    EXPECT_GE(child.choices.size(), 3u);
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Segment Title:") found_segment_title = true;
                if (fact.title == "Recorded On:") found_recorded_on = true;
                if (fact.title == "Duration:") found_duration = true;
            }
        }
    }

    EXPECT_TRUE(found_header);
    EXPECT_TRUE(found_date_input);
    EXPECT_TRUE(found_hour_choice);
    EXPECT_TRUE(found_segment_title);
    EXPECT_TRUE(found_recorded_on);
    EXPECT_TRUE(found_duration);

    bool found_play_action = false;
    bool found_download_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "radiocut_play") found_play_action = true;
        if (act.verb == "radiocut_download") found_download_action = true;
    }

    EXPECT_TRUE(found_play_action);
    EXPECT_TRUE(found_download_action);
}

TEST(radiocutAdaptiveTest, ActionDispatch) {
    rouen::cards::radiocut card{};

    // Initial state assertions
    EXPECT_FALSE(card.is_adaptive_view_mode());
    EXPECT_FALSE(card.is_playing());
    EXPECT_FALSE(card.is_downloading());
    EXPECT_EQ(card.get_broadcast_date(), "2026-10-06");
    EXPECT_EQ(card.get_broadcast_hour(), "08:00");
    EXPECT_FLOAT_EQ(card.get_playback_speed(), 1.25f);

    // 1. radiocut_play
    card.handle_action(R"({"type":"Action.Execute","verb":"radiocut_play","data":{"station":"Radio Mitre","cut_date":"2026-10-07","cut_hour":"09:00","cut_title":"Test Title"}})");
    EXPECT_EQ(card.get_last_action(), "radiocut_play");
    EXPECT_TRUE(card.is_playing());
    EXPECT_EQ(card.get_selected_station(), "Radio Mitre");
    EXPECT_EQ(card.get_broadcast_date(), "2026-10-07");
    EXPECT_EQ(card.get_broadcast_hour(), "09:00");
    EXPECT_EQ(card.get_cut_title(), "Test Title");

    // 2. radiocut_speed numeric
    card.handle_action(R"({"type":"Action.Execute","verb":"radiocut_speed","data":{"speed":1.5}})");
    EXPECT_EQ(card.get_last_action(), "radiocut_speed");
    EXPECT_FLOAT_EQ(card.get_playback_speed(), 1.5f);

    // radiocut_speed string format
    card.handle_action(R"({"type":"Action.Execute","verb":"radiocut_speed","data":{"speed":"2.0x"}})");
    EXPECT_FLOAT_EQ(card.get_playback_speed(), 2.0f);

    // 3. radiocut_download
    card.handle_action(R"({"type":"Action.Execute","verb":"radiocut_download"})");
    EXPECT_EQ(card.get_last_action(), "radiocut_download");
    EXPECT_TRUE(card.is_downloading());

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(radiocutAdaptiveTest, MalformedActionHandling) {
    rouen::cards::radiocut card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radiocut_play\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radiocut_speed\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radiocut_speed\",\"data\":{\"speed\":\"invalid\"}}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"radiocut_download\",\"data\":null}"));
}

TEST(rss_itemAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::rss_item card{"1"};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header = false;
    bool found_podcast_fact = false;
    bool found_format_fact = false;
    bool found_filesize_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "ColumnSet") {
                    for (const auto& col : child.columns) {
                        for (const auto& col_item : col.items) {
                            if (col_item.text.find("CppCast") != std::string::npos ||
                                col_item.text.find("RSS Item") != std::string::npos) {
                                found_header = true;
                            }
                        }
                    }
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title == "Podcast:") found_podcast_fact = true;
                if (fact.title == "Format:") found_format_fact = true;
                if (fact.title == "File Size:") found_filesize_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header);
    EXPECT_TRUE(found_podcast_fact);
    EXPECT_TRUE(found_format_fact);
    EXPECT_TRUE(found_filesize_fact);

    bool found_play_action = false;
    bool found_bookmark_action = false;
    bool found_shownotes_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "play_media") found_play_action = true;
        if (act.verb == "bookmark_item") found_bookmark_action = true;
        if (act.type == "Action.OpenUrl" || act.title == "Show Notes") found_shownotes_action = true;
    }

    EXPECT_TRUE(found_play_action);
    EXPECT_TRUE(found_bookmark_action);
    EXPECT_TRUE(found_shownotes_action);
}

TEST(rss_itemAdaptiveTest, ActionDispatch) {
    rouen::cards::rss_item card{"1"};

    // Initial state assertions
    EXPECT_FALSE(card.is_adaptive_view_mode());
    EXPECT_FALSE(card.is_playing());
    EXPECT_FALSE(card.is_bookmarked());

    // 1. play_media
    card.handle_action(R"({"type":"Action.Execute","verb":"play_media"})");
    EXPECT_EQ(card.get_last_action(), "play_media");
    EXPECT_TRUE(card.is_playing());

    // 2. pause_media
    card.handle_action(R"({"type":"Action.Execute","verb":"pause_media"})");
    EXPECT_EQ(card.get_last_action(), "pause_media");
    EXPECT_FALSE(card.is_playing());

    // 3. bookmark_item
    card.handle_action(R"({"type":"Action.Execute","verb":"bookmark_item"})");
    EXPECT_EQ(card.get_last_action(), "bookmark_item");
    EXPECT_TRUE(card.is_bookmarked());

    card.handle_action(R"({"type":"Action.Execute","verb":"bookmark_item"})");
    EXPECT_FALSE(card.is_bookmarked());

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(rss_itemAdaptiveTest, MalformedActionHandling) {
    rouen::cards::rss_item card{"1"};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"play_media\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"pause_media\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"bookmark_item\",\"data\":null}"));
}

static void ensure_rss_smart_list_test_services() {
    if (!registrar::try_get<std::function<bool()>>("quitting")) {
        registrar::add<std::function<bool()>>("quitting", std::make_shared<std::function<bool()>>([]() { return false; }));
    }
    if (!registrar::try_get<std::function<void(const std::string&)>>("notify")) {
        registrar::add<std::function<void(const std::string&)>>("notify", std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {}));
    }
    if (!registrar::try_get<std::function<void(const std::string&)>>("open_url")) {
        registrar::add<std::function<void(const std::string&)>>("open_url", std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {}));
    }
    if (!registrar::try_get<std::function<void(const std::string&)>>("create_card")) {
        registrar::add<std::function<void(const std::string&)>>("create_card", std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {}));
    }
}

TEST(rss_smart_listAdaptiveTest, JsonSerialization) {
    ensure_rss_smart_list_test_services();
    rouen::cards::rss_smart_list card{"Modern C++"};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header = false;
    bool found_rule_text = false;
    bool found_unread_badge = false;
    bool found_items_fact = false;
    bool found_keywords_input = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& child : elem.items) {
                if (child.type == "ColumnSet") {
                    for (const auto& col : child.columns) {
                        for (const auto& col_item : col.items) {
                            if (col_item.text.find("RSS Smart List") != std::string::npos ||
                                col_item.text.find("Modern C++") != std::string::npos) {
                                found_header = true;
                            }
                            if (col_item.text.find("Rule:") != std::string::npos) {
                                found_rule_text = true;
                            }
                            if (col_item.text.find("Unread") != std::string::npos) {
                                found_unread_badge = true;
                            }
                        }
                    }
                }
                if (child.type == "Input.Text" && child.id == "keywords") {
                    found_keywords_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            if (!elem.facts.empty()) {
                found_items_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header);
    EXPECT_TRUE(found_rule_text);
    EXPECT_TRUE(found_unread_badge);
    EXPECT_TRUE(found_items_fact);
    EXPECT_TRUE(found_keywords_input);

    bool found_refresh_action = false;
    bool found_mark_read_action = false;
    bool found_update_filter_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "smart_list_refresh") found_refresh_action = true;
        if (act.verb == "smart_list_mark_read") found_mark_read_action = true;
        if (act.verb == "smart_list_update_filter") found_update_filter_action = true;
    }

    EXPECT_TRUE(found_refresh_action);
    EXPECT_TRUE(found_mark_read_action);
    EXPECT_TRUE(found_update_filter_action);
}

TEST(rss_smart_listAdaptiveTest, ActionDispatch) {
    ensure_rss_smart_list_test_services();
    rouen::cards::rss_smart_list card{"Modern C++"};

    // Initial state assertions
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. smart_list_refresh
    card.handle_action(R"({"type":"Action.Execute","verb":"smart_list_refresh"})");
    EXPECT_EQ(card.get_last_action(), "smart_list_refresh");

    // 2. smart_list_mark_read
    card.handle_action(R"({"type":"Action.Execute","verb":"smart_list_mark_read"})");
    EXPECT_EQ(card.get_last_action(), "smart_list_mark_read");
    EXPECT_EQ(card.get_unread_count(), 0u);

    // 3. smart_list_update_filter
    card.handle_action(R"({"type":"Action.Execute","verb":"smart_list_update_filter","data":{"keywords":"c++20 OR modules"}})");
    EXPECT_EQ(card.get_last_action(), "smart_list_update_filter");
    EXPECT_EQ(card.get_filter_keywords(), "c++20 OR modules");

    card.handle_action(R"({"verb":"smart_list_update_filter","keywords":"ninja OR cmake"})");
    EXPECT_EQ(card.get_last_action(), "smart_list_update_filter");
    EXPECT_EQ(card.get_filter_keywords(), "ninja OR cmake");

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(rss_smart_listAdaptiveTest, MalformedActionHandling) {
    ensure_rss_smart_list_test_services();
    rouen::cards::rss_smart_list card{"Modern C++"};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"smart_list_refresh\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"smart_list_mark_read\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"smart_list_update_filter\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"smart_list_update_filter\",\"data\":{\"keywords\":null}}"));
}

TEST(settings_cardAdaptiveTest, JsonSerialization) {
    auto dummy_open_url = std::make_shared<std::function<void(const std::string&)>>([](const std::string&) {});
    registrar::add<std::function<void(const std::string&)>>("open_url", dummy_open_url);

    rouen::cards::settings_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header_title = false;
    bool found_provider_input = false;
    bool found_model_input = false;
    bool found_config_location_fact = false;
    bool found_llm_status_fact = false;
    bool found_theme_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& col_set : elem.items) {
                if (col_set.type == "ColumnSet") {
                    for (const auto& col : col_set.columns) {
                        for (const auto& item : col.items) {
                            if (item.type == "TextBlock") {
                                if (item.text.find("Global System & AI Configuration") != std::string::npos) {
                                    found_header_title = true;
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& item : elem.items) {
                if (item.type == "Input.ChoiceSet") {
                    if (item.id == "llm_provider") found_provider_input = true;
                    if (item.id == "active_model") found_model_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("Config Location") != std::string::npos) found_config_location_fact = true;
                if (fact.title.find("LLM Status") != std::string::npos) found_llm_status_fact = true;
                if (fact.title.find("Theme") != std::string::npos) found_theme_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header_title);
    EXPECT_TRUE(found_provider_input);
    EXPECT_TRUE(found_model_input);
    EXPECT_TRUE(found_config_location_fact);
    EXPECT_TRUE(found_llm_status_fact);
    EXPECT_TRUE(found_theme_fact);

    bool found_save_action = false;
    bool found_test_llm_action = false;
    bool found_query_models_action = false;

    for (const auto& act : doc.actions) {
        if (act.verb == "settings_save") found_save_action = true;
        if (act.verb == "settings_test_llm") found_test_llm_action = true;
        if (act.verb == "settings_query_models") found_query_models_action = true;
    }

    EXPECT_TRUE(found_save_action);
    EXPECT_TRUE(found_test_llm_action);
    EXPECT_TRUE(found_query_models_action);
}

TEST(settings_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::settings_card card{};
    card.set_allow_network_io(false);

    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. settings_save
    card.handle_action(R"({"type":"Action.Execute","verb":"settings_save","data":{"provider":"openai","model":"gpt-4o","theme":"Catppuccin Mocha"}})");
    EXPECT_EQ(card.get_last_action(), "settings_save");
    EXPECT_EQ(card.get_current_provider(), "openai");
    EXPECT_EQ(card.get_selected_model(), "gpt-4o");
    EXPECT_EQ(card.get_theme_name(), "Catppuccin Mocha");

    // 2. settings_test_llm
    card.handle_action(R"({"type":"Action.Execute","verb":"settings_test_llm"})");
    EXPECT_EQ(card.get_last_action(), "settings_test_llm");

    // 3. settings_query_models
    card.handle_action(R"({"type":"Action.Execute","verb":"settings_query_models","data":{"provider":"gemini"}})");
    EXPECT_EQ(card.get_last_action(), "settings_query_models");
    EXPECT_EQ(card.get_current_provider(), "gemini");

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(settings_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::settings_card card{};
    card.set_allow_network_io(false);
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"settings_save\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"settings_test_llm\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"settings_query_models\",\"data\":null}"));
}

TEST(solar_systemAdaptiveTest, JsonSerialization) {
    rouen::cards::solar_system card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header_title = false;
    bool found_celestial_body_input = false;
    bool found_dist_fact = false;
    bool found_velocity_fact = false;
    bool found_radius_fact = false;
    bool found_gravity_fact = false;
    bool found_moons_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& col_set : elem.items) {
                if (col_set.type == "ColumnSet") {
                    for (const auto& col : col_set.columns) {
                        for (const auto& item : col.items) {
                            if (item.type == "TextBlock") {
                                if (item.text.find("Solar System Ephemeris") != std::string::npos) {
                                    found_header_title = true;
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& item : elem.items) {
                if (item.type == "Input.ChoiceSet") {
                    if (item.id == "celestial_body") {
                        found_celestial_body_input = true;
                    }
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("Distance from Sun") != std::string::npos) found_dist_fact = true;
                if (fact.title.find("Orbital Velocity") != std::string::npos) found_velocity_fact = true;
                if (fact.title.find("Equatorial Radius") != std::string::npos) found_radius_fact = true;
                if (fact.title.find("Surface Gravity") != std::string::npos) found_gravity_fact = true;
                if (fact.title.find("Known Moons") != std::string::npos) found_moons_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header_title);
    EXPECT_TRUE(found_celestial_body_input);
    EXPECT_TRUE(found_dist_fact);
    EXPECT_TRUE(found_velocity_fact);
    EXPECT_TRUE(found_radius_fact);
    EXPECT_TRUE(found_gravity_fact);
    EXPECT_TRUE(found_moons_fact);

    bool found_select_body_action = false;
    bool found_open_url_action = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "select_body" || act.title.find("Inspect Body") != std::string::npos) {
            found_select_body_action = true;
        }
        if (act.type == "Action.OpenUrl" || act.url.find("horizons") != std::string::npos) {
            found_open_url_action = true;
        }
    }
    EXPECT_TRUE(found_select_body_action);
    EXPECT_TRUE(found_open_url_action);
}

TEST(solar_systemAdaptiveTest, ActionDispatch) {
    rouen::cards::solar_system card{};
    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. select_body
    card.handle_action(R"({"type":"Action.Execute","verb":"select_body","data":{"celestial_body":"Jupiter"}})");
    EXPECT_EQ(card.get_last_action(), "select_body");
    EXPECT_EQ(card.get_selected_body_name(), "Jupiter");

    card.handle_action(R"({"type":"Action.Execute","verb":"select_body","data":{"celestial_body":"Mars"}})");
    EXPECT_EQ(card.get_last_action(), "select_body");
    EXPECT_EQ(card.get_selected_body_name(), "Mars");

    // 2. time_lapse_speed
    card.handle_action(R"({"type":"Action.Execute","verb":"time_lapse_speed","data":{"speed_multiplier":5.0}})");
    EXPECT_EQ(card.get_last_action(), "time_lapse_speed");
    EXPECT_FLOAT_EQ(card.get_anim_speed(), 5.0f);

    // 3. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(solar_systemAdaptiveTest, MalformedActionHandling) {
    rouen::cards::solar_system card{};
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"select_body\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"time_lapse_speed\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"time_lapse_speed\",\"data\":{\"speed_multiplier\":\"invalid_number\"}}"));
}

TEST(subnet_scannerAdaptiveTest, JsonSerialization) {
    rouen::cards::subnet_scanner card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header_title = false;
    bool found_subnet_input = false;
    bool found_router_fact = false;
    bool found_win_fact = false;
    bool found_host_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& col_set : elem.items) {
                if (col_set.type == "ColumnSet") {
                    for (const auto& col : col_set.columns) {
                        for (const auto& item : col.items) {
                            if (item.type == "TextBlock") {
                                if (item.text.find("Local Subnet Scanner") != std::string::npos) {
                                    found_header_title = true;
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& item : elem.items) {
                if (item.type == "Input.Text" && item.id == "subnet_input") {
                    found_subnet_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("192.168.1.1:") != std::string::npos) found_router_fact = true;
                if (fact.title.find("192.168.1.54:") != std::string::npos) found_win_fact = true;
                if (fact.title.find("192.168.1.102:") != std::string::npos) found_host_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header_title);
    EXPECT_TRUE(found_subnet_input);
    EXPECT_TRUE(found_router_fact);
    EXPECT_TRUE(found_win_fact);
    EXPECT_TRUE(found_host_fact);

    bool found_scan_action = false;
    bool found_stop_action = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "subnet_scan") found_scan_action = true;
        if (act.verb == "subnet_stop") found_stop_action = true;
    }
    EXPECT_TRUE(found_scan_action);
    EXPECT_TRUE(found_stop_action);
}

TEST(subnet_scannerAdaptiveTest, ActionDispatch) {
    rouen::cards::subnet_scanner card{};
    card.set_allow_network_io(false);

    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. subnet_scan
    card.handle_action(R"({"type":"Action.Execute","verb":"subnet_scan","data":{"subnet":"10.0.0.0/24"}})");
    EXPECT_EQ(card.get_last_action(), "subnet_scan");
    EXPECT_EQ(card.get_subnet_cidr(), "10.0.0.0/24");

    // 2. subnet_stop
    card.handle_action(R"({"type":"Action.Execute","verb":"subnet_stop"})");
    EXPECT_EQ(card.get_last_action(), "subnet_stop");
    EXPECT_FALSE(card.get_is_scanning());

    // 3. subnet_ping
    card.handle_action(R"({"type":"Action.Execute","verb":"subnet_ping","data":{"ip_address":"192.168.1.54"}})");
    EXPECT_EQ(card.get_last_action(), "subnet_ping");
    EXPECT_EQ(card.get_last_pinged_ip(), "192.168.1.54");

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(subnet_scannerAdaptiveTest, MalformedActionHandling) {
    rouen::cards::subnet_scanner card{};
    card.set_allow_network_io(false);
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"subnet_scan\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"subnet_ping\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"subnet_stop\",\"data\":null}"));
}

TEST(sync_cardAdaptiveTest, JsonSerialization) {
    rouen::cards::sync_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header_title = false;
    bool found_status_badge = false;
    bool found_last_sync_fact = false;
    bool found_synced_dbs_fact = false;
    bool found_peers_fact = false;
    bool found_conflicts_fact = false;
    bool found_strategy_choice = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& col_set : elem.items) {
                if (col_set.type == "ColumnSet") {
                    for (const auto& col : col_set.columns) {
                        for (const auto& item : col.items) {
                            if (item.type == "TextBlock") {
                                if (item.text.find("Universal Cross-Device Synchronization") != std::string::npos) {
                                    found_header_title = true;
                                }
                                if (item.text.find("IN SYNC") != std::string::npos) {
                                    found_status_badge = true;
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& item : elem.items) {
                if (item.type == "Input.ChoiceSet" && item.id == "conflict_strategy") {
                    found_strategy_choice = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("Last Sync Timestamp:") != std::string::npos) found_last_sync_fact = true;
                if (fact.title.find("Synced Databases:") != std::string::npos) found_synced_dbs_fact = true;
                if (fact.title.find("Peer Devices in Mesh:") != std::string::npos) found_peers_fact = true;
                if (fact.title.find("Unresolved Conflicts:") != std::string::npos) found_conflicts_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header_title);
    EXPECT_TRUE(found_status_badge);
    EXPECT_TRUE(found_last_sync_fact);
    EXPECT_TRUE(found_synced_dbs_fact);
    EXPECT_TRUE(found_peers_fact);
    EXPECT_TRUE(found_conflicts_fact);
    EXPECT_TRUE(found_strategy_choice);

    bool found_sync_now = false;
    bool found_sync_export = false;
    bool found_resolve_conflicts = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "sync_now") found_sync_now = true;
        if (act.verb == "sync_export") found_sync_export = true;
        if (act.verb == "sync_resolve_conflicts") found_resolve_conflicts = true;
    }
    EXPECT_TRUE(found_sync_now);
    EXPECT_TRUE(found_sync_export);
    EXPECT_TRUE(found_resolve_conflicts);
}

TEST(sync_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::sync_card card{};
    card.set_allow_network_io(false);

    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. sync_now
    card.handle_action(R"({"type":"Action.Execute","verb":"sync_now"})");
    EXPECT_EQ(card.get_last_action(), "sync_now");

    // 2. sync_export
    card.handle_action(R"({"type":"Action.Execute","verb":"sync_export"})");
    EXPECT_EQ(card.get_last_action(), "sync_export");

    // 3. sync_resolve_conflicts
    card.set_conflict_count(3);
    EXPECT_EQ(card.get_conflict_count(), 3);
    card.handle_action(R"({"type":"Action.Execute","verb":"sync_resolve_conflicts","data":{"strategy":"remote-wins"}})");
    EXPECT_EQ(card.get_last_action(), "sync_resolve_conflicts");
    EXPECT_EQ(card.get_conflict_strategy(), "remote-wins");
    EXPECT_EQ(card.get_conflict_count(), 0);

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(sync_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::sync_card card{};
    card.set_allow_network_io(false);
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"sync_now\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"sync_export\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"sync_resolve_conflicts\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"sync_resolve_conflicts\",\"data\":{\"strategy\":null}}"));
}

TEST(telegram_cardAdaptiveTest, JsonSerialization) {
    rouen::cards::telegram_card card{};
    std::string card_json = card.get_adaptive_card_json();
    EXPECT_FALSE(card_json.empty());
    EXPECT_NE(card_json.find("AdaptiveCard"), std::string::npos);

    rouen::helpers::adaptive_cards::parser parser{};
    auto doc = parser.parse(card_json);
    EXPECT_EQ(doc.type, "AdaptiveCard");
    EXPECT_EQ(doc.version, "1.5");

    bool found_header_title = false;
    bool found_status_badge = false;
    bool found_message_input = false;
    bool found_ignacio_fact = false;
    bool found_dev_team_fact = false;

    for (const auto& elem : doc.body) {
        if (elem.type == "Container") {
            for (const auto& col_set : elem.items) {
                if (col_set.type == "ColumnSet") {
                    for (const auto& col : col_set.columns) {
                        for (const auto& item : col.items) {
                            if (item.type == "TextBlock") {
                                if (item.text.find("Telegram Bot Console") != std::string::npos) {
                                    found_header_title = true;
                                }
                                if (item.text.find("ACTIVE") != std::string::npos) {
                                    found_status_badge = true;
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& item : elem.items) {
                if (item.type == "Input.Text" && item.id == "tg_message_text") {
                    found_message_input = true;
                }
            }
        }
        if (elem.type == "FactSet") {
            for (const auto& fact : elem.facts) {
                if (fact.title.find("@ignacio") != std::string::npos) found_ignacio_fact = true;
                if (fact.title.find("@dev_team") != std::string::npos) found_dev_team_fact = true;
            }
        }
    }

    EXPECT_TRUE(found_header_title);
    EXPECT_TRUE(found_status_badge);
    EXPECT_TRUE(found_message_input);
    EXPECT_TRUE(found_ignacio_fact);
    EXPECT_TRUE(found_dev_team_fact);

    bool found_send_action = false;
    bool found_command_action = false;
    bool found_refresh_action = false;
    for (const auto& act : doc.actions) {
        if (act.verb == "telegram_send") found_send_action = true;
        if (act.verb == "telegram_command") found_command_action = true;
        if (act.verb == "telegram_refresh") found_refresh_action = true;
    }
    EXPECT_TRUE(found_send_action);
    EXPECT_TRUE(found_command_action);
    EXPECT_TRUE(found_refresh_action);
}

TEST(telegram_cardAdaptiveTest, ActionDispatch) {
    rouen::cards::telegram_card card{};
    card.set_allow_network_io(false);

    EXPECT_FALSE(card.is_adaptive_view_mode());

    // 1. telegram_send
    card.handle_action(R"({"type":"Action.Execute","verb":"telegram_send","data":{"chat_id":"987654321","text":"Hello from Rouen Adaptive Card"}})");
    EXPECT_EQ(card.get_last_action(), "telegram_send");
    EXPECT_EQ(card.get_last_sent_chat_id(), "987654321");
    EXPECT_EQ(card.get_last_sent_text(), "Hello from Rouen Adaptive Card");

    // 1b. telegram_send with tg_message_text input key
    card.handle_action(R"({"type":"Action.Execute","verb":"telegram_send","data":{"tg_message_text":"Message via input"}})");
    EXPECT_EQ(card.get_last_action(), "telegram_send");
    EXPECT_EQ(card.get_last_sent_text(), "Message via input");

    // 2. telegram_command
    card.handle_action(R"({"type":"Action.Execute","verb":"telegram_command","data":{"cmd":"/status"}})");
    EXPECT_EQ(card.get_last_action(), "telegram_command");
    EXPECT_EQ(card.get_last_command(), "/status");

    // 3. telegram_refresh
    card.handle_action(R"({"type":"Action.Execute","verb":"telegram_refresh"})");
    EXPECT_EQ(card.get_last_action(), "telegram_refresh");
    EXPECT_TRUE(card.get_refresh_triggered());

    // 4. toggle_adaptive_view
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_TRUE(card.is_adaptive_view_mode());
    card.handle_action(R"({"type":"Action.Execute","verb":"toggle_adaptive_view"})");
    EXPECT_FALSE(card.is_adaptive_view_mode());
}

TEST(telegram_cardAdaptiveTest, MalformedActionHandling) {
    rouen::cards::telegram_card card{};
    card.set_allow_network_io(false);
    EXPECT_NO_THROW(card.handle_action(""));
    EXPECT_NO_THROW(card.handle_action("not a json string"));
    EXPECT_NO_THROW(card.handle_action("{"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": 1234}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\": \"unknown_verb_xyz\"}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"telegram_send\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"telegram_command\",\"data\":null}"));
    EXPECT_NO_THROW(card.handle_action("{\"verb\":\"telegram_refresh\",\"data\":null}"));
}











