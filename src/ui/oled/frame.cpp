#include "ui/oled/frame.h"

#include <algorithm>
#include <cstdlib>

namespace deluge::ui::oled {

using gfx::oled::kEdge;
using gfx::oled::Point;
using gfx::oled::Rect;

OledFrame root_frame(gfx::oled::Canvas& canvas, const Style& style, const InputSnapshot& input, RepaintGate& gate) {
	const gfx::oled::Geometry g = canvas.geometry();
	return OledFrame{canvas,
	                 Rect{0, g.visible_top, g.width, g.visible_height()},
	                 Point{0, g.visible_top},
	                 gfx::oled::Size{g.width, g.visible_height()},
	                 style,
	                 input,
	                 gate};
}

namespace {
/// A frame whose clip collapsed to nothing draws nothing at all -- worth one early check
/// rather than relying on every span/rect computation below to come out empty.
bool empty(const Rect& clip) {
	return clip.w <= 0 || clip.h <= 0;
}

/// Local rect -> canvas rect, clipped. `w`/`h` of 0 means nothing survived. A free function
/// rather than a member so OledFrame stays an aggregate (see the note at the end of frame.h).
Rect to_canvas(const OledFrame& f, Rect r) {
	return f.clip.intersect(Rect{f.origin.x + r.x, f.origin.y + r.y, r.w, r.h});
}
} // namespace

void OledFrame::pixel(int32_t x, int32_t y) {
	const int32_t cx = origin.x + x;
	const int32_t cy = origin.y + y;
	if (clip.contains(Point{cx, cy})) {
		canvas.draw_pixel(cx, cy);
	}
}

void OledFrame::hline(int32_t y, int32_t x0, int32_t x1) {
	if (empty(clip)) {
		return;
	}
	const int32_t cy = origin.y + y;
	if (cy < clip.y || cy >= clip.bottom()) {
		return;
	}
	int32_t a = std::max(origin.x + std::min(x0, x1), clip.x);
	int32_t b = std::min(origin.x + std::max(x0, x1), clip.right() - 1);
	if (a <= b) {
		canvas.draw_horizontal_line(cy, a, b);
	}
}

void OledFrame::vline(int32_t x, int32_t y0, int32_t y1) {
	if (empty(clip)) {
		return;
	}
	const int32_t cx = origin.x + x;
	if (cx < clip.x || cx >= clip.right()) {
		return;
	}
	int32_t a = std::max(origin.y + std::min(y0, y1), clip.y);
	int32_t b = std::min(origin.y + std::max(y0, y1), clip.bottom() - 1);
	if (a <= b) {
		canvas.draw_vertical_line(cx, a, b);
	}
}

void OledFrame::line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool thick) {
	// The same Bresenham walk as Canvas::draw_line (gfx/oled/canvas.cpp), plotting through
	// this frame's clipped pixel() instead of the canvas's own draw_pixel. Clipping the two
	// endpoints and handing the shortened segment to gfx would rasterise a *different* set of
	// pixels (the error term restarts), so the walk is repeated here to keep a clipped line
	// a strict subset of the unclipped one.
	const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
	if (steep) {
		std::swap(x0, y0);
		std::swap(x1, y1);
	}
	if (x0 > x1) {
		std::swap(x0, x1);
		std::swap(y0, y1);
	}

	const int32_t dx = x1 - x0;
	const int32_t dy = std::abs(y1 - y0);
	int32_t error = dx / 2;
	int32_t y = y0;
	const int32_t y_step = y0 < y1 ? 1 : -1;

	for (int32_t x = x0; x <= x1; x++) {
		const int32_t actual_x = steep ? y : x;
		const int32_t actual_y = steep ? x : y;

		pixel(actual_x, actual_y);
		if (thick) {
			pixel(steep ? actual_x + 1 : actual_x, steep ? actual_y : actual_y - 1);
		}

		error -= dy;
		if (error < 0) {
			y += y_step;
			error += dx;
		}
	}
}

void OledFrame::rect(Rect r) {
	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	// Canvas::draw_rectangle's own decomposition: full-height sides, then the top and bottom
	// spans between them.
	vline(r.x, r.y, r.bottom() - 1);
	vline(r.right() - 1, r.y, r.bottom() - 1);
	hline(r.y, r.x + 1, r.right() - 2);
	hline(r.bottom() - 1, r.x + 1, r.right() - 2);
}

void OledFrame::rect_rounded(Rect r, gfx::oled::BorderRadius radius) {
	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	if (!style.rounded) {
		// Canvas::draw_rectangle_rounded's own `!rounded` arm is a plain rectangle, and `rect`
		// above is already that decomposition.
		rect(r);
		return;
	}

	// Canvas::draw_rectangle_rounded's decomposition (gfx/oled/canvas.cpp), replayed through this
	// frame's clipped primitives rather than handed to the canvas whole. A "rounded corner" is not
	// an arc: it is the four edges inset by the radius, plus -- at radius 2 -- one pixel diagonally
	// in from each corner. Every one of those is a span or a point, so every one of them clips
	// exactly, and a box that hangs off the region now draws the part of its border that is
	// inside it instead of nothing at all.
	const int32_t min_x = r.x;
	const int32_t min_y = r.y;
	const int32_t max_x = r.right() - 1;
	const int32_t max_y = r.bottom() - 1;
	const int32_t radius_pixels = (radius == gfx::oled::BorderRadius::Small) ? 1 : 2;

	// The canvas's own line routines return early on a reversed range (`end < start`, canvas.cpp),
	// while `hline`/`vline` above normalise one. For a box narrower or shorter than twice the
	// radius the inset runs *are* reversed, so the canvas's guard is reproduced here to keep this
	// pixel-identical to the call it replaces.
	if (min_y + radius_pixels <= max_y - radius_pixels) {
		vline(min_x, min_y + radius_pixels, max_y - radius_pixels);
		vline(max_x, min_y + radius_pixels, max_y - radius_pixels);
	}
	if (min_x + radius_pixels <= max_x - radius_pixels) {
		hline(min_y, min_x + radius_pixels, max_x - radius_pixels);
		hline(max_y, min_x + radius_pixels, max_x - radius_pixels);
	}

	if (radius_pixels == 2) {
		pixel(min_x + 1, min_y + 1); // top-left
		pixel(max_x - 1, min_y + 1); // top-right
		pixel(min_x + 1, max_y - 1); // bottom-left
		pixel(max_x - 1, max_y - 1); // bottom-right
	}
}

void OledFrame::invert(Rect r) {
	const Rect c = to_canvas(*this, r);
	if (c.w <= 0 || c.h <= 0) {
		return;
	}
	canvas.invert_area(c.x, c.w, c.y, c.bottom() - 1);
}

void OledFrame::invert_rounded(Rect r, gfx::oled::BorderRadius radius) {
	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	const Rect c{origin.x + r.x, origin.y + r.y, r.w, r.h};
	if (clip.intersect(c) != c) {
		return; // as rect_rounded
	}
	canvas.invert_area_rounded(c.x, c.w, c.y, c.bottom() - 1, radius, style.rounded);
}

void OledFrame::clear(Rect r) {
	const Rect c = to_canvas(*this, r);
	if (c.w <= 0 || c.h <= 0) {
		return;
	}
	canvas.clear_area_exact(c.x, c.y, c.right() - 1, c.bottom() - 1);
}

void OledFrame::glyphs(std::string_view s, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t scroll_px,
                       int32_t end_x, bool use_width) {
	if (empty(clip)) {
		return;
	}
	const int32_t canvas_end_x = (end_x == kEdge) ? clip.right() : std::min(origin.x + end_x, clip.right());
	canvas.draw_string(s, origin.x + x, origin.y + y, spacing_x, height, scroll_px, canvas_end_x, use_width);
}

void OledFrame::glyph(char c, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t end_x) {
	if (empty(clip)) {
		return;
	}
	const int32_t canvas_end_x = (end_x == kEdge) ? clip.right() : std::min(origin.x + end_x, clip.right());
	canvas.draw_char(static_cast<uint8_t>(c), origin.x + x, origin.y + y, spacing_x, height, 0, canvas_end_x);
}

void OledFrame::graphic(const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h, int32_t bytes_tall,
                        bool reversed) {
	if (w <= 0 || h <= 0) {
		return;
	}
	const Rect c{origin.x + x, origin.y + y, w, h};
	if (clip.intersect(c) != c) {
		return; // all-or-nothing: the blit has no span decomposition
	}
	canvas.draw_graphic_multi_line(data, c.x, c.y, w, h, bytes_tall, reversed);
}

void OledFrame::icon(const gfx::oled::Icon& i, int32_t x, int32_t y, bool reversed) {
	graphic(i.data, x, y, i.width, i.height, i.num_bytes_tall, reversed);
}

void OledFrame::icon_centered(const gfx::oled::Icon& i, int32_t start_x, int32_t total_width, int32_t y,
                              bool reversed) {
	const int32_t padding = (total_width - i.width) >> 1; // canvas.cpp:628, the same >> 1
	icon(i, start_x + padding, y, reversed);
}

} // namespace deluge::ui::oled
