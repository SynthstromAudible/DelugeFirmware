#pragma once

/// @file
/// @brief Where a menu's writes go: requests a host applies as Edits, so the scenes name no Edit and no engine object.

#include "controls/sink.h"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/fx_chain.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "deluge-project/param_address.h"
#include "instruments/deluge/cables.h"
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

/// @brief Whose effect settings a write names: a sound's slot, or a chain.
using controls::EffectOwner;

/// @brief Takes a menu's writes. The app's sink applies each as the target's Edit; a spec's records them.
class WriteSink {
public:
	virtual ~WriteSink() = default;
	/// @brief Apply, or record, @p request.
	/// @return Whether it was taken: false where the app refused it, which left the song as it was.
	virtual bool write(const WriteRequest& request) = 0;
	/// @brief Delete the automation of the param at @p address.
	/// @return Whether it was taken.
	virtual bool delete_automation(const project::ParamAddress& address) = 0;
	/// @brief Write @p text to the TEXT row @p target names (a MODEL target: a clip's, track's or lane's name, a MIDI
	/// track's device definition file) on the owner @p where names.
	/// @return Whether it was taken.
	virtual bool write_text(const Target& target, const Addresses& where, std::string_view text) = 0;
	/// @brief Make @p channel (an index of the input channel words) the input a new audio track starts on: the song's
	/// unsaved default, which the audio-source menu sets to whatever it last picked.
	virtual void set_default_audio_input_channel(int32_t channel) = 0;
	/// @brief Learn @p command as what @p target is played by, or unlearn @p target when @p command is nullopt. Any
	/// unlearn marks the current instrument edited, as `learn_knob`'s learn does; the menu is the only caller and
	/// unlearns only a param's knob.
	/// @param input A note learned on a track's or kit row's input: what the learn takes from the note's cable.
	/// @return Whether it was taken: false where @p target's owner is not in the song.
	virtual bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	                   std::optional<LearnInput> input) = 0;
	/// @brief Learn a knob onto @p target's param -- `MenuItemWithCCLearning::learnKnob`: a mod knob when
	/// @p midi_channel is 16 or more, in the current output's knob mode; otherwise a MIDI knob heard on @p cable. A
	/// learned knob marks the current instrument edited.
	/// @param target       The owner and the param.
	/// @param cable        The cable a MIDI knob was heard on; null for a mod knob.
	/// @param which_knob   The mod knob (0 lower, 1 upper), or the CC number (128 for pitch bend).
	/// @param midi_channel The channel a MIDI knob was heard on, or 16 or more for a mod knob.
	/// @return Whether the knob was learned.
	virtual bool learn_knob(const project::OwnerParamTarget& target, ::MIDICable* cable, int32_t which_knob,
	                        int32_t midi_channel) = 0;
	/// @brief Give every synth and sound kit row whose input is @p cable its velocity-to-level default, in every clip:
	/// the device menu's "for everything", a default set up for the user with no undo step.
	/// @return Whether it was taken.
	virtual bool velocity_to_level_everywhere(const ::MIDICable& cable) = 0;
};

/// @brief A gesture request, as a RecordingSink keeps it.
struct GestureRequest {
	enum class Kind : uint8_t {
		TEXT,
		DEFAULT_INPUT_CHANNEL,
		LEARN,
		LEARN_KNOB,
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
	int64_t value{0};
	std::optional<project::MappingTarget> mapping;
	std::optional<project::MidiCommand> command;
	std::optional<LearnInput> input;
	std::optional<project::KnobModeOwner> knob_owner; ///< a KNOB_MODE's owner; its mode is @ref value
	project::ChainAddress chain{};                    ///< a CHAIN_VIEW's chain; its key, and its word in @ref text
	std::string key;
	std::optional<project::ControlsAddress> controls_address; ///< a CONTROLS' owner
	std::vector<project::ControllerMapping> controls;
	KnobFocus focus{}; ///< a STUTTER's or GRAB_REGION's focus; a GRAB_REGION's position is @ref value
	bool begin{false}; ///< a STUTTER's begin (true) or end; a MIDI_CC_SELECT's on
	std::optional<EffectOwner> effect_owner; ///< an EFFECT_WORD's owner; its path in @ref key, its word in @ref text
	const ::MIDICable* cable{nullptr}; ///< a LEARN_KNOB's cable; its target in @ref mapping, its knob in @ref value
	int32_t midi_channel{0};           ///< a LEARN_KNOB's channel
};

/// @brief A param write by address, as a RecordingSink keeps it.
struct RecordedParamWrite {
	project::ParamAddress address;
	int32_t value{0};
	int32_t pos{0};
	int32_t length{0};
	controls::Resolution resolution{controls::Resolution::KNOB};
};

/// @brief A sink that records every request, in order.
class RecordingSink final : public WriteSink, public controls::Sink {
public:
	bool write(const WriteRequest& request) override;
	bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                 controls::Resolution resolution) override;
	void learned_written(const project::ParamAddress& address, int32_t knob_pos) override;
	std::optional<uint32_t> take_for(uint32_t output_id) override;
	bool make_param(const project::ParamAddress& address) override;
	void knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) override;
	bool delete_automation(const project::ParamAddress& address) override;
	bool write_text(const Target& target, const Addresses& where, std::string_view text) override;
	void set_default_audio_input_channel(int32_t channel) override;
	bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	           std::optional<LearnInput> input) override;
	bool learn_knob(const project::OwnerParamTarget& target, ::MIDICable* cable, int32_t which_knob,
	                int32_t midi_channel) override;
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
	std::vector<RecordedParamWrite> param_writes;
	std::vector<project::ParamAddress> deletes;
	std::vector<std::pair<project::ParamAddress, int32_t>> learned_follows; ///< each learned_written, in order
	std::vector<uint32_t> take_requests;                                    ///< each output take_for was asked about
	std::vector<project::ParamAddress> made;                                ///< each param make_param was asked for
	std::vector<std::pair<project::OwnerParamTarget, project::MidiCommand>> absolutes; ///< each knob_absolute
	std::optional<uint32_t> take_answer;                                               ///< what take_for answers
};

} // namespace deluge::scenes::bind
