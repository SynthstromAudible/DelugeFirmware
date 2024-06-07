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

#include "runtime_feature_settings.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include <cstring>
#include <string_view>

#define RUNTIME_FEATURE_SETTINGS_FILE "CommunityFeatures.XML"
#define TAG_RUNTIME_FEATURE_SETTINGS "runtimeFeatureSettings"
#define TAG_RUNTIME_FEATURE_SETTING "setting"
#define TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME "name"
#define TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE "value"

/// Unknown Settings container
struct UnknownSetting {
	std::string_view name;
	uint32_t value;
};

RuntimeFeatureSettings runtimeFeatureSettings{};

RuntimeFeatureSettings::RuntimeFeatureSettings() : unknownSettings(sizeof(UnknownSetting)) {
}

static void SetupOnOffSetting(RuntimeFeatureSetting& setting, deluge::l10n::String displayName,
                              std::string_view xmlName, RuntimeFeatureStateToggle def) {
	setting.displayName = displayName;
	setting.xmlName = xmlName;
	setting.value = static_cast<uint32_t>(def);

	setting.options = {
	    {
	        .displayName = "Off",
	        .value = RuntimeFeatureStateToggle::Off,
	    },
	    {
	        .displayName = "On",
	        .value = RuntimeFeatureStateToggle::On,
	    },
	};
}

static void SetupSyncScalingActionSetting(RuntimeFeatureSetting& setting, deluge::l10n::String displayName,
                                          std::string_view xmlName, RuntimeFeatureStateSyncScalingAction def) {
	setting.displayName = displayName;
	setting.xmlName = xmlName;
	setting.value = static_cast<uint32_t>(def);

	setting.options = {
	    {
	        .displayName = display->haveOLED() ? "Sync Scaling" : "SCAL",
	        .value = RuntimeFeatureStateSyncScalingAction::SyncScaling,
	    },
	    {
	        .displayName = display->haveOLED() ? "Fill mode" : "FILL",
	        .value = RuntimeFeatureStateSyncScalingAction::Fill,
	    },
	};
}

static void SetupEmulatedDisplaySetting(RuntimeFeatureSetting& setting, deluge::l10n::String displayName,
                                        std::string_view xmlName, RuntimeFeatureStateEmulatedDisplay def) {
	setting.displayName = displayName;
	setting.xmlName = xmlName;
	setting.value = static_cast<uint32_t>(def);

	// what is displayed depends on the physical display type more the active mode
	bool have_oled = deluge::hid::display::have_oled_screen;
	setting.options = {
	    {
	        .displayName = have_oled ? "OLED" : "7SEG",
	        .value = RuntimeFeatureStateEmulatedDisplay::Hardware,
	    },
	    {
	        .displayName = display->haveOLED() ? "Toggle" : "TOGL",
	        .value = RuntimeFeatureStateEmulatedDisplay::Toggle,
	    },
	    {
	        .displayName = have_oled ? "7SEG" : "OLED",
	        .value = RuntimeFeatureStateEmulatedDisplay::OnBoot,
	    },
	};
}

void RuntimeFeatureSettings::init() {
	using enum deluge::l10n::String;
	// Drum randomizer
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::DrumRandomizer], STRING_FOR_COMMUNITY_FEATURE_DRUM_RANDOMIZER,
	                  "drumRandomizer", RuntimeFeatureStateToggle::On);
	// Quantize
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::Quantize], STRING_FOR_COMMUNITY_FEATURE_QUANTIZE, "quantize",
	                  RuntimeFeatureStateToggle::On);
	// FineTempoKnob
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::FineTempoKnob], STRING_FOR_COMMUNITY_FEATURE_FINE_TEMPO_KNOB,
	                  "fineTempoKnob", RuntimeFeatureStateToggle::On);
	// PatchCableResolution
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::PatchCableResolution],
	                  STRING_FOR_COMMUNITY_FEATURE_MOD_DEPTH_DECIMALS, "modDepthDecimals",
	                  RuntimeFeatureStateToggle::On);
	// CatchNotes
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::CatchNotes], STRING_FOR_COMMUNITY_FEATURE_CATCH_NOTES,
	                  "catchNotes", RuntimeFeatureStateToggle::On);
	// DeleteUnusedKitRows
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::DeleteUnusedKitRows],
	                  STRING_FOR_COMMUNITY_FEATURE_DELETE_UNUSED_KIT_ROWS, "deleteUnusedKitRows",
	                  RuntimeFeatureStateToggle::On);
	// AltGoldenKnobDelayParams
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::AltGoldenKnobDelayParams],
	                  STRING_FOR_COMMUNITY_FEATURE_ALT_DELAY_PARAMS, "altGoldenKnobDelayParams",
	                  RuntimeFeatureStateToggle::Off);
	// QuantizedStutterRate
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::QuantizedStutterRate],
	                  STRING_FOR_COMMUNITY_FEATURE_QUANTIZED_STUTTER, "quantizedStutterRate",
	                  RuntimeFeatureStateToggle::Off);
	// devSysexAllowed
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::DevSysexAllowed], STRING_FOR_COMMUNITY_FEATURE_DEV_SYSEX,
	                  "devSysexAllowed", RuntimeFeatureStateToggle::Off);
	// SyncScalingAction
	SetupSyncScalingActionSetting(settings[RuntimeFeatureSettingType::SyncScalingAction],
	                              STRING_FOR_COMMUNITY_FEATURE_SYNC_SCALING_ACTION, "syncScalingAction",
	                              RuntimeFeatureStateSyncScalingAction::SyncScaling);
	// HighlightIncomingNotes
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::HighlightIncomingNotes],
	                  STRING_FOR_COMMUNITY_FEATURE_HIGHLIGHT_INCOMING_NOTES, "highlightIncomingNotes",
	                  RuntimeFeatureStateToggle::On);
	// DisplayNornsLayout
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::DisplayNornsLayout],
	                  STRING_FOR_COMMUNITY_FEATURE_NORNS_LAYOUT, "displayNornsLayout", RuntimeFeatureStateToggle::Off);

	// ShiftIsSticky
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::ShiftIsSticky], STRING_FOR_COMMUNITY_FEATURE_STICKY_SHIFT,
	                  "stickyShift", RuntimeFeatureStateToggle::Off);

	// LightShiftLed
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::LightShiftLed], STRING_FOR_COMMUNITY_FEATURE_LIGHT_SHIFT,
	                  "lightShift", RuntimeFeatureStateToggle::Off);

	// EnableGrainFX
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::EnableGrainFX], STRING_FOR_COMMUNITY_FEATURE_GRAIN_FX,
	                  "enableGrainFX", RuntimeFeatureStateToggle::Off);
	// EnableDxShortcuts
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::EnableDxShortcuts], STRING_FOR_COMMUNITY_FEATURE_DX_SHORTCUTS,
	                  "enableDxShortcuts", RuntimeFeatureStateToggle::Off);

	// EmulatedDisplay
	SetupEmulatedDisplaySetting(settings[RuntimeFeatureSettingType::EmulatedDisplay],
	                            STRING_FOR_COMMUNITY_FEATURE_EMULATED_DISPLAY, "emulatedDisplay",
	                            RuntimeFeatureStateEmulatedDisplay::Hardware);

	// EnableKeyboardViewSidebarMenuExit
	SetupOnOffSetting(settings[RuntimeFeatureSettingType::EnableKeyboardViewSidebarMenuExit],
	                  STRING_FOR_COMMUNITY_FEATURE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT, "enableKeyboardViewSidebarMenuExit",
	                  RuntimeFeatureStateToggle::Off);
}

void RuntimeFeatureSettings::readSettingsFromFile(StorageManager& bdsm) {
	FilePointer fp;

	bool success = bdsm.fileExists(RUNTIME_FEATURE_SETTINGS_FILE, &fp);
	if (!success) {
		return;
	}

	Error error = bdsm.openXMLFile(&fp, smDeserializer, TAG_RUNTIME_FEATURE_SETTINGS);
	if (error != Error::NONE) {
		return;
	}
	Deserializer& reader = smDeserializer;
	String currentName;
	int32_t currentValue = 0;
	char const* currentTag = nullptr;

	while (*(currentTag = reader.readNextTagOrAttributeName())) {

		if (strcmp(currentTag, "startupSong") == 0) {
			reader.readTagOrAttributeValueString(&startupSong);
		}
		if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING) == 0) {
			// Read name
			currentTag = reader.readNextTagOrAttributeName();
			if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME) != 0) {
				display->displayPopup("Community file err");
				break;
			}
			reader.readTagOrAttributeValueString(&currentName);
			reader.exitTag();

			// Read value
			currentTag = reader.readNextTagOrAttributeName();
			if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE) != 0) {
				display->displayPopup("Community file err");
				break;
			}

			currentValue = reader.readTagOrAttributeValueInt();
			reader.exitTag();

			bool found = false;
			for (auto& setting : settings) {
				if (strcmp(setting.xmlName.data(), currentName.get()) == 0) {
					found = true;
					setting.value = currentValue;
				}
			}

			// Remember unknown settings for writing them back
			if (!found) {
				// unknownSettings.insertSetting(&currentName, currentValue);
				int32_t idx = unknownSettings.getNumElements();
				if (unknownSettings.insertAtIndex(idx) != Error::NONE) {
					return;
				}
				void* address = unknownSettings.getElementAddress(idx);
				auto* unknownSetting = new (address) UnknownSetting();
				unknownSetting->name = currentName.get();
				unknownSetting->value = currentValue;
			}
		}
		reader.exitTag(currentTag);
	}
	f_close(&smDeserializer.readFIL);
}

void RuntimeFeatureSettings::writeSettingsToFile(StorageManager& bdsm) {
	f_unlink(RUNTIME_FEATURE_SETTINGS_FILE); // May give error, but no real consequence from that.

	Error error = bdsm.createXMLFile(RUNTIME_FEATURE_SETTINGS_FILE, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}
	Serializer& writer = smSerializer;
	writer.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTINGS);
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.1.3");
	writer.writeAttribute("startupSong", currentSong->getSongFullPath().get());
	writer.writeOpeningTagEnd();

	for (auto& setting : settings) {
		writer.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTING);
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME, setting.xmlName.data(), false);
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE, setting.value, false);
		writer.writeOpeningTagEnd(false);
		writer.writeClosingTag(TAG_RUNTIME_FEATURE_SETTING, false);
	}

	// Write unknown elements
	for (uint32_t idxUnknownSetting = 0; idxUnknownSetting < unknownSettings.getNumElements(); idxUnknownSetting++) {
		UnknownSetting* unknownSetting = (UnknownSetting*)unknownSettings.getElementAddress(idxUnknownSetting);
		writer.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTING);
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME, unknownSetting->name.data(), false);
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE, unknownSetting->value, false);
		writer.writeOpeningTagEnd(false);
		writer.writeClosingTag(TAG_RUNTIME_FEATURE_SETTING, false);
	}

	writer.writeClosingTag(TAG_RUNTIME_FEATURE_SETTINGS);
	writer.closeFileAfterWriting();
}
