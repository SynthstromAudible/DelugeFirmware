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

#include <cstddef>
#include <cstdint>
#include <cstdio>

#define HARDWARE_TEST_MODE 0

#define AUTOMATED_TESTER_ENABLED (0 && ALPHA_OR_BETA_VERSION)

#define ALLOW_SPAM_MODE 0 // For debugging (in buttons.cpp, audio_engine.cpp, deluge.cpp)

#if ALPHA_OR_BETA_VERSION
// #define TEST_VECTOR 1
// #define TEST_VECTOR_SEARCH_MULTIPLE 1
#define TEST_GENERAL_MEMORY_ALLOCATION 0
// #define TEST_VECTOR_DUPLICATES 1
// #define TEST_BST 1
// #define TEST_OPEN_ADDRESSING_HASH_TABLE 1
// #define TEST_SD_WRITE 1
// #define TEST_SAMPLE_LOOP_POINTS 1
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

constexpr int32_t kNumBytesInColUpdateMessage = 49;
constexpr int32_t kNumBytesInLongestMessage = 55;

constexpr int32_t kNumBytesInSidebarRedraw = (kNumBytesInColUpdateMessage);

constexpr int32_t kNumBytesInMainPadRedraw = (kNumBytesInColUpdateMessage * 8);

constexpr int32_t kDefaultClipLength = 96; // You'll want to <<displayWidthMagnitude this each time used
constexpr int32_t kDefaultArrangerZoom = (kDefaultClipLength >> 1);

constexpr int32_t kMinLedBrightness = 1;
constexpr int32_t kMaxLedBrightness = 25;

struct Pin {
	uint8_t port;
	uint8_t pin;
};

constexpr Pin LINE_OUT_DETECT_L = {6, 3};
constexpr Pin LINE_OUT_DETECT_R = {6, 4};
constexpr Pin ANALOG_CLOCK_IN = {1, 14};
constexpr Pin SPEAKER_ENABLE = {4, 1};
constexpr Pin HEADPHONE_DETECT = {6, 5};
constexpr Pin LINE_IN_DETECT = {6, 6};
constexpr Pin MIC_DETECT = {7, 9};
constexpr Pin SYNCED_LED = {6, 7};
constexpr Pin CODEC = {6, 12};

constexpr Pin BATTERY_LED = {1, 1};
constexpr int32_t SYS_VOLT_SENSE_PIN = 5;
constexpr Pin VOLT_SENSE = {1, 8 + SYS_VOLT_SENSE_PIN};

constexpr Pin SPI_CLK = {6, 0};
constexpr Pin SPI_MOSI = {6, 2};
constexpr Pin SPI_SSL = {6, 1};

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
	INSTRUMENT_INPUT,
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
constexpr int32_t kNumGateTypes = util::to_underlying(GateType::SPECIAL) + 1;

constexpr int32_t kNumSongSlots = 1000;
constexpr int32_t kNumInstrumentSlots = 1000;

// Don't ever make this less! The zoom rendering code uses this buffer for its stuff
constexpr size_t kFilenameBufferSize = 256;

enum class UIType : uint8_t {
	ARRANGER_VIEW,
	SESSION_VIEW,
	INSTRUMENT_CLIP_VIEW,
	AUDIO_CLIP_VIEW,
	KEYBOARD_SCREEN,
	AUTOMATION_VIEW,
	PERFORMANCE_SESSION_VIEW,
	TIMELINE_VIEW,
	BROWSER,
	CONTEXT_MENU,
	LOAD_SONG,
	LOAD_INSTRUMENT_PRESET,
	RENAME_OUTPUT,
	RENAME_DRUM,
	AUDIO_RECORDER,
	SOUND_EDITOR,
	SAMPLE_MARKER_EDITOR,
	SLICER,
	NONE = 255,
};

enum class AutomationSubType : uint8_t {
	ARRANGER,
	INSTRUMENT,
	AUDIO,
	NONE = 255,
};

enum class OutputType : uint8_t {
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

constexpr int32_t kModFXGrainBufferSize = 65536;
constexpr int32_t kModFXGrainBufferIndexMask = (kModFXGrainBufferSize - 1);

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
	HOLD,
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
	SIDECHAIN,
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

// constexpr PatchSource kFirstGlobalSourceWithChangedStatusAutomaticallyUpdated = PatchSource::ENVELOPE_0;
constexpr PatchSource kFirstLocalSource = PatchSource::ENVELOPE_0;
// constexpr PatchSource kFirstUnchangeableSource = PatchSource::VELOCITY;

// Menu Min Max Values

// regular menu range e.g. 0 - 50
constexpr int32_t kMaxMenuValue = 50;
constexpr int32_t kMinMenuValue = 0;
constexpr int32_t kMidMenuValue = kMinMenuValue + ((kMaxMenuValue - kMinMenuValue) / 2);

// relative menu range e.g. -25 to +25 - used with pan and pitch
constexpr int32_t kMaxMenuRelativeValue = kMaxMenuValue / 2;
constexpr int32_t kMinMenuRelativeValue = -1 * kMaxMenuRelativeValue;

// patch cable menu range e.g. -5000 to 5000
constexpr int32_t kMaxMenuPatchCableValue = kMaxMenuValue * 100;
constexpr int32_t kMinMenuPatchCableValue = -1 * kMaxMenuPatchCableValue;

// metronome volume menu range : 22 to 27
constexpr int32_t kMaxMenuMetronomeVolumeValue = 50;
constexpr int32_t kMinMenuMetronomeVolumeValue = 1;

// Automation View constants
constexpr int32_t kNoSelection = 255;
constexpr int32_t kKnobPosOffset = 64;
constexpr int32_t kMaxKnobPos = 128;
constexpr int32_t kParamValueIncrementForAutomationSinglePadPress = 18;
constexpr int32_t kParamValueIncrementForAutomationDisplay = 16;
constexpr int32_t kParamValueIncrementForAutomationPatchCableSinglePadPress = 30;
constexpr int32_t kParamValueIncrementForAutomationPatchCableDisplay = 32;
constexpr int32_t kParamNodeWidth = 3;
//

// Performance View constant
constexpr int32_t kNumParamsForPerformance = 16;
constexpr int32_t kParamValueIncrementForDelayAmount = kParamValueIncrementForAutomationSinglePadPress / 2;
constexpr int32_t kMaxKnobPosForDelayAmount = (kMaxKnobPos / 2) - 1;
constexpr int32_t kParamValueIncrementForQuantizedStutter = 15;
constexpr int32_t kMinKnobPosForQuantizedStutter = 52;

enum class PerformanceEditingMode : int32_t {
	DISABLED,
	VALUE,
	PARAM,
};

// Midi Follow Mode Feedback Automation Modes

enum class MIDIFollowFeedbackAutomationMode : uint8_t {
	DISABLED,
	LOW,
	MEDIUM,
	HIGH,
};

enum class OscType {
	SINE,
	TRIANGLE,
	SQUARE,
	ANALOG_SQUARE,
	SAW,
	ANALOG_SAW_2,
	WAVETABLE,
	SAMPLE,
	DX7,
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

constexpr int32_t kNumLFOTypes = util::to_underlying(LFOType::RANDOM_WALK) + 1;

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
constexpr int kNumSynthModes = util::to_underlying(::SynthMode::RINGMOD) + 1;

enum class ModFXType {
	NONE,
	FLANGER,
	CHORUS,
	PHASER,
	CHORUS_STEREO,
	GRAIN, // Look below if you want to add another one
};

// Warning: Currently GRAIN can be disabled and kNumModFXTypes might need to be used - 1
constexpr int32_t kNumModFXTypes = util::to_underlying(ModFXType::GRAIN) + 1;

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
constexpr size_t kNumGoldKnobIndicatorLEDs = 4;

constexpr int32_t kMaxNumSections = 12;

constexpr int32_t kNumPhysicalModKnobs = 2;

constexpr int32_t kNumAllpassFiltersPhaser = 6;

enum class Error {
	NONE,
	INSUFFICIENT_RAM,
	UNSPECIFIED,
	SD_CARD,
	NO_FURTHER_PRESETS,
	FILE_CORRUPTED,
	FILE_UNREADABLE, // Or not found, I think?
	FILE_UNSUPPORTED,
	FILE_FIRMWARE_VERSION_TOO_NEW,
	RESULT_TAG_UNUSED,
	FOLDER_DOESNT_EXIST,
	WRITE_PROTECTED,
	BUG,
	WRITE_FAIL,
	FILE_TOO_BIG,
	PRESET_IN_USE,
	NO_FURTHER_FILES_THIS_DIRECTION,
	FILE_ALREADY_EXISTS,
	FILE_NOT_FOUND,
	ABORTED_BY_USER,
	MAX_FILE_SIZE_REACHED,
	SD_CARD_FULL,
	FILE_NOT_LOADABLE_AS_WAVETABLE,
	FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO,
	NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP,
	NO_ERROR_BUT_GET_OUT,
	INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE,
	SD_CARD_NOT_PRESENT,
	SD_CARD_NO_FILESYSTEM,
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

enum class OldArpMode {
	OFF,
	UP,
	DOWN,
	BOTH,
	RANDOM,
};

enum class ArpMode {
	OFF,
	ARP,
};

enum class ArpPreset {
	OFF,
	UP,
	DOWN,
	BOTH,
	RANDOM,
	CUSTOM,
};

enum class ArpNoteMode {
	UP,
	DOWN,
	UP_DOWN,
	AS_PLAYED,
	RANDOM,
};

enum class ArpOctaveMode {
	UP,
	DOWN,
	UP_DOWN,
	ALTERNATE,
	RANDOM,
};

enum class ArpMpeModSource {
	OFF,
	AFTERTOUCH,
	MPE_Y,
};

enum class ModFXParam {
	DEPTH,
	FEEDBACK,
	OFFSET,
};

enum class CompParam {
	RATIO,
	ATTACK,
	RELEASE,
	SIDECHAIN,
	LAST,
};

constexpr auto kNumModFXParams = util::to_underlying(ModFXParam::OFFSET) + 1;

enum class PatchCableAcceptance {
	DISALLOWED,
	EDITABLE,
	ALLOWED,
	YET_TO_BE_DETERMINED,
};
enum class OverDubType { Normal, ContinuousLayering };

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
	FILL,
	TRANSPOSE,
	LAST, // Keep as boundary
};
constexpr auto kNumGlobalMIDICommands = util::to_underlying(GlobalMIDICommand::LAST) + 1;

enum class MIDITakeoverMode : uint8_t {
	JUMP,
	PICKUP,
	SCALE,
};
constexpr auto kNumMIDITakeoverModes = util::to_underlying(MIDITakeoverMode::SCALE) + 1;
constexpr int32_t kMIDITakeoverKnobSyncThreshold = 5;

enum class MIDIFollowChannelType : uint8_t {
	A,
	B,
	C,
	NONE,
};
constexpr auto kNumMIDIFollowChannelTypes = util::to_underlying(MIDIFollowChannelType::NONE);

enum class MIDITransposeControlMethod : uint8_t {
	INKEY,
	CHROMATIC,
	CHORD,
};
constexpr auto kNumMIDITransposeControlMethods = util::to_underlying(MIDITransposeControlMethod::CHORD) + 1;

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
constexpr int32_t kNumIterationValues = 35; // 1of2 to 8of8
constexpr int32_t kFillProbabilityValue = 0;
constexpr int32_t kNotFillProbabilityValue = 128; // This is like the "latched" state of Fill (zero ORed with 128)
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
	/// note - only for incoming/outgoing midi. Internally use CC_NUMBER_Y_AXIS
	CC_NUMBER_MOD_WHEEL = 1,
	CC_NUMBER_PITCH_BEND = 120,
	CC_NUMBER_AFTERTOUCH = 121,
	CC_NUMBER_Y_AXIS = 122,
	CC_NUMBER_NONE = 123,
};
constexpr int32_t kNumCCNumbersIncludingFake = 124;
constexpr int32_t kNumCCExpression = kNumCCNumbersIncludingFake - 1;
constexpr int32_t kNumRealCCNumbers = 120;
constexpr int32_t kMaxMIDIValue = 127;
constexpr int32_t ALL_NOTES_OFF = -32768;
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

// This is about right. Making it smaller didn't help. Tried it as 9, and I'm pretty sure some fast percussive details
// were lost in the output
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

// Anywhere between 30 and 40 seemed ideal. Point of interest - high numbers (e.g. I tried 140) screw up the high notes,
// so more is not more!
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

// Let's just do a 100 sample crossfade. Even 12 samples actually sounded fine for my voice - just obviously not so good
// for a low sine wave. Of course, if like 60 samples are being processed at a time under CPU load, then this might end
// up as low as 40.
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

enum class StealableQueue {
	NO_SONG_SAMPLE_DATA,
	NO_SONG_SAMPLE_DATA_CONVERTED, // E.g. from floating point file, or wrong endianness AIFF file.
	NO_SONG_WAVETABLE_BAND_DATA,
	NO_SONG_SAMPLE_DATA_REPITCHED_CACHE,
	NO_SONG_SAMPLE_DATA_PERC_CACHE,
	NO_SONG_AUDIO_FILE_OBJECTS, // TODO:having these in the Stealable region is a bad idea in general
	CURRENT_SONG_SAMPLE_DATA,
	CURRENT_SONG_SAMPLE_DATA_CONVERTED,
	CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE,
	CURRENT_SONG_SAMPLE_DATA_PERC_CACHE, // This one is super valuable and compacted data - lots of work
	                                     // to load it all again
};

constexpr int32_t kNumStealableQueue = 10;

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

// Not 4 - because NE10 can't do FFTs that small unless we enable its additional C code, which would take up program
// size for little gain.
constexpr int32_t kWavetableMinCycleSize = 8;
constexpr int32_t kWavetableMaxCycleSize = 65536; // TODO: work out what this should actually be.

constexpr int32_t kMaxImageStoreWidth = kDisplayWidth;

constexpr int32_t kNumExpressionDimensions = 3;

enum Expression {
	X_PITCH_BEND,
	Y_SLIDE_TIMBRE,
	Z_PRESSURE,
};

constexpr int32_t MIDI_CHANNEL_MPE_LOWER_ZONE = 16;
constexpr int32_t MIDI_CHANNEL_MPE_UPPER_ZONE = 17;
constexpr int32_t NUM_CHANNELS = 18;
constexpr int32_t MIDI_CHANNEL_NONE = 255;
constexpr int32_t MIDI_CC_NONE = 255;

constexpr int32_t NUM_INTERNAL_DESTS = 1;

constexpr int32_t IS_A_CC = NUM_CHANNELS;
constexpr int32_t IS_A_PC = IS_A_CC + NUM_CHANNELS; // CC128 is max.
constexpr int32_t IS_A_DEST = IS_A_PC + NUM_CHANNELS;
constexpr int32_t MIDI_CHANNEL_TRANSPOSE = IS_A_DEST + 1;
constexpr int32_t MIDI_TYPE_MAX = IS_A_DEST + NUM_INTERNAL_DESTS;
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

constexpr int32_t kDefaultCalculateRootNote = std::numeric_limits<int32_t>::max();

/// System sample rate, in samples per second. This is fixed in hardware because the Serial Sound Interface bit clock
/// is generated by a crystal, and the RZ/A1L provides only a divider on this clock.
/// See Figure 19.1 in the RZ/A1L TRM R01UH0437EJ0600 Rev.6.00 and the rest of section 19, Serial Sound Interface for
/// more detail.
constexpr int32_t kSampleRate = 44100;

// The Deluge deals with 24-bit PCM audio
constexpr int32_t kBitDepth = 24;

// The maximum value a (24-bit) sample can hold
constexpr int32_t kMaxSampleValue = 1 << kBitDepth; // 2 ** kBitDepth

/// Length of press that delineates a "short" press. Set to half a second (in units of samples, to work with
/// AudioEngine::audioSampleTimer)
constexpr uint32_t kShortPressTime = kSampleRate / 2;

/// Length of a press that delineates a "hold" press.
/// Used in Performance View and with Sticky Shift
constexpr uint32_t kHoldTime = kSampleRate / 10;

/// Rate at which midi follow feedback for automation is sent
constexpr uint32_t kLowFeedbackAutomationRate = (kSampleRate / 1000) * 500;    // 500 ms
constexpr uint32_t kMediumFeedbackAutomationRate = (kSampleRate / 1000) * 150; // 150 ms
constexpr uint32_t kHighFeedbackAutomationRate = (kSampleRate / 1000) * 40;    // 40 ms

enum KeyboardLayoutType : uint8_t {
	KeyboardLayoutTypeIsomorphic,
	KeyboardLayoutTypeInKey,
	KeyboardLayoutTypeDrums,
	KeyboardLayoutTypeNorns,
	KeyboardLayoutTypeMaxElement // Keep as boundary
};

enum SessionLayoutType : uint8_t {
	SessionLayoutTypeRows,
	SessionLayoutTypeGrid,
	SessionLayoutTypeMaxElement // Keep as boundary
};

enum GridDefaultActiveMode : uint8_t {
	GridDefaultActiveModeSelection,
	GridDefaultActiveModeGreen,
	GridDefaultActiveModeBlue,
	GridDefaultActiveModeMaxElement // Keep as boundary
};

// mapping of grid modes to y axis
enum GridMode : uint8_t {
	PINK,
	Unassigned1,
	Unassigned2,
	Unassigned3,
	Unassigned4,
	Unassigned5,
	BLUE,
	GREEN,
};

enum class ClipType {
	INSTRUMENT,
	AUDIO,
};

enum class LaunchStyle { DEFAULT, FILL, ONCE };

enum class StartupSongMode { BLANK, TEMPLATE, LASTOPENED, LASTSAVED };
constexpr auto kNumStartupSongMode = util::to_underlying(StartupSongMode::LASTSAVED) + 1;
