/// @file
/// @brief The menu's shortcut blinks under a scene that covers it, and the exclusive blink a pushed scene plays.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_core.h"
#include "scenes/menu/menu_state.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/pads.h"
#include "scenes/menu/shortcut_blink.h"

#include "gui/l10n/l10n.h"

#include "support/song_fixture.h"

#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::ui::grid::FlashColour;
using deluge::ui::grid::PadFlash;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// Every flash `MenuPads::render_grid` makes at @p now, its frame covered or not.
std::vector<PadFlash> frame_at(menu::MenuPads& pads, menu::MenuCore core, uint32_t now, bool covered) {
	deluge::ui::test::GridRig rig;
	rig.input.now_ms = now;
	rig.frame.covered = covered;
	pads.render_grid(core, rig.frame);
	return {rig.flashes.flashes().begin(), rig.flashes.flashes().end()};
}

} // namespace

// clang-format off
describe menu_pads("scenes::menu::MenuPads -- blinks under a covering scene", $ {
	it("plans no blink while a pushed scene covers the menu, and resumes when it leaves", _ {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		spec::SongFixture song = spec::make_synth_song();
		MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                                    /*kit_global_fx=*/false);
		menu::MenuState st{};
		st.path[0] = &nodes::lpf_freq;
		menu::MenuPads pads;
		const menu::MenuCore core{ctx, st};
		// The cutoff's own pad flashes on the param step of every cycle.
		const PadFlash cutoff{8, 7, FlashColour::Default};
		expect(std::ranges::count(frame_at(pads, core, 1000, false), cutoff)).to_equal(0); // step 0: the sources
		expect(std::ranges::count(frame_at(pads, core, 1000 + menu::kSourceBlinkMs, false), cutoff)).to_equal(1);
		// Covered: nothing at all, on either step.
		for (uint32_t t = 1200; t < 2000; t += 10) {
			expect(frame_at(pads, core, t, true).empty()).to_be_true();
		}
		// Uncovered again: the cycle starts over, sources first, then the param step 20 ms on.
		expect(std::ranges::count(frame_at(pads, core, 2005, false), cutoff)).to_equal(0);
		expect(std::ranges::count(frame_at(pads, core, 2005 + menu::kSourceBlinkMs, false), cutoff)).to_equal(1);
	});

	it("an exclusive blink flashes only its pad, on every other cycle", _ {
		const menu::BlinkPlan plan = menu::exclusive_blink(5, 5);
		expect(plan.any_source()).to_be_false();
		std::vector<uint32_t> steps;
		for (uint32_t step = 0; step < 12; step++) {
			deluge::ui::test::GridRig rig;
			menu::fire_blink_step(rig.frame, plan, step);
			for (const PadFlash& f : rig.flashes.flashes()) {
				expect(f == PadFlash{5, 5, FlashColour::Default}).to_be_true();
				steps.push_back(step);
			}
		}
		expect(steps == std::vector<uint32_t>{1, 5, 9}).to_be_true(); // the param step of cycles 0, 2 and 4
	});
});
// clang-format on

DELUGE_SPEC(menu_pads_spec, menu_pads)
