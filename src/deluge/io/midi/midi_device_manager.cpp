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
#include "deluge/deluge.h" // setTimeUSBInitializationEnds (app-owned popup-suppression window)
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/midi_io.h" // deluge_midi_init/_usb_port/_write/_write_space, deluge_midi_poll_usb_host_event
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "storage/byte_channel.h"
#include "storage/existence_policy.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include <algorithm>
#include <new>
#include <strings.h>

#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_DEVICES_XML "SETTINGS/MIDIDevices.XML"
/// Where the file lived before c1.3, at the card root. Migrated into the path above on a read that
/// finds nothing there.
#define LEGACY_MIDI_DEVICES_XML "MIDIDevices.XML"

PLACE_SDRAM_BSS ConnectedUSBMIDIDevice connectedUSBMIDIDevices[DELUGE_USB_NUM_CONTROLLERS][MAX_NUM_USB_MIDI_DEVICES];

namespace MIDIDeviceManager {

deluge::fast_vector<MIDICableUSBHosted*> hostedMIDIDevices;

/// Comparator maintaining hostedMIDIDevices' case-insensitive sort by name
static bool cableNameLess(MIDICableUSBHosted* cable, char const* name) {
	return strcasecmp(cable->name.c_str(), name) < 0;
}

bool differentiatingInputsByDevice = true;

struct USBDev {
	std::string name{};
	uint16_t vendorId{};
	uint16_t productId{};
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
	// Assign each device slot its boundary MIDI port, then initialise the MIDI
	// transport. Runs before the USB stack is opened, so the ports are valid
	// before any send/receive can use them.
	for (int32_t ip = 0; ip < DELUGE_USB_NUM_CONTROLLERS; ip++) {
		for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
			connectedUSBMIDIDevices[ip][d].port = deluge_midi_usb_port(ip, d);
		}
	}
	deluge_midi_init();
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

	for (MIDICableUSBHosted* device : hostedMIDIDevices) {
		device->sendMCMsNowIfNeeded();

		// This routine placed here because for whatever reason we can't send sysex from hostedDeviceConfigured
		if (device->freshly_connected) {
			device->hookOnConnected();
			device->freshly_connected = false; // Must be set to false here or the hook will run forever
		}
	}
}

extern "C" void giveDetailsOfDeviceBeingSetUp(int32_t ip, char const* name, uint16_t vendorId, uint16_t productId) {

	usbDeviceCurrentlyBeingSetUp[ip].name = name; // If that fails, it'll just have a 0-length name
	usbDeviceCurrentlyBeingSetUp[ip].vendorId = vendorId;
	usbDeviceCurrentlyBeingSetUp[ip].productId = productId;

	D_PRINTLN("name: %s  vendor: %d  product: %d", name, vendorId, productId);
}

// name can be NULL, or an empty std::string
MIDICableUSBHosted* getOrCreateHostedMIDIDeviceFromDetails(std::string* name, uint16_t vendorId, uint16_t productId) {

	// Do we know any details about this device already?

	bool gotAName = (name && !name->empty());

	if (gotAName) {
		// Search by name first
		auto it = std::lower_bound(hostedMIDIDevices.begin(), hostedMIDIDevices.end(), name->c_str(), cableNameLess);

		// If we'd already seen it before...
		if (it != hostedMIDIDevices.end() && strcasecmp((*it)->name.c_str(), name->c_str()) == 0) {
			MIDICableUSBHosted* device = *it;

			// Update vendor and product id, if we have those
			if (vendorId) {
				device->vendorId = vendorId;
				device->productId = productId;
			}

			return device;
		}
	}

	// Ok, try searching by vendor / product id
	for (MIDICableUSBHosted* candidate : hostedMIDIDevices) {
		if (candidate->vendorId == vendorId && candidate->productId == productId) {
			// Update its name - if we got one and it's different
			if (gotAName && !(candidate->name == *name)) {
				std::erase(hostedMIDIDevices, candidate);
				candidate->name = *name; // Can't fail
				hostedMIDIDevices.insert(std::lower_bound(hostedMIDIDevices.begin(), hostedMIDIDevices.end(),
				                                          candidate->name.c_str(), cableNameLess),
				                         candidate);
			}
			return candidate;
		}
	}

	try {
		hostedMIDIDevices.reserve(hostedMIDIDevices.size() + 1);
	} catch (deluge::exception&) {
		return nullptr;
	}

	MIDICableUSBHosted* device = nullptr;

	SpecificMidiDeviceType devType = getSpecificMidiDeviceType(vendorId, productId);
	if (devType == SpecificMidiDeviceType::LUMI_KEYS) {
		void* memory = deluge::memory::alloc_fast(sizeof(MIDIDeviceLumiKeys));
		if (!memory) {
			return nullptr;
		}

		MIDIDeviceLumiKeys* instDevice = new (memory) MIDIDeviceLumiKeys();
		device = instDevice;
	}
	else {
		void* memory = deluge::memory::alloc_fast(sizeof(MIDICableUSBHosted));
		if (!memory) {
			return nullptr;
		}

		MIDICableUSBHosted* instDevice = new (memory) MIDICableUSBHosted();
		device = instDevice;
	}

	if (gotAName) {
		device->name = *name;
	}
	device->vendorId = vendorId;
	device->productId = productId;

	// Store record of this device. We reserved space above, so this can't throw.
	hostedMIDIDevices.insert(
	    std::lower_bound(hostedMIDIDevices.begin(), hostedMIDIDevices.end(), device->name.c_str(), cableNameLess),
	    device);

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

	for (MIDICableUSBHosted* cable : hostedMIDIDevices) {
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

	// App policy, from the device's name. (The transport's own per-slot state —
	// sequence bit, counters, connected flag — is reset below the boundary, from
	// the HAL enumeration path.)
	connectedDevice->canHaveMIDISent = (bool)strcmp(device->name.c_str(), "Synthstrom MIDI Foot Controller");
	connectedDevice->canHaveMIDISent = (bool)strcmp(device->name.c_str(), "LUMI Keys BLOCK");

	device->connectedNow(midiDeviceNum);
	recountSmallestMPEZones(); // Must be called after setting device->connectionFlags

	device->freshly_connected = true; // Used to trigger hookOnConnected from the input loop

	std::string text;
	text = device->name;
	text.append(" attached");
	Error error = Error::NONE;
	if (error == Error::NONE) {
		consoleTextIfAllBootedUp(text.c_str());
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

	recountSmallestMPEZones();
}

// Returns NULL if insufficient details found, or not enough RAM to create
MIDICable* readDeviceReferenceFromFile(Deserializer& reader) {

	uint16_t vendorId = 0;
	uint16_t productId = 0;
	std::string name;
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
			reader.readTagOrAttributeValueString(name);
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
	if (!name.empty() || vendorId) {
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

/// @brief `writeDevicesToFile`'s body, run where blocking on the card is legal.
void writeDevicesOnOwner();
/// @brief `readDevicesFromFile`'s body, run where blocking on the card is legal.
void readDevicesOnOwner();
/// @brief Walk an already-opened document into the in-memory device table.
///
/// Shared by both arms: the owner arm's reader is a file, the channel arm's is the bytes the channel
/// delivered, and neither difference reaches here.
void parseDevices(Deserializer& reader);
/// @brief Write the device table into an already-opened serialiser, file- or memory-backed.
void serialiseDevices(Serializer& writer);
/// @brief `deluge::storage::bytes::ParseFn` for @ref parseDevices.
void parseDevicesBytes(void* ctx, char* bytes, uint32_t len);

/// @brief Whether any port or hosted device has settings worth putting in the file.
///
/// Factored out of `writeDevicesOnOwner` so the dispatcher can ask it before choosing an arm: a
/// "nothing worth writing" answer means DELETE the file, and this channel carries no unlink.
bool anythingWorthWriting() {
	if (dinMIDIPorts.worthWritingToFile() || upstreamUSBMIDICable1.worthWritingToFile()
	    || upstreamUSBMIDICable2.worthWritingToFile()) {
		return true;
	}
	for (MIDICableUSBHosted* device : hostedMIDIDevices) {
		if (device->worthWritingToFile()) {
			return true;
		}
	}
	return false;
}

void writeDevicesToFile() {
	if (deluge::storage::bytes::usingByteChannel() && anyChangesToSave && !anythingWorthWriting()) {
		// Nothing worth keeping: remove the file -- the unlink the owner arm below makes inline, carried by the channel
		// instead. A file that is already gone counts as removed.
		if (deluge::storage::bytes::submitUnlink(MIDI_DEVICES_XML)) {
			anyChangesToSave = false;
			return;
		}
	}
	if (deluge::storage::bytes::usingByteChannel() && anyChangesToSave && anythingWorthWriting()) {
		// Serialise into memory -- no card contact at all -- and hand the bytes over. The serialiser
		// dies at the end of this scope, which is safe because `submitWrite` copies.
		XMLSerializer writer;
		writer.setMemoryBased();
		writer.write(StorageManager::kXmlDocumentHead); // the owner arm gets this from createXMLFile
		serialiseDevices(writer);
		if (deluge::storage::bytes::submitWrite(MIDI_DEVICES_XML, writer.getBufferPtr(),
		                                        static_cast<uint32_t>(writer.bytesWritten()))) {
			// Cleared here rather than on the collect, for the reason `PerformanceView` gives: the
			// flag means "the table in memory matches what we last asked to be stored", and the ask
			// has happened.
			anyChangesToSave = false;
			return;
		}
		// Not taken: too large for the channel (a busy channel queues instead). The body below runs only where
		// blocking is legal; after boot, off the owner, it is refused and nothing is written.
	}
	// A refusal leaves `anyChangesToSave` set, so the next save retries.
	(void)deluge::storage::bytes::runInlineWhereLegal([](void*) { writeDevicesOnOwner(); }, nullptr);
}

void writeDevicesOnOwner() {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Settings);
	if (!anyChangesToSave) {
		return;
	}
	anyChangesToSave = false;

	if (!anythingWorthWriting()) {
		// If still here, nothing worth writing. Delete the file if there was one.
		deluge::io::unlink(MIDI_DEVICES_XML); // May give error, but no real consequence from that.
		return;
	}

	Error error = StorageManager::createXMLFile(MIDI_DEVICES_XML, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}

	serialiseDevices(GetSerializer());
	GetSerializer().closeFileAfterWriting();
}

void serialiseDevices(Serializer& writer) {
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

	for (MIDICableUSBHosted* device : hostedMIDIDevices) {
		if (device->worthWritingToFile()) {
			device->writeToFile(writer, "hostedUSBDevice");
		}
		// Stow this for the hook  point later
		device->hookOnWriteHostedDeviceToFile();
	}

	writer.writeClosingTag("midiDevices");
}

bool successfullyReadDevicesFromFile = false; // We'll only do this one time

void factoryReset(bool showPopup) {
	if (showPopup) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RESET_MIDI_DEVICES));
	}

	// May give an error (e.g. no such file), but there is no real consequence from that.
	(void)deluge::io::unlink(MIDI_DEVICES_XML);

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
	if (deluge::storage::bytes::usingByteChannel()) {
		if (successfullyReadDevicesFromFile) {
			return; // Yup, we only want to do this once
		}
		if (deluge::storage::bytes::submitRead(MIDI_DEVICES_XML, LEGACY_MIDI_DEVICES_XML, &parseDevicesBytes,
		                                       /*onAbsent=*/nullptr, nullptr)) {
			return;
		}
		// Not taken (a busy channel queues instead): the body below runs only where blocking is legal, which
		// here is nowhere, so the table in memory stands.
	}
	(void)deluge::storage::bytes::runInlineWhereLegal([](void*) { readDevicesOnOwner(); }, nullptr);
}

void parseDevicesBytes(void* /*ctx*/, char* bytes, uint32_t len) {
	// `activeDeserializer` goes back on EVERY path, the failed open included -- see
	// `RuntimeFeatureSettings::parseSettingsBytes`, which carries the full reasoning.
	FileDeserializer* previous = activeDeserializer;
	if (StorageManager::openDelugeBuffer(std::span{reinterpret_cast<uint8_t*>(bytes), len}, false, "midiDevices")
	    == Error::NONE) {
		parseDevices(*activeDeserializer);
		successfullyReadDevicesFromFile = true;
	}
	activeDeserializer = previous;
}

void readDevicesOnOwner() {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Settings);
	// Owner-audit exemption: this runs once at boot, before the storage-owner worker pump is
	// reliably driving, and reads the card when nothing else does (so it is not a FatFS-reentrancy
	// hazard). Left inline deliberately — dispatching onto the owner here could queue an op nothing
	// pumps yet. The "FatFS entered only on the owner" audit assert must whitelist this site.
	// See docs/superpowers/specs/2026-07-17-async-sd-rung4-ui-sync-design.md §7.
	if (successfullyReadDevicesFromFile) {
		return; // Yup, we only want to do this once
	}

	switch (deluge::storage::bootstrap_action(StorageManager::fileExists(MIDI_DEVICES_XML))) {
	case deluge::storage::Bootstrap::Load:
		break; // fall through to parsing, exactly as today

	case deluge::storage::Bootstrap::WriteDefaults: {
		// Genuinely absent: since we changed the file path for the MIDIDevices.XML in c1.3, it's
		// possible that a MIDIDevice file may exist in the root of the SD card; if so, let's move
		// it to the new SETTINGS folder (but first make sure folder exists).
		auto result = deluge::io::mkdir(SETTINGS_FOLDER);
		if (result.has_value() || result.error() == deluge::io::Status::EXISTS) {
			auto renamed = deluge::io::rename(LEGACY_MIDI_DEVICES_XML, MIDI_DEVICES_XML);
			if (renamed.has_value()) {
				// this means we moved it; confirm the migration actually landed before trusting
				// the new path.
				if (deluge::storage::bootstrap_action(StorageManager::fileExists(MIDI_DEVICES_XML))
				    == deluge::storage::Bootstrap::Load) {
					break; // fall through to parsing
				}
			}
		}
		return;
	}

	case deluge::storage::Bootstrap::UseInMemoryOnly:
		// Existence unknown: use in-memory defaults for this session and write nothing.
		return;
	}

	Error error = StorageManager::openDocumentSynchronously(MIDI_DEVICES_XML, "midiDevices");
	if (error != Error::NONE) {
		return;
	}
	parseDevices(*activeDeserializer);
	activeDeserializer->closeWriter();
	StorageManager::releaseDocumentStaging();
	successfullyReadDevicesFromFile = true;
}

void parseDevices(Deserializer& reader) {
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

	recountSmallestMPEZones();

	soundEditor.mpeZonesPotentiallyUpdated();
}

void readAHostedDeviceFromFile(Deserializer& reader) {
	MIDICableUSBHosted* device = nullptr;

	std::string name;
	uint16_t vendorId = 0;
	uint16_t productId = 0;

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
			reader.readTagOrAttributeValueString(name);
		}
		else if (!strcmp(tagName, "input")) {
			whichPort = MIDI_DIRECTION_INPUT_TO_DELUGE;
checkDevice:
			if (!device) {
				if (!name.empty() || vendorId) {
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
				if (!name.empty() || vendorId) {
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
				if (!name.empty() || vendorId) {
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
				if (!name.empty() || vendorId) {
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
				if (!name.empty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->is_relative = reader.readTagOrAttributeValueInt();
			}
		}
		else if (!strcmp(tagName, "midi_thru")) {
			// this is actually not much duplicated code, just checks for nulls and then an attempt to create a device
			if (!device) {
				if (!name.empty() || vendorId) {
					device = getOrCreateHostedMIDIDeviceFromDetails(&name, vendorId,
					                                                productId); // Will return NULL if error.
				}
			}

			if (device) {
				device->midi_thru = reader.readTagOrAttributeValueInt() != 0;
			}
		}

		reader.exitTag();
	}

	// Hook point!
	if (device) {}
}

} // namespace MIDIDeviceManager

// USB-MIDI I/O goes through the <libdeluge/midi_io.h> byte-stream boundary: one
// 4-byte event packet per message, on this device slot's port.
void ConnectedUSBMIDIDevice::bufferMessage(uint32_t fullMessage) {
	// The packet's bytes in little-endian word order — the same layout the
	// deluge_midi protocol library's UsbEventPacket::word() produces.
	uint8_t bytes[4] = {
	    (uint8_t)fullMessage,
	    (uint8_t)(fullMessage >> 8),
	    (uint8_t)(fullMessage >> 16),
	    (uint8_t)(fullMessage >> 24),
	};
	deluge_midi_write(port, bytes, sizeof(bytes));
}

int ConnectedUSBMIDIDevice::sendBufferSpace() {
	// The boundary reports transport bytes (4 per event packet); callers reason
	// in bytes of serial MIDI (3 per packet).
	return (int)(deluge_midi_write_space(port) / 4 * 3);
}

void ConnectedUSBMIDIDevice::setup() {
	// The transport state (counters, sequence bit) is reset below the boundary,
	// from the HAL enumeration path. Default to only a single port.
	maxPortConnected = 0;
}
ConnectedUSBMIDIDevice::ConnectedUSBMIDIDevice() {
	canHaveMIDISent = 0;
	// The real port is assigned by MIDIDeviceManager::init() before the USB stack
	// can deliver anything.
	port = 0;
	maxPortConnected = 0;
}

/*
    for (int32_t d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
        MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);

    }
 */
#pragma GCC diagnostic pop
