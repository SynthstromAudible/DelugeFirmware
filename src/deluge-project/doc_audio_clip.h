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

#include <cstdint>
#include <string>

namespace deluge::project {

/// The DOM-owned descriptor record for one AudioClip (M3 owner-4). Keyed by Clip::id() in AudioClipStructure,
/// separate from DocClip (which is shared with InstrumentClip). The sample is referenced by portable PATH + ranges,
/// never a pointer. Enums stored as underlying values. Rebuilt from the engine AudioClip at load + save.
struct DocAudioClip {
	std::string file_path;
	uint64_t start_pos{0};
	uint64_t end_pos{0};
	int16_t transpose{0};
	int8_t cents{0};
	uint8_t interpolation_mode{0}; ///< SampleControls::interpolationMode underlying
	bool pitch_and_speed_independent{false};
	bool reversed{false};
	int32_t attack{0};
	uint8_t voice_priority{0}; ///< VoicePriority underlying
	bool overdubs_should_clone{false};
};

} // namespace deluge::project
