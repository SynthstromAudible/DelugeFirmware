#pragma once

/// @file
/// @brief Schema fields made from setting rows: a setting is written under the last part of its path, as its row
/// says (a choice as its word, an integer within the row's range, a bool as true or false).

#include "plugin/types/setting.h"
#include "serialization/schema/fields.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace deluge::project::schema {

namespace sch = deluge::serialization::schema;

/// @brief The key @p row's setting is written under: its path's last part.
/// @param row The setting's row.
/// @return The key, e.g. `numOctaves` for `arpeggiator/numOctaves`.
[[nodiscard]] constexpr std::string_view key_of(const plugin::SettingInfo& row) {
	const std::size_t slash = row.path.rfind('/');
	return slash == std::string_view::npos ? row.path : row.path.substr(slash + 1);
}

/// @brief The rule @p row's setting is written and checked by.
/// @param row The setting's row.
/// @return A choice's words, an integer's range; nothing for any other kind.
[[nodiscard]] constexpr sch::ScalarRule rule_of(const plugin::SettingInfo& row) {
	switch (row.kind) {
	case plugin::SettingKind::CHOICE:
		return {.words = row.choices};
	case plugin::SettingKind::INTEGER:
		return {.min = row.min, .max = row.max};
	default:
		return {};
	}
}

/// @brief The field of the member @p Member, as @p row describes its setting.
/// @param row The setting's row.
/// @return The field.
template <auto Member>
[[nodiscard]] consteval sch::FieldDesc setting(const plugin::SettingInfo& row) {
	return sch::scalar<Member>(key_of(row), rule_of(row));
}

/// @brief Whether every row of @p rows, but those whose path @p skip lists, has exactly one field of @p fields under
/// its key.
/// @param fields A record's fields.
/// @param rows   Its setting rows.
/// @param skip   The paths of rows the record does not hold (a derived setting).
/// @return True when each row is bound once.
[[nodiscard]] consteval bool binds_each_row(std::span<const sch::FieldDesc> fields,
                                            std::span<const plugin::SettingInfo> rows,
                                            std::span<const std::string_view> skip = {}) {
	for (const plugin::SettingInfo& row : rows) {
		if (std::ranges::contains(skip, row.path)) {
			continue;
		}
		if (std::ranges::count(fields, key_of(row), &sch::FieldDesc::key) != 1) {
			return false;
		}
	}
	return true;
}

} // namespace deluge::project::schema
