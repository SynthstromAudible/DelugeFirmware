/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "modulation/params/param_descriptor.h"
#include "util/misc.h"
#include <algorithm>
#include <cstdint>

/// \namespace deluge::modulation::params
/// "Param"s are at the heart of the Deluge's modulation capabilities.
///
/// Each Param is identified by its Kind and its Type.
/// The Kind identifies how the numeric value of the Type should be interpreted.
///
/// Params have 4 separate modes via which they can be combined:
///   - Linear params have different sources multiplied together, then multiplied by the neutral value.
///   - Volume params act like linear params in how they are combined, but the sum is squared at the end.
///   - Hybrid params have different sources added together, then added to the neutral value
///   - Exp (exponential) params have different sources added together, converted to an exponential scale, then
///   multiplied by the neutral value

namespace deluge::modulation::params {
enum class Kind : int32_t {
	NONE,

	/// Voice-local parameters which can be modulated via the mod matrix
	PATCHED,
	/// Parameters which apply to the internal sound engine specifically
	UNPATCHED_SOUND,
	/// Parameters which can not be modulated and apply to the whole Output
	UNPATCHED_GLOBAL,
	/// Unused
	STATIC,
	/// Internal representation for MIDI CCs
	MIDI,
	/// Modulates the strength of the modulation between a mod source and another param
	PATCH_CABLE,
	/// Parameter connected to an MPE expression axis
	EXPRESSION,
};

/// Integer type used for all Param enumerations.
///
/// Should be as small as possible for efficiency. Some things might also depend on this being exactly uint8_t, so try
/// to audit for that if you need to change it.
using ParamType = uint8_t;

/// "Local" patched params, which apply to individual voices within the sound
enum Local : ParamType {
	// Local linear params begin
	LOCAL_OSC_A_VOLUME,
	LOCAL_OSC_B_VOLUME,
	LOCAL_VOLUME,
	LOCAL_NOISE_VOLUME,
	LOCAL_MODULATOR_0_VOLUME,
	LOCAL_MODULATOR_1_VOLUME,
	LOCAL_FOLD,

	// Local non-volume params begin
	FIRST_LOCAL_NON_VOLUME,
	LOCAL_MODULATOR_0_FEEDBACK = FIRST_LOCAL_NON_VOLUME,
	LOCAL_MODULATOR_1_FEEDBACK,
	LOCAL_CARRIER_0_FEEDBACK,
	LOCAL_CARRIER_1_FEEDBACK,
	LOCAL_LPF_RESONANCE,
	LOCAL_HPF_RESONANCE,
	LOCAL_ENV_0_SUSTAIN,
	LOCAL_ENV_1_SUSTAIN,
	LOCAL_LPF_MORPH,
	LOCAL_HPF_MORPH,

	// Local hybrid params begin
	FIRST_LOCAL__HYBRID,
	LOCAL_OSC_A_PHASE_WIDTH = FIRST_LOCAL__HYBRID,
	LOCAL_OSC_B_PHASE_WIDTH,
	LOCAL_OSC_A_WAVE_INDEX,
	LOCAL_OSC_B_WAVE_INDEX,
	LOCAL_PAN,

	// Local exp params begin
	FIRST_LOCAL_EXP,
	LOCAL_LPF_FREQ = FIRST_LOCAL_EXP,
	LOCAL_PITCH_ADJUST,
	LOCAL_OSC_A_PITCH_ADJUST,
	LOCAL_OSC_B_PITCH_ADJUST,
	LOCAL_MODULATOR_0_PITCH_ADJUST,
	LOCAL_MODULATOR_1_PITCH_ADJUST,
	LOCAL_HPF_FREQ,
	LOCAL_LFO_LOCAL_FREQ,
	LOCAL_ENV_0_ATTACK,
	LOCAL_ENV_1_ATTACK,
	LOCAL_ENV_0_DECAY,
	LOCAL_ENV_1_DECAY,
	LOCAL_ENV_0_RELEASE,
	LOCAL_ENV_1_RELEASE,

	/// Special value used to chain in to the Global params.
	LOCAL_LAST,
};

/// "Global" patched params, which apply to the whole sound.
/// ANY TIME YOU UPDATE THIS LIST, please also update getParamDisplayName and paramNameForFile!
enum Global : ParamType {
	FIRST_GLOBAL = LOCAL_LAST,
	// Global (linear) params begin
	GLOBAL_VOLUME_POST_FX = FIRST_GLOBAL,
	GLOBAL_VOLUME_POST_REVERB_SEND,
	GLOBAL_REVERB_AMOUNT,
	GLOBAL_MOD_FX_DEPTH,

	// Global non-volume params begin
	FIRST_GLOBAL_NON_VOLUME,
	GLOBAL_DELAY_FEEDBACK = FIRST_GLOBAL_NON_VOLUME,

	// Global hybrid params begin

	// There are no global hybrid params, so FIRST_GLOBAL_EXP is set to the same value. If you add a GLOBAL_HYBRID
	// param, make sure you undo that!
	FIRST_GLOBAL_HYBRID,

	// Global exp params begin
	FIRST_GLOBAL_EXP = FIRST_GLOBAL_HYBRID,
	GLOBAL_DELAY_RATE = FIRST_GLOBAL_EXP,
	GLOBAL_MOD_FX_RATE,
	GLOBAL_LFO_FREQ,
	GLOBAL_ARP_RATE,

	GLOBAL_NONE,
};

/// Fake param IDs for use when loading old presets
enum Placeholder : ParamType {
	/// Not a real param. For the purpose of reading old files from before V3.2.0
	PLACEHOLDER_RANGE = 89,
};

static_assert(util::to_underlying(PLACEHOLDER_RANGE) > util::to_underlying(GLOBAL_NONE),
              "RANGE placeholder collides with global params");

/// Offset to use for the start of unpatched params when patched and unpatched params need to be compressed in to a
/// single array.
constexpr ParamType UNPATCHED_START = 90;
static_assert(UNPATCHED_START > PLACEHOLDER_RANGE, "UNPATCHED params collide with placeholders");

/// IDs for UNPATCHED_* params, for all ModControllables. This is the prefix of UNPATCHED params shared between Sounds
/// and GlobalEffectables.
/// ANY TIME YOU UPDATE THIS LIST! paramNameForFile() in param.cpp
enum UnpatchedShared : ParamType {
	UNPATCHED_STUTTER_RATE,
	UNPATCHED_BASS,
	UNPATCHED_TREBLE,
	UNPATCHED_BASS_FREQ,
	UNPATCHED_TREBLE_FREQ,
	UNPATCHED_SAMPLE_RATE_REDUCTION,
	UNPATCHED_BITCRUSHING,
	UNPATCHED_MOD_FX_OFFSET,
	UNPATCHED_MOD_FX_FEEDBACK,
	UNPATCHED_SIDECHAIN_SHAPE,
	UNPATCHED_COMPRESSOR_THRESHOLD,
	/// Special value for chaining the UNPATCHED_* params
	UNPATCHED_NUM_SHARED,
};

/// Unpatched params which are only used for Sounds
enum UnpatchedSound : ParamType {
	UNPATCHED_ARP_GATE = UNPATCHED_NUM_SHARED,
	UNPATCHED_ARP_RATCHET_PROBABILITY,
	UNPATCHED_ARP_RATCHET_AMOUNT,
	UNPATCHED_ARP_SEQUENCE_LENGTH,
	UNPATCHED_ARP_RHYTHM,
	UNPATCHED_PORTAMENTO,
	UNPATCHED_SOUND_MAX_NUM,
};

/// Just for GlobalEffectables
enum UnpatchedGlobal : ParamType {
	UNPATCHED_MOD_FX_RATE = UNPATCHED_NUM_SHARED,
	UNPATCHED_MOD_FX_DEPTH,
	UNPATCHED_DELAY_RATE,
	UNPATCHED_DELAY_AMOUNT,
	UNPATCHED_PAN,
	UNPATCHED_LPF_FREQ,
	UNPATCHED_LPF_RES,
	UNPATCHED_LPF_MORPH,
	UNPATCHED_HPF_FREQ,
	UNPATCHED_HPF_RES,
	UNPATCHED_HPF_MORPH,
	UNPATCHED_REVERB_SEND_AMOUNT,
	UNPATCHED_VOLUME,
	UNPATCHED_SIDECHAIN_VOLUME,
	UNPATCHED_PITCH_ADJUST,
	UNPATCHED_TEMPO,
	UNPATCHED_GLOBAL_MAX_NUM,
};

constexpr ParamType STATIC_START = 162;

enum Static : ParamType {
	STATIC_SIDECHAIN_ATTACK = STATIC_START,
	STATIC_SIDECHAIN_RELEASE,

	// Only used for the reverb sidechain. Normally this is done with patching
	STATIC_SIDECHAIN_VOLUME,
};

/// Special case for representing patch cables
constexpr ParamType PATCH_CABLE = 190;

/// None is the last global param, 0 indexed so it's also the number of patched params
constexpr ParamType kNumParams = GLOBAL_NONE;
constexpr ParamType kMaxNumUnpatchedParams =
    std::max<ParamType>(util::to_underlying(UNPATCHED_GLOBAL_MAX_NUM), util::to_underlying(UNPATCHED_SOUND_MAX_NUM));

/// The absolute highest param number used by patched or unpatched params
///
/// fileStrongToParam uses this to iterate over all known parameters.
constexpr ParamType kUnpatchedAndPatchedMaximum = kMaxNumUnpatchedParams + UNPATCHED_START;

static_assert(kMaxNumUnpatchedParams < STATIC_START, "Error: Too many UNPATCHED parameters, (collision with STATIC)");

bool isParamBipolar(Kind kind, int32_t paramID);
bool isParamPan(Kind kind, int32_t paramID);
bool isParamPitch(Kind kind, int32_t paramID);
bool isParamArpRhythm(Kind kind, int32_t paramID);
bool isParamStutter(Kind kind, int32_t paramID);
bool isParamQuantizedStutter(Kind kind, int32_t paramID);

bool isVibratoPatchCableShortcut(int32_t xDisplay, int32_t yDisplay);
bool isSidechainPatchCableShortcut(int32_t xDisplay, int32_t yDisplay);
bool isPatchCableShortcut(int32_t xDisplay, int32_t yDisplay);
void getPatchCableFromShortcut(int32_t xDisplay, int32_t yDisplay, ParamDescriptor* paramDescriptor);

char const* getPatchedParamDisplayName(int32_t p);
/// Get the short version of a param name, for use in the OLED mod matrix display (maximum 10 characters)
char const* getPatchedParamShortName(ParamType type);
char const* getParamDisplayName(Kind kind, int32_t p);

bool paramNeedsLPF(ParamType p, bool fromAutomation);

/// Convert a ParamType (along with its Kind) in to a string suitable for use as an XML attribute name.
///
/// This handles Param::Local and Param::Unpatched. If an Param::Unpatched is passed for param, it *must* be offset by
/// UNPATCHED_START. The Kind is used to distinguish between UNPATCHED_Sound
/// (when kind == UNPATCHED_SOUND) and UNPATCHED_GlobalEffectable (when kind == UNPATCHED_GLOBAL) as these
/// two sub-ranges would otherwise overlap.
char const* paramNameForFile(Kind kind, ParamType param);

/// Given a string and the expected Kind, attempts to find the ParamType value for that param.
///
/// As with paramNameForFile, the returned ParamType is offset by UNPATCHED_START for unpatched params.
ParamType fileStringToParam(Kind kind, char const* name, bool allowPatched);

/// Magic number which represents an invalid or missing param type
constexpr uint32_t kNoParamID = 0xFFFFFFFF;

/// Grid sized array (patched param version) to assign automatable parameters to the grid
/// used in automation view and in midi follow
// clang-format off
const uint32_t patchedParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID              , kNoParamID                    , kNoParamID                    , kNoParamID             , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID},
    {kNoParamID              , kNoParamID                    , kNoParamID                    , kNoParamID             , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID},
    {LOCAL_OSC_A_VOLUME      , LOCAL_OSC_A_PITCH_ADJUST      , kNoParamID                    , LOCAL_OSC_A_PHASE_WIDTH, kNoParamID     , LOCAL_CARRIER_0_FEEDBACK  , LOCAL_OSC_A_WAVE_INDEX, LOCAL_NOISE_VOLUME},
    {LOCAL_OSC_B_VOLUME      , LOCAL_OSC_B_PITCH_ADJUST      , kNoParamID                    , LOCAL_OSC_B_PHASE_WIDTH, kNoParamID     , LOCAL_CARRIER_1_FEEDBACK  , LOCAL_OSC_B_WAVE_INDEX, kNoParamID},
    {LOCAL_MODULATOR_0_VOLUME, LOCAL_MODULATOR_0_PITCH_ADJUST, kNoParamID                    , kNoParamID             , kNoParamID     , LOCAL_MODULATOR_0_FEEDBACK, kNoParamID            , kNoParamID},
    {LOCAL_MODULATOR_1_VOLUME, LOCAL_MODULATOR_1_PITCH_ADJUST, kNoParamID                    , kNoParamID             , kNoParamID     , LOCAL_MODULATOR_1_FEEDBACK, kNoParamID            , kNoParamID},
    {GLOBAL_VOLUME_POST_FX   , LOCAL_PITCH_ADJUST            , kNoParamID                    , LOCAL_PAN              , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID},
    {kNoParamID              , kNoParamID                    , kNoParamID                    , kNoParamID             , kNoParamID     , kNoParamID                , kNoParamID            , LOCAL_FOLD},
    {LOCAL_ENV_0_RELEASE     , LOCAL_ENV_0_SUSTAIN           , LOCAL_ENV_0_DECAY             , LOCAL_ENV_0_ATTACK     , LOCAL_LPF_MORPH, kNoParamID                , LOCAL_LPF_RESONANCE   , LOCAL_LPF_FREQ},
    {LOCAL_ENV_1_RELEASE     , LOCAL_ENV_1_SUSTAIN           , LOCAL_ENV_1_DECAY             , LOCAL_ENV_1_ATTACK     , LOCAL_HPF_MORPH, kNoParamID                , LOCAL_HPF_RESONANCE   , LOCAL_HPF_FREQ},
    {kNoParamID              , kNoParamID                    , kNoParamID					 , kNoParamID             , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID},
    {GLOBAL_ARP_RATE         , kNoParamID                    , kNoParamID                    , kNoParamID             , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID},
    {GLOBAL_LFO_FREQ         , kNoParamID                    , kNoParamID                    , kNoParamID             , kNoParamID     , kNoParamID                , GLOBAL_MOD_FX_DEPTH   , GLOBAL_MOD_FX_RATE},
    {LOCAL_LFO_LOCAL_FREQ    , kNoParamID                    , kNoParamID                    , GLOBAL_REVERB_AMOUNT   , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID},
    {GLOBAL_DELAY_RATE       , kNoParamID                    , kNoParamID                    , GLOBAL_DELAY_FEEDBACK  , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID},
    {kNoParamID              , kNoParamID                    , kNoParamID                    , kNoParamID             , kNoParamID     , kNoParamID                , kNoParamID            , kNoParamID}
};
// clang-format on

/// Grid sized array (unpatched, non-global) to assign automatable parameters to the grid
/// used in automation view and in midi follow
// clang-format off
const uint32_t unpatchedNonGlobalParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , UNPATCHED_STUTTER_RATE},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , UNPATCHED_SAMPLE_RATE_REDUCTION, UNPATCHED_BITCRUSHING, kNoParamID},
    {UNPATCHED_PORTAMENTO, kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, UNPATCHED_SIDECHAIN_SHAPE , kNoParamID                     , UNPATCHED_BASS       , UNPATCHED_BASS_FREQ},
    {kNoParamID          , kNoParamID, UNPATCHED_ARP_GATE, kNoParamID, kNoParamID                , kNoParamID                     , UNPATCHED_TREBLE     , UNPATCHED_TREBLE_FREQ},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, UNPATCHED_MOD_FX_OFFSET   , UNPATCHED_MOD_FX_FEEDBACK      , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID},
    {kNoParamID          , kNoParamID, kNoParamID        , kNoParamID, kNoParamID                , kNoParamID                     , kNoParamID           , kNoParamID}
};
// clang-format on

/// Grid sized array (unpatched, global) to assign automatable parameters to the grid
/// used in automation view and in midi follow
// clang-format off
const uint32_t unpatchedGlobalParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , UNPATCHED_STUTTER_RATE},
    {UNPATCHED_VOLUME    , UNPATCHED_PITCH_ADJUST, kNoParamID                , UNPATCHED_PAN               , kNoParamID				   , UNPATCHED_SAMPLE_RATE_REDUCTION, UNPATCHED_BITCRUSHING , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , UNPATCHED_LPF_MORPH	   , kNoParamID						, UNPATCHED_LPF_RES     , UNPATCHED_LPF_FREQ},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , UNPATCHED_HPF_MORPH	   , kNoParamID						, UNPATCHED_HPF_RES     , UNPATCHED_HPF_FREQ},
    {kNoParamID          , kNoParamID            , UNPATCHED_SIDECHAIN_VOLUME, kNoParamID                  , UNPATCHED_SIDECHAIN_SHAPE , kNoParamID			   			, UNPATCHED_BASS        , UNPATCHED_BASS_FREQ},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, UNPATCHED_TREBLE      , UNPATCHED_TREBLE_FREQ},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , UNPATCHED_MOD_FX_OFFSET   , UNPATCHED_MOD_FX_FEEDBACK		, UNPATCHED_MOD_FX_DEPTH, UNPATCHED_MOD_FX_RATE},
    {kNoParamID          , kNoParamID            , kNoParamID                , UNPATCHED_REVERB_SEND_AMOUNT, kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID},
    {UNPATCHED_DELAY_RATE, kNoParamID            , kNoParamID                , UNPATCHED_DELAY_AMOUNT      , kNoParamID				   , kNoParamID			  		 	, kNoParamID            , kNoParamID},
    {kNoParamID          , kNoParamID            , kNoParamID                , kNoParamID                  , kNoParamID				   , kNoParamID			   		 	, kNoParamID            , kNoParamID}};
// clang-format on

} // namespace deluge::modulation::params
