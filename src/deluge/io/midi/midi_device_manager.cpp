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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_queue_manager.h"
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "storage/storage_manager.h"
#include "util/container/vector/named_thing_vector.h"
#include "util/misc.h"
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_basic_define.h"
#include "drivers/uart/uart.h"

extern uint8_t anyUSBSendingStillHappening[];
}
#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_DEVICES_XML "SETTINGS/MIDIDevices.XML"

namespace {
/// Outgoing queue storage for each entry of connectedUSBMIDIDevices, held in a parallel array.
///
/// ConnectedUSBMIDIDevice is also compiled as a plain C struct by the USB driver (see the warning in
/// midi_device_manager.h), and that driver indexes connectedUSBMIDIDevices[][] itself - see
/// r_usb_hlibusbip.c, which writes connectedUSBMIDIDevices[0][deviceNum] for deviceNum up to
/// MAX_NUM_USB_MIDI_DEVICES - 1. Keeping this ~25KB of queue state as a C++-only member would make the
/// C view of sizeof(ConnectedUSBMIDIDevice) much smaller than the C++ view that actually allocated the
/// array, so every C-side access with deviceNum >= 1 would compute the wrong address and corrupt memory.
///
/// Defined before connectedUSBMIDIDevices so it is constructed first: ConnectedUSBMIDIDevice's
/// constructor resets its queue storage through queue_manager().
PLACE_SDRAM_BSS MIDIQueueManagerUSB usbQueueManagers[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];
} // namespace

PLACE_SDRAM_BSS ConnectedUSBMIDIDevice connectedUSBMIDIDevices[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];
PLACE_SDRAM_BSS ConnectedDINMIDIDevice connectedDINMIDIDevice{};

// The USB driver's C view of this struct must keep the same stride as the C++ view that allocates the array
// above. maxPortConnected is the last shared member, and the struct's alignment is 8, so any C++-only data
// member added after it will push sizeof() past this bound and trip here rather than corrupting memory at
// runtime.
static_assert(sizeof(ConnectedUSBMIDIDevice) <= __builtin_offsetof(ConnectedUSBMIDIDevice, maxPortConnected) + 8,
              "ConnectedUSBMIDIDevice must not gain C++-only data members: the USB driver compiles it as a C "
              "struct and indexes connectedUSBMIDIDevices[][] directly, so C and C++ must agree on sizeof(). "
              "Put per-device C++ state in a parallel array (see usbQueueManagers) instead.");

// Resolves this device's queue storage from the parallel usbQueueManagers array: every
// ConnectedUSBMIDIDevice lives in connectedUSBMIDIDevices, so its position there is the index into the
// identically-shaped usbQueueManagers array.
MIDIQueueManagerUSB& ConnectedUSBMIDIDevice::queue_manager() {
	ptrdiff_t flat_index = this - &connectedUSBMIDIDevices[0][0];
	return (&usbQueueManagers[0][0])[flat_index];
}

namespace MIDIDeviceManager {

NamedThingVector hostedMIDIDevices{__builtin_offsetof(MIDICableUSBHosted, name)};

bool differentiatingInputsByDevice = true;

struct USBDev {
	String name{};
	uint16_t vendorId;
	uint16_t productId;
};
std::array<USBDev, USB_NUM_USBIP> usbDeviceCurrentlyBeingSetUp{};

// This class represents a thing you can send midi too,
// the virtual cable is an implementation detail
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable1{0, false, true};
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable2{1, true, false};
PLACE_SDRAM_BSS MIDICableUSBUpstream upstreamUSBMIDICable3{2, false, false};
PLACE_SDRAM_BSS MIDICableDINPorts dinMIDIPorts{};

uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

bool anyChangesToSave = false;

// Gets called within UITimerManager, which may get called during SD card routine.
void slowRoutine() {
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

	uartPrint("name: ");
	uartPrintln(name);
	uartPrint("vendor: ");
	uartPrintNumber(vendorId);
	uartPrint("product: ");
	uartPrintNumber(productId);
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
		MIDICableUSB* device = connectedDevice->cable[i];
		if (device) { // Surely always has one?
			device->connectionFlags &= ~(1 << midiDeviceNum);
		}
		connectedDevice->cable[i] = nullptr;
	}
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
	connectedDevice->canHaveMIDISent = 1;

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

void factoryReset(bool showPopup) {
	if (showPopup) {
		display->displayPopup(display->haveOLED()
		                          ? deluge::l10n::get(deluge::l10n::String::STRING_FOR_RESET_MIDI_DEVICES)
		                          : deluge::l10n::get(deluge::l10n::String::STRING_FOR_FACTORY_RESET));
	}

	f_unlink(MIDI_DEVICES_XML);

	new (&upstreamUSBMIDICable1) MIDICableUSBUpstream{0, false, true};
	new (&upstreamUSBMIDICable2) MIDICableUSBUpstream{1, true, false};
	new (&upstreamUSBMIDICable3) MIDICableUSBUpstream{2, false, false};
	new (&dinMIDIPorts) MIDICableDINPorts{};

	recountSmallestMPEZones();
	anyChangesToSave = false;
	successfullyReadDevicesFromFile = false;
	readDevicesFromFile();
}

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
		else if (!strcmp(tagName, "receiveClock")) {
			// this is actually not much duplicated code, just checks for nulls and then an attempt to create a device
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->receiveClock = reader.readTagOrAttributeValueInt();
			}
		}
		else if (!strcmp(tagName, "is_relative")) {
			// this is actually not much duplicated code, just checks for nulls and then an attempt to create a device
			if (!device) {
				if (!name.isEmpty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->is_relative = reader.readTagOrAttributeValueInt();
			}
		}

		reader.exitTag();
	}

	// Hook point!
	if (device) {}
}

} // namespace MIDIDeviceManager

// Forwards to this device's queue manager, which flushes opportunistically (or drops the message) if the
// target lane is still full afterward.
void ConnectedUSBMIDIDevice::enqueue_message(uint32_t fullMessage, MIDIIntent intent) {
	queue_manager().enqueue_message(fullMessage, intent);
}

bool ConnectedUSBMIDIDevice::hasBufferedSendData() {
	// True when at least one queued USB-MIDI message exists across any priority lane.
	return queue_manager().has_buffered_send_data();
}

// Reports capacity across all priority lanes, in MIDI payload bytes rather than raw USB event slots.
int ConnectedUSBMIDIDevice::sendBufferSpace() {
	return queue_manager().send_buffer_space();
}

// Moves queued USB messages into the contiguous dataSendingNow buffer handed to the hardware driver.
bool ConnectedUSBMIDIDevice::consume_queued_messages() {
	return queue_manager().consume_queued_messages(dataSendingNow, numBytesSendingNow, g_usb_usbmode == USB_HOST);
}

void ConnectedUSBMIDIDevice::setup() {
	numBytesSendingNow = 0;
	currentlyWaitingToReceive = false;
	numBytesReceived = 0;

	// default to only a single port
	maxPortConnected = 0;
}

ConnectedUSBMIDIDevice::ConnectedUSBMIDIDevice() {
	sq = 0;
	canHaveMIDISent = false;
	setup();
	memset(receiveData, 0, sizeof(receiveData));
	memset(dataSendingNow, 0, MIDI_SEND_BUFFER_LEN_INNER * 4);
	queue_manager().reset_queue_storage();
}

ConnectedDINMIDIDevice::ConnectedDINMIDIDevice() {
	queue_manager_.reset_queue_storage();
}

void ConnectedDINMIDIDevice::reset_serial_state(uint32_t now_sample_timer) {
	queue_manager_.reset_serial_state(now_sample_timer);
}

bool ConnectedDINMIDIDevice::has_serial_data() const {
	return queue_manager_.has_serial_data();
}

size_t ConnectedDINMIDIDevice::send_buffer_space() const {
	return queue_manager_.send_buffer_space();
}

void ConnectedDINMIDIDevice::enqueue_message(MIDIMessage message) {
	queue_manager_.enqueue_message(message);
}

bool ConnectedDINMIDIDevice::enqueue_sysex(uint8_t const* data, int32_t len) {
	return queue_manager_.enqueue_sysex(data, len);
}

void ConnectedDINMIDIDevice::consume_queued_messages(uint32_t now_sample_timer) {
	queue_manager_.consume_queued_messages(now_sample_timer);
}

#pragma GCC diagnostic pop
