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

/// @file
/// @brief The `SideChain` conversions the reverb's sidechain page shares, and the exported root span. The `Node`
///        literals are in `sound_sidechain.h`.

#include "scenes/menu/nodes/sound_sidechain.h"

#include "definitions_cxx.hpp"
#include "modulation/sync.h"
#include "plugin-api/types/rate_tables.h"
#include "plugin-api/types/sync.h"

#include <algorithm>

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief `getLookupIndexFromValue` (`util/functions.cpp:1428-1443`), transcribed: `util/` is outside
///        `src/scenes`' include allow-list. The `bestDistance` comparison is legacy's own signed/unsigned
///        mix, reproduced exactly rather than "fixed": changing it would change which index a given
///        stored rate shows as.
int32_t lookup_index(int32_t value, const int32_t* table, int32_t max_index) {
	uint32_t best_distance = 0xFFFFFFFF;
	int32_t closest = 0;
	for (int32_t i = 0; i <= max_index; i++) {
		int64_t distance = static_cast<int64_t>(value) - static_cast<int64_t>(table[i]);
		if (distance < 0) {
			distance = -distance;
		}
		if (distance < best_distance) {
			best_distance = static_cast<uint32_t>(distance);
			closest = i;
		}
	}
	return closest;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The shared sidechain conversions

int32_t sidechain_attack_to_menu(int32_t attack) {
	// `sidechain::Attack::readCurrentValue` (`sidechain/attack.h`).
	return lookup_index(attack >> 2, deluge::plugin::kSidechainAttackRates, 50);
}
int32_t sidechain_attack_from_menu(int32_t menu_value) {
	return deluge::plugin::kSidechainAttackRates[std::clamp<int32_t>(menu_value, 0, 50)] << 2; // `attack.h`'s write
}

int32_t sidechain_release_to_menu(int32_t release) {
	// `sidechain::Release::readCurrentValue` (`sidechain/release.h`).
	return lookup_index(release >> 3, deluge::plugin::kSidechainReleaseRates, 50);
}
int32_t sidechain_release_from_menu(int32_t menu_value) {
	return deluge::plugin::kSidechainReleaseRates[std::clamp<int32_t>(menu_value, 0, 50)] << 3; // `release.h`'s write
}

int32_t sidechain_sync_to_menu(deluge::plugin::SyncType type, deluge::plugin::SyncLevel level) {
	// `sidechain::Sync::readCurrentValue` (`sidechain/sync.h`) via
	// `SyncLevel::syncTypeAndLevelToMenuOption` (`gui/menu_item/sync_level.cpp`).
	return static_cast<int32_t>(type) + (static_cast<int32_t>(level) - (type != SYNC_TYPE_EVEN ? 1 : 0));
}

namespace {
constexpr const Node* kRoots[] = {&sidechain};
} // namespace

const std::span<const Node* const> kSoundSidechainChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
