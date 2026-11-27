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
#include "io/file.hpp"
#include "model/song/song.h"
#include "storage/byte_channel.h"
#include "storage/existence_policy.h"
#include "storage/storage_manager.h"
#include "util/c_string.h"
#include "util/notify.h"
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#define SETTINGS_FOLDER "SETTINGS"
#define RUNTIME_FEATURE_SETTINGS_FILE "SETTINGS/CommunityFeatures.XML"
/// Where the file lived before c1.3, at the card root. Migrated into the path above on a read that
/// finds nothing there.
#define LEGACY_RUNTIME_FEATURE_SETTINGS_FILE "CommunityFeatures.XML"
#define TAG_RUNTIME_FEATURE_SETTINGS "runtimeFeatureSettings"
#define TAG_RUNTIME_FEATURE_SETTING "setting"
#define TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME "name"
#define TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE "value"

RuntimeFeatureSettings runtimeFeatureSettings{};

RuntimeFeatureSettings::RuntimeFeatureSettings() {
}

namespace {

using deluge::project::Feature;
using deluge::project::FeatureInfo;

/// Each enumerator is its feature's id, so a type names the feature the table holds at that index.
constexpr bool same_feature(RuntimeFeatureSettingType type, Feature feature) {
	return static_cast<uint32_t>(type) == std::to_underlying(feature);
}
static_assert(same_feature(DrumRandomizer, Feature::DRUM_RANDOMIZER));
static_assert(same_feature(Quantize, Feature::QUANTIZE));
static_assert(same_feature(FineTempoKnob, Feature::FINE_TEMPO_KNOB));
static_assert(same_feature(CatchNotes, Feature::CATCH_NOTES));
static_assert(same_feature(DeleteUnusedKitRows, Feature::DELETE_UNUSED_KIT_ROWS));
static_assert(same_feature(AltGoldenKnobDelayParams, Feature::ALT_GOLDEN_KNOB_DELAY_PARAMS));
static_assert(same_feature(SyncScalingAction, Feature::SYNC_SCALING_ACTION));
static_assert(same_feature(HighlightIncomingNotes, Feature::HIGHLIGHT_INCOMING_NOTES));
static_assert(same_feature(DisplayNornsLayout, Feature::DISPLAY_NORNS_LAYOUT));
static_assert(same_feature(ShiftIsSticky, Feature::SHIFT_IS_STICKY));
static_assert(same_feature(LightShiftLed, Feature::LIGHT_SHIFT_LED));
static_assert(same_feature(EnableKeyboardViewSidebarMenuExit, Feature::ENABLE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT));
static_assert(same_feature(EnableLaunchEventPlayhead, Feature::ENABLE_LAUNCH_EVENT_PLAYHEAD));
static_assert(same_feature(DisplayChordKeyboard, Feature::DISPLAY_CHORD_KEYBOARD));
static_assert(same_feature(AlternativePlaybackStartBehaviour, Feature::ALTERNATIVE_PLAYBACK_START_BEHAVIOUR));
static_assert(same_feature(EnableGridViewLoopPads, Feature::ENABLE_GRID_VIEW_LOOP_PADS));
static_assert(same_feature(AlternativeTapTempoBehaviour, Feature::ALTERNATIVE_TAP_TEMPO_BEHAVIOUR));
static_assert(same_feature(HorizontalMenus, Feature::HORIZONTAL_MENUS));
static_assert(same_feature(TrimFromStartOfAudioClip, Feature::TRIM_FROM_START_OF_AUDIO_CLIP));
static_assert(same_feature(ShowBatteryLevel, Feature::SHOW_BATTERY_LEVEL));
static_assert(same_feature(RoundedCorners, Feature::ROUNDED_CORNERS));
static_assert(same_feature(ShortcutOverlay, Feature::SHORTCUT_OVERLAY));
static_assert(MaxElement == deluge::project::kFeatureCount);

// A state enum's values are its feature's value indices.
static_assert(RuntimeFeatureStateToggle::Off == 0 && RuntimeFeatureStateToggle::On == 1);
static_assert(RuntimeFeatureStateSyncScalingAction::SyncScaling
                  == std::to_underlying(deluge::project::SyncScalingAction::SYNC_SCALING)
              && RuntimeFeatureStateSyncScalingAction::Fill
                     == std::to_underlying(deluge::project::SyncScalingAction::FILL));
static_assert(std::to_underlying(RuntimeFeatureStateShortcutOverlay::Off)
                  == std::to_underlying(deluge::project::ShortcutOverlay::OFF)
              && std::to_underlying(RuntimeFeatureStateShortcutOverlay::MenuOnly)
                     == std::to_underlying(deluge::project::ShortcutOverlay::MENU_ONLY)
              && std::to_underlying(RuntimeFeatureStateShortcutOverlay::MenuAndClip)
                     == std::to_underlying(deluge::project::ShortcutOverlay::MENU_AND_CLIP));

} // namespace

void RuntimeFeatureSettings::init() {
	for (const FeatureInfo& feature : deluge::project::kFeatures) {
		values[std::to_underlying(feature.id)] = feature.default_value;
	}

	// A card may still say `legacyMenu` or `shellMenu`, names of settings that chose between two menus. No setting
	// here has either name, so `parseSettings` keeps each as an unknown setting and writes it back untouched.
}

void RuntimeFeatureSettings::factoryReset(bool showPopup) {
	if (showPopup) {
		deluge::base::notify::notice(deluge::l10n::String::STRING_FOR_RESET_COMMUNITY_FEATURES);
	}

	// May give an error (e.g. no such file), but there is no real consequence from that.
	(void)deluge::io::unlink(RUNTIME_FEATURE_SETTINGS_FILE);
	unknownSettings.clear();
	startupSong.clear();
	init();
	readSettingsFromFile();
}

bool RuntimeFeatureSettings::readSettingsFromFile(deluge::storage::bytes::DoneFn onDone) {
	// The channel first: it does the read in Rust, so nothing blocks anywhere. A false means the channel is
	// not the place for this read here (boot, or a build without it) -- a busy channel queues it
	// instead -- and the inline arm below does the same work where blocking is legal.
	if (deluge::storage::bytes::submitRead(RUNTIME_FEATURE_SETTINGS_FILE, LEGACY_RUNTIME_FEATURE_SETTINGS_FILE,
	                                       &RuntimeFeatureSettings::parseSettingsBytes,
	                                       /*onAbsent=*/nullptr, this, onDone)) {
		return true;
	}
	// No blocking arm: boot does not call this, so a refusal means the channel is switched off or
	// the path does not fit -- and either way the settings in memory stand, which is exactly what a
	// refusal has always meant here. The settings prelude sees the refusal as a read it must not
	// wait for.
	return false;
}

void RuntimeFeatureSettings::parseSettingsBytes(void* ctx, char* bytes, uint32_t len) {
	// `activeDeserializer` goes back on EVERY path, the failed open included: `openDelugeBuffer`
	// points it at the memory reader before it knows whether the document opened, and code downstream
	// WRITES through that global without opening anything of its own. Left pointing here it would
	// write into `bytes`, which is the channel's own staging buffer. Copied from
	// `deluge::storage::decodeSongPreview()`, which says it at length.
	FileDeserializer* previous = activeDeserializer;
	if (StorageManager::openDelugeBuffer(std::span{reinterpret_cast<uint8_t*>(bytes), len}, false,
	                                     TAG_RUNTIME_FEATURE_SETTINGS)
	    == Error::NONE) {
		static_cast<RuntimeFeatureSettings*>(ctx)->parseSettings(*activeDeserializer);
	}
	activeDeserializer = previous;
}

void RuntimeFeatureSettings::readSettingsInline() {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Settings);
	// CommunityFeatures.XML
	switch (deluge::storage::bootstrap_action(StorageManager::fileExists(RUNTIME_FEATURE_SETTINGS_FILE))) {
	case deluge::storage::Bootstrap::Load:
		break; // fall through to parsing, exactly as today

	case deluge::storage::Bootstrap::WriteDefaults: {
		// Genuinely absent: since we changed the file path for the CommunityFeatures.XML in c1.3,
		// it's possible that a CommunityFeatures file may exist in the root of the SD card; if so,
		// let's move it to the new SETTINGS folder (but first make sure folder exists).
		auto result = deluge::io::mkdir(SETTINGS_FOLDER);
		if (result.has_value() || result.error() == deluge::io::Status::EXISTS) {
			auto renamed = deluge::io::rename(LEGACY_RUNTIME_FEATURE_SETTINGS_FILE, RUNTIME_FEATURE_SETTINGS_FILE);
			if (renamed.has_value()) {
				// this means we moved it; confirm the migration actually landed before trusting
				// the new path.
				if (deluge::storage::bootstrap_action(StorageManager::fileExists(RUNTIME_FEATURE_SETTINGS_FILE))
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

	Error error =
	    StorageManager::openDocumentSynchronously(RUNTIME_FEATURE_SETTINGS_FILE, TAG_RUNTIME_FEATURE_SETTINGS);
	if (error != Error::NONE) {
		return;
	}
	parseSettings(*activeDeserializer);
	activeDeserializer->closeWriter();
	StorageManager::releaseDocumentStaging();
}

void RuntimeFeatureSettings::parseSettings(Deserializer& reader) {
	std::string currentName;
	int32_t currentValue = 0;
	char const* currentTag = nullptr;

	while (*(currentTag = reader.readNextTagOrAttributeName())) {

		if (strcmp(currentTag, "startupSong") == 0) {
			reader.readTagOrAttributeValueString(startupSong);
		}
		if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING) == 0) {
			// Read name
			currentTag = reader.readNextTagOrAttributeName();
			if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME) != 0) {
				deluge::base::notify::notice("Community file err");
				break;
			}
			reader.readTagOrAttributeValueString(currentName);
			reader.exitTag();

			// Read value
			currentTag = reader.readNextTagOrAttributeName();
			if (strcmp(currentTag, TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE) != 0) {
				deluge::base::notify::notice("Community file err");
				break;
			}

			currentValue = reader.readTagOrAttributeValueInt();
			reader.exitTag();

			// A feature takes its value; an unknown setting is remembered for writing it back.
			if (const FeatureInfo* feature = deluge::project::find_feature(currentName)) {
				values[std::to_underlying(feature->id)] = currentValue;
			}
			else {
				try {
					unknownSettings.push_back(UnknownSetting{currentName, static_cast<uint32_t>(currentValue)});
				} catch (deluge::exception&) {
					return;
				}
			}
		}
		reader.exitTag(currentTag);
	}
}

void RuntimeFeatureSettings::writeSettingsToFile() {
	if (deluge::storage::bytes::usingByteChannel()) {
		// Serialise into memory -- no card contact at all -- and hand the bytes over. The serialiser
		// dies at the end of this scope, which is safe because `submitWrite` copies.
		XMLSerializer writer;
		writer.setMemoryBased();
		writer.write(StorageManager::kXmlDocumentHead); // the inline arm gets this from createXMLFile
		serialiseSettings(writer);
		if (deluge::storage::bytes::submitWrite(RUNTIME_FEATURE_SETTINGS_FILE, writer.getBufferPtr(),
		                                        static_cast<uint32_t>(writer.bytesWritten()))) {
			return;
		}
		// Not taken: too large for the channel (a busy channel queues instead). The body below runs only where
		// blocking is legal; after boot it is refused and nothing is written.
	}
	(void)deluge::storage::bytes::runDuringBoot(
	    [](void* ctx) { static_cast<RuntimeFeatureSettings*>(ctx)->writeSettingsInline(); }, this);
}

void RuntimeFeatureSettings::writeSettingsInline() {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Settings);
	deluge::io::unlink(RUNTIME_FEATURE_SETTINGS_FILE); // May give error, but no real consequence from that.

	Error error = StorageManager::createXMLFile(RUNTIME_FEATURE_SETTINGS_FILE, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}
	serialiseSettings(GetSerializer());
	GetSerializer().closeFileAfterWriting();
}

void RuntimeFeatureSettings::serialiseSettings(Serializer& writer) {
	writer.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTINGS);
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.1.3");
	writer.writeAttribute("startupSong", currentSong->getSongFullPath().c_str());
	writer.writeOpeningTagEnd();

	for (const FeatureInfo& feature : deluge::project::kFeatures) {
		writer.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTING);
		// Each name is a string literal, which `features.cpp` asserts, so its text ends in a NUL.
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME, feature.path.data(), false);
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE, values[std::to_underlying(feature.id)], false);
		writer.writeOpeningTagEnd(false);
		writer.writeClosingTag(TAG_RUNTIME_FEATURE_SETTING, false);
	}

	// Write unknown elements
	for (UnknownSetting& unknownSetting : unknownSettings) {
		writer.writeOpeningTagBeginning(TAG_RUNTIME_FEATURE_SETTING);
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_NAME, unknownSetting.name.data(), false);
		writer.writeAttribute(TAG_RUNTIME_FEATURE_SETTING_ATTR_VALUE, unknownSetting.value, false);
		writer.writeOpeningTagEnd(false);
		writer.writeClosingTag(TAG_RUNTIME_FEATURE_SETTING, false);
	}

	writer.writeClosingTag(TAG_RUNTIME_FEATURE_SETTINGS);
}
