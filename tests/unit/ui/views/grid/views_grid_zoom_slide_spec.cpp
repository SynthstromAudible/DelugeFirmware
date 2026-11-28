#include "ui/gfx/grid/tween.h"
#include "ui/views/grid/zoom_slide.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <cstdint>

using namespace deluge::ui::views::grid;
using deluge::ui::gfx::grid::GridDimensions;
using deluge::ui::gfx::grid::PadGrid;
using deluge::ui::gfx::grid::ZoomParams;
using deluge::ui::widgets::grid::begin_zoom;
using deluge::ui::widgets::grid::ZoomTransition;
namespace uitest = deluge::ui::test;

namespace {

constexpr GridDimensions kMain{16, 0, 8};

/// A frame with something distinct on every pad, so a step that read the wrong grid shows.
PadGrid pattern(uint8_t salt) {
	PadGrid g(kMain);
	for (uint8_t y = 0; y < kMain.rows; y++) {
		for (uint8_t x = 0; x < kMain.main_cols; x++) {
			g.set(x, y, RGB{static_cast<uint8_t>(16 * x + salt), static_cast<uint8_t>(32 * y), salt});
		}
	}
	return g;
}

/// Zooming in one step about square 5.
constexpr ZoomParams kIn{
    .old_zoom = 3000, .new_zoom = 2048, .pin_square_from = 5 << 16, .pin_square_to = 5 << 16, .t = 0, .magnitude = 1};

} // namespace

// clang-format off
describe zoom_slide_v("ui::views::grid::zoom_slide", $ {
	it("paints nothing and answers false with no slide running", _ {
		uitest::GridRig r;
		ZoomTransition z;
		expect(zoom_slide(r.frame, z)).to_be_false();
		expect(r.grid.at(5, 3)).to_equal(RGB{});
	});

	it("paints the slide's step halfway through, and matches its golden", _ {
		uitest::GridRig r;
		ZoomTransition z;
		r.begin_tick(1000);
		begin_zoom(z, pattern(1), kIn, 1000) = pattern(77);
		r.begin_tick(1055);
		expect(zoom_slide(r.frame, z)).to_be_true();
		PadGrid half(kMain);
		ZoomParams at_half = kIn;
		at_half.t = 32768;
		deluge::ui::gfx::grid::zoom(pattern(1), pattern(77), at_half, half);
		expect(r.grid.at(5, 3)).to_equal(half.at(5, 3));
		expect(r.grid.at(16, 0)).to_equal(RGB{});
		expect_golden(self, "ui/views/grid/zoom_slide_half.state", r.state());
	});

	it("ends once the slide's time has run out, painting nothing", _ {
		uitest::GridRig r;
		ZoomTransition z;
		r.begin_tick(1000);
		begin_zoom(z, pattern(1), kIn, 1000) = pattern(77);
		r.begin_tick(1110);
		expect(zoom_slide(r.frame, z)).to_be_false();
		expect(z.active).to_be_false();
		expect(r.grid.at(5, 3)).to_equal(RGB{});
	});
});
// clang-format on

DELUGE_SPEC(views_grid_zoom_slide_spec, zoom_slide_v)
