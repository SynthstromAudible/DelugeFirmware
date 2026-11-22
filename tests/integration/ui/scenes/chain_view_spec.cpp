// The Deluge's reading of a chain's view (ui::scenes::bind::chain_view), through the pages the controls assemble from
// it: the filter button on the chain's filter type (EQ past it), the mod FX button on its mod FX view, and the song's
// reverb button on the compressor setting the knobs' compressor view edits, each a page the chain publishes.

#include "board_controls.hpp"
#include "ui/scenes/bind/page_sources.h"

#include "effects/plugin.h"
#include "effects/setting_params.h"
#include "host/chains/pages.h"
#include "host/chains/params.h"
#include "project/song.h"
#include "ui/controls/pages.h"

#include "cppspec.hpp"
#include "support/printers/param_id_printer.h"

#include "support/spec_main.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

namespace bind = deluge::ui::scenes::bind;
namespace controls = deluge::ui::controls;
namespace project = deluge::project;
namespace ge = deluge::host::chains::param;

const controls::KnobFocus kSong{.owner = controls::SongFocus{}};
const controls::KnobFocus kKitChain{.owner = controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, 8}}};

/// The song in mode @p mode.
project::Song song_in_mode(uint8_t mode) {
	project::Song song{};
	song.mod_knob_mode = mode;
	return song;
}

/// The ENGINE-space param @p page's slot @p slot turns.
deluge::plugin::ParamId turned(const controls::Page& page, size_t slot) {
	return page.slots[slot].target ? page.slots[slot].target->id : deluge::plugin::ParamId{0xFFFF'FFFF};
}

controls::SectionPages pages_of(const project::Song& song, const controls::KnobFocus& focus) {
	return controls::section_pages(song, focus, bind::app_turn_sources(kDelugeControls));
}

} // namespace

// clang-format off
describe chain_view("the Deluge's reading of a chain's view", ${
	it("puts a chain's filter knobs on the page of its filter view, a view past EQ on EQ", _ {
		project::Song song = song_in_mode(1);
		song.master_effects.filter_type = 1;
		expect(int(pages_of(song, kSong).current.page_id)).to_equal(1);
		song.master_effects.filter_type = 7;
		const controls::SectionPages pages = pages_of(song, kSong);
		expect(int(pages.current.page_id)).to_equal(int(deluge::host::chains::kEqPage));
		expect(turned(pages.current, 1)).to_equal(ge::kTreble, "the EQ page's second knob");
	});
	it("puts a chain's mod FX knobs on the view its record holds, even one its type has no param for", _ {
		project::Song song = song_in_mode(5);
		song.master_effects.mod_fx_param = 0;
		deluge::effects::EffectStates flanger{};
		flanger.mod_fx.type = deluge::effects::ModFXType::FLANGER;
		song.master_effects.effects = deluge::plugin::StateHandle::of<deluge::effects::DelugeEffects>(flanger);
		const controls::SectionPages pages = pages_of(song, kSong);
		expect(int(pages.current.page_id)).to_equal(0);
		expect(turned(pages.current, 0)).to_equal(ge::kModFXDepth, "the mod FX page's first knob");
		expect(std::ranges::none_of(pages.cycle, [](const controls::Page& p) { return p.page_id == 0; })).to_be_true();
	});
	it("gives a chain that does not resolve a new chain's mod FX view", _ {
		project::Song song{};
		song.output_structure.getOrCreate(8).mod_knob_mode = 5;
		expect(int(pages_of(song, kKitChain).current.page_id)).to_equal(int(project::FxChain{}.mod_fx_param));
	});
	it("enters the song's compressor group at the setting its compressor view holds, and edits it while editing", _ {
		const project::Song song = song_in_mode(4);
		controls::KnobFocus focus = kSong;
		focus.comp = {.editing = false, .param = 2};
		const controls::SectionPages reverb = pages_of(song, focus);
		expect(int(reverb.group_entry[1])).to_equal(3);
		expect(int(reverb.current.page_id)).to_equal(0);
		focus.comp.editing = true;
		const controls::SectionPages comp = pages_of(song, focus);
		expect(int(comp.current.page_id)).to_equal(3);
		expect(int(comp.current.group)).to_equal(1);
	});
	it("enters the compressor group at each setting's page of the chain's published group, and reaches every one", _ {
		namespace setting = deluge::effects::setting;
		const std::array kSettings{setting::kCompRatio, setting::kCompAttack, setting::kCompRelease, setting::kCompHpf,
		                           setting::kCompBlend};
		const project::Song song = song_in_mode(4);
		const controls::SectionPages pages = pages_of(song, kSong);
		const auto group_one = std::ranges::count_if(pages.cycle, [](const controls::Page& p) { return p.group == 1; });
		expect(size_t(group_one)).to_equal(kSettings.size());
		for (uint8_t param = 0; param < kSettings.size(); param++) {
			controls::KnobFocus focus = kSong;
			focus.comp = {.editing = false, .param = param};
			const uint32_t entry = pages_of(song, focus).group_entry[1];
			const auto found = std::ranges::find_if(pages.cycle, [entry](const controls::Page& p) { return p.page_id == entry; });
			expect(found).not_().to_equal(pages.cycle.end(), "the compressor setting's entry page");
			if (found != pages.cycle.end()) {
				expect(int(found->group)).to_equal(1);
				expect(turned(*found, 0)).to_equal(kSettings[param], "the entry page's first knob");
			}
		}
	});
	it("keeps a clip's chain on its reverb page whatever the compressor view holds", _ {
		project::Song song{};
		song.output_structure.getOrCreate(8).mod_knob_mode = 4;
		controls::KnobFocus focus = kKitChain;
		focus.comp = {.editing = true, .param = 2};
		expect(int(pages_of(song, focus).current.page_id)).to_equal(0);
	});
	it("puts every other section on its first page", _ {
		for (const uint32_t section : {0u, 2u, 3u, 6u, 7u}) {
			project::FxChain chain{};
			chain.filter_type = 2;
			chain.mod_fx_param = 2;
			expect(int(bind::chain_view(&chain, {.editing = true, .param = 1}, true, section).current)).to_equal(0);
		}
	});
});
// clang-format on

CPPSPEC_SPEC(chain_view)
