#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "../src/cards/information/adaptive_process_card.hpp"

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
