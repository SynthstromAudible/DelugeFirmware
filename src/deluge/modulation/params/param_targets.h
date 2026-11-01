/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include "deluge-project/param_address.h"
#include "effects/global_effectable/params.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/params.h"

#include <array>
#include <cstdint>
#include <optional>

/// @file The param shortcuts of the pad grid and the automation view's param orders, as targets. A sound's tables hold
/// the Deluge instrument's ids (a patch cable's as its depth id) and MPE expression targets; a global effectable's hold
/// the global-effectable table's ids.

namespace deluge::modulation::params {

namespace shortcuts_detail {
namespace ip = deluge::instrument::param;
namespace gp = deluge::global_effectable::param;
using deluge::project::ParamTarget;
[[nodiscard]] constexpr ParamTarget sound(deluge::plugin::ParamId id) {
	return deluge::project::engine_target(id.value);
}
[[nodiscard]] constexpr ParamTarget global(deluge::plugin::ParamId id) {
	return deluge::project::engine_target(id.value);
}
[[nodiscard]] constexpr ParamTarget cable(PatchSource source, deluge::plugin::ParamId destination) {
	return deluge::project::engine_target(deluge::instrument::cable_depth_id(source, destination)->value);
}
[[nodiscard]] constexpr ParamTarget expression(Expression dimension) {
	return {deluge::project::ParamSpace::EXPRESSION, deluge::plugin::ParamId{static_cast<uint32_t>(dimension)}};
}
} // namespace shortcuts_detail

/// @brief A pad-grid table of params, indexed [x][y]; an empty cell has no param.
using TargetGrid = std::array<std::array<std::optional<deluge::project::ParamTarget>, kDisplayHeight>, kDisplayWidth>;

// clang-format off
/// @brief A sound's shortcuts: its patched and unpatched params and the vibrato and sidechain-ducking cables.
inline constexpr TargetGrid kSoundParamShortcuts = [] {
	using namespace shortcuts_detail;
	return TargetGrid{{
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{sound(ip::kOscAVolume), sound(ip::kOscAPitch), std::nullopt, sound(ip::kOscAPhaseWidth), std::nullopt, sound(ip::kCarrier1Feedback), sound(ip::kOscAWavetablePosition), sound(ip::kNoiseVolume)}},
    {{sound(ip::kOscBVolume), sound(ip::kOscBPitch), std::nullopt, sound(ip::kOscBPhaseWidth), std::nullopt, sound(ip::kCarrier2Feedback), sound(ip::kOscBWavetablePosition), std::nullopt}},
    {{sound(ip::kModulator1Volume), sound(ip::kModulator1Pitch), std::nullopt, std::nullopt, std::nullopt, sound(ip::kModulator1Feedback), std::nullopt, std::nullopt}},
    {{sound(ip::kModulator2Volume), sound(ip::kModulator2Pitch), std::nullopt, std::nullopt, std::nullopt, sound(ip::kModulator2Feedback), std::nullopt, sound(ip::kStutterRate)}},
    {{sound(ip::kVolumePostFX), sound(ip::kPitch), cable(PatchSource::LFO_GLOBAL_1, ip::kPitch), sound(ip::kPan), std::nullopt, sound(ip::kSampleRateReduction), sound(ip::kBitcrushAmount), std::nullopt}},
    {{sound(ip::kPortamento), std::nullopt, std::nullopt, std::nullopt, std::nullopt, sound(ip::kCompressorThreshold), std::nullopt, sound(ip::kWaveFold)}},
    {{sound(ip::kEnv1Release), sound(ip::kEnv1Sustain), sound(ip::kEnv1Decay), sound(ip::kEnv1Attack), sound(ip::kLpfMorph), std::nullopt, sound(ip::kLpfResonance), sound(ip::kLpfFrequency)}},
    {{sound(ip::kEnv2Release), sound(ip::kEnv2Sustain), sound(ip::kEnv2Decay), sound(ip::kEnv2Attack), sound(ip::kHpfMorph), std::nullopt, sound(ip::kHpfResonance), sound(ip::kHpfFrequency)}},
    {{std::nullopt, std::nullopt, cable(PatchSource::SIDECHAIN, ip::kVolumePostReverbSend), std::nullopt, sound(ip::kCompressorShape), std::nullopt, sound(ip::kBass), sound(ip::kBassFreq)}},
    {{sound(ip::kArpRate), std::nullopt, sound(ip::kArpGate), std::nullopt, std::nullopt, std::nullopt, sound(ip::kTreble), sound(ip::kTrebleFreq)}},
    {{sound(ip::kLfo1Rate), std::nullopt, std::nullopt, std::nullopt, sound(ip::kModFXOffset), sound(ip::kModFXFeedback), sound(ip::kModFXDepth), sound(ip::kModFXRate)}},
    {{sound(ip::kLfo2Rate), std::nullopt, std::nullopt, sound(ip::kReverbAmount), std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{sound(ip::kDelayRate), std::nullopt, std::nullopt, sound(ip::kDelayFeedback), std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, sound(ip::kSpreadVelocity), std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
	}};
}();

/// @brief A sound's second shortcut layer (a second press of a first-layer shortcut).
inline constexpr TargetGrid kSoundParamShortcutsSecondLayer = [] {
	using namespace shortcuts_detail;
	return TargetGrid{{
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{sound(ip::kEnv3Release), sound(ip::kEnv3Sustain), sound(ip::kEnv3Decay), sound(ip::kEnv3Attack), std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{sound(ip::kEnv4Release), sound(ip::kEnv4Sustain), sound(ip::kEnv4Decay), sound(ip::kEnv4Attack), std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{sound(ip::kLfo3Rate), std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{sound(ip::kLfo4Rate), std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
	}};
}();

/// @brief A global effectable's shortcuts (the song, a kit's affect-entire, an audio clip).
inline constexpr TargetGrid kGlobalParamShortcuts = [] {
	using namespace shortcuts_detail;
	return TargetGrid{{
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, global(gp::kStutterRate)}},
    {{global(gp::kVolume), global(gp::kPitchAdjust), std::nullopt, global(gp::kPan), std::nullopt, global(gp::kSampleRateReduction), global(gp::kBitcrushAmount), std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, global(gp::kCompressorThreshold), std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, global(gp::kLpfMorph), std::nullopt, global(gp::kLpfResonance), global(gp::kLpfFrequency)}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, global(gp::kHpfMorph), std::nullopt, global(gp::kHpfResonance), global(gp::kHpfFrequency)}},
    {{std::nullopt, std::nullopt, global(gp::kSidechainCompressorVolume), std::nullopt, global(gp::kCompressorShape), std::nullopt, global(gp::kBass), global(gp::kBassFreq)}},
    {{global(gp::kArpRate), std::nullopt, global(gp::kArpGate), std::nullopt, std::nullopt, std::nullopt, global(gp::kTreble), global(gp::kTrebleFreq)}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, global(gp::kModFXOffset), global(gp::kModFXFeedback), global(gp::kModFXDepth), global(gp::kModFXRate)}},
    {{std::nullopt, std::nullopt, std::nullopt, global(gp::kReverbAmount), std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{global(gp::kDelayRate), std::nullopt, std::nullopt, global(gp::kDelayFeedback), std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
	}};
}();

/// @brief The order the automation view's select encoder walks a sound's params in.
inline constexpr std::array<deluge::project::ParamTarget, 83> kSoundParamsForAutomation = [] {
	using namespace shortcuts_detail;
	return std::array<ParamTarget, 83>{{
    sound(ip::kVolumePostFX),
    sound(ip::kPitch),
    sound(ip::kPan),
    sound(ip::kLpfFrequency),
    sound(ip::kLpfResonance),
    sound(ip::kLpfMorph),
    sound(ip::kHpfFrequency),
    sound(ip::kHpfResonance),
    sound(ip::kHpfMorph),
    sound(ip::kBass),
    sound(ip::kBassFreq),
    sound(ip::kTreble),
    sound(ip::kTrebleFreq),
    sound(ip::kReverbAmount),
    sound(ip::kDelayRate),
    sound(ip::kDelayFeedback),
    sound(ip::kCompressorShape),
    sound(ip::kSampleRateReduction),
    sound(ip::kBitcrushAmount),
    sound(ip::kWaveFold),
    sound(ip::kOscAVolume),
    sound(ip::kOscAPitch),
    sound(ip::kOscAPhaseWidth),
    sound(ip::kCarrier1Feedback),
    sound(ip::kOscAWavetablePosition),
    sound(ip::kOscBVolume),
    sound(ip::kOscBPitch),
    sound(ip::kOscBPhaseWidth),
    sound(ip::kCarrier2Feedback),
    sound(ip::kOscBWavetablePosition),
    sound(ip::kModulator1Volume),
    sound(ip::kModulator1Pitch),
    sound(ip::kModulator1Feedback),
    sound(ip::kModulator2Volume),
    sound(ip::kModulator2Pitch),
    sound(ip::kModulator2Feedback),
    sound(ip::kEnv1Attack),
    sound(ip::kEnv1Decay),
    sound(ip::kEnv1Sustain),
    sound(ip::kEnv1Release),
    sound(ip::kEnv2Attack),
    sound(ip::kEnv2Decay),
    sound(ip::kEnv2Sustain),
    sound(ip::kEnv2Release),
    sound(ip::kEnv3Attack),
    sound(ip::kEnv3Decay),
    sound(ip::kEnv3Sustain),
    sound(ip::kEnv3Release),
    sound(ip::kEnv4Attack),
    sound(ip::kEnv4Decay),
    sound(ip::kEnv4Sustain),
    sound(ip::kEnv4Release),
    sound(ip::kLfo1Rate),
    sound(ip::kLfo2Rate),
    sound(ip::kLfo3Rate),
    sound(ip::kLfo4Rate),
    sound(ip::kModFXOffset),
    sound(ip::kModFXFeedback),
    sound(ip::kModFXDepth),
    sound(ip::kModFXRate),
    sound(ip::kArpRate),
    sound(ip::kArpGate),
    sound(ip::kSpreadGate),
    sound(ip::kSpreadOctave),
    sound(ip::kSpreadVelocity),
    sound(ip::kRatchetAmount),
    sound(ip::kRatchetProbability),
    sound(ip::kChordPolyphony),
    sound(ip::kChordProbability),
    sound(ip::kNoteProbability),
    sound(ip::kBassProbability),
    sound(ip::kSwapProbability),
    sound(ip::kGlideProbability),
    sound(ip::kReverseProbability),
    sound(ip::kRhythm),
    sound(ip::kSequenceLength),
    sound(ip::kNoiseVolume),
    sound(ip::kPortamento),
    sound(ip::kStutterRate),
    sound(ip::kCompressorThreshold),
    expression(X_PITCH_BEND),
    expression(Y_SLIDE_TIMBRE),
    expression(Z_PRESSURE),
	}};
}();

/// @brief The order the automation view's select encoder walks a global effectable's params in.
inline constexpr std::array<deluge::project::ParamTarget, 39> kGlobalParamsForAutomation = [] {
	using namespace shortcuts_detail;
	return std::array<ParamTarget, 39>{{
    global(gp::kVolume),
    global(gp::kPitchAdjust),
    global(gp::kPan),
    global(gp::kLpfFrequency),
    global(gp::kLpfResonance),
    global(gp::kLpfMorph),
    global(gp::kHpfFrequency),
    global(gp::kHpfResonance),
    global(gp::kHpfMorph),
    global(gp::kBass),
    global(gp::kBassFreq),
    global(gp::kTreble),
    global(gp::kTrebleFreq),
    global(gp::kReverbAmount),
    global(gp::kDelayRate),
    global(gp::kDelayFeedback),
    global(gp::kSidechainCompressorVolume),
    global(gp::kCompressorShape),
    global(gp::kSampleRateReduction),
    global(gp::kBitcrushAmount),
    global(gp::kModFXOffset),
    global(gp::kModFXFeedback),
    global(gp::kModFXDepth),
    global(gp::kModFXRate),
    global(gp::kStutterRate),
    global(gp::kCompressorThreshold),
    global(gp::kArpRate),
    global(gp::kArpGate),
    global(gp::kSpreadGate),
    global(gp::kSpreadVelocity),
    global(gp::kRatchetAmount),
    global(gp::kRatchetProbability),
    global(gp::kNoteProbability),
    global(gp::kBassProbability),
    global(gp::kSwapProbability),
    global(gp::kGlideProbability),
    global(gp::kReverseProbability),
    global(gp::kRhythm),
    global(gp::kSequenceLength),
	}};
}();
// clang-format on

/// @brief The MPE expression shortcut at pad (@p x, @p y), if there is one.
[[nodiscard]] constexpr std::optional<deluge::project::ParamTarget> expressionShortcut(int32_t x, int32_t y) {
	using namespace shortcuts_detail;
	if (x == 14 && y == 7) {
		return expression(X_PITCH_BEND);
	}
	if (x == 15 && y == 0) {
		return expression(Z_PRESSURE);
	}
	if (x == 15 && y == 7) {
		return expression(Y_SLIDE_TIMBRE);
	}
	return std::nullopt;
}

} // namespace deluge::modulation::params
