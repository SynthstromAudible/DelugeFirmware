/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "storage/wave_table/wave_table_band_data.h"
#include "definitions_cxx.hpp"
#include "storage/audio/audio_file_manager.h"
#include "storage/wave_table/wave_table.h"

bool WaveTableBandData::mayBeStolen(void* thingNotToStealFrom) {
	// Because stealing us would mean the WaveTable being deleted too, we
	// have to abide by this rule as found in WaveTable::mayBeStolen().
	return waveTable != nullptr && (waveTable->numReasonsToBeLoaded > 0)
	       && thingNotToStealFrom != &audioFileManager.wavetableFiles;
}

void WaveTableBandData::steal(char const* errorCode) {
#if ALPHA_OR_BETA_VERSION
	if (!waveTable || waveTable->numReasonsToBeLoaded) {
		FREEZE_WITH_ERROR("E387");
	}
#endif

	// Tell the WaveTable that we're the BandData being stolen, so it won't deallocate us - our caller will do that.
	waveTable->bandDataBeingStolen(this);

	// Delete the WaveTable from memory - deallocating all other BandDatas and everything.
	audioFileManager.releaseFile(*waveTable);
}

StealableQueue WaveTableBandData::getAppropriateQueue() {
	return StealableQueue::NO_SONG_WAVETABLE_BAND_DATA;
}
