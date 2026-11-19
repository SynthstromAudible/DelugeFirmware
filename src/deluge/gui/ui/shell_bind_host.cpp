#include "gui/ui/shell_bind_host.h"
#include "board_controls.hpp"
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_slice_ops.h"
#include "gui/ui/ui.h"
#include "hid/led/pad_leds.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/voice/sound_voice.h"
#include "model/voice/voice_sample.h"

#include "controls/learned.h"
#include "deluge-project/setting_group.h"
#include "effects/plugin.h"
#include "effects/settings.h"
#include "gui/ui/browser/dx_cartridge_bank.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/output_replacement.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/knob_focus_from_stack.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/screensaver.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/words.h"
#include "io/debug/log.h"
#include "io/midi/follow_host.h"
#include "io/midi/learned_dispatch.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_engine.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/drum/drum.h"
#include "model/edits/attach_recorded_sample.h"
#include "model/edits/clear_clip.h"
#include "model/edits/delete_param_automation.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/edits/resize_audio_clip.h"
#include "model/edits/set_cable_polarity.h"
#include "model/edits/set_chain_view.h"
#include "model/edits/set_clip_colour.h"
#include "model/edits/set_controls.h"
#include "model/edits/set_effect_setting.h"
#include "model/edits/set_engine_setting.h"
#include "model/edits/set_kit_lane_setting.h"
#include "model/edits/set_kit_param_value.h"
#include "model/edits/set_kit_setting.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/edits/set_param_value.h"
#include "model/edits/set_patch_cable.h"
#include "model/edits/set_setting.h"
#include "model/edits/set_source_ranges.h"
#include "model/edits/set_velocity_to_level.h"
#include "model/edits/setting_edit.h"
#include "model/favourite/favourite_manager.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/setting_projection.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/last_file_path_loaded.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "plugin-api/types/setting.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/knob_assignment.h"
#include "scenes/menu/bind_host.h"
#include "storage/DX7Cartridge.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "util/containers.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <variant>

// The input a new audio track starts on: unsaved song state (`model/song/song.cpp`), set by the audio-source menu.
extern AudioInputChannel defaultAudioOutputInputChannel;
namespace deluge::gui {

namespace {

using deluge::scenes::bind::Addresses;
using deluge::scenes::bind::WriteRequest;

/// The file form of stored value @p stored of @p row: a CHOICE row's word, any other row's number.
std::optional<deluge::serialization::Value> value_of(const deluge::plugin::SettingInfo& row, int32_t stored) {
	if (row.kind == deluge::plugin::SettingKind::CHOICE) {
		std::array<char, 1> unused{};
		const std::optional<std::string_view> word = deluge::plugin::stored_text(row, stored, unused);
		return word ? std::optional{deluge::serialization::Value::makeScalar(std::string(*word))} : std::nullopt;
	}
	return deluge::serialization::Value::makeNumber(stored);
}

/// Whether writing stored value @p stored of @p row through @p target ends a kit row's notes: a sample played once
/// (`Repeat::writeCurrentValue` in `origin/main:src/deluge/gui/menu_item/sample/repeat.h`: its whole-kit arm and its
/// one-sound arm), whose notes the kit's arpeggiator would otherwise hold.
bool silences_kit_arp(const deluge::scenes::bind::Target& target, const deluge::plugin::SettingInfo& row,
                      int32_t stored) {
	if (target.kind != deluge::scenes::bind::Target::Kind::SOUND || !row.path.ends_with("/loopMode")) {
		return false;
	}
	return stored
	       == std::to_underlying(deluge::instrument::SampleRepeatMode::ONCE); // a loop mode is saved as its index
}

/// Whether @p kit has a sound drum: legacy's loop-mode note-off is sent from inside its loop over them.
bool has_sound_drum(const Kit& kit) {
	for (const Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type == DrumType::SOUND) {
			return true;
		}
	}
	return false;
}

/// Sends @p kit's selected drum's note-off through the kit's arpeggiator, as
/// `origin/main:src/deluge/gui/menu_item/sample/repeat.h`'s sendNoteOffForKitArpeggiator does.
void kit_arp_note_off(Kit* kit) {
	if (kit == nullptr || kit->selectedDrum == nullptr) {
		return;
	}
	// The clip the kit plays, else the one the menu was opened over, as legacy's current clip.
	Clip* clip = kit->getActiveClip();
	if (clip == nullptr && currentSong->getCurrentClip() != nullptr && currentSong->getCurrentClip()->output == kit) {
		clip = currentSong->getCurrentClip();
	}
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		return;
	}
	int32_t note_row_index = 0;
	NoteRow* note_row = static_cast<InstrumentClip*>(clip)->getNoteRowForDrum(kit->selectedDrum, &note_row_index);
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = reinterpret_cast<ModelStack*>(memory);
	ModControllable* mod = kit->selectedDrum->toModControllable();
	ParamManager* params = note_row != nullptr ? &note_row->paramManager : nullptr;
	kit->noteOffPreKitArp(
	    stack->addTimelineCounter(clip)->addNoteRow(note_row_index, note_row)->addOtherTwoThings(mod, params),
	    kit->selectedDrum);
}

/// Applies a menu's write requests as their targets' Edits on the current song. A sound target's request becomes
/// SetEngineSetting on the sound's slot, and its partner row's value a second one; with affect-entire held on a kit,
/// one SetKitSetting over the kit's sound drums the target's fan-out rule picks, read in each drum's own context, as
/// legacy's loop picks them (and its partner a second); a drum that refuses the value is left as it was, as legacy's
/// loop writes it harmlessly. A sample's loop mode set to play once ends the kit arpeggiator's note first. An
/// effect target's request is the same on a sound's slot, and on a host chain (the whole kit's, an audio track's, the
/// master's) SetEffectSetting on the chain, its partner second. A request whose slot or chain does not resolve, or a
/// sound with no slot, is refused: logged and counted, never applied through the runtime's setter alone.
class ShellEditSink final : public deluge::scenes::bind::WriteSink, public deluge::controls::Sink {
public:
	bool write(const WriteRequest& request) override;
	bool write_param(const deluge::project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                 deluge::controls::Resolution resolution) override;
	void learned_written(const deluge::project::ParamAddress& address, int32_t knob_pos) override;
	std::optional<uint32_t> take_for(uint32_t output_id) override {
		return currentSong != nullptr ? deluge::bridge::takeFor(*currentSong, output_id) : std::nullopt;
	}
	bool make_param(const deluge::project::ParamAddress& address) override;
	void knob_absolute(const deluge::project::OwnerParamTarget& target,
	                   const deluge::project::MidiCommand& source) override {
		if (currentSong != nullptr) {
			deluge::bridge::mappingNoLongerRelative(target, source, *currentSong);
		}
	}
	bool delete_automation(const deluge::project::ParamAddress& address) override;
	bool write_text(const deluge::scenes::bind::Target& target, const deluge::scenes::bind::Addresses& where,
	                std::string_view text) override;
	bool write_ranges(deluge::project::SlotAddress slot, uint8_t source,
	                  std::span<const deluge::instrument::RangeState> ranges,
	                  deluge::scenes::bind::RangesCause cause = {}) override;
	bool recolour_clip(deluge::project::ClipAddress clip, int32_t offset) override;
	bool clear_clip(deluge::project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) override;
	bool move_audio_clip_marker(deluge::project::ClipAddress clip, std::string_view path, int64_t value) override;
	void set_default_audio_input_channel(int32_t channel) override {
		defaultAudioOutputInputChannel = static_cast<AudioInputChannel>(channel);
	}
	void set_default_range(deluge::project::SlotAddress slot, uint8_t source, uint16_t range) override {
		::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
		if (sound != nullptr && source < kNumSources) {
			sound->sources[source].defaultRangeI = range;
		}
	}
	bool attach_recording(deluge::project::SlotAddress slot, uint8_t source, uint16_t range,
	                      std::string_view path) override;
	bool write_dx_patch(deluge::project::SlotAddress slot, uint8_t source,
	                    const deluge::instrument::DxPatchState& patch) override;
	bool load_dx_program(deluge::project::SlotAddress slot, uint8_t source, int32_t program) override;
	bool insert_range(deluge::project::SlotAddress slot, uint8_t source, uint16_t at, int16_t top_note,
	                  std::optional<int16_t> previous_top) override;
	bool delete_range(deluge::project::SlotAddress slot, uint8_t source, uint16_t index,
	                  std::optional<int16_t> lower_top) override;
	bool resize_audio_clip(deluge::project::ClipAddress clip, deluge::scenes::bind::ResizeKind kind, int32_t new_length,
	                       int32_t old_length) override;
	void set_undo_scroll_after(deluge::project::ClipAddress clip, int32_t x_scroll) override;
	bool learn(deluge::project::MappingTarget target, std::optional<deluge::project::MidiCommand> command,
	           std::optional<deluge::scenes::bind::LearnInput> input) override;
	bool learn_knob(const deluge::project::OwnerParamTarget& target, ::MIDICable* cable, int32_t which_knob,
	                int32_t midi_channel) override;
	bool write_source_transpose(const deluge::scenes::bind::Addresses& where, uint8_t source,
	                            std::optional<uint16_t> range, int32_t menu_value) override;
	bool knob_mode(deluge::project::KnobModeOwner owner, uint8_t mode) override {
		return deluge::edits::modKnobModeFor(owner, mode);
	}
	bool chain_view(deluge::project::ChainAddress chain, std::string_view key, std::string_view word) override;
	bool write_controls(deluge::project::ControlsAddress owner,
	                    std::vector<deluge::project::ControllerMapping> controls) override;
	bool stutter(const deluge::scenes::bind::KnobFocus& focus, bool begin) override;
	bool grab_region(const deluge::scenes::bind::KnobFocus& focus, uint32_t pos) override;
	bool write_effect(const deluge::scenes::bind::EffectOwner& owner, std::string_view path,
	                  const deluge::serialization::Value& value) override;
	bool midi_cc_select(const deluge::scenes::bind::KnobFocus& focus, int32_t knob, bool on) override;
	bool delete_knob_automation(const deluge::project::ParamAddress& address) override {
		deluge::edits::DeleteParamAutomation edit{address, ActionAddition::NOT_ALLOWED,
		                                          deluge::edits::SetParamValue::Resolve::WRITE};
		return currentSong != nullptr && applied(edit);
	}
	bool instrument_edited(const deluge::scenes::bind::KnobFocus& focus) override;
	bool velocity_to_level_everywhere(const ::MIDICable& cable) override {
		deluge::edits::SetVelocityToLevelEverywhere edit{cable};
		return currentSong != nullptr && applied(edit);
	}
	[[nodiscard]] uint32_t refusals() const { return refusals_; }

private:
	/// Applies @p request, counting a refusal.
	void apply(const WriteRequest& request);
	/// A PARAM target's write: SetParamValue by its address, or SetKitParamValue under affect-entire on a kit row; then
	/// MIDI follow's feedback.
	void writeParam(const WriteRequest& request);
	/// A CABLE target's write: SetPatchCable by its address, or, polarity only, SetCablePolarity.
	void writeCable(const WriteRequest& request);
	/// Applies @p edit on the current song, counting a refusal.
	bool applied(deluge::edits::Edit& edit);

	uint32_t refusals_{0};
	/// The undo step the last length change opened, and its clip: what a redo of it scrolls the view back to.
	::Action* resize_action_{nullptr};
	deluge::project::ClipAddress resize_clip_{};
};

/// The runtime facts, answered from the running app: the audio engine's inputs, each sound's sources and typed
/// state, found by its slot in the current song, and each host chain's effects, found by its address.
class ShellRuntimeFacts final : public deluge::scenes::bind::RuntimeFacts {
public:
	[[nodiscard]] bool audio_inputs_present() const override;
	[[nodiscard]] int32_t live_voice_count() const override;
	[[nodiscard]] bool kit_takes_whole_folder(deluge::project::SlotAddress slot) const override;
	[[nodiscard]] bool sample_load_in_flight() const override {
		return deluge::gui::sampleClaim.explicitInFlight() || deluge::gui::folderImport.inFlight()
		       || deluge::gui::ui::shellSlicerBusy();
	}
	[[nodiscard]] bool output_may_be_replaced() const override { return deluge::gui::anOutputMayBeReplaced(); }
	[[nodiscard]] std::optional<uint16_t> take_range(const deluge::scenes::bind::TakeTarget& target) const override;
	[[nodiscard]] bool auditioning() const override { return isUIModeActive(UI_MODE_AUDITIONING); }
	[[nodiscard]] deluge::scenes::bind::FlashCursor flash_cursor() const override {
		return static_cast<deluge::scenes::bind::FlashCursor>(PadLEDs::flashCursor);
	}
	[[nodiscard]] deluge::scenes::bind::PerformanceEditing performance_editing_mode() const override {
		// `EditingMode::readCurrentValue`: `defaultEditingMode` first, then `editingParam`.
		using deluge::scenes::bind::PerformanceEditing;
		if (!performanceView.defaultEditingMode) {
			return PerformanceEditing::Disabled;
		}
		return performanceView.editingParam ? PerformanceEditing::Param : PerformanceEditing::Value;
	}
	[[nodiscard]] bool popup_showing() const override {
		// `display->hasPopup()` for the guest: its popups and notifications are scenes on its stack.
		for (const deluge::ui::shell::Scene* scene : shellHostUI.shell().stack().scenes()) {
			if (scene->kind() == deluge::ui::shell::SceneKind::Popup
			    || scene->kind() == deluge::ui::shell::SceneKind::Notification) {
				return true;
			}
		}
		return false;
	}
	[[nodiscard]] std::optional<deluge::scenes::bind::IsomorphicKeyboard> isomorphic_keyboard() const override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (getRootUI() != &keyboardScreen || clip == nullptr
		    || clip->keyboardState.currentLayout != KeyboardLayoutType::KeyboardLayoutTypeIsomorphic) {
			return std::nullopt;
		}
		return deluge::scenes::bind::IsomorphicKeyboard{.scroll_offset = clip->keyboardState.isomorphic.scrollOffset,
		                                                .row_interval = clip->keyboardState.isomorphic.rowInterval};
	}
	[[nodiscard]] bool dx7_engine_enabled() const override {
		return runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableDX7Engine) == RuntimeFeatureStateToggle::On;
	}
	[[nodiscard]] std::optional<deluge::scenes::bind::DxCartridgeNames> dx_cartridge() const override {
		DX7Cartridge* bank = deluge::gui::dxCartridgeBank();
		if (bank == nullptr) {
			return std::nullopt;
		}
		deluge::scenes::bind::DxCartridgeNames out;
		char names[32][11];
		bank->getProgramNames(names);
		out.count = bank->numPatches();
		for (int32_t i = 0; i < out.count; i++) {
			std::copy_n(names[i], 11, out.names[i].begin());
		}
		return out;
	}
	[[nodiscard]] std::optional<uint16_t> default_range(deluge::project::SlotAddress slot,
	                                                    uint8_t source) const override {
		const ::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
		if (sound == nullptr || source >= kNumSources) {
			return std::nullopt;
		}
		const int32_t i = sound->sources[source].defaultRangeI;
		if (i < 0 || i >= std::ssize(sound->sources[source].ranges)) {
			return std::nullopt;
		}
		return static_cast<uint16_t>(i);
	}
	[[nodiscard]] std::optional<uint64_t> range_identity(deluge::project::SlotAddress slot, uint8_t source,
	                                                     uint16_t range) const override {
		const ::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
		if (sound == nullptr || source >= kNumSources || range >= std::ssize(sound->sources[source].ranges)) {
			return std::nullopt;
		}
		return sound->sources[source].ranges.getElement(range)->identity();
	}
	[[nodiscard]] uint8_t loaded_sources(deluge::project::SlotAddress slot) const override;
	[[nodiscard]] const deluge::instrument::SoundState* sound_state(deluge::project::SlotAddress slot) const override;
	[[nodiscard]] const deluge::effects::EffectStates*
	chain_effects(deluge::project::ChainAddress chain) const override;
	[[nodiscard]] int32_t played_bend_range(const deluge::project::SettingAddress& owner, int32_t which) const override;
	[[nodiscard]] bool allows_note_tails(deluge::project::SlotAddress slot,
	                                     std::optional<deluge::project::ClipAddress> clip) const override;
	[[nodiscard]] uint8_t arp_preset(const deluge::project::SettingAddress& address) const override;
	[[nodiscard]] std::optional<int32_t> param_value(const deluge::project::ParamAddress& address) const override;
	[[nodiscard]] bool param_automated(const deluge::project::ParamAddress& address) const override;
	[[nodiscard]] std::optional<int32_t> clip_param_value(const deluge::project::ParamAddress& address,
	                                                      std::optional<uint32_t> pos) const override;
	[[nodiscard]] std::optional<int32_t> param_value_at(const deluge::project::ParamAddress& address,
	                                                    uint32_t pos) const override {
		return currentSong != nullptr ? deluge::bridge::paramValueAt(*currentSong, address, pos) : std::nullopt;
	}
	[[nodiscard]] deluge::scenes::bind::CableList cables(const deluge::project::ParamAddress& owner) const override;
	[[nodiscard]] std::vector<deluge::scenes::bind::TrackFact> tracks() const override;
	[[nodiscard]] std::optional<deluge::scenes::bind::AudioClipFact>
	audio_clip(deluge::project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_playing(deluge::project::ClipAddress clip) const override;
	[[nodiscard]] int32_t clip_live_pos(deluge::project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_recording_linearly(deluge::project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_arrangement_only(deluge::project::ClipAddress clip) const override {
		const ::Clip* found = currentSong != nullptr ? currentSong->clipForId(clip.clip_id) : nullptr;
		return found != nullptr && found->isArrangementOnlyClip();
	}
	[[nodiscard]] bool stutter_active() const override {
		return currentSong != nullptr && currentSong->live.stutter.active();
	}
	[[nodiscard]] bool alt_delay() const override {
		return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
	}
	[[nodiscard]] std::span<const deluge::controls::PlayingOutput> playing_outputs() const override;
	[[nodiscard]] std::span<const deluge::controls::KitRow> sound_rows(uint32_t clip_id) const override;
	[[nodiscard]] deluge::controls::TakeoverInputs takeover_inputs(bool cable_relative) const override {
		return {.mode = static_cast<deluge::controls::TakeoverMode>(midiEngine.midiTakeover),
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
	[[nodiscard]] std::pair<int64_t, int64_t> audio_clip_samples(deluge::project::ClipAddress clip, int32_t scroll,
	                                                             int32_t zoom) const override;

private:
	/// What playing_outputs() and sound_rows() answer, rebuilt by each call.
	mutable std::vector<deluge::controls::PlayingOutput> outputs_;
	mutable std::vector<deluge::controls::KitRow> rows_;
};

/// The audio clip @p clip addresses in the current song, or null.
::AudioClip* audioClipAt(deluge::project::ClipAddress clip) {
	::Clip* found = currentSong != nullptr ? currentSong->clipForId(clip.clip_id) : nullptr;
	return found != nullptr && found->type == ClipType::AUDIO ? static_cast<::AudioClip*>(found) : nullptr;
}

/// Whether @p clip plays the sound @p slot names: a synth clip its own track, a kit clip one of its kit's sound rows. A
/// replaced synth kept hibernating under its id is not one: no clip plays it.
bool clipPlaysSlot(const ::Clip& clip, deluge::project::SlotAddress slot) {
	using Kind = deluge::project::SlotAddress::Kind;
	if (clip.output == nullptr) {
		return false;
	}
	if (slot.kind == Kind::OUTPUT) {
		return clip.output->type == OutputType::SYNTH && clip.output->id() == slot.id;
	}
	if (clip.output->type != OutputType::KIT) {
		return false;
	}
	for (const Drum* drum = static_cast<const Kit*>(clip.output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type == DrumType::SOUND && drum->id() == slot.id) {
			return true;
		}
	}
	return false;
}

std::optional<uint16_t> ShellRuntimeFacts::take_range(const deluge::scenes::bind::TakeTarget& target) const {
	// `AudioRecorder::targetStillHeld`, by identity: the clip and what it plays, then the range wherever it now sits.
	// Range identities are run-unique and never saved, so a range in another song cannot match.
	if (currentSong == nullptr || !target.clip || !target.range || target.source >= kNumSources) {
		return std::nullopt;
	}
	::Clip* clip = currentSong->clipForId(target.clip->clip_id);
	if (clip == nullptr || !clipPlaysSlot(*clip, target.slot)) {
		return std::nullopt;
	}
	const ::Sound* sound = deluge::bridge::soundAt(*currentSong, target.slot);
	if (sound == nullptr) {
		return std::nullopt;
	}
	const MultiRangeArray& ranges = sound->sources[target.source].ranges;
	for (int32_t i = 0; i < std::ssize(ranges); i++) {
		if (ranges.getElement(i)->identity() == *target.range) {
			return static_cast<uint16_t>(i);
		}
	}
	return std::nullopt;
}

static_assert(kMaxNumPatchCables <= deluge::scenes::bind::kMaxCables);

/// The waveforms of the current song: the sample a sound's source range holds, an audio clip's (its recorder's take
/// while it records linearly, as legacy's audio clip view draws it), or the preview sound's, which main's
/// `SampleBrowser::previewIfPossible` (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) draws from its
/// source 0, range 0.
class ShellWaveforms final : public deluge::scenes::services::Waveforms {
public:
	[[nodiscard]] std::optional<deluge::scenes::services::WaveformInfo>
	info(const deluge::scenes::services::WaveformAddress& address) const override {
		const Resolved at = resolve(address);
		if (at.sample == nullptr) {
			return std::nullopt;
		}
		return deluge::scenes::services::WaveformInfo{.length_samples = at.sample->lengthInSamples,
		                                              .sample_rate = at.sample->sampleRate,
		                                              .channels = at.sample->numChannels,
		                                              .max_peak_from_zero = at.sample->getMaxPeakFromZero(),
		                                              .centre = at.sample->getFoundValueCentrePoint(),
		                                              .span = at.sample->getValueSpan(),
		                                              .recording = at.recorder != nullptr,
		                                              .identity = reinterpret_cast<uintptr_t>(at.sample)};
	}
	bool peaks(const deluge::scenes::services::WaveformAddress& address, int64_t scroll, uint64_t zoom,
	           WaveformRenderData& cache, int32_t x_start, int32_t x_end) const override {
		const Resolved at = resolve(address);
		return at.sample != nullptr
		       && deluge::sample::find_peaks_per_col(at.sample, scroll, zoom, &cache, at.recorder, x_start, x_end);
	}
	[[nodiscard]] std::optional<int64_t>
	play_position(const deluge::scenes::services::WaveformAddress& address) const override {
		// `SampleMarkerEditor::graphicsRoutine`'s walk: an instrument clip's newest voice (`orderSounded`) on the
		// range's holder, its middle unison part; an audio clip's own voice sample.
		using Kind = deluge::scenes::services::WaveformAddress::Kind;
		const Resolved at = resolve(address);
		if (at.sample == nullptr || currentSong == nullptr) {
			return std::nullopt;
		}
		VoiceSample* voiceSample = nullptr;
		SamplePlaybackGuide* guide = nullptr;
		if (address.kind == Kind::AUDIO_CLIP) {
			::AudioClip* clip = audioClipAt(address.clip);
			if (clip == nullptr) {
				return std::nullopt;
			}
			voiceSample = clip->voiceSample;
			guide = &clip->guide;
		}
		else if (address.kind == Kind::RANGE) {
			::Sound* sound = deluge::bridge::soundAt(*currentSong, address.slot);
			if (sound == nullptr || !sound->hasActiveVoices()) {
				return std::nullopt;
			}
			auto* range =
			    static_cast<MultisampleRange*>(sound->sources[address.source].ranges.getElement(address.range));
			SoundVoice* newest = nullptr;
			for (const Sound::ActiveVoice& voice : sound->voices()) {
				if (voice->guides[address.source].audioFileHolder == range->getAudioFileHolder()
				    && (newest == nullptr || voice->order_sounded > newest->order_sounded)) {
					newest = &*voice;
				}
			}
			if (newest == nullptr) {
				return std::nullopt;
			}
			const int32_t u = sound->snapshot().num_unison >> 1;
			if (!newest->kernel.unison_parts[u].slots[address.source].active) {
				return std::nullopt;
			}
			voiceSample = newest->ports(u, address.source).sample.voiceSample();
			guide = &newest->guides[address.source];
		}
		if (voiceSample == nullptr) {
			return std::nullopt;
		}
		return voiceSample->getPlaySample(at.sample, guide);
	}

private:
	struct Resolved {
		::Sample* sample{nullptr};
		::SampleRecorder* recorder{nullptr};
	};
	static Resolved resolve(const deluge::scenes::services::WaveformAddress& address) {
		using Kind = deluge::scenes::services::WaveformAddress::Kind;
		if (address.kind == Kind::PREVIEW) {
			if (AudioEngine::sampleForPreview == nullptr) {
				return {};
			}
			::Source& source = AudioEngine::sampleForPreview->sources[0];
			if (source.ranges.size() < 1 || source.ranges.currentElementSize() != sizeof(MultisampleRange)) {
				return {};
			}
			return {static_cast<::Sample*>(
			    static_cast<MultisampleRange*>(source.ranges.getElement(0))->sampleHolder.audioFile)};
		}
		if (currentSong == nullptr) {
			return {};
		}
		if (address.kind == Kind::AUDIO_CLIP) {
			::Clip* clip = currentSong->clipForId(address.clip.clip_id);
			if (clip == nullptr || clip->type != ClipType::AUDIO) {
				return {};
			}
			auto* audio = static_cast<::AudioClip*>(clip);
			if (audio->getCurrentlyRecordingLinearly()) {
				return audio->recorder != nullptr ? Resolved{audio->recorder->sample, audio->recorder} : Resolved{};
			}
			return {static_cast<::Sample*>(audio->sampleHolder.audioFile)};
		}
		::Sound* sound = deluge::bridge::soundAt(*currentSong, address.slot);
		if (sound == nullptr || address.source >= kNumSources) {
			return {};
		}
		::Source& source = sound->sources[address.source];
		if (source.ranges.size() <= address.range || source.ranges.currentElementSize() != sizeof(MultisampleRange)) {
			return {};
		}
		auto* range = static_cast<MultisampleRange*>(source.ranges.getElement(address.range));
		return {static_cast<::Sample*>(range->sampleHolder.audioFile)};
	}
};

ShellEditSink& shell_edit_sink() {
	static ShellEditSink sink;
	return sink;
}

} // namespace

void ShellEditSink::writeParam(const WriteRequest& request) {
	const std::optional<deluge::project::ParamAddress> address =
	    deluge::scenes::bind::param_address(request.target, request.where);
	if (!address) {
		refusals_++;
		D_PRINTLN("menu write refused: param %u has no owner", static_cast<unsigned>(request.target.param.value));
		return;
	}
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	Error error = Error::NONE;
	const deluge::scenes::bind::Addresses& where = request.where;
	if (where.affect_entire && where.kit_output && request.target.whole_kit && where.clip && where.slot
	    && where.slot->kind == deluge::project::SlotAddress::Kind::DRUM) {
		// Legacy's affect-entire loop: every sound drum of the kit with a row in the clip, as one Edit.
		deluge::edits::SetKitParamValue edit{*where.kit_output, where.clip->clip_id, address->id, request.param_value};
		error = deluge::edits::apply(ctx, edit);
	}
	else {
		deluge::edits::SetParamValue edit{*address, request.param_value};
		error = deluge::edits::apply(ctx, edit);
	}
	if (error != Error::NONE) {
		refusals_++;
		D_PRINTLN("menu write refused: param %u", static_cast<unsigned>(address->id.value));
		return;
	}
	// MIDI follow's feedback for the param written, as legacy's param items send it after every write.
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*currentSong, *address, memory)) {
		view.sendMidiFollowFeedback(stack, stack->paramCollection->paramValueToKnobPos(request.param_value, stack));
	}
}

void ShellEditSink::writeCable(const WriteRequest& request) {
	const std::optional<deluge::project::ParamAddress> address =
	    deluge::scenes::bind::param_address(request.target, request.where);
	if (!address) {
		refusals_++;
		D_PRINTLN("menu write refused: cable %x has no owner", static_cast<unsigned>(request.target.param.value));
		return;
	}
	const auto polarity = static_cast<::Polarity>(request.polarity);
	if (request.polarity_only) {
		deluge::edits::SetCablePolarity edit{*address, polarity};
		(void)applied(edit);
		return;
	}
	// No MIDI-follow feedback: legacy's strength page sends none, and no CC follows a cable.
	deluge::edits::SetPatchCable edit{*address, request.param_value, polarity};
	(void)applied(edit);
}

bool ShellEditSink::applied(deluge::edits::Edit& edit) {
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		refusals_++;
		return false;
	}
	return true;
}

bool ShellEditSink::write_param(const deluge::project::ParamAddress& address, int32_t value, int32_t pos,
                                int32_t length, deluge::controls::Resolution resolution) {
	if (currentSong == nullptr) {
		return false;
	}
	// What the knob turns is made and a take recording the clip is written; a gold knob's write also marks the
	// instrument edited, a learned knob's does not.
	using Resolve = deluge::edits::SetParamValue::Resolve;
	deluge::edits::SetParamValue edit{address, value, pos, length,
	                                  resolution == deluge::controls::Resolution::LEARNED ? Resolve::LEARNED
	                                                                                      : Resolve::WRITE};
	return applied(edit);
}

bool ShellEditSink::make_param(const deluge::project::ParamAddress& address) {
	if (currentSong == nullptr) {
		return false;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	// Only a cable's depth is made, as a learned knob that reads it makes it (resolved as a learned write resolves it,
	// which records the cable as an undo step); any other param is only looked for.
	if (address.space != deluge::project::ParamSpace::ENGINE || !deluge::instrument::is_cable_depth(address.id)) {
		const ModelStackWithAutoParam* found = deluge::bridge::paramStackFollowingTake(*currentSong, address, memory);
		return found != nullptr && found->autoParam != nullptr;
	}
	deluge::project::ParamAddress landed = address;
	const ModelStackWithAutoParam* stack = deluge::bridge::paramStackForWrite(*currentSong, landed, memory);
	return stack != nullptr && stack->autoParam != nullptr;
}

void ShellEditSink::learned_written(const deluge::project::ParamAddress& address, int32_t knob_pos) {
	RootUI* rootUI = getRootUI();
	const bool onAutomation = rootUI == &automationView;
	if (currentSong == nullptr || (!onAutomation && rootUI != &performanceView)) {
		return;
	}
	const bool ofSong = address.owner.kind == deluge::project::OwnerId::Kind::SONG;
	// A clip's knob refreshes only the automation editor, and only on the clip it shows.
	if (!ofSong && (!onAutomation || automationView.onArrangerView)) {
		return;
	}
	const std::optional<uint32_t> clip_id =
	    address.owner.kind == deluge::project::OwnerId::Kind::CLIP ? std::optional{address.owner.id} : address.clip_id;
	::Clip* clip = clip_id ? currentSong->clipForId(*clip_id) : nullptr;
	if (!ofSong && (clip == nullptr || clip != getCurrentClip())) {
		return;
	}
	const std::optional<deluge::bridge::ParamLane> lane = deluge::bridge::paramLaneFollowingTake(*currentSong, address);
	if (!lane) {
		return;
	}
	if (onAutomation) {
		automationView.possiblyRefreshAutomationEditorGrid(ofSong ? nullptr : clip, lane->kind, lane->id);
	}
	else {
		performanceView.possiblyRefreshPerformanceViewDisplay(lane->kind, lane->id, knob_pos);
	}
}

bool ShellEditSink::delete_automation(const deluge::project::ParamAddress& address) {
	if (currentSong == nullptr) {
		return false;
	}
	deluge::edits::DeleteParamAutomation edit{address};
	return applied(edit);
}

namespace {

/// Whether another of @p owner's siblings already has @p name: another clip of the clip's track, another audio track,
/// another drum of the lane's kit, as legacy's rename editors refuse a name (`RenameClipUI::trySetName` in
/// `rename_clip_ui.cpp`, `RenameOutputUI::trySetName` in `rename_output_ui.cpp`, main's `RenameDrumUI`,
/// `origin/main:src/deluge/gui/ui/rename/rename_drum_ui.cpp`). Letter case does not count.
bool name_taken(const deluge::project::SettingAddress& owner, std::string_view name) {
	if (const auto* clip_at = std::get_if<deluge::project::ClipAddress>(&owner.owner)) {
		::Clip* clip = currentSong->clipForId(clip_at->clip_id);
		::Clip* other = clip != nullptr && clip->output != nullptr ? clip->output->getClipFromName(name) : nullptr;
		return other != nullptr && other != clip;
	}
	if (const auto* track = std::get_if<deluge::project::TrackAddress>(&owner.owner)) {
		void* other = currentSong->getAudioOutputFromName(name);
		return other != nullptr && other != currentSong->outputForId(track->output_id);
	}
	if (const auto* lane = std::get_if<deluge::project::LaneAddress>(&owner.owner)) {
		auto* kit = static_cast<::Kit*>(currentSong->outputForId(lane->kit_id));
		::Drum* drum = deluge::bridge::drumWithId(*currentSong, lane->drum_id);
		::Drum* other = kit != nullptr && kit->type == OutputType::KIT ? kit->getDrumFromName(name) : nullptr;
		return other != nullptr && other != drum;
	}
	return false;
}

} // namespace

bool ShellEditSink::write_text(const deluge::scenes::bind::Target& target, const deluge::scenes::bind::Addresses& where,
                               std::string_view text) {
	if (currentSong == nullptr) {
		return false;
	}
	const std::optional<deluge::project::SettingAddress> address = deluge::scenes::bind::setting_address(target, where);
	if (target.kind != deluge::scenes::bind::Target::Kind::MODEL || !address) {
		refusals_++;
		return false;
	}
	if (target.path == "name" && name_taken(*address, text)) {
		refusals_++;
		return false;
	}
	deluge::edits::SetSetting edit{*address, std::string{target.path},
	                               deluge::serialization::Value::makeScalar(std::string{text})};
	return applied(edit);
}

bool ShellEditSink::write_ranges(deluge::project::SlotAddress slot, uint8_t source,
                                 std::span<const deluge::instrument::RangeState> ranges,
                                 deluge::scenes::bind::RangesCause cause) {
	if (currentSong == nullptr) {
		return false;
	}
	deluge::edits::SetSourceRanges edit{slot, source, {ranges.begin(), ranges.end()}};
	if (!applied(edit)) {
		return false;
	}
	if (cause.kind == deluge::scenes::bind::RangesCause::Kind::MARKER) {
		// A marker moved: the sounding voices follow the zone, and the instrument counts as edited.
		::Sound* sound = deluge::bridge::soundAt(*currentSong, slot);
		deluge::bridge::zoneMoved(*sound, source, static_cast<MarkerType>(cause.marker), *currentSong);
		Instrument* instrument = sound->isDrum() ? static_cast<Instrument*>(static_cast<SoundDrum*>(sound)->kit)
		                                         : static_cast<Instrument*>(static_cast<SoundInstrument*>(sound));
		if (instrument != nullptr) {
			instrument->beenEdited(true);
		}
	}
	return true;
}

bool ShellEditSink::write_dx_patch(deluge::project::SlotAddress slot, uint8_t source,
                                   const deluge::instrument::DxPatchState& patch) {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	if (sound == nullptr || source >= kNumSources || sound->sources[source].oscType != OscType::DX7) {
		return false;
	}
	// The `SetDxPatch` Edit legacy's DX7 pages write through (`DxParam::setValue`,
	// `origin/main:src/deluge/gui/menu_item/dx/param.cpp`), which records no undo.
	deluge::edits::dxPatchFor(*sound, source, patch);
	return true;
}

bool ShellEditSink::load_dx_program(deluge::project::SlotAddress slot, uint8_t source, int32_t program) {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	DX7Cartridge* bank = deluge::gui::dxCartridgeBank();
	if (sound == nullptr || bank == nullptr || source >= kNumSources || sound->sources[source].oscType != OscType::DX7
	    || program < 0 || program >= bank->numPatches()) {
		return false;
	}
	deluge::gui::loadDxProgram(*sound, source, *bank, program);
	return true;
}

bool ShellEditSink::insert_range(deluge::project::SlotAddress slot, uint8_t source, uint16_t at, int16_t top_note,
                                 std::optional<int16_t> previous_top) {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	if (sound == nullptr || source >= kNumSources) {
		return false;
	}
	// `MultiRange::selectEncoderAction`'s insert (`origin/main:src/deluge/gui/menu_item/multi_range.cpp`): range
	// storage is about to change, so every voice is unassigned and none may be assigned during the allocation.
	sound->killAllVoices();
	::MultiRange* inserted = nullptr;
	{
		AudioEngine::AudioTickHold hold;
		inserted = sound->sources[source].ranges.insertMultiRange(at);
	}
	if (inserted == nullptr) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return false;
	}
	inserted->topNote = top_note;
	if (previous_top && at > 0) {
		sound->sources[source].ranges.getElement(at - 1)->topNote = *previous_top;
	}
	deluge::edits::recordRanges(*sound, source);
	return true;
}

bool ShellEditSink::delete_range(deluge::project::SlotAddress slot, uint8_t source, uint16_t index,
                                 std::optional<int16_t> lower_top) {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	if (sound == nullptr || source >= kNumSources || index >= std::ssize(sound->sources[source].ranges)) {
		return false;
	}
	// `MultiRange::deletePress` (`origin/main:src/deluge/gui/menu_item/multi_range.cpp`): `deleteMultiRange` unassigns
	// the voices.
	sound->deleteMultiRange(source, index);
	if (lower_top && index > 0) {
		sound->sources[source].ranges.getElement(index - 1)->topNote = *lower_top;
	}
	deluge::edits::recordRanges(*sound, source);
	return true;
}

bool ShellEditSink::attach_recording(deluge::project::SlotAddress slot, uint8_t source, uint16_t range,
                                     std::string_view path) {
	if (currentSong == nullptr) {
		return false;
	}
	deluge::edits::AttachRecordedSample edit{slot, source, range, std::string{path}};
	return applied(edit);
}

bool ShellEditSink::resize_audio_clip(deluge::project::ClipAddress clip, deluge::scenes::bind::ResizeKind kind,
                                      int32_t new_length, int32_t old_length) {
	if (currentSong == nullptr) {
		return false;
	}
	using Kind = deluge::edits::ResizeAudioClip::Kind;
	using Request = deluge::scenes::bind::ResizeKind;
	Kind edit_kind = Kind::END;
	switch (kind) {
	case Request::END:
		edit_kind = Kind::END;
		break;
	case Request::START:
		edit_kind = Kind::START;
		break;
	case Request::STRETCH:
		edit_kind = Kind::STRETCH;
		break;
	case Request::NO_STRETCH:
		edit_kind = Kind::NO_STRETCH;
		break;
	case Request::TO_SAMPLE_LENGTH:
		edit_kind = Kind::TO_SAMPLE_LENGTH;
		break;
	}
	deluge::edits::ResizeAudioClip edit{clip, edit_kind, new_length, old_length};
	resize_action_ = nullptr;
	if (!applied(edit)) {
		return false;
	}
	resize_action_ = edit.lengthAction();
	resize_clip_ = clip;
	return true;
}

bool ShellEditSink::learn(deluge::project::MappingTarget target, std::optional<deluge::project::MidiCommand> command,
                          std::optional<deluge::scenes::bind::LearnInput> input) {
	if (currentSong == nullptr) {
		return false;
	}
	if (!command) {
		// `MenuItemWithCCLearning::unlearnAction`: the menu unlearns only a param's knob, which marks the current
		// instrument edited (`SoundEditor::markInstrumentAsEdited`), as `learn_knob` does for the learn.
		deluge::edits::UnlearnMapping edit{std::move(target)};
		if (!applied(edit)) {
			return false;
		}
		if (Instrument* instrument = getCurrentInstrument()) {
			instrument->beenEdited();
		}
		return true;
	}
	std::optional<deluge::edits::InputLearn> learned_input;
	if (input) {
		learned_input = deluge::edits::InputLearn{.cable = input->cable, .channel_or_zone = input->channel_or_zone};
	}
	deluge::edits::LearnMapping edit{
	    deluge::project::Mapping{.source = std::move(*command), .target = std::move(target)}, std::nullopt,
	    learned_input};
	return applied(edit);
}

bool ShellEditSink::learn_knob(const deluge::project::OwnerParamTarget& target, ::MIDICable* cable, int32_t which_knob,
                               int32_t midi_channel) {
	if (currentSong == nullptr) {
		return false;
	}
	const std::optional<deluge::model::EffectsOwner> effects =
	    deluge::bridge::effectsOwnerAt(*currentSong, target.owner);
	::ModControllable* owner = deluge::bridge::controlsSlotAt(*currentSong, target.owner);
	if (!effects || owner == nullptr) {
		return false;
	}
	const bool isSound = effects->sound != nullptr;
	namespace params = deluge::modulation::params;
	const deluge::modulation::KnobTarget knob = deluge::bridge::knobOfControl(
	    target.param, isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL);
	if (!knob) {
		return false;
	}
	// `SoundEditor::modEncoderAction` learns a mod knob in the current output's knob mode.
	Output* output = getCurrentOutput();
	const int32_t mode = midi_channel >= 16 && output != nullptr ? output->modKnobMode : 0;
	if (!deluge::edits::learnKnobOn(*owner, knob, cable, which_knob, mode, midi_channel)) {
		return false;
	}
	// `SoundEditor::markInstrumentAsEdited`.
	if (Instrument* instrument = getCurrentInstrument()) {
		instrument->beenEdited();
	}
	return true;
}

bool ShellEditSink::write_source_transpose(const deluge::scenes::bind::Addresses& where, uint8_t source,
                                           std::optional<uint16_t> range, int32_t menu_value) {
	if (currentSong == nullptr || source >= kNumSources) {
		return false;
	}
	// `computeFinalValuesForTranspose` (`util/value_scaling.cpp`).
	const int32_t current = menu_value + 25600;
	const int32_t semitones = (current + 50) / 100;
	const int32_t cents = current - semitones * 100;
	const int32_t transpose = semitones - 256;
	// One sound: a sample source with ranges (not FM) in its range, any other source in its own setting.
	const auto one = [&](deluge::project::SlotAddress slot, std::optional<uint16_t> in_range) {
		::Sound* sound = deluge::bridge::soundAt(*currentSong, slot);
		if (sound == nullptr || sound->boundState() == nullptr) {
			return false;
		}
		const deluge::instrument::SoundState& state = *sound->boundState();
		const deluge::instrument::SourceState& src = state.sources[source];
		if (!src.ranges.empty() && state.synth_mode != deluge::instrument::SynthMode::FM
		    && src.osc_type == deluge::instrument::OscType::SAMPLE) {
			std::vector<deluge::instrument::RangeState> ranges = src.ranges;
			const size_t r = in_range && *in_range < ranges.size() ? *in_range : 0;
			ranges[r].transpose = static_cast<int16_t>(transpose);
			ranges[r].cents = static_cast<int8_t>(cents);
			deluge::edits::SetSourceRanges edit{slot, source, std::move(ranges)};
			return applied(edit);
		}
		const std::string osc = "osc" + std::to_string(source + 1) + "/";
		deluge::edits::SetEngineSetting semis{slot, osc + "transpose",
		                                      deluge::serialization::Value::makeNumber(transpose)};
		deluge::edits::SetEngineSetting fine{slot, osc + "cents", deluge::serialization::Value::makeNumber(cents)};
		const bool took = applied(semis);
		return applied(fine) && took;
	};
	if (where.affect_entire && where.kit_output) {
		// Every sound row of the kit, each in its range 0 (`origin/main:src/deluge/gui/menu_item/sample/transpose.h`'s
		// kit sweep).
		::Output* output = currentSong->outputForId(*where.kit_output);
		if (output == nullptr || output->type != OutputType::KIT) {
			return false;
		}
		bool took = true;
		for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND && drum->id()) {
				took = one({deluge::project::SlotAddress::Kind::DRUM, *drum->id()}, std::nullopt) && took;
			}
		}
		return took;
	}
	return where.slot && one(*where.slot, range);
}

void ShellEditSink::set_undo_scroll_after(deluge::project::ClipAddress clip, int32_t x_scroll) {
	::Action* const step = resize_action_;
	resize_action_ = nullptr; // one view per length change
	if (step == nullptr || !(clip == resize_clip_)) {
		return;
	}
	// The step is the log's still, unless something since has replaced the log.
	for (::Action* action = actionLogger.firstAction[BEFORE]; action != nullptr; action = action->nextAction) {
		if (action == step) {
			action->xScrollClip[AFTER] = x_scroll;
			return;
		}
	}
}

bool ShellEditSink::recolour_clip(deluge::project::ClipAddress clip, int32_t offset) {
	if (currentSong == nullptr) {
		return false;
	}
	deluge::edits::SetClipColour edit{clip, offset};
	return applied(edit);
}

bool ShellEditSink::clear_clip(deluge::project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) {
	if (currentSong == nullptr) {
		return false;
	}
	deluge::edits::ClearClip edit{clip, clear_automation, clear_sequence_and_mpe};
	return applied(edit);
}

bool ShellEditSink::move_audio_clip_marker(deluge::project::ClipAddress clip, std::string_view path, int64_t value) {
	if (currentSong == nullptr) {
		return false;
	}
	::Clip* found = currentSong->clipForId(clip.clip_id);
	if (found == nullptr || found->type != ClipType::AUDIO) {
		refusals_++;
		return false;
	}
	// The marker editor's move: the record's row written with the voice taken off the sample, the moved part's
	// clusters claimed and a playing clip resumed. A write of the row by any other gesture does not reseat.
	bool taken = false;
	deluge::bridge::withVoiceReseated(*static_cast<::AudioClip*>(found), *currentSong, [&] {
		deluge::edits::SetSetting edit{
		    deluge::project::SettingAddress{.group = deluge::project::SettingGroupId::AUDIO_CLIP, .owner = clip},
		    std::string{path}, deluge::serialization::Value::makeNumber(value)};
		taken = applied(edit);
	});
	return taken;
}

bool ShellEditSink::write(const WriteRequest& request) {
	if (currentSong == nullptr || request.target.kind == deluge::scenes::bind::Target::Kind::NONE) {
		return false;
	}
	const uint32_t refused = refusals_;
	apply(request);
	return refusals_ == refused;
}

void ShellEditSink::apply(const WriteRequest& request) {
	using Kind = deluge::scenes::bind::Target::Kind;
	const Kind kind = request.target.kind;
	if (kind == Kind::CABLE) {
		writeCable(request);
		return;
	}
	if (kind == Kind::PARAM) {
		writeParam(request);
		return;
	}
	// A model group's record (a MODEL target, or a sound setting on an audio clip), else a sound's or a chain's.
	const std::optional<deluge::project::SettingAddress> model =
	    deluge::scenes::bind::setting_address(request.target, request.where);
	if (kind == Kind::MODEL && !model) {
		refusals_++;
		D_PRINTLN("menu write refused: %.*s has no owner", static_cast<int>(request.target.path.size()),
		          request.target.path.data());
		return;
	}
	const std::span<const deluge::plugin::SettingInfo> table = model ? deluge::project::table_of(model->group)
	                                                           : kind == Kind::EFFECT ? deluge::effects::kEffectSettings
	                                                                                  : deluge::instrument::kSettings;
	const std::string_view path = model && kind == Kind::SOUND ? request.target.audio_clip_path : request.target.path;
	const deluge::plugin::SettingInfo* row = deluge::plugin::find_setting(table, path);
	const auto stored = row != nullptr ? deluge::plugin::from_menu(*row, request.menu_value) : std::nullopt;
	if (!stored) {
		refusals_++;
		D_PRINTLN("menu write refused: %.*s menu %d", static_cast<int>(request.target.path.size()),
		          request.target.path.data(), static_cast<int>(request.menu_value));
		return;
	}
	const std::string_view partner = deluge::plugin::mapping_of(*row).partner_path();
	const deluge::plugin::SettingInfo* partner_row =
	    partner.empty() ? nullptr : deluge::plugin::find_setting(table, partner);

	// Commits @p row's value, then its partner's, each as the Edit @p edit_for makes for a path and a file value.
	const auto commit_both = [&](deluge::edits::Context& ctx, const auto& edit_for) {
		const auto commit = [&](const deluge::plugin::SettingInfo& on, int32_t value) {
			const std::optional<deluge::serialization::Value> file = value_of(on, value);
			if (!file) {
				refusals_++;
				D_PRINTLN("menu write refused: %.*s has no word for %d", static_cast<int>(on.path.size()),
				          on.path.data(), static_cast<int>(value));
				return;
			}
			auto edit = edit_for(std::string(on.path), *file);
			if (deluge::edits::apply(ctx, edit) != Error::NONE) {
				refusals_++;
				D_PRINTLN("menu write refused: %.*s = %d", static_cast<int>(on.path.size()), on.path.data(),
				          static_cast<int>(value));
			}
		};
		commit(*row, stored->first);
		if (partner_row != nullptr) {
			commit(*partner_row, stored->second);
		}
	};

	if (model && model->group == deluge::project::SettingGroupId::LANE_ARP && request.where.affect_entire
	    && request.target.whole_kit && request.where.kit_output) {
		// A kit row's arp with affect-entire held: every lane that takes it, as legacy's loops write them.
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		const bool needs_note_tails =
		    request.target.fan_out_fact == deluge::scenes::bind::Target::FanOutFact::NOTE_TAILS;
		commit_both(ctx, [&](std::string on, const deluge::serialization::Value& value) {
			return deluge::edits::SetKitLaneSetting{*request.where.kit_output, std::move(on), value, needs_note_tails};
		});
		return;
	}
	if (model && model->group == deluge::project::SettingGroupId::LANE_ARP
	    && request.target.fan_out_fact == deluge::scenes::bind::Target::FanOutFact::NOTE_TAILS) {
		// A kit row whose sound cannot hold note tails stays out of its kit's arp (`IncludeInKitArp::writeCurrentValue`
		// in `origin/main:src/deluge/gui/menu_item/arpeggiator/include_in_kit_arp.h`).
		const auto& lane = std::get<deluge::project::LaneAddress>(model->owner);
		::Drum* drum = deluge::bridge::drumWithId(*currentSong, lane.drum_id);
		if (drum != nullptr && drum->type == DrumType::SOUND
		    && !deluge::scenes::menu::host_runtime_facts().allows_note_tails(
		        {deluge::project::SlotAddress::Kind::DRUM, lane.drum_id}, std::nullopt)) {
			refusals_++;
			D_PRINTLN("menu write refused: kit row %u cannot hold note tails for its kit's arp",
			          static_cast<unsigned>(lane.drum_id));
			return;
		}
	}
	if (model) {
		deluge::bridge::seedArpRecordAt(*currentSong, *model); // an arp's writers seed its record from the arp first
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string on, const deluge::serialization::Value& value) {
			return deluge::edits::SetSetting{*model, std::move(on), value};
		});
		return;
	}

	const auto apply_to = [&](deluge::project::SlotAddress slot) {
		if (deluge::bridge::soundAt(*currentSong, slot) == nullptr) {
			refusals_++;
			D_PRINTLN("menu write refused: no sound at the slot for %.*s", static_cast<int>(request.target.path.size()),
			          request.target.path.data());
			return;
		}
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string path, const deluge::serialization::Value& value) {
			return deluge::edits::SetEngineSetting{slot, std::move(path), value};
		});
	};

	if (request.where.affect_entire && request.target.whole_kit && request.where.kit_output) {
		Kit* kit = deluge::bridge::kitAt(*currentSong, *request.where.kit_output);
		if (kit == nullptr) {
			refusals_++;
			D_PRINTLN("menu write refused: no kit with id %u for %.*s",
			          static_cast<unsigned>(*request.where.kit_output), static_cast<int>(request.target.path.size()),
			          request.target.path.data());
			return;
		}
		if (silences_kit_arp(request.target, *row, stored->first) && has_sound_drum(*kit)) {
			kit_arp_note_off(kit);
		}
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string path, const deluge::serialization::Value& value) {
			return deluge::edits::SetKitSetting{*request.where.kit_output, std::move(path), value,
			                                    request.target.fan_out_when};
		});
		return;
	}
	if (request.where.slot) {
		if (silences_kit_arp(request.target, *row, stored->first) && request.where.kit_output) {
			kit_arp_note_off(deluge::bridge::kitAt(*currentSong, *request.where.kit_output));
		}
		apply_to(*request.where.slot);
		return;
	}
	if (kind == Kind::EFFECT && request.where.chain) {
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		commit_both(ctx, [&](std::string path, const deluge::serialization::Value& value) {
			return deluge::edits::SetEffectSetting{*request.where.chain, std::move(path), value};
		});
		return;
	}
	refusals_++;
	D_PRINTLN("menu write refused: %.*s has no slot or chain", static_cast<int>(request.target.path.size()),
	          request.target.path.data());
}

bool ShellRuntimeFacts::audio_inputs_present() const {
	return AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
}

int32_t ShellRuntimeFacts::live_voice_count() const {
	return AudioEngine::getNumVoices() + (currentSong != nullptr ? currentSong->countAudioVoices() : 0);
}

bool ShellRuntimeFacts::kit_takes_whole_folder(deluge::project::SlotAddress slot) const {
	::Clip* clip = currentSong != nullptr ? currentSong->getCurrentClip() : nullptr;
	if (slot.kind != deluge::project::SlotAddress::Kind::DRUM || clip == nullptr || clip->type != ClipType::INSTRUMENT
	    || clip->output == nullptr || clip->output->type != OutputType::KIT) {
		return false;
	}
	const auto& rows = static_cast<::InstrumentClip*>(clip)->noteRows;
	const ::Drum* drum = currentSong->drumForId(slot.id);
	const ::Kit* kit = static_cast<const ::Kit*>(clip->output);
	return drum != nullptr && !rows.empty() && rows[0].drum == drum && kit->firstDrum == drum && drum->next == nullptr;
}

uint8_t ShellRuntimeFacts::loaded_sources(deluge::project::SlotAddress slot) const {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	uint8_t loaded = 0;
	if (sound != nullptr) {
		for (uint8_t s = 0; s < kNumSources; ++s) {
			if (sound->sources[s].hasAtLeastOneAudioFileLoaded()) {
				loaded |= static_cast<uint8_t>(1u << s);
			}
		}
	}
	return loaded;
}

const deluge::instrument::SoundState* ShellRuntimeFacts::sound_state(deluge::project::SlotAddress slot) const {
	::Sound* sound = currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
	return sound != nullptr ? sound->boundState() : nullptr;
}

const deluge::effects::EffectStates* ShellRuntimeFacts::chain_effects(deluge::project::ChainAddress chain) const {
	// The effects the chain's record holds, which its owner plays.
	const deluge::project::FxChain* record =
	    currentSong != nullptr ? currentSong->project.resolveChain(chain) : nullptr;
	return record != nullptr ? record->effects.as<deluge::effects::DelugeEffects>() : nullptr;
}

int32_t ShellRuntimeFacts::played_bend_range(const deluge::project::SettingAddress& owner, int32_t which) const {
	const int32_t range = which == 0 ? BEND_RANGE_MAIN : BEND_RANGE_FINGER_LEVEL;
	if (const ExpressionParamSet* expression =
	        currentSong != nullptr ? deluge::bridge::expressionSetAt(*currentSong, owner) : nullptr) {
		return expression->bendRanges[range];
	}
	// A new set starts at the device's default; a kit row's at its main range (ExpressionParamSet's forDrum).
	const bool drum = owner.group == deluge::project::SettingGroupId::NOTE_ROW
	                  && std::get<deluge::project::NoteRowAddress>(owner.owner).is_kit;
	return FlashStorage::defaultBendRange[drum ? BEND_RANGE_MAIN : range];
}

bool ShellRuntimeFacts::allows_note_tails(deluge::project::SlotAddress slot,
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
	const deluge::scenes::bind::KnobFocus focus{.owner = deluge::scenes::bind::SoundFocus{.slot = slot, .clip = clip}};
	ModelStackWithThreeMainThings* owner = deluge::bridge::knobOwnerStackAt(*currentSong, focus, memory);
	return owner == nullptr || sound->allowNoteTails(owner->addSoundFlags());
}

bool ShellEditSink::chain_view(deluge::project::ChainAddress chain, std::string_view key, std::string_view word) {
	if (currentSong == nullptr) {
		return false;
	}
	deluge::edits::SetChainView edit{chain, std::string{key},
	                                 deluge::serialization::Value::makeScalar(std::string{word})};
	return applied(edit);
}

bool ShellEditSink::write_controls(deluge::project::ControlsAddress owner,
                                   std::vector<deluge::project::ControllerMapping> controls) {
	if (currentSong == nullptr) {
		return false;
	}
	deluge::edits::SetControls edit{owner, std::move(controls)};
	return applied(edit);
}

bool ShellEditSink::stutter(const deluge::scenes::bind::KnobFocus& focus, bool begin) {
	const std::optional<deluge::project::ParamAddress> owner = deluge::scenes::bind::knob_owner(focus);
	return currentSong != nullptr && owner && deluge::bridge::stutterAt(*currentSong, *owner, begin);
}

bool ShellEditSink::grab_region(const deluge::scenes::bind::KnobFocus& focus, uint32_t pos) {
	const std::optional<deluge::project::ParamAddress> owner = deluge::scenes::bind::knob_owner(focus);
	return currentSong != nullptr && owner && deluge::bridge::grabRegionAt(*currentSong, *owner, pos);
}

namespace {

/// The instrument @p focus's knobs edit: a synth, a kit row's kit, a kit's own chain; none for an audio track, a
/// MIDI track (whose edited flag is no instrument's) or the song.
Instrument* knobInstrumentOf(::Song& song, const deluge::scenes::bind::KnobFocus& focus) {
	namespace bind = deluge::scenes::bind;
	std::optional<uint32_t> id;
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		id = sound->kit ? sound->kit : std::optional{sound->slot.id};
	}
	else if (const auto* chain = std::get_if<bind::ChainFocus>(&focus.owner);
	         chain && chain->owner == bind::ChainOwner::KIT) {
		id = chain->chain.id;
	}
	Output* output = id ? deluge::bridge::outputWithId(song, *id) : nullptr;
	return output != nullptr && output->type != OutputType::AUDIO ? static_cast<Instrument*>(output) : nullptr;
}

} // namespace

bool ShellEditSink::write_effect(const deluge::scenes::bind::EffectOwner& owner, std::string_view path,
                                 const deluge::serialization::Value& value) {
	if (currentSong == nullptr) {
		return false;
	}
	if (const auto* slot = std::get_if<deluge::project::SlotAddress>(&owner)) {
		deluge::edits::SetEngineSetting edit{*slot, std::string{path}, value};
		return applied(edit);
	}
	deluge::edits::SetEffectSetting edit{std::get<deluge::project::ChainAddress>(owner), std::string{path}, value};
	return applied(edit);
}

bool ShellEditSink::instrument_edited(const deluge::scenes::bind::KnobFocus& focus) {
	Instrument* instrument = currentSong != nullptr ? knobInstrumentOf(*currentSong, focus) : nullptr;
	if (instrument == nullptr) {
		return false;
	}
	instrument->beenEdited();
	return true;
}

namespace {

/// The lane of @p clip that a MIDI track's knob sending @p cc moves; none for a knob that sends nothing. A CC 1 moves
/// CC lane 1, as legacy's `getParamToControlFromInputMIDIChannel` does, though `knob_push` names it the mod wheel.
std::optional<deluge::project::ParamAddress> midiKnobLane(deluge::project::ClipAddress clip,
                                                          const deluge::project::ModKnobCc& cc) {
	using Kind = deluge::project::ModKnobCc::Kind;
	using deluge::project::ParamSpace;
	const auto lane = [clip](ParamSpace space, uint32_t id) {
		return deluge::project::ParamAddress::of(deluge::project::OwnerId::clip(clip.clip_id),
		                                         {space, deluge::plugin::ParamId{id}});
	};
	switch (cc.kind) {
	case Kind::BEND:
		return lane(ParamSpace::EXPRESSION, 0);
	case Kind::AFTERTOUCH:
		return lane(ParamSpace::EXPRESSION, 2);
	case Kind::NUMBER:
		return cc.number == deluge::project::ModKnobCc::kYAxisNumber ? lane(ParamSpace::EXPRESSION, 1)
		                                                             : lane(ParamSpace::MIDI_CC, cc.number);
	case Kind::NONE:
		break;
	}
	return std::nullopt;
}

} // namespace

bool ShellEditSink::midi_cc_select(const deluge::scenes::bind::KnobFocus& focus, int32_t knob, bool on) {
	if (!on) {
		if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
			currentUIMode = UI_MODE_NONE;
			deluge::hid::display::OLED::removePopup();
		}
		return false;
	}
	const auto* midi = std::get_if<deluge::scenes::bind::MidiFocus>(&focus.owner);
	if (currentSong == nullptr || midi == nullptr || !midi->clip || currentUIMode != UI_MODE_NONE
	    || getCurrentUI()->toClipMinder() == nullptr) {
		return false;
	}
	const deluge::project::DocOutput* output = currentSong->project.output_structure.find(midi->track.output_id);
	if (output == nullptr) {
		return false;
	}
	// A clip the song does not hold has no CC to choose: legacy's push needs the clip's stack.
	alignas(ModelStackWithAutoParam) char owner_memory[MODEL_STACK_MAX_SIZE];
	if (deluge::bridge::knobOwnerStackAt(*currentSong, focus, owner_memory) == nullptr) {
		return false;
	}
	const size_t at =
	    (static_cast<size_t>(deluge::scenes::bind::knob_mode(currentSong->project, focus, kDelugeControls))
	     * kDelugeControls.slots)
	    + static_cast<size_t>(knob);
	const deluge::project::ModKnobCc cc =
	    at < output->mod_knob_ccs.size() ? output->mod_knob_ccs[at] : deluge::project::ModKnobCc{};
	// Reads only: a lane that does not exist yet has no automation, which is what creating it would report too.
	bool automated = false;
	if (const auto lane = midiKnobLane(*midi->clip, cc)) {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		const ModelStackWithAutoParam* stack = deluge::bridge::paramStackFollowingTake(*currentSong, *lane, memory);
		automated = stack != nullptr && stack->autoParam != nullptr && stack->autoParam->isAutomated(*stack->doc());
	}
	currentUIMode = UI_MODE_SELECTING_MIDI_CC;
	InstrumentClipMinder::editingMIDICCForWhichModKnob = static_cast<uint8_t>(knob);
	InstrumentClipMinder::drawMIDIControlNumber(MIDIInstrument::engineCcOf(cc), automated);
	return true;
}

std::optional<int32_t> ShellRuntimeFacts::param_value(const deluge::project::ParamAddress& address) const {
	if (currentSong == nullptr) {
		return std::nullopt;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackFollowingTake(*currentSong, address, memory);
	// A param the owner does not hold yet (a cable not made) comes back as a stack with no lane.
	return stack != nullptr && stack->autoParam != nullptr ? std::optional{stack->autoParam->getCurrentValue()}
	                                                       : std::nullopt;
}

bool ShellRuntimeFacts::param_automated(const deluge::project::ParamAddress& address) const {
	if (currentSong == nullptr) {
		return false;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(*currentSong, address, memory);
	return stack != nullptr && stack->autoParam != nullptr && stack->autoParam->isAutomated(*stack->doc());
}

std::optional<int32_t> ShellRuntimeFacts::clip_param_value(const deluge::project::ParamAddress& address,
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

std::span<const deluge::controls::PlayingOutput> ShellRuntimeFacts::playing_outputs() const {
	using Kind = deluge::controls::PlayingOutput::Kind;
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

std::span<const deluge::controls::KitRow> ShellRuntimeFacts::sound_rows(uint32_t clip_id) const {
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

std::optional<deluge::scenes::bind::AudioClipFact>
ShellRuntimeFacts::audio_clip(deluge::project::ClipAddress clip) const {
	::AudioClip* audio = audioClipAt(clip);
	if (audio == nullptr || audio->output == nullptr || !audio->output->id()) {
		return std::nullopt;
	}
	return deluge::scenes::bind::AudioClipFact{
	    .track_id = *audio->output->id(),
	    .max_zoom = audio->getMaxZoom(),
	    .unarmed = audio->armState == ArmState::OFF,
	    .reversed = audio->sampleControls.isCurrentlyReversed(),
	    .session_index = currentSong->live.clips.session_clips.getIndexForClip(audio),
	};
}

bool ShellRuntimeFacts::clip_playing(deluge::project::ClipAddress clip) const {
	::AudioClip* audio = audioClipAt(clip);
	return audio != nullptr && playbackHandler.playbackState != 0u && currentSong->isClipActive(audio)
	       && playbackHandler.ticksLeftInCountIn == 0u;
}

int32_t ShellRuntimeFacts::clip_live_pos(deluge::project::ClipAddress clip) const {
	::AudioClip* audio = audioClipAt(clip);
	return audio != nullptr ? static_cast<int32_t>(audio->getLivePos()) : 0;
}

bool ShellRuntimeFacts::clip_recording_linearly(deluge::project::ClipAddress clip) const {
	::AudioClip* audio = audioClipAt(clip);
	return audio != nullptr && audio->getCurrentlyRecordingLinearly();
}

std::pair<int64_t, int64_t> ShellRuntimeFacts::audio_clip_samples(deluge::project::ClipAddress clip, int32_t scroll,
                                                                  int32_t zoom) const {
	::AudioClip* audio = audioClipAt(clip);
	if (audio == nullptr) {
		return {0, 0};
	}
	int64_t scroll_samples = 0;
	int64_t zoom_samples = 0;
	audio->getScrollAndZoomInSamples(scroll, zoom, &scroll_samples, &zoom_samples);
	return {scroll_samples, zoom_samples};
}

std::vector<deluge::scenes::bind::TrackFact> ShellRuntimeFacts::tracks() const {
	using deluge::scenes::bind::TrackKind;
	std::vector<deluge::scenes::bind::TrackFact> list;
	if (currentSong == nullptr) {
		return list;
	}
	for (::Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
		if (!output->id()) {
			continue; // a track the song has not given an id cannot be named
		}
		deluge::scenes::bind::TrackFact fact{.output_id = *output->id()};
		switch (output->type) {
		case OutputType::KIT:
			fact.kind = TrackKind::KIT;
			break;
		case OutputType::MIDI_OUT:
			fact.kind = TrackKind::MIDI_OUT;
			fact.channel = static_cast<::NonAudioInstrument*>(output)->getChannel();
			break;
		case OutputType::CV:
			fact.kind = TrackKind::CV;
			fact.channel = static_cast<::NonAudioInstrument*>(output)->getChannel();
			break;
		case OutputType::AUDIO:
			fact.kind = TrackKind::AUDIO;
			break;
		default:
			fact.kind = TrackKind::SYNTH;
			break;
		}
		list.push_back(fact);
	}
	return list;
}

deluge::scenes::bind::CableList ShellRuntimeFacts::cables(const deluge::project::ParamAddress& owner) const {
	deluge::scenes::bind::CableList list;
	if (currentSong == nullptr) {
		return list;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	PatchCableSet* set = deluge::bridge::cableSetAt(*currentSong, owner, memory);
	if (set == nullptr) {
		return list;
	}
	for (int32_t i = 0; i < set->numPatchCables && list.count < list.items.size(); ++i) {
		const PatchCable& cable = set->patchCables[i];
		list.items[list.count++] = {.depth = deluge::bridge::cable_depth(cable.from, cable.destination),
		                            .polarity = static_cast<deluge::instrument::Polarity>(cable.polarity)};
	}
	return list;
}

uint8_t ShellRuntimeFacts::arp_preset(const deluge::project::SettingAddress& address) const {
	const ArpeggiatorSettings* arp = nullptr;
	if (currentSong != nullptr) {
		if (const auto* lane = std::get_if<deluge::project::LaneAddress>(&address.owner)) {
			const ::Drum* drum = deluge::bridge::drumWithId(*currentSong, lane->drum_id);
			arp = drum != nullptr ? &drum->arpSettings : nullptr;
		}
		else if (const auto* clip = std::get_if<deluge::project::ClipAddress>(&address.owner)) {
			const ::Clip* found = currentSong->clipForId(clip->clip_id);
			arp = found != nullptr && found->type == ClipType::INSTRUMENT
			          ? &static_cast<const InstrumentClip*>(found)->arpSettings
			          : nullptr;
		}
	}
	return arp != nullptr ? static_cast<uint8_t>(std::to_underlying(arp->preset)) : 0;
}

uint32_t shellEditRefusals() {
	return shell_edit_sink().refusals();
}

} // namespace deluge::gui

namespace deluge::scenes::menu {

bool host_binds(const ::Song& song) {
	return &song == currentSong;
}

deluge::scenes::bind::WriteSink& host_write_sink() {
	return gui::shell_edit_sink();
}

deluge::controls::Sink& host_controls_sink() {
	return gui::shell_edit_sink();
}

void host_grab_tempo(deluge::project::ClipAddress clip) {
	if (::AudioClip* audio = gui::audioClipAt(clip)) {
		playbackHandler.grabTempoFromClip(audio);
	}
}

void host_settings_reset() {
	gui::ui::reapplyLiveSettingsAfterReset();
}

const deluge::scenes::services::Waveforms& host_waveforms() {
	static const gui::ShellWaveforms waveforms;
	return waveforms;
}

std::string host_last_sample_path() {
	return deluge::model::lastFilePathLoaded();
}

namespace {

/// @brief The favourites the legacy browsers show, through `favouritesManager`.
class ShellFavourites final : public deluge::scenes::services::Favourites {
public:
	void set_category(std::string_view category) override { favouritesManager.setCategory(std::string{category}); }
	void close() override { favouritesManager.close(); }
	[[nodiscard]] deluge::scenes::services::FavouritesBank bank_state() const override {
		switch (favouritesManager.bankState()) {
		case deluge::favourites::BankState::Loading:
			return deluge::scenes::services::FavouritesBank::Loading;
		case deluge::favourites::BankState::Loaded:
			return deluge::scenes::services::FavouritesBank::Loaded;
		case deluge::favourites::BankState::Unavailable:
			break;
		}
		return deluge::scenes::services::FavouritesBank::Unavailable;
	}
	[[nodiscard]] std::array<std::optional<uint8_t>, deluge::ui::grid::kFavourites> colours() const override {
		return favouritesManager.getFavouriteColours();
	}
	[[nodiscard]] std::optional<uint8_t> current_favourite() const override {
		return favouritesManager.currentFavouriteNumber;
	}
	[[nodiscard]] uint8_t current_bank() const override { return favouritesManager.currentBankNumber; }
	[[nodiscard]] bool is_empty(uint8_t position) const override { return favouritesManager.isEmpty(position); }
	std::string recall(uint8_t position) override { return favouritesManager.getFavoriteFilename(position); }
	void set(uint8_t position, std::string_view path) override {
		favouritesManager.setFavorite(position, FavouritesManager::favouriteDefaultColor, std::string{path});
	}
	void unset(uint8_t position) override { favouritesManager.unsetFavorite(position); }
	void select_bank(uint8_t bank) override { favouritesManager.selectFavouritesBank(bank); }
	void change_colour(uint8_t position, int32_t offset) override { favouritesManager.changeColour(position, offset); }
};

} // namespace

deluge::scenes::services::Favourites& host_favourites() {
	static ShellFavourites favourites;
	return favourites;
}

bool host_release_sample_file(std::string_view path) {
	AudioEngine::stopAnyPreviewing();
	return audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(std::string{path}.c_str());
}

void host_forget_undo() {
	actionLogger.deleteAllLogs();
}

void host_clear_tick_squares() {
	PadLEDs::clearTickSquares(true);
}

void host_end_audition(AuditionEnd how) {
	switch (how) {
	case AuditionEnd::ClipViewWhenAuditioning:
		if (currentUIMode == UI_MODE_AUDITIONING) {
			instrumentClipView.cancelAllAuditioning();
		}
		break;
	case AuditionEnd::ClipViewForInstrumentClip:
		if (::Clip* clip = getCurrentClip(); clip != nullptr && clip->type == ClipType::INSTRUMENT) {
			instrumentClipView.cancelAllAuditioning();
		}
		break;
	case AuditionEnd::KeyboardWhenAuditioning:
		if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
			keyboardScreen.exitAuditionMode();
		}
		break;
	}
}

void host_screensaver_settings_changed() {
	deluge::hid::display::Screensaver::settingsChanged();
}

void host_pad_brightness_changed() {
	PadLEDs::setBrightnessLevel(FlashStorage::defaultPadBrightness);
}

void host_highlighting_changed() {
	gui::ui::pushShellHighlighting();
}

void host_rounded_corners_changed() {
	gui::ui::pushShellRoundedCorners();
}

void host_shift_sticky_changed() {
	// The guest's shift machine takes the setting (`opened()` seeds it only on the way in), and off, legacy's latch
	// goes with the guest's.
	const bool on = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ShiftIsSticky);
	shellHostUI.shell().lowering().set_shift_is_sticky(on);
	if (!on) {
		Buttons::clearShiftSticky();
	}
}

void host_pad_colours_changed() {
	renderingNeededRegardlessOfUI();
}

const deluge::scenes::bind::RuntimeFacts& host_runtime_facts() {
	static const gui::ShellRuntimeFacts facts;
	return facts;
}

} // namespace deluge::scenes::menu

namespace deluge::midi {

namespace {

/// The learned knobs one message moves, owner by owner, copied out of the index (a write the dispatch makes can rebuild
/// it) into a list kept from message to message. Each hit is assigned over the one before it in its place, so a
/// message reuses the list's room and its device names' storage, and allocates only where it outgrows them.
class KnobHitsOf final : public controls::HitsOf {
public:
	/// @brief Aim at the knobs a message moves: @p number (a CC, or 128 for a pitch bend) from @p from on @p channel,
	/// raw (a knob learned on an MPE zone never matches it).
	KnobHitsOf& aim(MIDICable& from, uint8_t channel, uint8_t number) {
		from_ = &from;
		channel_ = channel;
		number_ = number;
		return *this;
	}
	[[nodiscard]] bool any() override { return mappingIndex().anyKnob(*from_, channel_, number_); }
	[[nodiscard]] controls::KnobHits of(project::ControlsAddress owner) override {
		size_t count = 0;
		mappingIndex().forEachKnobWithSource(
		    owner, *from_, channel_, number_, [&](const IndexedEntry& hit, const project::MidiCommand& source) {
			    const auto* target = std::get_if<project::OwnerParamTarget>(&hit.target);
			    if (target == nullptr) {
				    return;
			    }
			    if (count < hits_.size()) {
				    controls::LearnedHit& kept = hits_[count];
				    kept.target = *target;
				    kept.source = source;
				    kept.relative = hit.relative;
			    }
			    else {
				    hits_.push_back({.target = *target, .source = source, .relative = hit.relative});
			    }
			    count++;
		    });
		return controls::KnobHits{hits_.data(), count};
	}

private:
	MIDICable* from_{nullptr};
	uint8_t channel_{0};
	uint8_t number_{0};
	deluge::fast_vector<controls::LearnedHit> hits_;
};

/// The one list of hits: the dispatch runs on the app loop only.
KnobHitsOf& knobHits(MIDICable& from, uint8_t channel, uint8_t number) {
	static KnobHitsOf hits;
	return hits.aim(from, channel, number);
}

/// 128 stands for pitch bend among a knob's CC numbers.
constexpr uint8_t kPitchBend = 128;

} // namespace

void learnedCcToSong(MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value) {
	if (currentSong == nullptr) {
		return;
	}
	controls::dispatch_song_cc(knobHits(cable, channel, cc), scenes::menu::host_runtime_facts(),
	                           scenes::menu::host_controls_sink(), takeoverStore(),
	                           {.channel = channel, .number = cc, .value = value, .cable_relative = cable.is_relative});
}

void learnedCcToOutput(const Output& output, MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value) {
	if (currentSong == nullptr || !output.id()) {
		return;
	}
	controls::dispatch_output_cc(
	    *output.id(), knobHits(cable, channel, cc), scenes::menu::host_runtime_facts(),
	    scenes::menu::host_controls_sink(), takeoverStore(),
	    {.channel = channel, .number = cc, .value = value, .cable_relative = cable.is_relative});
}

bool learnedPitchBendToOutput(const Output& output, MIDICable& cable, uint8_t channel, uint16_t value14) {
	if (currentSong == nullptr || !output.id()) {
		return false;
	}
	return controls::dispatch_output_bend(
	    *output.id(), knobHits(cable, channel, kPitchBend), scenes::menu::host_runtime_facts(),
	    scenes::menu::host_controls_sink(),
	    {.channel = channel, .number = kPitchBend, .value = value14, .cable_relative = cable.is_relative});
}

const controls::Facts& followFacts() {
	return scenes::menu::host_runtime_facts();
}

controls::KnobFocus followFocus(Clip& clip) {
	return gui::knobFocusOfClip(clip);
}

controls::Sink& followSink() {
	return scenes::menu::host_controls_sink();
}

} // namespace deluge::midi
