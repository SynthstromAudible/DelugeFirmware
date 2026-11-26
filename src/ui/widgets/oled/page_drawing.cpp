#include "ui/widgets/oled/page_drawing.h"

#include "ui/gfx/oled/canvas.h"

#include <algorithm>
#include <cstdlib>

namespace deluge::ui::widgets::oled {

namespace {

/// @brief What `fill_pattern` needs: the canvas and the fill's bottom row, both canvas-space.
struct FillPattern {
	gfx::oled::Canvas* canvas;
	int32_t end_y;
	gfx::oled::Point last{-1, -1};
};

void fill_pattern(gfx::oled::Point point, void* ctx) {
	auto* fill = static_cast<FillPattern*>(ctx);
	if (fill->last.x != point.x && point.x % 3 == 0) {
		for (int32_t y = point.y; y <= fill->end_y; y++) {
			if (y % 3 == 1) {
				fill->canvas->draw_pixel(point.x, y);
			}
		}
	}
	fill->last = point;
}

} // namespace

PageBody page_body(const OledFrame& f) {
	constexpr int32_t kPaddingX = 4;
	return {
	    .left = kPaddingX - 1, .right = f.size.w - kPaddingX, .top = f.style.title_height + 3, .bottom = f.size.h - 6};
}

void curve(OledFrame& f, const PageBody& b, float x0, float x1, LevelFn level, const void* ctx, int32_t steps) {
	int32_t last_x = static_cast<int32_t>(x0);
	int32_t last_y = b.y_at(level(0.f, ctx));
	for (int32_t i = 1; i <= steps; i++) {
		const float t = static_cast<float>(i) / static_cast<float>(steps);
		const auto x = static_cast<int32_t>(std::round(x0 + (x1 - x0) * t));
		const int32_t y = b.y_at(level(t, ctx));
		f.line(last_x, last_y, x, y);
		last_x = x;
		last_y = y;
	}
}

void polyline(OledFrame& f, std::span<const gfx::oled::Point> pts) {
	for (size_t i = 1; i < pts.size(); i++) {
		f.line(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y);
	}
}

void guide_v(OledFrame& f, const PageBody& b, int32_t x, int32_t from_y) {
	for (int32_t y = b.top - 1; y <= b.bottom + 1; y += 4) {
		if (y >= from_y - 1) {
			f.pixel(x, y);
		}
	}
}

void guide_h(OledFrame& f, int32_t y, int32_t x_from, int32_t x_to, std::span<const int32_t> skip_x) {
	for (int32_t x = x_from; x <= x_to; x++) {
		if (x % 6 != 3) {
			continue;
		}
		if (std::ranges::any_of(skip_x, [x](int32_t s) { return std::abs(x - s) <= 1; })) {
			continue;
		}
		f.pixel(x, y);
	}
}

void fill_line(OledFrame& f, gfx::oled::Point a, gfx::oled::Point b, int32_t fill_bottom, int32_t min_x,
               int32_t max_x) {
	ClipScope scope{f.canvas, f.clip};
	const int32_t ox = f.origin.x;
	const int32_t oy = f.origin.y;
	FillPattern fill{.canvas = &f.canvas, .end_y = fill_bottom + oy};
	f.canvas.draw_line(a.x + ox, a.y + oy, b.x + ox, b.y + oy,
	                   {.min_x = static_cast<uint8_t>(min_x + ox),
	                    .max_x = static_cast<uint8_t>(max_x + ox),
	                    .point_callback = fill_pattern,
	                    .callback_ctx = &fill});
}

void Handles::draw(int32_t x, int32_t y, bool focused) {
	if (!focused && x == focused_x && y == focused_y) {
		return;
	}
	f.clear({x - 1, y - 1, 3, 3});
	if (focused) {
		focused_x = x;
		focused_y = y;
		f.invert({x - 1, y - 1, 3, 3});
	}
	f.rect({x - 2, y - 2, 5, 5});
}

} // namespace deluge::ui::widgets::oled
