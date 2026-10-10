#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "../src/helpers/workspace_rules.hpp"

namespace test_helpers {
    inline void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "❌ Assertion failed: " << message << "\n";
            std::exit(1);
        } else {
            std::cout << "✅ " << message << ": PASSED\n";
        }
    }
}

void test_workspace_rules_loader() {
    std::cout << "\n--- Testing WorkspaceRulesLoader ---\n";

    // Test 1: Real Rouen workspace
    auto current_root = rouen::helpers::WorkspaceRulesLoader::find_workspace_root(std::filesystem::current_path());
    test_helpers::assert_true(!current_root.empty(), "find_workspace_root found current workspace root");
    test_helpers::assert_true(std::filesystem::exists(current_root / ".agents"), "Workspace root contains .agents");

    // Test 2: Rules injection includes critical project rules
    std::string rules = rouen::helpers::WorkspaceRulesLoader::load_workspace_rules(current_root, "process inbox/2026-10-08-test.md");
    test_helpers::assert_true(!rules.empty(), "Rules are not empty");
    test_helpers::assert_true(rules.find("-j2") != std::string::npos, "Rules mention -j2 parallelism limit");
    test_helpers::assert_true(rules.find("say") != std::string::npos, "Rules mention macOS say notification");
    test_helpers::assert_true(rules.find("9-step") != std::string::npos || rules.find("9-STEP") != std::string::npos, "Rules mention 9-step inbox lifecycle");
    test_helpers::assert_true(rules.find("ZERO CHAT OVERHEAD") != std::string::npos, "Rules mention ZERO CHAT OVERHEAD rule");
    test_helpers::assert_true(rules.find("LIMIT RECONNAISSANCE") != std::string::npos, "Rules mention LIMIT RECONNAISSANCE limit");
    test_helpers::assert_true(rules.find("AVAILABLE WORKSPACE SKILLS") != std::string::npos, "Rules list available skills");
    test_helpers::assert_true(rules.find("cpp20-module-migration") != std::string::npos, "Skills list includes cpp20-module-migration");

    // Test 2b: Query with "carry out steps 3 and 4" triggers inbox rules and critical execution rule
    std::string step_rules = rouen::helpers::WorkspaceRulesLoader::load_workspace_rules(current_root, "carry out steps 3 and 4");
    test_helpers::assert_true(step_rules.find("ZERO CHAT OVERHEAD") != std::string::npos, "Step query includes ZERO CHAT OVERHEAD rule");
    test_helpers::assert_true(step_rules.find("9-step") != std::string::npos || step_rules.find("9-STEP") != std::string::npos, "Step query includes 9-step workflow");

    // Test 2c: Non-inbox query (e.g. "check calculator card") also receives critical execution directives
    std::string card_rules = rouen::helpers::WorkspaceRulesLoader::load_workspace_rules(current_root, "check calculator card");
    test_helpers::assert_true(card_rules.find("ZERO CHAT OVERHEAD") != std::string::npos, "Card query includes ZERO CHAT OVERHEAD rule");
    test_helpers::assert_true(card_rules.find("NEVER ask for permission") != std::string::npos, "Card query forbids asking for permission");

    // Test 3: Fixture with no .agents returns empty string
    std::filesystem::path empty_fixture = std::filesystem::temp_directory_path() / "rouen_empty_ws_fixture";
    std::filesystem::create_directories(empty_fixture);
    std::string empty_rules = rouen::helpers::WorkspaceRulesLoader::load_workspace_rules(empty_fixture, "some query");
    test_helpers::assert_true(empty_rules.empty(), "Returns empty string when no .agents or rules exist");
    std::filesystem::remove_all(empty_fixture);

    // Test 4: Fixture with custom .agents/AGENTS.md
    std::filesystem::path custom_fixture = std::filesystem::temp_directory_path() / "rouen_custom_ws_fixture";
    std::filesystem::create_directories(custom_fixture / ".agents");
    {
        std::ofstream out(custom_fixture / ".agents" / "AGENTS.md");
        out << "Custom Project Rule 42: Always be awesome.\n";
    }
    std::string custom_rules = rouen::helpers::WorkspaceRulesLoader::load_workspace_rules(custom_fixture);
    test_helpers::assert_true(custom_rules.find("Custom Project Rule 42") != std::string::npos, "Custom AGENTS.md content loaded");
    std::filesystem::remove_all(custom_fixture);
}

int main() {
    test_workspace_rules_loader();
    std::cout << "\nAll WorkspaceRulesLoader tests passed successfully!\n";
    return 0;
}
