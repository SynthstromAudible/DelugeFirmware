// clang-format off
#include "controls/resolve.h"
#include "deluge-project/song.h"
#include "instruments/deluge/params.h"
#include "scenes/bind/section_pages.h"
#include <cppspec.hpp>

using namespace deluge;
namespace ip = deluge::instrument::param;

namespace {
/// A synth on output 3 in mode @p mode, its lower knob there the low-pass cutoff and its upper the resonance.
project::Song filter_on_lower_knob(uint8_t mode) {
	project::Song song{};
	project::DocOutput& output = song.output_structure.getOrCreate(3);
	output.mod_knob_mode = mode;
	output.controls = {{.control = {.button = mode, .knob = 0}, .target = ip::kLpfFrequency},
	                   {.control = {.button = mode, .knob = 1}, .target = ip::kLpfResonance}};
	return song;
}
const controls::KnobFocus kSynth{.owner = controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 3}}};
} // namespace

describe section_pages("the pages a press moves between, from the song's records", ${
	it("cycles a sound's filter knob on the lower knob of any button, its resonance partner with it", _{
		const project::Song song = filter_on_lower_knob(3);
		const controls::Target target =
		    controls::resolve_press(kSynth, scenes::bind::section_pages(song, kSynth, {}), 0, true);
		const auto* page = std::get_if<controls::SelectPage>(&target);
		expect(page != nullptr).to_be_true();
		expect(page->slots[0].target->id == ip::kHpfFrequency).to_be_true();
		expect(page->slots[1].target->id == ip::kHpfResonance).to_be_true();
	});
	it("does not cycle an FM sound's filter knob", _{
		const project::Song song = filter_on_lower_knob(1);
		const controls::Target target =
		    controls::resolve_press(kSynth, scenes::bind::section_pages(song, kSynth, {.fm = true}), 0, true);
		expect(std::holds_alternative<std::monostate>(target)).to_be_true();
	});
	it("picks a MIDI knob's CC on a press, with no clip too", _{
		const project::Song song{};
		const controls::KnobFocus midi{.owner = controls::MidiFocus{.track = {5}}};
		const controls::Target target =
		    controls::resolve_press(midi, scenes::bind::section_pages(song, midi, {}), 1, true);
		expect(std::get<controls::HostCommand>(target) == controls::HostCommand::MIDI_CC_SELECT_ON).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(section_pages)
