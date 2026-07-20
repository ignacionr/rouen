#include "../interface/card.hpp"

namespace rouen::cards {

// C++23 concept for converter functions
template<typename T>
concept ConversionFunction = requires(T t, double value) {
    { t(value) } -> std::convertible_to<double>;
};

// Base class for conversion categories
class conversion_category {
public:
    struct unit_info {
        std::string name;
        std::string symbol;
        std::function<double(double)> to_base;
        std::function<double(double)> from_base;
    };

    virtual ~conversion_category() = default;
    virtual std::string_view name() const = 0;
    virtual const std::vector<unit_info>& units() const = 0;
    virtual std::string format_result(double value, const unit_info& unit) const = 0;
};

// Length conversion category
class length_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Meters", "m", [](double v) { return v; }, [](double v) { return v; }},
        {"Kilometers", "km", [](double v) { return v * 1000.0; }, [](double v) { return v / 1000.0; }},
        {"Centimeters", "cm", [](double v) { return v / 100.0; }, [](double v) { return v * 100.0; }},
        {"Millimeters", "mm", [](double v) { return v / 1000.0; }, [](double v) { return v * 1000.0; }},
        {"Inches", "in", [](double v) { return v * 0.0254; }, [](double v) { return v / 0.0254; }},
        {"Feet", "ft", [](double v) { return v * 0.3048; }, [](double v) { return v / 0.3048; }},
        {"Yards", "yd", [](double v) { return v * 0.9144; }, [](double v) { return v / 0.9144; }},
        {"Miles", "mi", [](double v) { return v * 1609.344; }, [](double v) { return v / 1609.344; }}
    };

public:
    std::string_view name() const override { return "Length"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Area conversion category
class area_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Square Meters", "m²", [](double v) { return v; }, [](double v) { return v; }},
        {"Square Kilometers", "km²", [](double v) { return v * 1000000.0; }, [](double v) { return v / 1000000.0; }},
        {"Hectares", "ha", [](double v) { return v * 10000.0; }, [](double v) { return v / 10000.0; }},
        {"Acres", "ac", [](double v) { return v * 4046.856; }, [](double v) { return v / 4046.856; }},
        {"Square Feet", "ft²", [](double v) { return v * 0.092903; }, [](double v) { return v / 0.092903; }},
        {"Square Inches", "in²", [](double v) { return v * 0.00064516; }, [](double v) { return v / 0.00064516; }}
    };

public:
    std::string_view name() const override { return "Area"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Weight conversion category
class weight_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Kilograms", "kg", [](double v) { return v; }, [](double v) { return v; }},
        {"Grams", "g", [](double v) { return v / 1000.0; }, [](double v) { return v * 1000.0; }},
        {"Milligrams", "mg", [](double v) { return v / 1000000.0; }, [](double v) { return v * 1000000.0; }},
        {"Pounds", "lb", [](double v) { return v * 0.453592; }, [](double v) { return v / 0.453592; }},
        {"Ounces", "oz", [](double v) { return v * 0.0283495; }, [](double v) { return v / 0.0283495; }},
        {"Stones", "st", [](double v) { return v * 6.35029; }, [](double v) { return v / 6.35029; }},
        {"Tonnes", "t", [](double v) { return v * 1000.0; }, [](double v) { return v / 1000.0; }}
    };

public:
    std::string_view name() const override { return "Weight"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Volume conversion category
class volume_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Liters", "L", [](double v) { return v; }, [](double v) { return v; }},
        {"Milliliters", "mL", [](double v) { return v / 1000.0; }, [](double v) { return v * 1000.0; }},
        {"Cubic Meters", "m³", [](double v) { return v * 1000.0; }, [](double v) { return v / 1000.0; }},
        {"Gallons (US)", "gal", [](double v) { return v * 3.78541; }, [](double v) { return v / 3.78541; }},
        {"Quarts (US)", "qt", [](double v) { return v * 0.946353; }, [](double v) { return v / 0.946353; }},
        {"Pints (US)", "pt", [](double v) { return v * 0.473176; }, [](double v) { return v / 0.473176; }},
        {"Cups (US)", "cup", [](double v) { return v * 0.236588; }, [](double v) { return v / 0.236588; }},
        {"Fluid Ounces (US)", "fl oz", [](double v) { return v * 0.0295735; }, [](double v) { return v / 0.0295735; }}
    };

public:
    std::string_view name() const override { return "Volume"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Temperature conversion category
class temperature_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Celsius", "°C",
            [](double v) { return v; },
            [](double v) { return v; }},
        {"Fahrenheit", "°F",
            [](double v) { return (v - 32.0) * 5.0 / 9.0; },
            [](double v) { return v * 9.0 / 5.0 + 32.0; }},
        {"Kelvin", "K",
            [](double v) { return v - 273.15; },
            [](double v) { return v + 273.15; }},
        {"Rankine", "°R",
            [](double v) { return (v - 491.67) * 5.0 / 9.0; },
            [](double v) { return v * 9.0 / 5.0 + 491.67; }}
    };

public:
    std::string_view name() const override { return "Temperature"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.2f} {}", value, unit.symbol);
    }
};

// Time conversion category
class time_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Seconds", "s", [](double v) { return v; }, [](double v) { return v; }},
        {"Minutes", "min", [](double v) { return v * 60.0; }, [](double v) { return v / 60.0; }},
        {"Hours", "h", [](double v) { return v * 3600.0; }, [](double v) { return v / 3600.0; }},
        {"Days", "d", [](double v) { return v * 86400.0; }, [](double v) { return v / 86400.0; }},
        {"Weeks", "wk", [](double v) { return v * 604800.0; }, [](double v) { return v / 604800.0; }},
        {"Months", "mo", [](double v) { return v * 2629746.0; }, [](double v) { return v / 2629746.0; }}, // Average month
        {"Years", "yr", [](double v) { return v * 31556952.0; }, [](double v) { return v / 31556952.0; }}  // Average year
    };

public:
    std::string_view name() const override { return "Time"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Speed conversion category
class speed_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Meters per Second", "m/s", [](double v) { return v; }, [](double v) { return v; }},
        {"Kilometers per Hour", "km/h", [](double v) { return v / 3.6; }, [](double v) { return v * 3.6; }},
        {"Miles per Hour", "mph", [](double v) { return v / 2.23694; }, [](double v) { return v * 2.23694; }},
        {"Feet per Second", "ft/s", [](double v) { return v / 3.28084; }, [](double v) { return v * 3.28084; }},
        {"Knots", "kn", [](double v) { return v / 1.94384; }, [](double v) { return v * 1.94384; }}
    };

public:
    std::string_view name() const override { return "Speed"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Energy conversion category
class energy_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Joules", "J", [](double v) { return v; }, [](double v) { return v; }},
        {"Kilojoules", "kJ", [](double v) { return v * 1000.0; }, [](double v) { return v / 1000.0; }},
        {"Calories", "cal", [](double v) { return v * 4.184; }, [](double v) { return v / 4.184; }},
        {"Kilocalories", "kcal", [](double v) { return v * 4184.0; }, [](double v) { return v / 4184.0; }},
        {"Watt Hours", "Wh", [](double v) { return v * 3600.0; }, [](double v) { return v / 3600.0; }},
        {"Kilowatt Hours", "kWh", [](double v) { return v * 3600000.0; }, [](double v) { return v / 3600000.0; }},
        {"Foot Pounds", "ft·lb", [](double v) { return v * 1.35582; }, [](double v) { return v / 1.35582; }}
    };

public:
    std::string_view name() const override { return "Energy"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Data encoding conversion category
class encoding_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Text", "txt",
            [](double) { return 0.0; }, // Placeholder - actual conversion handled differently
            [](double) { return 0.0; }},
        {"Base64", "b64",
            [](double) { return 0.0; },
            [](double) { return 0.0; }},
        {"Hexadecimal", "hex",
            [](double) { return 0.0; },
            [](double) { return 0.0; }},
        {"URL Encoded", "url",
            [](double) { return 0.0; },
            [](double) { return 0.0; }}
    };

public:
    std::string_view name() const override { return "Encoding"; }
    const std::vector<unit_info>& units() const override { return units_; }

    std::string format_result(double, const unit_info&) const override {
        return ""; // Handled separately for text conversions
    }
};

// Main converter card
class converter : public card {
public:
    converter();
    bool render() override;
    std::string get_uri() const override;

private:
    std::vector<std::unique_ptr<conversion_category>> categories_;
    size_t current_category_index_ = 0;
    size_t from_unit_index_ = 0;
    size_t to_unit_index_ = 1;

    // Input buffers
    std::array<char, 256> numeric_input_{"1.0"};
    std::array<char, 1024> text_input_{"Hello, World!"};
    std::string conversion_result_;
    std::string error_message_;

    void render_category_selector();
    void render_numeric_converter();
    void render_encoding_converter();
    void perform_numeric_conversion();
    void perform_encoding_conversion();

    // Encoding helper functions
    static std::string encode_base64(const std::string& input);
    static std::string decode_base64(const std::string& input);
    static std::string encode_hex(const std::string& input);
    static std::string decode_hex(const std::string& input);
    static std::string encode_url(const std::string& input);
    static std::string decode_url(const std::string& input);

    void reset_unit_selections();
    void clear_results();
    void reverse_units();
};

} // namespace rouen::cards
