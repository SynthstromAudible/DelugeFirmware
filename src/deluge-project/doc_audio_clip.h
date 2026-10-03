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

#include "engine-api/types/interpolation_mode.h"
#include "engine-api/types/voice_priority.h"

#include <cstdint>
#include <string>

namespace deluge::project {

/// The DOM-owned descriptor record for one AudioClip (M3 owner-4). Keyed by Clip::id() in AudioClipStructure,
/// separate from DocClip (which is shared with InstrumentClip). The sample is referenced by portable PATH + ranges,
/// never a pointer. Enums stored as underlying values. Rebuilt from the engine AudioClip at load + save.
/// Member initializers hold the AudioClip's construction defaults (owner-4 is model-first: the ctor derives the
/// engine cache from a freshly-created record, so these values are the single source of the descriptor defaults).
/// interpolation_mode=SMOOTH + pitch_and_speed_independent=true + attack=INT_MIN + voice_priority=MEDIUM +
/// overdubs_should_clone=true mirror the old scattered AudioClip ctor assignments.
struct DocAudioClip {
	std::string file_path;
	uint64_t start_pos{0};
	uint64_t end_pos{0};
	int16_t transpose{0};
	int8_t cents{0};
	engine::InterpolationMode interpolation_mode{engine::InterpolationMode::SMOOTH};
	bool pitch_and_speed_independent{true};
	bool reversed{false};
	int32_t attack{-2147483648};
	engine::VoicePriority voice_priority{engine::VoicePriority::MEDIUM};
	bool overdubs_should_clone{true};
};

} // namespace deluge::project
