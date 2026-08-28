#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/grid/effects.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;

// The greyout overlay (PadLEDs::prepareColour) and the tap/hold gesture helper (the clip-press FSM
// in SessionView::padAction).

namespace {

const RGB kPainted{200, 100, 40};

// The colour legacy would leave: RGB::greyOut is the shared helper both sides call
// (pad_leds.cpp:295 -> gfx/grid/rgb.h:177), so the spec pins *which pads* change, not the maths.
RGB greyed(RGB c, int32_t amount) {
	return c.greyOut(amount);
}

uitest::GridRig painted_rig() {
	uitest::GridRig r;
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 18; x++) {
			r.frame.pad(x, y, kPainted);
		}
	}
	return r;
}

// Pad events must outlive the span the snapshot holds, so each step keeps its own vector.
struct Tick {
	std::vector<PadEvent> pads;
};

/// A *fresh* frame over the rig's grid, as the shell builds one per tick (ui/grid/frame.h: a
/// `GridFrame` is a per-tick object -- four references and a rectangle, free to build).
///
/// Reusing `GridRig::frame` across the ticks below would also work: `local_events()`' cache is
/// keyed on the snapshot's `pads` span, so installing a new tick's events invalidates it (see
/// grid_frame_spec's "re-filters when the snapshot's events are swapped"). This builds a fresh one
/// anyway, because that is the shape the shell has and the one the gesture FSM is specified for.
GestureEvent step(uitest::GridRig& r, Hold& hold, int32_t& second_x, int32_t& second_y) {
	GridFrame f = root_grid_frame(r.grid, r.input, r.gate);
	return gesture(f, hold, HoldPolicy{}, second_x, second_y);
}

constexpr int32_t kShortPressMs = 500; // definitions_cxx.hpp:1028 / qwerty_ui.cpp:220

} // namespace

// clang-format off
describe effects_spec("ui/grid/effects", $ {

	// --- greyout ------------------------------------------------------------------------------

	it("greys the column a cols bit selects, counting from the right", _ {
		// pad_leds.cpp:294 -- `greyoutCols & (1 << (kDisplayWidth + kSideBarWidth - 1 - x))`, so
		// bit 0 is the LAST column of the frame, not the first. Parity with legacy over the
		// brief's wording (see the amendment note in effects.h).
		uitest::GridRig r = painted_rig();
		greyout(r.frame, GreyoutMask{.cols = 1u << 0}, 0x100000);

		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(17, y)).to_equal(greyed(kPainted, 0x100000));
			expect(r.grid.at(16, y)).to_equal(kPainted);
			expect(r.grid.at(0, y)).to_equal(kPainted);
		}
	});

	it("greys column 0 from the top cols bit", _ {
		uitest::GridRig r = painted_rig();
		greyout(r.frame, GreyoutMask{.cols = 1u << 17}, 0x100000);

		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(0, y)).to_equal(greyed(kPainted, 0x100000));
			expect(r.grid.at(1, y)).to_equal(kPainted);
		}
	});

	it("greys the row a rows bit selects, counting from row 0", _ {
		uitest::GridRig r = painted_rig();
		greyout(r.frame, GreyoutMask{.rows = 1u << 3}, 0x100000);

		for (int32_t x = 0; x < 18; x++) {
			expect(r.grid.at(x, 3)).to_equal(greyed(kPainted, 0x100000));
			expect(r.grid.at(x, 2)).to_equal(kPainted);
		}
	});

	it("ORs the two masks into a cross", _ {
		uitest::GridRig r = painted_rig();
		greyout(r.frame, GreyoutMask{.cols = 1u << 17, .rows = 1u << 5}, 0x100000);

		expect(r.grid.at(0, 0)).to_equal(greyed(kPainted, 0x100000)); // column hit
		expect(r.grid.at(9, 5)).to_equal(greyed(kPainted, 0x100000)); // row hit
		expect(r.grid.at(0, 5)).to_equal(greyed(kPainted, 0x100000)); // both
		expect(r.grid.at(9, 0)).to_equal(kPainted);                   // neither
	});

	it("does nothing for an empty mask", _ {
		uitest::GridRig r = painted_rig();
		greyout(r.frame, GreyoutMask{}, 0x100000); // pad_leds.cpp:293's guard

		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 18; x++) {
				expect(r.grid.at(x, y)).to_equal(kPainted);
			}
		}
	});

	it("counts column bits against the frame it is given", _ {
		uitest::GridRig r = painted_rig();
		r.frame.region(Rect{4, 0, 4, 8}, [&](GridFrame& sub) { greyout(sub, GreyoutMask{.cols = 1u << 0}, 0x100000); });
		// Bit 0 is the region's own last column (absolute x = 7), and nothing outside moves.
		expect(r.grid.at(7, 0)).to_equal(greyed(kPainted, 0x100000));
		expect(r.grid.at(6, 0)).to_equal(kPainted);
		expect(r.grid.at(17, 0)).to_equal(kPainted);
	});

	it("records a greyed-out grid", _ {
		uitest::GridRig r = painted_rig();
		greyout(r.frame, GreyoutMask{.cols = (1u << 17) | (1u << 0), .rows = 1u << 4}, 0x400000);
		expect_golden(self, "../ui/widgets/greyout.state", r.state());
	});

	// --- gesture ------------------------------------------------------------------------------

	it("reports a tap when the pad is released inside the short-press window", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 5, .y = 2, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 0;
		expect(step(r, hold, sx, sy) == GestureEvent::None).to_be_true();
		expect(hold.held).to_be_true();
		expect((int32_t)hold.x).to_equal(5);
		expect((int32_t)hold.y).to_equal(2);

		Tick up{{{.x = 5, .y = 2, .velocity = 0, .pressed = false}}};
		r.input.pads = up.pads;
		r.input.now_ms = 100;
		expect(step(r, hold, sx, sy) == GestureEvent::Tap).to_be_true();
		expect(hold.held).to_be_false(); // session_view.cpp:966-975, the press is over
	});

	it("reports a hold exactly once when the deadline passes", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 5, .y = 2, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 0;
		step(r, hold, sx, sy);
		r.input.held_pads.set(5, 2, true);

		// A tick before the deadline: nothing.
		r.input.pads = {};
		r.input.now_ms = 499;
		expect(step(r, hold, sx, sy) == GestureEvent::None).to_be_true();

		r.input.now_ms = 600;
		expect(step(r, hold, sx, sy) == GestureEvent::Hold).to_be_true();
		// One-shot, like the timer at qwerty_ui.cpp:220.
		r.input.now_ms = 700;
		expect(step(r, hold, sx, sy) == GestureEvent::None).to_be_true();
		expect(hold.held).to_be_true();
	});

	it("arms the repaint deadline for the hold", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 5, .y = 2, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 40;
		step(r, hold, sx, sy);

		expect(r.gate.next_deadline().has_value()).to_be_true();
		expect((int32_t)r.gate.next_deadline().value()).to_equal(40 + kShortPressMs);
	});

	it("reports a release when the pad goes up after the deadline", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 5, .y = 2, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 0;
		step(r, hold, sx, sy);
		r.input.held_pads.set(5, 2, true);

		r.input.pads = {};
		r.input.now_ms = 600;
		expect(step(r, hold, sx, sy) == GestureEvent::Hold).to_be_true();

		// arranger_view.cpp:1307 -- `>= kShortPressTime` exits the sub-mode with no action.
		Tick up{{{.x = 5, .y = 2, .velocity = 0, .pressed = false}}};
		r.input.pads = up.pads;
		r.input.now_ms = 900;
		expect(step(r, hold, sx, sy) == GestureEvent::Release).to_be_true();
		expect(hold.held).to_be_false();
	});

	it("reports the second pad's coordinates when another pad is pressed", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 5, .y = 2, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 0;
		step(r, hold, sx, sy);
		r.input.held_pads.set(5, 2, true);

		// session_view.cpp:884-902 -- "clone it to newly-pressed row", which ends the press.
		Tick second{{{.x = 9, .y = 6, .velocity = 64, .pressed = true}}};
		r.input.pads = second.pads;
		r.input.now_ms = 120;
		expect(step(r, hold, sx, sy) == GestureEvent::SecondPad).to_be_true();
		expect(sx).to_equal(9);
		expect(sy).to_equal(6);
		expect(hold.held).to_be_false();
	});

	it("ignores a release of some other pad", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 5, .y = 2, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 0;
		step(r, hold, sx, sy);

		Tick other{{{.x = 1, .y = 1, .velocity = 0, .pressed = false}}};
		r.input.pads = other.pads;
		r.input.now_ms = 50;
		expect(step(r, hold, sx, sy) == GestureEvent::None).to_be_true();
		expect(hold.held).to_be_true();
	});

	it("handles a press and its release arriving in the same tick", _ {
		// Legacy runs padAction once per event (session_view.cpp:817-975), so a tick carrying both
		// edges still taps. Returning on the first event would have started a hold and never ended
		// it -- local_events() is rebuilt per frame, so the release would be lost, not deferred.
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick both{{{.x = 5, .y = 2, .velocity = 64, .pressed = true},
		           {.x = 5, .y = 2, .velocity = 0, .pressed = false}}};
		r.input.pads = both.pads;
		r.input.now_ms = 0;
		expect(step(r, hold, sx, sy) == GestureEvent::Tap).to_be_true();
		expect(hold.held).to_be_false();
	});

	it("handles two different pads pressed in the same tick", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick two{{{.x = 5, .y = 2, .velocity = 64, .pressed = true},
		          {.x = 9, .y = 6, .velocity = 64, .pressed = true}}};
		r.input.pads = two.pads;
		r.input.now_ms = 0;
		expect(step(r, hold, sx, sy) == GestureEvent::SecondPad).to_be_true();
		expect(sx).to_equal(9);
		expect(sy).to_equal(6);
		expect(hold.held).to_be_false(); // session_view.cpp:901 ends the press
	});

	it("keeps tracking a pad pressed after another was released in the same tick", _ {
		// The concrete drop the single-return loop caused: [release A, press B] reported the tap
		// and never started B's hold.
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 5, .y = 2, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 0;
		step(r, hold, sx, sy);

		Tick mixed{{{.x = 5, .y = 2, .velocity = 0, .pressed = false},
		            {.x = 9, .y = 6, .velocity = 64, .pressed = true}}};
		r.input.pads = mixed.pads;
		r.input.now_ms = 100;
		expect(step(r, hold, sx, sy) == GestureEvent::Tap).to_be_true();
		expect(hold.held).to_be_true(); // B is now the tracked pad
		expect((int32_t)hold.x).to_equal(9);
		expect((int32_t)hold.y).to_equal(6);
		expect((int32_t)hold.down_ms).to_equal(100);

		// ...and B behaves like any other press from here.
		Tick up_b{{{.x = 9, .y = 6, .velocity = 0, .pressed = false}}};
		r.input.pads = up_b.pads;
		r.input.now_ms = 800;
		expect(step(r, hold, sx, sy) == GestureEvent::Release).to_be_true();
	});

	it("uses frame-local coordinates inside a region", _ {
		uitest::GridRig r;
		Hold hold;
		int32_t sx = -1, sy = -1;

		Tick down{{{.x = 6, .y = 3, .velocity = 64, .pressed = true}}};
		r.input.pads = down.pads;
		r.input.now_ms = 0;
		r.frame.region(Rect{4, 2, 4, 4}, [&](GridFrame& sub) { gesture(sub, hold, HoldPolicy{}, sx, sy); });

		expect((int32_t)hold.x).to_equal(2); // 6 - 4
		expect((int32_t)hold.y).to_equal(1); // 3 - 2
	});
});
// clang-format on

// Hand-spelled exporter: create_test_sourcelist derives the extern name from this file's path
// relative to tests/spec, i.e. `ui_effects_spec`.
int ui_effects_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(effects_spec).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
