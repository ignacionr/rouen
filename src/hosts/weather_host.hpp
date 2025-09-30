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
#include <sstream>
#include <iomanip>
#include "../helpers/glaze_include.hpp"

#include "../helpers/fetch.hpp"
#include "../helpers/debug.hpp"

namespace rouen::hosts {

// Forward declarations for the weather data structures
namespace weather {
    struct Coord {
        double lat;
        double lon;
    };
    // OpenWeather uses 'coord' with lon/lat for current weather, and 'city' with name/country/coord in forecast
    struct Location {
        double lat{};
        double lon{};
        // Optional city info used in forecast 'city' object
        std::optional<std::string> name;
        std::optional<std::string> country;
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
    // New optional fields sometimes provided by the API
    std::optional<double> sea_level;   // sea_level pressure (hPa)
    std::optional<double> grnd_level;  // ground level pressure (hPa)
    std::optional<double> temp_kf;     // forecast temperature adjustment
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
        Coord coord;
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
        // Some forecast entries include a sys object with 'pod' (part of day)
        struct ForecastSys {
            std::string pod; // 'd' or 'n'
        };
        std::optional<ForecastSys> sys;
    };

    struct City {
        int id;
        std::string name;
        Coord coord;
        std::string country;
        std::optional<int> population;
        int timezone;
        std::optional<int64_t> sunrise;
        std::optional<int64_t> sunset;
    };

    struct Forecast {
        std::string cod;
        int message;
        int cnt;
        std::vector<ForecastItem> list;
        City city;
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
    WeatherHost() 
        : last_update_time_(std::chrono::steady_clock::now() - std::chrono::hours(2)), // Force initial update
          consecutive_failures_(0),
          backoff_minutes_(0)
    {
        WEATHER_INFO("WeatherHost: Initializing");
        
        // Get the API key from the environment variable
        auto var_api_key = std::getenv("OPENWEATHER_KEY");
        api_key_ = var_api_key ? var_api_key : std::string();
        if (api_key_.empty()) {
            WEATHER_ERROR("WeatherHost: OpenWeather API key not found in environment variables");
        } else {
            WEATHER_INFO_FMT("WeatherHost: Using OpenWeather API key: {}", api_key_);
        }
        
        // Default location - can be improved with geolocation
        location_ = "Montevideo,UY";
    }

    /**
     * Set the location for weather data
     */
    void setLocation(std::string_view location) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (location_ != location) {
            location_ = location;
            // Invalidate current data to force refresh on next request
            current_weather_.reset();
            forecast_.reset();
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
        
        // If we're in a backoff period due to previous failures, respect it
        if (consecutive_failures_ > 0 && elapsed < backoff_minutes_) {
            WEATHER_INFO_FMT("WeatherHost: Skipping update due to backoff period ({} min remaining)", 
                        backoff_minutes_ - elapsed);
            return false;
        }
        
        // Update weather every 30 minutes, or if we don't have data yet
        return elapsed >= 30 || !current_weather_ || !forecast_;
    }

    /**
     * Get the current weather data
     */
    const std::optional<weather::CurrentWeather> &getCurrentWeather() {
        updateWeatherIfNeeded();
        
        std::lock_guard<std::mutex> lock(mutex_);
        return current_weather_;
    }

    /**
     * Get the weather forecast data
     */
    std::optional<weather::Forecast> getForecast() {
        updateWeatherIfNeeded();
        
        std::lock_guard<std::mutex> lock(mutex_);
        return forecast_;
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
            WEATHER_INFO("WeatherHost: Creating new shared WeatherHost instance");
            shared_host = std::make_shared<WeatherHost>();
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
            url_encode(location_), api_key_
        );
        
        // Forecast URL
        std::string forecast_url = std::format(
            "https://api.openweathermap.org/data/2.5/forecast?q={}&appid={}&units=metric&cnt=5",
            url_encode(location_), api_key_
        );
        
        // Flag to track success of both API calls
        std::atomic<bool> current_success = false;
        std::atomic<bool> forecast_success = false;
        
        // Temporary storage for thread results
        std::string current_data;
        std::string forecast_data;
        
        // Fetch current weather
        std::thread current_thread([current_url, &current_data, &current_success]() {
            try {
                http::fetch fetcher(60); // Increase timeout for potential delays
                current_data = fetcher(current_url);
                current_success = true;
                WEATHER_INFO("WeatherHost: Fetched current weather data");
            } catch (const std::exception& e) {
                WEATHER_ERROR_FMT("WeatherHost: Failed to fetch current weather: {}", e.what());
            }
        });
        
        // Fetch forecast
        std::thread forecast_thread([forecast_url, &forecast_data, &forecast_success]() {
            try {
                http::fetch fetcher(60); // Increase timeout for potential delays
                forecast_data = fetcher(forecast_url);
                forecast_success = true;
                WEATHER_INFO("WeatherHost: Fetched forecast data");
            } catch (const std::exception& e) {
                WEATHER_ERROR_FMT("WeatherHost: Failed to fetch forecast: {}", e.what());
            }
        });
        
        // Wait for both threads to complete
        current_thread.join();
        forecast_thread.join();
        
        // Process results and update backoff state
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Parse current weather if fetch was successful
        if (current_success) {
            try {
                weather::CurrentWeather data;
                auto error = ::glz::read_json(data, current_data);
                if (error) {
                    WEATHER_ERROR_FMT("WeatherHost: Error parsing current weather data: {}", 
                                ::glz::format_error(error, current_data));
                    current_success = false;
                } else {
                    current_weather_ = std::move(data);
                }
            } catch (const std::exception& e) {
                WEATHER_ERROR_FMT("WeatherHost: Exception parsing current weather data: {}", e.what());
                current_success = false;
            }
        }
        
        // Parse forecast if fetch was successful
        if (forecast_success) {
            try {
                weather::Forecast data;
                auto error = ::glz::read_json(data, forecast_data);
                if (error) {
                    WEATHER_ERROR_FMT("WeatherHost: Error parsing forecast data: {}", 
                                ::glz::format_error(error, forecast_data));
                    forecast_success = false;
                } else {
                    forecast_ = std::move(data);
                }
            } catch (const std::exception& e) {
                WEATHER_ERROR_FMT("WeatherHost: Exception parsing forecast data: {}", e.what());
                forecast_success = false;
            }
        }
        
        // Update backoff state based on success or failure
        if (current_success && forecast_success) {
            // Successfully fetched both - reset backoff
            if (consecutive_failures_ > 0) {
                WEATHER_INFO("WeatherHost: API calls successful, resetting backoff");
                consecutive_failures_ = 0;
                backoff_minutes_ = 0;
            }
            // The weather name is available for card classes to use
        } else {
            // At least one API call failed - increase backoff
            consecutive_failures_++;
            
            // Exponential backoff: 5, 10, 20, 40, 60, 60, ... minutes (capped at 60)
            backoff_minutes_ = std::min(consecutive_failures_ < 2 ? 5 : backoff_minutes_ * 2, 60);
            
            WEATHER_WARN_FMT("WeatherHost: API call(s) failed, increased backoff to {} minutes after {} consecutive failures", 
                       backoff_minutes_, consecutive_failures_);
        }
    }

    mutable std::mutex mutex_;
    std::string api_key_;
    std::string location_;
    std::optional<weather::CurrentWeather> current_weather_;
    std::optional<weather::Forecast> forecast_;
    std::chrono::steady_clock::time_point last_update_time_;
    int consecutive_failures_;
    int backoff_minutes_;

    // Percent-encode a string for use in URL query parameters.
    // We preserve comma to allow the "City,CountryCode" format.
    static std::string url_encode(std::string_view s) {
        std::ostringstream oss;
        oss.fill('0');
        oss << std::hex << std::uppercase;
        for (char ch : s) {
            const unsigned char c = static_cast<unsigned char>(ch);
            // Unreserved characters according to RFC 3986 plus comma
            if ((c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~' || c == ',') {
                oss << static_cast<char>(c);
            } else if (c == ' ') {
                // Encode space as %20 (safer than '+')
                oss << "%20";
            } else {
                oss << '%' << std::setw(2) << int(c);
                oss << std::setw(0);
            }
        }
        return oss.str();
    }
};

} // namespace rouen::hosts

// Define glaze schemas for the weather structures
template <>
struct glz::meta<rouen::hosts::weather::Location> {
    using T = rouen::hosts::weather::Location;
    static constexpr auto values = glz::object(
        "lat", &T::lat,
        "lon", &T::lon,
        "name", &T::name,
        "country", &T::country
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Coord> {
    using T = rouen::hosts::weather::Coord;
    static constexpr auto values = glz::object(
        "lat", &T::lat,
        "lon", &T::lon
    );
    static constexpr auto options = glz::opts{ .error_on_unknown_keys = false };
};

template <>
struct glz::meta<rouen::hosts::weather::Weather> {
    using T = rouen::hosts::weather::Weather;
    static constexpr auto values = glz::object(
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
    static constexpr auto values = glz::object(
        "temp", &T::temp,
        "feels_like", &T::feels_like,
        "temp_min", &T::temp_min,
        "temp_max", &T::temp_max,
        "pressure", &T::pressure,
    "humidity", &T::humidity,
    "sea_level", &T::sea_level,
    "grnd_level", &T::grnd_level,
    "temp_kf", &T::temp_kf
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Wind> {
    using T = rouen::hosts::weather::Wind;
    static constexpr auto values = glz::object(
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
    static constexpr auto values = glz::object(
        "all", &T::all
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::Rain> {
    using T = rouen::hosts::weather::Rain;
    static constexpr auto values = glz::object(
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
    static constexpr auto values = glz::object(
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
    static constexpr auto values = glz::object(
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
    static constexpr auto values = glz::object(
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
struct glz::meta<rouen::hosts::weather::Forecast> {
    static constexpr auto values = glz::object(
        "cod", &rouen::hosts::weather::Forecast::cod,
        "message", &rouen::hosts::weather::Forecast::message,
        "cnt", &rouen::hosts::weather::Forecast::cnt,
        "list", &rouen::hosts::weather::Forecast::list,
        "city", &rouen::hosts::weather::Forecast::city
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::City> {
    using T = rouen::hosts::weather::City;
    static constexpr auto values = glz::object(
        "id", &T::id,
        "name", &T::name,
        "coord", &T::coord,
        "country", &T::country,
        "population", &T::population,
        "timezone", &T::timezone,
        "sunrise", &T::sunrise,
        "sunset", &T::sunset
    );
    static constexpr auto options = glz::opts{ .error_on_unknown_keys = false };
};

template <>
struct glz::meta<rouen::hosts::weather::ForecastItem> {
    static constexpr auto values = glz::object(
        "dt", &rouen::hosts::weather::ForecastItem::dt,
        "main", &rouen::hosts::weather::ForecastItem::main,
        "weather", &rouen::hosts::weather::ForecastItem::weather,
        "clouds", &rouen::hosts::weather::ForecastItem::clouds,
        "wind", &rouen::hosts::weather::ForecastItem::wind,
        "visibility", &rouen::hosts::weather::ForecastItem::visibility,
        "pop", &rouen::hosts::weather::ForecastItem::pop,
        "rain", &rouen::hosts::weather::ForecastItem::rain,
        "snow", &rouen::hosts::weather::ForecastItem::snow,
        "dt_txt", &rouen::hosts::weather::ForecastItem::dt_txt,
        "sys", &rouen::hosts::weather::ForecastItem::sys
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};

template <>
struct glz::meta<rouen::hosts::weather::ForecastItem::ForecastSys> {
    using T = rouen::hosts::weather::ForecastItem::ForecastSys;
    static constexpr auto values = glz::object(
        "pod", &T::pod
    );
    static constexpr auto options = glz::opts{
        .error_on_unknown_keys = false
    };
};
