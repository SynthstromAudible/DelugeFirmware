#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/grid/waveform.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;
namespace palette = deluge::gui::colours;

// Waveform-on-pads rendering (spec B section 7), ported from WaveformRenderer. Every colour
// assertion below transcribes *legacy's* expression and compares the port against that, rather
// than against literals the port and the spec could drift to together.

namespace {

// --- WaveformRenderer::renderAsSingleRow's colour body (waveform_renderer.cpp:137-146) --------
RGB legacy_single_row_colour(int32_t brightness, RGB rgb) {
	int32_t colourValue = (brightness * brightness);
	return rgb.transform([colourValue](auto channel) {
		int32_t valueHere = (colourValue * channel) >> 16;
		return std::clamp<int32_t>((valueHere + 6) & ~15, 0, RGB::channel_max);
	});
}

// --- WaveformRenderer::drawColBar (waveform_renderer.cpp:731-762), transcribed ----------------
// Writes into an 8-row column so the spec can compare a whole column against the port.
void legacy_draw_col_bar(std::array<RGB, 8>& column, int32_t min24, int32_t max24, int32_t brightness,
                         std::optional<RGB> rgb) {
	constexpr int32_t kDisplayHeight = 8;
	int32_t yStart = std::max((int32_t)(min24 >> 24), -(kDisplayHeight >> 1));
	int32_t yStop = std::min((int32_t)(max24 >> 24) + 1, kDisplayHeight >> 1);

	for (int32_t y = yStart; y < yStop; y++) {
		int32_t colourAmount;
		if (y == (min24 >> 24)) {
			int32_t howMuchThisSquare = (min24 - (y << 24)) >> 16;
			colourAmount = brightness - ((howMuchThisSquare * brightness) >> 8);
		}
		else if (y < (max24 >> 24)) {
			colourAmount = brightness;
		}
		else {
			int32_t howMuchThisSquare = (max24 - (y << 24)) >> 16;
			colourAmount = ((howMuchThisSquare * brightness) >> 8);
		}
		int32_t valueHere = (colourAmount * colourAmount) >> 8;
		column[y + (kDisplayHeight >> 1)] =
		    rgb.has_value() ? rgb.value().transform([valueHere](auto channel) { return (valueHere * channel) >> 8; })
		                    : RGB::monochrome(valueHere);
	}
}

// One whole pad row of a 16-wide waveform, so `reversed` mirrors about the width legacy uses.
constexpr Rect kMainArea{0, 0, 16, 8};

const RGB kBase{200, 40, 120};

} // namespace

// clang-format off
describe waveform_grid("ui/grid/waveform", $ {

	// --- waveform_row -------------------------------------------------------------------------

	it("paints a lit column through the legacy colour formula and a zero column black", _ {
		uitest::GridRig r;
		const int32_t brightness[] = {255, 0};
		r.frame.region(kMainArea, [&](GridFrame& main) { waveform_row(main, 3, brightness, kBase, false); });

		expect(r.grid.at(0, 3)).to_equal(legacy_single_row_colour(255, kBase));
		expect(r.grid.at(1, 3)).to_equal(palette::black);
		// Columns past the span have no data: legacy paints those black too (:127-130).
		expect(r.grid.at(15, 3)).to_equal(palette::black);
	});

	it("quantises every channel to legacy's 16-step ladder", _ {
		// The `& ~15` at :145 is the whole point of the formula; check it holds across the range
		// rather than only at the extremes.
		for (int32_t b : {0, 1, 17, 64, 100, 128, 200, 255, kWaveformMaxBrightness}) {
			expect(waveform_col_colour(b, kBase)).to_equal(legacy_single_row_colour(b, kBase));
			const RGB c = waveform_col_colour(b, kBase);
			expect((int32_t)(c.r & 15)).to_equal(0);
			expect((int32_t)(c.g & 15)).to_equal(0);
			expect((int32_t)(c.b & 15)).to_equal(0);
		}
	});

	it("accepts legacy's full 0..256 brightness range", _ {
		// getColBrightnessForSingleRow ends with min(peak16 >> 8, 256) (:168) -- one past uint8_t,
		// which is why the span is int32_t. 256 must not wrap to black.
		expect(waveform_col_colour(kWaveformMaxBrightness, kBase))
		    .to_equal(legacy_single_row_colour(256, kBase));
		expect(waveform_col_colour(kWaveformMaxBrightness, kBase) == palette::black).to_be_false();
	});

	it("mirrors the source columns when reversed", _ {
		uitest::GridRig r;
		int32_t brightness[16] = {};
		brightness[0] = 255;
		brightness[15] = 96;
		r.frame.region(kMainArea, [&](GridFrame& main) { waveform_row(main, 0, brightness, kBase, true); });

		// :131-134 -- output column x reads source column (width - 1 - x).
		expect(r.grid.at(0, 0)).to_equal(legacy_single_row_colour(96, kBase));
		expect(r.grid.at(15, 0)).to_equal(legacy_single_row_colour(255, kBase));
	});

	it("records a full 16-column single row", _ {
		uitest::GridRig r;
		int32_t brightness[16];
		for (int32_t x = 0; x < 16; x++) {
			brightness[x] = (x * kWaveformMaxBrightness) / 15;
		}
		r.frame.region(kMainArea, [&](GridFrame& main) { waveform_row(main, 4, brightness, kBase, false); });
		expect_golden(self, "../ui/widgets/waveform_row.state", r.state());
	});

	// --- waveform_full ------------------------------------------------------------------------

	it("draws one column's bar exactly as drawColBar does", _ {
		uitest::GridRig r;
		// A bar from -1.5 rows to +2.25 rows, in legacy's 8.24 units.
		const int32_t min24 = -(3 << 23);
		const int32_t max24 = (9 << 22);
		const PeakCol cols[] = {{.min24 = min24, .max24 = max24, .valid = true}};

		r.frame.region(kMainArea, [&](GridFrame& main) {
			waveform_full(main, cols, kWaveformBarBrightnessColoured, kBase);
		});

		std::array<RGB, 8> want{};
		legacy_draw_col_bar(want, min24, max24, kWaveformBarBrightnessColoured, kBase);
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(0, y)).to_equal(want[y]);
		}
	});

	it("uses monochrome when given no colour", _ {
		uitest::GridRig r;
		const int32_t min24 = -(1 << 24);
		const int32_t max24 = (1 << 24);
		const PeakCol cols[] = {{.min24 = min24, .max24 = max24, .valid = true}};

		r.frame.region(kMainArea, [&](GridFrame& main) {
			waveform_full(main, cols, kWaveformBarBrightnessMono, std::nullopt);
		});

		std::array<RGB, 8> want{};
		legacy_draw_col_bar(want, min24, max24, kWaveformBarBrightnessMono, std::nullopt);
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(0, y)).to_equal(want[y]);
		}
	});

	it("clears its columns and leaves invalid ones black", _ {
		uitest::GridRig r;
		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 16; x++) {
				r.frame.pad(x, y, RGB{9, 9, 9});
			}
			r.frame.pad(16, y, RGB{7, 7, 7}); // sidebar: not ours to clear
		}

		const PeakCol cols[] = {
		    {.min24 = 0, .max24 = 0, .valid = false},
		    {.min24 = -(1 << 24), .max24 = (1 << 24), .valid = true},
		};
		r.frame.region(kMainArea, [&](GridFrame& main) {
			waveform_full(main, cols, kWaveformBarBrightnessColoured, kBase);
		});

		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(0, y)).to_equal(palette::black); // cleared, then skipped
		}
		expect(r.grid.at(2, 0)).to_equal(RGB{9, 9, 9});   // outside the span: untouched
		expect(r.grid.at(16, 0)).to_equal(RGB{7, 7, 7});  // sidebar: untouched
	});

	it("records a full-screen waveform", _ {
		uitest::GridRig r;
		PeakCol cols[16];
		for (int32_t x = 0; x < 16; x++) {
			// A shape that exercises both partially-covered end rows on every column.
			const int32_t span = ((x % 8) + 1) * (1 << 22);
			cols[x] = PeakCol{.min24 = -span, .max24 = span, .valid = (x != 5)};
		}
		r.frame.region(kMainArea, [&](GridFrame& main) {
			waveform_full(main, cols, kWaveformBarBrightnessColoured, kBase);
		});
		expect_golden(self, "../ui/widgets/waveform_full.state", r.state());
	});
});
// clang-format on

// Not CPPSPEC_SPEC(waveform_grid): create_test_sourcelist derives the extern function name from
// this file's path relative to tests/spec (slashes become underscores), i.e.
// `ui_waveform_grid_spec`, which does not match the `waveform_grid` describe-block name the macro
// would assume. Spelled out by hand, as the other ui specs do.
int ui_waveform_grid_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(waveform_grid).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
