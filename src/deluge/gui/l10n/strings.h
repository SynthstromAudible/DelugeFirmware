#pragma once
#include "gui/l10n/strings.h"
#include <array>
#include <cstddef>

namespace deluge::l10n {
// Strings that can be localized
enum class String : size_t {
	EMPTY_STRING,

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
	STRING_FOR_PARAM_LOCAL_LPF_MORPH,
	STRING_FOR_PARAM_LOCAL_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST,
	STRING_FOR_PARAM_LOCAL_HPF_FREQ,
	STRING_FOR_PARAM_LOCAL_HPF_MORPH,
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
	STRING_FOR_DISABLED,
	STRING_FOR_ENABLED,
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

	// gui/context_menu/launch_style.cpp
	STRING_FOR_DEFAULT_LAUNCH,
	STRING_FOR_FILL_LAUNCH,

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

	// gui/menu_item/arpeggiator/mode.h
	STRING_FOR_UP,
	STRING_FOR_DOWN,
	STRING_FOR_BOTH,
	STRING_FOR_RANDOM,

	// gui/menu_item/cv/selection.h
	STRING_FOR_CV_OUTPUT_N,
	STRING_FOR_CV_OUTPUT_1,
	STRING_FOR_CV_OUTPUT_2,
	// gui/menu_item/delay/analog.h
	STRING_FOR_DIGITAL,
	STRING_FOR_ANALOG,

	// gui/menu_item/filter/lpf_mode.h
	STRING_FOR_DRIVE,
	STRING_FOR_HPLADDER,
	STRING_FOR_12DB_LADDER,
	STRING_FOR_24DB_LADDER,
	STRING_FOR_SVF_BAND,
	STRING_FOR_SVF_NOTCH,

	// gui/menu_item/flash/status.h
	STRING_FOR_FAST,
	STRING_FOR_SLOW,

	// gui/menu_item/gate/mode.h
	STRING_FOR_V_TRIGGER,
	STRING_FOR_S_TRIGGER,
	STRING_FOR_CLOCK,
	STRING_FOR_RUN_SIGNAL,

	// gui/menu_item/gate/selection.h
	STRING_FOR_GATE_MODE_TITLE,
	STRING_FOR_GATE_OUTPUT_1,
	STRING_FOR_GATE_OUTPUT_2,
	STRING_FOR_GATE_OUTPUT_3,
	STRING_FOR_GATE_OUTPUT_4,
	STRING_FOR_MINIMUM_OFF_TIME,

	// gui/menu_item/lfo/shape.h
	STRING_FOR_SINE,
	STRING_FOR_TRIANGLE,
	STRING_FOR_SQUARE,
	STRING_FOR_SAW,
	STRING_FOR_SAMPLE_AND_HOLD,
	STRING_FOR_RANDOM_WALK,

	// gui/menu_item/midi/takeover.h
	STRING_FOR_JUMP,
	STRING_FOR_PICK_UP,
	STRING_FOR_SCALE,

	// gui/menu_item/mod_fx/type.h
	STRING_FOR_FLANGER,
	STRING_FOR_CHORUS,
	STRING_FOR_PHASER,
	STRING_FOR_STEREO_CHORUS,

	// gui/menu_item/modulator/destination.h
	STRING_FOR_CARRIERS,
	STRING_FOR_MODULATOR_1,
	STRING_FOR_MODULATOR_2,

	// gui/menu_item/monitor/mode.h
	STRING_FOR_CONDITIONAL,

	STRING_FOR_LOWER_ZONE,
	STRING_FOR_UPPER_ZONE,

	STRING_FOR_ANALOG_SQUARE,
	STRING_FOR_ANALOG_SAW,
	STRING_FOR_SAMPLE,
	STRING_FOR_INPUT,
	STRING_FOR_INPUT_LEFT,
	STRING_FOR_INPUT_RIGHT,
	STRING_FOR_INPUT_STEREO,

	STRING_FOR_AUTOMATION_DELETED,

	STRING_FOR_RANGE_CONTAINS_ONE_NOTE,
	STRING_FOR_LAST_RANGE_CANT_DELETE,
	STRING_FOR_RANGE_DELETED,
	STRING_FOR_BOTTOM,

	STRING_FOR_SHORTCUTS_VERSION_1,
	STRING_FOR_SHORTCUTS_VERSION_3,
	STRING_FOR_CANT_RECORD_AUDIO_FM_MODE,

	//Autogen
	STRING_FOR_FACTORY_RESET,
	STRING_FOR_CLIPPING_OCCURRED,
	STRING_FOR_NO_SAMPLE,
	STRING_FOR_PARAMETER_NOT_APPLICABLE,
	STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE,
	STRING_FOR_UNIMPLEMENTED,
	STRING_FOR_CANT_LEARN,
	STRING_FOR_FOLDERS_CANNOT_BE_DELETED_ON_THE_DELUGE,
	STRING_FOR_ERROR_CREATING_MULTISAMPLED_INSTRUMENT,
	STRING_FOR_CLIP_IS_RECORDING,
	STRING_FOR_AUDIO_FILE_IS_USED_IN_CURRENT_SONG,
	STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT,
	STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED,
	STRING_FOR_UNLOADED_PARTS,
	STRING_FOR_SD_CARD_ERROR,
	STRING_FOR_ERROR_LOADING_SONG,
	STRING_FOR_DUPLICATE_NAMES,
	STRING_FOR_PRESET_SAVED,
	STRING_FOR_SAME_NAME,
	STRING_FOR_SONG_SAVED,
	STRING_FOR_ERROR_MOVING_TEMP_FILES,
	STRING_FOR_OVERDUBS_PENDING,
	STRING_FOR_INSTRUMENTS_WITH_CLIPS_CANT_BE_TURNED_INTO_AUDIO_TRACKS,
	STRING_FOR_NO_FREE_CHANNEL_SLOTS_AVAILABLE_IN_SONG,
	STRING_FOR_MIDI_MUST_BE_LEARNED_TO_KIT_ITEMS_INDIVIDUALLY,
	STRING_FOR_AUDIO_TRACKS_WITH_CLIPS_CANT_BE_TURNED_INTO_AN_INSTRUMENT,
	STRING_FOR_ARRANGEMENT_CLEARED,
	STRING_FOR_EMPTY_CLIP_INSTANCES_CANT_BE_MOVED_TO_THE_SESSION,
	STRING_FOR_AUDIO_CLIP_CLEARED,
	STRING_FOR_CANT_EDIT_LENGTH,
	STRING_FOR_QUANTIZE_ALL_ROW,
	STRING_FOR_QUANTIZE,
	STRING_FOR_VELOCITY_DECREASED,
	STRING_FOR_VELOCITY_INCREASED,
	STRING_FOR_RANDOMIZED,
	STRING_FOR_MAXIMUM_LENGTH_REACHED,
	STRING_FOR_NOTES_PASTED,
	STRING_FOR_AUTOMATION_PASTED,
	STRING_FOR_CANT_PASTE_AUTOMATION,
	STRING_FOR_NO_AUTOMATION_TO_PASTE,
	STRING_FOR_NOTES_COPIED,
	STRING_FOR_NO_AUTOMATION_TO_COPY,
	STRING_FOR_AUTOMATION_COPIED,
	STRING_FOR_DELETED_UNUSED_ROWS,
	STRING_FOR_AT_LEAST_ONE_ROW_NEEDS_TO_HAVE_NOTES,
	STRING_FOR_RECORDING_IN_PROGRESS,
	STRING_FOR_CANT_REMOVE_FINAL_CLIP,
	STRING_FOR_CLIP_NOT_EMPTY,
	STRING_FOR_RECORDING_TO_ARRANGEMENT,
	STRING_FOR_CANT_CREATE_OVERDUB_WHILE_CLIPS_SOLOING,
	STRING_FOR_CLIP_WOULD_BREACH_MAX_ARRANGEMENT_LENGTH,
	STRING_FOR_NO_UNUSED_CHANNELS,
	STRING_FOR_CLIP_HAS_INSTANCES_IN_ARRANGER,
	STRING_FOR_CANT_SAVE_WHILE_OVERDUBS_PENDING,
	STRING_FOR_DIN_PORTS,
	STRING_FOR_UPSTREAM_USB_PORT_3_SYSEX,
	STRING_FOR_UPSTREAM_USB_PORT_2,
	STRING_FOR_UPSTREAM_USB_PORT_1,
	STRING_FOR_HELLO_SYSEX,
	STRING_FOR_OTHER_SCALE,
	STRING_FOR_CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES_IN_USE,
	STRING_FOR_CLIP_CLEARED,
	STRING_FOR_NO_FURTHER_UNUSED_INSTRUMENT_NUMBERS,
	STRING_FOR_CHANNEL_PRESSURE,
	STRING_FOR_PITCH_BEND,
	STRING_FOR_NO_PARAM,
	STRING_FOR_NO_FURTHER_UNUSED_MIDI_PARAMS,
	STRING_FOR_ANALOG_DELAY,
	STRING_FOR_DIGITAL_DELAY,
	STRING_FOR_NORMAL_DELAY,
	STRING_FOR_PINGPONG_DELAY,
	STRING_FOR_NO_UNUSED_CHANNELS_AVAILABLE,
	STRING_FOR_NO_AVAILABLE_CHANNELS,
	STRING_FOR_PARAMETER_AUTOMATION_DELETED,
	STRING_FOR_CREATE_OVERDUB_FROM_WHICH_CLIP,
	STRING_FOR_AUDIO_TRACK_HAS_NO_INPUT_CHANNEL,
	STRING_FOR_CANT_GRAB_TEMPO_FROM_CLIP,
	STRING_FOR_SYNC_NUDGED,
	STRING_FOR_FOLLOWING_EXTERNAL_CLOCK,
	STRING_FOR_SLOW_SIDECHAIN_COMPRESSOR,
	STRING_FOR_FAST_SIDECHAIN_COMPRESSOR,
	STRING_FOR_REBOOT_TO_USE_THIS_SD_CARD,
	STRING_FOR_LARGE_ROOM_REVERB,
	STRING_FOR_MEDIUM_ROOM_REVERB,
	STRING_FOR_SMALL_ROOM_REVERB,
	STRING_FOR_USB_DEVICES_MAX,
	STRING_FOR_USB_DEVICE_DETACHED,
	STRING_FOR_USB_HUB_ATTACHED,
	STRING_FOR_USB_DEVICE_NOT_RECOGNIZED,

	STRING_FOR_SYNC_TYPE_TRIPLET,
	STRING_FOR_SYNC_TYPE_DOTTED,
	STRING_FOR_SYNC_TYPE_EVEN,
	STRING_FOR_SYNC_LEVEL_WHOLE,
	STRING_FOR_SYNC_LEVEL_2ND,
	STRING_FOR_SYNC_LEVEL_4TH,
	STRING_FOR_SYNC_LEVEL_8TH,
	STRING_FOR_SYNC_LEVEL_16TH,
	STRING_FOR_SYNC_LEVEL_32ND,
	STRING_FOR_SYNC_LEVEL_64TH,
	STRING_FOR_SYNC_LEVEL_128TH,
	STRING_FOR_SYNC_LEVEL_256TH,

	STRING_FOR_OUT,
	STRING_FOR_IN,

	STRING_FOR_DEV_MENU_A,
	STRING_FOR_DEV_MENU_B,
	STRING_FOR_DEV_MENU_C,
	STRING_FOR_DEV_MENU_D,
	STRING_FOR_DEV_MENU_E,
	STRING_FOR_DEV_MENU_F,
	STRING_FOR_DEV_MENU_G,
	STRING_FOR_ENVELOPE_1,
	STRING_FOR_ENVELOPE_2,
	STRING_FOR_VOLUME_LEVEL,
	STRING_FOR_AMOUNT_LEVEL,
	STRING_FOR_REPEAT_MODE,
	STRING_FOR_PITCH_SPEED,
	STRING_FOR_OSCILLATOR_SYNC,
	STRING_FOR_OSCILLATOR_1,
	STRING_FOR_OSCILLATOR_2,
	STRING_FOR_UNISON_NUMBER,
	STRING_FOR_UNISON_DETUNE,
	STRING_FOR_UNISON_STEREO_SPREAD,
	STRING_FOR_NUMBER_OF_OCTAVES,
	STRING_FOR_SHAPE,
	STRING_FOR_MOD_FX,
	STRING_FOR_BASS_FREQUENCY,
	STRING_FOR_TREBLE_FREQUENCY,
	STRING_FOR_POLY_FINGER_MPE,
	STRING_FOR_REVERB_SIDECHAIN,
	STRING_FOR_ROOM_SIZE,
	STRING_FOR_BITCRUSH,
	STRING_FOR_SUB_BANK,
	STRING_FOR_PLAY_DIRECTION,
	STRING_FOR_SWING_INTERVAL,
	STRING_FOR_SHORTCUTS_VERSION,
	STRING_FOR_KEYBOARD_FOR_TEXT,
	STRING_FOR_LOOP_MARGINS,
	STRING_FOR_SAMPLING_MONITORING,
	STRING_FOR_SAMPLE_PREVIEW,
	STRING_FOR_PLAY_CURSOR,
	STRING_FOR_FIRMWARE_VERSION,
	STRING_FOR_COMMUNITY_FTS,
	STRING_FOR_MIDI_THRU,
	STRING_FOR_TAKEOVER,
	STRING_FOR_RECORD,
	STRING_FOR_COMMANDS,
	STRING_FOR_OUTPUT,
	STRING_FOR_TEMPO_MAGNITUDE_MATCHING,
	STRING_FOR_TRIGGER_CLOCK,
	STRING_FOR_FM_MODULATOR_1,
	STRING_FOR_FM_MODULATOR_2,
	STRING_FOR_NOISE_LEVEL,
	STRING_FOR_MASTER_TRANSPOSE,
	STRING_FOR_SYNTH_MODE,
	STRING_FOR_FILTER_ROUTE,
	STRING_FOR_DEFAULT_FX,
	STRING_FOR_DEFAULT_FX_DELAY,
	STRING_FOR_DEFAULT_FX_DELAY_SYNC,
	STRING_FOR_DEFAULT_FX_DELAY_SYNC_TYPE,
	STRING_FOR_DEFAULT_FX_DELAY_SYNC_LEVEL,

	STRING_FOR_TRACK_STILL_HAS_CLIPS_IN_SESSION,
	STRING_FOR_DELETE_ALL_TRACKS_CLIPS_FIRST,
	STRING_FOR_CANT_DELETE_FINAL_CLIP,
	STRING_FOR_NEW_SYNTH_CREATED,
	STRING_FOR_NEW_KIT_CREATED,

	STRING_FOR_CAN_ONLY_IMPORT_WHOLE_FOLDER_INTO_BRAND_NEW_KIT,
	STRING_FOR_CANT_IMPORT_WHOLE_FOLDER_INTO_AUDIO_CLIP,

	STRING_FOR_IMPOSSIBLE_FROM_GRID,
	STRING_FOR_SWITCHING_TO_TRACK_FAILED,
	STRING_FOR_CANT_CLONE_AUDIO_IN_OTHER_TRACK,
	STRING_FOR_CANT_CONVERT_TYPE,
	STRING_FOR_TARGET_FULL,

	// Auto-extracted from menus.cpp
	STRING_FOR_LPF_MORPH,
	STRING_FOR_LPF_MODE,
	STRING_FOR_MORPH,
	STRING_FOR_HPF_MORPH,
	STRING_FOR_HPF_MODE,
	STRING_FOR_DECAY,
	STRING_FOR_SUSTAIN,
	STRING_FOR_WAVE_INDEX,
	STRING_FOR_RECORD_AUDIO,
	STRING_FOR_START_POINT,
	STRING_FOR_END_POINT,
	STRING_FOR_SPEED,
	STRING_FOR_INTERPOLATION,
	STRING_FOR_PULSE_WIDTH,
	STRING_FOR_UNISON,
	STRING_FOR_MODE,
	STRING_FOR_ARPEGGIATOR,
	STRING_FOR_POLYPHONY,
	STRING_FOR_PORTAMENTO,
	STRING_FOR_PRIORITY,
	STRING_FOR_VOICE,
	STRING_FOR_DESTINATION,
	STRING_FOR_RETRIGGER_PHASE,
	STRING_FOR_LFO1_TYPE,
	STRING_FOR_LFO1_RATE,
	STRING_FOR_LFO1_SYNC,
	STRING_FOR_LFO1,
	STRING_FOR_LFO2_TYPE,
	STRING_FOR_LFO2_RATE,
	STRING_FOR_LFO2,
	STRING_FOR_MODFX_TYPE,
	STRING_FOR_MODFX_RATE,
	STRING_FOR_FEEDBACK,
	STRING_FOR_MODFX_FEEDBACK,
	STRING_FOR_MODFX_DEPTH,
	STRING_FOR_OFFSET,
	STRING_FOR_MODFX_OFFSET,
	STRING_FOR_BASS,
	STRING_FOR_TREBLE,
	STRING_FOR_EQ,
	STRING_FOR_PINGPONG,
	STRING_FOR_DELAY_PINGPONG,
	STRING_FOR_DELAY_TYPE,
	STRING_FOR_DELAY_SYNC,
	STRING_FOR_NORMAL,
	STRING_FOR_SEND_TO_SIDECHAIN,
	STRING_FOR_SEND_TO_SIDECH_MENU_TITLE,
	STRING_FOR_SYNC,
	STRING_FOR_SIDECHAIN_SYNC,
	STRING_FOR_RELEASE,
	STRING_FOR_SIDECH_RELEASE_MENU_TITLE,
	STRING_FOR_SIDECHAIN_COMP_MENU_TITLE,
	STRING_FOR_DAMPENING,
	STRING_FOR_WIDTH,
	STRING_FOR_REVERB_WIDTH,
	STRING_FOR_REVERB_PAN,
	STRING_FOR_SATURATION,
	STRING_FOR_DECIMATION,
	STRING_FOR_BANK,
	STRING_FOR_MIDI_BANK,
	STRING_FOR_MIDI_SUB_BANK,
	STRING_FOR_PGM,
	STRING_FOR_REVERSE,
	STRING_FOR_WAVEFORM,
	STRING_FOR_LPF_FREQUENCY,
	STRING_FOR_LPF_RESONANCE,
	STRING_FOR_LPF,
	STRING_FOR_FREQUENCY,
	STRING_FOR_HPF_FREQUENCY,
	STRING_FOR_RESONANCE,
	STRING_FOR_HPF_RESONANCE,
	STRING_FOR_HPF,
	STRING_FOR_TYPE,
	STRING_FOR_MOD_FX_TYPE,
	STRING_FOR_MOD_FX_RATE,
	STRING_FOR_DEPTH,
	STRING_FOR_MOD_FX_DEPTH,
	STRING_FOR_DELAY_AMOUNT,
	STRING_FOR_RATE,
	STRING_FOR_DELAY_RATE,
	STRING_FOR_DELAY,
	STRING_FOR_AMOUNT,
	STRING_FOR_REVERB_AMOUNT,
	STRING_FOR_REVERB,
	STRING_FOR_VOLUME_DUCKING,
	STRING_FOR_SIDECHAIN_COMPRESSOR,
	STRING_FOR_ATTACK,
	STRING_FOR_FX,
	STRING_FOR_VOLTS_PER_OCTAVE,
	STRING_FOR_TRANSPOSE,
	STRING_FOR_CV,
	STRING_FOR_CV_OUTPUTS,
	STRING_FOR_GATE,
	STRING_FOR_GATE_OUTPUTS,
	STRING_FOR_KEY_LAYOUT,
	STRING_FOR_COLOURS,
	STRING_FOR_PADS,
	STRING_FOR_QUANTIZATION,
	STRING_FOR_COUNT_IN,
	STRING_FOR_REC_COUNT_IN,
	STRING_FOR_MONITORING,
	STRING_FOR_RECORDING,
	STRING_FOR_RESTART,
	STRING_FOR_PLAY,
	STRING_FOR_TAP_TEMPO,
	STRING_FOR_UNDO,
	STRING_FOR_REDO,
	STRING_FOR_LOOP,
	STRING_FOR_LAYERING_LOOP,
	STRING_FOR_MIDI_COMMANDS,
	STRING_FOR_DIFFERENTIATE_INPUTS,
	STRING_FOR_MIDI_CLOCK_OUT,
	STRING_FOR_MIDI_CLOCK_IN,
	STRING_FOR_DEVICES,
	STRING_FOR_MIDI_DEVICES,
	STRING_FOR_MPE,
	STRING_FOR_MIDI_CLOCK,
	STRING_FOR_MIDI,
	STRING_FOR_INPUT_PPQN,
	STRING_FOR_AUTO_START,
	STRING_FOR_PPQN,
	STRING_FOR_OUTPUT_PPQN,
	STRING_FOR_TEMPO,
	STRING_FOR_DEFAULT_TEMPO,
	STRING_FOR_SWING,
	STRING_FOR_DEFAULT_SWING,
	STRING_FOR_KEY,
	STRING_FOR_DEFAULT_KEY,
	STRING_FOR_DEFAULT_SCALE,
	STRING_FOR_VELOCITY,
	STRING_FOR_RESOLUTION,
	STRING_FOR_DEFAULT_BEND_R,
	STRING_FOR_DEFAULTS,
	STRING_FOR_VIBRATO,
	STRING_FOR_NAME,
	STRING_FOR_BEND_RANGE,
	STRING_FOR_MASTER_LEVEL,
	STRING_FOR_PAN,
	STRING_FOR_SOUND,
	STRING_FOR_AUDIO_CLIP,
	STRING_FOR_SETTINGS,

	// MENU TITLES
	STRING_FOR_ENV_ATTACK_MENU_TITLE,
	STRING_FOR_ENV_DECAY_MENU_TITLE,
	STRING_FOR_ENV_SUSTAIN_MENU_TITLE,
	STRING_FOR_ENV_RELEASE_MENU_TITLE,
	STRING_FOR_OSC_TYPE_MENU_TITLE,
	STRING_FOR_OSC_WAVE_IND_MENU_TITLE,
	STRING_FOR_OSC_LEVEL_MENU_TITLE,
	STRING_FOR_CARRIER_FEED_MENU_TITLE,
	STRING_FOR_SAMP_REVERSE_MENU_TITLE,
	STRING_FOR_SAMP_REPEAT_MENU_TITLE,
	STRING_FOR_OSC_TRANSPOSE_MENU_TITLE,
	STRING_FOR_SAMP_SPEED_MENU_TITLE,
	STRING_FOR_SAMP_INTERP_MENU_TITLE,
	STRING_FOR_OSC_P_WIDTH_MENU_TITLE,
	STRING_FOR_OSC_R_PHASE_MENU_TITLE,
	STRING_FOR_ARP_MODE_MENU_TITLE,
	STRING_FOR_ARP_SYNC_MENU_TITLE,
	STRING_FOR_ARP_OCTAVES_MENU_TITLE,
	STRING_FOR_ARP_GATE_MENU_TITLE,
	STRING_FOR_ARP_RATE_MENU_TITLE,
	STRING_FOR_FM_MOD_TRAN_MENU_TITLE,
	STRING_FOR_FM_MOD_LEVEL_MENU_TITLE,
	STRING_FOR_FM_MOD_FBACK_MENU_TITLE,
	STRING_FOR_FM_MOD2_DEST_MENU_TITLE,
	STRING_FOR_FM_MOD_RETRIG_MENU_TITLE,
	STRING_FOR_SIDECH_ATTACK_MENU_TITLE,
	STRING_FOR_SIDECH_SHAPE_MENU_TITLE,
	STRING_FOR_REVERB_SIDECH_MENU_TITLE,
	STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE,
	STRING_FOR_CV_V_PER_OCTAVE_MENU_TITLE,
	STRING_FOR_CV_TRANSPOSE_MENU_TITLE,
	STRING_FOR_SHORTCUTS_VER_MENU_TITLE,
	STRING_FOR_FIRMWARE_VER_MENU_TITLE,
	STRING_FOR_COMMUNITY_FTS_MENU_TITLE,
	STRING_FOR_TEMPO_M_MATCH_MENU_TITLE,
	STRING_FOR_T_CLOCK_INPUT_MENU_TITLE,
	STRING_FOR_T_CLOCK_OUT_MENU_TITLE,
	STRING_FOR_DEFAULT_VELOC_MENU_TITLE,
	STRING_FOR_DEFAULT_RESOL_MENU_TITLE,
	STRING_FOR_MASTER_TRAN_MENU_TITLE,
	STRING_FOR_MIDI_INST_MENU_TITLE,
	STRING_FOR_NUM_MEMBER_CH_MENU_TITLE,

	STRING_FOR_CV_INSTRUMENT,

	STRING_FOR_ACTIVE,
	STRING_FOR_STOPPED,
	STRING_FOR_MUTED,
	STRING_FOR_SOLOED,

	STRING_FOR_FILE_BROWSER,

	STRING_FOR_LOW,
	STRING_FOR_MEDIUM,
	STRING_FOR_HIGH,
	STRING_FOR_AUTO,
	STRING_FOR_POLYPHONIC,
	STRING_FOR_MONOPHONIC,
	STRING_FOR_LEGATO,
	STRING_FOR_CHOKE,
	STRING_FOR_MODULATE_WITH,
	STRING_FOR_MODULATE_DEPTH,
	STRING_FOR_FORWARD,
	STRING_FOR_REVERSED,
	STRING_FOR_PING_PONG,
	STRING_FOR_NONE,
	STRING_FOR_OFF,
	STRING_FOR_ON,
	STRING_FOR_CUT,
	STRING_FOR_ONCE,
	STRING_FOR_STRETCH,
	STRING_FOR_LINKED,
	STRING_FOR_INDEPENDENT,
	STRING_FOR_LINEAR,
	STRING_FOR_SINC,
	STRING_FOR_MPE_OUTPUT,
	STRING_FOR_MPE_INPUT,
	STRING_FOR_SUBTRACTIVE,
	STRING_FOR_FM,
	STRING_FOR_RINGMOD,
	STRING_FOR_NOTE_RANGE,
	STRING_FOR_PARALLEL,
	STRING_FOR_RED,
	STRING_FOR_GREEN,
	STRING_FOR_BLUE,
	STRING_FOR_YELLOW,
	STRING_FOR_CYAN,
	STRING_FOR_MAGENTA,
	STRING_FOR_AMBER,
	STRING_FOR_WHITE,
	STRING_FOR_PINK,
	STRING_FOR_COMMAND_UNASSIGNED,
	STRING_FOR_ANY_MIDI_DEVICE,
	STRING_FOR_MPE_LOWER_ZONE,
	STRING_FOR_MPE_UPPER_ZONE,
	STRING_FOR_CHANNEL,
	STRING_FOR_SET,
	STRING_FOR_UNLEARNED,
	STRING_FOR_LEARNED,
	STRING_FOR_RANGE_INSERTED,
	STRING_FOR_INSERT,
	STRING_FOR_SIDECHAIN_SHAPE,
	STRING_FOR_WAVEFOLDER,
	STRING_FOR_MASTER_PITCH,
	STRING_FOR_MASTER_PAN,
	STRING_FOR_SIDECHAIN_LEVEL,

	STRING_FOR_AUTOMATION,
	STRING_FOR_LOOP_TOO_SHORT,
	STRING_FOR_LOOP_HALVED,
	STRING_FOR_LOOP_TOO_LONG,
	STRING_FOR_LOOP_DOUBLED,
	STRING_FOR_CANT_AUTOMATE_CV,
	STRING_FOR_SHIFT_RIGHT,
	STRING_FOR_SHIFT_LEFT,
	STRING_FOR_INTERPOLATION_DISABLED,
	STRING_FOR_INTERPOLATION_ENABLED,
	STRING_FOR_COMING_SOON,

	STRING_FOR_WAVEFOLD,
	STRING_FOR_MOD_MATRIX,

	STRING_FOR_CHECKSUM_FAIL,
	STRING_FOR_WRONG_SIZE,
	STRING_FOR_NOTES_CLEARED,
	STRING_FOR_AUTOMATION_CLEARED,
	STRING_FOR_GRAIN,
	STRING_FOR_FILL,
	STRING_FOR_PAD_SELECTION_OFF,
	STRING_FOR_PAD_SELECTION_ON,

	STRING_LAST
};

// The maximum number of strings that can be localized
constexpr size_t kNumStrings = static_cast<size_t>(String::STRING_LAST);

} // namespace deluge::l10n
