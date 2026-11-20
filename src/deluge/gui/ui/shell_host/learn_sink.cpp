#include "gui/ui/shell_host/shell_host.h"

#include "io/midi/device_registry.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"

#include <optional>
#include <utility>

namespace deluge::gui::shell_host {
namespace {

/// The learns, applied to the current song.
class HostLearnSink final : public scenes::bind::LearnSink {
public:
	bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	           std::optional<scenes::bind::LearnInput> input) override {
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
		::ModControllable* owner = bridge::controlsSlotAt(*currentSong, target.owner);
		if (!effects || owner == nullptr) {
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
		if (!edits::learnKnobOn(*owner, knob, midi::cable_of(device), which_knob, mode, midi_channel)) {
			return false;
		}
		// `SoundEditor::markInstrumentAsEdited`.
		if (Instrument* instrument = getCurrentInstrument()) {
			instrument->beenEdited();
		}
		return true;
	}
};

} // namespace

scenes::bind::LearnSink& learn_sink() {
	static HostLearnSink sink;
	return sink;
}

} // namespace deluge::gui::shell_host
