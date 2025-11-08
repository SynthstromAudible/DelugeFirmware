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
#include "definitions_cxx.hpp"
#include "deluge/io/usb/usb_state.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/sysex.h"
#include "mem_functions.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "processing/engines/audio_engine.h"
#include "storage/smsysex.h"
#include "version.h"

using namespace deluge::io::usb;

extern "C" {
#include "RZA1/uart/sio_char.h"

extern void usb_cstd_usb_task();
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

	usbSetup();

	eventStackTop_ = eventStack_.begin();
}

bool MidiEngine::anythingInOutputBuffer() {
	return anythingInUSBOutputBuffer || (bool)uartGetTxBufferFullnessByItem(UART_ITEM_MIDI);
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

void MidiEngine::sendNote(MIDISource source, bool on, int32_t note, uint8_t velocity, uint8_t channel, int32_t filter,
                          uint8_t deviceFilter) {
	if (note < 0 || note >= 128) {
		return;
	}

	// This is the only place where velocity is limited like this. In the internal engine, it's allowed to go right
	// between 0 and 128
	velocity = std::clamp<uint8_t>(velocity, 1, 127);

	if (on) {
		sendMidi(source, MIDIMessage::noteOn(channel, note, velocity), filter, true, deviceFilter);
	}
	else {
		sendMidi(source, MIDIMessage::noteOff(channel, note, velocity), filter, true, deviceFilter);
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
	auto& dinCable = MIDIDeviceManager::root_din.cable;
	if (dinCable.wantsToOutputMIDIOnChannel(message, filter)) {
		auto error = dinCable.sendMessage(message);
		if (error != Error::NONE && error != Error::NO_ERROR_BUT_GET_OUT) {
			D_PRINTLN("MIDI send error: %d", static_cast<int>(error));
		}
	}

	--eventStackTop_;
}

void MidiEngine::sendMidi(MIDISource source, MIDIMessage message, int32_t filter, bool sendUSB, uint8_t deviceFilter) {
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
		sendUsbMidi(message, filter, deviceFilter);
	}

	// Send serial MIDI (DIN)
	// deviceFilter: 0 = ALL, 1 = DIN only, 2+ = USB devices
	auto& dinCable = MIDIDeviceManager::root_din.cable;
	if (dinCable.wantsToOutputMIDIOnChannel(message, filter)) {
		// Send to DIN if device filter is 0 (ALL) or 1 (DIN)
		if (deviceFilter == 0 || deviceFilter == 1) {
			auto error = dinCable.sendMessage(message);
			if (error != Error::NONE && error != Error::NO_ERROR_BUT_GET_OUT) {
				D_PRINTLN("MIDI send error: %d", static_cast<int>(error));
			}
		}
	}

	--eventStackTop_;
}

void MidiEngine::sendUsbMidi(MIDIMessage message, int32_t filter) {
	// If no USB device is connected, don't send anything. Otherwise, we send to all cables.
	if (MIDIDeviceManager::root_usb == nullptr) {
		return;
	}

	for (auto& cable : MIDIDeviceManager::root_usb->getCables()) {
		if (cable.wantsToOutputMIDIOnChannel(message, filter)) {
			cable.sendMessage(message);
		}
	}
}

void MidiEngine::sendUsbMidi(MIDIMessage message, int32_t filter, uint8_t deviceFilter) {
	if (MIDIDeviceManager::root_usb == nullptr) {
		return;
	}

	// deviceFilter: 0 = ALL devices, 1 = DIN only, 2+ = specific USB device (index = deviceFilter - 2)
	if (deviceFilter == 0) {
		// Send to ALL USB devices
		for (auto& cable : MIDIDeviceManager::root_usb->getCables()) {
			if (cable.wantsToOutputMIDIOnChannel(message, filter)) {
				cable.sendMessage(message);
			}
		}
	}
	else if (deviceFilter == 1) {
		// DIN only - don't send to USB
		return;
	}
	else {
		// Send to specific USB device (deviceFilter 2 = USB index 0, deviceFilter 3 = USB index 1, etc.)
		uint32_t usbIndex = deviceFilter - 2;
		size_t numCables = MIDIDeviceManager::root_usb->getNumCables();
		if (usbIndex < numCables) {
			MIDICable* cable = MIDIDeviceManager::root_usb->getCable(usbIndex);
			if (cable != nullptr && cable->wantsToOutputMIDIOnChannel(message, filter)) {
				cable->sendMessage(message);
			}
		}
	}
}

void MidiEngine::checkIncomingMidi() {
	if (!usbLock) {
		// Have to call this regularly, to do "callbacks" that will grab out the received data
		USBAutoLock lock;
		usb_cstd_usb_task();
	}

	// Check incoming USB MIDI
	if (MIDIDeviceManager::root_usb != nullptr) {
		auto error = MIDIDeviceManager::root_usb->poll();
		if (error != Error::NONE && error != Error::NO_ERROR_BUT_GET_OUT) {
			D_PRINTLN("USB poll error: %d\n", static_cast<int>(error));
		}
	}

	// Check incoming Serial MIDI
	for (int32_t i = 0; i < 12; i++) {
		auto error = MIDIDeviceManager::root_din.poll();
		if (error == Error::NO_ERROR_BUT_GET_OUT) {
			break;
		}
		else if (error != Error::NONE) {
			D_PRINTLN("MIDI poll error: %d\n", static_cast<int>(error));
		}
	}
}

// Warning - this will sometimes (not always) be called in an ISR
void MidiEngine::flushMIDI() {
	if (MIDIDeviceManager::root_usb) {
		MIDIDeviceManager::root_usb->flush();
	}
	MIDIDeviceManager::root_din.flush();
}

// Lock USB before calling this!
uint8_t usbCurrentlyInitialized = false;

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
	unsigned payloadLength = len - payloadOffset;
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
		bool shouldSendUSB = (&cable == &MIDIDeviceManager::root_din.cable);
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
