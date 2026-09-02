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
// and ::renderColumn (:1268). Every assertion here compares the port against a transcription of
// *legacy's own body*, held in this file, rather than against literals the port and the spec could
// drift to together -- the same discipline waveform_grid_spec.cpp uses for the renderer it sits on.

namespace {

constexpr int32_t kDisplayHeight = 8;
constexpr int32_t kDisplayWidth = 16;
constexpr int32_t kNumMarkerTypes = 4;

// A stand-in for PadLEDs::image: 8 rows x (16 main + 2 sidebar) columns, which is the shape
// legacy's `RGB thisImage[kDisplayHeight][kDisplayWidth + kSideBarWidth]` has.
using LegacyImage = std::array<std::array<RGB, kDisplayWidth + 2>, kDisplayHeight>;

// --- SampleMarkerEditor::renderMarkerInCol (:1058), minus its reverseRemap/colour switch ---------
// Legacy resolves MarkerType -> colour inside the function; the port takes the colour, because
// `reverseRemap` reads SampleControls. The blend is what is under test, so the transcription starts
// where the colour is already known.
void legacy_render_marker_in_col(LegacyImage& image, int32_t xDisplay, RGB markerColour, int32_t yStart, int32_t yEnd,
                                 bool dimmly) {
	if (dimmly) {
		markerColour = markerColour.dim(3);
	}
	for (int32_t y = yStart; y < yEnd; ++y) {
		uint8_t value = image[y][xDisplay][0];
		value /= 4;
		image[y][xDisplay] =
		    markerColour.transform([value](uint8_t a) { return value + static_cast<uint8_t>((uint32_t{a} * 3) / 4); });
	}
}

// --- SampleMarkerEditor::renderColumn (:1268), transcribed --------------------------------------
void legacy_render_column(LegacyImage& image, int32_t col, const std::array<int32_t, kNumMarkerTypes>& colOnScreen,
                          int32_t selectedMarker, int32_t supressMask,
                          const std::array<RGB, kNumMarkerTypes>& colours) {
	int32_t numMarkers = 0;
	int32_t activeMarkers = 0;

	for (int32_t markerColIdx = 0; markerColIdx < kNumMarkerTypes; ++markerColIdx) {
		if (colOnScreen[markerColIdx] == col) {
			activeMarkers |= 1 << markerColIdx;
			++numMarkers;
		}
	}

	if (numMarkers == 0) {
		return;
	}

	int32_t marker = 0;
	uint32_t markerMask = 1 << marker;
	while ((markerMask & activeMarkers) == 0) {
		markerMask <<= 1;
		++marker;
	}

	uint32_t selectedMarkerMask = selectedMarker >= 0 ? (1u << selectedMarker) : 0u;
	int32_t prevYEnd = kDisplayHeight;
	int32_t seenMarkers = 0;

	int32_t baseIncrement = 0;
	int32_t selectedIncrement = 0;
	switch (numMarkers) {
	case 1:
		baseIncrement = selectedIncrement = kDisplayHeight / 2;
		break;
	case 2:
		baseIncrement = selectedIncrement = 2;
		break;
	case 3:
		baseIncrement = 1;
		selectedIncrement = 2;
		break;
	case 4:
		baseIncrement = selectedIncrement = 1;
		break;
	default:
		break;
	}

	while (markerMask != (1u << kNumMarkerTypes)) {
		if ((markerMask & activeMarkers) == 0) {
			markerMask <<= 1;
			++marker;
			continue;
		}

		bool isSelected = (markerMask & selectedMarkerMask) != 0;
		seenMarkers++;

		int32_t yEnd = prevYEnd;
		int32_t yStart = 0;

		if (seenMarkers == numMarkers) {
			yStart = kDisplayHeight / 2;
		}
		else {
			yStart = yEnd - (isSelected ? selectedIncrement : baseIncrement);
		}

		bool allowed = (markerMask & static_cast<uint32_t>(supressMask)) == 0u;
		if (allowed) {
			bool dim = !isSelected;
			legacy_render_marker_in_col(image, col, colours[marker], yStart, yEnd, dim);
			legacy_render_marker_in_col(image, col, colours[marker], kDisplayHeight - yEnd, kDisplayHeight - yStart,
			                            dim);
		}

		prevYEnd = yStart;
		markerMask <<= 1;
		++marker;
	}
}

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

LegacyImage legacy_underlay() {
	LegacyImage image{};
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			image[y][x] = RGB::monochrome(static_cast<uint8_t>(8 * x + 4 * y));
		}
	}
	return image;
}

// Compare the rig's main columns against a legacy image, pad for pad.
template <typename Self>
void expect_matches_legacy(Self& self, uitest::GridRig& r, const LegacyImage& image) {
	int32_t mismatches = 0;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (!(r.grid.at(x, y) == image[y][x])) {
				mismatches++;
			}
		}
	}
	expect(mismatches).to_equal(0);
}

} // namespace

// clang-format off
describe markers_grid("ui/grid/markers", $ {

	// --- marker_tint --------------------------------------------------------------------------

	it("blends three quarters of the marker colour over a quarter of the pad's red channel", _ {
		uitest::GridRig r;
		paint_underlay(r);
		LegacyImage image = legacy_underlay();

		r.frame.region(kMainArea, [](GridFrame& main) {
			marker_tint(main, 5, 2, 6, RGB{0, 255, 0}, false);
		});
		legacy_render_marker_in_col(image, 5, RGB{0, 255, 0}, 2, 6, false);

		// The exact value, derived by hand rather than by running the port: at (5, 3) the underlay
		// is monochrome(8*5 + 4*3) = monochrome(52), so `value` is 52/4 = 13, and green's channels
		// are (0, 255, 0) -> (13 + 0, 13 + 191, 13 + 0).
		expect(r.grid.at(5, 3) == RGB{13, 204, 13}).to_be_true();
		// Untouched rows keep the underlay exactly.
		expect(r.grid.at(5, 1) == RGB::monochrome(44)).to_be_true();
		expect(r.grid.at(5, 6) == RGB::monochrome(64)).to_be_true();
		expect_matches_legacy(self, r, image);
	});

	it("dims by three bits before blending, not after", _ {
		uitest::GridRig r;
		paint_underlay(r);
		LegacyImage image = legacy_underlay();

		r.frame.region(kMainArea, [](GridFrame& main) {
			marker_tint(main, 5, 3, 4, RGB{0, 255, 0}, true);
		});
		legacy_render_marker_in_col(image, 5, RGB{0, 255, 0}, 3, 4, true);

		// green.dim(3) is (0, 31, 0); the blend then adds 13 (the underlay's quarter) to
		// (31 * 3) / 4 = 23. Dimming *after* the blend would give (204 >> 3) = 25 instead.
		expect(r.grid.at(5, 3) == RGB{13, 36, 13}).to_be_true();
		expect_matches_legacy(self, r, image);
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
		LegacyImage image = legacy_underlay();

		MarkerStack stack{.active = 0b0001, .suppress = 0, .selected = 0};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 4, stack, kMarkerColours); });
		legacy_render_column(image, 4, {4, -2147483648, -2147483648, -2147483648}, 0, 0, kMarkerColours);

		// Every row of column 4 is tinted -- the two halves meet in the middle.
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			expect(r.grid.at(4, y) == legacy_underlay()[y][4]).to_be_false();
		}
		// ...and no neighbouring column moved.
		expect(r.grid.at(3, 4) == RGB::monochrome(40)).to_be_true();
		expect_matches_legacy(self, r, image);
	});

	it("stacks two markers two rows each, mirrored about the middle", _ {
		uitest::GridRig r;
		paint_underlay(r);
		LegacyImage image = legacy_underlay();

		// START and END both on column 7; END selected.
		MarkerStack stack{.active = 0b1001, .suppress = 0, .selected = 3};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 7, stack, kMarkerColours); });
		legacy_render_column(image, 7, {7, -2147483648, -2147483648, 7}, 3, 0, kMarkerColours);

		// The FIRST marker in index order (START, dim green) takes rows 6..7 and their mirror 0..1;
		// the LAST (END, selected red) fills the rest, rows 4..5 and 2..3. Legacy walks the mask
		// from the bottom index up while walking the column from its bottom row up, so the lowest
		// marker index ends up at the OUTSIDE of the column -- which is the thing a stacking port
		// is most likely to get backwards.
		expect(r.grid.at(7, 7).g > r.grid.at(7, 7).r).to_be_true(); // outer pair: green
		expect(r.grid.at(7, 0).g > r.grid.at(7, 0).r).to_be_true();
		expect(r.grid.at(7, 4).r > r.grid.at(7, 4).g).to_be_true(); // inner pair: red
		expect(r.grid.at(7, 3).r > r.grid.at(7, 3).g).to_be_true();
		expect_matches_legacy(self, r, image);
	});

	it("gives the selected marker two rows and the others one when three share a column", _ {
		uitest::GridRig r;
		paint_underlay(r);
		LegacyImage image = legacy_underlay();

		// START, LOOP_START and END on column 9; LOOP_START selected.
		MarkerStack stack{.active = 0b1011, .suppress = 0, .selected = 1};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 9, stack, kMarkerColours); });
		legacy_render_column(image, 9, {9, 9, -2147483648, 9}, 1, 0, kMarkerColours);

		// START (not selected) gets one row, 7. LOOP_START (selected) gets two, 5..6. END fills
		// 4 and the mirror. Swapping baseIncrement/selectedIncrement would put cyan on row 6 only.
		expect(r.grid.at(9, 7).g > r.grid.at(9, 7).b).to_be_true();  // green, one row
		expect(r.grid.at(9, 6).b > r.grid.at(9, 6).r).to_be_true();  // cyan
		expect(r.grid.at(9, 5).b > r.grid.at(9, 5).r).to_be_true();  // cyan, second row
		expect(r.grid.at(9, 4).r > r.grid.at(9, 4).g).to_be_true();  // red
		expect_matches_legacy(self, r, image);
	});

	it("stacks four markers one row each", _ {
		uitest::GridRig r;
		paint_underlay(r);
		LegacyImage image = legacy_underlay();

		MarkerStack stack{.active = 0b1111, .suppress = 0, .selected = 2};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 2, stack, kMarkerColours); });
		legacy_render_column(image, 2, {2, 2, 2, 2}, 2, 0, kMarkerColours);
		expect_matches_legacy(self, r, image);
	});

	it("skips a suppressed marker and leaves the pads it would have drawn untouched", _ {
		uitest::GridRig r;
		paint_underlay(r);
		LegacyImage image = legacy_underlay();

		// Two markers, the selected one suppressed -- the blink's phase-3 state.
		MarkerStack stack{.active = 0b1001, .suppress = 0b1000, .selected = 3};
		r.frame.region(kMainArea, [&](GridFrame& main) { marker_column(main, 7, stack, kMarkerColours); });
		legacy_render_column(image, 7, {7, -2147483648, -2147483648, 7}, 3, 0b1000, kMarkerColours);

		// Rows 2..5 (END's own, and their mirror) keep the underlay; rows 0,1,6,7 still get START.
		expect(r.grid.at(7, 4) == RGB::monochrome(72)).to_be_true();
		expect(r.grid.at(7, 3) == RGB::monochrome(68)).to_be_true();
		expect(r.grid.at(7, 7).g > 0).to_be_true();
		expect_matches_legacy(self, r, image);
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
