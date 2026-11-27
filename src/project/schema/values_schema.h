#pragma once

/// @file
/// @brief The clip values' and the sound routing's schemas, and a clip value found by its path.

#include "project/clip_values.h"
#include "project/instrument_slot.h"
#include "project/schema/arp_schema.h"
#include "serialization/schema/lookup.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::project::schema {
namespace values_detail {

inline constexpr sch::FieldDesc kNumber = sch::value_scalar<int32_t>();
inline constexpr sch::FieldDesc kArp = sch::value_record(kArpDesc);

inline constexpr std::array kClipValuesFields{
    sch::optional<&ClipValues::midi_bank>("midiBank", kNumber),
    sch::optional<&ClipValues::midi_sub>("midiSub", kNumber),
    sch::optional<&ClipValues::midi_pgm>("midiPGM", kNumber),
    sch::optional<&ClipValues::bend_range>("bendRange", kNumber),
    sch::optional<&ClipValues::bend_range_mpe>("bendRangeMPE", kNumber),
    sch::optional<&ClipValues::arp>("arp", kArp),
};
static_assert(sch::keys_unique(kClipValuesFields));

inline constexpr sch::FieldDesc kChannel = sch::value_scalar<uint8_t>({.min = 0, .max = 15});
inline constexpr sch::FieldDesc kNote = sch::value_scalar<uint8_t>({.min = 0, .max = 127});

inline constexpr std::array kSoundRoutingFields{
    sch::scalar<&SoundRouting::side_chain_send>("sideChainSend", {.min = 0, .max = SoundRouting::kFullSend}),
    sch::optional<&SoundRouting::midi_channel>("midiChannel", kChannel),
    sch::optional<&SoundRouting::midi_note>("midiNote", kNote),
};
static_assert(sch::keys_unique(kSoundRoutingFields));

} // namespace values_detail

inline constexpr sch::RecordDesc kClipValuesDesc =
    sch::open_record<&ClipValues::extras>("clipValues", values_detail::kClipValuesFields);
inline constexpr sch::Schema<ClipValues> kClipValuesSchema{&kClipValuesDesc};

inline constexpr sch::RecordDesc kSoundRoutingDesc =
    sch::open_record<&SoundRouting::extras>("soundRouting", values_detail::kSoundRoutingFields);
inline constexpr sch::Schema<SoundRouting> kSoundRoutingSchema{&kSoundRoutingDesc};

} // namespace deluge::project::schema

namespace deluge::project {

/// @brief The clip value at @p path (`midiBank`, `midiSub`, `midiPGM`, `bendRange` or `bendRangeMPE`).
/// @param values The clip's values (const or not).
/// @param path   The value's path, which is its key.
/// @return The value's slot; nullptr for any other path.
template <typename Values>
[[nodiscard]] auto* value_at(Values& values, std::string_view path) {
	return serialization::schema::member_at<std::optional<int32_t>>(schema::kClipValuesSchema, values, path);
}

/// @brief The value a clip plays at @p path: its own when it holds one, else its track's.
/// @param values      The clip's values.
/// @param path        The value's path.
/// @param track_value The track's value.
/// @return The value.
[[nodiscard]] inline int32_t resolve(const ClipValues& values, std::string_view path, int32_t track_value) {
	const std::optional<int32_t>* slot = value_at(values, path);
	return slot != nullptr && slot->has_value() ? **slot : track_value;
}

} // namespace deluge::project
