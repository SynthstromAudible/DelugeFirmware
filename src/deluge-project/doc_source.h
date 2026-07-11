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
#include <optional>

namespace deluge::project {

/// The DX7 FM patch a Source carries when oscType == DX7 (else absent). The 156 raw patch bytes plus the two
/// extension scalars — everything the serializer round-trips (dx7patch hex + dx7enginemode + dx7randomdetune).
struct DocDxPatch {
	uint8_t params[156]{};
	uint8_t engine_mode{0};
	int32_t random_detune{0};
};

/// The DOM-owned structural record for one Sound oscillator Source (M3 owner-5, slice G). Nested per-source in
/// DocSound (Sound has kNumSources==2). Params are already in the DOM param tier; this holds the Source's non-param
/// structure. Slice G1 = the flat scalars; G2 adds the DX7 patch, G3 adds the sample/wavetable range list. Enums
/// stored as underlying values. `defaultRangeI`/`dxPatchChanged` are transient (not serialized) — excluded.
struct DocSource {
	uint8_t osc_type{0};    ///< OscType underlying
	int16_t transpose{0};   ///< non-sample osc types only
	int8_t cents{0};        ///< non-sample osc types only
	uint8_t repeat_mode{0}; ///< SampleRepeatMode underlying
	int8_t time_stretch_amount{0};
	uint8_t interpolation_mode{0}; ///< SampleControls::interpolationMode underlying
	bool pitch_and_speed_independent{false};
	bool reversed{false};

	/// G2: the DX7 patch — present iff oscType == DX7 (the engine's dxPatch pointer is non-null).
	std::optional<DocDxPatch> dx_patch;
};

} // namespace deluge::project
