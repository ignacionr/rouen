/**
 * Google Test for Objectives Tracking
 * Purpose: Test objective hierarchy, temporal gating, morning commits, evening rollovers, and forgiveness buffers.
 * Category: Productivity Feature Testing
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <memory>

#include "../src/models/productivity/objective_repository.hpp"

class ObjectivesTest : public ::testing::Test {
protected:
    std::filesystem::path temp_db_path;

    void SetUp() override {
        // Create isolated test database path
        auto test_root = std::filesystem::temp_directory_path() / "test_objectives_home";
        std::filesystem::remove_all(test_root);
        std::filesystem::create_directories(test_root);
        temp_db_path = test_root / "objectives_test.db";
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_db_path.parent_path());
    }
};

TEST_F(ObjectivesTest, HierarchyAndStateTransitions) {
    using namespace rouen::models::productivity;
    
    objective_repository repo(temp_db_path.string());
    auto date_ctx = objective_repository::get_current_date_context();

    // 1. Add Quarterly vision
    objective_record q_rec;
    q_rec.period = "quarterly";
    q_rec.period_identifier = date_ctx.quarter;
    q_rec.title = " Rouen Networking Version 1.0";
    q_rec.type = "binary";
    q_rec.target_val = 1.0;
    q_rec.current_val = 0.0;
    q_rec.status = "committed";
    int q_id = repo.add_objective(q_rec);
    EXPECT_GT(q_id, 0);

    // 2. Add Monthly milestone child
    objective_record m_rec;
    m_rec.parent_id = q_id;
    m_rec.period = "monthly";
    m_rec.period_identifier = date_ctx.month;
    m_rec.title = "Release Watchlist and RSS fixes";
    m_rec.type = "binary";
    m_rec.target_val = 1.0;
    m_rec.current_val = 0.0;
    m_rec.status = "committed";
    int m_id = repo.add_objective(m_rec);
    EXPECT_GT(m_id, 0);

    // 3. Add Weekly sprint goal child
    objective_record w_rec;
    w_rec.parent_id = m_id;
    w_rec.period = "weekly";
    w_rec.period_identifier = date_ctx.week;
    w_rec.title = "Analyze BBC feed issue and implement watermark completion reset";
    w_rec.type = "binary";
    w_rec.target_val = 1.0;
    w_rec.current_val = 0.0;
    w_rec.status = "committed";
    int w_id = repo.add_objective(w_rec);
    EXPECT_GT(w_id, 0);

    // 4. Staging Area: Pull Daily child micro-actions (pending)
    objective_record d_rec;
    d_rec.parent_id = w_id;
    d_rec.period = "daily";
    d_rec.period_identifier = date_ctx.date;
    d_rec.title = "Write 5 clean unit tests for watermarks";
    d_rec.type = "volumetric";
    d_rec.target_val = 5.0;
    d_rec.current_val = 0.0;
    d_rec.status = "pending";
    int d_id = repo.add_objective(d_rec);
    EXPECT_GT(d_id, 0);

    // Verify daily item is pending in staging
    auto staged_items = repo.get_objectives("daily", date_ctx.date);
    ASSERT_EQ(staged_items.size(), 1);
    EXPECT_EQ(staged_items[0].status, "pending");

    // 5. Commit Staging Goals (Locks them in)
    repo.commit_day_objectives(date_ctx.date);

    // Verify it is now committed
    auto committed_items = repo.get_objectives("daily", date_ctx.date);
    ASSERT_EQ(committed_items.size(), 1);
    EXPECT_EQ(committed_items[0].status, "committed");

    // 6. Update metrics during the day
    auto active_item = committed_items[0];
    active_item.current_val = 3.0;
    repo.update_objective(active_item);

    // Verify value updated
    auto updated = repo.get_objective_by_id(d_id);
    EXPECT_DOUBLE_EQ(updated.current_val, 3.0);
}

TEST_F(ObjectivesTest, CloseDayWithRolloversAndForgiveness) {
    using namespace rouen::models::productivity;
    
    objective_repository repo(temp_db_path.string());
    auto yesterday_ctx = objective_repository::get_yesterday_date_context();
    auto today_ctx = objective_repository::get_current_date_context();
    auto tomorrow_ctx = objective_repository::get_tomorrow_date_context();

    // Setup yesterday's unclosed state
    objective_record d_rec;
    d_rec.period = "daily";
    d_rec.period_identifier = yesterday_ctx.date;
    d_rec.title = "Review 5 PRs";
    d_rec.type = "volumetric";
    d_rec.target_val = 5.0;
    d_rec.current_val = 2.0; // Incomplete (2/5)
    d_rec.status = "committed";
    int d_id = repo.add_objective(d_rec);
    
    repo.initialize_day_ledger(yesterday_ctx.date);

    // Verify forgiveness buffer detects yesterday is unclosed
    std::string unclosed = repo.get_unclosed_day_before(today_ctx.date);
    EXPECT_EQ(unclosed, yesterday_ctx.date);
    EXPECT_FALSE(repo.is_day_closed(yesterday_ctx.date));

    // Close yesterday with a push rollover to tomorrow (which is today in the staging context)
    std::vector<std::pair<int, std::string>> rollovers = {{d_id, "push"}};
    repo.close_day(yesterday_ctx.date, rollovers);

    // Verify yesterday is now closed
    EXPECT_TRUE(repo.is_day_closed(yesterday_ctx.date));
    EXPECT_EQ(repo.get_objective_by_id(d_id).status, "dropped"); // Marked dropped on yesterday, rolled over

    // Verify rollover created a new pending objective for tomorrow (today)
    auto tomorrow_items = repo.get_objectives("daily", tomorrow_ctx.date);
    ASSERT_EQ(tomorrow_items.size(), 1);
    EXPECT_EQ(tomorrow_items[0].title, "Review 5 PRs");
    EXPECT_EQ(tomorrow_items[0].status, "pending");
    EXPECT_DOUBLE_EQ(tomorrow_items[0].current_val, 0.0);
}
