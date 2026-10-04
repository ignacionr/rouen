#include <gtest/gtest.h>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "../src/models/telegram_presence.hpp"
#include "../src/hosts/rouen_mesh_host.hpp"
#include "../src/hosts/telegram_host.hpp"
#include "../src/helpers/presence_service.hpp"
#include "../src/helpers/config_service.hpp"
#include "../src/registrar.hpp"

using namespace rouen::hosts;
using namespace rouen::services;

namespace rouen::hosts {
    mcp_host::mcp_host() {}
    void mcp_host::register_function(const std::string&, const function_definition&) {}
    std::vector<mcp_host::function_definition> mcp_host::get_available_functions() const { return {}; }
    mcp_host::execution_result mcp_host::execute_function(const std::string&, const std::string&) {
        return execution_result(false, "", "Not implemented");
    }
}

TEST(TelegramMeshNotifyTest, PresenceRecordSerialization) {
    telegram_presence_record rec{
        .client_id = "rouen-macbook",
        .hostname = "MacBook.local",
        .platform = "macos",
        .bot_username = "rouenapp_bot",
        .bot_first_name = "Rouen Assistant",
        .operator_chat_id = 6885715531,
        .status = "active",
        .local_api_port = 8081,
        .capabilities = {"send_notification", "send_message", "read_sessions"},
        .last_seen_epoch_ms = 1759410195000,
        .last_seen_iso = "2026-10-02T10:05:00Z"
    };

    std::string json_str;
    auto err = glz::write_json(rec, json_str);
    EXPECT_FALSE(err);
    EXPECT_NE(json_str.find("\"client_id\":\"rouen-macbook\""), std::string::npos);
    EXPECT_NE(json_str.find("\"operator_chat_id\":6885715531"), std::string::npos);
    EXPECT_NE(json_str.find("\"bot_username\":\"rouenapp_bot\""), std::string::npos);

    telegram_presence_record decoded{};
    auto read_err = glz::read_json(decoded, json_str);
    EXPECT_FALSE(read_err);
    EXPECT_EQ(decoded.client_id, "rouen-macbook");
    EXPECT_EQ(decoded.operator_chat_id, 6885715531);
    EXPECT_EQ(decoded.bot_username, "rouenapp_bot");
    EXPECT_EQ(decoded.status, "active");
    EXPECT_EQ(decoded.local_api_port, 8081);
    EXPECT_EQ(decoded.capabilities.size(), 3);
}

TEST(TelegramMeshNotifyTest, NotificationOptionsSerialization) {
    notification_options opts{
        .channel = "telegram",
        .urgent = true,
        .spoken = false,
        .target_chat_id = 123456789
    };

    std::string json_str;
    auto err = glz::write_json(opts, json_str);
    EXPECT_FALSE(err);

    notification_options decoded{};
    auto read_err = glz::read_json(decoded, json_str);
    EXPECT_FALSE(read_err);
    EXPECT_EQ(decoded.channel, "telegram");
    EXPECT_TRUE(decoded.urgent);
    EXPECT_FALSE(decoded.spoken);
    EXPECT_EQ(decoded.target_chat_id, 123456789);
}

TEST(TelegramMeshNotifyTest, GatewayDiscoveryPicksNewestActive) {
    auto& mesh = rouen_mesh_host::instance();
    mesh.clear();

    telegram_presence_record gw1{
        .client_id = "node-old",
        .hostname = "old.local",
        .platform = "linux",
        .bot_username = "old_bot",
        .bot_first_name = "Old Bot",
        .operator_chat_id = 111,
        .status = "active",
        .local_api_port = 8081,
        .capabilities = {"send_notification"},
        .last_seen_epoch_ms = 1000000,
        .last_seen_iso = "2026-10-01T00:00:00Z"
    };

    telegram_presence_record gw2{
        .client_id = "node-new",
        .hostname = "new.local",
        .platform = "macos",
        .bot_username = "new_bot",
        .bot_first_name = "New Bot",
        .operator_chat_id = 222,
        .status = "active",
        .local_api_port = 8081,
        .capabilities = {"send_notification"},
        .last_seen_epoch_ms = 2000000,
        .last_seen_iso = "2026-10-02T00:00:00Z"
    };

    telegram_presence_record gw_inactive{
        .client_id = "node-dead",
        .hostname = "dead.local",
        .platform = "windows",
        .bot_username = "dead_bot",
        .bot_first_name = "Dead Bot",
        .operator_chat_id = 333,
        .status = "inactive",
        .local_api_port = 8081,
        .capabilities = {"send_notification"},
        .last_seen_epoch_ms = 3000000,
        .last_seen_iso = "2026-10-03T00:00:00Z"
    };

    std::string s1, s2, s3;
    (void)glz::write_json(gw1, s1);
    (void)glz::write_json(gw2, s2);
    (void)glz::write_json(gw_inactive, s3);

    mesh.set_registry_value("telegram/presence/node-old", s1);
    mesh.set_registry_value("telegram/presence/node-new", s2);
    mesh.set_registry_value("telegram/presence/node-dead", s3);

    auto& ps = presence_service::instance();
    auto best = ps.find_active_telegram_gateway();
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->client_id, "node-new");
    EXPECT_EQ(best->operator_chat_id, 222);
}

TEST(TelegramMeshNotifyTest, RouteNotificationTelegramDirect) {
    auto& mesh = rouen_mesh_host::instance();
    mesh.clear();

    telegram_presence_record gw{
        .client_id = "tg-gateway",
        .hostname = "gw.local",
        .platform = "macos",
        .bot_username = "active_bot",
        .bot_first_name = "Active Bot",
        .operator_chat_id = 777888999,
        .status = "active",
        .local_api_port = 8081,
        .capabilities = {"send_notification"},
        .last_seen_epoch_ms = 5000000,
        .last_seen_iso = "2026-10-02T10:00:00Z"
    };
    std::string gw_json;
    (void)glz::write_json(gw, gw_json);
    mesh.set_registry_value("telegram/presence/tg-gateway", gw_json);

    auto& ps = presence_service::instance();
    notification_options opts{
        .channel = "telegram",
        .urgent = false,
        .spoken = false
    };

    auto [success, target] = ps.route_notification("🚨 Regression test failed", "", opts);
    EXPECT_TRUE(success);
    EXPECT_NE(target.find("tg-gateway"), std::string::npos);

    // Verify inbox fallback key was recorded in registry for tg-gateway
    auto entries = mesh.get_registry_entries("notifications/inbox/tg-gateway/");
    EXPECT_FALSE(entries.empty());
    for (const auto& [k, v] : entries) {
        EXPECT_NE(v.value.find("[telegram]"), std::string::npos);
        EXPECT_NE(v.value.find("Regression test failed"), std::string::npos);
    }
}

TEST(TelegramMeshNotifyTest, IncomingTelegramRelayExtraction) {
    auto& mesh = rouen_mesh_host::instance();
    mesh.clear();

    mesh_host_config cfg;
    cfg.client_id = "test-gateway-client";
    mesh.initialize(cfg);

    std::string relayed_msg;
    registrar::add<std::function<bool(const std::string&)>>(
        "telegram_relay_message",
        std::make_shared<std::function<bool(const std::string&)>>([&](const std::string& msg) {
            relayed_msg = msg;
            return true;
        })
    );

    // Simulate incoming mesh notification with [telegram] prefix
    rouen::mesh::inbox_notification_dto notif{
        .message = "[telegram] Build completed on ir-01 in 42s",
        .from_client = "ir-01",
        .spoken = false,
        .timestamp_ms = 1759410000000
    };
    std::string notif_json;
    (void)glz::write_json(notif, notif_json);
    mesh.set_registry_value("notifications/inbox/test-gateway-client/msg_1", notif_json);

    mesh.process_incoming_notifications();

    EXPECT_EQ(relayed_msg, "Build completed on ir-01 in 42s");
}

TEST(TelegramMeshNotifyTest, UrgentNotificationAlertsBothDesktopAndTelegram) {
    auto& mesh = rouen_mesh_host::instance();
    mesh.clear();

    telegram_presence_record gw{
        .client_id = "gateway-mac",
        .hostname = "gw.local",
        .platform = "macos",
        .bot_username = "urgent_bot",
        .bot_first_name = "Urgent Bot",
        .operator_chat_id = 999,
        .status = "active",
        .local_api_port = 8081,
        .capabilities = {"send_notification"},
        .last_seen_epoch_ms = 9999999,
        .last_seen_iso = "2026-10-02T10:00:00Z"
    };
    std::string gw_json;
    (void)glz::write_json(gw, gw_json);
    mesh.set_registry_value("telegram/presence/gateway-mac", gw_json);

    std::string local_alert;
    registrar::add<std::function<void(const std::string&)>>(
        "notify",
        std::make_shared<std::function<void(const std::string&)>>([&](const std::string& msg) {
            local_alert = msg;
        })
    );

    auto& ps = presence_service::instance();
    notification_options opts{
        .channel = "auto",
        .urgent = true,
        .spoken = true
    };

    auto [success, target] = ps.route_notification("🔥 High Priority Server Down", "", opts);
    EXPECT_TRUE(success);
    EXPECT_EQ(local_alert, "🔥 High Priority Server Down");
    EXPECT_NE(target.find("gateway-mac"), std::string::npos);
}

TEST(TelegramMeshNotifyTest, IdleAutoEscalationToTelegram) {
    auto& mesh = rouen_mesh_host::instance();
    mesh.clear();

    telegram_presence_record gw{
        .client_id = "idle-gateway",
        .hostname = "gw.local",
        .platform = "macos",
        .bot_username = "idle_bot",
        .bot_first_name = "Idle Bot",
        .operator_chat_id = 555,
        .status = "active",
        .local_api_port = 8081,
        .capabilities = {"send_notification"},
        .last_seen_epoch_ms = 8888888,
        .last_seen_iso = "2026-10-02T10:00:00Z"
    };
    std::string gw_json;
    (void)glz::write_json(gw, gw_json);
    mesh.set_registry_value("telegram/presence/idle-gateway", gw_json);

    // Simulate all peers being idle (> 500 seconds ago)
    presence_record idle_peer{
        .client_id = "peer-ir01",
        .user = "operator",
        .hostname = "ir-01",
        .platform = "windows",
        .last_active_epoch_ms = 1000,
        .last_active_iso = "2026-10-01T00:00:00Z",
        .status = "idle"
    };
    std::string peer_json;
    (void)glz::write_json(idle_peer, peer_json);
    mesh.set_registry_value("presence/peer-ir01", peer_json);

    auto& ps = presence_service::instance();
    // Default channel = "auto"
    notification_options opts{
        .channel = "auto",
        .urgent = false,
        .spoken = false
    };

    auto [success, target] = ps.route_notification("Nightly build finished", "", opts);
    EXPECT_TRUE(success);
}

TEST(TelegramMeshNotifyTest, TelegramHostStartupAndCallbacks) {
    auto host = telegram_host::get_host();
    ASSERT_NE(host, nullptr);

    // Verify registrar has required telegram services registered
    auto publish_fn = registrar::try_get<std::function<void()>>("publish_telegram_presence");
    EXPECT_NE(publish_fn, nullptr);

    auto send_fn = registrar::try_get<std::function<bool(const std::string&, int64_t)>>("telegram_send_local");
    EXPECT_NE(send_fn, nullptr);

    auto relay_fn = registrar::try_get<std::function<bool(const std::string&)>>("telegram_relay_message");
    EXPECT_NE(relay_fn, nullptr);
}

TEST(TelegramMeshNotifyTest, TelegramHostEnvConfigFallback) {
    auto host = telegram_host::get_host();
    ASSERT_NE(host, nullptr);

#ifdef _WIN32
    _putenv_s("TELEGRAM_OPERATOR_CHAT_ID", "6885715531");
#else
    ::setenv("TELEGRAM_OPERATOR_CHAT_ID", "6885715531", 1);
#endif
    EXPECT_NE(host->get_operator_chat_id(), 0);
}

TEST(TelegramMeshNotifyTest, TelegramHostPublishPresenceNoDeadlock) {
    auto host = telegram_host::get_host();
    ASSERT_NE(host, nullptr);

    host->publish_telegram_presence();
    auto rec = host->get_local_presence_record();
    EXPECT_FALSE(rec.client_id.empty());
    EXPECT_FALSE(rec.hostname.empty());
}

TEST(TelegramMeshNotifyTest, TelegramChatSessionVoiceGlazeSerialization) {
    telegram_chat_session s{
        .chat_id = 12345,
        .user_id = 67890,
        .user_name = "Alice",
        .last_message_text = "Hello",
        .last_message_time = 1700000000,
        .messages = {},
        .voice_enabled = true
    };

    std::string json_str;
    auto err = glz::write_json(s, json_str);
    EXPECT_FALSE(err);
    EXPECT_NE(json_str.find("\"voice_enabled\":true"), std::string::npos);

    telegram_chat_session decoded{};
    auto read_err = glz::read_json(decoded, json_str);
    EXPECT_FALSE(read_err);
    EXPECT_EQ(decoded.chat_id, 12345);
    EXPECT_TRUE(decoded.voice_enabled);

    // Test default when omitted
    std::string json_without_voice = R"({"chat_id":99999,"user_id":11111,"user_name":"Bob","last_message_text":"Hi","last_message_time":0,"messages":[]})";
    telegram_chat_session decoded2{};
    auto read_err2 = glz::read_json(decoded2, json_without_voice);
    EXPECT_FALSE(read_err2);
    EXPECT_FALSE(decoded2.voice_enabled);
}

TEST(TelegramMeshNotifyTest, TelegramHostCommandsHelpAndClear) {
    auto host = telegram_host::get_host();
    ASSERT_NE(host, nullptr);
    int64_t chat_id = 999111222;

    // Test /help
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/help"));
    auto sess1 = host->get_session(chat_id);
    ASSERT_TRUE(sess1.has_value());
    ASSERT_FALSE(sess1->messages.empty());
    EXPECT_TRUE(sess1->messages.back().is_outgoing);
    EXPECT_NE(sess1->messages.back().text.find("Available commands"), std::string::npos);
    EXPECT_NE(sess1->messages.back().text.find("/clear"), std::string::npos);
    EXPECT_NE(sess1->messages.back().text.find("/voice"), std::string::npos);

    // Test -help (dash command variant)
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "-help"));
    auto sess2 = host->get_session(chat_id);
    ASSERT_TRUE(sess2.has_value());
    ASSERT_FALSE(sess2->messages.empty());
    EXPECT_TRUE(sess2->messages.back().is_outgoing);
    EXPECT_NE(sess2->messages.back().text.find("Available commands"), std::string::npos);

    // Test /clear
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/clear"));
    auto sess3 = host->get_session(chat_id);
    ASSERT_TRUE(sess3.has_value());
    // Context cleared replies with message and stores only the reply
    ASSERT_FALSE(sess3->messages.empty());
    EXPECT_TRUE(sess3->messages.back().is_outgoing);
    EXPECT_NE(sess3->messages.back().text.find("Chat context and conversation history have been cleared"), std::string::npos);

    // Test -clear (dash variant)
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "-clear"));
    auto sess4 = host->get_session(chat_id);
    ASSERT_TRUE(sess4.has_value());
    ASSERT_FALSE(sess4->messages.empty());
    EXPECT_TRUE(sess4->messages.back().is_outgoing);
    EXPECT_NE(sess4->messages.back().text.find("Chat context and conversation history have been cleared"), std::string::npos);
}

TEST(TelegramMeshNotifyTest, TelegramHostCommandsVoice) {
    auto host = telegram_host::get_host();
    ASSERT_NE(host, nullptr);
    int64_t chat_id = 999333444;

#if !defined(__APPLE__)
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/voice on"));
    auto sess = host->get_session(chat_id);
    ASSERT_TRUE(sess.has_value());
    EXPECT_FALSE(host->is_voice_enabled(chat_id));
    EXPECT_NE(sess->messages.back().text.find("only available on macOS"), std::string::npos);
#else
    // Reset to initially disabled state for hermetic testing
    host->set_voice_enabled(chat_id, false);
    EXPECT_FALSE(host->is_voice_enabled(chat_id));

    // /voice on
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/voice on"));
    EXPECT_TRUE(host->is_voice_enabled(chat_id));
    auto sess1 = host->get_session(chat_id);
    ASSERT_TRUE(sess1.has_value());
    EXPECT_NE(sess1->messages.back().text.find("Voice mode enabled"), std::string::npos);

    // /voice off
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/voice off"));
    EXPECT_FALSE(host->is_voice_enabled(chat_id));
    auto sess2 = host->get_session(chat_id);
    ASSERT_TRUE(sess2.has_value());
    EXPECT_NE(sess2->messages.back().text.find("Voice mode disabled"), std::string::npos);

    // /voice (toggle to on)
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/voice"));
    EXPECT_TRUE(host->is_voice_enabled(chat_id));
    auto sess3 = host->get_session(chat_id);
    ASSERT_TRUE(sess3.has_value());
    EXPECT_NE(sess3->messages.back().text.find("Voice mode enabled"), std::string::npos);

    // /voice (toggle to off)
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/voice"));
    EXPECT_FALSE(host->is_voice_enabled(chat_id));
    auto sess4 = host->get_session(chat_id);
    ASSERT_TRUE(sess4.has_value());
    EXPECT_NE(sess4->messages.back().text.find("Voice mode disabled"), std::string::npos);

    // Case-insensitive /voice ON
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/voice ON"));
    EXPECT_TRUE(host->is_voice_enabled(chat_id));

    // Dash command: -voice off
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "-voice off"));
    EXPECT_FALSE(host->is_voice_enabled(chat_id));

    // Invalid argument
    EXPECT_TRUE(host->inject_incoming_message(chat_id, chat_id, "Tester", "/voice unknown_arg"));
    auto sess5 = host->get_session(chat_id);
    ASSERT_TRUE(sess5.has_value());
    EXPECT_NE(sess5->messages.back().text.find("Usage: /voice [on/off]"), std::string::npos);

    // Test send_voice_reply audio file synthesis
    host->set_voice_enabled(chat_id, true);
    // send_voice_reply synthesizes speech via say and converts to ogg/m4a, then invokes send_telegram_voice
    host->send_voice_reply(chat_id, "Hello, this is a test audio response synthesized via local say.");
    auto sess6 = host->get_session(chat_id);
    ASSERT_TRUE(sess6.has_value());
    ASSERT_FALSE(sess6->messages.empty());
    EXPECT_TRUE(sess6->messages.back().is_outgoing);
    EXPECT_NE(sess6->messages.back().text.find("Voice message"), std::string::npos);
#endif
}


