#include "project/device_settings.h"
#include "definitions_cxx.hpp"
#include "gui/colour/pad_colour.h"
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h"
#include "hid/buttons.h"
#include "hid/display/screensaver.h"
#include "hid/led/pad_leds.h"
#include "instruments/classic/cables.h"
#include "io/debug/log.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "io/usb/usb_audio.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "music/preset_scales.h"
#include "playback/playback_handler.h"
#include "plugin/types/display_mapping.h"
#include "plugin/types/setting.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/metronome/metronome.h"
#include "processing/stem_export/stem_export.h"
#include "project/features.h"
#include "serialization/tree.h"
#include "storage/flash_storage.h"
#include "util/value_scaling.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace deluge::gui::shell_host {
namespace {

using deluge::ui::scenes::bind::DeviceWrite;
using plugin::SettingInfo;
using project::device_path;
using project::Feature;
using project::FeatureInfo;
using project::FlashCursor;
using project::ShortcutsVersion;
using serialization::Value;

// A CHOICE row's index is the legacy value for these.
static_assert(std::to_underlying(FlashCursor::FAST) == FLASH_CURSOR_FAST);
static_assert(std::to_underlying(FlashCursor::OFF) == FLASH_CURSOR_OFF);
static_assert(std::to_underlying(FlashCursor::SLOW) == FLASH_CURSOR_SLOW);
static_assert(std::to_underlying(ShortcutsVersion::V1) == FlashStorage::kShortcutsVersion1);
static_assert(std::to_underlying(ShortcutsVersion::V3) == FlashStorage::kShortcutsVersion3);
static_assert(std::to_underlying(instruments::classic::Polarity::UNIPOLAR) == 0
              && std::to_underlying(instruments::classic::Polarity::BIPOLAR) == 1);
static_assert(std::to_underlying(StartupSongMode::LASTSAVED) == 3 && kNumStartupSongMode == 4);
static_assert(std::to_underlying(SampleRepeatMode::STRETCH) == 3);
static_assert(std::to_underlying(ThresholdRecordingMode::OFF) == 0 && kLastThresholdRecordingMode == 3);
static_assert(std::to_underlying(OutputType::SYNTH) == 0 && std::to_underlying(OutputType::AUDIO) == 4);
static_assert(std::to_underlying(project::FavouritesLayout::FAVOURITES) == FavouritesDefaultLayoutFavorites
              && std::to_underlying(project::FavouritesLayout::FAVOURITES_AND_BANKS)
                     == FavouritesDefaultLayoutFavoritesAndBanks
              && std::to_underlying(project::FavouritesLayout::OFF) == FavouritesDefaultLayoutOff);
static_assert(std::to_underlying(MenuHighlighting::NO_INVERSION) == 2);
static_assert(project::kDimmestPadLevel == kMinLedBrightness && project::kBrightestPadLevel == kMaxLedBrightness);
static_assert(SessionLayoutTypeGrid == 1 && GridDefaultActiveModeBlue == 2 && KeyboardLayoutTypePiano == 2);
static_assert(std::to_underlying(KeyboardLayout::QWERTZ) == 2);
static_assert(std::to_underlying(project::ScreensaverMode::OFF) == std::to_underlying(::ScreensaverMode::OFF));
static_assert(std::to_underlying(project::ScreensaverMode::BLANK) == std::to_underlying(::ScreensaverMode::BLANK));
static_assert(std::to_underlying(project::ScreensaverMode::STARSCAPE)
              == std::to_underlying(::ScreensaverMode::STARSCAPE));
static_assert(std::to_underlying(project::ScreensaverMode::DELUGE) == std::to_underlying(::ScreensaverMode::DELUGE));
static_assert(PREVIEW_OFF == 0 && PREVIEW_ONLY_WHILE_NOT_PLAYING == 1 && PREVIEW_ON == 2);
static_assert(deluge::gui::RED == 0 && deluge::gui::PINK == 8 && deluge::gui::kNumPadColours == 9);
static_assert(std::to_underlying(InputMonitoringMode::OFF) == 2 && kNumInputMonitoringModes == 3);
static_assert(std::to_underlying(usb_audio::OutputSource::LINE_IN) == 1 && usb_audio::kNumOutputSources == 2);
static_assert(project::kCvOutputCount == NUM_PHYSICAL_CV_CHANNELS && project::kGateOutputCount == NUM_GATE_CHANNELS);
static_assert(project::kRunGateOutput == WHICH_GATE_OUTPUT_IS_RUN
              && project::kClockGateOutput == WHICH_GATE_OUTPUT_IS_CLOCK);
static_assert(std::to_underlying(MIDIFollowFeedbackChannelType::TrackAndC) == 7
              && kLastValidMIDIFollowFeedbackChannelType == MIDIFollowFeedbackChannelType::TrackAndC);
// The automation feedback's choices are the legacy mode's valid values; its INVALID sentinel is no choice.
static_assert(std::to_underlying(project::FeedbackAutomation::DISABLED)
                  == std::to_underlying(MIDIFollowFeedbackAutomationMode::DISABLED)
              && std::to_underlying(project::FeedbackAutomation::LOW)
                     == std::to_underlying(MIDIFollowFeedbackAutomationMode::LOW)
              && std::to_underlying(project::FeedbackAutomation::MEDIUM)
                     == std::to_underlying(MIDIFollowFeedbackAutomationMode::MEDIUM)
              && std::to_underlying(project::FeedbackAutomation::HIGH)
                     == std::to_underlying(MIDIFollowFeedbackAutomationMode::HIGH)
              && kNumMIDIFollowFeedbackAutomationModes == 4);
static_assert(std::to_underlying(MIDITransposeControlMethod::CHROMATIC) == 1);
static_assert(std::to_underlying(MIDITakeoverMode::RELATIVE) == 3 && kNumMIDITakeoverModes == 4);
static_assert(std::to_underlying(GateType::V_TRIG) == 0 && std::to_underlying(GateType::S_TRIG) == 1
              && std::to_underlying(GateType::SPECIAL) == 2);

/// @brief A BOOL row's file form.
Value flag(bool on) {
	return Value::makeNumber(on ? 1 : 0);
}

/// @brief A CHOICE row's file form for choice @p index: its word, or, for an index the row has no choice at (a value
///        the device's flash held that no menu offers), the index itself as a number, which the port's read clamps.
Value choice(const SettingInfo& row, int64_t index) {
	return index >= 0 && static_cast<size_t>(index) < row.choices.size()
	           ? Value::makeScalar(std::string{row.choices[static_cast<size_t>(index)]})
	           : Value::makeNumber(index);
}

/// @brief An INTEGER row's file form: the value as the device holds it, which the port reads unclamped, as the menu's
///        number widget shows a value past its range until a turn clamps it.
Value number(int64_t value) {
	return Value::makeNumber(value);
}

/// @brief A BOOL row the device holds as one `bool`, whose store does nothing more. The audio tier and the clock read
///        some of them as they are; a store stays the one plain store the menu's item made.
struct FlagHome {
	project::DevicePath path;
	bool* value{nullptr};
};
/// Every such row: the flash settings', the clocks', USB audio's and MIDI's, and the stem export's options, which the
/// export keeps and nothing saves.
constexpr std::array<FlagHome, 28> kFlagHomes{{
    {.path = device_path::kAccessibilityShortcuts, .value = &FlashStorage::accessibilityShortcuts},
    {.path = device_path::kGridAllowGreenSelection, .value = &FlashStorage::gridAllowGreenSelection},
    {.path = device_path::kGridEmptyPadsUnarm, .value = &FlashStorage::gridEmptyPadsUnarm},
    {.path = device_path::kGridEmptyPadsCreateRec, .value = &FlashStorage::gridEmptyPadsCreateRec},
    {.path = device_path::kUseSharps, .value = &FlashStorage::defaultUseSharps},
    {.path = device_path::kCpuIndicator, .value = &FlashStorage::highCPUUsageIndicator},
    {.path = device_path::kRecordMargins, .value = &FlashStorage::audioClipRecordMargins},
    {.path = device_path::kUseLastClipType, .value = &FlashStorage::defaultUseLastClipType},
    {.path = device_path::kAutomationInterpolate, .value = &FlashStorage::automationInterpolate},
    {.path = device_path::kAutomationClear, .value = &FlashStorage::automationClear},
    {.path = device_path::kAutomationShift, .value = &FlashStorage::automationShift},
    {.path = device_path::kAutomationNudgeNote, .value = &FlashStorage::automationNudgeNote},
    {.path = device_path::kAutomationNoAuditionShortcuts,
     .value = &FlashStorage::automationDisableAuditionPadShortcuts},
    {.path = device_path::kTriggerInAutoStart, .value = &playbackHandler.analogClockInputAutoStart},
    {.path = device_path::kTempoMagnitudeMatching, .value = &playbackHandler.tempoMagnitudeMatchingEnabled},
    {.path = device_path::kUsbDirectMonitoring, .value = &usb_audio::directMonitoring},
    {.path = device_path::kFollowFeedbackFilter, .value = &midiEngine.midiFollowFeedbackFilter},
    {.path = device_path::kFollowDisplayParam, .value = &midiEngine.midiFollowDisplayParam},
    {.path = device_path::kMidiSelectKitRow, .value = &midiEngine.midiSelectKitRow},
    {.path = device_path::kMidiThru, .value = &midiEngine.midiThru},
    {.path = device_path::kDifferentiateInputs, .value = &MIDIDeviceManager::differentiatingInputsByDevice},
    {.path = device_path::kStemNormalize, .value = &stemExport.allowNormalization},
    {.path = device_path::kStemNormalizeDrums, .value = &stemExport.allowNormalizationForDrums},
    {.path = device_path::kStemToSilence, .value = &stemExport.exportToSilence},
    {.path = device_path::kStemSongFx, .value = &stemExport.includeSongFX},
    {.path = device_path::kStemKitFx, .value = &stemExport.includeKitFX},
    {.path = device_path::kStemOffline, .value = &stemExport.renderOffline},
    {.path = device_path::kStemMixdown, .value = &stemExport.exportMixdown},
}};

/// @brief The keyboard view's sidebar rows: the flash settings hold whether a sidebar glides, and the row whether it
///        acts only while held, the other sense (`InvertedToggleBool` in `origin/main:src/deluge/gui/ui/menus.cpp`).
///        A store does nothing more.
constexpr std::array<FlagHome, 2> kGlideHomes{{
    {.path = device_path::kVelocityMomentary, .value = &FlashStorage::keyboardFunctionsVelocityGlide},
    {.path = device_path::kModwheelMomentary, .value = &FlashStorage::keyboardFunctionsModwheelGlide},
}};

/// @brief A pad colour's row: which of the flash settings' colour slots it is.
struct ColourHome {
	project::DevicePath path;
	FlashStorage::PadColourSlot slot{};
};
/// The six, in the colours page's order.
constexpr std::array<ColourHome, FlashStorage::NUM_PAD_COLOUR_SLOTS> kColourHomes{{
    {.path = device_path::kPadColourActive, .slot = FlashStorage::PAD_COLOUR_ACTIVE},
    {.path = device_path::kPadColourStopped, .slot = FlashStorage::PAD_COLOUR_STOPPED},
    {.path = device_path::kPadColourMuted, .slot = FlashStorage::PAD_COLOUR_MUTED},
    {.path = device_path::kPadColourSoloed, .slot = FlashStorage::PAD_COLOUR_SOLOED},
    {.path = device_path::kPadColourFill, .slot = FlashStorage::PAD_COLOUR_FILL},
    {.path = device_path::kPadColourOnce, .slot = FlashStorage::PAD_COLOUR_ONCE},
}};

/// @brief An INTEGER row the device holds as one byte, whose store does nothing more.
struct ByteHome {
	project::DevicePath path;
	uint8_t* value{nullptr};
};
/// Every such row.
constexpr std::array<ByteHome, 6> kByteHomes{{
    {.path = device_path::kSwingInterval, .value = &FlashStorage::defaultSwingInterval},
    {.path = device_path::kBendRange, .value = &FlashStorage::defaultBendRange[BEND_RANGE_MAIN]},
    {.path = device_path::kRecordQuantize, .value = &FlashStorage::recordQuantizeLevel},
    {.path = device_path::kCountIn, .value = &playbackHandler.countInBars},
    {.path = device_path::kGateMinOffTime, .value = &cvEngine.minGateOffTime},
    {.path = device_path::kFollowKitRootNote, .value = &midiEngine.midiFollowKitRootNote},
}};

/// @brief A default range's end: which of the flash settings' ranges, and which end of it.
struct EndHome {
	project::DevicePath path;
	FlashStorage::DefaultRangeSlot slot{};
	bool upper{false};
};
/// The six ends.
constexpr std::array<EndHome, 6> kEndHomes{{
    {.path = device_path::kTempoLower, .slot = FlashStorage::DEFAULT_RANGE_TEMPO, .upper = false},
    {.path = device_path::kTempoUpper, .slot = FlashStorage::DEFAULT_RANGE_TEMPO, .upper = true},
    {.path = device_path::kSwingLower, .slot = FlashStorage::DEFAULT_RANGE_SWING_AMOUNT, .upper = false},
    {.path = device_path::kSwingUpper, .slot = FlashStorage::DEFAULT_RANGE_SWING_AMOUNT, .upper = true},
    {.path = device_path::kKeyLower, .slot = FlashStorage::DEFAULT_RANGE_KEY, .upper = false},
    {.path = device_path::kKeyUpper, .slot = FlashStorage::DEFAULT_RANGE_KEY, .upper = true},
}};

/// @brief Where the home of the row at @p path is among @p homes, or null.
template <typename Home, size_t N>
const Home* home_of(const std::array<Home, N>& homes, std::string_view path) {
	for (const Home& home : homes) {
		if (home.path == path) {
			return &home;
		}
	}
	return nullptr;
}

/// @brief The end @p home names, where the flash settings keep it.
int32_t& end_value(const EndHome& home) {
	FlashStorage::DefaultRange& range = FlashStorage::defaultRanges[home.slot];
	return home.upper ? range.upper : range.lower;
}

/// @brief The `defaults/scale` row's choice for the flash setting's scale code: the scale's index with the user scale's
///        slot removed, as `defaults::DefaultScale::readCurrentValue`
///        (`origin/main:src/deluge/gui/menu_item/defaults/scale.h`) reads it.
int64_t scale_choice(uint8_t code) {
	const music::Scale scale = music::flashStorageCodeToScale(code);
	return scale >= music::USER_SCALE ? int64_t{scale} - 1 : int64_t{scale};
}

/// @brief The flash setting's scale code for the `defaults/scale` row's choice @p index, as `writeCurrentValue` there
///        writes it.
uint8_t scale_code(int32_t index) {
	return music::scaleToFlashStorageCode(static_cast<music::Scale>(index >= music::USER_SCALE ? index + 1 : index));
}

/// @brief The preset scale a `defaults/scales/disabled/<i>` path names, or nullopt.
std::optional<size_t> disabled_scale_of(std::string_view path) {
	if (!path.starts_with(device_path::kDisabledScalePrefix)) {
		return std::nullopt;
	}
	const std::optional<int64_t> index = plugin::whole_number(path.substr(device_path::kDisabledScalePrefix.size()));
	if (!index || *index < 0 || *index >= music::NUM_PRESET_SCALES) {
		return std::nullopt;
	}
	return static_cast<size_t>(*index);
}

/// @brief The output a per-output row at @p path is of: the index in @p pattern's `*`, below @p count.
std::optional<uint8_t> output_at(std::string_view pattern, std::string_view path, size_t count) {
	const std::optional<size_t> output = plugin::index_in(pattern, path);
	return output && *output < count ? std::optional{static_cast<uint8_t>(*output)} : std::nullopt;
}

/// @brief The legacy settings' name for feature @p f, whose enumerators are the features' ids.
RuntimeFeatureSettingType legacy_type(Feature f) {
	return static_cast<RuntimeFeatureSettingType>(std::to_underlying(f));
}

/// @brief Stores @p stored as feature @p info's value, with what the feature's own menu item does as it writes.
void store_feature(const FeatureInfo& info, int32_t stored) {
	const auto value = static_cast<uint32_t>(stored);
	runtimeFeatureSettings.set(legacy_type(info.id), value);
	switch (info.id) {
	case Feature::SHIFT_IS_STICKY:
		// `ShiftIsSticky::writeCurrentValue`
		// (`origin/main:src/deluge/gui/menu_item/runtime_feature/shift_is_sticky.cpp`): on, the shift LED lights too.
		// The guest's shift machine takes the setting (`opened()` seeds it only on the way in), and off, legacy's latch
		// goes with the guest's.
		if (value != 0) {
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, RuntimeFeatureStateToggle::On);
		}
		deluge::gui::ui::shellRuntime().shell().lowering().set_shift_is_sticky(value != 0);
		if (value == 0) {
			Buttons::clearShiftSticky();
		}
		break;
	case Feature::ROUNDED_CORNERS:
		// `RoundedCornersSettingToggle::writeCurrentValue`: main's canvas flag and the guest's panels restyle.
		gui::ui::pushShellRoundedCorners();
		break;
	default:
		break;
	}
}

// Each group's load answers the file-form value of a row it holds, and nullopt for a row of another group; each
// group's store stores a row it holds and answers what the write did, and nullopt for a row of another group.

/// @brief The rows kept in one of the home tables above.
std::optional<Value> load_homed(const SettingInfo& row) {
	const std::string_view path = row.path;
	if (const FlagHome* home = home_of(kFlagHomes, path)) {
		return flag(*home->value);
	}
	if (const FlagHome* home = home_of(kGlideHomes, path)) {
		return flag(!*home->value);
	}
	if (const ColourHome* home = home_of(kColourHomes, path)) {
		return choice(row, FlashStorage::padColours[home->slot]);
	}
	if (const ByteHome* home = home_of(kByteHomes, path)) {
		return number(*home->value);
	}
	if (const EndHome* home = home_of(kEndHomes, path)) {
		return number(end_value(*home));
	}
	return std::nullopt;
}

/// @brief The rows kept in one of the home tables above.
std::optional<DeviceWrite> store_homed(std::string_view path, int32_t stored) {
	if (const FlagHome* home = home_of(kFlagHomes, path)) {
		*home->value = stored != 0;
		return DeviceWrite::STORED;
	}
	if (const FlagHome* home = home_of(kGlideHomes, path)) {
		*home->value = stored == 0;
		return DeviceWrite::STORED;
	}
	if (const ColourHome* home = home_of(kColourHomes, path)) {
		// `Colour::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/colour.h`): every pad re-renders,
		// whatever the UI.
		FlashStorage::padColours[home->slot] = static_cast<uint8_t>(stored);
		renderingNeededRegardlessOfUI();
		return DeviceWrite::STORED;
	}
	if (const ByteHome* home = home_of(kByteHomes, path)) {
		*home->value = static_cast<uint8_t>(stored);
		return DeviceWrite::STORED;
	}
	if (const EndHome* home = home_of(kEndHomes, path)) {
		end_value(*home) = stored;
		return DeviceWrite::STORED;
	}
	return std::nullopt;
}

/// @brief The defaults page's rows the tables do not hold, and the default disabled preset scales.
std::optional<Value> load_defaults(const SettingInfo& row) {
	const std::string_view path = row.path;
	if (path == device_path::kMagnitude) {
		return number(FlashStorage::defaultMagnitude);
	}
	if (path == device_path::kScale) {
		return choice(row, scale_choice(FlashStorage::defaultScale));
	}
	if (path == device_path::kVelocity) {
		return number(FlashStorage::defaultVelocity);
	}
	if (path == device_path::kMetronomeVolume) {
		return number(FlashStorage::defaultMetronomeVolume);
	}
	if (path == device_path::kHoldTime) {
		return number(FlashStorage::defaultHoldTime);
	}
	if (path == device_path::kStartupSong) {
		return choice(row, std::to_underlying(FlashStorage::defaultStartupSongMode));
	}
	if (path == device_path::kSliceMode) {
		return choice(row, std::to_underlying(FlashStorage::defaultSliceMode));
	}
	if (path == device_path::kThresholdRecording) {
		return choice(row, std::to_underlying(FlashStorage::defaultThresholdRecordingMode));
	}
	if (path == device_path::kNewClipType) {
		return choice(row, std::to_underlying(FlashStorage::defaultNewClipType));
	}
	if (path == device_path::kPatchCablePolarity) {
		return choice(row, std::to_underlying(FlashStorage::defaultPatchCablePolarity));
	}
	if (const std::optional<size_t> scale = disabled_scale_of(path)) {
		return flag(FlashStorage::defaultDisabledPresetScales[*scale]);
	}
	return std::nullopt;
}

/// @brief The defaults page's rows the tables do not hold, and the default disabled preset scales.
std::optional<DeviceWrite> store_defaults(std::string_view path, int32_t stored) {
	if (path == device_path::kMagnitude) {
		// `defaults::Magnitude::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/magnitude.h`):
		// the swing interval moves by the same delta, so its note length survives the resolution change.
		const int32_t old_magnitude = FlashStorage::defaultMagnitude;
		FlashStorage::defaultMagnitude = static_cast<int8_t>(stored);
		const int32_t delta = FlashStorage::defaultMagnitude - old_magnitude;
		FlashStorage::defaultSwingInterval =
		    static_cast<uint8_t>(plugin::clamp_swing_interval(FlashStorage::defaultSwingInterval - delta));
		return DeviceWrite::STORED;
	}
	if (path == device_path::kScale) {
		FlashStorage::defaultScale = scale_code(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kVelocity) {
		// `defaults::Velocity::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/velocity.h`): the
		// open song's instruments take the new default too.
		FlashStorage::defaultVelocity = static_cast<uint8_t>(stored);
		if (currentSong != nullptr) {
			currentSong->setDefaultVelocityForAllInstruments(FlashStorage::defaultVelocity);
		}
		return DeviceWrite::STORED;
	}
	if (path == device_path::kMetronomeVolume) {
		// `defaults::MetronomeVolume::writeCurrentValue`
		// (`origin/main:src/deluge/gui/menu_item/defaults/metronome_volume.h`): the metronome sounds at it at once.
		FlashStorage::defaultMetronomeVolume = static_cast<uint8_t>(stored);
		AudioEngine::metronome.setVolume(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kHoldTime) {
		// `defaults::HoldTime::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/hold_time.h`):
		// the press timer reads the hold in samples.
		FlashStorage::defaultHoldTime = static_cast<uint8_t>(stored);
		FlashStorage::holdTime = (FlashStorage::defaultHoldTime * kSampleRate) / 20;
		return DeviceWrite::STORED;
	}
	if (path == device_path::kStartupSong) {
		FlashStorage::defaultStartupSongMode = static_cast<StartupSongMode>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kSliceMode) {
		FlashStorage::defaultSliceMode = static_cast<SampleRepeatMode>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kThresholdRecording) {
		// `record::ThresholdMode::writeCurrentValue`
		// (`origin/main:src/deluge/gui/menu_item/record/threshold_mode.h`), the default kind: the flash setting,
		// then the open song's own mode too.
		FlashStorage::defaultThresholdRecordingMode = static_cast<ThresholdRecordingMode>(stored);
		if (currentSong != nullptr) {
			currentSong->thresholdRecordingMode = static_cast<ThresholdRecordingMode>(stored);
		}
		return DeviceWrite::STORED;
	}
	if (path == device_path::kNewClipType) {
		FlashStorage::defaultNewClipType = static_cast<OutputType>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kPatchCablePolarity) {
		FlashStorage::defaultPatchCablePolarity = static_cast<instruments::classic::Polarity>(stored);
		return DeviceWrite::STORED;
	}
	if (const std::optional<size_t> scale = disabled_scale_of(path)) {
		FlashStorage::defaultDisabledPresetScales[*scale] = stored != 0;
		return DeviceWrite::STORED;
	}
	return std::nullopt;
}

/// @brief The pads' and the screen's rows the tables do not hold, and the sample browser's preview.
std::optional<Value> load_pads_and_screen(const SettingInfo& row) {
	const std::string_view path = row.path;
	if (path == device_path::kShortcutsVersion) {
		// Any byte but V1's selects the current layout, as the shortcut lookups read it.
		return choice(row, std::to_underlying(FlashStorage::shortcutsVersion == FlashStorage::kShortcutsVersion1
		                                          ? ShortcutsVersion::V1
		                                          : ShortcutsVersion::V3));
	}
	if (path == device_path::kPlayCursor) {
		return choice(row, PadLEDs::flashCursor);
	}
	if (path == device_path::kTextKeyboardLayout) {
		return choice(row, std::to_underlying(FlashStorage::keyboardLayout));
	}
	if (path == device_path::kMenuHighlighting) {
		return choice(row, std::to_underlying(FlashStorage::accessibilityMenuHighlighting));
	}
	if (path == device_path::kSessionLayout) {
		return choice(row, FlashStorage::defaultSessionLayout);
	}
	if (path == device_path::kGridActiveMode) {
		return choice(row, FlashStorage::defaultGridActiveMode);
	}
	if (path == device_path::kKeyboardLayout) {
		// The flash may hold any of the device's seven layouts; one past the menu's three reads clamped to the
		// last, as the menu's list shows it.
		return choice(row, FlashStorage::defaultKeyboardLayout);
	}
	if (path == device_path::kFavouritesLayout) {
		return choice(row, FlashStorage::defaultFavouritesLayout);
	}
	if (path == device_path::kPadBrightness) {
		return number(FlashStorage::defaultPadBrightness);
	}
	if (path == device_path::kScreensaverMode) {
		return choice(row, std::to_underlying(FlashStorage::screensaverMode));
	}
	if (path == device_path::kScreensaverTimeout) {
		return number(FlashStorage::screensaverTimeoutMinutes);
	}
	if (path == device_path::kSamplePreview) {
		return choice(row, FlashStorage::sampleBrowserPreviewMode);
	}
	return std::nullopt;
}

/// @brief The pads' and the screen's rows the tables do not hold, and the sample browser's preview.
std::optional<DeviceWrite> store_pads_and_screen(std::string_view path, int32_t stored) {
	if (path == device_path::kShortcutsVersion) {
		FlashStorage::shortcutsVersion = static_cast<uint8_t>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kPlayCursor) {
		// Leaving the slow flash clears its tick squares, judged on the value being replaced while it is still the
		// current one (`flash::Status::writeCurrentValue` in
		// `origin/main:src/deluge/gui/menu_item/flash/status.h`).
		if (PadLEDs::flashCursor == FLASH_CURSOR_SLOW) {
			PadLEDs::clearTickSquares(true);
		}
		PadLEDs::flashCursor = static_cast<uint8_t>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kTextKeyboardLayout) {
		FlashStorage::keyboardLayout = static_cast<KeyboardLayout>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kMenuHighlighting) {
		// `AccessibilityMenuHighlighting::writeCurrentValue`
		// (`origin/main:src/deluge/gui/menu_item/defaults/accessibility_menu_highlighting.h`): main's canvas flag
		// takes it, and the guest's panels with it.
		FlashStorage::accessibilityMenuHighlighting = static_cast<MenuHighlighting>(stored);
		gui::ui::pushShellHighlighting();
		return DeviceWrite::STORED;
	}
	if (path == device_path::kSessionLayout) {
		FlashStorage::defaultSessionLayout = static_cast<SessionLayoutType>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kGridActiveMode) {
		FlashStorage::defaultGridActiveMode = static_cast<GridDefaultActiveMode>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kKeyboardLayout) {
		FlashStorage::defaultKeyboardLayout = static_cast<KeyboardLayoutType>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kFavouritesLayout) {
		FlashStorage::defaultFavouritesLayout = static_cast<FavouritesDefaultLayout>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kPadBrightness) {
		// `PadBrightness::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/pad_brightness.h`):
		// the pads' dimmer takes the level at once.
		FlashStorage::defaultPadBrightness = static_cast<uint8_t>(stored);
		PadLEDs::setBrightnessLevel(FlashStorage::defaultPadBrightness);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kScreensaverMode) {
		// `ScreensaverModeMenu::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/screensaver.h`):
		// both screensavers take the mode at once.
		FlashStorage::screensaverMode = static_cast<ScreensaverMode>(stored);
		deluge::hid::display::Screensaver::settingsChanged();
		return DeviceWrite::STORED;
	}
	if (path == device_path::kScreensaverTimeout) {
		// `ScreensaverTimeout::writeCurrentValue`: both screensavers re-arm for the timeout at once.
		FlashStorage::screensaverTimeoutMinutes = static_cast<uint8_t>(stored);
		deluge::hid::display::Screensaver::settingsChanged();
		return DeviceWrite::STORED;
	}
	if (path == device_path::kSamplePreview) {
		FlashStorage::sampleBrowserPreviewMode = static_cast<uint8_t>(stored);
		return DeviceWrite::STORED;
	}
	return std::nullopt;
}

/// @brief The audio rows the tables do not hold: input monitoring and USB audio.
std::optional<Value> load_audio(const SettingInfo& row) {
	const std::string_view path = row.path;
	if (path == device_path::kMonitorMode) {
		return choice(row, std::to_underlying(AudioEngine::inputMonitoringMode));
	}
	if (path == device_path::kUsbAudioEnabled) {
		return flag(FlashStorage::usbAudioEnabled);
	}
	if (path == device_path::kUsbOutputSource) {
		return choice(row, std::to_underlying(usb_audio::outputSource));
	}
	return std::nullopt;
}

/// @brief The audio rows the tables do not hold: input monitoring and USB audio.
std::optional<DeviceWrite> store_audio(std::string_view path, int32_t stored) {
	if (path == device_path::kMonitorMode) {
		AudioEngine::inputMonitoringMode = static_cast<InputMonitoringMode>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kUsbAudioEnabled) {
		// `usb::Audio::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/usb/audio.h`): the device offers
		// USB audio as it starts, so a value that no longer matches what this session presents waits for a restart.
		FlashStorage::usbAudioEnabled = stored != 0;
		return FlashStorage::usbAudioEnabled != usb_audio::present() ? DeviceWrite::STORED_FOR_RESTART
		                                                             : DeviceWrite::STORED;
	}
	if (path == device_path::kUsbOutputSource) {
		usb_audio::outputSource = static_cast<usb_audio::OutputSource>(stored);
		return DeviceWrite::STORED;
	}
	return std::nullopt;
}

/// @brief The clock rows the tables do not hold: the trigger clock's resolutions and the MIDI clock's directions.
std::optional<Value> load_clocks(const SettingInfo& row) {
	const std::string_view path = row.path;
	if (path == device_path::kTriggerInPpqn) {
		return number(playbackHandler.analogInTicksPPQN);
	}
	if (path == device_path::kTriggerOutPpqn) {
		return number(playbackHandler.analogOutTicksPPQN);
	}
	if (path == device_path::kMidiClockOut) {
		return flag(playbackHandler.midiOutClockEnabled);
	}
	if (path == device_path::kMidiClockIn) {
		return flag(playbackHandler.midiInClockEnabled);
	}
	return std::nullopt;
}

/// @brief The clock rows the tables do not hold: the trigger clock's resolutions and the MIDI clock's directions.
std::optional<DeviceWrite> store_clocks(std::string_view path, int32_t stored) {
	if (path == device_path::kTriggerInPpqn) {
		// `trigger::in::PPQN::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/trigger/in/ppqn.h`): while
		// the song follows the trigger clock, its ticks resync to the clock's at once.
		playbackHandler.analogInTicksPPQN = static_cast<uint32_t>(stored);
		if (playbackHandler.isExternalClockActive() && playbackHandler.usingAnalogClockInput
		    && currentSong != nullptr) {
			playbackHandler.resyncInternalTicksToInputTicks(currentSong);
		}
		return DeviceWrite::STORED;
	}
	if (path == device_path::kTriggerOutPpqn) {
		// `trigger::out::PPQN::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/trigger/out/ppqn.h`): the
		// clock's output resyncs to the song's ticks at once.
		playbackHandler.analogOutTicksPPQN = static_cast<uint32_t>(stored);
		playbackHandler.resyncAnalogOutTicksToInternalTicks();
		return DeviceWrite::STORED;
	}
	if (path == device_path::kMidiClockOut) {
		// `SendClock::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/send_clock.h`): the clock
		// starts or stops at once.
		playbackHandler.setMidiOutClockMode(stored != 0);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kMidiClockIn) {
		// `ReceiveClock::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/receive_clock.h`): off, the
		// song stops following an incoming clock.
		playbackHandler.setMidiInClockEnabled(stored != 0);
		return DeviceWrite::STORED;
	}
	return std::nullopt;
}

/// @brief The MIDI rows the tables do not hold: MIDI follow's feedback, transpose and takeover.
std::optional<Value> load_midi(const SettingInfo& row) {
	const std::string_view path = row.path;
	if (path == device_path::kFollowFeedbackChannel) {
		return choice(row, std::to_underlying(midiEngine.midiFollowFeedbackChannelType));
	}
	if (path == device_path::kFollowFeedbackAutomation) {
		return choice(row, std::to_underlying(midiEngine.midiFollowFeedbackAutomation));
	}
	if (path == device_path::kMidiTranspose) {
		// The device may hold the chord method, which no menu offers: it reads clamped to chromatic, as the
		// menu's list shows it.
		return choice(row, std::to_underlying(MIDITranspose::controlMethod));
	}
	if (path == device_path::kMidiTakeover) {
		return choice(row, std::to_underlying(midiEngine.midiTakeover));
	}
	return std::nullopt;
}

/// @brief The MIDI rows the tables do not hold: MIDI follow's feedback, transpose and takeover.
std::optional<DeviceWrite> store_midi(std::string_view path, int32_t stored) {
	if (path == device_path::kFollowFeedbackChannel) {
		midiEngine.midiFollowFeedbackChannelType = static_cast<MIDIFollowFeedbackChannelType>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kFollowFeedbackAutomation) {
		midiEngine.midiFollowFeedbackAutomation = static_cast<MIDIFollowFeedbackAutomationMode>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kMidiTranspose) {
		MIDITranspose::controlMethod = static_cast<MIDITransposeControlMethod>(stored);
		return DeviceWrite::STORED;
	}
	if (path == device_path::kMidiTakeover) {
		midiEngine.midiTakeover = static_cast<MIDITakeoverMode>(stored);
		return DeviceWrite::STORED;
	}
	return std::nullopt;
}

/// @brief The CV and gate outputs' rows, one per output.
std::optional<Value> load_cv_and_gate(const SettingInfo& row) {
	const std::string_view path = row.path;
	if (const auto output = output_at(device_path::kCvVoltsPerOctave, path, NUM_PHYSICAL_CV_CHANNELS)) {
		return number(cvEngine.cvChannels[*output].voltsPerOctave);
	}
	if (const auto output = output_at(device_path::kCvTranspose, path, NUM_PHYSICAL_CV_CHANNELS)) {
		// One number over the semitones and the cents, as `cv::Transpose::readCurrentValue`
		// (`origin/main:src/deluge/gui/menu_item/cv/transpose.h`) shows them.
		const CVChannel& channel = cvEngine.cvChannels[*output];
		return number(computeCurrentValueForTranspose(channel.transpose, channel.cents));
	}
	if (const auto output = output_at(device_path::kGateMode, path, NUM_GATE_CHANNELS)) {
		return choice(row, std::to_underlying(cvEngine.gateChannels[*output].mode));
	}
	return std::nullopt;
}

/// @brief The CV and gate outputs' rows, one per output.
std::optional<DeviceWrite> store_cv_and_gate(std::string_view path, int32_t stored) {
	if (const auto output = output_at(device_path::kCvVoltsPerOctave, path, NUM_PHYSICAL_CV_CHANNELS)) {
		// `cv::Volts::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/cv/volts.h`): the output's voltage
		// goes out again at once.
		cvEngine.setCVVoltsPerOctave(*output, static_cast<uint8_t>(stored));
		return DeviceWrite::STORED;
	}
	if (const auto output = output_at(device_path::kCvTranspose, path, NUM_PHYSICAL_CV_CHANNELS)) {
		// `cv::Transpose::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/cv/transpose.h`): the number
		// splits into semitones and cents, and the output's voltage goes out again at once.
		int32_t semitones = 0;
		int32_t cents = 0;
		computeFinalValuesForTranspose(stored, &semitones, &cents);
		cvEngine.setCVTranspose(*output, semitones, cents);
		return DeviceWrite::STORED;
	}
	if (const auto output = output_at(device_path::kGateMode, path, NUM_GATE_CHANNELS)) {
		// `gate::Mode::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/gate/mode.h`): the gate switches
		// at once, and the clock's and the run's gates start or stop carrying their signal.
		cvEngine.setGateType(*output, static_cast<GateType>(stored));
		return DeviceWrite::STORED;
	}
	return std::nullopt;
}

/// @brief The community features' rows.
std::optional<Value> load_features(const SettingInfo& row) {
	const FeatureInfo* feature = project::feature_at(row.path);
	if (feature == nullptr) {
		return std::nullopt;
	}
	// The file may hold any number: an on/off feature is on only at 1, as every reader of it tests
	// (`RuntimeFeatureSettings::isOn`), and a value past a feature's choices reads clamped into them.
	const uint32_t value = runtimeFeatureSettings.get(legacy_type(feature->id));
	return row.kind == plugin::SettingKind::BOOL ? flag(value == RuntimeFeatureStateToggle::On) : choice(row, value);
}

/// @brief The community features' rows.
std::optional<DeviceWrite> store_features(std::string_view path, int32_t stored) {
	const FeatureInfo* feature = project::feature_at(path);
	if (feature == nullptr) {
		return std::nullopt;
	}
	store_feature(*feature, stored);
	return DeviceWrite::STORED;
}

/// Every group's load, in the order a row is looked for; no row is in two groups.
constexpr std::array kGroupLoads{&load_homed,  &load_defaults, &load_pads_and_screen, &load_audio,
                                 &load_clocks, &load_midi,     &load_cv_and_gate,     &load_features};
/// Every group's store, in the same order.
constexpr std::array kGroupStores{&store_homed,  &store_defaults, &store_pads_and_screen, &store_audio,
                                  &store_clocks, &store_midi,     &store_cv_and_gate,     &store_features};

/// The device's own settings where the device keeps them: the flash settings, the pad controller's play cursor, the
/// community features, the audio engine's, USB audio's and the clocks' settings, MIDI's and MIDI follow's, the CV and
/// gate outputs', and the stem export's options. A store writes the value as the menu's own item does, with that item's
/// side effects; none saves (the settings menu saves on its close).
class HostDeviceSettings final : public deluge::ui::scenes::bind::DeviceSettings {
	[[nodiscard]] Value load(const SettingInfo& row) const override {
		for (const auto load_group : kGroupLoads) {
			if (std::optional<Value> value = load_group(row)) {
				return std::move(*value);
			}
		}
		D_PRINTLN("device setting %.*s has no home", static_cast<int>(row.path.size()), row.path.data());
		return Value::makeNumber(row.default_value);
	}

	[[nodiscard]] DeviceWrite store(const SettingInfo& row, const Value& value) override {
		const std::string_view path = row.path;
		// The front checked the value against the row, so it is a choice's word, a bool or an integer in range.
		const int32_t stored = plugin::stored_value(row, value.str()).value_or(row.default_value);
		for (const auto store_group : kGroupStores) {
			if (const std::optional<DeviceWrite> written = store_group(path, stored)) {
				return *written;
			}
		}
		// A row this host keeps nowhere: refused rather than dropped, so the write is seen not to have happened.
		D_PRINTLN("device setting %.*s has no home", static_cast<int>(path.size()), path.data());
		return DeviceWrite::REFUSED;
	}
};

} // namespace

deluge::ui::scenes::bind::DeviceSettings& device_settings() {
	static HostDeviceSettings settings;
	return settings;
}

} // namespace deluge::gui::shell_host
