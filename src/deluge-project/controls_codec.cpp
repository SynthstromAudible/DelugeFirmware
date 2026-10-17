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

#include "deluge-project/controls_codec.h"

#include "serialization/tree.h"

#include <string>
#include <string_view>

namespace deluge::project {

using deluge::engine::ParamRef;
using serialization::Value;

namespace {

/// The number of mod buttons, each with two knobs.
constexpr uint8_t kModButtons = 8;

std::string_view text(const Value* node) {
	const std::string* t = serialization::text_of(node);
	return t != nullptr ? std::string_view(*t) : std::string_view{};
}

/// The param a knob element names: `controlsParam`, `patchAmountFromSource`, `patchAmountFromSecondSource`.
ParamRef ref_in(const Value& knob) {
	return {text(knob.find("controlsParam")), text(knob.find("patchAmountFromSource")),
	        text(knob.find("patchAmountFromSecondSource"))};
}

void set_ref(Value& knob, const ParamRef& ref) {
	knob.set("controlsParam", Value::makeScalar(std::string(ref.param)));
	if (!ref.source.empty()) {
		knob.set("patchAmountFromSource", Value::makeScalar(std::string(ref.source)));
	}
	if (!ref.second_source.empty()) {
		knob.set("patchAmountFromSecondSource", Value::makeScalar(std::string(ref.second_source)));
	}
}

} // namespace

std::vector<ControllerMapping> decode_controls(const Value& element, const deluge::engine::ParamNaming& naming) {
	std::vector<ControllerMapping> controls;
	if (!element.isMap()) {
		return controls;
	}
	if (const Value* modKnobs = element.find("modKnobs")) {
		uint8_t place = 0;
		for (const Value* knob : serialization::elements_named(*modKnobs, "modKnob")) {
			const auto button = static_cast<uint8_t>(place / 2);
			const auto which = static_cast<uint8_t>(place % 2);
			++place;
			if (button >= kModButtons || !knob->isMap()) {
				continue;
			}
			std::optional<deluge::engine::ParamId> target = naming.id_of(ref_in(*knob));
			if (!target) {
				continue;
			}
			if (naming.mod_knob_target != nullptr) {
				target = naming.mod_knob_target(*target);
			}
			controls.push_back({ModKnobControl{button, which}, *target});
		}
	}
	return controls;
}

void encode_controls(const std::vector<ControllerMapping>& controls, const deluge::engine::ParamNaming& naming,
                     Value& element) {
	Value modKnobs = Value::makeArray();
	for (const ControllerMapping& mapping : controls) {
		Value knob = Value::makeMap();
		// A mod knob's place is its position in the list: knobs before it that the model holds no target for
		// are written empty, which a reader passes over.
		const ModKnobControl& place = mapping.control;
		const size_t at = static_cast<size_t>(place.button) * 2 + place.knob;
		while (modKnobs.items().size() < at) {
			Value empty = Value::makeMap();
			empty.set("modKnob", Value::makeMap());
			modKnobs.append(std::move(empty));
		}
		set_ref(knob, naming.ref_of(mapping.target));
		Value box = Value::makeMap();
		box.set("modKnob", std::move(knob));
		modKnobs.append(std::move(box));
	}
	if (!modKnobs.items().empty()) {
		element.set("modKnobs", std::move(modKnobs));
	}
}

} // namespace deluge::project
