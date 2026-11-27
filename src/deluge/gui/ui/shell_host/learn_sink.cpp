#include "gui/ui/shell_host/shell_host.h"

#include "definitions_cxx.hpp"
#include "io/midi/device_registry.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_engine.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"
#include "project/device_settings.h"
#include "project/midi_command.h"

#include <optional>
#include <utility>

namespace deluge::gui::shell_host {
namespace {

using deluge::ui::scenes::bind::LearnedBinding;

/// @brief Make @p binding what @p learned holds, or clear it for nullopt, as the menu's learn items wrote the engine's
///        slot: the cable the device names, the channel or zone, and the number. A binding naming a device with no
///        cable stores nothing.
void store(::LearnedMIDI& learned, const std::optional<LearnedBinding>& binding) {
	if (!binding) {
		learned.clear();
		return;
	}
	::MIDICable* cable = nullptr;
	if (binding->device) {
		cable = midi::cable_of(*binding->device);
		if (cable == nullptr) {
			return;
		}
	}
	learned.cable = cable;
	learned.channelOrZone = binding->channel_or_zone.value_or(MIDI_CHANNEL_NONE);
	learned.noteOrCC = binding->note_or_cc.value_or(project::MidiCommand::kNone);
}

/// The learns, applied to the current song.
class HostLearnSink final : public deluge::ui::scenes::bind::LearnSink {
public:
	bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	           std::optional<deluge::ui::scenes::bind::LearnInput> input) override {
		if (currentSong == nullptr) {
			return false;
		}
		if (!command) {
			// `MenuItemWithCCLearning::unlearnAction`: the menu unlearns only a param's knob, which marks the current
			// instrument edited (`SoundEditor::markInstrumentAsEdited`), as `learn_knob` does for the learn.
			edits::UnlearnMapping edit{std::move(target)};
			if (!applied(edit)) {
				return false;
			}
			if (Instrument* instrument = getCurrentInstrument()) {
				instrument->beenEdited();
			}
			return true;
		}
		std::optional<edits::InputLearn> learned_input;
		if (input) {
			learned_input =
			    edits::InputLearn{.cable = midi::cable_of(input->device), .channel_or_zone = input->channel_or_zone};
		}
		edits::LearnMapping edit{project::Mapping{.source = std::move(*command), .target = std::move(target)},
		                         std::nullopt, learned_input};
		return applied(edit);
	}

	bool learn_knob(const project::OwnerParamTarget& target, midi::DeviceId device, int32_t which_knob,
	                int32_t midi_channel) override {
		if (currentSong == nullptr) {
			return false;
		}
		const std::optional<model::EffectsOwner> effects = bridge::effectsOwnerAt(*currentSong, target.owner);
		const model::LaneOwner owner = bridge::controlsSlotAt(*currentSong, target.owner);
		if (!effects || model::is_none(owner)) {
			return false;
		}
		namespace params = modulation::params;
		const modulation::KnobTarget knob = bridge::knobOfControl(
		    target.param, effects->sound != nullptr ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL);
		if (!knob) {
			return false;
		}
		// `SoundEditor::modEncoderAction` learns a mod knob in the current output's knob mode.
		::Output* output = getCurrentOutput();
		const int32_t mode = midi_channel >= 16 && output != nullptr ? output->modKnobMode : 0;
		// A mod knob names no device, whose cable is null.
		if (!edits::learnKnobOn(owner, knob, midi::cable_of(device), which_knob, mode, midi_channel)) {
			return false;
		}
		// `SoundEditor::markInstrumentAsEdited`.
		if (Instrument* instrument = getCurrentInstrument()) {
			instrument->beenEdited();
		}
		return true;
	}

	void learn_global(project::GlobalCommand command, std::optional<LearnedBinding> binding) override {
		// `midi::Command`'s learns and unlearn (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`) write the
		// slot and save nothing; the settings menu saves on close.
		store(midiEngine.globalMIDICommands[std::to_underlying(command)], binding);
	}

	void learn_follow(project::FollowInput input, std::optional<LearnedBinding> binding) override {
		// `midi::FollowChannel`'s learns, unlearn and value write
		// (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_channel.h`).
		store(midiEngine.midiFollowChannelType[std::to_underlying(input)], binding);
	}
};

} // namespace

deluge::ui::scenes::bind::LearnSink& learn_sink() {
	static HostLearnSink sink;
	return sink;
}

} // namespace deluge::gui::shell_host
