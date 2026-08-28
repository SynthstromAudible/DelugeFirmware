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

/// One point of a walked shape, translated but not clipped -- the caller holds the ClipScope, so
/// the canvas drops it if it is outside. A free function rather than a member so OledFrame stays
/// an aggregate (see the note at the end of frame.h).
void plot(const OledFrame& f, int32_t x, int32_t y) {
	f.canvas.draw_pixel(f.origin.x + x, f.origin.y + y);
}
} // namespace

void OledFrame::pixel(int32_t x, int32_t y) {
	ClipScope scope{canvas, clip};
	canvas.draw_pixel(origin.x + x, origin.y + y);
}

void OledFrame::hline(int32_t y, int32_t x0, int32_t x1) {
	ClipScope scope{canvas, clip};
	// The canvas drops a reversed span; this one has always normalised it, so keep doing that
	// here and let the scope above do the clipping.
	canvas.draw_horizontal_line(origin.y + y, origin.x + std::min(x0, x1), origin.x + std::max(x0, x1));
}

void OledFrame::vline(int32_t x, int32_t y0, int32_t y1) {
	ClipScope scope{canvas, clip};
	canvas.draw_vertical_line(origin.x + x, origin.y + std::min(y0, y1), origin.y + std::max(y0, y1));
}

void OledFrame::line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool thick) {
	ClipScope scope{canvas, clip};

	// The same Bresenham walk as Canvas::draw_line (gfx/oled/canvas.cpp), plotting straight to
	// the canvas under this frame's clip rather than clipping the endpoints first: shortening the
	// segment would rasterise a *different* set of pixels (the error term restarts), whereas
	// clipping per plotted point keeps a clipped line a strict subset of the unclipped one.
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

		plot(*this, actual_x, actual_y);
		if (thick) {
			plot(*this, steep ? actual_x + 1 : actual_x, steep ? actual_y : actual_y - 1);
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
	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	ClipScope scope{canvas, clip};
	const Rect c{origin.x + r.x, origin.y + r.y, r.w, r.h};
	canvas.invert_area(c.x, c.w, c.y, c.bottom() - 1);
}

void OledFrame::invert_rounded(Rect r, gfx::oled::BorderRadius radius) {
	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	ClipScope scope{canvas, clip};
	const Rect c{origin.x + r.x, origin.y + r.y, r.w, r.h};
	// The XOR fill and the spared corners both clip in the canvas now, so a rounded highlight
	// that hangs off the region inverts the part inside it instead of being skipped whole.
	canvas.invert_area_rounded(c.x, c.w, c.y, c.bottom() - 1, radius, style.rounded);
}

void OledFrame::clear(Rect r) {
	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	ClipScope scope{canvas, clip};
	const Rect c{origin.x + r.x, origin.y + r.y, r.w, r.h};
	canvas.clear_area_exact(c.x, c.y, c.right() - 1, c.bottom() - 1);
}

void OledFrame::glyphs(std::string_view s, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t scroll_px,
                       int32_t end_x, bool use_width) {
	if (empty(clip)) {
		return;
	}
	ClipScope scope{canvas, clip};
	const int32_t canvas_end_x = (end_x == kEdge) ? clip.right() : std::min(origin.x + end_x, clip.right());
	canvas.draw_string(s, origin.x + x, origin.y + y, spacing_x, height, scroll_px, canvas_end_x, use_width);
}

void OledFrame::glyph(char c, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t end_x) {
	if (empty(clip)) {
		return;
	}
	ClipScope scope{canvas, clip};
	const int32_t canvas_end_x = (end_x == kEdge) ? clip.right() : std::min(origin.x + end_x, clip.right());
	canvas.draw_char(static_cast<uint8_t>(c), origin.x + x, origin.y + y, spacing_x, height, 0, canvas_end_x);
}

void OledFrame::graphic(const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h, int32_t bytes_tall,
                        bool reversed) {
	if (w <= 0 || h <= 0) {
		return;
	}
	ClipScope scope{canvas, clip};
	// The blit clips column by column and page-mask by page-mask inside the canvas; a bitmap
	// wider or taller than the region draws the part that fits rather than nothing at all.
	canvas.draw_graphic_multi_line(data, origin.x + x, origin.y + y, w, h, bytes_tall, reversed);
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
