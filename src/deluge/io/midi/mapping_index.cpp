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

#include "libdeluge/system.h"

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/song.h"

#include <algorithm>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace deluge::midi {

namespace {

using project::ControlsAddress;
using project::Mapping;

using Row = MappingSnapshot::Row;

/// The lookups a rebuild makes into a snapshot's lists, sorted by id so each is a binary search.
struct Awake {
	const MappingSnapshot& snapshot;

	explicit Awake(MappingSnapshot& from) : snapshot{from} {
		std::ranges::sort(from.outputs);
		std::ranges::sort(from.rows, {}, &Row::drum);
		std::ranges::sort(from.sessionClips, {}, &std::pair<uint32_t, uint32_t>::first);
	}

	[[nodiscard]] bool hasOutput(uint32_t id) const { return std::ranges::binary_search(snapshot.outputs, id); }

	[[nodiscard]] const Row* row(uint32_t drum) const {
		const auto it = std::ranges::lower_bound(snapshot.rows, drum, {}, &Row::drum);
		return it != snapshot.rows.end() && it->drum == drum ? &*it : nullptr;
	}

	[[nodiscard]] std::optional<uint32_t> sessionIndex(uint32_t clip) const {
		const auto it =
		    std::ranges::lower_bound(snapshot.sessionClips, clip, {}, &std::pair<uint32_t, uint32_t>::first);
		return it != snapshot.sessionClips.end() && it->first == clip ? std::optional{it->second} : std::nullopt;
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

/// @p mapping as the index holds it, at @p ordinal.
IndexedEntry entryOf(const Mapping& mapping, uint32_t ordinal) {
	return {.target = mapping.target,
	        .note_or_cc = mapping.source.note_or_cc,
	        .relative = mapping.relative,
	        .cable = cableOf(mapping),
	        .ordinal = ordinal};
}

} // namespace

MappingSnapshot MappingSnapshot::of(const project::MappingTable& table, ::Song& song) {
	MappingSnapshot snapshot;
	snapshot.entries.assign(table.entries().begin(), table.entries().end());
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (!output->id()) {
			continue;
		}
		snapshot.outputs.push_back(*output->id());
		if (output->type != OutputType::KIT) {
			continue;
		}
		uint32_t ordinal = 0;
		for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next, ordinal++) {
			if (drum->id()) {
				snapshot.rows.push_back({*drum->id(), *output->id(), ordinal});
			}
		}
	}
	for (int32_t c = 0; c < std::ssize(song.live.clips.session_clips); c++) {
		snapshot.sessionClips.emplace_back(song.live.clips.session_clips[c]->id(), static_cast<uint32_t>(c));
	}
	return snapshot;
}

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

void MappingIndex::rebuild(MappingSnapshot snapshot) {
	clear();
	const Awake awake{snapshot};
	// Each owner's knobs in table order: the place of each in its owner's knob list.
	std::map<std::pair<ControlsAddress::Kind, uint32_t>, uint32_t> knobCounts;

	for (const Mapping& mapping : snapshot.entries) {
		std::visit(
		    [&]<typename T>(const T& target) {
			    if constexpr (std::is_same_v<T, project::ParamTarget>) {
				    const uint32_t ordinal = knobCounts[{target.owner.kind, target.owner.id}]++;
				    if (mapping.source.contains_something() && awake.hasOwner(target.owner)) {
					    knobs_.push_back({mapping.source.channel_or_zone, target.owner, entryOf(mapping, ordinal)});
				    }
			    }
			    else if constexpr (std::is_same_v<T, project::SectionLaunchTarget>) {
				    if (mapping.source.contains_something() && target.section < kMaxNumSections) {
					    commands_.push_back(
					        {mapping.source.channel_or_zone, target.section, entryOf(mapping, target.section)});
				    }
			    }
			    else if constexpr (std::is_same_v<T, project::ClipMuteTarget>) {
				    const std::optional<uint32_t> index = awake.sessionIndex(target.clip_id);
				    if (mapping.source.contains_something() && index) {
					    // Clips after every section, the last in the session first.
					    const uint32_t rank = UINT32_MAX - *index;
					    commands_.push_back({mapping.source.channel_or_zone, rank, entryOf(mapping, *index)});
				    }
			    }
			    else if constexpr (std::is_same_v<T, project::DrumMuteTarget>
			                       || std::is_same_v<T, project::LaneInputTarget>) {
				    const Row* row = awake.row(target.drum_id);
				    if (mapping.source.contains_something() && row != nullptr) {
					    lanes_.push_back({row->kit, mapping.source.channel_or_zone,
					                      std::is_same_v<T, project::DrumMuteTarget>, entryOf(mapping, row->ordinal)});
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
	MappingSnapshot snapshot;
	{
		// The song's tick (on the audio tier) can splice the lists the snapshot copies: an auto-overdub's output, say.
		// Only the copy holds the render off; the index is built from it after.
		AudioExcludedSection section;
		// Cleared before the copy, so a change marked after it is not lost.
		mapping_index_detail::mapping_index_stale.store(false, std::memory_order_release);
		snapshot = MappingSnapshot::of(song.project.mappings, song);
	}
	MappingIndex built;
	built.rebuild(std::move(snapshot));
	// The index's only reader, the dispatch of an incoming message, runs on this tier.
	mappingIndex() = std::move(built);
}

void refreshMappingIndex() {
	if (!mappingIndexStale() || currentSong == nullptr) {
		return; // with no song, the marks wait for the next one
	}
	bridge::applyPendingOutputClones(*currentSong);
	if (mapping_index_detail::mapping_devices_changed.load(std::memory_order_acquire)) {
		bridge::projectDeviceMappings(*currentSong);
		// Cleared after: the entries the projection itself made mark the devices changed, and it has played them. Every
		// other device mark is made on this tier too, so none lands meanwhile.
		mapping_index_detail::mapping_devices_changed.store(false, std::memory_order_release);
	}
	rebuildMappingIndex(*currentSong);
}

} // namespace deluge::midi
