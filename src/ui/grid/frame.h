#pragma once
#include "gfx/grid/pad_grid.h"
#include "gfx/grid/rect.h"
#include "gfx/grid/rgb.h"
#include "ui/core/frame_context.h"
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/response.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace deluge::ui::grid {

/// The most pad events one frame will translate for `local_events()`. The Deluge reports at most
/// a handful of pad transitions per tick; 32 is well past that. Events past the 32nd that fall
/// inside a region are dropped -- see `local_events()`.
///
/// @note The buffer is a member of *every* frame, not only of the frames that call
///       `local_events()`: `GridFrame` is 176 bytes, of which 128 are this, and `region()`
///       zero-initialises it for each nested child. That is the price of keeping `GridFrame` an
///       aggregate with no heap and no caller-supplied scratch.
constexpr uint8_t kMaxLocalEvents = 32;

/// @brief The pad surface a component sees for one tick.
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
/// coordinate space. Nothing here treats the sidebar specially; a sidebar component simply opens
/// a `region` over `{16, 0, 2, rows}`.
///
/// An aggregate of references, built per frame on the stack and passed by reference. It owns
/// nothing and outlives nothing.
///
/// @note **A `GridFrame` is a per-tick object.** The shell builds the root frame at the top of
///       every tick and lets it die at the bottom; regions are built and destroyed inside one
///       call. It is a borrowed view over the grid and the tick's `FrameContext` -- two
///       references and two rectangles -- so building one costs nothing. `local_events()`
///       re-filters `ctx.input.pads` on every call rather than caching, so a frame that is kept
///       alive across ticks (or across two calls where the caller has mutated the same buffer in
///       place) always sees the snapshot as it stands right now -- but the lifetime the toolkit
///       is written and tested against is one frame per tick.
///
/// @note The clip rectangle is named `clip`, not `region`, to avoid colliding with the member
///       function `region(...)` that components call; it also matches `OledFrame::clip`, which
///       is likewise absolute. There is no `std::span<const PadEvent> events` member: a span
///       copied when the frame is built cannot see events the shell installs on the snapshot
///       afterwards, and a rig that holds one frame over a live snapshot (the `GridRig` in
///       tests/spec/ui/support/frames.h) does exactly that. Every frame instead reads
///       `ctx.input.pads` through the reference it already holds -- a child region sees *the
///       same* event span as its parent, never a filtered copy, without the staleness.
///       `local_events()` does the filtering, per call, on demand.
struct GridFrame : FrameInput {
	// `FrameInput` (ui/core/frame_context.h) supplies `ctx`, `mask`, every input accessor, the
	// `claim_*()` methods and `gate()` -- one copy of the input-consumption contract for both
	// surfaces. A public, non-virtual base keeps `GridFrame` an aggregate; its initialiser list
	// therefore starts with a `{ctx, mask}` brace pair for the base subobject. Everything
	// positional stays below, on this struct: pads are answered by geometry, never by focus.
	gfx::grid::PadGrid& grid;
	/// The rectangle the frame was *asked* for, in absolute pad coordinates. Its corner is the
	/// local origin, whether or not the parent could grant all of it: local `(0, 0)` is the
	/// region's own corner, so a component's column arithmetic does not shift under it when the
	/// region overhangs. `bounds()` reports this extent.
	gfx::grid::Rect rect;
	/// What the frame may actually paint: `rect` intersected with the parent's own clip, absolute.
	/// `visible()` reports it in local coordinates. `pad()` and `peek()` test against this, so an
	/// overhanging region paints the part that is inside its parent and drops the rest.
	gfx::grid::Rect clip;

	/// @name Accessors
	/// @{

	/// @brief The frame's extent in local coordinates: `{0, 0, w, h}`, the size the region asked
	///        for.
	///
	/// `visible()` is the part of it that can be painted.
	/// @return The requested extent, in this frame's local coordinates.
	gfx::grid::Rect bounds() const { return {0, 0, rect.w, rect.h}; }

	/// @brief The part of `bounds()` that survives the parent's clip, in the same local
	///        coordinates.
	///
	/// Equal to `bounds()` for a region that fits inside its parent; empty (`w == 0` or
	/// `h == 0`) for one entirely outside it. Layout measures against `bounds()`; only code that
	/// needs to know what is really reachable -- a component sizing an overlay to the pads it can
	/// light -- asks for this.
	/// @note For an empty result, only the extent is meaningful: the corner is
	///       `Rect::intersect`'s and may sit outside `bounds()`.
	/// @return The visible extent, in this frame's local coordinates.
	gfx::grid::Rect visible() const { return {clip.x - rect.x, clip.y - rect.y, clip.w, clip.h}; }

	/// @brief Which pads are down right now, in absolute coordinates.
	///
	/// Pad input, so never masked -- and absolute, because it is the level behind `pad()`'s
	/// `held`, which does its own translation.
	/// @return The live pad-held bitmask, in absolute grid coordinates.
	const PadMask& held_pads() const { return ctx.input.held_pads; }

	/// @}
	/// @name Pads
	/// @{

	/// @brief Paint one pad and report what happened to it this tick.
	///
	/// Outside `visible()` this paints nothing and returns a default `Response`, so a component
	/// may sweep a rectangle larger than the region it was given -- or larger than the part of
	/// it the parent granted -- and let the frame crop it. Inside, the pad is written through
	/// `PadGrid::set` (which marks the row dirty, so only touched rows are pushed to the
	/// hardware) and the returned `Response` carries:
	///
	/// - `pressed` / `velocity` -- a press event for this pad in `ctx.input.pads` this tick,
	/// - `released`             -- a release event for it this tick,
	/// - `held`                 -- the pad is in `held_pads()`, i.e. down since some earlier
	///                             tick. Level, not edge: a press reports `pressed` on the tick
	///                             it arrives and `held` on every tick until the release.
	///
	/// @note A press and a release for the same pad in the same tick set both flags; `velocity`
	///       comes from the press.
	/// @param x local column
	/// @param y local row
	/// @param colour colour to paint the pad
	/// @return Whether/how this pad was pressed, released, or is held this tick.
	Response pad(int32_t x, int32_t y, RGB colour);

	/// @brief Read back the colour currently on a pad, without painting.
	///
	/// The counterpart to `pad()` for an overlay that re-tints what earlier components already
	/// drew (`greyout`, ui/grid/effects.h). Clipping lives here, in the one place that owns it,
	/// rather than in each caller's own `rect.x + x` arithmetic.
	/// @param x local column
	/// @param y local row
	/// @return The pad's current colour, or black outside `visible()` -- matching `pad()`'s
	///         "paints nothing, reports nothing" for the same coordinates.
	RGB peek(int32_t x, int32_t y) const;

	/// @}
	/// @name Events
	/// @{

	/// @brief This tick's pad events that fall inside this frame, translated to local
	///        coordinates.
	///
	/// For multi-pad gestures (chords, drags) that `pad()`'s one-pad-at-a-time answer cannot
	/// express.
	/// @note Filtered and translated into a fixed member buffer, from scratch, on *every* call --
	///       there is no cache. Filtering `ctx.input.pads` (a handful of events, never
	///       heap-backed) is O(events) and was measured free; a cache keyed on the span's
	///       identity would be actively wrong: a caller that reuses one buffer across ticks with
	///       the same pointer and the same count -- or mutates it in place between two calls on
	///       the same frame -- would get back stale events instead of what `input.pads` holds
	///       right now.
	/// @warning At most `kMaxLocalEvents` (32) are kept; a tick with more events than that inside
	///          one region loses the later ones -- a lost gesture, never a corrupted one.
	/// @warning The returned span points into this frame and dangles the moment the frame does;
	///          in particular a span returned from inside a `region` body must not outlive that
	///          body.
	/// @return This tick's pad events inside the frame, in local coordinates.
	std::span<const PadEvent> local_events() const;

	/// @brief The first press inside @p local (this frame's coordinates), in `ctx.input.pads`
	///        order, with its coordinates translated to be local to *this* frame -- not to
	///        @p local.
	///
	/// The reuse the `void`-returning column components (ui/grid/columns.h) ask of their
	/// callers: "which pad in my column was hit this tick" is one scan over `local_events()`,
	/// and this is that scan, written once. Releases are skipped. @p local is clipped to
	/// `bounds()` first -- that bounds the caller's rectangle to the region it asked for; the
	/// events themselves were already cropped to what the parent granted, by `local_events()`'s
	/// `clip` filter, so a rectangle hanging off the frame reports only presses that really
	/// landed inside it.
	/// @note Never masked by focus: a press names its target by where it landed.
	/// @param local the rectangle to test, in this frame's coordinates
	/// @return The first press inside @p local this tick, or `std::nullopt` if none landed.
	std::optional<PadEvent> first_press_in(gfx::grid::Rect local) const;

	/// @brief Hit-test @p local (this frame's coordinates) against this tick's input without
	///        painting.
	///
	/// @note Ports the SDK's `Frame::interact`.
	///
	/// Walks every event in @p local this tick, exactly as `pad()` walks every event for its one
	/// pad (above): `pressed`/`released` accumulate independently, so a pad pressed and released
	/// in the same tick -- or two different pads both landing inside @p local -- can set *both*
	/// flags from two different events. `velocity` is only touched by a press match, so (like
	/// `pad()`) it ends up holding the *last* matching press's velocity.
	///
	/// @note **Last-wins.** `index` -- with no SDK counterpart -- follows the same
	///       last-write-wins rule as the rest of the loop: it is recomputed on every match, press
	///       or release, so after the loop it is the *last* matching event's row-major position
	///       within @p local -- `(event.y - local.y) * local.w + (event.x - local.x)` -- letting
	///       a component spanning several pads (a fader, a row of steps) tell which one fired
	///       last without its own scan over `local_events()`. Two worked cases: a press then a
	///       release of the same pad in one tick sets both `pressed` and `released`, with
	///       `index` from the release (the later event); two presses inside @p local set
	///       `pressed` with `index` from the second.
	///
	/// `held` is unrelated to any of the above: it is true whenever *any* pad inside @p local --
	/// clipped to `visible()`, the same gate `pad()` applies before it will read `held_pads()`
	/// for a single pad -- is currently held.
	///
	/// @p local is clipped to `bounds()` first, exactly as `first_press_in` clips -- a rectangle
	/// hanging off this frame reports only the part that is really inside it.
	/// @note Pads are never masked by focus: a pad names its target by where it landed, focused
	///       region or not. @see src/ui/README.md (rule 4)
	/// @param local the rectangle to test, in this frame's coordinates
	/// @return Aggregated press/release/held/index for pads inside @p local this tick.
	Response interact(gfx::grid::Rect local) const;

	/// @}
	/// @name Regions
	/// @{

	/// @brief Run @p body against a sub-frame whose `(0, 0)` is `local`'s corner and whose input
	///        is filtered to it.
	///
	/// Nests to any depth; panes route their own input with no routing code, because the
	/// child's `pad()` and `local_events()` are already local. The one templated entry point in
	/// the toolkit.
	///
	/// @param local the sub-rectangle, in this frame's coordinates
	/// @param granted the inputs the child may consume; intersected with this frame's own mask, so
	///        a region only ever narrows. Pads are not in the mask and are never narrowed.
	/// @param body  invoked as `body(GridFrame&)`
	template <class F>
	void region(gfx::grid::Rect local, InputMask granted, F&& body) {
		const gfx::grid::Rect translated{rect.x + local.x, rect.y + local.y, local.w, local.h};
		GridFrame child{{ctx, child_mask(granted)}, grid, translated, clip.intersect(translated)};
		std::forward<F>(body)(child);
	}

	/// @brief `region()` with the coarse spelling: `true` inherits every input this frame has,
	///        `false` grants none.
	template <class F>
	void region(gfx::grid::Rect local, bool granted, F&& body) {
		region(local, granted ? InputMask::all() : InputMask::none(), std::forward<F>(body));
	}

	/// @brief `region(local, InputMask::all(), body)`: a child that inherits this frame's mask.
	template <class F>
	void region(gfx::grid::Rect local, F&& body) {
		region(local, InputMask::all(), std::forward<F>(body));
	}

	/// @}

	// Scratch for local_events(), refilled from `ctx.input.pads` on every call -- not a cache; see
	// the note on local_events() above. Mutable because local_events() is const and filling the
	// buffer is not an observable change -- the frame's identity is the four members above.
	//
	// These must stay LAST and keep their initialisers: `region` above builds its child by
	// aggregate initialisation of `{ctx, mask}, grid, rect, clip` in that order -- the
	// FrameInput base subobject first, then these three members -- so GridFrame must remain an
	// aggregate and nothing may be inserted between `clip` and here.
	mutable PadEvent local_[kMaxLocalEvents] = {};
	mutable uint8_t local_count_ = 0;
};

/// @brief The root frame for a whole pad surface: the full grid, sidebar columns included,
///        granted @p mask (`InputMask::all()` by default).
///
/// @p mask is the shell's lever at the top of the tree, the grid-side twin of
/// `oled::root_frame`'s: a shell rendering several scenes onto one pad surface (a base scene
/// under a grid overlay) builds the frontmost, focused one's root with `all()` and every scene
/// beneath it with `none()`, so a scene under an overlay still paints -- and a component built
/// over that root still lights every pad it would have -- but reads 0/false from every masked
/// accessor and claims nothing.
///
/// @note **Pads are unaffected either way.** `pad()`, `peek()`, `local_events()`,
///       `first_press_in()`, `interact()` and `held_pads()` are never masked, so a root built
///       with `none()` still reports every press exactly as one built with `all()` would: a
///       component that only ever paints and hit-tests pads -- never reads an encoder --
///       behaves identically under either mask, which is what makes an inert *encoder* surface a
///       live *pad* surface still make sense as one frame.
/// @param grid the pad surface to draw and read input from
/// @param ctx the tick's frame context
/// @param mask input mask granted to the root frame; pads are unaffected regardless
/// @return The root `GridFrame` for this tick.
GridFrame root_grid_frame(gfx::grid::PadGrid& grid, FrameContext& ctx, InputMask mask = InputMask::all());

} // namespace deluge::ui::grid
