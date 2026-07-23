#include <cassert>
#include <filesystem>
#include <iostream>

#include "../src/models/series/series_repository.hpp"

int main() {
    const auto test_root = std::filesystem::temp_directory_path() / "rouen_series_repo_test";
    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(test_root);

    const auto db_path = (test_root / "series.db").string();

    rouen::models::series::series_repository repo(db_path);

    // Initial seed check
    auto all_series = repo.list_series();
    assert(all_series.size() == 3);

    // Get default series
    auto sales = repo.get_series_by_name("sales");
    assert(sales.has_value());
    assert(sales->title == "Monthly Sales Revenue");
    assert(sales->unit == "$");
    assert(sales->points.size() == 12);

    // Create custom series
    rouen::models::series::series_record new_series{
        0,
        "quarterly_targets",
        "Quarterly Targets",
        "units",
        {
            {"Q1", 100.0f},
            {"Q2", 150.0f},
            {"Q3", 120.0f},
            {"Q4", 200.0f}
        },
        true,
        3
    };

    int saved_id = repo.save_series(new_series);
    assert(saved_id > 0);

    auto fetched = repo.get_series_by_name("quarterly_targets");
    assert(fetched.has_value());
    assert(fetched->id == saved_id);
    assert(fetched->points.size() == 4);
    assert(fetched->points[3].label == "Q4");
    assert(fetched->points[3].value == 200.0f);

    // Update series
    fetched->points.push_back({"Q5 Bonus", 250.0f});
    fetched->color_index = 5;
    repo.save_series(*fetched);

    auto updated = repo.get_series_by_name("quarterly_targets");
    assert(updated.has_value());
    assert(updated->points.size() == 5);
    assert(updated->color_index == 5);

    // Export and Import check
    const auto export_dir = test_root / "export";
    repo.export_to_directory(export_dir);
    assert(std::filesystem::exists(export_dir / "quarterly-targets.json"));

    // Delete series
    bool deleted = repo.delete_series("quarterly_targets");
    assert(deleted);
    assert(!repo.get_series_by_name("quarterly_targets").has_value());

    // Re-import
    repo.import_from_directory(export_dir);
    assert(repo.get_series_by_name("quarterly_targets").has_value());

    std::cout << "series_repository tests passed successfully!" << std::endl;
    std::filesystem::remove_all(test_root);
    return 0;
}
