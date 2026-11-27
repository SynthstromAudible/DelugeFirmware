#include "ui/widgets/oled/timeline_strips.h"

#include <algorithm>

namespace deluge::ui::widgets::oled {

namespace {

/// A dotted outline of the rect from (x0, y0) to (x1, y1), inclusive, dots on even offsets from (x0, y0).
void dotted_box(OledFrame& f, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
	for (int32_t x = x0; x <= x1; x += 2) {
		f.pixel(x, y0);
		f.pixel(x, y1);
	}
	for (int32_t y = y0; y <= y1; y += 2) {
		f.pixel(x0, y);
		f.pixel(x1, y);
	}
}

/// The column of @p v out of @p total across @p r.
int32_t column(const gfx::oled::Rect& r, int64_t v, int64_t total) {
	return r.x + static_cast<int32_t>(v * r.w / std::max<int64_t>(1, total));
}

/// Columns [x0, x1] of a span, at least one column, clipped to @p r; false when nothing is left.
bool clip_span(const gfx::oled::Rect& r, int32_t& x0, int32_t& x1) {
	x1 = std::max(x1, x0);
	x0 = std::max(x0, r.x);
	x1 = std::min(x1, r.x + r.w - 1);
	return x0 <= x1;
}

} // namespace

void window_marks(OledFrame& f, gfx::oled::Rect r, int32_t x, int32_t w, int32_t playhead_x) {
	if (w > 0) {
		dotted_box(f, r.x + x, r.y, r.x + x + w - 1, r.y + r.h - 1);
	}
	if (playhead_x >= 0 && playhead_x < r.w) {
		f.invert({r.x + playhead_x, r.y, 1, r.h});
	}
}

void overview(OledFrame& f, gfx::oled::Rect r, int32_t lanes, int32_t units, std::span<const LaneSpan> spans,
              int32_t window_start, int32_t window_len, int32_t playhead) {
	const int32_t lane_h = r.h / std::max(1, lanes);
	const bool any_selected = std::ranges::any_of(spans, [](const LaneSpan& s) { return s.selected; });
	for (const LaneSpan& s : spans) {
		int32_t x0 = column(r, s.start, units);
		// two before the end's column: a blank column between back-to-back instances
		int32_t x1 = column(r, static_cast<int64_t>(s.start) + s.length, units) - 2;
		if (s.lane < 0 || s.lane >= lanes || !clip_span(r, x0, x1)) {
			continue;
		}
		const int32_t y0 = r.y + s.lane * lane_h;
		const int32_t y1 = y0 + lane_h - 2;
		if (s.muted) {
			dotted_box(f, x0, y0, x1, y1);
		}
		else if (any_selected && !s.selected) {
			f.rect({x0, y0, x1 - x0 + 1, y1 - y0 + 1});
		}
		else {
			for (int32_t y = y0; y <= y1; y++) {
				f.hline(y, x0, x1);
			}
		}
	}
	const int32_t wx = column(r, window_start, units) - r.x;
	const int32_t ww = column(r, static_cast<int64_t>(window_start) + window_len, units) - r.x - wx;
	window_marks(f, r, wx, window_len > 0 ? ww : 0, playhead >= 0 ? column(r, playhead, units) - r.x : -1);
}

void note_strip(OledFrame& f, gfx::oled::Rect r, int32_t length_ticks, std::span<const StripNote> notes, uint8_t low,
                uint8_t high, int32_t window_start, int32_t window_len, int32_t playhead) {
	for (const StripNote& n : notes) {
		int32_t x0 = column(r, n.start, length_ticks);
		int32_t x1 = column(r, static_cast<int64_t>(n.start) + n.length, length_ticks) - 1;
		if (!clip_span(r, x0, x1)) {
			continue;
		}
		int32_t y = r.y + (r.h - 1) / 2;
		if (high > low) {
			y = r.y + (static_cast<int32_t>(high) - n.pitch) * (r.h - 1) / (high - low);
		}
		y = std::clamp(y, r.y, r.y + r.h - 1);
		f.hline(y, x0, x1);
	}
	const int32_t wx = column(r, window_start, length_ticks) - r.x;
	const int32_t ww = column(r, static_cast<int64_t>(window_start) + window_len, length_ticks) - r.x - wx;
	window_marks(f, r, wx, window_len > 0 ? ww : 0, playhead >= 0 ? column(r, playhead, length_ticks) - r.x : -1);
}

} // namespace deluge::ui::widgets::oled
