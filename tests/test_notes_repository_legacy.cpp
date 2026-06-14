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

    std::cout << "notes_repository legacy tests passed" << std::endl;
    std::filesystem::remove_all(test_root);
    return 0;
}
