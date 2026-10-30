/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "instruments/deluge/mod_matrix.h"

namespace deluge::instrument {

std::optional<CableDelta> begin_add(const CableRecords& cables, CableRecord cable) {
	if (cable.target.is_null() || cables.size() >= synth::kMaxCables
	    || cables.find(cable.source, cable.target).has_value()) {
		return std::nullopt;
	}
	return CableDelta{.op = CableDelta::Op::ADD, .after = cable, .records_before = cables};
}

std::optional<CableDelta> begin_remove(const CableRecords& cables, size_t index) {
	if (index >= cables.size()) {
		return std::nullopt;
	}
	return CableDelta{.op = CableDelta::Op::REMOVE,
	                  .index = static_cast<uint8_t>(index),
	                  .before = cables[index],
	                  .records_before = cables};
}

std::optional<CableDelta> begin_set_polarity(const CableRecords& cables, size_t index, CablePolarity polarity) {
	if (index >= cables.size()) {
		return std::nullopt;
	}
	return CableDelta{.op = CableDelta::Op::SET_POLARITY,
	                  .index = static_cast<uint8_t>(index),
	                  .before = cables[index],
	                  .after = {cables[index].source, cables[index].target, polarity},
	                  .records_before = cables};
}

std::optional<CableDelta> begin_reroute(const CableRecords& cables, size_t index, CableTarget target) {
	if (index >= cables.size() || target.is_null() || cables.find(cables[index].source, target).has_value()) {
		return std::nullopt;
	}
	return CableDelta{.op = CableDelta::Op::REROUTE,
	                  .index = static_cast<uint8_t>(index),
	                  .before = cables[index],
	                  .after = {cables[index].source, target, cables[index].polarity},
	                  .records_before = cables};
}

bool needs_cable(const CableRecords& cables, PatchSource source, CableTarget target) {
	return !target.is_null() && !cables.find(source, target).has_value();
}

bool settles(bool depth_holds_something, bool clip_active) {
	return clip_active && !depth_holds_something;
}

} // namespace deluge::instrument
