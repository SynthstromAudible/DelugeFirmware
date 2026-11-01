#pragma once

/// @file
/// @brief Where a menu's writes go: requests a host applies as Edits, so the scenes name no Edit and no engine object.

#include "deluge-project/controller_mapping.h"
#include "deluge-project/fx_chain.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "deluge-project/param_address.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/sound_state.h"
#include "scenes/bind/knob_focus.h"
#include "scenes/bind/target.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

class MIDICable;

namespace deluge::scenes::bind {

/// @brief A note heard on a track's or kit row's input learn: the cable it came on and its channel or MPE zone, which
/// the learn takes the bend ranges and velocity default of.
struct LearnInput {
	const ::MIDICable* cable{nullptr};
	int32_t channel_or_zone{0};
	bool operator==(const LearnInput&) const = default;
};

/// @brief One menu write: what, where, and the menu value the user chose.
struct WriteRequest {
	Target target;
	Addresses where;
	int32_t menu_value{0};
	/// PARAM and CABLE: the stored word the menu value stands for (a cable's strength as the engine stores it).
	int32_t param_value{0};
	/// CABLE only: the polarity a new cable takes, or, with @ref polarity_only, the polarity an existing cable is set
	/// to.
	instrument::Polarity polarity{instrument::Polarity::UNIPOLAR};
	/// CABLE only: set an existing cable's polarity and leave its strength.
	bool polarity_only{false};
	bool operator==(const WriteRequest&) const = default;
};

/// @brief Why a source's ranges are written: a plain edit, or a marker the marker editor moved (which a sounding voice
///        follows, and which counts as editing the instrument).
struct RangesCause {
	enum class Kind : uint8_t {
		EDIT,
		MARKER,
	};
	Kind kind{Kind::EDIT};
	int8_t marker{0}; ///< the moved marker's `MarkerType` value (START 0 .. END 3), for a MARKER cause
	bool operator==(const RangesCause&) const = default;
};

/// @brief How an audio clip's length gesture resizes it.
enum class ResizeKind : uint8_t {
	END,              ///< the sample's end marker dragged to a new clip length
	START,            ///< the sample's start marker dragged to a new start tick
	STRETCH,          ///< the clip's length changed, the sample timestretched to fill it
	NO_STRETCH,       ///< the clip's length changed with the sample's end marker, keeping its speed
	TO_SAMPLE_LENGTH, ///< the clip's length set to the sample's
};

/// @brief Whose effect settings a write names: a sound's slot, or a chain.
using EffectOwner = std::variant<project::SlotAddress, project::ChainAddress>;

/// @brief Takes a menu's writes. The app's sink applies each as the target's Edit; a spec's records them.
class WriteSink {
public:
	virtual ~WriteSink() = default;
	/// @brief Apply, or record, @p request.
	/// @return Whether it was taken: false where the app refused it, which left the song as it was.
	virtual bool write(const WriteRequest& request) = 0;
	/// @brief Write the param at @p address over a region, as a gold knob turns it; no MIDI-follow feedback (the knob
	/// sends its own).
	/// @param address The param's address.
	/// @param value   The new value, as the engine stores it.
	/// @param pos     The region's start; with @p length 0, the value is set at the playhead.
	/// @param length  The region's length, or 0.
	/// @return Whether it was taken.
	virtual bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length) = 0;
	/// @brief Delete the automation of the param at @p address.
	/// @return Whether it was taken.
	virtual bool delete_automation(const project::ParamAddress& address) = 0;
	/// @brief Write @p text to the TEXT row @p target names (a MODEL target: a clip's, track's or lane's name, a MIDI
	/// track's device definition file) on the owner @p where names.
	/// @return Whether it was taken.
	virtual bool write_text(const Target& target, const Addresses& where, std::string_view text) = 0;
	/// @brief Write source @p source's ranges of the sound at @p slot.
	/// @return Whether it was taken.
	virtual bool write_ranges(project::SlotAddress slot, uint8_t source, std::span<const instrument::RangeState> ranges,
	                          RangesCause cause = {}) = 0;
	/// @brief Move @p clip's colour by @p offset. Not undoable, as legacy's recolour is not.
	/// @return Whether it was taken.
	virtual bool recolour_clip(project::ClipAddress clip, int32_t offset) = 0;
	/// @brief Clear @p clip, as one undo step.
	/// @return Whether it was taken.
	virtual bool clear_clip(project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) = 0;
	/// @brief Move an audio clip's marker (its AUDIO_CLIP row @p path) to @p value, a playing voice reseated on the
	/// moved sample.
	/// @return Whether it was taken.
	virtual bool move_audio_clip_marker(project::ClipAddress clip, std::string_view path, int64_t value) = 0;
	/// @brief Make @p channel (an index of the input channel words) the input a new audio track starts on: the song's
	/// unsaved default, which the audio-source menu sets to whatever it last picked.
	virtual void set_default_audio_input_channel(int32_t channel) = 0;
	/// @brief Attach the recorded take at @p path (a sample in memory, never read from the card) to range @p range of
	/// source @p source of the sound at @p slot, which becomes a sample source.
	/// @return Whether it was taken: false where no take at @p path is in memory or the address does not resolve.
	virtual bool attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range, std::string_view path) = 0;
	/// @brief Resize the audio clip @p clip, as one undo step: its length, and the sample's marker where @p kind moves
	/// one. A lengthen right after a shorten of the same clip undoes the shorten.
	/// @param clip       The clip.
	/// @param kind       What the gesture moves.
	/// @param new_length The new length in ticks (START: the new start tick; TO_SAMPLE_LENGTH: not read).
	/// @param old_length The length the gesture started from, in ticks.
	/// @return Whether it was taken: false where the clip does not resolve or has no sample to measure.
	virtual bool resize_audio_clip(project::ClipAddress clip, ResizeKind kind, int32_t new_length,
	                               int32_t old_length) = 0;
	/// @brief Record @p x_scroll as the view a redo of @p clip's last length change returns to: the view scrolls after
	/// the change, so the step learns it after.
	virtual void set_undo_scroll_after(project::ClipAddress clip, int32_t x_scroll) = 0;
	/// @brief Learn @p command as what @p target is played by, or unlearn @p target when @p command is nullopt.
	/// @param input A note learned on a track's or kit row's input: what the learn takes from the note's cable.
	/// @return Whether it was taken: false where @p target's owner is not in the song.
	virtual bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	                   std::optional<LearnInput> input) = 0;
	/// @brief Transpose source @p source of the sound @p where names -- every sound row of the kit under affect-entire
	/// -- by @p menu_value, in cents. A sample source with ranges (not FM) takes it in its range: @p range, or range 0
	/// on a whole kit; any other source in its own transpose and cents.
	/// @return Whether it was taken.
	virtual bool write_source_transpose(const Addresses& where, uint8_t source, std::optional<uint16_t> range,
	                                    int32_t menu_value) = 0;
	/// @brief Set @p owner's gold-knob mode to @p mode.
	/// @return Whether it was taken: false for a mode past the last button or a track the song does not hold.
	virtual bool knob_mode(project::KnobModeOwner owner, uint8_t mode) = 0;
	/// @brief Set the view word @p word of @p chain's view key @p key (`currentFilterType`, `modFXCurrentParam`): which
	/// value its knobs edit.
	/// @return Whether it was taken: false for a chain, key or word that does not resolve.
	virtual bool chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) = 0;
	/// @brief Make @p controls the knob mappings of @p owner, as a push that cycles a knob's param leaves them.
	/// @return Whether it was taken.
	virtual bool write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) = 0;
	/// @brief Begin (or end) the stutter on @p focus's owner: a runtime command, nothing saved.
	/// @return Whether it was taken: false where @p focus names no owner with a chain.
	virtual bool stutter(const KnobFocus& focus, bool begin) = 0;
	/// @brief Set @p focus's params to their automated values at @p pos in its clip, as a held pad grabs them: a
	/// runtime command, nothing saved.
	/// @return Whether it was taken: false where @p focus names no owner.
	virtual bool grab_region(const KnobFocus& focus, uint32_t pos) = 0;
	/// @brief Write effect setting @p path of @p owner as the word @p value: a gesture's exact value, which a menu
	/// value may not name.
	/// @return Whether it was taken: false for an owner, path or value the effects refuse.
	virtual bool write_effect(const EffectOwner& owner, std::string_view path, const serialization::Value& value) = 0;
	/// @brief Enter (or leave, @p on false) choosing knob @p knob's CC on @p focus's MIDI track with the select
	/// encoder: a runtime command on the clip minder's state.
	/// @return Whether it was taken.
	virtual bool midi_cc_select(const KnobFocus& focus, int32_t knob, bool on) = 0;
	/// @brief Give every synth and sound kit row whose input is @p cable its velocity-to-level default, in every clip:
	/// the device menu's "for everything", a default set up for the user with no undo step.
	/// @return Whether it was taken.
	virtual bool velocity_to_level_everywhere(const ::MIDICable& cable) = 0;
	/// @brief Delete the automation of the param at @p address as a gold knob's shift+push does: while the arrangement
	/// records the clip, in its take.
	/// @return Whether it was taken.
	virtual bool delete_knob_automation(const project::ParamAddress& address) = 0;
	/// @brief Mark the instrument @p focus's knobs edit as edited by the user (a synth, a kit row's kit, a kit's own
	/// chain): a runtime flag no record holds, which a gesture that edited it sets.
	/// @return Whether it was taken.
	virtual bool instrument_edited(const KnobFocus& focus) = 0;
};

/// @brief A gesture request, as a RecordingSink keeps it.
struct GestureRequest {
	enum class Kind : uint8_t {
		TEXT,
		RANGES,
		RECOLOUR,
		CLEAR,
		MARKER,
		DEFAULT_INPUT_CHANNEL,
		ATTACH,
		RESIZE,
		LEARN,
		TRANSPOSE,
		KNOB_MODE,
		CHAIN_VIEW,
		CONTROLS,
		STUTTER,
		GRAB_REGION,
		EFFECT_WORD,
		MIDI_CC_SELECT,
		VELOCITY_EVERYWHERE,
		DELETE_KNOB_AUTOMATION,
		INSTRUMENT_EDITED,
	};
	Kind kind{Kind::TEXT};
	Target target{};
	Addresses where{};
	std::string text;
	project::SlotAddress slot{};
	uint8_t source{0};
	std::optional<uint16_t> range; ///< the range an ATTACH or TRANSPOSE names; none for a source's own transpose
	std::vector<instrument::RangeState> ranges;
	RangesCause cause{};
	project::ClipAddress clip{};
	int64_t value{0};
	ResizeKind resize{ResizeKind::END};
	int32_t old_length{0};
	std::optional<project::MappingTarget> mapping;
	std::optional<project::MidiCommand> command;
	std::optional<LearnInput> input;
	bool clear_automation{false};
	bool clear_sequence_and_mpe{false};
	std::optional<project::KnobModeOwner> knob_owner; ///< a KNOB_MODE's owner; its mode is @ref value
	project::ChainAddress chain{};                    ///< a CHAIN_VIEW's chain; its key, and its word in @ref text
	std::string key;
	std::optional<project::ControlsAddress> controls_address; ///< a CONTROLS' owner
	std::vector<project::ControllerMapping> controls;
	KnobFocus focus{}; ///< a STUTTER's or GRAB_REGION's focus; a GRAB_REGION's position is @ref value
	bool begin{false}; ///< a STUTTER's begin (true) or end; a MIDI_CC_SELECT's on
	std::optional<EffectOwner> effect_owner; ///< an EFFECT_WORD's owner; its path in @ref key, its word in @ref text
};

/// @brief A param write by address, as a RecordingSink keeps it.
struct ParamWrite {
	project::ParamAddress address;
	int32_t value{0};
	int32_t pos{0};
	int32_t length{0};
};

/// @brief A sink that records every request, in order.
class RecordingSink final : public WriteSink {
public:
	bool write(const WriteRequest& request) override;
	bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length) override;
	bool delete_automation(const project::ParamAddress& address) override;
	bool write_text(const Target& target, const Addresses& where, std::string_view text) override;
	bool write_ranges(project::SlotAddress slot, uint8_t source, std::span<const instrument::RangeState> ranges,
	                  RangesCause cause = {}) override;
	bool recolour_clip(project::ClipAddress clip, int32_t offset) override;
	bool clear_clip(project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) override;
	bool move_audio_clip_marker(project::ClipAddress clip, std::string_view path, int64_t value) override;
	void set_default_audio_input_channel(int32_t channel) override;
	bool attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range, std::string_view path) override;
	bool resize_audio_clip(project::ClipAddress clip, ResizeKind kind, int32_t new_length, int32_t old_length) override;
	void set_undo_scroll_after(project::ClipAddress clip, int32_t x_scroll) override;
	bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	           std::optional<LearnInput> input) override;
	bool write_source_transpose(const Addresses& where, uint8_t source, std::optional<uint16_t> range,
	                            int32_t menu_value) override;
	bool knob_mode(project::KnobModeOwner owner, uint8_t mode) override;
	bool chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) override;
	bool write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) override;
	bool stutter(const KnobFocus& focus, bool begin) override;
	bool grab_region(const KnobFocus& focus, uint32_t pos) override;
	bool write_effect(const EffectOwner& owner, std::string_view path, const serialization::Value& value) override;
	bool midi_cc_select(const KnobFocus& focus, int32_t knob, bool on) override;
	bool velocity_to_level_everywhere(const ::MIDICable& cable) override;
	bool delete_knob_automation(const project::ParamAddress& address) override;
	bool instrument_edited(const KnobFocus& focus) override;
	std::vector<WriteRequest> requests;
	std::vector<GestureRequest> gestures;
	std::vector<ParamWrite> param_writes;
	std::vector<project::ParamAddress> deletes;
	std::vector<std::pair<project::ClipAddress, int32_t>> undo_scrolls; ///< each set_undo_scroll_after, in order
};

} // namespace deluge::scenes::bind
