#pragma once

/// @file
/// @brief The app's implementations of the scenes' ports, one per file in this directory, and the one set of ports
///        built from them. Only these files name the legacy runtime objects the scenes read.

#include "scenes/bind/ports/ports.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class AudioClip;
class Sample;

namespace deluge::edits {
class Edit;
} // namespace deluge::edits

namespace deluge::gui::shell_host {

/// @brief The identity a waveform's info carries for @p sample: one per file in memory, another for another file or a
///        take.
[[nodiscard]] uint64_t waveform_identity(const ::Sample& sample);

/// @brief The host's AudioFacts.
[[nodiscard]] const scenes::bind::AudioFacts& audio_facts();
/// @brief The host's CardFacts.
[[nodiscard]] const scenes::bind::CardFacts& card_facts();
/// @brief The host's UiFacts.
[[nodiscard]] const scenes::bind::UiFacts& ui_facts();

/// @brief The host's SoundFacts.
[[nodiscard]] const scenes::bind::SoundFacts& sound_facts();

/// @brief The host's ClipFacts.
[[nodiscard]] const scenes::bind::ClipFacts& clip_facts();

/// @brief The host's TrackFacts.
[[nodiscard]] const scenes::bind::TrackFacts& track_facts();

/// @brief The host's controls::Facts: the knobs' reads.
[[nodiscard]] const controls::Facts& control_facts();

/// @brief The host's ClipWrites: the clip edits, applied to the current song.
[[nodiscard]] scenes::bind::ClipWrites& clip_writes();

/// @brief The host's SoundWrites: the sound's source edits, applied to the current song.
[[nodiscard]] scenes::bind::SoundWrites& sound_writes();

/// @brief The host's LearnSink: the learns, applied to the current song.
[[nodiscard]] scenes::bind::LearnSink& learn_sink();

/// @brief The host's MidiDevices: the cables `MIDIDeviceManager` keeps, and their own settings.
[[nodiscard]] scenes::bind::MidiDevices& midi_devices();

/// @brief The host's DeviceSettings: the device's own settings where it keeps them (the flash settings, the pad
///        controller's play cursor), each stored with the side effects its menu item has. Nothing is saved on a write.
[[nodiscard]] scenes::bind::DeviceSettings& device_settings();

/// @brief The host's DeviceActions: the device's own actions, each the legacy call it names.
[[nodiscard]] scenes::bind::DeviceActions& device_actions();

/// @brief The host's MidiFeedback: MIDI follow's feedback, read and sent through the `midiFollow` engine.
[[nodiscard]] scenes::bind::MidiFeedback& midi_feedback();

/// @brief The host's Transport: playback, the clocks, recording and the metronome from `playbackHandler`, what undo
///        allows from `actionLogger`, and the current song's pending overdubs.
[[nodiscard]] const scenes::bind::Transport& transport();

/// @brief The host's TransportActions: each the call the legacy buttons make, popups included.
[[nodiscard]] scenes::bind::TransportActions& transport_actions();

/// @brief Give the overlays the app's ports they read. Idempotent; the boot and every guest open call it.
void install_scene_ports();

/// @brief The host's WriteSink and controls::Sink: the menus' writes and the knobs', applied to the current song as
///        Edits.
///
/// A sound target's request becomes SetEngineSetting on the sound's slot, and its partner row's value a second one;
/// with affect-entire held on a kit, one SetKitSetting over the kit's sound drums the target's fan-out rule picks,
/// read in each drum's own context, as legacy's loop picks them (and its partner a second); a drum that refuses the
/// value is left as it was, as legacy's loop writes it harmlessly. A sample's loop mode set to play once ends the kit
/// arpeggiator's note first. An effect target's request is the same on a sound's slot, and on a host chain (the whole
/// kit's, an audio track's, the master's) SetEffectSetting on the chain, its partner second. A request whose slot or
/// chain does not resolve, or a sound with no slot, is refused: logged and counted, never applied through the
/// runtime's setter alone.
class HostEditSink final : public scenes::bind::WriteSink, public controls::Sink {
public:
	bool write(const scenes::bind::WriteRequest& request) override;
	bool delete_automation(const project::ParamAddress& address) override;
	bool write_text(const scenes::bind::Target& target, const scenes::bind::Addresses& where,
	                std::string_view text) override;
	void set_default_audio_input_channel(int32_t channel) override;
	void set_song_threshold_recording(int32_t mode) override;
	void set_reverb_sidechain_sync_type(plugin::SyncType type) override;

	bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                 controls::Resolution resolution) override;
	void learned_written(const project::ParamAddress& address, int32_t knob_pos) override;
	std::optional<uint32_t> take_for(uint32_t output_id) override;
	bool make_param(const project::ParamAddress& address) override;
	void knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) override;
	bool knob_mode(project::KnobModeOwner owner, uint8_t mode) override;
	bool chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) override;
	bool write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) override;
	bool stutter(const controls::KnobFocus& focus, bool begin) override;
	bool grab_region(const controls::KnobFocus& focus, uint32_t pos) override;
	bool write_effect(const scenes::bind::EffectOwner& owner, std::string_view path,
	                  const serialization::Value& value) override;
	bool midi_cc_select(const controls::KnobFocus& focus, int32_t knob, bool on) override;
	bool delete_knob_automation(const project::ParamAddress& address) override;
	bool instrument_edited(const controls::KnobFocus& focus) override;

	/// @brief How many writes this sink has refused since start-up.
	/// @return The count: a request whose slot does not resolve, a sound with no slot, or a value its Edit refused.
	[[nodiscard]] uint32_t refusals() const { return refusals_; }

private:
	/// @brief Apply @p request, counting a refusal.
	void apply(const scenes::bind::WriteRequest& request);
	/// @brief A PARAM target's write: SetParamValue by its address, or SetKitParamValue under affect-entire on a kit
	///        row; then MIDI follow's feedback.
	void write_param_target(const scenes::bind::WriteRequest& request);
	/// @brief A CABLE target's write: SetPatchCable by its address, or, polarity only, SetCablePolarity.
	void write_cable_target(const scenes::bind::WriteRequest& request);
	/// @brief Apply @p edit to the current song, counting a refusal.
	/// @return Whether it applied.
	bool counted_apply(edits::Edit& edit);

	uint32_t refusals_{0}; ///< the writes refused since start-up
};

/// @brief The host's HostEditSink: both `Ports::writes` and `Ports::controls_sink`.
[[nodiscard]] HostEditSink& edit_sink();

/// @brief How many menu writes the host's edit sink has refused since start-up. Each is also logged.
/// @return HostEditSink::refusals of @ref edit_sink.
[[nodiscard]] uint32_t edit_refusals();

/// @brief Apply @p edit to the current song: the one way the write ports apply an Edit.
/// @param edit The edit. The caller has checked that a song is loaded.
/// @return Whether the edit applied (`Error::NONE`).
[[nodiscard]] bool applied(edits::Edit& edit);

/// @brief The audio clip @p clip addresses in the current song, or null: shared by the clip port and the waveforms.
[[nodiscard]] ::AudioClip* audio_clip_at(project::ClipAddress clip);

} // namespace deluge::gui::shell_host
