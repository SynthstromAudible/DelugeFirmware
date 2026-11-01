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

#include "dsp-next/fixed/curves/param_curve.h"
#include "plugin-api/types/param_mapping.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::effects {

/// @brief One of an effect's params as the effect defines it: how its stored word (Q31, a knob position) becomes the
/// value the effect renders with, and how a UI positions it. A param table binds an id, file name and default to it.
struct EffectParam {
	/// The effect's own name for it, the host chains' file name.
	std::string_view name;
	/// The curve its word goes through around @ref neutral; none when the effect uses the word as stored.
	std::optional<dsp::fixed::ParamCurve> curve{}; // NOLINT(readability-redundant-member-init): may be left out
	/// The curve's value at a centred word.
	int32_t neutral{0};
	plugin::MappingKind mapping{plugin::MappingKind::STANDARD};
	plugin::ParamFlags flags{}; // NOLINT(readability-redundant-member-init): a fragment may leave the flags out
};

/// @brief The value an effect renders @p param with when its word is @p word, its curve built around @p neutral.
/// @param param The param.
/// @param word The stored word, Q31.
/// @param neutral The value a centred word gives: the param's own neutral, or an owner's in its place.
/// @return The final value, in the curve's format; the word itself for a param used as stored.
[[nodiscard]] inline int32_t final_value(const EffectParam& param, int32_t word, int32_t neutral) {
	if (!param.curve) {
		return word;
	}
	const int32_t combination = dsp::fixed::preset_combination(word);
	switch (*param.curve) {
	case dsp::fixed::ParamCurve::SQUARE:
		return dsp::fixed::curve_square(neutral, combination);
	case dsp::fixed::ParamCurve::LINEAR:
		return dsp::fixed::curve_linear(neutral, combination);
	case dsp::fixed::ParamCurve::BIPOLAR:
		return dsp::fixed::curve_bipolar(neutral, combination);
	case dsp::fixed::ParamCurve::EXPONENTIAL:
		return dsp::fixed::curve_exponential(neutral, combination);
	case dsp::fixed::ParamCurve::EXPONENTIAL_INVERTED:
		return dsp::fixed::curve_exponential(neutral, -combination);
	case dsp::fixed::ParamCurve::RELEASE_TABLE:
		return multiply_32x32_rshift32(neutral, dsp::fixed::release_rate(combination));
	}
	return word;
}

/// @brief The value an effect renders @p param with when its word is @p word.
/// @param param The param.
/// @param word The stored word, Q31.
/// @return The final value, in the curve's format; the word itself for a param used as stored.
[[nodiscard]] inline int32_t final_value(const EffectParam& param, int32_t word) {
	return final_value(param, word, param.neutral);
}

} // namespace deluge::effects
