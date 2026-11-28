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
		expect(r.flashes.flashes().size()).to_equal(size_t(1));
		expect(r.flashes.flashes()[0]).to_equal(PadFlash{.x = 3, .y = 4, .colour = FlashColour::Default});
	});
	it("flashes a source pad on the source step", _ {
		uitest::GridRig r;
		BlinkPlan plan;
		plan.sources[1][2] = SourceBlink{.freq_mask = 0, .colour = FlashColour::Yellow, .alternates = false};
		fire_blink_step(r.frame, plan, 0);
		expect(r.flashes.flashes().size()).to_equal(size_t(1));
		expect(r.flashes.flashes()[0]).to_equal(
		    PadFlash{.x = kBlinkSourceCol + 1, .y = 2, .colour = FlashColour::Yellow});
		r.flashes.clear();
		fire_blink_step(r.frame, plan, 1);
		expect(r.flashes.flashes().size()).to_equal(size_t(0));
	});
	it("skips a source pad on a cycle its mask excludes", _ {
		uitest::GridRig r;
		BlinkPlan plan;
		plan.sources[0][0] = SourceBlink{.freq_mask = 3, .colour = FlashColour::Default, .alternates = false};
		fire_blink_step(r.frame, plan, 2); // cycle 1
		expect(r.flashes.flashes().size()).to_equal(size_t(0));
		fire_blink_step(r.frame, plan, 8); // cycle 4
		expect(r.flashes.flashes().size()).to_equal(size_t(1));
	});
	it("alternates a shared source pad to yellow", _ {
		uitest::GridRig r;
		BlinkPlan plan;
		plan.sources[0][5] = SourceBlink{.freq_mask = 0, .colour = FlashColour::Default, .alternates = true};
		fire_blink_step(r.frame, plan, 0); // cycle 0
		fire_blink_step(r.frame, plan, 2); // cycle 1
		expect(r.flashes.flashes().size()).to_equal(size_t(2));
		expect(r.flashes.flashes()[0].colour == FlashColour::Default).to_equal(true);
		expect(r.flashes.flashes()[1].colour == FlashColour::Yellow).to_equal(true);
	});
	it("flashes no param pad when none is valid", _ {
		uitest::GridRig r;
		fire_blink_step(r.frame, BlinkPlan{}, 1);
		expect(r.flashes.flashes().size()).to_equal(size_t(0));
	});
});
// clang-format on
DELUGE_SPEC(shortcut_blink_spec, shortcut_blink)
