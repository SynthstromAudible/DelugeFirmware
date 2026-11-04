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

#include "dsp-next/core/fixed_point.h"
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
	// The neutral read at Q31: its units are the param's, and the curves' arithmetic is the same for every format
	const auto typed_neutral = dsp::Q<31>::from_raw(neutral);
	const dsp::Q<29> combination = dsp::fixed::preset_combination(dsp::Q<31>::from_raw(word));
	switch (*param.curve) {
	case dsp::fixed::ParamCurve::SQUARE:
		return dsp::fixed::curve_square(typed_neutral, combination).raw();
	case dsp::fixed::ParamCurve::LINEAR:
		return dsp::fixed::curve_linear(typed_neutral, combination).raw();
	case dsp::fixed::ParamCurve::BIPOLAR:
		return dsp::fixed::curve_bipolar(typed_neutral, combination).raw();
	case dsp::fixed::ParamCurve::EXPONENTIAL:
		return dsp::fixed::curve_exponential(typed_neutral, combination).raw();
	case dsp::fixed::ParamCurve::EXPONENTIAL_INVERTED:
		// A stored word's combination is a quarter of the word, so its negation can't wrap
		return dsp::fixed::curve_exponential(typed_neutral, neg_wrap(combination)).raw();
	case dsp::fixed::ParamCurve::RELEASE_TABLE:
		// The neutral times the rate, taking the high word: the result is in the neutral's units, times rate / 2.
		return mul(typed_neutral, dsp::fixed::release_rate(combination)).raw();
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
