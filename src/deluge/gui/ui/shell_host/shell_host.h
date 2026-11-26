#pragma once

/// @file
/// @brief The app's implementations of the scenes' ports, one per file in this directory, and the one set of ports
///        built from them. Only these files name the legacy runtime objects the scenes read.

#include "ui/scenes/bind/ports/ports.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class AudioClip;
class SampleRecorder;
class Sample;

namespace deluge::edits {
class Edit;
} // namespace deluge::edits

namespace deluge::gui::shell_host {

/// @brief The identity a waveform's info carries for @p sample: its lifetime serial (`Sample::serial`), so another
///        file, a take, or a sample constructed later in the same memory has another. Never 0.
[[nodiscard]] uint64_t waveform_identity(const ::Sample& sample);

/// @brief The host's AudioFacts.
[[nodiscard]] const deluge::ui::scenes::bind::AudioFacts& audio_facts();
/// @brief The host's CardFacts.
[[nodiscard]] const deluge::ui::scenes::bind::CardFacts& card_facts();
/// @brief The host's Catalogue: the card's one file catalogue, `storage::FileCatalogue`.
[[nodiscard]] deluge::ui::scenes::bind::Catalogue& catalogue();
/// @brief The host's CardActions: the card's file actions, each the legacy step it names.
[[nodiscard]] deluge::ui::scenes::bind::CardActions& card_actions();
/// @brief The host's Favourites: the bank `favouritesManager` holds, which the legacy browsers show too.
[[nodiscard]] deluge::ui::scenes::bind::Favourites& favourites();
/// @brief The host's UiFacts.
[[nodiscard]] const deluge::ui::scenes::bind::UiFacts& ui_facts();

/// @brief The host's SoundFacts.
[[nodiscard]] const deluge::ui::scenes::bind::SoundFacts& sound_facts();

/// @brief The host's ClipFacts.
[[nodiscard]] const deluge::ui::scenes::bind::ClipFacts& clip_facts();

/// @brief The host's TrackFacts.
[[nodiscard]] const deluge::ui::scenes::bind::TrackFacts& track_facts();

/// @brief The host's controls::Facts: the knobs' reads.
[[nodiscard]] const deluge::ui::controls::Facts& control_facts();

/// @brief The host's ClipWrites: the clip edits, applied to the current song.
[[nodiscard]] deluge::ui::scenes::bind::ClipWrites& clip_writes();

/// @brief The host's SoundWrites: the sound's source edits, applied to the current song.
[[nodiscard]] deluge::ui::scenes::bind::SoundWrites& sound_writes();

/// @brief The host's LearnSink: the learns, applied to the current song.
[[nodiscard]] deluge::ui::scenes::bind::LearnSink& learn_sink();

/// @brief The host's MidiDevices: the cables `MIDIDeviceManager` keeps, and their own settings.
[[nodiscard]] deluge::ui::scenes::bind::MidiDevices& midi_devices();

/// @brief The host's DeviceSettings: the device's own settings where it keeps them (the flash settings, the pad
///        controller's play cursor), each stored with the side effects its menu item has. Nothing is saved on a write.
[[nodiscard]] deluge::ui::scenes::bind::DeviceSettings& device_settings();

/// @brief The host's DeviceActions: the device's own actions, each the legacy call it names.
[[nodiscard]] deluge::ui::scenes::bind::DeviceActions& device_actions();

/// @brief The host's MidiFeedback: MIDI follow's feedback, read and sent through the `midiFollow` engine.
[[nodiscard]] deluge::ui::scenes::bind::MidiFeedback& midi_feedback();

/// @brief The host's Transport: playback, the clocks, recording and the metronome from `playbackHandler`, what undo
///        allows from `actionLogger`, and the current song's pending overdubs.
[[nodiscard]] const deluge::ui::scenes::bind::Transport& transport();

/// @brief The host's TransportActions: each the call the legacy buttons make, popups included.
[[nodiscard]] deluge::ui::scenes::bind::TransportActions& transport_actions();

/// @brief The host's Waveforms: the samples a song's ranges and audio clips, the browser's preview and the slicer
///        hold, with the columns read lately kept per sample.
[[nodiscard]] const deluge::ui::scenes::bind::Waveforms& waveforms();

/// @brief The host's TakeActions: the recorder screen's input take, one recorder held from begin to finish or orphan.
[[nodiscard]] deluge::ui::scenes::bind::TakeActions& take_actions();

/// @brief The take `take_actions` holds.
/// @return The recorder, or null when no take is held.
[[nodiscard]] ::SampleRecorder* input_take();

/// @brief The host's Indicators: the pads' tick squares, through `PadLEDs`.
[[nodiscard]] deluge::ui::scenes::bind::Indicators& indicators();

/// @brief The host's ViewActions: each the call the legacy opener it names makes on the root view.
[[nodiscard]] deluge::ui::scenes::bind::ViewActions& view_actions();

/// @brief Give the overlays, the slicer and the audio clip scene the app's ports they read. Idempotent; the boot and
///        every guest open call it.
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
class HostEditSink final : public deluge::ui::scenes::bind::WriteSink, public deluge::ui::controls::Sink {
public:
	bool write(const deluge::ui::scenes::bind::WriteRequest& request) override;
	bool delete_automation(const project::ParamAddress& address) override;
	bool write_text(const deluge::ui::scenes::bind::Target& target, const deluge::ui::scenes::bind::Addresses& where,
	                std::string_view text) override;
	void set_default_audio_input_channel(int32_t channel) override;
	void set_song_threshold_recording(int32_t mode) override;
	void set_reverb_sidechain_sync_type(plugin::SyncType type) override;

	bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                 deluge::ui::controls::Resolution resolution) override;
	void learned_written(const project::ParamAddress& address, int32_t knob_pos) override;
	std::optional<uint32_t> take_for(uint32_t output_id) override;
	bool make_param(const project::ParamAddress& address) override;
	void knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) override;
	bool knob_mode(project::KnobModeOwner owner, uint8_t mode) override;
	bool chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) override;
	bool write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) override;
	bool stutter(const deluge::ui::controls::KnobFocus& focus, bool begin) override;
	bool grab_region(const deluge::ui::controls::KnobFocus& focus, uint32_t pos) override;
	bool write_effect(const deluge::ui::scenes::bind::EffectOwner& owner, std::string_view path,
	                  const serialization::Value& value) override;
	bool midi_cc_select(const deluge::ui::controls::KnobFocus& focus, int32_t knob, bool on) override;
	bool delete_knob_automation(const project::ParamAddress& address) override;
	bool instrument_edited(const deluge::ui::controls::KnobFocus& focus) override;

	/// @brief How many writes this sink has refused since start-up.
	/// @return The count: a request whose slot does not resolve, a sound with no slot, or a value its Edit refused.
	[[nodiscard]] uint32_t refusals() const { return refusals_; }

private:
	/// @brief Apply @p request, counting a refusal.
	void apply(const deluge::ui::scenes::bind::WriteRequest& request);
	/// @brief A PARAM target's write: SetParamValue by its address, or SetKitParamValue under affect-entire on a kit
	///        row; then MIDI follow's feedback.
	void write_param_target(const deluge::ui::scenes::bind::WriteRequest& request);
	/// @brief A CABLE target's write: SetPatchCable by its address, or, polarity only, SetCablePolarity.
	void write_cable_target(const deluge::ui::scenes::bind::WriteRequest& request);
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
