#pragma once
#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

/// @brief Cursor and scroll position for an edited string; the string itself belongs to the
///        caller (a `std::string` a scene owns, not this widget).
struct TextEditState {
	int16_t cursor = 0;     ///< Insertion point, 0..s.size().
	int16_t scroll = 0;     ///< Index of the first character `text_edit` draws.
	bool cursor_lit = true; ///< Blink phase; the caller's clock decides when this flips.
};

/// @brief Draw @p s in @p rect with a blinking underline cursor at `state.cursor`, scrolling
/// horizontally so the cursor stays inside the window.
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
///   - the cursor's row is legacy's blinking-underline case (`atVeryEnd` true, current UI):
///     a 3-row band, `kTextSpacingY - 4 .. kTextSpacingY - 2` from the text's top, one
///     `kTextSpacingX`-wide column at the cursor.
///
/// @note **Render-only; owns no input.** Legacy's other case -- `atVeryEnd` false -- inverts the
///       box from the cursor to the end of the string, which is how `QwertyUI` marks predicted
///       text as unconfirmed (`predictExtendedText`, driven by `FlashStorage`). That is model
///       behaviour a toolkit widget cannot have an opinion on, so it is not ported: whatever
///       `state.cursor` is, this widget always draws the same blinking underline there, lit or
///       dark per `state.cursor_lit`. Cursor motion is the caller's, too -- `horizontalEncoderAction`
///       (`qwerty_ui.cpp:420-447`) becomes the scene's own `f.horizontal()` handling (see
///       `RenameScene::handle`), exactly as the grid half's `text_keyboard` leaves typing and
///       backspace to its caller. `text_edit` never reads a frame's input at all, so it is safe to
///       call from an unfocused region purely for its looks.
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
