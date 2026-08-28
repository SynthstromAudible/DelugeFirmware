#pragma once
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::grid {

/// Waveform-on-pads rendering: spec B section 7's `waveform_row` / `waveform_full`, ported from
/// `WaveformRenderer` (gui/waveform/waveform_renderer.cpp).
///
/// **Rendering only.** Legacy's renderers do two jobs at once: they scan the `Sample`'s clusters
/// into per-column peaks (`findPeaksPerCol`, `getColBrightnessForSingleRow`,
/// `getColBarPositions`) and then turn those peaks into pad colours. Only the second half is here;
/// the scanning reads `Sample`, `SampleRecorder` and the card, and stays in `gui/waveform` until
/// it moves to `deluge-scenes` (spec B section 7 and section 10). Everything below is a pure
/// function of its arguments, so a spec can pin every branch without a card image.
///
/// Both entry points paint through `GridFrame::pad`, so they are region-local and the caller
/// decides which columns and rows the waveform occupies.

// ---------------------------------------------------------------------------------------------
// Single row -- WaveformRenderer::renderAsSingleRow (waveform_renderer.cpp:105-150)
// ---------------------------------------------------------------------------------------------

/// The largest per-column brightness legacy produces.
///
/// `getColBrightnessForSingleRow` (waveform_renderer.cpp:152-169) ends with
/// `std::min<int32_t>(peak16 >> 8, 256)` -- "Max 256 - for now. Looks great and bright" (:168) --
/// so the value legacy feeds the colour formula is 0..256 *inclusive*, one past what a `uint8_t`
/// can hold. Recorded as an amendment: the brief spells the brightness span
/// `std::span<const uint8_t> /*0..255 per col*/`, and `waveform_row` takes
/// `std::span<const int32_t>` instead. Clamping to 255 at the model boundary would move the
/// quantised output colour at some channel values (the formula's `& ~15` step means a one-count
/// difference in `brightness` is usually invisible but not always), and this port is meant to be
/// exact.
constexpr int32_t kWaveformMaxBrightness = 256;

/// The colour legacy paints for one single-row column at \p brightness over base colour \p base.
///
/// `renderAsSingleRow`, waveform_renderer.cpp:137-146, verbatim:
///
///     colourValue = (colourValue * colourValue);              // :137
///     rgb.transform([](channel) {                             // :140
///         int32_t valueHere = (colourValue * channel) >> 16;  // :141
///         return std::clamp<int32_t>((valueHere + 6) & ~15, 0, RGB::channel_max);  // :145
///     });
///
/// The `& ~15` is legacy's deliberate bit-depth limiter -- "to avoid problem with PIC firmware
/// where too many different colour shades cause big problems. The 6 is quite arbitrary, but I
/// think it looks good" (:142-144). It is not rounding and must not be "cleaned up": it is what
/// makes a waveform row land on a small set of shades.
///
/// A \p brightness of 0 yields black, which is also what legacy paints for a column it has no
/// data for (`colStatus != COL_STATUS_INVESTIGATED`, :127-130) -- so the two cases coincide and
/// the span needs no separate validity flag.
RGB waveform_col_colour(int32_t brightness, RGB base);

/// Paint a one-row waveform across the frame's full width -- spec B section 7's
/// `waveform_row(f, y, peaks)`.
///
/// \p brightness is one value per *source* column, in `[0, kWaveformMaxBrightness]`, produced by
/// the model's peak scan (legacy `getColBrightnessForSingleRow`). \p reversed mirrors the source
/// index, as `renderAsSingleRow` does at :131-134 (and :112-116 for the scan range).
///
/// Legacy mirrors about `kDisplayWidth`, the full width of the area it renders into; the port
/// mirrors about `f.bounds().w`, the frame's own width, which is the same thing *provided the
/// caller hands the component a frame exactly as wide as the waveform*. On a Deluge that means
/// opening a `region` over the 16 main columns rather than passing the 18-wide root frame -- a
/// root frame would mirror across the sidebar. Columns with no entry in \p brightness are painted
/// black, matching legacy's "If no data here (e.g. if Sample not recorded this far yet...)"
/// branch (:127-130).
void waveform_row(GridFrame& f, int32_t y, std::span<const int32_t> brightness, RGB base, bool reversed);

// ---------------------------------------------------------------------------------------------
// Full screen -- WaveformRenderer::renderFullScreen / renderOneCol / drawColBar
// (waveform_renderer.cpp:82-102, :764-778, :731-762)
// ---------------------------------------------------------------------------------------------

/// One column's vertical extent, in legacy's 8.24 fixed-point pad units.
///
/// Exactly what `getColBarPositions` (waveform_renderer.cpp:718-729) writes through its `min24` /
/// `max24` out-parameters: the column's minimum and maximum sample values, re-centred on the
/// sample's `getFoundValueCentrePoint()` and scaled by `getValueSpan()` so that one whole pad row
/// is `1 << 24`. `valid` is legacy's `colStatus[x] == COL_STATUS_INVESTIGATED` test
/// (`renderOneCol`, :771) lowered to a POD field, because the status array is model state.
///
/// The "at least 1 pixel's width" widening at :724-728 is part of the *scan*, not the render, and
/// stays with the model along with the rest of `getColBarPositions`.
struct PeakCol {
	int32_t min24 = 0;
	int32_t max24 = 0;
	bool valid = false;
};

/// The two brightnesses `renderOneCol` chooses between: `int32_t brightness = rgb ? 256 : 128`
/// (waveform_renderer.cpp:766). Named here because `waveform_full` takes the brightness as an
/// argument (the collapse animation interpolates it, :242-260) rather than deriving it.
constexpr int32_t kWaveformBarBrightnessColoured = 256;
constexpr int32_t kWaveformBarBrightnessMono = 128;

/// Paint a full-height waveform, one vertical bar per column -- spec B section 7's
/// `waveform_full(f, PeakGrid)`.
///
/// Clears the columns it covers first, as `renderFullScreen` does (`memset(thisImage[y], 0,
/// kDisplayWidth * 3)`, :92-95), then draws `drawColBar` for every `valid` column
/// (`renderOneCol`, :771-777). A column that is not `valid` is left black, which is what legacy's
/// early return leaves behind after that clear.
///
/// Bars are centred on the frame: legacy's `kDisplayHeight >> 1` (:733-734, :760) becomes
/// `f.bounds().h >> 1`, so the component works on a frame of any height.
///
/// Unlike `waveform_row`, this takes no `reversed` flag -- legacy applies reversal while choosing
/// which *source* column to read (`renderOneCol`, :769), i.e. before the peaks exist, so with the
/// scan on the model's side the caller reverses the span it passes.
void waveform_full(GridFrame& f, std::span<const PeakCol> cols, int32_t brightness, std::optional<RGB> rgb);

} // namespace deluge::ui::grid
