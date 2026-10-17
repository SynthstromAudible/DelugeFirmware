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

#include "deluge-project/mapping_table.h"

#include <algorithm>

namespace deluge::project {

namespace {

constexpr uint8_t kPitchBend = 128;

bool is_output_param(const MappingTarget& target, uint32_t output_id) {
	const auto* param = std::get_if<ParamTarget>(&target);
	return param != nullptr && param->owner.kind == ControlsAddress::Kind::OUTPUT && param->owner.id == output_id;
}

bool belongs_to_output(const MappingTarget& target, uint32_t output_id) {
	const auto* input = std::get_if<TrackInputTarget>(&target);
	return is_output_param(target, output_id) || (input != nullptr && input->output_id == output_id);
}

bool belongs_to_drum(const MappingTarget& target, uint32_t drum_id) {
	if (const auto* param = std::get_if<ParamTarget>(&target)) {
		return param->owner.kind == ControlsAddress::Kind::DRUM && param->owner.id == drum_id;
	}
	if (const auto* mute = std::get_if<DrumMuteTarget>(&target)) {
		return mute->drum_id == drum_id;
	}
	if (const auto* lane = std::get_if<LaneInputTarget>(&target)) {
		return lane->drum_id == drum_id;
	}
	return false;
}

bool is_param_of(const MappingTarget& target, ControlsAddress owner) {
	const auto* param = std::get_if<ParamTarget>(&target);
	return param != nullptr && param->owner == owner;
}

} // namespace

Mapping& MappingTable::learn(Mapping mapping) {
	const bool is_param = std::holds_alternative<ParamTarget>(mapping.target);
	if (is_param) {
		mapping.relative = mapping.source.note_or_cc != kPitchBend;
	}
	auto same = std::ranges::find_if(entries_, [&](const Mapping& entry) {
		return entry.target == mapping.target && (!is_param || entry.source == mapping.source);
	});
	if (same != entries_.end()) {
		*same = std::move(mapping);
		return *same;
	}
	return entries_.emplace_back(std::move(mapping));
}

size_t MappingTable::unlearn(const MappingTarget& target) {
	return std::erase_if(entries_, [&](const Mapping& entry) { return entry.target == target; });
}

const Mapping* MappingTable::find(const MappingTarget& target) const {
	auto found = std::ranges::find_if(entries_, [&](const Mapping& entry) { return entry.target == target; });
	return found == entries_.end() ? nullptr : &*found;
}

void MappingTable::retarget_output(uint32_t from, uint32_t to) {
	if (from == to) {
		return;
	}
	forget_output(to);
	for (Mapping& entry : entries_) {
		if (is_output_param(entry.target, from)) {
			std::get<ParamTarget>(entry.target).owner.id = to;
		}
		else if (auto* input = std::get_if<TrackInputTarget>(&entry.target);
		         input != nullptr && input->output_id == from) {
			input->output_id = to;
		}
	}
}

void MappingTable::copy_params(uint32_t from, uint32_t to) {
	if (from == to) {
		return;
	}
	std::erase_if(entries_, [&](const Mapping& entry) { return is_output_param(entry.target, to); });
	const size_t count = entries_.size();
	for (size_t i = 0; i < count; i++) {
		if (is_output_param(entries_[i].target, from)) {
			Mapping copy = entries_[i];
			std::get<ParamTarget>(copy.target).owner.id = to;
			entries_.push_back(std::move(copy));
		}
	}
}

void MappingTable::copy_clip_mute(uint32_t from, uint32_t to) {
	if (from == to) {
		return;
	}
	const Mapping* source = find(ClipMuteTarget{from});
	if (source == nullptr) {
		unlearn(ClipMuteTarget{to});
		return;
	}
	Mapping copy = *source;
	copy.target = ClipMuteTarget{to};
	learn(std::move(copy));
}

void MappingTable::set_params(ControlsAddress owner, std::span<const Mapping> knobs) {
	const auto first =
	    std::ranges::find_if(entries_, [&](const Mapping& entry) { return is_param_of(entry.target, owner); });
	const auto at = static_cast<size_t>(first - entries_.begin());
	std::erase_if(entries_, [&](const Mapping& entry) { return is_param_of(entry.target, owner); });
	entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(at), knobs.begin(), knobs.end());
}

std::vector<Mapping> MappingTable::params_of(ControlsAddress owner) const {
	std::vector<Mapping> knobs;
	for (const Mapping& entry : entries_) {
		if (is_param_of(entry.target, owner)) {
			knobs.push_back(entry);
		}
	}
	return knobs;
}

void MappingTable::forget_clip(uint32_t clip_id) {
	unlearn(ClipMuteTarget{clip_id});
}

void MappingTable::forget_drum(uint32_t drum_id) {
	std::erase_if(entries_, [&](const Mapping& entry) { return belongs_to_drum(entry.target, drum_id); });
}

void MappingTable::forget_output(uint32_t output_id) {
	std::erase_if(entries_, [&](const Mapping& entry) { return belongs_to_output(entry.target, output_id); });
}

} // namespace deluge::project
