#include "ui/widgets/oled/scope.h"

#include <algorithm>

namespace deluge::ui::widgets::oled {

void scope(OledFrame& f, gfx::oled::Rect r, std::span<const int8_t> samples) {
	const int32_t half = r.h / 2;
	const int32_t cy = r.y + half;
	for (int32_t x = r.x; x < r.x + r.w; x += 2) {
		f.pixel(x, cy);
	}
	const int32_t n = std::min(static_cast<int32_t>(samples.size()), r.w);
	int32_t prev = 0;
	for (int32_t i = 0; i < n; i++) {
		const int32_t y = std::clamp(cy - samples[static_cast<size_t>(i)] * half / 128, r.y, r.y + r.h - 1);
		if (i == 0) {
			f.pixel(r.x, y);
		}
		else {
			f.line(r.x + i - 1, prev, r.x + i, y);
		}
		prev = y;
	}
}

void scope_xy(OledFrame& f, gfx::oled::Rect r, std::span<const int8_t> left, std::span<const int8_t> right) {
	const int32_t w = std::min(r.w, 2 * r.h);
	const int32_t h = r.h;
	const int32_t bx = r.x + (r.w - w) / 2;
	const int32_t cx = bx + w / 2;
	const int32_t cy = r.y + h / 2;
	for (int32_t x = bx; x < bx + w; x += 2) {
		f.pixel(x, cy);
	}
	for (int32_t y = r.y; y < r.y + h; y += 2) {
		f.pixel(cx, y);
	}
	const size_t n = std::min(left.size(), right.size());
	int32_t px = 0;
	int32_t py = 0;
	for (size_t i = 0; i < n; i++) {
		const int32_t x = std::clamp(cx + left[i] * (w / 2) / 128, bx, bx + w - 1);
		const int32_t y = std::clamp(cy - right[i] * (h / 2) / 128, r.y, r.y + h - 1);
		if (i == 0) {
			f.pixel(x, y);
		}
		else {
			f.line(px, py, x, y);
		}
		px = x;
		py = y;
	}
}

} // namespace deluge::ui::widgets::oled
