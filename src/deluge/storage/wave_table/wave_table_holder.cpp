/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "storage/wave_table/wave_table_holder.h"
#include "storage/audio/audio_file.h"

WaveTableHolder::WaveTableHolder() {
	audioFileType = AudioFileType::WAVETABLE;
}

WaveTableHolder::~WaveTableHolder() {
	// The base AudioFileHolder destructor is empty, so (unlike SampleHolder) nothing was releasing the reason that
	// setAudioFile() took on our WaveTable. That leaked an AudioFile reason on every wavetable load, pinning the
	// underlying Cluster so it could never be stolen - exhausting RAM after loading enough wavetable-using songs.
	if (audioFile) {
		audioFile->removeReason("E396");
	}
}
