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

#include "modulation/params/param_set.h" // PatchedParamSet (for the inlined getValue in presetValue)
#include <cstdint>

class PatchCableSet;

/// The per-block inputs the Patcher reads from ParamManager-world, snapshotted into a small value so the
/// Patcher needs no `Sound&` / `ParamManager&` and references only `modulation/` types.
///
/// It carries the two things the patcher actually consumes: each patched param's current preset value
/// (already automation/MPE/expression-folded by the time patching runs) plus a one-param value de-zipper
/// override, and the patch-cable set (destinations, cables, strengths). Built once per patching call by
/// `Sound::patchControlBlock` and passed by value.
struct ControlBlock {
	PatchedParamSet* patched_params; ///< getValue(p) — the current (already-folded) preset value of param p.
	PatchCableSet* patch_cable_set;  ///< destinations / patchCables / strengths / sourcesPatchedToAnything.

	int32_t smoothing_param; ///< Sound::paramLPF.p (PARAM_LPF_OFF == -1 means nothing is being smoothed).
	int32_t smoothing_value; ///< Sound::paramLPF.currentValue (the smoothed value for smoothing_param).

	/// The patched param's smoothed preset value — bit-identical to `Sound::getSmoothedPatchedParamValue`.
	int32_t presetValue(int32_t p) const {
		return (p == smoothing_param) ? smoothing_value : patched_params->getValue(p);
	}
};
