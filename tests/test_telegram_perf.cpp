#include <iostream>
#include <chrono>
#include <thread>
#include <gtest/gtest.h>

#include "../src/helpers/glaze_include.hpp"
#include "../src/helpers/config_service.hpp"
#include "../src/helpers/persona_manager.hpp"
#include "../src/registrar.hpp"
#include "../src/hosts/telegram_host.hpp"
#include "../src/hosts/mcp_host.hpp"

namespace rouen::hosts {
    mcp_host::mcp_host() {}

    void mcp_host::register_function(const std::string& card_type, const function_definition& func) {
        std::lock_guard<std::mutex> lock(mutex_);
        functions_[func.name] = func;
        card_functions_[card_type].push_back(func.name);
    }

    std::vector<mcp_host::function_definition> mcp_host::get_available_functions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<function_definition> res;
        for (const auto& [name, func] : functions_) {
            res.push_back(func);
        }
        return res;
    }

    mcp_host::execution_result mcp_host::execute_function(const std::string& name, const std::string& params) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = functions_.find(name);
        if (it != functions_.end() && it->second.handler) {
            std::string res = it->second.handler(params);
            return execution_result(true, res);
        }
        return execution_result(false, "", "Function not found");
    }
}

TEST(TelegramPerfTest, TestLocalMLXCompletion) {
    std::cout << "[Test] Starting Telegram host Local MLX completion test for chat_id 6885715531..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    auto host = rouen::hosts::telegram_host::get_host();
    int64_t test_chat_id = 6885715531;
    
    // Register mcp_host in registrar so telegram_host can use it
    auto mock_mcp = std::make_shared<rouen::hosts::mcp_host>();
    
    rouen::hosts::mcp_host::function_definition f(
        "run_local_command",
        "Executes a local terminal/shell command.",
        R"({"type":"object","properties":{"command":{"type":"string"}},"required":["command"]})",
        [](const std::string& /*args*/) -> std::string {
            FILE* pipe = popen("df -h", "r");
            if (!pipe) return "Error running df -h";
            char buffer[128];
            std::string res = "";
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                res += buffer;
            }
            pclose(pipe);
            return res;
        },
        "terminal"
    );
    mock_mcp->register_function("terminal", f);
    
    registrar::add<rouen::helpers::mcp_service>("mcp_service", mock_mcp);

    // Inject incoming message for user 6885715531
    std::string prompt = "sorry, can you give me a summary of disk usage?";
    bool injected = host->inject_incoming_message(test_chat_id, 6885715531, "Игнасио Родригэз", prompt);
    EXPECT_TRUE(injected);

    std::cout << "[Test] Injected message '" << prompt << "', waiting for Local MLX AI persona response..." << std::endl;

    // Poll for outgoing message in session
    std::string reply_text;
    bool received = false;
    for (int i = 0; i < 180; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto session_opt = host->get_session(test_chat_id);
        if (session_opt.has_value() && !session_opt->messages.empty()) {
            const auto& last_msg = session_opt->messages.back();
            if (last_msg.is_outgoing && !last_msg.text.empty() && !last_msg.text.starts_with("🧹") && last_msg.text != "HTTP error 429") {
                reply_text = last_msg.text;
                received = true;
                break;
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double duration_sec = std::chrono::duration<double>(end - start).count();

    std::cout << "\n==========================================" << std::endl;
    std::cout << "TEST RESULTS FOR LOCAL MLX TELEGRAM ROUTE:" << std::endl;
    std::cout << "Elapsed Time: " << duration_sec << " seconds" << std::endl;
    std::cout << "Received Response: " << (received ? "YES" : "NO") << std::endl;
    std::cout << "Response Text:\n" << reply_text << std::endl;
    std::cout << "==========================================\n" << std::endl;

    EXPECT_TRUE(received);
    EXPECT_FALSE(reply_text.contains("HTTP error 429"));
    EXPECT_FALSE(reply_text.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
