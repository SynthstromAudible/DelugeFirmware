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

// Reference copies of the q31 word↔position arithmetic, kept verbatim as the oracle for plugin-api's mapping kinds:
// knob: ParamCollection::paramValueToKnobPos/knobPosToParamValue (standard), the UnpatchedParamSet (compressor
// threshold) and PatchedParamSet (phase widths) overrides (unipolar), the PatchCableSet overrides (cable);
// menu: value_scaling's standard, half-precision and pan scalings, PatchCableStrength's read and write (cable, 5000
// hundredths at a depth of 1); preset: getParamFromUserValue's param case; popup: View::calculateKnobPosForDisplay for
// a non-MIDI param, its pan-or-pitch test as @p centred.
#pragma once
#include <cmath>
#include <cstdint>

namespace deluge::plugin::test::reference {

inline int32_t standard_to_knob(int32_t v) {
	if (v >= (int32_t)(0x80000000 - (1 << 24))) {
		return 64;
	}
	return (v + (1 << 24)) >> 25;
}
inline int32_t standard_from_knob(int32_t k) {
	int32_t v = 2147483647;
	if (k < 64) {
		v = k << 25;
	}
	return v;
}
inline int32_t unipolar_to_knob(int32_t v) {
	return (v >> 24) - 64;
}
inline int32_t unipolar_from_knob(int32_t k) {
	int32_t v = 2147483647;
	if (k < 64) {
		v = (k + 64) << 24;
	}
	return v;
}
inline int32_t cable_to_knob(int32_t v) {
	return (v >> 23) - 64;
}
inline int32_t cable_from_knob(int32_t k) {
	int32_t v = 1073741824;
	if (k < 64) {
		v = (k + 64) << 23;
	}
	return v;
}
inline int32_t standard_to_menu(int32_t v) {
	return (((int64_t)v + 2147483648) * 50 + 2147483648) >> 32;
}
inline int32_t standard_from_menu(int32_t m) {
	if (m == 50) {
		return 2147483647;
	}
	if (m == 0) {
		return -2147483648;
	}
	return (uint32_t)m * 85899345 - 2147483648;
}
inline int32_t half_to_menu(int32_t v) {
	return ((int64_t)v * (50 * 2) + 2147483648) >> 32;
}
inline int32_t half_from_menu(int32_t m) {
	if (m == 50) {
		return 2147483647;
	}
	if (m == 0) {
		return 0;
	}
	return (uint32_t)m * (2147483648 / 25) >> 1;
}
inline int32_t pan_to_menu(int32_t v) {
	return ((int64_t)v * (25 * 2) + 2147483648) >> 32;
}
inline int32_t pan_from_menu(int32_t m) {
	if (m == 25) {
		return 2147483647;
	}
	if (m == -25) {
		return -2147483648;
	}
	return ((int32_t)m * (2147483648 / (25 * 2)) * 2);
}
inline int32_t cable_to_menu(int32_t v) {
	return ((int64_t)v * 5000 + (1 << 29)) >> 30;
}
inline int32_t cable_from_menu(int32_t m) {
	int64_t magicConstant = (922337203685477 * 5000) / 5000;
	return (magicConstant * m) >> 32;
}
inline int32_t preset_word(int8_t u) {
	return (uint32_t)u * 85899345 - 2147483648;
}
inline int32_t knob_pos_for_display(bool centred, int32_t knobPos) {
	float knobPosFloat = static_cast<float>(knobPos);
	float maxKnobPosFloat = static_cast<float>(128);
	float maxMenuValueFloat = static_cast<float>(50);
	float maxMenuRelativeValueFloat = static_cast<float>(25);
	float valueForDisplayFloat;
	valueForDisplayFloat = (knobPosFloat / maxKnobPosFloat) * maxMenuValueFloat;
	if (centred) {
		valueForDisplayFloat = valueForDisplayFloat - maxMenuRelativeValueFloat;
	}
	return static_cast<int32_t>(std::round(valueForDisplayFloat));
}

} // namespace deluge::plugin::test::reference
