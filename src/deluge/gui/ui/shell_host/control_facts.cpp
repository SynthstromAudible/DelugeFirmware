#include "gui/ui/shell_host/shell_host.h"

#include "definitions_cxx.hpp"
#include "gui/views/view.h"
#include "instruments/classic/engine_params.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "ui/controls/facts.h"
#include "ui/scenes/bind/knob_assignment.h"
#include "ui/scenes/bind/ports/param_info.h"

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace deluge::gui::shell_host {
namespace {

/// The knobs' reads, answered from the running app: params' live values, stutter, note tails, the playing outputs and
/// the params' rows.
class HostControlFacts final : public deluge::ui::controls::Facts {
public:
	[[nodiscard]] bool allows_note_tails(deluge::project::SlotAddress slot,
	                                     std::optional<deluge::project::ClipAddress> clip) const override;
	[[nodiscard]] std::optional<deluge::project::ParamAddress>
	playing_address(const deluge::project::ParamAddress& address) const override;
	[[nodiscard]] std::optional<int32_t> param_value(const deluge::project::ParamAddress& address) const override;
	[[nodiscard]] bool param_automated(const deluge::project::ParamAddress& address) const override;
	[[nodiscard]] std::optional<int32_t> clip_param_value(const deluge::project::ParamAddress& address,
	                                                      std::optional<uint32_t> pos) const override;
	[[nodiscard]] std::optional<int32_t> param_value_at(const deluge::project::ParamAddress& address,
	                                                    uint32_t pos) const override {
		return currentSong != nullptr ? deluge::bridge::paramValueAt(*currentSong, address, pos) : std::nullopt;
	}
	[[nodiscard]] bool stutter_active() const override {
		return currentSong != nullptr && currentSong->live.stutter.active();
	}
	[[nodiscard]] bool alt_delay() const override {
		return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
	}
	[[nodiscard]] std::span<const deluge::ui::controls::PlayingOutput> playing_outputs() const override;
	[[nodiscard]] std::span<const deluge::ui::controls::KitRow> sound_rows(uint32_t clip_id) const override;
	[[nodiscard]] const plugin::ParamInfo* param_info(plugin::PluginId plugin,
	                                                  const project::ParamTarget& target) const override {
		return deluge::ui::scenes::bind::deluge_param_info(plugin, target);
	}
	[[nodiscard]] deluge::ui::controls::TakeoverInputs takeover_inputs(bool cable_relative) const override {
		return {.mode = static_cast<deluge::ui::controls::TakeoverMode>(midiEngine.midiTakeover),
		        .cable_relative = cable_relative,
		        .recording = playbackHandler.isEitherClockActive() && playbackHandler.recording != RecordingMode::OFF};
	}
	[[nodiscard]] std::optional<std::pair<int32_t, int32_t>>
	step_region(std::optional<uint32_t> clip_id) const override {
		const ::Clip* clip = clip_id && currentSong != nullptr ? currentSong->clipForId(*clip_id) : nullptr;
		if (clip == nullptr || !view.stepEditRegionHolds(*clip)) {
			return std::nullopt;
		}
		return std::pair{static_cast<int32_t>(view.modPos()), static_cast<int32_t>(view.modLength())};
	}

private:
	/// What playing_outputs() and sound_rows() answer, rebuilt by each call.
	mutable std::vector<deluge::ui::controls::PlayingOutput> outputs_;
	mutable std::vector<deluge::ui::controls::KitRow> rows_;
};

bool HostControlFacts::allows_note_tails(deluge::project::SlotAddress slot,
                                         std::optional<deluge::project::ClipAddress> clip) const {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	if (sound == nullptr) {
		return true;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (slot.kind == deluge::project::SlotAddress::Kind::DRUM) {
		auto* drum = static_cast<SoundDrum*>(sound);
		ModelStackWithSoundFlags* stack = getModelStackFromSoundDrum(memory, drum)->addSoundFlags();
		return drum->allowNoteTails(stack, /*disregardSampleLoop=*/true);
	}
	// A synth's tails read the envelopes in a clip's params: the turn's, as `View::modEncoderAction` asks, or else the
	// clip its output plays.
	if (!clip) {
		auto* instrument = static_cast<::SoundInstrument*>(sound);
		const ::Clip* active = currentSong->getClipWithOutput(instrument, true);
		if (active == nullptr) {
			active = currentSong->getClipWithOutput(instrument);
		}
		if (active == nullptr) {
			return true;
		}
		clip = deluge::project::ClipAddress{active->id()};
	}
	const deluge::ui::scenes::bind::KnobFocus focus{
	    .owner = deluge::ui::scenes::bind::SoundFocus{.slot = slot, .clip = clip}};
	ModelStackWithThreeMainThings* owner = deluge::bridge::knobOwnerStackAt(*currentSong, focus, memory);
	return owner == nullptr || sound->allowNoteTails(owner->addSoundFlags());
}

std::optional<deluge::project::ParamAddress>
HostControlFacts::playing_address(const deluge::project::ParamAddress& address) const {
	using deluge::project::OwnerId;
	using deluge::project::SlotAddress;
	if (currentSong == nullptr || address.space != deluge::project::ParamSpace::ENGINE
	    || (address.owner.kind != OwnerId::Kind::INSTRUMENT && address.owner.kind != OwnerId::Kind::DRUM)) {
		return address;
	}
	const SlotAddress slot{address.owner.kind == OwnerId::Kind::DRUM ? SlotAddress::Kind::DRUM
	                                                                 : SlotAddress::Kind::OUTPUT,
	                       address.owner.id};
	const ::Sound* sound = deluge::bridge::soundAt(*currentSong, slot);
	if (sound == nullptr) {
		return address; // a kit's or an audio track's own effects: no engine
	}
	const std::optional<plugin::ParamId> playing =
	    deluge::instruments::classic::playing_target(sound->getSynthMode(), address.id);
	if (!playing) {
		return std::nullopt;
	}
	deluge::project::ParamAddress out = address;
	out.id = *playing;
	return out;
}

std::optional<int32_t> HostControlFacts::param_value(const deluge::project::ParamAddress& address) const {
	if (currentSong == nullptr) {
		return std::nullopt;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackFollowingTake(*currentSong, address, memory);
	// A param the owner does not hold yet (a cable not made) comes back as a stack with no lane.
	return stack != nullptr && stack->autoParam != nullptr ? std::optional{stack->autoParam->getCurrentValue()}
	                                                       : std::nullopt;
}

bool HostControlFacts::param_automated(const deluge::project::ParamAddress& address) const {
	if (currentSong == nullptr) {
		return false;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*currentSong, address, memory);
	return stack != nullptr && stack->autoParam != nullptr && stack->autoParam->isAutomated(*stack->doc());
}

std::optional<int32_t> HostControlFacts::clip_param_value(const deluge::project::ParamAddress& address,
                                                          std::optional<uint32_t> pos) const {
	if (currentSong == nullptr) {
		return std::nullopt;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*currentSong, address, memory);
	if (stack == nullptr || stack->autoParam == nullptr) {
		return std::nullopt;
	}
	return pos ? stack->autoParam->getValuePossiblyAtPos(static_cast<int32_t>(*pos), stack)
	           : stack->autoParam->getCurrentValue();
}

std::span<const deluge::ui::controls::PlayingOutput> HostControlFacts::playing_outputs() const {
	using Kind = deluge::ui::controls::PlayingOutput::Kind;
	outputs_.clear();
	if (currentSong == nullptr) {
		return outputs_;
	}
	for (const ::Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
		const ::Clip* active = output->getActiveClip();
		if (active == nullptr || !output->id()) {
			continue;
		}
		Kind kind = Kind::OTHER;
		deluge::project::OwnerId param_owner = deluge::project::OwnerId::instrument(*output->id());
		switch (output->type) {
		case OutputType::SYNTH:
			kind = Kind::SYNTH;
			break;
		case OutputType::KIT:
			kind = Kind::KIT;
			break;
		case OutputType::AUDIO:
			kind = Kind::AUDIO;
			param_owner = deluge::project::OwnerId::clip(active->id());
			break;
		default:
			break;
		}
		outputs_.push_back({.owner = {deluge::project::ControlsAddress::Kind::OUTPUT, *output->id()},
		                    .param_owner = param_owner,
		                    .kind = kind,
		                    .clip_id = active->id()});
	}
	return outputs_;
}

std::span<const deluge::ui::controls::KitRow> HostControlFacts::sound_rows(uint32_t clip_id) const {
	rows_.clear();
	const ::Clip* found = currentSong != nullptr ? currentSong->clipForId(clip_id) : nullptr;
	if (found == nullptr || found->type != ClipType::INSTRUMENT) {
		return rows_;
	}
	for (const ::NoteRow& row : static_cast<const ::InstrumentClip*>(found)->noteRows) {
		const ::Drum* drum = row.drum;
		if (drum != nullptr && drum->type == DrumType::SOUND && drum->id()) {
			rows_.push_back({.drum_id = *drum->id(), .param_owner = deluge::project::OwnerId::drum(*drum->id())});
		}
	}
	return rows_;
}

} // namespace

const deluge::ui::controls::Facts& control_facts() {
	static const HostControlFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
