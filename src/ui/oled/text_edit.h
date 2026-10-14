#pragma once
#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

/// @brief Cursor and scroll position for an edited string; the string itself belongs to the
///        caller (a `std::string` a scene owns, not this widget).
struct TextEditState {
	int16_t cursor = 0; ///< Insertion point, 0..s.size().
	int16_t scroll = 0; ///< Index of the first character `text_edit` draws.
	/// Blink phase for the end-of-string bar only; the caller's clock decides when this flips.
	/// Governs *only* the at-the-end presentation below -- the mid-string highlight is legacy's
	/// own unconditional `invertLeftEdgeForMenuHighlighting` call and never flashes off, so this
	/// field does nothing while the cursor sits mid-string, despite its name suggesting it gates
	/// the cursor generally.
	bool cursor_lit = true;
};

/// @brief Draw @p s in @p rect with a cursor at `state.cursor`, scrolling horizontally so the
/// cursor stays inside the window.
///
/// Ports `QwertyUI::drawTextForOLEDEditing` (`gui/ui/qwerty_ui.cpp:127-171`), generalised from
/// "the whole panel" (its `xPixel`/`xPixelMax`/`maxNumChars` triple) to `rect`, the way every
/// other widget in `src/ui/oled/` was:
///   - `max_num_chars` is `rect.w / kTextSpacingX` where legacy's callers each computed their own
///     (`RenameUI::renderOLED`'s literal 17, `Browser::renderOLED`'s `maxChars` from its own box
///     width);
///   - the scroll clamp is legacy's arithmetic verbatim, with `enteredTextEditPos` -> `cursor` and
///     `displayStringLength` -> `s.size()`, `atVeryEnd` (`cursor == s.size()`) still widening the
///     window by one column so the cursor can sit just past the last character;
///   - `state.scroll` is legacy's `scrollPosHorizontal`: a value the caller keeps across frames,
///     nudged in only as far as needed to keep the cursor visible, never recentred;
///   - the cursor has legacy's own two presentations, both ported, selected by `atVeryEnd`
///     (`cursor == s.size()`):
///     - **at the end**: the blinking-underline case (`qwerty_ui.cpp:149-154`) -- a 3-row band,
///       `kTextSpacingY - 4 .. kTextSpacingY - 2` from the text's top, one `kTextSpacingX`-wide
///       column at the cursor. `state.cursor_lit` gates only this bar, exactly as legacy's
///       `setupBlink` is only ever reached from this arm;
///     - **mid-string**: `invertLeftEdgeForMenuHighlighting` from the cursor to the rect's right
///       edge (`qwerty_ui.cpp:156-170`) -- legacy's *only* mid-string cursor presentation, not a
///       predictive-text overlay (`enteredTextEditPos` is plainly the cursor throughout this
///       routine). Unconditional, like its legacy call -- `state.cursor_lit` is not consulted
///       here, so this presentation never flashes off. Ported following `f.style.highlighting`,
///       exactly the way `list` and `columns` already port the same legacy primitive
///       (`invertLeftEdgeForMenuHighlighting`,
///       named in `list.cpp`'s D1 comment): `!= NoInversion` inverts the rounded box
///       (`invertAreaRounded`, legacy's `FULL_INVERSION`/`PARTIAL_INVERSION`), `NoInversion` draws a
///       single-column rule (`draw_vertical_line`, legacy's `NO_INVERSION`). The highlight's left
///       edge starts in the 1px gap *after* the previous character
///       (`rect.x + kTextSpacingX * scroll_amount - 1`), not on top of its last column --
///       otherwise the inversion eats a pixel column out of the letter before the cursor -- except
///       legacy's own special case, `NoInversion && scroll_amount == 0`, which starts the
///       rule at the panel's own left edge rather than one pixel short of the box; generalised
///       here to the rect's own left edge (`rect.x`), the same "whole panel" -> "this rect"
///       substitution every other edge in this widget (and `gfx::oled::kEdge`/`kCentre`) makes.
///
/// @note **Render-only; owns no input.** Cursor motion is the caller's --
///       `horizontalEncoderAction` (`qwerty_ui.cpp:420-447`) becomes the scene's own
///       `f.horizontal()` handling (see `RenameScene::handle`), exactly as the grid half's
///       `text_keyboard` leaves typing and backspace to its caller. `text_edit` never reads a
///       frame's input at all, so it is safe to call from an unfocused region purely for its
///       looks.
///
/// `state.cursor` is clamped into `[0, s.size()]` for *this* frame's string before anything else,
/// the same two-pass discipline `list` uses for its row count: a string that shrank out from
/// under a stale cursor is corrected on the same frame, not the next one.
///
/// @param f     Frame to draw into. Input is never read.
/// @param rect  Rect the text is laid out in, frame-local.
/// @param s     The string to draw.
/// @param state Cursor/scroll/blink state, in/out.
void text_edit(OledFrame& f, gfx::oled::Rect rect, std::string_view s, TextEditState& state);

} // namespace deluge::ui::oled
