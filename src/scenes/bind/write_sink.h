#pragma once

/// @file
/// @brief Where a menu's writes go: requests a host applies as Edits, so the scenes name no Edit and no engine object.

#include "controls/sink.h"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/fx_chain.h"
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

namespace deluge::scenes::bind {

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
};

/// @brief A gesture request, as a RecordingSink keeps it.
struct GestureRequest {
	enum class Kind : uint8_t {
		TEXT,
		DEFAULT_INPUT_CHANNEL,
		KNOB_MODE,
		CHAIN_VIEW,
		CONTROLS,
		STUTTER,
		GRAB_REGION,
		EFFECT_WORD,
		MIDI_CC_SELECT,
		DELETE_KNOB_AUTOMATION,
		INSTRUMENT_EDITED,
	};
	Kind kind{Kind::TEXT};
	Target target{};
	Addresses where{};
	std::string text;
	int64_t value{0};
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
	bool knob_mode(project::KnobModeOwner owner, uint8_t mode) override;
	bool chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) override;
	bool write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) override;
	bool stutter(const KnobFocus& focus, bool begin) override;
	bool grab_region(const KnobFocus& focus, uint32_t pos) override;
	bool write_effect(const EffectOwner& owner, std::string_view path, const serialization::Value& value) override;
	bool midi_cc_select(const KnobFocus& focus, int32_t knob, bool on) override;
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
