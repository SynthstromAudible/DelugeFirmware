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
#include "bsp/rza1/usb_midi.h" // BspUsbMidiDevice, bsp_usb_midi_device/init (transport state)
#include "definitions_cxx.hpp"
#include "deluge/deluge.h" // setTimeUSBInitializationEnds (app-owned popup-suppression window)
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/midi_io.h" // deluge_midi_poll_usb_host_event
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "storage/storage_manager.h"
#include "util/container/vector/named_thing_vector.h"
#include "util/misc.h"

extern "C" {

extern uint8_t anyUSBSendingStillHappening[];
}
#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_DEVICES_XML "SETTINGS/MIDIDevices.XML"

PLACE_SDRAM_BSS ConnectedUSBMIDIDevice connectedUSBMIDIDevices[DELUGE_USB_NUM_CONTROLLERS][MAX_NUM_USB_MIDI_DEVICES];

namespace MIDIDeviceManager {

NamedThingVector hostedMIDIDevices{__builtin_offsetof(MIDICableUSBHosted, name)};

bool differentiatingInputsByDevice = true;

struct USBDev {
	String name{};
	uint16_t vendorId;
	uint16_t productId;
};
std::array<USBDev, DELUGE_USB_NUM_CONTROLLERS> usbDeviceCurrentlyBeingSetUp{};

// This class represents a thing you can send midi too,
// the virtual cable is an implementation detail
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable1{0, false, true};
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable2{1, true, false};
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable3{2, false, false};
PLACE_SDRAM_BSS MIDICableDINPorts dinMIDIPorts{};

uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

bool anyChangesToSave = false;

void init() {
	// Point each device at its BSP-owned transport block, then let the BSP zero
	// the transport state. Runs before the USB stack is opened, so the transport
	// pointer is valid before any send/receive can touch it.
	for (int32_t ip = 0; ip < DELUGE_USB_NUM_CONTROLLERS; ip++) {
		for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
			connectedUSBMIDIDevices[ip][d].transport = bsp_usb_midi_device(ip, d);
		}
	}
	bsp_usb_midi_init();
}

// Gets called within UITimerManager, which may get called during SD card routine.
void slowRoutine() {
	// Drain USB host enumeration events from the BSP (pull-based: the USB host
	// stack records them, we poll here) and surface them as console popups. The
	// app owns the display + localization and the post-hub-attach popup-
	// suppression window — the USB stack no longer does any UI itself.
	for (DelugeUsbHostEvent usbEvent = deluge_midi_poll_usb_host_event(); usbEvent != DELUGE_USB_HOST_NONE;
	     usbEvent = deluge_midi_poll_usb_host_event()) {
		switch (usbEvent) {
		case DELUGE_USB_HOST_HUB_ATTACHED:
			consoleTextIfAllBootedUp(deluge::l10n::get(deluge::l10n::String::STRING_FOR_USB_HUB_ATTACHED));
			// A hub enumerates many devices at once; suppress the per-device
			// popups for ~2s (was setTimeUSBInitializationEnds(44100 << 1)).
			setTimeUSBInitializationEnds(kSampleRate * 2);
			break;
		case DELUGE_USB_HOST_DEVICE_DETACHED:
			consoleTextIfAllBootedUp(deluge::l10n::get(deluge::l10n::String::STRING_FOR_USB_DEVICE_DETACHED));
			break;
		case DELUGE_USB_HOST_DEVICE_NOT_RECOGNIZED:
			consoleTextIfAllBootedUp(deluge::l10n::get(deluge::l10n::String::STRING_FOR_USB_DEVICE_NOT_RECOGNIZED));
			break;
		case DELUGE_USB_HOST_DEVICES_MAX:
			consoleTextIfAllBootedUp(deluge::l10n::get(deluge::l10n::String::STRING_FOR_USB_DEVICES_MAX));
			break;
		case DELUGE_USB_HOST_NONE:
			break;
		}
	}

	upstreamUSBMIDICable1.sendMCMsNowIfNeeded();
	upstreamUSBMIDICable2.sendMCMsNowIfNeeded();
	// port3 is not used for channel data

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDICableUSBHosted* device = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
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

	D_PRINTLN("name: %s  vendor: %d  product: %d", name, vendorId, productId);
}

// name can be NULL, or an empty String
MIDICableUSBHosted* getOrCreateHostedMIDIDeviceFromDetails(String* name, uint16_t vendorId, uint16_t productId) {

	// Do we know any details about this device already?

	bool gotAName = (name && !name->isEmpty());
	int32_t i = 0; // Need default value for below if we skip first bit because !gotAName

	if (gotAName) {
		// Search by name first
		bool foundExact;
		i = hostedMIDIDevices.search(name->get(), GREATER_OR_EQUAL, &foundExact);

		// If we'd already seen it before...
		if (foundExact) {
			auto* device = static_cast<MIDICableUSBHosted*>(hostedMIDIDevices.getElement(i));

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
		auto* candidate = static_cast<MIDICableUSBHosted*>(hostedMIDIDevices.getElement(i));

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
		return nullptr;
	}

	MIDICableUSBHosted* device = nullptr;

	SpecificMidiDeviceType devType = getSpecificMidiDeviceType(vendorId, productId);
	if (devType == SpecificMidiDeviceType::LUMI_KEYS) {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDIDeviceLumiKeys));
		if (!memory) {
			return nullptr;
		}

		MIDIDeviceLumiKeys* instDevice = new (memory) MIDIDeviceLumiKeys();
		device = instDevice;
	}
	else {
		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDICableUSBHosted));
		if (!memory) {
			return nullptr;
		}

		MIDICableUSBHosted* instDevice = new (memory) MIDICableUSBHosted();
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

void recountSmallestMPEZonesForCable(MIDICable& cable) {
	if (!cable.connectionFlags) {
		return;
	}

	if (cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel
	    && cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel
	           < lowestLastMemberChannelOfLowerZoneOnConnectedOutput) {

		lowestLastMemberChannelOfLowerZoneOnConnectedOutput =
		    cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel;
	}

	if (cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel != 15
	    && cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel
	           > highestLastMemberChannelOfUpperZoneOnConnectedOutput) {

		highestLastMemberChannelOfUpperZoneOnConnectedOutput =
		    cable.ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel;
	}
}

void recountSmallestMPEZones() {
	lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
	highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

	recountSmallestMPEZonesForCable(upstreamUSBMIDICable1);
	recountSmallestMPEZonesForCable(upstreamUSBMIDICable2);
	recountSmallestMPEZonesForCable(dinMIDIPorts);

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDICableUSBHosted* cable = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
		recountSmallestMPEZonesForCable(*cable);
	}
}

// Create the midi device configuration and add to the USB midi array
extern "C" void hostedDeviceConfigured(int32_t ip, int32_t midiDeviceNum) {
	MIDICableUSBHosted* device = getOrCreateHostedMIDIDeviceFromDetails(&usbDeviceCurrentlyBeingSetUp[ip].name,
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
		connectedDevice->cable[i] = device;
	}

	connectedDevice->transport->sq = 0;
	connectedDevice->transport->canHaveMIDISent = (bool)strcmp(device->name.get(), "Synthstrom MIDI Foot Controller");
	connectedDevice->transport->canHaveMIDISent = (bool)strcmp(device->name.get(), "LUMI Keys BLOCK");
	connectedDevice->transport->connected = true; // a live send sink now (was cable[0] != NULL)

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

	D_PRINTLN("detached MIDI device: %d", midiDeviceNum);
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][midiDeviceNum];
	int32_t ports = connectedDevice->maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		MIDICableUSB* device = connectedDevice->cable[i];
		if (device) { // Surely always has one?
			device->connectionFlags &= ~(1 << midiDeviceNum);
		}
		connectedDevice->cable[i] = nullptr;
	}
	connectedDevice->transport->connected = false;
	recountSmallestMPEZones();
}

// called by USB setup
extern "C" void configuredAsPeripheral(int32_t ip) {
	// Leave this - we'll use this device for all upstream ports
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][0];

	// add second port here
	connectedDevice->setup();
	connectedDevice->cable[0] = &upstreamUSBMIDICable1;
	connectedDevice->cable[1] = &upstreamUSBMIDICable2;
	connectedDevice->cable[2] = &upstreamUSBMIDICable3;
	connectedDevice->maxPortConnected = 2;
	connectedDevice->transport->canHaveMIDISent = 1;
	connectedDevice->transport->connected = true;

	anyUSBSendingStillHappening[ip] = 0; // Initialize this. There's obviously nothing sending yet right now.

	upstreamUSBMIDICable1.connectedNow(0);
	upstreamUSBMIDICable2.connectedNow(0);
	upstreamUSBMIDICable3.connectedNow(0);
	recountSmallestMPEZones();
}

extern "C" void detachedAsPeripheral(int32_t ip) {
	// will need to reset all devices if more are added
	int32_t ports = connectedUSBMIDIDevices[ip][0].maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		connectedUSBMIDIDevices[ip][0].cable[i] = nullptr;
	}
	connectedUSBMIDIDevices[ip][0].transport->connected = false;
	upstreamUSBMIDICable1.connectionFlags = 0;
	upstreamUSBMIDICable2.connectionFlags = 0;
	upstreamUSBMIDICable3.connectionFlags = 0;
	anyUSBSendingStillHappening[ip] = 0; // Reset this again. Been meaning to do this, and can no longer quite remember
	                                     // reason or whether technically essential, but adds to safety at least.

	recountSmallestMPEZones();
}

// Returns NULL if insufficient details found, or not enough RAM to create
MIDICable* readDeviceReferenceFromFile(Deserializer& reader) {

	uint16_t vendorId = 0;
	uint16_t productId = 0;
	String name;
	MIDICable* device = nullptr;

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
			if (!strcmp(port, "upstreamUSB")) {
				device = &upstreamUSBMIDICable1;
			}
			else if (!strcmp(port, "upstreamUSB2")) {
				device = &upstreamUSBMIDICable2;
			}
			else if (!strcmp(port, "upstreamUSB3")) {
				device = &upstreamUSBMIDICable3;
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

	return nullptr;
}

static MIDICable* readCableFromFlash(uint8_t const* memory) {
	uint16_t vendorId = *(uint16_t const*)memory;

	MIDICable* cable;

	if (vendorId == VENDOR_ID_NONE) {
		cable = nullptr;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB) {
		cable = &upstreamUSBMIDICable1;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB2) {
		cable = &upstreamUSBMIDICable2;
	}
	else if (vendorId == VENDOR_ID_UPSTREAM_USB3) {
		cable = &upstreamUSBMIDICable3;
	}
	else if (vendorId == VENDOR_ID_DIN) {
		cable = &dinMIDIPorts;
	}
	else {
		uint16_t productId = *(uint16_t const*)(memory + 2);
		cable = getOrCreateHostedMIDIDeviceFromDetails(nullptr, vendorId, productId);
	}

	return cable;
}

void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory) {
	midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].cable = readCableFromFlash(memory);
}

void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory) {
	if (midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].cable) {
		midiEngine.globalMIDICommands[util::to_underlying(whichCommand)].cable->writeToFlash(memory);
	}
}

void readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType whichType, uint8_t const* memory) {
	midiEngine.midiFollowChannelType[util::to_underlying(whichType)].cable = readCableFromFlash(memory);
}

void writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType whichType, uint8_t* memory) {
	if (midiEngine.midiFollowChannelType[util::to_underlying(whichType)].cable) {
		midiEngine.midiFollowChannelType[util::to_underlying(whichType)].cable->writeToFlash(memory);
	}
}

void writeDevicesToFile() {
	if (!anyChangesToSave) {
		return;
	}
	anyChangesToSave = false;

	bool anyWorthWritting = dinMIDIPorts.worthWritingToFile() || upstreamUSBMIDICable1.worthWritingToFile()
	                        || upstreamUSBMIDICable2.worthWritingToFile();
	if (!anyWorthWritting) {
		for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
			MIDICableUSBHosted* device = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
			if (device->worthWritingToFile()) {
				anyWorthWritting = true;
				break;
			}
		}
	}

	if (!anyWorthWritting) {
		// If still here, nothing worth writing. Delete the file if there was one.
		f_unlink(MIDI_DEVICES_XML); // May give error, but no real consequence from that.
		return;
	}

	Error error = StorageManager::createXMLFile(MIDI_DEVICES_XML, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}

	Serializer& writer = GetSerializer();
	writer.writeOpeningTagBeginning("midiDevices");
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.0.0");
	writer.writeOpeningTagEnd();

	if (dinMIDIPorts.worthWritingToFile()) {
		dinMIDIPorts.writeToFile(writer, "dinPorts");
	}
	if (upstreamUSBMIDICable1.worthWritingToFile()) {
		upstreamUSBMIDICable1.writeToFile(writer, "upstreamUSBDevice");
	}
	if (upstreamUSBMIDICable2.worthWritingToFile()) {
		upstreamUSBMIDICable2.writeToFile(writer, "upstreamUSBDevice2");
	}

	for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDICableUSBHosted* device = (MIDICableUSBHosted*)hostedMIDIDevices.getElement(d);
		if (device->worthWritingToFile()) {
			device->writeToFile(writer, "hostedUSBDevice");
		}
		// Stow this for the hook  point later
		device->hookOnWriteHostedDeviceToFile();
	}

	writer.writeClosingTag("midiDevices");

	writer.closeFileAfterWriting();
}

bool successfullyReadDevicesFromFile = false; // We'll only do this one time

void readDevicesFromFile() {
	if (successfullyReadDevicesFromFile) {
		return; // Yup, we only want to do this once
	}

	FilePointer fp;
	bool success = StorageManager::fileExists(MIDI_DEVICES_XML, &fp);
	if (!success) {
		// since we changed the file path for the MIDIDevices.XML in c1.3, it's possible
		// that a MIDIDevice file may exists in the root of the SD card
		// if so, let's move it to the new SETTINGS folder (but first make sure folder exists)
		FRESULT result = f_mkdir(SETTINGS_FOLDER);
		if (result == FR_OK || result == FR_EXIST) {
			result = f_rename("MIDIDevices.XML", MIDI_DEVICES_XML);
			if (result == FR_OK) {
				// this means we moved it
				// now let's open it
				success = StorageManager::fileExists(MIDI_DEVICES_XML, &fp);
			}
		}
		if (!success) {
			return;
		}
	}

	Error error = StorageManager::openXMLFile(&fp, smDeserializer, "midiDevices");
	if (error != Error::NONE) {
		return;
	}
	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "dinPorts")) {
			dinMIDIPorts.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice")) {
			upstreamUSBMIDICable1.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice2")) {
			upstreamUSBMIDICable2.readFromFile(reader);
		}
		else if (!strcmp(tagName, "upstreamUSBDevice3")) {
			upstreamUSBMIDICable3.readFromFile(reader);
		}
		else if (!strcmp(tagName, "hostedUSBDevice")) {
			readAHostedDeviceFromFile(reader);
		}

		reader.exitTag();
	}

	activeDeserializer->closeWriter();

	recountSmallestMPEZones();

	soundEditor.mpeZonesPotentiallyUpdated();

	successfullyReadDevicesFromFile = true;
}

void readAHostedDeviceFromFile(Deserializer& reader) {
	MIDICableUSBHosted* device = nullptr;

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
				device->ports[whichPort].readFromFile(
				    reader, (whichPort == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) ? device : nullptr);
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
		else if (!strcmp(tagName, "sendClock")) {
			// this is actually not much duplicated code, just checks for nulls and then an attempt to create a device
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->sendClock = reader.readTagOrAttributeValueInt();
			}
		}

		reader.exitTag();
	}

	// Hook point!
	if (device) {}
}

} // namespace MIDIDeviceManager

// The send ring + send state machine live in the BSP usb_midi module now; these
// just forward to it.
void ConnectedUSBMIDIDevice::bufferMessage(uint32_t fullMessage) {
	bsp_usb_midi_buffer_message(transport, fullMessage);
}

int ConnectedUSBMIDIDevice::sendBufferSpace() {
	return bsp_usb_midi_send_buffer_space(transport);
}

void ConnectedUSBMIDIDevice::setup() {
	transport->numBytesSendingNow = 0;
	transport->currentlyWaitingToReceive = false;
	transport->numBytesReceived = 0;

	// default to only a single port
	maxPortConnected = 0;
}
ConnectedUSBMIDIDevice::ConnectedUSBMIDIDevice() {
	// The transport block (RX/TX buffers, send ring, counters) lives in the BSP
	// usb_midi module and is wired/zeroed by MIDIDeviceManager::init() at startup.
	transport = nullptr;
	maxPortConnected = 0;
}

/*
    for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
        MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);

    }
 */
#pragma GCC diagnostic pop
