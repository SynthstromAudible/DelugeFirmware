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

#ifndef Definitions_h
#define Definitions_h

#include "r_typedefs.h"
#include "cpu_specific.h"

#define FIRMWARE_OLD 0
#define FIRMWARE_1P2P0 1
#define FIRMWARE_1P3P0_PRETEST 2
#define FIRMWARE_1P3P0_BETA 3
#define FIRMWARE_1P3P0 4
#define FIRMWARE_1P3P1 5
#define FIRMWARE_1P3P2 6
#define FIRMWARE_1P4P0_PRETEST 7
#define FIRMWARE_1P4P0_BETA 8
#define FIRMWARE_1P4P0 9
#define FIRMWARE_1P5P0_PREBETA 10
#define FIRMWARE_2P0P0_BETA 11
#define FIRMWARE_2P0P0 12
#define FIRMWARE_2P0P1_BETA 13
#define FIRMWARE_2P0P1 14
#define FIRMWARE_2P0P2_BETA 15
#define FIRMWARE_2P0P2 16
#define FIRMWARE_2P0P3 17
#define FIRMWARE_2P1P0_BETA 18
#define FIRMWARE_2P1P0 19
#define FIRMWARE_2P1P1_BETA 20
#define FIRMWARE_2P1P1 21
#define FIRMWARE_2P1P2_BETA 22
#define FIRMWARE_2P1P2 23
#define FIRMWARE_2P1P3_BETA 24
#define FIRMWARE_2P1P3 25
#define FIRMWARE_2P1P4_BETA 26
#define FIRMWARE_2P1P4 27
#define FIRMWARE_3P0P0_ALPHA 28
#define FIRMWARE_3P0P0_BETA 29
#define FIRMWARE_3P0P0 30
#define FIRMWARE_3P0P1_BETA 31
#define FIRMWARE_3P0P1 32
#define FIRMWARE_3P0P2 33
#define FIRMWARE_3P0P3_ALPHA 34
#define FIRMWARE_3P0P3_BETA 35
#define FIRMWARE_3P0P3 36
#define FIRMWARE_3P0P4 37
#define FIRMWARE_3P0P5_BETA 38
#define FIRMWARE_3P0P5 39
#define FIRMWARE_3P1P0_ALPHA 40
#define FIRMWARE_3P1P0_ALPHA2 41
#define FIRMWARE_3P1P0_BETA 42
#define FIRMWARE_3P1P0 43
#define FIRMWARE_3P1P1_BETA 44
#define FIRMWARE_3P1P1 45
#define FIRMWARE_3P1P2_BETA 46
#define FIRMWARE_3P1P2 47
#define FIRMWARE_3P1P3_BETA 48
#define FIRMWARE_3P1P3 49
#define FIRMWARE_3P1P4_BETA 50
#define FIRMWARE_3P1P4 51
#define FIRMWARE_3P1P5_BETA 52
#define FIRMWARE_3P1P5 53
#define FIRMWARE_3P2P0_ALPHA 54
#define FIRMWARE_4P0P0_BETA 55
#define FIRMWARE_4P0P0 56
#define FIRMWARE_4P0P1_BETA 57
#define FIRMWARE_4P0P1 58
#define FIRMWARE_4P1P0_ALPHA 59
#define FIRMWARE_4P1P0_BETA 60
#define FIRMWARE_4P1P0 61
#define FIRMWARE_4P1P1_ALPHA 62
#define FIRMWARE_4P1P1 63
#define FIRMWARE_4P1P2 64
#define FIRMWARE_4P1P3_ALPHA 65
#define FIRMWARE_4P1P3_BETA 66
#define FIRMWARE_4P1P3 67
#define FIRMWARE_4P1P4_ALPHA 68
#define FIRMWARE_4P1P4_BETA 69
#define FIRMWARE_4P1P4 70
#define FIRMWARE_TOO_NEW 255

#define CURRENT_FIRMWARE_VERSION FIRMWARE_4P1P4_ALPHA

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD

#define syncScalingButtonX 8
#define syncScalingButtonY 1
#define syncScalingLedX 8
#define syncScalingLedY 2

#define crossScreenEditButtonX 5
#define crossScreenEditButtonY 0
#define crossScreenEditLedX 5
#define crossScreenEditLedY 3

#define xEncButtonX 1
#define xEncButtonY 2

#define selectEncButtonX 4
#define selectEncButtonY 1

#define yEncButtonX 0
#define yEncButtonY 2

#define tempoEncButtonX 7
#define tempoEncButtonY 2

#define backButtonX 4 // AKA back button
#define backButtonY 2
#define backLedX 4 // AKA back button
#define backLedY 1

#define syncedLedX 9
#define syncedLedY 3

#else

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

#endif

#define editPadPressBufferSize 8

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
#define NUM_MOD_BUTTONS 6
#define displayHeight 4
#define displayHeightMagnitude 2
#define displayWidth 8
#define displayWidthMagnitude 3
#define NO_PRESSES_HAPPENING_MESSAGE 141
#define RESEND_BUTTON_STATES_MESSAGE 72
#define NUM_BYTES_IN_COL_UPDATE_MESSAGE 13
#define NUM_BYTES_IN_LONGEST_MESSAGE 13
#define NUM_BYTES_IN_SIDEBAR_REDRAW (NUM_BYTES_IN_COL_UPDATE_MESSAGE * 2)
#define PAD_AND_BUTTON_MESSAGES_END 140
#else
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
#endif

#define NUM_BYTES_IN_MAIN_PAD_REDRAW (NUM_BYTES_IN_COL_UPDATE_MESSAGE * 8)

#define DEFAULT_ARRANGER_ZOOM (DEFAULT_CLIP_LENGTH >> 1)

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
#define LINE_OUT_DETECT_L_1 1
#define LINE_OUT_DETECT_L_2 4
#define LINE_OUT_DETECT_R_1 1
#define LINE_OUT_DETECT_R_2 3
#define ANALOG_CLOCK_IN_1 1
#define ANALOG_CLOCK_IN_2 2
#else
#define LINE_OUT_DETECT_L_1 6
#define LINE_OUT_DETECT_L_2 3
#define LINE_OUT_DETECT_R_1 6
#define LINE_OUT_DETECT_R_2 4
#define ANALOG_CLOCK_IN_1 1
#define ANALOG_CLOCK_IN_2 14
#define SPEAKER_ENABLE_1 4
#define SPEAKER_ENABLE_2 1
#define HEADPHONE_DETECT_1 6
#define HEADPHONE_DETECT_2 5
#endif

#define sideBarWidth 2
#define MAX_NUM_ANIMATED_ROWS ((displayHeight * 3) >> 1)

#define MIDI_LEARN_CLIP 1
#define MIDI_LEARN_NOTEROW_MUTE 2
#define MIDI_LEARN_PLAY_BUTTON 3
#define MIDI_LEARN_RECORD_BUTTON 4
#define MIDI_LEARN_TAP_TEMPO_BUTTON 5
#define MIDI_LEARN_SECTION 6
#define MIDI_LEARN_MELODIC_INSTRUMENT_INPUT 7
#define MIDI_LEARN_DRUM_INPUT 8

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

#define disabledColourRed 255
#define disabledColourGreen 0
#define disabledColourBlue 0

#define groupEnabledColourRed 0
#define groupEnabledColourGreen 255
#define groupEnabledColourBlue 6

#define enabledColourRed 0
#define enabledColourGreen 255
#define enabledColourBlue 6

#define mutedColourRed 255
#define mutedColourGreen 160
#define mutedColourBlue 0

#define midiCommandColourRed 255
#define midiCommandColourGreen 80
#define midiCommandColourBlue 120

#define midiNoCommandColourRed 50
#define midiNoCommandColourGreen 50
#define midiNoCommandColourBlue 50

#define selectedDrumColourRed 30
#define selectedDrumColourGreen 30
#define selectedDrumColourBlue 10

#define MAX_SEQUENCE_LENGTH 1610612736 // The biggest multiple of 3 which can fit in a signed 32-bit int
#define noteOnLatenessAllowed 2205     // In audio samples. That's 50mS. Multiply mS by 44.1

#define GATE_MODE_V_TRIG 0
#define GATE_MODE_S_TRIG 1

#define numSongSlots 1000
#define numInstrumentSlots 1000
#define maxNumInstrumentPresets 128
#define FILENAME_BUFFER_SIZE 256 // Don't ever make this less! The zoom rendering code uses this buffer for its stuff

#define INSTRUMENT_TYPE_SYNTH 0
#define INSTRUMENT_TYPE_KIT 1
#define INSTRUMENT_TYPE_MIDI_OUT 2
#define INSTRUMENT_TYPE_CV 3
#define OUTPUT_TYPE_AUDIO 4

#define THING_TYPE_SYNTH 0
#define THING_TYPE_KIT 1
#define THING_TYPE_SONG 2
#define THING_TYPE_NONE 3

#define audioEngineBufferSize                                                                                          \
	128 // Maximum num samples that may be processed in one "frame". Actual size of output buffer is in ssi.h
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

#define ENVELOPE_STAGE_ATTACK 0
#define ENVELOPE_STAGE_DECAY 1
#define ENVELOPE_STAGE_SUSTAIN 2
#define ENVELOPE_STAGE_RELEASE 3
#define ENVELOPE_STAGE_FAST_RELEASE 4
#define ENVELOPE_STAGE_OFF 5

#define NUM_ENVELOPE_STAGES 6

#define NUM_PRIORITY_OPTIONS 3

#define PATCH_SOURCE_LFO_GLOBAL 0
#define PATCH_SOURCE_COMPRESSOR 1
#define PATCH_SOURCE_ENVELOPE_0 2
#define PATCH_SOURCE_ENVELOPE_1 3
#define PATCH_SOURCE_LFO_LOCAL 4
#define PATCH_SOURCE_X 5
#define PATCH_SOURCE_Y 6
#define PATCH_SOURCE_AFTERTOUCH 7
#define PATCH_SOURCE_VELOCITY 8
#define PATCH_SOURCE_NOTE 9
#define PATCH_SOURCE_RANDOM 10
#define NUM_PATCH_SOURCES 11

#define PATCH_SOURCE_NONE (NUM_PATCH_SOURCES)

#define FIRST_GLOBAL_SOURCE_WITH_CHANGED_STATUS_AUTOMATICALLY_UPDATED (PATCH_SOURCE_ENVELOPE_0)
#define FIRST_LOCAL_SOURCE (PATCH_SOURCE_ENVELOPE_0)
#define FIRST_UNCHANGEABLE_SOURCE (PATCH_SOURCE_VELOCITY)

// Linear params have different sources multiplied together, then multiplied by the neutral value
// -- and "volume" ones get squared at the end

// Hybrid params have different sources added together, then added to the neutral value

// Exp params have different sources added together, converted to an exponential scale, then multiplied by the neutral value

#define PARAM_LOCAL_OSC_A_VOLUME 0
#define PARAM_LOCAL_OSC_B_VOLUME 1
#define PARAM_LOCAL_VOLUME 2
#define PARAM_LOCAL_NOISE_VOLUME 3
#define PARAM_LOCAL_MODULATOR_0_VOLUME 4
#define PARAM_LOCAL_MODULATOR_1_VOLUME 5
// Local non-volume params begin
#define PARAM_LOCAL_MODULATOR_0_FEEDBACK 6
#define PARAM_LOCAL_MODULATOR_1_FEEDBACK 7
#define PARAM_LOCAL_CARRIER_0_FEEDBACK 8
#define PARAM_LOCAL_CARRIER_1_FEEDBACK 9
#define PARAM_LOCAL_LPF_RESONANCE 10
#define PARAM_LOCAL_HPF_RESONANCE 11
#define PARAM_LOCAL_ENV_0_SUSTAIN 12
#define PARAM_LOCAL_ENV_1_SUSTAIN 13
// Local hybrid params begin
#define PARAM_LOCAL_OSC_A_PHASE_WIDTH 14
#define PARAM_LOCAL_OSC_B_PHASE_WIDTH 15
#define PARAM_LOCAL_OSC_A_WAVE_INDEX 16
#define PARAM_LOCAL_OSC_B_WAVE_INDEX 17
#define PARAM_LOCAL_PAN 18
// Local exp params begin
#define PARAM_LOCAL_LPF_FREQ 19
#define PARAM_LOCAL_PITCH_ADJUST 20
#define PARAM_LOCAL_OSC_A_PITCH_ADJUST 21
#define PARAM_LOCAL_OSC_B_PITCH_ADJUST 22
#define PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST 23
#define PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST 24
#define PARAM_LOCAL_HPF_FREQ 25
#define PARAM_LOCAL_LFO_LOCAL_FREQ 26
#define PARAM_LOCAL_ENV_0_ATTACK 27
#define PARAM_LOCAL_ENV_1_ATTACK 28
#define PARAM_LOCAL_ENV_0_DECAY 29
#define PARAM_LOCAL_ENV_1_DECAY 30
#define PARAM_LOCAL_ENV_0_RELEASE 31
#define PARAM_LOCAL_ENV_1_RELEASE 32
// Global params begin
#define PARAM_GLOBAL_VOLUME_POST_FX 33
#define PARAM_GLOBAL_VOLUME_POST_REVERB_SEND 34
#define PARAM_GLOBAL_REVERB_AMOUNT 35
#define PARAM_GLOBAL_MOD_FX_DEPTH 36
// Global non-volume params begin
#define PARAM_GLOBAL_DELAY_FEEDBACK 37
// Global hybrid params begin
// Global exp params begin
#define PARAM_GLOBAL_DELAY_RATE 38
#define PARAM_GLOBAL_MOD_FX_RATE 39
#define PARAM_GLOBAL_LFO_FREQ 40
#define PARAM_GLOBAL_ARP_RATE 41
// ANY TIME YOU UPDATE THIS LIST! CHANGE Sound::paramToString()

#define PARAM_NONE 42

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

// For all ModControllables
#define PARAM_UNPATCHED_STUTTER_RATE 0
#define PARAM_UNPATCHED_BASS 1
#define PARAM_UNPATCHED_TREBLE 2
#define PARAM_UNPATCHED_BASS_FREQ 3
#define PARAM_UNPATCHED_TREBLE_FREQ 4
#define PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION 5
#define PARAM_UNPATCHED_BITCRUSHING 6
#define PARAM_UNPATCHED_MOD_FX_OFFSET 7
#define PARAM_UNPATCHED_MOD_FX_FEEDBACK 8
#define PARAM_UNPATCHED_COMPRESSOR_SHAPE 9
// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp

#define NUM_SHARED_UNPATCHED_PARAMS 10

// Just for Sounds
#define PARAM_UNPATCHED_SOUND_ARP_GATE 10
#define PARAM_UNPATCHED_SOUND_PORTA 11
// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp

#define MAX_NUM_UNPATCHED_PARAM_FOR_SOUNDS 12

// Just for GlobalEffectables
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE 10
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH 11
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE 12
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT 13
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN 14
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ 15
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES 16
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ 17
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES 18
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT 19
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME 20
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME 21
#define PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST 22

#define MAX_NUM_UNPATCHED_PARAMS 23

#define KIT_SIDECHAIN_SHAPE (-601295438)

#define OSC_TYPE_SINE 0
#define OSC_TYPE_TRIANGLE 1
#define OSC_TYPE_SQUARE 2
#define OSC_TYPE_ANALOG_SQUARE 3
#define OSC_TYPE_SAW 4
#define OSC_TYPE_ANALOG_SAW_2 5
#define OSC_TYPE_WAVETABLE 6
#define OSC_TYPE_SAMPLE 7
#define OSC_TYPE_INPUT_L 8
#define OSC_TYPE_INPUT_R 9
#define OSC_TYPE_INPUT_STEREO 10

#define NUM_OSC_TYPES_RINGMODDABLE OSC_TYPE_SAMPLE

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
#define NUM_OSC_TYPES 7
#else
#define NUM_OSC_TYPES 11
#endif

#define LFO_TYPE_SINE 0
#define LFO_TYPE_TRIANGLE 1
#define LFO_TYPE_SQUARE 2
#define LFO_TYPE_SAW 3
#define LFO_TYPE_SAH 4
#define LFO_TYPE_RWALK 5

#define NUM_LFO_TYPES 6

// SyncType values correspond to the index of the first option of the specific
// type in the selection menu. There are 9 different levels for each type (see
// also SyncLevel)
typedef enum SyncType_ {
	SYNC_TYPE_EVEN = 0,
	SYNC_TYPE_TRIPLET = 10,
	SYNC_TYPE_DOTTED = 19,
} SyncType;

typedef enum SyncLevel_ {
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
} SyncLevel;

#define SYNTH_MODE_SUBTRACTIVE 0
#define SYNTH_MODE_FM 1
#define SYNTH_MODE_RINGMOD 2

#define MOD_FX_TYPE_NONE 0
#define MOD_FX_TYPE_FLANGER 1
#define MOD_FX_TYPE_CHORUS 2
#define MOD_FX_TYPE_PHASER 3
#define NUM_MOD_FX_TYPES 4

#define SAMPLE_MAX_TRANSPOSE 24
#define SAMPLE_MIN_TRANSPOSE (-96)

#define WAV_FORMAT_PCM 1
#define WAV_FORMAT_FLOAT 3

#define POLYPHONY_AUTO 0
#define POLYPHONY_POLY 1
#define POLYPHONY_MONO 2
#define POLYPHONY_LEGATO 3
#define POLYPHONY_CHOKE 4

#define NUM_POLYPHONY_TYPES 5

#define NUMERIC_DISPLAY_LENGTH 4

#define MAX_NUM_SECTIONS 12

#if DELUGE_MODEL >= DELUGE_MODEL_144_G
#define XTAL_SPEED_MHZ 13007402 // 1.65% lower, for SSCG
#else
#define XTAL_SPEED_MHZ 13225625
#endif

#define NUM_PHYSICAL_MOD_KNOBS 2

#define LPF_MODE_12DB 0
#define LPF_MODE_TRANSISTOR_24DB 1
#define LPF_MODE_TRANSISTOR_24DB_DRIVE 2
#define LPF_MODE_DIODE 3
#define NUM_LPF_MODES 3

#define PHASER_NUM_ALLPASS_FILTERS 6

#define NO_ERROR 0
#define ERROR_INSUFFICIENT_RAM 1
#define ERROR_UNSPECIFIED 2
#define ERROR_SD_CARD 3
#define ERROR_NO_FURTHER_PRESETS 4
#define ERROR_FILE_CORRUPTED 5
#define ERROR_FILE_UNREADABLE 6 // Or not found, I think?
#define ERROR_FILE_UNSUPPORTED 7
#define ERROR_FILE_FIRMWARE_VERSION_TOO_NEW 8
#define RESULT_TAG_UNUSED 9
#define ERROR_FOLDER_DOESNT_EXIST 10
#define ERROR_WRITE_PROTECTED 11
#define ERROR_BUG 12
#define ERROR_WRITE_FAIL 13
#define ERROR_FILE_TOO_BIG 14
#define ERROR_PRESET_IN_USE 15
#define ERROR_NO_FURTHER_FILES_THIS_DIRECTION 16
#define ERROR_FILE_ALREADY_EXISTS 17
#define ERROR_FILE_NOT_FOUND 18
#define ERROR_ABORTED_BY_USER 19
#define ERROR_MAX_FILE_SIZE_REACHED 20
#define ERROR_SD_CARD_FULL 21
#define ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE 22
#define ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO 23
#define ERROR_NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP 24
#define NO_ERROR_BUT_GET_OUT 25
#define ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE 26
#define ERROR_SD_CARD_NOT_PRESENT 27
#define ERROR_SD_CARD_NO_FILESYSTEM 28

#define SAMPLE_REPEAT_CUT 0
#define SAMPLE_REPEAT_ONCE 1
#define SAMPLE_REPEAT_LOOP 2
#define SAMPLE_REPEAT_STRETCH 3

#define NUM_REPEAT_MODES 4

#define FILTER_TYPE_LPF 0
#define FILTER_TYPE_HPF 1
#define FILTER_TYPE_EQ 2
#define NUM_FILTER_TYPES 3

#define NUM_SOURCES 2 // That's sources as in oscillators - within a Sound (synth).

#define PIC_MESSAGE_REFRESH_TIME 19

#define NUM_ARP_MODES 5
#define ARP_MODE_OFF 0
#define ARP_MODE_UP 1
#define ARP_MODE_DOWN 2
#define ARP_MODE_BOTH 3
#define ARP_MODE_RANDOM 4

#define ALLOW_SPAM_MODE 0 // For debugging I think?

#define KEYBOARD_ROW_INTERVAL 5

// UART
#define MIDI_TX_BUFFER_SIZE 1024

#define MIDI_RX_BUFFER_SIZE 512
#define MIDI_RX_TIMING_BUFFER_SIZE 32 // Must be <= MIDI_RX_BUFFER_SIZE, above

#define MOD_FX_PARAM_DEPTH 0
#define MOD_FX_PARAM_FEEDBACK 1
#define MOD_FX_PARAM_OFFSET 2
#define NUM_MOD_FX_PARAMS 3

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

#define NUM_GLOBAL_MIDI_COMMANDS 8
#define GLOBAL_MIDI_COMMAND_PLAYBACK_RESTART 0
#define GLOBAL_MIDI_COMMAND_PLAY 1
#define GLOBAL_MIDI_COMMAND_RECORD 2
#define GLOBAL_MIDI_COMMAND_TAP 3
#define GLOBAL_MIDI_COMMAND_LOOP 4
#define GLOBAL_MIDI_COMMAND_LOOP_CONTINUOUS_LAYERING 5
#define GLOBAL_MIDI_COMMAND_UNDO 6
#define GLOBAL_MIDI_COMMAND_REDO 7

#define NUM_CLUSTERS_LOADED_AHEAD 2

#define INPUT_MONITORING_SMART 0
#define INPUT_MONITORING_ON 1
#define INPUT_MONITORING_OFF 2
#define NUM_INPUT_MONITORING_MODES 3

#define CACHE_LINE_SIZE 32

#define CLUSTER_DONT_LOAD 0
#define CLUSTER_ENQUEUE 1
#define CLUSTER_LOAD_IMMEDIATELY 2
#define CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE 3

#define SCALE_TYPE_SCALE 0
#define SCALE_TYPE_CHROMATIC 1
#define SCALE_TYPE_KIT 2

#define ARM_STATE_OFF 0
#define ARM_STATE_ON_NORMAL 1 // Arming to stop or start normally, or to stop soloing
#define ARM_STATE_ON_TO_SOLO 2

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

#define DRUM_TYPE_SOUND 0
#define DRUM_TYPE_MIDI 1
#define DRUM_TYPE_GATE 2

#define PGM_CHANGE_SEND_NEVER 0
#define PGM_CHANGE_SEND_ONCE 1

#define MARKER_NONE -1
#define MARKER_START 0
#define MARKER_LOOP_START 1
#define MARKER_LOOP_END 2
#define MARKER_END 3
#define NUM_MARKER_TYPES 4

#define INTERPOLATION_MODE_LINEAR 0
#define INTERPOLATION_MODE_SMOOTH 1
#define NUM_INTERPOLATION_MODES 2

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

#define CLUSTER_EMPTY 0
#define CLUSTER_SAMPLE 1
#define CLUSTER_GENERAL_MEMORY 2
#define CLUSTER_SAMPLE_CACHE 3
#define CLUSTER_PERC_CACHE_FORWARDS 4
#define CLUSTER_PERC_CACHE_REVERSED 5
#define CLUSTER_OTHER 6

#define PLAY_HEAD_OLDER 0
#define PLAY_HEAD_NEWER 1

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

#define AUDIO_INPUT_CHANNEL_NONE 0
#define AUDIO_INPUT_CHANNEL_LEFT 1
#define AUDIO_INPUT_CHANNEL_RIGHT 2
#define AUDIO_INPUT_CHANNEL_STEREO 3
#define AUDIO_INPUT_CHANNEL_BALANCED 4
#define AUDIO_INPUT_CHANNEL_MIX 5
#define AUDIO_INPUT_CHANNEL_OUTPUT 6

#define AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION 5

#define ACTION_RESULT_DEALT_WITH 0
#define ACTION_RESULT_NOT_DEALT_WITH 1
#define ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE 2
#define ACTION_RESULT_ACTIONED_AND_CAUSED_CHANGE 3

#define ENABLE_CLIP_CUTTING_DIAGNOSTICS 1

#define AUDIO_CLIP_MARGIN_SIZE_POST_END 2048

// Let's just do a 100 sample crossfade. Even 12 samples actually sounded fine for my voice - just obviously not so good for a low sine wave.
// Of course, if like 60 samples are being processed at a time under CPU load, then this might end up as low as 40.
#define ANTI_CLICK_CROSSFADE_LENGTH 100

#define AUDIO_CLIP_DEFAULT_ATTACK_IF_PRE_MARGIN (7 * 85899345 - 2147483648)

#define AUDIO_RECORDING_FOLDER_CLIPS 0
#define AUDIO_RECORDING_FOLDER_RECORD 1
#define AUDIO_RECORDING_FOLDER_RESAMPLE 2
#define NUM_AUDIO_RECORDING_FOLDERS 3

#define MIDI_CC_FOR_COMMANDS_ENABLED 0 // Was partially developed I think.

#define KEYBOARD_LAYOUT_QWERTY 0
#define KEYBOARD_LAYOUT_AZERTY 1
#define KEYBOARD_LAYOUT_QWERTZ 2
#define NUM_KEYBOARD_LAYOUTS 3

#define INTERNAL_BUTTON_PRESS_LATENCY 380
#define MIDI_KEY_INPUT_LATENCY 100

#define LINEAR_RECORDING_EARLY_FIRST_NOTE_ALLOWANCE (100 * 44) // In samples

#define LOOP_LOW_LEVEL 1
#define LOOP_TIMESTRETCHER_LEVEL_IF_ACTIVE 2 // Will cause low-level looping if no time-stretching

#define TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED 4

#define INTERNAL_MEMORY_END 0x20300000
#define PROGRAM_STACK_MAX_SIZE 8192

#define STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA 0
#define STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_CONVERTED 1 // E.g. from floating point file, or wrong endianness AIFF file.
#define STEALABLE_QUEUE_NO_SONG_WAVETABLE_BAND_DATA 2
#define STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_REPITCHED_CACHE 3
#define STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_PERC_CACHE 4
#define STEALABLE_QUEUE_NO_SONG_AUDIO_FILE_OBJECTS 5
#define STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA 6
#define STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_CONVERTED 7
#define STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE 8
#define STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_PERC_CACHE                                                            \
	9 // This one is super valuable and compacted data - lots of work to load it all again
#define NUM_STEALABLE_QUEUES 10

#define TIMER_MIDI_GATE_OUTPUT 2
#define TIMER_SYSTEM_FAST 0
#define TIMER_SYSTEM_SLOW 4
#define TIMER_SYSTEM_SUPERFAST 1

#define SSI_TX_BUFFER_NUM_SAMPLES 128
#define SSI_RX_BUFFER_NUM_SAMPLES 2048
#define NUM_MONO_INPUT_CHANNELS (NUM_STEREO_INPUT_CHANNELS * 2)
#define NUM_MONO_OUTPUT_CHANNELS (NUM_STEREO_OUTPUT_CHANNELS * 2)

#define MAX_NUM_USB_MIDI_DEVICES 6

#define UNDEFINED_GREY_SHADE 7

#define HAVE_SEQUENCE_STEP_CONTROL 1

#define SEQUENCE_DIRECTION_FORWARD 0
#define SEQUENCE_DIRECTION_REVERSE 1
#define SEQUENCE_DIRECTION_PINGPONG 2
#define SEQUENCE_DIRECTION_OBEY_PARENT 3
#define NUM_SEQUENCE_DIRECTION_OPTIONS 3

#define AUDIO_FILE_TYPE_SAMPLE 0
#define AUDIO_FILE_TYPE_WAVETABLE 1

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
#define MIDI_CHANNEL_NONE 255

// To be used instead of MIDI_CHANNEL_MPE_LOWER_ZONE etc for functions that require a "midi output filter". Although in
// fact, any number <16 or >=18 would work, the way I've defined it.
#define MIDI_OUTPUT_FILTER_NO_MPE 0

#define AUTOMATED_TESTER_ENABLED (0 && ALPHA_OR_BETA_VERSION)

// OLED -----------------
#define OLED_MAIN_WIDTH_PIXELS 128

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

#endif
