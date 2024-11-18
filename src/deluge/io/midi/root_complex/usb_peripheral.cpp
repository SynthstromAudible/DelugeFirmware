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

#include "usb_peripheral.h"

#include "class/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/usb/usb_state.h"

using namespace deluge::io::usb;

MIDIRootComplexUSBPeripheral::MIDIRootComplexUSBPeripheral() : cables_{0, 1, 2} {
}

MIDIRootComplexUSBPeripheral::~MIDIRootComplexUSBPeripheral() {
	// Clean up the pointers to our cables
	for (auto i = 0; i < MAX_NUM_USB_MIDI_DEVICES; ++i) {
		auto& connected_device = connectedUSBMIDIDevices[0][i];
		for (auto & cable : connected_device.cable) {
			cable = nullptr;
		}
	}
}

void MIDIRootComplexUSBPeripheral::flush() {
	// NOOP in the tinyusb implementaiton, since the cable will have internally flushed already.
}

Error MIDIRootComplexUSBPeripheral::poll() {
	auto& connected_device = connectedUSBMIDIDevices[0][0];

	if ((connected_device.cable[0] != nullptr) && (connected_device.currentlyWaitingToReceive == 0u)) {
		std::array<uint8_t, 4> packet{};
		while (tud_midi_n_packet_read(0, packet.data())) {
			uint8_t status_type = packet[0] & 0x0F;
			uint8_t cable = (packet[0] & 0xF0) >> 4;
			uint8_t channel = packet[1] & 0x0F;
			uint8_t data1 = packet[2];
			uint8_t data2 = packet[3];

			if (status_type < 0x08) {
				if (status_type == 2 || status_type == 3) { // 2 or 3 byte system common messages
					status_type = 0x0F;
				}
				else { // Invalid, or sysex, or something
					if (cable < this->cables_.size()) {
						this->cables_[cable].checkIncomingSysex(packet.data(), 0, 0);
					}
					continue;
				}
			}
			// select appropriate device based on the cable number
			if (cable > connected_device.maxPortConnected) {
				// fallback to cable 0 since we don't support more than one port on hosted devices yet
				cable = 0;
			}
			midiEngine.midiMessageReceived(*connected_device.cable[cable], status_type, channel, data1, data2,
			                               &timeLastBRDY[0]);
		}
	}

	return Error::NONE;
}

MIDICable* MIDIRootComplexUSBPeripheral::getCable(size_t index) {
	if (index < 0 || index >= getNumCables()) {
		return nullptr;
	}
	return &cables_[index];
}
