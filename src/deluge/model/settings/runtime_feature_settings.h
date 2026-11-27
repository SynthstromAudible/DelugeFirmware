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

#pragma once

#include "gui/l10n/strings.h"
#include "project/features.h"
#include "storage/byte_channel.h"
#include "util/containers.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// State declarations
enum RuntimeFeatureStateToggle : uint32_t { Off = 0, On = 1 };

// Declare additional enums for specific multi state settings (e.g. like RuntimeFeatureStateTrackLaunchStyle)
enum RuntimeFeatureStateSyncScalingAction : uint32_t { SyncScaling = 0, Fill = 1 };

// Keep the former On value as MenuOnly to preserve existing settings.
enum class RuntimeFeatureStateShortcutOverlay : uint32_t { Off = 0, MenuOnly = 1, MenuAndClip = 2 };

/// Every feature, by its `deluge::project::Feature` value: the feature table (`project/features.h`) declares
/// them, and each enumerator here is its feature's id.
enum RuntimeFeatureSettingType : uint32_t {
	DrumRandomizer,
	Quantize,
	FineTempoKnob,
	CatchNotes,
	DeleteUnusedKitRows,
	AltGoldenKnobDelayParams,
	SyncScalingAction,
	HighlightIncomingNotes,
	DisplayNornsLayout,
	ShiftIsSticky,
	LightShiftLed,
	EnableKeyboardViewSidebarMenuExit,
	EnableLaunchEventPlayhead,
	DisplayChordKeyboard,
	AlternativePlaybackStartBehaviour,
	EnableGridViewLoopPads,
	AlternativeTapTempoBehaviour,
	HorizontalMenus,
	TrimFromStartOfAudioClip,
	ShowBatteryLevel,
	RoundedCorners,
	ShortcutOverlay,
	MaxElement // Keep as boundary
};

/// Setting read from the settings file that no current setting matches, kept for writing back
struct UnknownSetting {
	std::string name{};
	uint32_t value{};
};

class Serializer;
class Deserializer;

/// Encapsulating class
class RuntimeFeatureSettings {
public:
	RuntimeFeatureSettings();

	// Traded type safety for option values for code simplicity and size, use enum from above to compare
	inline uint32_t get(RuntimeFeatureSettingType type) { return values[type]; };
	inline bool isOn(RuntimeFeatureSettingType type) { return get(type) == RuntimeFeatureStateToggle::On; }

	/**
	 * Set a runtime feature setting.
	 *
	 * Make sure when you use this the settings are eventually written back to the SDCard!
	 */
	inline void set(RuntimeFeatureSettingType type, uint32_t value) { values[type] = value; }

	inline const char* getStartupSong() { return startupSong.c_str(); }
	void init();
	void factoryReset(bool showPopup = true);
	/// @brief Read the settings, preferring the channel.
	/// @param onDone Told how the channel operation ended, when the channel took it.
	/// @return Whether the CHANNEL accepted the read -- i.e. whether @p onDone will be called.
	bool readSettingsFromFile(deluge::storage::bytes::DoneFn onDone = nullptr);
	/// @brief readSettingsFromFile's body, run where blocking on the card is legal.
	void readSettingsInline();
	void writeSettingsToFile();
	/// @brief writeSettingsToFile's body, run where blocking on the card is legal.
	void writeSettingsInline();
	/// @brief Walk an already-opened document into the in-memory settings.
	///
	/// Shared by both arms: the inline arm's reader is a file, the channel arm's is the bytes the
	/// channel delivered, and neither difference reaches here.
	void parseSettings(Deserializer& reader);
	/// @brief Write the settings into an already-opened serialiser, file-backed or memory-backed.
	void serialiseSettings(Serializer& writer);
	/// @brief `deluge::storage::bytes::ParseFn` for @ref parseSettings, over channel-delivered
	///        bytes.
	static void parseSettingsBytes(void* ctx, char* bytes, uint32_t len);

protected:
	/// Each feature's value, indexed by its `deluge::project::Feature`; `kFeatures` names, words and defaults them.
	std::array<uint32_t, deluge::project::kFeatureCount> values{};
	std::string startupSong{};

private:
	deluge::vector<UnknownSetting> unknownSettings{};

public:
	/// The settings-channel gate's view into the unknown settings; see `settings_channel_scenario.cpp`.
	friend struct SettingsChannelProbe;
};

/// Static instance for external access
extern RuntimeFeatureSettings runtimeFeatureSettings;
