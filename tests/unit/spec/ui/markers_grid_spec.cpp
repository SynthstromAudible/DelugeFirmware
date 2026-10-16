#include "../gfx/support/golden.h"
#include "gfx/grid/palette.h"
#include "support/frames.h"
#include "ui/grid/markers.h"
#include "ui/grid/waveform.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;
namespace palette = deluge::gui::colours;

// The marker overlay, ported from SampleMarkerEditor::renderMarkerInCol (sample_marker_editor.cpp:1058)
// and ::renderColumn (:1268).
//
// **The legacy comparison is NOT here.** It used to be: this file held a transcription of those two
// bodies and compared the port against it, which one misreading written once would have satisfied
// on both sides (E3 final review, B2). It now lives in sim/scenes_spec/marker_scene_spec.cpp's
// "against the real legacy editor" group, which calls the shipped renderers themselves through the
// public SampleMarkerEditor::renderMainPads -- the sim spec driver links deluge_app and this host
// target cannot. Five layouts are compared there: markers apart, three sharing a column, four
// sharing a column, and every blink phase.
//
// What is left here is the widget's own contract at unit scale, on values derived by hand from
// legacy's arithmetic rather than by running the port. Keep it that way: a comparison against a
// second reading of legacy belongs where the first reading can be executed.

namespace {

constexpr int32_t kDisplayHeight = 8;
constexpr int32_t kDisplayWidth = 16;
constexpr int32_t kNumMarkerTypes = 4;

// Legacy's four marker colours, in MarkerType order (sample_marker_editor.cpp:1071-1082). Spelled
// out here rather than reached through `gui/colour/colour.h` so the spec pins the values as well as
// the arithmetic.
const std::array<RGB, kNumMarkerTypes> kMarkerColours{
    RGB{0, 255, 0},   // START -- colours::green
    RGB{0, 255, 255}, // LOOP_START -- colours::cyan
    RGB{255, 0, 255}, // LOOP_END -- colours::magenta
    RGB{255, 0, 0},   // END -- colours::red
};

constexpr Rect kMainArea{0, 0, kDisplayWidth, kDisplayHeight};

// A recognisable, non-uniform underlay so the blend has something to read: the marker keeps a
// quarter of the pad's RED channel, so a flat black background could not tell a working blend from
// one that ignored the underlay entirely.
void paint_underlay(uitest::GridRig& r) {
	r.frame.region(kMainArea, [](GridFrame& main) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				main.pad(x, y, RGB::monochrome(static_cast<uint8_t>(8 * x + 4 * y)));
			}
		}
	});
}

} // namespace

// clang-format off
describe markers_grid("ui/grid/markers", $ {

	// --- marker_tint --------------------------------------------------------------------------

	it("blends three quarters of the marker colour over a quarter of the pad's red channel", _ {
		uitest::GridRig r;
		paint_underlay(r);

		r.frame.region(kMainArea, [](GridFrame& main) {
			marker_tint(main, 5, 2, 6, RGB{0, 255, 0}, false);
		});

		// The exact value, derived by hand rather than by running the port: at (5, 3) the underlay
		// is monochrome(8*5 + 4*3) = monochrome(52), so `value` is 52/4 = 13, and green's channels
		// are (0, 255, 0) -> (13 + 0, 13 + 191, 13 + 0).
		expect(r.grid.at(5, 3) == RGB{13, 204, 13}).to_be_true();
		// Untouched rows keep the underlay exactly.
		expect(r.grid.at(5, 1) == RGB::monochrome(44)).to_be_true();
		expect(r.grid.at(5, 6) == RGB::monochrome(64)).to_be_true();
	});

	it("dims by three bits before blending, not after", _ {
		uitest::GridRig r;
		paint_underlay(r);

		r.frame.region(kMainArea, [](GridFrame& main) {
			marker_tint(main, 5, 3, 4, RGB{0, 255, 0}, true);
		});

		// green.dim(3) is (0, 31, 0); the blend then adds 13 (the underlay's quarter) to
		// (31 * 3) / 4 = 23. Dimming *after* the blend would give (204 >> 3) = 25 instead.
		expect(r.grid.at(5, 3) == RGB{13, 36, 13}).to_be_true();
	});

	it("reads the red channel alone, so a green-only underlay contributes nothing", _ {
		// The header's note, as an assertion: legacy takes `thisImage[y][x][0]`, and this is the
		// difference between that and taking the pad's brightness.
		uitest::GridRig r;
		r.frame.region(kMainArea, [](GridFrame& main) {
			main.pad(2, 4, RGB{0, 200, 0});
			marker_tint(main, 2, 4, 5, RGB{255, 0, 0}, false);
		});
		expect(r.grid.at(2, 4) == RGB{191, 0, 0}).to_be_true();
	});

	// --- marker_column ------------------------------------------------------------------------

	it("fills the whole column when one marker is on it", _ {
		uitest::GridRig r;
		paint_underlay(r);

		MarkerStack stack{.active = 0b0001, .suppress = 0, .selected = 0};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 4, stack, kMarkerColours); });

		// Every row of column 4 is tinted -- the two halves meet in the middle.
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			// The underlay at (4, y) is monochrome(8*4 + 4*y); any tint at all changes it.
			expect(r.grid.at(4, y) == RGB::monochrome(static_cast<uint8_t>(32 + 4 * y))).to_be_false();
		}
		// ...and no neighbouring column moved.
		expect(r.grid.at(3, 4) == RGB::monochrome(40)).to_be_true();
	});

	it("stacks two markers two rows each, mirrored about the middle", _ {
		uitest::GridRig r;
		paint_underlay(r);

		// START and END both on column 7; END selected.
		MarkerStack stack{.active = 0b1001, .suppress = 0, .selected = 3};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 7, stack, kMarkerColours); });

		// The FIRST marker in index order (START, dim green) takes rows 6..7 and their mirror 0..1;
		// the LAST (END, selected red) fills the rest, rows 4..5 and 2..3. Legacy walks the mask
		// from the bottom index up while walking the column from its bottom row up, so the lowest
		// marker index ends up at the OUTSIDE of the column -- which is the thing a stacking port
		// is most likely to get backwards.
		expect(r.grid.at(7, 7).g > r.grid.at(7, 7).r).to_be_true(); // outer pair: green
		expect(r.grid.at(7, 0).g > r.grid.at(7, 0).r).to_be_true();
		expect(r.grid.at(7, 4).r > r.grid.at(7, 4).g).to_be_true(); // inner pair: red
		expect(r.grid.at(7, 3).r > r.grid.at(7, 3).g).to_be_true();
	});

	it("gives the selected marker two rows and the others one when three share a column", _ {
		uitest::GridRig r;
		paint_underlay(r);

		// START, LOOP_START and END on column 9; LOOP_START selected.
		MarkerStack stack{.active = 0b1011, .suppress = 0, .selected = 1};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 9, stack, kMarkerColours); });

		// START (not selected) gets one row, 7. LOOP_START (selected) gets two, 5..6. END fills
		// 4 and the mirror. Swapping baseIncrement/selectedIncrement would put cyan on row 6 only.
		expect(r.grid.at(9, 7).g > r.grid.at(9, 7).b).to_be_true();  // green, one row
		expect(r.grid.at(9, 6).b > r.grid.at(9, 6).r).to_be_true();  // cyan
		expect(r.grid.at(9, 5).b > r.grid.at(9, 5).r).to_be_true();  // cyan, second row
		expect(r.grid.at(9, 4).r > r.grid.at(9, 4).g).to_be_true();  // red
	});

	it("stacks four markers one row each", _ {
		uitest::GridRig r;
		paint_underlay(r);

		// LOOP_END selected, so the one undimmed row is magenta.
		MarkerStack stack{.active = 0b1111, .suppress = 0, .selected = 2};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 2, stack, kMarkerColours); });

		// One row each, in index order from the OUTSIDE in, then mirrored: green 7/0, cyan 6/1,
		// magenta 5/2, red 4/3. Derived from legacy's arithmetic by hand -- with four markers
		// `baseIncrement` and `selectedIncrement` are both 1, and the last marker walked takes
		// `yStart = kDisplayHeight / 2`.
		expect(r.grid.at(2, 7).g > r.grid.at(2, 7).b).to_be_true(); // green
		expect(r.grid.at(2, 6).b > r.grid.at(2, 6).r).to_be_true(); // cyan
		expect(r.grid.at(2, 6).g > r.grid.at(2, 6).r).to_be_true();
		expect(r.grid.at(2, 5).r > r.grid.at(2, 5).g).to_be_true(); // magenta, selected
		expect(r.grid.at(2, 5).b > r.grid.at(2, 5).g).to_be_true();
		expect(r.grid.at(2, 4).r > r.grid.at(2, 4).g).to_be_true(); // red
		expect(r.grid.at(2, 4).r > r.grid.at(2, 4).b).to_be_true();
		// The mirror half carries the same four in the same order.
		expect(r.grid.at(2, 0).g > r.grid.at(2, 0).b).to_be_true();
		expect(r.grid.at(2, 1).b > r.grid.at(2, 1).r).to_be_true();
		expect(r.grid.at(2, 2).r > r.grid.at(2, 2).g).to_be_true();
		expect(r.grid.at(2, 3).r > r.grid.at(2, 3).b).to_be_true();
		// Only the selected marker is undimmed, so its row is far brighter than its neighbour's.
		expect(r.grid.at(2, 5).r > r.grid.at(2, 4).r).to_be_true();
	});

	it("skips a suppressed marker and leaves the pads it would have drawn untouched", _ {
		uitest::GridRig r;
		paint_underlay(r);

		// Two markers, the selected one suppressed -- the blink's phase-3 state.
		MarkerStack stack{.active = 0b1001, .suppress = 0b1000, .selected = 3};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 7, stack, kMarkerColours); });

		// Rows 2..5 (END's own, and their mirror) keep the underlay; rows 0,1,6,7 still get START.
		expect(r.grid.at(7, 4) == RGB::monochrome(72)).to_be_true();
		expect(r.grid.at(7, 3) == RGB::monochrome(68)).to_be_true();
		expect(r.grid.at(7, 7).g > 0).to_be_true();
	});

	it("paints nothing at all for a column no marker lands on", _ {
		uitest::GridRig r;
		paint_underlay(r);
		MarkerStack stack{.active = 0, .suppress = 0, .selected = 0};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 11, stack, kMarkerColours); });
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			expect(r.grid.at(11, y) == RGB::monochrome(static_cast<uint8_t>(88 + 4 * y))).to_be_true();
		}
	});

	// --- golden -------------------------------------------------------------------------------

	it("draws a whole marker set over a waveform", _ {
		uitest::GridRig r;
		// A recognisable waveform underneath, so the golden shows the blend rather than markers on
		// black: a widening triangle across the sixteen columns.
		std::array<PeakCol, 16> cols{};
		for (int32_t x = 0; x < 16; x++) {
			const int32_t amp = (x + 1) << 22;
			cols[static_cast<size_t>(x)] = PeakCol{.min24 = -amp, .max24 = amp, .valid = true};
		}
		r.frame.region(kMainArea, [&](GridFrame& main) {
			waveform_full(main, cols, kWaveformBarBrightnessMono, std::nullopt);
			// START at 2, LOOP_START and LOOP_END sharing 8, END at 13, LOOP_END selected.
			marker_column(main, 2, MarkerStack{.active = 0b0001, .selected = 2}, kMarkerColours);
			marker_column(main, 8, MarkerStack{.active = 0b0110, .selected = 2}, kMarkerColours);
			marker_column(main, 13, MarkerStack{.active = 0b1000, .selected = 2}, kMarkerColours);
		});
		expect_golden(self, "../ui/widgets/markers_over_waveform.state", r.state());
	});
});
// clang-format on
DELUGE_SPEC(ui_markers_grid_spec, markers_grid)
