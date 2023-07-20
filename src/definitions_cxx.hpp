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
constexpr FirmwareVersion kCurrentFirmwareVersion = FIRMWARE_4P1P4_ALPHA;

struct Cartesian {
	int x;
	int y;
};

// Buttons / LEDs ---------------------------------------------------------------
// Note: (Kate) All LED coordinates were identical to their button counterparts, so they
// have been removed

constexpr Cartesian syncScalingButtonCoord = {7, 2};
constexpr Cartesian crossScreenEditButtonCoord = {6, 2};
constexpr Cartesian xEncButtonCoord = {0, 1};
constexpr Cartesian yEncButtonCoord = {0, 0};
constexpr Cartesian tempoEncButtonCoord = {4, 1};
constexpr Cartesian affectEntireButtonCoord = {3, 0};
constexpr Cartesian modEncoder0ButtonCoord = {0, 2};
constexpr Cartesian modEncoder1ButtonCoord = {0, 3};
constexpr Cartesian shiftButtonCoord = {8, 0};
constexpr Cartesian playButtonCoord = {8, 3};
constexpr Cartesian recordButtonCoord = {8, 2};
constexpr Cartesian clipViewButtonCoord = {3, 2};
constexpr Cartesian sessionViewButtonCoord = {3, 1};
constexpr Cartesian synthButtonCoord = {5, 0};
constexpr Cartesian kitButtonCoord = {5, 1};
constexpr Cartesian midiButtonCoord = {5, 2};
constexpr Cartesian cvButtonCoord = {5, 3};
constexpr Cartesian learnButtonCoord = {7, 0};
constexpr Cartesian tapTempoButtonCoord = {7, 3};
constexpr Cartesian saveButtonCoord = {6, 3};
constexpr Cartesian loadButtonCoord = {6, 1};
constexpr Cartesian scaleModeButtonCoord = {6, 0};
constexpr Cartesian keyboardButtonCoord = {3, 3};
constexpr Cartesian selectEncButtonCoord = {4, 3};
constexpr Cartesian backButtonCoord = {7, 1};
constexpr Cartesian tripletsButtonCoord = {8, 1};

constexpr int editPadPressBufferSize = 8;

constexpr int NUM_MOD_BUTTONS = 8;

constexpr auto displayHeight = 8;
constexpr auto displayHeightMagnitude = 3;
constexpr auto displayWidth = 16;
constexpr auto displayWidthMagnitude = 4;

constexpr int NO_PRESSES_HAPPENING_MESSAGE = 254;
constexpr int RESEND_BUTTON_STATES_MESSAGE = 22;
constexpr int NUM_BYTES_IN_COL_UPDATE_MESSAGE = 49;
constexpr int NUM_BYTES_IN_LONGEST_MESSAGE = 55;
constexpr int NUM_BYTES_IN_SIDEBAR_REDRAW = (NUM_BYTES_IN_COL_UPDATE_MESSAGE);
constexpr int PAD_AND_BUTTON_MESSAGES_END = 180;

constexpr int NUM_BYTES_IN_MAIN_PAD_REDRAW = (NUM_BYTES_IN_COL_UPDATE_MESSAGE * 8);

constexpr int DEFAULT_CLIP_LENGTH = 96; // You'll want to <<displayWidthMagnitude this each time used
constexpr int DEFAULT_ARRANGER_ZOOM = (DEFAULT_CLIP_LENGTH >> 1);

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

constexpr int sideBarWidth = 2;
constexpr int MAX_NUM_ANIMATED_ROWS = ((displayHeight * 3) >> 1);

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

constexpr int minTimePerTimerTick = 1;
constexpr int numInputTicksToAverageTime = 24;
constexpr int numInputTicksToAllowTempoTargeting = 24; // This is probably even high enough to cause audible glitches
constexpr int maxOutputTickMagnitude = 5;

constexpr int buttonDebounceTime = 100; // Milliseconds
constexpr int padDebounceTime = 50;     // Milliseconds
constexpr int colTime = 36;             // In 21.25 uS's (or did I mean nS?)
constexpr int zoomSpeed = 110;
constexpr int clipCollapseSpeed = 200;
constexpr int fadeSpeed = 300;
constexpr int flashLength = 3;

constexpr int horizontalSongSelectorSpeed = 90;
constexpr int noteRowCollapseSpeed = 150;
constexpr int greyoutSpeed = (300 * 44);

constexpr int initialFlashTime = 250;
constexpr int flashTime = 110;
constexpr int fastFlashTime = 60;
constexpr int SAMPLE_MARKER_BLINK_TIME = 200;

constexpr int USE_DEFAULT_VELOCITY = 255;

constexpr int MAX_SEQUENCE_LENGTH = 1610612736; // The biggest multiple of 3 which can fit in a signed 32-bit int
constexpr int noteOnLatenessAllowed = 2205;     // In audio samples. That's 50mS. Multiply mS by 44.1

constexpr int GATE_MODE_V_TRIG = 0;
constexpr int GATE_MODE_S_TRIG = 1;

constexpr int numSongSlots = 1000;
constexpr int numInstrumentSlots = 1000;
constexpr int maxNumInstrumentPresets = 128;
constexpr int FILENAME_BUFFER_SIZE =
    256; // Don't ever make this less! The zoom rendering code uses this buffer for its stuff

enum class InstrumentType : uint8_t {
	SYNTH,
	KIT,
	MIDI_OUT,
	CV,
	AUDIO,
	NONE = 255,
};

enum class ThingType {
	SYNTH,
	KIT,
	SONG,
	NONE,
};

// Maximum num samples that may be processed in one "frame". Actual size of output buffer is in ssi.h
constexpr int audioEngineBufferSize = 128;

constexpr int modFXBufferSize = 512;
constexpr int modFXBufferIndexMask = (modFXBufferSize - 1);
constexpr int modFXMaxDelay = ((modFXBufferSize - 1) << 16);
constexpr int flangerMinTime = (3 << 16);
constexpr int flangerAmplitude = (modFXMaxDelay - flangerMinTime);
constexpr int flangerOffset = ((modFXMaxDelay + flangerMinTime) >> 1);

constexpr int numEnvelopes = 2;
constexpr int numLFOs = 2;
constexpr int numModulators = 2;

constexpr int maxNumUnison = 8;
constexpr int NUM_VOICES_STATIC = 24;
constexpr int NUM_VOICE_SAMPLES_STATIC = 20;
constexpr int NUM_TIME_STRETCHERS_STATIC = 6;
constexpr int maxNumNoteOnsPending = 64;

constexpr int NUM_UINTS_TO_REP_PATCH_CABLES = 1;
constexpr int MAX_NUM_PATCH_CABLES = (NUM_UINTS_TO_REP_PATCH_CABLES * 32);

enum class EnvelopeStage : uint8_t {
	ATTACK,
	DECAY,
	SUSTAIN,
	RELEASE,
	FAST_RELEASE,
	OFF,
};
constexpr int NUM_ENVELOPE_STAGES = util::to_underlying(EnvelopeStage::OFF) + 1;

constexpr int NUM_PRIORITY_OPTIONS = 3;

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
constexpr int kNumPatchSources = static_cast<int>(kLastPatchSource);

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

constexpr int FIRST_LOCAL_NON_VOLUME_PARAM = 6;
constexpr int FIRST_LOCAL_HYBRID_PARAM = 14;
constexpr int FIRST_LOCAL_EXP_PARAM = 19;

constexpr int FIRST_GLOBAL_PARAM = 33;
constexpr int FIRST_GLOBAL_NON_VOLUME_PARAM = 37;
constexpr int FIRST_GLOBAL_HYBRID_PARAM = 38;
constexpr int FIRST_GLOBAL_EXP_PARAM = 38;
#define NUM_PARAMS 42 // Not including the "none" param

constexpr int PARAM_PLACEHOLDER_RANGE = 89; // Not a real param. For the purpose of reading old files from before V3.2.0

constexpr int PARAM_UNPATCHED_SECTION = 90;

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

constexpr int KIT_SIDECHAIN_SHAPE = (-601295438);

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

constexpr int SAMPLE_MAX_TRANSPOSE = 24;
constexpr int SAMPLE_MIN_TRANSPOSE = (-96);

enum WavFormat {
	WAV_FORMAT_PCM = 1,
	WAV_FORMAT_FLOAT = 3,
};

enum class PolyphonyMode {
	AUTO,
	POLY,
	MONO,
	LEGATO,
	CHOKE,
};

constexpr auto kNumPolyphonyModes = util::to_underlying(PolyphonyMode::CHOKE) + 1;

constexpr int NUMERIC_DISPLAY_LENGTH = 4;

constexpr int MAX_NUM_SECTIONS = 12;

constexpr int NUM_PHYSICAL_MOD_KNOBS = 2;

enum class LPFMode {
	TRANSISTOR_12DB,
	TRANSISTOR_24DB,
	TRANSISTOR_24DB_DRIVE,
	SVF,
};
constexpr auto NUM_LPF_MODES = util::to_underlying(LPFMode::SVF) + 1;

constexpr int PHASER_NUM_ALLPASS_FILTERS = 6;

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

constexpr int NUM_SOURCES = 2; // That's sources as in oscillators - within a Sound (synth).

constexpr int PIC_MESSAGE_REFRESH_TIME = 19;

enum class ArpMode {
	OFF,
	UP,
	DOWN,
	BOTH,
	RANDOM,
};
constexpr auto NUM_ARP_MODES = util::to_underlying(ArpMode::RANDOM) + 1;

#define ALLOW_SPAM_MODE 0 // For debugging I think?

constexpr int KEYBOARD_ROW_INTERVAL_MAX = 16;

enum class ModFXParam {
	DEPTH,
	FEEDBACK,
	OFFSET,
};
constexpr auto NUM_MOD_FX_PARAMS = util::to_underlying(ModFXParam::OFFSET) + 1;

constexpr int PATCH_CABLE_ACCEPTANCE_YET_T0_BE_DETERMINED = 3;
constexpr int PATCH_CABLE_ACCEPTANCE_ALLOWED = 2;
constexpr int PATCH_CABLE_ACCEPTANCE_EDITABLE = 1;
constexpr int PATCH_CABLE_ACCEPTANCE_DISALLOWED = 0;

constexpr int DOUBLE_TAP_MS = 400;

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

enum class MIDITakeoverMode : uint8_t {
	JUMP,
	PICKUP,
	SCALE,
};
constexpr auto NUM_MIDI_TAKEOVER_MODES = util::to_underlying(MIDITakeoverMode::SCALE) + 1;

constexpr int NUM_CLUSTERS_LOADED_AHEAD = 2;

enum class InputMonitoringMode : uint8_t {
	SMART,
	ON,
	OFF,
};
constexpr auto NUM_INPUT_MONITORING_MODES = util::to_underlying(InputMonitoringMode::OFF) + 1;

enum ClusterLoad {
	CLUSTER_DONT_LOAD,
	CLUSTER_ENQUEUE,
	CLUSTER_LOAD_IMMEDIATELY,
	CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE,
};

enum class ScaleType {
	SCALE,
	CHROMATIC,
	KIT,
};

enum class ArmState {
	OFF,
	ON_NORMAL, // Arming to stop or start normally, or to stop soloing
	ON_TO_SOLO,
};

#define ALPHA_OR_BETA_VERSION 1 // Whether to compile with additional error-checking

constexpr int NUM_PROBABILITY_VALUES = 20;
constexpr int DEFAULT_LIFT_VALUE = 64;

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

constexpr int NAVIGATION_CLIP = 0;
constexpr int NAVIGATION_ARRANGEMENT = 1;

enum PresetSearch {
	PRESET_SEARCH_ALL,
	PRESET_SEARCH_NOT_ACTIVE_IN_SESSION,
	PRESET_SEARCH_NOT_ACTIVE_IN_ARRANGEMENT,
};

enum class Availability {
	ANY,
	INSTRUMENT_AVAILABLE_IN_SESSION,
	INSTRUMENT_UNUSED,
};

enum TimeType {
	BEFORE = 0,
	AFTER = 1,
};

enum class ExistenceChangeType {
	CREATE = 0,
	DELETE = 1,
};

constexpr int CC_NUMBER_PITCH_BEND = 120;
constexpr int CC_NUMBER_AFTERTOUCH = 121;
constexpr int CC_NUMBER_NONE = 122;
constexpr int NUM_CC_NUMBERS_INCLUDING_FAKE = 123;
constexpr int NUM_REAL_CC_NUMBERS = 120;

enum class InstrumentRemoval {
	NONE,
	DELETE_OR_HIBERNATE_IF_UNUSED,
	DELETE,
};

constexpr int HARDWARE_TEST_MODE = 0;

enum class DrumType {
	SOUND,
	MIDI,
	GATE,
};

enum class PgmChangeSend {
	NEVER,
	ONCE,
};

enum class MarkerType {
	NOT_AVAILABLE = -2,
	NONE = -1,
	START,
	LOOP_START,
	LOOP_END,
	END,
};
constexpr int kNumMarkerTypes = util::to_underlying(MarkerType::END) + 1;

enum class InterpolationMode {
	LINEAR,
	SMOOTH,
};
constexpr int NUM_INTERPOLATION_MODES = 2;


constexpr int CACHE_BYTE_DEPTH = 3;
constexpr int CACHE_BYTE_DEPTH_MAGNITUDE = 2; // Invalid / unused for odd numbers of bytes like 3

constexpr int MAX_UNISON_DETUNE = 50;

constexpr int PARAM_STATIC_COMPRESSOR_ATTACK = 162;
constexpr int PARAM_STATIC_COMPRESSOR_RELEASE = 163;

// Only used for the reverb compressor. Normally this is done with patching
constexpr int PARAM_STATIC_COMPRESSOR_VOLUME = 164;
constexpr int PARAM_STATIC_PATCH_CABLE = 190; // Special case

// This is about right. Making it smaller didn't help. Tried it as 9, and I'm pretty sure some fast percussive details were lost in the output
constexpr int PERC_BUFFER_REDUCTION_MAGNITUDE = 7;
constexpr int PERC_BUFFER_REDUCTION_SIZE = (1 << PERC_BUFFER_REDUCTION_MAGNITUDE);
constexpr int DIFFERENCE_LPF_POLES = 2;

constexpr int INTERPOLATION_MAX_NUM_SAMPLES = 16;
constexpr int INTERPOLATION_MAX_NUM_SAMPLES_MAGNITUDE = 4;

enum class ClusterType {
	EMPTY,
	Sample,
	GENERAL_MEMORY,
	SAMPLE_CACHE,
	PERC_CACHE_FORWARDS,
	PERC_CACHE_REVERSED,
	OTHER,
};

enum PlayHead {
	PLAY_HEAD_OLDER,
	PLAY_HEAD_NEWER,
};

constexpr int INPUT_RAW_BUFFER_SIZE = 8192;
constexpr int INPUT_REPITCHED_BUFFER_SIZE = 2048;
constexpr int INPUT_PERC_BUFFER_SIZE = (INPUT_RAW_BUFFER_SIZE >> PERC_BUFFER_REDUCTION_MAGNITUDE);

// Experimental, from when developing input pitch shifting. Probably won't actually work now, if it ever did!
#define INPUT_ENABLE_REPITCHED_BUFFER 0

constexpr int TIME_STRETCH_DEFAULT_FIRST_HOP_LENGTH = 200;

// 3 sounds way better than 2. After that, kinda diminishing returns
constexpr int TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES = 3;

// Anywhere between 30 and 40 seemed ideal. Point of interest - high numbers (e.g. I tried 140) screw up the high notes, so more is not more!
constexpr int TIME_STRETCH_CROSSFADE_MOVING_AVERAGE_LENGTH = 35;

// I think this was an experimental mode which allowed the pitch-change effect (i.e. windowed sinc interpolation) to be
// stored and reused between the two time-stretch play-heads. Probably won't work anymore. From memory, wasn't very
// beneficial, especially relative to its complexity and potential bugginess.
#define TIME_STRETCH_ENABLE_BUFFER 0

constexpr int TIME_STRETCH_BUFFER_SIZE = TIME_STRETCH_ENABLE_BUFFER ? 4096 : 256;

// We don't want the window too short, or some sounds / harmonics can be missed during the attack
constexpr int PITCH_DETECT_WINDOW_SIZE_MAGNITUDE = 13;
constexpr int PITCH_DETECT_WINDOW_SIZE = (1 << PITCH_DETECT_WINDOW_SIZE_MAGNITUDE);

constexpr int MAX_FILE_SIZE = 1073741824;

constexpr int QWERTY_HOME_ROW = 3;

constexpr int AUDIO_RECORD_LAG_COMPENTATION = 294;

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

enum class ActionResult {
	DEALT_WITH,
	NOT_DEALT_WITH,
	REMIND_ME_OUTSIDE_CARD_ROUTINE,
	ACTIONED_AND_CAUSED_CHANGE,
};

constexpr int ENABLE_CLIP_CUTTING_DIAGNOSTICS = 1;

constexpr int AUDIO_CLIP_MARGIN_SIZE_POST_END = 2048;

// Let's just do a 100 sample crossfade. Even 12 samples actually sounded fine for my voice - just obviously not so good for a low sine wave.
// Of course, if like 60 samples are being processed at a time under CPU load, then this might end up as low as 40.
constexpr int ANTI_CLICK_CROSSFADE_LENGTH = 100;

constexpr int AUDIO_CLIP_DEFAULT_ATTACK_IF_PRE_MARGIN = (7 * 85899345 - 2147483648);

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

constexpr int INTERNAL_BUTTON_PRESS_LATENCY = 380;
constexpr int MIDI_KEY_INPUT_LATENCY = 100;

constexpr int LINEAR_RECORDING_EARLY_FIRST_NOTE_ALLOWANCE = (100 * 44); // In samples;

constexpr int LOOP_LOW_LEVEL = 1;
constexpr int LOOP_TIMESTRETCHER_LEVEL_IF_ACTIVE = 2; // Will cause low-level looping if no time-stretching;

constexpr int INTERNAL_MEMORY_END = 0x20300000;
constexpr int PROGRAM_STACK_MAX_SIZE = 8192;

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

constexpr int UNDEFINED_GREY_SHADE = 7;

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

// Not 4 - because NE10 can't do FFTs that small unless we enable its additional C code, which would take up program size for little gain.
constexpr int WAVETABLE_MIN_CYCLE_SIZE = 8;
constexpr int WAVETABLE_MAX_CYCLE_SIZE = 65536; // TODO: work out what this should actually be.

constexpr int MAX_IMAGE_STORE_WIDTH = displayWidth;

constexpr int NUM_EXPRESSION_DIMENSIONS = 3;

constexpr int EXPRESSION_X_PITCH_BEND = 0;
constexpr int EXPRESSION_Y_SLIDE_TIMBRE = 1;
constexpr int EXPRESSION_Z_PRESSURE = 2;

constexpr int MIDI_CHANNEL_MPE_LOWER_ZONE = 16;
constexpr int MIDI_CHANNEL_MPE_UPPER_ZONE = 17;
constexpr int NUM_CHANNELS = 18;
constexpr int MIDI_CHANNEL_NONE = 255;
constexpr int IS_A_CC = NUM_CHANNELS;
// To be used instead of MIDI_CHANNEL_MPE_LOWER_ZONE etc for functions that require a "midi output filter". Although in
// fact, any number <16 or >=18 would work, the way I've defined it.
constexpr int MIDI_OUTPUT_FILTER_NO_MPE = 0;

#define AUTOMATED_TESTER_ENABLED (0 && ALPHA_OR_BETA_VERSION)

// OLED -----------------
constexpr int OLED_WIDTH_CHARS = 16;
constexpr int OLED_MENU_NUM_OPTIONS_VISIBLE = (OLED_HEIGHT_CHARS - 1);

constexpr int CONSOLE_IMAGE_HEIGHT = (OLED_MAIN_HEIGHT_PIXELS + 16);
constexpr int CONSOLE_IMAGE_NUM_ROWS = (CONSOLE_IMAGE_HEIGHT >> 3);

constexpr int TEXT_SPACING_X = 6;
constexpr int TEXT_SPACING_Y = 9;
constexpr int TEXT_SIZE_Y_UPDATED = 7;

constexpr int TEXT_TITLE_SPACING_X = 9;
constexpr int TEXT_TITLE_SIZE_Y = 10;

constexpr int TEXT_BIG_SPACING_X = 11;
constexpr int TEXT_BIG_SIZE_Y = 13;

constexpr int TEXT_HUGE_SPACING_X = 18;
constexpr int TEXT_HUGE_SIZE_Y = 20;

constexpr int OLED_ALLOW_LOWER_CASE = 0;

constexpr int NOTE_FOR_DRUM = 60;

enum BendRange {
	BEND_RANGE_MAIN,
	BEND_RANGE_FINGER_LEVEL,
};

enum class MIDICharacteristic {
	NOTE,
	CHANNEL,
};

constexpr int PLAYBACK_STOP_SHOULD_CLEAR_MONO_EXPRESSION = 1;

enum class IndependentNoteRowLengthIncrease {
	DOUBLE,
	ROUND_UP,
};

// From FatFS - we need access to this:
constexpr int DIR_FileSize = 28 /* File size (DWORD) */;

constexpr int MAX_NUM_UINTS_TO_REP_ALL_PARAMS = 2;

#if HAVE_OLED
constexpr int BROWSER_AND_MENU_NUM_LINES = 3;
#else
constexpr int BROWSER_AND_MENU_NUM_LINES = 1;
#endif
