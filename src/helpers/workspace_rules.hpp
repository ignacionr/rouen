#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <format>
#include "code_editor_service.hpp"

namespace rouen::helpers {

class WorkspaceRulesLoader {
public:
    struct SkillInfo {
        std::string name;
        std::string description;
        std::string path;
    };

    /**
     * Locate the workspace / repository root by searching upward for .agents or .git.
     */
    static std::filesystem::path find_workspace_root(const std::filesystem::path& start_path) {
        try {
            if (start_path.empty()) {
                return "";
            }
            std::filesystem::path current = start_path;
            std::error_code ec;
            if (std::filesystem::is_regular_file(current, ec)) {
                current = current.parent_path();
            }
            while (!current.empty()) {
                if (std::filesystem::exists(current / ".agents", ec) || std::filesystem::exists(current / ".git", ec)) {
                    return current;
                }
                if (current == current.root_path()) {
                    break;
                }
                current = current.parent_path();
            }
        } catch (...) {}
        return "";
    }

    /**
     * Discover skills defined in .agents/skills/
     */
    static std::vector<SkillInfo> discover_skills(const std::filesystem::path& workspace_root) {
        std::vector<SkillInfo> skills;
        try {
            std::error_code ec;
            auto skills_dir = workspace_root / ".agents" / "skills";
            if (!std::filesystem::exists(skills_dir, ec) || !std::filesystem::is_directory(skills_dir, ec)) {
                return skills;
            }
            for (const auto& entry : std::filesystem::directory_iterator(skills_dir, ec)) {
                if (!entry.is_directory(ec)) continue;
                auto skill_file = entry.path() / "SKILL.md";
                if (!std::filesystem::exists(skill_file, ec)) continue;

                SkillInfo info;
                info.name = entry.path().filename().string();
                info.path = skill_file.string();

                std::ifstream file(skill_file);
                if (file.is_open()) {
                    std::string line;
                    bool in_frontmatter = false;
                    while (std::getline(file, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line == "---") {
                            if (!in_frontmatter) {
                                in_frontmatter = true;
                                continue;
                            } else {
                                break;
                            }
                        }
                        if (in_frontmatter) {
                            if (line.starts_with("name:")) {
                                std::string n = line.substr(5);
                                while (!n.empty() && (n.front() == ' ' || n.front() == '\t')) n.erase(0, 1);
                                if (!n.empty()) info.name = n;
                            } else if (line.starts_with("description:")) {
                                std::string d = line.substr(12);
                                while (!d.empty() && (d.front() == ' ' || d.front() == '\t')) d.erase(0, 1);
                                info.description = d;
                            }
                        }
                    }
                }
                skills.push_back(std::move(info));
            }
        } catch (...) {}
        return skills;
    }

    /**
     * Load project rules, inbox workflow rules, and skill summaries for injection into the system prompt.
     */
    static std::string load_workspace_rules(const std::filesystem::path& workspace_root_candidate = "", [[maybe_unused]] const std::string& query = "") {
        std::filesystem::path ws_root = workspace_root_candidate;
        if (ws_root.empty()) {
            std::string def_ws = CodeEditorService::instance().get_default_workspace();
            if (!def_ws.empty()) {
                ws_root = def_ws;
            }
        }
        if (ws_root.empty()) {
            try {
                ws_root = std::filesystem::current_path();
            } catch (...) {}
        }

        std::filesystem::path detected_root = find_workspace_root(ws_root);
        if (!detected_root.empty()) {
            ws_root = detected_root;
        }

        std::error_code ec;
        if (ws_root.empty() || !std::filesystem::exists(ws_root, ec)) {
            return "";
        }

        auto agents_md = ws_root / ".agents" / "AGENTS.md";
        auto inbox_readme = ws_root / "inbox" / "README.md";
        bool has_agents_md = std::filesystem::exists(agents_md, ec);
        bool has_inbox = std::filesystem::exists(inbox_readme, ec) || std::filesystem::exists(ws_root / "inbox", ec);
        auto skills = discover_skills(ws_root);

        // If no .agents directory, no AGENTS.md, no skills, and no inbox, return empty string gracefully
        if (!has_agents_md && skills.empty() && !has_inbox) {
            return "";
        }

        std::ostringstream out;
        out << "### WORKSPACE CONTEXT\n";
        out << "Active Workspace Root: " << ws_root.string() << "\n\n";

        if (has_agents_md) {
            std::ifstream file(agents_md);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();
                if (!content.empty()) {
                    out << "### PROJECT RULES & WORKSPACE CONSTRAINTS (from .agents/AGENTS.md)\n";
                    out << content << "\n\n";
                }
            }
        }

        // Autonomous execution rule applies to ALL engineering & workspace interactions
        out << "### CRITICAL EXECUTION RULE (ZERO CHAT OVERHEAD & IMMEDIATE ACTION)\n";
        out << "- You are an autonomous software engineering agent with direct tool access (`code_read_file`, `code_write_file`, `code_apply_patch`, `run_unit_tests`, `run_local_command`, `build_and_deploy`, `update_inbox_item`).\n";
        out << "- NEVER ask for permission, assistance, or confirmation from the user as to whether you should proceed, continue, or implement code/fixes. Assume full authorization and carry out the work directly.\n";
        out << "- NEVER output documentation, architectural overviews, roadmaps, or speculative plans about 'what should be next' in place of actually carrying out the implementation.\n";
        out << "- When investigating, diagnosing, or verifying a feature or bug (such as inspecting whether a card supports adaptive cards), DO NOT stop after diagnosis. Immediately proceed to implement the required code changes, write tests, run tests, and compile in the same turn.\n";
        out << "- LIMIT RECONNAISSANCE: Do NOT get stuck in an endless research loop. Spend AT MOST 3 to 5 tool calls on reading or searching files. Once you locate the target file and the existing interface pattern, you MUST immediately start writing tests and applying code patches (`code_apply_patch`, `code_write_file`). Never make more than 5 consecutive read/search calls before modifying code.\n";
        out << "- Carry all tasks through to completion end-to-end (diagnose -> test -> implement -> verify -> compile) before returning your response.\n\n";

        if (has_inbox) {
            out << "### INBOX ISSUE LIFECYCLE (9-STEP WORKFLOW)\n";
            out << "When processing an item in `./inbox`, you MUST strictly adhere to this 9-step workflow:\n";
            out << "1. **Understand**: Read the filed report and trace the affected architecture and code paths.\n";
            out << "2. **Diagnose**: Pinpoint root causes across affected systems.\n";
            out << "3. **Setup Unit Test**: Add or expand unit tests in `tests/` covering the bug/feature.\n";
            out << "4. **Run and Fail**: Run the test to confirm it fails as expected (red).\n";
            out << "5. **Fix**: Implement the clean, minimal fix.\n";
            out << "6. **Run and Pass**: Re-run the unit test and verify it passes (green), along with target builds and deployment.\n";
            out << "7. **Commit**: Create a conventional commit (`fix(...)` or `feat(...)`).\n";
            out << "8. **Push**: Push commit to remote `origin`.\n";
            out << "9. **Rename Report**: Rename `inbox/<report>.md` to `inbox/done_<report>.md`.\n\n";
            out << "Do NOT treat these steps as topics to explain, outline, or plan in chat. Immediately invoke the necessary tools (`code_read_file`, `code_write_file`, `code_apply_patch`, `run_unit_tests`, `run_local_command`, `build_and_deploy`, `update_inbox_item`). Execute all requested steps through to completion before responding to the user.\n\n";
        }

        if (!skills.empty()) {
            out << "### AVAILABLE WORKSPACE SKILLS\n";
            out << "The following specialized skills and playbooks are available in `.agents/skills/`. Read the relevant `SKILL.md` when working on related tasks:\n";
            for (const auto& s : skills) {
                out << "- **" << s.name << "**: " << s.description << " (Path: `" << s.path << "`)\n";
            }
            out << "\n";
        }

        return out.str();
    }
};

inline std::string load_workspace_rules(const std::filesystem::path& workspace_root = "", const std::string& query = "") {
    return WorkspaceRulesLoader::load_workspace_rules(workspace_root, query);
}

} // namespace rouen::helpers
