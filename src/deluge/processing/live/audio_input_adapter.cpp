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
#include "processing/live/audio_input_adapter.h"
#include "processing/engines/audio_engine.h"

uint32_t AudioInputAdapter::sampleTime() const {
	return AudioEngine::audioSampleTimer;
}

bool AudioInputAdapter::stereoInputAvailable() const {
	return AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
}

const int32_t* AudioInputAdapter::ringReadPos() const {
	return reinterpret_cast<const int32_t*>(AudioEngine::inputRingPos);
}

const int32_t* AudioInputAdapter::ringEnd() const {
	return AudioEngine::inputRingEnd();
}

deluge::synth::LiveInputBuffer* AudioInputAdapter::bufferForChannel(OscType inputType, bool mayCreate) {
	return AudioEngine::getOrCreateLiveInputBuffer(inputType, mayCreate);
}

void AudioInputAdapter::reportHops(int32_t hopsEnded) {
	AudioEngine::numHopsEndedThisRoutineCall += hopsEnded;
}
