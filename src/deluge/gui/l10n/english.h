#pragma once
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
        {STRING_FOR_OFF, "Off"},
        {STRING_FOR_OK, "OK"},
        {STRING_FOR_DELETE, "Delete"},
        {STRING_FOR_SURE, "Sure"},

        // Menu Titles
        {STRING_FOR_AUDIO_INPUT_TITLE, "Audio source"},
        {STRING_FOR_ARE_YOU_SURE_QMARK, "Are you sure?"},
        {STRING_FOR_DELETE_QMARK, "Delete?"},

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

    });
} // namespace deluge::l10n::language_map
