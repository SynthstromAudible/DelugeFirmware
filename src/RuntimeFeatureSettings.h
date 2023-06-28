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

#ifndef RUNTIMEFEATURESETTINGS_H_
#define RUNTIMEFEATURESETTINGS_H_

#include <stdint.h>
#include "MenuItemRuntimeFeatureSettings.h"
#include "DString.h"
#include "ResizeableArray.h"

#define RUNTIME_FEATURE_SETTING_MAX_OPTIONS 8

// State declarations
enum RuntimeFeatureStateToggle : uint32_t { Off = 0, On = 1 };

// Declare additional enums for specific multi state settings (e.g. like RuntimeFeatureStateTrackLaunchStyle)

/// Every setting needs to be delcared in here
enum RuntimeFeatureSettingType : uint32_t {
	// FileFolderSorting // @TODO: Replace with actual identifier on first use
	MaxElement // Keep as boundary
};

/// Definition for selectable options
struct RuntimeFeatureSettingOption {
	const char* displayName;
	uint32_t value; // Value to be defined as typed Enum above
};

/// Every setting keeps its metadata and value in here
struct RuntimeFeatureSetting {
	const char* displayName;
	const char* xmlName;
	uint32_t value;
	RuntimeFeatureSettingOption options[RUNTIME_FEATURE_SETTING_MAX_OPTIONS]; // Limited to safe memory
};

/// Encapsulating class
class RuntimeFeatureSettings {
public:
	RuntimeFeatureSettings();

	// Traded type safety for option values for code simplicity and size, use enum from above to compare
	inline uint32_t get(RuntimeFeatureSettingType type) { return settings[type].value; };

public:
	void readSettingsFromFile();
	void writeSettingsToFile();

protected:
	RuntimeFeatureSetting settings[RuntimeFeatureSettingType::MaxElement] = {

	    //// @TODO: Remove example on first use
	    // [RuntimeFeatureSettingType::FileFolderSorting] =  {
	    //     .displayName = "File/Folder sorting",
	    //     .xmlName = "fileFolderSorting",
	    //     .value = RuntimeFeatureStateToggle::Off, // Default value
	    //     .options = {
	    //         { .displayName = "Off", .value = RuntimeFeatureStateToggle::Off },
	    //         { .displayName = "On", .value = RuntimeFeatureStateToggle::On },
	    //         { .displayName = NULL, .value = 0 }
	    //     }
	    // },

	    // Please extend RuntimeFeatureSettingType and here for additional settings
	    // Usage example -> (runtimeFeatureSettings.get(RuntimeFeatureSettingType::FileFolderSorting) == RuntimeFeatureStateToggle::On)
	};

private:
	ResizeableArray unknownSettings;

public:
	friend class MenuItemRuntimeFeatureSetting;
	friend class MenuItemRuntimeFeatureSettings;
};

/// Static instance for external access
extern RuntimeFeatureSettings runtimeFeatureSettings;

#endif /* RUNTIMEFEATURESETTINGS_H_ */
