#pragma once

/// @file
/// @brief What the mapping index plays of a song's learned knobs: the specs' view of a learned knob once the table
///        holds it, as incoming MIDI finds it.

#include "io/midi/mapping_index.h"
#include "io/midi/midi_device_manager.h"
#include "model/song/song.h"
#include "project/controller_mapping.h"
#include "project/mapping_table.h"

#include <cstdint>
#include <vector>

namespace deluge::ui::scenes::spec {

/// @brief The knobs of @p owner the index, rebuilt from @p song's table, plays for CC @p cc on @p channel (as the
///        table spells it) from any device: each with the cable it listens to and its relative flag, in table order.
/// @param song    The current song.
/// @param owner   Whose knobs.
/// @param channel The channel or zone the knobs were learned on.
/// @param cc      The CC number, or 128 for pitch bend.
/// @return The hits.
inline std::vector<midi::IndexedEntry> indexedKnobs(::Song& song, project::ControlsAddress owner, int32_t channel,
                                                    int32_t cc) {
	const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
	MIDIDeviceManager::differentiatingInputsByDevice = false; // a knob from any device is heard
	midi::rebuildMappingIndex(song);
	std::vector<midi::IndexedEntry> hits;
	midi::mappingIndex().forEachKnob(owner, MIDIDeviceManager::dinMIDIPorts, channel, cc,
	                                 [&](const midi::IndexedEntry& hit) { hits.push_back(hit); });
	MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
	return hits;
}

/// @brief Whether the index plays one knob of @p target's owner for CC @p cc on @p channel, and that one turns
///        @p target's param.
/// @param song    The current song.
/// @param target  The knob's owner and param.
/// @param channel The channel or zone the knob was learned on.
/// @param cc      The CC number, or 128 for pitch bend.
/// @return True if it does.
inline bool indexPlays(::Song& song, const project::OwnerParamTarget& target, int32_t channel, int32_t cc) {
	const std::vector<midi::IndexedEntry> hits = indexedKnobs(song, target.owner, channel, cc);
	return hits.size() == 1 && hits[0].target == project::MappingTarget{target};
}

} // namespace deluge::ui::scenes::spec
