// A param node a sound and a host chain share (the EQ) edits the owner its root names and no other: a synth's sound, a
// kit row's, a kit's own effects, an audio track's clip, the song. A host chain's own param node (its filter) shows
// where the chain's filter is on, and a kit's own-effects write reaches no drum.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/roots.h"
#include "scenes/menu/nodes/sound_fx.h"

#include "deluge-project/param_address.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>

using namespace deluge::scenes;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using deluge::scenes::menu::MenuContext;
namespace params = deluge::modulation::params;

namespace {

namespace nodes = menu::nodes;

std::optional<int32_t> playing(Song& song, OwnerId owner, params::Kind kind, int32_t index,
                               std::optional<uint32_t> clip) {
	const ParamAddress address{.owner = owner,
	                           .space = deluge::project::ParamSpace::ENGINE,
	                           .id = deluge::bridge::paramIdOf(kind, index),
	                           .clip_id = clip};
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	return stack != nullptr ? std::optional{stack->autoParam->getCurrentValue()} : std::nullopt;
}

/// Writes the menu's greatest value (or its least, when it holds that already) through @p node at @p ctx; the value
/// written, as the node reads it back.
int32_t turn(const menu::Node& node, const MenuContext& ctx) {
	const menu::IntRange range = menu::bindings::range(node, ctx);
	const int32_t wanted = menu::bindings::read(node, ctx) == range.max ? range.min : range.max;
	menu::bindings::write(node, ctx, wanted);
	return menu::bindings::read(node, ctx);
}

} // namespace

// clang-format off
describe param_chain_targets("a param node a sound and a host chain share", ${
	it("edits a kit row's sound on its row, and the kit's own effects on the kit, never the other", _ {
		spec::SongFixture kit = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(kit);
		kit.song->setCurrentClip(kit.clip);
		const MenuContext on_row = menu::context_for(*kit.song, deluge::gui::menuViewOf(*kit.song, kit.clip), 0, false, false, false, false);
		const MenuContext on_fx = menu::context_for(*kit.song, deluge::gui::menuViewOf(*kit.song, kit.clip), 0, false, false, false, true);
		const auto row_bass = [&] {
			return playing(*kit.song, OwnerId::drum(*drum->id()), params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS,
			               kit.clip->id());
		};
		const auto kit_bass = [&] {
			return playing(*kit.song, OwnerId::instrument(*kit.instrument->id()), params::Kind::UNPATCHED_GLOBAL,
			               params::UNPATCHED_BASS, kit.clip->id());
		};
		const auto fx_before = kit_bass();
		const int32_t on_row_value = turn(nodes::eq_bass, on_row);
		expect(menu::bindings::read(nodes::eq_bass, on_row)).to_equal(on_row_value);
		expect(kit_bass()).to_equal(fx_before);
		const auto row_before = row_bass();
		(void)turn(nodes::eq_bass, on_fx);
		expect(row_bass()).to_equal(row_before);
		expect(kit_bass() != fx_before).to_be_true();
	});

	it("edits an audio track's clip and the song's own", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		audio.song->setCurrentClip(audio.clip);
		const MenuContext on_audio = menu::context_for(*audio.song, deluge::gui::menuViewOf(*audio.song, audio.clip), 0, false, false, false, false);
		const auto clip_bass = [&] {
			return playing(*audio.song, OwnerId::clip(audio.clip->id()), params::Kind::UNPATCHED_GLOBAL,
			               params::UNPATCHED_BASS, std::nullopt);
		};
		const auto song_bass = [&] {
			return playing(*audio.song, OwnerId::song(), params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_BASS,
			               std::nullopt);
		};
		const auto song_before = song_bass();
		const auto clip_before = clip_bass();
		(void)turn(nodes::eq_bass, on_audio);
		expect(clip_bass() != clip_before).to_be_true();
		expect(song_bass()).to_equal(song_before);

		audio.song->setCurrentClip(nullptr);
		const MenuContext on_song = menu::context_for(*audio.song, deluge::gui::menuViewOf(*audio.song, nullptr), 0, false, true, false, false);
		const auto clip_now = clip_bass();
		(void)turn(nodes::eq_bass, on_song);
		expect(song_bass() != song_before).to_be_true();
		expect(clip_bass()).to_equal(clip_now);
	});

	it("shows a kit's own arp rate on the kit's effects root only", _ {
		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		kit.song->setCurrentClip(kit.clip);
		const MenuContext on_fx = menu::context_for(*kit.song, deluge::gui::menuViewOf(*kit.song, kit.clip), 0, false, false, false, true);
		const MenuContext on_row = menu::context_for(*kit.song, deluge::gui::menuViewOf(*kit.song, kit.clip), 0, false, false, false, false);
		expect(menu::bindings::relevant(nodes::arp_kit_rate, on_fx)).to_be_true();
		expect(menu::bindings::relevant(nodes::arp_kit_rate, on_row)).to_be_false();
	});

	it("shows a host chain's filter params only where its filter is on", _ {
		spec::SongFixture kit = spec::make_kit_song();
		kit.song->setCurrentClip(kit.clip);
		const MenuContext on_fx = menu::context_for(*kit.song, deluge::gui::menuViewOf(*kit.song, kit.clip), 0, false, false, false, true);
		auto& owner = *static_cast<Kit*>(kit.instrument);
		deluge::edits::changeEffect(*kit.song, owner, "lpfMode", deluge::serialization::Value::makeScalar("Off"));
		expect(menu::bindings::relevant(nodes::global_lpf_freq, on_fx)).to_be_false();
		deluge::edits::changeEffect(*kit.song, owner, "lpfMode", deluge::serialization::Value::makeScalar("24dB"));
		expect(menu::bindings::relevant(nodes::global_lpf_freq, on_fx)).to_be_true();
	});
});

CPPSPEC_SPEC(param_chain_targets)
