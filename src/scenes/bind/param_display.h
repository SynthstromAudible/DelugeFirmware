#pragma once

/// @file
/// @brief How a menu shows a param's stored word, per its row's mapping kind: the plugin API's menu mapping
/// (`plugin-api/types/param_mapping.h`), which reproduces legacy's value scaling (`gui/menu_item/value_scaling.cpp`).

#include "plugin-api/types/param.h"

#include <cstdint>
#include <utility>

namespace deluge::scenes::bind {

/// @brief The menu value of stored word @p stored in @p kind: the whole word range onto 0..50 (STANDARD), a word
/// that is never negative onto 0..50 (UNIPOLAR), the whole word range onto -25..25 (PAN).
/// @param kind   The param's mapping kind.
/// @param stored The stored word.
/// @return The menu value.
[[nodiscard]] inline int32_t menu_of(plugin::MappingKind kind, int32_t stored) {
	return plugin::to_menu(kind, stored);
}

/// @brief The stored word menu value @p menu stands for in @p kind: the inverse of menu_of, its ends the word's.
/// @param kind The param's mapping kind.
/// @param menu The menu value, within menu_range(kind).
/// @return The stored word.
[[nodiscard]] inline int32_t stored_of(plugin::MappingKind kind, int32_t menu) {
	return plugin::from_menu(kind, menu);
}

/// @brief The menu values @p kind shows: 0..50, or -25..25 for PAN.
/// @param kind The param's mapping kind.
/// @return The least and greatest menu values.
[[nodiscard]] inline std::pair<int32_t, int32_t> menu_range(plugin::MappingKind kind) {
	return {plugin::menu_min(kind), plugin::menu_max(kind)};
}

} // namespace deluge::scenes::bind
