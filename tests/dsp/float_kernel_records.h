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
inline constexpr uint32_t kSmootherExponential = 957903478U;
inline constexpr uint32_t kSvfLowPass = 3003083457U;
inline constexpr uint32_t kSvfNotch = 1315717876U;
inline constexpr uint32_t kSvfHighPass = 1567773475U;
#else
inline constexpr uint32_t kOnePoleLowPass = 1000713743U;
inline constexpr uint32_t kOnePoleHighPass = 1062954013U;
inline constexpr uint32_t kSmootherLinear = 1153755580U;
inline constexpr uint32_t kSmootherExponential = 1232577157U;
inline constexpr uint32_t kSvfLowPass = 857356867U;
inline constexpr uint32_t kSvfNotch = 3338462520U;
inline constexpr uint32_t kSvfHighPass = 4274204595U;
#endif
} // namespace records
