/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "util/fixedpoint.h" // signed_saturate, multiply_32x32_rshift32
#include <cstdint>

// The parameter scaling/patching math, split out of the grab-bag util/functions.h so the synth core
// (the mod matrix) can reach it without dragging in functions.h's gui/colour include. functions.h re-exports
// this header, so its existing users are unaffected. The definitions live in param_functions.cpp.

// Per-param scaling tables, populated by functionsInit().
extern int32_t paramRanges[];
extern int32_t paramNeutralValues[];

int32_t getExp(int32_t presetValue, int32_t adjustment);
int32_t lookupReleaseRate(int32_t input);

/// @brief A cable's source value scaled as an exponential param's patching adds it in.
[[nodiscard]] inline int32_t cableToExpParamShortcut(int32_t sourceValue) {
	return sourceValue >> 2;
}

int32_t getFinalParameterValueVolume(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueLinear(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueHybrid(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueExp(int32_t paramNeutralValue, int32_t patchedValue);
int32_t getFinalParameterValueExpWithDumbEnvelopeHack(int32_t paramNeutralValue, int32_t patchedValue, int32_t p);
