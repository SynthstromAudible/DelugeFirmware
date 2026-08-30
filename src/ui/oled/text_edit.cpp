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
	// qwerty_ui.cpp:149-154: the blinking-underline case (there gated on `atVeryEnd` and
	// `getCurrentUI() == this`; here the caller's `state.cursor_lit` stands in for both -- see
	// text_edit.h's render-only note for why the other, prediction-highlight branch is not
	// ported). `textBottomY - 4 .. textBottomY - 2` is a 3-row band near the bottom of the 9-row
	// text cell, one kTextSpacingX-wide column at the cursor's screen position.
	const int32_t cursor_x = rect.x + (cursor - scroll) * kTextSpacingX;
	const int32_t text_bottom_y = rect.y + kTextSpacingY;
	f.invert(Rect{cursor_x, text_bottom_y - 4, kTextSpacingX, 3});
}

} // namespace deluge::ui::oled
