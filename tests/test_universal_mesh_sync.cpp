/**
 * Test: Universal Mesh Sync Host & Lifecycle
 * Purpose: Verifies Mesh-backed Universal Sync mode detection, granular item sync,
 *          tombstone publication, and roundtrip envelope delivery.
 * Category: Feature / Unit Test
 */

#include "../src/hosts/universal_sync_host.hpp"
#include "../src/hosts/rouen_mesh_host.hpp"
#include "../src/helpers/sync_crypto_service.hpp"
#include "../src/helpers/persona_manager.hpp"
#include "../src/hosts/llm_host.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace test_helpers {
    void assert_true(bool condition, const std::string& test_name) {
        if (condition) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED\n";
            exit(1);
        }
    }

    void assert_equal(size_t expected, size_t actual, const std::string& test_name) {
        if (expected == actual) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED (expected " << expected << ", got " << actual << ")\n";
            exit(1);
        }
    }

    void assert_string_equal(const std::string& expected, const std::string& actual, const std::string& test_name) {
        if (expected == actual) {
            std::cout << "✅ " << test_name << ": PASSED\n";
        } else {
            std::cout << "❌ " << test_name << ": FAILED (expected \"" << expected << "\", got \"" << actual << "\")\n";
            exit(1);
        }
    }
}

void test_mode_detection() {
    std::cout << "\n--- Testing Universal Sync Mode Detection ---\n";
    auto& sync = rouen::hosts::UniversalSyncHost::instance();
    auto& mesh = rouen::hosts::rouen_mesh_host::instance();

    // Default configuration (unpaired)
    rouen::hosts::mesh_host_config cfg{};
    cfg.is_paired = false;
    mesh.set_config(cfg);

    test_helpers::assert_true(!sync.is_mesh_sync_active(), "When mesh is not paired, is_mesh_sync_active() is false");

    // Simulating paired mesh node
    cfg.is_paired = true;
    cfg.client_id = "test-node-01";
    cfg.public_key = "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff";
    mesh.set_config(cfg);

    test_helpers::assert_true(sync.is_mesh_sync_active(), "When mesh is paired, is_mesh_sync_active() is true (supercedes Git)");
}

void test_granular_item_sync() {
    std::cout << "\n--- Testing Granular Real-Time Item Sync ---\n";
    auto& sync = rouen::hosts::UniversalSyncHost::instance();
    auto& mesh = rouen::hosts::rouen_mesh_host::instance();
    auto& crypto = rouen::sync::SyncCryptoService::instance();

    crypto.set_passphrase("TestMeshSyncPassphrase123");
    crypto.set_salt("test-salt-123");

    std::string sample_note = "# Architecture Meeting\n\nDiscussion on Mesh sync protocols.";
    bool ok = sync.sync_item("notes", "arch_meeting.md", sample_note, false);
    test_helpers::assert_true(ok, "sync_item successfully published granular note");

    // Verify written to mesh registry under sync/v1/notes/arch_meeting.md
    auto raw_val = mesh.get_registry_value("sync/v1/notes/arch_meeting.md");
    test_helpers::assert_true(raw_val.has_value(), "Registry contains key 'sync/v1/notes/arch_meeting.md'");

    // Verify envelope is properly encrypted and decrypts to original note
    auto dec_item = crypto.decrypt_envelope(*raw_val);
    test_helpers::assert_true(dec_item.has_value(), "Decrypted envelope successfully");
    test_helpers::assert_string_equal("notes", dec_item->dataset, "Dataset is 'notes'");
    test_helpers::assert_string_equal("arch_meeting.md", dec_item->key, "Key is 'arch_meeting.md'");
    test_helpers::assert_string_equal(sample_note, dec_item->plaintext, "Decrypted plaintext matches original note");
    test_helpers::assert_true(!dec_item->deleted, "Item is not marked as deleted");

    // Check entry count helper
    size_t notes_count = sync.get_mesh_entry_count("notes");
    test_helpers::assert_true(notes_count >= 1, "get_mesh_entry_count('notes') >= 1");

    // Test Tombstone deletion
    bool del_ok = sync.sync_item("notes", "arch_meeting.md", "", true);
    test_helpers::assert_true(del_ok, "sync_item successfully published tombstone");

    auto tomb_val = mesh.get_registry_value("sync/v1/notes/arch_meeting.md");
    test_helpers::assert_true(tomb_val.has_value(), "Tombstone exists in registry");

    auto dec_tomb = crypto.decrypt_envelope(*tomb_val);
    test_helpers::assert_true(dec_tomb.has_value(), "Decrypted tombstone successfully");
    test_helpers::assert_true(dec_tomb->deleted, "Tombstone has deleted = true");
}

void test_staging_cache_resolution() {
    std::cout << "\n--- Testing Staging Cache Path Resolution ---\n";
    auto& sync = rouen::hosts::UniversalSyncHost::instance();
    auto cache_dir = sync.get_mesh_cache_path();

    test_helpers::assert_true(!cache_dir.empty(), "Mesh cache path is not empty");
    test_helpers::assert_true(cache_dir.string().find("rouen-mesh-sync") != std::string::npos, "Mesh cache path points to rouen-mesh-sync");
}

void test_canary_safeguards() {
    std::cout << "\n--- Testing Cluster Key Verification Canary Safeguards ---\n";
    auto& sync = rouen::hosts::UniversalSyncHost::instance();
    auto& crypto = rouen::sync::SyncCryptoService::instance();
    auto& mesh = rouen::hosts::rouen_mesh_host::instance();

    // Ensure mesh is active
    rouen::hosts::mesh_host_config cfg{};
    cfg.is_paired = true;
    cfg.client_id = "test-node-canary";
    mesh.set_config(cfg);

    // Initialize with Node A passphrase
    crypto.set_passphrase("NodeA_CorrectPassphrase456");
    bool init_ok = sync.initialize_canary();
    test_helpers::assert_true(init_ok, "initialize_canary() succeeded");

    // With matching passphrase, canary must be Valid
    test_helpers::assert_true(sync.check_canary() == rouen::hosts::UniversalSyncHost::CanaryStatus::Valid,
                              "Canary status is Valid with matching passphrase");
    test_helpers::assert_true(!sync.is_passphrase_mismatch(), "is_passphrase_mismatch() is false");

    // Simulate Node B with a mismatched passphrase
    crypto.set_passphrase("NodeB_WrongPassphrase789");
    test_helpers::assert_true(sync.check_canary() == rouen::hosts::UniversalSyncHost::CanaryStatus::Mismatch,
                              "Canary status is Mismatch with incorrect passphrase");
    test_helpers::assert_true(sync.is_passphrase_mismatch(), "is_passphrase_mismatch() is true");

    // Any sync attempt on mismatched node must be rejected and halted
    bool item_attempt = sync.sync_item("notes", "unauthorized.md", "leak", false);
    test_helpers::assert_true(!item_attempt, "sync_item rejected when passphrase mismatched");

    auto blocked_val = mesh.get_registry_value("sync/v1/notes/unauthorized.md");
    test_helpers::assert_true(!blocked_val.has_value(), "Unauthorized item was not written to mesh registry");

    // Restore correct passphrase
    crypto.set_passphrase("NodeA_CorrectPassphrase456");
    test_helpers::assert_true(!sync.is_passphrase_mismatch(), "is_passphrase_mismatch() clears after entering correct passphrase");
    bool valid_attempt = sync.sync_item("notes", "authorized.md", "safe data", false);
    test_helpers::assert_true(valid_attempt, "sync_item succeeds once correct passphrase is restored");
}

void test_incremental_and_periodic_sync() {
    std::cout << "\n--- Testing Incremental & Periodic Background Sync ---\n";
    auto& sync = rouen::hosts::UniversalSyncHost::instance();
    auto& mesh = rouen::hosts::rouen_mesh_host::instance();
    auto& crypto = rouen::sync::SyncCryptoService::instance();

    rouen::hosts::mesh_host_config cfg{};
    cfg.is_paired = true;
    cfg.client_id = "test-node-incremental";
    mesh.set_config(cfg);
    crypto.set_passphrase("IncrementalSecretKey123");
    sync.initialize_canary();

    // 1. Initial push (full)
    bool out_full = sync.sync_out_mesh("Initial full push", false /* full */);
    test_helpers::assert_true(out_full, "sync_out_mesh (full) succeeded");

    // 2. Incremental push immediately after without changing local files
    bool out_inc = sync.sync_out_mesh("Incremental push", true /* incremental */);
    test_helpers::assert_true(out_inc, "sync_out_mesh (incremental) succeeded");
    test_helpers::assert_true(sync.get_status_message().find("0 published") != std::string::npos,
                              "Incremental push skipped untouched files (0 published across wire)");

    // 3. Initial pull to record baseline timestamps
    bool in_base = sync.sync_in_mesh(false /* don't import config */, false /* full */);
    test_helpers::assert_true(in_base, "sync_in_mesh (baseline) succeeded");

    // 4. Subsequent incremental pull without any new registry updates
    bool in_inc = sync.sync_in_mesh(false /* don't import config */, true /* incremental */);
    test_helpers::assert_true(in_inc, "sync_in_mesh (incremental) succeeded");
    test_helpers::assert_true(sync.get_status_message().find("0 updates applied") != std::string::npos,
                              "Incremental pull skipped unchanged registry entries");

    // 4. Test periodic sync thread lifecycle
    sync.start_periodic_sync(2); // 2 second interval for testing
    test_helpers::assert_true(sync.is_periodic_sync_running(), "Periodic sync is running");
    test_helpers::assert_equal(2, sync.get_periodic_interval_seconds(), "Periodic interval is 2 seconds");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    sync.stop_periodic_sync();
    test_helpers::assert_true(!sync.is_periodic_sync_running(), "Periodic sync stopped cleanly");
}

void test_personas_universal_sync() {
    std::cout << "\n--- Testing Personas Configurations in Universal Sync ---\n";
    auto& sync = rouen::hosts::UniversalSyncHost::instance();
    auto& mesh = rouen::hosts::rouen_mesh_host::instance();
    auto& crypto = rouen::sync::SyncCryptoService::instance();
    auto& pm = rouen::helpers::PersonaManager::instance();

    // Ensure mesh is paired and active
    rouen::hosts::mesh_host_config cfg{};
    cfg.is_paired = true;
    cfg.client_id = "test-node-personas";
    mesh.set_config(cfg);
    crypto.set_passphrase("PersonaSyncSecretKey456");
    sync.initialize_canary();

    // 1. Test export_to_directory
    auto temp_dir = std::filesystem::temp_directory_path() / "test_personas_export";
    std::filesystem::remove_all(temp_dir);
    bool exp_ok = pm.export_to_directory(temp_dir);
    test_helpers::assert_true(exp_ok, "PersonaManager::export_to_directory succeeded");
    test_helpers::assert_true(std::filesystem::exists(temp_dir / "rouen-assistant.json"), "rouen-assistant.json was exported");
    test_helpers::assert_true(std::filesystem::exists(temp_dir / "active.json"), "active.json metadata was exported");

    // 2. Test granular sync_item for a custom persona
    rouen::helpers::Persona custom_p;
    custom_p.name = "Mesh Specialist";
    custom_p.description = "Expert in peer-to-peer mesh operations and sync.";
    custom_p.allowed_mcps = {"mesh", "terminal"};
    custom_p.system_prompt = "You are Mesh Specialist.";
    custom_p.llm_config_name = "Local MLX";
    custom_p.enable_search = false;
    custom_p.temperature = 0.2f;

    std::string custom_json = glz::write<glz::opts{.prettify = true}>(custom_p).value_or("");
    bool sync_item_ok = sync.sync_item("personas", "mesh-specialist.json", custom_json, false);
    test_helpers::assert_true(sync_item_ok, "sync_item succeeded for custom persona");

    // 3. Verify registry contains encrypted persona envelope
    auto p_val = mesh.get_registry_value("sync/v1/personas/mesh-specialist.json");
    test_helpers::assert_true(p_val.has_value(), "Registry contains key 'sync/v1/personas/mesh-specialist.json'");

    auto dec_p = crypto.decrypt_envelope(*p_val);
    test_helpers::assert_true(dec_p.has_value(), "Decrypted persona envelope successfully");
    test_helpers::assert_string_equal("personas", dec_p->dataset, "Dataset is 'personas'");
    test_helpers::assert_string_equal("mesh-specialist.json", dec_p->key, "Key is 'mesh-specialist.json'");
    test_helpers::assert_true(dec_p->plaintext.find("Mesh Specialist") != std::string::npos, "Decrypted plaintext contains 'Mesh Specialist'");
    test_helpers::assert_true(!dec_p->deleted, "Persona is not deleted");

    // 4. Verify get_mesh_entry_count("personas")
    size_t personas_count = sync.get_mesh_entry_count("personas");
    test_helpers::assert_true(personas_count >= 1, "get_mesh_entry_count('personas') >= 1");

    // 5. Test import_from_directory
    auto import_dir = std::filesystem::temp_directory_path() / "test_personas_import";
    std::filesystem::remove_all(import_dir);
    std::filesystem::create_directories(import_dir);
    {
        std::ofstream out(import_dir / "mesh-specialist.json");
        out << custom_json;
    }
    bool imp_ok = pm.import_from_directory(import_dir);
    test_helpers::assert_true(imp_ok, "PersonaManager::import_from_directory succeeded");

    bool found_imported = false;
    for (const auto& p : pm.get_personas()) {
        if (p.name == "Mesh Specialist") {
            found_imported = true;
            break;
        }
    }
    test_helpers::assert_true(found_imported, "Imported persona was successfully registered in PersonaManager");

    // Restore original personas from export
    pm.import_from_directory(temp_dir);

    // 6. Test LLM Config Sync under config/test_llm_configs.json
    std::string sample_llm_cfg = R"({"default_config_name":"Gemini Flash","configs":[{"name":"Gemini Flash","provider":"gemini","model_name":"gemini-3.8-flash","base_url":"https://generativelanguage.googleapis.com","api_key":"test_key"},{"name":"Local MLX","provider":"custom","base_url":"http://localhost:8098/v1","model_name":"mlx-community/Qwen3.5-9B-MLX-4bit","api_key":"mlx-local"}]})";
    bool llm_sync_ok = sync.sync_item("config", "test_llm_configs.json", sample_llm_cfg, false);
    test_helpers::assert_true(llm_sync_ok, "sync_item succeeded for test_llm_configs.json");

    auto llm_val = mesh.get_registry_value("sync/v1/config/test_llm_configs.json");
    test_helpers::assert_true(llm_val.has_value(), "Registry contains key 'sync/v1/config/test_llm_configs.json'");

    auto dec_llm = crypto.decrypt_envelope(*llm_val);
    test_helpers::assert_true(dec_llm.has_value(), "Decrypted llm_configs envelope successfully");
    test_helpers::assert_string_equal("config", dec_llm->dataset, "Dataset is 'config'");
    test_helpers::assert_string_equal("test_llm_configs.json", dec_llm->key, "Key is 'test_llm_configs.json'");

    // 7. Test Persona Tombstone deletion
    bool del_p_ok = sync.sync_item("personas", "mesh-specialist.json", "", true);
    test_helpers::assert_true(del_p_ok, "sync_item successfully published persona tombstone");

    auto tomb_p_val = mesh.get_registry_value("sync/v1/personas/mesh-specialist.json");
    test_helpers::assert_true(tomb_p_val.has_value(), "Persona tombstone exists in registry");

    auto dec_p_tomb = crypto.decrypt_envelope(*tomb_p_val);
    test_helpers::assert_true(dec_p_tomb.has_value(), "Decrypted persona tombstone successfully");
    test_helpers::assert_true(dec_p_tomb->deleted, "Persona tombstone has deleted = true");

    // Clean up temp directories
    std::filesystem::remove_all(temp_dir);
    std::filesystem::remove_all(import_dir);
}

void test_persona_llm_config_resolution() {
    std::cout << "\n--- Testing Persona LLM Config Resolution & Restoration ---\n";
    auto& lcm = rouen::hosts::LLMConfigManager::instance();

    // 1. Verify standard configs restoration even if an incomplete set is passed
    lcm.ensure_standard_configs();
    test_helpers::assert_string_equal("Gemini Flash", lcm.get_default_config_name(), "Default config is Gemini Flash");
    const auto* gemini_cfg = lcm.get_config("Gemini Flash");
    test_helpers::assert_true(gemini_cfg != nullptr, "Gemini Flash config exists in LLMConfigManager");
    if (gemini_cfg) {
        test_helpers::assert_string_equal("gemini", gemini_cfg->provider, "Gemini Flash provider is 'gemini'");
        test_helpers::assert_string_equal("gemini-3.8-flash", gemini_cfg->model_name, "Gemini Flash model is 'gemini-3.8-flash'");
    }

    const auto* mlx_cfg = lcm.get_config("Local MLX");
    test_helpers::assert_true(mlx_cfg != nullptr, "Local MLX config exists in LLMConfigManager");
    if (mlx_cfg) {
        test_helpers::assert_string_equal("custom", mlx_cfg->provider, "Local MLX provider is 'custom'");
        test_helpers::assert_true(!mlx_cfg->api_key.empty(), "Local MLX has non-empty api_key");
    }

    // 2. Test get_current_config for Rouen Assistant
    auto settings = rouen::hosts::LLMHost::get_current_config("Gemini Flash");
    test_helpers::assert_true(settings.provider == rouen::hosts::LLMHost::Provider::GEMINI, "Resolved Gemini provider");
    test_helpers::assert_string_equal("gemini-3.8-flash", settings.model_name, "Model name is 'gemini-3.8-flash'");

    // 3. Test Local MLX settings resolution
    auto mlx_settings = rouen::hosts::LLMHost::get_current_config("Local MLX");
    test_helpers::assert_true(mlx_settings.provider == rouen::hosts::LLMHost::Provider::CUSTOM, "Local MLX provider is CUSTOM");
    test_helpers::assert_true(mlx_settings.is_configured, "Local MLX is configured");
    test_helpers::assert_true(!mlx_settings.api_key.empty(), "Local MLX settings api_key is not empty");
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Starting Universal Mesh Sync Unit Tests\n";
    std::cout << "========================================\n";

    test_mode_detection();
    test_granular_item_sync();
    test_staging_cache_resolution();
    test_canary_safeguards();
    test_incremental_and_periodic_sync();
    test_personas_universal_sync();
    test_persona_llm_config_resolution();

    std::cout << "\n🎉 ALL UNIVERSAL MESH SYNC TESTS PASSED!\n";
    return 0;
}
