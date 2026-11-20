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

#include "definitions_cxx.hpp" // LFOType
#include "plugin-api/types/sync.h"

namespace deluge::synth {

/// @brief What an LFO saves: its shape, and how it follows the song's tempo (level NONE runs free).
struct LfoState {
	LFOType shape{LFOType::TRIANGLE};
	plugin::SyncType sync_type{plugin::SyncType::EVEN};
	plugin::SyncLevel sync_level{plugin::SyncLevel::NONE};
	bool operator==(const LfoState&) const = default;
};

} // namespace deluge::synth
