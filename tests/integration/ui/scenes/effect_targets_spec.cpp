// An effect target's write through the app's sink reaches the owner its menu names: a synth's or a kit row's own
// effects by its slot, else the whole kit's, an audio track's or the master's chain; and a sidechain timing write
// recomputes the reverb's, wherever it lands.

#include "gui/ui/menu_view_of.h"
#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/bind_host.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"

#include "effects/plugin.h"
#include "effects/settings.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/song/song.h"
#include "plugin/types/setting.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"

#include "support/menu_engine.h"
#include "support/printers/project_address_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/menu_context.h"
#include "support/spec_main.h"

#include <string>
#include <vector>

using namespace deluge::ui::scenes;
using deluge::effects::FilterMode;
using deluge::project::ChainAddress;
using deluge::ui::scenes::bind::ChainOwner;
using deluge::ui::scenes::bind::Target;
using deluge::ui::scenes::menu::MenuContext;

namespace {

Target effect(std::string_view path) {
	return {.kind = Target::Kind::EFFECT, .path = path};
}

/// The descriptor menu value of @p word at CHOICE row @p path.
int32_t menu_of(std::string_view path, std::string_view word) {
	const auto& row = *deluge::plugin::find_setting(deluge::effects::kEffectSettings, path);
	return deluge::plugin::to_menu(row, deluge::plugin::stored_value(row, word).value_or(0));
}

/// Writes @p path = @p menu through the app's sink at @p ctx's addresses.
void write(const MenuContext& ctx, std::string_view path, int32_t menu) {
	menu::host_ports().writes->write({.target = effect(path), .where = ctx.addresses, .menu_value = menu});
}

/// Writes the low-pass mode SVF_Notch at @p ctx.
void write_notch(const MenuContext& ctx) {
	write(ctx, "lpfMode", menu_of("lpfMode", "SVF_Notch"));
}

FilterMode lpf_of(deluge::model::LaneOwner owner) {
	return deluge::model::effects_owner_of(owner)->effects().filter.lpf_mode;
}

/// Whether @p chain's owner plays its record's effects.
bool chain_coherent(Song& song, ChainAddress chain) {
	const deluge::project::FxChain* record = song.project.resolveChain(chain);
	const std::optional<deluge::model::EffectsOwner> owner = deluge::bridge::chainAt(chain, song);
	return record != nullptr && owner && record->effects.as<deluge::effects::DelugeEffects>() == &owner->effects();
}

ChainAddress output_chain(Output& output) {
	return {.kind = ChainAddress::Kind::OUTPUT, .id = output.id().value_or(0)};
}

} // namespace

// clang-format off
describe effect_targets("an effect target's write reaches the owner its menu names", ${
	it("writes a synth's own effects by its slot", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		expect(ctx.addresses.slot.has_value()).to_be_true();
		expect(ctx.addresses.chain.has_value()).to_be_false();
		const FilterMode master = lpf_of(&f.song->live.master_channel);
		write_notch(ctx);
		expect(lpf_of(static_cast<SoundInstrument*>(f.instrument))).to_equal(FilterMode::SVF_NOTCH);
		expect(lpf_of(&f.song->live.master_channel)).to_equal(master);
	});

	it("writes a kit row's own effects by its slot, and not the kit's chain", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		expect(ctx.addresses.slot.has_value()).to_be_true();
		expect(ctx.addresses.chain.has_value()).to_be_false();
		const FilterMode chain = lpf_of(deluge::bridge::chain_owner_at(output_chain(*kit), *f.song));
		write_notch(ctx);
		expect(lpf_of(drum)).to_equal(FilterMode::SVF_NOTCH);
		expect(lpf_of(deluge::bridge::chain_owner_at(output_chain(*kit), *f.song))).to_equal(chain);
	});

	it("writes the whole kit's chain, and no drum's", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		auto* kit = static_cast<Kit*>(f.instrument);
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, /*kit_global_fx=*/true);
		expect(ctx.addresses.slot.has_value()).to_be_false();
		expect(ctx.addresses.chain).to_equal(std::optional{output_chain(*kit)});
		expect(ctx.addresses.chain_owner).to_equal(ChainOwner::KIT);
		const FilterMode row = lpf_of(drum);
		write_notch(ctx);
		expect(lpf_of(deluge::bridge::chain_owner_at(output_chain(*kit), *f.song))).to_equal(FilterMode::SVF_NOTCH);
		expect(chain_coherent(*f.song, output_chain(*kit))).to_be_true();
		expect(lpf_of(drum)).to_equal(row);
		expect(lpf_of(&f.song->live.master_channel)).not_().to_equal(FilterMode::SVF_NOTCH);
	});

	it("writes an audio track's chain", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		expect(ctx.addresses.chain).to_equal(std::optional{output_chain(*f.output)});
		expect(ctx.addresses.chain_owner).to_equal(ChainOwner::AUDIO_TRACK);
		write_notch(ctx);
		expect(lpf_of(deluge::bridge::chain_owner_at(output_chain(*f.output), *f.song))).to_equal(FilterMode::SVF_NOTCH);
		expect(chain_coherent(*f.song, output_chain(*f.output))).to_be_true();
		expect(lpf_of(&f.song->live.master_channel)).not_().to_equal(FilterMode::SVF_NOTCH);
	});

	it("writes the master's chain from the song root", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(nullptr), 0, false, true, false, false);
		expect(ctx.addresses.chain).to_equal(std::optional{ChainAddress{.kind = ChainAddress::Kind::SONG}});
		expect(ctx.addresses.chain_owner).to_equal(ChainOwner::SONG);
		const FilterMode synth = lpf_of(static_cast<SoundInstrument*>(f.instrument));
		write_notch(ctx);
		expect(lpf_of(&f.song->live.master_channel)).to_equal(FilterMode::SVF_NOTCH);
		expect(chain_coherent(*f.song, ChainAddress{.kind = ChainAddress::Kind::SONG})).to_be_true();
		expect(lpf_of(static_cast<SoundInstrument*>(f.instrument))).to_equal(synth);
	});

	it("edits the master from performance view, entered as production enters it", _ {
		spec::SongFixture f = spec::make_synth_song();
		// performance_view.cpp's openMenu: no clip, performance view, not a session or arranger root.
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(nullptr), 0, true, false, false, false);
		expect(ctx.root).to_equal(MenuContext::Root::Performance);
		expect(std::get<deluge::model::MasterChannel*>(deluge::ui::scenes::spec::menu_engine(ctx, *f.song).mod)).to_equal(&f.song->live.master_channel);
		expect(ctx.addresses.chain).to_equal(std::optional{ChainAddress{.kind = ChainAddress::Kind::SONG}});
		write_notch(ctx);
		expect(lpf_of(&f.song->live.master_channel)).to_equal(FilterMode::SVF_NOTCH);
	});

	it("recomputes the reverb after a sidechain timing write on any owner, and not after a filter write", _ {
		spec::SongFixture f = spec::make_kit_song();
		spec::add_drum<SoundDrum>(f);
		const MenuContext row = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		const MenuContext kit = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, /*kit_global_fx=*/true);
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
		write(row, "sidechain/attack", 20);
		expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
		write(kit, "sidechain/release", 20);
		expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
		write(kit, "sidechain/syncLevel", 3);
		expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
		write_notch(row);
		write_notch(kit);
		expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_false();
	});
});

DELUGE_SPEC(effect_targets_spec, effect_targets)
