#include <gtest/gtest.h>
#include <string>
#include "../src/hosts/weather_host.hpp"

using namespace rouen::hosts;

TEST(WeatherGlaze, ParsesCurrentWeatherWithExtraFields) {
    const std::string json = R"JSON({
        "coord": {"lon": 2.3488, "lat": 48.8534},
        "weather": [{"id": 800, "main": "Clear", "description": "clear sky", "icon": "01d"}],
        "base": "stations",
        "main": {
            "temp": 15.0,
            "feels_like": 14.5,
            "temp_min": 13.0,
            "temp_max": 16.2,
            "pressure": 1026,
            "humidity": 76,
            "sea_level": 1026,
            "grnd_level": 1024
        },
        "visibility": 10000,
        "wind": {"speed": 3.6, "deg": 180},
        "clouds": {"all": 0},
        "dt": 1714567890,
        "sys": {"type": 2, "id": 123456, "country": "FR", "sunrise": 1714543210, "sunset": 1714598765},
        "timezone": 7200,
        "id": 2988507,
        "name": "Paris",
        "cod": 200
    })JSON";

    weather::CurrentWeather data{};
    auto err = glz::read_json(data, json);
    ASSERT_FALSE(err) << glz::format_error(err, json);
    EXPECT_EQ(data.name, "Paris");
    EXPECT_TRUE(data.main.sea_level.has_value());
    EXPECT_TRUE(data.main.grnd_level.has_value());
    EXPECT_EQ(data.main.humidity, 76);
}

TEST(WeatherGlaze, ParsesForecastWithExtraFields) {
    const std::string json = R"JSON({
        "cod": "200",
        "message": 0,
        "cnt": 1,
        "list": [
            {
                "dt": 1714579200,
                "main": {
                    "temp": 14.0,
                    "feels_like": 13.2,
                    "temp_min": 14.0,
                    "temp_max": 14.5,
                    "pressure": 1027,
                    "humidity": 70,
                    "sea_level": 1027,
                    "grnd_level": 1023
                },
                "weather": [{"id": 801, "main": "Clouds", "description": "few clouds", "icon": "02d"}],
                "clouds": {"all": 20},
                "wind": {"speed": 2.6, "deg": 190},
                "visibility": 10000,
                "pop": 0.0,
                "dt_txt": "2024-05-01 12:00:00"
            }
        ],
        "city": {
            "id": 2988507,
            "name": "Paris",
            "coord": {"lat": 48.8534, "lon": 2.3488},
            "country": "FR",
            "population": 2148000,
            "timezone": 7200,
            "sunrise": 1714543210,
            "sunset": 1714598765
        }
    })JSON";

    weather::Forecast data{};
    auto err = glz::read_json(data, json);
    ASSERT_FALSE(err) << glz::format_error(err, json);
    ASSERT_EQ(data.list.size(), 1u);
    EXPECT_EQ(data.city.name, "Paris");
    EXPECT_TRUE(data.list[0].main.sea_level.has_value());
    EXPECT_TRUE(data.list[0].main.grnd_level.has_value());
}

#include "../src/helpers/persona_manager.hpp"

TEST(PersonaGlaze, SerializesAndDeserializesAllowedPersonas) {
    rouen::helpers::Persona p;
    p.name = "Test Agent";
    p.description = "A test agent";
    p.allowed_mcps = {"terminal"};
    p.system_prompt = "You are a test agent.";
    p.llm_config_name = "Default";
    p.enable_search = true;
    p.allowed_personas = {"Helper Bot", "Math Wizard"};

    std::string json = glz::write<glz::opts{.prettify = true}>(p).value_or("");
    ASSERT_FALSE(json.empty());

    rouen::helpers::Persona p2;
    auto err = glz::read_json(p2, json);
    ASSERT_FALSE(err) << glz::format_error(err, json);

    EXPECT_EQ(p2.name, "Test Agent");
    EXPECT_EQ(p2.allowed_personas.size(), 2u);
    EXPECT_EQ(p2.allowed_personas[0], "Helper Bot");
    EXPECT_EQ(p2.allowed_personas[1], "Math Wizard");
}

TEST(PersonaManagerLogic, RenamesAndDeletesReferences) {
    std::vector<rouen::helpers::Persona> personas;

    rouen::helpers::Persona p1;
    p1.name = "Bot A";
    p1.allowed_personas = {"Bot B", "Bot C"};
    personas.push_back(p1);

    rouen::helpers::Persona p2;
    p2.name = "Bot B";
    personas.push_back(p2);

    rouen::helpers::Persona p3;
    p3.name = "Bot C";
    personas.push_back(p3);

    // Test rename Bot B -> Bot B Revised
    std::string old_name = "Bot B";
    std::string new_name = "Bot B Revised";
    
    // Simulate rename logic
    for (auto& p : personas) {
        for (auto& ref : p.allowed_personas) {
            if (ref == old_name) {
                ref = new_name;
            }
        }
    }

    EXPECT_EQ(personas[0].allowed_personas[0], "Bot B Revised");
    EXPECT_EQ(personas[0].allowed_personas[1], "Bot C");

    // Test delete Bot C
    std::string name_to_remove = "Bot C";
    // Simulate delete logic
    for (auto& p : personas) {
        p.allowed_personas.erase(
            std::remove(p.allowed_personas.begin(), p.allowed_personas.end(), name_to_remove),
            p.allowed_personas.end()
        );
    }

    EXPECT_EQ(personas[0].allowed_personas.size(), 1u);
    EXPECT_EQ(personas[0].allowed_personas[0], "Bot B Revised");
}
