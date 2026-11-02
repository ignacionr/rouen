#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include "config_service.hpp"
#include "fetch.hpp"
#include "glaze_include.hpp"
#include "platform_utils.hpp"

namespace rouen::services {

struct verified_operator_identity {
    std::string sub;
    std::string email;
    std::string name;
    std::string iss;
    std::vector<std::string> roles;
    std::string workstation_node;
    int64_t auth_time{0};
    int64_t exp{0};

    [[nodiscard]] bool has_role(std::string_view required_role) const {
        if (required_role.empty()) {
            return true;
        }
        return std::ranges::find(roles, required_role) != roles.end();
    }
};

struct jwk_entry {
    std::string kty;
    std::string kid;
    std::string alg;
    std::string use;
    std::string crv;
    std::string n;
    std::string e;
    std::string x;
    std::string y;
};

struct jwks_document {
    std::vector<jwk_entry> keys;
};

class auth_service {
public:
    struct test_keypair {
        std::shared_ptr<EVP_PKEY> pkey;
        std::string jwk_json;
    };

    static auth_service& instance() {
        static auth_service inst;
        return inst;
    }

    void reset() {
        std::lock_guard<std::mutex> const lock(mutex_);
        jwks_.keys.clear();
        active_jwt_.clear();
        active_operator_.reset();
        expected_issuer_.clear();
        pkce_verifier_.clear();
        device_user_code_.clear();
        device_verification_uri_.clear();
        explicitly_loaded_ = false;
    }

    void set_expected_issuer(std::string issuer) {
        std::lock_guard<std::mutex> const lock(mutex_);
        expected_issuer_ = std::move(issuer);
    }

    bool load_jwks_json(std::string_view json_str) {
        jwks_document doc{};
        auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(doc, json_str);
        if (err) {
            return false;
        }
        std::lock_guard<std::mutex> const lock(mutex_);
        jwks_ = std::move(doc);
        explicitly_loaded_ = true;
        return true;
    }

    [[nodiscard]] static std::string base64url_encode(std::span<const unsigned char> data) {
        static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);
        size_t i = 0;
        while (i + 2 < data.size()) {
            uint32_t const n = (static_cast<uint32_t>(data[i]) << 16U) |
                               (static_cast<uint32_t>(data[i + 1]) << 8U) |
                               static_cast<uint32_t>(data[i + 2]);
            out.push_back(kAlphabet[(n >> 18U) & 0x3FU]);
            out.push_back(kAlphabet[(n >> 12U) & 0x3FU]);
            out.push_back(kAlphabet[(n >> 6U) & 0x3FU]);
            out.push_back(kAlphabet[n & 0x3FU]);
            i += 3;
        }
        if (i < data.size()) {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16U;
            if (i + 1 < data.size()) {
                n |= static_cast<uint32_t>(data[i + 1]) << 8U;
            }
            out.push_back(kAlphabet[(n >> 18U) & 0x3FU]);
            out.push_back(kAlphabet[(n >> 12U) & 0x3FU]);
            if (i + 1 < data.size()) {
                out.push_back(kAlphabet[(n >> 6U) & 0x3FU]);
            }
        }
        return out;
    }

    [[nodiscard]] static std::string base64url_encode_str(std::string_view s) {
        return base64url_encode(std::span<const unsigned char>(
            reinterpret_cast<const unsigned char*>(s.data()), s.size()));
    }

    [[nodiscard]] static std::vector<unsigned char> base64url_decode(std::string_view input) {
        auto decode_char = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '-' || c == '+') return 62;
            if (c == '_' || c == '/') return 63;
            return -1;
        };
        std::vector<unsigned char> out;
        out.reserve((input.size() * 3) / 4);
        uint32_t val = 0;
        int valb = -8;
        for (char c : input) {
            if (c == '=') break;
            int const d = decode_char(c);
            if (d < 0) continue;
            val = (val << 6U) | static_cast<uint32_t>(d);
            valb += 6;
            if (valb >= 0) {
                out.push_back(static_cast<unsigned char>((val >> static_cast<unsigned>(valb)) & 0xFFU));
                valb -= 8;
            }
        }
        return out;
    }

    [[nodiscard]] std::optional<verified_operator_identity> verify_jwt(std::string_view token) {
        ensure_loaded();
        auto const dot1 = token.find('.');
        if (dot1 == std::string_view::npos) return std::nullopt;
        auto const dot2 = token.find('.', dot1 + 1);
        if (dot2 == std::string_view::npos) return std::nullopt;

        std::string_view const header_b64 = token.substr(0, dot1);
        std::string_view const payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
        std::string_view const sig_b64 = token.substr(dot2 + 1);
        std::string_view const signing_input = token.substr(0, dot2);

        auto const header_bytes = base64url_decode(header_b64);
        auto const payload_bytes = base64url_decode(payload_b64);
        auto const sig_bytes = base64url_decode(sig_b64);
        if (header_bytes.empty() || payload_bytes.empty() || sig_bytes.empty()) {
            return std::nullopt;
        }

        std::string const header_json(reinterpret_cast<const char*>(header_bytes.data()), header_bytes.size());
        std::string const payload_json(reinterpret_cast<const char*>(payload_bytes.data()), payload_bytes.size());

        glz::json_t hdr_doc{};
        if (glz::read_json(hdr_doc, header_json) || !hdr_doc.is_object()) {
            return std::nullopt;
        }
        std::string const alg = (hdr_doc.contains("alg") && hdr_doc["alg"].holds<std::string>())
            ? hdr_doc["alg"].get<std::string>() : "";
        std::string const kid = (hdr_doc.contains("kid") && hdr_doc["kid"].holds<std::string>())
            ? hdr_doc["kid"].get<std::string>() : "";

        jwk_entry matching_jwk{};
        std::string expected_iss;
        {
            std::lock_guard<std::mutex> const lock(mutex_);
            expected_iss = expected_issuer_;
            bool found = false;
            for (auto const& k : jwks_.keys) {
                if ((!kid.empty() && k.kid == kid) || (kid.empty() && jwks_.keys.size() == 1)) {
                    matching_jwk = k;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return std::nullopt;
            }
        }

        auto pkey = jwk_to_evp_pkey(matching_jwk);
        if (!pkey) {
            return std::nullopt;
        }

        if (!verify_signature(pkey.get(), alg, signing_input, sig_bytes)) {
            return std::nullopt;
        }

        glz::json_t claims{};
        if (glz::read_json(claims, payload_json) || !claims.is_object()) {
            return std::nullopt;
        }

        auto const now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        verified_operator_identity id{};
        if (claims.contains("sub") && claims["sub"].holds<std::string>()) {
            id.sub = claims["sub"].get<std::string>();
        }
        if (claims.contains("email") && claims["email"].holds<std::string>()) {
            id.email = claims["email"].get<std::string>();
        }
        if (claims.contains("name") && claims["name"].holds<std::string>()) {
            id.name = claims["name"].get<std::string>();
        }
        if (claims.contains("iss") && claims["iss"].holds<std::string>()) {
            id.iss = claims["iss"].get<std::string>();
        }
        if (claims.contains("exp") && claims["exp"].holds<double>()) {
            id.exp = static_cast<int64_t>(claims["exp"].get<double>());
        }
        if (claims.contains("auth_time") && claims["auth_time"].holds<double>()) {
            id.auth_time = static_cast<int64_t>(claims["auth_time"].get<double>());
        }
        if (claims.contains("roles") && claims["roles"].is_array()) {
            for (auto const& r : claims["roles"].get<glz::json_t::array_t>()) {
                if (r.holds<std::string>()) {
                    id.roles.push_back(r.get<std::string>());
                }
            }
        } else if (claims.contains("groups") && claims["groups"].is_array()) {
            for (auto const& r : claims["groups"].get<glz::json_t::array_t>()) {
                if (r.holds<std::string>()) {
                    id.roles.push_back(r.get<std::string>());
                }
            }
        }

        // Check expiration (allow 30s clock skew)
        if (id.exp <= 0 || id.exp + 30 < now_epoch) {
            return std::nullopt;
        }

        // Check issuer if configured
        if (!expected_iss.empty() && id.iss != expected_iss) {
            return std::nullopt;
        }

        auto cfg = helpers::ConfigService::instance();
        id.workstation_node = cfg->get_env("ROUEN_MESH_CLIENT_ID");
        if (id.workstation_node.empty()) {
            id.workstation_node = "local-console";
        }
        return id;
    }

    bool set_active_session_jwt(std::string_view jwt) {
        auto verified = verify_jwt(jwt);
        if (!verified.has_value()) {
            return false;
        }
        std::lock_guard<std::mutex> const lock(mutex_);
        active_jwt_ = std::string(jwt);
        active_operator_ = std::move(verified);
        return true;
    }

    void lock_or_handover_session() {
        std::lock_guard<std::mutex> const lock(mutex_);
        active_jwt_.clear();
        active_operator_.reset();
    }

    [[nodiscard]] std::string get_active_session_jwt() const {
        std::lock_guard<std::mutex> const lock(mutex_);
        return active_jwt_;
    }

    [[nodiscard]] std::optional<verified_operator_identity> get_active_operator() {
        std::string jwt_copy;
        {
            std::lock_guard<std::mutex> const lock(mutex_);
            if (!active_operator_.has_value()) {
                return std::nullopt;
            }
            jwt_copy = active_jwt_;
        }
        return verify_jwt(jwt_copy);
    }

    // Scoped thread-local override for inbound HTTP requests carrying X-Rouen-Operator-JWT
    static void set_request_operator(std::optional<verified_operator_identity> op) {
        s_request_operator = std::move(op);
    }

    [[nodiscard]] static std::optional<verified_operator_identity> current_effective_operator() {
        if (s_request_operator.has_value()) {
            return s_request_operator;
        }
        return instance().get_active_operator();
    }

    // Strips any untrusted "_operator" field and injects verified_operator_identity if present
    [[nodiscard]] static std::string sanitize_and_inject_operator(std::string const& payload_json) {
        glz::json_t doc{};
        if (glz::read_json(doc, payload_json) || !doc.is_object()) {
            return payload_json;
        }
        auto& obj = doc.get<glz::json_t::object_t>();
        obj.erase("_operator");

        if (auto op = current_effective_operator(); op.has_value()) {
            glz::json_t op_obj = glz::json_t::object_t{};
            op_obj["sub"] = op->sub;
            op_obj["email"] = op->email;
            op_obj["name"] = op->name;
            glz::json_t roles_arr = glz::json_t::array_t{};
            for (auto const& r : op->roles) {
                roles_arr.get<glz::json_t::array_t>().push_back(r);
            }
            op_obj["roles"] = std::move(roles_arr);
            op_obj["workstation_node"] = op->workstation_node;
            op_obj["auth_time"] = static_cast<double>(op->auth_time);
            obj["_operator"] = std::move(op_obj);
        }

        std::string out;
        (void)glz::write_json(doc, out);
        return out.empty() ? payload_json : out;
    }

    // PKCE Authorization URL builder
    std::string begin_pkce_authorization_url() {
        std::array<unsigned char, 32> rnd{};
        (void)RAND_bytes(rnd.data(), static_cast<int>(rnd.size()));
        std::string verifier = base64url_encode(rnd);

        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int digest_len = 0;
        EVP_Digest(verifier.data(), verifier.size(), digest.data(), &digest_len, EVP_sha256(), nullptr);
        std::string const challenge = base64url_encode(std::span<const unsigned char>(digest.data(), digest_len));

        auto cfg = helpers::ConfigService::instance();
        std::string const issuer = cfg->get_env("ROUEN_OIDC_ISSUER_URL");
        std::string const client_id = cfg->get_env("ROUEN_OIDC_CLIENT_ID");
        std::string scopes = cfg->get_env("ROUEN_OIDC_SCOPES");
        if (scopes.empty()) scopes = "openid%20profile%20email";

        {
            std::lock_guard<std::mutex> const lock(mutex_);
            pkce_verifier_ = verifier;
        }
        return std::format(
            "{}/authorize?response_type=code&client_id={}&redirect_uri=http://127.0.0.1:8081/api/auth/callback&scope={}&code_challenge={}&code_challenge_method=S256",
            issuer, client_id, scopes, challenge);
    }

    [[nodiscard]] std::string to_status_json() {
        auto op = get_active_operator();
        glz::json_t root = glz::json_t::object_t{};
        root["authenticated"] = op.has_value();
        if (op.has_value()) {
            auto const now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            root["sub"] = op->sub;
            root["email"] = op->email;
            root["name"] = op->name;
            root["iss"] = op->iss;
            root["remaining_seconds"] = static_cast<double>(std::max<int64_t>(0, op->exp - now_epoch));
            glz::json_t roles_arr = glz::json_t::array_t{};
            for (auto const& r : op->roles) {
                roles_arr.get<glz::json_t::array_t>().push_back(r);
            }
            root["roles"] = std::move(roles_arr);
        }
        std::string out;
        (void)glz::write_json(root, out);
        return out;
    }

    // Helper for unit tests & local dev tokens: generates RS256, ES256, or EdDSA keypair + JWKS entry
    static test_keypair generate_test_keypair(std::string_view alg, std::string_view kid) {
        test_keypair result{};
        if (alg == "RS256") {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
            EVP_PKEY* pkey = nullptr;
            if (ctx && EVP_PKEY_keygen_init(ctx) > 0 &&
                EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) > 0 &&
                EVP_PKEY_keygen(ctx, &pkey) > 0) {
                result.pkey = std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);
            }
            if (ctx) EVP_PKEY_CTX_free(ctx);

            BIGNUM* n_bn = nullptr;
            BIGNUM* e_bn = nullptr;
            if (result.pkey &&
                EVP_PKEY_get_bn_param(result.pkey.get(), OSSL_PKEY_PARAM_RSA_N, &n_bn) == 1 &&
                EVP_PKEY_get_bn_param(result.pkey.get(), OSSL_PKEY_PARAM_RSA_E, &e_bn) == 1) {
                std::vector<unsigned char> n_buf(static_cast<size_t>(BN_num_bytes(n_bn)));
                std::vector<unsigned char> e_buf(static_cast<size_t>(BN_num_bytes(e_bn)));
                BN_bn2bin(n_bn, n_buf.data());
                BN_bn2bin(e_bn, e_buf.data());
                result.jwk_json = std::format(
                    R"({{"kty":"RSA","alg":"RS256","use":"sig","kid":"{}","n":"{}","e":"{}"}})",
                    kid, base64url_encode(n_buf), base64url_encode(e_buf));
            }
            BN_free(n_bn);
            BN_free(e_bn);
        } else if (alg == "ES256") {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
            EVP_PKEY* pkey = nullptr;
            if (ctx && EVP_PKEY_keygen_init(ctx) > 0 &&
                EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) > 0 &&
                EVP_PKEY_keygen(ctx, &pkey) > 0) {
                result.pkey = std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);
            }
            if (ctx) EVP_PKEY_CTX_free(ctx);

            BIGNUM* x_bn = nullptr;
            BIGNUM* y_bn = nullptr;
            if (result.pkey &&
                EVP_PKEY_get_bn_param(result.pkey.get(), OSSL_PKEY_PARAM_EC_PUB_X, &x_bn) == 1 &&
                EVP_PKEY_get_bn_param(result.pkey.get(), OSSL_PKEY_PARAM_EC_PUB_Y, &y_bn) == 1) {
                std::array<unsigned char, 32> x_buf{};
                std::array<unsigned char, 32> y_buf{};
                BN_bn2binpad(x_bn, x_buf.data(), 32);
                BN_bn2binpad(y_bn, y_buf.data(), 32);
                result.jwk_json = std::format(
                    R"({{"kty":"EC","crv":"P-256","alg":"ES256","use":"sig","kid":"{}","x":"{}","y":"{}"}})",
                    kid, base64url_encode(x_buf), base64url_encode(y_buf));
            }
            BN_free(x_bn);
            BN_free(y_bn);
        } else if (alg == "EdDSA") {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
            EVP_PKEY* pkey = nullptr;
            if (ctx && EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_keygen(ctx, &pkey) > 0) {
                result.pkey = std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);
            }
            if (ctx) EVP_PKEY_CTX_free(ctx);

            std::array<unsigned char, 32> pub{};
            size_t pub_len = pub.size();
            if (result.pkey && EVP_PKEY_get_raw_public_key(result.pkey.get(), pub.data(), &pub_len) == 1) {
                result.jwk_json = std::format(
                    R"({{"kty":"OKP","crv":"Ed25519","alg":"EdDSA","use":"sig","kid":"{}","x":"{}"}})",
                    kid, base64url_encode(std::span<const unsigned char>(pub.data(), pub_len)));
            }
        }
        return result;
    }

    static std::string sign_test_jwt(
        test_keypair const& kp,
        std::string_view alg,
        std::string_view kid,
        std::string_view claims_json
    ) {
        if (!kp.pkey) return {};
        std::string const header = std::format(R"({{"alg":"{}","typ":"JWT","kid":"{}"}})", alg, kid);
        std::string const signing_input = base64url_encode_str(header) + "." + base64url_encode_str(claims_json);

        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        if (!mctx) return {};

        std::vector<unsigned char> sig;
        if (alg == "EdDSA") {
            if (EVP_DigestSignInit(mctx, nullptr, nullptr, nullptr, kp.pkey.get()) == 1) {
                size_t sig_len = 0;
                EVP_DigestSign(mctx, nullptr, &sig_len,
                    reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());
                sig.resize(sig_len);
                EVP_DigestSign(mctx, sig.data(), &sig_len,
                    reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());
                sig.resize(sig_len);
            }
        } else if (alg == "RS256") {
            if (EVP_DigestSignInit(mctx, nullptr, EVP_sha256(), nullptr, kp.pkey.get()) == 1) {
                size_t sig_len = 0;
                EVP_DigestSign(mctx, nullptr, &sig_len,
                    reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());
                sig.resize(sig_len);
                EVP_DigestSign(mctx, sig.data(), &sig_len,
                    reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());
                sig.resize(sig_len);
            }
        } else if (alg == "ES256") {
            if (EVP_DigestSignInit(mctx, nullptr, EVP_sha256(), nullptr, kp.pkey.get()) == 1) {
                size_t der_len = 0;
                EVP_DigestSign(mctx, nullptr, &der_len,
                    reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());
                std::vector<unsigned char> der(der_len);
                if (EVP_DigestSign(mctx, der.data(), &der_len,
                        reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) == 1) {
                    const unsigned char* p = der.data();
                    ECDSA_SIG* ecdsa_sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der_len));
                    if (ecdsa_sig) {
                        const BIGNUM* r = nullptr;
                        const BIGNUM* s = nullptr;
                        ECDSA_SIG_get0(ecdsa_sig, &r, &s);
                        sig.resize(64, 0);
                        BN_bn2binpad(r, sig.data(), 32);
                        BN_bn2binpad(s, sig.data() + 32, 32);
                        ECDSA_SIG_free(ecdsa_sig);
                    }
                }
            }
        }
        EVP_MD_CTX_free(mctx);
        return signing_input + "." + base64url_encode(sig);
    }

private:
    auth_service() = default;

    void ensure_loaded() {
        std::lock_guard<std::mutex> const lock(mutex_);
        if (explicitly_loaded_) return;
        auto cfg = helpers::ConfigService::instance();
        if (expected_issuer_.empty()) {
            expected_issuer_ = cfg->get_env("ROUEN_OIDC_ISSUER_URL");
        }
        std::string const jwks_path = cfg->get_env("ROUEN_OIDC_JWKS_PATH");
        if (!jwks_path.empty() && std::filesystem::exists(jwks_path)) {
            std::ifstream ifs(jwks_path);
            if (ifs) {
                std::stringstream ss;
                ss << ifs.rdbuf();
                jwks_document doc{};
                if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(doc, ss.str())) {
                    jwks_ = std::move(doc);
                }
            }
        }
        explicitly_loaded_ = true;
    }

    static std::shared_ptr<EVP_PKEY> jwk_to_evp_pkey(jwk_entry const& jwk) {
        if (jwk.kty == "OKP" && (jwk.crv == "Ed25519" || jwk.alg == "EdDSA")) {
            auto const raw = base64url_decode(jwk.x);
            if (raw.size() != 32) return nullptr;
            EVP_PKEY* pk = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, raw.data(), raw.size());
            return pk ? std::shared_ptr<EVP_PKEY>(pk, EVP_PKEY_free) : nullptr;
        }
        if (jwk.kty == "RSA") {
            auto const n_bytes = base64url_decode(jwk.n);
            auto const e_bytes = base64url_decode(jwk.e);
            if (n_bytes.empty() || e_bytes.empty()) return nullptr;
            BIGNUM* n_bn = BN_bin2bn(n_bytes.data(), static_cast<int>(n_bytes.size()), nullptr);
            BIGNUM* e_bn = BN_bin2bn(e_bytes.data(), static_cast<int>(e_bytes.size()), nullptr);
            OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, n_bn);
            OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, e_bn);
            OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
            EVP_PKEY* pk = nullptr;
            if (ctx && EVP_PKEY_fromdata_init(ctx) > 0) {
                EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_PUBLIC_KEY, params);
            }
            if (ctx) EVP_PKEY_CTX_free(ctx);
            OSSL_PARAM_free(params);
            OSSL_PARAM_BLD_free(bld);
            BN_free(n_bn);
            BN_free(e_bn);
            return pk ? std::shared_ptr<EVP_PKEY>(pk, EVP_PKEY_free) : nullptr;
        }
        if (jwk.kty == "EC") {
            auto const x_bytes = base64url_decode(jwk.x);
            auto const y_bytes = base64url_decode(jwk.y);
            if (x_bytes.size() != 32 || y_bytes.size() != 32) return nullptr;
            std::array<unsigned char, 65> uncompressed{};
            uncompressed[0] = 0x04;
            std::memcpy(uncompressed.data() + 1, x_bytes.data(), 32);
            std::memcpy(uncompressed.data() + 33, y_bytes.data(), 32);
            OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
            OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0);
            OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, uncompressed.data(), uncompressed.size());
            OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
            EVP_PKEY* pk = nullptr;
            if (ctx && EVP_PKEY_fromdata_init(ctx) > 0) {
                EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_PUBLIC_KEY, params);
            }
            if (ctx) EVP_PKEY_CTX_free(ctx);
            OSSL_PARAM_free(params);
            OSSL_PARAM_BLD_free(bld);
            return pk ? std::shared_ptr<EVP_PKEY>(pk, EVP_PKEY_free) : nullptr;
        }
        return nullptr;
    }

    static bool verify_signature(
        EVP_PKEY* pkey,
        std::string_view alg,
        std::string_view signing_input,
        std::span<const unsigned char> sig_bytes
    ) {
        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        if (!mctx) return false;
        bool ok = false;
        if (alg == "EdDSA") {
            if (EVP_DigestVerifyInit(mctx, nullptr, nullptr, nullptr, pkey) == 1) {
                ok = (EVP_DigestVerify(
                    mctx, sig_bytes.data(), sig_bytes.size(),
                    reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) == 1);
            }
        } else if (alg == "RS256") {
            if (EVP_DigestVerifyInit(mctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
                ok = (EVP_DigestVerify(
                    mctx, sig_bytes.data(), sig_bytes.size(),
                    reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) == 1);
            }
        } else if (alg == "ES256" && sig_bytes.size() == 64) {
            ECDSA_SIG* ecdsa_sig = ECDSA_SIG_new();
            BIGNUM* r = BN_bin2bn(sig_bytes.data(), 32, nullptr);
            BIGNUM* s = BN_bin2bn(sig_bytes.data() + 32, 32, nullptr);
            ECDSA_SIG_set0(ecdsa_sig, r, s);
            unsigned char* der = nullptr;
            int const der_len = i2d_ECDSA_SIG(ecdsa_sig, &der);
            ECDSA_SIG_free(ecdsa_sig);
            if (der_len > 0 && der != nullptr) {
                if (EVP_DigestVerifyInit(mctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
                    ok = (EVP_DigestVerify(
                        mctx, der, static_cast<size_t>(der_len),
                        reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) == 1);
                }
                OPENSSL_free(der);
            }
        }
        EVP_MD_CTX_free(mctx);
        return ok;
    }

    mutable std::mutex mutex_;
    jwks_document jwks_{};
    std::string expected_issuer_;
    std::string active_jwt_;
    std::optional<verified_operator_identity> active_operator_;
    std::string pkce_verifier_;
    std::string device_user_code_;
    std::string device_verification_uri_;
    bool explicitly_loaded_{false};

    static inline thread_local std::optional<verified_operator_identity> s_request_operator{std::nullopt};
};

} // namespace rouen::services

template <>
struct glz::meta<rouen::services::jwk_entry> {
    using T = rouen::services::jwk_entry;
    static constexpr auto values = glz::object(
        "kty", &T::kty,
        "kid", &T::kid,
        "alg", &T::alg,
        "use", &T::use,
        "crv", &T::crv,
        "n", &T::n,
        "e", &T::e,
        "x", &T::x,
        "y", &T::y
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::services::jwks_document> {
    using T = rouen::services::jwks_document;
    static constexpr auto values = glz::object(
        "keys", &T::keys
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};
