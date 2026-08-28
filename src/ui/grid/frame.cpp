#include "ui/grid/frame.h"

#include "gfx/grid/palette.h"

namespace deluge::ui::grid {

using gfx::grid::Rect;

GridFrame root_grid_frame(gfx::grid::PadGrid& grid, FrameContext& ctx) {
	// The whole surface, sidebar included: GridDimensions::cols() is main_cols + sidebar_cols and
	// PadGrid addresses that as one range, so the root frame is one 18-wide coordinate space on a
	// Deluge rather than a main area with a sidebar bolted on beside it.
	const gfx::grid::GridDimensions d = grid.dims();
	const Rect all{0, 0, d.cols(), d.rows};
	// Requested == visible at the root: nothing clips the whole surface. Focused, because the root
	// of a surface is always live; regions below it narrow.
	return GridFrame{grid, all, all, ctx, true};
}

Response GridFrame::pad(int32_t x, int32_t y, RGB colour) {
	const int32_t abs_x = rect.x + x;
	const int32_t abs_y = rect.y + y;
	// Tested against `clip`, not `bounds()`: local coordinates are measured from the *requested*
	// corner, so a region that overhangs its parent paints the part the parent granted and drops
	// the rest. Outside it: paints nothing, reports nothing.
	if (!clip.contains(abs_x, abs_y)) {
		return {};
	}
	// clip is built from the grid's own dimensions and only ever shrinks (region() intersects), so
	// this is in range by construction; the guard is here because the coordinates below are
	// narrowed to uint8_t and a silently wrapped column would paint the wrong pad.
	if (abs_x < 0 || abs_y < 0 || abs_x > 0xFF || abs_y > 0xFF) {
		return {};
	}
	const auto px = static_cast<uint8_t>(abs_x);
	const auto py = static_cast<uint8_t>(abs_y);

	grid.set(px, py, colour); // marks row py dirty, and no-ops if the grid is smaller than clip

	Response r;
	for (const PadEvent& e : ctx.input.pads) {
		if (e.x != px || e.y != py) {
			continue;
		}
		if (e.pressed) {
			r.pressed = true;
			r.velocity = e.velocity;
		}
		else {
			r.released = true;
		}
	}
	r.held = held_pads().test(px, py);
	return r;
}

RGB GridFrame::peek(int32_t x, int32_t y) const {
	const int32_t abs_x = rect.x + x;
	const int32_t abs_y = rect.y + y;
	if (!clip.contains(abs_x, abs_y)) { // same crop as pad()
		return gui::colours::black;
	}
	// Same narrowing guard as pad(): clip only ever shrinks from the grid's own dimensions, so
	// this holds by construction, and a wrapped coordinate would read the wrong pad.
	if (abs_x < 0 || abs_y < 0 || abs_x > 0xFF || abs_y > 0xFF) {
		return gui::colours::black;
	}
	const auto px = static_cast<uint8_t>(abs_x);
	const auto py = static_cast<uint8_t>(abs_y);
	if (!grid.in_range(px, py)) {
		return gui::colours::black;
	}
	return grid.at(px, py);
}

std::span<const PadEvent> GridFrame::local_events() const {
	// No cache: re-filter ctx.input.pads from scratch every call. O(events), at most a handful per
	// tick, and dropping the cache is what makes a reused buffer (same pointer, same size, new
	// contents) always seen -- see the note on local_events() in frame.h.
	local_count_ = 0;
	for (const PadEvent& e : ctx.input.pads) {
		// Filtered by what is visible, translated by the *requested* corner: an event can only
		// arrive on a pad this frame can see, and its local coordinates are the ones `pad()` uses.
		if (!clip.contains(e.x, e.y)) {
			continue;
		}
		if (local_count_ >= kMaxLocalEvents) {
			break; // keep the first kMaxLocalEvents; see the note on local_events()
		}
		local_[local_count_++] = PadEvent{
		    .x = static_cast<uint8_t>(e.x - rect.x),
		    .y = static_cast<uint8_t>(e.y - rect.y),
		    .velocity = e.velocity,
		    .pressed = e.pressed,
		};
	}
	return {local_, local_count_};
}

std::optional<PadEvent> GridFrame::first_press_in(gfx::grid::Rect local) const {
	// Clip to the frame first, so a rectangle hanging off the edge reports only its real part --
	// the same rule `pad()` and `region()` follow.
	const Rect r = bounds().intersect(local);
	if (r.w <= 0 || r.h <= 0) {
		return std::nullopt;
	}
	for (const PadEvent& e : local_events()) {
		if (e.pressed && r.contains(e.x, e.y)) {
			return e;
		}
	}
	return std::nullopt;
}

} // namespace deluge::ui::grid
