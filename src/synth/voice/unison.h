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

#include "definitions_cxx.hpp" // kMaxNumVoicesUnison
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/pitch/fine_tune.h"

#include <array>
#include <cstdint>

/// @file
/// @brief How a sound's unison parts sit in pitch and across the field.

namespace deluge::synth {

/// @brief Unison as the voice plays it: each part's detune and pan, and the level that keeps the parts' sum at one
/// voice's.
/// @details With one part (or none) the detunes and pans are not read: the engines play the single part undetuned
/// and unpanned.
struct UnisonLayout {
	uint8_t count{1};
	std::array<dsp::fixed::FineTune, kMaxNumVoicesUnison> detunes{};
	std::array<dsp::Q<31>, kMaxNumVoicesUnison> pans{};
	dsp::Q<31> level{}; ///< the volume param's neutral over √count, the level each part is scaled to
	bool operator==(const UnisonLayout&) const = default;
};

/// @brief The layout of @p count parts spread over @p detune cents and @p spread of the stereo field.
/// @param count Parts per voice.
/// @param detune The cents between the lowest and highest part.
/// @param spread How far the parts spread across the field, 0..50.
[[nodiscard]] UnisonLayout derive_unison(uint8_t count, int8_t detune, uint8_t spread) noexcept;

} // namespace deluge::synth
