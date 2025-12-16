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

#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/usb/usb_state.h"

using namespace deluge::io::usb;

extern "C" {
#include "drivers/usb/userdef/r_usb_pmidi_config.h"

extern uint8_t currentlyAccessingCard;

extern void usb_send_start_rohan(usb_utr_t* ptr, uint16_t pipe, uint8_t const* data, int32_t size);

void usbSendCompleteAsPeripheral(int32_t ip) {
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][0];
	connectedDevice->numBytesSendingNow = 0; // Even easier!

	// I think this could happen as part of a detach see detachedAsPeripheral()
	if (anyUSBSendingStillHappening[ip] == 0) {
		return;
	}

	bool has_more = connectedDevice->consumeSendData();
	if (has_more) {
		// this is already the case:
		// anyUSBSendingStillHappening[ip] = 1;

		g_usb_midi_send_utr[ip].tranlen = connectedDevice->numBytesSendingNow;
		g_usb_midi_send_utr[ip].p_tranadr = connectedDevice->dataSendingNow;

		usb_send_start_rohan(NULL, USB_CFG_PMIDI_BULK_OUT, connectedDevice->dataSendingNow,
		                     connectedDevice->numBytesSendingNow);
	}
	else {
		// this effectively serves as a lock, does the sending part of the device, including the read part of the
		// ring buffer "belong" to ongoing/scheduled interrupts. Document this better.
		anyUSBSendingStillHappening[0] = 0;
	}
}
}

MIDIRootComplexUSBPeripheral::MIDIRootComplexUSBPeripheral() : cables_{0, 1, 2} {

	auto* cable_two = &cables_.at(1);
	for (auto& port : (cable_two->ports)) {
		port.mpeLowerZoneLastMemberChannel = 7;
		port.mpeUpperZoneLastMemberChannel = 8;
	}
}

MIDIRootComplexUSBPeripheral::~MIDIRootComplexUSBPeripheral() {
	// Clean up the pointers to our cables
	for (auto i = 0; i < MAX_NUM_USB_MIDI_DEVICES; ++i) {
		auto& connectedDevice = connectedUSBMIDIDevices[0][i];
		for (auto j = 0; j < ((sizeof(connectedDevice.cable) / sizeof(connectedDevice.cable[0]))); ++j) {
			connectedDevice.cable[j] = nullptr;
		}
	}
}

void MIDIRootComplexUSBPeripheral::flush() {
	constexpr int ip = 0;

	if (usbLock) {
		return;
	}

	anythingInUSBOutputBuffer = false;

	// is this still relevant? anyUSBSendingStillHappening[ip] acts as the lock between routine and interrupt
	// on the sending side. all other uses of usbLock seems to be about _receiving_. Can there be a conflict
	// between sending and receiving as well??
	USBAutoLock lock;

	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][0];

	if (anyUSBSendingStillHappening[ip]) {
		// still sending, call me later maybe
		anythingInUSBOutputBuffer = true;
		return;
	}

	if (!connectedDevice->consumeSendData()) {
		return;
	}

	g_usb_midi_send_utr[ip].keyword = USB_CFG_PMIDI_BULK_OUT;
	g_usb_midi_send_utr[ip].tranlen = connectedDevice->numBytesSendingNow;
	g_usb_midi_send_utr[ip].p_tranadr = connectedDevice->dataSendingNow;

	usbDeviceNumBeingSentToNow[ip] = 0;
	anyUSBSendingStillHappening[ip] = 1;

	g_p_usb_pipe[USB_CFG_PMIDI_BULK_OUT] = &g_usb_midi_send_utr[ip];
	usb_send_start_rohan(NULL, USB_CFG_PMIDI_BULK_OUT, connectedDevice->dataSendingNow,
	                     connectedDevice->numBytesSendingNow);

	// when done, usbSendCompleteAsPeripheral() will be called in an interrupt
}

Error MIDIRootComplexUSBPeripheral::poll() {
	if (currentlyAccessingCard != 0) {
		// D_PRINTLN("checkIncomingUsbMidi seeing currentlyAccessingCard non-zero");
		return Error::NO_ERROR_BUT_GET_OUT;
	}

	bool usbLockNow = usbLock;

	constexpr auto ip = 0;
	constexpr auto d = 0;

	// assumes only single device in peripheral mode
	int32_t numDevicesNow = 1;

	auto& connectedDevice = connectedUSBMIDIDevices[ip][d];

	if (connectedDevice.cable[0] && !connectedDevice.currentlyWaitingToReceive) {
		int32_t bytesReceivedHere = connectedDevice.numBytesReceived;
		if (bytesReceivedHere) {

			connectedDevice.numBytesReceived = 0;

			uint8_t const* __restrict__ readPos = connectedDevice.receiveData;
			uint8_t const* const stopAt = readPos + bytesReceivedHere;

			// Receive all the stuff from this device
			for (; readPos < stopAt; readPos += 4) {

				uint8_t statusType = readPos[0] & 0x0F;
				uint8_t cable = (readPos[0] & 0xF0) >> 4;
				uint8_t channel = readPos[1] & 0x0F;
				uint8_t data1 = readPos[2];
				uint8_t data2 = readPos[3];

				if (statusType < 0x08) {
					if (statusType == 2 || statusType == 3) { // 2 or 3 byte system common messages
						statusType = 0x0F;
					}
					else { // Invalid, or sysex, or something
						if (cable < this->cables_.size()) {
							this->cables_[cable].checkIncomingSysex(readPos, ip, d);
						}
						continue;
					}
				}
				// select appropriate device based on the cable number
				if (cable > connectedDevice.maxPortConnected) {
					// fallback to cable 0 since we don't support more than one port on hosted devices yet
					cable = 0;
				}
				midiEngine.midiMessageReceived(*connectedDevice.cable[cable], statusType, channel, data1, data2,
				                               &timeLastBRDY[ip]);
			}
		}

		if (usbLockNow) {
			return Error::NONE;
		}

		// And maybe setup transfer to receive more data

		g_usb_midi_recv_utr[ip][0].keyword = USB_CFG_PMIDI_BULK_IN;
		g_usb_midi_recv_utr[ip][0].tranlen = 64;

		connectedUSBMIDIDevices[ip][0].currentlyWaitingToReceive = 1;

		USBAutoLock lock;
		g_p_usb_pipe[USB_CFG_PMIDI_BULK_IN] = &g_usb_midi_recv_utr[ip][0];
		usb_receive_start_rohan_midi(USB_CFG_PMIDI_BULK_IN);
	}

	return Error::NONE;
}

MIDICable* MIDIRootComplexUSBPeripheral::getCable(size_t index) {
	// We need to use cables_.size() instead of getNumCables(), as that one doesn't
	// admit that the last cable exists, since it's a secret one!
	if (index < 0 || index >= cables_.size()) {
		return nullptr;
	}
	return &cables_[index];
}
