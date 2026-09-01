module;

#include "vu_meter.hpp"

export module rouen.helpers.vu_meter;

export namespace rouen::helpers::vu_meter {
    using rouen::helpers::vu_meter::VUMeterScaleType;
    using rouen::helpers::vu_meter::VUMeterTheme;
    using rouen::helpers::vu_meter::VUMeterTick;
    using rouen::helpers::vu_meter::VUMeterStyle;
    using rouen::helpers::vu_meter::VUMeterConfig;
    using rouen::helpers::vu_meter::get_preset_ticks;
    using rouen::helpers::vu_meter::draw_analog_dial;
    using rouen::helpers::vu_meter::draw_stereo_analog_dial;
    using rouen::helpers::vu_meter::draw_bar_meter;
    using rouen::helpers::vu_meter::render_analog_dial;
    using rouen::helpers::vu_meter::render_stereo_analog_dial;
    using rouen::helpers::vu_meter::render_bar_meter;
}
