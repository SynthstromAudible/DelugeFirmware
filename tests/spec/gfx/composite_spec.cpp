#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "gfx/grid/composite.h"
#include "support/golden.h"
#include "support/grid_state.h"

using namespace deluge::gfx::grid;

namespace {
/// A small 2x2 grid (no sidebar) is enough to exercise every corner of every blend mode by hand;
/// kDelugeGrid is reserved for the golden test below, where the shape doesn't matter but parity
/// with the other grid specs does.
constexpr GridDimensions kTiny{2, 0, 2};

PadGrid make_base() {
	PadGrid g(kTiny);
	g.set(0, 0, RGB{10, 20, 30});
	g.set(1, 0, RGB{0, 0, 0}); // black
	g.set(0, 1, RGB{200, 100, 50});
	g.set(1, 1, RGB{5, 5, 5});
	g.clear_dirty();
	return g;
}

PadGrid make_overlay() {
	PadGrid g(kTiny);
	g.set(0, 0, RGB{0, 0, 0}); // black: "nothing painted here" for Replace
	g.set(1, 0, RGB{111, 222, 33});
	g.set(0, 1, RGB{255, 255, 255});
	g.set(1, 1, RGB{128, 64, 32});
	g.clear_dirty();
	return g;
}
} // namespace

// clang-format off
describe grid_composite("gfx::grid::composite", $ {
	it("Replace: a black overlay pad leaves the base showing through, any other colour overwrites", _ {
		// layer.rs:91-93, adapted (composite.h): the SDK's Replace overwrites every non-`None`
		// overlay cell unconditionally; PadGrid has no `None`, so a black overlay pad stands in
		// for "nothing was painted here" and is the one cell Replace leaves alone.
		const PadGrid base = make_base();
		const PadGrid overlay = make_overlay();
		PadGrid out(kTiny);
		composite(base, overlay, BlendMode::Replace, out);
		expect(out.at(0, 0)).to_equal(RGB{10, 20, 30});   // overlay black -> base kept
		expect(out.at(1, 0)).to_equal(RGB{111, 222, 33}); // overlay non-black -> overwritten
		expect(out.at(0, 1)).to_equal(RGB{255, 255, 255});
		expect(out.at(1, 1)).to_equal(RGB{128, 64, 32});
	});
	it("Overlay: the overlay wins only where the base is currently black", _ {
		// layer.rs:94-99 verbatim: `if dst_cell == BLACK { dst = Some(src) }` -- a non-black base
		// pad is kept regardless of what the overlay holds there, even a non-black overlay colour.
		const PadGrid base = make_base();
		const PadGrid overlay = make_overlay();
		PadGrid out(kTiny);
		composite(base, overlay, BlendMode::Overlay, out);
		expect(out.at(0, 0)).to_equal(RGB{10, 20, 30});    // base non-black -> base kept
		expect(out.at(1, 0)).to_equal(RGB{111, 222, 33});  // base black -> overlay wins
		expect(out.at(0, 1)).to_equal(RGB{200, 100, 50});  // base non-black -> base kept
		expect(out.at(1, 1)).to_equal(RGB{5, 5, 5});       // base non-black (5,5,5) -> base kept,
		                                                    // even though the overlay has a colour
	});
	it("Tint: each channel is base * overlay / 255, truncating", _ {
		// layer.rs:100-106 verbatim: `((dst.c as u16 * src.c as u16) / 255) as u8` -- integer
		// division, no rounding. Hand-computed:
		//   (0,0): {10,20,30} * {0,0,0}     / 255 = {0,0,0}          (overlay black kills it)
		//   (1,0): {0,0,0}    * {111,222,33}/ 255 = {0,0,0}          (base black kills it)
		//   (0,1): {200,100,50}*{255,255,255}/255 = {200,100,50}    (white overlay is identity)
		//   (1,1): {5,5,5}    * {128,64,32} / 255:
		//     5*128=640,  640/255 = 2 (510 <= 640 < 765)
		//     5*64 =320,  320/255 = 1 (255 <= 320 < 510)
		//     5*32 =160,  160/255 = 0 (160 < 255)
		//     -> {2, 1, 0}
		const PadGrid base = make_base();
		const PadGrid overlay = make_overlay();
		PadGrid out(kTiny);
		composite(base, overlay, BlendMode::Tint, out);
		expect(out.at(0, 0)).to_equal(RGB{0, 0, 0});
		expect(out.at(1, 0)).to_equal(RGB{0, 0, 0});
		expect(out.at(0, 1)).to_equal(RGB{200, 100, 50});
		expect(out.at(1, 1)).to_equal(RGB{2, 1, 0});
	});
	it("marks only the rows that actually changed dirty", _ {
		const PadGrid base = make_base();
		const PadGrid overlay = make_overlay();
		PadGrid out(kTiny);
		out.set(0, 0, RGB{10, 20, 30}); // pre-seed row 0's result so Replace changes nothing in it
		out.set(1, 0, RGB{111, 222, 33});
		out.clear_dirty();
		composite(base, overlay, BlendMode::Replace, out);
		// Row 0 already held Replace's result for both cells -> untouched -> not dirty.
		// Row 1 goes from {0,0,0} (out's constructor default) to Replace's result -> dirty.
		expect((out.dirty_rows() & (1u << 0)) != 0).to_be_false();
		expect((out.dirty_rows() & (1u << 1)) != 0).to_be_true();
	});
	it("composites in place when out aliases base", _ {
		PadGrid g = make_base();
		const PadGrid overlay = make_overlay();
		composite(g, overlay, BlendMode::Overlay, g); // out IS base
		expect(g.at(0, 0)).to_equal(RGB{10, 20, 30});
		expect(g.at(1, 0)).to_equal(RGB{111, 222, 33});
		expect(g.at(0, 1)).to_equal(RGB{200, 100, 50});
		expect(g.at(1, 1)).to_equal(RGB{5, 5, 5});
	});
	it("dumps a golden state per blend mode", _ {
		PadGrid base(kDelugeGrid);
		base.fill_rect(0, 0, 18, 8, RGB{40, 60, 80});
		base.set(0, 0, RGB{0, 0, 0});   // one black base pad, for Overlay to punch through
		base.set(17, 7, RGB{0, 0, 0}); // and the far corner
		PadGrid overlay(kDelugeGrid);
		overlay.set(0, 0, RGB{255, 0, 0});
		overlay.set(3, 2, RGB{0, 255, 0});
		overlay.set(17, 7, RGB{0, 0, 255});
		// Everywhere else in `overlay` defaults to black, i.e. "nothing painted" for Replace.

		PadGrid replaced(kDelugeGrid);
		composite(base, overlay, BlendMode::Replace, replaced);
		expect_golden(self, "grid/composite_replace.state", grid_to_state(replaced));

		PadGrid overlaid(kDelugeGrid);
		composite(base, overlay, BlendMode::Overlay, overlaid);
		expect_golden(self, "grid/composite_overlay.state", grid_to_state(overlaid));

		PadGrid tinted(kDelugeGrid);
		composite(base, overlay, BlendMode::Tint, tinted);
		expect_golden(self, "grid/composite_tint.state", grid_to_state(tinted));
	});
});

DELUGE_SPEC(gfx_composite_spec, grid_composite)
