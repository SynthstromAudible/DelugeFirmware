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
#include "util/misc.h"
#include <algorithm>
#include <cstdint>

namespace deluge {
namespace modulation {
namespace params {

enum Kind : int32_t {
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

/// Parameters in the Deluge can be divided in to classes on several axies:
///   - The source kind of the parameter. This is encapsulated in the Kind of the param.
///   - The domain of the param (Local, Global, Shared, Sound, or GlobalEffectable)
///   -
/// Linear params have different sources multiplied together, then multiplied by the neutral value
/// -- and "volume" ones get squared at the end
/// Hybrid params have different sources added together, then added to the neutral value
/// Exp params have different sources added together, converted to an exponential scale, then multiplied by the neutral value
namespace Param {

namespace Local {
enum : ParamType {
	// Local linear params begin
	OSC_A_VOLUME,
	OSC_B_VOLUME,
	VOLUME,
	NOISE_VOLUME,
	MODULATOR_0_VOLUME,
	MODULATOR_1_VOLUME,
	FOLD,

	// Local non-volume params begin
	MODULATOR_0_FEEDBACK,
	MODULATOR_1_FEEDBACK,
	CARRIER_0_FEEDBACK,
	CARRIER_1_FEEDBACK,
	LPF_RESONANCE,
	HPF_RESONANCE,
	ENV_0_SUSTAIN,
	ENV_1_SUSTAIN,
	LPF_MORPH,
	HPF_MORPH,

	// Local hybrid params begin
	OSC_A_PHASE_WIDTH,
	OSC_B_PHASE_WIDTH,
	OSC_A_WAVE_INDEX,
	OSC_B_WAVE_INDEX,
	PAN,

	// Local exp params begin
	LPF_FREQ,
	PITCH_ADJUST,
	OSC_A_PITCH_ADJUST,
	OSC_B_PITCH_ADJUST,
	MODULATOR_0_PITCH_ADJUST,
	MODULATOR_1_PITCH_ADJUST,
	HPF_FREQ,
	LFO_LOCAL_FREQ,
	ENV_0_ATTACK,
	ENV_1_ATTACK,
	ENV_0_DECAY,
	ENV_1_DECAY,
	ENV_0_RELEASE,
	ENV_1_RELEASE,

	LAST,
};
constexpr ParamType FIRST_NON_VOLUME = MODULATOR_0_FEEDBACK;
constexpr ParamType FIRST_HYBRID = OSC_A_PHASE_WIDTH;
constexpr ParamType FIRST_EXP = LPF_FREQ;
} // namespace Local

namespace Global {
enum : ParamType {
	// Global (linear) params begin
	VOLUME_POST_FX = Local::LAST,
	VOLUME_POST_REVERB_SEND,
	REVERB_AMOUNT,
	MOD_FX_DEPTH,

	// Global non-volume params begin
	DELAY_FEEDBACK,

	// Global hybrid params begin
	// Global exp params begin
	DELAY_RATE,
	MOD_FX_RATE,
	LFO_FREQ,
	ARP_RATE,
	// ANY TIME YOU UPDATE THIS LIST! CHANGE Sound::paramToString()

	NONE,
};
constexpr ParamType FIRST = Global::VOLUME_POST_FX;
constexpr ParamType FIRST_NON_VOLUME = Global::DELAY_FEEDBACK;
constexpr ParamType FIRST_HYBRID = Global::DELAY_RATE;
constexpr ParamType FIRST_EXP = Global::DELAY_RATE;
} // namespace Global

constexpr ParamType PLACEHOLDER_RANGE = 89; // Not a real param. For the purpose of reading old files from before V3.2.0
namespace Unpatched {
constexpr ParamType START = 90;

enum Shared : ParamType {
	// For all ModControllables
	STUTTER_RATE,
	BASS,
	TREBLE,
	BASS_FREQ,
	TREBLE_FREQ,
	SAMPLE_RATE_REDUCTION,
	BITCRUSHING,
	MOD_FX_OFFSET,
	MOD_FX_FEEDBACK,
	COMPRESSOR_SHAPE,
	// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp
	NUM_SHARED,
};

// Just for Sounds
namespace Sound {
enum : ParamType {
	ARP_GATE = Param::Unpatched::NUM_SHARED,
	PORTAMENTO,
	// ANY TIME YOU UPDATE THIS LIST! paramToString() in functions.cpp
	MAX_NUM,
};
}

// Just for GlobalEffectables
namespace GlobalEffectable {
enum : ParamType {
	MOD_FX_RATE = Param::Unpatched::NUM_SHARED,
	MOD_FX_DEPTH,
	DELAY_RATE,
	DELAY_AMOUNT,
	PAN,
	LPF_FREQ,
	LPF_RES,
	HPF_FREQ,
	HPF_RES,
	REVERB_SEND_AMOUNT,
	VOLUME,
	SIDECHAIN_VOLUME,
	PITCH_ADJUST,
	MAX_NUM,
};
}
} // namespace Unpatched

namespace Static {
constexpr ParamType START = 162;

enum : ParamType {
	COMPRESSOR_ATTACK = START,
	COMPRESSOR_RELEASE,

	// Only used for the reverb compressor. Normally this is done with patching
	COMPRESSOR_VOLUME,
	PATCH_CABLE = 190, // Special case
};
} // namespace Static
static_assert(std::max<ParamType>(Unpatched::GlobalEffectable::MAX_NUM, Unpatched::Sound::MAX_NUM) + Unpatched::START
                  < Static::START,
              "Error: Too many Param::Unpatched (collision with Param::Static)");

} // namespace Param

//None is the last global param, 0 indexed so it's also the number of real params
constexpr ParamType kNumParams = util::to_underlying(Param::Global::NONE);
constexpr ParamType kMaxNumUnpatchedParams = Param::Unpatched::GlobalEffectable::MAX_NUM;

char const* getPatchedParamDisplayName(int32_t p);
char const* getParamDisplayName(Kind kind, int32_t p);

/// Magic number which represents an invalid or missing param type
constexpr uint32_t kNoParamID = 0xFFFFFFFF;

/// Grid sized array (patched param version) to assign automatable parameters to the grid
/// used in automation view and in midi follow
const uint32_t patchedParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Local::OSC_A_VOLUME, Param::Local::OSC_A_PITCH_ADJUST, kNoParamID, Param::Local::OSC_A_PHASE_WIDTH,
     kNoParamID, Param::Local::CARRIER_0_FEEDBACK, Param::Local::OSC_A_WAVE_INDEX, Param::Local::NOISE_VOLUME},
    {Param::Local::OSC_B_VOLUME, Param::Local::OSC_B_PITCH_ADJUST, kNoParamID, Param::Local::OSC_B_PHASE_WIDTH,
     kNoParamID, Param::Local::CARRIER_1_FEEDBACK, Param::Local::OSC_B_WAVE_INDEX, kNoParamID},
    {Param::Local::MODULATOR_0_VOLUME, Param::Local::MODULATOR_0_PITCH_ADJUST, kNoParamID, kNoParamID, kNoParamID,
     Param::Local::MODULATOR_0_FEEDBACK, kNoParamID, kNoParamID},
    {Param::Local::MODULATOR_1_VOLUME, Param::Local::MODULATOR_1_PITCH_ADJUST, kNoParamID, kNoParamID, kNoParamID,
     Param::Local::MODULATOR_1_FEEDBACK, kNoParamID, kNoParamID},
    {Param::Global::VOLUME_POST_FX, Param::Local::PITCH_ADJUST, kNoParamID, Param::Local::PAN, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Local::FOLD},
    {Param::Local::ENV_0_RELEASE, Param::Local::ENV_0_SUSTAIN, Param::Local::ENV_0_DECAY, Param::Local::ENV_0_ATTACK,
     Param::Local::LPF_MORPH, kNoParamID, Param::Local::LPF_RESONANCE, Param::Local::LPF_FREQ},
    {Param::Local::ENV_1_RELEASE, Param::Local::ENV_1_SUSTAIN, Param::Local::ENV_1_DECAY, Param::Local::ENV_1_ATTACK,
     Param::Local::HPF_MORPH, kNoParamID, Param::Local::HPF_RESONANCE, Param::Local::HPF_FREQ},
    {kNoParamID, kNoParamID, Param::Global::VOLUME_POST_REVERB_SEND, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID},
    {Param::Global::ARP_RATE, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Global::LFO_FREQ, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Global::MOD_FX_DEPTH,
     Param::Global::MOD_FX_RATE},
    {Param::Local::LFO_LOCAL_FREQ, kNoParamID, kNoParamID, Param::Global::REVERB_AMOUNT, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {Param::Global::DELAY_RATE, kNoParamID, kNoParamID, Param::Global::DELAY_FEEDBACK, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

/// Grid sized array (unpatched, non-global) to assign automatable parameters to the grid
/// used in automation view and in midi follow
const uint32_t unpatchedNonGlobalParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::STUTTER_RATE},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::SAMPLE_RATE_REDUCTION,
     Param::Unpatched::BITCRUSHING, kNoParamID},
    {Param::Unpatched::Sound::PORTAMENTO, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::COMPRESSOR_SHAPE, kNoParamID,
     Param::Unpatched::BASS, Param::Unpatched::BASS_FREQ},
    {kNoParamID, kNoParamID, Param::Unpatched::Sound::ARP_GATE, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::TREBLE, Param::Unpatched::TREBLE_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::MOD_FX_OFFSET, Param::Unpatched::MOD_FX_FEEDBACK,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

/// Grid sized array (unpatched, global) to assign automatable parameters to the grid
/// used in automation view and in midi follow
const uint32_t unpatchedGlobalParamShortcuts[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {Param::Unpatched::GlobalEffectable::VOLUME, Param::Unpatched::GlobalEffectable::PITCH_ADJUST, kNoParamID,
     Param::Unpatched::GlobalEffectable::PAN, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::LPF_RES, Param::Unpatched::GlobalEffectable::LPF_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::HPF_RES, Param::Unpatched::GlobalEffectable::HPF_FREQ},
    {kNoParamID, kNoParamID, Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH, Param::Unpatched::GlobalEffectable::MOD_FX_RATE},
    {kNoParamID, kNoParamID, kNoParamID, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT, kNoParamID, kNoParamID,
     kNoParamID, kNoParamID},
    {Param::Unpatched::GlobalEffectable::DELAY_RATE, kNoParamID, kNoParamID,
     Param::Unpatched::GlobalEffectable::DELAY_AMOUNT, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID}};

} // namespace params
} // namespace modulation
} // namespace deluge
