#include "ui/oled/values.h"

#include "ui/core/metrics.h"
#include "ui/oled/detail/int_to_string.h"
#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace deluge::ui::oled {

using gfx::oled::Rect;

namespace {

using detail::int_to_string;

/// `value + delta`, clamped into \p range. Widened to 64 bits first, because legacy's
/// `getValue() + offset` (integer.cpp:26) can overflow int32 near the type's ends and the
/// clamp that follows would then read a wrapped value.
int32_t add_clamped(int32_t value, int64_t delta, Range range) {
	const int64_t next = static_cast<int64_t>(value) + delta;
	return static_cast<int32_t>(std::clamp<int64_t>(next, range.min, range.max));
}

/// The `edit[]` pair as one signed amount: `edit[0]` (GoldUpper) at \p coarse per detent and
/// `edit[1]` (GoldLower) at \p fine. See the fine/coarse note in values.h.
int64_t edit_delta(const InputSnapshot& in, int32_t coarse, int32_t fine) {
	return static_cast<int64_t>(in.edit[0]) * coarse + static_cast<int64_t>(in.edit[1]) * fine;
}

/// A `step` of 0 would freeze the coarse knob; legacy had no such value (its step was always 1),
/// so it is read as the default rather than honoured.
int32_t coarse_step(Range range) {
	return range.step != 0 ? range.step : 1;
}

/// The same frame with `input.turn` zeroed, for the widgets that move their own cursor and must
/// stop `list` from moving it a second time. \p masked must outlive the returned frame.
OledFrame without_turn(const OledFrame& f, InputSnapshot& masked) {
	masked = f.input;
	masked.turn = 0;
	return OledFrame{f.canvas, f.clip, f.origin, f.size, f.style, masked, f.gate};
}

/// `10^exponent`, saturated at INT32_MAX -- legacy's `numberEditSize`, built by the same
/// repeated multiplication (decimal.cpp:40-42).
int32_t pow10(int32_t exponent) {
	int64_t v = 1;
	for (int32_t i = 0; i < exponent && v <= INT32_MAX; i++) {
		v *= 10;
	}
	return static_cast<int32_t>(std::min<int64_t>(v, INT32_MAX));
}

/// Is the blinking cursor lit at \p now? `OLED::performBlink` (oled.cpp:832-838) inverts the
/// cursor band every `kFlashTime`, starting one `kFlashTime` after `setupBlink` armed the timer
/// (oled.cpp:840-850) -- so the first half-period after arming is dark and every odd one after
/// that is lit. `blink.until_ms` is that arming instant; see values.h.
bool blink_lit(const Blink& blink, uint32_t now) {
	const uint32_t elapsed = now - blink.until_ms;
	return ((elapsed / blink.period_ms) & 1u) != 0;
}

/// Milliseconds until the blink flips, i.e. what the next frame has to be scheduled for.
uint32_t blink_next_flip_in(const Blink& blink, uint32_t now) {
	return blink.period_ms - ((now - blink.until_ms) % blink.period_ms);
}

} // namespace

Response int_value(OledFrame& f, Rect rect, int32_t& value, Range range, ValueStyle style, bool focused) {
	Response resp{};
	resp.focused = focused;

	if (focused) {
		// Integer::selectEncoderAction (integer.cpp:25-39): add, then clamp to [min, max].
		const int64_t delta = edit_delta(f.input, coarse_step(range), 1);
		if (delta != 0) {
			const int32_t next = add_clamped(value, delta, range);
			if (next != value) {
				value = next;
				resp.changed = true;
				f.gate.request_repaint();
			}
		}
	}

	// drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX, kTextHugeSizeY)
	// -- integer.cpp:52-61. The topmost-pixel term is the frame's origin, so the y is kValueY,
	// measured from the rect's top; with rect == f.bounds() that is the legacy panel-relative 18,
	// and the centring is over the legacy panel width.
	const gfx::oled::FontId font = f.style.value_font;
	const Rect band{rect.x, rect.y + kValueY, rect.w, text_height_for(font)};
	f.clear(band);

	if (style.off_at_zero && value == 0) {
		// IntegerWithOff::drawPixelsForOled, integer.cpp:41-50: "OFF" in place of the digits,
		// and without the unit.
		text(f, band, "OFF", TextStyle{.font = font, .align = Align::Centre});
		return resp;
	}

	// char buffer[12] + strncat(buffer, getUnit(), 4) (integer.cpp:53-55). The buffer here is
	// wide enough for both parts at once; legacy's 12 held only the digits, and its strncat
	// could run past them.
	std::array<char, 12 + 4> buf{};
	const std::string_view digits = int_to_string(value, 1, std::span{buf}); // intToString(..., 1)
	size_t length = digits.size();
	for (size_t i = 0; i < style.unit.size() && i < 4 && length < buf.size(); i++) {
		buf[length++] = style.unit[i];
	}
	text(f, band, std::string_view{buf.data(), length}, TextStyle{.font = font, .align = Align::Centre});
	return resp;
}

Response decimal_value(OledFrame& f, Rect rect, int32_t& value, Range range, uint8_t places, int8_t& edit_pos,
                       Blink& blink, bool focused) {
	Response resp{};
	resp.focused = focused;
	const uint32_t now = f.input.now_ms;
	const int32_t num_decimal_places = places;
	edit_pos = static_cast<int8_t>(std::max<int32_t>(0, edit_pos));

	bool horizontal_ticked = false;
	if (focused) {
		// Decimal::horizontalEncoderAction, decimal.cpp:83-101: one place per tick, right when
		// positive, left when the next place up is still inside the range.
		const int8_t horizontal = f.input.horizontal;
		// decimal.cpp:98 sets movingCursor *after and outside* the if/else at :84-96, so a tick
		// blocked by either bound below still restarts the blink lit.
		horizontal_ticked = horizontal != 0;
		if (horizontal > 0 && edit_pos > 0) {
			edit_pos--;
		}
		else if (horizontal < 0 && static_cast<int64_t>(pow10(edit_pos)) * 10 <= range.max) {
			edit_pos++;
		}

		// Decimal::selectEncoderAction, decimal.cpp:57-79: the value moves by numberEditSize per
		// detent (decimal.cpp:39-42 builds that as 10^numberEditPos), then clamps.
		const int64_t delta = edit_delta(f.input, pow10(edit_pos), coarse_step(range));
		if (delta != 0) {
			const int32_t next = add_clamped(value, delta, range);
			if (next != value) {
				value = next;
				resp.changed = true;
				f.gate.request_repaint();
			}
		}
	}

	// Legacy setupBlink is reached once per full redraw, so a value change restarts the phase
	// dark and a horizontal tick (its `shouldBlinkImmediately`, decimal.cpp:98-100 via :163)
	// restarts it lit. period_ms doubles as "armed": a Blink still holding state.h's 500ms default
	// has never been through here.
	if (blink.period_ms != kFlashTime) {
		blink.period_ms = kFlashTime;
		blink.until_ms = now;
	}
	if (horizontal_ticked) {
		blink.until_ms = now - blink.period_ms; // one half-period in: lit immediately
	}
	else if (resp.changed) {
		blink.until_ms = now;
	}
	f.gate.request_repaint_after(blink_next_flip_in(blink, now), now);

	// Everything below is decimal.cpp:128-163 verbatim. The x arithmetic is relative to `rect`,
	// which at f.bounds() on the root frame is legacy's OLED_MAIN_WIDTH_PIXELS span; the two y
	// constants stay canvas-absolute and are converted by subtracting the frame's origin -- see
	// the canvas-absolute note in values.h.
	const int32_t local_y = 20 - f.origin.y;  // decimal.cpp:149/152/161's literal 20
	const int32_t cursor_y = 41 - f.origin.y; // decimal.cpp:163's literal 41
	const int32_t local_end_x = rect.right(); // decimal.cpp:149's literal 128

	std::array<char, 13> buf{}; // char buffer[13], decimal.cpp:130
	const std::string_view text_all = int_to_string(value, num_decimal_places + 1, std::span{buf});
	const int32_t length = static_cast<int32_t>(text_all.size());

	int32_t editing_char = length - edit_pos;
	if (edit_pos >= num_decimal_places) {
		editing_char--;
	}

	// `Style::big_font` is the huge digit face this widget draws in -- the profile's answer to
	// "which face are the big numbers", where `value_font` is `int_value`'s. Both are
	// MetricBold20 on the shipped profiles, whose advance and height are exactly the
	// kTextHugeSpacingX / kTextHugeSizeY that decimal.cpp:149-163 spells as literals, so the
	// pixels are unchanged; the static_asserts below are what keeps that true.
	const gfx::oled::FontId big = f.style.big_font;
	static_assert(kDelugeStyle48.big_font == gfx::oled::FontId::MetricBold20
	              && kDelugeStyle64.big_font == gfx::oled::FontId::MetricBold20);
	static_assert(text_height_of(gfx::oled::FontId::MetricBold20) == kTextHugeSizeY);
	const int32_t digit_height = text_height_for(big);
	const int32_t digit_width = spacing_x_for(big);
	const int32_t period_width = digit_width / 2;
	const int32_t string_width = digit_width * length + (num_decimal_places != 0 ? period_width : 0);
	int32_t string_start_x = rect.x + ((rect.w - string_width) >> 1); // decimal.cpp:142
	int32_t our_digit_start_x = string_start_x + editing_char * digit_width;

	// The digits' own band and the cursor's, cleared before either is drawn: legacy got that
	// from renderUIsForOled clearing the whole canvas first, and the cursor in particular is an
	// inversion, which is only deterministic over a known background.
	f.clear(Rect{rect.x, local_y, rect.w, digit_height});
	f.clear(Rect{rect.x, cursor_y, rect.w, 2});

	std::string_view rest = text_all;
	if (num_decimal_places != 0) {
		const int32_t before_point = length - num_decimal_places;
		// Both halves are drawn with use_width set, so every digit advances exactly digit_width
		// and the period exactly period_width (decimal.cpp:149-153).
		f.glyphs(text_all.substr(0, static_cast<size_t>(before_point)), string_start_x, local_y, digit_width,
		         digit_height, 0, local_end_x, true);
		f.glyphs(".", string_start_x + before_point * digit_width, local_y, period_width, digit_height, 0, local_end_x,
		         true);
		rest = text_all.substr(static_cast<size_t>(before_point));
		string_start_x += before_point * digit_width + period_width;
		if (editing_char > before_point) {
			our_digit_start_x -= period_width;
		}
	}
	f.glyphs(rest, string_start_x, local_y, digit_width, digit_height, 0, local_end_x, true);

	// setupBlink(ourDigitStartX + 1, digitWidth - 2, 41, 42, movingCursor), decimal.cpp:163:
	// canvas rows 41..42 inclusive, i.e. a 2-row band, inverted while the phase is lit.
	if (blink_lit(blink, now)) {
		f.invert(Rect{our_digit_start_x + 1, cursor_y, digit_width - 2, 2});
	}
	return resp;
}

Response enum_value(OledFrame& f, Rect rect, int32_t& index, std::span<const std::string_view> names, ListState& state,
                    ScrollText& scroll_label, bool wrap) {
	Response resp{};
	const int32_t n = static_cast<int32_t>(names.size());
	if (n <= 0) {
		state.cursor = 0;
		state.scroll = 0;
		state.rows_last = 0;
		return resp;
	}

	// Enumeration::selectEncoderAction, enumeration.cpp:18-36.
	int32_t next = std::clamp<int32_t>(index, 0, n - 1);
	if (f.input.turn != 0) {
		next += f.input.turn;
		next = wrap ? ((next % n) + n) % n : std::clamp<int32_t>(next, 0, n - 1);
	}
	resp.changed = next != index;
	index = next;
	if (resp.changed) {
		f.gate.request_repaint();
	}

	const int32_t visible = std::min<int32_t>(list_visible_rows(f, rect), kMaxWindowRows);
	const int32_t start = list_window_start(index, n, visible);
	const int32_t count = std::min<int32_t>(visible, n - start);

	std::array<ListRow, kMaxWindowRows> window{};
	for (int32_t i = 0; i < count; i++) {
		window[static_cast<size_t>(i)] = ListRow{.label = names[static_cast<size_t>(start + i)]};
	}

	// list re-derives the window from the cursor it is given, and for a span of exactly the
	// visible count that always comes out at 0 -- the slice already *is* the window.
	ListState windowed{
	    .cursor = static_cast<int16_t>(index - start), .scroll = 0, .rows_last = static_cast<int16_t>(count)};
	InputSnapshot masked{};
	OledFrame inner = without_turn(f, masked);
	const Response inner_resp =
	    list(inner, rect, windowed, std::span{window.data(), static_cast<size_t>(count)}, scroll_label);
	resp.entered = inner_resp.entered;

	state.cursor = static_cast<int16_t>(index);
	state.scroll = static_cast<int16_t>(start);
	state.rows_last = static_cast<int16_t>(n);
	return resp;
}

Response toggle(OledFrame& f, Rect rect, bool& value, std::string_view off_label, std::string_view on_label,
                ScrollText& scroll_label) {
	Response resp{};

	bool next = value;
	if (f.input.turn != 0) {
		next = f.input.turn > 0; // Toggle::selectEncoderAction, toggle.cpp:14-17
	}
	resp.changed = next != value;
	value = next;
	if (resp.changed) {
		f.gate.request_repaint();
	}

	// toggle.cpp:29-42 -- `bool order[2] = {false, true}`, the current one highlighted.
	const std::array<ListRow, 2> rows{ListRow{.label = off_label}, ListRow{.label = on_label}};
	ListState windowed{.cursor = static_cast<int16_t>(value ? 1 : 0), .scroll = 0, .rows_last = 2};
	InputSnapshot masked{};
	OledFrame inner = without_turn(f, masked);
	list(inner, rect, windowed, std::span{rows}, scroll_label);

	// On this screen the Toggle is the current menu item, so selectButtonPress takes the
	// `getCurrentMenuItem() == this` branch and returns nullptr *without touching the value*
	// (toggle.h:35-36); sound_editor.cpp:357 + :386-389 then goes up a level. The toggleValue()
	// branch (toggle.h:39-43) is for a Toggle drawn as a row inside a Submenu, not for this
	// widget. So the press is reported and the scene navigates -- as with enum_value.
	resp.entered = f.input.select_pressed;
	return resp;
}

} // namespace deluge::ui::oled
