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

#include "definitions_cxx.hpp"                            // ModFXType
#include "model/mod_controllable/filters/filter_config.h" // FilterMode, FilterRoute

#include <cstdint>

namespace deluge::snapshot {

/// The engine's derived read-view of a ModControllableAudio's effect configuration, the counterpart of
/// SoundSnapshot for the effects base shared by Sound and GlobalEffectable. Projected from the bound
/// `DocModControllable` (ModControllableAudio::projectModControllableSnapshot()); an unbound owner writes it directly.
/// The delay, sidechain, compressor and stutter settings live in their DSP objects, which the projection writes in
/// place.
struct ModControllableSnapshot {
	uint8_t clippingAmount{0};                         ///< DocModControllable::clipping_amount
	FilterMode lpfMode{FilterMode::TRANSISTOR_24DB};   ///< DocModControllable::lpf_mode
	FilterMode hpfMode{FilterMode::HPLADDER};          ///< DocModControllable::hpf_mode
	FilterRoute filterRoute{FilterRoute::HIGH_TO_LOW}; ///< DocModControllable::filter_route
	ModFXType modFXType{ModFXType::NONE};              ///< DocModControllable::mod_fx_type
};

} // namespace deluge::snapshot
