#pragma once
#include "../../gfx/support/grid_state.h"
#include "../../gfx/support/pbm.h"
#include "gfx/grid/pad_grid.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/geometry.h"
#include "ui/core/frame_context.h"
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/style.h"
#include "ui/grid/frame.h"
#include "ui/oled/frame.h"

#include <string>

namespace deluge::ui::test {

/// @brief An InputSnapshot with everything zeroed/false and an empty pad span.
///
/// The baseline a spec starts from before setting the one or two fields it cares about.
/// @param now_ms The snapshot's simulated wall-clock time.
/// @return A zeroed InputSnapshot with `now_ms` set.
inline InputSnapshot null_input(uint32_t now_ms = 0) {
	return InputSnapshot{
	    .turn = 0,
	    .edit = {0, 0},
	    .horizontal = 0,
	    .vertical = 0,
	    .select_pressed = false,
	    .back_pressed = false,
	    .shift = false,
	    .pads = {},
	    .held_pads = {},
	    .now_ms = now_ms,
	};
}

/// @brief Everything an OLED widget spec needs for one panel geometry.
///
/// Bundles the framebuffer, the frame built over it exactly as the shell will build it, and two
/// ways to read the result back: a single pixel, for assertions that name a legacy coordinate,
/// and a PBM, for goldens.
///
/// `G` is the geometry, so a spec can render the same widget at 128x48-with-bezel and at
/// 128x64-flush and check both -- the whole point of the toolkit's geometry-as-a-value rule.
///
/// @note The rig's root frame defaults to a focused root (`InputMask::all()`). Call `begin_tick`
///       once per simulated tick to advance the clock and clear that tick's claims.
template <deluge::gfx::oled::Geometry G>
struct OledRig {
	/// The framebuffer the frame draws into.
	deluge::gfx::oled::OwnedCanvas<G> canvas;
	/// Tracks which of the rig's frames still owe a repaint.
	deluge::ui::RepaintGate gate;
	/// The tick's input; a spec mutates this directly before reading `frame`'s response.
	deluge::ui::InputSnapshot input = null_input();
	/// The tick's context, built over the two members above and read by every frame in the rig.
	/// Declared before `frame` so it is alive when the root frame binds to it; `ctx.claims` is
	/// what a spec inspects to see what a widget consumed.
	deluge::ui::FrameContext ctx{input, gate};
	/// The panel style matching `G`'s height (48px or 64px).
	const deluge::ui::Style& style = (G.height == 64) ? deluge::ui::kDelugeStyle64 : deluge::ui::kDelugeStyle48;
	/// The root frame under test; focused by default (see the rig's class note).
	deluge::ui::oled::OledFrame frame = deluge::ui::oled::root_frame(canvas, style, ctx);

	OledRig() {
		canvas.clear();
		reference.clear();
	}

	/// @brief Begin a new simulated tick: move the clock to @p now_ms and clear the tick's
	/// claims, as the shell's own pass will.
	///
	/// A spec that renders several ticks through one rig calls this between them; one that
	/// renders a single frame needs it only when it asserts on `ctx.tick` or wants `claims`
	/// reset.
	/// @param now_ms The simulated wall-clock time for the new tick.
	void begin_tick(uint32_t now_ms) {
		input.now_ms = now_ms;
		ctx.begin_tick(now_ms);
	}

	/// @return `canvas`'s contents as a PBM, for golden comparisons.
	std::string pbm() { return page_major_to_pbm(canvas.bytes().data(), G.width, G.height); }

	/// @brief Is the pixel at *canvas* (not frame-local) coordinates lit?
	/// @param x Canvas-space column.
	/// @param y Canvas-space row.
	/// @return true if the pixel is set.
	bool lit(int x, int y) const { return (canvas.bytes()[(y >> 3) * G.width + x] & (1u << (y & 7))) != 0; }

	/// A second, identically sized canvas for drawing the legacy routine's output into, so a
	/// port can be compared against its original rather than against its own last run.
	deluge::gfx::oled::OwnedCanvas<G> reference;
	/// @return `reference`'s contents as a PBM, for comparison against the legacy routine's output.
	std::string reference_pbm() { return page_major_to_pbm(reference.bytes().data(), G.width, G.height); }
};

using Rig48 = OledRig<deluge::gfx::oled::kDelugeOled>;
using Rig64 = OledRig<deluge::gfx::oled::Geometry{128, 64, 0}>;

/// @brief Everything a grid component spec needs.
///
/// Bundles a Deluge-shaped pad surface (16 main + 2 sidebar columns x 8 rows, addressed as one
/// 18-wide space), the root frame built over it exactly as the shell will build it, and the
/// deluge-sdk `.state` dump for golden assertions.
///
/// `input` is a live member, so a spec sets `r.input.pads` / `r.input.held_pads` *after*
/// constructing the rig and `r.frame` still sees it -- frames read the snapshot through the
/// reference they hold rather than copying the event span (see ui/grid/frame.h).
///
/// @note As `OledRig`: the rig's root frame defaults to a focused root, and `begin_tick` must be
///       called once per simulated tick.
struct GridRig {
	/// The pad surface the frame draws into and hit-tests against.
	deluge::gfx::grid::PadGrid grid{deluge::gfx::grid::kDelugeGrid};
	/// Tracks which of the rig's frames still owe a repaint.
	deluge::ui::RepaintGate gate;
	/// The tick's input; a spec mutates this directly before reading `frame`'s response.
	deluge::ui::InputSnapshot input = null_input();
	/// The tick's context, built over the two members above and read by every frame in the rig.
	deluge::ui::FrameContext ctx{input, gate};
	/// The root frame under test; focused by default (see the rig's class note).
	deluge::ui::grid::GridFrame frame = deluge::ui::grid::root_grid_frame(grid, ctx);

	/// @brief Begin a new simulated tick. As `OledRig::begin_tick`.
	/// @param now_ms The simulated wall-clock time for the new tick.
	void begin_tick(uint32_t now_ms) {
		input.now_ms = now_ms;
		ctx.begin_tick(now_ms);
	}

	/// @return The pad surface's contents as a deluge-sdk `.state` dump, for golden comparisons.
	std::string state() { return grid_to_state(grid); }
};

} // namespace deluge::ui::test
