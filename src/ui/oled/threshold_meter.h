#pragma once

/// @file
/// @brief The audio recorder's threshold-take screen: the status line and the two-channel input level meter.

#include "ui/oled/frame.h"

#include <array>
#include <cstdint>

namespace deluge::ui::oled {

/// @brief What the threshold screen shows: `AudioRecorder`'s `ThresholdMeterState` plus the threshold's x.
struct ThresholdMeter {
	/// Where each channel's bar ends, x; the meter's start draws no bar.
	std::array<int32_t, 2> level_end_x{};
	/// Where the dotted threshold line blinks while waiting, x.
	int32_t threshold_x = 0;
	/// The take has started: RECORDING with the record dot, and solid bars up to the caution line.
	bool recording = false;
	/// The record dot's phase while recording, the threshold line's while waiting.
	bool blink_on = false;
	bool operator==(const ThresholdMeter&) const = default;
};

/// @brief A level's x on the meter (`levelPosition` in `gui/ui/audio_recorder.cpp`): levels up to the caution level
///        fill the space left of the caution line, and levels from there to full scale the space right of it.
/// @param level The natural log of the mean absolute q31 sample, as `SampleRecorder::getInputLevels` gives it.
/// @param width The panel's width; the meter ends 14 px short of it.
/// @return The x.
int32_t threshold_meter_x(float level, int32_t width);

/// @brief `AudioRecorder::renderOLED`'s threshold arm, pixel for pixel.
///
/// Legacy lays the screen out on the canvas against the panel's whole height. @p f must be the surface's root frame:
/// its origin carries the bezel, and every canvas position is taken back through it.
/// @param f The root frame.
/// @param m What to show.
void threshold_meter(OledFrame& f, const ThresholdMeter& m);

} // namespace deluge::ui::oled
