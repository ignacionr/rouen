#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "../src/models/adaptive_cards/adaptive_cards_repository.hpp"

using namespace rouen::models::adaptive_cards;

TEST(AdaptiveCardsRepository, SaveAndRetrieveCard) {
    std::filesystem::path temp_db = std::filesystem::temp_directory_path() / "test_adaptive_cards.db";
    std::filesystem::remove(temp_db);

    {
        adaptive_cards_repository repo(temp_db.string());

        adaptive_card_record card{};
        card.title = "Test Card";
        card.name = "test-card";
        card.card_json = R"({"type":"AdaptiveCard","body":[{"type":"TextBlock","text":"Hello World"}]})";
        card.context_json = R"({"key":"value"})";

        int id = repo.save_card(card);
        EXPECT_GT(id, 0);

        auto fetched = repo.get_card_by_name("test-card");
        ASSERT_TRUE(fetched.has_value());
        EXPECT_EQ(fetched->title, "Test Card");
        EXPECT_EQ(fetched->card_json, card.card_json);
        EXPECT_EQ(fetched->context_json, card.context_json);
    }

    std::filesystem::remove(temp_db);
}

TEST(AdaptiveCardsRepository, ExportAndImportDirectory) {
    std::filesystem::path temp_db1 = std::filesystem::temp_directory_path() / "test_ac_db1.db";
    std::filesystem::path temp_db2 = std::filesystem::temp_directory_path() / "test_ac_db2.db";
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "test_ac_export";

    std::filesystem::remove(temp_db1);
    std::filesystem::remove(temp_db2);
    std::filesystem::remove_all(temp_dir);

    {
        adaptive_cards_repository repo1(temp_db1.string());
        adaptive_card_record card1{0, "card-one", "Card One", R"({"type":"AdaptiveCard"})", "{}", "", ""};
        adaptive_card_record card2{0, "card-two", "Card Two", R"({"type":"AdaptiveCard"})", "{}", "", ""};
        repo1.save_card(card1);
        repo1.save_card(card2);

        repo1.export_to_directory(temp_dir);

        adaptive_cards_repository repo2(temp_db2.string());
        repo2.import_from_directory(temp_dir);

        auto list = repo2.list_cards();
        EXPECT_GE(list.size(), 2U);

        auto fetch1 = repo2.get_card_by_name("card-one");
        ASSERT_TRUE(fetch1.has_value());
        EXPECT_EQ(fetch1->title, "Card One");
    }

    std::filesystem::remove(temp_db1);
    std::filesystem::remove(temp_db2);
    std::filesystem::remove_all(temp_dir);
}
