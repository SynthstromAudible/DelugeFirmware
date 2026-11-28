#include "support/frames.h"
#include "ui/widgets/grid/shortcut_blink.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

using namespace deluge::ui::widgets::grid;
namespace uitest = deluge::ui::test;

// clang-format off
describe shortcut_blink("ui::widgets::grid shortcut blink", $ {
	it("steps the counter at 20 and 200 ms", _ {
		expect(blink_step(0, 0)).to_equal(0u);   expect(blink_step(0, 19)).to_equal(0u);
		expect(blink_step(0, 20)).to_equal(1u);  expect(blink_step(0, 199)).to_equal(1u);
		expect(blink_step(0, 200)).to_equal(2u);
		expect(blink_step(0xFFFFFFF0u, 0x10u)).to_equal(1u); // 32 ms across the wrap
	});
	it("names the time to the next step", _ {
		expect(ms_to_next_step(0, 0)).to_equal(20u); expect(ms_to_next_step(0, 20)).to_equal(180u);
		expect(ms_to_next_step(0, 199)).to_equal(1u);
	});
	it("flashes an exclusive pad on the param step only", _ {
		uitest::GridRig r;
		fire_blink_step(r.frame, exclusive_blink(3, 4, 0), 0);
		expect(r.flashes.flashes().size()).to_equal(size_t(0));
		fire_blink_step(r.frame, exclusive_blink(3, 4, 0), 1);
		expect(r.flashes.flashes().size()).to_equal(size_t(1)); // at (3, 4)
	});
});
// clang-format on
DELUGE_SPEC(shortcut_blink_spec, shortcut_blink)
