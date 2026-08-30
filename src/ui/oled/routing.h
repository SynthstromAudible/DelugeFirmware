#pragma once

/// @brief Two widgets for showing a *routing*: what modulates what, and which of a small set of
///        modes that routing is in.
///
/// Both are ports of `deluge::gui::menu_item::PatchCableStrength::renderOLED`
/// (`gui/menu_item/patch_cable_strength.cpp:74-190`), which is the only screen in the firmware that
/// draws a signal path rather than a value. It is worth a widget rather than a one-off because the
/// thing it does -- carrying "A modulates B" pre-verbally, so the eye reads the routing before the
/// user parses a string -- is the whole job of that page, and because `src/scenes` may not call an
/// `OledFrame` drawing primitive at all (`scripts/check_layer_rules.py`'s
/// `scenes-no-frame-primitives`), so a scene that wants a diagram has to get one from here.
///
/// @note **Frame-local, laid out against the caller's rect.** Legacy computes every coordinate here
///       against the panel, but every one of them is `OLED_MAIN_TOPMOST_PIXEL + n` or a plain
///       offset from the left edge, and a frame's origin already carries `visible_top` -- so the
///       `n`s are what survive and both widgets are geometry-independent. There is no 48/64
///       constant pair here and there must not be one: a caller wanting legacy's absolute canvas
///       row passes a rect derived from `ui/core/metrics.h`, as `midi_command.cpp` does.

#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::ui::oled {

/// @brief How many stages `routing_chain` will draw. Legacy has exactly the two shapes.
///
/// A patch cable is either "source -> param" (`ParamDescriptor::isJustAParam()`) or
/// "range source -> source -> param" (a cable modulating another cable's depth), and legacy's
/// `renderOLED` is a two-armed `if` on precisely that
/// (`patch_cable_strength.cpp:94-117`). More stages have no legacy geometry to port, so a longer
/// span draws its first `kMaxRoutingStages`.
inline constexpr size_t kMaxRoutingStages = 3;

/// @brief Draw a signal path down the left of @p rect: each stage's short name, joined by legacy's
///        own elbow-and-arrowhead rules.
///
/// Ports `PatchCableStrength::renderOLED`'s first block (`patch_cable_strength.cpp:79-123`),
/// coordinate for coordinate, including the two things about it that look like mistakes and are
/// not ours to fix:
///
///   * the **two shapes use different vertical pitches** -- `kTextSpacingY` (9) for the two-stage
///     form and a literal 8 for the three-stage one (`:86`), so the three-stage diagram is tighter
///     than three rows of the same list would be;
///   * in the three-stage form the **elbow lands below the middle stage's label**: the vertical
///     drop from stage 0 runs to `y + (ySpacing << 1)` (`:96-97`) while the label is drawn at `y - 3`
///     after a `+= ySpacing - 1` (`:100-104`). Transcribed as-is.
///
/// Nothing is claimed and nothing is cleared: this is a draw-only widget over whatever the caller
/// has already put on the frame, exactly as legacy's block is.
///
/// @param f      Frame to draw into; input is never read.
/// @param rect   Where the diagram's top-left corner sits, in frame-local coordinates. Only
///               `rect.x` and `rect.y` are used for placement; `rect.w` clips the stage labels.
/// @param stages The path, source first, destination last. Fewer than two draws nothing; more than
///               `kMaxRoutingStages` draws the first three.
void routing_chain(OledFrame& f, gfx::oled::Rect rect, std::span<const std::string_view> stages);

/// @brief The bracketed multi-slot switch: N labelled slots side by side, at most one inverted.
///
/// Ports `PatchCableStrength::renderOLED`'s polarity switcher
/// (`patch_cable_strength.cpp:125-157`) -- the BI / UNI box -- as an N-slot widget, because
/// nothing about it is specific to polarity: it is "here are the modes, this is the one you are
/// in", and the no-polarity arm (`:151-156`) is the same box with one full-width label and nothing
/// inverted.
///
/// **The bracket is drawn in the two rows *above* @p area**, and one column outside it on each
/// side. Legacy's frame is three lines and two corner pixels -- verticals at `startX - 2` and
/// `endX + 2` spanning the slot band, a horizontal at `startY - 2` across it, and single pixels at
/// `(startX - 1, startY - 1)` / `(endX + 1, startY - 1)` that round the two top corners
/// (`:135-139`). So the widget inks rows `area.y - 2 .. area.y + area.h` and columns
/// `area.x - 2 .. area.x + sum(widths) + 1`; a caller must leave that room.
///
/// @note **The slots' text boxes and their inversion boxes are offset by one column**, for every
///       slot but the first. Legacy draws slot *i*'s label at `startX + sum(widths[0..i)) + 1`
///       (`:144`, the `+ 1` between BI's 25 columns and UNI's) but inverts from
///       `startX + sum(widths[0..i))` (`:147-149`). Reproduced rather than tidied, for the reason
///       `option_window` reproduces its own label/highlight mismatch: correcting it would move
///       pixels off legacy's.
///
/// @param f        Frame to draw into; input is never read.
/// @param area     The slot band, in frame-local coordinates: `area.x`/`area.y` are legacy's
///                 `startX`/`startY` and `area.h` its `kTextSpacingY`. `area.w` is ignored -- the
///                 band's width is `sum(widths)`, so that the slots and the bracket cannot disagree.
/// @param labels   One label per slot, in left-to-right order.
/// @param widths   Each slot's width in columns; must be the same length as @p labels.
/// @param selected Which slot is inverted, or **negative for none** -- the no-polarity arm.
void segmented_switch(OledFrame& f, gfx::oled::Rect area, std::span<const std::string_view> labels,
                      std::span<const int32_t> widths, int32_t selected);

} // namespace deluge::ui::oled
