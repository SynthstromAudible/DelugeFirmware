#include "ui/widgets/oled/timeline_strips.h"

#include <algorithm>
#include <cmath>

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

void quantize_graph(OledFrame& f, gfx::oled::Rect r, int32_t length_ticks, int32_t grid_ticks,
                    std::span<const int32_t> note_ticks, float amount) {
	amount = std::clamp(amount, 0.f, 1.f);
	const int32_t right = r.x + r.w - 1;
	const int32_t tick_h = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(r.h) / 5.f)));
	const int32_t block_h = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(r.h) / 4.f)));
	const int32_t block_top = r.y + r.h - block_h;
	const auto col = [&](int64_t t) { return std::min(right, column(r, t, length_ticks)); };
	if (grid_ticks > 0) {
		for (int64_t g = 0; g <= length_ticks; g += grid_ticks) {
			for (int32_t y = block_top; y < r.y + r.h; y += 2) {
				f.pixel(col(g), y);
			}
		}
	}
	for (const int32_t t : note_ticks) {
		const int32_t x0 = col(t);
		int32_t x1 = x0;
		if (grid_ticks > 0) {
			const int64_t snap = (int64_t{t} + grid_ticks / 2) / grid_ticks * grid_ticks;
			x1 = x0 + static_cast<int32_t>(std::lround(static_cast<float>(col(snap) - x0) * amount));
		}
		f.vline(x0, r.y, r.y + tick_h - 1);
		f.line(x0, r.y + tick_h, x1, block_top - 1);
		for (int32_t y = block_top; y < r.y + r.h; y++) {
			f.hline(y, std::max(r.x, x1 - 1), std::min(right, x1 + 1));
		}
	}
}

void automation_lane(OledFrame& f, gfx::oled::Rect r, int32_t length_ticks, std::span<const LaneNode> nodes,
                     int32_t empty_value, int32_t window_start, int32_t window_len, int32_t playhead) {
	const int32_t right = r.x + r.w - 1;
	const auto row = [&](int32_t v) { return r.y + (r.h - 1) - std::clamp(v, 0, 127) * (r.h - 1) / 127; };
	const auto col = [&](int64_t t) { return std::clamp(column(r, t, length_ticks), r.x, right); };
	if (nodes.empty()) {
		for (int32_t x = r.x; x <= right; x += 2) {
			f.pixel(x, row(empty_value));
		}
	}
	else {
		f.hline(row(nodes.front().value), r.x, col(nodes.front().pos));
		for (size_t i = 1; i < nodes.size(); i++) {
			const int32_t x0 = col(nodes[i - 1].pos);
			const int32_t y0 = row(nodes[i - 1].value);
			const int32_t x1 = col(nodes[i].pos);
			const int32_t y1 = row(nodes[i].value);
			if (nodes[i].interpolate) {
				f.line(x0, y0, x1, y1);
			}
			else {
				f.hline(y0, x0, x1);
				f.vline(x1, std::min(y0, y1), std::max(y0, y1));
			}
		}
		f.hline(row(nodes.back().value), col(nodes.back().pos), right);
	}
	const int32_t wx = column(r, window_start, length_ticks) - r.x;
	const int32_t ww = column(r, static_cast<int64_t>(window_start) + window_len, length_ticks) - r.x - wx;
	window_marks(f, r, wx, window_len > 0 ? ww : 0, playhead >= 0 ? column(r, playhead, length_ticks) - r.x : -1);
}

} // namespace deluge::ui::widgets::oled
