#include "ui/oled/list.h"

#include "ui/core/metrics.h"
#include "ui/oled/scroll_text.h"
#include "ui/oled/text.h"

#include <algorithm>

namespace deluge::ui::oled {

using gfx::oled::Rect;

namespace {

/// Legacy `getSubmenuItemTypeRenderLength`'s "spacing before" (menu_item.h:275, 278): the gap
/// kept between the label and whatever is rendered to its right, so the two never touch and the
/// label still has room to scroll.
constexpr int32_t kIconGapX = 4;

} // namespace

int32_t list_visible_rows(const OledFrame& f, Rect rect) {
	// style.menu_rows is legacy's OLED_HEIGHT_CHARS - 1; the rect.h term is this layer's own
	// bound, so a rect too short for that many rows cannot draw past its own bottom edge.
	// style.row_height is the row pitch (kTextSpacingY on both shipped profiles, pinned by the
	// static_asserts in ui/core/style.h), so a taller-rowed profile shows correspondingly fewer.
	return std::clamp<int32_t>(rect.h / std::max<int32_t>(1, f.style.row_height), 1,
	                           std::max<int32_t>(1, f.style.menu_rows));
}

/// `Selection::drawPixelsForOled`, selection.cpp:9-29: the index of the first visible row.
///
/// Ideally the cursor sits in the middle of the window (rounding down for even counts), but
/// near either end of the list it cannot, so `pos` -- its offset within the window -- is pulled
/// in. Note this is `Selection`'s form, not `Submenu`'s: `Submenu::drawPixelsForOled`
/// (submenu.cpp:82-84) additionally re-tightens `tail` in the first branch, which only matters
/// for a list shorter than the window, where both forms produce the same `cursor - pos` anyway.
int32_t list_window_start(int32_t cursor, int32_t n, int32_t visible) {
	const int32_t before = std::max<int32_t>(0, cursor - visible); // earliest row that might show
	const int32_t n_before = cursor - before;
	const int32_t after = std::min<int32_t>(cursor + visible, n); // first row offscreen
	const int32_t n_after = after - cursor;

	int32_t pos = (visible - 1) / 2;
	int32_t tail = visible - pos;
	if (n_before < pos) {
		pos = n_before;
	}
	else if (n_after < tail) {
		tail = n_after;
		pos = std::min<int32_t>(visible - tail, n_before);
	}
	return cursor - pos;
}

namespace {

/// `Submenu::selectEncoderAction`, submenu.cpp:132-176: walk `turn` detents, consuming one only
/// when the row landed on is selectable (legacy: relevant), and revert to the last selectable
/// row rather than wrapping when the walk runs off an end. With every row selectable this is
/// exactly `clamp(cursor + turn, 0, n - 1)`.
int32_t move_cursor(int32_t cursor, int32_t turn, std::span<const ListRow> rows, int32_t n) {
	int32_t last_ok = cursor;
	while (turn > 0) {
		if (++cursor >= n) {
			return last_ok;
		}
		if (rows[cursor].selectable) {
			last_ok = cursor;
			turn--;
		}
	}
	while (turn < 0) {
		if (--cursor < 0) {
			return last_ok;
		}
		if (rows[cursor].selectable) {
			last_ok = cursor;
			turn++;
		}
	}
	return cursor;
}

} // namespace

Rect menu_rect(const OledFrame& f) {
	// menu_item.cpp:50 -- `(OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14`, now a profile value
	// (`Style::menu_base_y`) rather than a read of the framebuffer's height. Frame-local, because
	// the `+= OLED_MAIN_TOPMOST_PIXEL` on the next legacy line is already in the frame's origin.
	const int32_t base = f.style.menu_base_y;
	const Rect b = f.bounds();
	return Rect{b.x, base, b.w, b.h - base};
}

Response list(OledFrame& f, Rect rect, ListState& state, std::span<const ListRow> rows, ScrollText& scroll_label,
              bool allow_motion) {
	Response resp{};
	const int32_t n = static_cast<int32_t>(rows.size());
	const int32_t visible = list_visible_rows(f, rect);

	// Two-pass, count then draw (SDK menu.rs:346-415): `n` above is already the count of the
	// span `list` was handed *this* frame, so the cursor clamps and walks against `n` directly --
	// never against `state.rows_last`, which is last frame's count and would lag a shrinking or
	// growing list by one frame. The two inputs this control owns are claimed on every focused
	// frame rather than only on the frames a detent or a press arrives: ownership is a property
	// of what is on screen, not of what the user did this millisecond (ui/core/frame_context.h
	// rule 2).
	int32_t cursor = std::clamp<int32_t>(state.cursor, 0, std::max<int32_t>(0, n - 1));
	f.claim_turn();
	f.claim_select();
	if (f.turn() != 0 && n > 0) {
		cursor = move_cursor(cursor, f.turn(), rows, n);
	}
	resp.changed = cursor != state.cursor;
	state.cursor = static_cast<int16_t>(cursor);
	// A pure cache from here on: republished for a caller that wants last frame's count (e.g.
	// `enum_value`'s windowed inner `list` call sizes itself from its own copy of this field),
	// never read back by `list` itself.
	state.rows_last = static_cast<int16_t>(n);
	if (resp.changed) {
		f.gate().request_repaint();
	}
	if (n == 0) {
		state.scroll = 0;
		return resp;
	}

	const int32_t start = list_window_start(cursor, n, visible);
	state.scroll = static_cast<int16_t>(start);
	resp.entered = f.select_pressed() && rows[cursor].selectable;

	const gfx::oled::FontId font = f.style.row_font;
	const int32_t row_h = f.style.row_height;                       // kTextSpacingY, per style.h
	const int32_t label_x = rect.x + f.style.inset_x;               // menu_item.cpp:58's kTextSpacingX
	const int32_t icon_x = rect.right() - kSubmenuIconSpacingX - 3; // menu_item.h:280

	for (int32_t o = 0; o < visible && start + o < n; o++) {
		const ListRow& row = rows[start + o];
		const int32_t y = rect.y + o * row_h; // menu_item.cpp:56
		f.clear(Rect{rect.x, y, rect.w, row_h});

		// menu_item.h:278 -- an icon costs the label `4 + kSubmenuIconSpacingX + 3` px.
		int32_t content_end =
		    row.icon != nullptr ? rect.right() - (kIconGapX + kSubmenuIconSpacingX + 3) : rect.right();
		if (!row.value.empty()) {
			// Right-aligned against the content edge, but never starting left of the label
			// origin and never leaving the label box negative. Unlike everything around it
			// this column has no legacy counterpart, so it gets an explicit bound rather than
			// inheriting drawStringAlignRight's unbounded `rightPos - stringWidth`.
			const int32_t value_w = measure(row.value, font).w;
			if (content_end > label_x) {
				f.glyphs(row.value, std::max<int32_t>(label_x, content_end - value_w), y, spacing_x_for(font),
				         text_height_for(font), 0, content_end);
			}
			content_end = std::max<int32_t>(label_x, content_end - (value_w + kIconGapX));
		}

		const bool selected = start + o == cursor;
		const Rect label_box{label_x, y, content_end - label_x, row_h};
		if (selected) {
			// setupSideScroller(0, name, kTextSpacingX, endX, yPixel, yPixel + 8, ..., true)
			// -- menu_item.cpp:62 / submenu.cpp:124 -- collapsed into the draw, since this
			// layer re-renders every frame instead of arming a timer. `highlight` stays false
			// and the row inversion below covers the whole row: that is the order legacy's
			// full redraw uses (drawString, then invertLeftEdgeForMenuHighlighting), and it is
			// what keeps the rounded corners. Legacy's scroller-tick path inverts only its own
			// box (oled.cpp:996-997), which erases the row's right-hand corners until the next
			// full redraw; that transient is not reproduced. The +4 clear padding is legacy's
			// non-inverting branch (oled.cpp:984-989).
			scroll_text(f, label_box, row.label, font, scroll_label, allow_motion, false,
			            f.style.invert_highlight ? 0 : kIconGapX);
		}
		else {
			// menu_item.cpp:58 / submenu.cpp:114 -- drawString at kTextSpacingX, clipped to
			// content_end.
			text(f, label_box, row.label, TextStyle{.font = font});
		}
		if (row.icon != nullptr) {
			f.icon(*row.icon, icon_x, y); // menu_item.cpp:74 / selection.cpp:41-56
		}
		if (!selected) {
			continue;
		}
		// invertLeftEdgeForMenuHighlighting(0, width, yPixel, yPixel + 8), canvas.cpp:26-33:
		// invertAreaRounded over the full row when inversion is on -- rounded per style.rounded,
		// because legacy routes it through Canvas::invertAreaRounded(..., roundedCornersEnabled)
		// (oled_canvas/canvas.h:136-138) -- otherwise just the rule down the left edge.
		if (f.style.invert_highlight) {
			f.invert_rounded(Rect{rect.x, y, rect.w, 9});
		}
		else {
			f.vline(rect.x, y, y + 8);
		}
	}
	return resp;
}

void scrollbar(OledFrame& f, Rect rect, int32_t first, int32_t visible, int32_t total) {
	if (rect.w <= 0 || rect.h <= 0 || total <= 0) {
		return;
	}
	f.vline(rect.right() - 1, rect.y, rect.bottom() - 1);

	// The thumb is `style.scrollbar_w` wide, right-aligned in the rect and never wider than it --
	// so the shipped profiles' 4 over the 4-wide rects every caller uses is the whole rect, and a
	// profile with a narrower bar draws a narrower thumb against the same track.
	const int32_t thumb_w = std::clamp<int32_t>(f.style.scrollbar_w, 1, rect.w);
	const int32_t thumb_h = std::max<int32_t>(2, rect.h * std::clamp(visible, 0, total) / total);
	const int32_t thumb_y = rect.y + rect.h * std::clamp(first, 0, total) / total;
	const int32_t last = std::min<int32_t>(thumb_y + thumb_h - 1, rect.bottom() - 1);
	for (int32_t y = thumb_y; y <= last; y++) {
		f.hline(y, rect.right() - thumb_w, rect.right() - 1);
	}
}

} // namespace deluge::ui::oled
