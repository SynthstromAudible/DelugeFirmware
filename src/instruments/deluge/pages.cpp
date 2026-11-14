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

#include "instruments/deluge/pages.h"

#include "instruments/deluge/plugin.h"

#include <optional>
#include <span>
#include <utility>

namespace deluge::instrument {

namespace {

uint32_t count_pages(const plugin::PageContext& /*ctx*/) {
	return static_cast<uint32_t>(kDevicePages.size());
}

bool get_page(const plugin::PageContext& /*ctx*/, uint32_t index, plugin::RemotePage& out) {
	if (index >= kDevicePages.size()) {
		return false;
	}
	out = kDevicePages[index];
	return true;
}

bool find_page(const plugin::PageContext& /*ctx*/, uint32_t section, uint32_t page_id, plugin::RemotePage& out) {
	const uint32_t cycle = kFilterCycle.size();
	if (page_id >= 2 * cycle) {
		return false;
	}
	out = kFilterCycle[page_id % cycle];
	out.section = section;
	if (page_id >= cycle) {
		out.page_id = page_id;
		out.group = 1;
		std::swap(out.slots[0], out.slots[1]);
	}
	return true;
}

plugin::RemoteSlot decorate_slot(const plugin::PageContext& ctx, plugin::ParamId param) {
	bool fm = false;
	if (ctx.state != nullptr) {
		if (const SoundState* sound = ctx.state->as<DelugeInstrument>()) {
			fm = sound->synth_mode == SynthMode::FM;
		}
	}
	return slot_for(param, {.fm = fm, .alt_delay = ctx.alt_delay});
}

} // namespace

std::array<KnobPair, 8> remap_for_mode(SynthMode from, SynthMode to, std::array<KnobPair, 8> knobs) {
	std::span<const KnobRemap> rows;
	if (to == SynthMode::FM && from != SynthMode::FM) {
		rows = kIntoFm;
	}
	else if (from == SynthMode::FM && to != SynthMode::FM) {
		rows = kOutOfFm;
	}
	for (KnobPair& pair : knobs) {
		const auto matches = [&pair](const KnobRemap& row) {
			return std::ranges::contains(row.lower_from, pair.lower)
			       && std::ranges::contains(row.upper_from, pair.upper);
		};
		if (const auto row = std::ranges::find_if(rows, matches); row != rows.end()) {
			pair = {.lower = row->lower_to, .upper = row->upper_to};
		}
	}
	return knobs;
}

const plugin::RemoteControls kRemoteControls{.plugin = plugin::kDelugeInstrument,
                                             .count = &count_pages,
                                             .get = &get_page,
                                             .find = &find_page,
                                             .decorate = &decorate_slot};

} // namespace deluge::instrument
