#include "ui/views/grid/blink.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

using namespace deluge::ui::views::grid;
using deluge::ui::widgets::grid::exclusive_blink;
using deluge::ui::widgets::grid::FlashColour;
using deluge::ui::widgets::grid::PadFlash;
namespace uitest = deluge::ui::test;

// clang-format off
describe blink_v("ui::views::grid::blink", $ {
	it("flashes the plan's pad on its step", _ {
		uitest::GridRig r;
		blink(r.frame, {exclusive_blink(3, 4, 0), 1, true});
		expect(r.flashes.flashes().size()).to_equal(size_t(1));
		expect(r.flashes.flashes()[0]).to_equal(PadFlash{.x = 3, .y = 4, .colour = FlashColour::Default});
	});
	it("flashes nothing for a step already played", _ {
		uitest::GridRig r;
		blink(r.frame, {exclusive_blink(3, 4, 0), 1, false});
		expect(r.flashes.flashes().size()).to_equal(size_t(0));
	});
});
// clang-format on

DELUGE_SPEC(blink_spec, blink_v)
