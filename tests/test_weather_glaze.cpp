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
