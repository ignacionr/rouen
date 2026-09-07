#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <thread>
#include <glaze/json/json_t.hpp>

#include "cards/productivity/js_card.hpp"
#include "hosts/event_bus_host.hpp"
#include "hosts/quickjs_host.hpp"

using namespace rouen::hosts;
using namespace rouen::cards;
using namespace rouen::events;

class QuickJSHostTest : public ::testing::Test {
protected:
    void SetUp() override {
        quickjs_host::instance().initialize();
    }

    void TearDown() override {
        event_bus_host::instance().clear();
    }
};

TEST_F(QuickJSHostTest, HostInitialization) {
    EXPECT_TRUE(quickjs_host::instance().is_initialized());
}

TEST_F(QuickJSHostTest, BasicEvalScript) {
    std::string result = quickjs_host::instance().eval_script("2 + 2");
    EXPECT_NE(result.find("4"), std::string::npos);
}

TEST_F(QuickJSHostTest, GlazeMarshalingBridge) {
    glz::json_t input_json = glz::json_t::object_t{
        {"name", "Rouen"},
        {"version", 1.3},
        {"enabled", true}
    };

    auto& host = quickjs_host::instance();
    // Test call_function with glaze json args
    host.eval_script("function testFunc(obj) { return { received: obj.name, v: obj.version * 2 }; }");
    glz::json_t res = host.call_function("testFunc", input_json);

    std::string res_str{};
    glz::write_json(res, res_str);
    EXPECT_NE(res_str.find("Rouen"), std::string::npos);
    EXPECT_NE(res_str.find("2.6"), std::string::npos);
}

TEST_F(QuickJSHostTest, RouenNamespaceLogAndPlugins) {
    std::string res = quickjs_host::instance().eval_script("Rouen.log('Test log string'); Rouen.plugins.list()");
    EXPECT_FALSE(res.empty());
}

TEST_F(QuickJSHostTest, EventBusIntegration) {
    bool event_received = false;
    std::string received_topic{};

    event_bus_host::instance().subscribe("test:js:*", [&](const rouen_event& evt) {
        event_received = true;
        received_topic = evt.topic;
    });

    quickjs_host::instance().eval_script("Rouen.emit('test:js:ping', { msg: 'hello' });");
    event_bus_host::instance().dispatch_pending_events();

    EXPECT_TRUE(event_received);
    EXPECT_EQ(received_topic, "test:js:ping");
}

TEST_F(QuickJSHostTest, EventBusSubscriptionInJS) {
    quickjs_host::instance().eval_script(R"(
        Rouen.on("test:cpp:pong", function(evt) {
            Rouen.log("JS received topic: " + evt.topic);
            Rouen.emit("test:js:ack", { original: evt.topic });
        });
    )");

    bool ack_received = false;
    event_bus_host::instance().subscribe("test:js:ack", [&](const rouen_event& /*evt*/) {
        ack_received = true;
    });

    event_bus_host::instance().publish(rouen_event{
        .topic = "test:cpp:pong",
        .source_id = "cpp_test",
        .payload = glz::json_t::object_t{{"test", 123}}
    });

    event_bus_host::instance().dispatch_pending_events();
    quickjs_host::instance().execute_pending_jobs();
    event_bus_host::instance().dispatch_pending_events();

    EXPECT_TRUE(ack_received);
}

TEST_F(QuickJSHostTest, JSCardAdapterRender) {
    js_card card_instance{"function onRender() { return { type: 'AdaptiveCard', body: [{ type: 'TextBlock', text: 'JS Test' }] }; }"};
    EXPECT_EQ(card_instance.get_uri(), "js:function onRender() { return { type: 'AdaptiveCard', body: [{ type: 'TextBlock', text: 'JS Test' }] }; }");
}

TEST_F(QuickJSHostTest, TimeoutGuardEnforcement) {
    // Evaluating infinite loop should be interrupted by 3s timeout guard
    auto start_time = std::chrono::steady_clock::now();
    std::string res = quickjs_host::instance().eval_script("while(true) {}");
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time);

    EXPECT_GE(duration.count(), 3);
    EXPECT_NE(res.find("interrupted"), std::string::npos);
}
