/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "midi_queue_definitions.h"
#ifdef __cplusplus
#include "definitions_cxx.hpp"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_common.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/midi_queue_manager.h"
#include "model/midi/message.h"
#include "util/container/vector/named_thing_vector.h"
class Serializer;
class Deserializer;

#else
#include "definitions.h"
struct MIDICableUSB;
#endif

#ifdef __cplusplus
/*
 * A ConnectedUSBMIDIDevice is used directly to interface with the USB driver
 * ConnectedUSBMIDIDevice holds per-device USB MIDI transfer state.
 *
 * Outgoing MIDI is queued in a private MIDIQueueManagerUSB, drained into
 * dataSendingNow when a USB transfer starts, and sent with dataSendingNow as the
 * USB pipe transfer buffer. Sends can also be triggered by the midiAndGateOutput
 * interrupt.
 *
 * Reads are more complicated.
 * Actual reads are done by usb_cstd_usb_task, which has a commented out interrupt associated
 * The function is instead called in the midiengine::checkincomingUSBmidi function, which is called
 * in the audio engine loop
 *
 * The USB read function is configured by setupUSBHostReceiveTransfer, which is called to
 * setup the next device after each successful read. Data is written directly into the receiveData
 * array from the USB device, it's set as the USB pipe address during midi engine setup
 */
class ConnectedUSBMIDIDevice {
public:
	MIDICableUSB* cable[4]; // If NULL, then no cable is connected here
	ConnectedUSBMIDIDevice();
	/// Classifies, optionally coalesces, and enqueues one outgoing MIDI message into USB priority lanes.
	void enqueue_message(uint32_t fullMessage);
	void setup();
	/// Drains queued USB messages into the hardware-send buffer.
	bool consume_queued_messages();
	/// Queue occupancy check (boolean form): true when any USB lane has queued output.
	/// Conceptually matches DIN `has_serial_data()`.
	bool hasBufferedSendData();
	/// Remaining USB queue capacity reported as MIDI payload bytes.
	int sendBufferSpace();
#else
// warning - accessed as a C struct from usb driver
struct ConnectedUSBMIDIDevice {
	struct MIDICableUSB* device[4];
#endif
	uint8_t currentlyWaitingToReceive;
	uint8_t sq; // Only for connections as HOST
	uint8_t canHaveMIDISent;
	uint16_t numBytesReceived;
	// Receive transfers are armed for one 64-byte packet at a time, but the peripheral-mode bulk pipe is
	// double-buffered, so its BRDY handler can have to drain up to two packets (plus margin) in one go rather than
	// dropping what the hardware already ACKed - see usb_pstd_brdy_pipe_process_rohan_midi().
	__attribute__((aligned(8))) uint8_t receiveData[192];

	// This buffer is passed directly to the USB driver, and is limited to what the hardware allows
	uint8_t dataSendingNow[MIDI_SEND_BUFFER_LEN_INNER * 4];
	// This will show a value after the general flush function is called, throughout other Devices being sent to before
	// this one, and until we've completed our send
	uint8_t numBytesSendingNow;
	uint8_t maxPortConnected;

#ifdef __cplusplus

private:
	MIDIQueueManagerUSB queue_manager_{};
#endif
};

#ifdef __cplusplus
class ConnectedDINMIDIDevice {
public:
	ConnectedDINMIDIDevice();

	/// Resets DIN pacing/allowance state to a known baseline at the provided sample timestamp.
	/// Note: this does not clear queued DIN bytes.
	void reset_serial_state(uint32_t now_sample_timer);
	/// Queue occupancy check (boolean form): true when any DIN lane has queued output.
	/// Conceptually matches USB `hasBufferedSendData()`.
	[[nodiscard]] bool has_serial_data() const;
	/// Remaining DIN queue capacity for raw SysEx bytes.
	[[nodiscard]] size_t send_buffer_space() const;
	/// Classifies, optionally coalesces, and enqueues one outgoing MIDI message into DIN priority lanes.
	void enqueue_message(MIDIMessage message);
	/// Queues one complete SysEx byte stream into DIN priority lanes.
	bool enqueue_sysex(uint8_t const* data, int32_t len);
	/// Drains queued DIN bytes into UART using send allowance, lane priorities, and CC gating.
	void consume_queued_messages(uint32_t now_sample_timer);

private:
	MIDIQueueManagerDIN queue_manager_{};
};
#endif

#ifdef __cplusplus
namespace MIDIDeviceManager {

void slowRoutine();
MIDICable* readDeviceReferenceFromFile(Deserializer& reader);
void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory);
void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory);
void recountSmallestMPEZones();
void writeDevicesToFile();
void readAHostedDeviceFromFile(Deserializer& reader);
void readDevicesFromFile();
void factoryReset(bool showPopup = true);

extern MIDICableUSBUpstream upstreamUSBMIDICable1;
extern MIDICableUSBUpstream upstreamUSBMIDICable2;
extern MIDICableUSBUpstream upstreamUSBMIDICable3;
extern MIDICableDINPorts dinMIDIPorts;

extern bool differentiatingInputsByDevice;

extern NamedThingVector hostedMIDIDevices;

extern uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput;
extern uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput;
extern bool anyChangesToSave;
} // namespace MIDIDeviceManager

#endif

extern struct ConnectedUSBMIDIDevice connectedUSBMIDIDevices[][MAX_NUM_USB_MIDI_DEVICES];
#ifdef __cplusplus
extern ConnectedDINMIDIDevice connectedDINMIDIDevice;
#endif
