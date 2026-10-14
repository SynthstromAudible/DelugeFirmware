#pragma once

/// @file
/// @brief The gold knobs' 4-LED indicator column: one knob position in, four segment brightnesses
///        out.

#include "ui/core/led_frame.h"

#include <cstdint>

namespace deluge::ui {

/// @brief Turn one knob's level into the four segment brightnesses its indicator column shows.
///
/// The port of the brightness half of `indicator_leds::actuallySetKnobIndicatorLevel`
/// (`src/deluge/hid/led/indicator_leds.cpp:180-244`) together with the two helpers it calls,
/// `getBipolarBrightnessOutputValue` (`:246-269`) and `getBrightnessOutputValue` (`:271-283`).
/// Everything else in that legacy function is state the port does not have here -- the
/// "unchanged, so skip" cache (`knobIndicatorLevels`/`knobIndicatorBipolar`, `:186-189`), the
/// metering timer (`setKnobIndicatorLevel`, `:172-178`) and the blink override
/// (`:216-228`) -- because an `LedFrame` is redeclared from scratch every tick and the caller owns
/// the blink phase (`KnobIndicatorBlink` below).
///
/// It lives in `src/ui/` rather than in the service that calls it because it is pure display
/// arithmetic over no model at all, which is exactly the split `scenes-no-frame-primitives`
/// (`scripts/check_layer_rules.py`) asks for: the scene decides *what level*, the toolkit decides
/// *what the column looks like*.
///
/// @param level   Legacy's `level`, "out of 128" -- and, as in legacy, a `uint8_t`: the parameter
///                is declared `uint8_t` at `indicator_leds.h:85`, so a caller's wider `knobPos`
///                is truncated before any of this arithmetic runs, and the port must truncate at
///                the same place to agree.
/// @param bipolar Legacy's `isBipolar`: the column reads outward from the middle rather than up
///                from the bottom, so `level - 64` is a signed deflection.
/// @return The four brightnesses, index 0 first, in `deluge_control_set_indicator`'s own order.
KnobIndicator knob_indicator_levels(uint8_t level, bool bipolar);

/// @brief How long `indicator_leds::blinkKnobIndicator` blinks an indicator for, in milliseconds.
///
/// `blinkKnobIndicator` (`indicator_leds.cpp:282-295`) sets `levelIndicatorBlinksLeft = 26` and
/// `blinkKnobIndicatorLevelTimeout` (`:304-312`) re-arms a 20 ms timer until the counter runs out,
/// so the blink lasts 26 half-periods of 20 ms.
constexpr uint32_t kKnobIndicatorBlinkHalfMs = 20;
/// @brief The number of half-periods `blinkKnobIndicator` runs for (`indicator_leds.cpp:292`).
constexpr uint32_t kKnobIndicatorBlinkHalves = 26;
/// @brief Total blink duration, `kKnobIndicatorBlinkHalves * kKnobIndicatorBlinkHalfMs`.
constexpr uint32_t kKnobIndicatorBlinkMs = kKnobIndicatorBlinkHalves * kKnobIndicatorBlinkHalfMs;

/// @brief One frame of `blinkKnobIndicator`'s flash, as a pure function of elapsed time.
///
/// Legacy drives this from a `TimerName::LEVEL_INDICATOR_BLINK` callback that alternates
/// `setKnobIndicatorLevel(which, blinkOn ? 64 : 0, blinkOn ? bipolar : false)` and then flips
/// `levelIndicatorBlinkOn` (`blinkKnobIndicatorLevelTimeout`, `indicator_leds.cpp:304-312`);
/// `levelIndicatorBlinkOn` starts **false**, so the first half-period is the dark one. On the lit
/// half of a *bipolar* blink, `actuallySetKnobIndicatorLevel`'s own override lights the two middle
/// segments outright rather than computing a brightness (`:216-228`), which is why this cannot be
/// spelled as two calls to `knob_indicator_levels`.
///
/// @param elapsed_ms Milliseconds since the blink began.
/// @param bipolar    The `isBipolar` `blinkKnobIndicator` was given.
/// @return The column for this instant.
KnobIndicator knob_indicator_blink(uint32_t elapsed_ms, bool bipolar);

} // namespace deluge::ui
