#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/waveform.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"

using namespace deluge::ui::views::grid;
using deluge::ui::views::oled::test::break_peaks;
using deluge::ui::widgets::grid::kWaveformBarBrightnessColoured;
using deluge::ui::widgets::grid::kWaveformBarBrightnessMono;
namespace uitest = deluge::ui::test;

// clang-format off
describe waveform_v("ui::views::grid::waveform", $ {
	it("paints the main pads and leaves the sidebar alone", _ {
		uitest::GridRig r;
		r.grid.set(16, 0, deluge::gui::colours::red);
		r.grid.set(17, 7, deluge::gui::colours::red);
		waveform(r.frame, break_peaks(16), kWaveformBarBrightnessMono, std::nullopt);
		expect(r.grid.at(16, 0)).to_equal(deluge::gui::colours::red);
		expect(r.grid.at(17, 7)).to_equal(deluge::gui::colours::red);
		// Column 1 is just after the break's first hit.
		int32_t lit = 0;
		for (uint8_t y = 0; y < 8; y++) {
			lit += r.grid.at(1, y) != deluge::gui::colours::black ? 1 : 0;
		}
		expect(lit).to_be_greater_than(0);
	});

	it("matches its goldens, monochrome and coloured", _ {
		uitest::GridRig mono;
		waveform(mono.frame, break_peaks(16), kWaveformBarBrightnessMono, std::nullopt);
		expect_golden(self, "ui/views/grid/waveform_mono.state", mono.state());
		uitest::GridRig coloured;
		waveform(coloured.frame, break_peaks(16), kWaveformBarBrightnessColoured, deluge::gui::colours::cyan);
		expect_golden(self, "ui/views/grid/waveform_coloured.state", coloured.state());
		expect(coloured.state() == mono.state()).to_equal(false);
	});
});
// clang-format on

DELUGE_SPEC(views_grid_waveform_spec, waveform_v)
