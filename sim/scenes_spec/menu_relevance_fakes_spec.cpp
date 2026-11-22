/// @file
/// @brief The menu's relevance over the ports' fakes, with a model song of the spec's own and no legacy song behind
///        the menu: a node whose relevance reads a port appears and disappears as that port's fake changes.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/settings.h"

#include "instruments/deluge/sound_state.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "serialization/value.h"

#include "support/fake_host_rig.h"
#include "support/fake_menu_shell.h"
#include "support/record_state.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>

#include "support/spec_main.h"

using namespace deluge::scenes;
using deluge::project::Feature;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using device_path = deluge::project::device_path;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// @brief Is @p node listed on @p page in @p ctx?
bool listed(const Node& page, const Node& node, const MenuContext& ctx) {
	return spec::row_of(page, node, ctx).has_value();
}

/// @brief The synth's state in the sound fake, which the spec edits in place.
deluge::instrument::SoundState& synth_state(spec::FakeHostRig& fakes) {
	return fakes.host.sound.states.at(spec::FakeHostRig::kSynthSlot.id);
}

} // namespace

// clang-format off
describe menu_relevance_fakes("the menu's relevance over the ports' fakes and a model song", ${
	it("lists the FM modulators on the sound root only while the sound fake answers an FM sound", _ {
		spec::FakeHostRig fakes;
		const MenuContext ctx = fakes.synth_context();
		expect(listed(nodes::sound_root, nodes::modulator0, ctx)).to_be_false();
		expect(listed(nodes::sound_root, nodes::modulator1, ctx)).to_be_false();

		deluge::test::set(synth_state(fakes), "mode", deluge::instrument::to_string(deluge::instrument::SynthMode::FM));
		expect(listed(nodes::sound_root, nodes::modulator0, ctx)).to_be_true();
		expect(listed(nodes::sound_root, nodes::modulator1, ctx)).to_be_true();

		deluge::test::set(synth_state(fakes), "mode", deluge::instrument::to_string(deluge::instrument::SynthMode::SUBTRACTIVE));
		expect(listed(nodes::sound_root, nodes::modulator0, ctx)).to_be_false();
	});

	it("offers a sample source's transpose only while the sound fake says its file is loaded", _ {
		spec::FakeHostRig fakes;
		fakes.hold_synth(deluge::test::with_kind(deluge::instrument::SoundState{}, deluge::instrument::SourceId::A,
		                                         deluge::instrument::OscType::SAMPLE));
		const MenuContext ctx = fakes.synth_context();
		fakes.host.sound.loaded[spec::FakeHostRig::kSynthSlot.id] = 0;
		expect(menu::bindings::relevant(nodes::source0_transpose, ctx)).to_be_false();

		fakes.host.sound.loaded[spec::FakeHostRig::kSynthSlot.id] = 0b01;
		expect(menu::bindings::relevant(nodes::source0_transpose, ctx)).to_be_true();
		// The other source's file says nothing of this one's.
		fakes.host.sound.loaded[spec::FakeHostRig::kSynthSlot.id] = 0b10;
		expect(menu::bindings::relevant(nodes::source0_transpose, ctx)).to_be_false();
	});

	it("lists the battery level on the settings root only while the device fake's feature shows it", _ {
		spec::FakeHostRig fakes;
		const MenuContext ctx = fakes.settings_context(/*with_model=*/false);
		const std::string path{device_path::feature(Feature::SHOW_BATTERY_LEVEL)};
		fakes.host.device.values.insert_or_assign(path, deluge::serialization::Value::makeNumber(0));
		expect(listed(nodes::root_settings, nodes::battery_level, ctx)).to_be_false();

		fakes.host.device.values.insert_or_assign(path, deluge::serialization::Value::makeNumber(1));
		expect(listed(nodes::root_settings, nodes::battery_level, ctx)).to_be_true();
		expect(fakes.host.device.stores.empty()).to_be_true();
	});
	it("offers the audio clip's waveform only while the waveforms fake holds the clip's finished file", _ {
		spec::FakeHostRig fakes;
		MenuContext ctx = fakes.synth_context();
		ctx.is_audio_clip = true;
		ctx.addresses.clip = spec::FakeHostRig::kSynthClip;
		expect(menu::bindings::relevant(nodes::audio_clip_waveform, ctx)).to_be_false();

		const bind::WaveformAddress clip{.kind = bind::WaveformAddress::Kind::AUDIO_CLIP,
		                                 .clip = spec::FakeHostRig::kSynthClip};
		fakes.host.waveforms.waveforms.push_back(
		    {.at = clip, .info = {.length_samples = 1000, .sample_rate = 44100, .channels = 1, .identity = 1}});
		expect(menu::bindings::relevant(nodes::audio_clip_waveform, ctx)).to_be_true();
		// A take still being recorded is no file yet.
		fakes.host.waveforms.waveforms[0].info.recording = true;
		expect(menu::bindings::relevant(nodes::audio_clip_waveform, ctx)).to_be_false();
		fakes.host.waveforms.waveforms[0].info.recording = false;
		// No waveforms port: nothing says the file is loaded.
		ctx.ports.waveforms = nullptr;
		expect(menu::bindings::relevant(nodes::audio_clip_waveform, ctx)).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(menu_relevance_fakes_spec, menu_relevance_fakes)
