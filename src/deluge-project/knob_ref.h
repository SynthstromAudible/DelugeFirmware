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
/// @brief The reading and writing of a knob element's param that the controls and mapping codecs share, so a mod knob
/// and a learned MIDI knob name their param under the same keys. Internal to deluge-project.

#include "engine-api/types/param_naming.h"
#include "serialization/tree.h"
#include "serialization/value.h"

#include <string>
#include <string_view>

namespace deluge::project::detail {

/// @brief The text @p node holds, or empty when it is absent or not a scalar.
/// @param node The element's child, or nullptr.
/// @return The text.
inline std::string_view text(const serialization::Value* node) {
	const std::string* t = serialization::text_of(node);
	return t != nullptr ? std::string_view(*t) : std::string_view{};
}

/// @brief The param a knob element names: `controlsParam`, `patchAmountFromSource`, `patchAmountFromSecondSource`.
/// @param knob The knob element.
/// @return The param's reference.
inline deluge::engine::ParamRef ref_in(const serialization::Value& knob) {
	return {text(knob.find("controlsParam")), text(knob.find("patchAmountFromSource")),
	        text(knob.find("patchAmountFromSecondSource"))};
}

/// @brief Write @p ref into @p knob under the keys ref_in reads; the sources only when the param has them.
/// @param knob The knob element.
/// @param ref  The param's reference.
inline void set_ref(serialization::Value& knob, const deluge::engine::ParamRef& ref) {
	knob.set("controlsParam", serialization::Value::makeScalar(std::string(ref.param)));
	if (!ref.source.empty()) {
		knob.set("patchAmountFromSource", serialization::Value::makeScalar(std::string(ref.source)));
	}
	if (!ref.second_source.empty()) {
		knob.set("patchAmountFromSecondSource", serialization::Value::makeScalar(std::string(ref.second_source)));
	}
}

} // namespace deluge::project::detail
