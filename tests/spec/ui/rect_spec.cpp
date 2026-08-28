#include "../support/spec_main.h"
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

DELUGE_SPEC(ui_rect_spec, grid_rect)
