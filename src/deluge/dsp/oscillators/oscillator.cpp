/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams
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
#include "oscillator.h"
#include "util/misc.h" // util::to_underlying

namespace deluge::dsp {
PLACE_INTERNAL_FRUNK int32_t oscSyncRenderingBuffer[SSI_TX_BUFFER_NUM_SAMPLES + 4]
    __attribute__((aligned(CACHE_LINE_SIZE)));

bool isVirtualAnalog(OscType type) {
	// The six VA waves are the first OscType entries, up to ANALOG_SAW_2; WAVETABLE and above are not.
	return util::to_underlying(type) <= util::to_underlying(OscType::ANALOG_SAW_2);
}

// The app's open OscType → the VA engine's closed wave set.
synth::va_shape::Wave vaWaveFor(OscType type) {
	switch (type) {
	case OscType::TRIANGLE:
		return synth::va_shape::Wave::Triangle;
	case OscType::SQUARE:
		return synth::va_shape::Wave::Square;
	case OscType::SAW:
		return synth::va_shape::Wave::Saw;
	case OscType::ANALOG_SAW_2:
		return synth::va_shape::Wave::AnalogSaw;
	case OscType::ANALOG_SQUARE:
		return synth::va_shape::Wave::AnalogSquare;
	default: // SINE
		return synth::va_shape::Wave::Sine;
	}
}

} // namespace deluge::dsp
