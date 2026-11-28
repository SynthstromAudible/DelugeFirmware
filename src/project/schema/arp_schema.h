#pragma once

/// @file
/// @brief The arp record's schema: its settings by their rows, then every host value, `null` where the record holds
/// none.

#include "plugin/types/arp.h"
#include "project/arp_record.h"
#include "project/schema/row_fields.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace deluge::project::schema {
namespace arp_detail {

[[nodiscard]] consteval const plugin::SettingInfo& row(std::string_view path) {
	return *plugin::find_setting(plugin::kArpRows, path);
}

/// Host value @p I as an optional: held when its bit in host_held is set.
template <std::size_t I>
struct HostValue {
	static constexpr auto kBit = static_cast<uint16_t>(1U << I);
	static bool has_value(const void* record) { return (static_cast<const ArpRecord*>(record)->host_held & kBit) != 0; }
	static const void* value(const void* record) { return &static_cast<const ArpRecord*>(record)->host_values[I]; }
	static void* emplace(void* record) {
		auto& arp = *static_cast<ArpRecord*>(record);
		arp.host_held |= kBit;
		arp.host_values[I] = 0;
		return &arp.host_values[I];
	}
	static void reset(void* record) {
		auto& arp = *static_cast<ArpRecord*>(record);
		arp.host_held &= static_cast<uint16_t>(~kBit);
		arp.host_values[I] = 0;
	}
	static constexpr sch::OptionalOps kOps{&has_value, &value, &emplace, &reset};
};

/// The sync type is a choice whose words are the numbers the record stores (0, 10, 19): written as its word.
inline void write_sync_type(const void* member, const sch::ScalarRule& rule, sch::Writer& out) {
	const uint8_t type = *static_cast<const uint8_t*>(member);
	const std::string word = std::to_string(type);
	if (std::ranges::contains(rule.words, std::string_view{word})) {
		out.text(word);
	}
	else {
		out.integer(type); // a type with no word: reading it back fails at its path
	}
}
inline std::expected<void, sch::LoadError::Kind> read_sync_type(void* member, const sch::ScalarRule& rule,
                                                                sch::Reader& in, sch::WalkContext& context) {
	const std::optional<std::string> word = in.text();
	if (!word) {
		return std::unexpected(sch::LoadError::Kind::WRONG_TYPE);
	}
	if (!std::ranges::contains(rule.words, std::string_view{*word})) {
		context.note(sch::LoadNote::Kind::UNKNOWN_WORD);
		return {};
	}
	uint8_t type = 0;
	std::from_chars(word->data(), word->data() + word->size(), type);
	*static_cast<uint8_t*>(member) = type;
	return {};
}
inline constexpr sch::ScalarCodec kSyncTypeCodec{.form = "integer", .write = &write_sync_type, .read = &read_sync_type};

inline constexpr sch::FieldDesc kHostNumber = sch::value_scalar<int32_t>();

template <std::size_t... I>
[[nodiscard]] consteval std::array<sch::FieldDesc, sizeof...(I)> host_fields(std::index_sequence<I...> /*rows*/) {
	return {sch::FieldDesc{key_of(plugin::kArpHostRows[I]), sch::kSelf,
	                       sch::OptionalShape{&HostValue<I>::kOps, &kHostNumber}}...};
}

inline constexpr std::array kSettingFields{
    setting<&ArpRecord::mode>(row("arpeggiator/arpMode")),
    setting<&ArpRecord::octave_mode>(row("arpeggiator/octaveMode")),
    setting<&ArpRecord::note_mode>(row("arpeggiator/noteMode")),
    setting<&ArpRecord::mpe_velocity>(row("arpeggiator/mpeVelocity")),
    setting<&ArpRecord::num_octaves>(row("arpeggiator/numOctaves")),
    setting<&ArpRecord::step_repeat>(row("arpeggiator/stepRepeat")),
    setting<&ArpRecord::chord_type>(row("arpeggiator/chordType")),
    setting<&ArpRecord::sync_level>(row("arpeggiator/syncLevel")),
    sch::scalar_with<&ArpRecord::sync_type>("syncType", kSyncTypeCodec, {.words = plugin::kArpSyncTypeWords}),
    setting<&ArpRecord::randomizer_lock>(row("arpeggiator/randomizerLock")),
    setting<&ArpRecord::kit_arp>(row("arpeggiator/kitArp")),
};

inline constexpr auto kFields =
    sch::join(kSettingFields, host_fields(std::make_index_sequence<plugin::kArpHostRows.size()>{}));

inline constexpr std::array<std::string_view, 1> kDerived{"arpeggiator/preset"};
static_assert(sch::keys_unique(kFields));
static_assert(binds_each_row(kFields, plugin::kArpRows, kDerived), "every arp row but the preset is one field");

} // namespace arp_detail

inline constexpr sch::RecordDesc kArpDesc = sch::open_record<&ArpRecord::extras>("arp", arp_detail::kFields);
inline constexpr sch::Schema<ArpRecord> kArpSchema{&kArpDesc};

} // namespace deluge::project::schema
