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

#include "din.h"

#include "io/debug/log.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
}

DINRootComplex::DINRootComplex() = default;
DINRootComplex::~DINRootComplex() = default;

void DINRootComplex::flush() {
	uartFlushIfNotSending(UART_ITEM_MIDI);
}

Error DINRootComplex::poll() {
	uint8_t thisSerialByte;
	uint32_t* timer = uartGetCharWithTiming(TIMING_CAPTURE_ITEM_MIDI, (char*)&thisSerialByte);

	while (timer != nullptr) {
		auto err = cable.onReceiveByte(*timer, thisSerialByte);
		if (err != Error::NONE) {
			return err;
		}

		timer = uartGetCharWithTiming(TIMING_CAPTURE_ITEM_MIDI, (char*)&thisSerialByte);
	}

	return Error::NO_ERROR_BUT_GET_OUT;
}
