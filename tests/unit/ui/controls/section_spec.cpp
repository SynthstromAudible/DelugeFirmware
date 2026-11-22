// clang-format off
#include "ui/controls/section.h"
#include "support/printers/controls_printer.h"
#include <cppspec.hpp>

using namespace deluge;
using namespace deluge::ui::controls;

namespace {
const KnobFocus kSound{.owner = SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 3}}};
const KnobFocus kChain{.owner = ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 4}}};
const KnobFocus kSong{.owner = SongFocus{}};
const SectionInputs kPerformance{.root_is_performance = true};
/// A press of a section whose pages keep a playing stutter.
const SectionInputs kKeeps{.keeps_stutter = true};
} // namespace

describe section("a mod button's press and release", ${
	it("changes the mode on a press, and not on a release", _{
		expect(resolve_section(kSound, 0, 3, true, {}).change_mode).to_be_true();
		expect(resolve_section(kSound, 0, 3, false, {}).change_mode).to_be_false();
	});
	it("shows the button's popup on a press, and not on a release", _{
		expect(resolve_section(kChain, 0, 3, true, {}).taken).to_be_true();
		expect(resolve_section(kChain, 0, 3, false, {}).taken).to_be_false();
	});
	it("names no mode owner, and so changes no mode, with no focus", _{
		expect(resolve_section(KnobFocus{}, 0, 3, true, {}).change_mode).to_be_false();
	});
	it("ends a sound's stutter on every section that does not keep it, on performance view too", _{
		expect(resolve_section(kSound, 0, 4, true, {}).end_stutter).to_be_true();
		expect(resolve_section(kSound, 0, 4, false, kPerformance).end_stutter).to_be_true();
		expect(resolve_section(kSound, 0, 4, true, kPerformance).end_stutter).to_be_true();
		expect(resolve_section(kSound, 0, 4, true, kKeeps).end_stutter).to_be_false();
	});
	it("ends a clip chain's stutter, except on a section that keeps it or on performance view", _{
		expect(resolve_section(kChain, 0, 3, true, {}).end_stutter).to_be_true();
		expect(resolve_section(kChain, 0, 4, true, kKeeps).end_stutter).to_be_false();
		expect(resolve_section(kChain, 0, 4, true, {}).end_stutter).to_be_true();
		expect(resolve_section(kChain, 0, 3, false, kPerformance).end_stutter).to_be_false();
	});
	it("ends the song's stutter, except on performance view", _{
		expect(resolve_section(kSong, 0, 4, true, {}).end_stutter).to_be_true();
		expect(resolve_section(kSong, 0, 3, false, kPerformance).end_stutter).to_be_false();
	});
	it("ends no MIDI track's stutter: it has none", _{
		const KnobFocus midi{.owner = MidiFocus{.track = {5}}};
		expect(resolve_section(midi, 0, 3, true, {}).end_stutter).to_be_false();
		expect(resolve_section(KnobFocus{}, 0, 3, false, {}).end_stutter).to_be_false();
	});
	it("toggles the VU meter on a press of button 0 when it is the mode, outside a clip minder", _{
		expect(resolve_section(kSong, 0, 0, true, {}).toggle_vu_meter).to_be_true();
		expect(resolve_section(kSong, 1, 0, true, {}).toggle_vu_meter).to_be_false();
		expect(resolve_section(kSong, 0, 0, true, {.root_is_clip_minder = true}).toggle_vu_meter).to_be_false();
		expect(resolve_section(kSong, 0, 0, false, {}).toggle_vu_meter).to_be_false();
		expect(resolve_section(kSong, 1, 1, true, {}).toggle_vu_meter).to_be_false();
	});
	it("ignores a press out of the button's UI modes: no mode, no stutter, no popup", _{
		const SectionResult out = resolve_section(kSound, 0, 0, true, {.mod_button_ui_mode_in_range = false});
		expect(out).to_equal(SectionResult{});
	});
	it("takes a press out of the button's UI modes on performance view", _{
		const SectionResult out = resolve_section(kSong, 0, 0, true,
		                                          {.root_is_performance = true, .mod_button_ui_mode_in_range = false});
		expect(out.change_mode).to_be_true();
		expect(out.toggle_vu_meter).to_be_true();
		expect(out.end_stutter).to_be_false();
	});
	it("takes a release out of the button's UI modes: it ends the stutter", _{
		expect(resolve_section(kSound, 0, 0, false, {.mod_button_ui_mode_in_range = false}).end_stutter).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(section)
