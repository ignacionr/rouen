#pragma once

// 1. Standard includes in alphabetic order
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "../../../external/IconsMaterialDesign.h"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../../registrar.hpp"

// 3. All other includes
#include "../interface/card.hpp"

namespace rouen::cards {

class solar_system : public card {
public:
    struct CelestialBody {
        std::string id;
        std::string name;
        std::string category; // "Sun", "Major Planet", "Dwarf Planet", "Asteroid / Comet", "Space Probe", "Moon"
        std::string symbol;
        ImVec4 color;
        float display_size{8.0f};
        
        // Orbital elements (Keplerian J2000 epoch)
        double a{1.0};           // Semi-major axis in AU
        double e{0.0};           // Eccentricity
        double I{0.0};           // Inclination in degrees
        double L0{0.0};          // Mean longitude at J2000 in degrees
        double Lrate{0.0};       // Mean longitude rate in degrees per Julian century
        double w0{0.0};          // Longitude of perihelion at J2000 in degrees
        double wrate{0.0};       // Perihelion rate in deg/century
        double node0{0.0};       // Longitude of ascending node at J2000 in degrees
        double noderate{0.0};    // Node rate in deg/century
        
        // Computed state for current date
        double helio_x{0.0};     // AU (Heliocentric X)
        double helio_y{0.0};     // AU (Heliocentric Y)
        double helio_z{0.0};     // AU (Heliocentric Z)
        double helio_dist{0.0};  // AU
        
        double geo_x{0.0};       // AU (Geocentric X)
        double geo_y{0.0};       // AU (Geocentric Y)
        double geo_z{0.0};       // AU (Geocentric Z)
        double geo_dist{0.0};    // AU
        
        double light_time_sec{0.0}; // Light travel time from Earth in seconds
        double ecliptic_lon{0.0};   // Degrees 0-360
        double ecliptic_lat{0.0};   // Degrees -90 to +90
        double ra_hours{0.0};       // Right Ascension in hours 0-24
        double dec_deg{0.0};        // Declination in degrees -90 to +90
        
        std::string constellation;
        std::string constellation_symbol;
        double mag{0.0};            // Apparent visual magnitude
        double phase_percent{100.0};// Phase illumination %
        double solar_elongation{0.0}; // Elongation angle from Sun (deg)
        
        // Historical orbit path storage for rendering
        std::vector<ImVec2> orbit_points_2d;
    };

    solar_system(std::string_view locator = "") {
        // Theme colors for Solar System card (Deep space palette)
        colors[0] = ImVec4(0.08f, 0.12f, 0.24f, 1.0f); // Deep Cosmic Navy
        colors[1] = ImVec4(1.00f, 0.85f, 0.30f, 0.8f); // Solar Gold
        
        get_color(2, ImVec4(0.40f, 0.70f, 1.00f, 1.0f)); // Cyan highlight
        get_color(3, ImVec4(0.85f, 0.45f, 0.95f, 1.0f)); // Deep space magenta
        get_color(4, ImVec4(0.20f, 0.25f, 0.40f, 0.6f)); // Orbit ring color
        get_color(5, ImVec4(0.90f, 0.92f, 0.98f, 1.0f)); // Pure text

        name("Solar System");
        width = 860.0f;
        requested_fps = 30; // Smooth animation capability

        init_celestial_catalog();

        // Parse date or target if locator provided
        if (!locator.empty()) {
            set_locator(locator);
        } else {
            set_to_current_time();
        }

        update_all_positions();
    }

    ~solar_system() override = default;

    std::string get_uri() const override {
        return "solar-system";
    }

    bool matches_uri(std::string_view uri) const override {
        return uri == "solar-system" || uri == "solar_system" || uri == "solarsystem";
    }

    void handle_uri(std::string_view uri) override {
        auto colon = uri.find(':');
        if (colon != std::string_view::npos) {
            set_locator(uri.substr(colon + 1));
        }
    }

    // MCP Tools Exposure
    std::vector<mcp_function> get_mcp_functions() const override {
        return {
            mcp_function(
                "get_planetary_positions",
                "Get the positions, constellations, and distances of all planets and major solar system bodies for a given ISO date (or current date).",
                R"mcp({"type":"object","properties":{"date":{"type":"string","description":"Optional date in YYYY-MM-DD format. Defaults to current date."}}})mcp",
                [](const std::string& params) -> std::string {
                    std::string target_date;
                    if (!params.empty()) {
                        struct DateParam { std::string date; };
                        DateParam p{};
                        auto res = glz::read_json(p, params);
                        if (!res) target_date = p.date;
                    }
                    
                    solar_system temp_card;
                    if (!target_date.empty()) {
                        temp_card.set_date_from_string(target_date);
                    } else {
                        temp_card.set_to_current_time();
                    }
                    temp_card.update_all_positions();
                    
                    std::string out = "{\n  \"date\": \"" + temp_card.get_current_date_string() + "\",\n  \"bodies\": [\n";
                    bool first = true;
                    for (const auto& b : temp_card.bodies_) {
                        if (!first) out += ",\n";
                        out += std::format(
                            "    {{\"name\": \"{}\", \"category\": \"{}\", \"helio_dist_au\": {:.4f}, \"geo_dist_au\": {:.4f}, \"constellation\": \"{}\", \"ra\": \"{:.2f}h\", \"dec\": \"{:.2f}°\", \"mag\": {:.1f}}}",
                            b.name, b.category, b.helio_dist, b.geo_dist, b.constellation, b.ra_hours, b.dec_deg, b.mag
                        );
                        first = false;
                    }
                    out += "\n  ]\n}";
                    return out;
                }
            ),
            mcp_function(
                "get_object_ephemeris",
                "Get detailed astronomical ephemeris and orbital data for a specific body (e.g., 'Mars', 'Jupiter', 'Sun', '1P/Halley', 'Voyager 1').",
                R"mcp({"type":"object","properties":{"body_name":{"type":"string","description":"Name of celestial object"},"date":{"type":"string","description":"Optional YYYY-MM-DD date"}},"required":["body_name"]})mcp",
                [](const std::string& params) -> std::string {
                    struct EphemParam { std::string body_name; std::string date; };
                    EphemParam p{};
                    auto res = glz::read_json(p, params);
                    if (res || p.body_name.empty()) {
                        return R"({"status":"error","message":"Invalid parameters. 'body_name' is required."})";
                    }
                    
                    solar_system temp_card;
                    if (!p.date.empty()) temp_card.set_date_from_string(p.date);
                    else temp_card.set_to_current_time();
                    temp_card.update_all_positions();
                    
                    auto it = std::find_if(temp_card.bodies_.begin(), temp_card.bodies_.end(), [&](const CelestialBody& b) {
                        return ::helpers::StringHelper::contains_case_insensitive(b.name, p.body_name);
                    });
                    
                    if (it == temp_card.bodies_.end()) {
                        return std::format(R"({{"status":"error","message":"Object '{}' not found in solar system catalog."}})", p.body_name);
                    }
                    
                    const auto& b = *it;
                    return std::format(
                        R"({{
  "name": "{}",
  "category": "{}",
  "symbol": "{}",
  "date": "{}",
  "heliocentric": {{"x_au": {:.6f}, "y_au": {:.6f}, "z_au": {:.6f}, "distance_au": {:.6f}}},
  "geocentric": {{"distance_au": {:.6f}, "distance_km": {:.1f}, "light_time_min": {:.2f}}},
  "sky_position": {{"ra_hours": {:.4f}, "dec_deg": {:.4f}, "constellation": "{}"}},
  "magnitude": {:.2f},
  "phase_percent": {:.1f}
}})",
                        b.name, b.category, b.symbol, temp_card.get_current_date_string(),
                        b.helio_x, b.helio_y, b.helio_z, b.helio_dist,
                        b.geo_dist, b.geo_dist * 149597870.7, b.light_time_sec / 60.0,
                        b.ra_hours, b.dec_deg, b.constellation,
                        b.mag, b.phase_percent
                    );
                }
            ),
            mcp_function(
                "create_solar_system_card",
                "Create or focus a Solar System card with an optional date or body focus.",
                R"mcp({"type":"object","properties":{"locator":{"type":"string","description":"Optional date or body name (e.g. '2026-08-16' or 'Mars')"}}})mcp",
                [](const std::string& params) -> std::string {
                    std::string loc;
                    if (!params.empty()) {
                        struct LocParam { std::string locator; };
                        LocParam p{};
                        auto res = glz::read_json(p, params);
                        if (!res) loc = p.locator;
                    }
                    try {
                        auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                        std::string uri = loc.empty() ? "solar-system" : std::format("solar-system:{}", loc);
                        (*create_card_fn)(uri);
                        return std::format(R"({{"status":"success","message":"Opened Solar System card with locator '{}'"}})", loc);
                    } catch (const std::exception& e) {
                        return std::format(R"({{"status":"error","message":"Failed to create card: {}"}})", e.what());
                    }
                }
            )
        };
    }

    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            render_header_controls(ui);
            ui.separator();
            
            // Tab bar for different views
            if (ImGui::BeginTabBar("SolarSystemTabs")) {
                if (ImGui::BeginTabItem(ICON_MD_PUBLIC " Interactive Orbit Canvas")) {
                    current_tab_ = Tab::OrbitCanvas;
                    render_orbit_canvas(ui);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(ICON_MD_TABLE_CHART " Ephemeris Table")) {
                    current_tab_ = Tab::EphemerisTable;
                    render_ephemeris_table(ui);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(ICON_MD_BEDTIME " Moon & Planet Phases")) {
                    current_tab_ = Tab::MoonPhases;
                    render_moon_phases_view(ui);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(ICON_MD_EXPLORE " Sky Visibility & Events")) {
                    current_tab_ = Tab::Events;
                    render_events_view(ui);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            // Animation tick
            if (is_animating_) {
                auto now = std::chrono::steady_clock::now();
                float dt = std::chrono::duration<float>(now - last_anim_time_).count();
                last_anim_time_ = now;
                if (dt > 0.0f && dt < 0.5f) {
                    julian_day_ += static_cast<double>(anim_speed_days_per_sec_) * static_cast<double>(dt);
                    update_date_from_julian();
                    update_all_positions();
                }
            }
        });
    }

private:
    enum class Tab { OrbitCanvas, EphemerisTable, MoonPhases, Events };
    Tab current_tab_{Tab::OrbitCanvas};

    // Date & Time engine state
    int year_{2026};
    int month_{8};
    int day_{16};
    int hour_{22};
    int minute_{29};
    int second_{0};
    double julian_day_{2460265.0};
    
    // Animation state
    bool is_animating_{false};
    float anim_speed_days_per_sec_{1.0f}; // Days simulated per real-world second
    std::chrono::steady_clock::time_point last_anim_time_;

    // Canvas view state
    ImVec2 pan_offset_{0.0f, 0.0f};
    float zoom_scale_{1.0f};
    float tilt_angle_deg_{25.0f}; // 0 = top-down 2D, >0 = isometric 3D perspective
    bool use_logarithmic_scale_{true};
    bool show_orbit_lines_{true};
    bool show_labels_{true};
    bool show_vectors_{false};
    bool show_grid_{true};
    size_t selected_body_index_{3}; // Default selected: Earth (index 3) or Sun
    size_t focused_center_index_{0}; // Default center: Sun (index 0)

    // Catalog of celestial bodies
    std::vector<CelestialBody> bodies_;
    char search_filter_[128] = "";

    void set_locator(std::string_view loc) {
        std::string s(loc);
        if (s.empty()) return;
        
        // Check if loc matches date format YYYY-MM-DD
        if (s.length() >= 10 && s[4] == '-' && s[7] == '-') {
            set_date_from_string(s);
        } else {
            // Treat as object focus request
            for (size_t i = 0; i < bodies_.size(); ++i) {
                if (::helpers::StringHelper::contains_case_insensitive(bodies_[i].name, s)) {
                    selected_body_index_ = i;
                    focused_center_index_ = i;
                    break;
                }
            }
        }
    }

    void set_to_current_time() {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        gmtime_s(&tm_buf, &tt);
#else
        gmtime_r(&tt, &tm_buf);
#endif
        year_ = tm_buf.tm_year + 1900;
        month_ = tm_buf.tm_mon + 1;
        day_ = tm_buf.tm_mday;
        hour_ = tm_buf.tm_hour;
        minute_ = tm_buf.tm_min;
        second_ = tm_buf.tm_sec;

        update_julian_from_date();
    }

    void set_date_from_string(const std::string& str) {
        try {
            int y, m, d;
            if (sscanf(str.c_str(), "%d-%d-%d", &y, &m, &d) >= 3) {
                year_ = y; month_ = m; day_ = d;
                hour_ = 12; minute_ = 0; second_ = 0;
                update_julian_from_date();
            }
        } catch (...) {}
    }

    void update_julian_from_date() {
        int y = year_;
        int m = month_;
        if (m <= 2) {
            y -= 1;
            m += 12;
        }
        int A = y / 100;
        int B = 2 - A + (A / 4);
        double day_fraction = (hour_ + (minute_ / 60.0) + (second_ / 3600.0)) / 24.0;
        julian_day_ = std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + day_ + day_fraction + B - 1524.5;
    }

    void update_date_from_julian() {
        double Z = std::floor(julian_day_ + 0.5);
        double F = (julian_day_ + 0.5) - Z;
        double A = Z;
        if (Z >= 2299161.0) {
            double alpha = std::floor((Z - 1867216.25) / 36524.25);
            A = Z + 1 + alpha - std::floor(alpha / 4.0);
        }
        double B = A + 1524;
        double C = std::floor((B - 122.1) / 365.25);
        double D = std::floor(365.25 * C);
        double E = std::floor((B - D) / 30.6001);

        double day_with_frac = B - D - std::floor(30.6001 * E) + F;
        day_ = static_cast<int>(day_with_frac);
        double frac = day_with_frac - day_;

        month_ = static_cast<int>((E < 14) ? (E - 1) : (E - 13));
        year_ = static_cast<int>((month_ > 2) ? (C - 4716) : (C - 4715));

        double total_hours = frac * 24.0;
        hour_ = static_cast<int>(total_hours);
        double total_mins = (total_hours - hour_) * 60.0;
        minute_ = static_cast<int>(total_mins);
        second_ = static_cast<int>((total_mins - minute_) * 60.0);
    }

    std::string get_current_date_string() const {
        return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d} UTC", year_, month_, day_, hour_, minute_, second_);
    }

    void init_celestial_catalog() {
        bodies_.clear();

        // 0: Sun
        CelestialBody sun;
        sun.id = "sun"; sun.name = "Sun"; sun.category = "Sun"; sun.symbol = ICON_MD_WB_SUNNY;
        sun.color = ImVec4(1.0f, 0.9f, 0.2f, 1.0f); sun.display_size = 14.0f;
        sun.a = 0.0; sun.e = 0.0; sun.mag = -26.74;
        bodies_.push_back(sun);

        // 1: Mercury
        CelestialBody merc;
        merc.id = "mercury"; merc.name = "Mercury"; merc.category = "Major Planet"; merc.symbol = ICON_MD_BRIGHTNESS_1;
        merc.color = ImVec4(0.75f, 0.75f, 0.78f, 1.0f); merc.display_size = 5.0f;
        merc.a = 0.38709893; merc.e = 0.20563069; merc.I = 7.00487;
        merc.L0 = 252.25084; merc.Lrate = 149472.67411;
        merc.w0 = 77.45645; merc.wrate = 1.5564775;
        merc.node0 = 48.33167; merc.noderate = -0.1253408;
        bodies_.push_back(merc);

        // 2: Venus
        CelestialBody ven;
        ven.id = "venus"; ven.name = "Venus"; ven.category = "Major Planet"; ven.symbol = ICON_MD_BRIGHTNESS_5;
        ven.color = ImVec4(0.95f, 0.85f, 0.55f, 1.0f); ven.display_size = 7.5f;
        ven.a = 0.72333199; ven.e = 0.00677323; ven.I = 3.39471;
        ven.L0 = 181.97973; ven.Lrate = 58517.81568;
        ven.w0 = 131.53298; ven.wrate = 1.4022288;
        ven.node0 = 76.67992; ven.noderate = -0.2776941;
        bodies_.push_back(ven);

        // 3: Earth
        CelestialBody earth;
        earth.id = "earth"; earth.name = "Earth"; earth.category = "Major Planet"; earth.symbol = ICON_MD_PUBLIC;
        earth.color = ImVec4(0.35f, 0.65f, 1.00f, 1.0f); earth.display_size = 8.0f;
        earth.a = 1.00000011; earth.e = 0.01671022; earth.I = 0.00005;
        earth.L0 = 100.46435; earth.Lrate = 35999.37245;
        earth.w0 = 102.94719; earth.wrate = 0.3179526;
        earth.node0 = -11.26064; earth.noderate = -0.2410708;
        bodies_.push_back(earth);

        // 4: Earth's Moon
        CelestialBody moon;
        moon.id = "moon"; moon.name = "Moon"; moon.category = "Moon"; moon.symbol = ICON_MD_BEDTIME;
        moon.color = ImVec4(0.88f, 0.88f, 0.92f, 1.0f); moon.display_size = 4.5f;
        moon.a = 1.0; // Follows Earth heliocentrically with geocentric offset
        bodies_.push_back(moon);

        // 5: Mars
        CelestialBody mars;
        mars.id = "mars"; mars.name = "Mars"; mars.category = "Major Planet"; mars.symbol = ICON_MD_ADJUST;
        mars.color = ImVec4(0.95f, 0.40f, 0.25f, 1.0f); mars.display_size = 6.5f;
        mars.a = 1.52366231; mars.e = 0.09341233; mars.I = 1.84970;
        mars.L0 = 355.45332; mars.Lrate = 19140.30268;
        mars.w0 = 336.04084; mars.wrate = 1.8410769;
        mars.node0 = 49.57854; mars.noderate = -0.2949880;
        bodies_.push_back(mars);

        // 6: Ceres (Dwarf Planet)
        CelestialBody ceres;
        ceres.id = "ceres"; ceres.name = "1 Ceres"; ceres.category = "Dwarf Planet"; ceres.symbol = ICON_MD_GRAIN;
        ceres.color = ImVec4(0.70f, 0.70f, 0.65f, 1.0f); ceres.display_size = 4.0f;
        ceres.a = 2.767; ceres.e = 0.0758; ceres.I = 10.59;
        ceres.L0 = 102.8; ceres.Lrate = 7820.0; ceres.w0 = 73.1; ceres.node0 = 80.3;
        bodies_.push_back(ceres);

        // 7: 4 Vesta (Asteroid)
        CelestialBody vesta;
        vesta.id = "vesta"; vesta.name = "4 Vesta"; vesta.category = "Asteroid / Comet"; vesta.symbol = ICON_MD_GRAIN;
        vesta.color = ImVec4(0.80f, 0.75f, 0.60f, 1.0f); vesta.display_size = 4.0f;
        vesta.a = 2.361; vesta.e = 0.0886; vesta.I = 7.14;
        vesta.L0 = 250.0; vesta.Lrate = 9800.0; vesta.w0 = 150.0; vesta.node0 = 103.8;
        bodies_.push_back(vesta);

        // 8: Jupiter
        CelestialBody jup;
        jup.id = "jupiter"; jup.name = "Jupiter"; jup.category = "Major Planet"; jup.symbol = ICON_MD_ADJUST;
        jup.color = ImVec4(0.88f, 0.68f, 0.48f, 1.0f); jup.display_size = 11.5f;
        jup.a = 5.20336301; jup.e = 0.04839266; jup.I = 1.30530;
        jup.L0 = 34.40438; jup.Lrate = 3034.74612;
        jup.w0 = 14.75385; jup.wrate = 0.7686884;
        jup.node0 = 100.55615; jup.noderate = 0.2046910;
        bodies_.push_back(jup);

        // 9: Saturn
        CelestialBody sat;
        sat.id = "saturn"; sat.name = "Saturn"; sat.category = "Major Planet"; sat.symbol = ICON_MD_BLUR_CIRCULAR;
        sat.color = ImVec4(0.92f, 0.82f, 0.55f, 1.0f); sat.display_size = 10.0f;
        sat.a = 9.53707032; sat.e = 0.05415060; sat.I = 2.48446;
        sat.L0 = 50.07744; sat.Lrate = 1222.49362;
        sat.w0 = 92.43194; sat.wrate = -0.4189721;
        sat.node0 = 113.71504; sat.noderate = -0.2886779;
        bodies_.push_back(sat);

        // 10: Uranus
        CelestialBody uran;
        uran.id = "uranus"; uran.name = "Uranus"; uran.category = "Major Planet"; uran.symbol = ICON_MD_SYNC;
        uran.color = ImVec4(0.55f, 0.85f, 0.90f, 1.0f); uran.display_size = 8.5f;
        uran.a = 19.19126393; uran.e = 0.04716771; uran.I = 0.76986;
        uran.L0 = 313.23218; uran.Lrate = 428.48203;
        uran.w0 = 170.96424; uran.wrate = 0.4080528;
        uran.node0 = 74.22988; uran.noderate = 0.0424058;
        bodies_.push_back(uran);

        // 11: Neptune
        CelestialBody nep;
        nep.id = "neptune"; nep.name = "Neptune"; nep.category = "Major Planet"; nep.symbol = ICON_MD_WATER;
        nep.color = ImVec4(0.35f, 0.50f, 0.95f, 1.0f); nep.display_size = 8.5f;
        nep.a = 30.06896348; nep.e = 0.00858587; nep.I = 1.76917;
        nep.L0 = 304.88003; nep.Lrate = 218.45945;
        nep.w0 = 44.97135; nep.wrate = -0.8442751;
        nep.node0 = 131.78060; nep.noderate = -0.0050866;
        bodies_.push_back(nep);

        // 12: Pluto (Dwarf Planet)
        CelestialBody pluto;
        pluto.id = "pluto"; pluto.name = "Pluto"; pluto.category = "Dwarf Planet"; pluto.symbol = ICON_MD_MORE_HORIZ;
        pluto.color = ImVec4(0.80f, 0.70f, 0.65f, 1.0f); pluto.display_size = 4.5f;
        pluto.a = 39.48168677; pluto.e = 0.24880766; pluto.I = 17.14175;
        pluto.L0 = 238.92881; pluto.Lrate = 145.2078;
        pluto.w0 = 224.06676; pluto.wrate = -0.0406294;
        pluto.node0 = 110.30347; pluto.noderate = -0.011834;
        bodies_.push_back(pluto);

        // 13: 1P/Halley (Comet)
        CelestialBody halley;
        halley.id = "halley"; halley.name = "1P/Halley"; halley.category = "Asteroid / Comet"; halley.symbol = ICON_MD_AUTO_AWESOME;
        halley.color = ImVec4(0.40f, 0.90f, 0.85f, 1.0f); halley.display_size = 5.0f;
        halley.a = 17.834; halley.e = 0.9671; halley.I = 162.26;
        halley.L0 = 111.3; halley.Lrate = 475.0; halley.w0 = 111.3; halley.node0 = 58.4;
        bodies_.push_back(halley);

        // 14: Voyager 1 (Space Probe)
        CelestialBody voyager1;
        voyager1.id = "voyager1"; voyager1.name = "Voyager 1"; voyager1.category = "Space Probe"; voyager1.symbol = ICON_MD_SATELLITE_ALT;
        voyager1.color = ImVec4(1.00f, 0.40f, 0.70f, 1.0f); voyager1.display_size = 5.0f;
        voyager1.a = 163.5; voyager1.e = 1.05; voyager1.I = 35.0; // Hyperbolic escape
        bodies_.push_back(voyager1);

        // 15: James Webb Space Telescope (Space Probe at L2)
        CelestialBody jwst;
        jwst.id = "jwst"; jwst.name = "JWST (L2)"; jwst.category = "Space Probe"; jwst.symbol = ICON_MD_EXPLORE;
        jwst.color = ImVec4(1.00f, 0.75f, 0.20f, 1.0f); jwst.display_size = 5.0f;
        jwst.a = 1.01; jwst.e = 0.0167; jwst.I = 0.0;
        bodies_.push_back(jwst);
    }

    void update_all_positions() {
        double T = (julian_day_ - 2451545.0) / 36525.0; // Julian centuries from J2000
        double d = julian_day_ - 2451545.0;

        // Calculate Earth's position first (Index 3)
        CelestialBody& earth = bodies_[3];
        calculate_body_kepler(earth, T);

        // Update Sun (Index 0)
        CelestialBody& sun = bodies_[0];
        sun.helio_x = 0.0; sun.helio_y = 0.0; sun.helio_z = 0.0; sun.helio_dist = 0.0;
        sun.geo_x = -earth.helio_x; sun.geo_y = -earth.helio_y; sun.geo_z = -earth.helio_z;
        sun.geo_dist = std::sqrt(sun.geo_x * sun.geo_x + sun.geo_y * sun.geo_y + sun.geo_z * sun.geo_z);
        sun.light_time_sec = sun.geo_dist * 499.00478;
        compute_sky_coords(sun, sun.geo_x, sun.geo_y, sun.geo_z);
        sun.mag = -26.74;

        // Update all other planets/bodies
        for (size_t i = 1; i < bodies_.size(); ++i) {
            if (i == 3) continue; // Skip Earth, already calculated

            CelestialBody& b = bodies_[i];
            if (b.id == "moon") {
                // Earth's Moon geocentric approximation
                double Lmoon = 218.316 + 13.176396 * d;
                double Mmoon = 134.963 + 13.064993 * d;
                double Fmoon = 93.272 + 13.229350 * d;
                double Delong = 297.850 + 12.190749 * d;
                
                double lam_moon_rad = (Lmoon + 6.289 * std::sin(Mmoon * std::numbers::pi / 180.0)) * std::numbers::pi / 180.0;
                double bet_moon_rad = (5.128 * std::sin(Fmoon * std::numbers::pi / 180.0)) * std::numbers::pi / 180.0;
                double r_moon_au = 0.00257 * (1.0 - 0.0549 * std::cos(Mmoon * std::numbers::pi / 180.0));

                b.geo_x = r_moon_au * std::cos(bet_moon_rad) * std::cos(lam_moon_rad);
                b.geo_y = r_moon_au * std::cos(bet_moon_rad) * std::sin(lam_moon_rad);
                b.geo_z = r_moon_au * std::sin(bet_moon_rad);
                b.geo_dist = r_moon_au;

                b.helio_x = earth.helio_x + b.geo_x;
                b.helio_y = earth.helio_y + b.geo_y;
                b.helio_z = earth.helio_z + b.geo_z;
                b.helio_dist = std::sqrt(b.helio_x * b.helio_x + b.helio_y * b.helio_y + b.helio_z * b.helio_z);
                
                b.light_time_sec = b.geo_dist * 499.00478;
                compute_sky_coords(b, b.geo_x, b.geo_y, b.geo_z);
                
                // Moon phase
                double norm_elong = std::fmod(Delong, 360.0);
                if (norm_elong < 0) norm_elong += 360.0;
                b.solar_elongation = norm_elong;
                b.phase_percent = (1.0 + std::cos((180.0 - norm_elong) * std::numbers::pi / 180.0)) / 2.0 * 100.0;
                b.mag = -12.74 + 5.0 * std::log10(b.geo_dist / 0.00257) + (100.0 - b.phase_percent) * 0.05;
            }
            else if (b.id == "voyager1") {
                // Voyager 1 outward velocity vector (~163 AU, ~35 deg inclination)
                double v1_dist = 163.5 + (d / 365.25) * 3.6;
                double v1_ra = 17.25 * 15.0 * std::numbers::pi / 180.0; // RA 17h15m
                double v1_dec = 12.4 * std::numbers::pi / 180.0;
                
                b.geo_x = v1_dist * std::cos(v1_dec) * std::cos(v1_ra);
                b.geo_y = v1_dist * std::cos(v1_dec) * std::sin(v1_ra);
                b.geo_z = v1_dist * std::sin(v1_dec);
                b.geo_dist = v1_dist;
                
                b.helio_x = earth.helio_x + b.geo_x;
                b.helio_y = earth.helio_y + b.geo_y;
                b.helio_z = earth.helio_z + b.geo_z;
                b.helio_dist = std::sqrt(b.helio_x * b.helio_x + b.helio_y * b.helio_y + b.helio_z * b.helio_z);
                b.light_time_sec = b.geo_dist * 499.00478;
                compute_sky_coords(b, b.geo_x, b.geo_y, b.geo_z);
                b.mag = 50.0; // Radio faint
            }
            else if (b.id == "jwst") {
                // JWST at L2 (1.5M km / 0.01 AU beyond Earth)
                b.helio_x = earth.helio_x * 1.01;
                b.helio_y = earth.helio_y * 1.01;
                b.helio_z = earth.helio_z * 1.01;
                b.helio_dist = earth.helio_dist * 1.01;

                b.geo_x = b.helio_x - earth.helio_x;
                b.geo_y = b.helio_y - earth.helio_y;
                b.geo_z = b.helio_z - earth.helio_z;
                b.geo_dist = std::sqrt(b.geo_x * b.geo_x + b.geo_y * b.geo_y + b.geo_z * b.geo_z);
                b.light_time_sec = b.geo_dist * 499.00478;
                compute_sky_coords(b, b.geo_x, b.geo_y, b.geo_z);
                b.mag = 16.5;
            }
            else {
                calculate_body_kepler(b, T);
                b.geo_x = b.helio_x - earth.helio_x;
                b.geo_y = b.helio_y - earth.helio_y;
                b.geo_z = b.helio_z - earth.helio_z;
                b.geo_dist = std::sqrt(b.geo_x * b.geo_x + b.geo_y * b.geo_y + b.geo_z * b.geo_z);
                b.light_time_sec = b.geo_dist * 499.00478;
                compute_sky_coords(b, b.geo_x, b.geo_y, b.geo_z);

                // Magnitude & Phase calculation
                double earth_sun_dist = earth.helio_dist;
                double cos_phase = (b.helio_dist * b.helio_dist + b.geo_dist * b.geo_dist - earth_sun_dist * earth_sun_dist) / (2.0 * b.helio_dist * b.geo_dist);
                cos_phase = std::clamp(cos_phase, -1.0, 1.0);
                b.solar_elongation = std::acos(cos_phase) * 180.0 / std::numbers::pi;
                b.phase_percent = (1.0 + cos_phase) / 2.0 * 100.0;

                // Standard visual magnitude approximation formulas
                if (b.id == "mercury") b.mag = -0.42 + 5.0 * std::log10(b.helio_dist * b.geo_dist) + 0.038 * (180.0 - b.solar_elongation);
                else if (b.id == "venus") b.mag = -4.40 + 5.0 * std::log10(b.helio_dist * b.geo_dist) + 0.0009 * (180.0 - b.solar_elongation);
                else if (b.id == "mars") b.mag = -1.52 + 5.0 * std::log10(b.helio_dist * b.geo_dist);
                else if (b.id == "jupiter") b.mag = -9.40 + 5.0 * std::log10(b.helio_dist * b.geo_dist);
                else if (b.id == "saturn") b.mag = -8.88 + 5.0 * std::log10(b.helio_dist * b.geo_dist);
                else if (b.id == "uranus") b.mag = -7.19 + 5.0 * std::log10(b.helio_dist * b.geo_dist);
                else if (b.id == "neptune") b.mag = -6.87 + 5.0 * std::log10(b.helio_dist * b.geo_dist);
                else b.mag = 3.5 + 5.0 * std::log10(b.helio_dist * b.geo_dist);
            }
        }

        generate_orbit_paths();
    }

    void calculate_body_kepler(CelestialBody& b, double T) {
        double L = std::fmod(b.L0 + b.Lrate * T, 360.0);
        if (L < 0) L += 360.0;
        double w = std::fmod(b.w0 + b.wrate * T, 360.0);
        if (w < 0) w += 360.0;
        double node = std::fmod(b.node0 + b.noderate * T, 360.0);
        if (node < 0) node += 360.0;

        double M = std::fmod(L - w, 360.0);
        if (M < 0) M += 360.0;
        double M_rad = M * std::numbers::pi / 180.0;

        // Newton-Raphson Kepler solver
        double E_rad = M_rad;
        for (int iter = 0; iter < 15; ++iter) {
            double dE = (E_rad - b.e * std::sin(E_rad) - M_rad) / (1.0 - b.e * std::cos(E_rad));
            E_rad -= dE;
            if (std::abs(dE) < 1e-7) break;
        }

        double x_plane = b.a * (std::cos(E_rad) - b.e);
        double y_plane = b.a * std::sqrt(1.0 - b.e * b.e) * std::sin(E_rad);

        double v_rad = std::atan2(y_plane, x_plane);
        double r = std::sqrt(x_plane * x_plane + y_plane * y_plane);

        double u_rad = v_rad + (w - node) * std::numbers::pi / 180.0;
        double I_rad = b.I * std::numbers::pi / 180.0;
        double node_rad = node * std::numbers::pi / 180.0;

        b.helio_x = r * (std::cos(node_rad) * std::cos(u_rad) - std::sin(node_rad) * std::sin(u_rad) * std::cos(I_rad));
        b.helio_y = r * (std::sin(node_rad) * std::cos(u_rad) + std::cos(node_rad) * std::sin(u_rad) * std::cos(I_rad));
        b.helio_z = r * (std::sin(u_rad) * std::sin(I_rad));
        b.helio_dist = r;
    }

    void compute_sky_coords(CelestialBody& b, double gx, double gy, double gz) {
        double dist = std::sqrt(gx * gx + gy * gy + gz * gz);
        if (dist <= 1e-9) dist = 1.0;

        // Ecliptic longitude & latitude
        double ecl_lon_rad = std::atan2(gy, gx);
        if (ecl_lon_rad < 0) ecl_lon_rad += 2.0 * std::numbers::pi;
        double ecl_lat_rad = std::asin(gz / dist);

        b.ecliptic_lon = ecl_lon_rad * 180.0 / std::numbers::pi;
        b.ecliptic_lat = ecl_lat_rad * 180.0 / std::numbers::pi;

        // Convert Ecliptic to Equatorial (Obliquity eps = 23.4393 deg)
        double eps = 23.4393 * std::numbers::pi / 180.0;
        double eq_x = gx;
        double eq_y = gy * std::cos(eps) - gz * std::sin(eps);
        double eq_z = gy * std::sin(eps) + gz * std::cos(eps);

        double ra_rad = std::atan2(eq_y, eq_x);
        if (ra_rad < 0) ra_rad += 2.0 * std::numbers::pi;
        b.ra_hours = (ra_rad * 180.0 / std::numbers::pi) / 15.0;
        b.dec_deg = std::asin(eq_z / dist) * 180.0 / std::numbers::pi;

        // Zodiac Constellation determination
        double lon = b.ecliptic_lon;
        if (lon >= 0 && lon < 30) { b.constellation = "Aries"; b.constellation_symbol = "[Ari]"; }
        else if (lon >= 30 && lon < 60) { b.constellation = "Taurus"; b.constellation_symbol = "[Tau]"; }
        else if (lon >= 60 && lon < 90) { b.constellation = "Gemini"; b.constellation_symbol = "[Gem]"; }
        else if (lon >= 90 && lon < 120) { b.constellation = "Cancer"; b.constellation_symbol = "[Can]"; }
        else if (lon >= 120 && lon < 150) { b.constellation = "Leo"; b.constellation_symbol = "[Leo]"; }
        else if (lon >= 150 && lon < 180) { b.constellation = "Virgo"; b.constellation_symbol = "[Vir]"; }
        else if (lon >= 180 && lon < 210) { b.constellation = "Libra"; b.constellation_symbol = "[Lib]"; }
        else if (lon >= 210 && lon < 240) { b.constellation = "Scorpius"; b.constellation_symbol = "[Sco]"; }
        else if (lon >= 240 && lon < 270) { b.constellation = "Sagittarius"; b.constellation_symbol = "[Sag]"; }
        else if (lon >= 270 && lon < 300) { b.constellation = "Capricornus"; b.constellation_symbol = "[Cap]"; }
        else if (lon >= 300 && lon < 330) { b.constellation = "Aquarius"; b.constellation_symbol = "[Aqu]"; }
        else { b.constellation = "Pisces"; b.constellation_symbol = "[Pis]"; }
    }

    void generate_orbit_paths() {
        for (auto& b : bodies_) {
            if (b.a <= 0.0 || b.id == "sun" || b.id == "moon" || b.id == "jwst" || b.id == "voyager1") continue;

            b.orbit_points_2d.clear();
            constexpr int STEPS = 64;
            for (int k = 0; k <= STEPS; ++k) {
                double E_rad = (k / static_cast<double>(STEPS)) * 2.0 * std::numbers::pi;
                double x_plane = b.a * (std::cos(E_rad) - b.e);
                double y_plane = b.a * std::sqrt(1.0 - b.e * b.e) * std::sin(E_rad);

                double v_rad = std::atan2(y_plane, x_plane);
                double r = std::sqrt(x_plane * x_plane + y_plane * y_plane);

                double u_rad = v_rad + (b.w0 - b.node0) * std::numbers::pi / 180.0;
                double I_rad = b.I * std::numbers::pi / 180.0;
                double node_rad = b.node0 * std::numbers::pi / 180.0;

                double hx = r * (std::cos(node_rad) * std::cos(u_rad) - std::sin(node_rad) * std::sin(u_rad) * std::cos(I_rad));
                double hy = r * (std::sin(node_rad) * std::cos(u_rad) + std::cos(node_rad) * std::sin(u_rad) * std::cos(I_rad));

                b.orbit_points_2d.push_back(ImVec2(static_cast<float>(hx), static_cast<float>(hy)));
            }
        }
    }

    void render_header_controls(rouen::ui::ui_context& ui) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));

        // Date / Time Display
        ui.text_colored(colors[1], ICON_MD_SCHEDULE " UTC Date & Time:");
        ImGui::SameLine();
        ImGui::Text("%s", get_current_date_string().c_str());

        ImGui::SameLine(ImGui::GetWindowWidth() - 340);
        if (ImGui::Button(ICON_MD_TODAY " Now")) {
            set_to_current_time();
            update_all_positions();
        }
        ImGui::SameLine();
        if (ImGui::Button("-1D")) { julian_day_ -= 1.0; update_date_from_julian(); update_all_positions(); }
        ImGui::SameLine();
        if (ImGui::Button("+1D")) { julian_day_ += 1.0; update_date_from_julian(); update_all_positions(); }
        ImGui::SameLine();
        if (ImGui::Button("-1M")) { julian_day_ -= 30.4375; update_date_from_julian(); update_all_positions(); }
        ImGui::SameLine();
        if (ImGui::Button("+1M")) { julian_day_ += 30.4375; update_date_from_julian(); update_all_positions(); }
        ImGui::SameLine();
        if (ImGui::Button("-1Y")) { julian_day_ -= 365.25; update_date_from_julian(); update_all_positions(); }
        ImGui::SameLine();
        if (ImGui::Button("+1Y")) { julian_day_ += 365.25; update_date_from_julian(); update_all_positions(); }

        // Animation Toggle & Speed Slider
        if (ImGui::Button(is_animating_ ? (ICON_MD_PAUSE " Pause") : (ICON_MD_PLAY_ARROW " Orbit Anim"))) {
            is_animating_ = !is_animating_;
            if (is_animating_) last_anim_time_ = std::chrono::steady_clock::now();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::SliderFloat("Speed (d/s)", &anim_speed_days_per_sec_, 0.1f, 100.0f, "%.1f d/s");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("Focus Center", bodies_[focused_center_index_].name.c_str())) {
            for (size_t i = 0; i < bodies_.size(); ++i) {
                bool is_sel = (focused_center_index_ == i);
                if (ImGui::Selectable(bodies_[i].name.c_str(), is_sel)) {
                    focused_center_index_ = i;
                    selected_body_index_ = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::PopStyleVar();
    }

    void render_orbit_canvas(rouen::ui::ui_context& ui) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y < 350.0f) avail.y = 350.0f;

        // View Controls Row
        ImGui::Checkbox("Log Scale", &use_logarithmic_scale_);
        ImGui::SameLine();
        ImGui::Checkbox("Orbits", &show_orbit_lines_);
        ImGui::SameLine();
        ImGui::Checkbox("Labels", &show_labels_);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &show_grid_);
        ImGui::SameLine();
        ImGui::Checkbox("Vectors", &show_vectors_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Zoom", &zoom_scale_, 0.2f, 5.0f, "%.1fx");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Tilt 3D", &tilt_angle_deg_, 0.0f, 75.0f, "%.0f°");
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_CENTER_FOCUS_STRONG " Reset View")) {
            pan_offset_ = ImVec2(0, 0);
            zoom_scale_ = 1.0f;
            tilt_angle_deg_ = 25.0f;
        }

        // Invisible canvas region for mouse interaction
        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = avail;
        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

        ImGui::InvisibleButton("SolarSystemCanvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool is_hovered = ImGui::IsItemHovered();
        bool is_active = ImGui::IsItemActive();

        if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            pan_offset_.x += delta.x;
            pan_offset_.y += delta.y;
        }
        if (is_hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                zoom_scale_ += wheel * 0.1f;
                zoom_scale_ = std::clamp(zoom_scale_, 0.1f, 15.0f);
            }
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->PushClipRect(canvas_p0, canvas_p1, true);

        // Render Cosmic Space Background
        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(10, 14, 26, 255));

        // Center calculation based on focused center body
        const CelestialBody& center_body = bodies_[focused_center_index_];
        ImVec2 canvas_center = ImVec2(canvas_p0.x + canvas_sz.x * 0.5f + pan_offset_.x, canvas_p0.y + canvas_sz.y * 0.5f + pan_offset_.y);

        double tilt_cos_d = std::cos(static_cast<double>(tilt_angle_deg_) * std::numbers::pi / 180.0);
        float tilt_cos = static_cast<float>(tilt_cos_d);
        float base_scale_px = (canvas_sz.x * 0.40f) * zoom_scale_;

        // Coordinate transformation lambda (Heliocentric -> Canvas 2D/3D screen space)
        auto world_to_screen = [&](double hx, double hy, double hz) -> ImVec2 {
            double rel_x = hx - center_body.helio_x;
            double rel_y = hy - center_body.helio_y;
            double rel_z = hz - center_body.helio_z;

            double dist_au = std::sqrt(rel_x * rel_x + rel_y * rel_y);
            double dist_scaled = dist_au;
            if (use_logarithmic_scale_ && dist_au > 0.001) {
                dist_scaled = std::log10(1.0 + dist_au * 3.0) / std::log10(1.0 + 30.0 * 3.0) * 30.0;
            }

            double angle = std::atan2(rel_y, rel_x);
            double px = dist_scaled * std::cos(angle);
            double py = dist_scaled * std::sin(angle);

            float sx = canvas_center.x + static_cast<float>(px) * (base_scale_px / 30.0f);
            float sy = canvas_center.y + (static_cast<float>(py) * tilt_cos - static_cast<float>(rel_z) * 0.5f) * (base_scale_px / 30.0f);
            return ImVec2(sx, sy);
        };

        // Render Celestial Concentric Grid / Distance Rings
        if (show_grid_) {
            const std::array<double, 6> grid_distances = {0.387, 1.0, 1.52, 5.2, 9.5, 30.0};
            for (double dist_au : grid_distances) {
                ImVec2 ring_center = world_to_screen(center_body.helio_x, center_body.helio_y, 0.0);
                float radius_px = static_cast<float>(use_logarithmic_scale_ ? (std::log10(1.0 + dist_au * 3.0) / std::log10(1.0 + 30.0 * 3.0) * 30.0) : dist_au) * (base_scale_px / 30.0f);
                draw_list->AddEllipse(ring_center, ImVec2(radius_px, radius_px * tilt_cos), IM_COL32(40, 55, 80, 80), 0.0f, 64, 1.0f);
            }
        }

        // Render Orbit Lines
        if (show_orbit_lines_) {
            for (const auto& b : bodies_) {
                if (b.orbit_points_2d.size() < 3) continue;

                ImVec2 prev_pt = world_to_screen(static_cast<double>(b.orbit_points_2d[0].x), static_cast<double>(b.orbit_points_2d[0].y), 0.0);
                for (size_t i = 1; i < b.orbit_points_2d.size(); ++i) {
                    ImVec2 curr_pt = world_to_screen(static_cast<double>(b.orbit_points_2d[i].x), static_cast<double>(b.orbit_points_2d[i].y), 0.0);
                    ImVec4 col = b.color;
                    draw_list->AddLine(prev_pt, curr_pt, ImColor(ImVec4(col.x, col.y, col.z, 0.35f)), 1.5f);
                    prev_pt = curr_pt;
                }
            }
        }

        // Render Sun Radial Glow
        ImVec2 sun_screen = world_to_screen(bodies_[0].helio_x, bodies_[0].helio_y, bodies_[0].helio_z);
        draw_list->AddCircleFilled(sun_screen, 24.0f * zoom_scale_, IM_COL32(255, 220, 80, 40));
        draw_list->AddCircleFilled(sun_screen, 16.0f * zoom_scale_, IM_COL32(255, 235, 120, 100));
        draw_list->AddCircleFilled(sun_screen, 10.0f * zoom_scale_, IM_COL32(255, 255, 200, 255));

        // Render Planets & Celestial Bodies
        for (size_t i = 0; i < bodies_.size(); ++i) {
            const auto& b = bodies_[i];
            ImVec2 screen_pos = world_to_screen(b.helio_x, b.helio_y, b.helio_z);

            // Skip drawing if outside window bounds
            if (screen_pos.x < canvas_p0.x - 50 || screen_pos.x > canvas_p1.x + 50 ||
                screen_pos.y < canvas_p0.y - 50 || screen_pos.y > canvas_p1.y + 50) continue;

            bool is_selected = (selected_body_index_ == i);
            float body_r = b.display_size;

            // Target crosshair reticle for selected body
            if (is_selected) {
                draw_list->AddCircle(screen_pos, body_r + 8.0f, IM_COL32(100, 220, 255, 255), 32, 2.0f);
                draw_list->AddLine(ImVec2(screen_pos.x - body_r - 12.0f, screen_pos.y), ImVec2(screen_pos.x - body_r - 4.0f, screen_pos.y), IM_COL32(100, 220, 255, 255), 1.5f);
                draw_list->AddLine(ImVec2(screen_pos.x + body_r + 4.0f, screen_pos.y), ImVec2(screen_pos.x + body_r + 12.0f, screen_pos.y), IM_COL32(100, 220, 255, 255), 1.5f);
                draw_list->AddLine(ImVec2(screen_pos.x, screen_pos.y - body_r - 12.0f), ImVec2(screen_pos.x, screen_pos.y - body_r - 4.0f), IM_COL32(100, 220, 255, 255), 1.5f);
                draw_list->AddLine(ImVec2(screen_pos.x, screen_pos.y + body_r + 4.0f), ImVec2(screen_pos.x, screen_pos.y + body_r + 12.0f), IM_COL32(100, 220, 255, 255), 1.5f);
            }

            // Saturn Rings Special Draw
            if (b.id == "saturn") {
                draw_list->AddEllipse(screen_pos, ImVec2(body_r * 2.2f, body_r * 0.8f * tilt_cos), IM_COL32(230, 210, 160, 180), 0.0f, 32, 2.5f);
            }

            // Draw Planet Sphere
            ImVec4 c = b.color;
            draw_list->AddCircleFilled(screen_pos, body_r, ImColor(c));
            draw_list->AddCircle(screen_pos, body_r, IM_COL32(255, 255, 255, 120), 0, 1.0f);

            // Motion Vector Arrow
            if (show_vectors_ && b.id != "sun") {
                double vx = -b.helio_y;
                double vy = b.helio_x;
                double v_len = std::sqrt(vx * vx + vy * vy);
                if (v_len > 1e-6) {
                    vx = (vx / v_len) * 0.5;
                    vy = (vy / v_len) * 0.5;
                    ImVec2 arrow_end = world_to_screen(b.helio_x + vx, b.helio_y + vy, b.helio_z);
                    draw_list->AddLine(screen_pos, arrow_end, IM_COL32(255, 200, 100, 200), 1.5f);
                }
            }

            // Click to select
            ImVec2 mpos = ImGui::GetMousePos();
            float dist_m = std::sqrt((mpos.x - screen_pos.x) * (mpos.x - screen_pos.x) + (mpos.y - screen_pos.y) * (mpos.y - screen_pos.y));
            if (is_hovered && dist_m <= body_r + 6.0f) {
                ImGui::SetTooltip("%s (%s)\nDist to Sun: %.3f AU\nDist to Earth: %.3f AU\nConstellation: %s %s\nMag: %.1f",
                    b.name.c_str(), b.category.c_str(), b.helio_dist, b.geo_dist, b.constellation.c_str(), b.constellation_symbol.c_str(), b.mag);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    selected_body_index_ = i;
                }
            }

            // Labels
            if (show_labels_) {
                std::string label_str = std::format("{} {}", b.symbol, b.name);
                draw_list->AddText(ImVec2(screen_pos.x + body_r + 4.0f, screen_pos.y - 8.0f), IM_COL32(230, 240, 255, 220), label_str.c_str());
            }
        }

        draw_list->PopClipRect();

        // Selected Body Overlay Inspector Card (Top-right corner of canvas)
        if (selected_body_index_ < bodies_.size()) {
            const auto& sel = bodies_[selected_body_index_];
            ImGui::SetCursorScreenPos(ImVec2(canvas_p0.x + 12.0f, canvas_p0.y + 12.0f));
            ImGui::BeginChild("BodyInspectorOverlay", ImVec2(280.0f, 190.0f), true, ImGuiWindowFlags_NoScrollbar);
            
            ui.text_colored(sel.color, std::format("{} {} Details", sel.symbol, sel.name).c_str());
            ui.separator();
            ImGui::Text("Category: %s", sel.category.c_str());
            ImGui::Text("Heliocentric Dist: %.4f AU", sel.helio_dist);
            ImGui::Text("Distance to Earth: %.4f AU (%.1fM km)", sel.geo_dist, sel.geo_dist * 149.6);
            ImGui::Text("Light Travel Time: %.1f min (%.0f s)", sel.light_time_sec / 60.0, sel.light_time_sec);
            ImGui::Text("Sky Position: RA %.2fh | Dec %.1f°", sel.ra_hours, sel.dec_deg);
            ImGui::Text("Constellation: %s %s", sel.constellation.c_str(), sel.constellation_symbol.c_str());
            ImGui::Text("Apparent Mag: %.1f | Phase: %.0f%%", sel.mag, sel.phase_percent);

            if (ImGui::Button(ICON_MD_CENTER_FOCUS_WEAK " Center Camera Here")) {
                focused_center_index_ = selected_body_index_;
            }
            ImGui::EndChild();
        }
    }

    void render_ephemeris_table(rouen::ui::ui_context& ui) {
        ui.text_colored(colors[2], ICON_MD_GRID_ON " Solar System Ephemeris Table");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##SearchEphem", "Filter bodies...", search_filter_, sizeof(search_filter_));

        if (ImGui::BeginTable("EphemerisTableGrid", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
            ImGui::TableSetupColumn("Name / Symbol", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Helio Dist (AU)", ImGuiTableColumnFlags_WidthFixed, 105.0f);
            ImGui::TableSetupColumn("Geo Dist (AU)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Light Time", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn("RA (Hours)", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn("Dec (Deg)", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn("Constellation", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Visual Mag", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            std::string filter_str = search_filter_;
            for (size_t i = 0; i < bodies_.size(); ++i) {
                const auto& b = bodies_[i];
                if (!filter_str.empty() && !::helpers::StringHelper::contains_case_insensitive(b.name, filter_str)) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool is_sel = (selected_body_index_ == i);
                std::string label = std::format("{} {}", b.symbol, b.name);
                if (ImGui::Selectable(label.c_str(), is_sel, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_body_index_ = i;
                }

                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", b.category.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.4f AU", b.helio_dist);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f AU", b.geo_dist);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.1f min", b.light_time_sec / 60.0);
                ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f h", b.ra_hours);
                ImGui::TableSetColumnIndex(6); ImGui::Text("%.1f°", b.dec_deg);
                ImGui::TableSetColumnIndex(7); ImGui::Text("%s %s", b.constellation.c_str(), b.constellation_symbol.c_str());
                ImGui::TableSetColumnIndex(8); ImGui::Text("%.1f", b.mag);
            }
            ImGui::EndTable();
        }
    }

    void render_moon_phases_view(rouen::ui::ui_context& ui) {
        ui.text_colored(colors[1], ICON_MD_BEDTIME " Earth's Moon Phase & Inner Planets Illumination");
        ui.separator();

        const auto& moon = bodies_[4]; // Moon
        ImGui::BeginChild("MoonPhaseDiagram", ImVec2(320.0f, 240.0f), true);
        ui.text_colored(colors[2], "Earth's Moon Current Phase");
        ui.separator();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 center = ImVec2(p0.x + 80.0f, p0.y + 80.0f);
        float radius = 45.0f;

        // Draw Moon Sphere
        dl->AddCircleFilled(center, radius, IM_COL32(210, 215, 225, 255));
        
        // Shadow overlay based on phase illumination
        float shadow_w = (100.0f - static_cast<float>(moon.phase_percent)) / 100.0f * (radius * 2.0f);
        dl->AddEllipseFilled(ImVec2(center.x + (shadow_w - radius) * 0.5f, center.y), ImVec2(shadow_w, radius), IM_COL32(30, 35, 45, 220));
        dl->AddCircle(center, radius, IM_COL32(255, 255, 255, 180), 0, 2.0f);

        ImGui::SetCursorScreenPos(ImVec2(p0.x + 180.0f, p0.y + 20.0f));
        ImGui::BeginGroup();
        std::string phase_name = "Full Moon";
        if (moon.phase_percent < 5) phase_name = "New Moon " ICON_MD_BEDTIME;
        else if (moon.phase_percent < 45) phase_name = "Crescent Moon " ICON_MD_BEDTIME;
        else if (moon.phase_percent < 55) phase_name = "Quarter Moon " ICON_MD_BEDTIME;
        else if (moon.phase_percent < 95) phase_name = "Gibbous Moon " ICON_MD_BEDTIME;
        else phase_name = "Full Moon " ICON_MD_BEDTIME;

        ImGui::Text("Phase: %s", phase_name.c_str());
        ImGui::Text("Illumination: %.1f%%", moon.phase_percent);
        ImGui::Text("Geocentric Dist: %.1f km", moon.geo_dist * 149597870.7);
        ImGui::Text("Elongation: %.1f°", moon.solar_elongation);
        ImGui::EndGroup();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("InnerPlanetsPhase", ImVec2(0.0f, 240.0f), true);
        ui.text_colored(colors[2], "Inner Planet Phases (Telescopic View)");
        ui.separator();

        const auto& merc = bodies_[1];
        const auto& ven = bodies_[2];

        ImGui::Text("%s Mercury: Illumination %.1f%% | Elongation %.1f°", merc.symbol.c_str(), merc.phase_percent, merc.solar_elongation);
        ImGui::ProgressBar(static_cast<float>(merc.phase_percent) / 100.0f, ImVec2(-1, 16), std::format("{:.1f}%", merc.phase_percent).c_str());
        ImGui::Spacing();

        ImGui::Text("%s Venus: Illumination %.1f%% | Elongation %.1f°", ven.symbol.c_str(), ven.phase_percent, ven.solar_elongation);
        ImGui::ProgressBar(static_cast<float>(ven.phase_percent) / 100.0f, ImVec2(-1, 16), std::format("{:.1f}%", ven.phase_percent).c_str());
        
        ImGui::Separator();
        if (ven.solar_elongation > 15.0) {
            if (ven.ra_hours > bodies_[0].ra_hours) {
                ui.text_colored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Venus is currently the 'Evening Star' (Visible after Sunset) " ICON_MD_NIGHTS_STAY);
            } else {
                ui.text_colored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Venus is currently the 'Morning Star' (Visible before Sunrise) " ICON_MD_WB_TWILIGHT);
            }
        }
        ImGui::EndChild();
    }

    void render_events_view(rouen::ui::ui_context& ui) {
        ui.text_colored(colors[1], ICON_MD_EXPLORE " Sky Visibility & Planetary Alignment Radar");
        ui.separator();

        ui.text_colored(colors[2], "Naked-Eye Visible Planets Tonight:");
        for (const auto& b : bodies_) {
            if (b.category == "Major Planet" && b.mag < 6.0) {
                std::string status = (b.solar_elongation > 20.0) ? "Visible in Night Sky " ICON_MD_CHECK : "Near Sun in Daytime Sky " ICON_MD_WB_SUNNY;
                ImGui::BulletText("%s %-10s | Mag: %-4.1f | Elongation: %-4.1f° | %s (%s) | %s",
                    b.symbol.c_str(), b.name.c_str(), b.mag, b.solar_elongation, b.constellation.c_str(), b.constellation_symbol.c_str(), status.c_str());
            }
        }

        ui.separator();
        ui.text_colored(colors[2], "Close Conjunctions & Alignments (< 5° Separation):");
        bool conjunction_found = false;
        for (size_t i = 1; i < bodies_.size(); ++i) {
            for (size_t j = i + 1; j < bodies_.size(); ++j) {
                if (bodies_[i].category == "Sun" || bodies_[j].category == "Sun") continue;

                double dra = (bodies_[i].ra_hours - bodies_[j].ra_hours) * 15.0;
                double ddec = bodies_[i].dec_deg - bodies_[j].dec_deg;
                double sep_deg = std::sqrt(dra * dra + ddec * ddec);

                if (sep_deg < 5.0) {
                    conjunction_found = true;
                    ImGui::Text("  " ICON_MD_AUTO_AWESOME " Conjunction: %s %s & %s %s (Separation: %.2f° in %s)",
                        bodies_[i].symbol.c_str(), bodies_[i].name.c_str(),
                        bodies_[j].symbol.c_str(), bodies_[j].name.c_str(),
                        sep_deg, bodies_[i].constellation.c_str());
                }
            }
        }
        if (!conjunction_found) {
            ImGui::Text("  No close planetary conjunctions (< 5°) detected on this date.");
        }
    }
};

} // namespace rouen::cards
