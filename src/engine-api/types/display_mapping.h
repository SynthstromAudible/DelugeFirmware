/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "engine-api/types/sync.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::engine {

/// @brief How a menu's value relates to the stored value(s), beyond UserScale's linear map.
enum class DisplayMappingKind : uint8_t {
	LINEAR,        ///< UserScale (and its sentinels)
	CLAMP_HIGH,    ///< LINEAR, but a stored value above the user scale's stored_max shows as `clamp_to`
	CHOICE_ORDER,  ///< a CHOICE whose menu lists the choices in another order (menu index i → choice order[i])
	INVERTED_BOOL, ///< a BOOL whose menu shows !stored (interpolation: menu 0 = LINEAR = stored 1)
	COMBINED_SYNC, ///< one menu value 0..27 (or the row's user range) over a syncLevel row and its partner syncType row
	TRANSPOSE_CENTS, ///< one menu value transpose*100+cents over a transpose row and its partner cents row
	PHASE_DEGREES,   ///< a retrigger phase: −1 (0xFFFFFFFF) ↔ menu −1 (free), else degrees 0..360 ↔ phase/11930464
	TABLE,           ///< a lookup table: `table[i] << table_shift` is the stored value for menu step i
};

/// @brief A setting row's menu mapping. Only the fields its kind names are read.
struct DisplayMapping {
	DisplayMappingKind kind{DisplayMappingKind::LINEAR};
	std::string_view partner{};       ///< COMBINED_SYNC / TRANSPOSE_CENTS: the second row's path
	std::span<const uint8_t> order{}; ///< CHOICE_ORDER: the choice index for each menu index
	std::span<const int32_t> table{}; ///< TABLE: the table value for each menu step
	uint8_t table_shift{0};           ///< TABLE: the left shift from a table value to the stored value
	int32_t clamp_to{0};              ///< CLAMP_HIGH: what a stored value above the user range shows as
};

/// @name Each two-row or non-linear kind's conversion, on bare values.
/// A CHOICE row's stored value is its choice index, so a sync type is 0 (even), 1 (triplet) or 2 (dotted).
/// @{

/// @brief The greatest combined sync menu value: the ninth dotted level.
inline constexpr int32_t kMaxCombinedSync = std::to_underlying(SyncType::DOTTED) + kMaxSyncLevel - 1;

/// @brief The sync types in their rows' choice order.
inline constexpr std::array<SyncType, 3> kSyncTypeChoices{SyncType::EVEN, SyncType::TRIPLET, SyncType::DOTTED};

/// @brief The combined sync menu value for @p level of the type at choice index @p type_choice.
/// @param level       The sync level, 0 (off) to kMaxSyncLevel.
/// @param type_choice The sync type's choice index; one outside 0..2 counts as even.
/// @return `type + level - 1` for a triplet or dotted type, else @p level (so a triplet or dotted level 0 lands on the
///         even list's last value, as the legacy menus place it).
[[nodiscard]] constexpr int32_t combined_sync_menu(int32_t level, int32_t type_choice) {
	const bool in_range = type_choice >= 0 && type_choice < static_cast<int32_t>(kSyncTypeChoices.size());
	const SyncType type = in_range ? kSyncTypeChoices[static_cast<std::size_t>(type_choice)] : SyncType::EVEN;
	return std::to_underlying(type) + level - (type != SyncType::EVEN ? 1 : 0);
}

/// @brief The sync level and type choice index for combined sync menu value @p menu.
/// @param menu A combined value, 0 to kMaxCombinedSync.
/// @return {level, type choice index}.
[[nodiscard]] constexpr std::pair<int32_t, int32_t> combined_sync_stored(int32_t menu) {
	constexpr int32_t kTriplet = std::to_underlying(SyncType::TRIPLET);
	constexpr int32_t kDotted = std::to_underlying(SyncType::DOTTED);
	if (menu < kTriplet) {
		return {menu, 0};
	}
	if (menu < kDotted) {
		return {menu - kTriplet + 1, 1};
	}
	return {menu - kDotted + 1, 2};
}

/// @brief The transpose menu's value for @p transpose semitones and @p cents.
/// @param transpose Semitones.
/// @param cents     Cents, −50..50.
/// @return `transpose * 100 + cents`.
[[nodiscard]] constexpr int32_t transpose_cents_menu(int32_t transpose, int32_t cents) {
	return transpose * 100 + cents;
}

/// @brief Transpose menu value @p menu split into semitones and cents, the cents rounded to the nearest semitone's
/// −50..49 (a half-semitone rounds up).
/// @param menu The menu value in cents; it must exceed −25600.
/// @return {transpose, cents}.
[[nodiscard]] constexpr std::pair<int32_t, int32_t> transpose_cents_stored(int32_t menu) {
	constexpr int32_t kBias = 25600; // keeps the division's operand positive, so it rounds as a floor
	const int32_t biased = menu + kBias;
	const int32_t semitones = (biased + 50) / 100;
	return {semitones - kBias / 100, biased - semitones * 100};
}

/// @brief A retrigger phase's stored value for "free-running": 0xFFFFFFFF as a signed integer.
inline constexpr int32_t kPhaseFree = -1;
/// @brief The menu's value for a free-running phase.
inline constexpr int32_t kPhaseFreeMenu = -1;
/// @brief The phase step of one degree: 2^32 / 360, truncated.
inline constexpr uint32_t kPhasePerDegree = 11930464;
/// @brief The greatest degree the retrigger phase menu offers.
inline constexpr int32_t kMaxPhaseDegrees = 360;

/// @brief The retrigger phase menu's degrees for stored phase @p stored.
/// @param stored The 32-bit phase as a signed integer.
/// @return kPhaseFreeMenu for kPhaseFree, else the whole degrees the unsigned phase spans.
[[nodiscard]] constexpr int32_t phase_degrees_menu(int32_t stored) {
	if (stored == kPhaseFree) {
		return kPhaseFreeMenu;
	}
	return static_cast<int32_t>(static_cast<uint32_t>(stored) / kPhasePerDegree);
}

/// @brief The stored phase for menu degrees @p menu.
/// @param menu kPhaseFreeMenu or below for free-running, else degrees.
/// @return kPhaseFree, or `menu * kPhasePerDegree` as a signed integer.
[[nodiscard]] constexpr int32_t phase_degrees_stored(int32_t menu) {
	if (menu < 0) {
		return kPhaseFree;
	}
	return static_cast<int32_t>(static_cast<uint32_t>(menu) * kPhasePerDegree);
}

/// @brief The menu step whose table value is closest to @p stored, the first on a tie.
/// @param stored The stored value.
/// @param table  The table values, one per menu step.
/// @param shift  The left shift from a table value to the stored value.
/// @return The closest step's index, or 0 for an empty table.
[[nodiscard]] constexpr int32_t table_menu(int32_t stored, std::span<const int32_t> table, uint8_t shift) {
	const int64_t value = stored >> shift;
	int64_t best = -1;
	int32_t closest = 0;
	for (std::size_t i = 0; i < table.size(); ++i) {
		int64_t distance = value - table[i];
		if (distance < 0) {
			distance = -distance;
		}
		if (best < 0 || distance < best) {
			best = distance;
			closest = static_cast<int32_t>(i);
		}
	}
	return closest;
}

/// @}

} // namespace deluge::engine
