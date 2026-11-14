// clang-format off
#include "board_controls.hpp"
#include "controls/pages.h"
#include "controls/resolve.h"
#include "deluge-project/song.h"
#include "effects/global_effectable/pages.h"
#include "effects/global_effectable/params.h"
#include "effects/plugin.h"
#include "instruments/deluge/pages.h"
#include "instruments/deluge/params.h"
#include "instruments/deluge/plugin.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include "mock_panic.h"

using namespace deluge;
namespace ip = deluge::instrument::param;
namespace ge = deluge::global_effectable::param;
using deluge::instrument::SynthMode;

namespace {
/// The allocations made through the global operator new since the driver started.
size_t g_allocations = 0;
} // namespace

// Counts every allocation, so a spec can pin a function as allocation-free.
void* operator new(size_t bytes) {
	g_allocations++;
	if (void* p = std::malloc(bytes == 0 ? 1 : bytes)) {
		return p;
	}
	throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t /*bytes*/) noexcept { std::free(p); }

namespace {
/// The publishers the app collects: the Deluge instrument's and the global effectable's.
const std::array<plugin::RemoteControls, 2> kPublishers{instrument::kRemoteControls, global_effectable::kRemoteControls};
/// What the stand-in view reading last saw.
struct ViewCall {
	const project::FxChain* chain{nullptr};
	controls::CompView comp;
	bool song{false};
	uint32_t section{0xFF};
};
ViewCall g_seen;
/// What the stand-in view reading answers: the app's mapping is pinned in the scenes' chain_view_spec.
controls::ChainView g_view;
controls::ChainView view_stand_in(const project::FxChain* chain, const controls::CompView& comp, bool song,
                                  uint32_t section) {
	g_seen = {.chain = chain, .comp = comp, .song = song, .section = section};
	return g_view;
}
const controls::PageSources kSources{.publishers = kPublishers, .board = kDelugeControls, .chain_view = &view_stand_in};

/// A synth on output 3 in mode @p mode, its lower knob there the low-pass cutoff and its upper the resonance, playing a
/// sound in @p synth_mode.
project::Song filter_on_lower_knob(uint8_t mode, SynthMode synth_mode = SynthMode::SUBTRACTIVE) {
	project::Song song{};
	project::DocOutput& output = song.output_structure.getOrCreate(3);
	output.mod_knob_mode = mode;
	output.controls = {{.control = {.button = mode, .knob = 0}, .target = ip::kLpfFrequency},
	                   {.control = {.button = mode, .knob = 1}, .target = ip::kLpfResonance}};
	instrument::SoundState sound{};
	sound.synth_mode = synth_mode;
	output.instrument = project::InstrumentSlot{
	    .plugin = "deluge.instrument", .state = plugin::StateHandle::of<instrument::DelugeInstrument>(sound)};
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

/// The allocations current_page makes for @p focus on @p song.
size_t current_page_allocations(const project::Song& song, const controls::KnobFocus& focus) {
	const size_t before = g_allocations;
	const controls::Page page = controls::current_page(song, focus, kSources);
	const size_t made = g_allocations - before;
	(void)page;
	return made;
}
} // namespace

describe pages("the pages a press moves between, from the published records and the song's", ${
	it("cycles a sound's filter knob on the lower knob of any button, its resonance partner with it", _{
		const project::Song song = filter_on_lower_knob(3);
		const controls::Target target =
		    controls::resolve_press(kSynth, controls::section_pages(song, kSynth, kSources), 0, true);
		const auto* page = std::get_if<controls::SelectPage>(&target);
		expect(page != nullptr).to_be_true();
		expect(page->slots[0].target->id == ip::kHpfFrequency).to_be_true();
		expect(page->slots[1].target->id == ip::kHpfResonance).to_be_true();
	});
	it("does not cycle an FM sound's filter knob", _{
		const project::Song song = filter_on_lower_knob(1, SynthMode::FM);
		const controls::Target target =
		    controls::resolve_press(kSynth, controls::section_pages(song, kSynth, kSources), 0, true);
		expect(std::holds_alternative<std::monostate>(target)).to_be_true();
	});
	it("picks a MIDI knob's CC on a press, with no clip too", _{
		const project::Song song{};
		const controls::KnobFocus midi{.owner = controls::MidiFocus{.track = {5}}};
		const controls::Target target =
		    controls::resolve_press(midi, controls::section_pages(song, midi, kSources), 1, true);
		expect(std::get<controls::HostCommand>(target) == controls::HostCommand::MIDI_CC_SELECT_ON).to_be_true();
	});
	it("puts a chain's knobs on the page its view reading names, found through its publisher", _{
		project::Song song = song_in_mode(1);
		g_view = {.current = 2, .group_entry = {0, 4}};
		const controls::SectionPages pages = controls::section_pages(song, kSong, kSources);
		expect(int(pages.current.page_id)).to_equal(2);
		expect(turned(pages.current, 1) == ge::kTreble).to_be_true();
		expect(pages.current == controls::current_page(song, kSong, kSources)).to_be_true();
		expect(pages.group_entry == g_view.group_entry).to_be_true();
		g_view = {};
	});
	it("puts a chain's knobs on a page its publisher finds outside its device pages", _{
		project::Song song = song_in_mode(5);
		effects::EffectStates flanger{};
		flanger.mod_fx.type = effects::ModFXType::FLANGER;
		song.master_effects.effects = plugin::StateHandle::of<effects::DelugeEffects>(flanger);
		g_view = {.current = 0};
		const controls::SectionPages pages = controls::section_pages(song, kSong, kSources);
		expect(int(pages.current.page_id)).to_equal(0);
		expect(turned(pages.current, 0) == ge::kModFXDepth).to_be_true();
		expect(std::ranges::none_of(pages.cycle, [](const controls::Page& p) { return p.page_id == 0; })).to_be_true();
		g_view = {};
	});
	it("hands the view reading the chain's record, the compressor view, the song flag and the section", _{
		project::Song song{};
		project::DocOutput& kit = song.output_structure.getOrCreate(8);
		kit.mod_knob_mode = 4;
		kit.effects = project::FxChain{};
		controls::KnobFocus focus = kKitChain;
		focus.comp = {.editing = true, .param = 2};
		g_seen = {};
		(void)controls::section_pages(song, focus, kSources);
		expect(g_seen.chain == &*song.output_structure.find(8)->effects).to_be_true();
		expect(g_seen.comp == focus.comp).to_be_true();
		expect(g_seen.song).to_be_false();
		expect(int(g_seen.section)).to_equal(4);
		(void)controls::section_pages(song_in_mode(1), kSong, kSources);
		expect(g_seen.song).to_be_true();
		expect(int(g_seen.section)).to_equal(1);
	});
	it("freezes on a chain with no view reading", _{
		controls::PageSources bare = kSources;
		bare.chain_view = nullptr;
		test::clear_freeze();
		(void)controls::current_page(song_in_mode(1), kSong, bare);
		expect(test::last_freeze()).to_equal(std::string{"E-chainview"});
		test::clear_freeze();
	});
	it("asks the pressed section's published pages whether it keeps a stutter", _{
		project::Song song = filter_on_lower_knob(0);
		expect(controls::section_keeps_stutter(song, kKitChain, 4, kSources)).to_be_true();
		expect(controls::section_keeps_stutter(song, kKitChain, 3, kSources)).to_be_false();
		expect(controls::section_keeps_stutter(song, kSong, 4, kSources)).to_be_false();
		for (uint32_t section = 0; section < kDelugeControls.sections; section++) {
			expect(controls::section_keeps_stutter(song, kSynth, section, kSources)).to_be_false();
		}
		expect(controls::section_keeps_stutter(song, controls::KnobFocus{}, 4, kSources)).to_be_false();
	});
	it("has an empty page and no cycle on a chain's envelope button", _{
		const project::Song song = song_in_mode(2);
		const controls::SectionPages pages = controls::section_pages(song, kSong, kSources);
		expect(int(pages.current.section)).to_equal(2);
		expect(std::ranges::none_of(pages.current.slots, [](const controls::Slot& s) { return s.target.has_value(); }))
		    .to_be_true();
		expect(pages.cycle.empty()).to_be_true();
	});
	it("has an empty page with no focus or no publisher", _{
		const project::Song song = filter_on_lower_knob(3);
		expect(controls::current_page(song, controls::KnobFocus{}, kSources) == controls::Page{}).to_be_true();
		const controls::Page bare = controls::current_page(song, kSynth, {.board = kDelugeControls});
		expect(std::ranges::none_of(bare.slots, [](const controls::Slot& s) { return s.target.has_value(); }))
		    .to_be_true();
	});
	it("carries the reverb button's stutter rule onto a clip chain's pages", _{
		project::Song song{};
		song.output_structure.getOrCreate(8).mod_knob_mode = 4;
		const controls::SectionPages pages = controls::section_pages(song, kKitChain, kSources);
		expect(pages.cycle.size()).to_equal(size_t{1});
		expect(pages.current.keeps_stutter).to_be_true();
		expect(std::ranges::all_of(pages.cycle, [](const controls::Page& p) { return p.keeps_stutter; })).to_be_true();
	});
	it("builds the page the knobs are on with no allocation, for every owner", _{
		project::Song song = filter_on_lower_knob(3);
		song.output_structure.getOrCreate(5).mod_knob_ccs = {{}, {}, {}, {}, {}, {}, {}, {}};
		const controls::KnobFocus midi{.owner = controls::MidiFocus{.track = {5}, .clip = project::ClipAddress{}}};
		controls::KnobFocus comp = kSong;
		comp.comp = {.editing = true, .param = 1};
		for (const uint8_t mode : {uint8_t{1}, uint8_t{4}, uint8_t{5}}) {
			song.mod_knob_mode = mode;
			song.output_structure.getOrCreate(3).mod_knob_mode = mode;
			song.output_structure.getOrCreate(8).mod_knob_mode = mode;
			for (const controls::KnobFocus& focus : {kSynth, kSong, comp, kKitChain, midi, controls::KnobFocus{}}) {
				expect(current_page_allocations(song, focus)).to_equal(size_t{0});
			}
		}
	});
});
// clang-format on

CPPSPEC_SPEC(pages)
