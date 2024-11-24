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

#include "deluge/io/midi/midi_device.h"

class MIDICableDINPorts final : public MIDICable {
private:
	using IntermediateMessageBuffer = std::array<uint8_t, 3>;
	IntermediateMessageBuffer messageBytes_;
	IntermediateMessageBuffer::size_type currentByte_;
	bool currentlyReceivingSysex_{false};

public:
	MIDICableDINPorts() {
		connectionFlags = 1; // DIN ports are always connected
	}

	/// Called by the DIN root complex when a byte is received
	[[nodiscard]] Error onReceiveByte(uint32_t timestamp, uint8_t byte);

	[[nodiscard]] bool wantsToOutputMIDIOnChannel(MIDIMessage message, int32_t filter) const override;

	void writeReferenceAttributesToFile(Serializer& writer) override;
	void writeToFlash(uint8_t* memory) override;
	char const* getDisplayName() override;

	[[nodiscard]] Error sendMessage(MIDIMessage message) override;
	[[nodiscard]] Error sendSysex(const uint8_t* data, int32_t len) override;
	size_t sendBufferSpace() const override;
};
