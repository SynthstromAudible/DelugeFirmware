#include "ui/views/grid/blink.h"
#include "unit/ui/views/grid/support/flashes.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"
#include "support/spec_main.h"
#include <vector>

using namespace deluge::ui::views::grid;
using deluge::ui::widgets::grid::exclusive_blink;
using deluge::ui::widgets::grid::FlashColour;
using deluge::ui::widgets::grid::PadFlash;
namespace uitest = deluge::ui::test;
using deluge::ui::views::grid::test::flashes_of;

// clang-format off
describe blink_v("ui::views::grid::blink", $ {
	it("flashes the plan's pad on its step", _ {
		uitest::GridRig r;
		blink(r.frame, {exclusive_blink(3, 4, 0), 1, true});
		expect(flashes_of(r)).to_equal(std::vector<PadFlash>{PadFlash{.x = 3, .y = 4, .colour = FlashColour::Default}});
	});
	it("flashes nothing for a step already played", _ {
		uitest::GridRig r;
		blink(r.frame, {exclusive_blink(3, 4, 0), 1, false});
		expect(flashes_of(r)).to_equal(std::vector<PadFlash>{});
	});
});
// clang-format on

DELUGE_SPEC(views_grid_blink_spec, blink_v)
