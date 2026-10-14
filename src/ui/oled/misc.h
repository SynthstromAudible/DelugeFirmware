#pragma once
#include "gfx/oled/geometry.h"
#include "gfx/oled/icon.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::ui::oled {

/// @brief Everything `output_name` draws, resolved by the scene.
///
/// Deliberately not an `Output*`: this keeps model types out of the library.
/// `View::displayOutputName` already does exactly this flattening before it calls
/// `drawOutputNameFromDetails`.
struct OutputNameData {
	/// @brief The output-type label, e.g. "Synth", "Kit", "MIDI".
	///
	/// @note Mirrors legacy's `getOutputTypeName(outputType, channel)`.
	std::string_view type_text;
	/// @brief The output's own name, when it has one.
	///
	/// Also the place a scene puts the strings legacy substituted for a slot: "Lower"/"Upper"
	/// for an MPE zone, "Transpose", "1 and 2" for both CV outputs. Pass those with
	/// `name_empty = false`.
	std::string_view name;
	/// @brief Slot number and sub-slot letter used when `name_empty` is set: the decimal slot,
	///        plus `'A' + sub_slot` when `sub_slot` is not -1.
	///
	/// @note Legacy's callers pass the channel already incremented, and so must this one.
	int32_t slot;
	int32_t sub_slot;
	/// @brief Selects between the two rendering branches: draw `name`, or fall back to the slot.
	///
	/// Not derivable from `name` alone -- legacy's `isNameEmpty` is passed separately from the
	/// name string itself, since the substitute strings in `name` (see above) arrive with this
	/// false.
	bool name_empty;
	/// @brief Unused: carried for parity with legacy's `editedByUser`, but nothing in this
	///        library reads it.
	///
	/// @note Draws nothing. The only renderer that ever consulted this was the 7-segment
	///       display, which no longer exists. Kept so a scene's data shape does not have to
	///       change later.
	bool edited;
};

/// @brief The pixel half of legacy's `View::drawOutputNameFromDetails`: the output type centred
///        in the small face near the top, and the output's name -- or its slot -- centred in
///        the title face below it.
///
/// Both y positions are the legacy `#if OLED_MAIN_HEIGHT_PIXELS == 64` pair, now selected from
/// `f.style.menu_base_y` exactly as `list`'s base-y is: `kOutputTypeY48`/`kOutputTypeY64`
/// (3 / 12) and `kOutputNameY48`/`kOutputNameY64` (17 / 30), all relative to @p rect's top
/// because the frame's origin already carries `OLED_MAIN_TOPMOST_PIXEL`.
///
/// The name is centred when it fits and left-aligned when it does not.
/// @note Legacy also armed a side-scroller in the left-aligned case; that is `scroll_text`'s job
///       now, and a scene that wants a long name to scroll composes the two -- this widget owns
///       no state and cannot. Likewise the clip's "Section N" second line is the scene's, being
///       model data this struct does not carry.
/// @note Clears nothing -- clearing the canvas is the shell's business in immediate mode.
/// @param f the OLED frame to draw into
/// @param rect the area to draw within
/// @param data the resolved output-name data to draw
void output_name(OledFrame& f, gfx::oled::Rect rect, const OutputNameData& data);

/// @brief Renders legacy's emulated seven-segment display: four seven-segment digits drawn at
///        33 px pitch, each segment a hand-tuned run of `invertArea` calls.
///
/// @note Ports legacy `OLED::renderEmulated7Seg`.
///
/// @warning @p segs is one bit-per-segment byte per digit, in the Deluge's own order, which is
///          **not** the textbook `gfedcba`: bit 0 is the *middle* bar, 1 and 2 the upper-left
///          and lower-left uprights, 3 the bottom bar, 4 and 5 the lower-right and upper-right
///          uprights, 6 the top bar, and 7 the decimal point. A caller holding textbook codes
///          will draw the wrong glyphs.
///
/// @note A full-panel renderer with canvas-absolute constants: legacy laid it out against
///       `main` with no `OLED_MAIN_TOPMOST_PIXEL` term anywhere, so the constants are converted
///       here by subtracting the frame's origin -- the same convention `decimal_value` uses for
///       its rows 20 and 41. @p rect positions the block; `f.bounds()` reproduces legacy exactly
///       on either panel.
/// @param f the OLED frame to draw into
/// @param rect the area to position the block within
/// @param segs one segment byte per digit, four digits
void segment_digits(OledFrame& f, gfx::oled::Rect rect, std::span<const uint8_t, 4> segs);

/// @brief A two-channel level meter across @p rect: @p l in the top half, @p r in the bottom.
///
/// @note New; legacy has no OLED VU meter to match pixels against -- legacy's
///       `potentiallyRenderVUMeter`/`renderVUMeter` paint the *grid* sidebar in RGB, not this
///       display. The eight-step quantisation `getMaxYDisplayForVUMeter` uses (one step per
///       grid row) becomes eight segments per channel here, lit left to right, a segment's
///       width being `rect.w / 8` less a one-pixel gap -- so the eight steps show as eight
///       blocks, not one bar.
/// @param f the OLED frame to draw into
/// @param rect the meter's bounds; split into top/bottom halves for the two channels
/// @param l left-channel level, 0..255, already scaled by the caller
/// @param r right-channel level, 0..255, already scaled by the caller
void vu(OledFrame& f, gfx::oled::Rect rect, uint8_t l, uint8_t r);

/// @brief An ADSR outline across @p rect: a ramp up to the peak over @p a, down to @p s over
///        @p d, held flat, then down to zero over @p r.
///
/// @note New; legacy has no combined ADSR renderer to match -- the closest legacy drawings are
///       `Number::drawAttack`/`drawRelease` (single stages in a 19x11 slot), with no combined
///       renderer anywhere.
/// @param f the OLED frame to draw into
/// @param rect the outline's bounds
/// @param a attack stage length, normalised [0, 1]
/// @param d decay stage length, normalised [0, 1]
/// @param s sustain level, normalised [0, 1] (0 at the bottom of @p rect, 1 at the top)
/// @param r release stage length, normalised [0, 1]
void envelope(OledFrame& f, gfx::oled::Rect rect, float a, float d, float s, float r);

/// @brief Draws a multi-line graphic bitmap, reached through the frame so a scene never holds a
///        `Canvas` directly.
///
/// Only @p rect's **corner** is read; its `w` and `h` are ignored. @p w, @p h and @p bytes_tall
/// describe the *source* bitmap: a position plus source dimensions, and a graphic can
/// legitimately be blitted at less than its full height (@p h below @p bytes_tall times 8 crops
/// it).
/// @note Whole or not at all against the frame's clip, like every blit.
/// @param f the OLED frame to draw into
/// @param rect only the corner is used as the draw position
/// @param data the source bitmap data
/// @param w source bitmap width in pixels
/// @param h height in pixels to draw (may be less than the source's full height)
/// @param bytes_tall source bitmap height in bytes (i.e. rows of 8 pixels)
/// @param reversed whether to invert the bitmap
void graphic(OledFrame& f, gfx::oled::Rect rect, const uint8_t* data, int32_t w, int32_t h, int32_t bytes_tall,
             bool reversed = false);

/// @brief Draws an icon, on the same terms as `graphic()`.
/// @param f the OLED frame to draw into
/// @param at the icon's top-left, in frame coordinates
/// @param i the icon to draw
/// @param reversed whether to invert the icon
void icon(OledFrame& f, gfx::oled::Point at, const gfx::oled::Icon& i, bool reversed = false);

} // namespace deluge::ui::oled
