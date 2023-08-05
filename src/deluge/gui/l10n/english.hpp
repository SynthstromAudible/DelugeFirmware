#pragma once
#include "strings.hpp"

namespace deluge::l10n::language_map {
using enum Strings;
constexpr std::array english = build_l10n_map( //
    {
        {STRING_FOR_ERROR_INSUFFICIENT_RAM, "Insufficient RAM"},
        {STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE, "Too many files in folder"},
        {STRING_FOR_ERROR_SD_CARD, "SD card error"},
        {STRING_FOR_ERROR_SD_CARD_NOT_PRESENT, "No SD card present"},
        {STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM, "Please use FAT32-formatted card"},
        {STRING_FOR_ERROR_FILE_CORRUPTED, "File corrupted"},
        {STRING_FOR_ERROR_FILE_NOT_FOUND, "File not found"},
        {STRING_FOR_ERROR_FILE_UNREADABLE, "File unreadable"},
        {STRING_FOR_ERROR_FILE_UNSUPPORTED, "File unsupported"},
        {STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW, "Your firmware version is too old to open this file"},
        {STRING_FOR_ERROR_FOLDER_DOESNT_EXIST, "Folder not found"},
        {STRING_FOR_ERROR_BUG, "Bug encountered"},
        {STRING_FOR_ERROR_WRITE_FAIL, "SD card write error"},
        {STRING_FOR_ERROR_FILE_TOO_BIG, "File too large"},
        {STRING_FOR_ERROR_PRESET_IN_USE, "This preset is in-use elsewhere in your song"},
        {STRING_FOR_ERROR_NO_FURTHER_PRESETS, "This preset is in-use elsewhere in your song"},
        {STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION, "No more presets found"},
        {STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED, "Maximum file size reached"},
        {STRING_FOR_ERROR_SD_CARD_FULL, "SD card full"},
        {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE, "File does not contain wavetable data"},
        {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO, "Stereo files cannot be used as wavetables"},
        {STRING_FOR_ERROR_WRITE_PROTECTED, "Card is write-protected"},
        {STRING_FOR_ERROR_GENERIC, "You've encountered an unspecified error. Please report this bug."},

        // Params (originally from functions.cpp)
        {STRING_FOR_PATCH_SOURCE_LFO_GLOBAL, "LFO1"},
        {STRING_FOR_PATCH_SOURCE_LFO_LOCAL, "LFO2"},
        {STRING_FOR_PATCH_SOURCE_ENVELOPE_0, "Envelope 1"},
        {STRING_FOR_PATCH_SOURCE_ENVELOPE_1, "Envelope 2"},
        {STRING_FOR_PATCH_SOURCE_VELOCITY, "Velocity"},
        {STRING_FOR_PATCH_SOURCE_NOTE, "Note"},
        {STRING_FOR_PATCH_SOURCE_COMPRESSOR, "Sidechain"},
        {STRING_FOR_PATCH_SOURCE_RANDOM, "Random"},
        {STRING_FOR_PATCH_SOURCE_AFTERTOUCH, "Aftertouch"},
        {STRING_FOR_PATCH_SOURCE_X, "MPE X"},
        {STRING_FOR_PATCH_SOURCE_Y, "MPE Y"},
        {STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME, "Osc1 level"},
        {STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME, "Osc2 level"},
        {STRING_FOR_PARAM_LOCAL_VOLUME, "Level"},
        {STRING_FOR_PARAM_LOCAL_NOISE_VOLUME, "Noise level"},
        {STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH, "Osc1 PW"},
        {STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH, "Osc2 PW"},
        {STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX, "Osc1 wave pos."},
        {STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX, "Osc2 wave pos."},
        {STRING_FOR_PARAM_LOCAL_LPF_RESONANCE, "LPF resonance"},
        {STRING_FOR_PARAM_LOCAL_HPF_RESONANCE, "HPF resonance"},
        {STRING_FOR_PARAM_LOCAL_PAN, "Pan"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME, "FM mod1 level"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME, "FM mod2 level"},
        {STRING_FOR_PARAM_LOCAL_LPF_FREQ, "LPF frequency"},
        {STRING_FOR_PARAM_LOCAL_PITCH_ADJUST, "Pitch"},
        {STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST, "Osc1 pitch"},
        {STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST, "Osc2 pitch"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST, "FM mod1 pitch"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST, "FM mod2 pitch"},
        {STRING_FOR_PARAM_LOCAL_HPF_FREQ, "HPF frequency"},
        {STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ, "LFO2 rate"},
        {STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK, "Env1 attack"},
        {STRING_FOR_PARAM_LOCAL_ENV_0_DECAY, "Env1 decay"},
        {STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN, "Env1 sustain"},
        {STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE, "Env1 release"},
        {STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK, "Env2 attack"},
        {STRING_FOR_PARAM_LOCAL_ENV_1_DECAY, "Env2 decay"},
        {STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN, "Env2 sustain"},
        {STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE, "Env2 release"},
        {STRING_FOR_PARAM_GLOBAL_LFO_FREQ, "LFO1 rate"},
        {STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX, "Level"},
        {STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND, "Level"},
        {STRING_FOR_PARAM_GLOBAL_DELAY_RATE, "Delay rate"},
        {STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK, "Delay amount"},
        {STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT, "Reverb amount"},
        {STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE, "Mod-FX rate"},
        {STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH, "Mod-FX depth"},
        {STRING_FOR_PARAM_GLOBAL_ARP_RATE, "Arp. rate"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK, "Mod1 feedback"},
        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK, "Mod2 feedback"},
        {STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK, "Carrier1 feed."},
        {STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK, "Carrier2 feed."},

        // General
        {STRING_FOR_DISABLED, "Disabled"},
        {STRING_FOR_ENABLED, "Enabled"},
        {STRING_FOR_OK, "OK"},
        {STRING_FOR_NEW, "New"},
        {STRING_FOR_DELETE, "Delete"},
        {STRING_FOR_SURE, "Sure"},
        {STRING_FOR_OVERWRITE, "Overwrite"},
        {STRING_FOR_OPTIONS, "Options"},

        // Menu Titles
        {STRING_FOR_AUDIO_SOURCE, "Audio source"},
        {STRING_FOR_ARE_YOU_SURE_QMARK, "Are you sure?"},
        {STRING_FOR_DELETE_QMARK, "Delete?"},
        {STRING_FOR_SAMPLES, "Sample(s)"},
        {STRING_FOR_LOAD_FILES, "Load file(s)"},
        {STRING_FOR_CLEAR_SONG_QMARK, "Clear song?"},
        {STRING_FOR_LOAD_PRESET, "Load preset"},
        {STRING_FOR_OVERWRITE_BOOTLOADER_TITLE, "Overwrite bootloader at own risk"},
        {STRING_FOR_OVERWRITE_QMARK, "Overwrite?"},

        // gui/context_menu/audio_input_selector.cpp
        {STRING_FOR_LEFT_INPUT, "Left input"},
        {STRING_FOR_LEFT_INPUT_MONITORING, "Left input (monitoring)"},
        {STRING_FOR_RIGHT_INPUT, "Right input"},
        {STRING_FOR_RIGHT_INPUT_MONITORING, "Right input (monitoring)"},
        {STRING_FOR_STEREO_INPUT, "Stereo input"},
        {STRING_FOR_STEREO_INPUT_MONITORING, "Stereo input (monitoring)"},
        {STRING_FOR_BALANCED_INPUT, "Bal. input"},
        {STRING_FOR_BALANCED_INPUT_MONITORING, "Bal. input (monitoring)"},
        {STRING_FOR_MIX_PRE_FX, "Deluge mix (pre fx)"},
        {STRING_FOR_MIX_POST_FX, "Deluge output (post fx)"},

        // gui/context_menu/sample_browser/kit.cpp
        {STRING_FOR_LOAD_ALL, "Load all"},
        {STRING_FOR_SLICE, "Slice"},

        // gui/context_menu/sample_browser/synth.cpp
        {STRING_FOR_MULTISAMPLES, "Multisamples"},
        {STRING_FOR_BASIC, "Basic"},
        {STRING_FOR_SINGLE_CYCLE, "Single-cycle"},
        {STRING_FOR_WAVETABLE, "Wavetable"},

        // gui/context_menu/delete_file.cpp
        {STRING_FOR_ERROR_DELETING_FILE, "Error deleting file"},
        {STRING_FOR_FILE_DELETED, "File deleted"},

        // gui/context_menu/load_instrument_preset.cpp
        {STRING_FOR_CLONE, "Clone"},

        // gui/context_menu/overwrite_bootloader.cpp
        {STRING_FOR_ACCEPT_RISK, "Accept risk"},
        {STRING_FOR_ERROR_BOOTLOADER_TOO_BIG, "Bootloader file too large"},
        {STRING_FOR_ERROR_BOOTLOADER_TOO_SMALL, "Bootloader file too small"},
        {STRING_FOR_BOOTLOADER_UPDATED, "Bootloader updated"},
        {STRING_FOR_ERROR_BOOTLOADER_FILE_NOT_FOUND, "No boot*.bin file found"},

        // gui/context_menu/save_song_or_instrument.cpp
        {STRING_FOR_COLLECT_MEDIA, "Collect media"},
        {STRING_FOR_CREATE_FOLDER, "Create folder"},

        // gui/menu_item/arpeggiator/mode.h
        {STRING_FOR_UP, "Up"},
        {STRING_FOR_DOWN, "Down"},
        {STRING_FOR_BOTH, "Both"},
        {STRING_FOR_RANDOM, "Random"},

        // gui/menu_item/cv/selection.h
        {STRING_FOR_CV_OUTPUT_N, "CV output {}"},

        // gui/menu_item/delay/analog.h
        {STRING_FOR_DIGITAL, "Digital"},
        {STRING_FOR_ANALOG, "Analog"},

        // gui/menu_item/filter/lpf_mode.h
        {STRING_FOR_DRIVE, "Drive"},
        {STRING_FOR_SVF, "State-Variable Filter"},

        // gui/menu_item/flash/status.h
        {STRING_FOR_FAST, "Fast"},
        {STRING_FOR_SLOW, "Slow"},
        {STRING_FOR_V_TRIGGER, "V-trig"},
        {STRING_FOR_S_TRIGGER, "S-trig"},
        {STRING_FOR_CLOCK, "Clock"},
        {STRING_FOR_RUN_SIGNAL, "Run signal"},

        // gui/menu_item/gate/selection.h
        {STRING_FOR_GATE_MODE_TITLE, "Gate out{} mode"},
        {STRING_FOR_GATE_OUTPUT_N, "Gate output {}"},
        {STRING_FOR_MINIMUM_OFF_TIME, "Minimum off-time"},

        // gui/menu_item/lfo/shape.h
        {STRING_FOR_SINE, "Sine"},
        {STRING_FOR_TRIANGLE, "Triangle"},
        {STRING_FOR_SQUARE, "Square"},
        {STRING_FOR_SAW, "Saw"},
        {STRING_FOR_SAMPLE_AND_HOLD, "S&H"},
        {STRING_FOR_RANDOM_WALK, "Random Walk"},

        // gui/menu_item/midi/takeover.h
        {STRING_FOR_JUMP, "Jump"},
        {STRING_FOR_PICK_UP, "Pickup"},
        {STRING_FOR_SCALE, "Scale"},

        // gui/menu_item/mod_fx/type.h
        {STRING_FOR_FLANGER, "Flanger"},
        {STRING_FOR_CHORUS, "Chorus"},
        {STRING_FOR_PHASER, "Phaser"},
        {STRING_FOR_STEREO_CHORUS, "Stereo Chorus"},

        // gui/menu_item/modulator/destination.h
        {STRING_FOR_CARRIERS, "Carriers"},
        {STRING_FOR_MODULATOR_N, "Modulator {}"},

        // gui/menu_item/monitor/mode.h
        {STRING_FOR_CONDITIONAL, "Conditional"},
        {STRING_FOR_LOWER_ZONE, "Lower zone"},
        {STRING_FOR_UPPER_ZONE, "Upper zone"},
        {STRING_FOR_ANALOG_SQUARE, "Analog square"},
        {STRING_FOR_ANALOG_SAW, "Analog saw"},
        {STRING_FOR_SAMPLE, "Sample"},
        {STRING_FOR_INPUT, "Input"},
        {STRING_FOR_INPUT_LEFT, "Input (left)"},
        {STRING_FOR_INPUT_RIGHT, "Input (right)"},
        {STRING_FOR_INPUT_STEREO, "Input (stereo)"},
        {STRING_FOR_AUTOMATION_DELETED, "Automation deleted"},
        {STRING_FOR_RANGE_CONTAINS_ONE_NOTE, "Range contains only 1 note"},
        {STRING_FOR_LAST_RANGE_CANT_DELETE, "Only 1 range - can't delete"},
        {STRING_FOR_RANGE_DELETED, "Range deleted"},
        {STRING_FOR_BOTTOM, "Bottom"},
        {STRING_FOR_SHORTCUTS_VERSION_1, "1.0"},
        {STRING_FOR_SHORTCUTS_VERSION_3, "3.0"},
        {STRING_FOR_CANT_RECORD_AUDIO_FM_MODE, "Can't record audio into an FM synth"},

        //Autogen
        {STRING_FOR_FACTORY_RESET, "Factory reset"},
        {STRING_FOR_CLIPPING_OCCURRED, "Clipping occurred"},
        {STRING_FOR_NO_SAMPLE, "No sample"},
        {STRING_FOR_PARAMETER_NOT_APPLICABLE, "Parameter not applicable"},
        {STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE, "Select a row or affect-entire"},
        {STRING_FOR_UNIMPLEMENTED, "Feature not (yet?) implemented"},
        {STRING_FOR_CANT_LEARN, "Can't learn"},
        {STRING_FOR_FOLDERS_CANNOT_BE_DELETED_ON_THE_DELUGE, "Folders cannot be deleted on the Deluge"},
        {STRING_FOR_ERROR_CREATING_MULTISAMPLED_INSTRUMENT, "Error creating multisampled instrument"},
        {STRING_FOR_CLIP_IS_RECORDING, "Clip is recording"},
        {STRING_FOR_AUDIO_FILE_IS_USED_IN_CURRENT_SONG, "Audio file is used in current song"},
        {STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT, "Can only user slicer for brand-new kit"},
        {STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED, "TEMP folder can't be browsed"},
        {STRING_FOR_UNLOADED_PARTS, "Can't return to current song, as parts have been unloaded"},
        {STRING_FOR_SD_CARD_ERROR, "SD card error"},
        {STRING_FOR_ERROR_LOADING_SONG, "Error loading song"},
        {STRING_FOR_DUPLICATE_NAMES, "Duplicate names"},
        {STRING_FOR_PRESET_SAVED, "Preset saved"},
        {STRING_FOR_SAME_NAME, "Another instrument in the song has the same name / number"},
        {STRING_FOR_SONG_SAVED, "Song saved"},
        {STRING_FOR_ERROR_MOVING_TEMP_FILES, "Song saved, but error moving temp files"},
        {STRING_FOR_OVERDUBS_PENDING, "Can't save while overdubs pending"},
        {STRING_FOR_INSTRUMENTS_WITH_CLIPS_CANT_BE_TURNED_INTO_AUDIO_TRACKS,
         "Instruments with clips can't be turned into audio tracks"},
        {STRING_FOR_NO_FREE_CHANNEL_SLOTS_AVAILABLE_IN_SONG, "No free channel slots available in song"},
        {STRING_FOR_MIDI_MUST_BE_LEARNED_TO_KIT_ITEMS_INDIVIDUALLY, "MIDI must be learned to kit items individually"},
        {STRING_FOR_AUDIO_TRACKS_WITH_CLIPS_CANT_BE_TURNED_INTO_AN_INSTRUMENT,
         "Audio tracks with clips can't be turned into an instrument"},
        {STRING_FOR_ARRANGEMENT_CLEARED, "Arrangement cleared"},
        {STRING_FOR_EMPTY_CLIP_INSTANCES_CANT_BE_MOVED_TO_THE_SESSION,
         "Empty clip instances can't be moved to the session"},
        {STRING_FOR_AUDIO_CLIP_CLEARED, "Audio clip cleared"},
        {STRING_FOR_CANT_EDIT_LENGTH, "Can't edit length"},
        {STRING_FOR_QUANTIZE_ALL_ROW, "QUANTIZE ALL ROW"},
        {STRING_FOR_QUANTIZE, "QUANTIZE"},
        {STRING_FOR_VELOCITY_DECREASED, "Velocity decreased"},
        {STRING_FOR_VELOCITY_INCREASED, "Velocity increased"},
        {STRING_FOR_RANDOMIZED, "Randomized"},
        {STRING_FOR_MAXIMUM_LENGTH_REACHED, "Maximum length reached"},
        {STRING_FOR_NOTES_PASTED, "Notes pasted"},
        {STRING_FOR_AUTOMATION_PASTED, "Automation pasted"},
        {STRING_FOR_CANT_PASTE_AUTOMATION, "Can't paste automation"},
        {STRING_FOR_NO_AUTOMATION_TO_PASTE, "No automation to paste"},
        {STRING_FOR_NOTES_COPIED, "Notes copied"},
        {STRING_FOR_NO_AUTOMATION_TO_COPY, "No automation to copy"},
        {STRING_FOR_AUTOMATION_COPIED, "Automation copied"},
        {STRING_FOR_DELETED_UNUSED_ROWS, "Deleted unused rows"},
        {STRING_FOR_AT_LEAST_ONE_ROW_NEEDS_TO_HAVE_NOTES, "At least one row needs to have notes"},
        {STRING_FOR_RECORDING_IN_PROGRESS, "Recording in progress"},
        {STRING_FOR_CANT_REMOVE_FINAL_CLIP, "Can't remove final clip"},
        {STRING_FOR_CLIP_NOT_EMPTY, "Clip not empty"},
        {STRING_FOR_RECORDING_TO_ARRANGEMENT, "Recording to arrangement"},
        {STRING_FOR_CANT_CREATE_OVERDUB_WHILE_CLIPS_SOLOING, "Can't create overdub while clips soloing"},
        {STRING_FOR_CLIP_WOULD_BREACH_MAX_ARRANGEMENT_LENGTH, "Clip would breach max arrangement length"},
        {STRING_FOR_NO_UNUSED_CHANNELS, "No unused channels"},
        {STRING_FOR_CLIP_HAS_INSTANCES_IN_ARRANGER, "Clip has instances in arranger"},
        {STRING_FOR_CANT_SAVE_WHILE_OVERDUBS_PENDING, "Can't save while overdubs pending"},
        {STRING_FOR_DIN_PORTS, "DIN ports"},
        {STRING_FOR_UPSTREAM_USB_PORT_3_SYSEX, "upstream USB port 3 (sysex)"},
        {STRING_FOR_UPSTREAM_USB_PORT_2, "upstream USB port 2"},
        {STRING_FOR_UPSTREAM_USB_PORT_1, "upstream USB port 1"},
        {STRING_FOR_HELLO_SYSEX, "hello sysex"},
        {STRING_FOR_OTHER_SCALE, "Other scale"},
        {STRING_FOR_CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES_IN_USE, "Custom scale with more than 7 notes in use"},
        {STRING_FOR_CLIP_CLEARED, "Clip cleared"},
        {STRING_FOR_NO_FURTHER_UNUSED_INSTRUMENT_NUMBERS, "No further unused instrument numbers"},
        {STRING_FOR_CHANNEL_PRESSURE, "Channel pressure"},
        {STRING_FOR_PITCH_BEND, "Pitch bend"},
        {STRING_FOR_NO_PARAM, "No param"},
        {STRING_FOR_NO_FURTHER_UNUSED_MIDI_PARAMS, "No further unused MIDI params"},
        {STRING_FOR_ANALOG_DELAY, "Analog delay"},
        {STRING_FOR_NO_UNUSED_CHANNELS_AVAILABLE, "No unused channels available"},
        {STRING_FOR_NO_AVAILABLE_CHANNELS, "No available channels"},
        {STRING_FOR_PARAMETER_AUTOMATION_DELETED, "Parameter automation deleted"},
        {STRING_FOR_CREATE_OVERDUB_FROM_WHICH_CLIP, "Create overdub from which clip?"},
        {STRING_FOR_AUDIO_TRACK_HAS_NO_INPUT_CHANNEL, "Audio track has no input channel"},
        {STRING_FOR_CANT_GRAB_TEMPO_FROM_CLIP, "Can't grab tempo from clip"},
        {STRING_FOR_SYNC_NUDGED, "Sync nudged"},
        {STRING_FOR_FOLLOWING_EXTERNAL_CLOCK, "Following external clock"},
        {STRING_FOR_SLOW_SIDECHAIN_COMPRESSOR, "Slow sidechain compressor"},
        {STRING_FOR_FAST_SIDECHAIN_COMPRESSOR, "Fast sidechain compressor"},
        {STRING_FOR_REBOOT_TO_USE_THIS_SD_CARD, "Reboot to use this SD card"},
        {STRING_FOR_LARGE_ROOM_REVERB, "Large room reverb"},
        {STRING_FOR_MEDIUM_ROOM_REVERB, "Medium room reverb"},
        {STRING_FOR_SMALL_ROOM_REVERB, "Small room reverb"},
        {STRING_FOR_USB_DEVICES_MAX, "Maximum number of USB devices already hosted"},
        {STRING_FOR_USB_DEVICE_DETACHED, "USB device detached"},
        {STRING_FOR_USB_HUB_ATTACHED, "USB hub attached"},
        {STRING_FOR_USB_DEVICE_NOT_RECOGNIZED, "USB device not recognized"},

        {STRING_FOR_OUT, "Out"},
        {STRING_FOR_IN, "In"},

        {STRING_FOR_DEV_MENU_A, "Dev Menu A"},
        {STRING_FOR_DEV_MENU_B, "Dev Menu B"},
        {STRING_FOR_DEV_MENU_C, "Dev Menu C"},
        {STRING_FOR_DEV_MENU_D, "Dev Menu D"},
        {STRING_FOR_DEV_MENU_E, "Dev Menu E"},
        {STRING_FOR_DEV_MENU_F, "Dev Menu F"},
        {STRING_FOR_DEV_MENU_G, "Dev Menu G"},
        {STRING_FOR_ENVELOPE_1, "Envelope 1"},
        {STRING_FOR_ENVELOPE_2, "Envelope 2"},
        {STRING_FOR_VOLUME_LEVEL, "Volume"},
        {STRING_FOR_AMOUNT_LEVEL, "Level"},
        {STRING_FOR_REPEAT_MODE, "Repeat mode"},
        {STRING_FOR_PITCH_SPEED, "Pitch/speed"},
        {STRING_FOR_OSCILLATOR_SYNC, "Oscillator sync"},
        {STRING_FOR_OSCILLATOR_1, "Oscillator 1"},
        {STRING_FOR_OSCILLATOR_2, "Oscillator 2"},
        {STRING_FOR_UNISON_NUMBER, "Unison number"},
        {STRING_FOR_UNISON_DETUNE, "Unison detune"},
        {STRING_FOR_UNISON_STEREO_SPREAD, "Unison stereo spread"},
        {STRING_FOR_NUMBER_OF_OCTAVES, "Number of octaves"},
        {STRING_FOR_SHAPE, "SHAPE"},
        {STRING_FOR_MOD_FX, "Mod-fx"},
        {STRING_FOR_BASS_FREQUENCY, "Bass frequency"},
        {STRING_FOR_TREBLE_FREQUENCY, "Treble frequency"},
        {STRING_FOR_POLY_FINGER_MPE, "Poly / finger / MPE"},
        {STRING_FOR_REVERB_SIDECHAIN, "Reverb sidechain"},
        {STRING_FOR_ROOM_SIZE, "Room size"},
        {STRING_FOR_BITCRUSH, "Bitcrush"},
        {STRING_FOR_SUB_BANK, "Sub-bank"},
        {STRING_FOR_PLAY_DIRECTION, "Play direction"},
        {STRING_FOR_SWING_INTERVAL, "Swing interval"},
        {STRING_FOR_SHORTCUTS_VERSION, "Shortcuts version"},
        {STRING_FOR_KEYBOARD_FOR_TEXT, "Keyboard for text"},
        {STRING_FOR_LOOP_MARGINS, "Loop margins"},
        {STRING_FOR_SAMPLING_MONITORING, "Sampling monitoring"},
        {STRING_FOR_SAMPLE_PREVIEW, "Sample preview"},
        {STRING_FOR_PLAY_CURSOR, "Play-cursor"},
        {STRING_FOR_FIRMWARE_VERSION, "Firmware version"},
        {STRING_FOR_COMMUNITY_FTS, "Community fts."},
        {STRING_FOR_MIDI_THRU, "MIDI-thru"},
        {STRING_FOR_TAKEOVER, "TAKEOVER"},
        {STRING_FOR_RECORD, "Record"},
        {STRING_FOR_COMMANDS, "Commands"},
        {STRING_FOR_OUTPUT, "Output"},
        {STRING_FOR_INPUT, "Input"},
        {STRING_FOR_TEMPO_MAGNITUDE_MATCHING, "Tempo magnitude matching"},
        {STRING_FOR_TRIGGER_CLOCK, "Trigger clock"},
        {STRING_FOR_FM_MODULATOR_1, "FM modulator 1"},
        {STRING_FOR_FM_MODULATOR_2, "FM modulator 2"},
        {STRING_FOR_NOISE_LEVEL, "Noise level"},
        {STRING_FOR_MASTER_TRANSPOSE, "Master transpose"},
        {STRING_FOR_SYNTH_MODE, "Synth mode"},

        {STRING_FOR_TRACK_STILL_HAS_CLIPS_IN_SESSION, "Track still has clips in session"},
        {STRING_FOR_DELETE_ALL_TRACKS_CLIPS_FIRST, "Delete all track's clips first"},
        {STRING_FOR_CANT_DELETE_FINAL_CLIP, "Can't delete final Clip"},
        {STRING_FOR_NEW_SYNTH_CREATED, "New synth created"},
        {STRING_FOR_NEW_KIT_CREATED, "New kit created"},

        {STRING_FOR_CAN_ONLY_IMPORT_WHOLE_FOLDER_INTO_BRAND_NEW_KIT, "Can only import whole folder into brand-new kit"},
        {STRING_FOR_CANT_IMPORT_WHOLE_FOLDER_INTO_AUDIO_CLIP, "Can't import whole folder into audio clip"},

    });
} // namespace deluge::l10n::language_map
