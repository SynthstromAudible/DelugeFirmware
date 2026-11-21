#pragma once

/// @file
/// @brief A song read from its XML text as a song file is read, for the specs that check what a load makes.

#include "integration/scenarios/drivers/tree_load.h"
#include "memory/general_memory_allocator.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include <optional>
#include <string_view>

namespace deluge::scenes::spec {

/// @brief Load @p xml into a new song, as a song file is read.
/// @param xml The song file's text.
/// @return The song, or null if the read failed.
inline Song* load_song(std::string_view xml) {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
	std::optional<deluge::storage::Value> tree = parseDocument(xml, deluge::storage::DocumentFormat::XML);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(deluge::storage::DocumentFormat::XML));
	Error error = openTreeFile(adapter, "song", deluge::storage::DocumentFormat::XML);
	if (error == Error::NONE) {
		error = deluge::bridge::loadSong(*song, adapter);
	}
	return error == Error::NONE ? song : nullptr;
}

/// @brief The first synth of @p song, playing or (with no clip) hibernating.
/// @param song The song.
/// @return The synth, or null if the song has none.
inline SoundInstrument* first_synth(Song& song) {
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* output = first; output != nullptr; output = output->next) {
			if (output->type == OutputType::SYNTH) {
				return static_cast<SoundInstrument*>(output);
			}
		}
	}
	return nullptr;
}

} // namespace deluge::scenes::spec
