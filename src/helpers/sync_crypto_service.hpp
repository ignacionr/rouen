#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "glaze_include.hpp"

namespace rouen::sync {

    struct sync_envelope {
        uint32_t version{1};
        std::string dataset;
        std::string key;
        uint64_t updated_at{0};
        bool deleted{false};
        std::string iv_b64;
        std::string tag_b64;
        std::string ciphertext_b64;
    };

    struct sync_decrypted_item {
        std::string dataset;
        std::string key;
        uint64_t updated_at{0};
        bool deleted{false};
        std::string plaintext;
    };

    /**
     * @brief SyncCryptoService handles Client-Side End-to-End Encryption (E2EE)
     * using AES-256-GCM AEAD and PBKDF2-HMAC-SHA256 key derivation.
     */
    class SyncCryptoService {
    public:
        static SyncCryptoService& instance();

        void set_passphrase(const std::string& passphrase);
        [[nodiscard]] std::string get_passphrase() const;

        void set_salt(const std::string& salt);
        [[nodiscard]] std::string get_salt() const;

        [[nodiscard]] bool is_configured() const;

        // Derives 32-byte AES-256 key via PBKDF2-HMAC-SHA256
        [[nodiscard]] std::vector<uint8_t> get_derived_key() const;

        // Envelope creation and extraction
        [[nodiscard]] std::optional<std::string> encrypt_envelope(
            std::string_view dataset,
            std::string_view key,
            std::string_view plaintext,
            uint64_t updated_at = 0,
            bool deleted = false
        ) const;

        [[nodiscard]] std::optional<sync_decrypted_item> decrypt_envelope(
            std::string_view envelope_json
        ) const;

        // Direct low-level AES-256-GCM AEAD helpers
        static std::optional<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> encrypt_gcm(
            std::span<const uint8_t> key,
            std::span<const uint8_t> iv,
            std::span<const uint8_t> plaintext,
            std::span<const uint8_t> aad = {}
        );

        static std::optional<std::vector<uint8_t>> decrypt_gcm(
            std::span<const uint8_t> key,
            std::span<const uint8_t> iv,
            std::span<const uint8_t> tag,
            std::span<const uint8_t> ciphertext,
            std::span<const uint8_t> aad = {}
        );

        // Base64 utilities
        static std::string base64_encode(std::span<const uint8_t> data);
        static std::optional<std::vector<uint8_t>> base64_decode(std::string_view text);

    private:
        SyncCryptoService();
        ~SyncCryptoService() = default;
        SyncCryptoService(const SyncCryptoService&) = delete;
        SyncCryptoService& operator=(const SyncCryptoService&) = delete;

        [[nodiscard]] std::string resolve_effective_passphrase() const;
        [[nodiscard]] std::string resolve_effective_salt() const;

        mutable std::mutex mutex_;
        std::string passphrase_;
        std::string salt_;
        mutable std::string cached_passphrase_;
        mutable std::string cached_salt_;
        mutable std::vector<uint8_t> cached_key_;
    };

} // namespace rouen::sync
