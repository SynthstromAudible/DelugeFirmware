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

#include "usb_hosted.h"
#include "deluge/io/usb/usb_state.h"
#include "drivers/uart/uart.h"
#include "io/debug/log.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"

using namespace deluge::io::usb;

MIDIRootComplexUSBHosted::MIDIRootComplexUSBHosted() = default;

MIDIRootComplexUSBHosted::~MIDIRootComplexUSBHosted() {
	// Clean up the pointers to our cables
	for (auto i = 0; i < MAX_NUM_USB_MIDI_DEVICES; ++i) {
		auto& connectedDevice = connectedUSBMIDIDevices[0][i];
		for (auto j = 0; j < ((sizeof(connectedDevice.cable) / sizeof(connectedDevice.cable[0]))); ++j) {
			connectedDevice.cable[j] = nullptr;
		}
	}
}

void flushUSBMIDIToHostedDevice(int32_t ip, int32_t d, bool resume = false);

extern "C" {

#include "RZA1/usb/userdef/r_usb_hmidi_config.h"

extern uint8_t currentlyAccessingCard;
extern void usb_send_start_rohan(usb_utr_t* ptr, uint16_t pipe, uint8_t const* data, int32_t size);
extern void change_destination_of_send_pipe(usb_utr_t* ptr, uint16_t pipe, uint16_t* tbl, int32_t sq);

// We now bypass calling this for successful as peripheral on A1 (see usb_pstd_bemp_pipe_process_rohan_midi())
void usbSendCompleteAsHost(int32_t ip) {

	int32_t midiDeviceNum = usbDeviceNumBeingSentToNow[ip];

	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];

	connectedDevice->numBytesSendingNow = 0; // We just do this instead from caller on A1 (see comment above)

	// check if there was more to send on the same device, then resume sending
	bool has_more = connectedDevice->consumeSendData();
	if (has_more) {
		// TODO: do some cooperative scheduling here. so if there is a flood of data
		// on connected device 1 and we just want to send a few notes on device 2,
		// make sure device 2 ges a fair shot now and then

		flushUSBMIDIToHostedDevice(ip, midiDeviceNum, true);
		return;
	}

	// If that was the last device we were going to send to, that send's been done, so we can just get out.
	if (midiDeviceNum == stopSendingAfterDeviceNum[ip]) {
		anyUSBSendingStillHappening[ip] = 0;
		return;
	}

	while (true) {
		midiDeviceNum++;
		if (midiDeviceNum >= MAX_NUM_USB_MIDI_DEVICES) {
			midiDeviceNum -= MAX_NUM_USB_MIDI_DEVICES;
		}
		connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];
		if (connectedDevice->cable[0] && connectedDevice->numBytesSendingNow) {
			// If here, we got a connected device, so flush
			flushUSBMIDIToHostedDevice(ip, midiDeviceNum);
			return;
		}
		if (midiDeviceNum == stopSendingAfterDeviceNum[ip]) { // If reached end of devices and last one got disconnected
			                                                  // in the interim (very rare)
			usbDeviceNumBeingSentToNow[ip] = stopSendingAfterDeviceNum[ip];
			anyUSBSendingStillHappening[ip] = 0;
			return;
		}
	}
}
}

void setupUSBHostReceiveTransfer(int32_t ip, int32_t midiDeviceNum) {
	connectedUSBMIDIDevices[ip][midiDeviceNum].currentlyWaitingToReceive = 1;

	int32_t pipeNumber = g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][midiDeviceNum][USB_EPL];

	g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum].keyword = pipeNumber;
	g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum].tranlen = 64;

	g_p_usb_pipe[pipeNumber] = &g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum];

	// uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];

	usb_receive_start_rohan_midi(pipeNumber);

	/*
	uint16_t endTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
	uint16_t duration = endTime - startTime;
	uint32_t timePassedNS = superfastTimerCountToNS(duration);
	uartPrint("send setup duration, nSec: ");
	uartPrintNumber(timePassedNS);
	*/
}

void flushUSBMIDIToHostedDevice(int32_t ip, int32_t d, bool resume) {
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];
	// there was an assumption that the pipe wouldn't have changed if we were resuming a transfer but that has turned
	// out not to be true if hubs are involved. A hub transaction seems to be able to run before the
	// usbSendCompleteAsHost interrupt is called and changes the pipe, and then the next write doesn't go anywhere
	// useful
	int32_t pipeNumber = g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d][0];
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].keyword = pipeNumber;
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].tranlen = connectedDevice->numBytesSendingNow;
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].p_tranadr = connectedDevice->dataSendingNow;

	usbDeviceNumBeingSentToNow[USB_CFG_USE_USBIP] = d;

	int32_t isInterrupt = (pipeNumber == USB_CFG_HMIDI_INT_SEND);

	if (d != currentDeviceNumWithSendPipe[isInterrupt]) {
		currentDeviceNumWithSendPipe[isInterrupt] = d;
		change_destination_of_send_pipe(&g_usb_midi_send_utr[USB_CFG_USE_USBIP], pipeNumber,
		                                g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d], connectedDevice->sq);
	}

	connectedDevice->sq = !connectedDevice->sq;

	g_p_usb_pipe[pipeNumber] = &g_usb_midi_send_utr[USB_CFG_USE_USBIP];

	usb_send_start_rohan(&g_usb_midi_send_utr[USB_CFG_USE_USBIP], pipeNumber, connectedDevice->dataSendingNow,
	                     connectedDevice->numBytesSendingNow);
}

void MIDIRootComplexUSBHosted::flush() {
	if (usbLock) {
		return;
	}

	constexpr int ip = 0;

	anythingInUSBOutputBuffer = false;

	// is this still relevant? anyUSBSendingStillHappening[ip] acts as the lock between routine and interrupt
	// on the sending side. all other uses of usbLock seems to be about _receiving_. Can there be a conflict
	// between sending and receiving as well??
	USBAutoLock lock;

	if (anyUSBSendingStillHappening[ip]) {
		// still sending, call me later maybe
		anythingInUSBOutputBuffer = true;
		return;
	}

	// This next bit was written with multiple devices on hubs in mind, but seems to work for a single MIDI
	// device too

	int32_t midiDeviceNumToSendTo = currentDeviceNumWithSendPipe[0]; // This will do
	if (midiDeviceNumToSendTo >= MAX_NUM_USB_MIDI_DEVICES) {
		midiDeviceNumToSendTo = 0; // In case it was set to "none", I think
	}

	int32_t newStopSendingAfter = midiDeviceNumToSendTo - 1;
	if (newStopSendingAfter < 0) {
		newStopSendingAfter += MAX_NUM_USB_MIDI_DEVICES;
	}

	// Make sure that's on a connected device - it probably would be...
	while (true) {
		ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNumToSendTo];
		if (connectedDevice->cable[0] && connectedDevice->hasBufferedSendData()) {
			break; // We found a connected one
		}
		if (midiDeviceNumToSendTo == newStopSendingAfter) {
			return;
		}
		midiDeviceNumToSendTo++;
		if (midiDeviceNumToSendTo >= MAX_NUM_USB_MIDI_DEVICES) {
			midiDeviceNumToSendTo = 0; // Wrap back to start of list
		}
	}

	// Stop after a device which we know is connected
	while (true) {
		ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][newStopSendingAfter];

		if (connectedDevice->cable[0] && connectedDevice->hasBufferedSendData()) {
			break; // We found a connected one
		}

		newStopSendingAfter--;
		if (newStopSendingAfter < 0) {
			newStopSendingAfter += MAX_NUM_USB_MIDI_DEVICES;
		}
	}

	// Copy the buffers for all devices
	int32_t d = midiDeviceNumToSendTo;
	while (true) {
		ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];

		if (connectedDevice->cable[0]) {
			connectedDevice->consumeSendData();
		}
		if (d == newStopSendingAfter) {
			break;
		}
		d++;
		if (d >= MAX_NUM_USB_MIDI_DEVICES) {
			d = 0;
		}
	}

	stopSendingAfterDeviceNum[ip] = newStopSendingAfter;
	anyUSBSendingStillHappening[ip] = 1;

	flushUSBMIDIToHostedDevice(ip, midiDeviceNumToSendTo);
}

Error MIDIRootComplexUSBHosted::poll() {
	if (currentlyAccessingCard != 0) {
		// D_PRINTLN("checkIncomingUsbMidi seeing currentlyAccessingCard non-zero");
		return Error::NO_ERROR_BUT_GET_OUT;
	}

	bool usbLockNow = usbLock;

	// TODO: delete this, we don't ever need to iterate over IPs
	constexpr auto ip = 0;

	// assumes only single device in peripheral mode
	int32_t numDevicesNow = MAX_NUM_USB_MIDI_DEVICES;

	for (int32_t d = 0; d < numDevicesNow; d++) {
		auto& connectedDevice = connectedUSBMIDIDevices[ip][d];
		if (connectedDevice.cable[0] != nullptr && connectedDevice.currentlyWaitingToReceive == 0u) {

			int32_t bytesReceivedHere = connectedDevice.numBytesReceived;
			if (bytesReceivedHere) {

				connectedDevice.numBytesReceived = 0;

				const uint8_t* __restrict__ readPos = connectedDevice.receiveData;
				const uint8_t* const stopAt = readPos + bytesReceivedHere;

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
							// XXX: this collapses all cables to cable 0, but we only technically support 1 cable on
							// remote USB devices for now..
							connectedDevice.cable[0]->checkIncomingSysex(readPos, ip, d);
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

			// If this is a reentrant invocation, don't do transfer setup
			if (usbLockNow) {
				continue;
			}

			// And maybe setup transfer to receive more data
			if (connectedDevice.cable[0] != nullptr) {
				// Only allowed to setup receive-transfer if not in the process of sending to various devices. (Wait,
				// still? Was this just because of that insane bug that's now fixed?)
				if (usbDeviceNumBeingSentToNow[ip] == stopSendingAfterDeviceNum[ip]) {
					USBAutoLock lock;
					setupUSBHostReceiveTransfer(ip, d);
				}
			}
		}
	}

	return Error::NONE;
}

MIDICable* MIDIRootComplexUSBHosted::getCable(size_t index) {
	if (index < 0 || index >= hostedMIDIDevices_.getNumElements()) {
		return nullptr;
	}
	return static_cast<MIDICableUSBHosted*>(hostedMIDIDevices_.getElement(static_cast<int32_t>(index)));
}
