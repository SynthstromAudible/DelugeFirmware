#pragma once
#include "gui/l10n/english.h"
#include "gui/l10n/language.h"
#include "gui/l10n/strings.h"

namespace deluge::l10n::built_in {
using enum String;
constexpr Language seven_segment{
    "Seven Segment",
    {
        //Errors
        {STRING_FOR_ERROR_INSUFFICIENT_RAM, "RAM"},
        {STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE, "RAM"},
        {STRING_FOR_ERROR_SD_CARD, "CARD"},
        {STRING_FOR_ERROR_SD_CARD_NOT_PRESENT, "CARD"},
        {STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM, "CARD"},
        {STRING_FOR_ERROR_FILE_CORRUPTED, "CORRUPTED"},
        {STRING_FOR_ERROR_FILE_NOT_FOUND, "FILE"},
        {STRING_FOR_ERROR_FILE_UNREADABLE, "FILE"},
        {STRING_FOR_ERROR_FILE_UNSUPPORTED, "UNSUPPORTED"},
        {STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW, "FIRMWARE"},
        {STRING_FOR_ERROR_FOLDER_DOESNT_EXIST, "FOLDER"},
        {STRING_FOR_ERROR_BUG, "BUG"},
        {STRING_FOR_ERROR_WRITE_FAIL, "FAIL"},
        {STRING_FOR_ERROR_FILE_TOO_BIG, "BIG"},
        {STRING_FOR_ERROR_PRESET_IN_USE, "USED"},
        {STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION, "NONE"},
        {STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED, "4GB"},
        {STRING_FOR_ERROR_SD_CARD_FULL, "FULL"},
        {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE, "CANT"},
        {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO, "STEREO"},
        {STRING_FOR_ERROR_WRITE_PROTECTED, "WRITE PROTECTED"},
        {STRING_FOR_ERROR_GENERIC, "ERROR"},

        // Patch sources
        {STRING_FOR_PATCH_SOURCE_LFO_GLOBAL, "LFO1"},
        {STRING_FOR_PATCH_SOURCE_LFO_LOCAL, "LFO2"},
        {STRING_FOR_PATCH_SOURCE_ENVELOPE_0, "ENV1"},
        {STRING_FOR_PATCH_SOURCE_ENVELOPE_1, "ENV2"},
        {STRING_FOR_PATCH_SOURCE_VELOCITY, "VELOCITY"},
        {STRING_FOR_PATCH_SOURCE_NOTE, "NOTE"},
        {STRING_FOR_PATCH_SOURCE_COMPRESSOR, "SIDE"},
        {STRING_FOR_PATCH_SOURCE_RANDOM, "RANDOM"},
        {STRING_FOR_PATCH_SOURCE_AFTERTOUCH, "AFTERTOUCH"},
        {STRING_FOR_PATCH_SOURCE_X, "X"},
        {STRING_FOR_PATCH_SOURCE_Y, "Y"},

        /*
		 * Start Parameter Strings
		 */

        /*
		 * Where used:
		 * - automation_instrument_clip_view.cpp
		 */

        // Patched Params (originally from functions.cpp)

        //Master Level, Pitch, Pan
        {STRING_FOR_PARAM_LOCAL_VOLUME, "Level"},
        {STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX, "Master Level"},
        {STRING_FOR_PARAM_LOCAL_PAN, "Master Pan"},
        {STRING_FOR_PARAM_LOCAL_PITCH_ADJUST, "Master Pitch"},

        //LPF Frequency, Resonance, Morph
        {STRING_FOR_PARAM_LOCAL_LPF_FREQ, "LPF frequency"},
        {STRING_FOR_PARAM_LOCAL_LPF_RESONANCE, "LPF resonance"},
        {STRING_FOR_PARAM_LOCAL_LPF_MORPH, "LPF morph"},

        //HPF Frequency, Resonance, Morph
        {STRING_FOR_PARAM_LOCAL_HPF_FREQ, "HPF frequency"},
        {STRING_FOR_PARAM_LOCAL_HPF_RESONANCE, "HPF resonance"},
        {STRING_FOR_PARAM_LOCAL_HPF_MORPH, "HPF morph"},

        //Reverb Amount
        {STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT, "Reverb amount"},

        //Delay Rate, Amount
        {STRING_FOR_PARAM_GLOBAL_DELAY_RATE, "Delay rate"},
        {STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK, "Delay amount"},

        //Sidechain Level
        {STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND, "Sidechain Level"},

        //Wavefolder
        {STRING_FOR_WAVEFOLDER, "Wavefolder"},

        //OSC 1 Level, Pitch, Phase Width, Carrier Feedback, Wave Position
        {STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME, "Osc1 level"},
        {STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST, "Osc1 pitch"},
        {STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH, "Osc1 PW"},
        {STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK, "Osc1 feedback"},
        {STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX, "Osc1 wave pos."},

        //OSC 2 Level, Pitch, Phase Width, Carrier Feedback, Wave Position
        {STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME, "Osc2 level"},
        {STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST, "Osc2 pitch"},
        {STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH, "Osc2 PW"},
        {STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK, "Osc2 feedback"},
        {STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX, "Osc2 wave pos."},

        //FM Mod 1 Level, Pitch, Feedback
        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME, "FM mod1 level"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST, "FM mod1 pitch"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK, "FM mod1 feedback"},

        //FM Mod 2 Level, Pitch, Feedback
        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME, "FM mod2 level"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST, "FM mod2 pitch"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK, "FM mod2 feedback"},

        //Env 1 ADSR
        {STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK, "Env1 attack"},
        {STRING_FOR_PARAM_LOCAL_ENV_0_DECAY, "Env1 decay"},
        {STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN, "Env1 sustain"},
        {STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE, "Env1 release"},

        //Env 2 ADSR
        {STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK, "Env2 attack"},
        {STRING_FOR_PARAM_LOCAL_ENV_1_DECAY, "Env2 decay"},
        {STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN, "Env2 sustain"},
        {STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE, "Env2 release"},

        //LFO 1 Rate
        {STRING_FOR_PARAM_GLOBAL_LFO_FREQ, "LFO1 rate"},

        //LFO 2 Rate
        {STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ, "LFO2 rate"},

        //Mod FX Depth, Rate
        {STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH, "Mod-FX depth"},
        {STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE, "Mod-FX rate"},

        //Arp Rate
        {STRING_FOR_PARAM_GLOBAL_ARP_RATE, "Arp. rate"},

        //Noise
        {STRING_FOR_PARAM_LOCAL_NOISE_VOLUME, "Noise level"},

        // Unpatched Params (originally from functions.cpp)

        //Master Level, Pitch, Pan
        {STRING_FOR_MASTER_LEVEL, "Master level"},
        {STRING_FOR_MASTER_PITCH, "Master pitch"},
        {STRING_FOR_MASTER_PAN, "Master pan"},

        //LPF Frequency, Resonance
        {STRING_FOR_LPF_FREQUENCY, "LPF frequency"},
        {STRING_FOR_LPF_RESONANCE, "LPF resonance"},

        //HPF Frequency, Resonance
        {STRING_FOR_HPF_FREQUENCY, "HPF frequency"},
        {STRING_FOR_HPF_RESONANCE, "HPF resonance"},

        //Bass, Bass Freq
        {STRING_FOR_BASS, "BASS"},
        {STRING_FOR_BASS_FREQUENCY, "Bass frequency"},

        //Treble, Treble Freq
        {STRING_FOR_TREBLE, "TREBLE"},
        {STRING_FOR_TREBLE_FREQUENCY, "Treble frequency"},

        //Reverb Amount
        {STRING_FOR_REVERB_AMOUNT, "Reverb amount"},

        //Delay Rate, Amount
        {STRING_FOR_DELAY_RATE, "Delay rate"},
        {STRING_FOR_DELAY_AMOUNT, "Delay amount"},

        //Sidechain Level, Shape
        {STRING_FOR_SIDECHAIN_LEVEL, "Sidechain level"},
        {STRING_FOR_SIDECHAIN_SHAPE, "Sidechain shape"},

        //Decimation, Bitcrush
        {STRING_FOR_DECIMATION, "DECIMATION"},
        {STRING_FOR_BITCRUSH, "Bitcrush"},

        //Mod FX Offset, Feedback, Depth, Rate
        {STRING_FOR_MODFX_OFFSET, "MOD-FX offset"},
        {STRING_FOR_MODFX_FEEDBACK, "MOD-FX feedback"},
        {STRING_FOR_MODFX_DEPTH, "MOD-FX depth"},
        {STRING_FOR_MODFX_RATE, "MOD-FX rate"},

        //Arp Gate
        {STRING_FOR_ARP_GATE_MENU_TITLE, "Arp. gate"},

        //Portamento
        {STRING_FOR_PORTAMENTO, "PORTAMENTO"},

        /*
		 * End Parameter Strings
		 */

        // General
        {STRING_FOR_DISABLED, "OFF"},
        {STRING_FOR_ENABLED, "ON"},
        {STRING_FOR_OK, "OK"},
        {STRING_FOR_NEW, "NEW"},
        {STRING_FOR_DELETE, "DELETE"},
        {STRING_FOR_SURE, "SURE"},
        {STRING_FOR_OVERWRITE, "OVERWRITE"},
        {STRING_FOR_OPTIONS, "OPTIONS"},

        // gui/context_menu/audio_input_selector.cpp
        {STRING_FOR_LEFT_INPUT, "LEFT"},
        {STRING_FOR_LEFT_INPUT_MONITORING, "LEFT."},
        {STRING_FOR_RIGHT_INPUT, "RIGH"},
        {STRING_FOR_RIGHT_INPUT_MONITORING, "RIGH."},
        {STRING_FOR_STEREO_INPUT, "STER"},
        {STRING_FOR_STEREO_INPUT_MONITORING, "STER."},
        {STRING_FOR_BALANCED_INPUT, "BALA"},
        {STRING_FOR_BALANCED_INPUT_MONITORING, "BALA."},
        {STRING_FOR_MIX_PRE_FX, "MIX"},
        {STRING_FOR_MIX_POST_FX, "OUTP"},

        // gui/context_menu/launch_style.cpp
        {STRING_FOR_DEFAULT_LAUNCH, "DEFA"},
        {STRING_FOR_FILL_LAUNCH, "FILL"},

        // gui/context_menu/sample_browser/kit.cpp
        {STRING_FOR_LOAD_ALL, "ALL"},
        {STRING_FOR_SLICE, "SLICE"},

        // gui/context_menu/sample_browser/synth.cpp
        {STRING_FOR_MULTISAMPLES, "MULTI"},
        {STRING_FOR_BASIC, "BASIC"},
        {STRING_FOR_SINGLE_CYCLE, "SINGLE-CYCLE"},
        {STRING_FOR_WAVETABLE, "WAVETABLE"},

        // gui/context_menu/delete_file.cpp
        {STRING_FOR_ERROR_DELETING_FILE, "ERROR"},
        {STRING_FOR_FILE_DELETED, "DONE"},

        // gui/context_menu/load_instrument_preset.cpp
        {STRING_FOR_CLONE, "COPY"},

        // gui/context_menu/overwrite_bootloader.cpp
        {STRING_FOR_ACCEPT_RISK, "SURE"},
        {STRING_FOR_ERROR_BOOTLOADER_TOO_BIG, "BIG"},
        {STRING_FOR_ERROR_BOOTLOADER_TOO_SMALL, "SMALL"},
        {STRING_FOR_BOOTLOADER_UPDATED, "DONE"},
        {STRING_FOR_ERROR_BOOTLOADER_FILE_NOT_FOUND, "FILE"},

        // gui/context_menu/save_song_or_instrument.cpp
        {STRING_FOR_COLLECT_MEDIA, "COLLECT MEDIA"},
        {STRING_FOR_CREATE_FOLDER, "CREATE FOLDER"},

        // gui/menu_item/arpeggiator/mode.h
        {STRING_FOR_UP, "UP"},
        {STRING_FOR_DOWN, "DOWN"},
        {STRING_FOR_BOTH, "BOTH"},
        {STRING_FOR_RANDOM, "RANDOM"},

        // gui/menu_item/cv/selection.h
        {STRING_FOR_CV_OUTPUT_N, "OUT{}"},

        // gui/menu_item/delay/analog.h
        {STRING_FOR_DIGITAL, "DIGI"},
        {STRING_FOR_ANALOG, "ANA"},

        // gui/menu_item/filter/lpf_mode.h
        {STRING_FOR_DRIVE, "Drive"},
        {STRING_FOR_SVF, "SVF"},

        // gui/menu_item/flash/status.h
        {STRING_FOR_FAST, "FAST"},
        {STRING_FOR_SLOW, "SLOW"},

        // gui/menu_item/gate/mode.h
        {STRING_FOR_V_TRIGGER, "VTRI"},
        {STRING_FOR_S_TRIGGER, "STRI"},
        {STRING_FOR_CLOCK, "CLK"},
        {STRING_FOR_RUN_SIGNAL, "RUN"},

        // gui/menu_item/gate/selection.h
        {STRING_FOR_GATE_MODE_TITLE, ""},
        {STRING_FOR_GATE_OUTPUT_N, "OUT{}"},
        {STRING_FOR_MINIMUM_OFF_TIME, "OFFT"},

        // gui/menu_item/lfo/shape.h
        {STRING_FOR_SINE, "SINE"},
        {STRING_FOR_TRIANGLE, "TRIA"},
        {STRING_FOR_SQUARE, "SQUA"},
        {STRING_FOR_SAW, "SAW"},
        {STRING_FOR_SAMPLE_AND_HOLD, "S/H"},
        {STRING_FOR_RANDOM_WALK, "R.WLK"},

        // gui/menu_item/midi/takeover.h
        {STRING_FOR_JUMP, "JUMP"},
        {STRING_FOR_PICK_UP, "PICK"},
        {STRING_FOR_SCALE, "SCAL"},

        // gui/menu_item/mod_fx/type.h
        {STRING_FOR_FLANGER, "FLANGER"},
        {STRING_FOR_CHORUS, "CHORUS"},
        {STRING_FOR_PHASER, "PHASER"},
        {STRING_FOR_STEREO_CHORUS, "S.CHORUS"},

        // gui/menu_item/modulator/destination.h
        {STRING_FOR_CARRIERS, "CARRIERS"},
        {STRING_FOR_MODULATOR_N, "Mod{}"},

        // gui/menu_item/monitor/mode.h
        {STRING_FOR_CONDITIONAL, "COND"},
        {STRING_FOR_LOWER_ZONE, "LOWE"},
        {STRING_FOR_UPPER_ZONE, "UPPE"},
        {STRING_FOR_ANALOG_SQUARE, "ASQUARE"},
        {STRING_FOR_ANALOG_SAW, "ASAW"},
        {STRING_FOR_SAMPLE, "SAMP"},
        {STRING_FOR_INPUT, "IN"},
        {STRING_FOR_INPUT_LEFT, "INL"},
        {STRING_FOR_INPUT_RIGHT, "INR"},
        {STRING_FOR_INPUT_STEREO, "INLR"},
        {STRING_FOR_AUTOMATION_DELETED, "DELETED"},
        {STRING_FOR_RANGE_CONTAINS_ONE_NOTE, "CANT"},
        {STRING_FOR_LAST_RANGE_CANT_DELETE, "CANT"},
        {STRING_FOR_RANGE_DELETED, "DELETED"},
        {STRING_FOR_BOTTOM, "BOT"},
        {STRING_FOR_SHORTCUTS_VERSION_1, "  1.0"},
        {STRING_FOR_SHORTCUTS_VERSION_3, "  3.0"},
        {STRING_FOR_CANT_RECORD_AUDIO_FM_MODE, "CANT"},

        // Autogen
        {STRING_FOR_FACTORY_RESET, "RESET"},
        {STRING_FOR_CLIPPING_OCCURRED, "CLIP"},
        {STRING_FOR_NO_SAMPLE, "CANT"},
        {STRING_FOR_PARAMETER_NOT_APPLICABLE, "CANT"},
        {STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE, "CANT"},
        {STRING_FOR_UNIMPLEMENTED, "SOON"},
        {STRING_FOR_CANT_LEARN, "CANT"},
        {STRING_FOR_FOLDERS_CANNOT_BE_DELETED_ON_THE_DELUGE, "CANT"},
        {STRING_FOR_ERROR_CREATING_MULTISAMPLED_INSTRUMENT, "FAIL"},
        {STRING_FOR_CLIP_IS_RECORDING, "CANT"},
        {STRING_FOR_AUDIO_FILE_IS_USED_IN_CURRENT_SONG, "USED"},
        {STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT, "CANT"},
        {STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED, "CANT"},
        {STRING_FOR_UNLOADED_PARTS, "CANT"},
        {STRING_FOR_SD_CARD_ERROR, "CARD"},
        {STRING_FOR_ERROR_LOADING_SONG, "ERROR"},
        {STRING_FOR_DUPLICATE_NAMES, "DUPLICATE"},
        {STRING_FOR_PRESET_SAVED, "DONE"},
        {STRING_FOR_SAME_NAME, "CANT"},
        {STRING_FOR_SONG_SAVED, "DONE"},
        {STRING_FOR_ERROR_MOVING_TEMP_FILES, "TEMP"},
        {STRING_FOR_OVERDUBS_PENDING, "CANT"},
        {STRING_FOR_INSTRUMENTS_WITH_CLIPS_CANT_BE_TURNED_INTO_AUDIO_TRACKS, "CANT"},
        {STRING_FOR_NO_FREE_CHANNEL_SLOTS_AVAILABLE_IN_SONG, "CANT"},
        {STRING_FOR_MIDI_MUST_BE_LEARNED_TO_KIT_ITEMS_INDIVIDUALLY, "CANT"},
        {STRING_FOR_AUDIO_TRACKS_WITH_CLIPS_CANT_BE_TURNED_INTO_AN_INSTRUMENT, "CANT"},
        {STRING_FOR_ARRANGEMENT_CLEARED, "CLEAR"},
        {STRING_FOR_EMPTY_CLIP_INSTANCES_CANT_BE_MOVED_TO_THE_SESSION, "EMPTY"},
        {STRING_FOR_AUDIO_CLIP_CLEARED, "CLEAR"},
        {STRING_FOR_CANT_EDIT_LENGTH, "CANT"},
        {STRING_FOR_QUANTIZE_ALL_ROW, "QTZA"},
        {STRING_FOR_QUANTIZE, "QTZ"},
        {STRING_FOR_VELOCITY_DECREASED, "LESS"},
        {STRING_FOR_VELOCITY_INCREASED, "MORE"},
        {STRING_FOR_RANDOMIZED, "RND"},
        {STRING_FOR_MAXIMUM_LENGTH_REACHED, "CANT"},
        {STRING_FOR_NOTES_PASTED, "PASTE"},
        {STRING_FOR_AUTOMATION_PASTED, "PASTE"},
        {STRING_FOR_CANT_PASTE_AUTOMATION, "CANT"},
        {STRING_FOR_NO_AUTOMATION_TO_PASTE, "NONE"},
        {STRING_FOR_NOTES_COPIED, "COPY"},
        {STRING_FOR_NO_AUTOMATION_TO_COPY, "NONE"},
        {STRING_FOR_AUTOMATION_COPIED, "COPY"},
        {STRING_FOR_DELETED_UNUSED_ROWS, "DELETED"},
        {STRING_FOR_AT_LEAST_ONE_ROW_NEEDS_TO_HAVE_NOTES, "CANT"},
        {STRING_FOR_RECORDING_IN_PROGRESS, "CANT"},
        {STRING_FOR_CANT_REMOVE_FINAL_CLIP, "LAST"},
        {STRING_FOR_CLIP_NOT_EMPTY, "CANT"},
        {STRING_FOR_RECORDING_TO_ARRANGEMENT, "CANT"},
        {STRING_FOR_CANT_CREATE_OVERDUB_WHILE_CLIPS_SOLOING, "SOLO"},
        {STRING_FOR_CLIP_WOULD_BREACH_MAX_ARRANGEMENT_LENGTH, "CANT"},
        {STRING_FOR_NO_UNUSED_CHANNELS, "CANT"},
        {STRING_FOR_CLIP_HAS_INSTANCES_IN_ARRANGER, "CANT"},
        {STRING_FOR_DIN_PORTS, "DIN"},
        {STRING_FOR_UPSTREAM_USB_PORT_3_SYSEX, "Computer 3"},
        {STRING_FOR_UPSTREAM_USB_PORT_2, "Computer 2"},
        {STRING_FOR_UPSTREAM_USB_PORT_1, "Computer 1"},
        {STRING_FOR_HELLO_SYSEX, "SYSX"},
        {STRING_FOR_OTHER_SCALE, "OTHER"},
        {STRING_FOR_CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES_IN_USE, "CANT"},
        {STRING_FOR_CLIP_CLEARED, "CLEAR"},
        {STRING_FOR_NO_FURTHER_UNUSED_INSTRUMENT_NUMBERS, "FULL"},
        {STRING_FOR_CHANNEL_PRESSURE, "AFTE"},
        {STRING_FOR_PITCH_BEND, "BEND"},
        {STRING_FOR_NO_PARAM, "NONE"},
        {STRING_FOR_NO_FURTHER_UNUSED_MIDI_PARAMS, "FULL"},
        {STRING_FOR_ANALOG_DELAY, "ANA"},
        {STRING_FOR_NO_UNUSED_CHANNELS_AVAILABLE, "CANT"},
        {STRING_FOR_NO_AVAILABLE_CHANNELS, "CANT"},
        {STRING_FOR_PARAMETER_AUTOMATION_DELETED, "ExistenceChangeType::DELETE"},
        {STRING_FOR_CREATE_OVERDUB_FROM_WHICH_CLIP, "WHICH"},
        {STRING_FOR_AUDIO_TRACK_HAS_NO_INPUT_CHANNEL, "CANT"},
        {STRING_FOR_CANT_GRAB_TEMPO_FROM_CLIP, "CANT"},
        {STRING_FOR_SYNC_NUDGED, "NUDGE"},
        {STRING_FOR_FOLLOWING_EXTERNAL_CLOCK, "CANT"},
        {STRING_FOR_SLOW_SIDECHAIN_COMPRESSOR, "SLOW"},
        {STRING_FOR_FAST_SIDECHAIN_COMPRESSOR, "FAST"},
        {STRING_FOR_REBOOT_TO_USE_THIS_SD_CARD, "DIFF"},
        {STRING_FOR_LARGE_ROOM_REVERB, "LARG"},
        {STRING_FOR_MEDIUM_ROOM_REVERB, "MEDI"},
        {STRING_FOR_SMALL_ROOM_REVERB, "SMAL"},
        {STRING_FOR_USB_DEVICES_MAX, "FULL"},
        {STRING_FOR_USB_DEVICE_DETACHED, "DETACH"},
        {STRING_FOR_USB_HUB_ATTACHED, "HUB"},
        {STRING_FOR_USB_DEVICE_NOT_RECOGNIZED, "UNKN"},

        // autogen menus.cpp
        {STRING_FOR_DEV_MENU_A, "DEVA"},
        {STRING_FOR_DEV_MENU_B, "DEVB"},
        {STRING_FOR_DEV_MENU_C, "DEVC"},
        {STRING_FOR_DEV_MENU_D, "DEVD"},
        {STRING_FOR_DEV_MENU_E, "DEVE"},
        {STRING_FOR_DEV_MENU_F, "DEVF"},
        {STRING_FOR_DEV_MENU_G, "DEVG"},
        {STRING_FOR_ENVELOPE_1, "ENV1"},
        {STRING_FOR_ENVELOPE_2, "ENV2"},
        {STRING_FOR_VOLUME_LEVEL, "VOLUME"},
        {STRING_FOR_REPEAT_MODE, "MODE"},
        {STRING_FOR_PITCH_SPEED, "PISP"},
        {STRING_FOR_OSCILLATOR_SYNC, "SYNC"},
        {STRING_FOR_OSCILLATOR_1, "OSC1"},
        {STRING_FOR_OSCILLATOR_2, "OSC2"},
        {STRING_FOR_UNISON_NUMBER, "NUM"},
        {STRING_FOR_UNISON_DETUNE, "DETUNE"},
        {STRING_FOR_UNISON_STEREO_SPREAD, "SPREAD"},
        {STRING_FOR_NUMBER_OF_OCTAVES, "OCTAVES"},
        {STRING_FOR_AMOUNT_LEVEL, "AMOUNT"},
        {STRING_FOR_SHAPE, "TYPE"},
        {STRING_FOR_MOD_FX, "MODU"},
        {STRING_FOR_BASS_FREQUENCY, "BAFR"},
        {STRING_FOR_TREBLE_FREQUENCY, "TRFR"},
        {STRING_FOR_POLY_FINGER_MPE, "MPE"},
        {STRING_FOR_REVERB_SIDECHAIN, "SIDE"},
        {STRING_FOR_ROOM_SIZE, "SIZE"},
        {STRING_FOR_BITCRUSH, "CRUSH"},
        {STRING_FOR_SUB_BANK, "SUB"},
        {STRING_FOR_PLAY_DIRECTION, "DIRECTION"},
        {STRING_FOR_SWING_INTERVAL, "SWIN"},
        {STRING_FOR_SHORTCUTS_VERSION, "SHOR"},
        {STRING_FOR_KEYBOARD_FOR_TEXT, "KEYB"},
        {STRING_FOR_LOOP_MARGINS, "MARGINS"},
        {STRING_FOR_SAMPLING_MONITORING, "MONITORING"},
        {STRING_FOR_SAMPLE_PREVIEW, "PREV"},
        {STRING_FOR_PLAY_CURSOR, "CURS"},
        {STRING_FOR_FIRMWARE_VERSION, "VER."},
        {STRING_FOR_COMMUNITY_FTS, "FEAT"},
        {STRING_FOR_MIDI_THRU, "THRU"},
        {STRING_FOR_TAKEOVER, "TOVR"},
        {STRING_FOR_RECORD, "REC"},
        {STRING_FOR_COMMANDS, "CMD"},
        {STRING_FOR_OUTPUT, "OUT"},
        {STRING_FOR_INPUT, "IN"},
        {STRING_FOR_TEMPO_MAGNITUDE_MATCHING, "MAGN"},
        {STRING_FOR_TRIGGER_CLOCK, "TCLOCK"},
        {STRING_FOR_FM_MODULATOR_1, "MOD1"},
        {STRING_FOR_FM_MODULATOR_2, "MOD2"},
        {STRING_FOR_NOISE_LEVEL, "NOISE"},
        {STRING_FOR_MASTER_TRANSPOSE, "TRANSPOSE"},
        {STRING_FOR_SYNTH_MODE, "MODE"},
        {STRING_FOR_FILTER_ROUTE, "ROUT"},

        {STRING_FOR_TRACK_STILL_HAS_CLIPS_IN_SESSION, "CANT"},
        {STRING_FOR_DELETE_ALL_TRACKS_CLIPS_FIRST, "CANT"},
        {STRING_FOR_CANT_DELETE_FINAL_CLIP, "CANT"},
        {STRING_FOR_NEW_SYNTH_CREATED, "NEW"},
        {STRING_FOR_NEW_KIT_CREATED, "NEW"},

        {STRING_FOR_CAN_ONLY_IMPORT_WHOLE_FOLDER_INTO_BRAND_NEW_KIT, "CANT"},
        {STRING_FOR_CANT_IMPORT_WHOLE_FOLDER_INTO_AUDIO_CLIP, "CANT"},

        {STRING_FOR_IMPOSSIBLE_FROM_GRID, "CANT"},
        {STRING_FOR_SWITCHING_TO_TRACK_FAILED, "ESG1"},
        {STRING_FOR_CANT_CLONE_AUDIO_IN_OTHER_TRACK, "CANT"},
        {STRING_FOR_TARGET_FULL, "FULL"},

        {STRING_FOR_WAVEFOLD, "FOLD"},

        {STRING_FOR_AUTOMATION, "AUTO"},
        {STRING_FOR_LOOP_TOO_SHORT, "CANT"},
        {STRING_FOR_LOOP_HALVED, "HALF"},
        {STRING_FOR_LOOP_TOO_LONG, "CANT"},
        {STRING_FOR_LOOP_DOUBLED, "DOUB"},
        {STRING_FOR_CANT_AUTOMATE_CV, "CANT"},
        {STRING_FOR_SHIFT_RIGHT, "RIGHT"},
        {STRING_FOR_SHIFT_LEFT, "LEFT"},
        {STRING_FOR_INTERPOLATION_DISABLED, "OFF"},
        {STRING_FOR_INTERPOLATION_ENABLED, "ON"},
        {STRING_FOR_COMING_SOON, "SOON"},
        {STRING_FOR_MOD_MATRIX, "MMTX"},

        {STRING_FOR_CHECKSUM_FAIL, "CRC FAIL"},
        {STRING_FOR_WRONG_SIZE, "SIZE FAIL"},
        {STRING_FOR_CLIP_CLEARED, "CLEAR"},
        {STRING_FOR_NOTES_CLEARED, "CLEAR"},
        {STRING_FOR_AUTOMATION_CLEARED, "CLEAR"},

        {STRING_FOR_PAD_SELECTION_OFF, "OFF"},
        {STRING_FOR_PAD_SELECTION_ON, "ON"},

    },
    &built_in::english,
};
} // namespace deluge::l10n::built_in
