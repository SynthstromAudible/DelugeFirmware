/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "processing/engines/audio_engine.h"
#include "io/midi/midi_engine.h"
#include "util/functions.h"
#include "gui/ui/sound_editor.h"
#include "io/uart/uart.h"
#include <string.h>
#include "playback/mode/playback_mode.h"
#include "model/song/song.h"
#include "hid/display/numeric_driver.h"
#include "RZA1/mtu/mtu.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"

extern "C" {
#include "RZA1/uart/sio_char.h"

volatile uint32_t usbLock = 0;
void usb_cstd_usb_task();

#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/system/iodefine.h"
#include "drivers/usb/userdef/r_usb_pmidi_config.h"

#include "RZA1/usb/userdef/r_usb_hmidi_config.h"
#include "RZA1/usb/r_usb_hmidi/src/inc/r_usb_hmidi.h"

extern uint16_t g_usb_peri_connected;
extern uint16_t g_usb_usbmode;

uint8_t stopSendingAfterDeviceNum[USB_NUM_USBIP];
uint8_t usbDeviceNumBeingSentToNow[USB_NUM_USBIP];
uint8_t anyUSBSendingStillHappening[USB_NUM_USBIP];

usb_utr_t g_usb_midi_send_utr[USB_NUM_USBIP];
usb_utr_t g_usb_midi_recv_utr[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

extern uint16_t g_usb_hmidi_tmp_ep_tbl[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES][(USB_EPL * 2) + 1];

extern usb_utr_t* g_p_usb_pipe[USB_MAX_PIPE_NO + 1u];

usb_regadr_t usb_hstd_get_usb_ip_adr(uint16_t ipno);
void change_destination_of_send_pipe(usb_utr_t* ptr, uint16_t pipe, uint16_t* tbl, int sq);
void usb_send_start_rohan(usb_utr_t* ptr, uint16_t pipe, uint8_t const* data, int size);
void usb_receive_start_rohan_midi(uint16_t pipe);
void usb_pstd_set_stall(uint16_t pipe);
void usb_cstd_set_nak(usb_utr_t* ptr, uint16_t pipe);
void hw_usb_clear_pid(usb_utr_t* ptr, uint16_t pipeno, uint16_t data);
uint16_t hw_usb_read_pipectr(usb_utr_t* ptr, uint16_t pipeno);

void flushUSBMIDIToHostedDevice(int ip, int d);

uint8_t currentDeviceNumWithSendPipe[USB_NUM_USBIP][2] = {
    MAX_NUM_USB_MIDI_DEVICES, MAX_NUM_USB_MIDI_DEVICES}; // One without, and one with, interrupt endpoints

// We now bypass calling this for successful as peripheral on A1 (see usb_pstd_bemp_pipe_process_rohan_midi())
void usbSendComplete(int ip) {

	int midiDeviceNum = usbDeviceNumBeingSentToNow[ip];

	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];

	connectedDevice->numBytesSendingNow = 0; // We just do this instead from caller on A1 (see comment above)

	bool inHostMode = (g_usb_usbmode == USB_HOST);

	// If in host mode, see if we want to send to another device
	if (inHostMode) {

		// If that was the last device we were going to send to, that send's been done, so we can just get out.
		if (midiDeviceNum == stopSendingAfterDeviceNum[ip]) {
			goto finishedAllSending;
		}

		while (true) {
			midiDeviceNum++;
			if (midiDeviceNum >= MAX_NUM_USB_MIDI_DEVICES) {
				midiDeviceNum -= MAX_NUM_USB_MIDI_DEVICES;
			}
			connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];
			if (connectedDevice->device && connectedDevice->numBytesSendingNow) {
				break; // If got connected device, flush to it
			}
			if (midiDeviceNum
			    == stopSendingAfterDeviceNum
			        [ip]) { // If reached end of devices and last one got disconnected in the interim (very rare)
				usbDeviceNumBeingSentToNow[ip] = stopSendingAfterDeviceNum[ip];
				goto finishedAllSending;
			}
		}

		// If here, we got a connected device, so flush
		flushUSBMIDIToHostedDevice(ip, midiDeviceNum);
	}
	else {
finishedAllSending:
		anyUSBSendingStillHappening[ip] = 0;
	}
}

// We now bypass calling this for successful as peripheral on A1 (see usb_pstd_bemp_pipe_process_rohan_midi())
void usbSendCompletePeripheralOrA1(usb_utr_t* p_mess, uint16_t data1, uint16_t data2) {

	// If error, forget about device.
	// No actually don't - sometimes there'll be an error if another device connected or disconnected from hub during fast MIDI sending. This seems to happen even though I've
	// stopped it from setting up or down the out-pipe as it goes
	if (p_mess->status == USB_DATA_ERR) {
		uartPrintln("send error!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
		//g_usb_host_connected[deviceNum] = 0;
	}

#if USB_NUM_USBIP == 1
	int ip = 0;
#else
	int ip = p_mess->ip;
#endif

	usbSendComplete(ip);
}

void usbReceiveComplete(int ip, int deviceNum, int tranlen) {
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][deviceNum];

	connectedDevice->numBytesReceived =
	    64
	    - tranlen; // Seems wack, but yet, tranlen is now how many bytes didn't get received out of the original transfer size
	// Warning - sometimes (with a Teensy, e.g. my knob box), length will be 0. Not sure why - but we need to cope with that case.

	connectedDevice->currentlyWaitingToReceive = 0; // Take note that we need to set up another receive
}

void usbReceiveCompletePeripheralOrA1(usb_utr_t* p_mess, uint16_t data1, uint16_t data2) {

#if USB_NUM_USBIP == 1
	int ip = 0;
#else
	int ip = p_mess->ip;
#endif

	if (p_mess->status == USB_DATA_ERR) {
		return; // Can happen if user disconnects device - totally normal
	}

	int deviceNum = p_mess - &g_usb_midi_recv_utr[ip][0];

	if (p_mess->status
	    != USB_DATA_SHT) { // Are there actually any other possibilities that could happen here? Can't remember.
		uartPrint("status: ");
		uartPrintNumber(p_mess->status);
	}

	usbReceiveComplete(ip, deviceNum, p_mess->tranlen);
}

uint32_t timeLastBRDY[USB_NUM_USBIP];

void brdyOccurred(int ip) {
	timeLastBRDY[ip] = DMACnNonVolatile(SSI_TX_DMA_CHANNEL).CRSA_n; // Reading this not as volatile works fine
}
}

MidiEngine midiEngine{};

bool anythingInUSBOutputBuffer = false;

MidiEngine::MidiEngine() {
	numSerialMidiInput = 0;
	lastStatusByteSent = 0;
	currentlyReceivingSysExSerial = false;
	midiThru = false;

	g_usb_peri_connected = 0; // Needs initializing with A2 driver

	for (int ip = 0; ip < USB_NUM_USBIP; ip++) {

		g_usb_midi_send_utr[ip].complete = (usb_cb_t)usbSendCompletePeripheralOrA1;

		g_usb_midi_send_utr[ip].p_setup = 0; /* Setup message address set */
		g_usb_midi_send_utr[ip].segment = USB_TRAN_END;
		g_usb_midi_send_utr[ip].ip = ip;
		g_usb_midi_send_utr[ip].ipp = usb_hstd_get_usb_ip_adr(ip);

		for (int d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
			g_usb_midi_recv_utr[ip][d].p_tranadr = connectedUSBMIDIDevices[ip][d].receiveData;
			g_usb_midi_recv_utr[ip][d].complete = (usb_cb_t)usbReceiveCompletePeripheralOrA1;

			g_usb_midi_recv_utr[ip][d].p_setup = 0; /* Setup message address set */
			g_usb_midi_recv_utr[ip][d].segment = USB_TRAN_END;
			g_usb_midi_recv_utr[ip][d].ip = ip;
			g_usb_midi_recv_utr[ip][d].ipp = usb_hstd_get_usb_ip_adr(ip);
		}
	}
}

extern "C" void flushUSBMIDIToHostedDevice(int ip, int d) {

	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];

	int pipeNumber = g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d][0];

	g_usb_midi_send_utr[USB_CFG_USE_USBIP].keyword = pipeNumber;
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].tranlen = connectedDevice->numBytesSendingNow;
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].p_tranadr = connectedDevice->dataSendingNow;

	usbDeviceNumBeingSentToNow[USB_CFG_USE_USBIP] = d;

	int isInterrupt = (pipeNumber == USB_CFG_HMIDI_INT_SEND);

	if (d != currentDeviceNumWithSendPipe[USB_CFG_USE_USBIP][isInterrupt]) {
		currentDeviceNumWithSendPipe[USB_CFG_USE_USBIP][isInterrupt] = d;
		change_destination_of_send_pipe(&g_usb_midi_send_utr[USB_CFG_USE_USBIP], pipeNumber,
		                                g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d], connectedDevice->sq);
	}

	connectedDevice->sq = !connectedDevice->sq;

	g_p_usb_pipe[pipeNumber] = &g_usb_midi_send_utr[USB_CFG_USE_USBIP];

	usb_send_start_rohan(&g_usb_midi_send_utr[USB_CFG_USE_USBIP], pipeNumber, connectedDevice->dataSendingNow,
	                     connectedDevice->numBytesSendingNow);
}

int MidiEngine::getPotentialNumConnectedUSBMIDIDevices(int ip) {
	bool potentiallyAHost = (g_usb_usbmode == USB_HOST);
	//bool aPeripheral = g_usb_peri_connected;
	return potentiallyAHost ? MAX_NUM_USB_MIDI_DEVICES : 1;
}

// Warning - this will sometimes (not always) be called in an ISR
void MidiEngine::flushUSBMIDIOutput() {

	if (usbLock) {
		return;
	}

	anythingInUSBOutputBuffer = false;

	usbLock = 1;

	for (int ip = 0; ip < USB_NUM_USBIP; ip++) {
		if (anyUSBSendingStillHappening[ip]) {
			continue;
		}

		bool potentiallyAHost = (g_usb_usbmode == USB_HOST);
		bool aPeripheral = g_usb_peri_connected;

		if (aPeripheral) {
			ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][0];

			if (!connectedDevice->numMessagesQueued) {
				continue;
			}

			// Copy the data into the actual output buffer
			// Note - does this mean it's fine to have multiple devices sharing one physical device?
			// Or would that slow it down? Probably depends how fast this section is
			connectedDevice->numBytesSendingNow = connectedDevice->numMessagesQueued << 2;
			connectedDevice->numMessagesQueued = 0;
			memcpy(connectedDevice->dataSendingNow, connectedDevice->preSendData, connectedDevice->numBytesSendingNow);

			g_usb_midi_send_utr[ip].keyword = USB_CFG_PMIDI_BULK_OUT;
			g_usb_midi_send_utr[ip].tranlen = connectedDevice->numBytesSendingNow;
			g_usb_midi_send_utr[ip].p_tranadr = connectedDevice->dataSendingNow;

			usbDeviceNumBeingSentToNow[ip] = 0;
			anyUSBSendingStillHappening[ip] = 1;

			g_p_usb_pipe[USB_CFG_PMIDI_BULK_OUT] = &g_usb_midi_send_utr[ip];
			usb_send_start_rohan(NULL, USB_CFG_PMIDI_BULK_OUT, connectedDevice->dataSendingNow,
			                     connectedDevice->numBytesSendingNow);
		}

		else if (potentiallyAHost) {
			// This next bit was written with multiple devices on hubs in mind, but seems to work for a single MIDI device too

			int midiDeviceNumToSendTo = currentDeviceNumWithSendPipe[ip][0]; // This will do
			if (midiDeviceNumToSendTo >= MAX_NUM_USB_MIDI_DEVICES) {
				midiDeviceNumToSendTo = 0; // In case it was set to "none", I think
			}

			int newStopSendingAfter = midiDeviceNumToSendTo - 1;
			if (newStopSendingAfter < 0) {
				newStopSendingAfter += MAX_NUM_USB_MIDI_DEVICES;
			}

			// Make sure that's on a connected device - it probably would be...
			while (true) {
				ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNumToSendTo];
				if (connectedDevice->device && connectedDevice->numMessagesQueued) {
					break; // We found a connected one
				}
				if (midiDeviceNumToSendTo == newStopSendingAfter) {
					goto getOut; // If back where we started, none are connected. Could this really happen? Probably.
				}
				midiDeviceNumToSendTo++;
				if (midiDeviceNumToSendTo >= MAX_NUM_USB_MIDI_DEVICES) {
					midiDeviceNumToSendTo = 0; // Wrap back to start of list
				}
			}

			// Stop after a device which we know is connected
			while (true) {
				ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][newStopSendingAfter];

				if (connectedDevice->device && connectedDevice->numMessagesQueued) {
					break; // We found a connected one
				}

				newStopSendingAfter--;
				if (newStopSendingAfter < 0) {
					newStopSendingAfter += MAX_NUM_USB_MIDI_DEVICES;
				}
			}

			// Copy the buffers for all devices
			int d = midiDeviceNumToSendTo;
			while (true) {
				ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];

				if (connectedDevice->device && connectedDevice->numMessagesQueued) {
					// Copy the data into the actual output buffer
					connectedDevice->numBytesSendingNow = connectedDevice->numMessagesQueued << 2;
					connectedDevice->numMessagesQueued = 0;
					memcpy(connectedDevice->dataSendingNow, connectedDevice->preSendData,
					       connectedDevice->numBytesSendingNow);
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
getOut : {}
	}

	usbLock = 0;
}

bool MidiEngine::anythingInOutputBuffer() {
	return anythingInUSBOutputBuffer || (bool)uartGetTxBufferFullnessByItem(UART_ITEM_MIDI);
}

void MidiEngine::sendNote(bool on, int note, uint8_t velocity, uint8_t channel, int filter) {
	if (note < 0 || note >= 128) {
		return;
	}

	// This is the only place where velocity is limited like this. In the internal engine, it's allowed to go right between 0 and 128
	velocity = getMax((uint8_t)1, velocity);
	velocity = getMin((uint8_t)127, velocity);

	sendMidi(on ? 0x09 : 0x08, channel, note, velocity, filter);
}

void MidiEngine::sendAllNotesOff(int channel, int filter) {
	sendCC(channel, 123, 0, filter);
}

void MidiEngine::sendCC(int channel, int cc, int value, int filter) {
	if (value > 127) {
		value = 127;
	}
	sendMidi(0x0B, channel, cc, value, filter);
}

void MidiEngine::sendClock(bool sendUSB, int howMany) {
	while (howMany--) {
		sendMidi(0x0F, 0x08, 0, 0, MIDI_OUTPUT_FILTER_NO_MPE, sendUSB);
	}
}

void MidiEngine::sendStart() {
	playbackHandler.timeLastMIDIStartOrContinueMessageSent = AudioEngine::audioSampleTimer;
	sendMidi(0x0F, 0x0A);
}

void MidiEngine::sendContinue() {
	playbackHandler.timeLastMIDIStartOrContinueMessageSent = AudioEngine::audioSampleTimer;
	sendMidi(0x0F, 0x0B);
}

void MidiEngine::sendStop() {
	sendMidi(0x0F, 0x0C);
}

void MidiEngine::sendPositionPointer(uint16_t positionPointer) {
	uint8_t positionPointerLSB = positionPointer & (uint32_t)0x7F;
	uint8_t positionPointerMSB = (positionPointer >> 7) & (uint32_t)0x7F;
	sendMidi(0x0F, 0x02, positionPointerLSB, positionPointerMSB);
}

void MidiEngine::sendBank(int channel, int num, int filter) {
	sendCC(channel, 0, num, filter);
}

void MidiEngine::sendSubBank(int channel, int num, int filter) {
	sendCC(channel, 32, num, filter);
}

void MidiEngine::sendPGMChange(int channel, int pgm, int filter) {
	sendMidi(0x0C, channel, pgm, 0, filter);
}

void MidiEngine::sendPitchBend(int channel, uint8_t lsbs, uint8_t msbs, int filter) {
	sendMidi(0x0E, channel, lsbs, msbs, filter);
}

void MidiEngine::sendChannelAftertouch(int channel, uint8_t value, int filter) {
	sendMidi(0x0D, channel, value, 0, filter);
}

void MidiEngine::sendPolyphonicAftertouch(int channel, uint8_t value, uint8_t noteCode, int filter) {
	sendMidi(0x0A, channel, noteCode, value, filter);
}

void MidiEngine::sendMidi(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2, int filter, bool sendUSB) {
	// Send USB MIDI
	if (sendUSB) {
		sendUsbMidi(statusType, channel, data1, data2, filter);
	}

	// Send serial MIDI
	if (statusType == 0x0F || MIDIDeviceManager::dinMIDIPorts.wantsToOutputMIDIOnChannel(channel, filter)) {
		sendSerialMidi(statusType, channel, data1, data2);
	}
}

uint32_t setupUSBMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) {
	//format message per USB midi spec on virtual cable 0
	uint8_t cin;
	uint8_t firstByte = (channel & 15) | (statusType << 4);

	switch (firstByte) {
	case 0xF2: // Position pointer
		cin = 0x03;
		break;

	default:
		cin = statusType; // Good for voice commands
		break;
	}

	return ((uint32_t)data2 << 24) | ((uint32_t)data1 << 16) | ((uint32_t)firstByte << 8) | cin;
}

void MidiEngine::sendUsbMidi(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2, int filter) {
	//TODO: Differentiate between ports on usb midi

	//formats message per USB midi spec on virtual cable 0
	uint32_t fullMessage = setupUSBMessage(statusType, channel, data1, data2);

	for (int ip = 0; ip < USB_NUM_USBIP; ip++) {
		int potentialNumDevices = getPotentialNumConnectedUSBMIDIDevices(ip);

		for (int d = 0; d < potentialNumDevices; d++) {
			ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];
			int maxPort = connectedDevice->maxPortConnected;
			for (int p = 0; p <= maxPort; p++) {

				if (connectedDevice->device[p] && connectedDevice->canHaveMIDISent
				    && (statusType == 0x0F
				        || connectedDevice->device[p]->wantsToOutputMIDIOnChannel(channel, filter))) {

					//Or with the port to add the cable number to the full message. This
					//is a bit hacky but it works
					uint32_t channeled_message = fullMessage | (p << 4);
					connectedDevice->bufferMessage(channeled_message);
				}
			}
		}
	}
}

// Warning - this will sometimes (not always) be called in an ISR
void MidiEngine::flushMIDI() {
	flushUSBMIDIOutput();
	uartFlushIfNotSending(UART_ITEM_MIDI);
}

void MidiEngine::sendSerialMidi(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) {

	uint8_t statusByte = channel | (statusType << 4);
	int messageLength = getMidiMessageLength(statusByte);
	// If only 1 byte, we have to send it, and keep no record of last status byte sent since we couldn't do running status anyway
	if (false && messageLength == 1) {
		bufferMIDIUart(statusByte);
		lastStatusByteSent = 0;
	}

	// Or, if message is longer, only send if status byte is different from last time
	else if (true || statusByte != lastStatusByteSent) { // Temporarily disabled
		bufferMIDIUart(statusByte);
		lastStatusByteSent = statusByte;
	}

	if (messageLength >= 2) {
		bufferMIDIUart(data1);

		if (messageLength == 3) {
			bufferMIDIUart(data2);
		}
	}
}

bool MidiEngine::checkIncomingSerialMidi() {

	uint8_t thisSerialByte;
	uint32_t* timer = uartGetCharWithTiming(TIMING_CAPTURE_ITEM_MIDI, (char*)&thisSerialByte);
	if (timer) {
		//Uart::println((unsigned int)thisSerialByte);
		MIDIDevice* dev = &MIDIDeviceManager::dinMIDIPorts;

		// If this is a status byte, then we have to store it as the first byte.
		if (thisSerialByte & 0x80) {

			switch (thisSerialByte) {

			// If it's a realtime message, we have to obey it right now, separately from any other message it was inserted into the middle of
			case 0xF8 ... 0xFF:
				midiMessageReceived(&MIDIDeviceManager::dinMIDIPorts, thisSerialByte >> 4, thisSerialByte & 0x0F, 0, 0,
				                    timer);
				return true;

			// Or if it's a SysEx start...
			case 0xF0:
				currentlyReceivingSysExSerial = true;
				Uart::println("Sysex start");
				dev->incomingSysexBuffer[0] = thisSerialByte;
				dev->incomingSysexPos = 1;
				//numSerialMidiInput = 0; // This would throw away any running status stuff...
				return true;
			}

			// If we didn't return for any of those, then it's just a regular old status message (or Sysex stop message). All these will end any ongoing SysEx

			// If it was a Sysex stop, that's all we need to do
			if (thisSerialByte == 0xF7) {
				Uart::println("Sysex end");
				if (currentlyReceivingSysExSerial) {
					currentlyReceivingSysExSerial = false;
					if (dev->incomingSysexPos < sizeof dev->incomingSysexBuffer) {
						dev->incomingSysexBuffer[dev->incomingSysexPos++] = thisSerialByte;
						midiSysexReceived(-1, -1, 0, dev->incomingSysexBuffer, dev->incomingSysexPos);
					}
				}
				return true;
			}

			currentlyReceivingSysExSerial = false;
			numSerialMidiInput = 0;
		}

		// If not a status byte...
		else {
			// If we're currently receiving a SysEx, don't throw it away
			if (currentlyReceivingSysExSerial) {
				// TODO: allocate a GMA buffer to some bigger size
				if (dev->incomingSysexPos < sizeof dev->incomingSysexBuffer) {
					dev->incomingSysexBuffer[dev->incomingSysexPos++] = thisSerialByte;
				}
				Uart::print("Sysex: ");
				Uart::println(thisSerialByte);
				return true;
			}

			// Ensure that we're not writing to the first byte of the buffer
			if (numSerialMidiInput == 0) {
				return true;
			}
		}

		serialMidiInput[numSerialMidiInput] = thisSerialByte;
		numSerialMidiInput++;

		// If we've received the whole MIDI message, deal with it
		if (getMidiMessageLength(serialMidiInput[0]) == numSerialMidiInput) {
			uint8_t channel = serialMidiInput[0] & 0x0F;

			midiMessageReceived(&MIDIDeviceManager::dinMIDIPorts, serialMidiInput[0] >> 4, channel, serialMidiInput[1],
			                    serialMidiInput[2], timer);

			// If message was more than 1 byte long, and was a voice or mode message, then allow for running status
			if (numSerialMidiInput > 1 && ((serialMidiInput[0] & 0xF0) != 0xF0)) {
				numSerialMidiInput = 1;
			}
			else {
				numSerialMidiInput = 0;
			}
		}
		return true;
	}
	return false;
}

// Lock USB before calling this!
void MidiEngine::setupUSBHostReceiveTransfer(int ip, int midiDeviceNum) {
	connectedUSBMIDIDevices[ip][midiDeviceNum].currentlyWaitingToReceive = 1;

	int pipeNumber = g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][midiDeviceNum][USB_EPL];

	g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum].keyword = pipeNumber;
	g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum].tranlen = 64;

	g_p_usb_pipe[pipeNumber] = &g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum];

	//uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];

	usb_receive_start_rohan_midi(pipeNumber);

	/*
    uint16_t endTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
	uint16_t duration = endTime - startTime;
	uint32_t timePassedNS = superfastTimerCountToNS(duration);
	uartPrint("send setup duration, nSec: ");
	uartPrintNumber(timePassedNS);
	*/
}

uint8_t usbCurrentlyInitialized = false;

void MidiEngine::checkIncomingUsbSysex(uint8_t const* msg, int ip, int d, int cable) {
	ConnectedUSBMIDIDevice* connected = &connectedUSBMIDIDevices[ip][d];
	if (cable > connectedUSBMIDIDevices[ip][d].maxPortConnected) {
		//fallback to cable 0 since we don't support more than one port on hosted devices yet
		cable = 0;
	}
	MIDIDevice* dev = connectedUSBMIDIDevices[ip][d].device[cable];

	uint8_t statusType = msg[0] & 15;
	int to_read = 0;
	bool will_end = false;
	if (statusType == 0x4) {
		// sysex start or continue
		if (msg[1] == 0xf0) {
			dev->incomingSysexPos = 0;
		}
		to_read = 3;
	}
	else if (statusType >= 0x5 && statusType <= 0x7) {
		to_read = statusType - 0x4; // read between 1-3 bytes
		will_end = true;
	}

	for (int i = 0; i < to_read; i++) {
		if (dev->incomingSysexPos >= sizeof(dev->incomingSysexBuffer)) {
			// TODO: allocate a GMA buffer to some bigger size
			dev->incomingSysexPos = 0;
			return; // bail out
		}
		dev->incomingSysexBuffer[dev->incomingSysexPos++] = msg[i + 1];
	}

	if (will_end) {
		if (dev->incomingSysexBuffer[0] == 0xf0) {
			midiSysexReceived(ip, d, cable, dev->incomingSysexBuffer, dev->incomingSysexPos);
		}
		dev->incomingSysexPos = 0;
	}
}

void MidiEngine::midiSysexReceived(int ip, int d, int cable, uint8_t* data, int len) {
	if (len < 4) {
		return;
	}

	// placeholder until we get a real manufacturer id.
	if (data[1] == 0x7D) {
		switch (data[2]) {
		case 0: // PING test message, reply
		{
			uint8_t pong[] = {0xf0, data[1], 0x7f, 0x00, 0xf7};
			if (len >= 5) {
				pong[3] = data[3];
			}
			sendSysex(ip, d, cable, pong, sizeof pong);
		} break;

		case 1:
			numericDriver.displayPopup(HAVE_OLED ? "hello sysex" : "SYSX");
			break;

		case 0x7f: // PONG, reserved
		default:
			break;
		}
	}
}

void MidiEngine::sendSysex(int ip, int d, int cable, uint8_t* data, int len) {
	if (len < 4 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return;
	}

	if (ip < 0) {
		// NB: beware of MIDI_TX_BUFFER_SIZE
		for (int i = 0; i < len; i++) {
			bufferMIDIUart(data[i]);
		}
	}
	else {
		int potentialNumDevices = getPotentialNumConnectedUSBMIDIDevices(ip);

		if (d >= potentialNumDevices) {
			return;
		}
		ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];
		int maxPort = connectedDevice->maxPortConnected;
		if (cable > maxPort) {
			return;
		}

		int pos = 0;
		while (pos < len) {
			int status, byte0 = 0, byte1 = 0, byte2 = 0;
			byte0 = data[pos];
			if (pos == 0 || len - pos > 3) {
				status = 0x4; // sysex start or continue
				byte1 = data[pos + 1];
				byte2 = data[pos + 2];
				pos += 3;
			}
			else {
				status = 0x4 + (len - pos); // sysex end with N bytes
				if ((len - pos) > 1) {
					byte1 = data[pos + 1];
				}
				if ((len - pos) > 2) {
					byte2 = data[pos + 2];
				}
				pos = len;
			}
			status |= (cable << 4);
			uint32_t packed = ((uint32_t)byte2 << 24) | ((uint32_t)byte1 << 16) | ((uint32_t)byte0 << 8) | status;
			connectedDevice->bufferMessage(packed);
		}
	}
}

void MidiEngine::checkIncomingUsbMidi() {

	if (!usbCurrentlyInitialized) {
		return;
	}

	bool usbLockNow = usbLock;

	if (!usbLockNow) {
		// Have to call this regularly, to do "callbacks" that will grab out the received data
		usbLock = 1;
		usb_cstd_usb_task();
		usbLock = 0;
	}

	for (int ip = 0; ip < USB_NUM_USBIP; ip++) {

		bool aPeripheral = (g_usb_usbmode != USB_HOST);
		if (aPeripheral && !g_usb_peri_connected) {
			continue;
		}
		//assumes only single device in peripheral mode
		int numDevicesNow = aPeripheral ? 1 : MAX_NUM_USB_MIDI_DEVICES;

		for (int d = 0; d < numDevicesNow; d++) {
			if (connectedUSBMIDIDevices[ip][d].device[0] && !connectedUSBMIDIDevices[ip][d].currentlyWaitingToReceive) {

				int bytesReceivedHere = connectedUSBMIDIDevices[ip][d].numBytesReceived;
				if (bytesReceivedHere) {

					connectedUSBMIDIDevices[ip][d].numBytesReceived = 0;

					__restrict__ uint8_t const* readPos = connectedUSBMIDIDevices[ip][d].receiveData;
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
								checkIncomingUsbSysex(readPos, ip, d, cable);
								continue;
							}
						}
						//select appropriate device based on the cable number
						if (cable > connectedUSBMIDIDevices[ip][d].maxPortConnected) {
							//fallback to cable 0 since we don't support more than one port on hosted devices yet
							cable = 0;
						}
						midiMessageReceived(connectedUSBMIDIDevices[ip][d].device[cable], statusType, channel, data1,
						                    data2, &timeLastBRDY[ip]);
					}
				}

				if (usbLockNow) {
					continue;
				}

				// And maybe setup transfer to receive more data

				// As peripheral
				if (aPeripheral) {

					g_usb_midi_recv_utr[ip][0].keyword = USB_CFG_PMIDI_BULK_IN;
					g_usb_midi_recv_utr[ip][0].tranlen = 64;

					connectedUSBMIDIDevices[ip][0].currentlyWaitingToReceive = 1;

					usbLock = 1;
					g_p_usb_pipe[USB_CFG_PMIDI_BULK_IN] = &g_usb_midi_recv_utr[ip][0];
					usb_receive_start_rohan_midi(USB_CFG_PMIDI_BULK_IN);
					usbLock = 0;
				}

				// Or as host
				else if (connectedUSBMIDIDevices[ip][d].device[0]) {

					// Only allowed to setup receive-transfer if not in the process of sending to various devices.
					// (Wait, still? Was this just because of that insane bug that's now fixed?)
					if (usbDeviceNumBeingSentToNow[ip] == stopSendingAfterDeviceNum[ip]) {
						usbLock = 1;
						setupUSBHostReceiveTransfer(ip, d);
						usbLock = 0;
					}
				}
			}
		}
	}
}

int MidiEngine::getMidiMessageLength(uint8_t statusByte) {
	if (statusByte == 0xF0    // System exclusive - dynamic length
	    || statusByte == 0xF4 // Undefined
	    || statusByte == 0xF5 // Undefined
	    || statusByte == 0xF6 // Tune request
	    || statusByte == 0xF7 // End of exclusive
	    || statusByte == 0xF8 // Timing clock
	    || statusByte == 0xF9 // Undefined
	    || statusByte == 0xFA // Start
	    || statusByte == 0xFB // Continue
	    || statusByte == 0xFC // Stop
	    || statusByte == 0xFD // Undefined
	    || statusByte == 0xFE // Active sensing
	    || statusByte == 0xFF // Reset
	) {
		return 1;
	}
	else if (statusByte == 0xF1         // Timecode
	         || statusByte == 0xF3      // Song select
	         || statusByte >> 4 == 0x0C // Program change
	         || statusByte >> 4 == 0x0D // Channel aftertouch
	) {
		return 2;
	}
	else {
		return 3;
	}
}

#define MISSING_MESSAGE_CHECK 0

#if MISSING_MESSAGE_CHECK
bool lastWasNoteOn = false;
#endif

void MidiEngine::midiMessageReceived(MIDIDevice* fromDevice, uint8_t statusType, uint8_t channel, uint8_t data1,
                                     uint8_t data2, uint32_t* timer) {

	bool shouldDoMidiThruNow = midiThru;

	// Make copies of these, cos we might modify the variables, and we need the originals to do MIDI thru at the end.
	uint8_t originalStatusType = statusType;
	uint8_t originalData2 = data2;

	if (statusType == 0x0F) {
		if (channel == 0x02) {
			if (currentSong) {
				playbackHandler.positionPointerReceived(data1, data2);
			}
		}
		else if (channel == 0x0A) {
			playbackHandler.startMessageReceived();
		}
		else if (channel == 0x0B) {
			playbackHandler.continueMessageReceived();
		}
		else if (channel == 0x0C) {
			playbackHandler.stopMessageReceived();
		}
		else if (channel == 0x08) {
			uint32_t time = 0;
			if (timer) {
				time = *timer;
			}
			playbackHandler.clockMessageReceived(time);
		}
	}
	else {

		// All these messages, we should only interpret if there's definitely a song loaded
		if (currentSong) {

			switch (statusType) {

			case 0x09: // Note on
				// If velocity 0, interpret that as a note-off.
				if (!data2) {
					data2 = DEFAULT_LIFT_VALUE;
					statusType = 0x08;
				}
				// No break

			case 0x08: // Note off, and note on continues here too
				playbackHandler.noteMessageReceived(fromDevice, statusType & 1, channel, data1, data2,
				                                    &shouldDoMidiThruNow);
#if MISSING_MESSAGE_CHECK
				if (lastWasNoteOn == (bool)(statusType & 1))
					numericDriver.freezeWithError("MISSED!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
				lastWasNoteOn = statusType & 1;
#endif
				break;

			case 0x0A: // Polyphonic aftertouch
				playbackHandler.aftertouchReceived(fromDevice, channel, data2, data1, &shouldDoMidiThruNow);
				break;

			case 0x0B: // CC or channel mode message
				// CC
				if (data1 < 120) {

					// Interpret RPN stuff, before we additionally try to process the CC within the song, in case it means something different to the user.
					switch (data1) {
					case 100: // RPN LSB
						fromDevice->inputChannels[channel].rpnLSB = data2;
						break;

					case 101: // RPN MSB
						fromDevice->inputChannels[channel].rpnMSB = data2;
						break;

					case 6: { // (RPN) data entry MSB
						char modelStackMemory[MODEL_STACK_MAX_SIZE];
						ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
						fromDevice->dataEntryMessageReceived(modelStack, channel, data2);
						break;
					}
					}

					playbackHandler.midiCCReceived(fromDevice, channel, data1, data2, &shouldDoMidiThruNow);
				}

				// Channel mode
				else {

					// All notes off
					if (data1 == 123) {
						if (data2 == 0) {
							playbackHandler.noteMessageReceived(fromDevice, false, channel, -32768, DEFAULT_LIFT_VALUE,
							                                    NULL);
						}
					}
				}
				break;

			case 0x0C: // Program change message
				playbackHandler.programChangeReceived(channel, data1);
				break;

			case 0x0D: // Channel pressure
				playbackHandler.aftertouchReceived(fromDevice, channel, data1, -1, &shouldDoMidiThruNow);
				break;

			case 0x0E: // Pitch bend
				playbackHandler.pitchBendReceived(fromDevice, channel, data1, data2, &shouldDoMidiThruNow);
				break;
			}
		}
	}

	// Do MIDI-thru if that's on and we didn't decide not to, above. This will let clock messages through along with all other messages, rather than using our special clock-specific system
	if (shouldDoMidiThruNow) {
		bool shouldSendUSB =
		    (fromDevice == &MIDIDeviceManager::dinMIDIPorts); // Only send out on USB if it didn't originate from USB
		sendMidi(originalStatusType, channel, data1, originalData2, MIDI_OUTPUT_FILTER_NO_MPE,
		         shouldSendUSB); // TODO: reconsider interaction with MPE?
	}
}
