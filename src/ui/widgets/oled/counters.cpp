#include "ui/widgets/oled/counters.h"

#include <algorithm>
#include <cmath>

namespace deluge::ui::widgets::oled {

void amount_line(OledFrame& f, int32_t y, int32_t x0, int32_t x1, float v) {
	const int32_t xv = x0 + static_cast<int32_t>(std::lround(std::clamp(v, 0.f, 1.f) * static_cast<float>(x1 - x0)));
	if (xv > x0) {
		f.hline(y, x0, xv);
	}
	for (int32_t x = xv + 2; x <= x1; x += 2) {
		f.pixel(x, y);
	}
}

void level_lines(OledFrame& f, gfx::oled::Rect r, float left, float right) {
	amount_line(f, r.y, r.x, r.x + r.w - 1, left);
	amount_line(f, r.y + 1, r.x, r.x + r.w - 1, right);
}

void beat_boxes(OledFrame& f, gfx::oled::Rect r, int32_t n, int32_t filled, int32_t box_w, int32_t gap) {
	if (n <= 0) {
		return;
	}
	filled = std::clamp(filled, 0, n);
	const int32_t total = n * box_w + (n - 1) * gap;
	const int32_t x0 = r.x + (r.w - total) / 2;
	for (int32_t i = 0; i < n; i++) {
		const gfx::oled::Rect box{x0 + i * (box_w + gap), r.y, box_w, r.h};
		if (i < filled) {
			for (int32_t y = box.y; y < box.y + box.h; y++) {
				f.hline(y, box.x, box.x + box.w - 1);
			}
		}
		else {
			f.rect(box);
		}
	}
}

void position_bar(OledFrame& f, gfx::oled::Rect r, int32_t total, int32_t start, int32_t len) {
	const int32_t mid = r.y + r.h / 2;
	for (int32_t x = r.x; x < r.x + r.w; x += 2) {
		f.pixel(x, mid);
	}
	const int64_t whole = std::max(1, total);
	const int32_t x0 = std::max(r.x, r.x + static_cast<int32_t>(int64_t{start} * r.w / whole));
	const int32_t x1 = std::min(r.x + r.w - 1, r.x + static_cast<int32_t>((int64_t{start} + len) * r.w / whole) - 1);
	// at least one column while the window has any length, so it never vanishes at a high zoom
	const int32_t x1_shown = len > 0 ? std::max(x1, std::min(x0, r.x + r.w - 1)) : x1;
	for (int32_t y = r.y; y < r.y + r.h && x0 <= x1_shown; y++) {
		f.hline(y, x0, x1_shown);
	}
}

} // namespace deluge::ui::widgets::oled
