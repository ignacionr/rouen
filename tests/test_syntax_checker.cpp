#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "../src/helpers/toolchain_service.hpp"
#include "../src/helpers/syntax_checker.hpp"

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

void test_toolchain_discovery() {
    std::cout << "\n--- Testing Toolchain Discovery ---\n";
    auto& tc_svc = rouen::helpers::ToolchainService::instance();
    auto tc = tc_svc.discover_toolchain();

    std::cout << "Platform: " << tc.platform << "\n";
    std::cout << "Discovered C++ Compiler: " << tc.cpp_compiler << "\n";
    std::cout << "Discovered C Compiler: " << tc.c_compiler << "\n";
    std::cout << "Discovered Build System: " << tc.build_system << "\n";
    std::cout << "Has Clang: " << (tc.has_clang ? "true" : "false") << "\n";
    std::cout << "Has GCC: " << (tc.has_gcc ? "true" : "false") << "\n";
    std::cout << "Has MSVC: " << (tc.has_msvc ? "true" : "false") << "\n";
    std::cout << "Has CMake: " << (tc.has_cmake ? "true" : "false") << "\n";
    std::cout << "Has Ninja: " << (tc.has_ninja ? "true" : "false") << "\n";
    std::cout << "Has Nix: " << (tc.has_nix ? "true" : "false") << "\n";
    std::cout << "Is Nix Workspace: " << (tc.is_nix_workspace ? "true" : "false") << "\n";
    std::cout << "Compile commands found: " << tc.compile_commands_count << " at " << tc.compile_commands_path << "\n";

    test_helpers::assert_true(!tc.cpp_compiler.empty(), "Discovered at least one C++ compiler");
    test_helpers::assert_true(tc.has_cmake || tc.has_ninja, "Discovered CMake or Ninja build system");
    test_helpers::assert_true(!tc.platform.empty(), "Platform detected");
}

void test_gcc_clang_diagnostic_parsing() {
    std::cout << "\n--- Testing GCC/Clang Diagnostic Parsing ---\n";

    std::string sample_output = 
        "/Users/test/project/src/main.cpp:42:15: error: use of undeclared identifier 'my_var'\n"
        "    int a = my_var + 1;\n"
        "            ^\n"
        "/Users/test/project/src/main.cpp:45:10: warning: unused variable 'unused_val' [-Wunused-variable]\n"
        "    int unused_val = 10;\n"
        "        ^\n"
        "/Users/test/project/include/header.hpp:12:1: note: candidate function not viable\n"
        "void foo(int x);\n"
        "^\n";

    auto diags = rouen::helpers::SyntaxChecker::parse_compiler_output(sample_output, "/Users/test/project/src/main.cpp");
    test_helpers::assert_true(diags.size() == 3, "Parsed exactly 3 diagnostics");

    // First diagnostic: Error
    test_helpers::assert_string_equal("/Users/test/project/src/main.cpp", diags[0].file, "Diag 0 file");
    test_helpers::assert_true(diags[0].line == 42, "Diag 0 line is 42");
    test_helpers::assert_true(diags[0].column == 15, "Diag 0 col is 15");
    test_helpers::assert_string_equal("error", diags[0].severity, "Diag 0 severity is error");
    test_helpers::assert_string_equal("use of undeclared identifier 'my_var'", diags[0].message, "Diag 0 message");

    // Second diagnostic: Warning
    test_helpers::assert_true(diags[1].line == 45, "Diag 1 line is 45");
    test_helpers::assert_string_equal("warning", diags[1].severity, "Diag 1 severity is warning");

    // Third diagnostic: Note
    test_helpers::assert_true(diags[2].line == 12, "Diag 2 line is 12");
    test_helpers::assert_string_equal("note", diags[2].severity, "Diag 2 severity is note");

    // Convert to ErrorMarkers
    auto markers = rouen::helpers::SyntaxChecker::to_error_markers(diags, "main.cpp");
    test_helpers::assert_true(markers.size() == 2, "Converted to 2 error markers (note skipped, warnings/errors kept)");
    test_helpers::assert_true(markers.find(42) != markers.end(), "Line 42 has marker");
    test_helpers::assert_true(markers.find(45) != markers.end(), "Line 45 has marker");
    test_helpers::assert_true(markers[42].find("[Error] use of undeclared identifier") != std::string::npos, "Marker 42 prefix");
    test_helpers::assert_true(markers[45].find("[Warning] unused variable") != std::string::npos, "Marker 45 prefix");
}

void test_msvc_diagnostic_parsing() {
    std::cout << "\n--- Testing MSVC Diagnostic Parsing ---\n";

    std::string sample_msvc = 
        "C:\\projects\\rouen\\src\\main.cpp(108,22): error C2065: 'undefined_fn': undeclared identifier\n"
        "C:\\projects\\rouen\\src\\main.cpp(112): warning C4101: 'unused': unreferenced local variable\n";

    auto diags = rouen::helpers::SyntaxChecker::parse_compiler_output(sample_msvc, "main.cpp");
    test_helpers::assert_true(diags.size() == 2, "Parsed 2 MSVC diagnostics");
    test_helpers::assert_true(diags[0].line == 108, "MSVC Diag 0 line is 108");
    test_helpers::assert_true(diags[0].column == 22, "MSVC Diag 0 col is 22");
    test_helpers::assert_string_equal("error", diags[0].severity, "MSVC Diag 0 severity");
    test_helpers::assert_true(diags[1].line == 112, "MSVC Diag 1 line is 112");
    test_helpers::assert_string_equal("warning", diags[1].severity, "MSVC Diag 1 severity");
}

void test_python_diagnostic_parsing() {
    std::cout << "\n--- Testing Python Diagnostic Parsing ---\n";

    std::string sample_py =
        "  File \"test_script.py\", line 17\n"
        "    def foo(\n"
        "           ^\n"
        "SyntaxError: expected ':'\n";

    auto diags = rouen::helpers::SyntaxChecker::parse_compiler_output(sample_py, "test_script.py");
    test_helpers::assert_true(diags.size() == 1, "Parsed 1 Python SyntaxError");
    test_helpers::assert_true(diags[0].line == 17, "Python error line is 17");
    test_helpers::assert_string_equal("error", diags[0].severity, "Python error severity is error");
    test_helpers::assert_string_equal("expected ':'", diags[0].message, "Python error message");
}

void test_live_syntax_check() {
    std::cout << "\n--- Testing Live Syntax Check on Temporary Broken File ---\n";
    auto& checker = rouen::helpers::SyntaxChecker::instance();

    std::string temp_py = "/tmp/test_syntax_broken.py";
    {
        std::ofstream f(temp_py);
        f << "def broken_fn(\n";
        f << "    print('missing colon')\n";
    }

    auto result = checker.check_file(temp_py);
    std::filesystem::remove(temp_py);

    test_helpers::assert_true(!result.success, "Broken file check returned success = false");
    test_helpers::assert_true(result.error_count >= 1, "Detected at least 1 syntax error in broken file");
    test_helpers::assert_true(!result.diagnostics.empty(), "Populated diagnostics list");
    std::cout << "Live diagnostic message: " << result.diagnostics[0].message << " on line " << result.diagnostics[0].line << "\n";
}

void test_live_cpp_syntax_check() {
    std::cout << "\n--- Testing Live C++ Syntax Check on Broken C++ File ---\n";
    auto& checker = rouen::helpers::SyntaxChecker::instance();

    std::string temp_cpp = "/tmp/test_syntax_broken.cpp";
    {
        std::ofstream f(temp_cpp);
        f << "#include <string>\n";
        f << "int main() {\n";
        f << "    int a = 10\n"; // Missing semicolon!
        f << "    return a;\n";
        f << "}\n";
    }

    auto result = checker.check_file(temp_cpp);
    std::filesystem::remove(temp_cpp);

    test_helpers::assert_true(!result.success, "Broken C++ file check returned success = false");
    test_helpers::assert_true(result.error_count >= 1, "Detected syntax error in broken C++ file");
    test_helpers::assert_true(!result.diagnostics.empty(), "Populated C++ diagnostics list");
    std::cout << "Live C++ diagnostic: " << result.diagnostics[0].message << " on line " << result.diagnostics[0].line << "\n";
    test_helpers::assert_true(result.diagnostics[0].line == 3 || result.diagnostics[0].line == 4, "Line of syntax error is 3 or 4");
}

void test_header_compile_command_fallback() {
    std::cout << "\n--- Testing Header Compile Command Fallback & Syntax Check ---\n";
    auto& tc_svc = rouen::helpers::ToolchainService::instance();
    auto& checker = rouen::helpers::SyntaxChecker::instance();

    std::string header_path = "src/cards/production/adlib.hpp";
    if (std::filesystem::exists(header_path)) {
        auto entry_opt = tc_svc.get_compile_command(header_path);
        test_helpers::assert_true(entry_opt.has_value(), "Resolved compile command for header via matching source stem");
        if (entry_opt.has_value()) {
            std::cout << "Resolved directory: " << entry_opt->directory << "\n";
            std::cout << "Resolved command: " << entry_opt->command.substr(0, 80) << "...\n";
            test_helpers::assert_true(!entry_opt->directory.empty(), "Compile entry directory is populated");
        }

        auto result = checker.check_file(header_path);
        std::cout << "Header syntax check result: success=" << (result.success ? "true" : "false") 
                  << ", errors=" << result.error_count << "\n";
        test_helpers::assert_true(result.error_count == 0, "Clean header has 0 syntax errors (no false imgui.h / module errors)");
    }
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Starting Syntax Checker & Toolchain Tests\n";
    std::cout << "========================================\n";

    test_toolchain_discovery();
    test_gcc_clang_diagnostic_parsing();
    test_msvc_diagnostic_parsing();
    test_python_diagnostic_parsing();
    test_live_syntax_check();
    test_live_cpp_syntax_check();
    test_header_compile_command_fallback();

    std::cout << "\n🎉 ALL SYNTAX CHECKER & TOOLCHAIN TESTS PASSED!\n";
    return 0;
}
