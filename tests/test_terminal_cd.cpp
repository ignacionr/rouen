#include <gtest/gtest.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include <format>
#include "../src/cards/system/terminal_bash.hpp"
#include "../src/cards/system/terminal_output.hpp"

using namespace rouen::cards;

TEST(TerminalCdTest, TracksDirectoryChangeOnCd) {
    TerminalOutput output;
    std::atomic<bool> is_command_running{false};

    std::filesystem::path initial_path = std::filesystem::temp_directory_path().lexically_normal();
    TerminalBash bash;
    bash.initialize_bash_session(initial_path.string(), output, is_command_running);

    ASSERT_TRUE(bash.is_interactive());
    
    // Wait for interactive bash PTY to finish starting up and processing environment initialization scripts
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Send a "cd /" command to switch directory
    std::string target_dir = "/";
    bash.send_to_bash(std::format("cd {}", target_dir), false);

    // Poll until bash reader thread processes prompt update and sets CWD
    bool directory_updated = false;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (bash.get_cwd() == target_dir) {
            directory_updated = true;
            break;
        }
    }

    EXPECT_TRUE(directory_updated) << "Expected CWD to update to '" << target_dir << "', but got: '" << bash.get_cwd() << "'";
    EXPECT_EQ(bash.get_cwd(), target_dir);

    bash.terminate_bash_session();
}

TEST(TerminalCopyTest, CollectsAllTextFromOutputBuffer) {
    TerminalOutput output;
    output.add_to_output("Welcome to Rouen Terminal", OutputType::System);
    output.add_to_output("ls -l", OutputType::Command);
    output.add_to_output("file1.txt\nfile2.txt", OutputType::StdOut);

    std::string full_text = output.get_all_text();
    EXPECT_NE(full_text.find("Welcome to Rouen Terminal"), std::string::npos);
    EXPECT_NE(full_text.find("ls -l"), std::string::npos);
    EXPECT_NE(full_text.find("file1.txt"), std::string::npos);
}

TEST(TerminalClearTest, ClearsOutputBufferAndResetsPrompt) {
    TerminalOutput output;
    output.add_to_output("Output line 1", OutputType::StdOut);
    output.add_to_output("Output line 2", OutputType::StdOut);
    
    output.clear_terminal("/tmp");
    
    std::string full_text = output.get_all_text();
    EXPECT_EQ(full_text.find("Output line 1"), std::string::npos);
    EXPECT_NE(full_text.find("Terminal cleared."), std::string::npos);
    EXPECT_NE(full_text.find("/tmp$"), std::string::npos);
}
