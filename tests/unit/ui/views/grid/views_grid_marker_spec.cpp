#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/marker.h"
#include "ui/views/grid/waveform.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <cstdint>

using namespace deluge::ui::views::grid;
using deluge::ui::views::oled::test::break_peaks;
using deluge::ui::widgets::grid::GridFrame;
using deluge::ui::widgets::grid::kWaveformBarBrightnessMono;
namespace uitest = deluge::ui::test;

namespace {

constexpr deluge::ui::gfx::grid::Rect kMain{0, 0, 16, 8};

/// START at 2, LOOP_START and LOOP_END sharing 8, END at 13, START selected; phase 1 floods START's column.
MarkerOverlay start_selected(int32_t flood) {
	MarkerOverlay d{.active = {},
	                .suppress = 0,
	                .selected = 0,
	                .colours = {deluge::gui::colours::green, deluge::gui::colours::cyan, deluge::gui::colours::magenta,
	                            deluge::gui::colours::red},
	                .flood = flood};
	d.active[2] = 0b0001;
	d.active[8] = 0b0110;
	d.active[13] = 0b1000;
	return d;
}

void draw(uitest::GridRig& r, const MarkerOverlay& d) {
	r.frame.region(kMain, [&](GridFrame& main) {
		waveform(main, break_peaks(16), kWaveformBarBrightnessMono, std::nullopt);
		marker_overlay(main, d);
	});
}

} // namespace

// clang-format off
describe marker_overlay_v("ui::views::grid::marker_overlay", $ {
	it("leaves a column no marker lands on as the waveform painted it", _ {
		uitest::GridRig bare;
		bare.frame.region(kMain, [&](GridFrame& main) {
			waveform(main, break_peaks(16), kWaveformBarBrightnessMono, std::nullopt);
		});
		uitest::GridRig r;
		draw(r, start_selected(-1));
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(5, y)).to_equal(bare.grid.at(5, y));
			expect(r.grid.at(2, y)).not_().to_equal(bare.grid.at(2, y));
		}
	});

	it("suppresses the markers its mask names", _ {
		uitest::GridRig bare;
		bare.frame.region(kMain, [&](GridFrame& main) {
			waveform(main, break_peaks(16), kWaveformBarBrightnessMono, std::nullopt);
		});
		uitest::GridRig shown;
		draw(shown, start_selected(-1));
		MarkerOverlay hidden = start_selected(-1);
		hidden.suppress = 0b0001;
		uitest::GridRig r;
		draw(r, hidden);
		expect(r.state() == shown.state()).to_equal(false);
		expect(r.grid.at(13, 0)).to_equal(shown.grid.at(13, 0));
		// START, the one marker on column 2, is gone: the column is the waveform's alone.
		for (uint8_t y = 0; y < 8; y++) {
			expect(r.grid.at(2, y)).to_equal(bare.grid.at(2, y));
		}
	});

	it("floods the selected marker's column on phase 1, and matches its golden", _ {
		uitest::GridRig r;
		draw(r, start_selected(2));
		expect_golden(self, "ui/views/grid/marker_start.state", r.state());
	});
});
// clang-format on

DELUGE_SPEC(views_grid_marker_spec, marker_overlay_v)
