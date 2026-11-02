#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "../src/cards/information/adaptive_process_card.hpp"
#include "../src/cards/interface/deck.hpp"

TEST(AdaptiveProcessCard, RefreshesUIOnSecondStdoutLineWithoutAction) {
    // Construct a command that prints two card JSON documents separated by a delay and newline
    std::string const cmd = 
        "python3 -c \"import time, sys; "
        "sys.stdout.write('{\\\"type\\\":\\\"AdaptiveCard\\\",\\\"body\\\":[{\\\"type\\\":\\\"TextBlock\\\",\\\"text\\\":\\\"Initial Card\\\"}]}\\n'); "
        "sys.stdout.flush(); "
        "time.sleep(0.2); "
        "sys.stdout.write('{\\\"type\\\":\\\"AdaptiveCard\\\",\\\"body\\\":[{\\\"type\\\":\\\"TextBlock\\\",\\\"text\\\":\\\"Updated Second Card\\\"}]}\\n'); "
        "sys.stdout.flush()\"";

    rouen::cards::adaptive_process_card card{cmd};

    // Wait for initial card to be received on stdout
    auto start = std::chrono::steady_clock::now();
    while (!card.has_pending_update() && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ASSERT_TRUE(card.has_pending_update()) << "Timed out waiting for initial card stdout line";
    card.apply_pending_update();

    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Initial Card");

    // Wait for second card stdout line (printed automatically by process, without any stdin action)
    start = std::chrono::steady_clock::now();
    while (!card.has_pending_update() && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ASSERT_TRUE(card.has_pending_update()) << "Timed out waiting for second card stdout line";
    card.apply_pending_update();

    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Updated Second Card");
}

TEST(AdaptiveProcessCard, HeadlessGetAdaptiveCardJsonAndHandleActionWithoutRender) {
    std::string const cmd =
        "python3 -c \"import json, sys; "
        "print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':150,'body':[{'type':'TextBlock','text':'Choke: 42%'}]}), flush=True); "
        "line = sys.stdin.readline(); "
        "msg = json.loads(line.strip()); "
        "val = msg.get('data', msg).get('choke', 0); "
        "print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':150,'body':[{'type':'TextBlock','text':f'Choke: {val}%'}]}), flush=True)\"";

    rouen::cards::adaptive_process_card card{cmd};

    // Without calling render() or apply_pending_update() manually, get_adaptive_card_json()
    // should wait briefly for initial output and populate bound_document()
    std::string initial_json = card.get_adaptive_card_json();
    ASSERT_FALSE(initial_json.empty());
    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Choke: 42%");
    EXPECT_EQ(card.bound_document().refreshIntervalMs, 150);

    // Send an action via handle_action() (as used by POST /api/cards/action in --daemon mode)
    card.handle_action(R"({"verb":"set_choke","data":{"choke":48}})");

    auto start = std::chrono::steady_clock::now();
    while (card.bound_document().body[0].text != "Choke: 48%" &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (void)card.get_adaptive_card_json();
    }

    EXPECT_EQ(card.bound_document().body[0].text, "Choke: 48%");
}

TEST(AdaptiveProcessCard, HeadlessPeriodicTickViaRefreshIntervalMs) {
    std::string const cmd =
        "python3 -c \"\n"
        "import json, sys\n"
        "n = 0\n"
        "print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':80,'body':[{'type':'TextBlock','text':f'Tick {n}'}]}), flush=True)\n"
        "for line in sys.stdin:\n"
        "    msg = json.loads(line.strip())\n"
        "    if msg.get('verb') == 'tick':\n"
        "        n += 1\n"
        "        print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':80,'body':[{'type':'TextBlock','text':f'Tick {n}'}]}), flush=True)\n"
        "        if n >= 1:\n"
        "            break\n"
        "\"";

    rouen::cards::adaptive_process_card card{cmd};
    (void)card.get_adaptive_card_json();
    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Tick 0");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    card.tick_background();

    auto start = std::chrono::steady_clock::now();
    while (card.bound_document().body[0].text != "Tick 1" &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        card.tick_background();
    }

    EXPECT_EQ(card.bound_document().body[0].text, "Tick 1");
}

TEST(AdaptiveProcessCard, HeadlessDeckCreatesAndTicksCardWithoutImGuiContext) {
    deck::no_initial_cards = true;
    deck daemon_deck{nullptr};

    auto create_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
    auto get_cards_fn = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
    ASSERT_NE(create_fn, nullptr);
    ASSERT_NE(get_cards_fn, nullptr);

    std::string const uri =
        "adaptive-process:python3 -c \"import json; print(json.dumps({'type':'AdaptiveCard','body':[{'type':'TextBlock','text':'Daemon Deck OK'}]}), flush=True)\"";
    (*create_fn)(uri);
    daemon_deck.tick_headless();

    auto cards = (*get_cards_fn)();
    bool found = false;
    for (auto const& c : cards) {
        if (c && c->get_uri() == uri) {
            found = true;
            std::string json_out = c->get_adaptive_card_json();
            EXPECT_NE(json_out.find("Daemon Deck OK"), std::string::npos);
        }
    }
    EXPECT_TRUE(found);
}
