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

#include "engines/deluge/cable_codec.h"

#include "engines/deluge/params.h"
#include "serialization/tree.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace deluge::instrument {

using serialization::Value;

engine::ParamId CableState::depth() const {
	return cable_depth_id(source, destination).value_or(engine::ParamId{});
}

namespace {

/// The destination a cable's `destination` names: a param, or unknown.
engine::ParamId destination_of(std::string_view name) {
	if (const engine::ParamInfo* row = engine::find_param(kParams, name)) {
		return row->id;
	}
	return kUnknownDestination;
}

std::string_view text(const Value* node) {
	const std::string* t = serialization::text_of(node);
	return t != nullptr ? std::string_view(*t) : std::string_view{};
}

/// Append the cables controlling an outer cable's depth (their destination is set once the outer cable is kept).
void decode_depth_controllers(const Value& depthControlledBy, size_t first, std::vector<CableState>& cables,
                              const AmountTest& contains_something) {
	for (const serialization::NamedChild& nested : serialization::children(depthControlledBy)) {
		if (cables.size() >= kMaxCables - 1) {
			break;
		}
		if (nested.name != "patchCable" || !nested.node->isMap()) {
			continue;
		}
		PatchSource source = PatchSource::NONE;
		const Value* amount = nullptr;
		Polarity polarity = Polarity::BIPOLAR;
		for (const auto& [key, value] : nested.node->entries()) {
			if (key == "source") {
				source = parse_patch_source(text(&value));
			}
			else if (key == "amount") {
				amount = &value;
			}
			else if (key == "polarity") {
				polarity = parse_polarity(text(&value));
			}
		}
		if (source == PatchSource::NONE || amount == nullptr || !contains_something(*amount)) {
			continue;
		}
		const bool repeated = std::any_of(cables.begin() + static_cast<std::ptrdiff_t>(first), cables.end(),
		                                  [&](const CableState& kept) { return kept.source == source; });
		if (!repeated) {
			cables.push_back({source, engine::ParamId{}, polarity, amount});
		}
	}
}

} // namespace

std::vector<CableState> decode_cables(const Value& patchCables, const AmountTest& contains_something) {
	std::vector<CableState> cables;
	for (const serialization::NamedChild& entry : serialization::children(patchCables)) {
		if (cables.size() >= kMaxCables) {
			break;
		}
		if (entry.name != "patchCable" || !entry.node->isMap()) {
			continue;
		}
		const size_t first = cables.size();
		PatchSource source = PatchSource::NONE;
		std::optional<engine::ParamId> destination;
		const Value* amount = nullptr;
		Polarity polarity = Polarity::BIPOLAR;
		for (const auto& [key, value] : entry.node->entries()) {
			if (key == "source") {
				source = parse_patch_source(text(&value));
				if (source == PatchSource::AFTERTOUCH) {
					polarity = Polarity::UNIPOLAR;
				}
			}
			else if (key == "destination") {
				destination = destination_of(text(&value));
			}
			else if (key == "amount") {
				amount = &value;
			}
			else if (key == "polarity") {
				polarity = parse_polarity(text(&value));
			}
			else if (key == "depthControlledBy") {
				decode_depth_controllers(value, first, cables, contains_something);
			}
		}
		const auto earlier = cables.begin() + static_cast<std::ptrdiff_t>(first);
		const bool keep = source != PatchSource::NONE && destination.has_value() && amount != nullptr
		                  && contains_something(*amount) && !(source == PatchSource::X && *destination == param::kPitch)
		                  && std::none_of(cables.begin(), earlier, [&](const CableState& kept) {
			                     return kept.source == source && kept.destination == *destination;
		                     });
		if (!keep) {
			cables.resize(first);
			continue;
		}
		const engine::ParamId cableDepth = cable_depth_id(source, *destination).value_or(engine::ParamId{});
		for (auto it = earlier; it != cables.end(); ++it) {
			it->destination = cableDepth;
		}
		cables.push_back({source, *destination, polarity, amount});
	}
	return cables;
}

} // namespace deluge::instrument
