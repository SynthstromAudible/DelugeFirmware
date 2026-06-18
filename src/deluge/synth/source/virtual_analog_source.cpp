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
#include "synth/source/virtual_analog_source.h"

namespace deluge::synth {

// The one note-on dispatch: build the variant alternative for the wave (replaces the legacy per-block OscType
// megaswitch). Rendering + the per-block crude/BL/pulse/sync choice all live inside the shape. The switch is
// exhaustive over the closed va_shape::Wave — no default, so -Wswitch flags any future wave that isn't handled.
VirtualAnalogSource::Shape VirtualAnalogSource::makeShape(va_shape::Wave wave, deluge::dsp::Phasor seed) {
	switch (wave) {
	case va_shape::Wave::Sine:
		return va_shape::Sine{seed};
	case va_shape::Wave::Triangle:
		return va_shape::Triangle{seed};
	case va_shape::Wave::Square:
		return va_shape::Square{seed};
	case va_shape::Wave::Saw:
		return va_shape::Saw{seed};
	case va_shape::Wave::AnalogSaw:
		return va_shape::AnalogSaw{seed};
	case va_shape::Wave::AnalogSquare:
		return va_shape::AnalogSquare{seed};
	}
	return va_shape::Sine{seed}; // unreachable: Wave is closed (the switch above is exhaustive).
}

VirtualAnalogSource::VirtualAnalogSource(va_shape::Wave wave, deluge::dsp::Phasor seed)
    : shape_{makeShape(wave, seed)} {
}

} // namespace deluge::synth
