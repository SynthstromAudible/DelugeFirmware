#pragma once
#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"

#include <cstdint>

namespace deluge::ui::oled {

/// @brief A sample position spelled twice: as a duration, and as a raw sample count.
///
/// The body of `SampleMarkerEditor::renderOLED` (`gui/ui/sample_marker_editor.cpp:1135-1222`) --
/// everything below the screen title -- as a component, so the scene that owns the marker editor
/// calls no `OledFrame` drawing primitive of its own (`check_layer_rules.py`,
/// `scenes-no-frame-primitives`).
///
/// Two lines, the second `kSamplePositionLineGap` pixels below the first:
///
///   * the duration, in the coarsest units the value needs -- `1h 23m 45.678s`, `12.345 sec`, or
///     `123.45 msec`. There is always a seconds figure below a minute (sample 0 draws
///     `0.00 msec`); past a minute the seconds are drawn only when they are non-zero; and
///   * `(<n> smpl)`.
///
/// @note **No `printf`, no `std::to_string`, no allocation.** Legacy formats through
///       `intToString`/`memmove` into a stack buffer (`util/cfunctions.h`, which `src/ui` may not
///       include); this reimplements the same digits-plus-inserted-point shape locally.

/// @brief The vertical distance between the duration line and the sample-count line.
///
/// Legacy's `yPixel += 11` (`sample_marker_editor.cpp:1208`), which is neither `kTextSpacingY` (9)
/// nor the style's `row_height`: the two lines are deliberately looser than a menu list.
inline constexpr int32_t kSamplePositionLineGap = 11;

/// @brief Draw @p samples at @p sample_rate as a duration and a sample count.
///
/// @param f           Frame to paint into.
/// @param rect        Where the first line starts (`rect.x`, `rect.y`) and how wide the text may
///                    run. Legacy uses `xPixel = 1` and `yPixel = OLED_MAIN_TOPMOST_PIXEL + 17`.
/// @param samples     The marker's position, in samples.
/// @param sample_rate The sample's own rate, in Hz. A rate of 0 draws the sample-count line only
///                    rather than dividing by zero -- legacy has no such guard because
///                    `waveformBasicNavigator.sample` is always a loaded file there.
///
/// @warning One thing here is **not** a port: legacy draws the `h` and `m` unit letters at a fixed
///          x of `kTextSpacingX` instead of at the running cursor
///          (`sample_marker_editor.cpp:1158`, `:1167`), so any position past a minute has its unit
///          letter drawn over its own digits. This draws them at the cursor. See the comment at the
///          site; positions under a minute are pixel-identical to legacy.
void sample_position(OledFrame& f, gfx::oled::Rect rect, uint32_t samples, uint32_t sample_rate);

} // namespace deluge::ui::oled
