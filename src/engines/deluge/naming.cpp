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

#include "engines/deluge/naming.h"

#include "engines/deluge/cables.h"
#include "engines/deluge/params.h"

#include <algorithm>
#include <array>

namespace deluge::instrument {

namespace {

std::optional<plugin::ParamId> id_of(const plugin::ParamRef& ref) {
	const plugin::ParamInfo* row = plugin::find_param(kParams, ref.param);
	if (row == nullptr) {
		return std::nullopt;
	}
	std::optional<plugin::ParamId> id = row->id;
	if (!ref.source.empty()) {
		id = cable_depth_id(parse_patch_source(ref.source), *id);
	}
	if (id && !ref.second_source.empty()) {
		id = cable_depth_id(parse_patch_source(ref.second_source), *id);
	}
	return id;
}

plugin::ParamRef ref_of(plugin::ParamId id) {
	std::array<PatchSource, 3> sources{};
	size_t n = 0;
	plugin::ParamId at = id;
	while (std::optional<CableRoute> route = cable_route(at)) {
		sources[n++] = route->source;
		at = route->destination;
	}
	const plugin::ParamInfo* row = plugin::find_param(kParams, at);
	if (row == nullptr || n > 2) {
		return {};
	}
	// The route unwinds from the outermost source in; a ref names the source nearest the param first.
	std::reverse(sources.begin(), sources.begin() + static_cast<std::ptrdiff_t>(n));
	return {row->file_name, n > 0 ? to_string(sources[0]) : std::string_view{},
	        n > 1 ? to_string(sources[1]) : std::string_view{}};
}

plugin::ParamId mod_knob_target(plugin::ParamId id) {
	const bool depth = is_cable_depth(id);
	const uint32_t p = depth ? (id.value & 0xFFu) : id.value;
	if (p != param::kVolumePostReverbSend.value && p != param::kVolumePostFX.value && p != param::kVolume.value) {
		return id;
	}
	if (!depth) {
		return param::kVolumePostFX;
	}
	// The source of the cable nearest the param sits in the first source field, as its value + 1.
	const auto nearest = static_cast<PatchSource>(((id.value >> 8) & 0x3Fu) - 1);
	const uint32_t volume =
	    nearest == PatchSource::SIDECHAIN ? param::kVolumePostReverbSend.value : param::kVolume.value;
	return plugin::ParamId{(id.value & ~0xFFu) | volume};
}

} // namespace

const plugin::ParamNaming kNaming{plugin::kDelugeInstrument, &id_of, &ref_of, &mod_knob_target};

} // namespace deluge::instrument
