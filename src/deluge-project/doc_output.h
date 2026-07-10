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

#include <cstdint>
#include <string>

namespace deluge::project {

/// The DOM-owned identity record for one engine Output (M3 owner-5, Output-base fields). Keyed by Output::id() in
/// OutputStructure, mirroring DocDrum/DrumStructure. Covers the fields common to every output type (instruments,
/// kits, audio outputs); per-instrument-type + Sound structure are later owner-5 slices. Rebuilt from the engine
/// at load + save.
struct DocOutput {
	std::string name;
	int16_t colour{0};
	uint8_t mod_knob_mode{0};
	bool muted_in_arrangement{false};
	bool soloing_in_arrangement{false};
	bool armed_for_recording{false};
};

} // namespace deluge::project
