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

uint32_t AudioInputAdapter::sample_time() const {
	return AudioEngine::audioSampleTimer;
}

bool AudioInputAdapter::stereo_input_available() const {
	return AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
}

const int32_t* AudioInputAdapter::ring_read_pos() const {
	return reinterpret_cast<const int32_t*>(AudioEngine::inputRingPos);
}

const int32_t* AudioInputAdapter::ring_end() const {
	return AudioEngine::inputRingEnd();
}

deluge::synth::LiveInputBuffer* AudioInputAdapter::buffer_for_channel(deluge::synth::InputChannel channel,
                                                                      bool mayCreate) {
	using enum deluge::synth::InputChannel;
	const OscType inputType = channel == RIGHT    ? OscType::INPUT_R
	                          : channel == STEREO ? OscType::INPUT_STEREO
	                                              : OscType::INPUT_L;
	return AudioEngine::getOrCreateLiveInputBuffer(inputType, mayCreate);
}

void AudioInputAdapter::report_hops(int32_t hopsEnded) {
	AudioEngine::numHopsEndedThisRoutineCall += hopsEnded;
}
