/**
 * Test: Event Bus Infrastructure
 * Purpose: Verifies thread-safe event publishing, wildcard topic matching, frame dispatching, and RAII auto-unsubscription.
 */

#include "../src/hosts/event_bus_host.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <atomic>
#include <vector>

namespace test_helpers {
    void assert_true(bool condition, const std::string& test_name) {
        if (condition) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED\n";
            exit(1);
        }
    }
}

void test_topic_matching() {
    std::cout << "\n--- Testing Topic Pattern Matching ---\n";
    using rouen::hosts::topic_matches;

    test_helpers::assert_true(topic_matches("*", "card:pomodoro:finished"), "Wildcard * matches any topic");
    test_helpers::assert_true(topic_matches("card:pomodoro:finished", "card:pomodoro:finished"), "Exact topic matches");
    test_helpers::assert_true(!topic_matches("card:pomodoro:finished", "card:pomodoro:started"), "Non-matching topic fails");
    test_helpers::assert_true(topic_matches("host:*", "host:process:exited"), "Prefix wildcard host:* matches host:process:exited");
    test_helpers::assert_true(topic_matches("host:*", "host:rss:feed_updated"), "Prefix wildcard host:* matches host:rss:feed_updated");
    test_helpers::assert_true(!topic_matches("host:*", "card:pomodoro:finished"), "Prefix wildcard host:* rejects card:pomodoro:finished");
    test_helpers::assert_true(topic_matches("*:exited", "host:process:exited"), "Suffix wildcard *:exited matches host:process:exited");
}

void test_pub_sub_dispatch() {
    std::cout << "\n--- Testing Pub/Sub & Frame Dispatch ---\n";
    auto& bus = rouen::hosts::event_bus_host::instance();
    bus.clear();

    std::atomic<int> pomodoro_count{0};
    std::atomic<int> host_count{0};
    std::atomic<int> total_count{0};

    auto sub1 = bus.subscribe("card:pomodoro:*", [&](const rouen::events::rouen_event& evt) {
        pomodoro_count++;
    });

    auto sub2 = bus.subscribe("host:*", [&](const rouen::events::rouen_event& evt) {
        host_count++;
    });

    auto sub3 = bus.subscribe("*", [&](const rouen::events::rouen_event& evt) {
        total_count++;
    });

    test_helpers::assert_true(bus.subscriber_count() == 3, "Registered 3 subscribers");

    // Publish events asynchronously off main thread
    bus.publish({.topic = "card:pomodoro:finished", .source_id = "pomodoro_card"});
    bus.publish({.topic = "host:process:exited", .source_id = "process_host"});
    bus.publish({.topic = "system:card_opened", .source_id = "deck"});

    test_helpers::assert_true(bus.pending_event_count() == 3, "3 events pending before dispatch");

    // Dispatch events on main thread
    bus.dispatch_pending_events();

    test_helpers::assert_true(bus.pending_event_count() == 0, "0 events pending after dispatch");
    test_helpers::assert_true(pomodoro_count == 1, "Pomodoro callback invoked 1 time");
    test_helpers::assert_true(host_count == 1, "Host callback invoked 1 time");
    test_helpers::assert_true(total_count == 3, "Wildcard callback invoked 3 times");

    // Cleanup subscriptions
    bus.unsubscribe(sub1);
    bus.unsubscribe(sub2);
    bus.unsubscribe(sub3);
    test_helpers::assert_true(bus.subscriber_count() == 0, "All subscriptions removed");
}

void test_raii_scoped_subscription() {
    std::cout << "\n--- Testing RAII Scoped Subscription ---\n";
    auto& bus = rouen::hosts::event_bus_host::instance();
    bus.clear();

    std::atomic<int> count{0};
    {
        rouen::hosts::scoped_event_subscription scoped_sub(
            bus.subscribe("test:topic", [&](const rouen::events::rouen_event&) { count++; })
        );
        test_helpers::assert_true(bus.subscriber_count() == 1, "Subscription active inside scope");

        bus.publish({.topic = "test:topic"});
        bus.dispatch_pending_events();
        test_helpers::assert_true(count == 1, "Callback invoked while scope active");
    }

    test_helpers::assert_true(bus.subscriber_count() == 0, "Subscription automatically removed on scope exit");

    bus.publish({.topic = "test:topic"});
    bus.dispatch_pending_events();
    test_helpers::assert_true(count == 1, "Callback NOT invoked after scope exit");
}

void test_multithreaded_publish() {
    std::cout << "\n--- Testing Multi-Threaded Publish ---\n";
    auto& bus = rouen::hosts::event_bus_host::instance();
    bus.clear();

    std::atomic<int> event_counter{0};
    auto sub = bus.subscribe("mt:*", [&](const rouen::events::rouen_event&) {
        event_counter++;
    });

    constexpr int threads_cnt = 4;
    constexpr int events_per_thread = 100;
    std::vector<std::thread> workers;

    for (int t = 0; t < threads_cnt; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < events_per_thread; ++i) {
                bus.publish({.topic = "mt:worker_event", .source_id = std::to_string(t)});
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    test_helpers::assert_true(bus.pending_event_count() == threads_cnt * events_per_thread, "All MT events queued cleanly");

    bus.dispatch_pending_events();
    test_helpers::assert_true(event_counter == threads_cnt * events_per_thread, "All MT events dispatched cleanly");

    bus.clear();
}

int main() {
    std::cout << "Rouen Event Bus Infrastructure Unit Tests\n";
    std::cout << "========================================\n";

    try {
        test_topic_matching();
        test_pub_sub_dispatch();
        test_raii_scoped_subscription();
        test_multithreaded_publish();

        std::cout << "\n" << std::string(40, '=') << "\n";
        std::cout << "✅ All Event Bus tests passed successfully!\n";
        std::cout << std::string(40, '=') << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
