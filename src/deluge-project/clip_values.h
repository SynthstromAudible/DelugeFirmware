#pragma once

/// @file
/// @brief A clip's value layer: the values a clip holds for settings a track also has, resolved clip first.
///
/// Each value is optional. A clip that holds one plays it; one that does not plays its track's. Today the track
/// holds none of these, so a caller passes the setting's default as the track's value.

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::project {

/// @brief A clip's own values, by setting path.
struct ClipValues {
	std::optional<int32_t> midi_bank;      ///< `midiBank`, 0..127, 128 = none
	std::optional<int32_t> midi_sub;       ///< `midiSub`, 0..127, 128 = none
	std::optional<int32_t> midi_pgm;       ///< `midiPGM`, 0..127, 128 = none
	std::optional<int32_t> bend_range;     ///< `bendRange`, semitones, the whole clip's bend
	std::optional<int32_t> bend_range_mpe; ///< `bendRangeMPE`, semitones, a finger's bend

	bool operator==(const ClipValues&) const = default;
};

/// @brief The value @p values holds at @p path.
/// @param values The clip's values (const or not).
/// @param path   A path of the clip-value group.
/// @return Its slot, or nullptr for a path the layer does not hold.
template <typename Values>
[[nodiscard]] auto* value_at(Values& values, std::string_view path) {
	using Slot = decltype(&values.midi_bank);
	if (path == "midiBank") {
		return &values.midi_bank;
	}
	if (path == "midiSub") {
		return &values.midi_sub;
	}
	if (path == "midiPGM") {
		return &values.midi_pgm;
	}
	if (path == "bendRange") {
		return &values.bend_range;
	}
	if (path == "bendRangeMPE") {
		return &values.bend_range_mpe;
	}
	return Slot{nullptr};
}

/// @brief The value a clip plays at @p path: its own when it holds one, else its track's.
/// @param values      The clip's values.
/// @param path        A path of the clip-value group.
/// @param track_value The track's value at @p path.
/// @return The resolved value.
[[nodiscard]] inline int32_t resolve(const ClipValues& values, std::string_view path, int32_t track_value) {
	const std::optional<int32_t>* slot = value_at(values, path);
	return slot != nullptr && slot->has_value() ? **slot : track_value;
}

} // namespace deluge::project
