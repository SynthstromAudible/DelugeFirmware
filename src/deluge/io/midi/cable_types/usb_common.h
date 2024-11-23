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

#include "deluge/io/midi//midi_device.h"

class MIDICableUSB : public MIDICable {
public:
	MIDICableUSB(uint8_t portNum = 0) {
		portNumber = portNum;
		needsToSendMCMs = 0;
	}

	void sendMessage(MIDIMessage message) override;
	void sendSysex(const uint8_t* data, int32_t len) override;
	size_t sendBufferSpace() override;

	void connectedNow(int32_t midiDeviceNum);
	void sendMCMsNowIfNeeded();
	uint8_t needsToSendMCMs;
	uint8_t portNumber;
};
