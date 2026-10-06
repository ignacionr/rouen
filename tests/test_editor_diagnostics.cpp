#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <format>

#include "src/editor/text_editor.hpp"
#include "src/helpers/syntax_checker.hpp"
#include "src/helpers/persona_manager.hpp"
#include "src/registrar.hpp"

using namespace rouen::helpers;

TEST(EditorDiagnosticsTest, DiagnosticsStorageAndMarkers) {
    rouen::editor::TextEditor editor;
    EXPECT_EQ(editor.getErrorsCount(), 0);
    EXPECT_EQ(editor.getWarningsCount(), 0);
    EXPECT_TRUE(editor.getDiagnostics().empty());

    std::vector<Diagnostic> diags = {
        Diagnostic{.file = "main.cpp", .line = 10, .column = 5, .severity = "error", .message = "undefined variable 'x'"},
        Diagnostic{.file = "main.cpp", .line = 15, .column = 2, .severity = "warning", .message = "unused parameter 'argc'"},
        Diagnostic{.file = "main.cpp", .line = 20, .column = 1, .severity = "fatal error", .message = "unterminated string literal"}
    };

    editor.setDiagnostics(diags);
    EXPECT_EQ(editor.getErrorsCount(), 2);
    EXPECT_EQ(editor.getWarningsCount(), 1);
    EXPECT_EQ(editor.getDiagnostics().size(), 3u);

    const auto& markers = editor.getErrorMarkers();
    EXPECT_EQ(markers.size(), 3u);
    EXPECT_NE(markers.find(10), markers.end());
    EXPECT_NE(markers.find(15), markers.end());
    EXPECT_NE(markers.find(20), markers.end());

    editor.clearDiagnostics();
    EXPECT_EQ(editor.getErrorsCount(), 0);
    EXPECT_EQ(editor.getWarningsCount(), 0);
    EXPECT_TRUE(editor.getDiagnostics().empty());
    EXPECT_TRUE(editor.getErrorMarkers().empty());
}

TEST(EditorDiagnosticsTest, CursorNavigationAndClamping) {
    rouen::editor::TextEditor editor;
    std::string test_file = "/tmp/test_cursor_nav.cpp";
    {
        std::ofstream out(test_file);
        for (int i = 1; i <= 30; ++i) {
            out << std::format("// Line {}\n", i);
        }
    }

    editor.select(test_file);
    EXPECT_EQ(editor.getCurrentLine(), 1);

    editor.jumpToLine(15);
    EXPECT_EQ(editor.getCurrentLine(), 15);

    // Clamping checks
    editor.jumpToLine(0);
    EXPECT_EQ(editor.getCurrentLine(), 1);

    editor.jumpToLine(100);
    EXPECT_LE(editor.getCurrentLine(), 31);
    EXPECT_GE(editor.getCurrentLine(), 29);

    std::filesystem::remove(test_file);
}

TEST(EditorDiagnosticsTest, NextAndPrevDiagnosticCycling) {
    rouen::editor::TextEditor editor;
    std::string test_file = "/tmp/test_cycling.cpp";
    {
        std::ofstream out(test_file);
        for (int i = 1; i <= 40; ++i) {
            out << std::format("int var_{} = {};\n", i, i);
        }
    }
    editor.select(test_file);

    std::vector<Diagnostic> diags = {
        Diagnostic{.file = test_file, .line = 5, .column = 1, .severity = "error", .message = "err 1"},
        Diagnostic{.file = test_file, .line = 15, .column = 1, .severity = "warning", .message = "warn 2"},
        Diagnostic{.file = test_file, .line = 25, .column = 1, .severity = "error", .message = "err 3"}
    };
    editor.setDiagnostics(diags);

    // Start at line 1
    editor.jumpToLine(1);
    editor.jumpToNextDiagnostic();
    EXPECT_EQ(editor.getCurrentLine(), 5);

    editor.jumpToNextDiagnostic();
    EXPECT_EQ(editor.getCurrentLine(), 15);

    editor.jumpToNextDiagnostic();
    EXPECT_EQ(editor.getCurrentLine(), 25);

    // Wrap around forward: 25 -> 5
    editor.jumpToNextDiagnostic();
    EXPECT_EQ(editor.getCurrentLine(), 5);

    // Wrap around backward: 5 -> 25
    editor.jumpToPrevDiagnostic();
    EXPECT_EQ(editor.getCurrentLine(), 25);

    editor.jumpToPrevDiagnostic();
    EXPECT_EQ(editor.getCurrentLine(), 15);

    std::filesystem::remove(test_file);
}

TEST(EditorDiagnosticsTest, SurroundingCodeContext) {
    rouen::editor::TextEditor editor;
    std::string test_file = "/tmp/test_surround.cpp";
    {
        std::ofstream out(test_file);
        for (int i = 1; i <= 20; ++i) {
            out << std::format("int value_{} = {};\n", i, i * 10);
        }
    }
    editor.select(test_file);

    std::string snippet = editor.getSurroundingCode(10, 2);
    EXPECT_NE(snippet.find("8 |"), std::string::npos);
    EXPECT_NE(snippet.find("10 |"), std::string::npos);
    EXPECT_NE(snippet.find("12 |"), std::string::npos);
    EXPECT_EQ(snippet.find("5 |"), std::string::npos);

    std::filesystem::remove(test_file);
}

TEST(EditorDiagnosticsTest, FixWithAIPromptFormatting) {
    rouen::editor::TextEditor editor;
    std::string test_file = "/tmp/test_prompt.cpp";
    {
        std::ofstream out(test_file);
        out << "int main() {\n"
            << "    int a = 10;\n"
            << "    int b = 20;\n"
            << "    int c = add_numbers(a, b);\n"
            << "    return 0;\n"
            << "}\n";
    }
    editor.select(test_file);

    std::vector<Diagnostic> diags = {
        Diagnostic{.file = test_file, .line = 4, .column = 13, .severity = "error", .message = "use of undeclared identifier 'add_numbers'"}
    };
    editor.setDiagnostics(diags);

    std::string prompt = editor.buildFixWithAIPrompt(4);

    EXPECT_NE(prompt.find("Target File"), std::string::npos);
    EXPECT_NE(prompt.find("**Line**: 4"), std::string::npos);
    EXPECT_NE(prompt.find("use of undeclared identifier 'add_numbers'"), std::string::npos);
    EXPECT_NE(prompt.find("code_apply_patch"), std::string::npos);
    EXPECT_NE(prompt.find("code_check_syntax"), std::string::npos);
    EXPECT_NE(prompt.find("int c = add_numbers(a, b)"), std::string::npos);

    std::filesystem::remove(test_file);
}

TEST(EditorDiagnosticsTest, FixWithAIRegistrarDispatch) {
    rouen::editor::TextEditor editor;
    std::string test_file = "/tmp/test_dispatch.cpp";
    {
        std::ofstream out(test_file);
        out << "int calculate(int x) {\n"
            << "    return x * 2;\n"
            << "}\n";
    }
    editor.select(test_file);

    std::vector<Diagnostic> diags = {
        Diagnostic{.file = test_file, .line = 2, .column = 12, .severity = "error", .message = "unexpected token"}
    };
    editor.setDiagnostics(diags);

    std::string captured_prompt;
    auto mock_send = std::make_shared<std::function<void(const std::string&)>>([&](const std::string& p) {
        captured_prompt = p;
    });
    registrar::add<std::function<void(const std::string&)>>("ai_chat_send_message", mock_send);

    editor.triggerFixWithAI(2);

    EXPECT_FALSE(captured_prompt.empty());
    EXPECT_NE(captured_prompt.find("unexpected token"), std::string::npos);
    EXPECT_NE(captured_prompt.find("calculate"), std::string::npos);
    EXPECT_EQ(PersonaManager::instance().get_active_persona().name, "Code & Git Architect");

    registrar::remove<std::function<void(const std::string&)>>("ai_chat_send_message");
    std::filesystem::remove(test_file);
}
