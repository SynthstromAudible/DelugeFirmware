#include "ui/oled/columns.h"

#include "gfx/oled/font.h"
#include "gfx/oled/icons.h"
#include "ui/core/metrics.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace deluge::ui::oled {

using gfx::oled::Point;
using gfx::oled::Rect;
using gfx::oled::Size;
using gfx::oled::string_width_px;

namespace {

/// The page is four slots wide: the panel divides into four column widths, and a page break
/// happens once the running total of slots would exceed four.
constexpr int32_t kPageSlots = 4;
/// A label row is one line of text tall.
constexpr int32_t kLabelHeight = kTextSpacingY;
/// Legacy's label and title buffers were bounded at 64 characters; stack arrays here, because
/// this layer allocates nothing.
constexpr size_t kTextBufferSize = 64;
/// One row of headroom above the value region -- see the note on `columns` in the header.
constexpr int32_t kSlotBleedY = 1;

/// Ports legacy `HorizontalMenu::renderColumnLabel`: draws the label with spaces removed, trimmed
/// until it clears the column edges, centred at the bottom of the box, with side ticks filling
/// the leftover width of a multi-slot column.
void column_label(OledFrame& f, const Column& col, int32_t label_y, int32_t slot_x, int32_t slot_w, bool is_selected) {
	// Strip spaces into the fixed-size buffer, matching legacy's removeSpaces.
	char buf[kTextBufferSize];
	size_t len = 0;
	for (const char c : col.label) {
		if (len == kTextBufferSize) {
			break;
		}
		if (std::isspace(static_cast<unsigned char>(c)) == 0) {
			buf[len++] = c;
		}
	}

	// If it fits, squeeze it in; otherwise chop letters until there is padding between the
	// columns. The `len == 0` guard is this layer's own: legacy's approach would underflow on a
	// slot narrower than the padding.
	int32_t label_width = 0;
	while ((label_width = string_width_px(std::string_view{buf, len}, kTextSpacingY)) + 4 >= slot_w) {
		if (len == 0) {
			break;
		}
		len--;
	}

	// NB `(slot_w - label_width) / 2` is not the same rounding as `Align::Centre`'s
	// `w / 2 - (width >> 1)` for an odd width -- hence the arithmetic here rather than a call to
	// `text`.
	const int32_t label_start_x = slot_x + (slot_w - label_width) / 2;
	f.glyphs(std::string_view{buf, len}, label_start_x, label_y, kTextSpacingX, kTextSpacingY);

	if (col.slots > 1 && !col.is_submenu && !is_selected) {
		// Small rules either side of the label when the slot is wider than it needs. The two
		// bounds checks guard against a reversed span: legacy's line draw returned early when
		// `end_x < start_x`, but `OledFrame::hline` sorts its endpoints instead, so without this
		// check a reversed span would draw a rule legacy never drew.
		const int32_t y = label_y + 4;
		if (slot_x + 4 <= label_start_x - 4) {
			f.hline(y, slot_x + 4, label_start_x - 4);
		}
		if (label_start_x + label_width + 2 <= slot_x + slot_w - 6) {
			f.hline(y, label_start_x + label_width + 2, slot_x + slot_w - 6);
		}
	}
}

/// The digits of `v`, capped at two characters, matching legacy's fixed 2-character buffer: it
/// truncates what does not fit rather than growing.
std::string_view two_digits(char (&buf)[2], int32_t v) {
	char reversed[12];
	int32_t digits = 0;
	uint32_t u = v < 0 ? 0u : static_cast<uint32_t>(v); // a page count is never negative
	do {
		reversed[digits++] = static_cast<char>('0' + (u % 10));
		u /= 10;
	} while (u != 0 && digits < 12);

	size_t len = 0;
	for (int32_t i = digits - 1; i >= 0 && len < 2; i--) {
		buf[len++] = reversed[i];
	}
	return std::string_view{buf, len};
}

/// Ports legacy `HorizontalMenu::renderPageCounters`: draws "n/N" in the top right, right to left
/// as total, separator, current -- the separator is a two-pixel-wide slanted line rather than a
/// glyph.
void page_counters(OledFrame& f, Rect rect, const Paging& paging) {
	if (paging.total_pages <= 1) {
		return;
	}
	const int32_t y = rect.y + kTitleY;
	int32_t x = rect.right() - kTextSpacingX - 1;
	char buf[2];
	f.glyphs(two_digits(buf, paging.total_pages), x, y, kTextSpacingX, kTextSpacingY);
	x -= kTextSpacingX - 1;
	f.line(x, y + kTextSpacingY - 2, x + 2, y + 1);
	x -= 6;
	f.glyphs(two_digits(buf, paging.page + 1), x, y, kTextSpacingX, kTextSpacingY);
}

/// The dotted frames drawn in the slots no column reached, matching legacy.
void placeholders(OledFrame& f, Rect rect, int32_t current_x, int32_t base_y, int32_t column_width) {
	if (column_width <= 0) {
		return;
	}
	for (int32_t k = (current_x - rect.x) / column_width; k < kPageSlots; k++) {
		const int32_t start_x = rect.x + k * column_width + 7;
		const int32_t end_x = start_x + 17;
		const int32_t start_y = base_y + 1;
		// Legacy's row is canvas-absolute (`OLED_MAIN_HEIGHT_PIXELS - 7`); the frame's height is
		// the canvas height less the bezel, so `rect.bottom() - 7` is that same row for the
		// full-frame rect, and stays on the panel for a geometry with no bezel.
		const int32_t end_y = rect.bottom() - 7;
		constexpr int32_t dot_interval = 5;

		for (int32_t x = start_x + 1; x < end_x; x += dot_interval) {
			f.pixel(x, start_y);
			f.pixel(x, end_y);
		}
		for (int32_t y = start_y + 3; y < end_y; y += dot_interval) {
			f.pixel(start_x - 2, y);
			f.pixel(end_x + 2, y);
		}
	}
}

/// Hand the value region to `Column::render` as a frame of its own.
///
/// This is like `OledFrame::region`, with one difference, built by hand for that difference
/// alone: the clip is one row taller at the top. Two of the ready-made slot renderers draw a
/// 16-row icon one row above the value box (the number knob's centred arc, and the toggle switch
/// icon), and `OledFrame` clips a blit exactly, so a region clipped tight to the value box would
/// shave the top row off. The origin is the value box's own corner either way, so nothing a
/// renderer computes moves.
void render_slot(OledFrame& f, Rect value_rect, const Column& col, bool focused) {
	if (value_rect.w <= 0 || value_rect.h <= 0) {
		return;
	}
	const Point origin{f.origin.x + value_rect.x, f.origin.y + value_rect.y};
	const Rect clip =
	    f.clip.intersect(Rect{origin.x, origin.y - kSlotBleedY, value_rect.w, value_rect.h + kSlotBleedY});
	// Input is narrowed per column *and* per input (ui/core/frame_context.h rule 3):
	//
	//   - a column that is not the focused one is granted nothing. `Column::render` is a
	//     caller-supplied function pointer and the widgets a scene reaches for there (`list`,
	//     `enum_value`, `toggle`) have no `focused` parameter at all, so the mask is their only
	//     "be inert" switch; without it, an unfocused column's enum would move on the very detent
	//     that walked the focus past it.
	//   - the focused column is granted everything *but the turn*. `columns` spends the select
	//     encoder on walking the column focus (see `columns` below), so passing the same detent
	//     on to the column it lands on would make one detent do two things: move focus and edit
	//     the value it lands on. The gold knobs, the press and the back button do go through:
	//     they are what a horizontal menu's value is edited and entered with.
	//
	// `col.render`'s own `focused` argument stays: a renderer still has to know it is the current
	// column in order to *draw* itself that way. The clock and `shift` are never masked, so every
	// column keeps animating either way.
	const InputMask granted = focused ? InputMask{.turn = false} : InputMask::none();
	OledFrame slot{{f.ctx, f.child_mask(granted)}, f.canvas, clip, origin, Size{value_rect.w, value_rect.h}, f.style};
	col.render(slot, focused, col.ctx);
}

/// The visible page: which columns it holds, and where the focused one sits in it.
struct PageWindow {
	int32_t first = 0; ///< index of the page's first column
	int32_t last = 0;  ///< one past its last
	Paging paging{};
};

/// Ports legacy `HorizontalMenu::preparePaging`. See `paging_of` in the header for the two legacy
/// quirks kept.
PageWindow window_of(std::span<const Column> cols, int32_t focused) {
	PageWindow w;
	const int32_t n = static_cast<int32_t>(cols.size());
	if (n == 0) {
		return w; // total_pages stays 0 for an empty span, matching legacy
	}
	// `paging_of` is public and total: an index outside the span reports the page its nearest
	// column is on, rather than a window spanning every column. `columns` clamps identically
	// before it calls in, so this changes nothing for the widget itself.
	focused = std::clamp<int32_t>(focused, 0, n - 1);

	int32_t page = 0;
	int32_t acquired = 0;
	int32_t total = 1;
	int32_t first = 0;
	int32_t pos = 0;
	int32_t focused_page = 0;
	int32_t focused_pos = 0;
	int32_t focused_first = 0;
	int32_t focused_last = n;
	bool found = false;
	bool closed = false;

	for (int32_t i = 0; i < n; i++) {
		const int32_t slots = std::max<int32_t>(1, cols[i].slots);
		// The `&& cols[i].relevant` guard is legacy's: an irrelevant column that does not fit is
		// appended to the current page instead of starting a new one.
		if (acquired + slots > kPageSlots && cols[i].relevant) {
			if (found && !closed) {
				focused_last = i; // the focused column's page ends here
				closed = true;
			}
			total++;
			page++;
			acquired = 0;
			first = i;
			pos = 0;
		}
		if (i == focused) {
			focused_page = page;
			focused_pos = pos;
			focused_first = first;
			found = true;
		}
		acquired += slots;
		pos++;
	}

	// Unreachable from here -- every column takes at least one slot, and the empty span returned
	// above -- but kept so the rule lives where the rest of the page maths does.
	if (total == 1 && acquired == 0) {
		total = 0;
	}

	w.first = focused_first;
	w.last = focused_last;
	w.paging = Paging{.page = static_cast<uint8_t>(focused_page),
	                  .total_pages = static_cast<uint8_t>(total),
	                  .focused_on_page = static_cast<uint8_t>(focused_pos)};
	return w;
}

/// `list`'s cursor walk over columns: one *relevant* column per detent, stopping at either end
/// rather than wrapping. Legacy moved the horizontal menu's focus with the instrument buttons and
/// gave the select encoder to the value, so there is no legacy turn behaviour to port here; this
/// reuses `list`'s walk instead.
int32_t move_focus(int32_t focused, int32_t turn, std::span<const Column> cols) {
	const int32_t n = static_cast<int32_t>(cols.size());
	int32_t last_ok = focused;
	while (turn > 0) {
		if (++focused >= n) {
			return last_ok;
		}
		if (cols[focused].relevant) {
			last_ok = focused;
			turn--;
		}
	}
	while (turn < 0) {
		if (--focused < 0) {
			return last_ok;
		}
		if (cols[focused].relevant) {
			last_ok = focused;
			turn++;
		}
	}
	return focused;
}

/// Ports legacy `HorizontalMenu::renderMenuItems`, over one page's columns.
void render_page(OledFrame& f, Rect rect, std::span<const Column> cols, const PageWindow& w, int32_t focused) {
	// Legacy's base_y is a literal `14 + OLED_MAIN_TOPMOST_PIXEL` with no panel-height branch, so
	// it is the same row on every geometry -- NOT list's height-selected kMenuBaseY48/64; see the
	// note on kHorizontalMenuBaseY in ui/core/metrics.h.
	const int32_t base_y = rect.y + kHorizontalMenuBaseY;
	const int32_t column_width = rect.w / kPageSlots;
	const int32_t page_columns = w.last - w.first;
	int32_t current_x = rect.x;

	for (int32_t i = w.first; i < w.last; i++) {
		const Column& col = cols[i];
		const bool is_selected = i == focused;
		const int32_t slots = std::max<int32_t>(1, col.slots);
		const int32_t box_width = column_width * slots;
		const int32_t label_y = base_y + kHorizontalBoxHeight - kLabelHeight;
		int32_t content_height = kHorizontalBoxHeight;

		if (col.show_label) {
			column_label(f, col, label_y, current_x, box_width, is_selected);
			content_height -= kLabelHeight;
		}

		if (!col.relevant) {
			// A dash in the title face, centred across the whole box (not the box less one
			// pixel that a value region gets).
			text(f, Rect{current_x, base_y + kHorizontalSlotYOffset, box_width, kTextTitleSizeY}, "-",
			     TextStyle{.font = gfx::oled::FontId::MetricBold9, .truncate = true});
		}
		else if (col.render != nullptr) {
			// Matches legacy's renderInHorizontalMenu value-region geometry: one pixel narrower
			// than the box.
			render_slot(f, Rect{current_x, base_y, box_width - 1, content_height}, col, is_selected);
		}

		// Highlight the focused column, unless it is the page's only column and fills the page.
		if (is_selected && (page_columns > 1 || std::max<int32_t>(1, cols[w.first].slots) < kPageSlots)) {
			if (f.style.invert_highlight) { // legacy's MenuHighlighting::FULL_INVERSION
				const bool whole_slot = !col.show_label || col.is_submenu;
				const int32_t start_y = whole_slot ? base_y - 1 : label_y;
				const int32_t end_y = whole_slot ? base_y + kHorizontalBoxHeight - 1 : label_y + kLabelHeight - 1;
				f.invert_rounded(Rect{current_x + 1, start_y, box_width - 3, end_y - start_y + 1});
			}
			else {
				// Legacy's PARTIAL_INVERSION/NO_INVERSION row is `OLED_MAIN_VISIBLE_HEIGHT + 2`
				// in canvas coordinates, i.e. three rows up from the bottom of the visible area;
				// taken frame-relative here, because the canvas-absolute reading falls off the
				// bottom of a panel with no bezel.
				f.hline(rect.bottom() - 3, current_x, current_x + box_width - 2);
			}
		}

		current_x += box_width;
	}

	placeholders(f, rect, current_x, base_y, column_width);
}

} // namespace

Paging paging_of(std::span<const Column> cols, int32_t focused) {
	return window_of(cols, focused).paging;
}

void title_paged(OledFrame& f, std::string_view s, bool paged) {
	char buf[kTextBufferSize];
	std::string_view out = s;

	if (paged) {
		// The legacy width is a uint8_t, so a title past 255px wraps and is left alone.
		const uint8_t title_width = static_cast<uint8_t>(measure(s, f.style.title_font).w);
		constexpr int32_t paging_width = kTextSpacingX * 2 + 10;

		if (title_width > f.bounds().w - paging_width) {
			// Split on spaces. Only the count and the first two words matter, so the walk stops
			// once a third word appears.
			std::string_view words[2];
			size_t count = 0;
			size_t start = 0;
			while (start < s.size() && count < 3) {
				while (start < s.size() && s[start] == ' ') {
					start++;
				}
				if (start >= s.size()) {
					break;
				}
				size_t end = start;
				while (end < s.size() && s[end] != ' ') {
					end++;
				}
				if (count < 2) {
					words[count] = s.substr(start, end - start);
				}
				count++;
				start = end;
			}

			if (count == 2 && !words[0].empty()) {
				size_t len = 0;
				buf[len++] = words[0][0];
				buf[len++] = '.';
				buf[len++] = ' ';
				for (const char c : words[1]) {
					if (len == kTextBufferSize) {
						break;
					}
					buf[len++] = c;
				}
				out = std::string_view{buf, len};
			}
		}
	}

	title(f, out, false); // matches legacy drawScreenTitle(title, false): no separator rule
}

Response columns(OledFrame& f, Rect rect, ColumnsState& state, std::span<const Column> cols,
                 std::string_view title_text) {
	Response resp{};
	const int32_t n = static_cast<int32_t>(cols.size());

	// This control owns the select encoder: it walks the column focus with it, and `render_slot`
	// grants the focused column every input except that one. The *press* is only ours when it
	// enters a submenu -- on a value column it passes through the slot's mask to the widget in
	// the slot, which claims it there -- so that claim sits with the `entered` computation below.
	f.claim_turn();
	int32_t focused = std::clamp<int32_t>(state.focused, 0, std::max<int32_t>(0, n - 1));
	if (f.turn() != 0 && n > 0) {
		focused = move_focus(focused, f.turn(), cols);
	}
	// "focus moved" includes the clamp: a scene whose column list shrank under a stale
	// `state.focused` gets one `changed` frame as the index is pulled back into range with no
	// turn. That is the same bound `list` puts on its cursor.
	resp.changed = focused != state.focused;
	state.focused = static_cast<int8_t>(focused);
	if (resp.changed) {
		f.gate().request_repaint(); // navigation renders before the shell blocks
	}

	const PageWindow w = window_of(cols, focused);
	state.page = w.paging.page;
	// The press legacy routed to handleItemAction -> enterSubmenu. Claimed only for a submenu
	// column, which is the only shape in which `columns` consumes it.
	const bool owns_press = n > 0 && cols[focused].is_submenu && cols[focused].relevant;
	if (owns_press) {
		f.claim_select();
	}
	resp.entered = owns_press && f.select_pressed();

	// Legacy got a cleared panel from renderUIsForOled before renderOLED ran; an immediate-mode
	// widget owns its own area instead, exactly as `list` clears its row bands.
	f.clear(rect);

	if (!title_text.empty()) {
		// Drawn through a region so the title lands at the rect's corner and measures itself
		// against the rect's width; `title` itself has no rect parameter.
		f.region(rect, [&](OledFrame& sub) { title_paged(sub, title_text, w.paging.total_pages > 1); });
	}
	page_counters(f, rect, w.paging);
	render_page(f, rect, cols, w, focused);
	return resp;
}

void meter_column(OledFrame& slot_frame, bool /*focused*/, void* ctx) {
	const auto* data = static_cast<const MeterColumn*>(ctx);
	if (data == nullptr) {
		return;
	}
	meter(slot_frame, slot_frame.bounds(), data->hint, data->norm, data->raw, data->raw_max);
}

void text_column(OledFrame& slot_frame, bool /*focused*/, void* ctx) {
	const auto* data = static_cast<const TextColumn*>(ctx);
	if (data == nullptr) {
		return;
	}
	// Matches legacy's drawStringCentered call: `TextStyle::truncate` over the region.
	const Rect b = slot_frame.bounds();
	text(slot_frame, Rect{b.x, b.y + kHorizontalSlotYOffset, b.w, kTextSpacingY}, data->value,
	     TextStyle{.font = slot_frame.style.row_font, .truncate = true});
}

void submenu_column(OledFrame& slot_frame, bool /*focused*/, void* /*ctx*/) {
	// The bold arrow, centred with legacy's own off-by-one nudge left.
	const Rect b = slot_frame.bounds();
	const int32_t arrow_y = b.y + kHorizontalSlotYOffset;
	const int32_t arrow_x = b.x + (b.w - kSubmenuIconSpacingX) / 2 - 1;
	slot_frame.icon(gfx::oled::icons::submenuArrowBold, arrow_x, arrow_y);
}

void toggle_column(OledFrame& slot_frame, bool /*focused*/, void* ctx) {
	const auto* data = static_cast<const ToggleColumn*>(ctx);
	if (data == nullptr) {
		return;
	}
	// Matches legacy's drawIconCentered call, one row above the region.
	const Rect b = slot_frame.bounds();
	slot_frame.icon_centered(data->on ? gfx::oled::icons::switcherOn : gfx::oled::icons::switcherOff, b.x, b.w,
	                         b.y - 1);
}

} // namespace deluge::ui::oled
