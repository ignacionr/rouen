#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "../src/helpers/conventional_commit.hpp"
#include "../src/helpers/syntax_checker.hpp"
#include "../src/helpers/persona_manager.hpp"
#include "../src/helpers/llm_config.hpp"
#include "../src/helpers/fetch.hpp"

using namespace rouen::helpers;

TEST(CMakeWorkflowTest, ConventionalCommitMessageCleaning) {
    // Leading/trailing whitespace
    std::string raw1 = "  \n\t feat(cmake): add syntax check button   \n";
    EXPECT_EQ(ConventionalCommitGenerator::clean_commit_message(raw1), "feat(cmake): add syntax check button");

    // Markdown code fence wrapped
    std::string raw2 = "```git\nfeat(editor): improve diagnostics drawer\n\n- Add jump buttons\n```";
    EXPECT_EQ(ConventionalCommitGenerator::clean_commit_message(raw2), "feat(editor): improve diagnostics drawer\n\n- Add jump buttons");

    // Quoted message
    std::string raw3 = "\"fix(ninja): correct build output diagnostic parsing\"";
    EXPECT_EQ(ConventionalCommitGenerator::clean_commit_message(raw3), "fix(ninja): correct build output diagnostic parsing");

    std::string raw4 = "```\nchore: clean up temporary build artifacts\n```";
    EXPECT_EQ(ConventionalCommitGenerator::clean_commit_message(raw4), "chore: clean up temporary build artifacts");
}

TEST(CMakeWorkflowTest, ConventionalCommitParsingValid) {
    // 1. Simple type without scope
    auto c1 = ConventionalCommitGenerator::parse_conventional_commit("feat: implement visual diff card");
    EXPECT_TRUE(c1.is_valid);
    EXPECT_EQ(c1.type, "feat");
    EXPECT_TRUE(c1.scope.empty());
    EXPECT_EQ(c1.description, "implement visual diff card");
    EXPECT_FALSE(c1.is_breaking);

    // 2. Scoped commit
    auto c2 = ConventionalCommitGenerator::parse_conventional_commit("fix(cmake): resolve ninja error parsing");
    EXPECT_TRUE(c2.is_valid);
    EXPECT_EQ(c2.type, "fix");
    EXPECT_EQ(c2.scope, "cmake");
    EXPECT_EQ(c2.description, "resolve ninja error parsing");
    EXPECT_FALSE(c2.is_breaking);

    // 3. Breaking change indicator !
    auto c3 = ConventionalCommitGenerator::parse_conventional_commit("feat(api)!: migrate to asynchronous job queue");
    EXPECT_TRUE(c3.is_valid);
    EXPECT_EQ(c3.type, "feat");
    EXPECT_EQ(c3.scope, "api");
    EXPECT_TRUE(c3.is_breaking);

    // 4. Multi-line body and BREAKING CHANGE footer
    std::string full_msg = 
        "refactor(indexer): optimize symbol search with prefix trees\n\n"
        "Replace linear symbol search with prefix trie to achieve O(k) lookups.\n"
        "Reduce memory overhead by storing compressed node paths.\n\n"
        "BREAKING CHANGE: SymbolIndex::lookup now returns an immutable vector reference.";
    auto c4 = ConventionalCommitGenerator::parse_conventional_commit(full_msg);
    EXPECT_TRUE(c4.is_valid);
    EXPECT_EQ(c4.type, "refactor");
    EXPECT_EQ(c4.scope, "indexer");
    EXPECT_TRUE(c4.is_breaking);
    EXPECT_FALSE(c4.body.empty());
    EXPECT_FALSE(c4.footers.empty());
    EXPECT_EQ(c4.footers[0], "BREAKING CHANGE: SymbolIndex::lookup now returns an immutable vector reference.");

    // 5. Build, ci, docs, test, perf, chore types
    EXPECT_TRUE(ConventionalCommitGenerator::parse_conventional_commit("build(deps): update glaze library").is_valid);
    EXPECT_TRUE(ConventionalCommitGenerator::parse_conventional_commit("ci: configure github actions mac arm64").is_valid);
    EXPECT_TRUE(ConventionalCommitGenerator::parse_conventional_commit("docs: update coding feasibility plan").is_valid);
    EXPECT_TRUE(ConventionalCommitGenerator::parse_conventional_commit("test: add ninja triage unit tests").is_valid);
    EXPECT_TRUE(ConventionalCommitGenerator::parse_conventional_commit("perf: accelerate myers diff ses algorithm").is_valid);
    EXPECT_TRUE(ConventionalCommitGenerator::parse_conventional_commit("chore: bump version to 0.9.5").is_valid);
}

TEST(CMakeWorkflowTest, ConventionalCommitParsingInvalid) {
    // Invalid type
    auto c1 = ConventionalCommitGenerator::parse_conventional_commit("random: update files");
    EXPECT_FALSE(c1.is_valid);

    // Missing colon
    auto c2 = ConventionalCommitGenerator::parse_conventional_commit("feat add new button");
    EXPECT_FALSE(c2.is_valid);

    // Empty message
    auto c3 = ConventionalCommitGenerator::parse_conventional_commit("");
    EXPECT_FALSE(c3.is_valid);
}

TEST(CMakeWorkflowTest, NinjaBuildOutputDiagnosticsParsingClangGcc) {
    std::string ninja_output =
        "[1/3] Building CXX object CMakeFiles/rouen.dir/src/main.cpp.o\n"
        "/Users/user/src/rouen/src/main.cpp:42:15: error: expected ';' after expression\n"
        "    int x = 10\n"
        "              ^\n"
        "              ;\n"
        "[2/3] Building CXX object CMakeFiles/rouen.dir/src/helpers/utils.cpp.o\n"
        "/Users/user/src/rouen/src/helpers/utils.cpp:18:5: warning: unused variable 'unused_val' [-Wunused-variable]\n"
        "    int unused_val = 0;\n"
        "        ^\n"
        "ninja: build stopped: subcommand failed.\n";

    auto diagnostics = SyntaxChecker::parse_compiler_output(ninja_output);
    ASSERT_GE(diagnostics.size(), 2u);

    // First diagnostic: Clang error
    EXPECT_EQ(diagnostics[0].file, "/Users/user/src/rouen/src/main.cpp");
    EXPECT_EQ(diagnostics[0].line, 42);
    EXPECT_EQ(diagnostics[0].column, 15);
    EXPECT_EQ(diagnostics[0].severity, "error");
    EXPECT_NE(diagnostics[0].message.find("expected ';' after expression"), std::string::npos);

    // Second diagnostic: Clang warning
    EXPECT_EQ(diagnostics[1].file, "/Users/user/src/rouen/src/helpers/utils.cpp");
    EXPECT_EQ(diagnostics[1].line, 18);
    EXPECT_EQ(diagnostics[1].column, 5);
    EXPECT_EQ(diagnostics[1].severity, "warning");
    EXPECT_NE(diagnostics[1].message.find("unused variable"), std::string::npos);
}

TEST(CMakeWorkflowTest, NinjaBuildOutputDiagnosticsParsingMSVC) {
    std::string msvc_ninja_output =
        "[1/2] Building CXX object CMakeFiles/rouen.dir/src/window.cpp.obj\n"
        "C:\\Users\\user\\src\\rouen\\src\\window.cpp(65,12): error C2065: 'missing_identifier': undeclared identifier\n"
        "ninja: build stopped: subcommand failed.\n";

    auto diagnostics = SyntaxChecker::parse_compiler_output(msvc_ninja_output);
    ASSERT_GE(diagnostics.size(), 1u);

    EXPECT_EQ(diagnostics[0].file, "C:\\Users\\user\\src\\rouen\\src\\window.cpp");
    EXPECT_EQ(diagnostics[0].line, 65);
    EXPECT_EQ(diagnostics[0].column, 12);
    EXPECT_EQ(diagnostics[0].severity, "error");
    EXPECT_NE(diagnostics[0].message.find("missing_identifier"), std::string::npos);
}

TEST(CMakeWorkflowTest, BuildErrorTriagePromptSynthesis) {
    // Create temporary source file with error context
    auto temp_dir = std::filesystem::temp_directory_path() / "rouen_triage_test";
    std::filesystem::create_directories(temp_dir);
    auto test_file = temp_dir / "calculator.cpp";

    {
        std::ofstream out(test_file);
        out << "int calculate(int a, int b) {\n"
            << "    int result = a + b\n" // missing semicolon
            << "    return result;\n"
            << "}\n";
    }

    Diagnostic diag;
    diag.file = test_file.string();
    diag.line = 2;
    diag.column = 23;
    diag.severity = "error";
    diag.message = "expected ';' after expression";

    // Read context
    std::string snippet;
    std::ifstream file(test_file);
    std::string line;
    int line_num = 1;
    while (std::getline(file, line)) {
        snippet += std::format("{:4d} | {}\n", line_num++, line);
    }

    std::string prompt = std::format(
        "Please investigate and resolve this build error encountered during CMake/Ninja compilation:\n\n"
        "- **Target File**: `{}`\n"
        "- **Line**: {}, **Column**: {}\n"
        "- **Severity**: `{}`\n"
        "- **Compiler Diagnostic**: `{}`\n\n"
        "**Code Context**:\n```cpp\n{}```\n\n"
        "Please inspect the issue and apply the correction using `code_apply_patch`.",
        diag.file, diag.line, diag.column, diag.severity, diag.message, snippet
    );

    EXPECT_NE(prompt.find(diag.file), std::string::npos);
    EXPECT_NE(prompt.find("**Line**: 2"), std::string::npos);
    EXPECT_NE(prompt.find("expected ';' after expression"), std::string::npos);
    EXPECT_NE(prompt.find("code_apply_patch"), std::string::npos);
    EXPECT_NE(prompt.find("int result = a + b"), std::string::npos);

    std::filesystem::remove_all(temp_dir);
}

TEST(CMakeWorkflowTest, AIArchitectPersonaSwitching) {
    auto& mgr = PersonaManager::instance();
    bool selected = mgr.select_persona_by_name("Code & Git Architect");
    EXPECT_TRUE(selected);

    auto active = mgr.get_active_persona();
    EXPECT_EQ(active.name, "Code & Git Architect");
    EXPECT_FALSE(active.allowed_mcps.empty());
    EXPECT_TRUE(std::find(active.allowed_mcps.begin(), active.allowed_mcps.end(), "editor") != active.allowed_mcps.end());
}

TEST(CMakeWorkflowTest, LiveAIConventionalCommitSynthesis) {
    if (!LLMConfig::is_configured()) {
        GTEST_SKIP() << "LLM is not configured (no API key in environment or .env)";
    }

    std::string diff_sample =
        "diff --git a/src/cards/development/cmake.cpp b/src/cards/development/cmake.cpp\n"
        "--- a/src/cards/development/cmake.cpp\n"
        "+++ b/src/cards/development/cmake.cpp\n"
        "@@ -250,6 +250,9 @@ bool cmake_card::render() {\n"
        "+            if (ImGui::Button(\"Check Syntax Only\")) {\n"
        "+                check_syntax();\n"
        "+            }\n"
        "+            ImGui::SameLine();\n";

    std::string commit_msg = ConventionalCommitGenerator::generate_commit_message(
        diff_sample, "M src/cards/development/cmake.cpp", "rouen"
    );

    ASSERT_FALSE(commit_msg.empty()) << "LLM should produce a commit message for the diff";

    auto parsed = ConventionalCommitGenerator::parse_conventional_commit(commit_msg);
    EXPECT_TRUE(parsed.is_valid) << "Generated commit message should conform to Conventional Commits: " << commit_msg;
    EXPECT_FALSE(parsed.type.empty());
    EXPECT_FALSE(parsed.description.empty());
}
