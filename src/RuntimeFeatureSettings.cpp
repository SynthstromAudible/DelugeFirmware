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
#include "numericDriver.h"

#define RUNTIME_FEATURE_SETTINGS_FILE "CommunityFeatures.XML"
#define TAG_RUNTIME_FEATURE_SETTINGS "runtimeFeatureSettings"
//RuntimeFeatureSetting RuntimeFeatureSettings::settings[RuntimeFeatureSettingType::MaxElement] = ;

RuntimeFeatureSettings runtimeFeatureSettings;

RuntimeFeatureSettings::RuntimeFeatureSettings() {}

void RuntimeFeatureSettings::readSettingsFromFile() {
    numericDriver.displayPopup("LDXML");

    //runtimeFeatureSettingsMenu.items
    // Copied code from MIDIDeviceManager for reference
    /*
    	if (successfullyReadDevicesFromFile) return; // Yup, we only want to do this once

	FilePointer fp;
	bool success = storageManager.fileExists(RUNTIME_FEATURE_SETTINGS_FILE, &fp);
	if (!success) return;

	int error = storageManager.openXMLFile(&fp, TAG_RUNTIME_FEATURE_SETTINGS);
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
	f_unlink(RUNTIME_FEATURE_SETTINGS_FILE); // May give error, but no real consequence from that.

	int error = storageManager.createXMLFile(RUNTIME_FEATURE_SETTINGS_FILE, true);
	if (error) return;

	storageManager.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTINGS);
	storageManager.writeFirmwareVersion();
	storageManager.writeEarliestCompatibleFirmwareVersion("4.1.3");
	storageManager.writeOpeningTagEnd();

    for(uint32_t idxSetting = 0; idxSetting < RuntimeFeatureSettingType::MaxElement; ++idxSetting) {
        //@TODO: Adapt
        /*
        storageManager.writeOpeningTagBeginning("sound");
        storageManager.writeAttribute("name", name.get());

        Sound::writeToFile(savingSong, paramManager, &arpSettings);

        if (savingSong) Drum::writeMIDICommandsToFile();

        storageManager.writeClosingTag("sound");
        */
    }
    
    //@TODO: Write unknown settings

	storageManager.writeClosingTag(TAG_RUNTIME_FEATURE_SETTINGS);

	storageManager.closeFileAfterWriting();
}
