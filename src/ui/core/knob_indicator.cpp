#include "ui/core/knob_indicator.h"

#include <cstdlib>

namespace deluge::ui {

namespace {

/// Legacy's `kKnobPosOffset` (`definitions_cxx.hpp:348`), repeated rather than included: this
/// layer's headers are the toolkit's own and `definitions_cxx.hpp` is the application's.
constexpr int32_t kKnobPosOffset = 64;

/// @brief `indicator_leds::getBrightnessOutputValue` (`indicator_leds.cpp:271-283`): segments below
///        the fully-on count are full, the one at the count carries the remainder, the rest are
///        dark.
/// @param which_indicator     Segment index, counting from the bottom.
/// @param num_fully_on        How many segments are fully lit.
/// @param brightness          The squared remainder for the partially-lit segment.
/// @return The segment's brightness, 0..255.
int32_t brightness_output_value(int32_t which_indicator, int32_t num_fully_on, int32_t brightness) {
	if (which_indicator < num_fully_on) {
		return 255;
	}
	if (which_indicator == num_fully_on) {
		return brightness;
	}
	return 0;
}

/// @brief `indicator_leds::getBipolarBrightnessOutputValue` (`indicator_leds.cpp:246-269`): the
///        column reads outward from the middle, upper two segments for a positive deflection and
///        lower two (mirrored) for a negative one.
/// @param which_indicator Segment index, counting from the bottom.
/// @param num_fully_on    How many segments are fully lit.
/// @param brightness      The squared remainder for the partially-lit segment.
/// @param bipolar_level   `level - kKnobPosOffset`, the signed deflection.
/// @return The segment's brightness, 0..255.
int32_t bipolar_brightness_output_value(int32_t which_indicator, int32_t num_fully_on, int32_t brightness,
                                        int32_t bipolar_level) {
	if (bipolar_level > 0 && which_indicator > 1) {
		return brightness_output_value(which_indicator - 2, num_fully_on, brightness);
	}
	if (bipolar_level < 0 && which_indicator < 2) {
		// Legacy's `(whichIndicator * -1) + 1`: 0 -> 1 and 1 -> 0, i.e. the bottom pair mirrored so
		// segment 1 (nearest the middle) is the first to light.
		return brightness_output_value(-which_indicator + 1, num_fully_on, brightness);
	}
	return 0;
}

} // namespace

KnobIndicator knob_indicator_levels(uint8_t level, bool bipolar) {
	// `indicator_leds.cpp:193-207`.
	const int32_t bipolar_level = static_cast<int32_t>(level) - kKnobPosOffset;
	int32_t num_fully_on = 0;
	int32_t brightness = 0;
	if (bipolar) {
		const int32_t abs_level = std::abs(bipolar_level);
		num_fully_on = abs_level >> 5;
		brightness = (abs_level & 31) << 3;
	}
	else {
		num_fully_on = static_cast<int32_t>(level) >> 5;
		brightness = (static_cast<int32_t>(level) & 31) << 3;
	}
	brightness = (brightness * brightness) >> 8; // `:207` -- square it

	KnobIndicator out{};
	for (int32_t i = 0; i < 4; i++) { // `:211-238`
		const int32_t value = bipolar ? bipolar_brightness_output_value(i, num_fully_on, brightness, bipolar_level)
		                              : brightness_output_value(i, num_fully_on, brightness);
		out.levels[i] = static_cast<uint8_t>(value);
	}
	return out;
}

KnobIndicator knob_indicator_blink(uint32_t elapsed_ms, bool bipolar) {
	// `levelIndicatorBlinkOn` starts false and flips after each timeout, so half-period 0 is dark,
	// 1 is lit, and so on (`blinkKnobIndicatorLevelTimeout`, `indicator_leds.cpp:304-312`).
	const uint32_t half = elapsed_ms / kKnobIndicatorBlinkHalfMs;
	if ((half % 2) == 0) {
		// `setKnobIndicatorLevel(which, 0, false)` -- all dark, and deliberately *not* bipolar, so
		// there is no middle-segment glow on the dark half.
		return knob_indicator_levels(0, false);
	}
	// `levelIndicatorBlinksLeft` counts down from `kKnobIndicatorBlinkHalves` and is decremented
	// *after* each level is set, so half-period `half` is the call that saw `blinksLeft ==
	// kKnobIndicatorBlinkHalves - half`.
	const uint32_t blinks_left = half < kKnobIndicatorBlinkHalves ? kKnobIndicatorBlinkHalves - half : 0;
	if (bipolar && blinks_left > 1) {
		// `actuallySetKnobIndicatorLevel`'s blink override (`:216-228`): the two middle segments
		// full, the outer two dark. The `blinksLeft > 1` guard is legacy's own, and it bites: the
		// **last** lit half-period falls through to the ordinary bipolar arm instead, where level
		// 64 is a zero deflection and every segment comes out dark. So a bipolar blink ends dark
		// rather than lit, and that is reproduced here rather than smoothed over.
		return KnobIndicator{{0, 255, 255, 0}};
	}
	// `setKnobIndicatorLevel(which, 64, blinkOn ? bipolar : false)` -- the ordinary lit half.
	return knob_indicator_levels(64, bipolar);
}

} // namespace deluge::ui
