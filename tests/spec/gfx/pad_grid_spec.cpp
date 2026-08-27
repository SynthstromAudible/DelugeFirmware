#include "cppspec.hpp"
#include "gfx/grid/pad_grid.h"
#include "support/golden.h"
#include "support/grid_state.h"

using namespace deluge::gfx::grid;

// clang-format off
describe pad_grid("gfx::grid::PadGrid", $ {
	it("sizes itself from the dimensions and starts black and clean", _ {
		PadGrid g(kDelugeGrid);
		expect(g.dims().cols()).to_equal(uint8_t(18));
		expect(g.dirty_rows()).to_equal(0u);
		expect(g.at(17, 7)).to_equal(RGB{0, 0, 0});
	});

	it("tracks dirty rows on set and ignores out-of-range writes", _ {
		PadGrid g(kDelugeGrid);
		g.set(3, 5, RGB{255, 0, 0});
		expect(g.dirty_rows()).to_equal(1u << 5);
		g.set(18, 0, RGB{1, 1, 1}); // one past the sidebar
		g.set(0, 8, RGB{1, 1, 1});  // one past the last row
		expect(g.dirty_rows()).to_equal(1u << 5);
		g.clear_dirty();
		expect(g.dirty_rows()).to_equal(0u);
	});

	it("fills rectangles clipped to the grid", _ {
		PadGrid g(GridDimensions{4, 1, 3});
		g.fill_rect(3, 1, 10, 10, RGB{0, 0, 9});
		expect(g.at(3, 1)).to_equal(RGB{0, 0, 9});
		expect(g.at(4, 2)).to_equal(RGB{0, 0, 9});
		expect(g.at(2, 1)).to_equal(RGB{0, 0, 0});
		expect(g.dirty_rows()).to_equal((1u << 1) | (1u << 2));
	});

	it("dumps a golden state", _ {
		PadGrid g(kDelugeGrid);
		g.set(0, 0, RGB{255, 0, 0});
		g.set(17, 7, RGB{0, 255, 0});
		g.fill_rect(4, 3, 2, 2, RGB{10, 20, 30});
		expect_golden(self, "grid/pad_grid_basic.state", grid_to_state(g));
	});
});

// Not CPPSPEC_SPEC(pad_grid): create_test_sourcelist derives the extern function name from this file's
// path relative to tests/spec (slashes become underscores) — see canvas_golden_spec.cpp.
int gfx_pad_grid_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(pad_grid).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE; // needs <cstdlib>
}
