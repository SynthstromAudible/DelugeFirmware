/*
 * Copyright © 2024 Synthstrom Audible Limited
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
#include <cstdint>

namespace deluge::project {

/// @brief The reverb's algorithm, in the order a song file numbers it.
enum class ReverbModel : uint8_t { FREEVERB, MUTABLE, DIGITAL };

/// @brief A song's reverb send settings and algorithm.
///
/// A component of deluge::project::Song; fields are accessed as `project.reverb.room_size`.
struct ReverbSettings {
	float room_size{0};
	float hpf{0};
	float lpf{0};
	float damping{0};
	float width{0};
	int32_t pan{0};
	int32_t sidechain_volume{0};
	int32_t sidechain_shape{0};
	int32_t sidechain_attack{0};
	int32_t sidechain_release{0};
	engine::SyncLevel sidechain_sync{};
	ReverbModel model{ReverbModel::FREEVERB};

	bool operator==(const ReverbSettings&) const = default;
};

} // namespace deluge::project
