#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "../src/cards/information/adaptive_process_card.hpp"
#include "../src/cards/information/number_series.hpp"
#include "../src/cards/interface/deck.hpp"
#include "../src/helpers/adaptive_process_profiles.hpp"
#include "../src/helpers/alarm_manager.hpp"
#include "../src/helpers/auth_service.hpp"
#include "../src/helpers/command_audit_log.hpp"
#include "../src/helpers/telemetry_spool.hpp"

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

TEST(AdaptiveProcessCard, SboRbacEnforcementAndDualIdentityCommandAuditLog) {
    auto& auth = rouen::services::auth_service::instance();
    auto& audit = rouen::helpers::command_audit_log::instance();
    auth.reset();
    audit.clear_all();

    auto const now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto kp = rouen::services::auth_service::generate_test_keypair("EdDSA", "kid-rbac");
    ASSERT_TRUE(auth.load_jwks_json(std::format(R"({{"keys":[{}]}})", kp.jwk_json)));

    std::string const viewer_jwt = rouen::services::auth_service::sign_test_jwt(
        kp, "EdDSA", "kid-rbac", std::format(R"({{
            "sub": "viewer-01",
            "email": "analyst@permian-energy.com",
            "name": "Read Only Analyst",
            "roles": ["scada.viewer"],
            "exp": {}
        }})", now_epoch + 3600));

    std::string const operator_jwt = rouen::services::auth_service::sign_test_jwt(
        kp, "EdDSA", "kid-rbac", std::format(R"({{
            "sub": "op-02",
            "email": "controller@permian-energy.com",
            "name": "Lead Gas Controller",
            "roles": ["scada.operator"],
            "exp": {}
        }})", now_epoch + 3600));

    std::string const cmd =
        "python3 -c \"\n"
        "import json, sys\n"
        "card = {\n"
        "  'type': 'AdaptiveCard',\n"
        "  'version': '1.5',\n"
        "  'body': [{'type': 'TextBlock', 'text': 'Well 3H Ready'}],\n"
        "  'actions': [{\n"
        "    'type': 'Action.Execute',\n"
        "    'title': 'ESD Shut-In',\n"
        "    'verb': 'esd_shutin',\n"
        "    'requiredRole': 'scada.operator',\n"
        "    'requiresConfirmation': True,\n"
        "    'requireReasonNote': True\n"
        "  }]\n"
        "}\n"
        "print(json.dumps(card), flush=True)\n"
        "line = sys.stdin.readline()\n"
        "msg = json.loads(line.strip())\n"
        "reason = msg.get('operatorReason', '')\n"
        "email = msg.get('_operator', {}).get('email', '')\n"
        "card['body'][0]['text'] = f'ESD Executed by {email} ({reason})'\n"
        "print(json.dumps(card), flush=True)\n"
        "\"";

    rouen::cards::adaptive_process_card card{cmd};
    (void)card.get_adaptive_card_json();
    ASSERT_FALSE(card.bound_document().body.empty());
    ASSERT_EQ(card.bound_document().actions.size(), 1u);
    EXPECT_EQ(card.bound_document().actions[0].requiredRole, "scada.operator");

    // 1. Unauthenticated attempt on action with requiredRole -> DENIED_UNAUTHENTICATED
    card.handle_action(R"({"verb":"esd_shutin","operatorReason":"High casing pressure"})");
    EXPECT_NE(card.last_error().find("401 Unauthorized"), std::string::npos);

    // 2. Authenticated as scada.viewer -> DENIED_INSUFFICIENT_ROLE (403 Forbidden)
    ASSERT_TRUE(auth.set_active_session_jwt(viewer_jwt));
    card.handle_action(R"({"verb":"esd_shutin","operatorReason":"High casing pressure"})");
    EXPECT_NE(card.last_error().find("403 Forbidden"), std::string::npos);

    // 3. Authenticated as scada.operator -> EXECUTED
    ASSERT_TRUE(auth.set_active_session_jwt(operator_jwt));
    card.handle_action(R"({"verb":"esd_shutin","operatorReason":"High casing pressure"})");

    auto start = std::chrono::steady_clock::now();
    while (card.bound_document().body[0].text == "Well 3H Ready" &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (void)card.get_adaptive_card_json();
    }
    EXPECT_EQ(card.bound_document().body[0].text, "ESD Executed by controller@permian-energy.com (High casing pressure)");

    auto const logs = audit.recent_entries(10);
    ASSERT_GE(logs.size(), 3u);
    // Most recent first or chronological check
    bool saw_unauth = false, saw_forbidden = false, saw_executed = false;
    for (auto const& entry : logs) {
        if (entry.decision == "DENIED_UNAUTHENTICATED") saw_unauth = true;
        if (entry.decision == "DENIED_INSUFFICIENT_ROLE" && entry.operator_email == "analyst@permian-energy.com") saw_forbidden = true;
        if (entry.decision == "EXECUTED" && entry.operator_email == "controller@permian-energy.com" &&
            entry.operator_reason == "High casing pressure") saw_executed = true;
    }
    EXPECT_TRUE(saw_unauth);
    EXPECT_TRUE(saw_forbidden);
    EXPECT_TRUE(saw_executed);

    auth.reset();
    audit.clear_all();
}

TEST(AdaptiveProcessCard, Isa182AlarmLifecycleAndProcessAckForwarding) {
    auto& alarms = rouen::helpers::alarm_manager::instance();
    alarms.clear_all();

    // 1. Direct state machine tests:
    // Path A: NORMAL -> UNACK_ALM -> ACK_ALM -> NORMAL
    std::vector<rouen::helpers::adaptive_cards::alarm_declaration> active_batch{
        {.id = "pad14.well3h.h2s_high", .severity = "critical", .title = "Pad 14 H2S High", .value = "14.2 ppm", .latched = true},
        {.id = "pad14.well3h.tbg_low", .severity = "high", .title = "Pad 14 Tubing Low", .value = "180 psi", .latched = true}
    };
    alarms.report_process_alarms("adaptive-process:test-pad14", active_batch);

    auto a1 = alarms.get_alarm("pad14.well3h.h2s_high");
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(a1->state, "UNACK_ALM");

    // Operator acknowledges pad14.well3h.h2s_high while still active -> ACK_ALM
    ASSERT_TRUE(alarms.acknowledge("pad14.well3h.h2s_high", "controller@permian-energy.com"));
    a1 = alarms.get_alarm("pad14.well3h.h2s_high");
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(a1->state, "ACK_ALM");
    EXPECT_EQ(a1->acknowledged_by, "controller@permian-energy.com");

    // Physical conditions clear for both alarms:
    // - pad14.well3h.h2s_high was ACK_ALM -> transitions to NORMAL
    // - pad14.well3h.tbg_low was UNACK_ALM (latched) -> transitions to UNACK_RTN
    alarms.report_process_alarms("adaptive-process:test-pad14", {});
    a1 = alarms.get_alarm("pad14.well3h.h2s_high");
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(a1->state, "NORMAL");

    auto a2 = alarms.get_alarm("pad14.well3h.tbg_low");
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(a2->state, "UNACK_RTN");

    // Acknowledging UNACK_RTN clears it to NORMAL
    ASSERT_TRUE(alarms.acknowledge("pad14.well3h.tbg_low", "controller@permian-energy.com"));
    a2 = alarms.get_alarm("pad14.well3h.tbg_low");
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(a2->state, "NORMAL");

    // Timed shelving test
    alarms.report_process_alarms("adaptive-process:test-pad14", {active_batch[0]});
    ASSERT_TRUE(alarms.shelve("pad14.well3h.h2s_high", 1, "Sensor calibration", "controller@permian-energy.com"));
    a1 = alarms.get_alarm("pad14.well3h.h2s_high");
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(a1->state, "SHELVED");
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    alarms.check_expirations();
    a1 = alarms.get_alarm("pad14.well3h.h2s_high");
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(a1->state, "UNACK_ALM");

    // 2. End-to-end adaptive_process_card "alarms" array ingestion & ack_alarm stdin forwarding
    alarms.clear_all();
    std::string const cmd =
        "python3 -c \"import json, sys\n"
        "sys.stdout.write(json.dumps({'type':'AdaptiveCard','alarms':[{'id':'sep1.lvl_hh','severity':'critical','title':'Separator 1 Level High-High','value':'94%','latched':True}],'body':[{'type':'TextBlock','text':'ALARM_ACTIVE'}]}) + '\\n')\n"
        "sys.stdout.flush()\n"
        "for line in sys.stdin:\n"
        "    msg = json.loads(line)\n"
        "    if msg.get('verb') == 'ack_alarm' and msg.get('alarm_id') == 'sep1.lvl_hh':\n"
        "        sys.stdout.write(json.dumps({'type':'AdaptiveCard','alarms':[],'body':[{'type':'TextBlock','text':'ACK_RECEIVED:' + msg.get('alarm_id', '')}]}) + '\\n')\n"
        "        sys.stdout.flush()\n"
        "        break\n"
        "\"";

    rouen::cards::adaptive_process_card card{cmd};
    (void)card.get_adaptive_card_json();
    auto sep_alm = alarms.get_alarm("sep1.lvl_hh");
    ASSERT_TRUE(sep_alm.has_value());
    EXPECT_EQ(sep_alm->state, "UNACK_ALM");

    // Acknowledge via alarm_manager -> forwards {"verb":"ack_alarm","alarm_id":"sep1.lvl_hh"} to child process stdin
    ASSERT_TRUE(alarms.acknowledge("sep1.lvl_hh", "controller@permian-energy.com"));

    auto start = std::chrono::steady_clock::now();
    while (!card.bound_document().body.empty() &&
           card.bound_document().body[0].text == "ALARM_ACTIVE" &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (void)card.get_adaptive_card_json();
    }
    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(card.bound_document().body[0].text, "ACK_RECEIVED:sep1.lvl_hh");

    sep_alm = alarms.get_alarm("sep1.lvl_hh");
    ASSERT_TRUE(sep_alm.has_value());
    EXPECT_EQ(sep_alm->state, "NORMAL");

    alarms.clear_all();
}

TEST(AdaptiveProcessCard, StoreAndForwardTelemetrySpoolRingBufferAndBackfillMerge) {
    auto& spool = rouen::helpers::telemetry_spool::instance();
    spool.clear_all();
    spool.set_max_rows(3);

    // 1. Ring buffer capacity & FIFO eviction test (max_rows = 3, insert 4 samples)
    spool.enqueue_sample("adaptive-process:pad14", "pad14.well3h.tbg_psi", 1000, 470.0);
    spool.enqueue_sample("adaptive-process:pad14", "pad14.well3h.tbg_psi", 2000, 475.0);
    spool.enqueue_sample("adaptive-process:pad14", "pad14.well3h.tbg_psi", 3000, 480.0);
    spool.enqueue_sample("adaptive-process:pad14", "pad14.well3h.tbg_psi", 4000, 485.0);

    EXPECT_EQ(spool.pending_unsynced_count(), 3u);
    auto batch = spool.drain_unsynced_batch(10);
    ASSERT_EQ(batch.size(), 3u);
    // Oldest sample (timestamp 1000) was evicted; remaining are 2000, 3000, 4000 in sequence order
    EXPECT_EQ(batch[0].timestamp_ms, 2000);
    EXPECT_DOUBLE_EQ(batch[0].value, 475.0);
    EXPECT_EQ(batch[2].timestamp_ms, 4000);
    EXPECT_DOUBLE_EQ(batch[2].value, 485.0);

    // Acknowledge up to batch[1].seq_id -> only 1 unsynced sample remains
    spool.acknowledge_up_to(batch[1].seq_id);
    EXPECT_EQ(spool.pending_unsynced_count(), 1u);

    // 2. End-to-end adaptive_process_card "telemetry" envelope ingestion & deduplicated backfill merge into series_repository
    spool.clear_all();
    spool.set_max_rows(100000);
    rouen::models::series::series_repository repo{};
    (void)repo.delete_series("pad14.well3h.oil_bopd");

    std::string const cmd =
        "python3 -c \"import json, sys\n"
        "card = {\n"
        "  'type': 'AdaptiveCard',\n"
        "  'telemetry': {\n"
        "    'timestamp_ms': 1793586000000,\n"
        "    'series': {\n"
        "      'pad14.well3h.oil_bopd': 1420.5\n"
        "    }\n"
        "  },\n"
        "  'body': [{'type': 'TextBlock', 'text': 'Wellpad Telemetry Active'}]\n"
        "}\n"
        "sys.stdout.write(json.dumps(card) + '\\n')\n"
        "sys.stdout.flush()\n"
        "\"";

    rouen::cards::adaptive_process_card card{cmd};
    (void)card.get_adaptive_card_json();
    ASSERT_FALSE(card.bound_document().body.empty());
    EXPECT_EQ(spool.pending_unsynced_count(), 1u);

    auto unsynced = spool.drain_unsynced_batch(10);
    ASSERT_EQ(unsynced.size(), 1u);
    EXPECT_EQ(unsynced[0].series_name, "pad14.well3h.oil_bopd");
    EXPECT_DOUBLE_EQ(unsynced[0].value, 1420.5);

    // Ingest backfill batch twice with a second point to verify deduplicated merge into series_repository
    std::vector<rouen::helpers::telemetry_sample> backfill = unsynced;
    backfill.push_back(rouen::helpers::telemetry_sample{
        .seq_id = unsynced[0].seq_id + 1,
        .source_uri = "adaptive-process:pad14",
        .series_name = "pad14.well3h.oil_bopd",
        .timestamp_ms = 1793586002000LL,
        .value = 1428.0
    });
    spool.ingest_backfill_batch(backfill);
    spool.ingest_backfill_batch(backfill); // Duplicate replay must not duplicate points

    auto merged = repo.get_series_by_name("pad14.well3h.oil_bopd");
    ASSERT_TRUE(merged.has_value());
    ASSERT_EQ(merged->points.size(), 2u);
    EXPECT_FLOAT_EQ(merged->points[0].value, 1420.5f);
    EXPECT_FLOAT_EQ(merged->points[1].value, 1428.0f);

    (void)repo.delete_series("pad14.well3h.oil_bopd");
    spool.clear_all();
}
