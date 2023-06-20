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
#include <string.h>
#include "numericDriver.h"
#include "storageManager.h"
#include "DString.h"

#define RUNTIME_FEATURE_SETTINGS_FILE "CommunityFeatures.XML"
#define TAG_RUNTIME_FEATURE_SETTINGS "runtimeFeatureSettings"
#define TAG_RUNTIME_FEATURE_SETTING "setting"
#define TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME "name"
#define TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE "value"


RuntimeFeatureSettings runtimeFeatureSettings;

RuntimeFeatureSettings::RuntimeFeatureSettings() {}

void RuntimeFeatureSettings::readSettingsFromFile() {
    numericDriver.displayPopup("Community file err");

	FilePointer fp;
	bool success = storageManager.fileExists(RUNTIME_FEATURE_SETTINGS_FILE, &fp);
	if (!success) return;

	int error = storageManager.openXMLFile(&fp, TAG_RUNTIME_FEATURE_SETTINGS);
	if (error) return;

    String currentName;
    int32_t currentValue = 0;
	char const* currentTag;
	while (*(currentTag = storageManager.readNextTagOrAttributeName())) {
		if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING) == 0) {
            // Read name
            currentTag = storageManager.readNextTagOrAttributeName();
            if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME) != 0) { numericDriver.displayPopup("Community file err"); goto readEnd; }
            storageManager.readTagOrAttributeValueString(&currentName);
            storageManager.exitTag();

            // Read value
            currentTag = storageManager.readNextTagOrAttributeName();
            if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE) != 0) { numericDriver.displayPopup("Community file err"); goto readEnd; }
            currentValue = storageManager.readTagOrAttributeValueInt();
            storageManager.exitTag();

            //@TODO: Search for the setting and store it either in the setting struct or in the unsupported settings
		}

		storageManager.exitTag();
	}

readEnd:
	storageManager.closeFile();
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
        storageManager.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTING);
        storageManager.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME, settings[idxSetting].xmlName, false);
        storageManager.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE, settings[idxSetting].value, false);
        storageManager.writeOpeningTagEnd(false);
        storageManager.writeClosingTag(TAG_RUNTIME_FEATURE_SETTING, false);
    }
    
    //@TODO: Write unknown settings

	storageManager.writeClosingTag(TAG_RUNTIME_FEATURE_SETTINGS);

	storageManager.closeFileAfterWriting();
}
