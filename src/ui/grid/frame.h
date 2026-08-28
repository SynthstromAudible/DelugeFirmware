#pragma once
#include "gfx/grid/pad_grid.h"
#include "gfx/grid/rect.h"
#include "gfx/grid/rgb.h"
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/response.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace deluge::ui::grid {

/// The most pad events one frame will translate for `local_events()` (spec B §4). The Deluge
/// reports at most a handful of pad transitions per tick; 32 is well past that. Events past the
/// 32nd that fall inside a region are dropped -- see `local_events()`.
///
/// The buffer is a member of *every* frame, not only of the frames that call `local_events()`:
/// `GridFrame` is 176 bytes, of which 128 are this, and `region()` zero-initialises it for each
/// nested child. That is the price of keeping `GridFrame` an aggregate with no heap and no
/// caller-supplied scratch; spec §8's "a frame is ~40 bytes" does not describe this one.
constexpr uint8_t kMaxLocalEvents = 32;

/// The pad surface a component sees for one tick (spec B §4).
///
/// The grid's defining trick is that **painting and hit-testing are one call**: `pad()` writes a
/// colour and, in the same breath, reports whether that pad was pressed, released or is being
/// held. Pad -> meaning is therefore computed exactly once, by the code that already knows what
/// the pad means, instead of once in a render path and again in an input path.
///
/// Coordinates are *local*: `(0, 0)` is the frame's own corner. The root frame spans the whole
/// `PadGrid`, sidebar included -- `GridDimensions::cols()` is `main_cols + sidebar_cols` and
/// `PadGrid::at(x, y)` addresses that one range (gfx/grid/pad_grid.h), so on a Deluge the columns
/// are `x = 0..15` for the main pads and `x = 16..17` for the two sidebar columns, in one
/// coordinate space. Nothing here treats the sidebar specially; `sidebar_columns()` (spec B §7)
/// will simply be a component that opens a `region` over `{16, 0, 2, rows}`.
///
/// An aggregate of references, built per frame on the stack and passed by reference. It owns
/// nothing and outlives nothing.
///
/// **A `GridFrame` is a per-tick object.** The shell builds the root frame at the top of every
/// tick and lets it die at the bottom; regions are built and destroyed inside one call. It is a
/// borrowed view over the grid, the snapshot and the gate -- four references and a rectangle --
/// so building one costs nothing and there is no reason to keep one alive between ticks. Nothing
/// here *breaks* if a frame outlives its tick: `local_events()` re-filters `input.pads` on every
/// call rather than caching, so a frame that is kept alive across ticks (or across two calls
/// where the caller has mutated the same buffer in place) always sees the snapshot as it stands
/// right now. But the lifetime the toolkit is written and tested against is one frame per tick.
///
/// Two recorded deviations from spec B §4's code block, which does not compile as written:
///
/// 1. §4 names the clip rectangle `region` *and* declares a member function `region(...)`. C++
///    forbids the collision, and the function is the name components call, so the rectangle is
///    spelled `clip` here -- also matching `OledFrame::clip`, which is likewise absolute.
/// 2. §4 gives the frame its own `std::span<const PadEvent> events`. Dropped: a span copied when
///    the frame is built cannot see the events the shell installs on the snapshot afterwards, and
///    a rig that holds one frame over a live snapshot (the `GridRig` in
///    tests/spec/ui/support/frames.h) does exactly that. Every frame instead reads `input.pads`
///    through the reference it already holds, which satisfies the routing rule the member existed
///    for -- a child region sees *the same* event span as its parent, never a filtered copy --
///    without the staleness. `local_events()` does the filtering, per call, on demand.
struct GridFrame {
	gfx::grid::PadGrid& grid;
	/// The frame's rectangle in absolute pad coordinates. Its corner is also the local origin:
	/// unlike `OledFrame`, a grid frame has no separate unclipped `size`, so `bounds()` reports
	/// the *clipped* extent and a region hanging off its parent's left or top edge would shift
	/// its own origin inward. Pads are addressed from 0, so no caller has a reason to ask for a
	/// negative origin; `region()` clamps into the parent either way and nothing escapes.
	gfx::grid::Rect clip;
	const InputSnapshot& input;
	RepaintGate& gate;

	/// The frame's extent in local coordinates: spec B §4's `{0, 0, w, h}`.
	gfx::grid::Rect bounds() const { return {0, 0, clip.w, clip.h}; }

	/// Paint one pad and report what happened to it this tick -- the primitive of spec B §7.
	///
	/// Outside `bounds()` this paints nothing and returns a default `Response`, so a component
	/// may sweep a rectangle larger than the region it was given and let the frame crop it.
	/// Inside, the pad is written through `PadGrid::set` (which marks the row dirty, so only
	/// touched rows are pushed to the hardware) and the returned `Response` carries:
	///
	/// - `pressed` / `velocity` -- a press event for this pad in `input.pads` this tick,
	/// - `released`             -- a release event for it this tick,
	/// - `held`                 -- the pad is in `input.held_pads`, i.e. down since some earlier
	///                             tick. Level, not edge: a press reports `pressed` on the tick
	///                             it arrives and `held` on every tick until the release.
	///
	/// A press and a release for the same pad in the same tick set both flags; `velocity` comes
	/// from the press.
	Response pad(int32_t x, int32_t y, RGB colour);

	/// Read back the colour currently on a pad, without painting -- the counterpart to `pad()`
	/// for an overlay that re-tints what earlier components already drew (`greyout`, ui/grid/
	/// effects.h). Clipping lives here, in the one place that owns it, rather than in each
	/// caller's own `clip.x + x` arithmetic. Outside `bounds()` this reports black, matching
	/// `pad()`'s "paints nothing, reports nothing" for the same coordinates.
	RGB peek(int32_t x, int32_t y) const;

	/// This tick's pad events that fall inside this frame, translated to local coordinates.
	///
	/// For multi-pad gestures (chords, drags) that `pad()`'s one-pad-at-a-time answer cannot
	/// express. Filtered and translated into a fixed member buffer, from scratch, on *every*
	/// call -- there is no cache. Filtering `input.pads` (a handful of events, never heap-backed)
	/// is O(events) and was measured free; a cache keyed on the span's identity is not a
	/// meaningful saving and it is actively wrong: a caller that reuses one buffer across ticks
	/// with the same pointer and the same count -- or mutates it in place between two calls on
	/// the same frame -- would get back stale events instead of what `input.pads` holds right
	/// now. At most `kMaxLocalEvents` are kept; a tick with more events than that inside one
	/// region loses the later ones, which is a lost gesture, never a corrupted one.
	///
	/// The span points into this frame. It dangles the moment the frame does -- in particular a
	/// span returned from inside a `region` body must not outlive that body.
	std::span<const PadEvent> local_events() const;

	/// The first press inside \p local (this frame's coordinates), in `input.pads` order, with its
	/// coordinates translated to be local to *this* frame -- not to \p local.
	///
	/// The reuse the `void`-returning column components (ui/grid/columns.h) ask of their callers:
	/// "which pad in my column was hit this tick" is one scan over `local_events()`, and this is
	/// that scan, written once. Releases are skipped; `std::nullopt` means no press landed inside
	/// the rectangle. \p local is clipped to `bounds()` first, so a rectangle hanging off the
	/// frame reports only the part that is really inside it.
	std::optional<PadEvent> first_press_in(gfx::grid::Rect local) const;

	/// Run \p body against a sub-frame whose `(0, 0)` is `local`'s corner and whose input is
	/// filtered to it (spec B §4). Nests to any depth; panes route their own input with no
	/// routing code, because the child's `pad()` and `local_events()` are already local.
	///
	/// The one templated entry point in the toolkit (spec B §2 rule 3: "only `region` bodies").
	///
	/// @param local the sub-rectangle, in this frame's coordinates
	/// @param body  invoked as `body(GridFrame&)`
	template <class F>
	void region(gfx::grid::Rect local, F&& body) {
		const gfx::grid::Rect translated{clip.x + local.x, clip.y + local.y, local.w, local.h};
		GridFrame child{grid, clip.intersect(translated), input, gate};
		std::forward<F>(body)(child);
	}

	// Scratch for local_events(), refilled from `input.pads` on every call -- not a cache; see the
	// note on local_events() above. Mutable because local_events() is const in spec B §4 and
	// filling the buffer is not an observable change -- the frame's identity is the four members
	// above.
	//
	// These must stay LAST and keep their initialisers: `region` above builds its child by
	// aggregate initialisation of the first four members, so GridFrame must remain an aggregate
	// and nothing may be inserted between `gate` and here.
	mutable PadEvent local_[kMaxLocalEvents] = {};
	mutable uint8_t local_count_ = 0;
};

/// The root frame for a whole pad surface: the full grid, sidebar columns included.
GridFrame root_grid_frame(gfx::grid::PadGrid& grid, const InputSnapshot& input, RepaintGate& gate);

} // namespace deluge::ui::grid
