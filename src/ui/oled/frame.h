#pragma once
#include "gfx/oled/canvas.h"
#include "gfx/oled/geometry.h"
#include "gfx/oled/icon.h"
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/style.h"
#include <cstdint>
#include <string_view>
#include <utility>

namespace deluge::ui::oled {

/// The drawing surface a widget sees for one frame (spec B §4).
///
/// Everything a widget draws is in *local* coordinates: `(0, 0)` is the top-left of the
/// frame, not of the panel, and a widget never learns that the Deluge's faceplate hides the
/// top five rows. The shell builds the root frame with `origin = (0, visible_top)`;
/// `root_frame()` below does exactly that.
///
/// `origin` translates local to canvas coordinates and `clip` (a canvas-coordinate rectangle)
/// bounds what reaches the framebuffer. That bound is complete for the geometric primitives
/// and all-or-nothing for the rounded and blitting ones, but it is NOT complete for `glyphs`:
/// text is clipped to the frame's right edge and to the canvas, and can still escape a region
/// on the left or vertically. Closing that gap needs a clip rectangle threaded through
/// `Canvas::draw_char` / `draw_graphic_multi_line` in gfx, tracked as a follow-up and a
/// prerequisite for the widgets that draw text inside sub-regions (`list`, `columns`,
/// `popup_frame`). Per-member details below.
///
/// An aggregate of references, built per frame on the stack and passed by reference. It owns
/// nothing and outlives nothing.
struct OledFrame {
	gfx::oled::Canvas& canvas;
	gfx::oled::Rect clip;    ///< absolute, on the canvas
	gfx::oled::Point origin; ///< local (0,0) in canvas coordinates
	gfx::oled::Size size;    ///< the region's own extent, before clipping
	const Style& style;
	const InputSnapshot& input;
	RepaintGate& gate;

	/// The frame's extent in local coordinates: spec B §4's `{0, 0, w, h}`.
	///
	/// This is the region's *requested* size, not the visible part of it. Local `(0, 0)` stays
	/// the region's own corner even when the region hangs off the parent's left or top edge, so
	/// a widget's layout arithmetic does not shift under it; the pixels that fall outside are
	/// clipped away. Reporting the clipped size instead would silently move the origin for a
	/// left/top overhang, which is worse than drawing nothing there.
	gfx::oled::Rect bounds() const { return {0, 0, size.w, size.h}; }

	/// Run \p body against a sub-frame whose (0,0) is `local`'s corner and whose clip is
	/// `local` intersected with this frame's. Nests to any depth; panes route their own
	/// drawing with no routing code.
	///
	/// @param local the sub-rectangle, in this frame's coordinates
	/// @param body invoked as `body(OledFrame&)`
	template <class F>
	void region(gfx::oled::Rect local, F&& body) {
		const gfx::oled::Point child_origin{origin.x + local.x, origin.y + local.y};
		const gfx::oled::Rect child_clip =
		    clip.intersect(gfx::oled::Rect{child_origin.x, child_origin.y, local.w, local.h});
		OledFrame child{canvas, child_clip, child_origin, gfx::oled::Size{local.w, local.h}, style, input, gate};
		std::forward<F>(body)(child);
	}

	/// @{
	/// @name Drawing primitives
	///
	/// All take local coordinates and translate by `origin`. Nothing any of them draws lands
	/// outside `clip`; how each one gets there differs, because a rectangle intersection is
	/// the right answer for a filled area and the wrong answer for an outline (it would draw
	/// an edge along the clip boundary that is not part of the shape):
	///
	/// - `pixel`/`hline`/`vline`/`line`/`invert`/`clear` clip exactly, pixel for pixel.
	/// - `rect` is decomposed into its four clipped edges, which is exactly what
	///   `Canvas::draw_rectangle` does, so the clipping is exact there too.
	/// - the rounded and blitting primitives (`rect_rounded`, `invert_rounded`, `graphic`,
	///   `icon`) are all-or-nothing: a shape that would fall even partly outside `clip` is
	///   skipped. Their corner arcs and bitmaps cannot be decomposed into clippable spans,
	///   and silently overdrawing a sibling region would be worse than drawing nothing.
	/// - `glyphs` clips to `clip`'s right edge, and to the canvas otherwise; see below.

	void pixel(int32_t x, int32_t y);
	void hline(int32_t y, int32_t x0, int32_t x1);
	void vline(int32_t x, int32_t y0, int32_t y1);
	void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool thick = false);
	void rect(gfx::oled::Rect r);
	void rect_rounded(gfx::oled::Rect r, gfx::oled::BorderRadius radius = gfx::oled::SMALL);
	void invert(gfx::oled::Rect r);
	void invert_rounded(gfx::oled::Rect r, gfx::oled::BorderRadius radius = gfx::oled::SMALL);
	void clear(gfx::oled::Rect r);

	/// Draw text, straight through to `Canvas::draw_string` (see it for `scroll_px`,
	/// `end_x` and `use_width`).
	///
	/// Clipping caveat: the gfx glyph rasteriser bounds itself to the canvas and to `end_x`,
	/// which is all the legacy code ever did, and per-glyph clipping is not something this
	/// task adds to gfx. So `end_x` is clamped to the frame's right edge (and defaults to
	/// it), but a string starting left of the frame, or a glyph taller than the frame, is
	/// bounded only by the canvas. Widgets size their own text; the layer above must not rely
	/// on `glyphs` to crop it.
	void glyphs(std::string_view s, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t scroll_px = 0,
	            int32_t end_x = gfx::oled::kEdge, bool use_width = false);

	/// Blit a column-major 1-bpp bitmap, whole or not at all (see the clipping note above).
	void graphic(const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h, int32_t bytes_tall,
	             bool reversed = false);
	/// `reversed` mirrors `Canvas::draw_icon`'s own parameter; the brief's interface omitted it.
	void icon(const gfx::oled::Icon& i, int32_t x, int32_t y, bool reversed = false);
	/// @}

	// Deliberately no private section, and no data member may be added below the public ones:
	// OledFrame must stay an aggregate, because `region` above builds its child with aggregate
	// initialisation. Helpers live in frame.cpp.
};

/// The root frame for a whole panel: origin at the top of the *visible* area, clipped to it.
/// No widget below this point knows the bezel exists.
OledFrame root_frame(gfx::oled::Canvas& canvas, const Style& style, const InputSnapshot& input, RepaintGate& gate);

} // namespace deluge::ui::oled
