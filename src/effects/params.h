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

/// @file
/// @brief Every effect param, declared once: what its stored word means to the effect and how a UI positions it. An
///        owner's table gives each its id and default (effects/param_row.h); the effects read their copies.

#include "effects/param_fragment.h"

#include <array>

namespace deluge::effects::params {

// The filters, in Filter::Words order. Neutrals: the cutoffs Q31 of the filter's frequency scale, the resonances and
// morphs Q29 (2^29 is 1).
inline constexpr EffectParam kLpfFrequency{
    .name = "lpfFrequency", .curve = dsp::fixed::ParamCurve::EXPONENTIAL, .neutral = 2000000};
inline constexpr EffectParam kLpfResonance{
    .name = "lpfResonance", .curve = dsp::fixed::ParamCurve::LINEAR, .neutral = 268435450};
inline constexpr EffectParam kLpfMorph{
    .name = "lpfMorph", .curve = dsp::fixed::ParamCurve::LINEAR, .neutral = 268435450};
inline constexpr EffectParam kHpfFrequency{
    .name = "hpfFrequency", .curve = dsp::fixed::ParamCurve::EXPONENTIAL, .neutral = 2672947};
inline constexpr EffectParam kHpfResonance{
    .name = "hpfResonance", .curve = dsp::fixed::ParamCurve::LINEAR, .neutral = 268435450};
inline constexpr EffectParam kHpfMorph{
    .name = "hpfMorph", .curve = dsp::fixed::ParamCurve::LINEAR, .neutral = 268435450};
/// @brief The filters' params, in Filter::Words order.
inline constexpr std::array<EffectParam, 6> kFilterParams{kLpfFrequency, kLpfResonance, kLpfMorph,
                                                          kHpfFrequency, kHpfResonance, kHpfMorph};

// The EQ, in EqualizerParams order: their words are used as stored.
inline constexpr EffectParam kBass{.name = "bass", .flags = plugin::ParamFlag::MIDDLE_DETENT};
inline constexpr EffectParam kTreble{.name = "treble", .flags = plugin::ParamFlag::MIDDLE_DETENT};
inline constexpr EffectParam kBassFrequency{.name = "bassFreq"};
inline constexpr EffectParam kTrebleFrequency{.name = "trebleFreq"};
/// @brief The EQ's params, in EqualizerParams order.
inline constexpr std::array<EffectParam, 4> kEqualizerParams{kBass, kTreble, kBassFrequency, kTrebleFrequency};

// The bitcrush, in BitcrushParams order: their words are used as stored.
inline constexpr EffectParam kBitcrushAmount{.name = "bitcrushAmount"};
inline constexpr EffectParam kSampleRateReduction{.name = "sampleRateReduction"};
/// @brief The bitcrush's params, in BitcrushParams order.
inline constexpr std::array<EffectParam, 2> kBitcrushParams{kBitcrushAmount, kSampleRateReduction};

// The mod FX, in ModFx::Words order. Neutrals: the rate UQ0.32 per frame, the depth Q31.
inline constexpr EffectParam kModFxRate{
    .name = "modFXRate", .curve = dsp::fixed::ParamCurve::EXPONENTIAL, .neutral = 121739};
inline constexpr EffectParam kModFxDepth{
    .name = "modFXDepth", .curve = dsp::fixed::ParamCurve::SQUARE, .neutral = 526133494};
inline constexpr EffectParam kModFxOffset{.name = "modFXOffset"};
inline constexpr EffectParam kModFxFeedback{.name = "modFXFeedback"};
/// @brief The mod FX's params, in ModFx::Words order.
inline constexpr std::array<EffectParam, 4> kModFxParams{kModFxRate, kModFxDepth, kModFxOffset, kModFxFeedback};

/// @brief The delay's rate param. Neutral: 2^24, the neutral rate.
inline constexpr EffectParam kDelayRate{.name = "delayRate",
                                        .curve = dsp::fixed::ParamCurve::EXPONENTIAL,
                                        .neutral = 16777216,
                                        .flags = plugin::ParamFlag::MIDDLE_DETENT};
/// @brief The delay's feedback param. Neutral: 2^30 (Q31 feedback).
inline constexpr EffectParam kDelayFeedback{.name = "delayFeedback",
                                            .curve = dsp::fixed::ParamCurve::LINEAR,
                                            .neutral = 1073741824,
                                            .flags = plugin::ParamFlag::MIDDLE_DETENT};

/// @brief An owner's reverb send param. Neutral: 2^27, Q31 send; a clip's owner builds it around its own amount.
inline constexpr EffectParam kReverbSend{
    .name = "reverbAmount", .curve = dsp::fixed::ParamCurve::SQUARE, .neutral = 134217728};

/// @brief The stutter's rate param, on the delay's rate scale (neutral 2^24).
inline constexpr EffectParam kStutterRate{
    .name = "stutterRate", .curve = dsp::fixed::ParamCurve::EXPONENTIAL, .neutral = 16777216};

/// @brief The compressor's threshold param: its word, used as stored, is non-negative.
inline constexpr EffectParam kCompressorThreshold{.name = "compressorThreshold",
                                                  .mapping = plugin::MappingKind::UNIPOLAR};

/// @brief The sidechain's shape param, Q31: the release's curve, used as stored.
inline constexpr EffectParam kSidechainShape{.name = "compressorShape"};

} // namespace deluge::effects::params
