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

#include "project/controls_codec.h"

#include "arp/params.h"
#include "project/knob_ref.h"
#include "serialization/tree.h"

#include <string>
#include <string_view>

namespace deluge::project {

using serialization::Value;

namespace {

using detail::ref_in;
using detail::set_ref;

/// The number of mod buttons, each with two knobs.
constexpr uint8_t kModButtons = 8;

} // namespace

std::vector<ControllerMapping> decode_controls(const Value& element, const deluge::plugin::ParamNaming& naming) {
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
			const deluge::plugin::ParamRef ref = ref_in(*knob);
			// The plugin's param of the name, else the arp's (a plain name the plugin does not take)
			if (std::optional<deluge::plugin::ParamId> id = naming.id_of(ref)) {
				if (naming.mod_knob_target != nullptr) {
					id = naming.mod_knob_target(*id);
				}
				controls.push_back({ModKnobControl{button, which}, engine_target(id->value)});
			}
			else if (const std::optional<deluge::plugin::ParamId> arp =
			             ref.source.empty() ? deluge::arp::find_by_file_name(ref.param) : std::nullopt) {
				controls.push_back({ModKnobControl{button, which}, ParamTarget{.space = ParamSpace::ARP, .id = *arp}});
			}
		}
	}
	return controls;
}

void encode_controls(const std::vector<ControllerMapping>& controls, const deluge::plugin::ParamNaming& naming,
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
		set_ref(knob, mapping.target.space == ParamSpace::ARP
		                  ? deluge::plugin::ParamRef{.param = deluge::arp::row(mapping.target.id).info.file_name}
		                  : naming.ref_of(mapping.target.id));
		Value box = Value::makeMap();
		box.set("modKnob", std::move(knob));
		modKnobs.append(std::move(box));
	}
	if (!modKnobs.items().empty()) {
		element.set("modKnobs", std::move(modKnobs));
	}
}

} // namespace deluge::project
