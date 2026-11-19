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

// A sample synth's heap across an oscillator switch away and back, run as the source_memory scenario: a new synth's
// first oscillator plays the card's SAMPLES/SM/TONE.WAV; it switches to a saw and back to a sample by two settings
// Edits, loading its files after each as a song load would. Each heap's free bytes and low-water mark are printed after
// the load, after the switch away and after the switch back. A measurement, not a check: it always returns 0.
//
// Gate source-memory:
//     nothing: it prints the SRAM and SDRAM heaps' free bytes and low-water marks across a sample oscillator's switch
//     to a saw and back, and whether the sample was bound at each point

#include "definitions_cxx.hpp"
#include "libdeluge/alloc.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/edits/edit.h"
#include "model/edits/set_engine_setting.h"
#include "model/fx/chain_params.h"
#include "model/instrument/instrument.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "scenarios/drivers/driver.h"
#include "serialization/value.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "support/sound_fixtures.h"

#include <cstdio>

namespace {

using deluge::scenarios::driver::Io;
using deluge::serialization::Value;

/// A song for the synth to live in.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
	return song;
}

/// Whether the synth's first oscillator has a file bound.
bool sampleBound(SoundInstrument& synth) {
	if (synth.sources[0].ranges.empty()) {
		return false;
	}
	AudioFileHolder* holder = synth.sources[0].ranges.getElement(0)->getAudioFileHolder();
	return holder != nullptr && holder->audioFile != nullptr;
}

/// One line per heap: its free bytes now and the lowest they have been.
void report(Io io, const char* point, SoundInstrument& synth) {
	const std::pair<const char*, DelugeHeap*> heaps[] = {{"sram", deluge::memory::sram_heap()},
	                                                     {"sdram", deluge::memory::sdram_heap()}};
	for (const auto& [name, heap] : heaps) {
		size_t pool = 0;
		size_t free = 0;
		size_t min_free = 0;
		(void)deluge_heap_stats(heap, &pool, &free, &min_free);
		std::fprintf(io.out, "mem %s %s free %zu min_free %zu pool %zu bound %d\n", point, name, free, min_free, pool,
		             sampleBound(synth) ? 1 : 0);
	}
}

int source_memory_main(int /*argc*/, char** /*argv*/, Io io) {
	Song* song = freshSong();
	currentSong = song;
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		std::fprintf(io.err, "source_memory: no new synth\n");
		return 0;
	}
	auto& synth = *static_cast<SoundInstrument*>(instrument);
	deluge::test::setOscType(synth, 0, OscType::SAMPLE);
	if (MultiRange* range = synth.getOrCreateFirstRange(0); range != nullptr) {
		range->getAudioFileHolder()->filePath = "SAMPLES/SM/TONE.WAV";
	}
	song->live.param_backup.backUp(synth, nullptr, &paramManager, true);
	song->addOutput(&synth);
	synth.loadAllAudioFiles(true);
	report(io, "loaded", synth);

	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(synth);
	if (!address) {
		std::fprintf(io.err, "source_memory: the synth has no slot\n");
		return 0;
	}
	deluge::edits::Context ctx{*song, song->project};
	deluge::edits::SetEngineSetting away{*address, "osc1/type", Value::makeScalar("saw")};
	(void)deluge::edits::apply(ctx, away);
	synth.loadAllAudioFiles(true);
	report(io, "away", synth);
	deluge::edits::SetEngineSetting back{*address, "osc1/type", Value::makeScalar("sample")};
	(void)deluge::edits::apply(ctx, back);
	synth.loadAllAudioFiles(true);
	report(io, "back", synth);
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(source_memory, source_memory_main)
