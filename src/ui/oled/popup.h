#pragma once
#include "gfx/oled/geometry.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

/// Legacy's `PopupType` (hid/display/oled.h), reduced to the three that differ in *pixels*.
///
///  - `Framed` is `setupPopup` with anything but `PopupType::NOTIFICATION`: cleared interior plus
///    a rounded border (hid/display/oled.cpp:132-135).
///  - `Notification` is `PopupType::NOTIFICATION`: the same cleared interior and **no border at
///    all** -- the `if (type != PopupType::NOTIFICATION)` at :132 is the whole difference.
///  - `Permanent` is `drawPermanentPopupLookingText` (:570-601), which draws a **square**
///    `drawRectangle` (:588) rather than a rounded one and centres its box with a different
///    (mirrored) arithmetic; see `popup_rect_for`.
///
/// Legacy's remaining types (`GENERAL`, `QUANTIZE`, ...) select timeout and priority policy in the
/// display driver, not geometry, and none of that is a widget's business.
enum class PopupStyle : uint8_t { Framed, Notification, Permanent };

/// Where a popup box holding \p content goes: centred on \p f, `kPopupMargin` bigger on both
/// axes (legacy's `doubleMargin`, oled.cpp:610-611 -- one margin's worth on *each* side, despite
/// the name).
///
/// **The returned rect is legacy's `{minX, minY, width, height}` pair, and the border
/// `popup_frame` draws from it spans `w + 1` columns and `h + 1` rows.** `setupPopup` computes
/// `popupMaxX = popupMinX + width` and hands that to `clearAreaExact` / `drawRectangleRounded`
/// as an *inclusive* maximum (oled.cpp:126-135), so a popup legacy calls 42 wide inks 43
/// columns. Kept rather than "corrected", because correcting it would move every popup border by
/// a pixel; `popup_frame` is the only consumer and does the +1 in one place.
///
/// **Centred on `f.bounds()`, not on the panel.** Legacy divides `OLED_MAIN_HEIGHT_PIXELS`
/// (oled.cpp:127-129), which on the shipped 48-row panel includes the five rows the faceplate
/// hides, so its popups sit two or three rows above the visible centre. A frame has no access to
/// those rows by construction (frame.h: "no widget knows about the bezel"), so the box is centred
/// on the frame instead. On a 64-row flush panel the two rules agree exactly; on the 48-row panel
/// the port's popup sits `visible_top / 2` rows lower than legacy's. This is the one deliberate
/// pixel deviation in this file.
///
/// @param style `Permanent` uses `drawPermanentPopupLookingText`'s own centring --
///        `minX = (w - content.w - margin) >> 1` with `maxX = w - minX` (oled.cpp:582-586) --
///        which mirrors rather than adds, and so lands one pixel wider than the `Framed` rule
///        whenever `w - content.w - margin` is odd. Defaults to `Framed` so the two-argument
///        call shape still compiles.
gfx::oled::Rect popup_rect_for(OledFrame& f, gfx::oled::Size content, PopupStyle style = PopupStyle::Framed);

/// Clear \p rect's interior and draw its border, per \p style. `rect` is `popup_rect_for`'s --
/// see the `w + 1` note there.
///
/// The clear is `clearAreaExact(minX, minY, maxX, maxY)` (oled.cpp:131). Legacy's `Permanent`
/// path does *not* clear (it drew straight onto a `clearMainImage`d canvas, oled.cpp:588); this
/// clears for every style, which is what spec B section 6 asks of `popup_frame` ("border +
/// cleared interior") and is identical on the cleared canvas legacy always had.
void popup_frame(OledFrame& f, gfx::oled::Rect rect, PopupStyle style);

/// The whole of `OLED::popupText` (oled.cpp:605-643) bar its timers: wrap \p text, size a box to
/// the widest line, frame it, and centre the lines inside.
///
/// Layout, all legacy's: wrap at `f.bounds().w - kPopupMargin` in `f.style.row_font`
/// (oled.cpp:610-613; both shipped profiles set `Apple7`, whose metrics are exactly the
/// `kTextSpacingX`/`kTextSpacingY` pair legacy hard-coded); box `= {longest line,
/// count * 9} + kPopupMargin` (:615-618); first line at `(height - text height) >> 1`, clamped
/// up to 0 (:620-623); each line at `(width - its width) >> 1` (:630). The line widths are
/// `wrap_lines`' own, not `measure`'s -- they differ by the trailing inter-character spacing that
/// `breakStringIntoLines` subtracts off (text_wrap.h).
///
/// `PopupStyle::Permanent` follows `drawPermanentPopupLookingText` (:570-601) instead, which is
/// the same routine with a square border, the mirrored centring above, and no "line fell off the
/// bottom" check (:626-628 has one; :595 does not).
///
/// Centres on the *frame*; see `popup_rect_for`. Draws no timer and sets no state: how long a
/// popup stays up is the shell's business, not a widget's.
void popup_text(OledFrame& f, std::string_view text, PopupStyle style = PopupStyle::Framed);

/// `updateWorkingAnimation` (oled.cpp:647-721): the 18x10 box of sliding bars the Deluge shows in
/// the top-right corner while it loads or saves.
///
/// \p word is legacy's own direction switch and **nothing is printed**: `displayWorkingAnimation`
/// keeps only `loading = !strcmp(word, "Loading")` (:723) and the animation runs right-to-left
/// for "Loading" and left-to-right for anything else ("Saving"). There is no word drawn on screen
/// and no row of dots anywhere in the routine; the task brief's ""Loading" + animated dots" does
/// not describe any legacy code, and legacy is what is ported here.
///
/// Placed at \p rect's top-right corner with a one-pixel gap, as legacy places it at the panel's
/// (`popupX = OLED_MAIN_WIDTH_PIXELS - 1 - animation_width`, `popupY = OLED_MAIN_TOPMOST_PIXEL +
/// 2`, :654-655); with `rect == f.bounds()` those are the same pixels. It is the one popup legacy
/// does *not* centre.
///
/// \p blink is the animation's clock. `period_ms` doubles as "armed" (the same idiom
/// `decimal_value` uses): a `Blink` not already holding `kWorkingStepMs` is (re)anchored at
/// `f.input.now_ms`, so zeroing it restarts the animation the way `displayWorkingAnimation`'s
/// `working_animation_count = 1` did. The step is then derived from the elapsed time --
/// `kWorkingPauseMs` before the sequence starts, `kWorkingStepMs` per step, `kWorkingPauseMs`
/// again on the final frame of each cycle (:711-720) -- and `request_repaint_after` is armed for
/// the remainder of the current step, replacing `TimerName::LOADING_ANIMATION` entirely.
///
/// Legacy accumulated this animation on a persistent canvas: most frames repaint only the top
/// `h2` rows and leave the rest standing. An immediate-mode frame has no history, so this replays
/// the frames that produced it -- the initialiser, then (for any cycle after the first) the
/// full-interior repaint that ends a cycle, then every step of the current cycle. At most 29
/// steps of at most six one-row spans each; the result is the same pixels legacy's canvas held.
void working(OledFrame& f, gfx::oled::Rect rect, std::string_view word, Blink& blink);

/// A progress bar filling \p rect left to right at \p num / \p den.
///
/// **Legacy has no progress bar.** Nothing under `src/deluge` draws one for a long operation --
/// loading and saving show `working` above, and there is no percentage popup or stem-export
/// readout in this tree. The closest legacy pixels are `Number::drawHorizontalBar`
/// (gui/menu_item/number.cpp:33-73), the full-screen menu bar that task 8 deliberately left out
/// of `meters.h` because no horizontal-menu slot reaches it, and this *is* that routine with
/// `min_value = 0`, `value = num`, `max_value = den`:
///
///  - the outline is `drawRectangleRounded(left_most, y_top, right_most - 1, y_top + height)`
///    (:72), i.e. exactly \p rect;
///  - a zero numerator draws the outline and nothing else (:41-44);
///  - otherwise `invertArea` fills `num * rect.w / den` columns from the left across
///    `rect.y + 1 .. rect.bottom() - 2` (:66-70), and a non-zero numerator too small to fill one
///    column still inks a single column, which is legacy's "= 0, draw vertical line" arm
///    (:55-60) reached at `zero_pos_horizontal == 0`.
///
/// Unlike `popup_rect_for`'s rect, \p rect here is the outline's true pixel extent: nothing in
/// legacy computes this box from a `width`, so there is no off-by-one to preserve.
///
/// \p num is clamped to `[0, den]` and a \p den of zero or less draws the empty outline. Legacy
/// needed neither guard (its value was in range by construction) and has neither.
void progress(OledFrame& f, gfx::oled::Rect rect, int32_t num, int32_t den);

} // namespace deluge::ui::oled
