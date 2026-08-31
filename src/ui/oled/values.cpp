#include "ui/oled/values.h"

#include "ui/core/metrics.h"
#include "ui/oled/detail/int_to_string.h"
#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace deluge::ui::oled {

using gfx::oled::Rect;

namespace {

using detail::int_to_string;

/// `value + delta`, clamped into \p range. Widened to 64 bits first because the sum can overflow
/// int32 near the type's ends, and the clamp that follows would then read a wrapped value.
int32_t add_clamped(int32_t value, int64_t delta, Range range) {
	const int64_t next = static_cast<int64_t>(value) + delta;
	return static_cast<int32_t>(std::clamp<int64_t>(next, range.min, range.max));
}

/// The `edit[]` pair as one signed amount: `edit[0]` (GoldUpper) at \p coarse per detent and
/// `edit[1]` (GoldLower) at \p fine. See the fine/coarse note in values.h. Read through the
/// frame, so an unfocused frame contributes nothing.
int64_t edit_delta(const OledFrame& f, int32_t coarse, int32_t fine) {
	return static_cast<int64_t>(f.edit(0)) * coarse + static_cast<int64_t>(f.edit(1)) * fine;
}

/// A `step` of 0 would freeze the coarse knob, so it is read as the default (1) rather than
/// honoured.
int32_t coarse_step(Range range) {
	return range.step != 0 ? range.step : 1;
}

/// Render \p body over the whole of \p f, *unfocused*.
///
/// The composite widgets below (`enum_value`, `toggle`) own the select encoder themselves and
/// draw a `list` only for its window and highlight. An unfocused frame is the contract's way to
/// say exactly that (ui/core/frame_context.h rule 3): the list paints pixel for pixel as it would
/// have -- the clock is not masked, so its label still scrolls -- but reads no turn and no press,
/// and claims nothing, because the outer widget has already claimed both.
///
/// `f.bounds()` is the whole frame and `clip` is already inside it, so the child comes out with
/// the same origin, size and clip as \p f -- this is `region()` used for its focus argument
/// alone, rather than a fourth place that spells out `OledFrame`'s member list.
template <class F>
void render_unfocused(OledFrame& f, F&& body) {
	f.region(f.bounds(), /*focused*/ false, std::forward<F>(body));
}

/// `10^exponent`, saturated at INT32_MAX.
int32_t pow10(int32_t exponent) {
	int64_t v = 1;
	for (int32_t i = 0; i < exponent && v <= INT32_MAX; i++) {
		v *= 10;
	}
	return static_cast<int32_t>(std::min<int64_t>(v, INT32_MAX));
}

/// Is the blinking cursor lit at \p now? The cursor inverts every `period_ms`, starting dark for
/// the first half-period after `until_ms` (the arming instant) and lit on every odd interval
/// after that. See the blink phase note on `decimal_value` in values.h.
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
		// Ports legacy `Integer::selectEncoderAction`: add, then clamp to [min, max].
		f.claim_edit(0);
		f.claim_edit(1);
		const int64_t delta = edit_delta(f, coarse_step(range), 1);
		if (delta != 0) {
			const int32_t next = add_clamped(value, delta, range);
			if (next != value) {
				value = next;
				resp.changed = true;
				f.gate().request_repaint();
			}
		}
	}

	// Ports legacy `Integer::drawPixelsForOled`'s centred digit layout. The frame's origin absorbs
	// the topmost-pixel term, so kValueY is measured from the rect's top; with rect == f.bounds()
	// that matches the legacy panel-relative offset and centring.
	const gfx::oled::FontId font = f.style.value_font;
	const Rect band{rect.x, rect.y + kValueY, rect.w, text_height_for(font)};
	f.clear(band);

	if (style.off_at_zero && value == 0) {
		// Ports legacy `IntegerWithOff::drawPixelsForOled`: "OFF" in place of the digits, without
		// the unit.
		text(f, band, "OFF", TextStyle{.font = font, .align = Align::Centre});
		return resp;
	}

	// Buffer is wide enough for the digits and the unit together; legacy's fixed-size buffer held
	// only the digits, and appending the unit could run past it.
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
                       Blink& blink, bool focused, const DecimalLayout* layout) {
	Response resp{};
	resp.focused = focused;
	const uint32_t now = f.now_ms();
	const int32_t num_decimal_places = places;
	edit_pos = static_cast<int8_t>(std::max<int32_t>(0, edit_pos));

	bool horizontal_ticked = false;
	if (focused) {
		// Ports legacy `Decimal::horizontalEncoderAction`: one place per tick, right when
		// positive, left when the next place up is still inside the range.
		f.claim_horizontal();
		f.claim_edit(0);
		f.claim_edit(1);
		const int8_t horizontal = f.horizontal();
		// A tick still restarts the blink lit even when blocked by either bound below (matches
		// the legacy ordering, where the moving-cursor flag is set unconditionally after the
		// position update).
		horizontal_ticked = horizontal != 0;
		if (horizontal > 0 && edit_pos > 0) {
			edit_pos--;
		}
		else if (horizontal < 0 && static_cast<int64_t>(pow10(edit_pos)) * 10 <= range.max) {
			edit_pos++;
		}

		// Ports legacy `Decimal::selectEncoderAction`: the value moves by 10^edit_pos per detent,
		// then clamps.
		const int64_t delta = edit_delta(f, pow10(edit_pos), coarse_step(range));
		if (delta != 0) {
			const int32_t next = add_clamped(value, delta, range);
			if (next != value) {
				value = next;
				resp.changed = true;
				f.gate().request_repaint();
			}
		}
	}

	// A value change restarts the blink phase dark; a horizontal tick restarts it lit
	// immediately. period_ms doubles as "armed": a Blink still holding its default period has
	// never been through here.
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
	f.gate().request_repaint_after(blink_next_flip_in(blink, now), now);

	// The x arithmetic is relative to `rect`, which at f.bounds() on the root frame spans the
	// whole panel width; the two y constants stay canvas-absolute and are converted to
	// frame-local by subtracting the frame's origin -- see the canvas-absolute note in values.h.
	const int32_t local_y = 20 - f.origin.y;  // canvas-absolute row, converted to frame-local
	const int32_t cursor_y = 41 - f.origin.y; // canvas-absolute row, converted to frame-local
	const int32_t local_end_x = rect.right();

	std::array<char, 13> buf{};
	const std::string_view text_all = int_to_string(value, num_decimal_places + 1, std::span{buf});
	const int32_t length = static_cast<int32_t>(text_all.size());

	int32_t editing_char = length - edit_pos;
	if (edit_pos >= num_decimal_places) {
		editing_char--;
	}

	// `Style::big_font` is the huge digit face this widget draws in -- the profile's answer to
	// "which face are the big numbers", distinct from `value_font` which `int_value` uses. The
	// static_asserts below enforce that both shipped profiles set the same metrics this layout
	// assumes.
	const gfx::oled::FontId big = layout != nullptr ? layout->font : f.style.big_font;
	static_assert(kDelugeStyle48.big_font == gfx::oled::FontId::MetricBold20
	              && kDelugeStyle64.big_font == gfx::oled::FontId::MetricBold20);
	static_assert(text_height_of(gfx::oled::FontId::MetricBold20) == kTextHugeSizeY);
	const int32_t digit_height = text_height_for(big);
	const int32_t digit_width = spacing_x_for(big);
	const int32_t period_width = digit_width / 2;
	const int32_t string_width = digit_width * length + (num_decimal_places != 0 ? period_width : 0);
	// Centred across `rect.w` by default; right-anchored when the caller shares the panel
	// (`DecimalLayout`), which is `image.drawStringAlignRight(..., OLED_MAIN_WIDTH_PIXELS - 3)`
	// (`gui/menu_item/patch_cable_strength.cpp:177`).
	int32_t string_start_x =
	    layout != nullptr ? rect.right() - layout->right_pad - string_width : rect.x + ((rect.w - string_width) >> 1);
	int32_t our_digit_start_x = string_start_x + editing_char * digit_width;

	// The digits' band and the cursor's band are cleared before either is drawn: the cursor is an
	// inversion, which is only deterministic over a known background.
	f.clear(Rect{rect.x, local_y, rect.w, digit_height});
	f.clear(Rect{rect.x, cursor_y, rect.w, 2});

	std::string_view rest = text_all;
	if (num_decimal_places != 0) {
		const int32_t before_point = length - num_decimal_places;
		// Both halves are drawn with use_width set, so every digit advances exactly digit_width
		// and the period exactly period_width.
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

	// A 2-row band at the edited digit's position, inverted while the phase is lit.
	if (blink_lit(blink, now)) {
		f.invert(Rect{our_digit_start_x + 1, cursor_y, digit_width - 2, 2});
	}
	return resp;
}

Response enum_value(OledFrame& f, Rect rect, int32_t& index, std::span<const std::string_view> names, ListState& state,
                    ScrollText& scroll_label, bool wrap) {
	Response resp{};
	const int32_t n = static_cast<int32_t>(names.size());
	// Claimed before the empty case returns: ownership is a property of the control being on
	// screen, not of the model behind it (ui/core/frame_context.h rule 2), which is also where
	// `list` and `columns` claim.
	f.claim_turn();
	f.claim_select();
	if (n <= 0) {
		state.cursor = 0;
		state.scroll = 0;
		state.rows_last = 0;
		return resp;
	}

	// Ports legacy `Enumeration::selectEncoderAction`.
	int32_t next = std::clamp<int32_t>(index, 0, n - 1);
	if (f.turn() != 0) {
		next += f.turn();
		next = wrap ? ((next % n) + n) % n : std::clamp<int32_t>(next, 0, n - 1);
	}
	resp.changed = next != index;
	index = next;
	if (resp.changed) {
		f.gate().request_repaint();
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
	render_unfocused(f, [&](OledFrame& inner) {
		list(inner, rect, windowed, std::span{window.data(), static_cast<size_t>(count)}, scroll_label);
	});
	// The press is this widget's, not the inner list's: every row it built is selectable (the
	// `selectable` default), so `list`'s own `entered` was always exactly this -- and the inner
	// frame is unfocused, so it no longer reports one at all.
	resp.entered = f.select_pressed();

	state.cursor = static_cast<int16_t>(index);
	state.scroll = static_cast<int16_t>(start);
	state.rows_last = static_cast<int16_t>(n);
	return resp;
}

Response toggle(OledFrame& f, Rect rect, bool& value, std::string_view off_label, std::string_view on_label,
                ScrollText& scroll_label) {
	Response resp{};

	f.claim_turn();
	f.claim_select();
	bool next = value;
	if (f.turn() != 0) {
		next = f.turn() > 0; // Ports legacy `Toggle::selectEncoderAction`
	}
	resp.changed = next != value;
	value = next;
	if (resp.changed) {
		f.gate().request_repaint();
	}

	// off/on order, the current one highlighted.
	const std::array<ListRow, 2> rows{ListRow{.label = off_label}, ListRow{.label = on_label}};
	ListState windowed{.cursor = static_cast<int16_t>(value ? 1 : 0), .scroll = 0, .rows_last = 2};
	render_unfocused(f, [&](OledFrame& inner) { list(inner, rect, windowed, std::span{rows}, scroll_label); });

	// This widget only renders while the toggle is the current menu item; a press there exits the
	// item without touching the value in legacy too. The value-flipping behaviour belongs to a
	// toggle rendered as a row inside a parent menu, which is a different code path. So the press
	// is reported and the scene navigates -- as with enum_value.
	resp.entered = f.select_pressed();
	return resp;
}

Response range_value(OledFrame& f, Rect rect, int32_t& lower, int32_t& upper, Range range, RangeEdge& edge,
                     RangeFormatFn format, RangeClamp clamp, bool focused) {
	Response resp{};
	resp.focused = focused;

	if (focused) {
		// `Range::horizontalEncoderAction` (`gui/menu_item/range.cpp:35-84`), with the goto lattice
		// flattened: a turn *towards* the end already selected switches editing off, any other turn
		// selects the end it points at. (Legacy's `mayEditRangeEdge` arms have no override that
		// reaches this widget -- see values.h.)
		f.claim_horizontal();
		const int8_t horizontal = f.horizontal();
		if (horizontal != 0) {
			const RangeEdge wanted = horizontal < 0 ? RangeEdge::Left : RangeEdge::Right;
			const RangeEdge next = edge == wanted ? RangeEdge::Off : wanted;
			if (next != edge) {
				edge = next;
				resp.changed = true;
				f.gate().request_repaint();
			}
		}

		f.claim_edit(0);
		f.claim_edit(1);
		const int64_t delta = edit_delta(f, coarse_step(range), 1);
		if (delta != 0) {
			const int32_t was_lower = lower;
			const int32_t was_upper = upper;
			switch (edge) {
			case RangeEdge::Left:
				if (clamp == RangeClamp::Push) {
					// `IntegerRange::selectEncoderAction` (`integer_range.cpp:39-44`).
					lower = add_clamped(lower, delta, range);
					if (upper < lower) {
						upper = lower;
					}
				}
				else {
					// `KeyRange::selectEncoderAction` (`key_range.cpp:36-39`): the *other end* is
					// the ceiling, so `Range::max` never enters this arm.
					lower = add_clamped(lower, delta, Range{range.min, upper, range.step});
				}
				break;
			case RangeEdge::Right:
				if (clamp == RangeClamp::Push) {
					// `integer_range.cpp:47-52`.
					upper = add_clamped(upper, delta, range);
					if (upper < lower) {
						lower = upper;
					}
				}
				else {
					// `key_range.cpp:41-43`.
					upper = add_clamped(upper, delta, Range{lower, range.max, range.step});
				}
				break;
			case RangeEdge::Off:
				// `integer_range.cpp:57-67` / `key_range.cpp:48-57`, which are the same statement
				// in both subclasses: with no edge selected a split range refuses to move at all,
				// and a coincident one moves as one value.
				if (lower == upper) {
					lower = add_clamped(lower, delta, range);
					upper = lower;
				}
				break;
			}
			if (lower != was_lower || upper != was_upper) {
				resp.changed = true;
				f.gate().request_repaint();
			}
		}
	}

	// `Range::drawPixelsForOled` (`gui/menu_item/range.cpp:97-131`). The frame's origin absorbs the
	// `OLED_MAIN_TOPMOST_PIXEL` term, exactly as `int_value`'s band does.
	const gfx::oled::FontId font = f.style.value_font;
	const int32_t digit_w = spacing_x_for(font);
	const int32_t digit_h = text_height_for(font);
	const Rect band{rect.x, rect.y + kValueY, rect.w, digit_h};

	// **Cleared one row taller than it is drawn, top and bottom.** The highlight below inverts
	// `digit_h + 2` rows -- legacy's own `baseY - 1 .. baseY + digitHeight + 1` (`:122-123`) -- and an
	// XOR over a row this pass did not clear toggles that row on every frame instead of settling.
	// Legacy never sees it because `OLED::main` is cleared whole before `drawPixelsForOled`; this
	// widget clears only what it owns, so what it owns has to be the *inverted* extent, not the glyph
	// band. Row `kValueY - 1` is 17, five rows below the title rule at `kTitleSeparatorY` (12), so
	// nothing above is touched. Pinned by the "moves nothing across twenty quiet ticks" example,
	// which is the one that caught it.
	f.clear(Rect{rect.x, band.y - 1, rect.w, digit_h + 2});

	// `getText(buffer, &leftLength, &rightLength, editingRangeEdge == RangeEdit::OFF)` (`:102`).
	std::array<char, kRangeTextMax> left_scratch{};
	std::array<char, kRangeTextMax> right_scratch{};
	const std::string_view left = format(lower, std::span{left_scratch});
	const bool show_just_one = edge == RangeEdge::Off && lower == upper;
	const std::string_view right = show_just_one ? std::string_view{} : format(upper, std::span{right_scratch});

	const auto left_len = static_cast<int32_t>(left.size());
	const auto right_len = static_cast<int32_t>(right.size());

	std::array<char, kRangeTextMax * 2 + 1> line{};
	size_t out = 0;
	for (const char c : left) {
		line[out++] = c;
	}
	if (right_len != 0) {
		line[out++] = '-';
		for (const char c : right) {
			line[out++] = c;
		}
	}

	// `:104-113` -- the *monospaced* width, not `measure()`'s. See values.h.
	const int32_t text_length = left_len + right_len + (right_len != 0 ? 1 : 0);
	const int32_t string_w = digit_w * text_length;
	const int32_t start_x = rect.x + ((rect.w - string_w) >> 1);
	f.glyphs(std::string_view{line.data(), out}, start_x, band.y, digit_w, digit_h, 0, rect.right());

	// `:115-130` -- one end plus a 1px margin above and below. `invertArea`'s `endY` is inclusive
	// (`OledFrame::invert` passes `bottom() - 1`), so legacy's `baseY .. baseY + digitHeight + 1`
	// is a rect `digitHeight + 2` tall starting one row above the band.
	if (edge != RangeEdge::Off) {
		int32_t highlight_x = start_x;
		int32_t highlight_w = digit_w * left_len;
		if (edge == RangeEdge::Right) {
			const int32_t string_end_x = rect.x + ((rect.w + string_w) >> 1);
			highlight_w = digit_w * right_len;
			highlight_x = string_end_x - highlight_w;
		}
		f.invert(Rect{highlight_x, band.y - 1, highlight_w, digit_h + 2});
	}
	return resp;
}

} // namespace deluge::ui::oled
