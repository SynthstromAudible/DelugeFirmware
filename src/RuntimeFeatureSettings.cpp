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

#include "RuntimeFeatureSettings.h"

RuntimeFeatureSettings runtimeFeatureSettings;

RuntimeFeatureSettings::RuntimeFeatureSettings() {

}

void RuntimeFeatureSettings::readSettingsFromFile() {
    // Copied code from MIDIDeviceManager for reference
    /*
    	if (successfullyReadDevicesFromFile) return; // Yup, we only want to do this once

	FilePointer fp;
	bool success = storageManager.fileExists("MIDIDevices.XML", &fp);
	if (!success) return;

	int error = storageManager.openXMLFile(&fp, "midiDevices");
	if (error) return;

	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "dinPorts")) {
			dinMIDIPorts.readFromFile();
		}
		else if (!strcmp(tagName, "upstreamUSBDevice")) {
			upstreamUSBMIDIDevice.readFromFile();
		}
		else if (!strcmp(tagName, "hostedUSBDevice")) {
			readAHostedDeviceFromFile();
		}

		storageManager.exitTag();
	}

	storageManager.closeFile();

	recountSmallestMPEZones();

	soundEditor.mpeZonesPotentiallyUpdated();

	successfullyReadDevicesFromFile = true;
}

void readAHostedDeviceFromFile() {
	MIDIDeviceUSBHosted* device = NULL;

	String name;
	uint16_t vendorId;
	uint16_t productId;

	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {

		int whichPort;

		if (!strcmp(tagName, "vendorId")) {
			vendorId = storageManager.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "productId")) {
			productId = storageManager.readTagOrAttributeValueHex(0);
		}
		else if (!strcmp(tagName, "name")) {
			storageManager.readTagOrAttributeValueString(&name);
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
				device->ports[whichPort].readFromFile((whichPort == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) ? device : NULL);
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

			if (device) device->defaultVelocityToLevel = storageManager.readTagOrAttributeValueInt();
		}

		storageManager.exitTag();
	}
    */
}

void RuntimeFeatureSettings::writeSettingsToFile() {
    // Copied code from MIDIDeviceManager for reference
    /*
    	if (!anyChangesToSave) return;
	anyChangesToSave = false;

	// First, see if it's even worth writing anything
	if (dinMIDIPorts.worthWritingToFile()) goto worthIt;
	if (upstreamUSBMIDIDevice.worthWritingToFile()) goto worthIt;

	for (int d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);
		if (device->worthWritingToFile()) goto worthIt;
	}

	// If still here, nothing worth writing. Delete the file if there was one.
	f_unlink("MIDIDevices.XML"); // May give error, but no real consequence from that.
	return;

worthIt:
	int error = storageManager.createXMLFile("MIDIDevices.XML", true);
	if (error) return;

	storageManager.writeOpeningTagBeginning("midiDevices");
	storageManager.writeFirmwareVersion();
	storageManager.writeEarliestCompatibleFirmwareVersion("4.0.0");
	storageManager.writeOpeningTagEnd();

	if (dinMIDIPorts.worthWritingToFile()) dinMIDIPorts.writeToFile("dinPorts");
	if (upstreamUSBMIDIDevice.worthWritingToFile()) upstreamUSBMIDIDevice.writeToFile("upstreamUSBDevice");

	for (int d = 0; d < hostedMIDIDevices.getNumElements(); d++) {
		MIDIDeviceUSBHosted* device = (MIDIDeviceUSBHosted*)hostedMIDIDevices.getElement(d);
		if (device->worthWritingToFile()) {
			device->writeToFile("hostedUSBDevice");
		}
	}

	storageManager.writeClosingTag("midiDevices");

	storageManager.closeFileAfterWriting();
    */
}