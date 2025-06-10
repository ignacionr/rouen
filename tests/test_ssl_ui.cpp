#include <gtest/gtest.h>
#include "../src/helpers/fetch.hpp"
#include "../src/helpers/config_service.hpp"
#include <string>
#include <cstdlib>

using namespace http;
using namespace rouen::helpers;

// Mock for ConfigServiceInitializer, needed when testing in isolation
namespace rouen::helpers {
    class ConfigServiceInitializer {
    public:
        static void initialize() {
            // Already initialized by test setup
        }
    };
}

// Test fixture for SSL UI configuration tests
class SSLUIConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Store original environment variable if it exists
        original_ssl_mode = std::getenv("ROUEN_SSL_MODE");
        
        // Clear any existing SSL mode setting
        unsetenv("ROUEN_SSL_MODE");
        
        // Get fresh ConfigService instance
        config_service = ConfigService::instance();
        config_service->refresh_cache();
    }
    
    void TearDown() override {
        // Restore original environment variable if it existed
        if (original_ssl_mode) {
            setenv("ROUEN_SSL_MODE", original_ssl_mode, 1);
        } else {
            unsetenv("ROUEN_SSL_MODE");
        }
        
        config_service->refresh_cache();
    }
    
    // Helper to simulate UI selecting a new SSL mode
    void simulate_ui_selection(const std::string& mode) {
        // Set environment variable as the UI would
        setenv("ROUEN_SSL_MODE", mode.c_str(), 1);
        
        // Refresh configuration cache
        config_service->refresh_cache();
    }
    
    const char* original_ssl_mode = nullptr;
    std::shared_ptr<ConfigService> config_service;
};

// Test default SSL mode
TEST_F(SSLUIConfigTest, DefaultSSLMode) {
    // Default should be "strict"
    auto default_mode = config_service->get_env_optional("ROUEN_SSL_MODE").value_or("strict");
    EXPECT_EQ(default_mode, "strict");
    
    // Create fetch instance which will use default SSL settings
    fetch client;
    auto options = client.get_ssl_options();
    
    // Verify default strict settings
    EXPECT_TRUE(options.verify_peer);
    EXPECT_TRUE(options.verify_host);
    EXPECT_TRUE(options.check_revocation);
    
    // Also verify the cipher list is the strict default
    EXPECT_FALSE(options.cipher_list.empty());
}

// Test changing SSL mode through UI simulation
TEST_F(SSLUIConfigTest, UISelectionChangesSSLOptions) {
    // Simulate UI selecting "relaxed" mode
    simulate_ui_selection("relaxed");
    
    // Verify config service has the new value
    auto mode = config_service->get_env("ROUEN_SSL_MODE");
    EXPECT_EQ(mode, "relaxed");
    
    // Create a new fetch client that should pick up the relaxed mode
    fetch client;
    auto options = client.get_ssl_options();
    
    // Verify relaxed settings
    EXPECT_TRUE(options.verify_peer);
    EXPECT_TRUE(options.verify_host);
    EXPECT_FALSE(options.check_revocation);
}

// Test all SSL modes
TEST_F(SSLUIConfigTest, AllSSLModes) {
    struct TestMode {
        std::string name;
        bool verify_peer;
        bool verify_host;
        bool check_revocation;
    };
    
    std::vector<TestMode> modes = {
        {"strict", true, true, true},
        {"relaxed", true, true, false},
        {"compatible", true, true, false},
        {"atlassian", true, true, false},
        {"insecure", false, false, false}
    };
    
    for (const auto& test_mode : modes) {
        // Simulate UI selecting this mode
        simulate_ui_selection(test_mode.name);
        
        // Verify config service has the new value
        auto mode = config_service->get_env("ROUEN_SSL_MODE");
        EXPECT_EQ(mode, test_mode.name);
        
        // Create a new fetch client that should pick up the new mode
        fetch client;
        auto options = client.get_ssl_options();
        
        // Verify settings match expected values
        EXPECT_EQ(options.verify_peer, test_mode.verify_peer);
        EXPECT_EQ(options.verify_host, test_mode.verify_host);
        EXPECT_EQ(options.check_revocation, test_mode.check_revocation);
    }
}

// Test specific settings for Atlassian mode
TEST_F(SSLUIConfigTest, AtlassianModeHasSpecialCiphers) {
    // Simulate UI selecting "atlassian" mode
    simulate_ui_selection("atlassian");
    
    // Create fetch instance which should use Atlassian SSL settings
    fetch client;
    auto options = client.get_ssl_options();
    
    // Verify Atlassian mode has specific cipher configurations
    EXPECT_TRUE(options.cipher_list.find("ECDHE-RSA-AES256-GCM-SHA384") != std::string::npos);
    EXPECT_TRUE(options.cipher_list.find("AES256-GCM-SHA384") != std::string::npos);
}

// Main test runner
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
