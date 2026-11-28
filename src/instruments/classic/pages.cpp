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

#include "instruments/classic/pages.h"

#include "instruments/classic/engine_params.h"
#include "instruments/classic/state_access.h"

#include "instruments/classic/deluge_synth.h"

#include <optional>
#include <span>
#include <utility>

namespace deluge::instruments::classic {

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
		if (const SoundState* sound = ctx.state->as<DelugeSynth>()) {
			fm = synth_mode(*sound) == SynthMode::FM;
		}
	}
	return slot_for(param, {.fm = fm, .alt_delay = ctx.alt_delay});
}

} // namespace

KnobPairs remap_for_mode(SynthMode from, SynthMode to, KnobPairs knobs) {
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
		// Into or out of DX7 mode a knob on a param with a counterpart there turns the counterpart
		for (plugin::ParamId* knob : {&pair.lower, &pair.upper}) {
			if (const std::optional<plugin::ParamId> playing = playing_target(to, *knob)) {
				*knob = *playing;
			}
		}
	}
	return knobs;
}

namespace {

/// Mode 7's button: the one the wavetable claim reassigns.
constexpr size_t kWavetableButton = 7;

/// The depth of the first local LFO's cable to oscillator 1's wave index.
static_assert(cable_depth_id(PatchSource::LFO_LOCAL_1, param::kOscAWavetablePosition).has_value());
constexpr plugin::ParamId kLfoOnWaveA = *cable_depth_id(PatchSource::LFO_LOCAL_1, param::kOscAWavetablePosition);

[[nodiscard]] plugin::ParamId wave_index_of(uint8_t osc) {
	return osc == 0 ? param::kOscAWavetablePosition : param::kOscBWavetablePosition;
}

/// Whether @p knob is @p param, or the depth of a cable (of any depth level) whose param is @p param.
[[nodiscard]] bool targets_param(plugin::ParamId knob, plugin::ParamId param) {
	return is_cable_depth(knob) ? detail::root_param_field(knob.value) == param.value : knob == param;
}

} // namespace

KnobPairs wavetable_claimed(KnobPairs knobs, uint8_t osc) {
	KnobPair& pair = knobs[kWavetableButton];
	if (osc == 0) {
		pair.upper = param::kOscAWavetablePosition;
		if (pair.lower != param::kOscBWavetablePosition) {
			pair.lower = kLfoOnWaveA;
		}
	}
	else {
		pair.lower = param::kOscBWavetablePosition;
	}
	return knobs;
}

KnobPairs wavetable_released(KnobPairs knobs, uint8_t osc) {
	KnobPair& pair = knobs[kWavetableButton];
	const plugin::ParamId wave = wave_index_of(osc);
	if (targets_param(pair.lower, wave)) {
		pair.lower = param::kBitcrushAmount;
	}
	if (targets_param(pair.upper, wave)) {
		pair.upper = param::kSampleRateReduction;
	}
	return knobs;
}

const plugin::RemoteControls kRemoteControls{.plugin = plugin::kDelugeInstrument,
                                             .count = &count_pages,
                                             .get = &get_page,
                                             .find = &find_page,
                                             .decorate = &decorate_slot};

} // namespace deluge::instruments::classic
