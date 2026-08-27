#pragma once
#include "gfx/oled/geometry.h"
#include "gfx/oled/icon.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::ui::oled {

/// Everything `output_name` draws, resolved by the scene. Deliberately not an `Output*`: spec B
/// section 2 rule 2 keeps `model` out of this library, and `View::displayOutputName`
/// (gui/views/view.cpp:1992-2020) already does exactly this flattening before it calls
/// `drawOutputNameFromDetails`.
struct OutputNameData {
	/// `getOutputTypeName(outputType, channel)` (view.cpp:2091) -- "Synth", "Kit", "MIDI"...
	std::string_view type_text;
	/// The output's own name, when it has one. Also the place a scene puts the strings legacy
	/// substituted for a slot: "Lower"/"Upper" for an MPE zone, "Transpose", "1 and 2" for both
	/// CV outputs (view.cpp:2154-2174). Pass those with `name_empty = false`.
	std::string_view name;
	/// `slotToString(slot, sub_slot, buffer, 1)` (foundation/string_format.c:205-213) when
	/// `name_empty`: the decimal slot, plus `'A' + sub_slot` when `sub_slot` is not -1. Legacy's
	/// callers pass the channel *already incremented* (view.cpp:2150), and so must this one.
	int32_t slot;
	int32_t sub_slot;
	bool name_empty;
	/// `View::drawOutputNameFromDetails`' `editedByUser` (view.cpp:2025). **Draws nothing.** The
	/// parameter survives in legacy's signature but its body never reads it: the only renderer
	/// that ever did was the 7-segment one, deleted with the emulated-display feature (commit
	/// 6a4ccefbc). Carried so the scene's data does not have to change when a use returns; the
	/// task brief's "`edited` adds the legacy asterisk" describes no code in this tree.
	bool edited;
};

/// The pixel half of `View::drawOutputNameFromDetails` (gui/views/view.cpp:2087-2175): the output
/// type centred in the small face near the top, and the output's name -- or its slot -- centred
/// in the title face below it.
///
/// Both y positions are the legacy `#if OLED_MAIN_HEIGHT_PIXELS == 64` pair, now selected from
/// `f.canvas.geometry().height` exactly as `list`'s base-y is: `kOutputTypeY48`/`kOutputTypeY64`
/// (3 / 12) and `kOutputNameY48`/`kOutputNameY64` (17 / 30), all relative to \p rect's top
/// because the frame's origin already carries `OLED_MAIN_TOPMOST_PIXEL`.
///
/// The name is centred when it fits and left-aligned when it does not (view.cpp:2123-2131).
/// **Legacy also armed a side-scroller in the second case** (`OLED::setupSideScroller`, :2128);
/// that is `scroll_text`'s job now, and a scene that wants a long name to scroll composes the two
/// -- this widget owns no state and cannot. Likewise the clip's "Section N" second line
/// (:2133-2144) is the scene's, being model data this struct does not carry.
///
/// Clears nothing: legacy opened with `clearMainImage()` (:2089), which is the shell's business
/// in immediate mode.
void output_name(OledFrame& f, gfx::oled::Rect rect, const OutputNameData& data);

/// `OLED::renderEmulated7Seg` (hid/display/oled.cpp:768-803 as of commit 6a4ccefbc^, the last
/// commit before the emulated-display feature was removed): four seven-segment digits drawn at
/// 33 px pitch, each segment a hand-tuned run of `invertArea` calls.
///
/// \p segs is one bit-per-segment byte per digit, in the Deluge's own order, which is **not** the
/// textbook `gfedcba`: bit 0 is the *middle* bar, 1 and 2 the upper-left and lower-left uprights,
/// 3 the bottom bar, 4 and 5 the lower-right and upper-right uprights, 6 the top bar, and 7 the
/// decimal point. That falls out of the two tables legacy indexes the segments through -- the
/// horizontal bars in the order `{6, 0, 3}` and the uprights `{1, 2, 5, 4}` (:774, :784) -- and it
/// agrees with the panel's own digit table, `numberSegments[10] = {0x7E, 0x30, 0x6D, ...}`
/// (hid/display/seven_segment.cpp:60 @ 6a4ccefbc^), where '0' is everything but bit 0 and '1' is
/// bits 4 and 5 alone. A caller holding textbook codes will draw the wrong glyphs.
///
/// **A full-panel renderer with canvas-absolute constants.** Legacy laid it out against `main`
/// with no `OLED_MAIN_TOPMOST_PIXEL` term anywhere (rows 7 to 43 of a 48-row panel), so the
/// constants are converted here by subtracting the frame's origin -- the same convention
/// `decimal_value` uses for its rows 20 and 41. \p rect positions the block; `f.bounds()`
/// reproduces legacy exactly on either panel.
void segment_digits(OledFrame& f, gfx::oled::Rect rect, std::span<const uint8_t, 4> segs);

/// A two-channel level meter across \p rect: \p l in the top half, \p r in the bottom.
///
/// **New; legacy has no OLED VU.** `View::potentiallyRenderVUMeter` / `renderVUMeter`
/// (gui/views/view.cpp:1753-1860) paint the *grid* sidebar in RGB and there is no `renderVU` of
/// any kind under `hid/display`, so there are no pixels to match. Spec B section 6 lists the
/// widget, so it is drawn here on the only part of legacy that carries over: the eight-step
/// quantisation of `getMaxYDisplayForVUMeter` (:1813-1846, one step per grid row) becomes eight
/// segments per channel, lit left to right, a segment's width being `rect.w / 8` less a
/// one-pixel gap. Golden-tested only; there is nothing to compare against.
///
/// \p l and \p r are 0..255 levels, already scaled by the caller -- the dBFS curve legacy applies
/// reads `AudioEngine::approxRMSLevel`, which is model data this layer cannot see.
void vu(OledFrame& f, gfx::oled::Rect rect, uint8_t l, uint8_t r);

/// An ADSR outline across \p rect: a ramp up to the peak over \p a, down to \p s over \p d, held
/// flat, then down to zero over \p r.
///
/// **New.** Spec B section 6 asks for "ADSR outline" and points at `gui/menu_item/envelope/`,
/// which holds only menu items -- the closest legacy drawings are `Number::drawAttack` and
/// `drawRelease` (`envelope_stage` in meters.h), single stages in a 19x11 slot with no combined
/// renderer anywhere. So there is no pixel parity to keep here either, only a golden.
///
/// \p a, \p d and \p r are normalised stage lengths that each claim up to a quarter of \p rect's
/// width, the sustain segment taking whatever is left; \p s is the sustain level, 0 at the bottom
/// of \p rect and 1 at the top. All four are clamped to [0, 1].
void envelope(OledFrame& f, gfx::oled::Rect rect, float a, float d, float s, float r);

/// `Canvas::draw_graphic_multi_line` (gfx/oled/canvas.cpp), reached through the frame so a scene
/// never holds a `Canvas`.
///
/// Only \p rect's **corner** is read; its `w` and `h` are ignored, as `meters.h`'s slot height is.
/// \p w, \p h and \p bytes_tall describe the *source* bitmap, which is why spec B section 6 spells
/// them as separate arguments: `drawGraphicMultiLine` takes a position plus source dimensions, and
/// a graphic can legitimately be blitted at less than its full height (\p h below \p bytes_tall
/// times 8 crops it). Passing the source size as `rect.w`/`rect.h` too is harmless and reads well.
///
/// Whole or not at all against the frame's clip, like every blit (frame.h).
void graphic(OledFrame& f, gfx::oled::Rect rect, const uint8_t* data, int32_t w, int32_t h, int32_t bytes_tall,
             bool reversed = false);

/// `Canvas::draw_icon`, on the same terms: \p at is the icon's top-left in frame coordinates.
void icon(OledFrame& f, gfx::oled::Point at, const gfx::oled::Icon& i, bool reversed = false);

} // namespace deluge::ui::oled
