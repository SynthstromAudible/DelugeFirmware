#pragma once
#include "gfx/oled/geometry.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

/// @brief Legacy's `PopupType`, reduced to the three that differ in *pixels*.
///
///  - `Framed` is `setupPopup` with anything but `PopupType::NOTIFICATION`: cleared interior plus
///    a rounded border.
///  - `Notification` is `PopupType::NOTIFICATION`: the same cleared interior and **no border at
///    all**.
///  - `Permanent` is `drawPermanentPopupLookingText`, which draws a **square** rectangle rather
///    than a rounded one and centres its box with a different (mirrored) arithmetic; see
///    `popup_rect_for`.
///
/// Legacy's remaining types (`GENERAL`, `QUANTIZE`, ...) select timeout and priority policy in the
/// display driver, not geometry, and none of that is a widget's business.
enum class PopupStyle : uint8_t { Framed, Notification, Permanent };

/// @brief Where a popup box holding \p content goes: centred on the panel, `kPopupMargin` bigger
///        on both axes (one margin's worth on *each* side).
///
/// @note **The returned rect is legacy's `{minX, minY, width, height}` pair, and the border
///       `popup_frame` draws from it spans `w + 1` columns and `h + 1` rows.** Legacy treats
///       `minX + width` as an *inclusive* maximum when clearing and drawing the border, so a
///       popup legacy calls 42 wide inks 43 columns. Kept rather than "corrected", because
///       correcting it would move every popup border by a pixel; `popup_frame` is the only
///       consumer and does the +1 in one place.
///
/// @note **Centred on the whole canvas, not on the frame** -- the returned rect is nonetheless in
///       \p f's local coordinates. Legacy centres against the *whole* panel -- on the shipped
///       48-row board that includes the five rows the faceplate hides, so a legacy popup sits
///       above the visible centre. This reproduces that exactly by centring on
///       `f.canvas.geometry()` and converting to frame coordinates by subtracting `f.origin`.
///
///       So for a 30x9 content on the shipped 48-row panel the box is canvas rows 13..34
///       (`(48 - 21) / 2 = 13`) -- not the row centring against the 43 *visible* rows would give,
///       which would not reproduce the device. At `Geometry{128, 64, 0}` the two rules coincide,
///       `origin` is zero, and the arithmetic is legacy's unchanged.
///
/// @note A popup is a panel-global object in legacy -- there is no sub-region popup anywhere -- so
///       the centring deliberately ignores which region \p f covers. A frame whose origin is not
///       the panel's corner still gets the panel's popup, drawn where legacy would draw it and
///       clipped to the frame like anything else.
///
/// @param f       Target frame; used for its canvas geometry and origin, not its own bounds.
/// @param content Size of the content the popup box must fit.
/// @param style   `Permanent` uses its own centring -- `minX = (w - content.w - margin) >> 1` with
///                `maxX = w - minX` -- which mirrors rather than adds, and so lands one pixel
///                wider than the `Framed` rule whenever `w - content.w - margin` is odd. Defaults
///                to `Framed` so the two-argument call shape still compiles.
/// @return The popup box, in \p f's local coordinates.
gfx::oled::Rect popup_rect_for(OledFrame& f, gfx::oled::Size content, PopupStyle style = PopupStyle::Framed);

/// @brief Clear \p rect's interior and draw its border, per \p style.
///
/// @note Legacy's `Permanent` path does *not* clear -- it drew straight onto an already-cleared
///       canvas. This clears for every style instead, which is identical on the cleared canvas
///       legacy always had.
///
/// @param f     Target frame.
/// @param rect  The popup box; see `popup_rect_for` for the `w + 1`/`h + 1` note.
/// @param style Border style; see `PopupStyle`.
void popup_frame(OledFrame& f, gfx::oled::Rect rect, PopupStyle style);

/// @brief Ports legacy `OLED::popupText` bar its timers: wrap \p text, size a box to the widest
///        line, frame it, and centre the lines inside.
///
/// Layout, all against the *canvas* extent rather than the frame's (see `popup_rect_for`): wrap
/// at `panel width - kPopupMargin` in `f.style.row_font` (both shipped profiles set `Apple7`,
/// whose metrics are exactly the `kTextSpacingX`/`kTextSpacingY` pair legacy hard-coded); box
/// `= {longest line, count * 9} + kPopupMargin`; first line at
/// `(panel height - text height) >> 1`, clamped up to 0; each line at
/// `(panel width - its width) >> 1`. The line widths are `wrap_lines`' own, not `measure`'s -- they
/// differ by the trailing inter-character spacing that `breakStringIntoLines` subtracts off.
///
/// @note `PopupStyle::Permanent` follows `drawPermanentPopupLookingText` instead, which is the
///       same routine with a square border, the mirrored centring described on `popup_rect_for`,
///       and no "line fell off the bottom" check.
///
/// Centres on the panel, in \p f's coordinates; see `popup_rect_for`. Draws no timer and sets no
/// state: how long a popup stays up is the shell's business, not a widget's.
///
/// @param f     Target frame.
/// @param text  Text to wrap and display.
/// @param style Border style; see `PopupStyle`.
void popup_text(OledFrame& f, std::string_view text, PopupStyle style = PopupStyle::Framed);

/// @brief Ports legacy `updateWorkingAnimation`: the 18x10 box of sliding bars the Deluge shows in
///        the top-right corner while it loads or saves.
///
/// @note \p word is legacy's own direction switch and **nothing is printed**: only
///       `loading = (word == "Loading")` is read, and the animation runs right-to-left for
///       "Loading" and left-to-right for anything else ("Saving"). There is no word drawn on
///       screen and no row of dots anywhere in the routine.
///
/// @note Placed hard against \p rect's top-right corner, as legacy places it against the panel's:
///       the box's last column lands on the panel's last column, with no gap, and its top is two
///       rows below the frame's origin. It is the one popup legacy does *not* centre; with
///       `rect == f.bounds()` these are legacy's pixels exactly, and it needs no canvas-absolute
///       conversion (unlike the other popups here).
///
/// @note \p blink is the animation's clock. `period_ms` doubles as "armed" (the same idiom
///       `decimal_value` uses): a `Blink` not already holding `kWorkingStepMs` is (re)anchored at
///       `f.now_ms()`, so zeroing it restarts the animation. The step is then derived from the
///       elapsed time -- `kWorkingPauseMs` before the sequence starts, `kWorkingStepMs` per step,
///       `kWorkingPauseMs` again on the final frame of each cycle -- and `request_repaint_after`
///       is armed for the remainder of the current step.
///
/// @note Legacy accumulated this animation on a persistent canvas: most frames repaint only the
///       top few rows and leave the rest standing. An immediate-mode frame has no history, so
///       this replays the frames that produced it -- the initialiser, then (for any cycle after
///       the first) the full-interior repaint that ends a cycle, then every step of the current
///       cycle -- landing on the same pixels legacy's canvas held.
///
/// @param f     Target frame.
/// @param rect  Region to anchor the animation's top-right corner against.
/// @param word  Legacy's direction switch: `"Loading"` runs right-to-left, anything else
///              left-to-right. Nothing is printed.
/// @param blink Animation clock, persisted by the caller across frames.
void working(OledFrame& f, gfx::oled::Rect rect, std::string_view word, Blink& blink);

/// @brief A progress bar filling \p rect left to right at \p num / \p den.
///
/// @note Legacy has no progress bar as such. The closest legacy pixels are
///       `Number::drawHorizontalBar`, the full-screen menu bar (out of scope for `meters.h`, since
///       no horizontal-menu slot reaches it), and this is that routine with `min_value = 0`,
///       `value = num`, `max_value = den`:
///
///  - the outline is exactly \p rect;
///  - a zero numerator draws the outline and nothing else;
///  - otherwise the fill covers `num * rect.w / den` columns from the left, across
///    `rect.y + 1 .. rect.bottom() - 2`, and a non-zero numerator too small to fill one column
///    still inks a single column (legacy's "draw vertical line" arm at zero fill width).
///
/// @note Unlike `popup_rect_for`'s rect, \p rect here is the outline's true pixel extent: nothing
///       in legacy computes this box from a `width`, so there is no off-by-one to preserve.
///
/// @param f    Target frame.
/// @param rect The bar's outline, its true pixel extent.
/// @param num  Numerator; clamped to `[0, den]`.
/// @param den  Denominator; zero or less draws the empty outline.
void progress(OledFrame& f, gfx::oled::Rect rect, int32_t num, int32_t den);

} // namespace deluge::ui::oled
