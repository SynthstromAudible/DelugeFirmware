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

#include <cstdint>

/// @file
/// @brief The float kernels' output digests (FNV-1a over the output bits), per fusion policy: the unfused record
/// holds on the x86 host and on the A9 under qemu; the fused record on the x86 host built with -mfma.
namespace records {
#if defined(__ARM_FEATURE_FMA) || defined(__FMA__)
inline constexpr uint32_t kOnePoleLowPass = 1000713743U;
inline constexpr uint32_t kOnePoleHighPass = 1062954013U;
inline constexpr uint32_t kSmootherLinear = 2508712197U;
inline constexpr uint32_t kSmootherExponential = 539644771U;
inline constexpr uint32_t kSvfLowPass = 2930967566U;
inline constexpr uint32_t kSvfNotch = 26242838U;
inline constexpr uint32_t kSvfHighPass = 595765005U;
inline constexpr uint32_t kBlepSaw = 262873454U;
inline constexpr uint32_t kBlepPulse = 2717416499U;
inline constexpr uint32_t kTableSaw = 4229038149U;
inline constexpr uint32_t kEnvAdsr = 691627950U;
inline constexpr uint32_t kEnvFourSegment = 4163814850U;
inline constexpr uint32_t kBlepVoice = 12489596U;
inline constexpr uint32_t kTableVoice = 3142855665U;
inline constexpr uint32_t kLadderLp24Clean = 1272964085U;
inline constexpr uint32_t kLadderLp24Drive = 3868079174U;
inline constexpr uint32_t kLadderMorphHpBp = 3217917105U;
inline constexpr uint32_t kLadderVoice = 4122284882U;
#else
inline constexpr uint32_t kBlepSaw = 262873454U;
inline constexpr uint32_t kBlepPulse = 2717416499U;
inline constexpr uint32_t kTableSaw = 811564572U;
inline constexpr uint32_t kEnvAdsr = 2959455807U;
inline constexpr uint32_t kEnvFourSegment = 4163814850U;
inline constexpr uint32_t kBlepVoice = 2440633774U;
inline constexpr uint32_t kTableVoice = 274084809U;
inline constexpr uint32_t kLadderLp24Clean = 1959320587U;
inline constexpr uint32_t kLadderLp24Drive = 616290635U;
inline constexpr uint32_t kLadderMorphHpBp = 1346162305U;
inline constexpr uint32_t kLadderVoice = 1116247649U;
inline constexpr uint32_t kOnePoleLowPass = 1000713743U;
inline constexpr uint32_t kOnePoleHighPass = 1062954013U;
inline constexpr uint32_t kSmootherLinear = 1153755580U;
inline constexpr uint32_t kSmootherExponential = 3365775788U;
inline constexpr uint32_t kSvfLowPass = 1703463923U;
inline constexpr uint32_t kSvfNotch = 3138167853U;
inline constexpr uint32_t kSvfHighPass = 3402081241U;
#endif
/// The PolyBLEP saw's aliased energy (the 0.39 fs bin of a 0.09 fs saw) over its fundamental's: measured (4.4e-4),
/// rounded up to one significant digit.
inline constexpr float kBlepAliasBound = 5e-4f;
} // namespace records
