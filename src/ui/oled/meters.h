#pragma once
#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"
#include <cstdint>

namespace deluge::ui::oled {

/// The horizontal-menu meters: `Number::drawPercent` / `drawKnob` / `drawBar` / `drawSlider` /
/// `drawLengthSlider` / `drawPan` / `drawHpf` / `drawLpf` / `drawAttack` / `drawRelease` /
/// `drawSidechainDucking` (gui/menu_item/number.cpp:107-427), one free function each.
///
/// **`slot` is the legacy `SlotPosition{start_x, start_y, width, height}` as a `Rect`**, in frame
/// coordinates: `slot.x` is `start_x`, `slot.y` is `start_y`, `slot.w` is `width`. Every routine
/// below lays out from those exactly as legacy did, so a slot at the horizontal menu's own
/// geometry (`Rect{base_x, base_y, 31, kHorizontalBoxHeight}`) reproduces legacy pixel for pixel.
/// Legacy's `start_y` was already panel-relative (the horizontal menu adds
/// `OLED_MAIN_TOPMOST_PIXEL` when it computes `base_y`), and the frame's origin now carries that
/// term, so the same slot on a 64-row panel draws the same pixels shifted up by `visible_top`.
/// `slot.h` is unused, as it was in legacy -- not one of the eleven routines reads
/// `SlotPosition::height`.
///
/// Where legacy derived a number from the menu item, this layer takes it as a parameter and the
/// arithmetic is unchanged: `normalize(getValue())` (number.cpp:27-31, the value's position in
/// `[min, max]`) becomes \p norm, and the routines that read the raw value instead --
/// `drawPercent`'s `getValue() * 2`, `drawPan`'s -25..25 and `drawSidechainDucking`'s value and
/// maximum -- take that integer.
///
/// `Number::drawHorizontalBar` (number.cpp:33-73) is deliberately NOT here: it is the *full
/// screen* renderer's bar, laid out against `OLED_MAIN_WIDTH_PIXELS` and margins rather than a
/// slot, and no `renderInHorizontalMenu` arm reaches it.

/// Which side of `drawHpf` (number.cpp:271-299) / `drawLpf` (:301-329) `filter_curve` draws.
enum class FilterKind : uint8_t { Lpf, Hpf };

/// Which of `drawAttack` (number.cpp:361-387) / `drawRelease` (:331-359) `envelope_stage` draws.
enum class StageKind : uint8_t { Attack, Release };

/// `Number::getRenderingStyle()`'s cases, as the dispatch tag `meter` takes. The names are spec
/// B's; `None`, `Enum` and `Toggle` all land on `renderInHorizontalMenu`'s `default:` arm.
enum class Hint : uint8_t {
	None,
	Percent,
	Knob,
	Bar,
	Slider,
	LengthSlider,
	Pan,
	Lpf,
	Hpf,
	Attack,
	Release,
	Ducking,
	Enum,
	Toggle,
};

/// `Number::drawPercent` (number.cpp:107-125): the number, then a `%` two pixels after it, the
/// pair centred across the slot (and then nudged one pixel right, as legacy's `+ 1` does).
///
/// \p value is the number to *print*, i.e. legacy's `getValue() * 2` (number.cpp:111) already
/// doubled -- the percent style stores 0..50 for 0..100%. `meter` below does that doubling.
///
/// The face is `f.style.row_font`; both shipped profiles set `Apple7`, whose metrics are exactly
/// the `kTextSpacingX` / `kTextSpacingY` pair legacy hard-coded, so the pixels are unchanged.
void percent(OledFrame& f, gfx::oled::Rect slot, int32_t value);

/// `Number::drawKnob` (number.cpp:127-154): the `knobArc` bitmap centred across the slot at
/// `slot.y - 1`, with a thick radial line from radius 3.5 to radius 9 at
/// `165 deg + 210 deg * norm`, about a centre at `(slot.x + slot.w / 2, slot.y + 10)`.
///
/// The arc is a bitmap because it was easier to make pixel-perfect that way (number.cpp:130-131);
/// `gfx::oled::icons::knobArc` is the gfx registry's name for the legacy `OLED::knobArcIcon`.
/// Angles grow clockwise on screen, y being downwards, so 165 deg starts just below the left of
/// the arc's gap and 375 deg (= 15 deg) ends just below its right.
void knob_arc(OledFrame& f, gfx::oled::Rect slot, float norm);

/// `Number::drawBar` (number.cpp:156-173): a 21x5 bar at `(slot.x + 5, slot.y + 2 + 2)` inside a
/// rectangle 2px larger on every side, filled from the left by inverting `norm * 21` columns.
///
/// The fill width truncates towards zero through a `uint8_t` exactly as legacy does, so
/// `norm == 0.5` inverts 10 columns, not 10.5 and not 11.
void unipolar_bar(OledFrame& f, gfx::oled::Rect slot, float norm);

/// `Number::drawPan` (number.cpp:229-269): `unipolar_bar`'s outline and bar, plus two notches
/// above and below the centre column and a full-height midline, filled outwards from the midline
/// in the direction of \p value.
///
/// \p value is the raw -25..25 pan amount. A non-zero magnitude under 3 is rounded up to 3
/// (number.cpp:258-261) so that "not centred" always shows, and the fill is
/// `abs_value / 25 * 10` columns wide plus one.
void pan(OledFrame& f, gfx::oled::Rect slot, int32_t value);

/// `Number::drawSlider` (number.cpp:175-200): a 23x11 track at `(slot.x + 4, slot.y + 1)` drawn
/// as dotted pairs every 3px, with a 2px-wide full-height thumb at `norm * (width - 2)`.
///
/// The two dots that would touch the thumb are skipped (number.cpp:192). Legacy's optional
/// `value` override (`normalize(value.value_or(getValue()))`, :185) is just "whatever number the
/// caller wants normalised", which is \p norm.
void slider(OledFrame& f, gfx::oled::Rect slot, float norm);

/// `Number::drawLengthSlider` (number.cpp:202-227): `slider`'s track, but filled from the left
/// with a 3px-tall bar up to the thumb, and the dots laid out from the *right*.
///
/// \p min_pos is legacy's `min_slider_pos`, which shifts the thumb's zero one pixel right
/// (number.cpp:213) -- the caller sets it for a length whose minimum is still a non-empty value.
void length_slider(OledFrame& f, gfx::oled::Rect slot, float norm, bool min_pos);

/// `Number::drawLpf` (number.cpp:301-329) and `Number::drawHpf` (:271-299): a 21x11 filter
/// response at `(slot.x + 5, slot.y + 1)` -- a 2px-thick corner slope 5px wide, the passband as
/// two solid rows, and the stopband as a dotted line along the top and (once the corner has moved
/// off the end) down the far edge.
///
/// The corner travels `lerp(start + 3, end - 5, norm)` for the low-pass and
/// `lerp(start, end - 5 - 4, norm)` for the high-pass; the two are not mirror images of each
/// other, and both are kept verbatim.
void filter_curve(OledFrame& f, gfx::oled::Rect slot, float norm, FilterKind kind);

/// `Number::drawAttack` (number.cpp:361-387) and `Number::drawRelease` (:331-359): a 19x11
/// envelope segment with a 5x4 solid block marking the moving end of the ramp and a dotted line
/// running from there to the far edge.
///
/// The two differ by more than their direction and are kept verbatim: attack starts at
/// `slot.x + 7` and `slot.y + 2` and travels to `end - 2`, release starts at `slot.x + 5` and
/// `slot.y + 2 - 1`, travels the full width from `start + 4`, and draws a lead-in along its top.
void envelope_stage(OledFrame& f, gfx::oled::Rect slot, float norm, StageKind kind);

/// `Number::drawSidechainDucking` (number.cpp:389-427): the sidechain envelope as a 23x11
/// wedge -- a vertical edge at the left, a diagonal to the plateau and the plateau itself --
/// centred across the slot, growing downwards for a positive \p value and upwards for a negative
/// one.
///
/// \p value and \p max are legacy's `getValue()` and `getMaxValue()`. The amount is
/// `abs(value) / 5000` when `max > 50` and `value / 50` otherwise (number.cpp:402) -- two
/// different parameter scales sharing one renderer -- so this widget cannot take a normalised
/// float; it needs both numbers. Note the `max > 50` arm takes the magnitude and the other does
/// not, which is legacy's own asymmetry.
void sidechain_ducking(OledFrame& f, gfx::oled::Rect slot, int32_t value, int32_t max);

/// `Number::renderInHorizontalMenu` (number.cpp:75-105): pick a meter by \p hint.
///
/// \p norm is `normalize(getValue())` and \p raw is `getValue()`; `Hint::Percent` doubles \p raw
/// (number.cpp:111) and `Hint::Pan` passes it through. `Hint::None`, `Hint::Enum` and
/// `Hint::Toggle` take the `default:` arm, which draws \p raw centred in the title face
/// (number.cpp:100-103) -- what a `Number` with no meter of its own does. The *enumeration's*
/// horizontal-slot rendering (`Enumeration::renderInHorizontalMenu`, enumeration.cpp:44-52) draws
/// a short option name, which is a string this signature does not carry, so it is not this
/// function's job.
///
/// \p raw_max is `getMaxValue()`, which only `Hint::Ducking` reads (number.cpp:402); it defaults
/// to 50, the value that selects legacy's `getValue() / 50.0f` arm.
void meter(OledFrame& f, gfx::oled::Rect slot, Hint hint, float norm, int32_t raw, int32_t raw_max = 50);

} // namespace deluge::ui::oled
