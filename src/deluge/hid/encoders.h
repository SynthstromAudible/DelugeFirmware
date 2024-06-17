/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "hid/encoder.h"
#include "util/misc.h"
#include <array>

namespace deluge::hid::encoders {

/// Index of the encoder.
enum class EncoderName {
	SCROLL_Y,
	SCROLL_X,
	TEMPO,
	SELECT,
	/// End of function (black, detented) encoders,
	MAX_FUNCTION_ENCODERS,
	/// The upper gold encoder
	MOD_1 = MAX_FUNCTION_ENCODERS,
	/// The lower gold encoder
	MOD_0,
	/// Total number of encoders
	MAX_ENCODER,
};

/// Last AudioEngine::audioSampleTimer tick at which we noticed a change on one of the mod encoders.
extern uint32_t timeModEncoderLastTurned[];

void init();
void readEncoders();
bool interpretEncoders(bool skipActioning = false);

Encoder& getEncoder(EncoderName which);
} // namespace deluge::hid::encoders
