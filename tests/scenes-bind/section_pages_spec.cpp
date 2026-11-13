// clang-format off
#include "controls/resolve.h"
#include "deluge-project/song.h"
#include "effects/global_effectable/params.h"
#include "instruments/deluge/params.h"
#include "scenes/bind/section_pages.h"
#include <algorithm>
#include <cppspec.hpp>

using namespace deluge;
namespace ip = deluge::instrument::param;
namespace ge = deluge::global_effectable::param;

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
const controls::KnobFocus kSong{.owner = controls::SongFocus{}};
/// A kit's chain on output 8.
const controls::KnobFocus kKitChain{.owner = controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 8}}};

/// The song in mode @p mode.
project::Song song_in_mode(uint8_t mode) {
	project::Song song{};
	song.mod_knob_mode = mode;
	return song;
}

/// The ENGINE-space param @p page's slot @p slot turns.
plugin::ParamId turned(const controls::Page& page, size_t slot) {
	return page.slots[slot].target ? page.slots[slot].target->id : plugin::ParamId{0xFFFF'FFFF};
}
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
	it("puts a chain's filter knobs on the page of its filter view, a view past EQ on EQ", _{
		project::Song song = song_in_mode(1);
		song.master_effects.filter_type = 1;
		expect(int(scenes::bind::section_pages(song, kSong, {}).current.page_id)).to_equal(1);
		song.master_effects.filter_type = 7;
		const controls::SectionPages pages = scenes::bind::section_pages(song, kSong, {});
		expect(int(pages.current.page_id)).to_equal(2);
		expect(turned(pages.current, 1) == ge::kTreble).to_be_true();
		expect(pages.current == scenes::bind::current_page(song, kSong, {})).to_be_true();
	});
	it("puts a chain's mod FX knobs on the view its record holds, even one its type has no param for", _{
		project::Song song = song_in_mode(5);
		song.master_effects.mod_fx_param = 0;
		scenes::bind::PageInputs flanger{};
		flanger.effects.mod_fx.type = effects::ModFXType::FLANGER;
		const controls::SectionPages pages = scenes::bind::section_pages(song, kSong, flanger);
		expect(int(pages.current.page_id)).to_equal(0);
		expect(turned(pages.current, 0) == ge::kModFXDepth).to_be_true();
		expect(std::ranges::none_of(pages.cycle, [](const controls::Page& p) { return p.page_id == 0; })).to_be_true();
		expect(pages.current == scenes::bind::current_page(song, kSong, flanger)).to_be_true();
	});
	it("enters the song's compressor group at the setting its compressor view holds, and edits it while editing", _{
		const project::Song song = song_in_mode(4);
		controls::KnobFocus focus = kSong;
		focus.comp = {.editing = false, .param = 2};
		const controls::SectionPages reverb = scenes::bind::section_pages(song, focus, {});
		expect(int(reverb.group_entry[1])).to_equal(3);
		expect(int(reverb.current.page_id)).to_equal(0);
		focus.comp.editing = true;
		const controls::SectionPages comp = scenes::bind::section_pages(song, focus, {});
		expect(int(comp.current.page_id)).to_equal(3);
		expect(int(comp.current.group)).to_equal(1);
		expect(comp.current == scenes::bind::current_page(song, focus, {})).to_be_true();
	});
	it("keeps a clip's chain on its reverb page whatever the compressor view holds", _{
		project::Song song{};
		song.output_structure.getOrCreate(8).mod_knob_mode = 4;
		controls::KnobFocus focus = kKitChain;
		focus.comp = {.editing = true, .param = 2};
		const controls::SectionPages pages = scenes::bind::section_pages(song, focus, {});
		expect(int(pages.current.page_id)).to_equal(0);
		expect(turned(pages.current, 0) == ge::kReverbAmount).to_be_true();
		expect(pages.cycle.size()).to_equal(size_t{1});
	});
	it("has an empty page and no cycle on a chain's envelope button", _{
		const project::Song song = song_in_mode(2);
		const controls::SectionPages pages = scenes::bind::section_pages(song, kSong, {});
		expect(int(pages.current.section)).to_equal(2);
		expect(std::ranges::none_of(pages.current.slots, [](const controls::Slot& s) { return s.target.has_value(); }))
		    .to_be_true();
		expect(pages.cycle.empty()).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(section_pages)
