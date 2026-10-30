/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "plugin-api/types/interpolation_mode.h"
#include "plugin-api/types/voice_priority.h"

#include <cstdint>
#include <string>

namespace deluge::project {

/// @brief The DOM-owned descriptor record for one AudioClip.
///
/// Keyed by Clip::id() in AudioClipStructure, separate from DocClip (which is shared with InstrumentClip). The
/// sample is referenced by portable PATH + ranges, never a pointer. Enums are stored as underlying values. Rebuilt
/// from the engine AudioClip at load and save.
///
/// The member initializers are the AudioClip's construction defaults: the engine clip derives its cache from a
/// freshly-created record, so these values are the single source of the descriptor defaults
/// (interpolation_mode SMOOTH, pitch_and_speed_independent true, attack INT_MIN, voice_priority MEDIUM,
/// overdubs_should_clone true).
struct DocAudioClip {
	std::string file_path;
	uint64_t start_pos{0};
	uint64_t end_pos{0};
	int16_t transpose{0};
	int8_t cents{0};
	plugin::InterpolationMode interpolation_mode{plugin::InterpolationMode::SMOOTH};
	bool pitch_and_speed_independent{true};
	bool reversed{false};
	int32_t attack{-2147483648};
	plugin::VoicePriority voice_priority{plugin::VoicePriority::MEDIUM};
	bool overdubs_should_clone{true};

	bool operator==(const DocAudioClip&) const = default;
};

} // namespace deluge::project
