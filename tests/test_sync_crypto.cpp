/**
 * Test: Universal Sync Client-Side Crypto Service (E2EE)
 * Purpose: Verifies AES-256-GCM authenticated encryption/decryption, PBKDF2 key derivation,
 *          tamper protection, AAD binding, and envelope serialization for Mesh Universal Sync.
 * Category: Feature / Unit Test
 */

#include "../src/helpers/sync_crypto_service.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

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

void test_base64_utilities() {
    std::cout << "\n--- Testing Base64 Encode and Decode ---\n";
    using namespace rouen::sync;

    std::string sample = "Hello, Rouen Mesh Sync with AES-256-GCM!";
    std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(sample.data()), sample.size());

    std::string b64 = SyncCryptoService::base64_encode(data);
    test_helpers::assert_true(!b64.empty(), "Base64 encoded string is not empty");

    auto decoded = SyncCryptoService::base64_decode(b64);
    test_helpers::assert_true(decoded.has_value(), "Base64 decoded successfully");
    std::string recovered(reinterpret_cast<const char*>(decoded->data()), decoded->size());
    test_helpers::assert_string_equal(sample, recovered, "Base64 recovered text matches original");

    // Test padding variations (1 byte, 2 bytes, 3 bytes)
    for (size_t len = 1; len <= 5; ++len) {
        std::vector<uint8_t> test_bytes(len);
        for (size_t i = 0; i < len; ++i) test_bytes[i] = static_cast<uint8_t>(i + 65);
        std::string enc = SyncCryptoService::base64_encode(test_bytes);
        auto dec = SyncCryptoService::base64_decode(enc);
        test_helpers::assert_true(dec.has_value() && dec->size() == len, "Base64 length " + std::to_string(len) + " matches");
    }
}

void test_low_level_aes_gcm() {
    std::cout << "\n--- Testing Low-Level AES-256-GCM AEAD ---\n";
    using namespace rouen::sync;

    std::vector<uint8_t> key(32, 0x42); // 256-bit test key
    std::vector<uint8_t> iv(12, 0x11);  // 96-bit test IV
    std::string plaintext = "Confidential Contact: John Doe, +1 555 123456";
    std::string aad = "rouen-sync-v1:contacts:john_doe";

    std::span<const uint8_t> pt_span(reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
    std::span<const uint8_t> aad_span(reinterpret_cast<const uint8_t*>(aad.data()), aad.size());

    auto enc_res = SyncCryptoService::encrypt_gcm(key, iv, pt_span, aad_span);
    test_helpers::assert_true(enc_res.has_value(), "AES-GCM encryption succeeded");

    const auto& ciphertext = enc_res->first;
    const auto& tag = enc_res->second;

    test_helpers::assert_equal(16, tag.size(), "Authentication tag is 16 bytes");
    test_helpers::assert_equal(plaintext.size(), ciphertext.size(), "Ciphertext size matches plaintext size");

    // Successful decryption
    auto dec_res = SyncCryptoService::decrypt_gcm(key, iv, tag, ciphertext, aad_span);
    test_helpers::assert_true(dec_res.has_value(), "AES-GCM decryption succeeded");
    std::string decrypted_str(reinterpret_cast<const char*>(dec_res->data()), dec_res->size());
    test_helpers::assert_string_equal(plaintext, decrypted_str, "Decrypted text matches original plaintext");

    // Tampered ciphertext fails
    auto tampered_ct = ciphertext;
    tampered_ct[0] ^= 0x01;
    auto fail_ct = SyncCryptoService::decrypt_gcm(key, iv, tag, tampered_ct, aad_span);
    test_helpers::assert_true(!fail_ct.has_value(), "Tampered ciphertext rejected by authentication tag");

    // Tampered tag fails
    auto tampered_tag = tag;
    tampered_tag[0] ^= 0x01;
    auto fail_tag = SyncCryptoService::decrypt_gcm(key, iv, tampered_tag, ciphertext, aad_span);
    test_helpers::assert_true(!fail_tag.has_value(), "Tampered tag rejected by authentication");

    // Mismatched AAD fails (cross-record / transplantation attack prevention)
    std::string wrong_aad = "rouen-sync-v1:notes:john_doe";
    std::span<const uint8_t> wrong_aad_span(reinterpret_cast<const uint8_t*>(wrong_aad.data()), wrong_aad.size());
    auto fail_aad = SyncCryptoService::decrypt_gcm(key, iv, tag, ciphertext, wrong_aad_span);
    test_helpers::assert_true(!fail_aad.has_value(), "Mismatched AAD rejected (transplantation protection)");
}

void test_pbkdf2_key_derivation() {
    std::cout << "\n--- Testing PBKDF2 Key Derivation ---\n";
    using namespace rouen::sync;

    auto& crypto = SyncCryptoService::instance();
    crypto.set_passphrase("CorrectHorseBatteryStaple123!");
    crypto.set_salt("custom-salt-test-rouen");

    auto key1 = crypto.get_derived_key();
    test_helpers::assert_equal(32, key1.size(), "Derived key is 32 bytes (256 bits)");

    auto key2 = crypto.get_derived_key();
    test_helpers::assert_true(key1 == key2, "PBKDF2 key derivation is deterministic for same passphrase and salt");

    // Changing passphrase changes key
    crypto.set_passphrase("DifferentPassword456!");
    auto key3 = crypto.get_derived_key();
    test_helpers::assert_true(key1 != key3, "Changing passphrase derives distinct key");
}

void test_sync_envelope_lifecycle() {
    std::cout << "\n--- Testing Sync Envelope Lifecycle & Serialization ---\n";
    using namespace rouen::sync;

    auto& crypto = SyncCryptoService::instance();
    crypto.set_passphrase("MasterSyncKey2026");
    crypto.set_salt("rouen-cluster-salt-xyz");

    std::string note_content = "# Project Rouen\n\nMesh-backed Universal Sync architecture.";
    uint64_t timestamp = 1728054000;

    // 1. Encrypt envelope
    auto env_json_opt = crypto.encrypt_envelope("notes", "project_rouen", note_content, timestamp, false);
    test_helpers::assert_true(env_json_opt.has_value(), "Encrypted envelope JSON generated");

    std::string env_json = *env_json_opt;
    test_helpers::assert_true(env_json.find("\"dataset\":\"notes\"") != std::string::npos, "Envelope JSON contains dataset");
    test_helpers::assert_true(env_json.find("\"key\":\"project_rouen\"") != std::string::npos, "Envelope JSON contains key");
    test_helpers::assert_true(env_json.find(note_content) == std::string::npos, "Plaintext content is not exposed in envelope JSON");

    // 2. Decrypt envelope
    auto dec_item_opt = crypto.decrypt_envelope(env_json);
    test_helpers::assert_true(dec_item_opt.has_value(), "Envelope decrypted successfully");

    const auto& dec_item = *dec_item_opt;
    test_helpers::assert_string_equal("notes", dec_item.dataset, "Decrypted dataset matches");
    test_helpers::assert_string_equal("project_rouen", dec_item.key, "Decrypted key matches");
    test_helpers::assert_equal(timestamp, dec_item.updated_at, "Decrypted timestamp matches");
    test_helpers::assert_true(!dec_item.deleted, "Item is not marked as deleted");
    test_helpers::assert_string_equal(note_content, dec_item.plaintext, "Decrypted plaintext matches original content");

    // 3. Test Tombstone envelope
    auto tombstone_json_opt = crypto.encrypt_envelope("notes", "project_rouen", "", timestamp + 10, true);
    test_helpers::assert_true(tombstone_json_opt.has_value(), "Tombstone envelope generated");

    auto dec_tombstone_opt = crypto.decrypt_envelope(*tombstone_json_opt);
    test_helpers::assert_true(dec_tombstone_opt.has_value(), "Tombstone parsed successfully");
    test_helpers::assert_true(dec_tombstone_opt->deleted, "Parsed item is marked deleted = true");
    test_helpers::assert_equal(timestamp + 10, dec_tombstone_opt->updated_at, "Tombstone timestamp matches");
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Starting Universal Sync Crypto Unit Tests\n";
    std::cout << "========================================\n";

    test_base64_utilities();
    test_low_level_aes_gcm();
    test_pbkdf2_key_derivation();
    test_sync_envelope_lifecycle();

    std::cout << "\n🎉 ALL UNIVERSAL SYNC CRYPTO TESTS PASSED!\n";
    return 0;
}
