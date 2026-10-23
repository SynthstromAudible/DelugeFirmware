// The arp menus edit the arp their root names, by descriptor: a synth's, MIDI or CV clip's arp; a kit row's (sound,
// MIDI or gate) lane arp; the whole kit's clip arp. Each node shown on a root reads and writes that owner's record,
// never another root's, and choosing the Custom preset still opens the octave and note pages.

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/target.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/roots.h"
#include "scenes/menu/nodes/sound_arp.h"
#include "scenes/menu/nodes/sound_randomizer.h"

#include "deluge-project/setting_group.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;

namespace {

namespace nodes = menu::nodes;

/// The arp nodes bound to the arp's rows.
const Node* const kArpTargets[] = {
    &nodes::arp_mode,
    &nodes::arp_sync,
    &nodes::arp_octaves,
    &nodes::arp_step_repeat,
    &nodes::arp_octave_mode,
    &nodes::arp_note_mode,
    &nodes::arp_chord_simulator,
    &nodes::arp_mpe_velocity,
    &nodes::arp_include_in_kit_arp,
    &nodes::arp_rhythm_midi,
    &nodes::arp_sequence_length_midi,
    &nodes::arp_gate_midi,
    &nodes::arp_rate_midi,
    &nodes::randomizer_lock,
    &nodes::spread_gate_midi,
    &nodes::spread_octave_midi,
    &nodes::spread_velocity_midi,
    &nodes::ratchet_amount_midi,
    &nodes::ratchet_probability_midi,
    &nodes::chord_polyphony_midi,
    &nodes::chord_probability_midi,
    &nodes::note_probability_midi,
    &nodes::swap_probability_midi,
    &nodes::bass_probability_midi,
    &nodes::glide_probability_midi,
};

/// One root and the arp record its menus edit.
struct Root {
	std::string name;
	MenuContext ctx;
	SettingAddress owner;
};

MenuContext at(Song& song, Clip* clip, bool kit_global_fx = false) {
	return menu::context_for(song, deluge::gui::menuViewOf(song, clip), 0, false, false, false, kit_global_fx);
}

/// Which nodes of kArpTargets, shown at @p root, do not read back what they wrote, or whose write names another owner.
std::vector<std::string> misbound(const Root& root) {
	std::vector<std::string> found;
	for (const Node* node : kArpTargets) {
		if (!node->bind.target.valid()) {
			found.push_back(root.name + ": a node has no target");
			continue;
		}
		if (!menu::bindings::relevant(*node, root.ctx)) {
			continue;
		}
		const std::optional<SettingAddress> owner = bind::setting_address(node->bind.target, root.ctx.addresses);
		if (owner != root.owner) {
			found.push_back(root.name + " " + std::string(node->bind.target.path) + ": edits another owner");
			continue;
		}
		const menu::IntRange range = menu::bindings::range(*node, root.ctx);
		const int32_t before = menu::bindings::read(*node, root.ctx);
		const int32_t wanted = before == range.max ? range.min : range.max;
		menu::bindings::write(*node, root.ctx, wanted);
		if (menu::bindings::read(*node, root.ctx) != wanted) {
			found.push_back(root.name + " " + std::string(node->bind.target.path) + ": reads back "
			                + std::to_string(menu::bindings::read(*node, root.ctx)) + " for " + std::to_string(wanted));
		}
	}
	return found;
}

} // namespace

// clang-format off
describe arp_targets("the arp menus edit the arp their root names", ${
	it("edits a synth clip's arp", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = at(*f.song, f.clip);
		expect(misbound({"synth", ctx, {SettingGroupId::CLIP_ARP, *ctx.addresses.clip}})).to_equal(std::vector<std::string>{});
	});

	it("edits a MIDI clip's arp", _ {
		spec::SongFixture f = spec::make_midi_song();
		const MenuContext ctx = at(*f.song, f.clip);
		expect(misbound({"midi", ctx, {SettingGroupId::CLIP_ARP, *ctx.addresses.clip}})).to_equal(std::vector<std::string>{});
	});

	it("edits a sound, a MIDI and a gate kit row's lane arp", _ {
		for (int kind = 0; kind < 3; ++kind) {
			spec::SongFixture f = spec::make_kit_song();
			if (kind == 0) {
				(void)spec::add_drum<SoundDrum>(f);
			}
			else if (kind == 1) {
				(void)spec::add_drum<MIDIDrum>(f);
			}
			else {
				(void)spec::add_drum<GateDrum>(f);
			}
			const MenuContext ctx = at(*f.song, f.clip);
			expect(ctx.addresses.lane.has_value()).to_be_true();
			expect(misbound({"lane " + std::to_string(kind), ctx, {SettingGroupId::LANE_ARP, *ctx.addresses.lane}}))
			    .to_equal(std::vector<std::string>{});
		}
	});

	it("edits the whole kit's clip arp", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = at(*f.song, f.clip, true);
		expect(ctx.addresses.lane.has_value()).to_be_false();
		expect(misbound({"whole kit", ctx, {SettingGroupId::CLIP_ARP, *ctx.addresses.clip}}))
		    .to_equal(std::vector<std::string>{});
	});

	it("keeps a kit row that cannot hold note tails out of its kit's arp, and shows it off", _ {
		spec::SongFixture f = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(f);
		char memory[MODEL_STACK_MAX_SIZE];
		PatchedParamSet* patched = getModelStackFromSoundDrum(memory, drum)->paramManager->getPatchedParamSet();
		patched->params[deluge::modulation::params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
		    *patched->docForParam(deluge::modulation::params::LOCAL_ENV_0_SUSTAIN), std::numeric_limits<int32_t>::min());
		patched->params[deluge::modulation::params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    *patched->docForParam(deluge::modulation::params::LOCAL_ENV_0_RELEASE), std::numeric_limits<int32_t>::max());
		const MenuContext ctx = at(*f.song, f.clip);
		const SettingAddress lane{SettingGroupId::LANE_ARP, *ctx.addresses.lane};
		const auto before = deluge::project::read_setting(f.song->project, lane, "arpeggiator/kitArp");
		menu::bindings::write(nodes::arp_include_in_kit_arp, ctx, before && before->str() == "1" ? 0 : 1);
		const auto after = deluge::project::read_setting(f.song->project, lane, "arpeggiator/kitArp");
		expect(after.has_value() && before.has_value() && after->str() == before->str()).to_be_true();
		expect(menu::bindings::read(nodes::arp_include_in_kit_arp, ctx)).to_equal(0);
	});

	it("shows Custom once chosen, and chains to the octave mode page", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = at(*f.song, f.clip);
		menu::bindings::write(nodes::arp_preset, ctx, static_cast<int32_t>(ArpPreset::CUSTOM));
		expect(menu::bindings::read(nodes::arp_preset, ctx)).to_equal(static_cast<int32_t>(ArpPreset::CUSTOM));
		const menu::MenuState state{};
		expect(nodes::arp_preset_chain(ctx, state) == &nodes::arp_octave_mode_to_note_mode).to_be_true();
	});
});

CPPSPEC_SPEC(arp_targets)
