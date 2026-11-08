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

// The float pack's spec, built with -mfma so that kFusedMulAdd holds on the x86 host: the fused policy, which an
// AArch64 or VFPv4 target runs, held to the same lane-for-lane equalities (tests/dsp/CMakeLists.txt sets the flag).
#include "pack_f32_spec.cpp" // NOLINT(bugprone-suspicious-include)
