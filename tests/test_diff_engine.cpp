#include <gtest/gtest.h>
#include "../src/helpers/diff_engine.hpp"

using namespace rouen::helpers;

TEST(DiffEngineTest, IdenticalContentYieldsNoChanges) {
    std::string text = "int main() {\n    return 0;\n}\n";
    auto diff = DiffEngine::compute_diff(text, text, "test.cpp", "test.cpp");
    EXPECT_FALSE(diff.has_changes);
    EXPECT_EQ(diff.total_additions, 0);
    EXPECT_EQ(diff.total_deletions, 0);
    EXPECT_TRUE(diff.chunks.empty());
}

TEST(DiffEngineTest, SimpleAdditionAndDeletion) {
    std::string old_text = "line 1\nline 2\nline 3\n";
    std::string new_text = "line 1\nline 2 modified\nline 3\n";

    auto diff = DiffEngine::compute_diff(old_text, new_text, "old.txt", "new.txt", 1);
    EXPECT_TRUE(diff.has_changes);
    EXPECT_EQ(diff.total_additions, 1);
    EXPECT_EQ(diff.total_deletions, 1);
    EXPECT_EQ(diff.chunks.size(), 1);

    const auto& chunk = diff.chunks[0];
    EXPECT_EQ(chunk.additions_count(), 1);
    EXPECT_EQ(chunk.deletions_count(), 1);
    EXPECT_EQ(chunk.old_start_line, 1);
    EXPECT_EQ(chunk.new_start_line, 1);
}

TEST(DiffEngineTest, PureInsertionAndPureDeletion) {
    std::string empty = "";
    std::string content = "alpha\nbeta\ngamma\n";

    // Pure insertion
    auto diff_ins = DiffEngine::compute_diff(empty, content);
    EXPECT_TRUE(diff_ins.has_changes);
    EXPECT_EQ(diff_ins.total_additions, 3);
    EXPECT_EQ(diff_ins.total_deletions, 0);

    // Pure deletion
    auto diff_del = DiffEngine::compute_diff(content, empty);
    EXPECT_TRUE(diff_del.has_changes);
    EXPECT_EQ(diff_del.total_additions, 0);
    EXPECT_EQ(diff_del.total_deletions, 3);
}

TEST(DiffEngineTest, MultipleHunksSeparation) {
    // 2 edits separated by 10 unchanged lines with context = 2
    std::string old_text = 
        "H1_A\n"
        "gap 1\ngap 2\ngap 3\ngap 4\ngap 5\n"
        "gap 6\ngap 7\ngap 8\ngap 9\ngap 10\n"
        "H2_A\n";
    std::string new_text = 
        "H1_MODIFIED\n"
        "gap 1\ngap 2\ngap 3\ngap 4\ngap 5\n"
        "gap 6\ngap 7\ngap 8\ngap 9\ngap 10\n"
        "H2_MODIFIED\n";

    auto diff = DiffEngine::compute_diff(old_text, new_text, "a", "b", 2);
    EXPECT_TRUE(diff.has_changes);
    EXPECT_EQ(diff.chunks.size(), 2);
    EXPECT_EQ(diff.chunks[0].id, 1);
    EXPECT_EQ(diff.chunks[1].id, 2);
}

TEST(DiffEngineTest, SelectiveChunkApply) {
    std::string old_text = 
        "Section 1\n"
        "gap 1\ngap 2\ngap 3\ngap 4\ngap 5\n"
        "gap 6\ngap 7\ngap 8\ngap 9\ngap 10\n"
        "Section 2\n";
    std::string new_text = 
        "Section 1 Modified\n"
        "gap 1\ngap 2\ngap 3\ngap 4\ngap 5\n"
        "gap 6\ngap 7\ngap 8\ngap 9\ngap 10\n"
        "Section 2 Modified\n";

    auto diff = DiffEngine::compute_diff(old_text, new_text, "a", "b", 2);
    ASSERT_EQ(diff.chunks.size(), 2);

    // Case 1: Accept chunk 1, discard chunk 2
    diff.chunks[0].status = ChunkStatus::Accepted;
    diff.chunks[1].status = ChunkStatus::Discarded;

    std::string applied_1 = DiffEngine::apply_selected_chunks(old_text, diff);
    EXPECT_NE(applied_1.find("Section 1 Modified"), std::string::npos);
    EXPECT_EQ(applied_1.find("Section 2 Modified"), std::string::npos);
    EXPECT_NE(applied_1.find("Section 2\n"), std::string::npos);

    // Case 2: Discard chunk 1, accept chunk 2
    diff.chunks[0].status = ChunkStatus::Discarded;
    diff.chunks[1].status = ChunkStatus::Accepted;

    std::string applied_2 = DiffEngine::apply_selected_chunks(old_text, diff);
    EXPECT_EQ(applied_2.find("Section 1 Modified"), std::string::npos);
    EXPECT_NE(applied_2.find("Section 1\n"), std::string::npos);
    EXPECT_NE(applied_2.find("Section 2 Modified"), std::string::npos);

    // Case 3: Accept all
    diff.accept_all();
    std::string applied_all = DiffEngine::apply_selected_chunks(old_text, diff);
    EXPECT_NE(applied_all.find("Section 1 Modified"), std::string::npos);
    EXPECT_NE(applied_all.find("Section 2 Modified"), std::string::npos);

    // Case 4: Discard all
    diff.discard_all();
    std::string applied_none = DiffEngine::apply_selected_chunks(old_text, diff);
    EXPECT_EQ(applied_none, old_text);
}

TEST(DiffEngineTest, UnifiedDiffFormatAndParseRoundtrip) {
    std::string old_text = "int a = 1;\nint b = 2;\nreturn a + b;\n";
    std::string new_text = "int a = 10;\nint b = 2;\nint c = 3;\nreturn a + b + c;\n";

    auto diff = DiffEngine::compute_diff(old_text, new_text, "src/math.cpp", "src/math.cpp", 1);
    std::string unified = DiffEngine::to_unified_diff(diff);

    EXPECT_NE(unified.find("--- src/math.cpp"), std::string::npos);
    EXPECT_NE(unified.find("+++ src/math.cpp"), std::string::npos);
    EXPECT_NE(unified.find("@@"), std::string::npos);
    EXPECT_NE(unified.find("+int a = 10;"), std::string::npos);
    EXPECT_NE(unified.find("-int a = 1;"), std::string::npos);

    // Parse back
    auto parsed = DiffEngine::parse_unified_diff(unified);
    EXPECT_TRUE(parsed.has_changes);
    EXPECT_EQ(parsed.total_additions, diff.total_additions);
    EXPECT_EQ(parsed.total_deletions, diff.total_deletions);
    EXPECT_EQ(parsed.chunks.size(), diff.chunks.size());
}

#include "../src/helpers/code_editor_service.hpp"

TEST(DiffEngineTest, UndoAndRedoStack) {
    auto& editor = CodeEditorService::instance();
    editor.clear_history();

    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "rouen_undo_test";
    std::filesystem::create_directories(temp_dir);
    std::string test_file = (temp_dir / "undo_sample.txt").string();

    // Initial write
    std::string initial_content = "version 1\n";
    auto w1 = editor.write_file(test_file, initial_content, true);
    ASSERT_TRUE(w1.success);

    // Second write
    std::string v2_content = "version 2\n";
    auto w2 = editor.write_file(test_file, v2_content, true);
    ASSERT_TRUE(w2.success);

    // Verify history recorded
    EXPECT_TRUE(editor.can_undo());
    EXPECT_FALSE(editor.can_redo());
    auto history = editor.get_history();
    ASSERT_GE(history.size(), 2);
    EXPECT_EQ(history[0].file_path, test_file);

    // Perform Undo
    bool undone = editor.undo();
    EXPECT_TRUE(undone);
    EXPECT_TRUE(editor.can_redo());

    // File on disk should now be back to version 1
    auto r1 = editor.read_file(test_file, 1, -1, false);
    EXPECT_EQ(r1.content, initial_content);

    // Perform Redo
    bool redone = editor.redo();
    EXPECT_TRUE(redone);
    auto r2 = editor.read_file(test_file, 1, -1, false);
    EXPECT_EQ(r2.content, v2_content);

    // Cleanup
    std::filesystem::remove(test_file);
}

TEST(DiffEngineTest, StagingBufferAndSelectiveApply) {
    auto& editor = CodeEditorService::instance();
    editor.clear_staged_edits();

    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "rouen_staging_test";
    std::filesystem::create_directories(temp_dir);
    std::string test_file = (temp_dir / "stage_sample.txt").string();

    std::string base = "line 1\nline 2\nline 3\n";
    editor.write_file(test_file, base, true);

    // Stage patch (does NOT touch disk yet)
    std::string staged_id = editor.stage_patch(test_file, "line 2", "line 2 proposed", 1, -1, false, "", "AI Proposed Fix");
    EXPECT_FALSE(staged_id.empty());
    EXPECT_TRUE(editor.has_staged_edits());

    // Verify file on disk is still original
    auto r_before = editor.read_file(test_file, 1, -1, false);
    EXPECT_EQ(r_before.content, base);

    // Verify staged edit contents
    auto staged_list = editor.get_staged_edits();
    ASSERT_EQ(staged_list.size(), 1);
    EXPECT_EQ(staged_list[0].id, staged_id);
    EXPECT_TRUE(staged_list[0].diff.has_changes);

    // Apply staged edit
    bool applied = editor.apply_staged_edit(staged_id, false);
    EXPECT_TRUE(applied);
    EXPECT_FALSE(editor.has_staged_edits());

    // Verify file on disk is now updated
    auto r_after = editor.read_file(test_file, 1, -1, false);
    EXPECT_NE(r_after.content.find("line 2 proposed"), std::string::npos);

    // Cleanup
    std::filesystem::remove(test_file);
}
