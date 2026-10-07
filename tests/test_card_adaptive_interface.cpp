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
#include "../src/cards/system/about.hpp"
#include "../src/cards/system/sysinfo.hpp"
#include "../src/cards/productivity/pomodoro.hpp"
#include "../src/cards/development/vcproject_card.hpp"
#include "../src/cards/development/fs-directory.hpp"
#include "../src/cards/development/git.hpp"
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


