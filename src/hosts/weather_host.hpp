#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <optional>
#include <glaze/glaze.hpp>

#include "../helpers/fetch.hpp"
#include "../helpers/debug.hpp"

namespace rouen::hosts {

// Forward declarations for the weather data structures
namespace weather {
    struct Location {
        std::string name;
        std::string country;
        double lat;
        double lon;
    };

    struct Weather {
        int id;
        std::string main;
        std::string description;
        std::string icon;
    };

    struct Main {
        double temp;
        double feels_like;
        double temp_min;
        double temp_max;
        double pressure;
        int humidity;
    };

    struct Wind {
        double speed;
        double deg;
        std::optional<double> gust;
    };

    struct Clouds {
        int all;
    };

    struct Rain {
        std::optional<double> one_h; // 1h
        std::optional<double> three_h; // 3h
    };

    struct Snow {
        std::optional<double> one_h; // 1h
        std::optional<double> three_h; // 3h
    };

    struct Sys {
        int type;
        int id;
        std::string country;
        int64_t sunrise;
        int64_t sunset;
    };

    struct CurrentWeather {
        Location coord;
        std::vector<Weather> weather;
        std::string base;
        Main main;
        int visibility;
        Wind wind;
        Clouds clouds;
        std::optional<Rain> rain;
        std::optional<Snow> snow;
        int64_t dt;
        Sys sys;
        int timezone;
        int id;
        std::string name;
        int cod;
    };

    struct ForecastItem {
        int64_t dt;
        Main main;
        std::vector<Weather> weather;
        Clouds clouds;
        Wind wind;
        int visibility;
        double pop; // Probability of precipitation
        std::optional<Rain> rain;
        std::optional<Snow> snow;
        std::string dt_txt;
    };

    struct Forecast {
        std::string cod;
        int message;
        int cnt;
        std::vector<ForecastItem> list;
        Location city;
    };
}

/**
 * Weather Host Controller
 * 
 * This class acts as a controller for weather data, managing the communication
 * between the UI (cards) and the OpenWeather API.
 * It provides methods for fetching current weather and forecast data.
 */
class WeatherHost {
public:
    /**
     * Constructor initializes the Weather host with a system runner
     */
    WeatherHost(std::function<std::string(std::string_view)> system_runner) 
        : system_runner_(system_runner),
          last_update_time_(std::chrono::steady_clock::now() - std::chrono::hours(2)) // Force initial update
    {
        DB_INFO("WeatherHost: Initializing");
        
        // Get the API key from the environment variable
        api_key_ = std::getenv("OPENWEATHER_KEY");
        if (api_key_.empty()) {
            DB_ERROR("WeatherHost: OpenWeather API key not found in environment variables");
        } else {
            DB_INFO_FMT("WeatherHost: Using OpenWeather API key: {}", api_key_);
        }
        
        // Default location - can be improved with geolocation
        location_ = "Paris,fr";
    }

    /**
     * Set the location for weather data
     */
    void setLocation(std::string_view location) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (location_ != location) {
            location_ = location;
            // Invalidate current data to force refresh on next request
            current_weather_data_.clear();
            forecast_data_.clear();
        }
    }

    /**
     * Get the current location
     */
    std::string getLocation() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return location_;
    }

    /**
     * Check if the weather data needs to be updated
     */
    bool needsUpdate() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_update_time_).count();
        
        // Update weather every 30 minutes
        return elapsed >= 30 || current_weather_data_.empty() || forecast_data_.empty();
    }

    /**
     * Get the current weather data
     */
    std::optional<weather::CurrentWeather> getCurrentWeather() {
        updateWeatherIfNeeded();
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (current_weather_data_.empty()) {
            return std::nullopt;
        }
        
        try {
            weather::CurrentWeather data;
            auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(data, current_weather_data_);
            if (error) {
                DB_ERROR_FMT("WeatherHost: Error parsing current weather data: {}", 
                            glz::format_error(error));
                return std::nullopt;
            }
            return data;
        } catch (const std::exception& e) {
            DB_ERROR_FMT("WeatherHost: Exception parsing current weather data: {}", e.what());
            return std::nullopt;
        }
    }

    /**
     * Get the weather forecast data
     */
    std::optional<weather::Forecast> getForecast() {
        updateWeatherIfNeeded();
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (forecast_data_.empty()) {
            return std::nullopt;
        }
        
        try {
            weather::Forecast data;
            auto error = glz::read<glz::opts{.error_on_unknown_keys=false}>(data, forecast_data_);
            if (error) {
                DB_ERROR_FMT("WeatherHost: Error parsing forecast data: {}", 
                            glz::format_error(error));
                return std::nullopt;
            }
            return data;
        } catch (const std::exception& e) {
            DB_ERROR_FMT("WeatherHost: Exception parsing forecast data: {}", e.what());
            return std::nullopt;
        }
    }

    /**
     * Force a refresh of the weather data
     */
    void refreshWeather() {
        fetchWeatherData();
    }

    /**
     * Get access to the Weather host controller (needed for weather cards)
     */
    static std::shared_ptr<WeatherHost> getHost() {
        static std::mutex host_mutex;
        static std::shared_ptr<WeatherHost> shared_host = nullptr;
        
        std::lock_guard<std::mutex> lock(host_mutex);
        
        if (!shared_host) {
            DB_INFO("WeatherHost: Creating new shared WeatherHost instance");
            shared_host = std::make_shared<WeatherHost>([](std::string_view cmd) -> std::string {
                return ""; // Not using system commands in this implementation
            });
        }
        
        return shared_host;
    }

private:
    /**
     * Update weather data if needed
     */
    void updateWeatherIfNeeded() {
        if (needsUpdate()) {
            fetchWeatherData();
        }
    }

    /**
     * Fetch weather data from the OpenWeather API
     */
    void fetchWeatherData() {
        if (api_key_.empty()) {
            return;
        }
        
        last_update_time_ = std::chrono::steady_clock::now();
        
        // Current weather URL
        std::string current_url = std::format(
            "https://api.openweathermap.org/data/2.5/weather?q={}&appid={}&units=metric",
            location_, api_key_
        );
        
        // Forecast URL
        std::string forecast_url = std::format(
            "https://api.openweathermap.org/data/2.5/forecast?q={}&appid={}&units=metric&cnt=5",
            location_, api_key_
        );
        
        // Fetch current weather
        std::thread current_thread([this, current_url]() {
            try {
                http::fetch fetcher(60); // Increase timeout for potential delays
                std::string response = fetcher(current_url);
                
                std::lock_guard<std::mutex> lock(mutex_);
                current_weather_data_ = response;
                DB_INFO("WeatherHost: Fetched current weather data");
            } catch (const std::exception& e) {
                DB_ERROR_FMT("WeatherHost: Failed to fetch current weather: {}", e.what());
            }
        });
        
        // Fetch forecast
        std::thread forecast_thread([this, forecast_url]() {
            try {
                http::fetch fetcher(60); // Increase timeout for potential delays
                std::string response = fetcher(forecast_url);
                
                std::lock_guard<std::mutex> lock(mutex_);
                forecast_data_ = response;
                DB_INFO("WeatherHost: Fetched forecast data");
            } catch (const std::exception& e) {
                DB_ERROR_FMT("WeatherHost: Failed to fetch forecast: {}", e.what());
            }
        });
        
        // Let the threads run independently
        current_thread.detach();
        forecast_thread.detach();
    }

    std::function<std::string(std::string_view)> system_runner_;
    mutable std::mutex mutex_;
    std::string api_key_;
    std::string location_;
    std::string current_weather_data_;
    std::string forecast_data_;
    std::chrono::steady_clock::time_point last_update_time_;
};

} // namespace rouen::hosts

// Define glaze schemas for the weather structures
template <>
struct glz::meta<rouen::hosts::weather::Location> {
    using T = rouen::hosts::weather::Location;
    static constexpr auto value = glz::object(
        "name", &T::name,
        "country", &T::country,
        "lat", &T::lat,
        "lon", &T::lon
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Weather> {
    using T = rouen::hosts::weather::Weather;
    static constexpr auto value = glz::object(
        "id", &T::id,
        "main", &T::main,
        "description", &T::description,
        "icon", &T::icon
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Main> {
    using T = rouen::hosts::weather::Main;
    static constexpr auto value = glz::object(
        "temp", &T::temp,
        "feels_like", &T::feels_like,
        "temp_min", &T::temp_min,
        "temp_max", &T::temp_max,
        "pressure", &T::pressure,
        "humidity", &T::humidity
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Wind> {
    using T = rouen::hosts::weather::Wind;
    static constexpr auto value = glz::object(
        "speed", &T::speed,
        "deg", &T::deg,
        "gust", &T::gust
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Clouds> {
    using T = rouen::hosts::weather::Clouds;
    static constexpr auto value = glz::object(
        "all", &T::all
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Rain> {
    using T = rouen::hosts::weather::Rain;
    static constexpr auto value = glz::object(
        "1h", &T::one_h,
        "3h", &T::three_h
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Snow> {
    using T = rouen::hosts::weather::Snow;
    static constexpr auto value = glz::object(
        "1h", &T::one_h,
        "3h", &T::three_h
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Sys> {
    using T = rouen::hosts::weather::Sys;
    static constexpr auto value = glz::object(
        "type", &T::type,
        "id", &T::id,
        "country", &T::country,
        "sunrise", &T::sunrise,
        "sunset", &T::sunset
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::CurrentWeather> {
    using T = rouen::hosts::weather::CurrentWeather;
    static constexpr auto value = glz::object(
        "coord", &T::coord,
        "weather", &T::weather,
        "base", &T::base,
        "main", &T::main,
        "visibility", &T::visibility,
        "wind", &T::wind,
        "clouds", &T::clouds,
        "rain", &T::rain,
        "snow", &T::snow,
        "dt", &T::dt,
        "sys", &T::sys,
        "timezone", &T::timezone,
        "id", &T::id,
        "name", &T::name,
        "cod", &T::cod
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::ForecastItem> {
    using T = rouen::hosts::weather::ForecastItem;
    static constexpr auto value = glz::object(
        "dt", &T::dt,
        "main", &T::main,
        "weather", &T::weather,
        "clouds", &T::clouds,
        "wind", &T::wind,
        "visibility", &T::visibility,
        "pop", &T::pop,
        "rain", &T::rain,
        "snow", &T::snow,
        "dt_txt", &T::dt_txt
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Forecast> {
    using T = rouen::hosts::weather::Forecast;
    static constexpr auto value = glz::object(
        "cod", &T::cod,
        "message", &T::message,
        "cnt", &T::cnt,
        "list", &T::list,
        "city", &T::city
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};