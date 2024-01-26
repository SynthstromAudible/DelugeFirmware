#include "definitions_cxx.hpp"
#include "gui/l10n/language.h"
#include "gui/l10n/strings.h"

namespace deluge::l10n::built_in {
using enum String;
PLACE_SDRAM_DATA Language seven_segment{
    "Seven Segment",
    {
        // Errors
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
        {STRING_FOR_ONCE_LAUNCH, "ONCE"},

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

        // gui/context_menu/save_song_or_instrument.cpp
        {STRING_FOR_COLLECT_MEDIA, "COLLECT MEDIA"},
        {STRING_FOR_CREATE_FOLDER, "CREATE FOLDER"},

        // gui/menu_item/arpeggiator/mode.h
        {STRING_FOR_UP, "UP"},
        {STRING_FOR_DOWN, "DOWN"},
        {STRING_FOR_BOTH, "BOTH"},
        {STRING_FOR_RANDOM, "RANDOM"},

        // gui/menu_item/cv/selection.h
        {STRING_FOR_CV_OUTPUT_N, "OUT*"},
        {STRING_FOR_CV_OUTPUT_1, "OUT1"},
        {STRING_FOR_CV_OUTPUT_2, "OUT2"},

        // gui/menu_item/delay/analog.h
        {STRING_FOR_DIGITAL, "DIGI"},
        {STRING_FOR_ANALOG, "ANA"},

        // gui/menu_item/filter/lpf_mode.h
        {STRING_FOR_DRIVE, "Drive"},
        {STRING_FOR_SVF_BAND, "SV_B"},
        {STRING_FOR_SVF_NOTCH, "SV_N"},
        {STRING_FOR_HPLADDER, "HP_L"},
        {STRING_FOR_12DB_LADDER, "LA12"},
        {STRING_FOR_24DB_LADDER, "LA24"},
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
        {STRING_FOR_GATE_OUTPUT_1, "OUT1"},
        {STRING_FOR_GATE_OUTPUT_2, "OUT2"},
        {STRING_FOR_GATE_OUTPUT_3, "OUT3"},
        {STRING_FOR_GATE_OUTPUT_4, "OUT4"},
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
        {STRING_FOR_MODULATOR_1, "Mod1"},
        {STRING_FOR_MODULATOR_2, "Mod2"},

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
        {STRING_FOR_MOD_WHEEL, "MODW"},
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
        {STRING_FOR_MPE_MONO, "POLY2MONO"},
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
        {STRING_FOR_FIRMWARE_VERSION, "FIRM"},
        {STRING_FOR_COMMUNITY_FTS, "FEAT"},
        {STRING_FOR_MIDI_THRU, "THRU"},
        {STRING_FOR_TAKEOVER, "TOVR"},
        {STRING_FOR_RECORD, "REC"},
        {STRING_FOR_COMMANDS, "CMD"},
        {STRING_FOR_OUTPUT, "OUT"},
        {STRING_FOR_DEFAULT_UI, "UI"},
        {STRING_FOR_DEFAULT_UI_GRID, "GRID"},
        {STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE, "DEFMODE"},
        {STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION, "GREENSELECT"},
        {STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE_SELECTION, "SELE"},
        {STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS, "EMPTY"},
        {STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM, "UNARM"},
        {STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC, "CREATE+REC"},
        {STRING_FOR_DEFAULT_UI_LAYOUT, "LAYT"},
        {STRING_FOR_DEFAULT_UI_KEYBOARD, "KBD"},
        {STRING_FOR_DEFAULT_UI_KEYBOARD_LAYOUT_ISOMORPHIC, "ISO"},
        {STRING_FOR_DEFAULT_UI_KEYBOARD_LAYOUT_INKEY, "INKY"},
        {STRING_FOR_DEFAULT_UI_SONG, "SONG"},
        {STRING_FOR_DEFAULT_UI_SONG_LAYOUT_ROWS, "ROWS"},
        {STRING_FOR_INPUT, "IN"},
        {STRING_FOR_TEMPO_MAGNITUDE_MATCHING, "MAGN"},
        {STRING_FOR_TRIGGER_CLOCK, "TCLOCK"},
        {STRING_FOR_FM_MODULATOR_1, "MOD1"},
        {STRING_FOR_FM_MODULATOR_2, "MOD2"},
        {STRING_FOR_NOISE_LEVEL, "NOISE"},
        {STRING_FOR_MASTER_TRANSPOSE, "TRANSPOSE"},
        {STRING_FOR_SYNTH_MODE, "MODE"},
        {STRING_FOR_FILTER_ROUTE, "ROUT"},

        {STRING_FOR_COMMUNITY_FEATURE_DRUM_RANDOMIZER, "DRUM"},
        {STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR, "COMP"},
        {STRING_FOR_COMMUNITY_FEATURE_QUANTIZE, "QUAN"},
        {STRING_FOR_COMMUNITY_FEATURE_FINE_TEMPO_KNOB, "TEMP"},
        {STRING_FOR_COMMUNITY_FEATURE_MOD_DEPTH_DECIMALS, "MOD."},
        {STRING_FOR_COMMUNITY_FEATURE_CATCH_NOTES, "CATC"},
        {STRING_FOR_COMMUNITY_FEATURE_DELETE_UNUSED_KIT_ROWS, "UNUS"},
        {STRING_FOR_COMMUNITY_FEATURE_ALT_DELAY_PARAMS, "DELA"},
        {STRING_FOR_COMMUNITY_FEATURE_QUANTIZED_STUTTER, "STUT"},
        {STRING_FOR_COMMUNITY_FEATURE_AUTOMATION, "AUTO"},
        {STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_INTERPOLATION, "INTE"},
        {STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_CLEAR_CLIP, "CLEA"},
        {STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_NUDGE_NOTE, "NUDG"},
        {STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_SHIFT_CLIP, "SHIF"},
        {STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS, "SCUT"},
        {STRING_FOR_COMMUNITY_FEATURE_DEV_SYSEX, "SYSX"},
        {STRING_FOR_COMMUNITY_FEATURE_SYNC_SCALING_ACTION, "SCAL"},
        {STRING_FOR_COMMUNITY_FEATURE_HIGHLIGHT_INCOMING_NOTES, "HIGH"},
        {STRING_FOR_COMMUNITY_FEATURE_NORNS_LAYOUT, "NORN"},
        {STRING_FOR_COMMUNITY_FEATURE_GRAIN_FX, "GRFX"},

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
        {STRING_FOR_CANT_CONVERT_TYPE, "CANT"},
        {STRING_FOR_TARGET_FULL, "FULL"},

        {STRING_FOR_WAVEFOLD, "FOLD"},

        {STRING_FOR_LOOP_TOO_SHORT, "CANT"},
        {STRING_FOR_LOOP_HALVED, "HALF"},
        {STRING_FOR_LOOP_TOO_LONG, "CANT"},
        {STRING_FOR_LOOP_DOUBLED, "DOUB"},
        {STRING_FOR_MOD_MATRIX, "MMTX"},

        {STRING_FOR_CHECKSUM_FAIL, "CRC FAIL"},
        {STRING_FOR_WRONG_SIZE, "SIZE FAIL"},
        {STRING_FOR_BAD_KEY, "KEY FAIL"},
        {STRING_FOR_CLIP_CLEARED, "CLEAR"},
        {STRING_FOR_NOTES_CLEARED, "CLEAR"},
        {STRING_FOR_AUTOMATION_CLEARED, "CLEAR"},

        {STRING_FOR_MIDILOOPBACK, "M LP"},

        /* Strings Specifically for Automation Instrument Clip View
         * automation_instrument_clip_view.cpp
         */
        {STRING_FOR_AUTOMATION, "AUTO"},
        {STRING_FOR_COMING_SOON, "SOON"},
        {STRING_FOR_CANT_AUTOMATE_CV, "CANT"},
        {STRING_FOR_SHIFT_LEFT, "LEFT"},
        {STRING_FOR_SHIFT_RIGHT, "RIGHT"},
        {STRING_FOR_INTERPOLATION_DISABLED, "OFF"},
        {STRING_FOR_INTERPOLATION_ENABLED, "ON"},
        {STRING_FOR_PAD_SELECTION_OFF, "OFF"},
        {STRING_FOR_PAD_SELECTION_ON, "ON"},

        /* Strings for Midi Learning View
         * midi_follow.cpp
         */

        {STRING_FOR_FOLLOW_TITLE, "FOLO"},
        {STRING_FOR_FOLLOW, "FOLO"},
        {STRING_FOR_CHANNEL, "CHAN"},
        {STRING_FOR_FOLLOW_KIT_ROOT_NOTE, "KIT"},
        {STRING_FOR_FOLLOW_DISPLAY_PARAM, "DISP"},
        {STRING_FOR_FOLLOW_FEEDBACK, "FEED"},
        {STRING_FOR_FOLLOW_FEEDBACK_AUTOMATION, "AUTO"},
        {STRING_FOR_FOLLOW_FEEDBACK_FILTER, "FLTR"},

        // String for Select Kit Row menu
        {STRING_FOR_SELECT_KIT_ROW, "KROW"},

        {STRING_FOR_MODEL, "MODE"},
        {STRING_FOR_FREEVERB, "FVRB"},
        {STRING_FOR_MUTABLE, "MTBL"},

    },
    &built_in::english,
};
} // namespace deluge::l10n::built_in
