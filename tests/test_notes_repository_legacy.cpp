#include <cassert>
#include <filesystem>
#include <iostream>

#include "../src/models/notes/notes_repository.hpp"

int main() {
    const auto test_root = std::filesystem::temp_directory_path() / "rouen_notes_repo_test";
    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(test_root);

    const auto db_path = (test_root / "notes.db").string();

    rouen::models::notes::notes_repository repo(db_path);

    const int alpha_id = repo.save_note("Alpha", "Link to [[Beta]]", "one,two");
    const int beta_id = repo.save_note("Beta", "Link back to [[Alpha]]", "two");

    assert(alpha_id > 0);
    assert(beta_id > 0);

    const auto alpha = repo.get_note_by_title("Alpha");
    assert(alpha.has_value());
    assert(alpha->tags == "one,two");

    const auto links = rouen::models::notes::notes_repository::parse_wiki_links("See [[One]] and [[Two]] and [[One]]");
    assert(links.size() == 2);
    assert(links[0] == "One");
    assert(links[1] == "Two");

    const auto backlinks = repo.backlinks_for_title("Alpha");
    assert(backlinks.size() == 1);
    assert(backlinks.front().title == "Beta");

    const auto found = repo.list_notes("Beta", "");
    assert(!found.empty());

    const bool deleted = repo.delete_note(beta_id);
    assert(deleted);
    assert(!repo.get_note_by_title("Beta").has_value());

    // Test 1: CRLF line endings and trailing whitespace normalization
    {
        const auto sync_dir = test_root / "sync_crlf";
        std::filesystem::create_directories(sync_dir);

        // Alpha was saved with content "Link to [[Beta]]"
        // Simulate a peer writing CRLF with trailing newlines
        {
            std::ofstream out(sync_dir / "alpha.md", std::ios::binary);
            out << "# Alpha\r\n\r\n<!-- tags:one,two -->\r\n\r\nLink to [[Beta]]\r\n\r\n";
        }

        // Simulate stale notes_last_sync from months ago
        repo.set_sync_meta("notes_last_sync", "2020-01-01 00:00:00");

        repo.import_from_directory(sync_dir);

        // Alpha should not have spawned a conflict note!
        const auto all_notes = repo.list_notes("", "");
        for (const auto& n : all_notes) {
            assert(n.title.find("conflict") == std::string::npos);
        }
        const auto alpha_after = repo.get_note_by_title("Alpha");
        assert(alpha_after.has_value());
        assert(alpha_after->content == "Link to [[Beta]]");
    }

    // Test 2: Recursive conflict loop prevention
    {
        const auto conflict_dir = test_root / "sync_conflict";
        std::filesystem::create_directories(conflict_dir);

        const std::string conflict_title = "MyNote (conflict 2026-10-04 15-18-06)";
        repo.save_note(conflict_title, "Local conflicting body", "");
        repo.set_sync_meta("notes_last_sync", "2020-01-01 00:00:00");

        // Remote peer has a different version of this conflict note
        {
            std::ofstream out(conflict_dir / "mynote-conflict-2026-10-04-15-18-06.md", std::ios::binary);
            out << "# MyNote (conflict 2026-10-04 15-18-06)\n\nRemote different body\n";
        }

        repo.import_from_directory(conflict_dir);

        // Should NOT spawn nested conflict title like "(conflict ...) (conflict ...)"
        const auto all_notes = repo.list_notes("", "");
        for (const auto& n : all_notes) {
            size_t first = n.title.find("conflict");
            if (first != std::string::npos) {
                size_t second = n.title.find("conflict", first + 1);
                assert(second == std::string::npos); // Must NOT have recursive conflict!
            }
        }
    }

    // Test 3: notes_last_sync metadata maintenance
    {
        const auto sync_meta_dir = test_root / "sync_meta";
        std::filesystem::create_directories(sync_meta_dir);
        repo.set_sync_meta("notes_last_sync", "2020-01-01 00:00:00");

        repo.import_from_directory(sync_meta_dir);
        const auto last_sync = repo.get_sync_meta("notes_last_sync");
        assert(!last_sync.empty());
        assert(last_sync != "2020-01-01 00:00:00");
    }

    // Test 4: Repeated import/export of unchanged notes preserves updated_at, avoids SQLite ID churn,
    // preserves local edits against stale staging files, and purges orphaned/conflict files.
    {
        const auto roundtrip_dir = test_root / "sync_roundtrip";
        std::filesystem::create_directories(roundtrip_dir);

        // Save a note with leading newlines and embedded tags comment
        repo.save_note("Personal Data", "\n\n\n<!-- tags:personal, data -->\n\nThe user is Ignacio.", "");
        const auto pd_note = repo.get_note_by_title("Personal Data");
        assert(pd_note.has_value());
        assert(pd_note->tags == "personal, data");
        assert(pd_note->content == "The user is Ignacio.");

        // Force an older updated_at timestamp on Personal Data in SQLite
        repo.export_to_directory(roundtrip_dir);

        // Conflict note should NOT be exported to roundtrip_dir
        assert(!std::filesystem::exists(roundtrip_dir / "mynote-conflict-2026-10-04-15-18-06.md"));
        assert(std::filesystem::exists(roundtrip_dir / "personal-data.md"));

        // Capture file mtime before re-export
        const auto mtime_before = std::filesystem::last_write_time(roundtrip_dir / "personal-data.md");
        repo.export_to_directory(roundtrip_dir);
        const auto mtime_after = std::filesystem::last_write_time(roundtrip_dir / "personal-data.md");
        assert(mtime_before == mtime_after);

        // Edit Personal Data locally in SQLite without re-exporting yet;
        // importing the stale staging directory must NOT overwrite the newer local edit!
        repo.save_note("Personal Data", "The user is Ignacio Rodriguez.", "personal, data");
        repo.import_from_directory(roundtrip_dir);
        const auto pd_after_stale_import = repo.get_note_by_title("Personal Data");
        assert(pd_after_stale_import.has_value());
        assert(pd_after_stale_import->content == "The user is Ignacio Rodriguez.");

        // Deleting a note and exporting must remove its orphaned .md file from the staging directory
        assert(repo.delete_note(pd_after_stale_import->id));
        repo.export_to_directory(roundtrip_dir);
        assert(!std::filesystem::exists(roundtrip_dir / "personal-data.md"));

        // purge_conflict_notes removes any remaining conflict notes from the database
        const int purged = repo.purge_conflict_notes();
        assert(purged >= 1);
        assert(!repo.get_note_by_title("MyNote (conflict 2026-10-04 15-18-06)").has_value());
    }

    std::cout << "notes_repository legacy tests passed" << std::endl;
    std::filesystem::remove_all(test_root);
    return 0;
}
