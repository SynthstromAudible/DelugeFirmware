#pragma once

/// @file
/// @brief A preset: one output as a song holds it, its drums, and the mappings that belong to it.

#include "project/document.h"
#include "serialization/schema/extras.h"

#include <cstdint>
#include <map>
#include <vector>

namespace deluge::project {

/// @brief One output as a song holds it (a synth, a kit, a MIDI or CV output, an audio track), with a kit's drums and
/// the learned bindings whose targets are its own.
struct PresetDocument {
	std::vector<SongDocument::OutputEntry> output_order; ///< the one output's entry
	std::map<uint32_t, DocOutput> outputs;
	std::map<uint32_t, DocDrum> drums;
	MappingTable mappings;
	/// Keys a document held here that this firmware does not know, written back unchanged.
	serialization::schema::Extras extras;
	bool operator==(const PresetDocument&) const = default;
};

} // namespace deluge::project
