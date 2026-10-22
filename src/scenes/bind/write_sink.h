#pragma once

/// @file
/// @brief Where a menu's writes go: requests a host applies as Edits, so the scenes name no Edit and no engine object.

#include "deluge-project/param_address.h"
#include "engines/deluge/cables.h"
#include "engines/deluge/sound_state.h"
#include "scenes/bind/target.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
};

/// @brief A gesture request, as a RecordingSink keeps it.
struct GestureRequest {
	enum class Kind : uint8_t { TEXT, RANGES, RECOLOUR, CLEAR, MARKER, DEFAULT_INPUT_CHANNEL };
	Kind kind{Kind::TEXT};
	Target target{};
	Addresses where{};
	std::string text;
	project::SlotAddress slot{};
	uint8_t source{0};
	std::vector<instrument::RangeState> ranges;
	RangesCause cause{};
	project::ClipAddress clip{};
	int64_t value{0};
	bool clear_automation{false};
	bool clear_sequence_and_mpe{false};
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
	std::vector<WriteRequest> requests;
	std::vector<GestureRequest> gestures;
	std::vector<ParamWrite> param_writes;
	std::vector<project::ParamAddress> deletes;
};

} // namespace deluge::scenes::bind
