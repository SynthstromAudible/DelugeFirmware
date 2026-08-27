#include "cppspec.hpp"
#include "gfx/grid/rect.h"
#include "ui/core/metrics.h"

using deluge::gfx::grid::Rect;

// clang-format off
describe grid_rect("gfx::grid::Rect", $ {
	it("contains and intersects in pad units", _ {
		Rect r{2, 1, 4, 3};
		expect(r.contains(2, 1)).to_be_true();
		expect(r.contains(6, 1)).to_be_false();
		expect(r.intersect(Rect{4, 2, 10, 10})).to_equal(Rect{4, 2, 2, 2});
		expect(deluge::ui::kTextSpacingY).to_equal(9);
	});
});

// Not CPPSPEC_SPEC(grid_rect): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_rect_spec` for
// ui/rect_spec.cpp, which does not match the `grid_rect` describe-block name the macro would
// otherwise assume. Spelled out by hand so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern` (see gfx/canvas_golden_spec.cpp for the same pattern).
int ui_rect_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(grid_rect).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
