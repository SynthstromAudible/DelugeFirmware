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

namespace deluge::project {

/// Portable DOM record for ModControllableAudio's non-param structural fields (filter/modFX + delay/sidechain/
/// compressor/stutter). SHARED base — nested in DocSound now, reusable by future GlobalEffectable FX doc types.
/// Enums stored as underlying; compressor knob-positions are q31 (int32). midi_knobs (MIDI-learn) is out of scope.
struct DocModControllable {
	uint8_t clipping_amount{0};
	uint8_t lpf_mode{0};
	uint8_t hpf_mode{0};
	uint8_t filter_route{0};
	uint8_t mod_fx_type{0};

	struct Delay {
		bool ping_pong{true};
		bool analog{false};
		uint8_t sync_type{0};
		uint8_t sync_level{0};
	} delay;

	struct Sidechain {
		int32_t attack{0};
		int32_t release{0};
		uint8_t sync_type{0};
		uint8_t sync_level{0};
	} sidechain;

	struct Compressor { ///< q31 knob positions (via getters); NOT in the param-DOM tier
		int32_t attack{0};
		int32_t release{0};
		int32_t threshold{0};
		int32_t ratio{0};
		int32_t sidechain_hpf{0};
		int32_t blend{0};
	} compressor;

	struct Stutter {
		bool quantized{true};
		bool reversed{false};
		bool ping_pong{false};
	} stutter;
};

} // namespace deluge::project
