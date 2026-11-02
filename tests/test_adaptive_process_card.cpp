#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "../src/cards/information/adaptive_process_card.hpp"
#include "../src/cards/information/number_series.hpp"
#include "../src/cards/interface/deck.hpp"
#include "../src/helpers/adaptive_process_profiles.hpp"
#include "../src/helpers/auth_service.hpp"

TEST(AdaptiveProcessCard, RefreshesUIOnSecondStdoutLineWithoutAction) {
    // Construct a command that prints two card JSON documents separated by a delay and newline
    std::string const cmd = 
        "python3 -c \"import time, sys; "
        "sys.stdout.write('{\\\"type\\\":\\\"AdaptiveCard\\\",\\\"body\\\":[{\\\"type\\\":\\\"TextBlock\\\",\\\"text\\\":\\\"Initial Card\\\"}]}\\n'); "
        "sys.stdout.flush(); "
        "time.sleep(0.2); "
        "sys.stdout.write('{\\\"type\\\":\\\"AdaptiveCard\\\",\\\"body\\\":[{\\\"type\\\":\\\"TextBlock\\\",\\\"text\\\":\\\"Updated Second Card\\\"}]}\\n'); "
        "sys.stdout.flush()\"";

    rouen::cards::adaptive_process_card card{cmd};

    // Wait for initial card to be received on stdout
    auto start = std::chrono::steady_clock::now();
    while (!card.has_pending_update() && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ASSERT_TRUE(card.has_pending_update()) << "Timed out waiting for initial card stdout line";
    card.apply_pending_update();

    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Initial Card");

    // Wait for second card stdout line (printed automatically by process, without any stdin action)
    start = std::chrono::steady_clock::now();
    while (!card.has_pending_update() && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ASSERT_TRUE(card.has_pending_update()) << "Timed out waiting for second card stdout line";
    card.apply_pending_update();

    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Updated Second Card");
}

TEST(AdaptiveProcessCard, HeadlessGetAdaptiveCardJsonAndHandleActionWithoutRender) {
    std::string const cmd =
        "python3 -c \"import json, sys; "
        "print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':150,'body':[{'type':'TextBlock','text':'Choke: 42%'}]}), flush=True); "
        "line = sys.stdin.readline(); "
        "msg = json.loads(line.strip()); "
        "val = msg.get('data', msg).get('choke', 0); "
        "print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':150,'body':[{'type':'TextBlock','text':f'Choke: {val}%'}]}), flush=True)\"";

    rouen::cards::adaptive_process_card card{cmd};

    // Without calling render() or apply_pending_update() manually, get_adaptive_card_json()
    // should wait briefly for initial output and populate bound_document()
    std::string initial_json = card.get_adaptive_card_json();
    ASSERT_FALSE(initial_json.empty());
    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Choke: 42%");
    EXPECT_EQ(card.bound_document().refreshIntervalMs, 150);

    // Send an action via handle_action() (as used by POST /api/cards/action in --daemon mode)
    card.handle_action(R"({"verb":"set_choke","data":{"choke":48}})");

    auto start = std::chrono::steady_clock::now();
    while (card.bound_document().body[0].text != "Choke: 48%" &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (void)card.get_adaptive_card_json();
    }

    EXPECT_EQ(card.bound_document().body[0].text, "Choke: 48%");
}

TEST(AdaptiveProcessCard, HeadlessPeriodicTickViaRefreshIntervalMs) {
    std::string const cmd =
        "python3 -c \"\n"
        "import json, sys\n"
        "n = 0\n"
        "print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':80,'body':[{'type':'TextBlock','text':f'Tick {n}'}]}), flush=True)\n"
        "for line in sys.stdin:\n"
        "    msg = json.loads(line.strip())\n"
        "    if msg.get('verb') == 'tick':\n"
        "        n += 1\n"
        "        print(json.dumps({'type':'AdaptiveCard','version':'1.5','refreshIntervalMs':80,'body':[{'type':'TextBlock','text':f'Tick {n}'}]}), flush=True)\n"
        "        if n >= 1:\n"
        "            break\n"
        "\"";

    rouen::cards::adaptive_process_card card{cmd};
    (void)card.get_adaptive_card_json();
    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "Tick 0");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    card.tick_background();

    auto start = std::chrono::steady_clock::now();
    while (card.bound_document().body[0].text != "Tick 1" &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        card.tick_background();
    }

    EXPECT_EQ(card.bound_document().body[0].text, "Tick 1");
}

TEST(AdaptiveProcessCard, HeadlessDeckCreatesAndTicksCardWithoutImGuiContext) {
    deck::no_initial_cards = true;
    deck daemon_deck{nullptr};

    auto create_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
    auto get_cards_fn = registrar::get<std::function<std::vector<std::shared_ptr<card>>()>>("get_active_cards");
    ASSERT_NE(create_fn, nullptr);
    ASSERT_NE(get_cards_fn, nullptr);

    std::string const uri =
        "adaptive-process:python3 -c \"import json; print(json.dumps({'type':'AdaptiveCard','body':[{'type':'TextBlock','text':'Daemon Deck OK'}]}), flush=True)\"";
    (*create_fn)(uri);
    daemon_deck.tick_headless();

    auto cards = (*get_cards_fn)();
    bool found = false;
    for (auto const& c : cards) {
        if (c && c->get_uri() == uri) {
            found = true;
            std::string json_out = c->get_adaptive_card_json();
            EXPECT_NE(json_out.find("Daemon Deck OK"), std::string::npos);
        }
    }
    EXPECT_TRUE(found);
}

TEST(AdaptiveProcessCard, AllowlistModeBlocksUnlistedCommandsAndAllowsRegisteredProfiles) {
    auto& registry = rouen::helpers::adaptive_process_profiles::instance();
    std::string const profiles_json = R"({
        "allowArbitraryCommands": false,
        "profiles": {
            "well-3h-esp": {
                "title": "Permian Pad 14 — Well 3H ESP",
                "command": "python3 -c \"import json; print(json.dumps({'type':'AdaptiveCard','body':[{'type':'TextBlock','text':'Profile Well 3H Active'}]}), flush=True)\""
            }
        }
    })";
    ASSERT_TRUE(registry.load_from_json(profiles_json));
    EXPECT_TRUE(registry.is_allowlist_only());

    // 1. Unlisted raw command should be blocked before reaching piped_process
    rouen::cards::adaptive_process_card blocked_card{"python3 -c \"print('should not run')\""};
    EXPECT_NE(blocked_card.last_error().find("allowlist-only mode"), std::string::npos);

    // 2. Registered profile alias should resolve and launch
    rouen::cards::adaptive_process_card allowed_card{"well-3h-esp"};
    std::string card_json = allowed_card.get_adaptive_card_json();
    EXPECT_TRUE(allowed_card.last_error().empty()) << allowed_card.last_error();
    ASSERT_FALSE(allowed_card.bound_document().body.empty());
    EXPECT_EQ(allowed_card.bound_document().body[0].text, "Profile Well 3H Active");

    // 3. Discovery catalog JSON includes the profile
    std::string catalog_json = registry.to_catalog_json();
    EXPECT_NE(catalog_json.find("well-3h-esp"), std::string::npos);
    EXPECT_NE(catalog_json.find("Permian Pad 14"), std::string::npos);

    registry.reset();
}

TEST(AdaptiveProcessCard, Sha256IntegrityCheckVerifiesAndBlocksTamperedScripts) {
    auto& registry = rouen::helpers::adaptive_process_profiles::instance();
    auto const temp_script = std::filesystem::temp_directory_path() / "rouen_test_esp_script.py";
    {
        std::ofstream ofs(temp_script, std::ios::binary | std::ios::trunc);
        ofs << "import json\nprint(json.dumps({'type':'AdaptiveCard','body':[{'type':'TextBlock','text':'SHA256 Verified OK'}]}), flush=True)\n";
    }

    std::string const actual_sha256 = rouen::helpers::adaptive_process_profiles::compute_file_sha256(temp_script);
    ASSERT_EQ(actual_sha256.size(), 64u);

    std::string const profiles_json = std::format(R"({{
        "allowArbitraryCommands": false,
        "profiles": {{
            "verified-esp": {{
                "title": "Verified ESP Script",
                "command": "python3 {}",
                "scriptPath": "{}",
                "sha256": "{}"
            }},
            "tampered-esp": {{
                "title": "Tampered ESP Script",
                "command": "python3 {}",
                "scriptPath": "{}",
                "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
            }}
        }}
    }})", temp_script.string(), temp_script.string(), actual_sha256, temp_script.string(), temp_script.string());

    ASSERT_TRUE(registry.load_from_json(profiles_json));

    rouen::cards::adaptive_process_card verified_card{"verified-esp"};
    (void)verified_card.get_adaptive_card_json();
    EXPECT_TRUE(verified_card.last_error().empty()) << verified_card.last_error();
    ASSERT_FALSE(verified_card.bound_document().body.empty());
    EXPECT_EQ(verified_card.bound_document().body[0].text, "SHA256 Verified OK");

    rouen::cards::adaptive_process_card tampered_card{"tampered-esp"};
    EXPECT_NE(tampered_card.last_error().find("SHA-256"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(temp_script, ec);
    registry.reset();
}

TEST(AdaptiveProcessCard, SeriesPlotPersistsToRepositoryAndNumberSeriesEmitsSeriesPlot) {
    std::string const cmd =
        "python3 -c \"import json; print(json.dumps({"
        "'type':'AdaptiveCard','version':'1.5','body':[{"
        "'type':'Rouen.SeriesPlot',"
        "'title':'Tubing Pressure (Last 15m)',"
        "'seriesName':'test_pad14_well3h_tbg_psi',"
        "'persistToRepository':True,"
        "'unit':'psi',"
        "'isBarChart':False,"
        "'points':[{'label':'21:10','value':478.0},{'label':'21:12','value':485.0}]"
        "}]}), flush=True)\"";

    rouen::cards::adaptive_process_card proc_card{cmd};
    (void)proc_card.get_adaptive_card_json();
    ASSERT_FALSE(proc_card.bound_document().body.empty());
    EXPECT_EQ(proc_card.bound_document().body[0].type, "Rouen.SeriesPlot");

    rouen::models::series::series_repository repo{};
    auto saved = repo.get_series_by_name("test_pad14_well3h_tbg_psi");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->title, "Tubing Pressure (Last 15m)");
    EXPECT_EQ(saved->unit, "psi");
    ASSERT_EQ(saved->points.size(), 2u);
    EXPECT_FLOAT_EQ(saved->points[1].value, 485.0f);

    rouen::cards::number_series_card ns_card{"test_pad14_well3h_tbg_psi"};
    std::string const ns_adaptive_json = ns_card.get_adaptive_card_json();
    EXPECT_NE(ns_adaptive_json.find("Rouen.SeriesPlot"), std::string::npos);
    EXPECT_NE(ns_adaptive_json.find("test_pad14_well3h_tbg_psi"), std::string::npos);

    (void)repo.delete_series("test_pad14_well3h_tbg_psi");
}

TEST(AdaptiveProcessCard, OidcJwtVerificationRs256Es256EdDsaAndSpoofProtection) {
    auto& auth = rouen::services::auth_service::instance();
    auth.reset();

    auto const now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // 1. Test RS256, ES256, and EdDSA key generation, JWKS export, and JWT signing/verification
    auto rs256_kp = rouen::services::auth_service::generate_test_keypair("RS256", "kid-rs256");
    auto es256_kp = rouen::services::auth_service::generate_test_keypair("ES256", "kid-es256");
    auto eddsa_kp = rouen::services::auth_service::generate_test_keypair("EdDSA", "kid-eddsa");

    std::string const jwks_json = std::format(R"({{"keys":[{},{},{}]}})",
        rs256_kp.jwk_json, es256_kp.jwk_json, eddsa_kp.jwk_json);
    ASSERT_TRUE(auth.load_jwks_json(jwks_json));
    auth.set_expected_issuer("https://sso.permian-energy.com");

    std::string const valid_claims = std::format(R"({{
        "sub": "00u1a2b3c4",
        "email": "jdoe@permian-energy.com",
        "name": "Jane Doe",
        "iss": "https://sso.permian-energy.com",
        "roles": ["scada.operator", "pad-permian-14"],
        "auth_time": {},
        "exp": {}
    }})", now_epoch - 60, now_epoch + 3600);

    // Verify RS256
    std::string const jwt_rs256 = rouen::services::auth_service::sign_test_jwt(
        rs256_kp, "RS256", "kid-rs256", valid_claims);
    auto v_rs = auth.verify_jwt(jwt_rs256);
    ASSERT_TRUE(v_rs.has_value());
    EXPECT_EQ(v_rs->email, "jdoe@permian-energy.com");
    EXPECT_EQ(v_rs->name, "Jane Doe");
    ASSERT_EQ(v_rs->roles.size(), 2u);
    EXPECT_EQ(v_rs->roles[0], "scada.operator");

    // Verify ES256
    std::string const jwt_es256 = rouen::services::auth_service::sign_test_jwt(
        es256_kp, "ES256", "kid-es256", valid_claims);
    auto v_es = auth.verify_jwt(jwt_es256);
    ASSERT_TRUE(v_es.has_value());
    EXPECT_EQ(v_es->sub, "00u1a2b3c4");

    // Verify EdDSA
    std::string const jwt_eddsa = rouen::services::auth_service::sign_test_jwt(
        eddsa_kp, "EdDSA", "kid-eddsa", valid_claims);
    auto v_ed = auth.verify_jwt(jwt_eddsa);
    ASSERT_TRUE(v_ed.has_value());
    EXPECT_EQ(v_ed->email, "jdoe@permian-energy.com");

    // Reject expired token
    std::string const expired_claims = std::format(R"({{
        "sub": "00u1a2b3c4",
        "email": "jdoe@permian-energy.com",
        "iss": "https://sso.permian-energy.com",
        "exp": {}
    }})", now_epoch - 7200);
    std::string const jwt_expired = rouen::services::auth_service::sign_test_jwt(
        eddsa_kp, "EdDSA", "kid-eddsa", expired_claims);
    EXPECT_FALSE(auth.verify_jwt(jwt_expired).has_value());

    // Reject wrong issuer
    std::string const wrong_iss_claims = std::format(R"({{
        "sub": "00u1a2b3c4",
        "email": "jdoe@permian-energy.com",
        "iss": "https://evil.example.com",
        "exp": {}
    }})", now_epoch + 3600);
    std::string const jwt_wrong_iss = rouen::services::auth_service::sign_test_jwt(
        eddsa_kp, "EdDSA", "kid-eddsa", wrong_iss_claims);
    EXPECT_FALSE(auth.verify_jwt(jwt_wrong_iss).has_value());

    // Reject unknown kid
    std::string const jwt_unknown_kid = rouen::services::auth_service::sign_test_jwt(
        eddsa_kp, "EdDSA", "kid-unknown", valid_claims);
    EXPECT_FALSE(auth.verify_jwt(jwt_unknown_kid).has_value());

    // 2. Verify adaptive_process_card strips spoofed "_operator" and injects verified operator identity
    ASSERT_TRUE(auth.set_active_session_jwt(jwt_eddsa));

    std::string const cmd =
        "python3 -c \"\n"
        "import json, sys\n"
        "print(json.dumps({'type':'AdaptiveCard','body':[{'type':'TextBlock','text':'Ready'}]}), flush=True)\n"
        "line = sys.stdin.readline()\n"
        "msg = json.loads(line.strip())\n"
        "op = msg.get('_operator', {})\n"
        "email = op.get('email', 'none')\n"
        "roles = ','.join(op.get('roles', []))\n"
        "print(json.dumps({'type':'AdaptiveCard','body':[{'type':'TextBlock','text':f'Op:{email}|Roles:{roles}'}]}), flush=True)\n"
        "\"";

    rouen::cards::adaptive_process_card card{cmd};
    (void)card.get_adaptive_card_json();
    ASSERT_FALSE(card.bound_document().body.empty());

    // Send action with a spoofed "_operator" field; it must be overwritten with verified Jane Doe
    card.handle_action(R"({"verb":"set_choke","data":{"choke":48},"_operator":{"email":"attacker@evil.com","roles":["admin"]}})");

    auto start = std::chrono::steady_clock::now();
    while (card.bound_document().body[0].text == "Ready" &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (void)card.get_adaptive_card_json();
    }

    EXPECT_EQ(card.bound_document().body[0].text, "Op:jdoe@permian-energy.com|Roles:scada.operator,pad-permian-14");

    auth.reset();
}
