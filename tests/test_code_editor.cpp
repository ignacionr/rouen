#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "../src/helpers/code_editor_service.hpp"

namespace test_helpers {
    inline void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "❌ Assertion failed: " << message << "\n";
            std::exit(1);
        } else {
            std::cout << "✅ " << message << ": PASSED\n";
        }
    }

    inline void assert_string_equal(const std::string& expected, const std::string& actual, const std::string& message) {
        if (expected != actual) {
            std::cerr << "❌ Assertion failed: " << message << " (expected: '" << expected << "', got: '" << actual << "')\n";
            std::exit(1);
        } else {
            std::cout << "✅ " << message << ": PASSED\n";
        }
    }
}

void test_read_file() {
    std::cout << "\n--- Testing CodeEditorService::read_file ---\n";
    std::string test_path = "/tmp/test_code_editor_read.txt";
    std::filesystem::remove(test_path);

    {
        std::ofstream out(test_path);
        for (int i = 1; i <= 20; ++i) {
            out << "Line content " << i << "\n";
        }
    }

    auto& editor = rouen::helpers::CodeEditorService::instance();

    // Full read with line numbers
    auto full_res = editor.read_file(test_path, 1, 20, true);
    test_helpers::assert_true(full_res.success, "read_file succeeded");
    test_helpers::assert_true(full_res.total_lines == 20, "total_lines is 20");
    test_helpers::assert_true(full_res.content.find("1: Line content 1\n") != std::string::npos, "Has line 1 prefix");
    test_helpers::assert_true(full_res.content.find("20: Line content 20\n") != std::string::npos, "Has line 20 prefix");

    // Slice read (lines 5 to 8) without line numbers
    auto slice_res = editor.read_file(test_path, 5, 8, false);
    test_helpers::assert_true(slice_res.success, "Slice read succeeded");
    test_helpers::assert_true(slice_res.start_line == 5, "start_line is 5");
    test_helpers::assert_true(slice_res.end_line == 8, "end_line is 8");
    test_helpers::assert_string_equal("Line content 5\nLine content 6\nLine content 7\nLine content 8\n", slice_res.content, "Slice content exact match");

    // Non-existent file
    auto non_res = editor.read_file("/tmp/non_existent_file_xyz_123.txt");
    test_helpers::assert_true(!non_res.success, "Non-existent file reports failure");

    std::filesystem::remove(test_path);
}

void test_write_file() {
    std::cout << "\n--- Testing CodeEditorService::write_file ---\n";
    std::string test_dir = "/tmp/rouen_test_nested_dir";
    std::string test_path = test_dir + "/sub/sample.txt";
    std::filesystem::remove_all(test_dir);

    auto& editor = rouen::helpers::CodeEditorService::instance();

    // Create new file with directories
    auto res1 = editor.write_file(test_path, "Hello World 123", false);
    test_helpers::assert_true(res1.success, "write_file created new file");
    test_helpers::assert_true(std::filesystem::exists(test_path), "File exists on disk");
    test_helpers::assert_true(res1.bytes_written == 15, "bytes_written is 15");

    // Attempt write without overwrite flag
    auto res2 = editor.write_file(test_path, "Replacement", false);
    test_helpers::assert_true(!res2.success, "Refused overwrite when overwrite=false");

    // Overwrite with flag
    auto res3 = editor.write_file(test_path, "Updated text", true);
    test_helpers::assert_true(res3.success, "Succeeded overwrite when overwrite=true");

    std::filesystem::remove_all(test_dir);
}

void test_apply_patch() {
    std::cout << "\n--- Testing CodeEditorService::apply_patch ---\n";
    std::string test_path = "/tmp/test_code_patch.cpp";
    std::filesystem::remove(test_path);

    std::string initial = 
        "#include <iostream>\n"
        "\n"
        "int compute_val() {\n"
        "    int a = 10;\n"
        "    int b = 20;\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    std::cout << compute_val() << std::endl;\n"
        "    return 0;\n"
        "}\n";

    {
        std::ofstream out(test_path);
        out << initial;
    }

    auto& editor = rouen::helpers::CodeEditorService::instance();

    // 1. Surgical single patch
    std::string target = "    int b = 20;\n    return a + b;";
    std::string replacement = "    int b = 30;\n    return (a * 2) + b;";

    auto patch_res1 = editor.apply_patch(test_path, target, replacement);
    test_helpers::assert_true(patch_res1.success, "Single patch applied successfully");
    test_helpers::assert_true(patch_res1.replacement_count == 1, "replacement_count is 1");

    auto read1 = editor.read_file(test_path, 1, 15, false);
    test_helpers::assert_true(read1.content.find("int b = 30;") != std::string::npos, "Found modified int b = 30");
    test_helpers::assert_true(read1.content.find("return (a * 2) + b;") != std::string::npos, "Found modified return expression");

    // 2. Ambiguity check: duplicate pattern
    std::string test_dup_path = "/tmp/test_dup_patch.txt";
    {
        std::ofstream out(test_dup_path);
        out << "apple\nbanana\napple\norange\n";
    }

    auto dup_res = editor.apply_patch(test_dup_path, "apple", "peach", 1, -1, false);
    test_helpers::assert_true(!dup_res.success, "Rejected ambiguous target when allow_multiple=false");

    auto multi_res = editor.apply_patch(test_dup_path, "apple", "peach", 1, -1, true);
    test_helpers::assert_true(multi_res.success, "Succeeded multi-replacement when allow_multiple=true");
    test_helpers::assert_true(multi_res.replacement_count == 2, "Replaced both occurrences");

    std::filesystem::remove(test_path);
    std::filesystem::remove(test_dup_path);
}

void test_self_correction_syntax_feedback() {
    std::cout << "\n--- Testing Self-Correction Syntax Diagnostic Feedback ---\n";
    std::string test_cpp = "/tmp/test_syntax_feedback.cpp";
    std::filesystem::remove(test_cpp);

    std::string valid_code = 
        "#include <iostream>\n"
        "int get_number() {\n"
        "    return 42;\n"
        "}\n";

    auto& editor = rouen::helpers::CodeEditorService::instance();
    auto init_write = editor.write_file(test_cpp, valid_code, true);
    test_helpers::assert_true(init_write.success, "Initial valid write succeeded");
    test_helpers::assert_true(init_write.syntax_check_passed, "Initial syntax check passed with 0 errors");

    // Apply patch introducing a syntax error (missing semicolon)
    auto err_patch = editor.apply_patch(test_cpp, "return 42;\n", "return 42\n");
    test_helpers::assert_true(err_patch.success, "File modification succeeded on disk");
    test_helpers::assert_true(!err_patch.syntax_check_passed, "Automated syntax check correctly caught missing semicolon");
    test_helpers::assert_true(err_patch.error_count > 0, "error_count is greater than 0");
    test_helpers::assert_true(!err_patch.diagnostics.empty(), "Diagnostics list is populated");
    test_helpers::assert_true(err_patch.message.find("syntax error") != std::string::npos, "Message alerts caller to syntax error");

    // Self-correct by repairing the missing semicolon
    auto fix_patch = editor.apply_patch(test_cpp, "return 42\n", "return 100;\n");
    test_helpers::assert_true(fix_patch.success, "Fix patch applied");
    test_helpers::assert_true(fix_patch.syntax_check_passed, "Automated syntax check passed after self-correction");
    test_helpers::assert_true(fix_patch.error_count == 0, "error_count is 0 after fix");

    std::filesystem::remove(test_cpp);
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Starting Code Editor Service Unit Tests \n";
    std::cout << "========================================\n";

    test_read_file();
    test_write_file();
    test_apply_patch();
    test_self_correction_syntax_feedback();

    std::cout << "\n🎉 ALL CODE EDITOR SERVICE TESTS PASSED!\n";
    return 0;
}
