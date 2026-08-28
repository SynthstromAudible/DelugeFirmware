#pragma once
#include "gfx/oled/canvas.h"
#include "gfx/oled/geometry.h"
#include "gfx/oled/icon.h"
#include "ui/core/frame_context.h"
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/style.h"
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace deluge::ui::oled {

/// @brief RAII scope that narrows the canvas clip to a region for the duration of one drawing
///        call and restores whatever was there before on exit.
///
/// Every `OledFrame` drawing method opens one of these over its own `clip` before it touches the
/// canvas, which is what makes a frame's region binding on *every* primitive rather than only on
/// the ones this file can decompose into spans.
///
/// @note **Intersects, never replaces.** A scope can only ever narrow what the canvas already
///       allows, so a clip set from outside the frame tree -- a shell clipping a repaint to a
///       damage rectangle, say -- keeps bounding everything a widget draws inside it. An empty
///       intersection is set as an empty clip (not cleared), which is the canvas's own spelling of
///       "draw nothing"; clearing would be exactly the widening this type must not do.
///
/// @note **Restores, never clears, on exit.** This is what lets scopes nest: a widget drawing
///       inside a sub-region leaves the parent's clip exactly as it found it, and an outer clip
///       survives a widget that draws through a child frame.
///
/// @warning Non-copyable, stack-only, and never held across a call that returns to the caller's
///          own drawing -- the clip it restores is the one that was live when it was constructed.
struct ClipScope {
	gfx::oled::Canvas& canvas;
	std::optional<gfx::oled::Rect> saved;

	ClipScope(gfx::oled::Canvas& c, gfx::oled::Rect region) : canvas{c}, saved{c.clip()} {
		canvas.set_clip(saved.has_value() ? saved->intersect(region) : region);
	}
	~ClipScope() {
		if (saved.has_value()) {
			canvas.set_clip(*saved);
		}
		else {
			canvas.clear_clip();
		}
	}

	ClipScope(const ClipScope&) = delete;
	ClipScope& operator=(const ClipScope&) = delete;
};

/// @brief The drawing surface a widget sees for one frame.
///
/// Everything a widget draws is in *local* coordinates: `(0, 0)` is the top-left of the
/// frame, not of the panel, and a widget never learns that the Deluge's faceplate hides the
/// top five rows. The shell builds the root frame with `origin = (0, visible_top)`;
/// `root_frame()` below does exactly that.
///
/// @note **Clipping is complete.** `origin` translates local to canvas coordinates and `clip` (a
///       canvas-coordinate rectangle) bounds what reaches the framebuffer: every drawing method
///       opens a \ref ClipScope over `clip` first, and `gfx::oled::Canvas` honours its clip pixel
///       for pixel in every primitive, glyphs and bitmaps included. Nothing a widget draws can
///       escape its region on any edge. Per-member details below.
///
/// @warning An aggregate of references, built per frame on the stack and passed by reference. It
///          owns nothing and outlives nothing.
struct OledFrame : FrameInput {
	// `FrameInput` (ui/core/frame_context.h) supplies `ctx`, `mask`, every input accessor, the
	// `claim_*()` methods and `gate()` -- one copy of the input-consumption contract for both
	// surfaces. A public, non-virtual base keeps `OledFrame` an aggregate; its initialiser list
	// therefore starts with a `{ctx, mask}` brace pair for the base subobject.
	gfx::oled::Canvas& canvas;
	gfx::oled::Rect clip;    ///< absolute, on the canvas
	gfx::oled::Point origin; ///< local (0,0) in canvas coordinates
	gfx::oled::Size size;    ///< the region's own extent, before clipping
	const Style& style;

	/// @brief The frame's extent in local coordinates: `{0, 0, w, h}`.
	///
	/// This is the region's *requested* size, not the visible part of it -- `visible()` is that.
	/// Local `(0, 0)` stays the region's own corner even when the region hangs off the parent's
	/// left or top edge, so a widget's layout arithmetic does not shift under it; the pixels that
	/// fall outside are clipped away. Reporting the clipped size instead would silently move the
	/// origin for a left/top overhang, which is worse than drawing nothing there.
	///
	/// @return The frame's requested extent, `{0, 0, size.w, size.h}`.
	gfx::oled::Rect bounds() const { return {0, 0, size.w, size.h}; }

	/// @brief The part of `bounds()` that can actually reach the framebuffer, in the same local
	///        coordinates: `clip` translated back through `origin`.
	///
	/// Equal to `bounds()` for a region that fits inside its parent, and empty (`w == 0` or
	/// `h == 0`) for one entirely outside it -- where only the extent is meaningful: the corner of
	/// an empty rectangle is `Rect::intersect`'s and may sit outside `bounds()`.
	///
	/// @warning Test emptiness, never the corner.
	///
	/// Drawing never needs this -- every primitive clips itself -- but a widget that *measures*
	/// does: "how many rows can be seen here" is a different question from "how tall was I asked
	/// to be", and only the second one may drive layout.
	///
	/// @return The visible sub-rectangle in local coordinates; empty if none of the frame is
	///         visible.
	gfx::oled::Rect visible() const { return {clip.x - origin.x, clip.y - origin.y, clip.w, clip.h}; }

	/// @brief Run \p body against a sub-frame whose (0,0) is `local`'s corner and whose clip is
	///        `local` intersected with this frame's.
	///
	/// Nests to any depth; panes route their own drawing with no routing code.
	///
	/// @param local the sub-rectangle, in this frame's coordinates
	/// @param granted the inputs the child may consume; intersected with this frame's own mask, so
	///        a region only ever narrows. `InputMask::all()` inherits, `none()` draws without
	///        consuming, and a partial mask keeps some inputs for the parent.
	/// @param body invoked as `body(OledFrame&)`
	template <class F>
	void region(gfx::oled::Rect local, InputMask granted, F&& body) {
		const gfx::oled::Point child_origin{origin.x + local.x, origin.y + local.y};
		const gfx::oled::Rect child_clip =
		    clip.intersect(gfx::oled::Rect{child_origin.x, child_origin.y, local.w, local.h});
		OledFrame child{{ctx, child_mask(granted)},        canvas, child_clip, child_origin,
		                gfx::oled::Size{local.w, local.h}, style};
		std::forward<F>(body)(child);
	}

	/// @brief `region` with the coarse spelling: `true` inherits every input this frame has,
	///        `false` grants none.
	/// @param local the sub-rectangle, in this frame's coordinates
	/// @param granted whether the child inherits this frame's input mask
	/// @param body invoked as `body(OledFrame&)`
	template <class F>
	void region(gfx::oled::Rect local, bool granted, F&& body) {
		region(local, granted ? InputMask::all() : InputMask::none(), std::forward<F>(body));
	}

	/// @brief `region(local, InputMask::all(), body)`: a child that inherits this frame's mask.
	/// @param local the sub-rectangle, in this frame's coordinates
	/// @param body invoked as `body(OledFrame&)`
	template <class F>
	void region(gfx::oled::Rect local, F&& body) {
		region(local, InputMask::all(), std::forward<F>(body));
	}

	/// @name Drawing primitives
	/// @{
	///
	/// All take local coordinates and translate by `origin`, and every one of them clips exactly
	/// to `clip` -- pixel for pixel, on all four edges. Each opens a \ref ClipScope and then hands
	/// the translated shape to the canvas, which does the clipping itself; a glyph, a bitmap or a
	/// rounded invert that straddles the region boundary draws the part that is inside and
	/// nothing else. There is no all-or-nothing case and no leaky edge left.
	///
	/// `line`, `rect` and `rect_rounded` stay decomposed here rather than being handed over whole,
	/// but that is now a *shape* decision, not a clipping one: `hline`/`vline` normalise a
	/// reversed span where the canvas's own routines drop it, so the decomposition is what these
	/// three have always drawn, and collapsing them onto the canvas calls would quietly change
	/// degenerate rectangles.

	/// @brief Draw a single pixel.
	/// @param x local x
	/// @param y local y
	void pixel(int32_t x, int32_t y);
	/// @brief Draw a horizontal span at row @p y from @p x0 to @p x1 (either order).
	/// @param y  row to draw at
	/// @param x0 one end of the span
	/// @param x1 the other end of the span
	void hline(int32_t y, int32_t x0, int32_t x1);
	/// @brief Draw a vertical span at column @p x from @p y0 to @p y1 (either order).
	/// @param x  column to draw at
	/// @param y0 one end of the span
	/// @param y1 the other end of the span
	void vline(int32_t x, int32_t y0, int32_t y1);
	/// @brief Draw a line between two points, Bresenham-style.
	/// @param x0    start x
	/// @param y0    start y
	/// @param x1    end x
	/// @param y1    end y
	/// @param thick draw a second, adjacent line of pixels alongside the first
	void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool thick = false);
	/// @brief Draw a rectangle's outline.
	/// @param r Rectangle to outline, in local coordinates.
	void rect(gfx::oled::Rect r);
	/// @brief Draw a rectangle's outline with rounded corners.
	///
	/// `Canvas::draw_rectangle_rounded`, decomposed into its four inset edges and its corner
	/// pixels so that each clips exactly; `style.rounded` selects the square arm, which is `rect`.
	/// @param r      Rectangle to outline, in local coordinates.
	/// @param radius Corner radius.
	void rect_rounded(gfx::oled::Rect r, gfx::oled::BorderRadius radius = gfx::oled::BorderRadius::Small);
	/// @brief Invert (XOR) the pixels inside a rectangle.
	/// @param r Rectangle to invert, in local coordinates.
	void invert(gfx::oled::Rect r);
	/// @brief Invert (XOR) the pixels inside a rectangle with rounded corners.
	/// @param r      Rectangle to invert, in local coordinates.
	/// @param radius Corner radius.
	void invert_rounded(gfx::oled::Rect r, gfx::oled::BorderRadius radius = gfx::oled::BorderRadius::Small);
	/// @brief Clear the pixels inside a rectangle.
	/// @param r Rectangle to clear, in local coordinates.
	void clear(gfx::oled::Rect r);

	/// @brief Draw text, straight through to `Canvas::draw_string`.
	///
	/// Clipped to the region on every edge, glyph by glyph: a string starting left of the frame,
	/// or one whose face is taller than the frame, draws only the part inside. `end_x` is still
	/// clamped to (and defaults to) the frame's right edge, which stops the rasteriser walking
	/// glyphs that could not appear anyway.
	///
	/// @param s         the text to draw
	/// @param x         local x of the string's start
	/// @param y         local y of the string's baseline
	/// @param spacing_x per-character advance
	/// @param height    the font height to draw at
	/// @param scroll_px see `Canvas::draw_string`
	/// @param end_x     local x to stop drawing at; defaults to the frame's right edge
	/// @param use_width see `Canvas::draw_string`
	void glyphs(std::string_view s, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t scroll_px = 0,
	            int32_t end_x = gfx::oled::kEdge, bool use_width = false);

	/// @brief Draw a single character, straight through to `Canvas::draw_char`.
	///
	/// `glyphs` with a one-character view is pixel-identical for a monospaced face, but not for a
	/// proportional one (it would pass the *measured* width where `draw_char` passes the caller's
	/// `spacing_x`, and `draw_char` centres the glyph inside that width), so the character case
	/// gets its own forwarder rather than a lookalike. Clipped exactly, as `glyphs`.
	///
	/// @param c         the character to draw
	/// @param x         local x
	/// @param y         local y
	/// @param spacing_x the advance width to centre the glyph within
	/// @param height    the font height to draw at
	/// @param end_x     local x to stop drawing at; defaults to the frame's right edge
	void glyph(char c, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t end_x = gfx::oled::kEdge);

	/// @brief Blit a column-major 1-bpp bitmap, clipped to the region column by column and row by
	///        row.
	/// @param data       the bitmap's column-major, page-packed 1-bpp pixel data
	/// @param x          local x
	/// @param y          local y
	/// @param w          bitmap width in pixels
	/// @param h          bitmap height in pixels
	/// @param bytes_tall the bitmap's height in bytes (pages)
	/// @param reversed   invert the bitmap's pixels as they are blitted
	void graphic(const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h, int32_t bytes_tall,
	             bool reversed = false);
	/// @brief Draw an icon.
	///
	/// @p reversed mirrors `Canvas::draw_icon`'s own parameter.
	/// @param i        Icon to draw.
	/// @param x        local x
	/// @param y        local y
	/// @param reversed invert the icon's pixels as they are blitted
	void icon(const gfx::oled::Icon& i, int32_t x, int32_t y, bool reversed = false);
	/// @brief Draw an icon centred across a span.
	///
	/// `Canvas::draw_icon_centered`: the icon centred across `total_width` starting from
	/// `start_x`, i.e. at `start_x + ((total_width - i.width) >> 1)`. Clipped, as `icon`.
	///
	/// @note Ports the centring legacy `Number::drawKnob` uses for its arc.
	///
	/// @param i           the icon to draw
	/// @param start_x     local x of the span's start
	/// @param total_width the span's width to centre within
	/// @param y           local y
	/// @param reversed    invert the icon's pixels as they are blitted
	void icon_centered(const gfx::oled::Icon& i, int32_t start_x, int32_t total_width, int32_t y,
	                   bool reversed = false);
	/// @}

	// Deliberately no private section, and no data member may be added below the public ones:
	// OledFrame must stay an aggregate, because `region` above builds its child with aggregate
	// initialisation of `{ctx, mask}, canvas, clip, origin, size, style` in that order -- the
	// FrameInput base subobject first, then this struct's own members. Helpers live in frame.cpp.
};

/// @brief The root frame for a whole panel: origin at the top of the *visible* area, clipped to
///        it, and granted \p mask.
///
/// \p mask defaults to `InputMask::all()`, since a root not otherwise told holds every input and
/// regions below it only ever narrow.
///
/// \p mask is the shell's lever at the top of the tree: the toolkit builds one `OledFrame`
/// tree per surface per tick, and a shell that stacks several scenes on one surface (an overlay
/// over a base scene) has to be able to tell a scene "you may draw, but the scene above you owns
/// this tick's input" -- exactly `region`'s `false`/`none()` argument one level up, where there is
/// no parent frame to call it on. `InputMask::all()` is what the top (frontmost, focused) scene's
/// root is built with; every scene beneath it gets `InputMask::none()`, so its widgets still render
/// -- and a `list` built over that root still paints its window and highlight -- but read 0/false
/// from every masked accessor and claim nothing, exactly as an unfocused `region()` does. No widget
/// below this point knows the bezel exists, regardless of \p mask.
///
/// @param canvas the canvas to draw to
/// @param style  the style the frame and its descendants draw with
/// @param ctx    the frame's input context
/// @param mask   the inputs this frame (and its descendants, absent a narrower `region()`) may
///               consume
/// @return The root `OledFrame` for this panel this tick.
OledFrame root_frame(gfx::oled::Canvas& canvas, const Style& style, FrameContext& ctx,
                     InputMask mask = InputMask::all());

} // namespace deluge::ui::oled
