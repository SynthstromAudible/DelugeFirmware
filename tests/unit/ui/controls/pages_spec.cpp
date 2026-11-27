// clang-format off
#include "board_controls.hpp"
#include "ui/controls/pages.h"
#include "ui/controls/resolve.h"
#include "project/song.h"
#include "host/chains/pages.h"
#include "host/chains/params.h"
#include "effects/plugin.h"
#include "instruments/classic/pages.h"
#include "instruments/classic/params.h"
#include "instruments/classic/deluge_synth.h"
#include "support/printers/controls_printer.h"
#include "support/printers/knob_focus_printer.h"
#include "support/printers/param_id_printer.h"
#include <algorithm>
#include <array>
#include "instruments/classic/state_rules.h"
#include <cppspec.hpp>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include "mock_panic.h"

using namespace deluge;
namespace ip = deluge::instruments::classic::param;
namespace ge = deluge::host::chains::param;
using deluge::instruments::classic::SynthMode;

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
const std::array<plugin::RemoteControls, 2> kPublishers{instruments::classic::kRemoteControls, host::chains::kRemoteControls};
/// What the stand-in view reading last saw.
struct ViewCall {
	const project::FxChain* chain{nullptr};
	ui::controls::CompView comp;
	bool song{false};
	uint32_t section{0xFF};
};
ViewCall g_seen;
/// What the stand-in view reading answers: the app's mapping is pinned in the scenes' chain_view_spec.
ui::controls::ChainView g_view;
ui::controls::ChainView view_stand_in(const project::FxChain* chain, const ui::controls::CompView& comp, bool song,
                                  uint32_t section) {
	g_seen = {.chain = chain, .comp = comp, .song = song, .section = section};
	return g_view;
}
const ui::controls::PageSources kSources{.publishers = kPublishers, .board = kDelugeControls, .chain_view = &view_stand_in};

/// A synth on output 3 in mode @p mode, its lower knob there the low-pass cutoff and its upper the resonance, playing a
/// sound in @p synth_mode.
project::Song filter_on_lower_knob(uint8_t mode, SynthMode synth_mode = SynthMode::SUBTRACTIVE) {
	project::Song song{};
	project::DocOutput& output = song.output_structure.getOrCreate(3);
	output.mod_knob_mode = mode;
	output.controls = {{.control = {.button = mode, .knob = 0}, .target = project::engine_target(ip::kLpfFrequency.value)},
	                   {.control = {.button = mode, .knob = 1}, .target = project::engine_target(ip::kLpfResonance.value)}};
	output.instrument = project::InstrumentSlot{
	    .plugin = "deluge.instrument",
	    .state = plugin::StateHandle::of<instruments::classic::DelugeSynth>(instruments::classic::switch_engine(instruments::classic::SoundState{}, synth_mode))};
	return song;
}
const ui::controls::KnobFocus kSynth{.owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 3}}};
const ui::controls::KnobFocus kSong{.owner = ui::controls::SongFocus{}};
/// A kit's chain on output 8.
const ui::controls::KnobFocus kKitChain{.owner = ui::controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 8}}};

/// The song in mode @p mode.
project::Song song_in_mode(uint8_t mode) {
	project::Song song{};
	song.mod_knob_mode = mode;
	return song;
}

/// The ENGINE-space param @p page's slot @p slot turns.
plugin::ParamId turned(const ui::controls::Page& page, size_t slot) {
	return page.slots[slot].target ? page.slots[slot].target->id : plugin::ParamId{0xFFFF'FFFF};
}

/// The allocations current_page makes for @p focus on @p song.
size_t current_page_allocations(const project::Song& song, const ui::controls::KnobFocus& focus) {
	const size_t before = g_allocations;
	const ui::controls::Page page = ui::controls::current_page(song, focus, kSources);
	const size_t made = g_allocations - before;
	(void)page;
	return made;
}
} // namespace

describe pages("the pages a press moves between, from the published records and the song's", ${
	it("cycles a sound's filter knob on the lower knob of any button, its resonance partner with it", _{
		const project::Song song = filter_on_lower_knob(3);
		const ui::controls::Target target =
		    ui::controls::resolve_press(kSynth, ui::controls::section_pages(song, kSynth, kSources), 0, true);
		const auto* page = std::get_if<ui::controls::SelectPage>(&target);
		expect(page).not_().to_be_null();
		if (page != nullptr) {
			expect(page->slots[0].target->id).to_equal(ip::kHpfFrequency);
			expect(page->slots[1].target->id).to_equal(ip::kHpfResonance);
		}
	});
	it("does not cycle an FM sound's filter knob", _{
		const project::Song song = filter_on_lower_knob(1, SynthMode::FM);
		const ui::controls::Target target =
		    ui::controls::resolve_press(kSynth, ui::controls::section_pages(song, kSynth, kSources), 0, true);
		expect(std::holds_alternative<std::monostate>(target)).to_be_true();
	});
	it("gives an owner with no controls the empty layout: no pages, no cycle, no allocation", _{
		const project::Song song = song_in_mode(1);
		const ui::controls::KnobFocus drum{.owner = ui::controls::NoControlsFocus{
		    .kind = ui::controls::NoControlsFocus::Kind::GATE_DRUM, .track = {2}, .drum = project::KitRowAddress{2, 9}}};
		const ui::controls::SectionPages pages = ui::controls::section_pages(song, drum, kSources);
		expect(pages.current).to_equal(ui::controls::Page{});
		expect(pages.cycle.empty()).to_be_true();
		expect(current_page_allocations(song, drum)).to_equal(size_t{0});
		expect(std::holds_alternative<std::monostate>(ui::controls::resolve_turn(drum, pages, 0))).to_be_true();
	});
	it("picks a MIDI knob's CC on a press, with no clip too", _{
		const project::Song song{};
		const ui::controls::KnobFocus midi{.owner = ui::controls::MidiFocus{.track = {5}}};
		const ui::controls::Target target =
		    ui::controls::resolve_press(midi, ui::controls::section_pages(song, midi, kSources), 1, true);
		expect(std::get<ui::controls::HostCommand>(target)).to_equal(ui::controls::HostCommand::MIDI_CC_SELECT_ON);
	});
	it("puts a chain's knobs on the page its view reading names, found through its publisher", _{
		project::Song song = song_in_mode(1);
		g_view = {.current = 2, .group_entry = {0, 4}};
		const ui::controls::SectionPages pages = ui::controls::section_pages(song, kSong, kSources);
		expect(int(pages.current.page_id)).to_equal(2);
		expect(turned(pages.current, 1)).to_equal(ge::kTreble);
		expect(pages.current).to_equal(ui::controls::current_page(song, kSong, kSources));
		expect(pages.group_entry).to_equal(g_view.group_entry);
		g_view = {};
	});
	it("puts a chain's knobs on a page its publisher finds outside its device pages", _{
		project::Song song = song_in_mode(5);
		effects::EffectStates flanger{};
		flanger.mod_fx.type = effects::ModFXType::FLANGER;
		song.master_effects.effects = plugin::StateHandle::of<effects::DelugeEffects>(flanger);
		g_view = {.current = 0};
		const ui::controls::SectionPages pages = ui::controls::section_pages(song, kSong, kSources);
		expect(int(pages.current.page_id)).to_equal(0);
		expect(turned(pages.current, 0)).to_equal(ge::kModFXDepth);
		expect(std::ranges::none_of(pages.cycle, [](const ui::controls::Page& p) { return p.page_id == 0; })).to_be_true();
		g_view = {};
	});
	it("hands the view reading the chain's record, the compressor view, the song flag and the section", _{
		project::Song song{};
		project::DocOutput& kit = song.output_structure.getOrCreate(8);
		kit.mod_knob_mode = 4;
		kit.effects = project::FxChain{};
		ui::controls::KnobFocus focus = kKitChain;
		focus.comp = {.editing = true, .param = 2};
		g_seen = {};
		(void)ui::controls::section_pages(song, focus, kSources);
		expect(g_seen.chain).to_equal(&*song.output_structure.find(8)->effects);
		expect(g_seen.comp).to_equal(focus.comp);
		expect(g_seen.song).to_be_false();
		expect(int(g_seen.section)).to_equal(4);
		(void)ui::controls::section_pages(song_in_mode(1), kSong, kSources);
		expect(g_seen.song).to_be_true();
		expect(int(g_seen.section)).to_equal(1);
	});
	it("freezes on a chain with no view reading", _{
		ui::controls::PageSources bare = kSources;
		bare.chain_view = nullptr;
		test::clear_freeze();
		(void)ui::controls::current_page(song_in_mode(1), kSong, bare);
		expect(test::last_freeze()).to_equal(std::string{"E-chainview"});
		test::clear_freeze();
	});
	it("asks the pressed section's published pages whether it keeps a stutter", _{
		project::Song song = filter_on_lower_knob(0);
		expect(ui::controls::section_keeps_stutter(song, kKitChain, 4, kSources)).to_be_true();
		expect(ui::controls::section_keeps_stutter(song, kKitChain, 3, kSources)).to_be_false();
		expect(ui::controls::section_keeps_stutter(song, kSong, 4, kSources)).to_be_false();
		for (uint32_t section = 0; section < kDelugeControls.sections; section++) {
			expect(ui::controls::section_keeps_stutter(song, kSynth, section, kSources)).to_be_false();
		}
		expect(ui::controls::section_keeps_stutter(song, ui::controls::KnobFocus{}, 4, kSources)).to_be_false();
	});
	it("has an empty page and no cycle on a chain's envelope button", _{
		const project::Song song = song_in_mode(2);
		const ui::controls::SectionPages pages = ui::controls::section_pages(song, kSong, kSources);
		expect(int(pages.current.section)).to_equal(2);
		expect(std::ranges::none_of(pages.current.slots, [](const ui::controls::Slot& s) { return s.target.has_value(); }))
		    .to_be_true();
		expect(pages.cycle.empty()).to_be_true();
	});
	it("has an empty page with no focus or no publisher", _{
		const project::Song song = filter_on_lower_knob(3);
		expect(ui::controls::current_page(song, ui::controls::KnobFocus{}, kSources)).to_equal(ui::controls::Page{});
		const ui::controls::Page bare = ui::controls::current_page(song, kSynth, {.board = kDelugeControls});
		expect(std::ranges::none_of(bare.slots, [](const ui::controls::Slot& s) { return s.target.has_value(); }))
		    .to_be_true();
	});
	it("carries the reverb button's stutter rule onto a clip chain's pages", _{
		project::Song song{};
		song.output_structure.getOrCreate(8).mod_knob_mode = 4;
		const ui::controls::SectionPages pages = ui::controls::section_pages(song, kKitChain, kSources);
		expect(pages.cycle.size()).to_equal(size_t{1});
		expect(pages.current.keeps_stutter).to_be_true();
		expect(std::ranges::all_of(pages.cycle, [](const ui::controls::Page& p) { return p.keeps_stutter; })).to_be_true();
	});
	it("builds the page the knobs are on with no allocation, for every owner and every mod button", _{
		using Cc = project::ModKnobCc;
		const ui::controls::KnobFocus midi{.owner = ui::controls::MidiFocus{.track = {5}, .clip = project::ClipAddress{}}};
		const ui::controls::KnobFocus none{.owner = ui::controls::NoControlsFocus{.kind = ui::controls::NoControlsFocus::Kind::CV_TRACK,
		                                                                   .track = {6}}};
		ui::controls::KnobFocus comp = kSong;
		comp.comp = {.editing = true, .param = 1};
		for (uint8_t mode = 0; mode < 8; mode++) {
			// A sound whose knobs on this button are saved (so its page decorates them), and a MIDI track whose CCs
			// are of every kind.
			project::Song song = filter_on_lower_knob(mode);
			song.mod_knob_mode = mode;
			song.output_structure.getOrCreate(8).mod_knob_mode = mode;
			project::DocOutput& midi_output = song.output_structure.getOrCreate(5);
			midi_output.mod_knob_mode = mode;
			midi_output.mod_knob_ccs.assign(16, Cc{});
			midi_output.mod_knob_ccs[(mode * 2) % 16] = {.kind = Cc::Kind::NUMBER, .number = 7};
			midi_output.mod_knob_ccs[(mode * 2 + 1) % 16] = {.kind = Cc::Kind::BEND};
			for (const ui::controls::KnobFocus& focus : {kSynth, kSong, comp, kKitChain, midi, none, ui::controls::KnobFocus{}}) {
				expect(current_page_allocations(song, focus)).to_equal(size_t{0});
			}
		}
	});
});
// clang-format on

CPPSPEC_SPEC(pages)
