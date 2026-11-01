// The runtime's word↔position arithmetic as it read at 3b854650f, copied verbatim as the oracle for plugin-api's
// mapping kinds. Sources:
// knob: modulation/params/param_collection.cpp ParamCollection::paramValueToKnobPos/knobPosToParamValue (standard),
// modulation/params/param_set.cpp UnpatchedParamSet (compressor threshold) and PatchedParamSet (phase widths)
// overrides (unipolar), modulation/patch/patch_cable_set.cpp PatchCableSet overrides (cable);
// menu: gui/menu_item/value_scaling.cpp (standard, half precision, pan) and gui/menu_item/patch_cable_strength.cpp
// PatchCableStrength::readCurrentValue/writeCurrentValue (cable, kMaxMenuPatchCableValue = 5000);
// preset: util/functions.cpp getParamFromUserValue's default case; popup: gui/views/view.cpp
// View::calculateKnobPosForDisplay for a non-MIDI param, its pan-or-pitch test as @p centred.
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
