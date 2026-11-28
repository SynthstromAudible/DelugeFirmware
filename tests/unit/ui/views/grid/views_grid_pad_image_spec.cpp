#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/pad_image.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <cstdint>

using namespace deluge::ui::views::grid;
namespace uitest = deluge::ui::test;

namespace {

/// Something distinct on every main pad, so a pad painted from the wrong place shows.
PadImage pattern() {
	PadImage d{};
	for (uint8_t y = 0; y < 8; y++) {
		for (uint8_t x = 0; x < 16; x++) {
			d.pads[y][x] = RGB{static_cast<uint8_t>(16 * x), static_cast<uint8_t>(32 * y), 7};
		}
	}
	return d;
}

} // namespace

// clang-format off
describe pad_image_v("ui::views::grid::pads", $ {
	it("paints every main pad as the image holds it and leaves the sidebar alone", _ {
		uitest::GridRig r;
		r.grid.set(16, 0, deluge::gui::colours::red);
		r.grid.set(17, 7, deluge::gui::colours::red);
		const PadImage d = pattern();
		pads(r.frame, d);
		for (uint8_t y = 0; y < 8; y++) {
			for (uint8_t x = 0; x < 16; x++) {
				expect(r.grid.at(x, y)).to_equal(d.pads[y][x]);
			}
		}
		expect(r.grid.at(16, 0)).to_equal(deluge::gui::colours::red);
		expect(r.grid.at(17, 7)).to_equal(deluge::gui::colours::red);
		expect_golden(self, "ui/views/grid/pads_pattern.state", r.state());
	});

	it("paints only the pads a smaller region covers", _ {
		uitest::GridRig r;
		const PadImage d = pattern();
		r.frame.region(deluge::ui::gfx::grid::Rect{0, 0, 4, 2},
		               [&](deluge::ui::widgets::grid::GridFrame& corner) { pads(corner, d); });
		expect(r.grid.at(3, 1)).to_equal(d.pads[1][3]);
		expect(r.grid.at(4, 1)).to_equal(RGB{});
		expect(r.grid.at(3, 2)).to_equal(RGB{});
	});
});
// clang-format on

DELUGE_SPEC(views_grid_pad_image_spec, pad_image_v)
