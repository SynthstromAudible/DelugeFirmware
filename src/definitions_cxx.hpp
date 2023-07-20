/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "util/misc.h"

enum FirmwareVersion {
	FIRMWARE_OLD = 0,
	FIRMWARE_1P2P0 = 1,
	FIRMWARE_1P3P0_PRETEST = 2,
	FIRMWARE_1P3P0_BETA = 3,
	FIRMWARE_1P3P0 = 4,
	FIRMWARE_1P3P1 = 5,
	FIRMWARE_1P3P2 = 6,
	FIRMWARE_1P4P0_PRETEST = 7,
	FIRMWARE_1P4P0_BETA = 8,
	FIRMWARE_1P4P0 = 9,
	FIRMWARE_1P5P0_PREBETA = 10,
	FIRMWARE_2P0P0_BETA = 11,
	FIRMWARE_2P0P0 = 12,
	FIRMWARE_2P0P1_BETA = 13,
	FIRMWARE_2P0P1 = 14,
	FIRMWARE_2P0P2_BETA = 15,
	FIRMWARE_2P0P2 = 16,
	FIRMWARE_2P0P3 = 17,
	FIRMWARE_2P1P0_BETA = 18,
	FIRMWARE_2P1P0 = 19,
	FIRMWARE_2P1P1_BETA = 20,
	FIRMWARE_2P1P1 = 21,
	FIRMWARE_2P1P2_BETA = 22,
	FIRMWARE_2P1P2 = 23,
	FIRMWARE_2P1P3_BETA = 24,
	FIRMWARE_2P1P3 = 25,
	FIRMWARE_2P1P4_BETA = 26,
	FIRMWARE_2P1P4 = 27,
	FIRMWARE_3P0P0_ALPHA = 28,
	FIRMWARE_3P0P0_BETA = 29,
	FIRMWARE_3P0P0 = 30,
	FIRMWARE_3P0P1_BETA = 31,
	FIRMWARE_3P0P1 = 32,
	FIRMWARE_3P0P2 = 33,
	FIRMWARE_3P0P3_ALPHA = 34,
	FIRMWARE_3P0P3_BETA = 35,
	FIRMWARE_3P0P3 = 36,
	FIRMWARE_3P0P4 = 37,
	FIRMWARE_3P0P5_BETA = 38,
	FIRMWARE_3P0P5 = 39,
	FIRMWARE_3P1P0_ALPHA = 40,
	FIRMWARE_3P1P0_ALPHA2 = 41,
	FIRMWARE_3P1P0_BETA = 42,
	FIRMWARE_3P1P0 = 43,
	FIRMWARE_3P1P1_BETA = 44,
	FIRMWARE_3P1P1 = 45,
	FIRMWARE_3P1P2_BETA = 46,
	FIRMWARE_3P1P2 = 47,
	FIRMWARE_3P1P3_BETA = 48,
	FIRMWARE_3P1P3 = 49,
	FIRMWARE_3P1P4_BETA = 50,
	FIRMWARE_3P1P4 = 51,
	FIRMWARE_3P1P5_BETA = 52,
	FIRMWARE_3P1P5 = 53,
	FIRMWARE_3P2P0_ALPHA = 54,
	FIRMWARE_4P0P0_BETA = 55,
	FIRMWARE_4P0P0 = 56,
	FIRMWARE_4P0P1_BETA = 57,
	FIRMWARE_4P0P1 = 58,
	FIRMWARE_4P1P0_ALPHA = 59,
	FIRMWARE_4P1P0_BETA = 60,
	FIRMWARE_4P1P0 = 61,
	FIRMWARE_4P1P1_ALPHA = 62,
	FIRMWARE_4P1P1 = 63,
	FIRMWARE_4P1P2 = 64,
	FIRMWARE_4P1P3_ALPHA = 65,
	FIRMWARE_4P1P3_BETA = 66,
	FIRMWARE_4P1P3 = 67,
	FIRMWARE_4P1P4_ALPHA = 68,
	FIRMWARE_4P1P4_BETA = 69,
	FIRMWARE_4P1P4 = 70,
	FIRMWARE_TOO_NEW = 255,
};
constexpr FirmwareVersion CURRENT_FIRMWARE_VERSION = FIRMWARE_4P1P4_ALPHA;

#define syncScalingButtonX 7
#define syncScalingButtonY 2
#define syncScalingLedX 7
#define syncScalingLedY 2

#define crossScreenEditButtonX 6
#define crossScreenEditButtonY 2
#define crossScreenEditLedX 6
#define crossScreenEditLedY 2

#define xEncButtonX 0
#define xEncButtonY 1

#define yEncButtonX 0
#define yEncButtonY 0

#define tempoEncButtonX 4
#define tempoEncButtonY 1

#define affectEntireButtonX 3
#define affectEntireButtonY 0
#define affectEntireLedX 3
#define affectEntireLedY 0

#define modEncoder0ButtonX 0
#define modEncoder0ButtonY 2

#define modEncoder1ButtonX 0
#define modEncoder1ButtonY 3

#define editPadPressBufferSize 8

#define NUM_MOD_BUTTONS 8

#define displayHeight 8
#define displayHeightMagnitude 3
#define displayWidth 16
#define displayWidthMagnitude 4
#define NO_PRESSES_HAPPENING_MESSAGE 254
#define RESEND_BUTTON_STATES_MESSAGE 22
#define NUM_BYTES_IN_COL_UPDATE_MESSAGE 49
#define NUM_BYTES_IN_LONGEST_MESSAGE 55
#define NUM_BYTES_IN_SIDEBAR_REDRAW (NUM_BYTES_IN_COL_UPDATE_MESSAGE)
#define PAD_AND_BUTTON_MESSAGES_END 180

#define NUM_BYTES_IN_MAIN_PAD_REDRAW (NUM_BYTES_IN_COL_UPDATE_MESSAGE * 8)

#define DEFAULT_ARRANGER_ZOOM (DEFAULT_CLIP_LENGTH >> 1)

constexpr uint8_t LINE_OUT_DETECT_L_1 = 6;
constexpr uint8_t LINE_OUT_DETECT_L_2 = 3;
constexpr uint8_t LINE_OUT_DETECT_R_1 = 6;
constexpr uint8_t LINE_OUT_DETECT_R_2 = 4;
constexpr uint8_t ANALOG_CLOCK_IN_1 = 1;
constexpr uint8_t ANALOG_CLOCK_IN_2 = 14;
constexpr uint8_t SPEAKER_ENABLE_1 = 4;
constexpr uint8_t SPEAKER_ENABLE_2 = 1;
constexpr uint8_t HEADPHONE_DETECT_1 = 6;
constexpr uint8_t HEADPHONE_DETECT_2 = 5;

#define sideBarWidth 2
#define MAX_NUM_ANIMATED_ROWS ((displayHeight * 3) >> 1)

enum class MidiLearn : uint8_t {
	NONE,
	CLIP,
	NOTEROW_MUTE,
	PLAY_BUTTON,
	RECORD_BUTTON,
	TAP_TEMPO_BUTTON,
	SECTION,
	MELODIC_INSTRUMENT_INPUT,
	DRUM_INPUT,
};

#define minTimePerTimerTick 1
#define numInputTicksToAverageTime 24
#define numInputTicksToAllowTempoTargeting 24 // This is probably even high enough to cause audible glitches
#define maxOutputTickMagnitude 5

#define buttonDebounceTime 100 // Milliseconds
#define padDebounceTime 50     // Milliseconds
#define colTime 36             // In 21.25 uS's (or did I mean nS?)
#define zoomSpeed 110
#define clipCollapseSpeed 200
#define fadeSpeed 300
#define flashLength 3
#define DEFAULT_CLIP_LENGTH 96 // You'll want to <<displayWidthMagnitude this each time used
#define horizontalSongSelectorSpeed 90
#define noteRowCollapseSpeed 150
#define greyoutSpeed (300 * 44)

#define initialFlashTime 250
#define flashTime 110
#define fastFlashTime 60
#define SAMPLE_MARKER_BLINK_TIME 200

#define USE_DEFAULT_VELOCITY 255

#define MAX_SEQUENCE_LENGTH 1610612736 // The biggest multiple of 3 which can fit in a signed 32-bit int
#define noteOnLatenessAllowed 2205     // In audio samples. That's 50mS. Multiply mS by 44.1

#define GATE_MODE_V_TRIG 0
#define GATE_MODE_S_TRIG 1

#define numSongSlots 1000
#define numInstrumentSlots 1000
#define maxNumInstrumentPresets 128
#define FILENAME_BUFFER_SIZE 256 // Don't ever make this less! The zoom rendering code uses this buffer for its stuff

enum class InstrumentType : uint8_t { SYNTH, KIT, MIDI_OUT, CV, AUDIO, NONE = 255 };

enum class ThingType {
	SYNTH,
	KIT,
	SONG,
	NONE,
};

// Maximum num samples that may be processed in one "frame". Actual size of output buffer is in ssi.h
#define audioEngineBufferSize 128

#define modFXBufferSize 512
#define modFXBufferIndexMask (modFXBufferSize - 1)
#define modFXMaxDelay ((modFXBufferSize - 1) << 16)
#define flangerMinTime (3 << 16)
#define flangerAmplitude (modFXMaxDelay - flangerMinTime)
#define flangerOffset ((modFXMaxDelay + flangerMinTime) >> 1)

#define numEnvelopes 2
#define numLFOs 2
#define numModulators 2

#define maxNumUnison 8
#define NUM_VOICES_STATIC 24
#define NUM_VOICE_SAMPLES_STATIC 20
#define NUM_TIME_STRETCHERS_STATIC 6
#define maxNumNoteOnsPending 64

#define NUM_UINTS_TO_REP_PATCH_CABLES 1
#define MAX_NUM_PATCH_CABLES (NUM_UINTS_TO_REP_PATCH_CABLES * 32)

enum class EnvelopeStage : uint8_t {
	ATTACK,
	DECAY,
	SUSTAIN,
	RELEASE,
	FAST_RELEASE,
	OFF,
	NUM_ENVELOPE_STAGES,
};

#define NUM_PRIORITY_OPTIONS 3

enum class PatchSource : uint8_t {
	LFO_GLOBAL,
	COMPRESSOR,
	ENVELOPE_0,
	ENVELOPE_1,
	LFO_LOCAL,
	X,
	Y,
	AFTERTOUCH,
	VELOCITY,
	NOTE,
	RANDOM,
	NONE,

	// Used for shortcuts
	SOON = 254,
	NOT_AVAILABLE = 255,
};
constexpr PatchSource kLastPatchSource = PatchSource::NONE;
constexpr size_t kNumPatchSources = static_cast<size_t>(kLastPatchSource);

constexpr PatchSource FIRST_GLOBAL_SOURCE_WITH_CHANGED_STATUS_AUTOMATICALLY_UPDATED = PatchSource::ENVELOPE_0;
constexpr PatchSource FIRST_LOCAL_SOURCE = PatchSource::ENVELOPE_0;
constexpr PatchSource FIRST_UNCHANGEABLE_SOURCE = PatchSource::VELOCITY;

// Linear params have different sources multiplied together, then multiplied by the neutral value
// -- and "volume" ones get squared at the end

// Hybrid params have different sources added together, then added to the neutral value

// Exp params have different sources added together, converted to an exponential scale, then multiplied by the neutral value

enum Param : uint8_t {
	// Local linear params begin
	PARAM_LOCAL_OSC_A_VOLUME = 0,
	PARAM_LOCAL_OSC_B_VOLUME = 1,
	PARAM_LOCAL_VOLUME = 2,
	PARAM_LOCAL_NOISE_VOLUME = 3,
	PARAM_LOCAL_MODULATOR_0_VOLUME = 4,
	PARAM_LOCAL_MODULATOR_1_VOLUME = 5,

	// Local non-volume params begin
	PARAM_LOCAL_MODULATOR_0_FEEDBACK = 6,
	PARAM_LOCAL_MODULATOR_1_FEEDBACK = 7,
	PARAM_LOCAL_CARRIER_0_FEEDBACK = 8,
	PARAM_LOCAL_CARRIER_1_FEEDBACK = 9,
	PARAM_LOCAL_LPF_RESONANCE = 10,
	PARAM_LOCAL_HPF_RESONANCE = 11,
	PARAM_LOCAL_ENV_0_SUSTAIN = 12,
	PARAM_LOCAL_ENV_1_SUSTAIN = 13,

	// Local hybrid params begin
	PARAM_LOCAL_OSC_A_PHASE_WIDTH = 14,
	PARAM_LOCAL_OSC_B_PHASE_WIDTH = 15,
	PARAM_LOCAL_OSC_A_WAVE_INDEX = 16,
	PARAM_LOCAL_OSC_B_WAVE_INDEX = 17,
	PARAM_LOCAL_PAN = 18,

	// Local exp params begin
	PARAM_LOCAL_LPF_FREQ = 19,
	PARAM_LOCAL_PITCH_ADJUST = 20,
	PARAM_LOCAL_OSC_A_PITCH_ADJUST = 21,
	PARAM_LOCAL_OSC_B_PITCH_ADJUST = 22,
	PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST = 23,
	PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST = 24,
	PARAM_LOCAL_HPF_FREQ = 25,
	PARAM_LOCAL_LFO_LOCAL_FREQ = 26,
	PARAM_LOCAL_ENV_0_ATTACK = 27,
	PARAM_LOCAL_ENV_1_ATTACK = 28,
	PARAM_LOCAL_ENV_0_DECAY = 29,
	PARAM_LOCAL_ENV_1_DECAY = 30,
	PARAM_LOCAL_ENV_0_RELEASE = 31,
	PARAM_LOCAL_ENV_1_RELEASE = 32,

	// Global (linear) params begin
	PARAM_GLOBAL_VOLUME_POST_FX = 33,
	PARAM_GLOBAL_VOLUME_POST_REVERB_SEND = 34,
	PARAM_GLOBAL_REVERB_AMOUNT = 35,
	PARAM_GLOBAL_MOD_FX_DEPTH = 36,

	// Global non-volume params begin
	PARAM_GLOBAL_DELAY_FEEDBACK = 37,

	// Global hybrid params begin
	// Global exp params begin
	PARAM_GLOBAL_DELAY_RATE = 38,
	PARAM_GLOBAL_MOD_FX_RATE = 39,
	PARAM_GLOBAL_LFO_FREQ = 40,
	PARAM_GLOBAL_ARP_RATE = 41,
	// ANY TIME YOU UPDATE THIS LIST! CHANGE Sound::paramToString()

	PARAM_NONE = 42,
};

#define FIRST_LOCAL_NON_VOLUME_PARAM 6
#define FIRST_LOCAL_HYBRID_PARAM 14
#define FIRST_LOCAL_EXP_PARAM 19

#define FIRST_GLOBAL_PARAM 33
#define FIRST_GLOBAL_NON_VOLUME_PARAM 37
#define FIRST_GLOBAL_HYBRID_PARAM 38
#define FIRST_GLOBAL_EXP_PARAM 38
#define NUM_PARAMS 42 // Not including the "none" param

#define PARAM_PLACEHOLDER_RANGE 89 // Not a real param. For the purpose of reading old files from before V3.2.0

#define PARAM_UNPATCHED_SECTION 90

enum ParamUnpatchedAll : uint8_t {
	// For all ModControllables
	PARAM_UNPATCHED_STUTTER_RATE,
	PARAM_UNPATCHED_BASS,
	PARAM_UNPATCHED_TREBLE,
	PARAM_UNPATCHED_BASS_FREQ,
	PARAM_UNPATCHED_TREBLE_FREQ,
	PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION,
	PARAM_UNPATCHED_BITCRUSHING,
	PARAM_UNPATCHED_MOD_FX_OFFSET,
	PARAM_UNPATCHED_MOD_FX_FEEDBACK,
	PARAM_UNPATCHED_COMPRESSOR_SHAPE,
	// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp
	NUM_SHARED_UNPATCHED_PARAMS,
};

// Just for Sounds
enum ParamUnpatchedSound : uint8_t {
	PARAM_UNPATCHED_SOUND_ARP_GATE = NUM_SHARED_UNPATCHED_PARAMS,
	PARAM_UNPATCHED_SOUND_PORTA,
	// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp
	MAX_NUM_UNPATCHED_PARAM_FOR_SOUNDS,
};

// Just for GlobalEffectables
enum ParamUnpatchedGlobalEffectable : uint8_t {
	PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE = NUM_SHARED_UNPATCHED_PARAMS,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME,
	PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST,
	MAX_NUM_UNPATCHED_PARAM_FOR_GLOBALEFFECTABLE,
};

constexpr uint8_t MAX_NUM_UNPATCHED_PARAMS = MAX_NUM_UNPATCHED_PARAM_FOR_GLOBALEFFECTABLE;

#define KIT_SIDECHAIN_SHAPE (-601295438)

enum class OscType {
	SINE,
	TRIANGLE,
	SQUARE,
	ANALOG_SQUARE,
	SAW,
	ANALOG_SAW_2,
	WAVETABLE,
	SAMPLE,
	INPUT_L,
	INPUT_R,
	INPUT_STEREO,
};

constexpr OscType LAST_RINGMODDABLE_OSC_TYPE = OscType::WAVETABLE;
constexpr auto kNumOscTypesRingModdable = util::to_underlying(LAST_RINGMODDABLE_OSC_TYPE) + 1;
constexpr auto kNumOscTypes = util::to_underlying(OscType::INPUT_STEREO) + 1;

enum class LFOType {
	SINE,
	TRIANGLE,
	SQUARE,
	SAW,
	SAMPLE_AND_HOLD,
	RANDOM_WALK,
};

constexpr auto kNumLFOTypes = util::to_underlying(LFOType::RANDOM_WALK);

// SyncType values correspond to the index of the first option of the specific
// type in the selection menu. There are 9 different levels for each type (see
// also SyncLevel)
enum SyncType {
	SYNC_TYPE_EVEN = 0,
	SYNC_TYPE_TRIPLET = 10,
	SYNC_TYPE_DOTTED = 19,
};

enum SyncLevel {
	SYNC_LEVEL_NONE = 0,
	SYNC_LEVEL_WHOLE = 1,
	SYNC_LEVEL_2ND = 2,
	SYNC_LEVEL_4TH = 3,
	SYNC_LEVEL_8TH = 4,
	SYNC_LEVEL_16TH = 5,
	SYNC_LEVEL_32ND = 6,
	SYNC_LEVEL_64TH = 7,
	SYNC_LEVEL_128TH = 8,
	SYNC_LEVEL_256TH = 9,
};

enum class SynthMode {
	SUBTRACTIVE,
	FM,
	RINGMOD,
};

enum class ModFXType {
	NONE,
	FLANGER,
	CHORUS,
	PHASER,
	CHORUS_STEREO,
};

constexpr auto NUM_MOD_FX_TYPES = util::to_underlying(ModFXType::CHORUS_STEREO) + 1;

#define SAMPLE_MAX_TRANSPOSE 24
#define SAMPLE_MIN_TRANSPOSE (-96)

#define WAV_FORMAT_PCM 1
#define WAV_FORMAT_FLOAT 3

enum class PolyphonyMode {
	AUTO,
	POLY,
	MONO,
	LEGATO,
	CHOKE,
};

constexpr auto kNumPolyphonyModes = util::to_underlying(PolyphonyMode::CHOKE) + 1;

#define NUMERIC_DISPLAY_LENGTH 4

#define MAX_NUM_SECTIONS 12

#define NUM_PHYSICAL_MOD_KNOBS 2

enum class LPFMode {
	TRANSISTOR_12DB,
	TRANSISTOR_24DB,
	TRANSISTOR_24DB_DRIVE,
	SVF,
};
constexpr auto NUM_LPF_MODES = util::to_underlying(LPFMode::SVF) + 1;


#define PHASER_NUM_ALLPASS_FILTERS 6

enum ErrorType {
	NO_ERROR,
	ERROR_INSUFFICIENT_RAM,
	ERROR_UNSPECIFIED,
	ERROR_SD_CARD,
	ERROR_NO_FURTHER_PRESETS,
	ERROR_FILE_CORRUPTED,
	ERROR_FILE_UNREADABLE, // Or not found, I think?
	ERROR_FILE_UNSUPPORTED,
	ERROR_FILE_FIRMWARE_VERSION_TOO_NEW,
	RESULT_TAG_UNUSED,
	ERROR_FOLDER_DOESNT_EXIST,
	ERROR_WRITE_PROTECTED,
	ERROR_BUG,
	ERROR_WRITE_FAIL,
	ERROR_FILE_TOO_BIG,
	ERROR_PRESET_IN_USE,
	ERROR_NO_FURTHER_FILES_THIS_DIRECTION,
	ERROR_FILE_ALREADY_EXISTS,
	ERROR_FILE_NOT_FOUND,
	ERROR_ABORTED_BY_USER,
	ERROR_MAX_FILE_SIZE_REACHED,
	ERROR_SD_CARD_FULL,
	ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE,
	ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO,
	ERROR_NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP,
	NO_ERROR_BUT_GET_OUT,
	ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE,
	ERROR_SD_CARD_NOT_PRESENT,
	ERROR_SD_CARD_NO_FILESYSTEM,
};

enum class SampleRepeatMode {
	CUT,
	ONCE,
	LOOP,
	STRETCH,
};
constexpr auto NUM_REPEAT_MODES = util::to_underlying(SampleRepeatMode::STRETCH) + 1;


enum class FilterType {
	LPF,
	HPF,
	EQ,
};
constexpr auto NUM_FILTER_TYPES = util::to_underlying(FilterType::EQ) + 1;

#define NUM_SOURCES 2 // That's sources as in oscillators - within a Sound (synth).

#define PIC_MESSAGE_REFRESH_TIME 19

enum class ArpMode {
	OFF,
	UP,
	DOWN,
	BOTH,
	RANDOM,
};
constexpr auto NUM_ARP_MODES = util::to_underlying(ArpMode::RANDOM) + 1;


#define ALLOW_SPAM_MODE 0 // For debugging I think?

#define KEYBOARD_ROW_INTERVAL_MAX 16

enum class ModFXParam {
	DEPTH,
	FEEDBACK,
	OFFSET,
};
constexpr auto NUM_MOD_FX_PARAMS = util::to_underlying(ModFXParam::OFFSET) + 1;

#define PATCH_CABLE_ACCEPTANCE_YET_T0_BE_DETERMINED 3
#define PATCH_CABLE_ACCEPTANCE_ALLOWED 2
#define PATCH_CABLE_ACCEPTANCE_EDITABLE 1
#define PATCH_CABLE_ACCEPTANCE_DISALLOWED 0

#define DOUBLE_TAP_MS 400

#define SD_TEST_MODE_ENABLED 0
#define SD_TEST_MODE_ENABLED_LOAD_SONGS 0
#define SD_TEST_MODE_ENABLED_SAVE_SONGS 0
#define UNDO_REDO_TEST_ENABLED 0
#define RECORDING_TEST_ENABLED 0
#define AUTOPILOT_TEST_ENABLED 0
#define LAUNCH_CLIP_TEST_ENABLED 0

enum class GlobalMIDICommand {
	NONE = -1,
	PLAYBACK_RESTART,
	PLAY,
	RECORD,
	TAP,
	LOOP,
	LOOP_CONTINUOUS_LAYERING,
	UNDO,
	REDO,
};
constexpr auto NUM_GLOBAL_MIDI_COMMANDS = util::to_underlying(GlobalMIDICommand::REDO) + 1;

enum MidiTakeoverMode {
	MIDI_TAKEOVER_JUMP,
	MIDI_TAKEOVER_PICKUP,
	MIDI_TAKEOVER_SCALE,
	NUM_MIDI_TAKEOVER_MODES,
};

#define NUM_CLUSTERS_LOADED_AHEAD 2

enum InputMonitoringMode {
	INPUT_MONITORING_SMART,
	INPUT_MONITORING_ON,
	INPUT_MONITORING_OFF,
	NUM_INPUT_MONITORING_MODES,
};

enum ClusterLoad {
	CLUSTER_DONT_LOAD,
	CLUSTER_ENQUEUE,
	CLUSTER_LOAD_IMMEDIATELY,
	CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE,
};

enum ScaleType {
	SCALE_TYPE_SCALE,
	SCALE_TYPE_CHROMATIC,
	SCALE_TYPE_KIT,
};

enum ArmState {
	ARM_STATE_OFF,
	ARM_STATE_ON_NORMAL, // Arming to stop or start normally, or to stop soloing
	ARM_STATE_ON_TO_SOLO,
};

#define ALPHA_OR_BETA_VERSION 1 // Whether to compile with additional error-checking

#define NUM_PROBABILITY_VALUES 20
#define DEFAULT_LIFT_VALUE 64

#if ALPHA_OR_BETA_VERSION
//#define TEST_VECTOR 1
//#define TEST_VECTOR_SEARCH_MULTIPLE 1
//#define TEST_GENERAL_MEMORY_ALLOCATION 1
//#define TEST_VECTOR_DUPLICATES 1
//#define TEST_BST 1
//#define TEST_OPEN_ADDRESSING_HASH_TABLE 1
//#define TEST_SD_WRITE 1
//#define TEST_SAMPLE_LOOP_POINTS 1
#endif

#define NAVIGATION_CLIP 0
#define NAVIGATION_ARRANGEMENT 1

#define PRESET_SEARCH_ALL 0
#define PRESET_SEARCH_NOT_ACTIVE_IN_SESSION 1
#define PRESET_SEARCH_NOT_ACTIVE_IN_ARRANGEMENT 2

#define AVAILABILITY_ANY 0
#define AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION 1
#define AVAILABILITY_INSTRUMENT_UNUSED 2

#define BEFORE 0
#define AFTER 1

#define DELETE 1
#define CREATE 0

#define CC_NUMBER_PITCH_BEND 120
#define CC_NUMBER_AFTERTOUCH 121
#define CC_NUMBER_NONE 122
#define NUM_CC_NUMBERS_INCLUDING_FAKE 123
#define NUM_REAL_CC_NUMBERS 120

#define INSTRUMENT_REMOVAL_NONE 0
#define INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED 1
#define INSTRUMENT_REMOVAL_DELETE 2

#define HARDWARE_TEST_MODE 0

enum class DrumType {
	SOUND,
	MIDI,
	GATE,
};

enum PgmChangeSend {
	PGM_CHANGE_SEND_NEVER,
	PGM_CHANGE_SEND_ONCE,
};

enum Marker : int8_t {
	MARKER_NONE = -1,
	MARKER_START,
	MARKER_LOOP_START,
	MARKER_LOOP_END,
	MARKER_END,
	NUM_MARKER_TYPES,
};

enum InterpolationMode {
	INTERPOLATION_MODE_LINEAR,
	INTERPOLATION_MODE_SMOOTH,
	NUM_INTERPOLATION_MODES,
};

#define CACHE_BYTE_DEPTH 3
#define CACHE_BYTE_DEPTH_MAGNITUDE 2 // Invalid / unused for odd numbers of bytes like 3

#define MAX_UNISON_DETUNE 50

#define PARAM_STATIC_COMPRESSOR_ATTACK 162
#define PARAM_STATIC_COMPRESSOR_RELEASE 163
#define PARAM_STATIC_COMPRESSOR_VOLUME 164 // Only used for the reverb compressor. Normally this is done with patching
#define PARAM_STATIC_PATCH_CABLE 190       // Special case

#define PERC_BUFFER_REDUCTION_MAGNITUDE                                                                                \
	7 // This is about right. Making it smaller didn't help. Tried it as 9, and I'm pretty sure some fast percussive details were lost in the output
#define PERC_BUFFER_REDUCTION_SIZE (1 << PERC_BUFFER_REDUCTION_MAGNITUDE)
#define DIFFERENCE_LPF_POLES 2

#define INTERPOLATION_MAX_NUM_SAMPLES 16
#define INTERPOLATION_MAX_NUM_SAMPLES_MAGNITUDE 4

enum ClusterType {
	CLUSTER_EMPTY,
	CLUSTER_SAMPLE,
	CLUSTER_GENERAL_MEMORY,
	CLUSTER_SAMPLE_CACHE,
	CLUSTER_PERC_CACHE_FORWARDS,
	CLUSTER_PERC_CACHE_REVERSED,
	CLUSTER_OTHER,
};

enum PlayHead {
	PLAY_HEAD_OLDER,
	PLAY_HEAD_NEWER,
};

#define INPUT_RAW_BUFFER_SIZE 8192
#define INPUT_REPITCHED_BUFFER_SIZE 2048
#define INPUT_PERC_BUFFER_SIZE (INPUT_RAW_BUFFER_SIZE >> PERC_BUFFER_REDUCTION_MAGNITUDE)

#define INPUT_ENABLE_REPITCHED_BUFFER                                                                                  \
	0 // Experimental, from when developing input pitch shifting. Probably won't actually work now, if it ever did!

#define TIME_STRETCH_DEFAULT_FIRST_HOP_LENGTH 200

#define TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES                                                                     \
	3 // 3 sounds way better than 2. After that, kinda diminishing returns
#define TIME_STRETCH_CROSSFADE_MOVING_AVERAGE_LENGTH                                                                   \
	35 // Anywhere between 30 and 40 seemed ideal. Point of interest - high numbers (e.g. I tried 140) screw up the high notes, so more is not more!

// I think this was an experimental mode which allowed the pitch-change effect (i.e. windowed sinc interpolation) to be
// stored and reused between the two time-stretch play-heads. Probably won't work anymore. From memory, wasn't very
// beneficial, especially relative to its complexity and potential bugginess.
#define TIME_STRETCH_ENABLE_BUFFER 0

#if TIME_STRETCH_ENABLE_BUFFER
#define TIME_STRETCH_BUFFER_SIZE 4096
#else
#define TIME_STRETCH_BUFFER_SIZE 256
#endif

#define PITCH_DETECT_WINDOW_SIZE_MAGNITUDE                                                                             \
	13 // We don't want the window too short, or some sounds / harmonics can be missed during the attack
#define PITCH_DETECT_WINDOW_SIZE (1 << PITCH_DETECT_WINDOW_SIZE_MAGNITUDE)

#define MAX_FILE_SIZE 1073741824

#define QWERTY_HOME_ROW 3

#define AUDIO_RECORD_LAG_COMPENTATION 294

enum class AudioInputChannel {
	UNSET = -1,
	NONE,
	LEFT,
	RIGHT,
	STEREO,
	BALANCED,
	MIX,
	OUTPUT,
};

constexpr AudioInputChannel AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION = AudioInputChannel::MIX;

enum ActionResult {
	ACTION_RESULT_DEALT_WITH,
	ACTION_RESULT_NOT_DEALT_WITH,
	ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE,
	ACTION_RESULT_ACTIONED_AND_CAUSED_CHANGE,
};

#define ENABLE_CLIP_CUTTING_DIAGNOSTICS 1

#define AUDIO_CLIP_MARGIN_SIZE_POST_END 2048

// Let's just do a 100 sample crossfade. Even 12 samples actually sounded fine for my voice - just obviously not so good for a low sine wave.
// Of course, if like 60 samples are being processed at a time under CPU load, then this might end up as low as 40.
#define ANTI_CLICK_CROSSFADE_LENGTH 100

#define AUDIO_CLIP_DEFAULT_ATTACK_IF_PRE_MARGIN (7 * 85899345 - 2147483648)

enum class AudioRecordingFolder {
	CLIPS,
	RECORD,
	RESAMPLE,
};
constexpr auto NUM_AUDIO_RECORDING_FOLDERS = util::to_underlying(AudioRecordingFolder::RESAMPLE) + 1;

enum class KeyboardLayout : uint8_t {
	QWERTY,
	AZERTY,
	QWERTZ,
};
constexpr auto NUM_KEYBOARD_LAYOUTS = util::to_underlying(KeyboardLayout::QWERTZ) + 1;

#define INTERNAL_BUTTON_PRESS_LATENCY 380
#define MIDI_KEY_INPUT_LATENCY 100

#define LINEAR_RECORDING_EARLY_FIRST_NOTE_ALLOWANCE (100 * 44) // In samples

#define LOOP_LOW_LEVEL 1
#define LOOP_TIMESTRETCHER_LEVEL_IF_ACTIVE 2 // Will cause low-level looping if no time-stretching

#define INTERNAL_MEMORY_END 0x20300000
#define PROGRAM_STACK_MAX_SIZE 8192

enum StealableQueue {
	STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA,
	STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_CONVERTED, // E.g. from floating point file, or wrong endianness AIFF file.
	STEALABLE_QUEUE_NO_SONG_WAVETABLE_BAND_DATA,
	STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_REPITCHED_CACHE,
	STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_PERC_CACHE,
	STEALABLE_QUEUE_NO_SONG_AUDIO_FILE_OBJECTS,
	STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA,
	STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_CONVERTED,
	STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE,
	STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_PERC_CACHE, // This one is super valuable and compacted data - lots of work to load it all again
	NUM_STEALABLE_QUEUES,
};

#define UNDEFINED_GREY_SHADE 7

#define HAVE_SEQUENCE_STEP_CONTROL 1

enum class SequenceDirection {
	FORWARD,
	REVERSE,
	PINGPONG,
	OBEY_PARENT,
};
constexpr auto NUM_SEQUENCE_DIRECTION_OPTIONS = util::to_underlying(SequenceDirection::OBEY_PARENT);

enum class AudioFileType {
	SAMPLE,
	WAVETABLE,
};

#define WAVETABLE_MIN_CYCLE_SIZE                                                                                       \
	8 // Not 4 - because NE10 can't do FFTs that small unless we enable its additional C code, which would take up program size for little gain.
#define WAVETABLE_MAX_CYCLE_SIZE 65536 // TODO: work out what this should actually be.

#define MAX_IMAGE_STORE_WIDTH displayWidth

#define NUM_EXPRESSION_DIMENSIONS 3

#define EXPRESSION_X_PITCH_BEND 0
#define EXPRESSION_Y_SLIDE_TIMBRE 1
#define EXPRESSION_Z_PRESSURE 2

#define MIDI_CHANNEL_MPE_LOWER_ZONE 16
#define MIDI_CHANNEL_MPE_UPPER_ZONE 17
#define NUM_CHANNELS 18
#define MIDI_CHANNEL_NONE 255
#define IS_A_CC NUM_CHANNELS
// To be used instead of MIDI_CHANNEL_MPE_LOWER_ZONE etc for functions that require a "midi output filter". Although in
// fact, any number <16 or >=18 would work, the way I've defined it.
#define MIDI_OUTPUT_FILTER_NO_MPE 0

#define AUTOMATED_TESTER_ENABLED (0 && ALPHA_OR_BETA_VERSION)

// OLED -----------------
#define OLED_WIDTH_CHARS 16
#define OLED_MENU_NUM_OPTIONS_VISIBLE (OLED_HEIGHT_CHARS - 1)

#define CONSOLE_IMAGE_HEIGHT (OLED_MAIN_HEIGHT_PIXELS + 16)
#define CONSOLE_IMAGE_NUM_ROWS (CONSOLE_IMAGE_HEIGHT >> 3)

#define TEXT_SPACING_X 6
#define TEXT_SPACING_Y 9
#define TEXT_SIZE_Y_UPDATED 7

#define TEXT_TITLE_SPACING_X 9
#define TEXT_TITLE_SIZE_Y 10

#define TEXT_BIG_SPACING_X 11
#define TEXT_BIG_SIZE_Y 13

#define TEXT_HUGE_SPACING_X 18
#define TEXT_HUGE_SIZE_Y 20

#define OLED_ALLOW_LOWER_CASE 0

#define NOTE_FOR_DRUM 60

#define BEND_RANGE_MAIN 0
#define BEND_RANGE_FINGER_LEVEL 1

#define MIDI_CHARACTERISTIC_NOTE 0
#define MIDI_CHARACTERISTIC_CHANNEL 1

#define PLAYBACK_STOP_SHOULD_CLEAR_MONO_EXPRESSION 1

#define INDEPENDENT_NOTEROW_LENGTH_INCREASE_DOUBLE 0
#define INDEPENDENT_NOTEROW_LENGTH_INCREASE_ROUND_UP 1

// From FatFS - we need access to this:
#define DIR_FileSize 28 /* File size (DWORD) */

#define MAX_NUM_UINTS_TO_REP_ALL_PARAMS 2

#if HAVE_OLED
#define BROWSER_AND_MENU_NUM_LINES 3
#else
#define BROWSER_AND_MENU_NUM_LINES 1
#endif
