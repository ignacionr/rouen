#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "../src/helpers/code_indexer.hpp"

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

void test_symbol_parsing() {
    std::cout << "\n--- Testing C++ Symbol Parsing ---\n";

    std::string sample_code = 
        "#pragma once\n"
        "#define ROUEN_VERSION_MACRO \"1.4.17\"\n"
        "namespace rouen::editor {\n"
        "enum class EditorState {\n"
        "    Idle,\n"
        "    Active\n"
        "};\n"
        "struct ErrorMarkerInfo {\n"
        "    int line;\n"
        "    std::string text;\n"
        "};\n"
        "class TextEditor {\n"
        "public:\n"
        "    void SetErrorMarkers(const std::map<int, std::string>& markers);\n"
        "    std::string GetText() const;\n"
        "};\n"
        "}\n";

    auto symbols = rouen::helpers::CodeIndexer::parse_symbols(sample_code, "test_editor.hpp");
    test_helpers::assert_true(symbols.size() >= 5, "Extracted at least 5 symbols");

    bool has_macro = false;
    bool has_namespace = false;
    bool has_enum = false;
    bool has_struct = false;
    bool has_class = false;
    bool has_method = false;

    for (const auto& s : symbols) {
        if (s.name == "ROUEN_VERSION_MACRO" && s.kind == "macro") has_macro = true;
        if (s.name == "rouen" && s.kind == "namespace") has_namespace = true;
        if (s.name == "EditorState" && s.kind == "enum") has_enum = true;
        if (s.name == "ErrorMarkerInfo" && s.kind == "struct") has_struct = true;
        if (s.name == "TextEditor" && s.kind == "class") has_class = true;
        if (s.name == "SetErrorMarkers" && s.kind == "function") has_method = true;
    }

    test_helpers::assert_true(has_macro, "Parsed macro symbol");
    test_helpers::assert_true(has_namespace, "Parsed namespace symbol");
    test_helpers::assert_true(has_enum, "Parsed enum symbol");
    test_helpers::assert_true(has_struct, "Parsed struct symbol");
    test_helpers::assert_true(has_class, "Parsed class symbol");
    test_helpers::assert_true(has_method, "Parsed method symbol");
}

void test_fts5_indexing_and_search() {
    std::cout << "\n--- Testing FTS5 Indexing & Trigram Search ---\n";

    std::string temp_db = "/tmp/test_rouen_code_index.db";
    std::string temp_src = "/tmp/test_widget.cpp";
    std::filesystem::remove(temp_db);
    std::filesystem::remove(temp_src);

    {
        std::ofstream f(temp_src);
        f << "#include <iostream>\n";
        f << "class WidgetComponent {\n";
        f << "public:\n";
        f << "    void RenderWidgetSurface() {\n";
        f << "        std::cout << \"Rendering unique_secret_token_123\" << std::endl;\n";
        f << "    }\n";
        f << "};\n";
    }

    rouen::helpers::CodeIndexer indexer(temp_db);
    bool indexed = indexer.index_file(temp_src);
    test_helpers::assert_true(indexed, "First index_file returned true (indexed)");

    // Test incremental indexing - should skip because mtime is untouched
    bool reindexed = indexer.index_file(temp_src, false);
    test_helpers::assert_true(!reindexed, "Incremental index_file skipped untouched file");

    // Search for trigram substring
    auto matches = indexer.search("unique_secret_token_123");
    test_helpers::assert_true(!matches.empty(), "Found search match for unique token");
    test_helpers::assert_string_equal(temp_src, matches[0].filepath, "Match filepath matches");
    test_helpers::assert_true(matches[0].line_number == 5, "Match line number is 5");

    // Search for symbols
    auto symbols = indexer.find_symbol("WidgetComponent");
    test_helpers::assert_true(!symbols.empty(), "Found symbol 'WidgetComponent'");
    test_helpers::assert_string_equal("class", symbols[0].kind, "Symbol kind is class");

    auto funcs = indexer.find_symbol("RenderWidgetSurface");
    test_helpers::assert_true(!funcs.empty(), "Found function 'RenderWidgetSurface'");
    test_helpers::assert_string_equal("function", funcs[0].kind, "Symbol kind is function");

    // Wildcard symbol search
    auto wildcard_res = indexer.find_symbol("Render*");
    test_helpers::assert_true(!wildcard_res.empty(), "Wildcard 'Render*' matched symbol");

    std::filesystem::remove(temp_db);
    std::filesystem::remove(temp_src);
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Starting Code Indexer (SQLite FTS5) Tests\n";
    std::cout << "========================================\n";

    test_symbol_parsing();
    test_fts5_indexing_and_search();

    std::cout << "\n🎉 ALL CODE INDEXER TESTS PASSED!\n";
    return 0;
}
