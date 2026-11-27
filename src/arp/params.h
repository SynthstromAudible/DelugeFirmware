#pragma once

/// @file
/// @brief The arpeggiator's params: the sixteen values every arp plays by, one table for every owner. A param is named
///        `project::ParamTarget{ParamSpace::ARP, id}` on any owner, with its id from this table.

#include "plugin/types/param.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace deluge::arp {

/// @brief The arp's param ids, dense from 0 in its table's order.
namespace param {
inline constexpr plugin::ParamId kGate{0};
inline constexpr plugin::ParamId kRate{1};
inline constexpr plugin::ParamId kRhythm{2};
inline constexpr plugin::ParamId kSequenceLength{3};
inline constexpr plugin::ParamId kChordPolyphony{4};
inline constexpr plugin::ParamId kRatchetAmount{5};
inline constexpr plugin::ParamId kNoteProbability{6};
inline constexpr plugin::ParamId kBassProbability{7};
inline constexpr plugin::ParamId kSwapProbability{8};
inline constexpr plugin::ParamId kGlideProbability{9};
inline constexpr plugin::ParamId kReverseProbability{10};
inline constexpr plugin::ParamId kChordProbability{11};
inline constexpr plugin::ParamId kRatchetProbability{12};
inline constexpr plugin::ParamId kSpreadVelocity{13};
inline constexpr plugin::ParamId kSpreadGate{14};
inline constexpr plugin::ParamId kSpreadOctave{15};
} // namespace param

/// How many params an arp has.
inline constexpr std::size_t kParamCount = 16;

/// @brief Who holds an arp. Only a kit's own arp starts somewhere else: its gate starts fully open.
enum class Owner : uint8_t {
	SOUND, ///< a synth track's arp, or a sound kit row's
	KIT,   ///< a kit's own arp
	ROW,   ///< a MIDI or gate kit row's
	CLIP,  ///< a MIDI or CV clip's
};

/// @brief One row of the arp's table.
struct Param {
	plugin::ParamInfo info;
	/// The name an arp element's attribute and the setting path (`arpeggiator/<name>`) give it.
	std::string_view setting_name;
	/// Whether the engine reads the word as an unsigned number, word + 2^31 (every row but the gate and the rate). An
	/// arp element's attribute holds that unsigned number.
	bool unsigned_field{true};
};

namespace detail {
constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();

/// @brief A row whose engine field is unsigned (word + 2^31), named @p file_name in files and @p setting_name in an
///        arp element.
constexpr Param unsigned_row(plugin::ParamId id, std::string_view file_name, std::string_view setting_name,
                             int32_t default_value = kMin) {
	return {.info = {.id = id, .file_name = file_name, .default_value = default_value},
	        .setting_name = setting_name,
	        .unsigned_field = true};
}

/// @brief A row whose engine field is the signed word itself.
constexpr Param signed_row(plugin::ParamId id, std::string_view file_name, std::string_view setting_name) {
	return {.info = {.id = id, .file_name = file_name}, .setting_name = setting_name, .unsigned_field = false};
}
} // namespace detail

/// @brief The arp's params, by id.
inline constexpr std::array<Param, kParamCount> kParams{
    detail::signed_row(param::kGate, "arpGate", "gate"),
    detail::signed_row(param::kRate, "arpRate", "rate"),
    detail::unsigned_row(param::kRhythm, "rhythm", "rhythm"),
    detail::unsigned_row(param::kSequenceLength, "sequenceLength", "sequenceLength"),
    detail::unsigned_row(param::kChordPolyphony, "chordPolyphony", "chordPolyphony"),
    detail::unsigned_row(param::kRatchetAmount, "ratchetAmount", "ratchetAmount"),
    detail::unsigned_row(param::kNoteProbability, "noteProbability", "noteProbability", detail::kMax),
    detail::unsigned_row(param::kBassProbability, "bassProbability", "bassProbability"),
    detail::unsigned_row(param::kSwapProbability, "swapProbability", "swapProbability"),
    detail::unsigned_row(param::kGlideProbability, "glideProbability", "glideProbability"),
    detail::unsigned_row(param::kReverseProbability, "reverseProbability", "reverseProbability"),
    detail::unsigned_row(param::kChordProbability, "chordProbability", "chordProbability"),
    detail::unsigned_row(param::kRatchetProbability, "ratchetProbability", "ratchetProbability"),
    detail::unsigned_row(param::kSpreadVelocity, "spreadVelocity", "spreadVelocity"),
    detail::unsigned_row(param::kSpreadGate, "spreadGate", "spreadGate"),
    detail::unsigned_row(param::kSpreadOctave, "spreadOctave", "spreadOctave"),
};

/// @brief The rows' descriptors alone, for a host that reads params by descriptor.
inline constexpr std::array<plugin::ParamInfo, kParamCount> kParamInfos = [] {
	std::array<plugin::ParamInfo, kParamCount> infos{};
	for (std::size_t i = 0; i < kParamCount; ++i) {
		infos[i] = kParams[i].info;
	}
	return infos;
}();

/// @brief The id of the row at @p index.
/// @param index A row index, below kParamCount.
[[nodiscard]] constexpr plugin::ParamId id_at(std::size_t index) {
	return plugin::ParamId{static_cast<uint32_t>(index)};
}

/// @brief The row of @p id.
/// @param id An arp param id, below kParamCount.
[[nodiscard]] constexpr const Param& row(plugin::ParamId id) {
	return kParams[id.value];
}

/// @brief The word an arp held by @p owner starts with at @p id.
/// @param owner Who holds the arp.
/// @param id    An arp param id, below kParamCount.
[[nodiscard]] constexpr int32_t default_value(Owner owner, plugin::ParamId id) {
	if (owner == Owner::KIT && id == param::kGate) {
		return detail::kMax;
	}
	return row(id).info.default_value;
}

/// @brief The id of the row whose file name is @p name.
/// @return The id, or nullopt for a name that is not an arp param's.
[[nodiscard]] constexpr std::optional<plugin::ParamId> find_by_file_name(std::string_view name) {
	for (const Param& p : kParams) {
		if (p.info.file_name == name) {
			return p.info.id;
		}
	}
	return std::nullopt;
}

/// @brief The id of the row whose setting name (an arp element's attribute) is @p name.
/// @return The id, or nullopt for a name that is not an arp param's.
[[nodiscard]] constexpr std::optional<plugin::ParamId> find_by_setting_name(std::string_view name) {
	for (const Param& p : kParams) {
		if (p.setting_name == name) {
			return p.info.id;
		}
	}
	return std::nullopt;
}

/// @brief Whether @p id is the rhythm, whose popup shows the rhythm's name rather than a number.
[[nodiscard]] constexpr bool is_rhythm(plugin::ParamId id) {
	return id == param::kRhythm;
}

/// @brief The word an arp element's attribute @p field holds for @p id: the unsigned rows hold word + 2^31.
[[nodiscard]] constexpr int32_t word_of_field(plugin::ParamId id, int32_t field) {
	return row(id).unsigned_field ? static_cast<int32_t>(static_cast<uint32_t>(field) ^ 0x80000000U) : field;
}

/// @brief The attribute an arp element holds for @p id's word @p word: the inverse of word_of_field.
[[nodiscard]] constexpr int32_t field_of_word(plugin::ParamId id, int32_t word) {
	return word_of_field(id, word);
}

} // namespace deluge::arp
