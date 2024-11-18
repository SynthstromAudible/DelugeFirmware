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
#include "io/midi/cable_types/usb_common.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/root_complex/usb_peripheral.h"
#include "io/usb/usb_state.h"
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "storage/storage_manager.h"
#include "util/container/vector/named_thing_vector.h"
#include "util/misc.h"
#include <memory>

using namespace deluge::io::usb;

extern "C" {
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_basic_define.h"
#include "drivers/uart/uart.h"
}
#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_DEVICES_XML "SETTINGS/MIDIDevices.XML"

PLACE_SDRAM_BSS ConnectedUSBMIDIDevice connectedUSBMIDIDevices[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

namespace MIDIDeviceManager {

bool differentiatingInputsByDevice = true;

struct USBDev {
	String name{};
	uint16_t vendorId;
	uint16_t productId;
};
std::array<USBDev, USB_NUM_USBIP> usbDeviceCurrentlyBeingSetUp{};

PLACE_SDRAM_BSS DINRootComplex root_din{};
gsl::owner<MIDIRootComplex*> root_usb{nullptr};

uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput = 15;
uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput = 0;

bool anyChangesToSave = false;

// Gets called within UITimerManager, which may get called during SD card routine.
void slowRoutine() {
	if (!root_usb) {
		// Nothing to do if there's no USB device connected.
		return;
	}

	for (auto& usbCable : root_usb->getCables()) {
		auto& cable = static_cast<MIDICableUSB&>(usbCable);
		cable.sendMCMsNowIfNeeded();

		if (root_usb->getType() == RootComplexType::RC_USB_HOST) {
			auto& hostCable = static_cast<MIDICableUSBHosted&>(cable);
			if (hostCable.freshly_connected) {
				hostCable.hookOnConnected();
				hostCable.freshly_connected = false;
			}
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
	MIDIRootComplexUSBHosted* root = getHosted();
	if (getHosted() == nullptr) {
		return nullptr;
	}

	auto& hostedMIDIDevices = root->getHostedMIDIDevices();

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
			if (vendorId != 0u) {
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

	if (root_usb) {
		for (auto& cable : root_usb->getCables()) {
			recountSmallestMPEZonesForCable(cable);
		}
	}

	recountSmallestMPEZonesForCable(root_din.cable);
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
extern "C" void tud_mount_cb() {
	constexpr size_t ip = 0;
	// Leave this - we'll use this device for all upstream ports
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][0];

	auto* root = new MIDIRootComplexUSBPeripheral();
	MIDIDeviceManager::setUSBRoot(root);

	// add second port here
	connectedDevice->setup();
	for (auto i = 0; i < 3; ++i) {
		auto* cable = static_cast<MIDICableUSB*>(root->getCable(i));
		cable->connectedNow(0);
		connectedDevice->cable[i] = cable;
	}

	connectedDevice->maxPortConnected = 2;
	connectedDevice->canHaveMIDISent = 1;

	anyUSBSendingStillHappening[ip] = 0; // Initialize this. There's obviously nothing sending yet right now.

	recountSmallestMPEZones();
}

extern "C" void tud_unmount_cb() {
	constexpr size_t ip = 0;
	// will need to reset all devices if more are added
	int32_t ports = connectedUSBMIDIDevices[ip][0].maxPortConnected;
	for (int32_t i = 0; i <= ports; i++) {
		auto* cable = connectedUSBMIDIDevices[ip][0].cable[i];
		cable->connectionFlags = 0;
		connectedUSBMIDIDevices[ip][0].cable[i] = nullptr;
	}
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
			constexpr char const* const kUpstreamUSB = "upstreamUSB";
			constexpr auto kUpstreamUSBLen = (std::string{kUpstreamUSB}).length();

			if (root_usb && root_usb->getType() == RootComplexType::RC_USB_PERIPHERAL
			    && !strncmp(kUpstreamUSB, port, strlen(kUpstreamUSB))) {
				switch (port[kUpstreamUSBLen]) {
				case '\0':
					device = root_usb->getCable(0);
					break;
				case '2':
					device = root_usb->getCable(1);
					break;
				case '3':
					device = root_usb->getCable(2);
					break;
				default:
					break;
				}
			}
			else if (!strcmp(port, "din")) {
				device = &root_din.cable;
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
	uint16_t vendor_id = *(uint16_t const*)memory;

	MIDICable* cable;

	if (vendor_id == VENDOR_ID_NONE) {
		cable = nullptr;
	}
	else if (root_usb != nullptr && root_usb->getType() == RootComplexType::RC_USB_PERIPHERAL) {
		if (vendor_id == VENDOR_ID_UPSTREAM_USB) {
			cable = root_usb->getCable(0);
		}
		else if (vendor_id == VENDOR_ID_UPSTREAM_USB2) {
			cable = root_usb->getCable(1);
		}
		else if (vendor_id == VENDOR_ID_UPSTREAM_USB3) {
			cable = root_usb->getCable(2);
		}
	}
	else if (vendor_id == VENDOR_ID_DIN) {
		cable = &root_din.cable;
	}
	else {
		uint16_t product_id = *(reinterpret_cast<uint16_t const*>(memory + 2));
		cable = getOrCreateHostedMIDIDeviceFromDetails(nullptr, vendor_id, product_id);
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

	bool anyWorthWritting = root_din.cable.worthWritingToFile();

	// First, see if it's even worth writing anything
	if (!anyWorthWritting && root_usb != nullptr) {
		for (auto& cable : root_usb->getCables()) {
			if (cable.worthWritingToFile()) {
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

	if (root_din.cable.worthWritingToFile()) {
		root_din.cable.writeToFile(writer, "dinPorts");
	}

	if (root_usb != nullptr) {
		switch (root_usb->getType()) {
		case RootComplexType::RC_DIN:
			// illegal
			break;
		case RootComplexType::RC_USB_PERIPHERAL:
			if (root_usb->getCable(0)->worthWritingToFile()) {
				root_usb->getCable(0)->writeToFile(writer, "upstreamUSBDevice");
			}
			if (root_usb->getCable(1)->worthWritingToFile()) {
				root_usb->getCable(1)->writeToFile(writer, "upstreamUSBDevice2");
			}
			break;
		case RootComplexType::RC_USB_HOST:
			for (auto& cable : root_usb->getCables()) {
				auto& cableHosted = static_cast<MIDICableUSBHosted&>(cable);
				if (cableHosted.worthWritingToFile()) {
					cableHosted.writeToFile(writer, "hostedUSBDevice");
				}
				cableHosted.hookOnWriteHostedDeviceToFile();
			}
			break;
		}
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
			root_din.cable.readFromFile(reader);
		}
		else if (root_usb != nullptr) {
			auto type = root_usb->getType();
			if (type == RootComplexType::RC_USB_PERIPHERAL) {
				constexpr char const* const kUpstreamUSB = "upstreamUSBDevice";
				constexpr auto kUpstreamUSBLen = (std::string{kUpstreamUSB}).length();

				if (strncmp(kUpstreamUSB, tagName, strlen(kUpstreamUSB)) == 0) {

					switch (tagName[kUpstreamUSBLen]) {
					case '\0':
						root_usb->getCable(0)->readFromFile(reader);
						break;
					case '2':
						root_usb->getCable(1)->readFromFile(reader);
						break;
					case '3':
						root_usb->getCable(3)->readFromFile(reader);
						break;
					}
				}
			}
			else if (type == RootComplexType::RC_USB_HOST && strcmp(tagName, "hostedUSBDevice") == 0) {
				readAHostedDeviceFromFile(reader);
			}
		}

		reader.exitTag();
	}

	activeDeserializer->closeWriter();

	recountSmallestMPEZones();

	soundEditor.mpeZonesPotentiallyUpdated();

	successfullyReadDevicesFromFile = true;
}

/// Read a single hosted USB device. This assumes the root complex is a MIDIRootComplexUSBHosted
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

void setUSBRoot(gsl::owner<MIDIRootComplex*> root) {
	delete root_usb;
	root_usb = root;
}

MIDIRootComplexUSBHosted* getHosted() {
	if (root_usb == nullptr) {
		return nullptr;
	}
	if (root_usb->getType() != RootComplexType::RC_USB_HOST) {
		return nullptr;
	}

	return static_cast<MIDIRootComplexUSBHosted*>(root_usb);
}

} // namespace MIDIDeviceManager

void ConnectedUSBMIDIDevice::bufferMessage(uint32_t fullMessage) {
	uint32_t queued = ringBufWriteIdx - ringBufReadIdx;
	if (queued > 16) {
		if (!anyUSBSendingStillHappening[0]) {
			midiEngine.flushMIDI();
		}
		queued = ringBufWriteIdx - ringBufReadIdx;
	}
	if (queued > MIDI_SEND_BUFFER_LEN_RING) {
		// TODO: show some error message
		return;
	}

	sendDataRingBuf[ringBufWriteIdx & MIDI_SEND_RING_MASK] = fullMessage;
	ringBufWriteIdx++;

	deluge::io::usb::anythingInUSBOutputBuffer = true;
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

		// some seem to take even less, especially with hubs involved. The hydrasynth seems to only respond to a max of
		// 2 messages per transfer, the third gets blocked. For MPE this leads to ignoring note ons as the x and y
		// resets are sent before the note on
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
ConnectedUSBMIDIDevice::ConnectedUSBMIDIDevice() {
	currentlyWaitingToReceive = 0;
	sq = 0;
	canHaveMIDISent = 0;
	numBytesReceived = 0;
	memset(receiveData, 0, 64);
	memset(dataSendingNow, 0, MIDI_SEND_BUFFER_LEN_INNER * 4);
	numBytesSendingNow = 0;
	memset(sendDataRingBuf, 0, MIDI_SEND_BUFFER_LEN_RING);
	ringBufWriteIdx = 0;
	ringBufReadIdx = 0;

	maxPortConnected = 0;
}

/*
    for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
        MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);

    }
 */
#pragma GCC diagnostic pop
