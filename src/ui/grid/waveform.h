#pragma once
#include "gfx/grid/rgb.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::grid {

/// @brief Waveform-on-pads rendering.
///
/// @note Rendering only. The scan that turns a `Sample`'s clusters into per-column peaks
///       (finding peaks per column, computing brightness, computing bar positions) reads
///       `Sample`, `SampleRecorder` and the card, and stays outside this module. Everything below
///       is a pure function of its arguments.
///
/// Both entry points paint through `GridFrame::pad`, so they are region-local and the caller
/// decides which columns and rows the waveform occupies.

// ---------------------------------------------------------------------------------------------
// Single row
// ---------------------------------------------------------------------------------------------

/// @brief The largest per-column brightness this module accepts.
///
/// The value is 0..256 *inclusive*, one past what a `uint8_t` can hold, to match the source of
/// per-column brightness values exactly: clamping the span to 0..255 at the model boundary would
/// move the quantised output colour at some channel values (the colour formula's `& ~15` step
/// means a one-count difference in brightness is usually invisible but not always).
constexpr int32_t kWaveformMaxBrightness = 256;

/// @brief The colour to paint for one single-row column at @p brightness over base colour @p base.
///
/// Computed as:
///
///     colour_value = brightness * brightness;
///     base.transform([](channel) {
///         value_here = (colour_value * channel) >> 16;
///         return clamp((value_here + 6) & ~15, 0, channel_max);
///     });
///
/// @note The `& ~15` step is a deliberate bit-depth limiter, not rounding, and must not be "cleaned
///       up": it is what makes a waveform row land on a small set of shades rather than a smooth
///       gradient.
///
/// A @p brightness of 0 yields black, which is also the colour painted for a column with no data
/// -- so the two cases coincide and the caller needs no separate validity flag.
///
/// @param brightness Column brightness, in `[0, kWaveformMaxBrightness]`.
/// @param base       Base colour to scale.
/// @return The scaled, bit-depth-limited colour.
RGB waveform_col_colour(int32_t brightness, RGB base);

/// @brief Paint a one-row waveform across the frame's full width.
///
/// @p brightness is one value per *source* column, in `[0, kWaveformMaxBrightness]`, produced by
/// the model's peak scan. @p reversed mirrors the source index.
///
/// @note Mirroring is about the frame's own width (`f.bounds().w`), not a fixed display width, so
///       the caller must hand this component a frame exactly as wide as the waveform -- on a
///       Deluge that means opening a `region` over the main columns rather than passing a
///       sidebar-inclusive root frame, or the mirror would run across the sidebar too. Columns
///       with no entry in @p brightness are painted black.
///
/// @param f          Frame to paint into.
/// @param y          Row to paint.
/// @param brightness Per-source-column brightness values.
/// @param base       Base colour to scale each column's brightness against.
/// @param reversed   If true, column @p x reads source column `w - 1 - x` instead of `x`.
void waveform_row(GridFrame& f, int32_t y, std::span<const int32_t> brightness, RGB base, bool reversed);

// ---------------------------------------------------------------------------------------------
// Full screen
// ---------------------------------------------------------------------------------------------

/// @brief One column's vertical extent, in 8.24 fixed-point pad units.
///
/// The column's minimum and maximum sample values, re-centred on the sample's found value centre
/// point and scaled so that one whole pad row is `1 << 24`. `valid` marks whether this column has
/// been investigated by the scan; an uninvestigated column is left black.
///
/// @note The "at least 1 pixel's width" widening of a bar is part of the scan that produces
///       `min24`/`max24`, not the render, and lives with the rest of that scan.
struct PeakCol {
	int32_t min24 = 0;
	int32_t max24 = 0;
	bool valid = false;
};

/// @brief The two brightnesses a full-screen bar chooses between: coloured bars use
///        `kWaveformBarBrightnessColoured`, monochrome bars use `kWaveformBarBrightnessMono`.
///
/// Named as constants because `waveform_full` takes the brightness as an argument (the collapse
/// animation interpolates it) rather than deriving it from whether a colour is supplied.
constexpr int32_t kWaveformBarBrightnessColoured = 256;
constexpr int32_t kWaveformBarBrightnessMono = 128;

/// @brief Paint a full-height waveform, one vertical bar per column.
///
/// Clears the whole frame first, then draws a bar for every `valid` column. A main column past the
/// end of the waveform, or one that is not `valid`, ends up black -- the same fallback
/// `waveform_row` uses for a column with no brightness entry. The frame is the waveform's area, so
/// give the component a region rather than the sidebar-inclusive root frame.
///
/// Bars are centred on the frame: `f.bounds().h >> 1`, so the component works on a frame of any
/// height.
///
/// @warning **Precondition:** pass `kWaveformBarBrightnessMono` when @p rgb is empty. The
///          monochrome path relies on `brightness` never exceeding 128, since a monochrome call
///          with brightness 256 would compute `RGB::monochrome(256)`, which narrows to 0 in the
///          `uint8_t` parameter and paints black bars. This is not clamped here: the arithmetic is
///          transcribed exactly, and silently changing it would hide the caller's mistake instead
///          of surfacing it.
///
/// @note Unlike `waveform_row`, this takes no `reversed` flag: reversal is applied while choosing
///       which *source* column to read, before the peaks exist, so with the scan on the model's
///       side the caller reverses the span it passes in.
///
/// @param f          Frame to paint into.
/// @param cols       Per-column peak extents, one entry per column.
/// @param brightness Bar brightness; pass `kWaveformBarBrightnessColoured` or
///                    `kWaveformBarBrightnessMono` (see the precondition above).
/// @param rgb        Bar colour, or `nullopt` to paint monochrome bars.
void waveform_full(GridFrame& f, std::span<const PeakCol> cols, int32_t brightness, std::optional<RGB> rgb);

} // namespace deluge::ui::grid
