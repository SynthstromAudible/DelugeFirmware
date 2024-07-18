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

#include "io/midi/midi_device_manager.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "storage/storage_manager.h"
#include "util/container/vector/named_thing_vector.h"
#include "util/misc.h"

extern "C" {
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_basic_define.h"
#include "drivers/uart/uart.h"

extern uint8_t anyUSBSendingStillHappening[];
}
#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

PLACE_INTERNAL_FRUNK ConnectedUSBMIDIDevice connectedUSBMIDIDevices[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

namespace MIDIDeviceManager {

NamedThingVector hostedMIDIDevices{__builtin_offsetof(MIDIDeviceUSBHosted, name)};

bool differentiatingInputsByDevice = true;

struct {
	String name{};
	uint16_t vendorId;
	uint16_t productId;
} usbDeviceCurrentlyBeingSetUp[USB_NUM_USBIP] = {};

// This class represents a thing you can send midi too,
// the virtual cable is an implementation detail
MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port1{0};
MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port2{1};
MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port3{2};
MIDIDeviceDINPorts dinMIDIPorts{};
MIDIDeviceLoopback loopbackMidi{};

uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

bool anyChangesToSave = false;

// Gets called within UITimerManager, which may get called during SD card routine.
void slowRoutine() {
	upstreamUSBMIDIDevice_port1.sendMCMsNowIfNeeded();
	upstreamUSBMIDIDevice_port2.sendMCMsNowIfNeeded();
	// port3 is not used for channel data

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);
		device->sendMCMsNowIfNeeded();

		// This routine placed here because for whatever reason we can't send sysex from hostedDeviceConfigured
		if (device->freshly_connected) {
			device->hookOnConnected();
			device->freshly_connected = false; // Must be set to false here or the hook will run forever
		}
	}
}

extern "C" void giveDetailsOfDeviceBeingSetUp(int32_t ip, char const* name, uint16_t vendorId, uint16_t productId) {

	usbDeviceCurrentlyBeingSetUp[ip].name.set(name); // If that fails, it'll just have a 0-length name
	usbDeviceCurrentlyBeingSetUp[ip].vendorId = vendorId;
	usbDeviceCurrentlyBeingSetUp[ip].productId = productId;

	uartPrint("name: ");
	uartPrintln(name);
	uartPrint("vendor: ");
	uartPrintNumber(vendorId);
	uartPrint("product: ");
	uartPrintNumber(productId);
}

// name can be NULL, or an empty String
MIDIDeviceUSBHosted* getOrCreateHostedMIDIDeviceFromDetails(String* name, uint16_t vendorId, uint16_t productId) {

	// Do we know any details about this device already?

	bool gotAName = (name && !name->isEmpty());
	int32_t i = 0; // Need default value for below if we skip first bit because !gotAName

	if (gotAName) {
		// Search by name first
		bool foundExact;
		i = hostedMIDIDevices.search(name->get(), GREATER_OR_EQUAL, &foundExact);

		// If we'd already seen it before...
		if (foundExact) {
			MIDIDeviceUSBHosted* device = recastSpecificMidiDevice(hostedMIDIDevices.getElement(i));

			// Update vendor and product id, if we have those
			if (vendorId) {
				device->vendorId = vendorId;
				device->productId = productId;
			}

			return device;
		}
	}

	// Ok, try searching by vendor / product id
	for (int32_t i = 0; i < hostedMIDIDevices.getNumElements(); i++) {
		MIDIDeviceUSBHosted* candidate = recastSpecificMidiDevice(hostedMIDIDevices.getElement(i));

		if (candidate->vendorId == vendorId && candidate->productId == productId) {
			// Update its name - if we got one and it's different
			if (gotAName && !candidate->name.equals(name)) {
				hostedMIDIDevices.renameMember(i, name);
			}
			return candidate;
		}
	}

	bool success = hostedMIDIDevices.ensureEnoughSpaceAllocated(1);
	if (!success) {
		return NULL;
	}

	MIDIDeviceUSBHosted* device = NULL;

	SpecificMidiDeviceType devType = getSpecificMidiDeviceType(vendorId, productId);
	if (devType == SpecificMidiDeviceType::LUMI_KEYS) {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDIDeviceLumiKeys));
		if (!memory) {
			return NULL;
		}

		MIDIDeviceLumiKeys* instDevice = new (memory) MIDIDeviceLumiKeys();
		device = instDevice;
	}
	else {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDIDeviceUSBHosted));
		if (!memory) {
			return NULL;
		}

		MIDIDeviceUSBHosted* instDevice = new (memory) MIDIDeviceUSBHosted();
		device = instDevice;
	}

	if (gotAName) {
		device->name.set(name);
	}
	device->vendorId = vendorId;
	device->productId = productId;

	// Store record of this device
	Error error = hostedMIDIDevices.insertElement(device, i); // We made sure, above, that there's space
#if ALPHA_OR_BETA_VERSION
	if (error != Error::NONE) {
		FREEZE_WITH_ERROR("E405");
	}
#endif

	return device;
}

void recountSmallestMPEZonesForDevice(MIDIDevice* device) {
	if (!device->connectionFlags) {
		return;
	}

	if (device->ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel
	    && device->ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel
	           < lowestLastMemberChannelOfLowerZoneOnConnectedOutput) {

		lowestLastMemberChannelOfLowerZoneOnConnectedOutput =
		    device->ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel;
	}

	if (device->ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel != 15
	    && device->ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel
	           > highestLastMemberChannelOfUpperZoneOnConnectedOutput) {

		highestLastMemberChannelOfUpperZoneOnConnectedOutput =
		    device->ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel;
	}
}

void recountSmallestMPEZones() {
	lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
	highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

	recountSmallestMPEZonesForDevice(&upstreamUSBMIDIDevice_port1);
	recountSmallestMPEZonesForDevice(&upstreamUSBMIDIDevice_port2);
	recountSmallestMPEZonesForDevice(&dinMIDIPorts);

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);
		recountSmallestMPEZonesForDevice(device);
	}
}

// Create the midi device configuration and add to the USB midi array
extern "C" void hostedDeviceConfigured(int32_t ip, int32_t midiDeviceNum) {
	MIDIDeviceUSBHosted* device = getOrCreateHostedMIDIDeviceFromDetails(&usbDeviceCurrentlyBeingSetUp[ip].name,
	                                                                     usbDeviceCurrentlyBeingSetUp[ip].vendorId,
	                                                                     usbDeviceCurrentlyBeingSetUp[ip].productId);

	usbDeviceCurrentlyBeingSetUp[ip].name.clear(); // Save some memory. Not strictly necessary

	if (!device) {
		return; // Only if ran out of RAM - i.e. very unlikely.
	}

	// Associate with USB port
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];

	connectedDevice->setup();
	int32_t ports = connectedDevice->maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		connectedDevice->device[i] = device;
	}

	connectedDevice->sq = 0;
	connectedDevice->canHaveMIDISent = (bool)strcmp(device->name.get(), "Synthstrom MIDI Foot Controller");
	connectedDevice->canHaveMIDISent = (bool)strcmp(device->name.get(), "LUMI Keys BLOCK");

	device->connectedNow(midiDeviceNum);
	recountSmallestMPEZones(); // Must be called after setting device->connectionFlags

	device->freshly_connected = true; // Used to trigger hookOnConnected from the input loop

	if (display->haveOLED()) {
		String text;
		text.set(&device->name);
		Error error = text.concatenate(" attached");
		if (error == Error::NONE) {
			consoleTextIfAllBootedUp(text.get());
		}
	}
	else {
		consoleTextIfAllBootedUp("MIDI");
	}
}

extern "C" void hostedDeviceDetached(int32_t ip, int32_t midiDeviceNum) {

#if ALPHA_OR_BETA_VERSION
	if (midiDeviceNum == MAX_NUM_USB_MIDI_DEVICES) {
		FREEZE_WITH_ERROR("E367");
	}
#endif

	uartPrint("detached MIDI device: ");
	uartPrintNumber(midiDeviceNum);
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];
	int32_t ports = connectedDevice->maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		MIDIDeviceUSB* device = connectedDevice->device[i];
		if (device) { // Surely always has one?
			device->connectionFlags &= ~(1 << midiDeviceNum);
		}
		connectedDevice->device[i] = NULL;
	}
	recountSmallestMPEZones();
}

// called by USB setup
extern "C" void configuredAsPeripheral(int32_t ip) {
	// Leave this - we'll use this device for all upstream ports
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][0];

	// add second port here
	connectedDevice->setup();
	connectedDevice->device[0] = &upstreamUSBMIDIDevice_port1;
	connectedDevice->device[1] = &upstreamUSBMIDIDevice_port2;
	connectedDevice->device[2] = &upstreamUSBMIDIDevice_port3;
	connectedDevice->maxPortConnected = 2;
	connectedDevice->canHaveMIDISent = 1;

	anyUSBSendingStillHappening[ip] = 0; // Initialize this. There's obviously nothing sending yet right now.

	upstreamUSBMIDIDevice_port1.connectedNow(0);
	upstreamUSBMIDIDevice_port2.connectedNow(0);
	upstreamUSBMIDIDevice_port3.connectedNow(0);
	recountSmallestMPEZones();
}

extern "C" void detachedAsPeripheral(int32_t ip) {
	// will need to reset all devices if more are added
	int32_t ports = connectedUSBMIDIDevices[ip][0].maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		connectedUSBMIDIDevices[ip][0].device[i] = NULL;
	}
	upstreamUSBMIDIDevice_port1.connectionFlags = 0;
	upstreamUSBMIDIDevice_port2.connectionFlags = 0;
	upstreamUSBMIDIDevice_port3.connectionFlags = 0;
	anyUSBSendingStillHappening[ip] = 0; // Reset this again. Been meaning to do this, and can no longer quite remember
	                                     // reason or whether technically essential, but adds to safety at least.

	recountSmallestMPEZones();
}

// Returns NULL if insufficient details found, or not enough RAM to create
MIDIDevice* readDeviceReferenceFromFile(Deserializer& reader) {

	uint16_t vendorId = 0;
	uint16_t productId = 0;
	String name;
	MIDIDevice* device = NULL;

	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "vendorId")) {
			vendorId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "productId")) {
			productId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "name")) {
			reader.readTagOrAttributeValueString(&name);
		}
		else if (!strcmp(tagName, "port")) {
			char const* port = reader.readTagOrAttributeValue();
			if (!strcmp(port, "loopbackMidi")) {
				device = &loopbackMidi;
			}
			else if (!strcmp(port, "upstreamUSB")) {
				device = &upstreamUSBMIDIDevice_port1;
			}
			else if (!strcmp(port, "upstreamUSB2")) {
				device = &upstreamUSBMIDIDevice_port2;
			}
			else if (!strcmp(port, "upstreamUSB3")) {
				device = &upstreamUSBMIDIDevice_port3;
			}
			else if (!strcmp(port, "din")) {
				device = &dinMIDIPorts;
			}
		}

		reader.exitTag();
	}

	if (device) {
		return device;
	}

	// If we got something, go use it
	if (!name.isEmpty() || vendorId) {
		return getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId, productId); // Will return NULL if error.
	}

	return NULL;
}

void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory) {

	uint16_t vendorId = *(uint16_t const*)memory;

	MIDIDevice* device;

	if (vendorId == VENDOR_ID_NONE) {
		device = NULL;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB) {
		device = &upstreamUSBMIDIDevice_port1;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB2) {
		device = &upstreamUSBMIDIDevice_port2;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB3) {
		device = &upstreamUSBMIDIDevice_port3;
	}
	else if (vendorId == VENDOR_ID_LOOPBACK) {
		device = &loopbackMidi;
	}
	else if (vendorId == VENDOR_ID_DIN) {
		device = &dinMIDIPorts;
	}
	else {
		uint16_t productId = *(uint16_t const*)(memory + 2);
		device = getOrCreateHostedMIDIDeviceFromDetails(NULL, vendorId, productId);
	}

	midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].device = device;
}

void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory) {
	if (midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].device) {
		midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].device->writeToFlash(memory);
	}
}

void readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType whichType, uint8_t const* memory) {

	uint16_t vendorId = *(uint16_t const*)memory;

	MIDIDevice* device;

	if (vendorId == VENDOR_ID_NONE) {
		device = NULL;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB) {
		device = &upstreamUSBMIDIDevice_port1;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB2) {
		device = &upstreamUSBMIDIDevice_port2;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB3) {
		device = &upstreamUSBMIDIDevice_port3;
	}
	else if (vendorId == VENDOR_ID_LOOPBACK) {
		device = &loopbackMidi;
	}
	else if (vendorId == VENDOR_ID_DIN) {
		device = &dinMIDIPorts;
	}
	else {
		uint16_t productId = *(uint16_t const*)(memory + 2);
		device = getOrCreateHostedMIDIDeviceFromDetails(NULL, vendorId, productId);
	}

	midiEngine.midiFollowChannelType[util::to_underlying(whichType)].device = device;
}

void writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType whichType, uint8_t* memory) {
	if (midiEngine.midiFollowChannelType[util::to_underlying(whichType)].device) {
		midiEngine.midiFollowChannelType[util::to_underlying(whichType)].device->writeToFlash(memory);
	}
}

void writeDevicesToFile(StorageManager& bdsm) {
	if (!anyChangesToSave) {
		return;
	}
	anyChangesToSave = false;

	// First, see if it's even worth writing anything
	if (dinMIDIPorts.worthWritingToFile()) {
		goto worthIt;
	}
	if (upstreamUSBMIDIDevice_port1.worthWritingToFile()) {
		goto worthIt;
	}
	if (upstreamUSBMIDIDevice_port2.worthWritingToFile()) {
		goto worthIt;
	}
	if (loopbackMidi.worthWritingToFile()) {
		goto worthIt;
	}

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);
		if (device->worthWritingToFile()) {
			goto worthIt;
		}
	}

	// If still here, nothing worth writing. Delete the file if there was one.
	f_unlink("MIDIDevices.XML"); // May give error, but no real consequence from that.
	return;

worthIt:
	Error error = bdsm.createXMLFile("MIDIDevices.XML", smSerializer, true);
	if (error != Error::NONE) {
		return;
	}

	MIDIDeviceUSBHosted* specificMIDIDevice = NULL;
	Serializer& writer = GetSerializer();
	writer.writeOpeningTagBeginning("midiDevices");
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.0.0");
	writer.writeOpeningTagEnd();

	if (dinMIDIPorts.worthWritingToFile()) {
		dinMIDIPorts.writeToFile(writer, "dinPorts");
	}
	if (upstreamUSBMIDIDevice_port1.worthWritingToFile()) {
		upstreamUSBMIDIDevice_port1.writeToFile(writer, "upstreamUSBDevice");
	}
	if (upstreamUSBMIDIDevice_port2.worthWritingToFile()) {
		upstreamUSBMIDIDevice_port2.writeToFile(writer, "upstreamUSBDevice2");
	}
	if (loopbackMidi.worthWritingToFile()) {
		loopbackMidi.writeToFile(writer, "loopbackMidi");
	}

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);
		if (device->worthWritingToFile()) {
			device->writeToFile(writer, "hostedUSBDevice");
		}
		// Stow this for the hook  point later
		specificMIDIDevice = recastSpecificMidiDevice(device);
	}

	writer.writeClosingTag("midiDevices");

	writer.closeFileAfterWriting();

	// Hook point for Hosted USB MIDI Device
	if (specificMIDIDevice != NULL) {
		specificMIDIDevice->hookOnWriteHostedDeviceToFile();
	}
}

bool successfullyReadDevicesFromFile = false; // We'll only do this one time

void readDevicesFromFile(StorageManager& bdsm) {
	if (successfullyReadDevicesFromFile) {
		return; // Yup, we only want to do this once
	}

	FilePointer fp;
	bool success = bdsm.fileExists("MIDIDevices.XML", &fp);
	if (!success) {
		return;
	}

	Error error = bdsm.openXMLFile(&fp, smDeserializer, "midiDevices");
	if (error != Error::NONE) {
		return;
	}
	Deserializer& reader = smDeserializer;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "dinPorts")) {
			dinMIDIPorts.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice")) {
			upstreamUSBMIDIDevice_port1.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice2")) {
			upstreamUSBMIDIDevice_port2.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice3")) {
			upstreamUSBMIDIDevice_port3.readFromFile(reader);
		}
		else if (!strcmp(tagName, "loopbackMidi")) {
			loopbackMidi.readFromFile(reader);
		}
		else if (!strcmp(tagName, "hostedUSBDevice")) {
			readAHostedDeviceFromFile(reader);
		}

		reader.exitTag();
	}

	f_close(&smDeserializer.readFIL);

	recountSmallestMPEZones();

	soundEditor.mpeZonesPotentiallyUpdated();

	successfullyReadDevicesFromFile = true;
}

void readAHostedDeviceFromFile(Deserializer& reader) {
	MIDIDeviceUSBHosted* device = NULL;

	String name;
	uint16_t vendorId;
	uint16_t productId;

	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {

		int32_t whichPort;

		if (!strcmp(tagName, "vendorId")) {
			vendorId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "productId")) {
			productId = reader.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "name")) {
			reader.readTagOrAttributeValueString(&name);
		}
		else if (!strcmp(tagName, "input")) {
			whichPort = MIDI_DIRECTION_INPUT_TO_DELUGE;
checkDevice:
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->ports[whichPort].readFromFile(reader,
				                                      (whichPort == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) ? device : NULL);
			}
		}
		else if (!strcmp(tagName, "output")) {
			whichPort = MIDI_DIRECTION_OUTPUT_FROM_DELUGE;
			goto checkDevice;
		}
		else if (!strcmp(tagName, "defaultVolumeVelocitySensitivity")) {

			// Sorry, I cloned this code from above.
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->defaultVelocityToLevel = reader.readTagOrAttributeValueInt();
			}
		}

		reader.exitTag();
	}

	// Hook point!
	if (device) {}
}

} // namespace MIDIDeviceManager

void ConnectedUSBMIDIDevice::bufferMessage(uint32_t fullMessage) {
	uint32_t queued = ringBufWriteIdx - ringBufReadIdx;
	if (queued > 16) {
		if (!anyUSBSendingStillHappening[0]) {
			midiEngine.flushUSBMIDIOutput();
		}
		queued = ringBufWriteIdx - ringBufReadIdx;
	}
	if (queued > MIDI_SEND_BUFFER_LEN_RING) {
		// TODO: show some error message
		return;
	}

	sendDataRingBuf[ringBufWriteIdx & MIDI_SEND_RING_MASK] = fullMessage;
	ringBufWriteIdx++;

	anythingInUSBOutputBuffer = true;
}

bool ConnectedUSBMIDIDevice::hasBufferedSendData() {
	// must be the same unsigned type as ringBufWriteIdx/ringBufReadIdx
	uint32_t queued = ringBufWriteIdx - ringBufReadIdx;
	return queued > 0;
}

int ConnectedUSBMIDIDevice::sendBufferSpace() {
	// must be the same unsigned type as ringBufWriteIdx/ringBufReadIdx
	uint32_t queued = ringBufWriteIdx - ringBufReadIdx;
	// each 4-byte MIDI-USB message contains 3 bytes of serial MIDI data
	return (MIDI_SEND_BUFFER_LEN_RING - queued) * 3;
}

// This tries to read data from the ring buffer, and
// moves data into the smaller "dataSendingNow" buffer where
// it is ready to be used by the hardware driver.
bool ConnectedUSBMIDIDevice::consumeSendData() {
	uint32_t queued = ringBufWriteIdx - ringBufReadIdx;
	if (queued == 0) {
		return false;
	}

	int32_t i = 0;
	uint32_t max_size = MIDI_SEND_BUFFER_LEN_INNER;
	if (g_usb_usbmode == USB_HOST) {
		// many devices do not accept more than 64 bytes of data at a time
		// likely this can be inferred from the device metadata somehow?
		max_size = MIDI_SEND_BUFFER_LEN_INNER_HOST;
	}

	int32_t to_send = std::min(queued, max_size);
	for (i = 0; i < to_send; i++) {
		memcpy(dataSendingNow + (i * 4), &sendDataRingBuf[ringBufReadIdx & MIDI_SEND_RING_MASK], 4);
		ringBufReadIdx++;
	}

	numBytesSendingNow = to_send * 4;
	return true;
}

void ConnectedUSBMIDIDevice::setup() {
	numBytesSendingNow = 0;
	currentlyWaitingToReceive = false;
	numBytesReceived = 0;

	// default to only a single port
	maxPortConnected = 0;
}

/*
    for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
        MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);

    }
 */
#pragma GCC diagnostic pop
