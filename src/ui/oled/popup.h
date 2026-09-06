#pragma once
#include "gfx/oled/geometry.h"
#include "ui/core/state.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <span>
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

/// @brief How many option rows `option_window` shows at once.
///
/// Legacy's own `if (i >= 2) break;` (`gui/context_menu/context_menu.cpp`'s `renderOLED`), not a
/// number derived from the window height: the 40px window has room for two 9px rows below its
/// title rule and legacy hard-codes exactly that.
inline constexpr int32_t kOptionWindowRows = 2;

/// @brief Ports legacy `ContextMenu::renderOLED` (`gui/context_menu/context_menu.cpp`): the
///        centred 100x40 window with a heading, a rule under it, and a two-row scrolling option
///        list with the focused row inverted.
///
/// A member of the popup family rather than a `list` composition, because none of `list`'s three
/// layout rules are the ones this window uses:
///
///  - **Label origin.** Legacy puts the label one pixel inside the highlight's left edge
///    (`textPixelX = invertStartX + 1`, i.e. canvas x 23), where `list` insets by
///    `style.inset_x` (`kTextSpacingX`, 6) as `MenuItem::drawItemsForOled` does.
///  - **Right clip.** Legacy clips the label at `panel width - 27` (101) while the highlight runs
///    to `panel width - 44 + 22` (106), so the drawn box and the inverted box are *different
///    rectangles*; `list` derives both from the one `rect` it is given.
///  - **Windowing.** `ContextMenu::selectEncoderAction` keeps the cursor on the *bottom* row while
///    scrolling down (it only moves `scrollPos` once the cursor has already left it), where
///    `list_window_start` keeps the cursor near the middle -- on a two-row window, the top row.
///    That rule is input, not pixels, so it stays with the caller (`@p first` below); this widget
///    just draws the window the caller chose.
///
/// Everything is laid out against the **panel**, not against a rect, exactly as `popup_rect_for`
/// and `popup_text` are and for the same reason: every coordinate legacy computes here is against
/// `OLED_MAIN_WIDTH_PIXELS` / `OLED_MAIN_HEIGHT_PIXELS`, hidden bezel rows included. The window is
/// `PopupStyle::Permanent` -- legacy's `drawRectangle` is the *square* primitive, not the rounded
/// one -- with the same clear-then-outline result its `clearAreaExact` + `drawRectangle` pair
/// produces.
///
/// @note **The focus highlight follows `style.invert_highlight`**, exactly as `list` and
///       `text_edit` port the same legacy primitive (`invertLeftEdgeForMenuHighlighting`): true
///       inverts the rounded 84px band from x 22, false draws a single-column rule there instead
///       and shifts every label right by one `kTextSpacingX` -- legacy's own
///       `MenuHighlighting::NO_INVERSION` arm.
///
/// @note **Nothing here is claimed.** Render-only, as `text_edit` is: the select encoder's turn
///       and the accept button are the scene's (`ContextMenu::selectEncoderAction` /
///       `buttonAction`), so this widget reads no input and is safe to draw on an unfocused frame.
///
/// @note Legacy's `isCurrentOptionAvailable()` filter is **not** here. Its render loop skips
///       unavailable options and its encoder loop turns past them, which is the same thing as
///       operating on the list with those entries removed -- so a caller with dynamic
///       availability compacts @p options before calling, and @p selected / @p first index the
///       compacted list. No shipped caller has one.
///
/// @param f            Frame to draw into; input is never read.
/// @param heading      The window's title (`ContextMenu::getTitle`), drawn left-aligned at x 22 in
///                     `style.row_font` -- legacy's plain `drawString`, **not** `drawScreenTitle`,
///                     so `title()` is the wrong widget here.
/// @param options      Every option, in legacy's order (`ContextMenu::getOptions`).
/// @param selected     The focused option (`currentOption`); clamped into @p options.
/// @param first        The first option shown (`scrollPos`); clamped so the focused row is one of
///                     the `kOptionWindowRows` drawn.
/// @param label        Side-scroller state for the focused row (`OLED::setupSideScroller`), owned
///                     by the caller across frames.
/// @param allow_motion Whether that scroller may advance this frame.
/// @param footer       An extra line under the option rows, or empty for none.
///                     `AudioInputSelector::renderOLED` (`gui/context_menu/audio_input_selector.cpp:222-237`)
///                     is the one legacy subclass that draws past the base renderer, and this is
///                     that call: `drawString(trackName, 22, windowMinY + 20 + kTextSpacingY,
///                     kTextSpacingX, kTextSpacingY, 0, OLED_MAIN_WIDTH_PIXELS - 26)`. Its row is
///                     two pixels below the second option row's, and it is *not* cleared first --
///                     legacy inks it straight over whatever the base drew there.
///
///                     **Drawn last, after the option rows, because that is legacy's order:**
///                     `AudioInputSelector::renderOLED` calls `ContextMenu::renderOLED` and then
///                     this. Review round 1 asked for the order to match rather than be argued
///                     around, and it now does. Two arguments for the old order were offered and
///                     both are wrong: the bands are not on "different rows" (they overlap by two
///                     pixels), and "the descriptor never shows both" is a property of that
///                     descriptor -- it forces its option onto the first row whenever a footer is
///                     up (`selectEncoderAction`'s "Keep Track on the first visible row so the
///                     selected track name has room below it", `:142-145`) -- not of this widget.
///                     The reason it was in fact safe is narrower and worth knowing: **the glyph
///                     blit is `|=`** (`gfx/oled/canvas.cpp:626`, `:663`, `:673`), so two
///                     `draw_string`s over the same pixels give the same union whichever runs
///                     first, and no golden can tell the orders apart -- verified by re-running
///                     `option_window_spec.cpp` with the draw moved back. Matching legacy is
///                     therefore about the code staying reviewable against the original, and about
///                     already being right on the day a footer wants a clear or something
///                     subtractive. No clear of its own either: one *would* erase pixels legacy
///                     leaves alone, which a golden does catch.
void option_window(OledFrame& f, std::string_view heading, std::span<const std::string_view> options, int32_t selected,
                   int32_t first, ScrollText& label, bool allow_motion = true, std::string_view footer = {});

/// @brief Ports legacy `Slicer::renderOLED` (`gui/ui/slicer.cpp:74-98`): a framed 100x31 window
///        with a left-aligned heading, a rule under it, and one value centred below the rule.
///
/// A member of the popup family for the same reason `option_window` is: every coordinate legacy
/// computes here is against `OLED_MAIN_WIDTH_PIXELS` / `OLED_MAIN_HEIGHT_PIXELS` -- the *panel*,
/// hidden bezel rows included -- so the layout is done in `panel_of(f)` and lands in frame
/// coordinates for free. It is `option_window`'s window with three constants changed, not a
/// composition of it: the box is 31 rows rather than 40, it is nudged right by 6 and down by 2,
/// and there is one centred value instead of two option rows.
///
/// @note **The two shifts are the whole reason this cannot reuse `option_window`'s geometry.**
///       Legacy adds `horizontalShift` (6) to `windowMinX` *and* to the value's centre position
///       (`(OLED_MAIN_WIDTH_PIXELS >> 1) + horizontalShift`), but **not** to the rule, which spans
///       a fixed `26 .. panel width - 22`. So the rule is inset 6 from the box on the left and 14
///       on the right -- asymmetric, and reproduced rather than tidied.
///
/// @note The box is `PopupStyle::Permanent` because legacy's `drawRectangle` is the *square*
///       primitive. Legacy clears only the interior (`clearAreaExact(minX + 1, minY + 1,
///       maxX - 1, maxY - 1)`) and then inks the border ring on top; `popup_frame` clears the ring
///       as well and then inks it, which is the same result.
///
/// @note Nothing here is claimed: render-only, exactly as `option_window` is. The encoder turns
///       that change @p value belong to the scene.
///
/// @param f       Target frame.
/// @param heading The window's label, drawn left-aligned at panel x 30 in `style.row_font` --
///                legacy's plain `drawString`, **not** `drawScreenTitle`.
/// @param value   The value, centred on panel x `(width >> 1) + 6`, one line below the rule.
void count_window(OledFrame& f, std::string_view heading, std::string_view value);

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

/// @brief Ports legacy `OLED::displayNotification` (`hid/display/oled.cpp:744-794`): the one-line
///        "title: value" strip pinned to the top of the panel -- the mod-encoder value popup and
///        every other plain text/value notification.
///
/// Unlike every other popup in this file, this one is **not centred** -- it is pinned to the
/// panel's top-left corner, spanning the full width, exactly as legacy's own `start_x = 0`,
/// `start_y = OLED_MAIN_TOPMOST_PIXEL` are frame-local zero once the frame's `origin` has already
/// absorbed `visible_top` (the same convention `title()`/`title_badge()` use, not
/// `popup_rect_for`'s canvas-absolute one -- see `panel_of`'s note on that split).
///
/// @note **Truncates the title, not the value**, and measures the *first* width against the
///       untruncated @p title -- legacy's own quirk: `title_width` is measured from `param_title`
///       before `titleBuf` (which is already capped to `kNotificationTitleCap` characters) is
///       ever touched, so the truncation loop's starting point can be wider than the 25-character
///       buffer it is about to shrink. Reproduced rather than fixed, since it only differs from
///       "measure the capped buffer instead" when @p title exceeds 25 characters, which no shipped
///       caller's parameter/patch-source name ever does.
/// @note **One guard legacy does not need**: `titleBuf.resize(titleBuf.size() - 1)` on an
///       already-empty `etl::string` is legacy's own well-defined no-op; the equivalent
///       `std::string_view::remove_suffix(1)` on an empty view is undefined behaviour, so the
///       truncation loop here stops at an empty title instead. No shipped caller's title/value
///       pair is long enough to reach it either way.
/// @note The inversion-vs-dotted-rule split follows `f.style.invert_highlight`, exactly as
///       `option_window`/`list`/`text_edit` already port the same legacy accessibility branch
///       (`FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION`).
///
/// @param f     Target frame.
/// @param title The parameter/patch name; truncated from the end if @p value does not fit
///              alongside it.
/// @param value The value string, or empty for a title-only notification (legacy's
///              `param_value` with no `has_value()`) -- no colon, no value text, title drawn in
///              full.
void notification(OledFrame& f, std::string_view title, std::string_view value);

} // namespace deluge::ui::oled
