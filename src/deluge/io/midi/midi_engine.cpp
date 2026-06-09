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

#include "io/midi/midi_engine.h"
#include "bsp/rza1/usb_midi.h" // BspUsbMidiDevice, bsp_usb_midi_* (transport state); receive decode still reaches in
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/sysex.h"
#include "libdeluge/midi_io.h"
#include "libdeluge/system.h"
#include "mem_functions.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "processing/engines/audio_engine.h"
#include "storage/smsysex.h"
#include "version.h"

extern "C" {

// App-owned re-entrancy guard for USB-MIDI servicing (shared with the BSP send
// path, which reads it during flush). Pumping and DIN/USB I/O now go through the
// <libdeluge/midi_io.h> boundary, so no HAL header is included here.
volatile uint32_t usbLock = 0;

// Send-path state lives in the BSP usb_midi module now; the receive decode below
// still reads these two (the host re-arm guard) until the receive path moves down.
extern uint8_t stopSendingAfterDeviceNum[DELUGE_USB_NUM_CONTROLLERS];
extern uint8_t usbDeviceNumBeingSentToNow[DELUGE_USB_NUM_CONTROLLERS];

// Timestamp of the last bulk-IN completion, recorded by the HAL and defined in the
// BSP usb_midi module; read here to timestamp incoming MIDI clock messages.
extern uint32_t timeLastBRDY[DELUGE_USB_NUM_CONTROLLERS];

// The USB-MIDI send and receive *transport* (transfer descriptors, transfer setup,
// completion handlers, the bulk-IN timestamp) now lives in the BSP usb_midi module.
// The application drives it through bsp_usb_midi_* and decodes the bytes the HAL
// leaves in the receive buffers.
}

PLACE_SDRAM_BSS MidiEngine midiEngine{};

MidiEngine::MidiEngine() {
	numSerialMidiInput = 0;
	currentlyReceivingSysExSerial = false;
	midiThru = false;
	for (auto& midiChannelType : midiFollowChannelType) {
		midiChannelType.clear();
	}
	midiFollowKitRootNote = 36;
	midiFollowDisplayParam = false;
	midiFollowFeedbackChannelType = MIDIFollowChannelType::NONE;
	midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
	midiFollowFeedbackFilter = false;
	midiTakeover = MIDITakeoverMode::JUMP;
	midiSelectKitRow = false;

	// The USB-MIDI transfer descriptors (send and receive) and g_usb_peri_connected
	// are set up by bsp_usb_midi_init(), called from MIDIDeviceManager::init() before
	// the USB stack is opened.

	eventStackTop_ = eventStack_.begin();
}

int32_t MidiEngine::getPotentialNumConnectedUSBMIDIDevices(int32_t ip) {
	bool potentiallyAHost = deluge_midi_usb_is_host();
	return potentiallyAHost ? MAX_NUM_USB_MIDI_DEVICES : 1;
}

bool MidiEngine::anythingInOutputBuffer() {
	return bsp_usb_midi_anything_in_output_buffer() || (deluge_midi_write_pending(DELUGE_MIDI_DIN) != 0);
}

void MidiEngine::sendNote(MIDISource source, bool on, int32_t note, uint8_t velocity, uint8_t channel, int32_t filter) {
	if (note < 0 || note >= 128) {
		return;
	}

	// This is the only place where velocity is limited like this. In the internal engine, it's allowed to go right
	// between 0 and 128
	velocity = std::max((uint8_t)1, velocity);
	velocity = std::min((uint8_t)127, velocity);

	if (on) {
		sendMidi(source, MIDIMessage::noteOn(channel, note, velocity), filter);
	}
	else {
		sendMidi(source, MIDIMessage::noteOff(channel, note, velocity), filter);
	}
}

void MidiEngine::sendAllNotesOff(MIDISource source, int32_t channel, int32_t filter) {
	sendMidi(source, MIDIMessage::cc(channel, 123, 0), filter);
}

void MidiEngine::sendCC(MIDISource source, int32_t channel, int32_t cc, int32_t value, int32_t filter) {
	if (value > 127) {
		value = 127;
	}
	sendMidi(source, MIDIMessage::cc(channel, cc, value), filter);
}

void MidiEngine::sendClock(MIDISource source, bool sendUSB, int32_t howMany) {
	while (howMany--) {
		sendMidi(source, MIDIMessage::realtimeClock(), kMIDIOutputFilterNoMPE, sendUSB);
	}
}

void MidiEngine::sendStart(MIDISource source) {
	playbackHandler.timeLastMIDIStartOrContinueMessageSent = AudioEngine::audioSampleTimer;
	sendMidi(source, MIDIMessage::realtimeStart());
}

void MidiEngine::sendContinue(MIDISource source) {
	playbackHandler.timeLastMIDIStartOrContinueMessageSent = AudioEngine::audioSampleTimer;
	sendMidi(source, MIDIMessage::realtimeContinue());
}

void MidiEngine::sendStop(MIDISource source) {
	sendMidi(source, MIDIMessage::realtimeStop());
}

void MidiEngine::sendPositionPointer(MIDISource source, uint16_t positionPointer) {
	sendMidi(source, MIDIMessage::systemPositionPointer(positionPointer));
}

void MidiEngine::sendBank(MIDISource source, int32_t channel, int32_t num, int32_t filter) {
	sendCC(source, channel, 0, num, filter);
}

void MidiEngine::sendSubBank(MIDISource source, int32_t channel, int32_t num, int32_t filter) {
	sendCC(source, channel, 32, num, filter);
}

void MidiEngine::sendPGMChange(MIDISource source, int32_t channel, int32_t pgm, int32_t filter) {
	sendMidi(source, MIDIMessage::programChange(channel, pgm), filter);
}

void MidiEngine::sendPitchBend(MIDISource source, int32_t channel, uint16_t bend, int32_t filter) {
	sendMidi(source, MIDIMessage::pitchBend(channel, bend), filter);
}

void MidiEngine::sendChannelAftertouch(MIDISource source, int32_t channel, uint8_t value, int32_t filter) {
	sendMidi(source, MIDIMessage::channelAftertouch(channel, value), filter);
}

void MidiEngine::sendPolyphonicAftertouch(MIDISource source, int32_t channel, uint8_t value, uint8_t noteCode,
                                          int32_t filter) {
	sendMidi(source, MIDIMessage::polyphonicAftertouch(channel, noteCode, value), filter);
}

void MidiEngine::sendMidi(MIDISource source, MIDIMessage message, int32_t filter, bool sendUSB) {
	if (eventStackTop_ == eventStack_.end()) {
		// We're somehow 16 messages deep, reject this message.
		return;
	}

	EventStackStorage::const_iterator stackSearchIt{eventStackTop_};
	while (stackSearchIt != eventStack_.begin()) {
		stackSearchIt--;
		if ((*stackSearchIt) == source) {
			// We've already processed an event from this source, avoid infinite recursion and reject it
			return;
		}
	}
	*eventStackTop_ = source;
	++eventStackTop_;

	// Send USB MIDI
	if (sendUSB) {
		sendUsbMidi(message, filter);
	}

	// Send serial MIDI
	if (MIDIDeviceManager::dinMIDIPorts.wantsToOutputMIDIOnChannel(message, filter)) {
		sendSerialMidi(message);
	}

	--eventStackTop_;
}

uint32_t setupUSBMessage(MIDIMessage message) {
	// format message per USB midi spec on virtual cable 0
	uint8_t cin;
	uint8_t firstByte = (message.channel & 15) | (message.statusType << 4);

	switch (firstByte) {
	case 0xF2: // Position pointer
		cin = 0x03;
		break;

	default:
		cin = message.statusType; // Good for voice commands
		break;
	}

	return ((uint32_t)message.data2 << 24) | ((uint32_t)message.data1 << 16) | ((uint32_t)firstByte << 8) | cin;
}

void MidiEngine::sendUsbMidi(MIDIMessage message, int32_t filter) {
	// TODO: Differentiate between ports on usb midi
	bool isSystemMessage = message.isSystemMessage();

	// formats message per USB midi spec on virtual cable 0
	uint32_t fullMessage = setupUSBMessage(message);
	for (int32_t ip = 0; ip < DELUGE_USB_NUM_CONTROLLERS; ip++) {
		int32_t potentialNumDevices = getPotentialNumConnectedUSBMIDIDevices(ip);

		for (int32_t d = 0; d < potentialNumDevices; d++) {
			ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];
			if (!connectedDevice->transport->canHaveMIDISent) {
				continue;
			}
			int32_t maxPort = connectedDevice->maxPortConnected;
			for (int32_t p = 0; p <= maxPort; p++) {
				// if device exists, it's not port 3 (for sysex)
				if (connectedDevice->cable[p]
				    && connectedDevice->cable[p] != &MIDIDeviceManager::upstreamUSBMIDICable3) {
					// if it's a clock (or sysex technically but we don't send that to this function)
					// or if it's a message that this channel wants
					if (connectedDevice->cable[p]->wantsToOutputMIDIOnChannel(message, filter)) {

						// Or with the port to add the cable number to the full message. This
						// is a bit hacky but it works
						uint32_t channeled_message = fullMessage | (p << 4);
						connectedDevice->bufferMessage(channeled_message);
					}
				}
			}
		}
	}
}

// Warning - this will sometimes (not always) be called in an ISR
void MidiEngine::flushMIDI() {
	bsp_usb_midi_flush_output();
	deluge_midi_flush(DELUGE_MIDI_DIN);
}

void MidiEngine::sendSerialMidi(MIDIMessage message) {

	uint8_t statusByte = message.channel | (message.statusType << 4);
	int32_t messageLength = bytesPerStatusMessage(statusByte);

	uint8_t bytes[3];
	bytes[0] = statusByte;
	if (messageLength >= 2) {
		bytes[1] = message.data1;
		if (messageLength == 3) {
			bytes[2] = message.data2;
		}
	}
	deluge_midi_write(DELUGE_MIDI_DIN, bytes, messageLength);
}

bool MidiEngine::checkIncomingSerialMidi() {

	uint8_t thisSerialByte;
	uint32_t arrivalTime;
	if (deluge_midi_din_read_timed(&thisSerialByte, &arrivalTime)) {
		uint32_t* timer = &arrivalTime;
		// D_PRINTLN((uint32_t)thisSerialByte);
		MIDICable& cable = MIDIDeviceManager::dinMIDIPorts;

		// If this is a status byte, then we have to store it as the first byte.
		if (thisSerialByte & 0x80) {

			switch (thisSerialByte) {

			// If it's a realtime message, we have to obey it right now, separately from any other message it was
			// inserted into the middle of
			case 0xF8 ... 0xFF:
				midiMessageReceived(cable, thisSerialByte >> 4, thisSerialByte & 0x0F, 0, 0, timer);
				return true;

			// Or if it's a SysEx start...
			case 0xF0:
				currentlyReceivingSysExSerial = true;
				D_PRINTLN("Sysex start");
				cable.incomingSysexBuffer[0] = thisSerialByte;
				cable.incomingSysexPos = 1;
				// numSerialMidiInput = 0; // This would throw away any running status stuff...
				return true;
			}

			// If we didn't return for any of those, then it's just a regular old status message (or Sysex stop
			// message). All these will end any ongoing SysEx

			// If it was a Sysex stop, that's all we need to do
			if (thisSerialByte == 0xF7) {
				D_PRINTLN("Sysex end");
				if (currentlyReceivingSysExSerial) {
					currentlyReceivingSysExSerial = false;
					if (cable.incomingSysexPos < sizeof cable.incomingSysexBuffer) {
						cable.incomingSysexBuffer[cable.incomingSysexPos++] = thisSerialByte;
						midiSysexReceived(cable, cable.incomingSysexBuffer, cable.incomingSysexPos);
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
				if (cable.incomingSysexPos < sizeof cable.incomingSysexBuffer) {
					cable.incomingSysexBuffer[cable.incomingSysexPos++] = thisSerialByte;
				}
				D_PRINTLN("Sysex:  %d", thisSerialByte);
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
		if (bytesPerStatusMessage(serialMidiInput[0]) == numSerialMidiInput) {
			uint8_t channel = serialMidiInput[0] & 0x0F;

			midiMessageReceived(MIDIDeviceManager::dinMIDIPorts, serialMidiInput[0] >> 4, channel, serialMidiInput[1],
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

uint8_t usbCurrentlyInitialized = false;

void MidiEngine::checkIncomingUsbSysex(uint8_t const* msg, int32_t ip, int32_t d, int32_t cableIdx) {
	ConnectedUSBMIDIDevice* connected = &connectedUSBMIDIDevices[ip][d];
	if (cableIdx > connectedUSBMIDIDevices[ip][d].maxPortConnected) {
		// fallback to cable 0 since we don't support more than one port on hosted devices yet
		cableIdx = 0;
	}
	MIDICable& cable = *connectedUSBMIDIDevices[ip][d].cable[cableIdx];

	uint8_t statusType = msg[0] & 15;
	int32_t to_read = 0;
	bool will_end = false;
	if (statusType == 0x4) {
		// sysex start or continue
		if (msg[1] == 0xf0) {
			cable.incomingSysexPos = 0;
		}
		to_read = 3;
	}
	else if (statusType >= 0x5 && statusType <= 0x7) {
		to_read = statusType - 0x4; // read between 1-3 bytes
		will_end = true;
	}

	for (int32_t i = 0; i < to_read; i++) {
		if (cable.incomingSysexPos >= sizeof(cable.incomingSysexBuffer)) {
			// TODO: allocate a GMA buffer to some bigger size
			cable.incomingSysexPos = 0;
			return; // bail out
		}
		cable.incomingSysexBuffer[cable.incomingSysexPos++] = msg[i + 1];
	}

	if (will_end) {
		if (cable.incomingSysexBuffer[0] == 0xf0) {
			midiSysexReceived(cable, cable.incomingSysexBuffer, cable.incomingSysexPos);
		}
		cable.incomingSysexPos = 0;
	}
}

bool developerSysexCodeReceived = false;

void MidiEngine::midiSysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {
	if (len < 4) {
		return;
	}
	unsigned payloadOffset = 2;
	// Non-real time universal SysEx broadcast
	if (data[1] == SysEx::SYSEX_UNIVERSAL_NONRT && data[2] == 0x7f) {
		// Identity request
		if (data[3] == SysEx::SYSEX_UNIVERSAL_IDENTITY && data[4] == 0x01) {
			const uint8_t reply[] = {
			    SysEx::SYSEX_START, SysEx::SYSEX_UNIVERSAL_NONRT,
			    0x7f, // Device channel, we don't have one yet
			    SysEx::SYSEX_UNIVERSAL_IDENTITY, 0x02,
			    // Manufacturer ID
			    SysEx::DELUGE_SYSEX_ID_BYTE0, SysEx::DELUGE_SYSEX_ID_BYTE1, SysEx::DELUGE_SYSEX_ID_BYTE2,
			    // 14bit device family LSB, MSB
			    SysEx::DELUGE_SYSEX_ID_BYTE3, 0,
			    // 14bit device family member LSB, MSB
			    0, 0,
			    // Four byte firmware version in human readable order
			    FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH, 0, SysEx::SYSEX_END};
			cable.sendSysex(reply, sizeof(reply));
		}
		return;
	}

	if (data[1] == SysEx::DELUGE_SYSEX_ID_BYTE0 && data[2] == SysEx::DELUGE_SYSEX_ID_BYTE1
	    && data[3] == SysEx::DELUGE_SYSEX_ID_BYTE2 && data[4] == SysEx::DELUGE_SYSEX_ID_BYTE3) {
		payloadOffset = 5;
		developerSysexCodeReceived = false;
	}
	else {
		if (data[1] != 0x7D)
			return;
		developerSysexCodeReceived = true;
	}
	// The payload includes the msgID and the ending 0F0
	int payloadLength = len - payloadOffset;
	if (payloadLength < 1) {
		// in this case it's garbage so just ignore it
		return;
	}
	uint8_t* payloadStart = data + payloadOffset;
	switch (data[payloadOffset]) {
	case SysEx::SysexCommands::Ping: // PING test message, reply
	{
		uint8_t longPong[] = {0xf0, 0x00, 0x21, 0x7B, 0x01, 0x7f, 0x00, 0xf7};
		if (len >= 8) {
			longPong[6] = data[6];
		}
		cable.sendSysex(longPong, sizeof longPong);
	} break;

	case SysEx::SysexCommands::Popup:
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_HELLO_SYSEX));
		break;

	case SysEx::SysexCommands::HID:
		HIDSysex::sysexReceived(cable, payloadStart, payloadLength);
		break;

	case SysEx::SysexCommands::Debug:
		// debug namespace: for sysex calls useful for debugging purposes
		// and/or might require a debug build to function.
		Debug::sysexReceived(cable, payloadStart, payloadLength);
		break;

	case SysEx::SysexCommands::Json:
		smSysex::sysexReceived(cable, payloadStart, payloadLength);
		break;

	case SysEx::SysexCommands::Pong: // PONG, reserved
		D_PRINTLN("Pong");
	default:
		break;
	}
}

extern "C" {

extern uint8_t currentlyAccessingCard;
}

void MidiEngine::check_incoming_usb() {
	bool usbLockNow = usbLock;

	if (!usbLockNow) {
		// Service the MIDI transport regularly: this pumps the HAL USB-stack task and
		// drains the pipe completions it records (received bytes, chained sends). The
		// usbLock guard stays app-side as the gate against re-entrant servicing.
		usbLock = 1;
		deluge_midi_service();
		usbLock = 0;
	}
}

void MidiEngine::checkIncomingUsbMidi() {

	if (!usbCurrentlyInitialized
	    || currentlyAccessingCard != 0) { // hack to avoid SysEx handlers clashing with other sd-card activity.
		if (currentlyAccessingCard != 0) {
			// D_PRINTLN("checkIncomingUsbMidi seeing currentlyAccessingCard non-zero");
		}
		return;
	}
	bool usbLockNow = usbLock;
	check_incoming_usb();

	for (int32_t ip = 0; ip < DELUGE_USB_NUM_CONTROLLERS; ip++) {

		bool aPeripheral = !deluge_midi_usb_is_host();
		if (aPeripheral && !deluge_midi_usb_peripheral_connected()) {
			continue;
		}
		// assumes only single device in peripheral mode
		int32_t numDevicesNow = aPeripheral ? 1 : MAX_NUM_USB_MIDI_DEVICES;

		for (int32_t d = 0; d < numDevicesNow; d++) {
			if (connectedUSBMIDIDevices[ip][d].cable[0]
			    && !connectedUSBMIDIDevices[ip][d].transport->currentlyWaitingToReceive) {

				int32_t bytesReceivedHere = connectedUSBMIDIDevices[ip][d].transport->numBytesReceived;
				if (bytesReceivedHere) {
					connectedUSBMIDIDevices[ip][d].transport->numBytesReceived = 0;

					uint8_t const* readPos = connectedUSBMIDIDevices[ip][d].transport->receiveData;
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
						if (data1 & 0x80 || data2 & 0x80) {
							// This shouldn't be possible for non-sysex messages, indicates an error in
							// transmission so just ignore the rest of the frame
							break;
						}
						// select appropriate device based on the cable number
						if (cable > connectedUSBMIDIDevices[ip][d].maxPortConnected) {
							// fallback to cable 0 since we don't support more than one port on hosted devices yet
							cable = 0;
						}
						midiMessageReceived(*connectedUSBMIDIDevices[ip][d].cable[cable], statusType, channel, data1,
						                    data2, &timeLastBRDY[ip]);
					}
				}

				if (usbLockNow) {
					continue;
				}

				// And maybe setup transfer to receive more data

				// As peripheral
				if (aPeripheral) {
					usbLock = 1;
					bsp_usb_midi_rearm_peripheral_receive(ip);
					usbLock = 0;
				}

				// Or as host
				else if (connectedUSBMIDIDevices[ip][d].cable[0]) {

					// Only allowed to setup receive-transfer if not in the process of sending to various devices.
					// (Wait, still? Was this just because of that insane bug that's now fixed?)
					if (usbDeviceNumBeingSentToNow[ip] == stopSendingAfterDeviceNum[ip]) {
						usbLock = 1;
						bsp_usb_midi_setup_host_receive_transfer(ip, d);
						usbLock = 0;
					}
				}
			}
		}
	}
}

#define MISSING_MESSAGE_CHECK 0

#if MISSING_MESSAGE_CHECK
bool lastWasNoteOn = false;
#endif

void MidiEngine::midiMessageReceived(MIDICable& cable, uint8_t statusType, uint8_t channel, uint8_t data1,
                                     uint8_t data2, uint32_t* timer) {

	bool shouldDoMidiThruNow = midiThru;

	// Make copies of these, cos we might modify the variables, and we need the originals to do MIDI thru at the end.
	uint8_t originalStatusType = statusType;
	uint8_t originalData2 = data2;

	if (statusType == 0x0F && cable.receiveClock) {
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
					data2 = kDefaultLiftValue;
					statusType = 0x08;
				}
				// No break

			case 0x08: // Note off, and note on continues here too
				playbackHandler.noteMessageReceived(cable, statusType & 1, channel, data1, data2, &shouldDoMidiThruNow);
#if MISSING_MESSAGE_CHECK
				if (lastWasNoteOn == (bool)(statusType & 1))
					FREEZE_WITH_ERROR("MISSED!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
				lastWasNoteOn = statusType & 1;
#endif
				break;

			case 0x0A: // Polyphonic aftertouch
				playbackHandler.aftertouchReceived(cable, channel, data2, data1, &shouldDoMidiThruNow);
				break;

			case 0x0B: // CC or channel mode message
				// CC
				if (data1 < 120) {

					// Interpret RPN stuff, before we additionally try to process the CC within the song, in case it
					// means something different to the user.
					switch (data1) {
					case 100: // RPN LSB
						cable.inputChannels[channel].rpnLSB = data2;
						break;

					case 101: // RPN MSB
						cable.inputChannels[channel].rpnMSB = data2;
						break;

					case 6: { // (RPN) data entry MSB
						char modelStackMemory[MODEL_STACK_MAX_SIZE];
						ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
						cable.dataEntryMessageReceived(modelStack, channel, data2);
						break;
					}
					default: { // not an rpn - let's reset the msb/lsb
						cable.inputChannels[channel].rpnLSB = 0x7F;
						cable.inputChannels[channel].rpnMSB = 0x7F;
					}
					}

					playbackHandler.midiCCReceived(cable, channel, data1, data2, &shouldDoMidiThruNow);
				}

				// Channel mode
				else {

					// All notes off
					if (data1 == 123) {
						if (data2 == 0) {
							playbackHandler.noteMessageReceived(cable, false, channel, -32768, kDefaultLiftValue,
							                                    nullptr);
						}
					}
				}
				break;

			case 0x0C: // Program change message
				playbackHandler.programChangeReceived(cable, channel, data1);
				break;

			case 0x0D: // Channel pressure
				playbackHandler.aftertouchReceived(cable, channel, data1, -1, &shouldDoMidiThruNow);
				break;

			case 0x0E: // Pitch bend
				playbackHandler.pitchBendReceived(cable, channel, data1, data2, &shouldDoMidiThruNow);
				break;
			}
		}
	}

	// Do MIDI-thru if that's on and we didn't decide not to, above. This will let clock messages through along with all
	// other messages, rather than using our special clock-specific system
	if (shouldDoMidiThruNow) {
		// Only send out on USB if it didn't originate from USB
		bool shouldSendUSB = (&cable == &MIDIDeviceManager::dinMIDIPorts);
		// TODO: reconsider interaction with MPE?
		sendMidi(cable,
		         MIDIMessage{
		             .statusType = originalStatusType,
		             .channel = channel,
		             .data1 = data1,
		             .data2 = originalData2,
		         },
		         kMIDIOutputFilterNoMPE, shouldSendUSB);
	}
}
