#include "support/frames.h"
#include "ui/grid/frame.h"

#include <optional>

#include "cppspec.hpp"

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;

// clang-format off
describe grid_frame("ui::grid::GridFrame", $ {
	it("paints and hit-tests in one call", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{3, 5, 100, true}};
		r.input.pads = ev;
		Response a = r.frame.pad(3, 5, RGB{255, 0, 0});
		Response b = r.frame.pad(4, 5, RGB{0, 255, 0});
		expect(a.pressed).to_be_true(); expect(a.velocity).to_equal(uint8_t(100));
		expect(b.pressed).to_be_false();
		expect(r.grid.at(3, 5)).to_equal(RGB{255, 0, 0});
		// `!= 0` added to the brief's `expect(dirty_rows() & (1u << 5)).to_be_true()`: cppspec's
		// to_be_true() static_asserts on `bool`, and the masked uint32_t is not one.
		expect((r.grid.dirty_rows() & (1u << 5)) != 0).to_be_true();
	});
	it("routes events to exactly one region and translates them", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{10, 2, 255, true}};
		r.input.pads = ev;
		int hits = 0;
		r.frame.region(Rect{0, 0, 8, 8}, [&](GridFrame& left) { if (left.pad(2, 2, RGB{1,1,1}).pressed) hits++; expect(left.local_events().size()).to_equal(size_t(0)); });
		r.frame.region(Rect{8, 0, 8, 8}, [&](GridFrame& right) {
			expect(right.local_events().size()).to_equal(size_t(1));
			expect(int(right.local_events()[0].x)).to_equal(2);
			if (right.pad(2, 2, RGB{2,2,2}).pressed) hits++;
			right.pad(9, 0, RGB{9,9,9});    // outside the region: not painted
		});
		expect(hits).to_equal(1);
		expect(r.grid.at(10, 2)).to_equal(RGB{2, 2, 2});
		expect(r.grid.at(17, 0)).to_equal(RGB{0, 0, 0});
	});
	it("spans the sidebar in the same coordinate space as the main pads", _ {
		uitest::GridRig r;
		// GridDimensions::cols() is main_cols + sidebar_cols and PadGrid::at addresses that one
		// range, so the root frame is 18 wide: x = 16, 17 are the sidebar columns.
		expect(r.frame.bounds()).to_equal(Rect{0, 0, 18, 8});
		r.frame.pad(17, 7, RGB{4, 5, 6});
		expect(r.grid.at(17, 7)).to_equal(RGB{4, 5, 6});
		expect(r.state()).to_equal(std::string("pad 17 7 4 5 6\n"));
		r.frame.pad(18, 0, RGB{7, 7, 7});   // past the last sidebar column: nothing to paint
		expect(r.state()).to_equal(std::string("pad 17 7 4 5 6\n"));
	});
	it("reports held from the level, not this tick's edges", _ {
		uitest::GridRig r;
		r.input.held_pads.set(2, 3, true);
		Response held = r.frame.pad(2, 3, RGB{1, 0, 0});
		expect(held.held).to_be_true();
		expect(held.pressed).to_be_false();       // the press was some earlier tick
		expect(held.released).to_be_false();
		expect(held.velocity).to_equal(uint8_t(0));
		Response idle = r.frame.pad(2, 4, RGB{1, 0, 0});
		expect(idle.held).to_be_false();
		// held_pads is absolute, so a region reads the same pad through its own local coordinates.
		r.frame.region(Rect{2, 2, 4, 4}, [&](GridFrame& f) {
			expect(f.pad(0, 1, RGB{1, 0, 0}).held).to_be_true();
			expect(f.pad(0, 0, RGB{1, 0, 0}).held).to_be_false();
		});
	});
	it("reports a release, and both flags when a pad is pressed and released in one tick", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{1, 1, 0, false}, {6, 6, 90, true}, {6, 6, 0, false}};
		r.input.pads = ev;
		Response rel = r.frame.pad(1, 1, RGB{3, 3, 3});
		expect(rel.released).to_be_true();
		expect(rel.pressed).to_be_false();
		Response both = r.frame.pad(6, 6, RGB{3, 3, 3});
		expect(both.pressed).to_be_true();
		expect(both.released).to_be_true();
		expect(both.velocity).to_equal(uint8_t(90));   // the velocity is the press's
	});
	it("nests regions, translating and clipping at each level", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{12, 5, 77, true}};
		r.input.pads = ev;
		int deep_hits = 0;
		r.frame.region(Rect{8, 0, 8, 8}, [&](GridFrame& outer) {
			expect(outer.bounds()).to_equal(Rect{0, 0, 8, 8});
			expect(outer.local_events().size()).to_equal(size_t(1));
			expect(int(outer.local_events()[0].x)).to_equal(4);
			outer.region(Rect{4, 4, 4, 4}, [&](GridFrame& inner) {
				expect(inner.bounds()).to_equal(Rect{0, 0, 4, 4});
				expect(inner.local_events().size()).to_equal(size_t(1));
				expect(int(inner.local_events()[0].x)).to_equal(0);
				expect(int(inner.local_events()[0].y)).to_equal(1);
				if (inner.pad(0, 1, RGB{8, 8, 8}).pressed) deep_hits++;
				inner.pad(4, 0, RGB{5, 5, 5});   // outside the INNER region, inside the outer
			});
			// A child that overhangs its parent is clipped to it, not to the grid.
			outer.region(Rect{6, 0, 8, 2}, [&](GridFrame& clipped) {
				expect(clipped.bounds()).to_equal(Rect{0, 0, 2, 2});
				clipped.pad(3, 0, RGB{6, 6, 6});  // would be absolute (17,0) but for the clip
			});
		});
		expect(deep_hits).to_equal(1);
		expect(r.grid.at(12, 5)).to_equal(RGB{8, 8, 8});
		expect(r.grid.at(16, 4)).to_equal(RGB{0, 0, 0});   // the inner region's overhang
		expect(r.grid.at(17, 0)).to_equal(RGB{0, 0, 0});   // the clipped child's overhang
	});
	it("drops an event that lands in no region, and never double-routes one", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{13, 7, 64, true}};      // below and right of every region opened below
		r.input.pads = ev;
		int seen = 0;
		r.frame.region(Rect{0, 0, 8, 4}, [&](GridFrame& f) { seen += int(f.local_events().size()); });
		r.frame.region(Rect{8, 0, 4, 4}, [&](GridFrame& f) { seen += int(f.local_events().size()); });
		r.frame.region(Rect{0, 4, 8, 4}, [&](GridFrame& f) { seen += int(f.local_events().size()); });
		expect(seen).to_equal(0);
		// The root still sees it: the event is unrouted, not lost.
		expect(r.frame.local_events().size()).to_equal(size_t(1));
		expect(int(r.frame.local_events()[0].x)).to_equal(13);
	});
	it("keeps the first kMaxLocalEvents when a tick overflows the buffer", _ {
		uitest::GridRig r;
		PadEvent ev[40];
		for (int i = 0; i < 40; i++) {
			ev[i] = PadEvent{uint8_t(i % 18), uint8_t(i / 18), uint8_t(i + 1), true};
		}
		r.input.pads = ev;
		expect(size_t(kMaxLocalEvents)).to_equal(size_t(32));
		expect(r.frame.local_events().size()).to_equal(size_t(32));
		expect(int(r.frame.local_events()[0].velocity)).to_equal(1);
		expect(int(r.frame.local_events()[31].velocity)).to_equal(32);   // the 32nd, not the 40th
		// pad() does not go through the buffer, so a dropped event is still hit-tested.
		expect(r.frame.pad(3, 2, RGB{1, 1, 1}).pressed).to_be_true();    // event 39
		expect(int(r.frame.pad(3, 2, RGB{1, 1, 1}).velocity)).to_equal(40);
	});
	it("re-filters when the snapshot's events are swapped under a reused frame", _ {
		// The frame is per-tick by contract (ui/grid/frame.h), but the cache must not *depend* on
		// that: it is keyed on the snapshot's `pads` span, not on "have I run before", so the one
		// long-lived frame a rig (or a shell that reuses frames) holds still sees each new tick.
		uitest::GridRig r;
		PadEvent tick1[] = {{4, 1, 64, true}};
		r.input.pads = tick1;
		expect(r.frame.local_events().size()).to_equal(size_t(1));
		expect(int(r.frame.local_events()[0].x)).to_equal(4);

		PadEvent tick2[] = {{9, 6, 100, true}, {2, 0, 0, false}};
		r.input.pads = tick2;                                            // same frame, new events
		expect(r.frame.local_events().size()).to_equal(size_t(2));
		expect(int(r.frame.local_events()[0].x)).to_equal(9);
		expect(int(r.frame.local_events()[1].x)).to_equal(2);

		r.input.pads = {};                                               // and a tick with none
		expect(r.frame.local_events().size()).to_equal(size_t(0));
	});
	it("finds the first press inside a rectangle, in frame coordinates", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{2, 1, 30, false}, {5, 6, 77, true}, {5, 2, 90, true}, {12, 0, 40, true}};
		r.input.pads = ev;
		// Column 5, all eight rows: the release at (2,1) and the press at (12,0) are outside it,
		// and of the two presses inside, the first *in event order* wins -- not the topmost.
		const std::optional<PadEvent> hit = r.frame.first_press_in(Rect{5, 0, 1, 8});
		expect(hit.has_value()).to_be_true();
		expect(int(hit->x)).to_equal(5);
		expect(int(hit->y)).to_equal(6);
		expect(int(hit->velocity)).to_equal(77);
		// Coordinates are this frame's, not the rectangle's: inside a region they are the
		// region's, which is what a column component's caller wants.
		r.frame.region(Rect{4, 0, 4, 8}, [&](GridFrame& f) {
			const std::optional<PadEvent> inner = f.first_press_in(Rect{1, 0, 1, 8});
			expect(inner.has_value()).to_be_true();
			expect(int(inner->x)).to_equal(1);   // 5 absolute, 1 in the region
			expect(int(inner->y)).to_equal(6);
		});
		// A rectangle with no press in it, and one entirely off the frame, both report nothing.
		expect(r.frame.first_press_in(Rect{0, 0, 2, 8}).has_value()).to_be_false();
		expect(r.frame.first_press_in(Rect{40, 0, 4, 8}).has_value()).to_be_false();
	});
});
// clang-format on

// Not CPPSPEC_SPEC(grid_frame): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_grid_frame_spec`,
// which does not match the `grid_frame` describe-block name the macro would assume. Spelled out
// by hand so the exported symbol matches what the generated driver declares `extern` (see
// ui/oled_frame_spec.cpp for the same pattern).
int ui_grid_frame_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(grid_frame).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
