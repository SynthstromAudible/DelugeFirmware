#pragma once
#include "gui/l10n/strings.h"
#include "strings.h"

namespace deluge::l10n::language_map {
using enum Strings;
constexpr std::array seven_segment = build_l10n_map({
    //Errors
    {STRING_FOR_ERROR_INSUFFICIENT_RAM, //
     "RAM"},
    {STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE, //
     "RAM"},
    {STRING_FOR_ERROR_SD_CARD, //
     "CARD"},
    {STRING_FOR_ERROR_SD_CARD_NOT_PRESENT, //
     "CARD"},
    {STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM, //
     "CARD"},
    {STRING_FOR_ERROR_FILE_CORRUPTED, //
     "CORRUPTED"},
    {STRING_FOR_ERROR_FILE_NOT_FOUND, //
     "FILE"},
    {STRING_FOR_ERROR_FILE_UNREADABLE, //
     "FILE"},
    {STRING_FOR_ERROR_FILE_UNSUPPORTED, //
     "UNSUPPORTED"},
    {STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW, //
     "FIRMWARE"},
    {STRING_FOR_ERROR_FOLDER_DOESNT_EXIST, //
     "FOLDER"},
    {STRING_FOR_ERROR_BUG, //
     "BUG"},
    {STRING_FOR_ERROR_WRITE_FAIL, //
     "FAIL"},
    {STRING_FOR_ERROR_FILE_TOO_BIG, //
     "BIG"},
    {STRING_FOR_ERROR_PRESET_IN_USE, //
     "USED"},
    {STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION, //
     "NONE"},
    {STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED, //
     "4GB"},
    {STRING_FOR_ERROR_SD_CARD_FULL, //
     "FULL"},
    {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE, //
     "CANT"},
    {STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO, //
     "STEREO"},
    {STRING_FOR_ERROR_WRITE_PROTECTED, //
     "WRITE PROTECTED"},
    {STRING_FOR_ERROR_GENERIC, //
     "ERROR"},

    // Patch sources
    {STRING_FOR_PATCH_SOURCE_LFO_GLOBAL, //
     "LFO1"},
    {STRING_FOR_PATCH_SOURCE_LFO_LOCAL, //
     "LFO2"},
    {STRING_FOR_PATCH_SOURCE_ENVELOPE_0, //
     "ENV1"},
    {STRING_FOR_PATCH_SOURCE_ENVELOPE_1, //
     "ENV2"},
    {STRING_FOR_PATCH_SOURCE_VELOCITY, //
     "VELOCITY"},
    {STRING_FOR_PATCH_SOURCE_NOTE, //
     "NOTE"},
    {STRING_FOR_PATCH_SOURCE_COMPRESSOR, //
     "SIDE"},
    {STRING_FOR_PATCH_SOURCE_RANDOM, //
     "RANDOM"},
    {STRING_FOR_PATCH_SOURCE_AFTERTOUCH, //
     "AFTERTOUCH"},
    {STRING_FOR_PATCH_SOURCE_X, //
     "X"},
    {STRING_FOR_PATCH_SOURCE_Y, //
     "Y"},

    // General
    {STRING_FOR_OFF, "OFF"},
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

    // gui/context_menu/sample_browser/kit.cpp
    {STRING_FOR_LOAD_ALL, "ALL"},
    {STRING_FOR_SLICE, "SLICE"},

    // gui/context_menu/sample_browser/synth.cpp
    {STRING_FOR_MULTISAMPLES, "MULTI"},
    {STRING_FOR_BASIC, "BASIC"},
    {STRING_FOR_SINGLE_CYCLE, "SINGLE-CYCLE"},
    {STRING_FOR_WAVETABLE, "WAVETABLE"},

    // gui/context_menu/load_instrument_preset.cpp
    {STRING_FOR_CLONE, "COPY"},

    // gui/context_menu/overwrite_bootloader.cpp
    {STRING_FOR_ACCEPT_RISK, "SURE"},

    // gui/context_menu/save_song_or_instrument.cpp
    {STRING_FOR_COLLECT_MEDIA, "COLLECT MEDIA"},
    {STRING_FOR_CREATE_FOLDER, "CREATE FOLDER"},
});
} // namespace deluge::l10n::language_map
