#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/slicer.h"
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

/// Four slices, the third current, in region mode.
SliceMarks region() {
	SliceMarks d{.manual = false, .count = 4, .current = 2, .cols = {}};
	d.cols = {0, 4, 8, 12};
	return d;
}

/// Three slices, the second current; the last at the very end of the sample, one past the main columns.
SliceMarks manual() {
	SliceMarks d{.manual = true, .count = 3, .current = 1, .cols = {}};
	d.cols = {1, 7, 16};
	return d;
}

void draw(uitest::GridRig& r, const SliceMarks& d) {
	r.frame.region(kMain, [&](GridFrame& main) {
		waveform(main, break_peaks(16), kWaveformBarBrightnessMono, std::nullopt);
		slice_marks(main, d);
	});
}

} // namespace

// clang-format off
describe slice_marks_v("ui::views::grid::slice_marks", $ {
	it("leaves the waveform as painted in region mode", _ {
		uitest::GridRig bare;
		bare.frame.region(kMain, [&](GridFrame& main) {
			waveform(main, break_peaks(16), kWaveformBarBrightnessMono, std::nullopt);
		});
		uitest::GridRig r;
		draw(r, region());
		expect(r.state()).to_equal(bare.state());
		expect_golden(self, "ui/views/grid/slice_marks_region.state", r.state());
	});

	it("marks each slice on the lower half's first row, the current one brighter, and clips one past the edge", _ {
		uitest::GridRig r;
		draw(r, manual());
		expect(r.grid.at(1, 4)).to_equal(RGB(1, 16, 1));
		expect(r.grid.at(7, 4)).to_equal(RGB(1, 200, 1));
		expect(r.grid.at(16, 4)).not_().to_equal(RGB(1, 16, 1));
	});

	it("draws the slot grid: placed slots brighter than free ones, the current one full", _ {
		uitest::GridRig r;
		draw(r, manual());
		expect(r.grid.at(1, 0)).to_equal(deluge::gui::colours::green.dim());
		expect(r.grid.at(0, 0)).to_equal(deluge::gui::colours::green.dim(2));
		expect(r.grid.at(3, 3)).to_equal(deluge::gui::colours::green.dim(6));
		expect(r.grid.at(4, 0)).to_equal(deluge::gui::colours::darkblue.dim(6));
		expect_golden(self, "ui/views/grid/slice_marks_manual.state", r.state());
	});
});
// clang-format on

DELUGE_SPEC(views_grid_slicer_spec, slice_marks_v)
