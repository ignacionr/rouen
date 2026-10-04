#include "sync_crypto_service.hpp"

#include <chrono>
#include <cstring>
#include <format>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "config_service.hpp"
#include "../hosts/rouen_mesh_host.hpp"

namespace rouen::sync {

    SyncCryptoService& SyncCryptoService::instance() {
        static SyncCryptoService instance;
        return instance;
    }

    SyncCryptoService::SyncCryptoService() {
        auto config = helpers::ConfigService::instance();
        if (config) {
            passphrase_ = config->get_env("ROUEN_SYNC_PASSPHRASE");
            salt_ = config->get_env("ROUEN_SYNC_SALT");
        }
    }

    void SyncCryptoService::set_passphrase(const std::string& passphrase) {
        std::lock_guard<std::mutex> lock(mutex_);
        passphrase_ = passphrase;
        auto config = helpers::ConfigService::instance();
        if (config) {
            config->set_env_value("ROUEN_SYNC_PASSPHRASE", passphrase, true);
        }
    }

    std::string SyncCryptoService::get_passphrase() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return passphrase_;
    }

    void SyncCryptoService::set_salt(const std::string& salt) {
        std::lock_guard<std::mutex> lock(mutex_);
        salt_ = salt;
        auto config = helpers::ConfigService::instance();
        if (config) {
            config->set_env_value("ROUEN_SYNC_SALT", salt, true);
        }
    }

    std::string SyncCryptoService::get_salt() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return salt_;
    }

    bool SyncCryptoService::is_configured() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !resolve_effective_passphrase().empty();
    }

    std::string SyncCryptoService::resolve_effective_passphrase() const {
        if (!passphrase_.empty()) {
            return passphrase_;
        }
        auto config = helpers::ConfigService::instance();
        if (config) {
            std::string env_pass = config->get_env("ROUEN_SYNC_PASSPHRASE");
            if (!env_pass.empty()) return env_pass;
        }

        // Automatic Mesh fallback: If paired, use the mesh public key as default cluster passphrase
        auto mesh_cfg = hosts::rouen_mesh_host::instance().get_config();
        if (!mesh_cfg.public_key.empty()) {
            return "rouen-mesh-cluster:" + mesh_cfg.public_key;
        }
        return "";
    }

    std::string SyncCryptoService::resolve_effective_salt() const {
        if (!salt_.empty()) {
            return salt_;
        }
        auto config = helpers::ConfigService::instance();
        if (config) {
            std::string env_salt = config->get_env("ROUEN_SYNC_SALT");
            if (!env_salt.empty()) return env_salt;
        }
        return "rouen-universal-sync-salt-v1";
    }

    std::vector<uint8_t> SyncCryptoService::get_derived_key() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string effective_pass = resolve_effective_passphrase();
        if (effective_pass.empty()) {
            return {};
        }

        std::string effective_salt = resolve_effective_salt();
        std::vector<uint8_t> key(32); // 256 bits

        int res = PKCS5_PBKDF2_HMAC(
            effective_pass.data(),
            static_cast<int>(effective_pass.size()),
            reinterpret_cast<const unsigned char*>(effective_salt.data()),
            static_cast<int>(effective_salt.size()),
            100000,
            EVP_sha256(),
            static_cast<int>(key.size()),
            key.data()
        );

        if (res != 1) {
            return {};
        }

        return key;
    }

    std::optional<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> SyncCryptoService::encrypt_gcm(
        std::span<const uint8_t> key,
        std::span<const uint8_t> iv,
        std::span<const uint8_t> plaintext,
        std::span<const uint8_t> aad
    ) {
        if (key.size() != 32 || iv.size() != 12) {
            return std::nullopt;
        }

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return std::nullopt;

        auto cleanup = [ctx]() { EVP_CIPHER_CTX_free(ctx); };

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            cleanup();
            return std::nullopt;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) {
            cleanup();
            return std::nullopt;
        }

        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
            cleanup();
            return std::nullopt;
        }

        int len = 0;
        if (!aad.empty()) {
            if (EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1) {
                cleanup();
                return std::nullopt;
            }
        }

        std::vector<uint8_t> ciphertext(plaintext.size() + 16);
        int ciphertext_len = 0;

        if (!plaintext.empty()) {
            if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
                cleanup();
                return std::nullopt;
            }
            ciphertext_len = len;
        }

        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + ciphertext_len, &len) != 1) {
            cleanup();
            return std::nullopt;
        }
        ciphertext_len += len;
        ciphertext.resize(static_cast<size_t>(ciphertext_len));

        std::vector<uint8_t> tag(16);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
            cleanup();
            return std::nullopt;
        }

        cleanup();
        return std::make_pair(std::move(ciphertext), std::move(tag));
    }

    std::optional<std::vector<uint8_t>> SyncCryptoService::decrypt_gcm(
        std::span<const uint8_t> key,
        std::span<const uint8_t> iv,
        std::span<const uint8_t> tag,
        std::span<const uint8_t> ciphertext,
        std::span<const uint8_t> aad
    ) {
        if (key.size() != 32 || iv.size() != 12 || tag.size() != 16) {
            return std::nullopt;
        }

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return std::nullopt;

        auto cleanup = [ctx]() { EVP_CIPHER_CTX_free(ctx); };

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            cleanup();
            return std::nullopt;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) {
            cleanup();
            return std::nullopt;
        }

        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
            cleanup();
            return std::nullopt;
        }

        int len = 0;
        if (!aad.empty()) {
            if (EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1) {
                cleanup();
                return std::nullopt;
            }
        }

        std::vector<uint8_t> plaintext(ciphertext.size());
        int plaintext_len = 0;

        if (!ciphertext.empty()) {
            if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
                cleanup();
                return std::nullopt;
            }
            plaintext_len = len;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), const_cast<uint8_t*>(tag.data())) != 1) {
            cleanup();
            return std::nullopt;
        }

        int res = EVP_DecryptFinal_ex(ctx, plaintext.data() + plaintext_len, &len);
        cleanup();

        if (res <= 0) {
            return std::nullopt; // Authentication failed (corrupted or tampered)
        }

        plaintext_len += len;
        plaintext.resize(static_cast<size_t>(plaintext_len));
        return plaintext;
    }

    std::string SyncCryptoService::base64_encode(std::span<const uint8_t> data) {
        static constexpr std::string_view chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve(((data.size() + 2) / 3) * 4);

        int val = 0, valb = -6;
        for (uint8_t c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                result.push_back(chars[static_cast<size_t>((val >> valb) & 0x3F)]);
                valb -= 6;
            }
        }
        if (valb > -6) {
            result.push_back(chars[static_cast<size_t>(((val << 8) >> (valb + 8)) & 0x3F)]);
        }
        while (result.size() % 4 != 0) {
            result.push_back('=');
        }
        return result;
    }

    std::optional<std::vector<uint8_t>> SyncCryptoService::base64_decode(std::string_view text) {
        static constexpr std::array<int, 256> table = []() {
            std::array<int, 256> t{};
            t.fill(-1);
            for (int i = 0; i < 26; ++i) t[static_cast<size_t>('A' + i)] = i;
            for (int i = 0; i < 26; ++i) t[static_cast<size_t>('a' + i)] = 26 + i;
            for (int i = 0; i < 10; ++i) t[static_cast<size_t>('0' + i)] = 52 + i;
            t[static_cast<size_t>('+')] = 62;
            t[static_cast<size_t>('/')] = 63;
            return t;
        }();

        std::vector<uint8_t> result;
        result.reserve((text.size() * 3) / 4);

        int val = 0, valb = -8;
        for (char c : text) {
            if (c == '=') break;
            int v = table[static_cast<uint8_t>(c)];
            if (v == -1) {
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
                return std::nullopt; // Invalid character
            }
            val = (val << 6) + v;
            valb += 6;
            if (valb >= 0) {
                result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }

    std::optional<std::string> SyncCryptoService::encrypt_envelope(
        std::string_view dataset,
        std::string_view key,
        std::string_view plaintext,
        uint64_t updated_at,
        bool deleted
    ) const {
        if (updated_at == 0) {
            updated_at = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }

        sync_envelope env{
            .version = 1,
            .dataset = std::string(dataset),
            .key = std::string(key),
            .updated_at = updated_at,
            .deleted = deleted,
            .iv_b64 = "",
            .tag_b64 = "",
            .ciphertext_b64 = ""
        };

        if (deleted) {
            // Tombstones do not require encrypted payload
            std::string json;
            if (glz::write_json(env, json) == glz::error_code::none) {
                return json;
            }
            return std::nullopt;
        }

        auto derived_key = get_derived_key();
        if (derived_key.size() != 32) {
            return std::nullopt;
        }

        // Generate 12-byte random IV
        std::vector<uint8_t> iv(12);
        if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1) {
            return std::nullopt;
        }

        // Associated Authenticated Data: "rouen-sync-v1:{dataset}:{key}"
        std::string aad_str = std::format("rouen-sync-v1:{}:{}", dataset, key);
        std::span<const uint8_t> aad_span(reinterpret_cast<const uint8_t*>(aad_str.data()), aad_str.size());
        std::span<const uint8_t> pt_span(reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());

        auto enc_res = encrypt_gcm(derived_key, iv, pt_span, aad_span);
        if (!enc_res.has_value()) {
            return std::nullopt;
        }

        env.iv_b64 = base64_encode(iv);
        env.ciphertext_b64 = base64_encode(enc_res->first);
        env.tag_b64 = base64_encode(enc_res->second);

        std::string json;
        if (glz::write_json(env, json) == glz::error_code::none) {
            return json;
        }
        return std::nullopt;
    }

    std::optional<sync_decrypted_item> SyncCryptoService::decrypt_envelope(std::string_view envelope_json) const {
        sync_envelope env{};
        if (glz::read_json(env, envelope_json) != glz::error_code::none) {
            return std::nullopt;
        }

        sync_decrypted_item item{
            .dataset = env.dataset,
            .key = env.key,
            .updated_at = env.updated_at,
            .deleted = env.deleted,
            .plaintext = ""
        };

        if (env.deleted) {
            return item;
        }

        auto derived_key = get_derived_key();
        if (derived_key.size() != 32) {
            return std::nullopt;
        }

        auto iv = base64_decode(env.iv_b64);
        auto tag = base64_decode(env.tag_b64);
        auto ciphertext = base64_decode(env.ciphertext_b64);

        if (!iv || !tag || !ciphertext) {
            return std::nullopt;
        }

        std::string aad_str = std::format("rouen-sync-v1:{}:{}", env.dataset, env.key);
        std::span<const uint8_t> aad_span(reinterpret_cast<const uint8_t*>(aad_str.data()), aad_str.size());

        auto pt = decrypt_gcm(derived_key, *iv, *tag, *ciphertext, aad_span);
        if (!pt.has_value()) {
            return std::nullopt; // Authentication or decryption failed
        }

        item.plaintext = std::string(reinterpret_cast<const char*>(pt->data()), pt->size());
        return item;
    }

} // namespace rouen::sync
