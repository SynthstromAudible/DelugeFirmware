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
#include "util/misc.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#define ALPHA_OR_BETA_VERSION 1 // Whether to compile with additional error-checking

#define HARDWARE_TEST_MODE 0

#define AUTOMATED_TESTER_ENABLED (0 && ALPHA_OR_BETA_VERSION)

#define ALLOW_SPAM_MODE 0 // For debugging (in buttons.cpp, audio_engine.cpp, deluge.cpp)

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

#define SD_TEST_MODE_ENABLED 0
#define SD_TEST_MODE_ENABLED_LOAD_SONGS 0
#define SD_TEST_MODE_ENABLED_SAVE_SONGS 0
#define UNDO_REDO_TEST_ENABLED 0
#define RECORDING_TEST_ENABLED 0
#define AUTOPILOT_TEST_ENABLED 0
#define LAUNCH_CLIP_TEST_ENABLED 0

#define PLAYBACK_STOP_SHOULD_CLEAR_MONO_EXPRESSION 1

#define HAVE_SEQUENCE_STEP_CONTROL 1

#define ENABLE_CLIP_CUTTING_DIAGNOSTICS 1

#define PITCH_DETECT_DEBUG_LEVEL 0

struct SemVer {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	// NOTE: below needs C++20
	//auto operator<=>(const SemVer &) const = default
};
constexpr SemVer kCommunityFirmwareVersion{1, 0, 0};

// FIXME: These need to be nuked and all references in the codebase removed in prep for the Community Firmware v1.0.0 release
// correspondingly, we should probably we storing the semver version in three bytes in the flash rather than trying to compress
// it all to one (see above class)
enum FirmwareVersion : uint8_t {
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

constexpr uint8_t kOctaveSize = 12;

struct Cartesian {
	int32_t x;
	int32_t y;
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

constexpr int32_t kEditPadPressBufferSize = 8;

constexpr int32_t kNumModButtons = 8;

// Display information
constexpr int32_t kDisplayHeight = 8;
constexpr int32_t kDisplayHeightMagnitude = 3;
constexpr int32_t kDisplayWidth = 16;
constexpr int32_t kDisplayWidthMagnitude = 4;

enum PICMessage : uint8_t {
	REFRESH_TIME = 19,
	RESEND_BUTTON_STATES = 22,
	NO_PRESSES_HAPPENING = 254,
};

constexpr int32_t kPadAndButtonMessagesEnd = 180;

constexpr int32_t kNumBytesInColUpdateMessage = 49;
constexpr int32_t kNumBytesInLongestMessage = 55;

constexpr int32_t kNumBytesInSidebarRedraw = (kNumBytesInColUpdateMessage);

constexpr int32_t kNumBytesInMainPadRedraw = (kNumBytesInColUpdateMessage * 8);

constexpr int32_t kDefaultClipLength = 96; // You'll want to <<displayWidthMagnitude this each time used
constexpr int32_t kDefaultArrangerZoom = (kDefaultClipLength >> 1);

struct Pin {
	uint8_t port;
	uint8_t pin;
};

constexpr Pin LINE_OUT_DETECT_L = {6, 3};
constexpr Pin LINE_OUT_DETECT_R = {6, 4};
constexpr Pin ANALOG_CLOCK_IN = {1, 14};
constexpr Pin SPEAKER_ENABLE = {4, 1};
constexpr Pin HEADPHONE_DETECT = {6, 5};
constexpr Pin LINE_IN = {6, 6};
constexpr Pin MIC = {7, 9};
constexpr Pin SYNCED_LED = {6, 7};

constexpr Pin BATTERY_LED = {1, 1};
constexpr int32_t SYS_VOLT_SENSE_PIN = 5;

constexpr int32_t kSideBarWidth = 2;
constexpr int32_t kMaxNumAnimatedRows = ((kDisplayHeight * 3) >> 1);

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

constexpr size_t kMinTimePerTimerTick = 1;
constexpr int32_t kNumInputTicksToAverageTime = 24;
constexpr int32_t kNumInputTicksToAllowTempoTargeting =
    24; // This is probably even high enough to cause audible glitches
constexpr int32_t kMaxOutputTickMagnitude = 5;

constexpr int32_t kZoomSpeed = 110;
constexpr int32_t kClipCollapseSpeed = 200;
constexpr int32_t kFadeSpeed = 300;

constexpr int32_t kNoteRowCollapseSpeed = 150;
constexpr int32_t kGreyoutSpeed = (300 * 44);

constexpr int32_t kInitialFlashTime = 250;
constexpr int32_t kFlashTime = 110;
constexpr int32_t kFastFlashTime = 60;
constexpr int32_t kSampleMarkerBlinkTime = 200;

constexpr int32_t USE_DEFAULT_VELOCITY = 255;

constexpr int32_t kMaxSequenceLength = 1610612736; // The biggest multiple of 3 which can fit in a signed 32-bit int32_t
constexpr int32_t kAmountNoteOnLatenessAllowed = 2205; // In audio samples. That's 50mS. Multiply mS by 44.1

enum class GateType : uint8_t {
	V_TRIG,
	S_TRIG,
	SPECIAL,
};

constexpr int32_t kNumSongSlots = 1000;
constexpr int32_t kNumInstrumentSlots = 1000;

// Don't ever make this less! The zoom rendering code uses this buffer for its stuff
constexpr size_t kFilenameBufferSize = 256;

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

constexpr int32_t kModFXBufferSize = 512;
constexpr int32_t kModFXBufferIndexMask = (kModFXBufferSize - 1);
constexpr int32_t kModFXMaxDelay = ((kModFXBufferSize - 1) << 16);

constexpr int32_t kFlangerMinTime = (3 << 16);
constexpr int32_t kFlangerAmplitude = (kModFXMaxDelay - kFlangerMinTime);
constexpr int32_t kFlangerOffset = ((kModFXMaxDelay + kFlangerMinTime) >> 1);

constexpr int32_t kNumEnvelopes = 2;
constexpr int32_t kNumLFOs = 2;
constexpr int32_t kNumModulators = 2;

constexpr int32_t kMaxNumVoicesUnison = 8;

// TODO: Investigate whether we can move static voices to dynamic allocation and remove these
constexpr int32_t kNumVoicesStatic = 24;
constexpr int32_t kNumVoiceSamplesStatic = 20;
constexpr int32_t kNumTimeStretchersStatic = 6;

constexpr int32_t kMaxNumNoteOnsPending = 64;

constexpr int32_t kNumUnsignedIntegersToRepPatchCables = 1;
constexpr int32_t kMaxNumPatchCables = (kNumUnsignedIntegersToRepPatchCables * 32);

enum class EnvelopeStage : uint8_t {
	ATTACK,
	DECAY,
	SUSTAIN,
	RELEASE,
	FAST_RELEASE,
	OFF,
};
constexpr int32_t kNumEnvelopeStages = util::to_underlying(EnvelopeStage::OFF) + 1;

enum class VoicePriority : uint8_t {
	LOW,
	MEDIUM,
	HIGH,
};

constexpr size_t kNumVoicePriorities = util::to_underlying(VoicePriority::HIGH) + 1;

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
constexpr int32_t kNumPatchSources = static_cast<int32_t>(kLastPatchSource);

//constexpr PatchSource kFirstGlobalSourceWithChangedStatusAutomaticallyUpdated = PatchSource::ENVELOPE_0;
constexpr PatchSource kFirstLocalSource = PatchSource::ENVELOPE_0;
//constexpr PatchSource kFirstUnchangeableSource = PatchSource::VELOCITY;

// Linear params have different sources multiplied together, then multiplied by the neutral value
// -- and "volume" ones get squared at the end

// Hybrid params have different sources added together, then added to the neutral value

// Exp params have different sources added together, converted to an exponential scale, then multiplied by the neutral value

using ParamType = uint8_t;

namespace Param {
namespace Local {
enum : ParamType {
	// Local linear params begin
	OSC_A_VOLUME,
	OSC_B_VOLUME,
	VOLUME,
	NOISE_VOLUME,
	MODULATOR_0_VOLUME,
	MODULATOR_1_VOLUME,

	// Local non-volume params begin
	MODULATOR_0_FEEDBACK,
	MODULATOR_1_FEEDBACK,
	CARRIER_0_FEEDBACK,
	CARRIER_1_FEEDBACK,
	LPF_RESONANCE,
	HPF_RESONANCE,
	ENV_0_SUSTAIN,
	ENV_1_SUSTAIN,

	// Local hybrid params begin
	OSC_A_PHASE_WIDTH,
	OSC_B_PHASE_WIDTH,
	OSC_A_WAVE_INDEX,
	OSC_B_WAVE_INDEX,
	PAN,

	// Local exp params begin
	LPF_FREQ,
	PITCH_ADJUST,
	OSC_A_PITCH_ADJUST,
	OSC_B_PITCH_ADJUST,
	MODULATOR_0_PITCH_ADJUST,
	MODULATOR_1_PITCH_ADJUST,
	HPF_FREQ,
	LFO_LOCAL_FREQ,
	ENV_0_ATTACK,
	ENV_1_ATTACK,
	ENV_0_DECAY,
	ENV_1_DECAY,
	ENV_0_RELEASE,
	ENV_1_RELEASE,

	LAST,
};
constexpr ParamType FIRST_NON_VOLUME = MODULATOR_0_FEEDBACK;
constexpr ParamType FIRST_HYBRID = OSC_A_PHASE_WIDTH;
constexpr ParamType FIRST_EXP = LPF_FREQ;
} // namespace Local

namespace Global {
enum : ParamType {
	// Global (linear) params begin
	VOLUME_POST_FX = Local::LAST,
	VOLUME_POST_REVERB_SEND,
	REVERB_AMOUNT,
	MOD_FX_DEPTH,

	// Global non-volume params begin
	DELAY_FEEDBACK,

	// Global hybrid params begin
	// Global exp params begin
	DELAY_RATE,
	MOD_FX_RATE,
	LFO_FREQ,
	ARP_RATE,
	// ANY TIME YOU UPDATE THIS LIST! CHANGE Sound::paramToString()

	NONE,
};
constexpr ParamType FIRST = Global::VOLUME_POST_FX;
constexpr ParamType FIRST_NON_VOLUME = Global::DELAY_FEEDBACK;
constexpr ParamType FIRST_HYBRID = Global::DELAY_RATE;
constexpr ParamType FIRST_EXP = Global::DELAY_RATE;
} // namespace Global

constexpr ParamType PLACEHOLDER_RANGE = 89; // Not a real param. For the purpose of reading old files from before V3.2.0
namespace Unpatched {
constexpr ParamType START = 90;

enum Shared : ParamType {
	// For all ModControllables
	STUTTER_RATE,
	BASS,
	TREBLE,
	BASS_FREQ,
	TREBLE_FREQ,
	SAMPLE_RATE_REDUCTION,
	BITCRUSHING,
	MOD_FX_OFFSET,
	MOD_FX_FEEDBACK,
	COMPRESSOR_SHAPE,
	// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp
	NUM_SHARED,
};

// Just for Sounds
namespace Sound {
enum : ParamType {
	ARP_GATE = Param::Unpatched::NUM_SHARED,
	PORTAMENTO,
	// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp
	MAX_NUM,
};
}

// Just for GlobalEffectables
namespace GlobalEffectable {
enum : ParamType {
	MOD_FX_RATE = Param::Unpatched::NUM_SHARED,
	MOD_FX_DEPTH,
	DELAY_RATE,
	DELAY_AMOUNT,
	PAN,
	LPF_FREQ,
	LPF_RES,
	HPF_FREQ,
	HPF_RES,
	REVERB_SEND_AMOUNT,
	VOLUME,
	SIDECHAIN_VOLUME,
	PITCH_ADJUST,
	MAX_NUM,
};
}
} // namespace Unpatched

namespace Static {
constexpr ParamType START = 162;

enum : ParamType {
	COMPRESSOR_ATTACK = START,
	COMPRESSOR_RELEASE,

	// Only used for the reverb compressor. Normally this is done with patching
	COMPRESSOR_VOLUME,
	PATCH_CABLE = 190, // Special case
};
} // namespace Static
static_assert(std::max<ParamType>(Unpatched::GlobalEffectable::MAX_NUM, Unpatched::Sound::MAX_NUM) + Unpatched::START
                  < Static::START,
              "Error: Too many Param::Unpatched (collision with Param::Static)");

} // namespace Param

//None is the last global param, 0 indexed so it's also the number of real params
constexpr ParamType kNumParams = util::to_underlying(Param::Global::NONE);
constexpr ParamType kMaxNumUnpatchedParams = Param::Unpatched::GlobalEffectable::MAX_NUM;

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

constexpr OscType kLastRingmoddableOscType = OscType::WAVETABLE;
constexpr int32_t kNumOscTypesRingModdable = util::to_underlying(kLastRingmoddableOscType) + 1;
constexpr int32_t kNumOscTypes = util::to_underlying(OscType::INPUT_STEREO) + 1;

enum class LFOType {
	SINE,
	TRIANGLE,
	SQUARE,
	SAW,
	SAMPLE_AND_HOLD,
	RANDOM_WALK,
};

constexpr int32_t kNumLFOTypes = util::to_underlying(LFOType::RANDOM_WALK);

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
constexpr int kNumSynthModes = util::to_underlying(SynthMode::RINGMOD) + 1;

enum class ModFXType {
	NONE,
	FLANGER,
	CHORUS,
	PHASER,
	CHORUS_STEREO,
};

constexpr int32_t kNumModFXTypes = util::to_underlying(ModFXType::CHORUS_STEREO) + 1;

constexpr int32_t SAMPLE_MAX_TRANSPOSE = 24;
constexpr int32_t SAMPLE_MIN_TRANSPOSE = (-96);

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

constexpr int32_t kNumericDisplayLength = 4;

constexpr int32_t kMaxNumSections = 12;

constexpr int32_t kNumPhysicalModKnobs = 2;

enum class LPFMode {
	TRANSISTOR_12DB,
	TRANSISTOR_24DB,
	TRANSISTOR_24DB_DRIVE, //filter logic relies on ladders being first and contiguous
	SVF,
	OFF, //Keep last as a sentinel. Signifies that the filter is not on, used for filter reset logic
};
constexpr LPFMode kLastLadder = LPFMode::TRANSISTOR_24DB_DRIVE;
//Off is not an LPF mode but is used to reset filters
constexpr int32_t kNumLPFModes = util::to_underlying(LPFMode::OFF);

constexpr int32_t kNumAllpassFiltersPhaser = 6;

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
constexpr auto kNumRepeatModes = util::to_underlying(SampleRepeatMode::STRETCH) + 1;

enum class FilterType {
	LPF,
	HPF,
	EQ,
};
constexpr auto kNumFilterTypes = util::to_underlying(FilterType::EQ) + 1;

constexpr int32_t kNumSources = 2; // That's sources as in oscillators - within a Sound (synth).

enum class ArpMode {
	OFF,
	UP,
	DOWN,
	BOTH,
	RANDOM,
};
constexpr auto kNumArpModes = util::to_underlying(ArpMode::RANDOM) + 1;

enum class ModFXParam {
	DEPTH,
	FEEDBACK,
	OFFSET,
};
constexpr auto kNumModFXParams = util::to_underlying(ModFXParam::OFFSET) + 1;

enum class PatchCableAcceptance {
	DISALLOWED,
	EDITABLE,
	ALLOWED,
	YET_TO_BE_DETERMINED,
};

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
constexpr auto kNumGlobalMIDICommands = util::to_underlying(GlobalMIDICommand::REDO) + 1;

enum class MIDITakeoverMode : uint8_t {
	JUMP,
	PICKUP,
	SCALE,
};
constexpr auto kNumMIDITakeoverModes = util::to_underlying(MIDITakeoverMode::SCALE) + 1;

constexpr int32_t kNumClustersLoadedAhead = 2;

enum class InputMonitoringMode : uint8_t {
	SMART,
	ON,
	OFF,
};
constexpr auto kNumInputMonitoringModes = util::to_underlying(InputMonitoringMode::OFF) + 1;

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

constexpr int32_t kNumProbabilityValues = 20;
constexpr int32_t kDefaultLiftValue = 64;

enum Navigation {
	NAVIGATION_CLIP,
	NAVIGATION_ARRANGEMENT,
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

enum CCNumber {
	CC_NUMBER_PITCH_BEND = 120,
	CC_NUMBER_AFTERTOUCH = 121,
	CC_NUMBER_NONE = 122,
};
constexpr int32_t kNumCCNumbersIncludingFake = 123;
constexpr int32_t kNumRealCCNumbers = 120;

enum class InstrumentRemoval {
	NONE,
	DELETE_OR_HIBERNATE_IF_UNUSED,
	DELETE,
};

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
constexpr int32_t kNumMarkerTypes = util::to_underlying(MarkerType::END) + 1;

enum class InterpolationMode {
	LINEAR,
	SMOOTH,
};
constexpr int32_t kNumInterpolationModes = 2;

constexpr int32_t kCacheByteDepth = 3;
constexpr int32_t kCacheByteDepthMagnitude = 2; // Invalid / unused for odd numbers of bytes like 3

constexpr int32_t kMaxUnisonDetune = 50;
constexpr int32_t kMaxUnisonStereoSpread = 50;

// This is about right. Making it smaller didn't help. Tried it as 9, and I'm pretty sure some fast percussive details were lost in the output
constexpr int32_t kPercBufferReductionMagnitude = 7;
constexpr int32_t kPercBufferReductionSize = (1 << kPercBufferReductionMagnitude);
constexpr int32_t kDifferenceLPFPoles = 2;

constexpr int32_t kInterpolationMaxNumSamples = 16;
constexpr int32_t kInterpolationMaxNumSamplesMagnitude = 4;

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

constexpr int32_t kInputRawBufferSize = 8192;
constexpr int32_t kInputRepitchedBufferSize = 2048;
constexpr int32_t kInputPercBufferSize = (kInputRawBufferSize >> kPercBufferReductionMagnitude);

// Experimental, from when developing input pitch shifting. Probably won't actually work now, if it ever did!
#define INPUT_ENABLE_REPITCHED_BUFFER 0

// I think this was an experimental mode which allowed the pitch-change effect (i.e. windowed sinc interpolation) to be
// stored and reused between the two time-stretch play-heads. Probably won't work anymore. From memory, wasn't very
// beneficial, especially relative to its complexity and potential bugginess.
#define TIME_STRETCH_ENABLE_BUFFER 0

namespace TimeStretch {
constexpr int32_t kDefaultFirstHopLength = 200;

namespace Crossfade {

// 3 sounds way better than 2. After that, kinda diminishing returns
constexpr int32_t kNumMovingAverages = 3;

// Anywhere between 30 and 40 seemed ideal. Point of interest - high numbers (e.g. I tried 140) screw up the high notes, so more is not more!
constexpr int32_t kMovingAverageLength = 35;

} // namespace Crossfade

constexpr int32_t kBufferSize = TIME_STRETCH_ENABLE_BUFFER ? 4096 : 256;
} // namespace TimeStretch

// We don't want the window too short, or some sounds / harmonics can be missed during the attack
constexpr int32_t kPitchDetectWindowSizeMagnitude = 13;
constexpr int32_t kPitchDetectWindowSize = (1 << kPitchDetectWindowSizeMagnitude);

constexpr int32_t kMaxFileSize = 0x40000000;

constexpr int32_t kQwertyHomeRow = 3;

constexpr int32_t kAudioRecordLagCompensation = 294;

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

constexpr int32_t kAudioClipMarginSizePostEnd = 2048;

// Let's just do a 100 sample crossfade. Even 12 samples actually sounded fine for my voice - just obviously not so good for a low sine wave.
// Of course, if like 60 samples are being processed at a time under CPU load, then this might end up as low as 40.
constexpr int32_t kAntiClickCrossfadeLength = 100;

constexpr int32_t kAudioClipDefaultAttackIfPreMargin = (7 * 85899345 - 2147483648);

enum class AudioRecordingFolder {
	CLIPS,
	RECORD,
	RESAMPLE,
};
constexpr auto kNumAudioRecordingFolders = util::to_underlying(AudioRecordingFolder::RESAMPLE) + 1;

enum class KeyboardLayout : uint8_t {
	QWERTY,
	AZERTY,
	QWERTZ,
};
constexpr auto kNumKeyboardLayouts = util::to_underlying(KeyboardLayout::QWERTZ) + 1;

constexpr int32_t kInternalButtonPressLatency = 380;
constexpr int32_t kMIDIKeyInputLatency = 100;

constexpr int32_t kLinearRecordingEarlyFirstNoteAllowance = (100 * 44); // In samples;

enum class LoopType {
	NONE,
	LOW_LEVEL,
	TIMESTRETCHER_LEVEL_IF_ACTIVE, // Will cause low-level looping if no time-stretching;
};

constexpr int32_t kInternalMemoryEnd = 0x20300000;
constexpr int32_t kProgramStackMaxSize = 8192;

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

constexpr int32_t kUndefinedGreyShade = 7;

enum class SequenceDirection {
	FORWARD,
	REVERSE,
	PINGPONG,
	OBEY_PARENT,
};
constexpr auto kNumSequenceDirections = util::to_underlying(SequenceDirection::OBEY_PARENT);

enum class AudioFileType {
	SAMPLE,
	WAVETABLE,
};

// Not 4 - because NE10 can't do FFTs that small unless we enable its additional C code, which would take up program size for little gain.
constexpr int32_t kWavetableMinCycleSize = 8;
constexpr int32_t kWavetableMaxCycleSize = 65536; // TODO: work out what this should actually be.

constexpr int32_t kMaxImageStoreWidth = kDisplayWidth;

constexpr int32_t kNumExpressionDimensions = 3;

enum class Expression {
	X_PITCH_BEND,
	Y_SLIDE_TIMBRE,
	Z_PRESSURE,
};

constexpr int32_t MIDI_CHANNEL_MPE_LOWER_ZONE = 16;
constexpr int32_t MIDI_CHANNEL_MPE_UPPER_ZONE = 17;
constexpr int32_t NUM_CHANNELS = 18;
constexpr int32_t MIDI_CHANNEL_NONE = 255;

constexpr int32_t IS_A_CC = NUM_CHANNELS;
// To be used instead of MIDI_CHANNEL_MPE_LOWER_ZONE etc for functions that require a "midi output filter". Although in
// fact, any number <16 or >=18 would work, the way I've defined it.
constexpr int32_t kMIDIOutputFilterNoMPE = 0;

// OLED -----------------
constexpr int32_t kOLEDWidthChars = 16;
constexpr int32_t kOLEDMenuNumOptionsVisible = (OLED_HEIGHT_CHARS - 1);

constexpr int32_t kConsoleImageHeight = (OLED_MAIN_HEIGHT_PIXELS + 16);
constexpr int32_t kConsoleImageNumRows = (kConsoleImageHeight >> 3);

constexpr int32_t kTextSpacingX = 6;
constexpr int32_t kTextSpacingY = 9;
constexpr int32_t kTextSizeYUpdated = 7;

constexpr int32_t kTextTitleSpacingX = 9;
constexpr int32_t kTextTitleSizeY = 10;

constexpr int32_t kTextBigSpacingX = 11;
constexpr int32_t kTextBigSizeY = 13;

constexpr int32_t kTextHugeSpacingX = 18;
constexpr int32_t kTextHugeSizeY = 20;

constexpr int32_t kNoteForDrum = 60;

enum BendRange {
	BEND_RANGE_MAIN,
	BEND_RANGE_FINGER_LEVEL,
};

enum class MIDICharacteristic {
	NOTE,
	CHANNEL,
};

enum class IndependentNoteRowLengthIncrease {
	DOUBLE,
	ROUND_UP,
};

// From FatFS - we need access to this:
constexpr int32_t DIR_FileSize = 28 /* File size (DWORD) */;

constexpr int32_t kMaxNumUnsignedIntegerstoRepAllParams = 2;

#if HAVE_OLED
constexpr int32_t kNumBrowserAndMenuLines = 3;
#else
constexpr int32_t kNumBrowserAndMenuLines = 1;
#endif

constexpr int32_t kDefaultCalculateRootNote = std::numeric_limits<int32_t>::max();

/// System sample rate, in samples per second. This is fixed in hardware because the Serial Sound Interface bit clock
/// is generated by a crystal, and the RZ/A1L provides only a divider on this clock.
/// See Figure 19.1 in the RZ/A1L TRM R01UH0437EJ0600 Rev.6.00 and the rest of section 19, Serial Sound Interface for
/// more detail.
constexpr uint32_t kSampleRate = 44100;

/// Length of press that deliniates a "short" press. Set to half a second (in units of samples, to work with
/// AudioEngine::audioSampleTimer)
constexpr uint32_t kShortPressTime = kSampleRate / 2;
