#pragma once
#include <array>
#include <cstddef>

namespace deluge::l10n {
// Strings that can be localized
enum class Strings : size_t {
	// Errors
	STRING_FOR_ERROR_INSUFFICIENT_RAM,
	STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE,
	STRING_FOR_ERROR_SD_CARD,
	STRING_FOR_ERROR_SD_CARD_NOT_PRESENT,
	STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM,
	STRING_FOR_ERROR_FILE_CORRUPTED,
	STRING_FOR_ERROR_FILE_NOT_FOUND,
	STRING_FOR_ERROR_FILE_UNREADABLE,
	STRING_FOR_ERROR_FILE_UNSUPPORTED,
	STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW,
	STRING_FOR_ERROR_FOLDER_DOESNT_EXIST,
	STRING_FOR_ERROR_BUG,
	STRING_FOR_ERROR_WRITE_FAIL,
	STRING_FOR_ERROR_FILE_TOO_BIG,
	STRING_FOR_ERROR_PRESET_IN_USE,
	STRING_FOR_ERROR_NO_FURTHER_PRESETS,
	STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION,
	STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED,
	STRING_FOR_ERROR_SD_CARD_FULL,
	STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE,
	STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO,
	STRING_FOR_ERROR_WRITE_PROTECTED,
	STRING_FOR_ERROR_GENERIC,

	// Param sources (from functions.cpp)
	STRING_FOR_PATCH_SOURCE_LFO_GLOBAL,
	STRING_FOR_PATCH_SOURCE_LFO_LOCAL,
	STRING_FOR_PATCH_SOURCE_ENVELOPE_0,
	STRING_FOR_PATCH_SOURCE_ENVELOPE_1,
	STRING_FOR_PATCH_SOURCE_VELOCITY,
	STRING_FOR_PATCH_SOURCE_NOTE,
	STRING_FOR_PATCH_SOURCE_COMPRESSOR,
	STRING_FOR_PATCH_SOURCE_RANDOM,
	STRING_FOR_PATCH_SOURCE_AFTERTOUCH,
	STRING_FOR_PATCH_SOURCE_X,
	STRING_FOR_PATCH_SOURCE_Y,
	STRING_FOR_PATCH_SOURCE_UNKNOWN,

	// Params (originally from functions.cpp)
	STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME,
	STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME,
	STRING_FOR_PARAM_LOCAL_VOLUME,
	STRING_FOR_PARAM_LOCAL_NOISE_VOLUME,
	STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH,
	STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH,
	STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX,
	STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX,
	STRING_FOR_PARAM_LOCAL_LPF_RESONANCE,
	STRING_FOR_PARAM_LOCAL_HPF_RESONANCE,
	STRING_FOR_PARAM_LOCAL_PAN,
	STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME,
	STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME,
	STRING_FOR_PARAM_LOCAL_LPF_FREQ,
	STRING_FOR_PARAM_LOCAL_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_HPF_FREQ,
	STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ,
	STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK,
	STRING_FOR_PARAM_LOCAL_ENV_0_DECAY,
	STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN,
	STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE,
	STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK,
	STRING_FOR_PARAM_LOCAL_ENV_1_DECAY,
	STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN,
	STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE,
	STRING_FOR_PARAM_GLOBAL_LFO_FREQ,
	STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX,
	STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND,
	STRING_FOR_PARAM_GLOBAL_DELAY_RATE,
	STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK,
	STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT,
	STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE,
	STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH,
	STRING_FOR_PARAM_GLOBAL_ARP_RATE,
	STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK,
	STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK,
	STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK,
	STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK,

	// General
	STRING_FOR_OFF,
	STRING_FOR_OK,
	STRING_FOR_NEW,
	STRING_FOR_DELETE,
	STRING_FOR_SURE,
	STRING_FOR_OVERWRITE,
	STRING_FOR_OPTIONS,

	// Menu Titles
	STRING_FOR_AUDIO_SOURCE,
	STRING_FOR_ARE_YOU_SURE_QMARK,
	STRING_FOR_DELETE_QMARK,
	STRING_FOR_SAMPLES,
	STRING_FOR_LOAD_FILES,
	STRING_FOR_CLEAR_SONG_QMARK,
	STRING_FOR_LOAD_PRESET,
	STRING_FOR_OVERWRITE_BOOTLOADER_TITLE,
	STRING_FOR_OVERWRITE_QMARK,

	// gui/context_menu/audio_input_selector.cpp
	STRING_FOR_LEFT_INPUT,
	STRING_FOR_LEFT_INPUT_MONITORING,
	STRING_FOR_RIGHT_INPUT,
	STRING_FOR_RIGHT_INPUT_MONITORING,
	STRING_FOR_STEREO_INPUT,
	STRING_FOR_STEREO_INPUT_MONITORING,
	STRING_FOR_BALANCED_INPUT,
	STRING_FOR_BALANCED_INPUT_MONITORING,
	STRING_FOR_MIX_PRE_FX,
	STRING_FOR_MIX_POST_FX,

	// gui/context_menu/sample_browser/kit.cpp
	STRING_FOR_LOAD_ALL,
	STRING_FOR_SLICE,

	// gui/context_menu/sample_browser/synth.cpp
	STRING_FOR_MULTISAMPLES,
	STRING_FOR_BASIC,
	STRING_FOR_SINGLE_CYCLE,
	STRING_FOR_WAVETABLE,

	// gui/context_menu/delete_file.cpp
	STRING_FOR_ERROR_DELETING_FILE,
	STRING_FOR_FILE_DELETED,

	// gui/context_menu/load_instrument_preset.cpp
	STRING_FOR_CLONE,

	// gui/context_menu/overwrite_bootloader.cpp
	STRING_FOR_ACCEPT_RISK,
	STRING_FOR_ERROR_BOOTLOADER_TOO_BIG,
	STRING_FOR_ERROR_BOOTLOADER_TOO_SMALL,
	STRING_FOR_BOOTLOADER_UPDATED,
	STRING_FOR_ERROR_BOOTLOADER_FILE_NOT_FOUND,

	// gui/context_menu/save_song_or_instrument.cpp
	STRING_FOR_COLLECT_MEDIA,
	STRING_FOR_CREATE_FOLDER,

	STRINGS_LAST
};

// The maximum number of strings that can be localized
constexpr size_t kNumStrings = static_cast<size_t>(Strings::STRINGS_LAST);

/**
 * @brief Helper for creating localization maps
 * @todo: When c++20 support can be finalized (e2 calls dbt for building),
 *        this should be switched to `consteval` so that it _never_ runs on the Deluge
 */
consteval std::array<const char*, kNumStrings>
build_l10n_map(std::initializer_list<std::pair<l10n::Strings, const char*>> stringmaps) {
	std::array<const char*, kNumStrings> output = {};

	// default all values to empty string
	std::fill(output.begin(), output.end(), "");

	// Replace with stringmap values for valid entries
	for (auto [string_id, s] : stringmaps) {
		const auto id = static_cast<size_t>(string_id);
		output[id] = s;
	}
	return output;
};

} // namespace deluge::l10n
