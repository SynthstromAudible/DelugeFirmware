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

#include "io/midi/mapping_index.h"

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/song.h"

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace deluge::midi {

namespace {

using project::ControlsAddress;
using project::Mapping;

/// A kit row of a playing kit: the kit's id and the row's place in it.
struct Row {
	uint32_t drum;
	uint32_t kit;
	uint32_t ordinal;
};

/// What of a song can be offered MIDI: its playing outputs, their kits' rows, and its session clips.
struct Awake {
	std::vector<uint32_t> outputs;
	std::vector<Row> rows;
	std::vector<std::pair<uint32_t, uint32_t>> sessionClips; ///< clip id, session index

	explicit Awake(::Song& song) {
		for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
			if (!output->id()) {
				continue;
			}
			outputs.push_back(*output->id());
			if (output->type != OutputType::KIT) {
				continue;
			}
			uint32_t ordinal = 0;
			for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next, ordinal++) {
				if (drum->id()) {
					rows.push_back({*drum->id(), *output->id(), ordinal});
				}
			}
		}
		for (int32_t c = 0; c < std::ssize(song.live.clips.session_clips); c++) {
			sessionClips.emplace_back(song.live.clips.session_clips[c]->id(), static_cast<uint32_t>(c));
		}
	}

	[[nodiscard]] bool hasOutput(uint32_t id) const { return std::ranges::find(outputs, id) != outputs.end(); }

	[[nodiscard]] const Row* row(uint32_t drum) const {
		const auto it = std::ranges::find(rows, drum, &Row::drum);
		return it != rows.end() ? &*it : nullptr;
	}

	[[nodiscard]] std::optional<uint32_t> sessionIndex(uint32_t clip) const {
		const auto it = std::ranges::find(sessionClips, clip, &std::pair<uint32_t, uint32_t>::first);
		return it != sessionClips.end() ? std::optional{it->second} : std::nullopt;
	}

	/// Whether @p owner's knobs can be offered MIDI: the song's always, an output's or a row's while it plays.
	[[nodiscard]] bool hasOwner(const ControlsAddress& owner) const {
		switch (owner.kind) {
		case ControlsAddress::Kind::SONG:
			return true;
		case ControlsAddress::Kind::OUTPUT:
			return hasOutput(owner.id);
		case ControlsAddress::Kind::DRUM:
			return row(owner.id) != nullptr;
		}
		return false;
	}
};

/// The cable @p mapping's source listens to: see IndexedEntry::cable.
MIDICable* cableOf(const Mapping& mapping) {
	if (!mapping.source.device) {
		return nullptr;
	}
	MIDICable* cable = bridge::find_cable(*mapping.source.device);
	return cable != nullptr ? cable : MappingIndex::absentDevice();
}

} // namespace

MIDICable* MappingIndex::absentDevice() {
	// Only its address is used: it is never a cable MIDI arrives on.
	static constinit char tag = 0;
	return reinterpret_cast<MIDICable*>(&tag);
}

void MappingIndex::clear() {
	commands_.clear();
	knobs_.clear();
	lanes_.clear();
}

void MappingIndex::rebuild(const project::MappingTable& table, ::Song& song) {
	clear();
	const Awake awake{song};
	// Each owner's knobs in table order: the place of each in its owner's knob list.
	std::vector<std::pair<ControlsAddress, uint32_t>> knobCounts;

	for (const Mapping& mapping : table.entries()) {
		std::visit(
		    [&]<typename T>(const T& target) {
			    if constexpr (std::is_same_v<T, project::ParamTarget>) {
				    auto count =
				        std::ranges::find(knobCounts, target.owner, &std::pair<ControlsAddress, uint32_t>::first);
				    if (count == knobCounts.end()) {
					    count = knobCounts.emplace(knobCounts.end(), target.owner, 0u);
				    }
				    const uint32_t ordinal = count->second++;
				    if (mapping.source.contains_something() && awake.hasOwner(target.owner)) {
					    knobs_.push_back(
					        {mapping.source.channel_or_zone, target.owner, {&mapping, cableOf(mapping), ordinal}});
				    }
			    }
			    else if constexpr (std::is_same_v<T, project::SectionLaunchTarget>) {
				    if (mapping.source.contains_something() && target.section < kMaxNumSections) {
					    commands_.push_back({mapping.source.channel_or_zone,
					                         target.section,
					                         {&mapping, cableOf(mapping), target.section}});
				    }
			    }
			    else if constexpr (std::is_same_v<T, project::ClipMuteTarget>) {
				    const std::optional<uint32_t> index = awake.sessionIndex(target.clip_id);
				    if (mapping.source.contains_something() && index) {
					    // Clips after every section, the last in the session first.
					    const uint32_t rank = UINT32_MAX - *index;
					    commands_.push_back(
					        {mapping.source.channel_or_zone, rank, {&mapping, cableOf(mapping), *index}});
				    }
			    }
			    else if constexpr (std::is_same_v<T, project::DrumMuteTarget>
			                       || std::is_same_v<T, project::LaneInputTarget>) {
				    const Row* row = awake.row(target.drum_id);
				    if (mapping.source.contains_something() && row != nullptr) {
					    lanes_.push_back({row->kit,
					                      mapping.source.channel_or_zone,
					                      std::is_same_v<T, project::DrumMuteTarget>,
					                      {&mapping, cableOf(mapping), row->ordinal}});
				    }
			    }
			    else {
				    // A track's input is matched by its instrument, which acts on notes it does not match too.
				    static_assert(std::is_same_v<T, project::TrackInputTarget>);
			    }
		    },
		    mapping.target);
	}

	std::ranges::sort(commands_, {}, [](const Command& c) { return std::tuple{c.channel, c.rank}; });
	std::ranges::stable_sort(knobs_, {}, [](const Knob& k) {
		return std::tuple{int32_t{k.channel}, k.owner.kind, k.owner.id, k.entry.ordinal};
	});
	std::ranges::sort(lanes_, {}, [](const Lane& l) { return std::tuple{l.kit, l.channel, l.entry.ordinal, l.mute}; });
}

MappingIndex& mappingIndex() {
	static MappingIndex index;
	return index;
}

void rebuildMappingIndex(::Song& song) {
	if (&song != currentSong) {
		return;
	}
	mappingIndex().rebuild(song.project.mappings, song);
}

} // namespace deluge::midi
