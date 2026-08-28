#pragma once
#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"
#include <cstdint>

namespace deluge::ui::oled {

/// @brief The horizontal-menu meters, one free function per legacy `Number::draw*` renderer
///        (`drawPercent`, `drawKnob`, `drawBar`, `drawSlider`, `drawLengthSlider`, `drawPan`,
///        `drawHpf`, `drawLpf`, `drawAttack`, `drawRelease`, `drawSidechainDucking`).
///
/// @note **Slot geometry.** `slot` is the legacy `SlotPosition{start_x, start_y, width, height}`
///       as a `Rect`, in frame coordinates: `slot.x` is `start_x`, `slot.y` is `start_y`, `slot.w`
///       is `width`. Every routine below lays out from those exactly as legacy did, so a slot at
///       the horizontal menu's own geometry reproduces legacy pixel for pixel. The frame's origin
///       now carries the panel-relative offset legacy folded into `start_y`, so the same slot on
///       a taller panel draws the same pixels shifted by the visible area's offset. `slot.h` is
///       unused, as it was in legacy -- none of these routines read `SlotPosition::height`.
///
/// Where legacy derived a number from the menu item, this layer takes it as a parameter and the
/// arithmetic is unchanged: the value's normalized position in `[min, max]` becomes \p norm, and
/// the routines that read the raw value instead -- `percent`'s doubled value, `pan`'s -25..25 and
/// `sidechain_ducking`'s value and maximum -- take that integer.
///
/// @note `Number::drawHorizontalBar` is deliberately NOT ported here: it is the *full screen*
///       renderer's bar, laid out against the panel width and margins rather than a slot, and no
///       horizontal-menu dispatch reaches it.

/// @brief Which side `filter_curve` draws: `Number::drawHpf` or `Number::drawLpf`.
enum class FilterKind : uint8_t { Lpf, Hpf };

/// @brief Which stage `envelope_stage` draws: `Number::drawAttack` or `Number::drawRelease`.
enum class StageKind : uint8_t { Attack, Release };

/// @brief Dispatch tag `meter` takes, mirroring `Number::getRenderingStyle()`'s cases.
///
/// @note `None`, `Enum` and `Toggle` all land on `meter`'s `default:` arm (the raw number).
///       `Enum`/`Toggle` slots are actually drawn by `columns` via `enum_value` / `toggle`, not by
///       `meter` -- they are listed here so a slot's style can be named in one enum, but their
///       real slot renderings take a string, which this signature does not carry.
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

/// @brief Draws @p value as a percentage: the number, then `%` two pixels after it, the pair
///        centred across the slot (nudged one pixel right).
///
/// @param f     Frame to render into.
/// @param slot  Slot geometry to lay out in.
/// @param value The number to *print* -- already doubled (the percent style stores 0..50 for
///              0..100%). `meter` does that doubling before calling this.
///
/// @note Ports legacy `Number::drawPercent`.
void percent(OledFrame& f, gfx::oled::Rect slot, int32_t value);

/// @brief Draws a knob: the `knobArc` bitmap centred across the slot at `slot.y - 1`, with a
///        thick radial line from radius 3.5 to radius 9 at `165 deg + 210 deg * norm`, about a
///        centre at `(slot.x + slot.w / 2, slot.y + 10)`.
///
/// Angles grow clockwise on screen (y downwards), so 165 deg starts just below the left of the
/// arc's gap and 375 deg (= 15 deg) ends just below its right. `gfx::oled::icons::knobArc` is the
/// gfx registry's name for the arc bitmap, used because it's easier to keep pixel-perfect than a
/// drawn arc.
///
/// @param f    Frame to render into.
/// @param slot Slot geometry to lay out in.
/// @param norm Normalized value in `[0, 1]`.
///
/// @note Ports legacy `Number::drawKnob`.
void knob_arc(OledFrame& f, gfx::oled::Rect slot, float norm);

/// @brief Draws a unipolar bar: a 21x5 bar at `(slot.x + 5, slot.y + 4)` inside an outline 2px
///        larger on every side, filled from the left by inverting `norm * 21` columns.
///
/// @note The fill width truncates towards zero, matching legacy -- `norm == 0.5` inverts 10
///       columns, not 10.5 or 11.
///
/// @param f    Frame to render into.
/// @param slot Slot geometry to lay out in.
/// @param norm Normalized value in `[0, 1]`.
///
/// @note Ports legacy `Number::drawBar`.
void unipolar_bar(OledFrame& f, gfx::oled::Rect slot, float norm);

/// @brief Draws a pan indicator: `unipolar_bar`'s outline and bar, plus two notches above and
///        below the centre column and a full-height midline, filled outwards from the midline in
///        the direction of @p value.
///
/// @param f     Frame to render into.
/// @param slot  Slot geometry to lay out in.
/// @param value Raw -25..25 pan amount.
///
/// @note A non-zero magnitude under 3 is rounded up to 3 so that "not centred" always shows; the
///       fill is `abs_value / 25 * 10` columns wide plus one.
///
/// @note Ports legacy `Number::drawPan`.
void pan(OledFrame& f, gfx::oled::Rect slot, int32_t value);

/// @brief Draws a slider: a 23x11 track at `(slot.x + 4, slot.y + 1)` drawn as dotted pairs every
///        3px, with a 2px-wide full-height thumb at `norm * (width - 2)`.
///
/// @note The two dots that would touch the thumb are skipped.
///
/// @param f    Frame to render into.
/// @param slot Slot geometry to lay out in.
/// @param norm Normalized value in `[0, 1]`.
///
/// @note Ports legacy `Number::drawSlider`.
void slider(OledFrame& f, gfx::oled::Rect slot, float norm);

/// @brief Draws a length slider: `slider`'s track, filled from the left with a 3px-tall bar up to
///        the thumb, with the dots laid out from the *right*.
///
/// @param f       Frame to render into.
/// @param slot    Slot geometry to lay out in.
/// @param norm    Normalized value in `[0, 1]`.
/// @param min_pos Shifts the thumb's zero one pixel right, moving the thumb, the fill's right end
///                and the dotted run's left bound at the bottom of the range (they coincide from
///                about norm 0.5 up).
///
/// @note Defaults to `true`: every shipped caller of the legacy equivalent used that value (its
///       own default was a bool copy-initialised from an integer literal, almost certainly a
///       typo) -- kept as `true` rather than "corrected", since that's the behaviour every
///       shipped LENGTH_SLIDER slot has always drawn.
///
/// @note Ports legacy `Number::drawLengthSlider`.
void length_slider(OledFrame& f, gfx::oled::Rect slot, float norm, bool min_pos = true);

/// @brief Draws a filter response curve: a 21x11 shape at `(slot.x + 5, slot.y + 1)` -- a
///        2px-thick corner slope 5px wide, the passband as two solid rows, and the stopband as a
///        dotted line along the top and (once the corner has moved off the end) down the far
///        edge.
///
/// @param f    Frame to render into.
/// @param slot Slot geometry to lay out in.
/// @param norm Normalized value in `[0, 1]`.
/// @param kind Which side of the corner (low-pass or high-pass) to draw.
///
/// @note Low-pass and high-pass are not mirror images of each other: the corner travels
///       `lerp(start + 3, end - 5, norm)` for the low-pass and `lerp(start, end - 9, norm)` for
///       the high-pass.
///
/// @note Ports legacy `Number::drawLpf` and `Number::drawHpf`.
void filter_curve(OledFrame& f, gfx::oled::Rect slot, float norm, FilterKind kind);

/// @brief Draws an envelope stage: a 19x11 segment with a 5x4 solid block marking the moving end
///        of the ramp and a dotted line running from there to the far edge.
///
/// @param f    Frame to render into.
/// @param slot Slot geometry to lay out in.
/// @param norm Normalized value in `[0, 1]`.
/// @param kind Which stage (attack or release) to draw.
///
/// @note Attack and release differ by more than direction: attack starts at `slot.x + 7`,
///       `slot.y + 2` and travels to `end - 2`; release starts at `slot.x + 5`, `slot.y + 1`,
///       travels the full width from `start + 4`, and draws a lead-in along its top.
///
/// @note Ports legacy `Number::drawAttack` and `Number::drawRelease`.
void envelope_stage(OledFrame& f, gfx::oled::Rect slot, float norm, StageKind kind);

/// @brief Draws the sidechain ducking envelope as a 23x11 wedge -- a vertical edge at the left, a
///        diagonal to the plateau and the plateau itself -- centred across the slot, growing
///        downwards for a positive @p value and upwards for a negative one.
///
/// @param f     Frame to render into.
/// @param slot  Slot geometry to lay out in.
/// @param value Raw ducking amount.
/// @param max   Raw maximum, selecting which of two value scales @p value is read on.
///
/// @note Takes raw @p value and @p max rather than a normalized float because two different
///       parameter scales share this one renderer: the fill amount is `abs(value) / 5000` when
///       `max > 50` and `value / 50` otherwise -- and only the first arm takes the magnitude. This
///       asymmetry is preserved as-is.
///
/// @note Ports legacy `Number::drawSidechainDucking`.
void sidechain_ducking(OledFrame& f, gfx::oled::Rect slot, int32_t value, int32_t max);

/// @brief Dispatches to the right meter renderer for @p hint.
///
/// @param f       Frame to render into.
/// @param slot    Slot geometry to lay out in.
/// @param hint    Which meter style to draw; see Hint.
/// @param norm    Normalized value in `[0, 1]`.
/// @param raw     Raw value; `Hint::Percent` doubles it, `Hint::Pan` passes it through unchanged.
/// @param raw_max Raw maximum; read only by `Hint::Ducking`, where it selects between the
///                renderer's two value scales.
///
/// @note `Hint::None`, `Hint::Enum` and `Hint::Toggle` all take the `default:` arm, which draws
///       @p raw centred in the title face -- what a value with no meter of its own does. An
///       enum's horizontal-slot rendering draws a short option name instead, which is a string
///       this signature does not carry, so that rendering is not this function's job.
///
/// @warning **`Hint::Ducking` callers must pass `raw_max`.** The default of 50 exists only so the
///          call still compiles without it; omitting a real maximum above 50 silently selects the
///          wrong one of the renderer's two value scales and draws a wrong-but-plausible wedge
///          with no diagnostic. Every other hint ignores this parameter.
///
/// @note Ports legacy `Number::renderInHorizontalMenu`.
void meter(OledFrame& f, gfx::oled::Rect slot, Hint hint, float norm, int32_t raw, int32_t raw_max = 50);

} // namespace deluge::ui::oled
