#pragma once
#include "gui/l10n/strings.h"
#include "strings.h"

namespace deluge::l10n::language_map {
using enum Strings;
constexpr std::array english = build_l10n_map( //
    {
        {STRING_FOR_ERROR_INSUFFICIENT_RAM, //
         "Insufficient RAM"},
        {STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE, //
         "Too many files in folder"},
        {STRING_FOR_ERROR_SD_CARD, //
         "SD card error"},
        {STRING_FOR_ERROR_SD_CARD_NOT_PRESENT, //
         "No SD card present"},
        {STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM, //
         "Please use FAT32-formatted card"},
        {STRING_FOR_ERROR_FILE_CORRUPTED, //
         "File corrupted"},
        {STRING_FOR_ERROR_FILE_NOT_FOUND, //
         "File not found"},
        {STRING_FOR_ERROR_FILE_UNREADABLE, //
         "File unreadable"},
        {STRING_FOR_ERROR_FILE_UNSUPPORTED, //
         "File unsupported"},
        {STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW, //
         "Your firmware version is too old to open this file"},
        {STRING_FOR_ERROR_FOLDER_DOESNT_EXIST, //
         "Folder not found"},
        {STRING_FOR_ERROR_BUG, //
         "Bug encountered"},
        {STRING_FOR_ERROR_WRITE_FAIL, //
         "SD card write error"},
        {STRING_FOR_ERROR_FILE_TOO_BIG, //
         "File too large"},
        {STRING_FOR_ERROR_PRESET_IN_USE, //
         "This preset is in-use elsewhere in your song"},
        {STRING_FOR_ERROR_NO_FURTHER_PRESETS, //
         "This preset is in-use elsewhere in your song"},
        {STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION, //
         "No more presets found"},
        {STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED, //
         "Maximum file size reached"},
        {STRING_FOR_ERROR_SD_CARD_FULL, //
         "SD card full"},
        {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE, //
         "File does not contain wavetable data"},
        {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO, //
         "Stereo files cannot be used as wavetables"},
        {STRING_FOR_ERROR_WRITE_PROTECTED, //
         "Card is write-protected"},
        {STRING_FOR_ERROR_GENERIC,                                            //
         "You've encountered an unspecified error. Please report this bug."}, //

        // Params (originally from functions.cpp)
        {STRING_FOR_PATCH_SOURCE_LFO_GLOBAL, //
         "LFO1"},

        {STRING_FOR_PATCH_SOURCE_LFO_LOCAL, //
         "LFO2"},

        {STRING_FOR_PATCH_SOURCE_ENVELOPE_0, //
         "Envelope 1"},

        {STRING_FOR_PATCH_SOURCE_ENVELOPE_1, //
         "Envelope 2"},

        {STRING_FOR_PATCH_SOURCE_VELOCITY, //
         "Velocity"},

        {STRING_FOR_PATCH_SOURCE_NOTE, //
         "Note"},

        {STRING_FOR_PATCH_SOURCE_COMPRESSOR, //
         "Sidechain"},

        {STRING_FOR_PATCH_SOURCE_RANDOM, //
         "Random"},

        {STRING_FOR_PATCH_SOURCE_AFTERTOUCH, //
         "Aftertouch"},

        {STRING_FOR_PATCH_SOURCE_X, //
         "MPE X"},

        {STRING_FOR_PATCH_SOURCE_Y, //
         "MPE Y"},

        {STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME, //
         "Osc1 level"},

        {STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME, //
         "Osc2 level"},

        {STRING_FOR_PARAM_LOCAL_VOLUME, //
         "Level"},

        {STRING_FOR_PARAM_LOCAL_NOISE_VOLUME, //
         "Noise level"},

        {STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH, //
         "Osc1 PW"},

        {STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH, //
         "Osc2 PW"},

        {STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX, //
         "Osc1 wave pos."},

        {STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX, //
         "Osc2 wave pos."},

        {STRING_FOR_PARAM_LOCAL_LPF_RESONANCE, //
         "LPF resonance"},

        {STRING_FOR_PARAM_LOCAL_HPF_RESONANCE, //
         "HPF resonance"},

        {STRING_FOR_PARAM_LOCAL_PAN, //
         "Pan"},

        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME, //
         "FM mod1 level"},

        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME, //
         "FM mod2 level"},

        {STRING_FOR_PARAM_LOCAL_LPF_FREQ, //
         "LPF frequency"},

        {STRING_FOR_PARAM_LOCAL_PITCH_ADJUST, //
         "Pitch"},

        {STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST, //
         "Osc1 pitch"},

        {STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST, //
         "Osc2 pitch"},

        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST, //
         "FM mod1 pitch"},

        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST, //
         "FM mod2 pitch"},

        {STRING_FOR_PARAM_LOCAL_HPF_FREQ, //
         "HPF frequency"},

        {STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ, //
         "LFO2 rate"},

        {STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK, //
         "Env1 attack"},

        {STRING_FOR_PARAM_LOCAL_ENV_0_DECAY, //
         "Env1 decay"},

        {STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN, //
         "Env1 sustain"},

        {STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE, //
         "Env1 release"},

        {STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK, //
         "Env2 attack"},

        {STRING_FOR_PARAM_LOCAL_ENV_1_DECAY, //
         "Env2 decay"},

        {STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN, //
         "Env2 sustain"},

        {STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE, //
         "Env2 release"},

        {STRING_FOR_PARAM_GLOBAL_LFO_FREQ, //
         "LFO1 rate"},

        {STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX, //
         "Level"},

        {STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND, //
         "Level"},

        {STRING_FOR_PARAM_GLOBAL_DELAY_RATE, //
         "Delay rate"},

        {STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK, //
         "Delay amount"},

        {STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT, //
         "Reverb amount"},

        {STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE, //
         "Mod-FX rate"},

        {STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH, //
         "Mod-FX depth"},

        {STRING_FOR_PARAM_GLOBAL_ARP_RATE, //
         "Arp. rate"},

        {STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK, //
         "Mod1 feedback"},

        {STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK, //
         "Mod2 feedback"},

        {STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK, //
         "Carrier1 feed."},

        {STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK, //
         "Carrier2 feed."},

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

        // gui/menu_item/flash/status.h
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
    });
} // namespace deluge::l10n::language_map
