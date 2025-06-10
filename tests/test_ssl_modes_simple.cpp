#include <gtest/gtest.h>
#include <string>
#include <cstdlib>
#include "../src/helpers/fetch.hpp"

using namespace http;

// Simple test that directly tests the SSL modes from the fetch helper
TEST(SSLModeTest, StrictModeSettings) {
    // Test strict mode
    auto strict_opts = fetch::SSLOptions::strict();
    EXPECT_TRUE(strict_opts.verify_peer);
    EXPECT_TRUE(strict_opts.verify_host);
    EXPECT_TRUE(strict_opts.check_revocation);
}

TEST(SSLModeTest, RelaxedModeSettings) {
    // Test relaxed mode
    auto relaxed_opts = fetch::SSLOptions::relaxed();
    EXPECT_TRUE(relaxed_opts.verify_peer);
    EXPECT_TRUE(relaxed_opts.verify_host);
    EXPECT_FALSE(relaxed_opts.check_revocation);
    
    // Should include typical ciphers for corporate environments
    EXPECT_TRUE(relaxed_opts.cipher_list.find("ECDHE") != std::string::npos);
}

TEST(SSLModeTest, CompatibleModeSettings) {
    // Test compatible mode
    auto compat_opts = fetch::SSLOptions::compatible();
    EXPECT_TRUE(compat_opts.verify_peer);
    EXPECT_TRUE(compat_opts.verify_host);
    EXPECT_FALSE(compat_opts.check_revocation);
    
    // Should include more relaxed cipher list
    EXPECT_TRUE(compat_opts.cipher_list.find("ALL") != std::string::npos);
}

TEST(SSLModeTest, AtlassianModeSettings) {
    // Test Atlassian mode
    auto atlassian_opts = fetch::SSLOptions::atlassian();
    EXPECT_TRUE(atlassian_opts.verify_peer);
    EXPECT_TRUE(atlassian_opts.verify_host);
    EXPECT_FALSE(atlassian_opts.check_revocation);
    
    // Should include specific Atlassian ciphers
    EXPECT_TRUE(atlassian_opts.cipher_list.find("ECDHE-RSA-AES") != std::string::npos);
}

TEST(SSLModeTest, InsecureModeSettings) {
    // Test insecure mode
    auto insecure_opts = fetch::SSLOptions::insecure();
    EXPECT_FALSE(insecure_opts.verify_peer);
    EXPECT_FALSE(insecure_opts.verify_host);
    EXPECT_FALSE(insecure_opts.check_revocation);
}

// Test that environment variables can change SSL settings in fetch
TEST(SSLEnvTest, EnvironmentVariablesChangeMode) {
    // Save original environment variable if it exists
    const char* original_mode = std::getenv("ROUEN_SSL_MODE");
    
    // Test with relaxed mode
    setenv("ROUEN_SSL_MODE", "relaxed", 1);
    fetch relaxed_client;
    auto relaxed_opts = relaxed_client.get_ssl_options();
    EXPECT_FALSE(relaxed_opts.check_revocation) << "With ROUEN_SSL_MODE=relaxed, revocation checks should be disabled";
    
    // Test with strict mode
    setenv("ROUEN_SSL_MODE", "strict", 1);
    fetch strict_client;
    auto strict_opts = strict_client.get_ssl_options();
    EXPECT_TRUE(strict_opts.check_revocation) << "With ROUEN_SSL_MODE=strict, revocation checks should be enabled";
    
    // Test with insecure mode
    setenv("ROUEN_SSL_MODE", "insecure", 1);
    fetch insecure_client;
    auto insecure_opts = insecure_client.get_ssl_options();
    EXPECT_FALSE(insecure_opts.verify_peer) << "With ROUEN_SSL_MODE=insecure, peer verification should be disabled";
    
    // Restore original environment
    if (original_mode) {
        setenv("ROUEN_SSL_MODE", original_mode, 1);
    } else {
        unsetenv("ROUEN_SSL_MODE");
    }
}

// This simpler test verifies that the SSL mode dropdown works by simulating what happens when
// the user selects a different mode from the UI, without requiring a full ConfigService implementation
TEST(SSLUiSimulationTest, SelectingModeViaUiChangesSSLOptions) {
    // Save original environment variable if it exists
    const char* original_mode = std::getenv("ROUEN_SSL_MODE");
    
    // Simulate selecting "relaxed" SSL mode from UI dropdown
    // In the real app, this sets the environment variable and refreshes ConfigService
    setenv("ROUEN_SSL_MODE", "relaxed", 1);
    
    // When creating a new fetch client, it should pick up the new mode from the environment
    fetch client_after_ui_change;
    auto options = client_after_ui_change.get_ssl_options();
    
    // Verify options match relaxed mode
    EXPECT_TRUE(options.verify_peer);
    EXPECT_TRUE(options.verify_host);
    EXPECT_FALSE(options.check_revocation);
    
    // Restore original environment
    if (original_mode) {
        setenv("ROUEN_SSL_MODE", original_mode, 1);
    } else {
        unsetenv("ROUEN_SSL_MODE");
    }
}

// Test all SSL modes to confirm they're all configured correctly
TEST(SSLUiSimulationTest, AllModesConfiguredCorrectly) {
    // Save original environment variable if it exists
    const char* original_mode = std::getenv("ROUEN_SSL_MODE");
    
    struct TestMode {
        const char* name;
        bool verify_peer;
        bool verify_host;
        bool check_revocation;
    };
    
    // Define all the modes and their expected configurations
    TestMode modes[] = {
        {"strict", true, true, true},
        {"relaxed", true, true, false},
        {"compatible", true, true, false},
        {"atlassian", true, true, false},
        {"insecure", false, false, false}
    };
    
    for (const auto& mode : modes) {
        // Simulate selecting this mode from UI
        setenv("ROUEN_SSL_MODE", mode.name, 1);
        
        // Create a new client that should pick up this mode
        fetch client;
        auto options = client.get_ssl_options();
        
        // Verify settings match expected values
        EXPECT_EQ(options.verify_peer, mode.verify_peer) 
            << "Verify peer setting incorrect for mode " << mode.name;
        EXPECT_EQ(options.verify_host, mode.verify_host) 
            << "Verify host setting incorrect for mode " << mode.name;
        EXPECT_EQ(options.check_revocation, mode.check_revocation) 
            << "Check revocation setting incorrect for mode " << mode.name;
    }
    
    // Restore original environment
    if (original_mode) {
        setenv("ROUEN_SSL_MODE", original_mode, 1);
    } else {
        unsetenv("ROUEN_SSL_MODE");
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
