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

// A loaded song's param sets and heaps, run as the heap_footprint scenario: it loads the song DRV_ARGS names, as a song
// file is read, then counts the param managers it holds (its clips', their note rows', the song's own and the parked
// backups) with their unpatched and arp sets, and prints both heaps' bytes the load took. A measurement, not a check:
// it returns 0 whenever the song loads.
//
// Gate heap-footprint:
//     nothing: it prints a loaded song's param managers, unpatched and arp sets, each set's size and the SRAM and
//     SDRAM heaps' bytes the load took

#include "deluge_base_alloc.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/arp_param_set.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using deluge::scenarios::driver::Io;

/// The free bytes of the SRAM and the SDRAM heaps.
struct HeapFree {
	size_t sram = 0;
	size_t sdram = 0;
};

HeapFree heap_free() {
	HeapFree out;
	size_t pool = 0;
	size_t min_free = 0;
	(void)deluge_heap_stats(deluge::memory::sram_heap(), &pool, &out.sram, &min_free);
	(void)deluge_heap_stats(deluge::memory::sdram_heap(), &pool, &out.sdram, &min_free);
	return out;
}

/// The param managers and sets a song holds.
struct Counts {
	size_t managers = 0;
	size_t unpatched = 0;
	size_t arp = 0;

	void add(ParamManager& manager) {
		managers++;
		unpatched += manager.getUnpatchedParamSet() != nullptr ? 1 : 0;
		arp += manager.getArpParamSet() != nullptr ? 1 : 0;
	}
};

/// Every param manager of @p song: its clips' and their note rows', the song's own and the parked backups.
Counts count(Song& song) {
	Counts counts;
	counts.add(song.paramManager);
	for (ClipArray* clips : {&song.live.clips.session_clips, &song.live.clips.arrangement_only_clips}) {
		for (size_t i = 0; i < clips->size(); i++) {
			Clip* clip = (*clips)[i];
			counts.add(clip->paramManager);
			if (clip->type == ClipType::INSTRUMENT) {
				for (NoteRow& row : static_cast<InstrumentClip*>(clip)->noteRows) {
					if (row.paramManager.containsAnyParamCollectionsIncludingExpression()) {
						counts.add(row.paramManager);
					}
				}
			}
		}
	}
	for (auto& [key, manager] : song.live.param_backup.map) {
		counts.add(manager);
	}
	return counts;
}

/// The song file at @p path, as its bytes, or nullopt where it cannot be read.
std::optional<std::string> file_text(const char* path) {
	FILE* f = std::fopen(path, "rb");
	if (f == nullptr) {
		return std::nullopt;
	}
	std::string text;
	char buffer[65536];
	size_t n = 0;
	while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
		text.append(buffer, n);
	}
	std::fclose(f);
	return text;
}

int heap_footprint_main(int argc, char** argv, Io io) {
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song file>\n", argv[0]);
		return 2;
	}
	const char* path = argv[1];
	std::optional<std::string> text = file_text(path);
	if (!text) {
		std::fprintf(io.err, "heap_footprint: cannot open %s\n", path);
		return 2;
	}
	const deluge::storage::DocumentFormat format = deluge::storage::formatOfPath(path);
	std::optional<deluge::storage::Value> tree = parseDocument(*text, format);
	text.reset();
	if (!tree) {
		std::fprintf(io.err, "heap_footprint: %s does not parse\n", path);
		return 1;
	}

	const HeapFree before = heap_free();
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
	Error error = openTreeFile(adapter, "song", format);
	if (error == Error::NONE) {
		error = deluge::bridge::loadSong(*song, adapter);
	}
	if (error != Error::NONE) {
		std::fprintf(io.err, "heap_footprint: %s did not load (Error %d)\n", path, static_cast<int>(error));
		return 1;
	}
	const HeapFree after = heap_free();
	const Counts counts = count(*song);
	std::fprintf(io.out,
	             "HEAP-FOOTPRINT managers=%zu unpatched_sets=%zu arp_sets=%zu unpatched_set_bytes=%zu "
	             "arp_set_bytes=%zu auto_param_bytes=%zu sram_used=%zd sdram_used=%zd\n",
	             counts.managers, counts.unpatched, counts.arp, sizeof(UnpatchedParamSet), sizeof(ArpParamSet),
	             sizeof(AutoParam), static_cast<ptrdiff_t>(before.sram) - static_cast<ptrdiff_t>(after.sram),
	             static_cast<ptrdiff_t>(before.sdram) - static_cast<ptrdiff_t>(after.sdram));
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(heap_footprint, heap_footprint_main)
