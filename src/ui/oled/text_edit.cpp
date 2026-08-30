#include "ui/oled/text_edit.h"

#include "gfx/oled/font.h"

#include <algorithm>
#include <cstdint>

namespace deluge::ui::oled {

using gfx::oled::kTextSpacingX;
using gfx::oled::kTextSpacingY;
using gfx::oled::Rect;

void text_edit(OledFrame& f, Rect rect, std::string_view s, TextEditState& state) {
	const int32_t length = static_cast<int32_t>(s.size());
	// qwerty_ui.cpp's callers each compute their own maxNumChars from their own box width
	// (RenameUI::renderOLED's literal 17, Browser::renderOLED's maxChars); here it is rect.w in
	// whole characters, at least one so a very narrow rect never divides by zero below.
	const int32_t max_num_chars = std::max<int32_t>(1, rect.w / kTextSpacingX);

	// Two-pass, as `list`'s row count: clamp into this frame's string before anything below reads
	// it, so a string that shrank out from under a stale cursor corrects on this same frame.
	const int32_t cursor = std::clamp<int32_t>(state.cursor, 0, length);
	state.cursor = static_cast<int16_t>(cursor);
	const bool at_very_end = cursor == length;

	// qwerty_ui.cpp:135-144, verbatim: `scrollPosHorizontal` (here `state.scroll`) is nudged in
	// only as far as needed to keep the cursor inside the window, never recentred outright.
	// "Prevent being scrolled too far left."
	int32_t min_scroll =
	    std::min<int32_t>(cursor + 3 - max_num_chars, length - max_num_chars + static_cast<int32_t>(at_very_end));
	min_scroll = std::max<int32_t>(min_scroll, 0);
	int32_t scroll = std::max<int32_t>(state.scroll, min_scroll);

	// "Prevent being scrolled too far right."
	int32_t max_scroll = std::min<int32_t>(length - max_num_chars + static_cast<int32_t>(at_very_end), cursor - 3);
	max_scroll = std::max<int32_t>(max_scroll, 0);
	scroll = std::min<int32_t>(scroll, max_scroll);
	state.scroll = static_cast<int16_t>(scroll);

	// qwerty_ui.cpp:146: drawString(&displayName[scrollPosHorizontal], xPixel, yPixel,
	// kTextSpacingX, kTextSpacingY, 0, xPixel + maxNumChars * kTextSpacingX).
	f.glyphs(s.substr(static_cast<size_t>(scroll)), rect.x, rect.y, kTextSpacingX, kTextSpacingY, 0,
	         rect.x + max_num_chars * kTextSpacingX);

	if (!state.cursor_lit) {
		return;
	}

	if (at_very_end) {
		// qwerty_ui.cpp:149-154: the blinking-underline case (there gated on `atVeryEnd` and
		// `getCurrentUI() == this`; here the caller's `state.cursor_lit` stands in for both).
		// `textBottomY - 4 .. textBottomY - 2` is a 3-row band near the bottom of the 9-row text
		// cell, one kTextSpacingX-wide column at the cursor's screen position.
		const int32_t cursor_x = rect.x + (cursor - scroll) * kTextSpacingX;
		const int32_t text_bottom_y = rect.y + kTextSpacingY;
		f.invert(Rect{cursor_x, text_bottom_y - 4, kTextSpacingX, 3});
		return;
	}

	// qwerty_ui.cpp:156-170: legacy's *only* mid-string cursor presentation -- not a
	// predictive-text overlay, `enteredTextEditPos` is plainly the cursor throughout this
	// routine -- so it is ported, not skipped: invert from the cursor to the rect's right edge,
	// following `f.style.invert_highlight` exactly the way `list`/`columns` already port the same
	// legacy primitive (`invertLeftEdgeForMenuHighlighting`).
	const int32_t scroll_amount = cursor - scroll; // == enteredTextEditPos - scrollPosHorizontal
	int32_t highlight_start_x;
	if (!f.style.invert_highlight && scroll_amount == 0) {
		// Legacy's NO_INVERSION special case starts the rule at the panel's own left edge (x =
		// 0) rather than one pixel short of the box -- generalised here to the rect's own left
		// edge, the same "whole panel" -> "this rect" substitution every other edge in this
		// widget makes.
		highlight_start_x = rect.x;
	}
	else {
		// Start in the 1px gap that follows the previous character, rather than on top of that
		// character's last column -- otherwise the inversion eats a pixel column out of the
		// letter before the cursor.
		highlight_start_x = rect.x + kTextSpacingX * scroll_amount - 1;
	}

	if (f.style.invert_highlight) {
		f.invert_rounded(Rect{highlight_start_x, rect.y, rect.right() - highlight_start_x, kTextSpacingY});
	}
	else {
		f.vline(highlight_start_x, rect.y, rect.y + kTextSpacingY - 1);
	}
}

} // namespace deluge::ui::oled
